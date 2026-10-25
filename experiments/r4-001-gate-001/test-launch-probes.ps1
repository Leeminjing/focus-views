param([Parameter(Mandatory)][string]$ResultsDir,[switch]$AssessExisting)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(-not $AssessExisting){if(Test-Path $ResultsDir){throw 'New ResultsDir required'};New-Item -ItemType Directory $ResultsDir|Out-Null}
$probe=Join-Path $PSScriptRoot 'out/launch-probe.exe';$image=Join-Path $PSScriptRoot '../r3-005b-001/out/r305a.exe'
$cases=[Collections.Generic.List[object]]::new();$errors=[Collections.Generic.List[string]]::new()
try{
 foreach($mode in @('own','fixture','own-debug','fixture-debug')){
  $log=Join-Path $ResultsDir ($mode+'.jsonl');$exitCode=$null
  if(-not $AssessExisting){$arguments=@();if($mode -like 'fixture*'){$arguments+=@($image)};if($mode -like '*-debug'){$arguments+=@('--debug')}; & $probe @arguments *> $log;$exitCode=$LASTEXITCODE}
  $rows=@(Get-Content $log|ForEach-Object{$_|ConvertFrom-Json});$done=@($rows|Where-Object type -eq launch_probe_complete);$failure=@($rows|Where-Object type -eq launch_probe_failure)
  $okay=$done.Count -eq 1 -and -not $done[0].creationFailed -and $done[0].completed -eq 500 -and $failure.Count -eq 0
  if(-not $AssessExisting){$okay=$okay -and $exitCode -eq 0}
  $cases.Add([pscustomobject]@{mode=$mode;processExit=$exitCode;assessedExistingLog=[bool]$AssessExisting;passed=$okay;completion=$done;failures=$failure})
 }
}catch{$errors.Add($_.ToString());$_|Out-String|Set-Content (Join-Path $ResultsDir 'assessment-error.txt')}
finally{
 $inputs=@('test-launch-probes.ps1','launch-probe.cpp','launch-diagnostics.hpp','build.ps1','../r3/protocol.hpp')
 [ordered]@{collectedAt=(Get-Date -Format o);baseCommit=(& git -C $PSScriptRoot rev-parse HEAD);assessedExistingLogs=[bool]$AssessExisting;sources=@($inputs|ForEach-Object{@{file=$_;sha256=(Get-FileHash (Join-Path $PSScriptRoot $_)).Hash.ToLowerInvariant()}});probeSha256=(Get-FileHash $probe).Hash.ToLowerInvariant();fixtureSha256=(Get-FileHash $image).Hash.ToLowerInvariant();systemSettingsChanged=$false;pureCreationOnly=$true}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $ResultsDir 'identity.json') -Encoding utf8
 $okay=$cases.Count -eq 4 -and @($cases|Where-Object{-not $_.passed}).Count -eq 0 -and $errors.Count -eq 0
 [ordered]@{allCreationChecksPassed=$okay;cases=$cases;errors=@($errors);rootCauseConfirmed=$false;noAutomaticRetry=$true}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $ResultsDir 'summary.json') -Encoding utf8
}
if(-not $okay){throw 'Native creation probe remains red; preserve first failure and summary'}
