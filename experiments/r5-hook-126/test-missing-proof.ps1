param([Parameter(Mandatory)][string]$OriginRoot,[Parameter(Mandatory)][string]$InputOutDir,[Parameter(Mandatory)][string]$CetOutDir,[Parameter(Mandatory)][string]$ControlOutDir,[Parameter(Mandatory)][string]$Profile,[Parameter(Mandatory)][string]$ResultsDir)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
if(Test-Path $ResultsDir){throw 'New negative evidence directory required'};New-Item -ItemType Directory $ResultsDir|Out-Null
$hash=(Get-FileHash "$OriginRoot/summary.json").Hash;$copy=[IO.Path]::GetFullPath("$ResultsDir/private-evidence")
Copy-Item -LiteralPath $OriginRoot -Destination $copy -Recurse
$s=Get-Content "$copy/summary.json" -Raw|ConvertFrom-Json
foreach($o in $s.operations){$o.supervisor=$o.supervisor.Replace([IO.Path]::GetFullPath($OriginRoot),$copy);$o.manager=$o.manager.Replace([IO.Path]::GetFullPath($OriginRoot),$copy);$o.certificate=$o.certificate.Replace([IO.Path]::GetFullPath($OriginRoot),$copy)}
$s|ConvertTo-Json -Depth 15|Set-Content "$copy/summary.json" -Encoding utf8
$missing=[IO.Path]::GetFullPath((Join-Path $s.operations[0].supervisor 'owner.fvc'))
if(-not $missing.StartsWith($copy+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Refuse to change any original proof'}
Remove-Item -LiteralPath $missing
$id=Get-Content "$copy/operation-identity.json" -Raw|ConvertFrom-Json;$id.originalSummarySha256=(Get-FileHash "$copy/summary.json").Hash
$id|Add-Member -NotePropertyName artificialProofOmission -NotePropertyValue $true;$id|ConvertTo-Json|Set-Content "$copy/operation-identity.json" -Encoding utf8
$caught='';try{& "$PSScriptRoot/test-real.ps1" -InputOutDir $InputOutDir -CetOutDir $CetOutDir -ControlOutDir $ControlOutDir -Profile $Profile -ResultsDir "$ResultsDir/actual-driver" -FollowupOnlyEvidence $copy *> "$ResultsDir/driver-console.log"}catch{$caught=$_.ToString()}
$result=Get-Content "$ResultsDir/actual-driver/summary.json" -Raw|ConvertFrom-Json;$a=$result.assessment
$passed=$caught -and -not $result.serialVerificationPassed -and $a.reason -eq 'missing-cleanup-proof' -and $a.newControlPairRuns -eq 0 -and -not(Test-Path "$ResultsDir/actual-driver/followup/close-controls") -and (Get-FileHash "$OriginRoot/summary.json").Hash -eq $hash
[ordered]@{passed=[bool]$passed;synthetic=$true;actualDriverEntryInvoked=$true;missingPrivateProof=$missing;originalSummaryUnchanged=((Get-FileHash "$OriginRoot/summary.json").Hash -eq $hash);refusal=$result;originalOperationStillFailed=($s.passed -eq $false)}|ConvertTo-Json -Depth 18|Set-Content "$ResultsDir/summary.json" -Encoding utf8
if(-not $passed){throw 'Missing-cleanup-proof driver refusal failed'}
