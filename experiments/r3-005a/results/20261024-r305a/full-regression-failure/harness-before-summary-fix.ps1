param(
    [string]$OutDir=(Join-Path $PSScriptRoot 'out'),
    [string]$ResultsDir=(Join-Path $PSScriptRoot ('results\'+(Get-Date -Format 'yyyyMMdd-HHmmss'))),
    [ValidateSet('minimal','all','ret-probe')][string]$Scenario='all',
    [string]$R304OutDir=(Join-Path $PSScriptRoot '../r3-004/out')
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition 'public static class R305Receipt { public static ulong Compute(ulong reg,ulong leaf,ulong middle,ulong outer) { unchecked { ulong x=(reg^leaf)+middle; return ((x<<13)|(x>>(64-13)))^outer; } } }'
$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(Test-Path -LiteralPath $ResultsDir){throw 'Use a new ResultsDir; preserve failure evidence.'}
New-Item -ItemType Directory -Path $ResultsDir | Out-Null
$taskExe=[IO.Path]::GetFullPath((Join-Path $OutDir 'r305a.exe'))
$taskRun=Join-Path $ResultsDir 'run'
$taskChecks=[Collections.Generic.List[object]]::new()
function Check([string]$Name,[bool]$Okay){$taskChecks.Add([pscustomobject]@{name=$Name;passed=$Okay});Write-Output "evidence $Name=$Okay"}
function Lines([string]$Path){@(Get-Content -LiteralPath $Path -Encoding utf8 | Where-Object {$_} | ForEach-Object {$_ | ConvertFrom-Json})}
$taskSources=@('controller.cpp','fixture.cpp','protocol.hpp','traps.asm','build.ps1','test.ps1','../r3-004/runtime.cpp','../r3-004/channel.hpp','../r3-004/iat.hpp','../r3-004/input.cpp','../r3-004/observer.cpp','../r3/bridge.cpp','../r3/protocol.hpp','../common/native.hpp','../r1/snapshot.hpp')
$taskIdentity=foreach($taskSource in $taskSources){[ordered]@{file=$taskSource;sha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $taskSource)).Hash.ToLowerInvariant()}}
$taskPrincipal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
[ordered]@{time=(Get-Date -Format o);os=[Environment]::OSVersion.VersionString;architecture=$env:PROCESSOR_ARCHITECTURE
 elevated=$taskPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator);baseCommit=(& git -C $PSScriptRoot rev-parse HEAD)
 sourceFiles=$taskIdentity;exeSha256=(Get-FileHash -LiteralPath $taskExe).Hash.ToLowerInvariant();dllSha256=(Get-FileHash -LiteralPath (Join-Path $OutDir 'runtime304.dll')).Hash.ToLowerInvariant()
 systemSettingsChanged=$false;CETDisabled=$false;GSDisabled=$false;driver=$false;manualHostTest='not_executed'
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $ResultsDir 'identity.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $OutDir 'build.json') -Destination (Join-Path $ResultsDir 'build.json')
Select-String -LiteralPath (Join-Path $OutDir 'r305a.map') -Pattern 'CheckpointSite|ReadySite|ContinuationTrap|__security_check_cookie|\?leaf@|\?middle@|\?outer@|\?application@' |
 ForEach-Object {$_.Line} | Set-Content -LiteralPath (Join-Path $ResultsDir 'symbols.txt') -Encoding utf8
& $taskExe --output $taskRun --scenario $Scenario 2>&1 | Tee-Object -FilePath (Join-Path $ResultsDir 'console.log')
$taskExit=$LASTEXITCODE
$taskEvents=Lines (Join-Path $taskRun 'controller.jsonl')
$taskDone=@($taskEvents | Where-Object type -eq 'experiment_complete')
Check 'diagnostic-finished-without-instrumentation-assertion-failures' ($taskDone.Count -eq 1 -and $taskDone[0].failedAssertions -eq 0)
$taskPrimary=$taskDone.Count -eq 1 -and $taskDone[0].primaryRestorePassed
Check 'exit-status-preserves-experiment-verdict' (($taskPrimary -and $taskExit -eq 0) -or (-not $taskPrimary -and $taskExit -eq 2))
$taskSnapshot=@($taskEvents | Where-Object type -eq 'snapshot')[0]
$taskLayout=@($taskEvents | Where-Object type -eq 'raw_layout')[0]
$taskStack=[IO.File]::ReadAllBytes((Join-Path $taskRun 'S0-active-stack.bin'))
$taskData=[IO.File]::ReadAllBytes((Join-Path $taskRun 'S0-data.bin'))
$taskContext=[IO.File]::ReadAllBytes((Join-Path $taskRun 'S0-context.bin'))
$taskLocals=foreach($taskAddress in $taskSnapshot.localAddresses){[BitConverter]::ToUInt64($taskStack,[int]([uint64]$taskAddress-[uint64]$taskSnapshot.rsp))}
$taskRegister=[BitConverter]::ToUInt64($taskContext,[int]$taskLayout.r10Offset)
$taskExpected=[R305Receipt]::Compute($taskRegister,$taskLocals[0],$taskLocals[1],$taskLocals[2])
Check 'raw-stack-register-and-independent-receipt-agree' ($taskExpected -eq [uint64]$taskSnapshot.expectedReceipt -and $taskRegister -eq [uint64]$taskSnapshot.registerR10 -and
 $taskLocals[0] -eq [uint64]$taskSnapshot.leaf -and $taskLocals[1] -eq [uint64]$taskSnapshot.middle -and $taskLocals[2] -eq [uint64]$taskSnapshot.outer)
$taskSourceReturn=@($taskEvents | Where-Object { $_.type -eq 'assertion' -and $_.name -eq 'source.normal-return-chain' })
Check 'source-returned-through-the-captured-ordinary-CPP-frames' ($taskSourceReturn.Count -eq 1 -and $taskSourceReturn[0].passed -and
 @($taskEvents | Where-Object { $_.type -eq 'assertion' -and $_.name -eq 'source.native-unwind-sees-three-CPP-frames' -and $_.passed }).Count -eq 1)
$taskSourceExit=@($taskEvents | Where-Object { $_.type -eq 'process_exited' -and $_.name -eq 'source' })[0]
$taskTargetCreate=@($taskEvents | Where-Object { $_.type -eq 'process_created' -and $_.name -eq 'restored' })[0]
Check 'source-confirmed-dead-before-new-carrier' ($taskSourceExit.pid -ne $taskTargetCreate.pid -and $taskSourceExit.tickMs -le $taskTargetCreate.tickMs)
$taskChanged=@($taskEvents | Where-Object type -eq 'source_changed')[0]
Check 'source-changed-after-fixed-S0' ($taskChanged.work.text -ne $taskSnapshot.work.text -and $taskChanged.work.counter -gt $taskSnapshot.work.counter)
$taskRebind=@($taskEvents | Where-Object { $_.type -eq 'resource_rebind' -and $_.view -eq 'restored' })
$taskTargetData=[IO.File]::ReadAllBytes((Join-Path $taskRun 'target-data-before-continue.bin'))
$taskTargetStack=[IO.File]::ReadAllBytes((Join-Path $taskRun 'target-active-stack-before-continue.bin'))
$taskExpectedData=$taskData.Clone();$taskExpectedStack=$taskStack.Clone();$taskReferences=$true
foreach($taskRef in $taskRebind){
 $taskIsData=[uint64]$taskRef.address -lt [uint64]$taskSnapshot.rsp
 $taskOffset=if($taskIsData){[int]([uint64]$taskRef.address-[uint64]$taskLayout.dataAddress)}else{[int]([uint64]$taskRef.address-[uint64]$taskSnapshot.rsp)}
 $taskBuffer=if($taskIsData){,$taskExpectedData}else{,$taskExpectedStack}
 if([BitConverter]::ToUInt64($taskBuffer,$taskOffset) -ne [uint64]$taskRef.oldHwnd -or $taskRef.oldHwnd -eq $taskRef.newHwnd -or $taskRef.logicalWindow -ne 1){$taskReferences=$false}
 [Array]::Copy([BitConverter]::GetBytes([uint64]$taskRef.newHwnd),0,$taskBuffer,$taskOffset,8)
}
Check 'only-declared-live-window-references-rebound-in-raw-bytes' ($taskReferences -and $taskRebind.Count -eq 3 -and
 [Convert]::ToBase64String($taskExpectedData) -eq [Convert]::ToBase64String($taskTargetData) -and [Convert]::ToBase64String($taskExpectedStack) -eq [Convert]::ToBase64String($taskTargetStack))
$taskTargetContext=[IO.File]::ReadAllBytes((Join-Path $taskRun 'target-context-before-continue.bin'))
Check 'raw-target-context-has-saved-RIP-RSP-register' ([BitConverter]::ToUInt64($taskTargetContext,[int]$taskLayout.ripOffset) -eq [uint64]$taskSnapshot.rip -and
 [BitConverter]::ToUInt64($taskTargetContext,[int]$taskLayout.rspOffset) -eq [uint64]$taskSnapshot.rsp -and [BitConverter]::ToUInt64($taskTargetContext,[int]$taskLayout.r10Offset) -eq $taskRegister)
$taskXmmOffset=[int]$taskLayout.xmmOffset
Check 'SSE-register-bytes-readback-matches-capture' ([Convert]::ToBase64String($taskContext,$taskXmmOffset,256) -eq [Convert]::ToBase64String($taskTargetContext,$taskXmmOffset,256))
Check 'window-resource-actually-created-in-target-thread' (@($taskEvents | Where-Object { $_.type -eq 'window_resource_verified' -and $_.view -eq 'restored' -and $_.pid -eq $taskTargetCreate.pid -and $_.tid -eq $taskTargetCreate.tid -and $_.class -eq 'FocusViewsR305SelfDraw' -and $_.userData -eq $taskLayout.dataAddress }).Count -eq 1)
$taskTargetHalts=@($taskEvents | Where-Object { $_.type -eq 'halt' -and $_.view -eq 'restored' })
Check 'carrier-never-replays-business-initialization-or-S0-checkpoint' (@($taskTargetHalts | Where-Object { $_.initializations -ne 0 -or $_.checkpoints -ne 0 }).Count -eq 0)
if(-not $taskPrimary){
 $taskFault=@($taskEvents | Where-Object { $_.type -eq 'continuation_fault' -and $_.view -eq 'restored' })[0]
 $taskShadow=@($taskEvents | Where-Object { $_.type -eq 'shadow_stack_diagnostic' -and $_.view -eq 'restored' })[0]
 $taskCet=@($taskEvents | Where-Object { $_.type -eq 'xstate_read' -and $_.view -eq 'restored' })[0]
 Check 'actual-first-RET-fails-with-CET-subcode-57' ($taskFault.exceptionCode -eq 3221226505 -and $taskFault.parameters[0] -eq 57 -and
 $taskFault.instructionHex.StartsWith('c3') -and $taskFault.rip -eq ([uint64]$taskSnapshot.rip+3) -and $taskFault.normalReturn -eq $taskShadow.savedNormalReturn)
 Check 'unchanged-target-SSP-and-different-shadow-return-proven' ($taskCet.cetPresent -and ($taskCet.cetControl -band 1) -eq 1 -and $taskCet.ssp -eq $taskShadow.targetBootSsp -and
 $taskCet.ssp -ne $taskShadow.sourceSsp -and $taskShadow.targetShadowReturn -ne $taskShadow.savedNormalReturn)
 Check 'failed-restore-never-publishes-usable-View-or-stale-frame' (@($taskEvents | Where-Object { $_.type -eq 'view_published' -and $_.view -eq 'restored' }).Count -eq 0 -and
 @($taskEvents | Where-Object { $_.type -eq 'halt' -and $_.view -eq 'restored' -and $_.phase -eq 'continued' }).Count -eq 0 -and
 -not(Test-Path -LiteralPath (Join-Path $taskRun 'restored-S0.png')) -and -not(Test-Path -LiteralPath (Join-Path $taskRun 'restored-after-input.png')))
}
if($Scenario -eq 'all'){
 $taskExpectedErrors=@{build=1306;window=1407;reference=13;context=5}
 foreach($taskCase in $taskExpectedErrors.Keys){
  Check ("negative.$taskCase.actual-rejection") (@($taskEvents | Where-Object { $_.type -eq 'restore_rejected' -and $_.case -eq $taskCase -and $_.error -eq $taskExpectedErrors[$taskCase] }).Count -eq 1 -and
   @($taskEvents | Where-Object { $_.type -eq 'view_published' -and $_.view -eq "negative-$taskCase" }).Count -eq 0)
 }
 if(-not $taskPrimary){Check 'masked-state-omission-tests-explicitly-not-executed' (@($taskEvents | Where-Object type -eq 'negative_skipped').Count -eq 3)}
}
foreach($taskCapture in @($taskEvents | Where-Object type -eq 'capture')){
 $taskBitmap=[Drawing.Bitmap]::new((Join-Path $taskRun ($taskCapture.name+'.png')));$taskTick=0;$taskValid=$true
 try{for($taskBit=0;$taskBit -lt 16;$taskBit++){
  $taskPixel=$taskBitmap.GetPixel(($taskCapture.clientDx+28+10*$taskBit),($taskCapture.clientDy+356))
  $taskWhite=$taskPixel.R -gt 240 -and $taskPixel.G -gt 240 -and $taskPixel.B -gt 240
  $taskBlack=$taskPixel.R -lt 15 -and $taskPixel.G -lt 15 -and $taskPixel.B -lt 15
  if(-not($taskWhite -or $taskBlack)){$taskValid=$false};if($taskWhite){$taskTick=$taskTick -bor (1 -shl $taskBit)}
 }}finally{$taskBitmap.Dispose()}
 $taskPaints=@(Lines (Join-Path $taskRun ($taskCapture.view+'-events.jsonl')) | Where-Object { $_.event -in 'wm_paint_end','wm_printclient' -and
  $_.state.lastDrawUpdate -eq $taskTick -and $_.tickMs -ge $taskCapture.apiBeginMs -and $_.tickMs -le $taskCapture.apiEndMs })
 Check ($taskCapture.name+'.independent-PNG-and-real-target-paint') ($taskValid -and $taskTick -eq $taskCapture.counter -and $taskCapture.error -eq 0 -and $taskPaints.Count -gt 0)
}
if($Scenario -eq 'ret-probe'){
 Check 'minimal-failure-independent-of-input-hooks-and-capture' (@($taskEvents | Where-Object type -in 'input_processed','capture').Count -eq 0)
}
if($Scenario -eq 'all'){
 & (Join-Path $PSScriptRoot '../r3-004/test.ps1') -OutDir $R304OutDir -ResultsDir (Join-Path $ResultsDir 'r304-regression') | Tee-Object -FilePath (Join-Path $ResultsDir 'r304-regression-console.log')
 $taskRegression=Get-Content -LiteralPath (Join-Path $ResultsDir 'r304-regression/summary.json') -Raw | ConvertFrom-Json
 Check 'unmodified-R3-004-and-nested-keyboard-regressions' ($taskRegression.allEvidenceChecksPassed -and $taskRegression.processExit -eq 0)
}
$taskCreated=@($taskEvents | Where-Object type -eq 'process_created');$taskExited=@($taskEvents | Where-Object type -eq 'process_exited')
Check 'all-owned-process-exits-confirmed' ($taskCreated.Count -eq $taskExited.Count -and @($taskCreated | Where-Object { $_.pid -notin $taskExited.pid }).Count -eq 0)
$taskDesktops=@($taskEvents | Where-Object type -eq 'desktop_created');$taskClosed=@($taskEvents | Where-Object type -eq 'desktop_closed')
Check 'all-owned-desktops-closed' ($taskDesktops.Count -eq $taskClosed.Count -and @($taskClosed | Where-Object closed -ne $true).Count -eq 0)
Check 'no-cleanup-errors' (@($taskEvents | Where-Object type -eq 'cleanup_error').Count -eq 0)
$taskOldDiff=@(& git -C $PSScriptRoot diff 64c2c88 -- ../r1 ../r2 ../r3 ../r3-002 ../r3-003 ../r3-004 ../common)
Check 'original-experiments-and-evidence-unchanged' ($taskOldDiff.Count -eq 0)
$taskForbidden=foreach($taskFile in $taskSources | Where-Object {$_ -match '\.(cpp|hpp)$'}){
 Select-String -LiteralPath (Join-Path $PSScriptRoot $taskFile) -Pattern '\b(SendInput|SwitchDesktop|SetCursorPos|SetForegroundWindow|AttachThreadInput|SetWindowText[AW]?|SetProcessMitigationPolicy)\s*\(|\bEM_SETSEL\b'
}
Check 'no-host-input-business-setters-or-security-policy-change' (@($taskForbidden).Count -eq 0)
$taskEvidencePassed=@($taskChecks | Where-Object passed -ne $true).Count -eq 0
[ordered]@{scenario=$Scenario;processExit=$taskExit;evidenceChecksPassed=$taskEvidencePassed;experimentPassed=$taskPrimary;checks=$taskChecks
 assertions=@($taskEvents | Where-Object type -eq 'assertion');manualHostTest='not_executed';R1R2Rerun=$false;R3Complete=$false;generalGUIRestore=$false
 components=[ordered]@{rawMemory='readback_verified';rawStack='readback_verified';CPUContext='legacy_fields_readback_verified';executionContinuation=$(if($taskPrimary){'passed'}else{'failed'})
 fixedWindowRecreation='descriptor_and_references_verified';GDI=$(if($taskPrimary){'restored_application_verified'}elseif($Scenario -eq 'ret-probe'){'not_captured'}else{'source_and_runtime_preparation_only'})
 restoredInput=$(if($taskPrimary){'verified'}else{'not_reached'})}
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $ResultsDir 'summary.json') -Encoding utf8
if(-not $taskEvidencePassed){throw 'R3-005a diagnostic evidence checks failed; retain logs.'}
if(-not $taskPrimary){Write-Output 'R3_005A_RESTORE_FAILED (diagnostic evidence is consistent; GUI execution restore did not pass)';exit 2}
Write-Output 'R3_005A_CONTROLLED_RESTORE_PASSED (only this declared managed GUI boundary)'
