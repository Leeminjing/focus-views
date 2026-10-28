param([ValidateSet('fixture','recorder')][string]$Application='fixture',[string]$ResultsDir='D:/fv/b/p1',[string]$OutDir=(Join-Path $PSScriptRoot 'out'),[string]$KillRestore='',[string]$Scenario='positive',[switch]$Legacy,[switch]$SameBase)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$OutDir=[IO.Path]::GetFullPath($OutDir);$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(Test-Path -LiteralPath $ResultsDir){throw 'Evidence directory must be new'}
New-Item -ItemType Directory -Path $ResultsDir|Out-Null
. (Join-Path $PSScriptRoot '../r5-004/verification.ps1')
$checks=[Collections.Generic.List[object]]::new();$operations=[Collections.Generic.List[object]]::new()
$store=Join-Path $ResultsDir 'store';$appFile=if($Application -eq 'fixture'){'fixture.exe'}else{'second.exe'}
$source=Join-Path $OutDir $appFile
if($Legacy){$source=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../r5-003/out/$appFile"))}
$taskNameHash=[Security.Cryptography.SHA256]::Create();try{$taskCopyName='r5004b-'+[Convert]::ToHexString($taskNameHash.ComputeHash([Text.Encoding]::UTF8.GetBytes($ResultsDir))).Substring(0,24)+'.exe'}finally{$taskNameHash.Dispose()}
$copy=Join-Path (Split-Path $source) $taskCopyName
if($SameBase){$copy=$source}else{if(Test-Path -LiteralPath $copy){throw 'Private loader copy must be new; no retry'};Copy-Item -LiteralPath $source -Destination $copy}
Check 'same-EXE-bytes-no-relink' ((Sha $source) -eq (Sha $copy))
Check 'same-working-and-DLL-search-directory' ((Split-Path $source) -ceq (Split-Path $copy))
function Phase([string]$Mode,[string]$App,[string]$Previous,[string]$Stage,[string]$Kill='',[string]$Fault='positive'){
 $root=Join-Path $ResultsDir $Stage;New-Item -ItemType Directory -Path $root|Out-Null
 & "$OutDir/controller.exe" --manager "$OutDir/manager.exe" --client "$OutDir/$Application-client.dll" --fixture $App --mode $Mode --store $store --manager-output "$root/manager" --output "$root/supervisor" --kill-at $Kill --previous $Previous --scenario $Fault *> "$root/console.log"
 $exit=$LASTEXITCODE;$summary=Get-Content -Raw "$root/supervisor/summary.json"|ConvertFrom-Json
 $r=[pscustomobject]@{manager="$root/manager";supervisor="$root/supervisor";certificate="$root/supervisor/owner.fvc";managerExit=$summary.managerExit;controllerExit=$exit;managerPid=$summary.managerPid}
 $operations.Add($r);if($exit -ne 0){throw "Supervisor failed ($exit): $root/supervisor/summary.json"};return $r
}
$errorText='';$passed=$false
try {
 $save=Phase save $source '' save
 $sourceRows=Crash-Audit $save.manager $save.supervisor 'save' $false
 Check 'save-native-manager-completed' ($save.managerExit -eq 0)
 if($save.managerExit -ne 0){throw 'Save failed; inspect primary controller/recovery logs'}
 $fixed=InventoryText $store
 $restore=Phase restore $copy $save.certificate restore $KillRestore $Scenario
 $rows=Crash-Audit $restore.manager $restore.supervisor 'restore' ([bool]$KillRestore)
 if($Scenario -like 'relocation-*'){
  $refusals=@($rows|Where-Object type -eq 'view_restore_rejected');$abort=@($rows|Where-Object type -eq 'abort_native_window_cleanup');$survivor=@($rows|Where-Object type -eq 'other_view_survived_rejection')
  $expectedApi=switch($Scenario){relocation-missing-return{'relocation.return_slot_registry'}relocation-bad-offset{'relocation.reference_shape'}relocation-missing-reference{'relocation.source_reference_ABI'}relocation-wrong-image{'restore.build_or_image_identity'}}
  Check 'fault-rejected-before-source-write' ($restore.managerExit -eq 2 -and $refusals.Count -eq 1 -and $refusals[0].view -eq 'RB' -and $refusals[0].api -eq $expectedApi -and @($rows|Where-Object {$_.type -eq 'restore_source_write_begin' -and $_.view -eq 'RB'}).Count -eq 0)
  Check 'fault-target-native-cleanup-confirmed' ($abort.Count -eq 1 -and $abort[0].view -eq 'RB' -and $abort[0].verified -and $abort[0].goneMask -eq 7)
  Check 'already-restored-RA-still-runs-and-paints' ($survivor.Count -eq 1 -and $survivor[0].survivor -eq 'RA' -and $survivor[0].alive -and $survivor[0].nativeObservationAndFrame)
  Check 'rejected-object-never-published' (@($rows|Where-Object {$_.type -eq 'view_published' -and $_.view -eq 'RB'}).Count -eq 0 -and -not(Test-Path "$($restore.manager)/workspace/RB"))
  Check 'fixed-store-unchanged-after-refusal' ((InventoryText $store) -ceq $fixed)
  $restore=Phase restore $copy $restore.certificate fresh
  $rows=Crash-Audit $restore.manager $restore.supervisor 'fresh' $false
 }
 if($KillRestore){
  Check 'committed-store-unchanged-after-restore-crash' ((InventoryText $store) -ceq $fixed)
  $restore=Phase restore $copy $restore.certificate fresh
  $rows=Crash-Audit $restore.manager $restore.supervisor 'fresh' $false
 }
 Check 'restore-native-manager-completed' ($restore.managerExit -eq 0)
 if($restore.managerExit -ne 0){throw 'Restore failed; inspect primary controller/recovery logs'}
 $native=Audit $restore.manager 'native'
 Check-Restore 'restored' $Application $save.manager $restore.manager $sourceRows $native
 $plans=@($rows|Where-Object type -eq 'image_reassociation_prepared')
 if($SameBase){Check 'legacy-same-base-no-implicit-relocation' ($plans.Count -eq 0)}else{
  Check 'all-three-Views-actually-rebased' ($plans.Count -eq 3 -and @($plans|Where-Object {$_.sourceBase -eq $_.targetBase -or $_.shadowStackWritten -or $_.sourceStateWritten -or -not $_.sourceSnapshotUnchanged}).Count -eq 0)
  & python (Join-Path $PSScriptRoot 'audit-relocation.py') $save.manager $restore.manager "$ResultsDir/relocation-byte-audit.json"
  Check 'independent-raw-byte-reassociation-audit' ($LASTEXITCODE -eq 0)
 }
 Check 'fixed-store-unchanged' ((InventoryText $store) -ceq $fixed)
 $passed=@($checks|Where-Object {-not $_.passed}).Count -eq 0
}catch{$errorText=$_.ToString();Write-Output $errorText}finally{
 [ordered]@{application=$Application;scenario=$Scenario;killRestore=$KillRestore;legacy=[bool]$Legacy;sameBase=[bool]$SameBase;sourceHead=(& git -C $PSScriptRoot rev-parse HEAD);sourceExe=$source;copyExe=$copy;workingDirectory=(Split-Path $source);sourceExeSha256=(Sha $source);copyExeSha256=(Sha $copy);checks=$checks;operations=$operations;error=$errorText;passed=$passed;aslrRelocationVerified=($passed -and -not $SameBase);hostRebootRecoveryVerified=$false;gateClosed=$false}|ConvertTo-Json -Depth 8|Set-Content "$ResultsDir/summary.json" -Encoding utf8
}
if(-not $passed){throw "R5-004b case failed: $ResultsDir/summary.json"}
