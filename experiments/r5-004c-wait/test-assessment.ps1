param([Parameter(Mandatory)][string]$ResultsDir)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
if(Test-Path -LiteralPath $ResultsDir){throw 'Preserve previous results'}
New-Item -ItemType Directory -Path $ResultsDir|Out-Null
. (Join-Path $PSScriptRoot 'assessment.ps1')
$checks=@()
foreach($case in @(
 @{name='normal-success';controller=0;manager=0;api='';error=0;copies=1;expected=$true},
 @{name='close-success-manager-failed-cleanup-succeeded';controller=0;manager=2;api='v.stop';error=170;copies=1;expected=$false},
 @{name='controller-failed';controller=2;manager=0;api='';error=0;copies=1;expected=$false},
 @{name='primary-error-preserved';controller=0;manager=0;api='v.stop';error=170;copies=1;expected=$false},
 @{name='duplicate-completion';controller=0;manager=0;api='';error=0;copies=2;expected=$false},
 @{name='missing-completion';controller=0;manager=0;api='';error=0;copies=0;expected=$false}
)){
 $supervisor=[pscustomobject]@{managerExit=$case.manager;protocolPassed=$true;cleanupVerified=$true}
 $rows=@(for($i=0;$i -lt $case.copies;$i++){[pscustomobject]@{nativeCleanupPassed=$true;firstApi=$case.api;firstError=$case.error}})
 $actual=Get-NormalCloseOutcome $case.controller $supervisor $rows
 $checks+=[ordered]@{name=$case.name;artificialEvidence=$true;expected=$case.expected;actual=$actual;passed=($actual.operationPassed -eq $case.expected)}
}
$okay=@($checks|Where-Object{-not $_.passed}).Count -eq 0
[ordered]@{passed=$okay;nativeProcessesStarted=$false;assessmentSha256=(Get-FileHash "$PSScriptRoot/assessment.ps1").Hash;checks=$checks}|ConvertTo-Json -Depth 8|Set-Content "$ResultsDir/summary.json" -Encoding utf8
if(-not $okay){throw 'Normal-close operation verdict failed the deterministic evidence test'}
