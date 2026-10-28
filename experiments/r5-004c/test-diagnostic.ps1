param([string]$OutDir=(Join-Path $PSScriptRoot 'out-verified'),[string]$ResultsDir='D:/fv/c/diagnostic-final')
$ErrorActionPreference='Stop';$OutDir=[IO.Path]::GetFullPath($OutDir)
if(Test-Path $ResultsDir){throw 'New diagnostic evidence root required'}
$caught=''
try{& "$PSScriptRoot/../r5-004b/test-one.ps1" -Application fixture -OutDir $OutDir -ResultsDir $ResultsDir -Scenario relocation-missing-reference -InputScenarioPrefix blocked: *> "$OutDir/diagnostic-console.log"}catch{$caught=$_.ToString()}
$base=Get-Content -Raw "$ResultsDir/summary.json"|ConvertFrom-Json
$events=@(Get-Content "$ResultsDir/restore/manager/controller.jsonl"|ForEach-Object {$_|ConvertFrom-Json})
$physical=Get-Content -Raw "$ResultsDir/restore/supervisor/summary.json"|ConvertFrom-Json
$file="$ResultsDir/restore/manager/RB/cleanup-diagnostic.jsonl"
$sample=if(Test-Path $file){@(Get-Content $file|Where-Object {$_}|ForEach-Object {$_|ConvertFrom-Json})}else{@()}
$native=@($events|Where-Object type -eq 'abort_native_window_cleanup')
$finished=@($events|Where-Object type -eq 'cleanup_diagnostic_finished')
$frames=@($sample|Where-Object type -eq 'frame')
$checks=[ordered]@{
 intentionalNativeFailureRetained=(-not $base.passed -and $caught -and $native.Count -eq 1 -and -not $native[0].verified -and -not $native[0].processExited)
 realOwnedWindowDestructionBlocked=(@($events|Where-Object {$_.type -eq 'cleanup_phase' -and $_.stage -eq 10}).Count -eq 1)
 diagnosticBounded=($finished.Count -eq 1 -and $finished[0].boundedMilliseconds -eq 2000)
 actualStoppedThreadAndStack=(@($sample|Where-Object {$_.type -eq 'thread_context' -and $_.result -eq 1 -and $_.tid -eq $native[0].observedTid}).Count -eq 1 -and $frames.Count -gt 0)
 noFalseRootCauseClaim=$true
 independentPhysicalCleanup=($physical.protocolPassed -and $physical.cleanupVerified)
 survivorAndFreshRecovery=(@($base.checks|Where-Object {$_.name -in @('already-restored-RA-still-runs-and-paints','restore-native-manager-completed','fixed-store-unchanged') -and $_.passed}).Count -eq 3)
}
$okay=@($checks.Values|Where-Object {-not $_}).Count -eq 0
[ordered]@{passed=$okay;synthetic=$true;checks=$checks;nativeCleanupPassed=$false;diagnosticFrames=$frames.Count;diagnosticForced=if($finished.Count){$finished[0].forced}else{$null};historicalNaturalRootCauseVerified=$false;originalFailureSummary='summary.json'}|ConvertTo-Json -Depth 6|Set-Content "$ResultsDir/diagnostic-assessment.json" -Encoding utf8
if(-not $okay){throw 'Bounded diagnostic negative failed'}
