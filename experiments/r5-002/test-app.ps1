param(
 [string]$OutDir=(Join-Path $PSScriptRoot 'out'),
 [string]$ResultsDir=(Join-Path $PSScriptRoot ('results/'+(Get-Date -Format 'yyyyMMdd-HHmmss'))),
 [ValidateRange(1,3)][int]$Repeats=2,
 [ValidateSet("fixture","recorder","legacy")][string]$Application="fixture"
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '../r4-001-gate-001/lifetimes.ps1')
. (Join-Path $PSScriptRoot 'guard-evidence.ps1')
Add-Type -AssemblyName System.Drawing
if(-not ('R5001bReceipt' -as [type])){Add-Type -TypeDefinition 'public static class R5001bReceipt { public static ulong Compute(ulong r,ulong l,ulong m,ulong o,ulong seed) { unchecked { ulong v=(r^l)+m; return ((v<<13)|(v>>51))^o^seed; } } public static ulong Four(ulong r,ulong l,ulong m,ulong o,ulong fourth,ulong d,ulong last) { unchecked { ulong v=((r+l)^m)*1099511628211UL; return (((((v<<7)|(v>>57))+o)^fourth)+0x10591UL)^d^last; } } public static ulong Recorder(ulong r,ulong l,ulong m,ulong o,ulong d,ulong last) { unchecked { ulong v=((r+l)^m)*1099511628211UL; return (((v<<7)|(v>>57))+o)^d^last; } } }'}
$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(Test-Path -LiteralPath $ResultsDir){throw 'New ResultsDir required; preserve raw evidence.'}
New-Item -ItemType Directory -Path $ResultsDir | Out-Null
$checks=[Collections.Generic.List[object]]::new();$cases=[Collections.Generic.List[object]]::new();$errors=[Collections.Generic.List[object]]::new()
function Check([string]$Name,[bool]$Okay){$checks.Add([pscustomobject]@{name=$Name;passed=$Okay});Write-Output "$Name=$Okay"}
function Rows([string]$Path){@(Get-Content -LiteralPath $Path -Encoding utf8|Where-Object {$_}|ForEach-Object {$_|ConvertFrom-Json})}
function Bytes([string]$Path){,[IO.File]::ReadAllBytes($Path)}
function U64([byte[]]$Data,[long]$Offset){[BitConverter]::ToUInt64($Data,[int]$Offset)}
function Same([byte[]]$A,[byte[]]$B){[Convert]::ToBase64String($A) -eq [Convert]::ToBase64String($B)}
$exe=Join-Path $OutDir 'manager.exe'
$fixture=Join-Path $OutDir $(if($Application -eq 'fixture'){'fixture.exe'}elseif($Application -eq 'legacy'){'legacy.exe'}else{'second.exe'})
$client=Join-Path $OutDir $(if($Application -eq 'fixture'){'fixture-client.dll'}elseif($Application -eq 'legacy'){'legacy-client.dll'}else{'recorder-client.dll'})
$fileName=if($Application -eq 'fixture'){'document.txt'}else{'records.csv'}
$scenarios=@('positive','undo-unsupported','file-open','file-version','file-write','branch-failure','artifact-hash','wrong-image','abi-mismatch','unknown-resource')
if($Application -eq 'recorder'){$scenarios+=@('bad-layout','missing-frame','bad-unwind','omit-fourth','omit-register')}

