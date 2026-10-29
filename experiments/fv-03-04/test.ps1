param([string]$OutDir='C:/fv-c/live/bin',[string]$ResultsDir='C:/fv-c/live/run',[ValidateSet('full','minimal','manual')][string]$Suite='full')
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$OutDir=[IO.Path]::GetFullPath($OutDir);$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(Test-Path $ResultsDir){throw 'Evidence directory must be new'}
New-Item -ItemType Directory $ResultsDir|Out-Null
. "$PSScriptRoot/../r5-004/verification.ps1"
# Audit starts only after the supervisor confirms writer exit. Cache immutable
# JSON rows, not assertions or results, to avoid reparsing each view log per PNG.
$readRows=(Get-Command Rows -CommandType Function).ScriptBlock
$rowCache=@{}
function Rows([string]$Path){
 $key=[IO.Path]::GetFullPath($Path)
 if(-not $rowCache.ContainsKey($key)){$rowCache[$key]=@(& $readRows $key)}
 $rowCache[$key]
}
$checks=[Collections.Generic.List[object]]::new();$cases=[Collections.Generic.List[object]]::new();$errorText=''
[ordered]@{startedUtc=[DateTime]::UtcNow.ToString('o');testSha256=(Get-FileHash $PSCommandPath).Hash.ToLowerInvariant();buildSha256=(Get-FileHash "$OutDir/build.json").Hash.ToLowerInvariant();scriptSourceHead=(& git -C $PSScriptRoot rev-parse HEAD);os=[Environment]::OSVersion.VersionString;architecture=$env:PROCESSOR_ARCHITECTURE;boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o')}|ConvertTo-Json|Set-Content "$ResultsDir/test-identity.json" -Encoding utf8
$frozen=@{};Get-ChildItem $OutDir -File|Where-Object Extension -in @('.exe','.dll','.lib','.obj')|ForEach-Object{$frozen[$_.Name]=Sha $_.FullName}
function Frozen{foreach($file in $frozen.Keys){Check "frozen.$file" ((Sha "$OutDir/$file") -ceq $frozen[$file])}}
function Run-Case([string]$App,[string]$Scenario){
 $case=Join-Path $ResultsDir "$App-$Scenario";New-Item -ItemType Directory $case|Out-Null;$manualResult='not_executed'
 $exe=if($App -eq 'fixture'){'fixture.exe'}else{'second.exe'};$store="$case/store";$previous='';$fixed='';$casePassed=$false
 try{
  foreach($phase in @('save','restore')){
   $root="$case/$phase";New-Item -ItemType Directory $root|Out-Null
   $kill=if($phase -eq 'restore' -and $Scenario -eq 'live-manager-exit'){'live-ready'}else{''}
   $scenarioArg=if($phase -eq 'restore'){$Scenario}else{'positive'}
   & "$OutDir/controller.exe" --manager "$OutDir/manager.exe" --client "$OutDir/$App-client.dll" --fixture "$OutDir/$exe" --mode $phase --store $store --manager-output "$root/manager" --output "$root/supervisor" --kill-at $kill --previous $previous --scenario $scenarioArg *> "$root/console.log"
   $controllerExit=$LASTEXITCODE
   if(-not(Test-Path "$root/supervisor/summary.json")){throw "Missing supervisor summary; primary log: $root/console.log"}
   $s=Get-Content -Raw "$root/supervisor/summary.json"|ConvertFrom-Json
   Check "$App.$Scenario.$phase.controller" ($controllerExit -eq 0)
   $rows=Crash-Audit "$root/manager" "$root/supervisor" "$App.$Scenario.$phase" ([bool]$kill)
   $expected=if($phase -eq 'restore' -and $Scenario -eq 'live-input-exit'){2}elseif($kill){[Convert]::ToUInt32('E5040001',16)}else{0}
   Check "$App.$Scenario.$phase.manager" ($s.managerExit -eq $expected)
   if($controllerExit -ne 0 -or $s.managerExit -ne $expected){throw "Operation failed: $root/manager/controller.jsonl"}
   if($phase -eq 'save'){$previous="$root/supervisor/owner.fvc";$fixed=InventoryText $store;$saveRows=$rows;continue}
   Check "$App.$Scenario.fixed-store" ((InventoryText $store) -ceq $fixed)
   if($kill){Check "$App.$Scenario.actual-preview-owned-at-kill" (@($rows|Where-Object {$_.type -eq 'process_created' -and $_.name -like 'preview-*'}).Count -eq 1);continue}
   $live=@($rows|Where-Object type -eq 'live_complete')
   if($Scenario -eq 'live-input-exit'){
    $inputFailure=@($rows|Where-Object type -eq 'live_input_failure');$primaryFailure=@($rows|Where-Object type -eq 'experiment_failure')
    Check "$App.$Scenario.original-input-error" ($inputFailure.Count -eq 1 -and $primaryFailure.Count -eq 1 -and $primaryFailure[0].api -ceq $inputFailure[0].api -and $primaryFailure[0].error -eq $inputFailure[0].error -and $live.Count -eq 0)
    Check "$App.$Scenario.other-Views-survived" (@($rows|Where-Object type -eq 'live_other_views_survived_input_failure').Count -eq 1 -and @($rows|Where-Object type -eq 'live_secondary_error').Count -eq 0)
    continue
   }
   $native=Audit "$root/manager" "$App.$Scenario.native"
   Check-Restore "$App.$Scenario.restore" $App "$case/save/manager" "$root/manager" $saveRows $native
   if($Scenario -eq 'positive'){Check "$App.default-unchanged" (@($rows|Where-Object type -eq 'live_started').Count -eq 0);continue}
   Check "$App.$Scenario.live-completed" ($live.Count -eq 1 -and $live[0].automaticPassed -and $live[0].previewCloseAndCrashIndependent)
   $manualResult=$live[0].manualHostTest
   $cycles=@($rows|Where-Object type -eq 'live_cycle')
   foreach($label in @('no-preview','preview-open','preview-closed','preview-crashed','preview-reopened','all-previews-exited')){Check "$App.$Scenario.phase-$label" (@($cycles|Where-Object phase -eq $label).Count -gt 0)}
   Check "$App.$Scenario.all-Views-operated" (@($cycles.target|Sort-Object -Unique).Count -eq 3)
   $frames=@($rows|Where-Object type -eq 'live_frame')
   foreach($frame in $frames){
    Check "$App.Frame.$($frame.view).$($frame.sequence).bytes" ((Sha $frame.file) -ceq $frame.sha256)
    Check "$App.Frame.$($frame.view).$($frame.sequence).provenance" ($frame.created -gt 0 -and $frame.endMs -ge $frame.beginMs -and @($native|Where-Object {$_.type -eq 'capture' -and $_.view -eq $frame.view -and $_.name -eq "live-frame-$($frame.sequence)" -and $_.counter -eq $frame.counter}).Count -eq 1)
   }
   $previews=@(Get-ChildItem "$root/manager/preview-*.jsonl")
   Check "$App.$Scenario.three-preview-lifetimes" ($previews.Count -eq 3)
   foreach($preview in $previews){
    $p=Rows $preview.FullName;$ready=@($p|Where-Object type -eq 'aux_ready');$paint=@($p|Where-Object type -eq 'preview_frame_painted')
    Check "$App.$Scenario.$($preview.BaseName).read-only-paint" ($ready.Count -eq 1 -and $ready[0].feedReadOnly -and $paint.Count -gt 0)
    foreach($f in $paint){Check "$App.$Scenario.preview-source-$($f.view)-$($f.sequence)" (@($frames|Where-Object {$_.view -eq $f.view -and $_.pid -eq $f.pid -and $_.created -eq $f.created -and $_.sequence -eq $f.sequence -and $_.sha256 -ceq $f.sha256}).Count -eq 1 -and $f.paintTick -ge $f.endMs -and $f.paintTick-$f.endMs -le 3000)}
   }
   $closed=@($rows|Where-Object {$_.type -eq 'live_aux_exit' -and -not $_.forced})
   $reopened=Rows "$root/manager/preview-2.jsonl";$firstFresh=@($reopened|Where-Object type -eq 'preview_frame_painted')[0]
   Check "$App.$Scenario.reopen-new-capture-not-cached" ($firstFresh.beginMs -gt $closed[0].tickMs -and $firstFresh.sequence -gt @((Rows "$root/manager/preview-0.jsonl")|Where-Object {$_.type -eq 'preview_frame_painted' -and $_.view -eq 'RA'})[-1].sequence)
   $hostSamples=@($rows|Where-Object type -eq 'live_host_sample');$quietChanges=0;$externalIntervals=0
   for($i=1;$i -lt $hostSamples.Count;$i++){if($hostSamples[$i].lastInput -ne $hostSamples[$i-1].lastInput){++$externalIntervals;continue};if($hostSamples[$i].foreground -ne $hostSamples[$i-1].foreground -or $hostSamples[$i].inputDesktop -cne $hostSamples[$i-1].inputDesktop -or $hostSamples[$i].cursorX -ne $hostSamples[$i-1].cursorX -or $hostSamples[$i].cursorY -ne $hostSamples[$i-1].cursorY){++$quietChanges}}
   [ordered]@{samples=$hostSamples.Count;quietChanges=$quietChanges;externalIntervals=$externalIntervals;classification=$(if($quietChanges -or $externalIntervals){'inconclusive'}else{'no_change_in_bounded_samples'});manualHostTest=$manualResult;continuousHostGuarantee=$false}|ConvertTo-Json|Set-Content "$root/manager/host-assessment.json" -Encoding utf8
   if($Suite -ne 'manual'){Check "$App.$Scenario.manual-not-claimed" ($live[0].manualHostTest -eq 'not_executed')}
  }
  $casePassed=$true
 }finally{$cases.Add([ordered]@{application=$App;scenario=$Scenario;root=$case;operationExpectedOutcome=$casePassed;manualHostTest=$manualResult})}
}
try{
 Frozen
 if($Suite -eq 'manual'){Run-Case fixture live-manual}
 else{
  Run-Case fixture live-auto
  if($Suite -eq 'full'){Run-Case recorder live-auto;Run-Case fixture positive;Run-Case recorder positive;Run-Case fixture live-input-exit;Run-Case recorder live-manager-exit}
 }
 Frozen
}catch{$errorText=$_.ToString();Write-Output $errorText}
finally{
 $passed=($cases.Count -gt 0 -and -not $errorText -and @($checks|Where-Object {-not $_.passed}).Count -eq 0)
 [ordered]@{experiment='FV-03/FV-04-restored-interaction';suite=$Suite;sourceHead=(& git -C $PSScriptRoot rev-parse HEAD);passed=$passed;checks=$checks;cases=$cases;error=$errorText;manualHostTest=$(if(@($cases|Where-Object manualHostTest -eq 'passed').Count){'passed'}elseif(@($cases|Where-Object manualHostTest -eq 'not_verified').Count){'not_verified'}else{'not_executed'});fullNestedRegressionExecuted=$false;cleanupCausalFixVerified=$false;fullR5004Passed=$false;gateClosed=$false;binaryInventory=$frozen}|ConvertTo-Json -Depth 10|Set-Content "$ResultsDir/summary.json" -Encoding utf8
}
if(-not $passed){throw "Acceptance failed: $ResultsDir/summary.json"}
