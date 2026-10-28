param([Parameter(Mandatory)][string]$ResultsDir)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
if(Test-Path $ResultsDir){throw 'New evidence directory required'};New-Item -ItemType Directory $ResultsDir|Out-Null
. "$PSScriptRoot/followup.ps1"
$checks=@()
foreach($c in @(
 @{name='natural-failure-after-cleanup';failed=$true;clean=$true;boot='2026-10-28T19:42:46.5000000+08:00';used=$false;expected=$true},
 @{name='no-natural-failure';failed=$false;clean=$true;boot='2026-10-28T19:42:46.5000000+08:00';used=$false;expected=$false},
 @{name='unconfirmed-resources';failed=$true;clean=$false;boot='2026-10-28T19:42:46.5000000+08:00';used=$false;expected=$false},
 @{name='changed-boot';failed=$true;clean=$true;boot='2026-10-28T19:42:47.5000000+08:00';used=$false;expected=$false},
 @{name='already-attempted';failed=$true;clean=$true;boot='2026-10-28T19:42:46.5000000+08:00';used=$true;expected=$false}
)){$a=Get-CleanupFollowupDecision $c.failed $c.clean '2026-10-28T19:42:46.5000000+08:00' $c.boot $c.used;$checks+=[ordered]@{name=$c.name;passed=($a.run -eq $c.expected);actual=$a;artificial=$true}}
foreach($both in @($false,$true)){$p=[pscustomobject]@{evidenceComplete=$true;cases=@([pscustomobject]@{sameWaitingPattern=$true;waitingEvidence=[pscustomobject]@{status='matched';links=@{guiTid=1;workerTid=2}};installationAttested=$true;supervisor=[pscustomobject]@{cleanupVerified=$true}},[pscustomobject]@{sameWaitingPattern=$both;waitingEvidence=[pscustomobject]@{status='matched';links=@{guiTid=1;workerTid=2}};installationAttested=$true;supervisor=[pscustomobject]@{cleanupVerified=$true}})};$checks+=[ordered]@{name="direct-only-both-patterns-$both";passed=((Test-DirectWindowFollowup $p) -eq $both);artificial=$true}}
$roundTrip=([ordered]@{boot='2026-10-28T19:42:46.5000000+08:00'}|ConvertTo-Json|ConvertFrom-Json).boot
$a=Get-CleanupFollowupDecision $true $true $roundTrip '2026-10-28T11:42:46.5000000Z' $false
$checks+=[ordered]@{name='json-date-object-same-utc-instance';passed=$a.run;artificial=$true}
$a=Get-CleanupFollowupDecision $true $true $roundTrip '2026-10-28T11:42:46.5000001Z' $false
$checks+=[ordered]@{name='one-tick-difference-is-different-boot';passed=(-not $a.run);artificial=$true}
$passed=@($checks|Where-Object{-not $_.passed}).Count -eq 0
[ordered]@{passed=$passed;nativeProcessesStarted=$false;checks=$checks}|ConvertTo-Json -Depth 6|Set-Content "$ResultsDir/summary.json" -Encoding utf8
if(-not $passed){throw 'Follow-up safety decision mismatch'}
