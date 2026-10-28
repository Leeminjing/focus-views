param([Parameter(Mandatory)][string]$InputOutDir,[Parameter(Mandatory)][string]$QueryFailureOutDir,[Parameter(Mandatory)][string]$ResultsDir)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
if(Test-Path $ResultsDir){throw 'Fresh evidence required'};New-Item -ItemType Directory $ResultsDir|Out-Null
. "$PSScriptRoot/verification.ps1";Assert-InputDiagnosticBuild $InputOutDir;Assert-InputDiagnosticBuild $QueryFailureOutDir
. "$PSScriptRoot/../r4-001-gate-001/lifetimes.ps1"
$qbuild=Get-Content "$QueryFailureOutDir/build.json" -Raw|ConvertFrom-Json;if(-not $qbuild.diagnosticQueryFailure){throw 'Explicit query-failure build required'}
$app="$ResultsDir/missing-dll-app";New-Item -ItemType Directory $app|Out-Null;Copy-Item "$InputOutDir/r305a.exe" "$app/r305a.exe"
$checks=@();$cases=@()
foreach($mode in @('missing-dll','query-failure')){
 $exe=if($mode -eq 'missing-dll'){"$app/r305a.exe"}else{"$QueryFailureOutDir/r305a.exe"};$root="$ResultsDir/$mode";$errorText='';$exit=-1
 try{& $exe --role controller --scenario positive --output $root *> "$ResultsDir/$mode-console.log";$exit=$LASTEXITCODE}catch{$errorText=$_.ToString()}
 $rows=if(Test-Path "$root/controller.jsonl"){@(Get-Content "$root/controller.jsonl"|ForEach-Object{$_|ConvertFrom-Json})}else{@()}
 $inputs=@(Get-ChildItem $root -Filter '*-input.jsonl' -File|ForEach-Object{Get-Content $_.FullName|ForEach-Object{$_|ConvertFrom-Json}})
 $api=@($inputs|Where-Object{$_.stage -eq 'install_diag' -and $_.event -eq 'api'});$primary=@($rows|Where-Object type -eq 'experiment_failure');$audit=Test-OwnedLifetimes $rows
 $local=[ordered]@{ownedLifetimes=$audit.passed;noScriptException=(-not $errorText)}
 if($mode -eq 'missing-dll'){
  $load=@($api|Where-Object api -eq 'LoadLibraryW.runtime');$invalid=@($inputs|Where-Object stage -eq 'channel_invalid');$hook=@($api|Where-Object api -like 'SetWindowsHookExW*')
  $local.originalExecutableBytes=((Get-FileHash $exe).Hash -eq (Get-FileHash "$InputOutDir/r305a.exe").Hash)
  $local.realLoadLibraryFailure=$load.Count -eq 1 -and $load[0].result -eq 0 -and $load[0].error -eq 126 -and -not(Test-Path "$app/runtime304.dll")
  $local.channelFirstError=$invalid.Count -eq 1 -and $invalid[0].primaryApi -eq 'LoadLibraryW.runtime' -and $invalid[0].primaryError -eq 126
  $local.controllerFirstError=$exit -ne 0 -and $primary.Count -eq 1 -and $primary[0].api -eq 'LoadLibraryW.runtime' -and $primary[0].error -eq 126
  $local.hookNeverAttempted=$hook.Count -eq 0
  $local.inputHelperExitConfirmed=$load.Count -eq 1 -and @($rows|Where-Object{$_.type -eq 'process_exited' -and $_.pid -eq $load[0].pid -and $_.exitCode -eq 2}).Count -eq 1
  $local.noViewPublished=@($rows|Where-Object type -eq 'view_published').Count -eq 0
 }else{
  $dq=@($inputs|Where-Object{$_.stage -eq 'install_diag' -and $_.event -eq 'diagnostic_failure' -and $_.api -eq 'GetProcessTimes.install_diagnostic_test' -and $_.error -eq 6})
  $closed=@($inputs|Where-Object stage -eq 'channel_closed');$done=@($rows|Where-Object type -eq 'experiment_complete')
  $local.realAdditionalQueryFailed=$dq.Count -gt 0
  $local.originalCallsStillSucceed=$api.Count -gt 0 -and @($api|Where-Object{$_.error -ne 0 -or $_.result -eq 0}).Count -eq 0
  $local.noChannelFirstError=$closed.Count -gt 0 -and @($closed|Where-Object{$_.primaryError -ne 0 -or $_.primaryApi}).Count -eq 0
  $local.realControlAndContinuation=$exit -eq 0 -and $primary.Count -eq 0 -and $done.Count -eq 1 -and $done[0].failedAssertions -eq 0 -and $done[0].postRestoreInputPassed -and $done[0].executionContinuationPassed
 }
 $passed=@($local.Values|Where-Object{-not $_}).Count -eq 0
 $cases+=[ordered]@{mode=$mode;synthetic=$true;exit=$exit;error=$errorText;passed=$passed;checks=$local;lifetimeAudit=$audit;rawAPIs=$api}
 $cases|ConvertTo-Json -Depth 12|Set-Content "$ResultsDir/cases.json" -Encoding utf8
}
$passed=@($cases|Where-Object{-not $_.passed}).Count -eq 0
[ordered]@{passed=$passed;cases=$cases;historicalSetWindowsHook126Reproduced=$false;installationAPIReturnsModified=$false;rootCauseConfirmed=$false;gateClosed=$false}|ConvertTo-Json -Depth 14|Set-Content "$ResultsDir/summary.json" -Encoding utf8
if(-not $passed){throw 'Real input failure-path verification failed; original errors retained'}
