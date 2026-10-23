param(
    [Parameter(Mandatory=$true)][string]$SourceExe,
    [Parameter(Mandatory=$true)][string]$Corpus,
    [Parameter(Mandatory=$true)][string]$ReportDirectory,
    [int]$CaseLimit=0,
    [switch]$HeadlessDecode,
    [string]$RealSource
)
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force -Path $ReportDirectory | Out-Null
$clone=Join-Path $ReportDirectory ('player-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $clone | Out-Null
$sourceDirectory=Split-Path $SourceExe
$sourceIni=[IO.Path]::ChangeExtension($SourceExe,'.ini')
$originalHash=(Get-FileHash -Algorithm SHA256 -LiteralPath $sourceIni).Hash
$exe=Join-Path $clone 'FoxicMP64.exe'
Copy-Item -LiteralPath $SourceExe -Destination $exe
foreach($name in @('d3dcompiler_47.dll','d3dx9_43.dll')) {
    Copy-Item -LiteralPath (Join-Path $sourceDirectory $name) -Destination $clone
}
# A new portable profile disables integration; never copy/write the live profile.
@'
[Settings]
ContextMenuPrompted=1
ContextMenuEnabled=0
MultipleInstances=1
KeepHistory=0
RememberFilePos=0
RememberWindowPos=0
[Audio]
Volume=0
[WebServer]
EnableWebServer=1
LocalhostOnly=1
EnablePreview=1
WebRoot=*./webroot
CGI=
'@ | Set-Content -LiteralPath (Join-Path $clone 'FoxicMP64.ini') -Encoding Unicode
if($HeadlessDecode) {
    Add-Content -LiteralPath (Join-Path $clone 'FoxicMP64.ini') -Encoding Unicode -Value "`r`n[Video]`r`nVideoRenderer=11`r`n[Audio]`r`nAudioRenderer=Null Audio Renderer (Uncompressed)`r`nVolume=0"
}
$manifest=Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $Corpus 'manifest.json') | ConvertFrom-Json
$cases=@($manifest.cases)
if($RealSource) {
    $real=Get-Item -LiteralPath $RealSource
    $realModified=$real.LastWriteTimeUtc
    $cases=@([pscustomobject]@{file=$real.Name;expected='play';video=1;bytes=$real.Length;sha256=$null})
}
if($CaseLimit -gt 0) {$cases=@($cases | Select-Object -First $CaseLimit)}
$listener=New-Object Net.Sockets.TcpListener([Net.IPAddress]::Loopback,0)
$listener.Start(); $port=$listener.LocalEndpoint.Port; $listener.Stop()
$results=New-Object Collections.Generic.List[Object]
$process=$null
function Get-Status {
    $response=Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$port/status.html" -TimeoutSec 2
    $body=[Text.Encoding]::UTF8.GetString($response.RawContentStream.ToArray())
    if($body -match "^OnStatus\('(?:[^'\\]|\\.)*', '(?:[^'\\]|\\.)*', (\d+), '[^']*', (\d+),") {
        return @{positionMs=[int]$Matches[1];durationMs=[int]$Matches[2];raw=$body}
    }
    throw "Unrecognized Classic status: $body"
}
try {
    $caseIndex=0
    foreach($case in $cases) {
        $path=Join-Path $Corpus $case.file
        if($RealSource) {$path=$RealSource}
        elseif((Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLower() -ne $case.sha256){throw "Fixture hash mismatch: $path"}
        $caseIndex++
        $testPath=Join-Path $clone ('sample-'+$caseIndex+[IO.Path]::GetExtension($path))
        if($RealSource) {$testPath=$path} else {Copy-Item -LiteralPath $path -Destination $testPath}
        # One isolated process per case prevents asynchronous close/open from
        # reporting the preceding file as the current compatibility result.
        if($process -and -not $process.HasExited) {
            try {Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$port/command.html?wm_command=816" -TimeoutSec 2 | Out-Null} catch {}
            if(-not $process.WaitForExit(3000)){Stop-Process -Id $process.Id -Force}
        }
        $arguments=@('/new','/minimized','/play','/webport',"$port",('"'+$testPath+'"'))
        $process=Start-Process -FilePath $exe -WorkingDirectory $clone -ArgumentList $arguments -PassThru
        $startupDeadline=[DateTime]::UtcNow.AddSeconds(12)
        do {
            try {$initial=Get-Status;break} catch {if([DateTime]::UtcNow -gt $startupDeadline){throw}; Start-Sleep -Milliseconds 100}
        } while($true)
        $deadline=[DateTime]::UtcNow.AddSeconds(4)
        $observed=$false; $status=$null
        do {
            $status=Get-Status
            if($status.raw.Contains($testPath) -and $status.durationMs -gt 2000 -and $status.positionMs -gt 150){$observed=$true;break}
            Start-Sleep -Milliseconds 80
        } while([DateTime]::UtcNow -lt $deadline)
        $snapshotColored=$null; $snapshotBytes=0
        if($observed -and $case.video -gt 0 -and -not $HeadlessDecode) {
            $snapshot=Join-Path $ReportDirectory ($case.file+'.jpg')
            Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$port/snapshot.jpg" -OutFile $snapshot -TimeoutSec 3
            $snapshotBytes=(Get-Item -LiteralPath $snapshot).Length
            $image=[Drawing.Bitmap]::FromFile($snapshot)
            try {
                $colored=0
                for($y=0;$y -lt $image.Height;$y+=12) {for($x=0;$x -lt $image.Width;$x+=12) {
                    $c=$image.GetPixel($x,$y);$v=@([int]$c.R,[int]$c.G,[int]$c.B) | Measure-Object -Minimum -Maximum
                    if(($v.Maximum-$v.Minimum) -gt 35){$colored++}
                }}
                $snapshotColored=$colored -gt 20
            } finally {$image.Dispose()}
        }
        $results.Add([ordered]@{file=$case.file;sha256=$case.sha256;asciiAlias=(-not [bool]$RealSource);expected=$case.expected;playbackObserved=$observed;
            statusRaw=$status.raw;positionMs=$status.positionMs;durationMs=$status.durationMs;snapshotColored=$snapshotColored;snapshotBytes=$snapshotBytes;
            passed=($case.expected -eq 'play' -and $observed -and ($HeadlessDecode -or $case.video -eq 0 -or $snapshotColored)) -or ($case.expected -eq 'reject' -and -not $observed)})
        [Console]::WriteLine($case.file+': '+$observed)
    }
} finally {
    if($process -and -not $process.HasExited) {
        try {Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$port/command.html?wm_command=816" -TimeoutSec 2 | Out-Null} catch {}
        if(-not $process.WaitForExit(3000)){Stop-Process -Id $process.Id -Force}
    }
    $report=[ordered]@{schema=1;sourceVersion=(Get-Item -LiteralPath $SourceExe).VersionInfo.FileVersion;
        sourceExeSha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $SourceExe).Hash.ToLower();
        headlessUncompressedRenderers=[bool]$HeadlessDecode;sourceProfileUnchanged=((Get-FileHash -Algorithm SHA256 -LiteralPath $sourceIni).Hash -eq $originalHash);
        clone=$clone;results=@($results.ToArray());
        realSourceUnchanged=($(if($RealSource){((Get-Item -LiteralPath $RealSource).LastWriteTimeUtc -eq $realModified) -and ((Get-Item -LiteralPath $RealSource).Length -eq $real.Length)}else{$null}));limits=@('Synthetic 3-second corpus; opening/progress and video snapshot checked',
            'Audio output, track switching and seek not measured on Classic; no physical A/V validation')}
    $report | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $ReportDirectory 'report.json')
}
