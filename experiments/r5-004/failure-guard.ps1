param([string]$FailedRun=(Join-Path $PSScriptRoot 'results/g'),[string]$Output=(Join-Path $PSScriptRoot 'out/failure-guard.json'))
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'verification.ps1')
$tokens=$null;$parseErrors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'test.ps1'),[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw 'Cannot parse current verification driver'}
$definition=@($ast.EndBlock.Statements|Where-Object {$_ -is [Management.Automation.Language.FunctionDefinitionAst] -and $_.Name -eq 'Complete-Restore'})
if($definition.Count -ne 1){throw 'Exactly one completion guard required'};. ([scriptblock]::Create($definition[0].Extent.Text))
$checks=[Collections.Generic.List[object]]::new();$s=Get-Content -Raw (Join-Path $FailedRun 'summary.json')|ConvertFrom-Json
$operation=@($s.operations|Where-Object {$_.root -like '*fixture-fault-resource-failure/restore' -or $_.root -like '*fixture-fault-resource-failure\restore'})
if($operation.Count -ne 1){throw 'Preserve the original concrete preflight refusal'}
$message='';try{$null=Complete-Restore 'primary-refusal' fixture $null $operation[0] @() $true}catch{$message=$_.Exception.Message}
$passed=$message.Contains('recovery.owner_cleanup_unconfirmed') -and -not $message.Contains('Cannot find path') -and @($checks|Where-Object name -like '*failed-window-native-close*').Count -eq 0
[ordered]@{diagnosticGuardVerified=$passed;sourceFailure='g/fixture-fault-resource-failure/restore/manager/recovery.jsonl';nativeApplicationsStarted=$false;nativeMatrixRerun=$false;actualError=$message;underlyingRecoveryPassed=$false;observedChecks=$checks}|ConvertTo-Json -Depth 6|Set-Content $Output -Encoding utf8
if(-not $passed){throw 'Original API must survive without secondary missing evidence'}
Write-Output 'PRIMARY_FAILURE_GUARD_VERIFIED'
