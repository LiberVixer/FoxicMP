# Build the generated WiX 3 per-user MSI and the EXE bundle containing that MSI.
[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$ProjectDirectory,
 [string]$WixDirectory = 'C:\Program Files (x86)\WiX Toolset v3.14\bin',
 [switch]$BundleOnly
)
$ErrorActionPreference='Stop'
$ProjectDirectory=(Resolve-Path -LiteralPath $ProjectDirectory).Path
foreach($source in Get-ChildItem $ProjectDirectory -Filter '*.wxs' | Where-Object Name -notlike '*-bundle.wxs') {
 $name=$source.BaseName
 if(!$BundleOnly) {
 & "$WixDirectory\candle.exe" -nologo -arch x64 -ext WixUIExtension -out "$ProjectDirectory\$name.wixobj" $source.FullName
 if($LASTEXITCODE){throw "MSI compilation exit $LASTEXITCODE"}
 # The persistent Classic INI intentionally has a file keypath so a pre-MSI
 # profile is protected by NeverOverwrite. ICE38 disallows that combination.
 # ICE91 assumes an optional per-machine install; this MSI is strictly per-user.
 & "$WixDirectory\light.exe" -nologo -ext WixUIExtension -sice:ICE38 -sice:ICE91 -out "$ProjectDirectory\$name.msi" "$ProjectDirectory\$name.wixobj"
 if($LASTEXITCODE){throw "MSI linking exit $LASTEXITCODE"}
 }
 & "$WixDirectory\candle.exe" -nologo -ext WixBalExtension -out "$ProjectDirectory\$name-bundle.wixobj" "$ProjectDirectory\$name-bundle.wxs"
 if($LASTEXITCODE){throw "EXE compilation exit $LASTEXITCODE"}
 & "$WixDirectory\light.exe" -nologo -ext WixBalExtension -out "$ProjectDirectory\$name-setup.exe" "$ProjectDirectory\$name-bundle.wixobj"
 if($LASTEXITCODE){throw "EXE linking exit $LASTEXITCODE"}
}
