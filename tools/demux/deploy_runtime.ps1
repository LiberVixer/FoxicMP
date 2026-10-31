param([Parameter(Mandatory=$true)][string]$BinDirectory,[Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference='Stop'
$null=New-Item -ItemType Directory -Force $Destination
$queue=[Collections.Generic.Queue[string]]::new()
foreach($pattern in @('avformat-*.dll','avcodec-*.dll','avutil-*.dll')){
 $files=@(Get-ChildItem $BinDirectory -Filter $pattern);if($files.Count -ne 1){throw "Expected one runtime: $pattern"}
 $target=Join-Path $Destination $files[0].Name;Copy-Item $files[0].FullName $target -Force;$queue.Enqueue($target)
}
$visited=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
while($queue.Count){
 $file=$queue.Dequeue();if(!$visited.Add($file)){continue}
 $imports=& (Join-Path $BinDirectory 'objdump.exe') -p $file
 if($LASTEXITCODE){throw 'Cannot inspect demux runtime'}
 foreach($line in $imports){
  if($line -notmatch 'DLL Name:\s*(.+?)\s*$'){continue};$name=$Matches[1]
  if($name -ieq 'msys-2.0.dll' -or $name -like 'cygwin*'){throw 'Non-native runtime dependency'}
  $source=Join-Path $BinDirectory $name;$target=Join-Path $Destination $name
  if(Test-Path $source){if(!(Test-Path $target)){Copy-Item $source $target};$queue.Enqueue($target)}
  elseif($name -notlike 'api-ms-*' -and $name -notlike 'ext-ms-*' -and !(Test-Path (Join-Path "$env:SystemRoot\System32" $name))){throw "Missing runtime dependency: $name"}
 }
}
