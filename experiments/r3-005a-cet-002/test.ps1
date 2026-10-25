param([string]$OutDir=(Join-Path $PSScriptRoot 'out'),[string]$ResultsDir=(Join-Path $PSScriptRoot ('results/'+(Get-Date -Format 'yyyyMMdd-HHmmss'))),[ValidateRange(1,5)][int]$Repeats=2)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '../r4-001-gate-001/lifetimes.ps1')
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition 'public static class Cet002Receipt { public static ulong Compute(ulong r,ulong l,ulong m,ulong o,ulong seed) { unchecked { ulong v=(r^l)+m; return ((v<<13)|(v>>51))^o^seed; } } }'
$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(Test-Path -LiteralPath $ResultsDir){throw 'Use a new ResultsDir; retain earlier evidence.'}
New-Item -ItemType Directory -Path $ResultsDir | Out-Null
$caseErrors=[Collections.Generic.List[object]]::new()
$checks=[Collections.Generic.List[object]]::new();$cases=[Collections.Generic.List[object]]::new()
function Check([string]$Name,[bool]$Okay){$checks.Add([pscustomobject]@{name=$Name;passed=$Okay});Write-Output "$Name=$Okay"}
function Lines([string]$Path){@(Get-Content -LiteralPath $Path -Encoding utf8 | Where-Object {$_} | ForEach-Object {$_ | ConvertFrom-Json})}
function Bytes([string]$Path){,[IO.File]::ReadAllBytes($Path)}
function U64([byte[]]$Data,[long]$Offset){[BitConverter]::ToUInt64($Data,[int]$Offset)}
function Same([byte[]]$A,[byte[]]$B){[Convert]::ToBase64String($A) -eq [Convert]::ToBase64String($B)}
$exe=Join-Path $OutDir 'r305a.exe'
$inputs=@('controller.cpp','fixture.cpp','protocol.hpp','traps.asm','build.ps1','test.ps1','../r3-004/runtime.cpp','../r3-004/channel.hpp','../r3-004/iat.hpp','../r3-004/input.cpp','../r3-004/observer.cpp','../r3/bridge.cpp','../r3/protocol.hpp','../common/native.hpp','../r1/snapshot.hpp','../r4-001-gate-001/launch-diagnostics.hpp','../r4-001-gate-001/courier.hpp','../r4-001-gate-001/lifetimes.ps1')
$sources=@($inputs | ForEach-Object {[ordered]@{file=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $_)).Hash.ToLowerInvariant()}})
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
[ordered]@{time=(Get-Date -Format o);baseCommit=(& git -C $PSScriptRoot rev-parse HEAD);os=[Environment]::OSVersion.VersionString;architecture=$env:PROCESSOR_ARCHITECTURE;elevated=$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator);sources=$sources;exeSha256=(Get-FileHash $exe).Hash.ToLowerInvariant();runtimeSha256=(Get-FileHash (Join-Path $OutDir 'runtime304.dll')).Hash.ToLowerInvariant();systemSettingsChanged=$false;manualHostTest='not_executed'} | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $ResultsDir 'identity.json') -Encoding utf8
foreach($file in @('build.json','build.log','loadconfig.txt')) {Copy-Item -LiteralPath (Join-Path $OutDir $file) -Destination (Join-Path $ResultsDir $file)}
$assembly=Get-Content (Join-Path $OutDir 'fixture.asm') -Raw
$frameText=([regex]::Matches($assembly,'(?ms)^\?(?:leaf|middle|outer|application|managed_entry)@[^\r\n]* PROC.*?^\?[^\r\n]* ENDP[^\r\n]*') | ForEach-Object Value) -join "`r`n"
$frameText | Set-Content (Join-Path $ResultsDir 'compiled-frames.asm.txt') -Encoding utf8
Check 'compiler-emits-normal-CALL-RET-and-GS-checks' ($frameText -match 'call\s+CheckpointTrap' -and ([regex]::Matches($frameText,'call\s+__security_check_cookie')).Count -ge 3 -and ([regex]::Matches($frameText,'\bret\b')).Count -ge 3)
Check 'PE-contains-CFG-instrumentation-and-function-table' ((Get-Content (Join-Path $OutDir 'loadconfig.txt') -Raw) -match 'CF Instrumented')
foreach($repeat in 1..$Repeats){foreach($scenario in @('core','return-mismatch','stack','register','memory','cookie','gui')){
 $label="$scenario-$repeat";$dir=Join-Path $ResultsDir $label
 $rows=@()
 try{
 & $exe --output $dir --scenario $scenario 2>&1 | Tee-Object -FilePath (Join-Path $ResultsDir ($label+'-console.log'))
 $exit=$LASTEXITCODE;$rows=Lines (Join-Path $dir 'controller.jsonl')
 $negative=$scenario -notin @('core','gui')
 $complete=@($rows | Where-Object type -eq 'experiment_complete')
 Check "$label.verdict-and-exit" ($complete.Count -eq 1 -and $complete[0].failedAssertions -eq 0 -and $complete[0].primaryRestorePassed -eq (-not $negative) -and $exit -eq $(if($negative){2}else{0}))
 if($complete.Count -ne 1){throw 'Controller did not complete; preserve primary error before dependent S0 checks.'}
 $effects=@($rows | Where-Object type -eq 'business_effect')
 Check "$label.independent-business-events-source-only" ($effects.Count -eq 4 -and @($effects | Where-Object view -ne 'source').Count -eq 0 -and $effects[-1].initializations -eq 1 -and $effects[-1].outer -eq 1 -and $effects[-1].middle -eq 1 -and $effects[-1].leaf -eq 1)
 $policies=@($rows | Where-Object type -eq 'security_policy')
 Check "$label.actual-CET-CFG-policies-enabled" ($policies.Count -eq 2 -and @($policies | Where-Object {($_.shadowPolicy -band 1) -eq 0 -or ($_.cfgPolicy -band 1) -eq 0}).Count -eq 0)
 $sourceDead=@($rows | Where-Object { $_.type -eq 'process_exited' -and $_.name -eq 'source' })[0]
 $target=@($rows | Where-Object { $_.type -eq 'process_created' -and $_.name -eq 'restored' })[0]
 Check "$label.source-dead-before-new-PID" ($sourceDead.pid -ne $target.pid -and [array]::IndexOf($rows,$sourceDead) -lt [array]::IndexOf($rows,$target))
 $snapshot=@($rows | Where-Object type -eq 'snapshot')[0];$layout=@($rows | Where-Object type -eq 'raw_layout')[0]
 $srcStack=Bytes (Join-Path $dir 'S0-active-stack.bin');$srcData=Bytes (Join-Path $dir 'S0-data.bin');$srcCpu=Bytes (Join-Path $dir 'S0-context.bin')
 $locals=@($snapshot.localAddresses | ForEach-Object {U64 $srcStack ($_-$snapshot.rsp)})
 $expect=[Cet002Receipt]::Compute((U64 $srcCpu $layout.r10Offset),$locals[0],$locals[1],$locals[2],(U64 $srcData 0))
 $changed=@($rows | Where-Object type -eq 'source_changed')[0].work
 Check "$label.source-receipt-recomputed-from-raw-bytes" ($expect -eq $snapshot.expectedReceipt -and $changed.receipt -eq $expect -and $changed.returned -eq 7 -and $changed.counter -gt $snapshot.work.counter -and $changed.text -ne $snapshot.work.text)
 $chains=@($rows | Where-Object type -eq 'call_chain');$srcChain=@($chains | Where-Object view -eq 'source');$targetChain=@($chains | Where-Object phase -eq 'target-before-restore')
 Check "$label.normal-returns-match-kernel-observed-shadow-returns" ($srcChain.Count -eq 5 -and $targetChain.Count -eq 5 -and @($chains | Where-Object { $_.normalReturn -ne $_.shadowReturn }).Count -eq 0)
 foreach($frame in $srcChain){Check "$label.source-return-slot-$($frame.index)" ((U64 $srcStack ($frame.returnSlot-$snapshot.rsp)) -eq $frame.normalReturn)}
 $match=@($rows | Where-Object type -eq 'chain_match')[0]
 Check "$label.structural-entry-no-business-replay" ($match.initializations -eq 0 -and $match.businessPrefixes -eq 0)
 $result=@($rows | Where-Object type -eq 'execution_result');$fault=@($rows | Where-Object type -eq 'continuation_fault');$failure=@($rows | Where-Object type -eq 'primary_restore_failed')
 if($scenario -eq 'return-mismatch'){
  Check "$label.actual-alternate-callsite-rejected-before-state-write" (-not $match.matched -and $failure.Count -eq 1 -and $failure[0].api -eq 'restore.call_chain_mismatch' -and $failure[0].error -eq 13 -and $result.Count -eq 0 -and -not (Test-Path (Join-Path $dir 'target-context-before-continue.bin')))
 }else{
  Check "$label.same-original-call-sites-and-RSP" ($match.matched -and $match.sourceRsp -eq $match.targetRsp -and (Same ([Text.Encoding]::UTF8.GetBytes(($srcChain.normalReturn -join ','))) ([Text.Encoding]::UTF8.GetBytes(($targetChain.normalReturn -join ',')))))
  $beforeStack=Bytes (Join-Path $dir 'target-before-restore-stack.bin');$beforeData=Bytes (Join-Path $dir 'target-before-restore-data.bin')
  $stagedStack=Bytes (Join-Path $dir 'target-active-stack-before-continue.bin');$stagedData=Bytes (Join-Path $dir 'target-data-before-continue.bin');$stagedCpu=Bytes (Join-Path $dir 'target-context-before-continue.bin')
  Check "$label.target-had-no-saved-business-data-before-restore" ((U64 $beforeData 0) -eq 0 -and (U64 $beforeStack ($snapshot.localAddresses[0]-$snapshot.rsp)) -eq 0 -and (U64 $stagedCpu $layout.rspOffset) -eq $snapshot.rsp)
  $expectedStack=[byte[]]$srcStack.Clone();$expectedData=[byte[]]$srcData.Clone()
  foreach($ref in @($rows | Where-Object type -eq 'resource_rebind')){
   if($ref.address -ge $layout.dataAddress -and $ref.address -lt $layout.dataAddress+$srcData.Length){$a=$expectedData;$offset=[int]($ref.address-$layout.dataAddress)}else{$a=$expectedStack;$offset=[int]($ref.address-$snapshot.rsp)}
   Check "$label.HWND-preimage-$($ref.address)" ((U64 $a $offset) -eq $ref.oldHwnd -and $ref.oldHwnd -ne $ref.newHwnd)
   [Array]::Copy([BitConverter]::GetBytes([ulong]$ref.newHwnd),0,$a,$offset,8)
  }
  if($scenario -eq 'stack'){$offset=[int]($snapshot.localAddresses[0]-$snapshot.rsp);[Array]::Copy($beforeStack,$offset,$expectedStack,$offset,8)}
  if($scenario -eq 'memory'){[Array]::Copy($beforeData,0,$expectedData,0,8)}
  Check "$label.only-declared-relocations-or-explicit-omission-in-raw-state" ((Same $expectedStack $stagedStack) -and (Same $expectedData $stagedData))
  Check "$label.rebuilt-chain-still-valid-after-raw-restore" (@($chains | Where-Object phase -eq 'after-ordinary-context-restore').Count -eq 5)
  if($scenario -eq 'cookie'){
   Check "$label.GS-really-rejects-foreign-cookie" ($fault.Count -eq 1 -and $fault[0].exceptionCode -eq 3221226505 -and $fault[0].parameters[0] -eq 2 -and $result.Count -eq 0)
  }else{
   $stagedLocals=@($snapshot.localAddresses | ForEach-Object {U64 $stagedStack ($_-$snapshot.rsp)})
   $actual=[Cet002Receipt]::Compute((U64 $stagedCpu $layout.r10Offset),$stagedLocals[0],$stagedLocals[1],$stagedLocals[2],(U64 $stagedData 0))
   Check "$label.actual-computation-consumes-restored-memory-locals-register" ($result.Count -eq 1 -and $result[0].work.receipt -eq $actual -and $result[0].work.returned -eq 7 -and $fault.Count -eq 0)
   if($negative){Check "$label.omission-not-masked-by-CET-or-early-fault" ($actual -ne $expect -and $failure.Count -eq 1 -and $failure[0].api -eq 'restore.execution_receipt' -and $failure[0].error -eq 23)}else{Check "$label.real-source-execution-restored" ($actual -eq $expect -and $result[0].work.seed -eq $snapshot.work.seed -and $result[0].work.text -eq $snapshot.work.text)}
  }
 }
 if($negative){Check "$label.failed-case-never-published" (@($rows | Where-Object type -eq 'view_published').Count -eq 0)}
 if($scenario -eq 'gui'){
  foreach($cap in @($rows | Where-Object type -eq 'capture')){
   $bmp=[Drawing.Bitmap]::new((Join-Path $dir ($cap.name+'.png')));$tick=0;$valid=$true
   try{foreach($bit in 0..15){$pixel=$bmp.GetPixel(($cap.clientDx+28+10*$bit),($cap.clientDy+356));$white=$pixel.R -gt 240 -and $pixel.G -gt 240 -and $pixel.B -gt 240;$black=$pixel.R -lt 15 -and $pixel.G -lt 15 -and $pixel.B -lt 15;if(-not($white -or $black)){$valid=$false};if($white){$tick=$tick -bor (1 -shl $bit)}}}finally{$bmp.Dispose()}
   $paints=@((Lines (Join-Path $dir ($cap.view+'-events.jsonl'))) | Where-Object {$_.event -in @('wm_paint_end','wm_printclient') -and $_.state.lastDrawUpdate -eq $tick -and $_.tickMs -ge $cap.apiBeginMs -and $_.tickMs -le $cap.apiEndMs})
   Check "$label.$($cap.name)-real-GDI-and-PNG" ($valid -and $tick -eq $cap.counter -and $cap.error -eq 0 -and $paints.Count -gt 0)
  }
  $published=@($rows | Where-Object type -eq 'view_published');$asserts=@($rows | Where-Object type -eq 'assertion')
  Check "$label.restored-native-window-input-and-rendering" (@($rows | Where-Object type -eq 'resource_rebind').Count -eq 3 -and @($asserts | Where-Object { $_.name -eq 'restored.real-GUI-input' -and $_.passed }).Count -eq 1 -and $published.Count -eq 1 -and $published[0].afterGDIAndInputValidation)
 }elseif($scenario -eq 'core'){Check "$label.minimal-run-no-windows-input-or-capture" (@($rows | Where-Object type -in @('capture','input_processed','resource_rebind')).Count -eq 0)}
 $created=@($rows | Where-Object type -eq 'process_created');$exited=@($rows | Where-Object type -eq 'process_exited');$opened=@($rows | Where-Object type -eq 'desktop_created');$closed=@($rows | Where-Object type -eq 'desktop_closed')
 $cases.Add([pscustomobject]@{scenario=$scenario;repeat=$repeat;processExit=$exit;sourceExecutionRestored=$complete[0].primaryRestorePassed;expectedFailure=$negative})
 }catch{
  $record=[pscustomobject]@{case=$label;error=$_.ToString();controllerLog=(Join-Path $dir 'controller.jsonl');primaryFailures=@($rows|Where-Object type -in @('process_launch_failed','primary_restore_failed'))}
  $caseErrors.Add($record);$record|ConvertTo-Json -Depth 8|Set-Content (Join-Path $ResultsDir ($label+'-evidence-error.json')) -Encoding utf8
  Check "$label.evidence-processing-completed" $false
  $cases.Add([pscustomobject]@{scenario=$scenario;repeat=$repeat;processExit=$exit;evidenceError=$_.ToString();expectedFailure=$false})
 }finally{
  $audit=Test-OwnedLifetimes $rows
  $audit|ConvertTo-Json -Depth 7|Set-Content (Join-Path $dir 'lifetime-audit.json') -Encoding utf8
  Check "$label.cleanup-owned-processes-and-desktops" ($rows.Count -gt 0 -and $audit.passed)
 }
}}
$old=@(& git -C $PSScriptRoot diff 2ec1d2b -- '../r1' '../r2' '../r3' '../r3-002' '../r3-003' '../r3-004' '../r3-005a' '../r3-005a-cet-001' '../common')
Check 'old-experiments-and-evidence-unchanged' ($old.Count -eq 0)
$okay=$cases.Count -eq 7*$Repeats -and @($checks | Where-Object {-not $_.passed}).Count -eq 0
[ordered]@{allEvidenceChecksPassed=$okay;controlledCooperativeContinuationPassed=$okay;controlledGUIIntegrationPassed=$okay;generalGUIRestore=$false;R3Complete=$false;R3005bStarted=$false;R4Started=$false;checks=$checks;cases=$cases;caseProcessingErrors=$caseErrors;manualHostTest='not_executed';oldGateRerun=$false} | ConvertTo-Json -Depth 7 | Set-Content (Join-Path $ResultsDir 'summary.json') -Encoding utf8
if(-not $okay){throw 'CET002 evidence mismatch; preserve failure logs.'}
Write-Output 'CET002_COOPERATIVE_CONTINUATION_PASSED_WITH_DECLARED_COMPILE_TIME_SUPPORT'
