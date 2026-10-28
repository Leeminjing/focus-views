# R5-004a complete assertion matrix, candidate build / disk-v2 oracle.
param([string]$OutDir=(Join-Path $PSScriptRoot 'out'),[string]$ResultsDir=(Join-Path $PSScriptRoot 'results/a'),[ValidateRange(1,2)][int]$Repeats=2,[switch]$SkipRegression)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$OutDir=[IO.Path]::GetFullPath($OutDir);$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(Test-Path -LiteralPath $ResultsDir){throw 'Choose a new ResultsDir; evidence is immutable'}
New-Item -ItemType Directory -Path $ResultsDir|Out-Null
. (Join-Path $PSScriptRoot '../r5-004b/verification.ps1')
$checks=[Collections.Generic.List[object]]::new();$cases=[Collections.Generic.List[object]]::new();$errors=[Collections.Generic.List[object]]::new();$operations=[Collections.Generic.List[object]]::new();$latestOwners=@{}
# Reuse the existing supervised runner and the full CPU/CET/GUI/file oracle.
$driver=Join-Path $PSScriptRoot '../r5-004/test.ps1';$tokens=$null;$parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($driver,[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw 'Cannot parse existing verification driver'}
foreach($statement in $ast.EndBlock.Statements){if($statement -is [Management.Automation.Language.FunctionDefinitionAst] -and $statement.Name -in @('Run-Supervised','Safe-Case','Complete-Restore')){. ([scriptblock]::Create($statement.Extent.Text))}}
$testHash=Sha $PSCommandPath
$toolchain=Get-Content -Raw (Join-Path $OutDir 'build.json')|ConvertFrom-Json
[ordered]@{time=(Get-Date -Format o);sourceHead=(& git -C $PSScriptRoot rev-parse HEAD);testSha256=$testHash;driverSha256=(Sha $driver);verificationSha256=(Sha (Join-Path $PSScriptRoot '../r5-004/verification.ps1'));oracleSha256=(Sha $verificationSource);system=[Environment]::OSVersion.VersionString;repeats=$Repeats;skipRegression=[bool]$SkipRegression}|ConvertTo-Json|Set-Content (Join-Path $ResultsDir 'identity.json')
Copy-Item -LiteralPath (Join-Path $OutDir 'build.json') -Destination (Join-Path $ResultsDir 'build.json')
Copy-Item -LiteralPath (Join-Path $OutDir 'reuse.json') -Destination (Join-Path $ResultsDir 'reuse.json')
$legacy=Join-Path $ResultsDir 'reg3/r5-002-regression/r5-001b-regression/r5-001a-regression/r4-003-001-regression/r4-002a-regression/c001-regression/b001-regression/cet002-regression/register-1/target-active-stack-before-continue.bin'
if(-not $SkipRegression -and $legacy.Length -ge 260){[ordered]@{allEvidenceChecksPassed=$false;stage='legacy-path-preflight';length=$legacy.Length;applicationsStarted=$false}|ConvertTo-Json|Set-Content (Join-Path $ResultsDir 'summary.json');throw 'Use a shorter ResultsDir for legacy native evidence'}
foreach($app in @('fixture','recorder')){foreach($iteration in 1..$Repeats){$label="$app-$iteration";$root=Join-Path $ResultsDir $label
 Safe-Case $label 'pending-rejection-cleanup-then-independent-recovery' {
  $store=Join-Path $root 'store';$save=Run-Supervised $app save (Join-Path $root 'save') $store 'session-committed'
  $source=Crash-Audit $save.manager $save.supervisor "$label.source" $true
  $fixed=InventoryText $store;$rejected=Run-Supervised $app restore (Join-Path $root 'reject') $store '' $save.certificate 'checkpoint-abort-reject'
  $m=Crash-Audit $rejected.manager $rejected.supervisor "$label.rejected" $false
  $guard=@($m|Where-Object type -eq 'private_image_guard_fault');$stop=@($m|Where-Object {$_.type -eq 'checkpoint_before_snapshot_restore' -and $_.view -eq 'RA'})
  Check "$label.actual-checkpoint-before-source-CPU" ($stop.Count -eq 1 -and $stop[0].pending -and $stop[0].eventCode -eq 1 -and -not $stop[0].snapshotCpuApplied -and $guard.Count -eq 1 -and $guard[0].originalSnapshotUntouched -and -not $guard[0].snapshotCpuApplied)
  $failure=@($m|Where-Object type -eq 'view_restore_rejected')
  Check "$label.exact-refusal-no-View-or-source-state-write" ($rejected.managerExit -eq 2 -and $failure.Count -eq 1 -and $failure[0].api -eq 'restore.build_or_image_identity' -and $failure[0].error -eq 1306 -and -not $failure[0].published -and -not $failure[0].workspaceExists -and @($m|Where-Object type -in @('restored_context','execution_result','view_published','business_effect')).Count -eq 0)
  $prepared=@($m|Where-Object type -eq 'abort_handler_prepared');$dispatch=@($m|Where-Object type -eq 'abort_exception_dispatched');$native=@($m|Where-Object type -eq 'abort_native_window_cleanup')
  Check "$label.owner-thread-native-DestroyWindow-and-exit" ($prepared.Count -eq 1 -and $dispatch.Count -eq 1 -and $native.Count -eq 1 -and $native[0].verified -and $native[0].destroyWindow -and $native[0].goneMask -eq 7 -and $native[0].error -eq 0 -and $native[0].observedTid -eq $stop[0].tid -and $native[0].processExited -and $native[0].exit -eq [Convert]::ToUInt32('E504A001',16))
  Check "$label.original-exception-dispatched-no-context-write" ($dispatch[0].status -eq [Convert]::ToUInt32('80010001',16) -and -not $dispatch[0].sourceCpuWritten -and $dispatch[0].pid -eq $stop[0].pid -and $dispatch[0].tid -eq $stop[0].tid -and @($m|Where-Object type -eq 'cleanup_debug_detached').Count -eq 0)
  $continued=@($m|Where-Object {$_.type -eq 'cleanup_debug_continue_begin' -and $_.event -eq 1 -and $_.tickMs -ge $stop[0].tickMs})
  Check "$label.no-swallowed-cleanup-exceptions" (@($continued|Where-Object status -ne ([Convert]::ToUInt32('80010001',16))).Count -eq 0)
  Check "$label.no-native-cleanup-error" (@($m|Where-Object type -in @('cleanup_error','rejected_view_cleanup_error','cleanup_abort_failure','cleanup_stop_secondary_failure')).Count -eq 0)
  $system=@($m|Where-Object type -eq 'cleanup_system_handle_probe')
  Check "$label.system-debug-handles-released-by-Windows" ($system.Count -gt 1 -and @($system|Where-Object {$_.valid -or $_.error -ne 6 -or $_.manuallyClosed}).Count -eq 0)
  $own=@($m|Where-Object type -eq 'cleanup_own_instance_handle_closed')
  Check "$label.own-process-and-thread-handles-closed-before-desktop" ($own.Count -eq 4 -and @($own|Where-Object {-not $_.closed -or $_.error -ne 0}).Count -eq 0 -and ($own|Measure-Object tickMs -Maximum).Maximum -le @($m|Where-Object type -eq 'desktop_closed')[0].tickMs)
  $helper=Rows (Join-Path $rejected.manager 'RA/abort-helper.jsonl');$removed=@($helper|Where-Object type -eq 'abort_hook_removed')
  Check "$label.hook-helper-release-after-pinned-target-exit" ($removed.Count -eq 1 -and $removed[0].targetExited -and $removed[0].releaseBasis -eq 'pinned-target-process-exit' -and @($m|Where-Object {$_.type -eq 'process_exited' -and $_.name -eq 'RA-abort-helper' -and $_.exitCode -eq 0}).Count -eq 1)
  Check "$label.fixed-Snapshots-and-Artifacts-survive-rejection" ((InventoryText $store) -ceq $fixed)
  $fresh=Run-Supervised $app restore (Join-Path $root 'fresh') $store '' $rejected.certificate
  $null=Complete-Restore "$label.fresh" $app $save $fresh $source
  Check "$label.new-independent-manager" ($fresh.managerPid -ne $rejected.managerPid -and $fresh.managerCreated -gt $rejected.managerCreated)
  Check "$label.fixed-Snapshots-and-Artifacts-survive-recovery" ((InventoryText $store) -ceq $fixed)
 }
}}
# Per-instance cleanup must still work after a different View has restored source state.
foreach($app in @('fixture','recorder')){$label="$app-RB";$root=Join-Path $ResultsDir $label
 Safe-Case $label 'published-RA-survives-RB-checkpoint-rejection' {
  $store=Join-Path $root 'store';$save=Run-Supervised $app save (Join-Path $root 'save') $store 'session-committed';$source=Crash-Audit $save.manager $save.supervisor "$label.source" $true;$fixed=InventoryText $store
  $rejected=Run-Supervised $app restore (Join-Path $root 'reject') $store '' $save.certificate 'checkpoint-abort-RB-reject'
  $m=Crash-Audit $rejected.manager $rejected.supervisor "$label.rejected" $false;$null=Audit $rejected.manager "$label.native"
  $failure=@($m|Where-Object type -eq 'view_restore_rejected');$native=@($m|Where-Object type -eq 'abort_native_window_cleanup');$survivor=@($m|Where-Object type -eq 'other_view_survived_rejection')
  Check "$label.only-RB-refused-after-RA-source-write" ($rejected.managerExit -eq 2 -and $failure.Count -eq 1 -and $failure[0].view -eq 'RB' -and $failure[0].api -eq 'restore.build_or_image_identity' -and @($m|Where-Object {$_.type -eq 'restore_source_write_begin' -and $_.view -eq 'RA'}).Count -eq 1 -and @($m|Where-Object {$_.type -eq 'restore_source_write_begin' -and $_.view -eq 'RB'}).Count -eq 0)
  Check "$label.RB-native-destroy-and-exit" ($native.Count -eq 1 -and $native[0].view -eq 'RB' -and $native[0].verified -and $native[0].destroyWindow -and $native[0].goneMask -eq 7)
  Check "$label.RA-still-runs-observes-and-paints" ($survivor.Count -eq 1 -and $survivor[0].rejected -eq 'RB' -and $survivor[0].survivor -eq 'RA' -and $survivor[0].alive -and $survivor[0].nativeObservationAndFrame -and @($m|Where-Object {$_.type -eq 'capture' -and $_.view -eq 'RA' -and $_.name -eq 'survived-rejection'}).Count -eq 1)
  Check "$label.fixed-store-unchanged" ((InventoryText $store) -ceq $fixed)
  $fresh=Run-Supervised $app restore (Join-Path $root 'fresh') $store '' $rejected.certificate;$null=Complete-Restore "$label.fresh" $app $save $fresh $source
  Check "$label.fixed-store-unchanged-after-fresh-recovery" ((InventoryText $store) -ceq $fixed)
 }
}
# Bounded safety negatives for the new hook/abort trust boundary.
foreach($app in @('fixture','recorder')){foreach($fault in @('abort-wrong-thread','abort-mapping-missing','checkpoint-postwrite-reject')){$label="$app-$fault";$root=Join-Path $ResultsDir $label
 Safe-Case $label 'unsafe-cleanup-refused' {
  $store=Join-Path $root 'store';$save=Run-Supervised $app save (Join-Path $root 'save') $store 'session-committed';$source=Crash-Audit $save.manager $save.supervisor "$label.source" $true;$fixed=InventoryText $store
  $before=$operations.Count;$rejected=$null
  try{$rejected=Run-Supervised $app restore (Join-Path $root 'reject') $store '' $save.certificate $fault}catch{if($operations.Count -ne $before+1){throw};$rejected=$operations[-1]}
  $summary=Get-Content -Raw (Join-Path $rejected.supervisor 'summary.json')|ConvertFrom-Json;$m=Rows (Join-Path $rejected.manager 'controller.jsonl')
  Check "$label.no-usable-View" ($summary.protocolPassed -and $rejected.managerExit -ne 0 -and @($m|Where-Object type -eq 'view_published').Count -eq 0 -and @($m|Where-Object type -eq 'abort_exception_dispatched').Count -eq 0)
  if($fault -eq 'abort-wrong-thread'){
   $helper=Rows (Join-Path $rejected.manager 'RA/abort-helper.jsonl');$native=@($helper|Where-Object type -eq 'abort_helper_failure')
   Check "$label.native-owner-rejection-before-install" ($native.Count -eq 1 -and $native[0].api -eq 'abort.thread_owner' -and $native[0].error -eq 1307 -and @($helper|Where-Object type -eq 'abort_hook_installed').Count -eq 0 -and @($m|Where-Object type -eq 'restored_context').Count -eq 0)
  }elseif($fault -eq 'abort-mapping-missing'){
   $failure=@($m|Where-Object type -eq 'cleanup_runtime_failure');$refusal=@($m|Where-Object type -eq 'view_restore_rejected')
   Check "$label.native-mapping-failure-once-no-retry" ($failure.Count -eq 1 -and $failure[0].api -eq 'OpenFileMappingW.abort' -and $failure[0].error -eq 2 -and -not $failure[0].queryRetried -and $refusal.Count -eq 1 -and $refusal[0].api -eq 'OpenFileMappingW.abort' -and @($m|Where-Object type -in @('abort_handler_prepared','restored_context')).Count -eq 0)
  }else{
   $written=@($m|Where-Object type -eq 'after_context_fault');$refused=@($m|Where-Object type -eq 'cleanup_abort_refused_dirty')
   Check "$label.actual-source-context-written-abort-forbidden" ($written.Count -eq 1 -and $written[0].sourceCpuWritten -and $refused.Count -ge 1 -and @($refused|Where-Object {$_.continueForUserExecution -or -not $_.sourceStateMayBePartiallyWritten}).Count -eq 0)
  }
  Check "$label.fixed-store-unchanged" ((InventoryText $store) -ceq $fixed)
  if($summary.cleanupVerified){
   $null=Crash-Audit $rejected.manager $rejected.supervisor "$label.cleanup" $false
   $fresh=Run-Supervised $app restore (Join-Path $root 'fresh') $store '' $rejected.certificate
   $null=Complete-Restore "$label.fresh" $app $save $fresh $source
  }else{
   $supervisor=Rows (Join-Path $rejected.supervisor 'supervisor.jsonl');$failure=@($supervisor|Where-Object type -eq 'supervisor_failure')
   Check "$label.unconfirmed-cleanup-no-certificate" (-not(Test-Path -LiteralPath $rejected.certificate) -and $failure.Count -eq 1 -and $failure[0].api -eq 'crash.desktop_retained' -and -not(Test-Path -LiteralPath (Join-Path ($store+'.owners') ("$($summary.managerPid)-$($summary.managerCreated).owner.done"))))
   # This is a new explicit operation, not a retry of the failed restore.
   $blocked=Run-Supervised $app restore (Join-Path $root 'blocked') $store '' $save.certificate -ExactPrevious
   $recovery=Rows (Join-Path $blocked.manager 'recovery.jsonl');Check "$label.next-operation-fails-closed" ($blocked.managerExit -eq 2 -and @($recovery|Where-Object {$_.type -eq 'recovery_failure' -and $_.api -eq 'recovery.owner_cleanup_unconfirmed'}).Count -eq 1 -and -not(Test-Path -LiteralPath (Join-Path $blocked.manager 'controller.jsonl')))
  }
  Check "$label.fixed-store-still-unchanged" ((InventoryText $store) -ceq $fixed)
 }
}}
foreach($operation in $operations){$rows=Rows (Join-Path $operation.supervisor 'supervisor.jsonl');foreach($pending in @($rows|Where-Object type -eq 'image_diagnostic_pending')){$resolved=@($rows|Where-Object {$_.type -eq 'deferred_image_exit_confirmed' -and $_.pid -eq $pending.pid -and $_.created -eq $pending.created});Check "$($operation.app).$($operation.managerPid).image-diagnostic-$($pending.pid)-same-instance" ($resolved.Count -eq 1 -and $resolved[0].originalError -eq $pending.error -and $resolved[0].samePinnedHandle -and $resolved[0].nativeWaitSignaled -and -not $resolved[0].queryRetried)}}
Check 'test-source-unchanged' ((Sha $PSCommandPath) -eq $testHash)
$body=$errors.Count -eq 0 -and @($checks|Where-Object {-not $_.passed}).Count -eq 0;$regression='not_run'
if(-not $SkipRegression){try{& (Join-Path $PSScriptRoot '../r5-003/build.ps1') -MsvcRoot $toolchain.msvc -SdkRoot $toolchain.sdk -SdkVersion $toolchain.sdkVersion *> (Join-Path $ResultsDir 'regression-build.log');& (Join-Path $PSScriptRoot '../r5-003/test.ps1') -ResultsDir (Join-Path $ResultsDir 'reg3') *> (Join-Path $ResultsDir 'regression-console.log');$old=Get-Content -Raw (Join-Path $ResultsDir 'reg3/summary.json')|ConvertFrom-Json;$regression=if($old.allEvidenceChecksPassed -and $old.regressions -eq 'passed'){'passed'}else{'failed'};Check 'R5-003-and-complete-nested-regressions' ($regression -eq 'passed')}catch{$regression='failed';$errors.Add([pscustomobject]@{case='regression';error=$_.ToString()});Check 'R5-003-and-complete-nested-regressions' $false}}
$okay=$errors.Count -eq 0 -and @($checks|Where-Object {-not $_.passed}).Count -eq 0
[ordered]@{allEvidenceChecksPassed=$okay;preSourceWriteCheckpointCleanupVerified=$body;regressions=$regression;aslrRelocationVerified=$false;fullR5004Passed=$false;gateClosed=$false;manualHostTest='not_executed';checks=$checks;cases=$cases;operations=$operations;errors=$errors}|ConvertTo-Json -Depth 10|Set-Content (Join-Path $ResultsDir 'summary.json') -Encoding utf8
if(-not $okay){throw 'R5-004a failed; native evidence preserved'}
'R5_004A_STOPPED_FAILURE_CLEANUP_PASSED'
