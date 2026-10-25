param(
 [Parameter(Mandatory=$true)][ValidateSet('classic','qt')][string]$Edition,
 [Parameter(Mandatory=$true)][string]$ProjectDirectory,
 [Parameter(Mandatory=$true)][string]$TestRoot,
 [string[]]$InstalledProfiles=@(),
 [string]$Version='0.2.0'
)
$ErrorActionPreference='Stop'
$base=(Resolve-Path -LiteralPath $ProjectDirectory).Path
$root=Join-Path $TestRoot $Edition
if(Test-Path $root){throw "Fresh test directory required: $root"}
New-Item -ItemType Directory -Path $root | Out-Null
function Snapshot-Shell {
 $rows=@()
 $rootkey='HKCU:\Software\Classes\SystemFileAssociations'
 Get-ChildItem $rootkey -Recurse -ErrorAction SilentlyContinue | Where-Object {$_.Name -match '\\shell\\FoxicMP(\\|$)'} | ForEach-Object {
   $key=Get-Item $_.PSPath
   foreach($name in $key.GetValueNames()){$rows+=$key.Name+'|'+$name+'|'+$key.GetValue($name)}
 }
 return (($rows|Sort-Object)-join "`n")
}
$before=Snapshot-Shell
$hashes=@{};foreach($path in $installedProfiles){if(Test-Path $path){$hashes[$path]=(Get-FileHash $path).Hash}}
$prefix=if($Edition -eq 'classic'){'FoxicMPClassic'}else{'FoxicMPQt'}
$exe=if($Edition -eq 'classic'){'FoxicMP64.exe'}else{'FoxicMPq.exe'}
$ini=if($Edition -eq 'classic'){'FoxicMP64.ini'}else{'FoxicMPQt.ini'}
$msi="$base\$prefix-$Version-windows-x64.msi"
$bundle="$base\$prefix-$Version-windows-x64-setup.exe"
$installer=New-Object -ComObject WindowsInstaller.Installer
$db=$installer.OpenDatabase($msi,0)
$view=$db.OpenView("SELECT `Value` FROM `Property` WHERE `Property`='ProductCode'");$view.Execute();$product=$view.Fetch().StringData(1);$view.Close()
if($installer.ProductState($product) -eq 5){throw 'Test product is already installed; do not alter it'}
$db=$null
$profile=Join-Path $root $ini
if($Edition -eq 'classic') {
 [IO.File]::WriteAllText($profile,"[Settings]`r`nContextMenuPrompted=1`r`nContextMenuEnabled=0`r`n[Audio]`r`nVolume=31`r`n",[Text.Encoding]::Unicode)
} else {[IO.File]::WriteAllText($profile,"[audio]`nvolume=31`n[integration]`nprompted=true`n",[Text.Encoding]::UTF8)}
$profileHash=(Get-FileHash $profile).Hash
function Run-Msi($arguments,$name) {
 $p=Start-Process "$env:SystemRoot\System32\msiexec.exe" -ArgumentList "$arguments /qn /norestart /l*v `"$root\$name.log`"" -Wait -PassThru
 if($p.ExitCode -notin 0,3010){throw "$name exit $($p.ExitCode)"}
 "$name exit $($p.ExitCode)" | Add-Content "$root\result.txt"
}
function Check-Profile {
 if((Get-FileHash $profile).Hash -ne $profileHash){throw 'Personal profile was modified'}
}
Run-Msi "/i `"$msi`" INSTALLDIR=`"$root`"" 'msi-install';Check-Profile
if(!(Test-Path "$root\$exe")){throw 'Player missing after MSI install'}
if($Edition -eq 'qt') {
 $env:QT_QPA_PLATFORM='offscreen'
 $probe=[Diagnostics.Process]::new()
 $probe.StartInfo.FileName="$root\$exe"
 $probe.StartInfo.Arguments='--version'
 $probe.StartInfo.UseShellExecute=$false
 $probe.StartInfo.RedirectStandardOutput=$true
 $probe.StartInfo.RedirectStandardError=$true
 $null=$probe.Start()
 if(!$probe.WaitForExit(10000)){$probe.Kill();throw 'Installed player version check timed out'}
 $reportedVersion=$probe.StandardOutput.ReadToEnd().Trim()
 if($probe.ExitCode -ne 0 -or $reportedVersion -ne "FoxicMPq $Version"){throw "Installed version failed: $reportedVersion"}
 $probe.Dispose();Check-Profile
 "installed-player-version $reportedVersion exit 0"|Add-Content "$root\result.txt"
}
Run-Msi "/fa `"$msi`"" 'msi-repair';Check-Profile
Run-Msi "/x $product" 'msi-uninstall';Check-Profile
if(Test-Path "$root\$exe"){throw 'MSI uninstall did not remove player'}
$p=Start-Process $bundle -ArgumentList "/quiet /norestart InstallFolder=`"$root`" /log `"$root\exe-install.log`"" -Wait -PassThru
if($p.ExitCode -notin 0,3010){throw "EXE install exit $($p.ExitCode)"}
"exe-install exit $($p.ExitCode)"|Add-Content "$root\result.txt";Check-Profile
if(!(Test-Path "$root\$exe")){throw 'Player missing after EXE install'}
$p=Start-Process $bundle -ArgumentList "/uninstall /quiet /norestart /log `"$root\exe-uninstall.log`"" -Wait -PassThru
if($p.ExitCode -notin 0,3010){throw "EXE uninstall exit $($p.ExitCode)"}
"exe-uninstall exit $($p.ExitCode)"|Add-Content "$root\result.txt";Check-Profile
if(Test-Path "$root\$exe"){throw 'EXE uninstall did not remove player'}
if((Snapshot-Shell) -ne $before){throw 'Installed Classic shell registration changed'}
foreach($path in $hashes.Keys){if((Get-FileHash $path).Hash -ne $hashes[$path]){throw "Installed profile changed: $path"}}
'PASS: MSI install/repair/uninstall and EXE install/uninstall; profile and installed applications preserved'|Add-Content "$root\result.txt"
Get-Content "$root\result.txt"