$inputs=@('manager_main.cpp','clients/command.hpp','clients/observation.hpp','clients/host.hpp','clients/fixture_client.cpp','clients/recorder_client.cpp','recorder/contract.hpp','recorder/main.cpp','recorder/io.cpp','runtime/abi.hpp','runtime/execution.hpp','runtime/execution.cpp','runtime/processes.hpp','runtime/image.hpp','clients/legacy_client.cpp','recorder/traps.asm','build.ps1','test-app.ps1','test.ps1','test-guard.ps1','guard-evidence.ps1','archive-results.ps1','package.ps1')
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
[ordered]@{time=(Get-Date -Format o);baseCommit=(& git -C $PSScriptRoot rev-parse HEAD);os=[Environment]::OSVersion.VersionString;architecture=$env:PROCESSOR_ARCHITECTURE;elevated=$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator);systemSettingsChanged=$false;manualHostTest='not_executed';exeSha256=(Get-FileHash $exe).Hash.ToLowerInvariant();clientSha256=(Get-FileHash $client).Hash.ToLowerInvariant();application=$Application;kernelSha256=(Get-FileHash (Join-Path $OutDir 'kernel.lib')).Hash.ToLowerInvariant();fixtureSha256=(Get-FileHash $fixture).Hash.ToLowerInvariant();helperSha256=(Get-FileHash (Join-Path $OutDir 'helper.exe')).Hash.ToLowerInvariant();runtimeSha256=(Get-FileHash (Join-Path $OutDir 'runtime304.dll')).Hash.ToLowerInvariant();sources=@($inputs|ForEach-Object{[ordered]@{file=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $_)).Hash.ToLowerInvariant()}})}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $ResultsDir 'identity.json') -Encoding utf8
foreach($file in @('build.json','build.log','manager-loadconfig.txt','fixture-loadconfig.txt','second-loadconfig.txt','link-boundaries.json','reuse.json','kernel-identity.json')){Copy-Item -LiteralPath (Join-Path $OutDir $file) -Destination (Join-Path $ResultsDir $file)}
foreach($repeat in 1..$Repeats){
 foreach($scenario in $scenarios){
  $label="$scenario-$repeat";$dir=Join-Path $ResultsDir $label;$rows=@();$exit=$null
  try{
   & $exe --client $client --fixture $fixture --output $dir --scenario $scenario *> (Join-Path $ResultsDir "$label-console.log")
   $exit=$LASTEXITCODE;$rows=Rows (Join-Path $dir 'controller.jsonl');$done=@($rows|Where-Object type -eq 'experiment_complete')
   Check "$label.completed" ($exit -eq 0 -and $done.Count -eq 1 -and $done[0].evidencePassed -and @($rows|Where-Object type -eq 'experiment_failure').Count -eq 0)
   if($done.Count -ne 1 -or $exit -ne 0 -or -not $done[0].evidencePassed){throw 'Experiment failed; dependent evidence checks skipped, primary errors preserved.'}
   $assertions=@($rows|Where-Object type -eq 'assertion');Check "$label.controller-assertions" ($assertions.Count -gt 25 -and @($assertions|Where-Object{-not $_.passed}).Count -eq 0)
   $created=@($rows|Where-Object type -eq 'process_created');$exited=@($rows|Where-Object type -eq 'process_exited');$published=@($rows|Where-Object type -eq 'view_published');$resources=@($rows|Where-Object type -eq 'resource_result')
   foreach($publication in $published){
    $ready=@($resources|Where-Object {$_.view -eq $publication.view -and $_.phase -eq 'input-ready'})
    $processed=@($rows|Where-Object {$_.type -eq 'input_processed' -and $_.view -eq $publication.view -and $_.sequence -gt 0 -and $_.consumed -eq $_.sequence -and $_.tickMs -le $publication.tickMs})
    Check "$label.$($publication.view)-actual-input-before-publication" ($ready.Count -eq 1 -and $ready[0].error -eq 0 -and $processed.Count -gt 0 -and [array]::IndexOf($rows,$ready[0]) -lt [array]::IndexOf($rows,$publication))
   }
   $sessions=@($rows|Where-Object type -eq 'input_session_boundary')
   Check "$label.native-neutral-before-application-input" ($sessions.Count -ge 5 -and @($sessions|Where-Object{-not $_.neutral -or $_.beforeApplied -ne 0 -or $_.afterCtrl -band 0x8000 -or $_.afterShift -band 0x8000 -or $_.afterLeft -band 0x8000}).Count -eq 0)
   if($Application -eq 'recorder'){
    foreach($owner in @('source','A','B')){
     $queueEvidence=Test-CheckpointQueueEvidence $rows (Rows (Join-Path $dir "$owner/events.jsonl")) $owner
     Check "$label.$owner.actual-capture-queue-evidence" $queueEvidence.passed
    }
   }
   $policies=@($rows|Where-Object type -eq 'security_policy');Check "$label.CET-CFG-enabled" ($policies.Count -ge 3 -and @($policies|Where-Object{($_.shadowPolicy -band 1) -eq 0 -or ($_.cfgPolicy -band 1) -eq 0}).Count -eq 0)
   $routes=@($rows|Where-Object type -eq 'debug_route');Check "$label.one-pump-exact-routing" ($routes.Count -gt 50 -and @($routes|Where-Object{$_.eventPid -ne $_.recipientPid}).Count -eq 0 -and @($routes.ownerThread|Select-Object -Unique).Count -eq 1)
   foreach($r in $resources){$owner=@($created|Where-Object name -eq $r.view)[0];Check "$label.$($r.view)-$($r.phase)-GUI-owner" ($r.resources.queryTid -eq $owner.tid)}
   $snapshots=@($rows|Where-Object type -eq 'snapshot');$layout=@($rows|Where-Object type -eq 'raw_layout')[0]
   $fingerprints=@($rows|Where-Object type -eq 'snapshot_fingerprint')
   $schema=@($rows|Where-Object type -eq 'client_schema')[0]
   Check "$label.declared-application-schema" ($schema.file -eq $fileName -and $schema.stateBytes -gt 0)

   foreach($snap in $snapshots){
    $id=$snap.label;$data=Bytes (Join-Path $dir "$id-data.bin");$stack=Bytes (Join-Path $dir "$id-active-stack.bin");$cpu=Bytes (Join-Path $dir "$id-context.bin")
    $locals=@($snap.localAddresses|ForEach-Object{U64 $stack ($_-$snap.rsp)})
    if($Application -eq 'fixture'){$computed=[R5001bReceipt]::Compute((U64 $cpu $layout.r10Offset),$locals[0],$locals[1],$locals[2],(U64 $data $schema.seedOffset))}
    else{
     $computed=if($Application -eq 'recorder'){[R5001bReceipt]::Four((U64 $cpu $layout.r10Offset),$locals[0],$locals[1],$locals[2],$locals[3],(U64 $data $schema.descriptorOffset),(U64 $data ($schema.samplesOffset+7*$schema.sampleStride+$schema.valueOffset)))}else{[R5001bReceipt]::Recorder((U64 $cpu $layout.r10Offset),$locals[0],$locals[1],$locals[2],(U64 $data $schema.descriptorOffset),(U64 $data ($schema.samplesOffset+7*$schema.sampleStride+$schema.valueOffset)))}
     $rawValues=@(0..7|ForEach-Object {U64 $data ($schema.samplesOffset+$_*$schema.sampleStride+$schema.valueOffset)})
     Check "$label.$id.real-sample-array" (($rawValues -join ',') -eq ($snap.work.application.values -join ',') -and (U64 $data $schema.seedOffset) -eq $snap.work.seed -and $data.Length -eq 8192 -and $schema.stateBytes -eq 480 -and $schema.sampleStride -eq 24 -and $schema.seedOffset -eq 416 -and $schema.samplesOffset -eq 8 -and $schema.descriptorOffset -eq 0 -and $layout.dataAddress -eq 1868310773760)
    }
    $profile=Bytes (Join-Path $dir "$id-execution-layout.bin")
    $metadataBytes=Bytes (Join-Path $dir "$id-metadata.bin");$abiVersion=[BitConverter]::ToUInt32($metadataBytes,8)
    $frameCount=[BitConverter]::ToUInt32($profile,8);$returnCount=[BitConverter]::ToUInt32($profile,12)
    $expectedFrames=if($Application -eq 'recorder'){4}else{3}
    Check "$label.$id.raw-execution-profile" ($abiVersion -eq $(if($Application -eq 'recorder'){2}else{1}) -and $frameCount -eq $expectedFrames -and $returnCount -eq $expectedFrames+2 -and $locals.Count -eq $expectedFrames -and (Bytes (Join-Path $dir "$id-returns.bin")).Length -eq 8*$returnCount)
    $unwindFrames=@($rows|Where-Object{$_.type -eq 'native_unwind_frame' -and $_.declaredFrames -eq $frameCount})
    Check "$label.$id.real-ordered-unwind" (@($unwindFrames.index|Select-Object -Unique).Count -eq $frameCount+1)
    Check "$label.$id.raw-computation" ($computed -eq $snap.expectedReceipt)
    $fp=@($fingerprints|Where-Object {$_.phase -eq "$id-published" -or $_.phase -eq "final-$id" -or ($id -eq 'S0' -and $_.phase -eq 'after-rejection')});$first=$fp[0]
    Check "$label.$id.fingerprints-immutable" ($fp.Count -ge 2 -and @($fp.sha256|Select-Object -Unique).Count -eq 1)
    $stream=[IO.MemoryStream]::new()
    try{
     foreach($pair in @(@('fileManifest','file.json'),@('fileContent','file.bin'),@('targetImage','target-image.bin'),@('memory','data.bin'),@('stack','full-stack.bin'),@('context','context.bin'),@('resources','resources.bin'),@('metadata','metadata.bin'),@('executionLayout','execution-layout.bin'),@('returns','returns.bin'),@('abi','abi.bin'),@('executable','executable.bin'))){$file=Join-Path $dir "$id-$($pair[1])";Check "$label.$id.raw-$($pair[0])-hash" ((Get-FileHash $file).Hash.ToLowerInvariant() -eq $first.($pair[0]));$raw=Bytes $file;$stream.Write($raw,0,$raw.Length)}
     $sha=[Security.Cryptography.SHA256]::Create();try{$hash=[BitConverter]::ToString($sha.ComputeHash($stream.ToArray())).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()};Check "$label.$id.whole-hash" ($hash -eq $first.sha256)
    }finally{$stream.Dispose()}
    foreach($view in $(if($id -eq 'S0'){@('A','B','C')}elseif($id -eq 'SA'){@('RA')}else{@('RB')})){
     $results=@($rows|Where-Object{$_.type -eq 'execution_result' -and $_.view -eq $view});if(-not $results.Count){continue}
     $v=$results[0];$fault=$v.fault
     Check "$label.$view.execution-not-replayed" ($v.work.returned -eq $schema.returnMask -and ($v.work.receipt -eq $computed) -eq ($fault -eq ''))
     if($fault -ne ''){continue}
     Check "$label.$view.actual-memory-and-counter" ($v.work.seed -eq $snap.work.seed -and $v.work.counter -eq $snap.work.counter -and $v.work.text -eq $snap.work.text -and $v.work.documentReads -eq $snap.work.documentReads -and $v.work.documentWrites -eq $snap.work.documentWrites -and $v.work.processedBytes -eq $snap.work.processedBytes)
     $match=@($rows|Where-Object{$_.type -eq 'chain_match' -and $_.view -eq $view})[0];$chain=@($rows|Where-Object{$_.type -eq 'call_chain' -and $_.view -eq $view -and $_.phase -in @('target-before-restore','after-ordinary-context-restore')})
     Check "$label.$view.target-owned-CET" ($match.matched -and $match.sourceSsp -ne $match.targetSsp -and $match.initializations -eq 0 -and $match.businessPrefixes -eq 0 -and $chain.Count -eq $(if($Application -eq 'recorder'){12}else{10}) -and @($chain|Where-Object{$_.normalReturn -ne $_.shadowReturn}).Count -eq 0)
     $cloneData=[byte[]]$data.Clone();$cloneStack=[byte[]]$stack.Clone();$refs=@($rows|Where-Object{$_.type -eq 'resource_rebind' -and $_.view -eq $view})
     foreach($ref in $refs){if($ref.address -ge $layout.dataAddress -and $ref.address -lt $layout.dataAddress+$data.Length){$arr=$cloneData;$offset=[int]($ref.address-$layout.dataAddress)}else{$arr=$cloneStack;$offset=[int]($ref.address-$snap.rsp)};Check "$label.$view.declared-HWND-preimage-$offset" ((U64 $arr $offset) -eq $ref.oldHwnd);[array]::Copy([BitConverter]::GetBytes([ulong]$ref.newHwnd),0,$arr,$offset,8)}
     Check "$label.$view.only-declared-relocations" ($refs.Count -eq 7 -and (Same $cloneData (Bytes (Join-Path $dir "$view/target-data-before-continue.bin"))) -and (Same $cloneStack (Bytes (Join-Path $dir "$view/target-active-stack-before-continue.bin"))))
    }
   }
   $split=@($rows|Where-Object type -eq 'binary_split')[0]
   Check "$label.physically-distinct-application" ($split.manager -ne $split.application -and $split.managerSha256 -ne $split.applicationSha256 -and $split.applicationSha256 -eq (Get-FileHash $fixture).Hash.ToLowerInvariant())
   $contracts=@($rows|Where-Object type -eq 'client_contract')
   Check "$label.explicit-client-layout" ($contracts.Count -ge 4 -and @($contracts|Where-Object{$_.version -ne $(if($Application -eq 'fixture'){1}else{2}) -or $_.workBytes -le 0 -or $_.mailboxBytes -le 0}).Count -eq 0)
   $s0resources=Get-Content (Join-Path $dir 'S0-resources.json') -Raw|ConvertFrom-Json
   Check "$label.native-control-identities" ($s0resources.nodes[1].id -eq $schema.editId -and $s0resources.nodes[2].id -eq $schema.buttonId)
   $unwind=@($rows|Where-Object type -eq 'target_image_unwind')
   Check "$label.target-PE-unwind-not-manager" ($unwind.Count -gt 5 -and @($unwind|Where-Object{$_.targetBase -eq $_.managerBase -or $_.functionCount -le 0 -or $_.mirrorExecutable -or $_.usesManagerFunctionTable}).Count -eq 0)
   if($scenario -in @('wrong-image','abi-mismatch','unknown-resource','bad-layout','missing-frame','bad-unwind','omit-fourth','omit-register')){
     $failure=@($rows|Where-Object type -eq 'view_restore_rejected')[0]
     $expectedApi=if($scenario -eq 'wrong-image'){'image.file_identity_mismatch'}elseif($scenario -eq 'abi-mismatch'){'abi.header_mismatch'}elseif($scenario -eq 'bad-layout'){'unwind.declared_depth_mismatch'}elseif($scenario -eq 'missing-frame'){'abi.execution_frame_missing'}elseif($scenario -eq 'bad-unwind'){'unwind.declared_frame_mismatch'}elseif($scenario -in @('omit-fourth','omit-register')){'oracle.execution_receipt'}else{'abi.unknown_required_resource'}
     Check "$label.exact-compatibility-rejection" ($failure.api -eq $expectedApi -and -not $failure.published)
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
    Check "$label.only-five-restored-Views" ($published.Count -eq 5 -and $done[0].declaredR4IntegrationPassed)
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
    $expectedViews=if($scenario -in @('undo-unsupported','file-open')){@('A','B','RB','C')}elseif($scenario -in @('file-version','file-write','branch-failure','wrong-image','abi-mismatch','unknown-resource','bad-layout','missing-frame','bad-unwind','omit-fourth','omit-register')){@('A','B','RA','C')}else{@('A','B','RA','RB','C')}
    Check "$label.rejection-not-integration-success" ($done[0].expectedRejectionDetected -and -not $done[0].declaredR4IntegrationPassed -and (($published.view|Sort-Object) -join ',') -eq (($expectedViews|Sort-Object) -join ','))
    if($scenario -in @('file-version','file-write','branch-failure','wrong-image','abi-mismatch','unknown-resource','bad-layout','missing-frame','bad-unwind','omit-fourth','omit-register')){
     $failure=@($rows|Where-Object type -eq 'view_restore_rejected')[0]
     Check "$label.failed-RB-cleaned" ($failure.view -eq 'RB' -and -not $failure.published -and -not $failure.workspaceExists -and -not (Test-Path (Join-Path $dir 'workspace/RB.restore-partial')))
    }
    if($scenario -in @('undo-unsupported','file-open')){Check "$label.rejected-SA-never-published" (-not (Test-Path (Join-Path $dir 'SA-data.bin')) -and @($rows|Where-Object {$_.type -eq 'snapshot_published' -and $_.label -eq 'SA'}).Count -eq 0)}
   }
   Check "$label.scope-preserved" (-not $done[0].projectComplete -and -not $done[0].generalGUIRestore -and -not $done[0].gateClosed -and $done[0].manualHostTest -eq 'not_executed')
   $actions=Rows (Join-Path $dir 'actions.jsonl')
   foreach($group in @($actions|Group-Object action)){
    $g=@($group.Group);$states=@($g.state);$ordered=if($states[-1] -eq 'failed'){($states -join ',') -in @('submitted,accepted,failed','submitted,accepted,delivered,failed')}else{($states -join ',') -eq 'submitted,accepted,delivered,effect_confirmed'}
    Check "$label.$($group.Name)-Action-lifecycle" ($ordered -and @($g.view|Select-Object -Unique).Count -eq 1 -and @($g.sequence|Select-Object -Unique).Count -eq 1 -and $g[0].action -eq ($g[0].view+'-'+$g[0].sequence))
   }
   Check "$label.Action-failures-explicit" (@($actions|Where-Object state -eq 'failed').Count -eq $(if($scenario -eq 'positive'){0}else{1}))
   $artifacts=@($rows|Where-Object type -eq 'artifact_published')
   Check "$label.exact-fixed-versions" ($artifacts.Count -eq 2 -and (@($artifacts.artifact.id|Sort-Object) -join ',') -eq 'A-v1,B-v1')
   foreach($record in $artifacts){
    $artifact=$record.artifact;$aroot=Join-Path $dir ('artifacts/'+$artifact.id);$manifest=Get-Content (Join-Path $aroot 'manifest.json') -Raw|ConvertFrom-Json
    $hash=(Get-FileHash (Join-Path $aroot 'data.bin')).Hash.ToLowerInvariant();$content=[IO.File]::ReadAllText((Join-Path $aroot 'data.bin'))
    if($Application -ne 'fixture'){
     Check "$label.$($artifact.id)-recorder-format" ($content.StartsWith("RECORDER/2`n") -and $content.Contains("tick,value,quality`n") -and ($content -split "`n").Count -eq 13)
    }
    $producer=@($actions|Where-Object{$_.action -eq $artifact.producedAction -and $_.state -eq 'effect_confirmed'})[0];$effect=$producer.evidence.effect
    $native=@((Rows (Join-Path $dir ($artifact.sourceView+'/file-effects.jsonl')))|Where-Object action -eq $effect.action)[0];$proc=@($created|Where-Object name -eq $artifact.sourceView)[0]
    Check "$label.$($artifact.id)-provenance-and-bytes" ($manifest.sha256 -eq $hash -and $manifest.sourcePid -eq $proc.pid -and $manifest.producedAction -eq $artifact.producedAction -and $producer.operation -eq 'application.save' -and $native.pid -eq $proc.pid -and $native.tid -eq $proc.tid -and $native.liveHandles -eq 0 -and $native.writes -eq 1 -and $native.closes -eq 1 -and $native.flushes -eq 1 -and $native.written -ceq $content -and $native.sha256 -eq $hash)
    $verified=@($rows|Where-Object{$_.type -eq 'artifact_verified' -and $_.artifact.id -eq $artifact.id})
    Check "$label.$($artifact.id)-immutable" (@($verified|Where-Object phase -eq 'final').Count -eq 1 -and @($verified|Where-Object{$_.artifact.sha256 -ne $hash -or $_.artifact.producedAction -ne $artifact.producedAction}).Count -eq 0)
   }
   $cEffect=@((Rows (Join-Path $dir 'C/file-effects.jsonl'))|Where-Object operation -eq 2)[0]
   $aBytes=[IO.File]::ReadAllText((Join-Path $dir 'artifacts/A-v1/data.bin'));$bBytes=[IO.File]::ReadAllText((Join-Path $dir 'artifacts/B-v1/data.bin'))
   Check "$label.C-actual-native-read" ($cEffect.readA -ceq $aBytes -and $cEffect.readB -ceq $bBytes -and $cEffect.reads -eq 2 -and $cEffect.writes -eq 3 -and $cEffect.flushes -eq 3 -and $cEffect.closes -eq 5 -and $cEffect.liveHandles -eq 0 -and $cEffect.error -eq 0)
   Check "$label.C-result-from-both-inputs" ($cEffect.written.Contains($aBytes) -and $cEffect.written.Contains($bBytes) -and [IO.File]::ReadAllText((Join-Path $dir 'workspace/C/processed.txt')) -ceq $cEffect.written)
   foreach($id in @('A','B')){
    $import=[IO.File]::ReadAllText((Join-Path $dir "workspace/C/imports/$id.txt"));$fixed=[IO.File]::ReadAllText((Join-Path $dir "artifacts/$id-v1/data.bin"))
    Check "$label.C-private-$id-not-source" ($import.StartsWith($fixed) -and $import.Contains('C-private-edit') -and (Get-FileHash (Join-Path $dir "workspace/$id/$fileName")).Hash -eq (Get-FileHash (Join-Path $dir "artifacts/$id-v1/data.bin")).Hash)
   }
   foreach($snapshot in @($snapshots|Where-Object label -ne 'S0')){
    $name=$snapshot.label;$v=Get-Content (Join-Path $dir "$name-file.json") -Raw|ConvertFrom-Json
    Check "$label.$name-captured-file-version" ($v.present -and $v.sourceView -eq $snapshot.view -and $v.sourcePid -eq @($created|Where-Object name -eq $snapshot.view)[0].pid -and $v.sha256 -eq (Get-FileHash (Join-Path $dir "$name-file.bin")).Hash.ToLowerInvariant() -and (Same (Bytes (Join-Path $dir "$name-file.bin")) (Bytes (Join-Path $dir "workspace/$($v.sourceView)/$fileName"))))
   }
   foreach($r in @($rows|Where-Object{$_.type -eq 'workspace_restored' -and $_.file.present})){
    Check "$label.$($r.view)-new-native-file-object" ($r.targetVolume -ne $r.file.sourceVolume -or $r.targetObject -ne $r.file.sourceObject)
   }
   $cases.Add([pscustomobject]@{case=$label;exit=$exit;completed=$true;declaredR4IntegrationPassed=$done[0].declaredR4IntegrationPassed;expectedRejection=$done[0].expectedRejectionDetected;controllerAssertions=$assertions.Count})
  }catch{$errors.Add([pscustomobject]@{case=$label;error=$_.ToString();primaryFailures=@($rows|Where-Object {($_.type -in @('experiment_failure','process_launch_failed')) -or ($_.type -eq 'assertion' -and -not $_.passed)})});Check "$label.evidence-processing" $false;$cases.Add([pscustomobject]@{case=$label;exit=$exit;completed=$false})}
  finally{if($rows.Count){$audit=Test-OwnedLifetimes $rows;$audit|ConvertTo-Json -Depth 7|Set-Content (Join-Path $dir 'lifetime-audit.json');Check "$label.owned-resources-cleaned" $audit.passed}}
 }
}
$okay=$cases.Count -eq $scenarios.Count*$Repeats -and $errors.Count -eq 0 -and @($checks|Where-Object{-not $_.passed}).Count -eq 0
[ordered]@{allEvidenceChecksPassed=$okay;application=$Application;generalGUIRestore=$false;gateClosed=$false;manualHostTest='not_executed';checks=$checks;cases=$cases;errors=$errors}|ConvertTo-Json -Depth 10|Set-Content (Join-Path $ResultsDir 'summary.json') -Encoding utf8
if(-not $okay){throw 'R5-002 application evidence mismatch; preserve raw errors and results.'}
Write-Output 'R5_002_APPLICATION_PASSED'
