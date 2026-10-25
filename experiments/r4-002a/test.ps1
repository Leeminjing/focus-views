param(
 [string]$OutDir=(Join-Path $PSScriptRoot 'out'),
 [string]$ResultsDir=(Join-Path $PSScriptRoot ('results/'+(Get-Date -Format 'yyyyMMdd-HHmmss'))),
 [ValidateRange(1,3)][int]$Repeats=2,
 [switch]$SkipRegression
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '../r4-001-gate-001/lifetimes.ps1')
Add-Type -AssemblyName System.Drawing
if(-not ('R4002Receipt' -as [type])){Add-Type -TypeDefinition 'public static class R4002Receipt { public static ulong Compute(ulong r,ulong l,ulong m,ulong o,ulong seed) { unchecked { ulong v=(r^l)+m; return ((v<<13)|(v>>51))^o^seed; } } }'}
$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(Test-Path -LiteralPath $ResultsDir){throw 'New ResultsDir required; preserve raw evidence.'}
New-Item -ItemType Directory -Path $ResultsDir | Out-Null
$checks=[Collections.Generic.List[object]]::new();$cases=[Collections.Generic.List[object]]::new();$errors=[Collections.Generic.List[object]]::new()
function Check([string]$Name,[bool]$Okay){$checks.Add([pscustomobject]@{name=$Name;passed=$Okay});Write-Output "$Name=$Okay"}
function Rows([string]$Path){@(Get-Content -LiteralPath $Path -Encoding utf8|Where-Object {$_}|ForEach-Object {$_|ConvertFrom-Json})}
function Bytes([string]$Path){,[IO.File]::ReadAllBytes($Path)}
function U64([byte[]]$Data,[long]$Offset){[BitConverter]::ToUInt64($Data,[int]$Offset)}
function Same([byte[]]$A,[byte[]]$B){[Convert]::ToBase64String($A) -eq [Convert]::ToBase64String($B)}
$exe=Join-Path $OutDir 'r305a.exe'
$inputs=@('controller.cpp','fixture.cpp','resources.cpp','undo.cpp','undo.hpp','undo-probe.cpp','capture.cpp','bridge.cpp','protocol.hpp','traps.asm','build.ps1','test.ps1','../r3-004/runtime.cpp','../r3-004/input.cpp','../r3-004/observer.cpp','../r3-004/iat.hpp','../r3-004/channel.hpp','../r3/protocol.hpp','../common/native.hpp','../r1/snapshot.hpp','../r4-001-gate-001/launch-diagnostics.hpp','../r4-001-gate-001/courier.hpp','../r4-001-gate-001/lifetimes.ps1')
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
[ordered]@{time=(Get-Date -Format o);baseCommit=(& git -C $PSScriptRoot rev-parse HEAD);os=[Environment]::OSVersion.VersionString;architecture=$env:PROCESSOR_ARCHITECTURE;elevated=$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator);systemSettingsChanged=$false;manualHostTest='not_executed';exeSha256=(Get-FileHash $exe).Hash.ToLowerInvariant();runtimeSha256=(Get-FileHash (Join-Path $OutDir 'runtime304.dll')).Hash.ToLowerInvariant();sources=@($inputs|ForEach-Object{[ordered]@{file=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $_)).Hash.ToLowerInvariant()}})}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $ResultsDir 'identity.json') -Encoding utf8
foreach($file in @('build.json','build.log','loadconfig.txt')){Copy-Item -LiteralPath (Join-Path $OutDir $file) -Destination (Join-Path $ResultsDir $file)}
foreach($repeat in 1..$Repeats){
 $probe=Join-Path $ResultsDir "native-undo-$repeat.jsonl"
 try{
  & $exe --role undo-probe --log $probe *> (Join-Path $ResultsDir "native-undo-$repeat-console.log")
  $exit=$LASTEXITCODE;$rows=Rows $probe;$done=@($rows|Where-Object phase -eq 'complete');$compare=@($rows|Where-Object phase -eq 'undo-comparison')
  Check "native-undo-$repeat.real-native-reference" ($exit -eq 0 -and $done.Count -eq 1 -and $done[0].passed -and $done[0].sourceUntouched -and $compare.Count -eq 2 -and @($compare|Where-Object{-not $_.equal}).Count -eq 0)
 }catch{$errors.Add([pscustomobject]@{case="native-undo-$repeat";error=$_.ToString()});Check "native-undo-$repeat.completed" $false}
 foreach($scenario in @('positive','multiple','IME','no-undo','stack','register','memory','bad-chain','unknown-edit','wrong-undo')){
  $label="$scenario-$repeat";$dir=Join-Path $ResultsDir $label;$rows=@();$exit=$null
  try{
   & $exe --output $dir --scenario $scenario *> (Join-Path $ResultsDir "$label-console.log")
   $exit=$LASTEXITCODE;$rows=Rows (Join-Path $dir 'controller.jsonl');$done=@($rows|Where-Object type -eq 'experiment_complete')
   Check "$label.completed" ($exit -eq 0 -and $done.Count -eq 1 -and $done[0].evidencePassed -and @($rows|Where-Object type -eq 'experiment_failure').Count -eq 0)
   if($done.Count -ne 1 -or $exit -ne 0 -or -not $done[0].evidencePassed){throw 'Experiment failed; dependent evidence checks skipped, primary errors preserved.'}
   $assertions=@($rows|Where-Object type -eq 'assertion');Check "$label.controller-assertions" ($assertions.Count -gt 25 -and @($assertions|Where-Object{-not $_.passed}).Count -eq 0)
   $created=@($rows|Where-Object type -eq 'process_created');$exited=@($rows|Where-Object type -eq 'process_exited');$published=@($rows|Where-Object type -eq 'view_published');$resources=@($rows|Where-Object type -eq 'resource_result')
   $policies=@($rows|Where-Object type -eq 'security_policy');Check "$label.CET-CFG-enabled" ($policies.Count -ge 3 -and @($policies|Where-Object{($_.shadowPolicy -band 1) -eq 0 -or ($_.cfgPolicy -band 1) -eq 0}).Count -eq 0)
   $routes=@($rows|Where-Object type -eq 'debug_route');Check "$label.one-pump-exact-routing" ($routes.Count -gt 50 -and @($routes|Where-Object{$_.eventPid -ne $_.recipientPid}).Count -eq 0 -and @($routes.ownerThread|Select-Object -Unique).Count -eq 1)
   foreach($r in $resources){$owner=@($created|Where-Object name -eq $r.view)[0];Check "$label.$($r.view)-$($r.phase)-GUI-owner" ($r.resources.queryTid -eq $owner.tid)}
   $snapshots=@($rows|Where-Object type -eq 'snapshot');$layout=@($rows|Where-Object type -eq 'raw_layout')[0]
   $fingerprints=@($rows|Where-Object type -eq 'snapshot_fingerprint')
   foreach($snap in $snapshots){
    $id=$snap.label;$data=Bytes (Join-Path $dir "$id-data.bin");$stack=Bytes (Join-Path $dir "$id-active-stack.bin");$cpu=Bytes (Join-Path $dir "$id-context.bin")
    $locals=@($snap.localAddresses|ForEach-Object{U64 $stack ($_-$snap.rsp)})
    $computed=[R4002Receipt]::Compute((U64 $cpu $layout.r10Offset),$locals[0],$locals[1],$locals[2],(U64 $data 0))
    Check "$label.$id.raw-computation" ($computed -eq $snap.expectedReceipt)
    $fp=@($fingerprints|Where-Object {$_.phase -eq "$id-published" -or $_.phase -eq "final-$id" -or ($id -eq 'S0' -and $_.phase -eq 'after-rejection')});$first=$fp[0]
    Check "$label.$id.fingerprints-immutable" ($fp.Count -ge 2 -and @($fp.sha256|Select-Object -Unique).Count -eq 1)
    $stream=[IO.MemoryStream]::new()
    try{
     foreach($pair in @(@('memory','data'),@('stack','full-stack'),@('context','context'),@('resources','resources'),@('metadata','metadata'),@('returns','returns'),@('abi','abi'),@('executable','executable'))){$file=Join-Path $dir "$id-$($pair[1]).bin";Check "$label.$id.raw-$($pair[0])-hash" ((Get-FileHash $file).Hash.ToLowerInvariant() -eq $first.($pair[0]));$raw=Bytes $file;$stream.Write($raw,0,$raw.Length)}
     $sha=[Security.Cryptography.SHA256]::Create();try{$hash=[BitConverter]::ToString($sha.ComputeHash($stream.ToArray())).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()};Check "$label.$id.whole-hash" ($hash -eq $first.sha256)
    }finally{$stream.Dispose()}
    foreach($view in $(if($id -eq 'S0'){@('A','B')}elseif($id -eq 'SA'){@('RA')}else{@('RB')})){
     $results=@($rows|Where-Object{$_.type -eq 'execution_result' -and $_.view -eq $view});if(-not $results.Count){continue}
     $v=$results[0];$fault=$v.fault
     Check "$label.$view.execution-not-replayed" ($v.work.returned -eq 7 -and ($v.work.receipt -eq $computed) -eq ($fault -eq ''))
     if($fault -ne ''){continue}
     Check "$label.$view.actual-memory-and-counter" ($v.work.seed -eq $snap.work.seed -and $v.work.counter -eq $snap.work.counter -and $v.work.text -eq $snap.work.text)
     $match=@($rows|Where-Object{$_.type -eq 'chain_match' -and $_.view -eq $view})[0];$chain=@($rows|Where-Object{$_.type -eq 'call_chain' -and $_.view -eq $view -and $_.phase -in @('target-before-restore','after-ordinary-context-restore')})
     Check "$label.$view.target-owned-CET" ($match.matched -and $match.sourceSsp -ne $match.targetSsp -and $match.initializations -eq 0 -and $match.businessPrefixes -eq 0 -and $chain.Count -eq 10 -and @($chain|Where-Object{$_.normalReturn -ne $_.shadowReturn}).Count -eq 0)
     $cloneData=[byte[]]$data.Clone();$cloneStack=[byte[]]$stack.Clone();$refs=@($rows|Where-Object{$_.type -eq 'resource_rebind' -and $_.view -eq $view})
     foreach($ref in $refs){if($ref.address -ge $layout.dataAddress -and $ref.address -lt $layout.dataAddress+$data.Length){$arr=$cloneData;$offset=[int]($ref.address-$layout.dataAddress)}else{$arr=$cloneStack;$offset=[int]($ref.address-$snap.rsp)};Check "$label.$view.declared-HWND-preimage-$offset" ((U64 $arr $offset) -eq $ref.oldHwnd);[array]::Copy([BitConverter]::GetBytes([ulong]$ref.newHwnd),0,$arr,$offset,8)}
     Check "$label.$view.only-declared-relocations" ($refs.Count -eq 7 -and (Same $cloneData (Bytes (Join-Path $dir "$view/target-data-before-continue.bin"))) -and (Same $cloneStack (Bytes (Join-Path $dir "$view/target-active-stack-before-continue.bin"))))
    }
   }
   $effects=@($rows|Where-Object type -eq 'business_effect')
   Check "$label.no-target-initialization" (@($effects|Where-Object{$_.initializations -ne 0 -and $_.view -ne 'source'}).Count -eq 0)
   foreach($cap in @($rows|Where-Object type -eq 'capture')){
    $bmp=[Drawing.Bitmap]::new((Join-Path $dir $cap.file));$tick=0;$valid=$true
    try{foreach($bit in 0..15){$pixel=$bmp.GetPixel(($cap.clientDx+28+10*$bit),($cap.clientDy+356));$white=$pixel.R -gt 240 -and $pixel.G -gt 240 -and $pixel.B -gt 240;$black=$pixel.R -lt 15 -and $pixel.G -lt 15 -and $pixel.B -lt 15;if(-not($white -or $black)){$valid=$false};if($white){$tick=$tick -bor (1 -shl $bit)}}}finally{$bmp.Dispose()}
    $paint=@((Rows (Join-Path $dir "$($cap.view)/events.jsonl"))|Where-Object{$_.event -in @('wm_paint_end','wm_printclient') -and $_.state.lastDrawUpdate -eq $tick -and $_.tickMs -ge $cap.apiBeginMs -and $_.tickMs -le $cap.apiEndMs})
    Check "$label.$($cap.view)-$($cap.name)-fresh-GDI-pixels" ($valid -and $tick -eq $cap.counter -and $cap.error -eq 0 -and $paint.Count -gt 0)
   }
   if($scenario -eq 'positive'){
    Check "$label.only-four-restored-Views" ($published.Count -eq 4 -and $done[0].terminalRestorePassed)
    $sourceExit=@($exited|Where-Object name -eq 'source')[0];$aCreated=@($created|Where-Object name -eq 'A')[0];Check "$label.source-exit-before-restore" ([array]::IndexOf($rows,$sourceExit) -lt [array]::IndexOf($rows,$aCreated))
    foreach($pair in @(@('A','RA','SA'),@('B','RB','SB'))){
     $old=@($created|Where-Object name -eq $pair[0])[0];$terminated=@($exited|Where-Object name -eq $pair[0])[0];$new=@($created|Where-Object name -eq $pair[1])[0]
     Check "$label.$($pair[1]).old-exit-first" ($old.pid -ne $new.pid -and [array]::IndexOf($rows,$terminated) -lt [array]::IndexOf($rows,$new))
     $saved=@($resources|Where-Object{$_.view -eq $pair[0] -and $_.phase -eq $pair[2]})[0].resources;$initial=@($resources|Where-Object{$_.view -eq $pair[1] -and $_.phase -eq 'initial'})[0].resources
     Check "$label.$($pair[1]).native-undo-current-state" ($saved.canUndo -eq 1 -and $saved.undo.valid -eq 1 -and $saved.undo.operations -eq 1 -and $initial.canUndo -eq 1 -and $initial.nodes[1].text -eq $saved.nodes[1].text -and $initial.selectionStart -eq $saved.selectionStart -and $initial.selectionEnd -eq $saved.selectionEnd -and $initial.modified -eq $saved.modified)
     # Compare visible EDIT interiors, excluding the blinking caret column.
     $before=@($rows|Where-Object{$_.type -eq 'capture' -and $_.view -eq $pair[0] -and $_.name -eq $pair[2]})[0];$after=@($rows|Where-Object{$_.type -eq 'capture' -and $_.view -eq $pair[1] -and $_.name -eq 'initial'})[0]
     $a=[Drawing.Bitmap]::new((Join-Path $dir $before.file));$b=[Drawing.Bitmap]::new((Join-Path $dir $after.file));$edit=$saved.nodes[1];$same=0;$total=0;$ink=0
     try{for($y=$edit.top+4;$y -lt $edit.bottom-4;++$y){for($x=$edit.left+4;$x -lt $edit.right-4;++$x){if([Math]::Abs($x-($edit.left+$saved.caretX)) -le 2){continue};$pa=$a.GetPixel(($before.clientDx+$x),($before.clientDy+$y));$pb=$b.GetPixel(($after.clientDx+$x),($after.clientDy+$y));++$total;if($pa.ToArgb() -eq $pb.ToArgb()){++$same};if($pb.R -lt 80 -or $pb.G -lt 80 -or $pb.B -lt 80){++$ink}}}}finally{$a.Dispose();$b.Dispose()}
     [ordered]@{same=$same;total=$total;ratio=$same/[double]$total;ink=$ink}|ConvertTo-Json|Set-Content (Join-Path $dir "$($pair[1])/EDIT-pixels.json")
     Check "$label.$($pair[1]).native-EDIT-pixels" ($total -gt 0 -and $same/[double]$total -gt 0.995 -and $ink -gt 5)
    }
   }else{
    if($scenario -in @('no-undo','wrong-undo')){
     $failedNative=@($resources|Where-Object{$_.view -eq 'RA' -and $_.phase -eq 'prepared'})[0]
     $expectedText=if($scenario -eq 'no-undo'){'A'}else{'!'};$expectedUndo=if($scenario -eq 'no-undo'){0}else{1}
     Check "$label.actual-failed-native-readback" ($failedNative.error -eq 50 -and $failedNative.diagnosticError -eq 0 -and $failedNative.resources.nodes[1].text -eq $expectedText -and $failedNative.resources.canUndo -eq $expectedUndo)
    }
    Check "$label.rejection-not-capability-pass" ($done[0].expectedRejectionDetected -and -not $done[0].terminalRestorePassed -and @($published|Where-Object view -in @('RA','RB')).Count -eq 0)}
   Check "$label.scope-preserved" (-not $done[0].fullR4 -and -not $done[0].generalGUIRestore -and -not $done[0].gateClosed -and $done[0].manualHostTest -eq 'not_executed')
   $cases.Add([pscustomobject]@{case=$label;exit=$exit;completed=$true;terminalRestorePassed=$done[0].terminalRestorePassed;expectedRejection=$done[0].expectedRejectionDetected;controllerAssertions=$assertions.Count})
  }catch{$errors.Add([pscustomobject]@{case=$label;error=$_.ToString();primaryFailures=@($rows|Where-Object type -in @('experiment_failure','process_launch_failed'))});Check "$label.evidence-processing" $false;$cases.Add([pscustomobject]@{case=$label;exit=$exit;completed=$false})}
  finally{if($rows.Count){$audit=Test-OwnedLifetimes $rows;$audit|ConvertTo-Json -Depth 7|Set-Content (Join-Path $dir 'lifetime-audit.json');Check "$label.owned-resources-cleaned" $audit.passed}}
 }
}
$regression='not_run'
if(-not $SkipRegression){try{
 & (Join-Path $PSScriptRoot '../r3-005c-001/test.ps1') -ResultsDir (Join-Path $ResultsDir 'c001-regression') | Tee-Object -FilePath (Join-Path $ResultsDir 'c001-regression-console.log')
 $r=Get-Content (Join-Path $ResultsDir 'c001-regression/summary.json') -Raw|ConvertFrom-Json
 $regression=if($r.allEvidenceChecksPassed -and $r.B001AndCET002Regression -eq 'passed'){'passed'}else{'failed'}
}catch{$regression='failed';$_|Out-String|Set-Content (Join-Path $ResultsDir 'regression-error.txt')};Check 'c001-b001-CET002-regressions' ($regression -eq 'passed')}
$unchanged=@(& git -C $PSScriptRoot diff HEAD -- ../common ../r1 ../r2 ../r3 ../r3-002 ../r3-003 ../r3-004 ../r3-005a ../r3-005a-cet-001 ../r3-005a-cet-002 ../r3-005b-001 ../r3-005c-001 ../r4-001)
Check 'prior-experiments-unchanged' ($unchanged.Count -eq 0)
$okay=$cases.Count -eq 10*$Repeats -and $errors.Count -eq 0 -and @($checks|Where-Object{-not $_.passed}).Count -eq 0
[ordered]@{allEvidenceChecksPassed=$okay;declaredSingleUndoAndTerminalRestorePassed=$okay;fullR4=$false;generalGUIRestore=$false;gateClosed=$false;oldRegressions=$regression;manualHostTest='not_executed';checks=$checks;cases=$cases;errors=$errors}|ConvertTo-Json -Depth 10|Set-Content (Join-Path $ResultsDir 'summary.json') -Encoding utf8
if(-not $okay){throw 'R4-002a evidence mismatch; preserve raw errors and results.'}
Write-Output 'R4_002A_DECLARED_SINGLE_UNDO_TERMINAL_RESTORE_PASSED'
