param([string]$OutDir=(Join-Path $PSScriptRoot 'out'),[string]$ResultsDir='D:/fv/c/r1',[switch]$SkipRegression,[Parameter(Mandatory)][string]$FrozenInputs,[Parameter(Mandatory)][string]$ControlOutDir,[Parameter(Mandatory)][string]$Profile,[Parameter(Mandatory)][string]$InputOutDir,[Parameter(Mandatory)][string]$CetOutDir,[Parameter(Mandatory)][string]$QueryFailureOutDir)
$ErrorActionPreference='Stop';$OutDir=[IO.Path]::GetFullPath($OutDir);$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(Test-Path $ResultsDir){throw 'New results root required'};New-Item -ItemType Directory -Path $ResultsDir|Out-Null
$boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime;$results=[Collections.Generic.List[object]]::new()
# Prebuilt inputs are immutable for the entire combination; no build/copy phase
# is allowed after this point, including the nested regression input profile.
. "$PSScriptRoot/frozen-inputs.ps1"
$frozen=Get-Content $FrozenInputs -Raw|ConvertFrom-Json
Copy-Item $FrozenInputs "$ResultsDir/frozen-inputs.json"
$requiredDirs=@($OutDir,$ControlOutDir,$InputOutDir,$CetOutDir,$QueryFailureOutDir)
if(-not $SkipRegression){$requiredDirs+=@('r3-005a-cet-002','r3-005b-001','r3-005c-001','r4-001','r4-002a','r4-003-001','r5-001a','r5-001b','r5-002','r5-003')|ForEach-Object{"$PSScriptRoot/../$_/out"}}
$declared=@($frozen.buildDirs|ForEach-Object{[IO.Path]::GetFullPath($_)})
$unlisted=@($requiredDirs|Where-Object{[IO.Path]::GetFullPath($_) -notin $declared})
if($unlisted.Count -or [IO.Path]::GetFullPath($Profile) -ne [IO.Path]::GetFullPath($frozen.profile)){throw 'Actual build/profile arguments are absent from frozen inventory'}
$initial=Test-CombinationInputs $frozen.inputs
if(-not $initial.passed){$initial|ConvertTo-Json -Depth 5|Set-Content "$ResultsDir/preflight-failure.json";throw 'Frozen input mismatch before any native test'}
$binding=Test-ProfileImageBinding $Profile "$ControlOutDir/second.exe"
$binding|ConvertTo-Json -Depth 5|Set-Content "$ResultsDir/profile-binding.json"
if(-not $binding.passed){[ordered]@{candidateFunctionalVerificationPassed=$false;stage='profile-image-binding';applicationsStarted=$false;profileBinding=$binding}|ConvertTo-Json -Depth 6|Set-Content "$ResultsDir/summary.json";throw 'Resource profile belongs to another image; no native operation started'}
$followups=@()
function FailureFollowups([string]$Root){
 foreach($file in Get-ChildItem $Root -Recurse -Filter summary.json -File){
  $s=Get-Content $file.FullName -Raw|ConvertFrom-Json
  if($s.PSObject.Properties.Name -notcontains 'operations' -or -not @($s.operations).Count){continue}
  $failed=@(foreach($op in $s.operations){$log=Join-Path $op.manager 'controller.jsonl';if(Test-Path $log){Get-Content $log|ForEach-Object{$_|ConvertFrom-Json}|Where-Object{$_.type -eq 'abort_native_window_cleanup' -and -not $_.verified}}})
  if(-not $failed.Count){continue}
  $origin=$file.DirectoryName;$id=Join-Path $origin 'operation-identity.json'
  if(-not(Test-Path $id)){[ordered]@{boot=$boot;synthetic=$false;originalSummarySha256=(Get-FileHash $file.FullName).Hash}|ConvertTo-Json|Set-Content $id}
  $next="$ResultsDir/followups/$($script:followups.Count)";$err=''
  try{& "$PSScriptRoot/../r5-004c-wait/run-failure-followup.ps1" -OriginRoot $origin -OutDir $ControlOutDir -Profile $Profile -ResultsDir $next *> "$ResultsDir/followup-$($script:followups.Count).log"}catch{$err=$_.ToString()}
  $script:followups+= [ordered]@{origin=$origin;error=$err;assessment="$next/assessment.json"}
 }
}
function Stage([string]$Name,[scriptblock]$Run,[string]$Summary){
 $before=Test-CombinationInputs $frozen.inputs
 $errorText='';try{if(-not $before.passed){throw 'Frozen input changed before stage'};& $Run *> "$ResultsDir/$Name-console.log"}catch{$errorText=$_.ToString()}
 $data=if(Test-Path $Summary){Get-Content -Raw $Summary|ConvertFrom-Json}else{$null}
 $key=if($Name -in @('cleanup','crash','legacy')){'allEvidenceChecksPassed'}else{'passed'}
 $flag=$null -ne $data -and $data.PSObject.Properties.Name -contains $key -and $data.$key -eq $true
 if($Name -eq 'legacy'){$flag=$flag -and $data.regressions -eq 'passed'}
 $after=Test-CombinationInputs $frozen.inputs
 $okay=$null -ne $data -and -not $errorText -and $flag -and $after.passed
 $results.Add([pscustomobject]@{name=$Name;passed=$okay;error=$errorText;summary=$Summary;before=$before;after=$after});$results|ConvertTo-Json -Depth 7|Set-Content "$ResultsDir/stages.json"
 if($Name -in @('focus','cleanup','relocation','crash') -and (Test-Path (Split-Path $Summary))){
  try{FailureFollowups (Split-Path $Summary)}catch{$script:followups+=[ordered]@{origin=(Split-Path $Summary);error=$_.ToString();assessment='driver-failed-before-assessment'}}
 }
 Write-Output "$Name $okay"
}
# Explicitly run the cleanup matrix; the historical gate entry alone omits it.
Stage failure-once {& "$PSScriptRoot/../r5-004c-wait/test-followup-once.ps1" -OutDir $ControlOutDir -Profile $Profile -ResultsDir "$ResultsDir/once"} "$ResultsDir/once/summary.json"
Stage missing-proof {& "$PSScriptRoot/../r5-hook-126/test-missing-proof.ps1" -OriginRoot "$ResultsDir/once/serial-origin" -InputOutDir $InputOutDir -CetOutDir $CetOutDir -ControlOutDir $ControlOutDir -Profile $Profile -ResultsDir "$ResultsDir/proof"} "$ResultsDir/proof/summary.json"
Stage input-errors {& "$PSScriptRoot/../r5-hook-126/test-input-failures.ps1" -InputOutDir $InputOutDir -QueryFailureOutDir $QueryFailureOutDir -ResultsDir "$ResultsDir/input"} "$ResultsDir/input/summary.json"
Stage focus {& "$PSScriptRoot/test-focus.ps1" -OutDir $OutDir -ResultsDir "$ResultsDir/focus"} "$ResultsDir/focus/summary.json"
Stage cleanup {& "$PSScriptRoot/test-cleanup.ps1" -OutDir $OutDir -ResultsDir "$ResultsDir/cleanup" -SkipRegression} "$ResultsDir/cleanup/summary.json"
Stage relocation {& "$PSScriptRoot/../r5-004b/test.ps1" -OutDir $OutDir -ResultsDir "$ResultsDir/relocation"} "$ResultsDir/relocation/summary.json"
Stage crash {& "$PSScriptRoot/../r5-004b/test-gate.ps1" -OutDir $OutDir -ResultsDir "$ResultsDir/crash" -SkipRegression} "$ResultsDir/crash/summary.json"
$regression='not_run'
if(-not $SkipRegression){
 Stage legacy {
  & "$PSScriptRoot/../r5-003/test.ps1" -ResultsDir "$ResultsDir/reg3" -PreparedRegressionInput $frozen.preparedRegressionInput
 } "$ResultsDir/reg3/summary.json"
 $regression=if($results[-1].passed){'passed'}else{'failed'}
}
$finalInputs=Test-CombinationInputs $frozen.inputs;$sameBuild=$finalInputs.passed
$sameBoot=$boot -eq (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
$passed=@($results|Where-Object {-not $_.passed}).Count -eq 0 -and $sameBuild -and $sameBoot
[ordered]@{candidateFunctionalVerificationPassed=$passed;sameFinalBuild=$sameBuild;sameBoot=$sameBoot;boot=$boot;regressions=$regression;stages=$results;frozenInputAudit=$finalInputs;followups=$followups;fullNestedRegressionAttempted=(-not $SkipRegression);fullNestedRegressionExecuted=(Test-Path "$ResultsDir/reg3/r5-002-regression/r5-001b-regression/r5-001a-regression/r4-003-001-regression/r4-002a-regression/c001-regression/b001-regression/cet002-regression/summary.json");cleanupCausalFixVerified=$false;fullR5004Passed=$false;gateClosed=$false;manualHostTest='not_executed';hostRebootRecoveryVerified=$false}|ConvertTo-Json -Depth 7|Set-Content "$ResultsDir/summary.json" -Encoding utf8
if(-not $passed){throw 'Candidate combined verification failed; inspect individual preserved errors'}
