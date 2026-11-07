[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$ToolchainDirectory,
  [Parameter(Mandatory=$true)][string]$BuildDirectory,
  [int]$Jobs = 8,
  [string]$QtCoreRuntimeDirectory
)
$ErrorActionPreference = 'Stop'
# CMake persists compiler paths in generated CMake files. Backslashes such as
# \ucrt64 are interpreted as escape sequences there; use forward slashes.
$toolchain = (Resolve-Path -LiteralPath $ToolchainDirectory).Path.Replace('\', '/')
$prefix = "$toolchain/ucrt64"
$bin = "$prefix/bin"
$env:PATH = "$bin;$env:PATH"
if (!$QtCoreRuntimeDirectory) { $QtCoreRuntimeDirectory = Join-Path $BuildDirectory 'qtcore-runtime' }
$coreMetadataPath = Join-Path $QtCoreRuntimeDirectory 'qtcore-longpaths-build.json'
if (!(Test-Path $coreMetadataPath)) { throw 'Prepare the private QtCore runtime first; see README.ru.md (long profile paths)' }
$coreMetadata = Get-Content $coreMetadataPath -Raw | ConvertFrom-Json
$corePin = Get-Content (Join-Path $PSScriptRoot '../../../tools/runtime/qt-6.12.0-long-lock-paths.json') -Raw | ConvertFrom-Json
$coreLibrary = Join-Path $QtCoreRuntimeDirectory 'Qt6Core.dll'
if ($coreMetadata.architecture -ne 'x86_64' -or $coreMetadata.qtVersion -ne $corePin.version -or
    $coreMetadata.localPatchSha256 -ne $corePin.localPatchSha256 -or
    !$coreMetadata.allStockExportsPreserved -or !$coreMetadata.allFeatureFlagsEqual -or
    (Get-FileHash $coreLibrary -Algorithm SHA256).Hash.ToLower() -ne $coreMetadata.librarySha256 -or
    (Get-FileHash (Join-Path $bin 'Qt6Core.dll') -Algorithm SHA256).Hash.ToLower() -ne $coreMetadata.stockLibrarySha256) {
  throw 'Private QtCore runtime does not match this toolchain'
}
$source = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path.Replace('\', '/')
& "$bin/cmake.exe" -S $source -B $BuildDirectory -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON "-DCMAKE_PREFIX_PATH=$prefix" "-DCMAKE_CXX_COMPILER=$bin/g++.exe" "-DCMAKE_RC_COMPILER=$bin/windres.exe"
if ($LASTEXITCODE) { throw 'CMake configure failed' }
& "$bin/cmake.exe" --build $BuildDirectory -j $Jobs
if ($LASTEXITCODE) { throw 'Build failed' }

# Ship a private runtime with detached-thread TSD cleanup. Never replace MSYS2's DLL.
$runtime = Join-Path $BuildDirectory 'winpthreads-runtime'
& "$bin/python3.exe" (Join-Path $PSScriptRoot 'build_winpthread.py') --toolchain $toolchain --work $runtime
if ($LASTEXITCODE) { throw 'Private winpthreads build failed' }
Copy-Item (Join-Path $runtime 'libwinpthread-1.dll') $BuildDirectory -Force
Copy-Item (Join-Path $runtime 'winpthread-build.json') $BuildDirectory -Force

# Tests and the player use the same corrected Core without touching MSYS2.
Copy-Item $coreLibrary (Join-Path $BuildDirectory 'Qt6Core.dll') -Force
Copy-Item $coreMetadataPath $BuildDirectory -Force

Copy-Item (Join-Path $QtCoreRuntimeDirectory 'qtcore-licenses') $BuildDirectory -Recurse -Force
