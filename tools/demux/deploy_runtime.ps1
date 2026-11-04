param([Parameter(Mandatory=$true)][string]$BinDirectory,[Parameter(Mandatory=$true)][string]$Destination,[string]$WorkerDirectory)
$ErrorActionPreference='Stop'
if($WorkerDirectory -and (Test-Path $Destination)){throw 'Use a new runtime destination; installed profiles are never updated here'}
$null=New-Item -ItemType Directory -Force $Destination
if($WorkerDirectory){
 foreach($name in @('foxicmp-demux-worker.exe','foxicmp-remux-stream-worker.exe')){
  $target=Join-Path $Destination $name
  if(Test-Path $target){throw "Worker already exists; use a fresh runtime destination: $target"}
  Copy-Item (Join-Path $WorkerDirectory $name) $target
 }
}
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
if(Test-Path (Join-Path $Destination 'foxicmp-remux-stream-worker.exe')){
 $info=& (Join-Path $Destination 'foxicmp-remux-stream-worker.exe') --info
 if($LASTEXITCODE){throw 'Continuous worker FFmpeg ABI validation failed'}
 $meta=$info | ConvertFrom-Json
 if($meta.workerProtocol -ne 1 -or $meta.runtimeVerified -ne $true){throw 'Unexpected continuous worker protocol'}
 $info | Set-Content (Join-Path $Destination 'continuous-remux-worker.json')
}
if($WorkerDirectory){
 $licenses=Join-Path (Split-Path $BinDirectory) 'share/licenses'
 if(!(Test-Path $licenses)){throw 'Runtime distribution requires third-party licenses'}
 Copy-Item $licenses (Join-Path $Destination 'licenses') -Recurse
 $files=[ordered]@{}
 Get-ChildItem $Destination -File | Sort-Object Name | ForEach-Object {$files[$_.Name]=(Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower()}
 [ordered]@{schemaVersion=1;workerProtocol=1;files=$files} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $Destination 'continuous-runtime-manifest.json')
}
