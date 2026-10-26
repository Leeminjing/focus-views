param([string]$OutDir=(Join-Path $PSScriptRoot 'out'),[Parameter(Mandatory)][string]$ResultsDir)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '../r4-001-gate-001/lifetimes.ps1')
$OutDir=[IO.Path]::GetFullPath($OutDir);$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(Test-Path $ResultsDir){throw 'Preserve old probe evidence; choose a new directory'}
New-Item -ItemType Directory $ResultsDir|Out-Null
$checks=[Collections.Generic.List[object]]::new();$errors=[Collections.Generic.List[object]]::new()
function Check([string]$Name,[bool]$Okay){$checks.Add([pscustomobject]@{name=$Name;passed=$Okay});Write-Output "$Name=$Okay"}
[ordered]@{time=(Get-Date -Format o);scriptSha256=(Get-FileHash $PSCommandPath).Hash.ToLowerInvariant();baseCommit=(& git -C $PSScriptRoot rev-parse HEAD);managerSha256=(Get-FileHash (Join-Path $OutDir 'manager.exe')).Hash.ToLowerInvariant();applicationSha256=(Get-FileHash (Join-Path $OutDir 'second.exe')).Hash.ToLowerInvariant();kernelSha256=(Get-FileHash (Join-Path $OutDir 'kernel.lib')).Hash.ToLowerInvariant()}|ConvertTo-Json|Set-Content (Join-Path $ResultsDir 'identity.json')
foreach($scenario in @('queued-paint','queued-unknown','queued-quit')){
 try{
  $dir=Join-Path $ResultsDir $scenario
  & (Join-Path $OutDir 'manager.exe') --client (Join-Path $OutDir 'recorder-client.dll') --fixture (Join-Path $OutDir 'second.exe') --output $dir --scenario $scenario *> (Join-Path $ResultsDir "$scenario-console.log")
  $exit=$LASTEXITCODE
  $rows=@(Get-Content (Join-Path $dir 'controller.jsonl')|ForEach-Object{$_|ConvertFrom-Json});$events=@(Get-Content (Join-Path $dir 'source/events.jsonl')|ForEach-Object{$_|ConvertFrom-Json})
  $done=@($rows|Where-Object type -eq 'experiment_complete')[0]
  if($scenario -eq 'queued-paint'){
   $guards=@($events|Where-Object event -eq 'checkpoint_guard');$drained=@($events|Where-Object event -eq 'checkpoint_paint_drained');$final=@($events|Where-Object event -eq 'checkpoint_guard_final')
   Check 'paint.reproduced-owned-late-message' (@($guards|Where-Object{$_.queued -and $_.message -eq 15 -and $_.frozen -eq 1 -and $_.resourcesReady -eq 1 -and $_.paintDepth -eq 0 -and $_.capture -eq 0}).Count -ge 1)
   Check 'paint.normal-dispatch-then-empty-queue' ($drained.Count -ge 1 -and @($drained|Where-Object message -ne 15).Count -eq 0 -and $final.Count -eq 1 -and -not $final[0].queued -and $final[0].inputClosed -eq 1)
   Check 'paint.same-attempt-completes-no-business-replay' ($exit -eq 0 -and $done.evidencePassed -and @($rows|Where-Object type -eq 'view_published').Count -eq 5 -and @($rows|Where-Object{$_.type -eq 'assertion' -and -not $_.passed}).Count -eq 0)
  }else{
   $rejection=@($events|Where-Object event -eq $(if($scenario -eq 'queued-unknown'){'checkpoint_queue_rejected'}else{'checkpoint_removed_rejected'}))
   Check "$scenario.actual-unsupported-message" ($rejection.Count -eq 1 -and $rejection[0].message -eq $(if($scenario -eq 'queued-unknown'){0x8000+499}else{0x12}))
   Check "$scenario.rejects-before-Snapshot" ($exit -eq 2 -and -not $done.evidencePassed -and @($rows|Where-Object{$_.type -eq 'halt' -and $_.phase -eq 'rejected' -and $_.error -eq 170}).Count -eq 1 -and -not(Test-Path (Join-Path $dir 'S0-data.bin')) -and @($rows|Where-Object type -eq 'view_published').Count -eq 0)
  }
  $audit=Test-OwnedLifetimes $rows;$audit|ConvertTo-Json -Depth 6|Set-Content (Join-Path $dir 'lifetime-audit.json');Check "$scenario.resources-cleaned" $audit.passed
 }catch{$errors.Add([pscustomobject]@{scenario=$scenario;error=$_.ToString()});Check "$scenario.evidence" $false}
}
$okay=$errors.Count -eq 0 -and @($checks|Where-Object{-not $_.passed}).Count -eq 0
[ordered]@{allEvidenceChecksPassed=$okay;historicalExactMessageUnknown=$true;checks=$checks;errors=$errors}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $ResultsDir 'summary.json')
if(-not $okay){throw 'Checkpoint guard proof failed'}
Write-Output 'CHECKPOINT_GUARD_PASSED'
