# Private probe output only; never point this at an installed player directory.
param([Parameter(Mandatory=$true)][string]$Directory)
$ErrorActionPreference='Stop'
$folder=(Resolve-Path -LiteralPath $Directory).Path
foreach($name in @('probe','graph')) {
    if(-not (Test-Path -LiteralPath (Join-Path $folder "$name.exe") -PathType Leaf)) {
        throw "Missing private $name.exe"
    }
}
foreach($name in @('probe','graph')) {
    [IO.File]::WriteAllText((Join-Path $folder "$name.ini"),
        "[Settings]`r`nContextMenuPrompted=1`r`nContextMenuEnabled=0`r`n",
        [Text.Encoding]::Unicode)
}
