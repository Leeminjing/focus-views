param([string]$OutDir=(Join-Path $PSScriptRoot 'out'),[string]$ResultsDir=(Join-Path $PSScriptRoot ('results/'+(Get-Date -Format 'yyyyMMdd-HHmmss'))),[ValidateRange(1,3)][int]$Repeats=2,[switch]$SkipRegression)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '../r4-001-gate-001/lifetimes.ps1')
Add-Type -AssemblyName System.Drawing
if(-not ('RestartReceipt' -as [type])){Add-Type -TypeDefinition 'public static class RestartReceipt { public static ulong Fixture(ulong r,ulong l,ulong m,ulong o,ulong seed){unchecked{ulong v=(r^l)+m;return ((v<<13)|(v>>51))^o^seed;}} public static ulong Four(ulong r,ulong l,ulong m,ulong o,ulong lastLocal,ulong d,ulong last){unchecked{ulong v=((r+l)^m)*1099511628211UL;return (((((v<<7)|(v>>57))+o)^lastLocal)+0x10591UL)^d^last;}}}'}
$OutDir=[IO.Path]::GetFullPath($OutDir);$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(Test-Path -LiteralPath $ResultsDir){throw 'New ResultsDir required; preserve prior runs'}
New-Item -ItemType Directory -Path $ResultsDir|Out-Null
$checks=[Collections.Generic.List[object]]::new();$cases=[Collections.Generic.List[object]]::new();$errors=[Collections.Generic.List[object]]::new();$managers=[Collections.Generic.List[object]]::new()
$testSourceSha=(Get-FileHash -LiteralPath $PSCommandPath).Hash.ToLowerInvariant()
[ordered]@{time=(Get-Date -Format o);sourceHead=(& git -C $PSScriptRoot rev-parse HEAD);testSha256=$testSourceSha;repeats=$Repeats;skipRegression=[bool]$SkipRegression;os=[Environment]::OSVersion.VersionString;architecture=$env:PROCESSOR_ARCHITECTURE;manualHostTest='not_executed';sourceInputs=@(Get-ChildItem -LiteralPath $PSScriptRoot -File|Where-Object Extension -in @('.ps1','.cpp','.md')|ForEach-Object{[ordered]@{file=$_.Name;sha256=(Get-FileHash $_.FullName).Hash.ToLowerInvariant()}})}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $ResultsDir 'test-identity.json') -Encoding utf8
function Check([string]$Name,[bool]$Passed){$checks.Add([pscustomobject]@{name=$Name;passed=$Passed});Write-Host "$Name=$Passed"}
function Rows([string]$Path){@(Get-Content -LiteralPath $Path -Encoding utf8|Where-Object {$_}|ForEach-Object {$_|ConvertFrom-Json})}
function Primary([string[]]$Directories){foreach($dir in $Directories){$path=Join-Path $dir 'controller.jsonl';if(Test-Path -LiteralPath $path){Rows $path|Where-Object{$_.type -in @('experiment_failure','process_launch_failed','view_restore_rejected') -or ($_.type -eq 'assertion' -and -not $_.passed)}}}}
function Sha([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Same([string]$A,[string]$B){(Sha $A) -eq (Sha $B)}
function Inventory([string]$Root){@((Get-ChildItem -LiteralPath $Root -Recurse -File|Sort-Object FullName)|ForEach-Object{[pscustomobject]@{file=[IO.Path]::GetRelativePath($Root,$_.FullName);bytes=$_.Length;sha256=(Sha $_.FullName)}})}
function InventoryText([string]$Root){(Inventory $Root)|ConvertTo-Json -Compress -Depth 4}
function Run-Manager([string]$Application,[string]$Mode,[string]$Store,[string]$Output,[string]$Scenario='positive',[string]$OverrideApp=''){
 $exe=Join-Path $OutDir 'manager.exe';$app=if($OverrideApp){$OverrideApp}elseif($Application -eq 'fixture'){Join-Path $OutDir 'fixture.exe'}else{Join-Path $OutDir 'second.exe'}
 $info=[Diagnostics.ProcessStartInfo]::new($exe);$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($arg in @('--client',(Join-Path $OutDir "$Application-client.dll"),'--fixture',$app,'--mode',$Mode,'--store',$Store,'--output',$Output,'--scenario',$Scenario)){$info.ArgumentList.Add($arg)}
 $p=[Diagnostics.Process]::new();$p.StartInfo=$info;$start=Get-Date
 if(-not $p.Start()){throw 'Manager process did not start'}
 $taskManagerPid=$p.Id;$created=$p.StartTime.ToUniversalTime().ToString('o');$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();$timedOut=-not $p.WaitForExit(120000)
 if($timedOut){$p.Kill($true);$p.WaitForExit()}
 [IO.File]::WriteAllText($Output+'-stdout.log',$stdout.GetAwaiter().GetResult());[IO.File]::WriteAllText($Output+'-stderr.log',$stderr.GetAwaiter().GetResult())
 $result=[pscustomobject]@{application=$Application;mode=$Mode;scenario=$Scenario;pid=$taskManagerPid;createdUtc=$created;start=$start.ToString('o');ended=(Get-Date -Format o);exit=$p.ExitCode;exited=$p.HasExited;timedOut=$timedOut;output=$Output;store=$Store;sha256=(Sha $exe)}
 $managers.Add($result);$p.Dispose();return $result
}
function Audit([string]$Dir,[string]$Label){
 $rows=Rows (Join-Path $Dir 'controller.jsonl');$life=Test-OwnedLifetimes $rows;$life|ConvertTo-Json -Depth 6|Set-Content (Join-Path $Dir 'lifetime-audit.json')
 Check "$Label.owned-processes-and-desktops-cleaned" $life.passed
 $assertions=@($rows|Where-Object type -eq 'assertion');Check "$Label.all-native-assertions" (@($assertions|Where-Object {-not $_.passed}).Count -eq 0)
 foreach($cap in @($rows|Where-Object type -eq 'capture')){
  $bmp=[Drawing.Bitmap]::new((Join-Path $Dir $cap.file));$tick=0;$valid=$true
  try{foreach($bit in 0..15){$pixel=$bmp.GetPixel(($cap.clientDx+28+10*$bit),($cap.clientDy+356));$white=$pixel.R -gt 240 -and $pixel.G -gt 240 -and $pixel.B -gt 240;$black=$pixel.R -lt 15 -and $pixel.G -lt 15 -and $pixel.B -lt 15;if(-not($white -or $black)){$valid=$false};if($white){$tick=$tick -bor (1 -shl $bit)}}}finally{$bmp.Dispose()}
  $paint=@((Rows (Join-Path $Dir "$($cap.view)/events.jsonl"))|Where-Object{$_.event -in @('wm_paint_end','wm_printclient') -and $_.state.lastDrawUpdate -eq $tick -and $_.tickMs -ge $cap.apiBeginMs -and $_.tickMs -le $cap.apiEndMs})
  Check "$Label.$($cap.view)-$($cap.name)-real-frame" ($valid -and $tick -eq $cap.counter -and $cap.error -eq 0 -and $paint.Count -gt 0)
 }
 $actions=Rows (Join-Path $Dir 'actions.jsonl')
 foreach($group in @($actions|Group-Object action)){
  $g=@($group.Group);$states=@($g.state);$ordered=if($states[-1] -eq 'failed'){($states -join ',') -in @('submitted,accepted,failed','submitted,accepted,delivered,failed')}else{($states -join ',') -eq 'submitted,accepted,delivered,effect_confirmed'}
  Check "$Label.$($group.Name)-Action" ($ordered -and @($g.view|Select-Object -Unique).Count -eq 1)
 }
 foreach($viewDir in @(Get-ChildItem -LiteralPath $Dir -Directory)){
  foreach($inputLog in @(Get-ChildItem -LiteralPath $viewDir.FullName -Filter 'input-*.jsonl' -File)){
   $next=0;$ordered=$true
   foreach($line in [IO.File]::ReadLines($inputLog.FullName)){
    if(-not $line.StartsWith('{"stage":"virtual_query"')){continue}
    $match=[regex]::Match($line,'^\{"stage":"virtual_query","index":([0-9]+),');++$next
    if(-not $match.Success -or [long]$match.Groups[1].Value -ne $next){$ordered=$false}
   }
   Check "$Label.$($viewDir.Name)-$($inputLog.Name)-queries-no-loss-or-duplicate" ($ordered -and $next -gt 0)
  }
 }
 return ,$rows
}
function Check-Packet([string]$Path,[string]$Label){
 $b=[IO.File]::ReadAllBytes($Path);$length=[BitConverter]::ToUInt64($b,24)
 $sha=[Security.Cryptography.SHA256]::Create();try{$actual=[Convert]::ToHexString($sha.ComputeHash($b,0,$b.Length-32)).ToLowerInvariant()}finally{$sha.Dispose()}
 $stored=[Convert]::ToHexString($b[($b.Length-32)..($b.Length-1)]).ToLowerInvariant()
 Check "$Label.packet-version-length-and-SHA256" ($b.Length -eq 64+$length -and [BitConverter]::ToUInt32($b,8) -eq 1 -and [BitConverter]::ToUInt32($b,12) -eq 0x8664 -and $actual -eq $stored)
}
function Check-Restore([string]$Label,[string]$Application,[string]$First,[string]$Second,[object[]]$SaveRows,[object[]]$RestoreRows,[bool]$Failure=$false){
 $snapshots=@($SaveRows|Where-Object type -eq 'snapshot');$schema=@($SaveRows|Where-Object type -eq 'client_schema')[0]
 $published=@($RestoreRows|Where-Object type -eq 'view_published');Check "$Label.exact-restored-Views" ((($published.view|Sort-Object)-join ',') -eq $(if($Failure){'C,RA'}else{'C,RA,RB'}))
 Check "$Label.no-business-initialization-replay" (@($RestoreRows|Where-Object type -eq 'business_effect').Count -eq 0)
 foreach($snapshot in $snapshots){
  $id=$snapshot.label;$stack=[IO.File]::ReadAllBytes((Join-Path $First "$id-active-stack.bin"));$data=[IO.File]::ReadAllBytes((Join-Path $First "$id-data.bin"));$cpu=[IO.File]::ReadAllBytes((Join-Path $First "$id-context.bin"))
  $layout=@($SaveRows|Where-Object type -eq 'raw_layout')[0];$r10=[BitConverter]::ToUInt64($cpu,$layout.r10Offset);$locals=@($snapshot.localAddresses|ForEach-Object{[BitConverter]::ToUInt64($stack,[int]($_-$snapshot.rsp))})
  $seed=[BitConverter]::ToUInt64($data,$schema.seedOffset)
  $expected=if($Application -eq 'fixture'){[RestartReceipt]::Fixture($r10,$locals[0],$locals[1],$locals[2],$seed)}else{[RestartReceipt]::Four($r10,$locals[0],$locals[1],$locals[2],$locals[3],[BitConverter]::ToUInt64($data,$schema.descriptorOffset),[BitConverter]::ToUInt64($data,($schema.samplesOffset+7*$schema.sampleStride+$schema.valueOffset)))}
  Check "$Label.$id.raw-CPU-stack-memory-computation" ($expected -eq $snapshot.expectedReceipt -and $locals.Count -eq $(if($Application -eq 'fixture'){3}else{4}))
  foreach($suffix in @('data.bin','context.bin','metadata.bin','execution-layout.bin','full-stack.bin','resources.bin','abi.bin','returns.bin','executable.bin','target-image.bin','file.json','file.bin')){Check "$Label.$id.loaded-$suffix" (Same (Join-Path $First "$id-$suffix") (Join-Path $Second "$id-$suffix"))}
  $view=if($id -eq 'S0'){'C'}elseif($id -eq 'SA'){'RA'}else{'RB'}
  if($Failure -and $view -eq 'RB'){continue}
  $result=@($RestoreRows|Where-Object{$_.type -eq 'execution_result' -and $_.view -eq $view})[0];$match=@($RestoreRows|Where-Object{$_.type -eq 'chain_match' -and $_.view -eq $view})[0]
  $chain=@($RestoreRows|Where-Object{$_.type -eq 'call_chain' -and $_.view -eq $view -and $_.phase -in @('target-before-restore','after-ordinary-context-restore')})
  Check "$Label.$view.real-RET-and-original-state" ($result.work.receipt -eq $expected -and $result.work.returned -eq $schema.returnMask -and $result.work.seed -eq $seed -and $result.work.counter -eq $snapshot.work.counter -and $result.work.text -eq $snapshot.work.text)
  Check "$Label.$view.own-CET-chain" ($match.matched -and $match.initializations -eq 0 -and $match.businessPrefixes -eq 0 -and $match.sourceSsp -ne $match.targetSsp -and $chain.Count -eq $(if($Application -eq 'fixture'){10}else{12}) -and @($chain|Where-Object{$_.normalReturn -ne $_.shadowReturn}).Count -eq 0)
  Check "$Label.$view.seven-explicit-HWND-rebindings" (@($RestoreRows|Where-Object{$_.type -eq 'resource_rebind' -and $_.view -eq $view}).Count -eq 7)
  $profile=@($RestoreRows|Where-Object{$_.type -eq 'snapshot_disk_loaded' -and $_.label -eq $id})[0]
  Check "$Label.$id.actual-ABI" ($profile.abi -eq $(if($Application -eq 'fixture'){1}else{2}) -and $profile.frames -eq $(if($Application -eq 'fixture'){3}else{4}))
 }
 foreach($pair in @(@('A','RA','SA'),@('B','RB','SB'))){
  if($Failure -and $pair[1] -eq 'RB'){continue}
  $saved=@($SaveRows|Where-Object{$_.type -eq 'resource_result' -and $_.view -eq $pair[0] -and $_.phase -eq $pair[2]})[0].resources
  $initial=@($RestoreRows|Where-Object{$_.type -eq 'resource_result' -and $_.view -eq $pair[1] -and $_.phase -eq 'initial'})[0].resources
  Check "$Label.$($pair[1]).Undo-and-selection" ($saved.canUndo -eq 1 -and $initial.canUndo -eq 1 -and $initial.undo.valid -eq 1 -and $initial.undo.operations -eq 1 -and $saved.nodes[1].text -eq $initial.nodes[1].text -and $saved.selectionStart -eq $initial.selectionStart -and $saved.selectionEnd -eq $initial.selectionEnd -and $saved.modified -eq $initial.modified -and $saved.focus -eq $initial.focus)
  $before=@($SaveRows|Where-Object{$_.type -eq 'capture' -and $_.view -eq $pair[0] -and $_.name -eq $pair[2]})[0];$after=@($RestoreRows|Where-Object{$_.type -eq 'capture' -and $_.view -eq $pair[1] -and $_.name -eq 'initial'})[0]
  $a=[Drawing.Bitmap]::new((Join-Path $First $before.file));$b=[Drawing.Bitmap]::new((Join-Path $Second $after.file));$edit=$saved.nodes[1];$same=0;$total=0;$ink=0
  try{for($y=$edit.top+4;$y -lt $edit.bottom-4;++$y){for($x=$edit.left+4;$x -lt $edit.right-4;++$x){if([Math]::Abs($x-($edit.left+$saved.caretX)) -le 2){continue};$pa=$a.GetPixel(($before.clientDx+$x),($before.clientDy+$y));$pb=$b.GetPixel(($after.clientDx+$x),($after.clientDy+$y));++$total;if($pa.ToArgb() -eq $pb.ToArgb()){++$same};if($pb.R -lt 80 -or $pb.G -lt 80 -or $pb.B -lt 80){++$ink}}}}finally{$a.Dispose();$b.Dispose()}
  Check "$Label.$($pair[1]).actual-restored-EDIT-pixels" ($total -gt 0 -and $same/[double]$total -gt 0.995 -and $ink -gt 5)
  [ordered]@{same=$same;total=$total;ink=$ink}|ConvertTo-Json|Set-Content (Join-Path $Second "$($pair[1])/EDIT-pixels.json")
 }
 $effect=@((Rows (Join-Path $Second 'C/file-effects.jsonl'))|Where-Object operation -eq 2)[0]
 Check "$Label.C-native-file-processing" ($effect.reads -eq 2 -and $effect.writes -eq 3 -and $effect.closes -eq 5 -and $effect.liveHandles -eq 0 -and $effect.written -ceq [IO.File]::ReadAllText((Join-Path $Second 'workspace/C/processed.txt')))
 $firstCreated=@($SaveRows|Where-Object type -eq 'process_created');$nextCreated=@($RestoreRows|Where-Object type -eq 'process_created')
 foreach($pair in @(@('A','RA'),@('B','RB'),@('source','C'))){if($Failure -and $pair[1] -eq 'RB'){continue};$old=@($firstCreated|Where-Object name -eq $pair[0])[0];$new=@($nextCreated|Where-Object name -eq $pair[1])[0];Check "$Label.$($pair[1]).different-application-PID" ($old.pid -ne $new.pid)}
}
Copy-Item (Join-Path $OutDir 'build.json') (Join-Path $ResultsDir 'build.json')
$originalManagerHash=Sha (Join-Path $OutDir 'manager.exe');$kernelHash=Sha (Join-Path $OutDir 'kernel.lib')
foreach($application in @('fixture','recorder')){
 $baselineStore='';$baselineFirst='';$baselineRows=@();$invalidAbi=''
 for($repeat=1;$repeat -le $Repeats;++$repeat){$label="$application-positive-$repeat";$caseDir=Join-Path $ResultsDir $label;New-Item -ItemType Directory -Path $caseDir|Out-Null;$first=Join-Path $caseDir 'manager1';$offline=Join-Path $caseDir 'manager1-offline';$second=Join-Path $caseDir 'manager2';$store=Join-Path $caseDir 'store'
  try{
   $one=Run-Manager $application save $store $first;Check "$label.Manager1-normal-exit" ($one.exit -eq 0 -and $one.exited -and -not $one.timedOut)
   $firstRows=Audit $first "$label.save";$done=@($firstRows|Where-Object type -eq 'restart_complete');if($one.exit -ne 0 -or $done.Count -ne 1 -or -not $done[0].passed){throw 'Manager1 failed; preserve primary log before checking Snapshot files'}
   foreach($name in @('S0','SA','SB','session')){Check-Packet (Join-Path $store "$name.fvs") "$label.$name"}
   $fixed=InventoryText $store;[IO.File]::WriteAllText((Join-Path $caseDir 'store-before.json'),$fixed)
   # Both paths are resolved and confined before moving this owned evidence tree.
   $resolved=[IO.Path]::GetFullPath($first);$destination=[IO.Path]::GetFullPath($offline);$boundary=[IO.Path]::GetFullPath($caseDir)+[IO.Path]::DirectorySeparatorChar
   if(-not($resolved.StartsWith($boundary) -and $destination.StartsWith($boundary))){throw 'Evidence rename escaped owned case directory'}
   Move-Item -LiteralPath $resolved -Destination $destination
   Check "$label.Manager1-evidence-path-unavailable" (-not(Test-Path -LiteralPath $first))
   $two=Run-Manager $application restore $store $second;Check "$label.Manager2-new-PID-and-normal-exit" ($two.pid -ne $one.pid -and $two.exit -eq 0 -and $two.exited -and -not $two.timedOut -and $two.createdUtc -gt $one.createdUtc)
   $rows=Audit $second "$label.restore";$finish=@($rows|Where-Object type -eq 'restart_complete');Check "$label.actual-cross-manager-restore" ($finish.Count -eq 1 -and $finish[0].passed -and $two.exit -eq 0)
   Check-Restore $label $application $offline $second $firstRows $rows
   Check "$label.entire-store-unchanged" ((InventoryText $store) -ceq $fixed)
   $target=Join-Path $OutDir $(if($application -eq 'fixture'){'fixture.exe'}else{'second.exe'});$codecDir=Join-Path $caseDir 'codec'
   & (Join-Path $OutDir 'codec_test.exe') --fixture $target --snapshot (Join-Path $store 'SA.fvs') --output $codecDir *> (Join-Path $caseDir 'codec-console.log')
   $codecExit=$LASTEXITCODE;Check "$label.native-codec-positive-and-negative-matrix" ($codecExit -eq 0)
   $codec=Rows (Join-Path $codecDir 'codec.jsonl');foreach($row in @($codec|Where-Object type -eq 'check')){Check "$label.codec-$($row.name)" $row.passed}
   $cases.Add([pscustomobject]@{case=$label;passed=($two.exit -eq 0 -and @($checks|Where-Object{$_.name -like "$label.*" -and -not $_.passed}).Count -eq 0);kind='cross-manager-positive';manager1=$one.pid;manager2=$two.pid})
   if($repeat -eq 1){$baselineStore=$store;$baselineFirst=$offline;$baselineRows=$firstRows;$invalidAbi=Join-Path $codecDir 'invalid-abi.fvs'}
  }catch{$errors.Add([pscustomobject]@{case=$label;error=$_.ToString();primaryFailures=@(Primary @($first,$offline,$second))});Check "$label.evidence-complete" $false;$cases.Add([pscustomobject]@{case=$label;passed=$false;kind='failed'})}
 }
 if(-not $baselineStore){continue}
 foreach($scenario in @('missing','tamper','partial','format','abi','wrong-image','artifact-hash','resource-failure','query-capacity')){
  $label="$application-$scenario";$caseDir=Join-Path $ResultsDir $label;New-Item -ItemType Directory -Path $caseDir|Out-Null;$output=Join-Path $caseDir 'manager2';$store=$baselineStore;$override='';$runScenario='positive'
  try{
   $originalStore=InventoryText $baselineStore
   $scenarioFirst=$baselineFirst;$scenarioRows=$baselineRows
   if($scenario -in @('resource-failure','query-capacity')){
    # These exercise post-load behavior. Give each an independent checkpoint
    # setup, rather than letting a previous EXE-fault/caching change mask it.
    # This is a separate test, never a retry of a failed restore operation.
    $store=Join-Path $caseDir 'store';$setup=Join-Path $caseDir 'manager1';$scenarioFirst=Join-Path $caseDir 'manager1-offline'
    $one=Run-Manager $application save $store $setup;Check "$label.Manager1-normal-exit" ($one.exit -eq 0 -and $one.exited -and -not $one.timedOut)
    $scenarioRows=Audit $setup "$label.save";if($one.exit -ne 0){throw 'Independent scenario setup failed; no restore attempted'}
    $resolved=[IO.Path]::GetFullPath($setup);$destination=[IO.Path]::GetFullPath($scenarioFirst);$boundary=[IO.Path]::GetFullPath($caseDir)+[IO.Path]::DirectorySeparatorChar
    if(-not($resolved.StartsWith($boundary) -and $destination.StartsWith($boundary))){throw 'Scenario evidence rename escaped owned directory'}
    Move-Item -LiteralPath $resolved -Destination $destination
   }
   if($scenario -notin @('resource-failure','wrong-image','query-capacity')){$store=Join-Path $caseDir 'fault-store';Copy-Item -LiteralPath $baselineStore -Destination $store -Recurse
    $fault=Join-Path $store 'SA.fvs'
    switch($scenario){
     missing {Remove-Item -LiteralPath $fault}
     tamper {$b=[IO.File]::ReadAllBytes($fault);$b[[int]($b.Length/2)]=$b[[int]($b.Length/2)] -bxor 1;[IO.File]::WriteAllBytes($fault,$b)}
     partial {$b=[IO.File]::ReadAllBytes($fault);[IO.File]::WriteAllBytes($fault,$b[0..([int]($b.Length/2))])}
     format {$b=[IO.File]::ReadAllBytes($fault);$b[8]=99;[IO.File]::WriteAllBytes($fault,$b)}
     abi {Copy-Item -LiteralPath $invalidAbi -Destination $fault -Force}
     artifact-hash {$fault=Join-Path $store 'artifacts/A-v1/data.bin';(Get-Item -LiteralPath $fault).IsReadOnly=$false;$b=[IO.File]::ReadAllBytes($fault);$b[0]=$b[0] -bxor 1;[IO.File]::WriteAllBytes($fault,$b)}
    }
   }
   if($scenario -eq 'wrong-image'){$source=Join-Path $OutDir $(if($application -eq 'fixture'){'fixture.exe'}else{'second.exe'});$override=Join-Path $caseDir 'wrong-image.exe';$b=[IO.File]::ReadAllBytes($source);$pe=[BitConverter]::ToInt32($b,60);$b[$pe+8]=$b[$pe+8] -bxor 1;[IO.File]::WriteAllBytes($override,$b)}
   if($scenario -in @('resource-failure','query-capacity')){$runScenario=$scenario}
   $result=Run-Manager $application restore $store $output $runScenario $override;$rows=Audit $output $label
   $failure=@($rows|Where-Object type -eq 'experiment_failure');$published=@($rows|Where-Object type -eq 'view_published')
   if($scenario -eq 'query-capacity'){
    $probe=@($rows|Where-Object type -eq 'query_capacity_probe');Check "$label.idle-past-old-limit" ($result.exit -eq 0 -and $probe.Count -eq 1 -and $probe[0].count -gt $probe[0].capacity -and $probe[0].health -eq 1 -and $probe[0].bridgeAlive)
    if($result.exit -ne 0){throw 'Idle query scenario failed; primary API records retained'}
    Check-Restore $label $application $scenarioFirst $output $scenarioRows $rows
   }elseif($scenario -eq 'resource-failure'){
    Check "$label.expected-isolated-resource-rejection" ($result.exit -eq 0 -and @($rows|Where-Object{$_.type -eq 'view_restore_rejected' -and $_.view -eq 'RB' -and $_.api -eq 'resource.restore.parent_relation' -and -not $_.published}).Count -eq 1 -and @($rows|Where-Object{$_.type -eq 'restart_complete' -and $_.passed -and $_.expectedRejectionDetected}).Count -eq 1)
    if($result.exit -ne 0){throw 'Native resource failure did not finish expected survivor path; primary API records retained'}
    Check-Restore $label $application $scenarioFirst $output $scenarioRows $rows $true
   }else{
    $api=switch($scenario){missing {'CreateFileW.snapshot_load'} tamper {'snapshot.integrity'} partial {'snapshot.blob_length'} format {'snapshot.format_version'} abi {'snapshot.runtime_abi'} wrong-image {'snapshot.target_image'} artifact-hash {'artifact.persisted_hash'}}
    Check "$label.precise-refusal-no-View" ($result.exit -ne 0 -and $failure.Count -eq 1 -and $failure[0].api -eq $api -and $published.Count -eq 0 -and @($rows|Where-Object type -eq 'process_created').Count -eq 0)
   }
   Check "$label.original-fixed-store-unchanged" ((InventoryText $baselineStore) -ceq $originalStore)
   $cases.Add([pscustomobject]@{case=$label;kind=$(if($scenario -eq 'query-capacity'){'idle-query-restart-positive'}elseif($scenario -eq 'resource-failure'){'isolated-resource-rejection'}else{'disk-load-rejection'});exit=$result.exit;manager2=$result.pid;passed=(@($checks|Where-Object{$_.name -like "$label.*" -and -not $_.passed}).Count -eq 0)})
  }catch{$errors.Add([pscustomobject]@{case=$label;error=$_.ToString();primaryFailures=@(Primary @($output))});Check "$label.evidence-complete" $false;$cases.Add([pscustomobject]@{case=$label;passed=$false;kind='failed'})}
 }
}
Check 'same-manager-throughout' ((Sha (Join-Path $OutDir 'manager.exe')) -eq $originalManagerHash -and @($managers.sha256|Select-Object -Unique).Count -eq 1)
Check 'same-R5-002-kernel-binary' ((Sha (Join-Path $OutDir 'kernel.lib')) -eq $kernelHash -and $kernelHash -eq (Sha (Join-Path $PSScriptRoot '../r5-002/out/kernel.lib')))
Check 'test-source-unchanged-during-run' ((Sha $PSCommandPath) -eq $testSourceSha)
$bodyPassed=$errors.Count -eq 0 -and @($checks|Where-Object{-not $_.passed}).Count -eq 0
$regression='not_run'
if(-not $SkipRegression){try{
 & (Join-Path $PSScriptRoot 'prepare-regression-input.ps1') -OutDir $OutDir -EvidenceDir (Join-Path $ResultsDir 'regression-input-profile') | Tee-Object -FilePath (Join-Path $ResultsDir 'regression-input-profile-console.log')
 & (Join-Path $PSScriptRoot '../r5-002/test.ps1') -ResultsDir (Join-Path $ResultsDir 'r5-002-regression') | Tee-Object -FilePath (Join-Path $ResultsDir 'regression-console.log')
 $old=Get-Content -Raw (Join-Path $ResultsDir 'r5-002-regression/summary.json')|ConvertFrom-Json
 Check 'R5-002-and-all-nested-regressions' ($old.allEvidenceChecksPassed -and $old.oldRegressions -eq 'passed');$regression=if($old.allEvidenceChecksPassed -and $old.oldRegressions -eq 'passed'){'passed'}else{'failed'}
}catch{$regression='failed';$errors.Add([pscustomobject]@{case='regression';error=$_.ToString()});Check 'R5-002-and-all-nested-regressions' $false}}
$okay=$errors.Count -eq 0 -and @($checks|Where-Object{-not $_.passed}).Count -eq 0
[ordered]@{allEvidenceChecksPassed=$okay;normalExitCrossManagerRestorePassed=$bodyPassed;regressions=$regression;gateClosed=$false;crashRecoveryVerified=$false;hostRebootRecoveryVerified=$false;stableSnapshotProtocol=$false;manualHostTest='not_executed';managerSha256=$originalManagerHash;kernelSha256=$kernelHash;checks=$checks;cases=$cases;managers=$managers;errors=$errors}|ConvertTo-Json -Depth 10|Set-Content (Join-Path $ResultsDir 'summary.json') -Encoding utf8
if(-not $okay){throw 'R5-003 evidence failed; preserve primary API errors and all raw records'}
Write-Output 'R5_003_NORMAL_EXIT_RESTART_PASSED'
