# Native UCRT64 runtime packaging; no MSYS runtime or developer profile is shipped.
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$BuildDirectory,
  [Parameter(Mandatory=$true)][string]$ToolchainDirectory,
  [Parameter(Mandatory=$true)][string]$Destination,
  [string]$YtDlpExecutable,
  [string]$YtDlpLicense
)
$ErrorActionPreference = 'Stop'
$bin = Join-Path $ToolchainDirectory 'ucrt64\bin'
$env:PATH = "$bin;$env:PATH"
if (Test-Path $Destination) { throw "Use a new staging directory: $Destination" }
& "$bin\cmake.exe" --install $BuildDirectory --prefix $Destination
if ($LASTEXITCODE) { throw 'CMake install failed' }
# Windows PowerShell 5 treats native stderr warnings as terminating errors under
# Stop. Preserve the tool's actual exit code; Qt may warn about unused D3D12.
$ErrorActionPreference = 'Continue'
& "$bin\windeployqt6.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw --openssl-root (Join-Path $ToolchainDirectory 'ucrt64') $Destination\FoxicMPq.exe 2>&1 | ForEach-Object { $_.ToString() }
$deployCode = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($deployCode) { throw 'Qt runtime deployment failed' }
# MSYS2 uses share/qt6/plugins at build time; the portable package is flat.
[IO.File]::WriteAllText((Join-Path $Destination 'qt.conf'), "[Paths]`nPrefix=.`nPlugins=.`nLibraries=.`nTranslations=translations`n")
Copy-Item (Join-Path $ToolchainDirectory 'ucrt64\share\qt6\plugins\platforms\qoffscreen.dll') (Join-Path $Destination 'platforms')
# Helpers must be found next to the player, independent of the user's PATH.
Copy-Item "$bin\ffmpeg.exe" $Destination
if ($YtDlpExecutable) { Copy-Item $YtDlpExecutable (Join-Path $Destination 'yt-dlp.exe') }
$queue = [System.Collections.Generic.Queue[string]]::new()
Get-ChildItem $Destination -Recurse -File | Where-Object { $_.Extension -in '.exe','.dll' } | ForEach-Object { $queue.Enqueue($_.FullName) }
$visited = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
while ($queue.Count) {
  $file = $queue.Dequeue()
  if (!$visited.Add($file)) { continue }
  $imports = & "$bin\objdump.exe" -p $file
  if ($LASTEXITCODE) { throw "Cannot inspect $file" }
  foreach ($line in $imports) {
    if ($line -notmatch 'DLL Name:\s*(.+?)\s*$') { continue }
    $name = $Matches[1]
    if ($name -ieq 'msys-2.0.dll' -or $name -like 'cygwin*') { throw "Non-native runtime dependency: $name" }
    if (Test-Path (Join-Path $Destination $name)) { continue }
    $source = Join-Path $bin $name
    if (Test-Path $source) {
      $target = Join-Path $Destination $name
      Copy-Item $source $target; $queue.Enqueue($target)
    } elseif ($name -notlike 'api-ms-*' -and $name -notlike 'ext-ms-*' -and
              !(Test-Path (Join-Path $env:SystemRoot "System32\$name"))) {
      throw "Unresolved runtime dependency $name imported by $file"
    }
  }
}
$licenses = Join-Path $ToolchainDirectory 'ucrt64\share\licenses'
if (Test-Path $licenses) { Copy-Item $licenses (Join-Path $Destination 'licenses') -Recurse }
if ($YtDlpExecutable) {
  if (!$YtDlpLicense) { $YtDlpLicense = Join-Path (Split-Path $YtDlpExecutable) 'yt-dlp-licenses.txt' }
  if (!(Test-Path $YtDlpLicense)) { throw 'Supply YtDlpLicense with the bundled third-party licenses' }
  $null = New-Item -ItemType Directory -Force (Join-Path $Destination 'licenses')
  Copy-Item $YtDlpLicense (Join-Path $Destination 'licenses\yt-dlp.txt')
}
$metadata = Join-Path (Split-Path $ToolchainDirectory) 'qt-dependencies.json'
if (Test-Path $metadata) { Copy-Item $metadata $Destination }
# Test the portable folder with only Windows' standard PATH, not the build tools.
$originalPath = $env:PATH
try {
  $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
  $env:QT_QPA_PLATFORM = 'offscreen'
  $probe = [System.Diagnostics.Process]::new()
  $probe.StartInfo.FileName = Join-Path $Destination 'FoxicMPq.exe'
  $probe.StartInfo.Arguments = '--version'
  $probe.StartInfo.UseShellExecute = $false
  $probe.StartInfo.RedirectStandardOutput = $true
  $probe.StartInfo.RedirectStandardError = $true
  $probe.StartInfo.CreateNoWindow = $true
  $null = $probe.Start()
  if (!$probe.WaitForExit(10000)) { $probe.Kill(); throw 'Standalone runtime check timed out' }
  $version = $probe.StandardOutput.ReadToEnd()
  if ($probe.ExitCode -or $version -notmatch 'FoxicMPq \d+\.\d+\.\d+') {
    throw ('Standalone runtime check failed: ' + $probe.StandardError.ReadToEnd())
  }
  Write-Output $version.Trim()
  $probe.Dispose()
} finally { $env:PATH = $originalPath; Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue }
