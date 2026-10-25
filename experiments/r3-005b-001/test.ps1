param([string]$OutDir=(Join-Path $PSScriptRoot 'out'),[string]$ResultsDir=(Join-Path $PSScriptRoot ('results/'+(Get-Date -Format 'yyyyMMdd-HHmmss'))),[ValidateRange(1,4)][int]$Repeats=2,[switch]$SkipRegression)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition 'public static class R305bReceipt { public static ulong Compute(ulong r,ulong l,ulong m,ulong o,ulong seed) { unchecked { ulong v=(r^l)+m; return ((v<<13)|(v>>51))^o^seed; } } }'
$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(Test-Path -LiteralPath $ResultsDir){throw 'Use a new ResultsDir; preserve earlier evidence.'}
New-Item -ItemType Directory -Path $ResultsDir | Out-Null
$checks=[Collections.Generic.List[object]]::new();$cases=[Collections.Generic.List[object]]::new()
function Check([string]$Name,[bool]$Okay){$checks.Add([pscustomobject]@{name=$Name;passed=$Okay});Write-Output "$Name=$Okay"}
function Lines([string]$Path){@(Get-Content -LiteralPath $Path -Encoding utf8 | Where-Object {$_} | ForEach-Object {$_ | ConvertFrom-Json})}
function Bytes([string]$Path){,[IO.File]::ReadAllBytes($Path)}
function U64([byte[]]$Data,[long]$Offset){[BitConverter]::ToUInt64($Data,[int]$Offset)}
function Same([byte[]]$A,[byte[]]$B){[Convert]::ToBase64String($A) -eq [Convert]::ToBase64String($B)}
$exe=Join-Path $OutDir 'r305a.exe'
$inputs=@('controller.cpp','fixture.cpp','resources.cpp','bridge.cpp','protocol.hpp','traps.asm','build.ps1','test.ps1','../r3-004/runtime.cpp','../r3-004/channel.hpp','../r3-004/iat.hpp','../r3-004/input.cpp','../r3-004/observer.cpp','../r3/protocol.hpp','../common/native.hpp','../r1/snapshot.hpp')
$sources=@($inputs|ForEach-Object{[ordered]@{file=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $_)).Hash.ToLowerInvariant()}})
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
[ordered]@{time=(Get-Date -Format o);baseCommit=(& git -C $PSScriptRoot rev-parse HEAD);os=[Environment]::OSVersion.VersionString;architecture=$env:PROCESSOR_ARCHITECTURE;elevated=$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator);sources=$sources;exeSha256=(Get-FileHash $exe).Hash.ToLowerInvariant();runtimeSha256=(Get-FileHash (Join-Path $OutDir 'runtime304.dll')).Hash.ToLowerInvariant();systemSettingsChanged=$false;manualHostTest='not_executed'}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $ResultsDir 'identity.json') -Encoding utf8
foreach($file in @('build.json','build.log','loadconfig.txt')){Copy-Item -LiteralPath (Join-Path $OutDir $file) -Destination (Join-Path $ResultsDir $file)}
Check 'adapter-does-not-empty-source-undo-buffer' (-not ((Get-Content (Join-Path $PSScriptRoot 'resources.cpp') -Raw) -match 'EM_EMPTYUNDOBUFFER|EM_UNDO|WM_UNDO'))
Check 'adapter-is-independent-of-business-text' (-not ((Get-Content (Join-Path $PSScriptRoot 'resources.cpp') -Raw) -match 'BASE-|BCrypt|work->'))
foreach($repeat in 1..$Repeats){foreach($scenario in @('positive','undo','unknown-child','bad-parent','unregistered-ref','omit-text','omit-selection','omit-modify','omit-focus')){
 $label="$scenario-$repeat";$dir=Join-Path $ResultsDir $label
 & $exe --output $dir --scenario $scenario 2>&1|Tee-Object -FilePath (Join-Path $ResultsDir ($label+'-console.log'))
 $exit=$LASTEXITCODE;$rows=Lines (Join-Path $dir 'controller.jsonl');$positive=$scenario -eq 'positive'
 $done=@($rows|Where-Object type -eq 'experiment_complete');$fails=@($rows|Where-Object type -eq 'experiment_failure');$resources=@($rows|Where-Object type -eq 'resource_result')
 Check "$label.actual-verdict-and-error-status" ($done.Count -eq 1 -and $done[0].failedAssertions -eq 0 -and $done[0].primaryRestorePassed -eq $positive -and $exit -eq $(if($positive){0}else{2}))
 foreach($r in $resources){if($r.resources.queryTid){$proc=@($rows|Where-Object{ $_.type -eq 'process_created' -and $_.name -eq $r.view })[0];Check "$label.$($r.phase)-owner-GUI-thread-query" ($r.resources.queryTid -eq $proc.tid)}}
 $policies=@($rows|Where-Object type -eq 'security_policy')
 Check "$label.CET-CFG-stay-enabled" ($policies.Count -ge 1 -and @($policies|Where-Object{($_.shadowPolicy -band 1) -eq 0 -or ($_.cfgPolicy -band 1) -eq 0}).Count -eq 0)
 $s0=@($resources|Where-Object phase -eq 'S0')[0]
 if($scenario -in @('undo','unknown-child')){
  Check "$label.snapshot-not-published" (@($rows|Where-Object type -eq 'snapshot_published').Count -eq 0 -and -not(Test-Path (Join-Path $dir 'S0-data.bin')))
  $after=@($resources|Where-Object phase -eq 'source-after-rejection')[0].resources
  Check "$label.source-resource-state-not-changed-to-force-success" ($s0.resources.nodes[1].text -eq $after.nodes[1].text -and $s0.resources.canUndo -eq $after.canUndo -and $s0.resources.selectionStart -eq $after.selectionStart -and $s0.resources.selectionEnd -eq $after.selectionEnd)
  if($scenario -eq 'undo'){Check "$label.real-undo-history-refused-and-preserved" ($s0.resources.canUndo -eq 1 -and $after.canUndo -eq 1 -and $fails[0].api -eq 'resource.EDIT.undo_history' -and $fails[0].error -eq 50)}
  else{Check "$label.actual-unknown-descendant-refused" ($s0.resources.descendants -eq 3 -and $fails[0].api -eq 'resource.unknown_descendant' -and $fails[0].error -eq 50)}
 }else{
  Check "$label.native-source-state-supported-with-empty-undo" ($s0.error -eq 0 -and $s0.resources.descendants -eq 2 -and $s0.resources.canUndo -eq 0 -and $s0.resources.modified -eq 1 -and $s0.resources.focus -eq 2 -and $s0.resources.selectionStart -eq 0 -and $s0.resources.selectionEnd -eq $s0.resources.nodes[1].text.Length)
  $sourceStop=@($rows|Where-Object{ $_.type -eq 'halt' -and $_.view -eq 'source' -and $_.phase -eq 'checkpoint' })[0]
  Check "$label.resource-capture-before-debug-freeze" ([array]::IndexOf($rows,$s0) -lt [array]::IndexOf($rows,$sourceStop))
  $changed=@($resources|Where-Object phase -eq 'source-changed')[0].resources
  Check "$label.source-real-edit-changes-after-S0" ($changed.nodes[1].text -ne $s0.resources.nodes[1].text -and $changed.canUndo -eq 1)
  $dead=@($rows|Where-Object{ $_.type -eq 'process_exited' -and $_.name -eq 'source' })[0];$target=@($rows|Where-Object{ $_.type -eq 'process_created' -and $_.name -eq 'restored' })[0]
  Check "$label.source-exited-before-new-PID-TID" ($dead.pid -ne $target.pid -and [array]::IndexOf($rows,$dead) -lt [array]::IndexOf($rows,$target))
  $prepared=@($resources|Where-Object phase -eq 'target-prepared')[0]
  if($scenario -eq 'bad-parent'){Check "$label.actual-native-parent-mismatch-rejected" ($prepared.resources.nodes[2].parent -eq 2 -and $fails[0].api -eq 'resource.restore.parent_relation' -and $fails[0].error -eq 13)}
  elseif($scenario -like 'omit-*'){
   Check "$label.omission-refused-before-execution" ($fails[0].api -eq 'resource.restore.readback' -and $fails[0].error -eq 23 -and -not $done[0].executionContinuationPassed)
   $a=$prepared.resources;$b=$s0.resources
   $difference=switch($scenario){'omit-text'{$a.nodes[1].text -ne $b.nodes[1].text};'omit-selection'{$a.selectionStart -ne $b.selectionStart -or $a.selectionEnd -ne $b.selectionEnd};'omit-modify'{$a.modified -ne $b.modified};'omit-focus'{$a.focus -ne $b.focus}}
   Check "$label.actual-native-field-mismatch-recorded" ([bool]$difference)
  }else{
   Check "$label.native-resource-preparation-matches-S0" ($prepared.error -eq 0 -and $prepared.resources.nodes[1].text -eq $s0.resources.nodes[1].text -and $prepared.resources.selectionStart -eq $s0.resources.selectionStart -and $prepared.resources.selectionEnd -eq $s0.resources.selectionEnd -and $prepared.resources.modified -eq $s0.resources.modified -and $prepared.resources.canUndo -eq 0 -and $prepared.resources.focus -eq 2 -and $prepared.resources.caretX -eq $s0.resources.caretX -and $prepared.resources.caretY -eq $s0.resources.caretY)
   foreach($i in 0..2){$a=$s0.resources.nodes[$i];$b=$prepared.resources.nodes[$i];Check "$label.window-$($i+1)-identity-geometry-class" ($a.logical -eq $b.logical -and $a.parent -eq $b.parent -and $a.id -eq $b.id -and $a.class -eq $b.class -and $a.style -eq $b.style -and $a.exStyle -eq $b.exStyle -and $a.left -eq $b.left -and $a.top -eq $b.top -and $a.right -eq $b.right -and $a.bottom -eq $b.bottom -and $a.hwnd -ne $b.hwnd -and $a.pid -ne $b.pid -and $a.tid -ne $b.tid)}
   $targetStop=@($rows|Where-Object{ $_.type -eq 'halt' -and $_.view -eq 'restored' -and $_.phase -eq 'checkpoint' })[0]
   Check "$label.native-preparation-precedes-CET-chain-checkpoint" ([array]::IndexOf($rows,$prepared) -lt [array]::IndexOf($rows,$targetStop))
   if($scenario -eq 'unregistered-ref'){$registry=@($rows|Where-Object type -eq 'reference_registry')[0];Check "$label.declared-live-reference-omission-refused" ($registry.sourceTracked -eq 7 -and $registry.targetRequired -eq 7 -and $registry.targetTracked -eq 6 -and $fails[0].api -eq 'restore.unregistered_live_HWND' -and $fails[0].error -eq 13)}
  }
 }
 if($positive){
  $snap=@($rows|Where-Object type -eq 'snapshot')[0];$layout=@($rows|Where-Object type -eq 'raw_layout')[0]
  $stack=Bytes (Join-Path $dir 'S0-active-stack.bin');$data=Bytes (Join-Path $dir 'S0-data.bin');$cpu=Bytes (Join-Path $dir 'S0-context.bin')
  $locals=@($snap.localAddresses|ForEach-Object{U64 $stack ($_-$snap.rsp)});$expected=[R305bReceipt]::Compute((U64 $cpu $layout.r10Offset),$locals[0],$locals[1],$locals[2],(U64 $data 0))
  $result=@($rows|Where-Object type -eq 'execution_result')[0].work;$later=@($rows|Where-Object type -eq 'source_changed')[0].work
  Check "$label.raw-memory-stack-register-computation-and-original-S0" ($expected -eq $snap.expectedReceipt -and $result.receipt -eq $expected -and $result.returned -eq 7 -and $result.text -eq $snap.work.text -and $result.counter -eq $snap.work.counter -and $later.text -ne $result.text -and $later.counter -gt $result.counter)
  $chains=@($rows|Where-Object type -eq 'call_chain')
  Check "$label.normal-and-CET-shadow-returns-match-before-and-after-restore" ($chains.Count -eq 15 -and @($chains|Where-Object{$_.normalReturn -ne $_.shadowReturn}).Count -eq 0)
  $effects=@($rows|Where-Object type -eq 'business_effect');Check "$label.target-does-not-replay-business" ($effects.Count -eq 4 -and @($effects|Where-Object view -ne 'source').Count -eq 0)
  $sourceStack=[byte[]]$stack.Clone();$sourceData=[byte[]]$data.Clone();$refs=@($rows|Where-Object type -eq 'resource_rebind')
  foreach($ref in $refs){if($ref.address -ge $layout.dataAddress -and $ref.address -lt $layout.dataAddress+$data.Length){$arr=$sourceData;$offset=[int]($ref.address-$layout.dataAddress)}else{$arr=$sourceStack;$offset=[int]($ref.address-$snap.rsp)};Check "$label.reference-$($ref.address)-source-preimage" ((U64 $arr $offset) -eq $ref.oldHwnd);[array]::Copy([BitConverter]::GetBytes([ulong]$ref.newHwnd),0,$arr,$offset,8)}
  Check "$label.only-seven-declared-HWND-relocations" ($refs.Count -eq 7 -and (Same $sourceData (Bytes (Join-Path $dir 'target-data-before-continue.bin'))) -and (Same $sourceStack (Bytes (Join-Path $dir 'target-active-stack-before-continue.bin'))))
  $resumed=@($resources|Where-Object phase -eq 'restored-S0')[0].resources;$selected=@($resources|Where-Object phase -eq 'selected-after-restore')[0].resources;$caretAtHome=@($resources|Where-Object phase -eq 'after-home')[0].resources;$final=@($resources|Where-Object phase -eq 'after-input')[0].resources
  Check "$label.native-state-after-CPU-return-still-S0" ($resumed.nodes[1].text -eq $s0.resources.nodes[1].text -and $resumed.selectionEnd -eq $s0.resources.selectionEnd -and $resumed.modified -eq 1 -and $resumed.canUndo -eq 0 -and $resumed.focus -eq 2)
  Check "$label.native-selection-edit-and-caret-after-restore" ($selected.selectionStart -eq 0 -and $selected.selectionEnd -eq $selected.nodes[1].text.Length -and $caretAtHome.selectionStart -eq 0 -and $caretAtHome.selectionEnd -eq 0 -and $final.nodes[1].text -eq 'Z' -and $final.selectionStart -eq 1 -and $final.selectionEnd -eq 1 -and $final.canUndo -eq 1)
  $processed=@((Lines (Join-Path $dir 'restored-events.jsonl'))|Where-Object event -eq 'native_edit_processed')
  Check "$label.original-control-processes-key-and-char-messages" (@($processed|Where-Object message -eq 256).Count -gt 0 -and @($processed|Where-Object message -eq 258).Count -gt 0)
  foreach($cap in @($rows|Where-Object type -eq 'capture')){
   $bmp=[Drawing.Bitmap]::new((Join-Path $dir ($cap.name+'.png')));$tick=0;$valid=$true
   try{foreach($bit in 0..15){$p=$bmp.GetPixel(($cap.clientDx+28+10*$bit),($cap.clientDy+356));$white=$p.R -gt 240 -and $p.G -gt 240 -and $p.B -gt 240;$black=$p.R -lt 15 -and $p.G -lt 15 -and $p.B -lt 15;if(-not($white -or $black)){$valid=$false};if($white){$tick=$tick -bor (1 -shl $bit)}}}finally{$bmp.Dispose()}
   $paint=@((Lines (Join-Path $dir ($cap.view+'-events.jsonl')))|Where-Object{$_.event -in @('wm_paint_end','wm_printclient') -and $_.state.lastDrawUpdate -eq $tick -and $_.tickMs -ge $cap.apiBeginMs -and $_.tickMs -le $cap.apiEndMs})
   Check "$label.$($cap.name)-native-paint-and-PNG" ($valid -and $tick -eq $cap.counter -and $cap.error -eq 0 -and $paint.Count -gt 0)
  }
  $caps=@($rows|Where-Object type -eq 'capture');$before=@($caps|Where-Object name -eq 'source-S0')[0];$after=@($caps|Where-Object name -eq 'restored-S0')[0]
  $a=[Drawing.Bitmap]::new((Join-Path $dir 'source-S0.png'));$b=[Drawing.Bitmap]::new((Join-Path $dir 'restored-S0.png'));$edit=$s0.resources.nodes[1];$same=0;$total=0;$ink=0;$buttonInk=0
  try{for($y=$edit.top+4;$y -lt $edit.bottom-4;++$y){for($x=$edit.left+4;$x -lt $edit.right-4;++$x){if([Math]::Abs($x-($edit.left+$s0.resources.caretX)) -le 2){continue};$pa=$a.GetPixel(($before.clientDx+$x),($before.clientDy+$y));$pb=$b.GetPixel(($after.clientDx+$x),($after.clientDy+$y));++$total;if($pa.ToArgb() -eq $pb.ToArgb()){++$same};if($pb.R -lt 80 -or $pb.G -lt 80 -or $pb.B -lt 80){++$ink}}}
   $button=$s0.resources.nodes[2];for($y=$button.top+10;$y -lt $button.bottom-10;++$y){for($x=$button.left+15;$x -lt $button.right-15;++$x){$p=$b.GetPixel(($after.clientDx+$x),($after.clientDy+$y));if($p.R -lt 80 -and $p.G -lt 80 -and $p.B -lt 80){++$buttonInk}}}
  }finally{$a.Dispose();$b.Dispose()}
  [ordered]@{samePixels=$same;pixels=$total;ratio=$same/[double]$total;editInk=$ink;buttonCaptionInk=$buttonInk;caretColumnExcluded=$true}|ConvertTo-Json|Set-Content (Join-Path $dir 'native-control-pixels.json') -Encoding utf8
  Check "$label.real-EDIT-region-restored-and-button-caption-painted" ($total -gt 0 -and $same/[double]$total -gt 0.995 -and $ink -gt 100 -and $buttonInk -gt 20)
  $pub=@($rows|Where-Object type -eq 'view_published');Check "$label.publish-only-after-control-execution-input-validation" ($pub.Count -eq 1 -and $pub[0].resourcesAndExecutionAndInputVerified -and $done[0].controlResourceReconstructionPassed -and $done[0].executionContinuationPassed -and $done[0].postRestoreInputPassed)
 }else{Check "$label.failed-case-not-published" (@($rows|Where-Object type -eq 'view_published').Count -eq 0)}
 $created=@($rows|Where-Object type -eq 'process_created');$exited=@($rows|Where-Object type -eq 'process_exited');$opened=@($rows|Where-Object type -eq 'desktop_created');$closed=@($rows|Where-Object type -eq 'desktop_closed')
 Check "$label.owned-process-and-desktop-cleanup" ($created.Count -eq $exited.Count -and @($created|Where-Object{$p=$_;@($exited|Where-Object pid -eq $p.pid).Count -ne 1}).Count -eq 0 -and $opened.Count -eq $closed.Count -and @($closed|Where-Object{-not $_.closed}).Count -eq 0 -and @($rows|Where-Object type -eq 'cleanup_error').Count -eq 0)
 $cases.Add([pscustomobject]@{scenario=$scenario;repeat=$repeat;processExit=$exit;controlResourcesPassed=$done[0].controlResourceReconstructionPassed;executionPassed=$done[0].executionContinuationPassed;declaredEDITIntegrationPassed=$done[0].primaryRestorePassed;expectedFailure=(-not $positive)})
}}
$regression='not_run'
if(-not $SkipRegression){
 try{& (Join-Path $PSScriptRoot '../r3-005a-cet-002/test.ps1') -ResultsDir (Join-Path $ResultsDir 'cet002-regression')|Tee-Object -FilePath (Join-Path $ResultsDir 'cet002-regression-console.log');$r=Get-Content (Join-Path $ResultsDir 'cet002-regression/summary.json') -Raw|ConvertFrom-Json;$regression=if($r.allEvidenceChecksPassed){'passed'}else{'failed'}}catch{$regression='failed';$_|Out-String|Set-Content (Join-Path $ResultsDir 'cet002-regression-error.txt')}
 Check 'unchanged-CET002-full-regression' ($regression -eq 'passed')
}
$old=@(& git -C $PSScriptRoot diff 759326d -- '../r1' '../r2' '../r3' '../r3-002' '../r3-003' '../r3-004' '../r3-005a' '../r3-005a-cet-001' '../r3-005a-cet-002' '../common');Check 'old-experiments-and-original-evidence-unchanged' ($old.Count -eq 0)
$okay=$cases.Count -eq 9*$Repeats -and @($checks|Where-Object{-not $_.passed}).Count -eq 0
[ordered]@{allEvidenceChecksPassed=$okay;declaredEDITResourceReconstructionPassed=$okay;controlledExecutionContinuationPassed=$okay;declaredGUIIntegrationPassed=$okay;generalGUIRestore=$false;R3Complete=$false;R4Started=$false;checks=$checks;cases=$cases;CET002Regression=$regression;manualHostTest='not_executed'}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $ResultsDir 'summary.json') -Encoding utf8
if(-not $okay){throw 'R3-005b-001 evidence mismatch; preserve raw logs and images.'}
Write-Output 'R3_005B_001_DECLARED_NATIVE_EDIT_RESOURCE_AND_EXECUTION_INTEGRATION_PASSED'
