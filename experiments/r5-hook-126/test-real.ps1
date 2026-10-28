param([Parameter(Mandatory)][string]$InputOutDir,[Parameter(Mandatory)][string]$CetOutDir,[Parameter(Mandatory)][string]$ControlOutDir,[Parameter(Mandatory)][string]$Profile,[Parameter(Mandatory)][string]$ResultsDir,[switch]$BlockedCleanup,[string]$FollowupOnlyEvidence='',[string]$QueryFailureOutDir='')
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(Test-Path $ResultsDir){throw 'New evidence directory required; do not retry a failed operation'};New-Item -ItemType Directory $ResultsDir|Out-Null
foreach($dir in @($InputOutDir,$CetOutDir)){foreach($file in @('build.log','loadconfig.txt','build.json','r305a.exe','runtime304.dll')){if(-not(Test-Path (Join-Path $dir $file))){[ordered]@{stage='preflight';path=(Join-Path $dir $file);applicationsStarted=$false;serialVerificationPassed=$false}|ConvertTo-Json|Set-Content "$ResultsDir/summary.json";throw 'Prebuilt suite input missing; no native operation started'}}}
. "$PSScriptRoot/verification.ps1";Assert-InputDiagnosticBuild $InputOutDir;Assert-InputDiagnosticBuild $CetOutDir
. "$PSScriptRoot/../r5-004c-wait/followup.ps1"
if($FollowupOnlyEvidence){
 $caught='';try{& "$PSScriptRoot/../r5-004c-wait/run-failure-followup.ps1" -OriginRoot $FollowupOnlyEvidence -OutDir $ControlOutDir -Profile $Profile -ResultsDir "$ResultsDir/followup" *> "$ResultsDir/followup-console.log"}catch{$caught=$_.ToString()}
 $a=Get-Content "$ResultsDir/followup/assessment.json" -Raw|ConvertFrom-Json
 [ordered]@{serialVerificationPassed=$a.diagnosticChainPassed;followupOnly=$true;assessment=$a;error=$caught}|ConvertTo-Json -Depth 18|Set-Content "$ResultsDir/summary.json" -Encoding utf8
 if(-not $a.diagnosticChainPassed){throw 'Follow-up-only entry refused; no fallback or Snapshot retry'};return
}
$boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToString('o');$stages=@();$followupAttempted=$false;$directAttempted=$false
$dirs=@($InputOutDir,$CetOutDir,$ControlOutDir);if($QueryFailureOutDir){Assert-InputDiagnosticBuild $QueryFailureOutDir;$dirs+=$QueryFailureOutDir}
$frozen=@(foreach($dir in $dirs){Get-ChildItem $dir -File|Where-Object Extension -in @('.exe','.dll','.lib','.json')|ForEach-Object{[ordered]@{path=$_.FullName;sha256=(Get-FileHash $_.FullName).Hash}}})
$frozen+=[ordered]@{path=[IO.Path]::GetFullPath($Profile);sha256=(Get-FileHash $Profile).Hash}
$frozen|ConvertTo-Json -Depth 4|Set-Content "$ResultsDir/frozen-inputs.json" -Encoding utf8
function Stage([string]$Name,[scriptblock]$Body,[string]$Summary,[string]$Flag){
 $errorText='';try{& $Body *> "$ResultsDir/$Name-console.log"}catch{$errorText=$_.ToString()}
 $s=if(Test-Path $Summary){Get-Content $Summary -Raw|ConvertFrom-Json}else{$null}
 $passed=$null -ne $s -and $s.PSObject.Properties.Name -contains $Flag -and $s.$Flag -eq $true -and -not $errorText
 $script:stages+= [ordered]@{name=$Name;passed=$passed;error=$errorText;summaryPath=$Summary;summary=$s}
 # Write failures before any later diagnostic operation; never replace originals.
 $script:stages|ConvertTo-Json -Depth 15|Set-Content "$ResultsDir/stages.json" -Encoding utf8
}
if($BlockedCleanup){
 Stage 'artificial-cleanup-handler' {& "$PSScriptRoot/../r5-004c/test-diagnostic.ps1" -OutDir $ControlOutDir -ResultsDir "$ResultsDir/cleanup"} "$ResultsDir/cleanup/diagnostic-assessment.json" 'passed'
}else{Stage 'cleanup-minimal' {& "$PSScriptRoot/../r5-004b/test-one.ps1" -Application recorder -Scenario relocation-bad-offset -InputScenarioPrefix 'inline:' -OutDir $ControlOutDir -ResultsDir "$ResultsDir/cleanup"} "$ResultsDir/cleanup/summary.json" 'passed'}
$origin=Get-Content "$ResultsDir/cleanup/summary.json" -Raw|ConvertFrom-Json
[ordered]@{boot=$boot;synthetic=[bool]$BlockedCleanup;originalSummarySha256=(Get-FileHash "$ResultsDir/cleanup/summary.json").Hash}|ConvertTo-Json|Set-Content "$ResultsDir/cleanup/operation-identity.json" -Encoding utf8
$failures=@(Get-ChildItem "$ResultsDir/cleanup" -Filter controller.jsonl -File -Recurse|ForEach-Object{Get-Content $_.FullName|ForEach-Object{$_|ConvertFrom-Json}|Where-Object{$_.type -eq 'abort_native_window_cleanup' -and -not $_.verified}})
if($failures.Count){
 $followupAttempted=$true
 Stage 'failure-followup' {& "$PSScriptRoot/../r5-004c-wait/run-failure-followup.ps1" -OriginRoot "$ResultsDir/cleanup" -OutDir $ControlOutDir -Profile $Profile -ResultsDir "$ResultsDir/failure-followup"} "$ResultsDir/failure-followup/assessment.json" 'diagnosticChainPassed'
 $directAttempted=$stages[-1].summary.directWindowExecuted
}
if($BlockedCleanup){Stage 'missing-proof-refusal' {& "$PSScriptRoot/test-missing-proof.ps1" -OriginRoot "$ResultsDir/cleanup" -InputOutDir $InputOutDir -CetOutDir $CetOutDir -ControlOutDir $ControlOutDir -Profile $Profile -ResultsDir "$ResultsDir/missing-proof"} "$ResultsDir/missing-proof/summary.json" 'passed'}
if($QueryFailureOutDir){Stage 'input-failure-paths' {& "$PSScriptRoot/test-input-failures.ps1" -InputOutDir $InputOutDir -QueryFailureOutDir $QueryFailureOutDir -ResultsDir "$ResultsDir/input-failures"} "$ResultsDir/input-failures/summary.json" 'passed'}
Stage 'legacy-minimal' {
 & "$InputOutDir/r305a.exe" --role controller --scenario positive --output "$ResultsDir/legacy-minimal"; $exit=$LASTEXITCODE
 $rows=@(Get-Content "$ResultsDir/legacy-minimal/controller.jsonl"|ForEach-Object{$_|ConvertFrom-Json});$done=@($rows|Where-Object type -eq 'experiment_complete')
 . "$PSScriptRoot/../r4-001-gate-001/lifetimes.ps1";$audit=Test-OwnedLifetimes $rows
 [ordered]@{passed=($exit -eq 0 -and $done.Count -eq 1 -and $done[0].failedAssertions -eq 0 -and $audit.passed);processExit=$exit;complete=$done;lifetimeAudit=$audit}|ConvertTo-Json -Depth 9|Set-Content "$ResultsDir/legacy-minimal/summary.json" -Encoding utf8
} "$ResultsDir/legacy-minimal/summary.json" 'passed'
Stage 'b001-suite' {& "$PSScriptRoot/../r3-005b-001/test.ps1" -InstallDiagnostics -SkipRegression -OutDir $InputOutDir -ResultsDir "$ResultsDir/b001"} "$ResultsDir/b001/summary.json" 'allEvidenceChecksPassed'
Stage 'cet002-suite' {& "$PSScriptRoot/../r3-005a-cet-002/test.ps1" -InstallDiagnostics -OutDir $CetOutDir -ResultsDir "$ResultsDir/cet002"} "$ResultsDir/cet002/summary.json" 'allEvidenceChecksPassed'
$sameBuild=@($frozen|Where-Object{(Get-FileHash $_.path).Hash -ne $_.sha256}).Count -eq 0;$sameBoot=$boot -ceq (Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToString('o')
[ordered]@{serialVerificationPassed=($sameBuild -and $sameBoot -and @($stages|Where-Object{-not $_.passed}).Count -eq 0);sameBuild=$sameBuild;sameBoot=$sameBoot;boot=$boot;stages=$stages;syntheticCleanupFault=[bool]$BlockedCleanup;originalCleanupOperationPassed=$origin.passed;naturalCleanupFailureObserved=($failures.Count -gt 0 -and -not $BlockedCleanup);cleanupControlPairExecuted=$followupAttempted;directWindowExecuted=$directAttempted;fullNestedRegressionExecuted=$false;cleanupCausalFixVerified=$false;fullR5004Passed=$false;gateClosed=$false;manualHostTest='not_executed'}|ConvertTo-Json -Depth 18|Set-Content "$ResultsDir/summary.json" -Encoding utf8
if(-not $sameBuild -or -not $sameBoot -or @($stages|Where-Object{-not $_.passed}).Count){throw 'Serial diagnostic verification contains failures; original evidence retained'}
