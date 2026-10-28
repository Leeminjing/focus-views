param([string]$OutDir=(Join-Path $PSScriptRoot 'out-probe'),[string]$ResultsDir='D:/fv/c/probe1')
$ErrorActionPreference='Stop';$OutDir=[IO.Path]::GetFullPath($OutDir)
if(Test-Path $ResultsDir){throw 'New evidence directory required'};New-Item -ItemType Directory -Path $ResultsDir|Out-Null
$boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime;$rows=@()
foreach($worker in @('plain','exception','registration')){foreach($mode in @('inline','deferred')){
 $label="$mode-$worker";& "$OutDir/probe.exe" --target "$OutDir/probe-target.exe" --output "$ResultsDir/$label" --mode $mode --worker $worker *> "$ResultsDir/$label-console.log";$exit=$LASTEXITCODE
 $file="$ResultsDir/$label/summary.json";$summary=if(Test-Path $file){Get-Content -Raw $file|ConvertFrom-Json}else{$null}
 $rows+=[pscustomobject]@{case=$label;supervisorExit=$exit;result=$summary};Write-Output "$label exit=$exit completed=$($summary.cleanupCompleted)"
}}
Copy-Item "$OutDir/build.json" "$ResultsDir/build.json"
$controls=@($rows|Where-Object {$_.case -like '*plain'});$before=@($rows|Where-Object case -eq 'inline-exception')[0];$after=@($rows|Where-Object case -eq 'deferred-exception')[0]
$measured=$controls.Count -eq 2 -and @($controls|Where-Object {-not $_.result.cleanupCompleted}).Count -eq 0 -and -not $before.result.cleanupCompleted -and $after.result.cleanupCompleted
$clean=@($rows|Where-Object {$_.supervisorExit -ne 0 -or -not $_.result.jobEmpty -or -not $_.result.desktopAbsent}).Count -eq 0
[ordered]@{sameBoot=($boot -eq (Get-CimInstance Win32_OperatingSystem).LastBootUpTime);boot=$boot;synthetic=$true;historicalNaturalRootCauseVerified=$false;exceptionDependencyDifferenceVerified=$measured;allOwnedResourcesCleaned=$clean;cases=$rows}|ConvertTo-Json -Depth 7|Set-Content "$ResultsDir/summary.json" -Encoding utf8
if(-not $clean){throw 'Probe resource cleanup failed'}
