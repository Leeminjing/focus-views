# Reads original failures and every native cleanup proof before starting one
# independent diagnostic operation. Never retries the failed Snapshot.
param([Parameter(Mandatory)][string]$OriginRoot,[Parameter(Mandatory)][string]$OutDir,[Parameter(Mandatory)][string]$Profile,[Parameter(Mandatory)][string]$ResultsDir)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
if(Test-Path $ResultsDir){throw 'Fresh follow-up output required'};New-Item -ItemType Directory $ResultsDir|Out-Null
. "$PSScriptRoot/followup.ps1"
$reason='missing-origin-evidence';$proofs=@();$failure=@();$originPassed=$null;$pair=$null;$pairRun=$false;$direct=$false;$sequence=$false;$synthetic=$false;$originHash='';$failureLogHashes=@()
try{
 $id=Get-Content "$OriginRoot/operation-identity.json" -Raw|ConvertFrom-Json;$synthetic=$id.synthetic
 $originHash=(Get-FileHash "$OriginRoot/summary.json").Hash
 if($originHash -ne $id.originalSummarySha256){$reason='origin-summary-identity';throw 'Changed original summary'}
 $origin=Get-Content "$OriginRoot/summary.json" -Raw|ConvertFrom-Json;$originPassed=$origin.passed
 foreach($f in Get-ChildItem $OriginRoot -Filter controller.jsonl -File -Recurse){$failureLogHashes+=[ordered]@{path=$f.FullName;sha256=(Get-FileHash $f.FullName).Hash};$failure+=@(Get-Content $f.FullName|ForEach-Object{$_|ConvertFrom-Json}|Where-Object{$_.type -eq 'abort_native_window_cleanup' -and -not $_.verified})}
 if(-not $failure.Count){$reason='no-original-cleanup-failure';throw 'No original cleanup failure'}
 $reason='missing-cleanup-proof'
 if(-not $origin.operations.Count){throw 'No owned operations'}
 foreach($op in $origin.operations){$summary=Join-Path $op.supervisor 'summary.json';$certificate=Join-Path $op.supervisor 'owner.fvc'
  if(-not(Test-Path $summary) -or -not(Test-Path $certificate)){throw "Missing original cleanup proof: $($op.supervisor)"}
  $p=Get-Content $summary -Raw|ConvertFrom-Json
  if(-not $p.protocolPassed -or -not $p.cleanupVerified){$reason='cleanup-unconfirmed';throw 'Original owned resources not confirmed gone'}
  $proofs+=[ordered]@{summary=$summary;summarySha256=(Get-FileHash $summary).Hash;certificate=$certificate;certificateSha256=(Get-FileHash $certificate).Hash;managerPid=$p.managerPid;managerCreated=$p.managerCreated;cleanupVerified=$p.cleanupVerified}
 }
 $now=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToString('o');$d=Get-CleanupFollowupDecision $true $true $id.boot $now $false
 if(-not $d.run){$reason='boot-changed';throw 'Different boot; no new diagnostic operation'}
 [ordered]@{synthetic=$synthetic;originalOperationPassed=$originPassed;originalSummarySha256=$originHash;failures=$failure;proofs=$proofs;decision=$d}|ConvertTo-Json -Depth 10|Set-Content "$ResultsDir/before-launch.json" -Encoding utf8
 $pairRun=$true;try{& "$PSScriptRoot/test-no-cleanup.ps1" -OutDir $OutDir -Profile $Profile -ResultsDir "$ResultsDir/close-controls" *> "$ResultsDir/close-controls-console.log"}catch{$_|Out-String|Set-Content "$ResultsDir/control-error.txt"}
 $pair=Get-Content "$ResultsDir/close-controls/summary.json" -Raw|ConvertFrom-Json
 $sequence=$pair.evidenceComplete -and @($pair.cases|Where-Object{-not $_.supervisor.cleanupVerified -or -not $_.supervisor.protocolPassed}).Count -eq 0
 if(Test-DirectWindowFollowup $pair){$direct=$true;try{& "$PSScriptRoot/test-no-cleanup.ps1" -OutDir $OutDir -Profile $Profile -ResultsDir "$ResultsDir/direct-window" -DirectWindow *> "$ResultsDir/direct-console.log"}catch{$_|Out-String|Set-Content "$ResultsDir/direct-error.txt"};$dp=Get-Content "$ResultsDir/direct-window/summary.json" -Raw|ConvertFrom-Json;$sequence=$sequence -and $dp.evidenceComplete -and $dp.cases[0].supervisor.cleanupVerified}
 $reason='diagnostic-outcomes-recorded'
}catch{$_.ToString()|Set-Content "$ResultsDir/entry-error.txt" -Encoding utf8}
$unchanged=$originHash -and (Get-FileHash "$OriginRoot/summary.json").Hash -eq $originHash
foreach($f in $failureLogHashes){if((Get-FileHash $f.path).Hash -ne $f.sha256){$unchanged=$false}}
$pass=$pairRun -and $sequence -and $unchanged
[ordered]@{diagnosticChainPassed=$pass;reason=$reason;synthetic=$synthetic;originalOperationPassed=$originPassed;originalFailureRetained=($unchanged -and $originPassed -eq $false);originalSummarySha256=$originHash;originalLogHashes=$failureLogHashes;proofs=$proofs;newControlPairRuns=$(if($pairRun){1}else{0});controlPair=$pair;directWindowExecuted=$direct;nativeCleanupPassed=$false;cleanupCausalFixVerified=$false;fullR5004Passed=$false;gateClosed=$false}|ConvertTo-Json -Depth 15|Set-Content "$ResultsDir/assessment.json" -Encoding utf8
if(-not $pass){throw "Failure follow-up refused or incomplete: $reason"}
