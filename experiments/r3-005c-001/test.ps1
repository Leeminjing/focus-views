param(
 [string]$OutDir=(Join-Path $PSScriptRoot 'out'),
 [string]$ResultsDir=(Join-Path $PSScriptRoot ('results/'+(Get-Date -Format 'yyyyMMdd-HHmmss'))),
 [ValidateRange(1,4)][int]$Repeats=2,
 [switch]$SkipRegression
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition 'public static class R305cReceipt { public static ulong Compute(ulong r,ulong l,ulong m,ulong o,ulong seed) { unchecked { ulong v=(r^l)+m; return ((v<<13)|(v>>51))^o^seed; } } }'
$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(Test-Path -LiteralPath $ResultsDir){throw 'Use a new ResultsDir; preserve earlier evidence.'}
New-Item -ItemType Directory -Path $ResultsDir|Out-Null
$checks=[Collections.Generic.List[object]]::new()
$cases=[Collections.Generic.List[object]]::new()
function Check([string]$Name,[bool]$Okay){$checks.Add([pscustomobject]@{name=$Name;passed=$Okay});Write-Output "$Name=$Okay"}
function Lines([string]$Path){@(Get-Content -LiteralPath $Path -Encoding utf8|Where-Object {$_}|ForEach-Object {$_|ConvertFrom-Json})}
function Bytes([string]$Path){,[IO.File]::ReadAllBytes($Path)}
function U64([byte[]]$Data,[long]$Offset){[BitConverter]::ToUInt64($Data,[int]$Offset)}
function Same([byte[]]$A,[byte[]]$B){[Convert]::ToBase64String($A) -eq [Convert]::ToBase64String($B)}
function Stable($A,$B){
 $x=$A.resources;$y=$B.resources
 $x.nodes[1].text -eq $y.nodes[1].text -and $x.selectionStart -eq $y.selectionStart -and $x.selectionEnd -eq $y.selectionEnd -and $x.modified -eq $y.modified -and $x.canUndo -eq $y.canUndo -and $x.focus -eq $y.focus -and $x.nodes[1].hwnd -eq $y.nodes[1].hwnd -and $A.work.text -eq $B.work.text -and $A.work.seed -eq $B.work.seed -and $A.work.receipt -eq $B.work.receipt -and $A.work.clicks -eq $B.work.clicks -and $B.work.counter -ge $A.work.counter -and $A.frozen -eq 0 -and $B.frozen -eq 0 -and -not $A.pending -and -not $B.pending
}
$exe=Join-Path $OutDir 'r305a.exe'
$inputs=@('controller.cpp','fixture.cpp','resources.cpp','capture.cpp','bridge.cpp','protocol.hpp','traps.asm','build.ps1','test.ps1','../r3-004/runtime.cpp','../r3-004/channel.hpp','../r3-004/iat.hpp','../r3-004/input.cpp','../r3-004/observer.cpp','../r3/protocol.hpp','../common/native.hpp','../r1/snapshot.hpp')
$sources=@($inputs|ForEach-Object{[ordered]@{file=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $_)).Hash.ToLowerInvariant()}})
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
[ordered]@{time=(Get-Date -Format o);baseCommit=(& git -C $PSScriptRoot rev-parse HEAD);os=[Environment]::OSVersion.VersionString;architecture=$env:PROCESSOR_ARCHITECTURE;elevated=$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator);sources=$sources;exeSha256=(Get-FileHash $exe).Hash.ToLowerInvariant();runtimeSha256=(Get-FileHash (Join-Path $OutDir 'runtime304.dll')).Hash.ToLowerInvariant();systemSettingsChanged=$false;manualHostTest='not_executed'}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $ResultsDir 'identity.json') -Encoding utf8
foreach($file in @('build.json','build.log','loadconfig.txt')){Copy-Item -LiteralPath (Join-Path $OutDir $file) -Destination (Join-Path $ResultsDir $file)}
foreach($repeat in 1..$Repeats){foreach($scenario in @('AB','ABC','fail-B')){
 $label="$scenario-$repeat";$dir=Join-Path $ResultsDir $label
 & $exe --output $dir --scenario $scenario 2>&1|Tee-Object -FilePath (Join-Path $ResultsDir ($label+'-console.log'))
 $exit=$LASTEXITCODE;$rows=Lines (Join-Path $dir 'controller.jsonl')
 $done=@($rows|Where-Object type -eq 'experiment_complete')
 Check "$label.actual-run-completed" ($exit -eq 0 -and $done.Count -eq 1 -and $done[0].multiViewRestorePassed -and $done[0].failedAssertions -eq 0 -and @($rows|Where-Object type -eq 'experiment_failure').Count -eq 0)
 if($done.Count -ne 1){throw "$label did not complete; preserve raw evidence."}
 if($exit -ne 0 -or -not $done[0].multiViewRestorePassed){$cases.Add([pscustomobject]@{scenario=$scenario;repeat=$repeat;processExit=$exit;multiViewRestorePassed=$false;BFailureIsolated=$done[0].BFailureIsolated});continue}
 $assertions=@($rows|Where-Object type -eq 'assertion')
 Check "$label.all-controller-assertions" ($assertions.Count -gt 20 -and @($assertions|Where-Object{-not $_.passed}).Count -eq 0)
 $resources=@($rows|Where-Object type -eq 'resource_result')
 $s0=@($resources|Where-Object{$_.view -eq 'source' -and $_.phase -eq 'S0'})[0]
 $snap=@($rows|Where-Object type -eq 'snapshot')[0];$layout=@($rows|Where-Object type -eq 'raw_layout')[0]
 $data=Bytes (Join-Path $dir 'S0-data.bin');$stack=Bytes (Join-Path $dir 'S0-active-stack.bin');$cpu=Bytes (Join-Path $dir 'S0-context.bin')
 $locals=@($snap.localAddresses|ForEach-Object{U64 $stack ($_-$snap.rsp)})
 $expected=[R305cReceipt]::Compute((U64 $cpu $layout.r10Offset),$locals[0],$locals[1],$locals[2],(U64 $data 0))
 Check "$label.raw-S0-calculation" ($expected -eq $snap.expectedReceipt)
 Check "$label.only-one-S0-naturally-empty-undo" (@($rows|Where-Object type -eq 'snapshot_published').Count -eq 1 -and $s0.resources.canUndo -eq 0 -and $s0.resources.modified -eq 1 -and $s0.resources.focus -eq 2 -and $s0.resources.selectionStart -eq 0 -and $s0.resources.selectionEnd -eq $s0.resources.nodes[1].text.Length)
 $fp=@($rows|Where-Object type -eq 'snapshot_fingerprint')
 Check "$label.all-S0-fingerprints-unchanged" ($fp.Count -ge 5 -and @($fp.sha256|Select-Object -Unique).Count -eq 1)
 $stream=[IO.MemoryStream]::new()
 try{
  foreach($pair in @(@('memory','S0-data.bin'),@('stack','S0-full-stack.bin'),@('context','S0-context.bin'),@('resources','S0-resources.bin'),@('metadata','S0-metadata.bin'),@('returns','S0-returns.bin'),@('abi','S0-abi.bin'),@('executable','S0-executable.bin'))){
   $path=Join-Path $dir $pair[1];$hash=(Get-FileHash $path).Hash.ToLowerInvariant()
   Check "$label.fingerprint-$($pair[0])-matches-original-bytes" ($hash -eq $fp[0].($pair[0]))
   $raw=Bytes $path;$stream.Write($raw,0,$raw.Length)
  }
  $sha=[Security.Cryptography.SHA256]::Create()
  try{$hash=[BitConverter]::ToString($sha.ComputeHash($stream.ToArray())).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
  Check "$label.whole-S0-fingerprint-independently-recomputed" ($hash -eq $fp[0].sha256)
 }finally{$stream.Dispose()}
 $created=@($rows|Where-Object type -eq 'process_created');$exited=@($rows|Where-Object type -eq 'process_exited')
 $sourceExit=@($exited|Where-Object name -eq 'source')[0]
 $firstA=@($created|Where-Object name -eq 'A')[0]
 Check "$label.source-exit-before-new-A" ([array]::IndexOf($rows,$sourceExit) -lt [array]::IndexOf($rows,$firstA) -and $sourceExit.pid -ne $firstA.pid)
 $effects=@($rows|Where-Object type -eq 'business_effect')
 Check "$label.no-target-initialization-or-business-replay" ($effects.Count -eq 4 -and @($effects|Where-Object view -ne 'source').Count -eq 0)
 $routes=@($rows|Where-Object type -eq 'debug_route')
 Check "$label.central-pump-exact-PID-routing" ($routes.Count -gt 50 -and @($routes|Where-Object{$_.eventPid -ne $_.recipientPid}).Count -eq 0 -and @($routes.ownerThread|Select-Object -Unique).Count -eq 1)
 $foreign=@($rows|Where-Object type -eq 'foreign_event_rejected')
 Check "$label.real-B-or-C-event-not-consumed-by-A" ($foreign.Count -ge 1 -and @($foreign|Where-Object{$_.eventPid -eq $_.intendedPid -or $_.error -ne 1307 -or -not $_.eventPreserved}).Count -eq 0)
 $published=@($rows|Where-Object type -eq 'view_published')
 $views=if($scenario -eq 'AB'){@('A','B')}elseif($scenario -eq 'ABC'){@('A','B','C')}else{@('A','C')}
 Check "$label.only-verified-Views-published" ($published.Count -eq $views.Count -and @($published|Where-Object{-not $_.resourceExecutionFrameAndInputChannelVerified -or $_.view -notin $views}).Count -eq 0)
 $policies=@($rows|Where-Object type -eq 'security_policy')
 Check "$label.actual-CET-CFG-enabled" (@($policies|Where-Object{($_.shadowPolicy -band 1) -eq 0 -or ($_.cfgPolicy -band 1) -eq 0}).Count -eq 0)
 foreach($r in $resources){$owner=@($created|Where-Object name -eq $r.view)[0];Check "$label.$($r.view)-$($r.phase)-native-owner-thread" ($r.resources.queryTid -eq $owner.tid)}
 foreach($view in $views){
  $vroot=Join-Path $dir $view
  $vcreated=@($created|Where-Object name -eq $view)[0]
  $prepared=@($resources|Where-Object{$_.view -eq $view -and $_.phase -eq 'prepared'})[0]
  $initial=@($resources|Where-Object{$_.view -eq $view -and $_.phase -eq 'initial'})[0].resources
  Check "$label.$view-restored-native-control-state" ($prepared.error -eq 0 -and $initial.nodes[1].text -eq $s0.resources.nodes[1].text -and $initial.selectionStart -eq 0 -and $initial.selectionEnd -eq $s0.resources.selectionEnd -and $initial.modified -eq 1 -and $initial.focus -eq 2 -and $initial.canUndo -eq 0 -and $initial.caretX -eq $s0.resources.caretX -and $initial.caretY -eq $s0.resources.caretY)
  foreach($i in 0..2){$x=$s0.resources.nodes[$i];$y=$initial.nodes[$i];Check "$label.$view-window-$i-native-identity" ($y.hwnd -ne $x.hwnd -and $y.pid -eq $vcreated.pid -and $y.tid -eq $vcreated.tid -and $y.parent -eq $x.parent -and $y.id -eq $x.id -and $y.class -eq $x.class -and $y.style -eq $x.style -and $y.exStyle -eq $x.exStyle -and $y.left -eq $x.left -and $y.top -eq $x.top -and $y.right -eq $x.right -and $y.bottom -eq $x.bottom)}
  $chain=@($rows|Where-Object{$_.type -eq 'call_chain' -and $_.view -eq $view})
  Check "$label.$view-target-own-CET-chain-before-and-after" ($chain.Count -eq 10 -and @($chain|Where-Object{$_.normalReturn -ne $_.shadowReturn}).Count -eq 0)
  $match=@($rows|Where-Object{$_.type -eq 'chain_match' -and $_.view -eq $view})[0]
  Check "$label.$view-normal-CALL-not-source-SSP" ($match.matched -and $match.sourceSsp -ne $match.targetSsp -and $match.sourceRsp -eq $match.targetRsp -and $match.initializations -eq 0 -and $match.businessPrefixes -eq 0)
  $receipt=@($rows|Where-Object{$_.type -eq 'execution_result' -and $_.view -eq $view})[0].work
  Check "$label.$view-real-restored-computation" ($receipt.receipt -eq $expected -and $receipt.returned -eq 7 -and $receipt.seed -eq $snap.work.seed -and $receipt.counter -eq $snap.work.counter -and $receipt.text -eq $snap.work.text)
  $cloneData=[byte[]]$data.Clone();$cloneStack=[byte[]]$stack.Clone()
  $refs=@($rows|Where-Object{$_.type -eq 'resource_rebind' -and $_.view -eq $view})
  foreach($ref in $refs){
   if($ref.address -ge $layout.dataAddress -and $ref.address -lt $layout.dataAddress+$data.Length){$arr=$cloneData;$offset=[int]($ref.address-$layout.dataAddress)}else{$arr=$cloneStack;$offset=[int]($ref.address-$snap.rsp)}
   Check "$label.$view-ref-$($ref.address)-old-value" ((U64 $arr $offset) -eq $ref.oldHwnd)
   [array]::Copy([BitConverter]::GetBytes([ulong]$ref.newHwnd),0,$arr,$offset,8)
  }
  Check "$label.$view-only-seven-declared-HWND-relocations" ($refs.Count -eq 7 -and (Same $cloneData (Bytes (Join-Path $vroot 'target-data-before-continue.bin'))) -and (Same $cloneStack (Bytes (Join-Path $vroot 'target-active-stack-before-continue.bin'))))
  $events=Lines (Join-Path $vroot 'events.jsonl')
  Check "$label.$view-original-EDIT-actually-processes-input" (@($events|Where-Object{$_.event -eq 'native_edit_processed' -and $_.message -eq 258}).Count -gt 0)
  $final=@($resources|Where-Object{$_.view -eq $view -and $_.phase -eq 'final'})[0].resources
  $expectedText=if($view -eq 'A'){if($scenario -eq 'AB'){'A1'}elseif($scenario -eq 'ABC'){'A13'}else{'A3'}}elseif($view -eq 'B'){'B2'+$(if($scenario -eq 'ABC'){'4'}else{''})}else{'C'}
  Check "$label.$view-final-native-edit-and-new-undo" ($final.nodes[1].text -eq $expectedText -and $final.selectionStart -eq $expectedText.Length -and $final.selectionEnd -eq $expectedText.Length -and $final.canUndo -eq 1 -and $final.focus -eq 2)
 }
 $observed=@($rows|Where-Object type -eq 'branch_observed')
 $pairs=@()
 if($scenario -eq 'fail-B'){$pairs+=,@('A','before-B-failure','after-B-failure-preserved')}
 else{$pairs=@(@('B','before-A','after-A'),@('A','before-B','after-B'),@('B','before-A-again','after-A-again'),@('A','before-B-again','after-B-again'))}
 if($scenario -ne 'AB'){$pairs+=,@('A','before-C','after-C');$pairs+=,@('C','before-final-A','after-final-A');$pairs+=,@('A','before-C-edit','after-C-edit');if($scenario -eq 'ABC'){$pairs+=,@('B','before-C','after-C');$pairs+=,@('A','before-final-B','after-final-B');$pairs+=,@('B','before-C-edit','after-C-edit')}}
 foreach($pair in $pairs){$x=@($observed|Where-Object{$_.view -eq $pair[0] -and $_.phase -eq $pair[1]})[0];$y=@($observed|Where-Object{$_.view -eq $pair[0] -and $_.phase -eq $pair[2]})[0];Check "$label.$($pair[0])-$($pair[1])-to-$($pair[2])-isolated" (Stable $x $y)}
 $live=@($rows|Where-Object type -eq 'simultaneously_running')
 foreach($row in $live){Check "$label.$($row.phase)-distinct-PID-TID-Desktop" (@($row.views.pid|Select-Object -Unique).Count -eq $row.views.Count -and @($row.views.tid|Select-Object -Unique).Count -eq $row.views.Count -and @($row.views.desktop|Select-Object -Unique).Count -eq $row.views.Count)}
 if($scenario -ne 'AB'){
  $ccreated=@($created|Where-Object name -eq 'C')[0]
  foreach($view in $(if($scenario -eq 'ABC'){@('A','B')}else{@('A')})){
   $x=@($observed|Where-Object{$_.view -eq $view -and $_.phase -eq 'before-C'})[0];$y=@($observed|Where-Object{$_.view -eq $view -and $_.phase -eq 'after-C'})[0]
   Check "$label.$view-not-paused-during-C-creation" ($y.work.counter -gt $x.work.counter -and [array]::IndexOf($rows,$x) -lt [array]::IndexOf($rows,$ccreated) -and [array]::IndexOf($rows,$ccreated) -lt [array]::IndexOf($rows,$y))
  }
 }
 if($scenario -eq 'fail-B'){
  $failure=@($rows|Where-Object type -eq 'branch_creation_failed')[0]
  $bad=@($resources|Where-Object{$_.view -eq 'B' -and $_.phase -eq 'prepared'})[0]
  $bExit=@($exited|Where-Object name -eq 'B')[0]
  Check "$label.B-actual-resource-failure-not-published" ($failure.api -eq 'resource.restore.parent_relation' -and $failure.error -eq 13 -and -not $failure.published -and $bad.resources.nodes[2].parent -eq 2 -and @($published|Where-Object view -eq 'B').Count -eq 0 -and @($rows|Where-Object{$_.type -eq 'execution_result' -and $_.view -eq 'B'}).Count -eq 0)
  Check "$label.B-cleaned-before-C-and-A-keeps-working" ([array]::IndexOf($rows,$bExit) -lt [array]::IndexOf($rows,$ccreated) -and $done[0].BFailureIsolated)
 }
 $caps=@($rows|Where-Object type -eq 'capture')
 foreach($cap in $caps){
  $bmp=[Drawing.Bitmap]::new((Join-Path $dir $cap.file));$tick=0;$valid=$true
  try{foreach($bit in 0..15){$p=$bmp.GetPixel(($cap.clientDx+28+10*$bit),($cap.clientDy+356));$white=$p.R -gt 240 -and $p.G -gt 240 -and $p.B -gt 240;$black=$p.R -lt 15 -and $p.G -lt 15 -and $p.B -lt 15;if(-not($white -or $black)){$valid=$false};if($white){$tick=$tick -bor (1 -shl $bit)}}}finally{$bmp.Dispose()}
  $paint=@((Lines (Join-Path $dir ($cap.view+'/events.jsonl')))|Where-Object{$_.event -in @('wm_paint_end','wm_printclient') -and $_.state.lastDrawUpdate -eq $tick -and $_.tickMs -ge $cap.apiBeginMs -and $_.tickMs -le $cap.apiEndMs})
  Check "$label.$($cap.view)-$($cap.name)-real-owned-frame" ($valid -and $tick -eq $cap.counter -and $cap.error -eq 0 -and $paint.Count -gt 0 -and $cap.file.StartsWith($cap.view+'/'))
 }
 foreach($view in $views){
  $before=@($caps|Where-Object{$_.view -eq 'source' -and $_.name -eq 'S0'})[0]
  $after=@($caps|Where-Object{$_.view -eq $view -and $_.name -eq 'initial'})[0]
  $a=[Drawing.Bitmap]::new((Join-Path $dir $before.file));$b=[Drawing.Bitmap]::new((Join-Path $dir $after.file));$edit=$s0.resources.nodes[1];$same=0;$total=0;$ink=0
  try{for($y=$edit.top+4;$y -lt $edit.bottom-4;++$y){for($x=$edit.left+4;$x -lt $edit.right-4;++$x){if([Math]::Abs($x-($edit.left+$s0.resources.caretX)) -le 2){continue};$pa=$a.GetPixel(($before.clientDx+$x),($before.clientDy+$y));$pb=$b.GetPixel(($after.clientDx+$x),($after.clientDy+$y));++$total;if($pa.ToArgb() -eq $pb.ToArgb()){++$same};if($pb.R -lt 80 -or $pb.G -lt 80 -or $pb.B -lt 80){++$ink}}}}finally{$a.Dispose();$b.Dispose()}
  [ordered]@{samePixels=$same;pixels=$total;ratio=$same/[double]$total;editInk=$ink;caretColumnExcluded=$true}|ConvertTo-Json|Set-Content (Join-Path $dir ($view+'/initial-control-pixels.json')) -Encoding utf8
  Check "$label.$view-real-initial-EDIT-pixels-match-S0" ($total -gt 0 -and $same/[double]$total -gt 0.995 -and $ink -gt 100)
 }
 $opened=@($rows|Where-Object type -eq 'desktop_created');$closed=@($rows|Where-Object type -eq 'desktop_closed')
 Check "$label.owned-processes-and-desktops-cleaned" ($created.Count -eq $exited.Count -and @($created|Where-Object{$p=$_;@($exited|Where-Object pid -eq $p.pid).Count -ne 1}).Count -eq 0 -and $opened.Count -eq $closed.Count -and @($closed|Where-Object{-not $_.closed}).Count -eq 0 -and @($rows|Where-Object type -eq 'cleanup_error').Count -eq 0)
 Check "$label.no-SA-SB-or-R4-claim" (-not $done[0].SA_SB_restore -and -not $done[0].R3Complete -and -not $done[0].R4Started)
 $cases.Add([pscustomobject]@{scenario=$scenario;repeat=$repeat;processExit=$exit;multiViewRestorePassed=$done[0].multiViewRestorePassed;BFailureIsolated=$done[0].BFailureIsolated;publishedViews=$views;processes=$created.Count;closedDesktops=$closed.Count})
}}
$regression='not_run'
if(-not $SkipRegression){
 try{
  & (Join-Path $PSScriptRoot '../r3-005b-001/test.ps1') -ResultsDir (Join-Path $ResultsDir 'b001-regression')|Tee-Object -FilePath (Join-Path $ResultsDir 'b001-regression-console.log')
  $r=Get-Content (Join-Path $ResultsDir 'b001-regression/summary.json') -Raw|ConvertFrom-Json
  $regression=if($r.allEvidenceChecksPassed -and $r.CET002Regression -eq 'passed'){'passed'}else{'failed'}
 }catch{$regression='failed';$_|Out-String|Set-Content (Join-Path $ResultsDir 'b001-regression-error.txt')}
 Check 'unchanged-b001-and-CET002-full-regressions' ($regression -eq 'passed')
}
$old=@(& git -C $PSScriptRoot diff b156519 -- '../r1' '../r2' '../r3' '../r3-002' '../r3-003' '../r3-004' '../r3-005a' '../r3-005a-cet-001' '../r3-005a-cet-002' '../r3-005b-001' '../common')
Check 'old-experiments-and-original-evidence-unchanged' ($old.Count -eq 0)
$okay=$cases.Count -eq 3*$Repeats -and @($checks|Where-Object{-not $_.passed}).Count -eq 0
[ordered]@{allEvidenceChecksPassed=$okay;declaredConcurrentGUIViewsPassed=$okay;generalGUIRestore=$false;SA_SB_restore=$false;R3Complete=$false;R4Started=$false;checks=$checks;cases=$cases;B001AndCET002Regression=$regression;manualHostTest='not_executed'}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $ResultsDir 'summary.json') -Encoding utf8
if(-not $okay){throw 'R3-005c-001 evidence mismatch; preserve raw logs and images.'}
Write-Output 'R3_005C_001_DECLARED_CONCURRENT_GUI_VIEWS_PASSED'
