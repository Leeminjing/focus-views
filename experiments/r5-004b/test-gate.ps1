# R5-004 matrix reused with the opt-in R5-004b build and disk-v2 oracle.
param([string]$OutDir=(Join-Path $PSScriptRoot 'out'),[string]$ResultsDir=(Join-Path $PSScriptRoot 'results/gate-1'),[ValidateRange(1,2)][int]$Repeats=2,[switch]$SkipRegression)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$OutDir=[IO.Path]::GetFullPath($OutDir);$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(Test-Path -LiteralPath $ResultsDir){throw 'Preserve previous runs; choose a new short ResultsDir'}
New-Item -ItemType Directory -Path $ResultsDir|Out-Null
. (Join-Path $PSScriptRoot 'verification.ps1')
$checks=[Collections.Generic.List[object]]::new();$cases=[Collections.Generic.List[object]]::new();$errors=[Collections.Generic.List[object]]::new();$operations=[Collections.Generic.List[object]]::new()
$testSha=Sha $PSCommandPath;$build=Get-Content -Raw (Join-Path $OutDir 'build.json')|ConvertFrom-Json
[ordered]@{time=(Get-Date -Format o);sourceHead=(& git -C $PSScriptRoot rev-parse HEAD);testSha256=$testSha;verificationSha256=(Sha (Join-Path $PSScriptRoot 'verification.ps1'));reusedOracleSha256=(Sha $verificationSource);repeats=$Repeats;skipRegression=[bool]$SkipRegression;system=[Environment]::OSVersion.VersionString;manualHostTest='not_executed'}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $ResultsDir 'identity.json') -Encoding utf8
Copy-Item (Join-Path $OutDir 'build.json') (Join-Path $ResultsDir 'build.json')
$legacy=Join-Path $ResultsDir 'reg3/r5-002-regression/r5-001b-regression/r5-001a-regression/r4-003-001-regression/r4-002a-regression/c001-regression/b001-regression/cet002-regression/register-1/target-active-stack-before-continue.bin'
if(-not $SkipRegression -and $legacy.Length -ge 260){[ordered]@{allEvidenceChecksPassed=$false;stage='legacy-path-preflight';length=$legacy.Length;applicationsStarted=$false}|ConvertTo-Json|Set-Content (Join-Path $ResultsDir 'summary.json');throw 'Use a shorter ResultsDir for legacy native evidence'}
$latestOwners=@{}
function Run-Supervised([string]$App,[string]$Mode,[string]$Root,[string]$Store,[string]$Kill='',[string]$Previous='',[string]$Scenario='positive',[switch]$ExactPrevious){
 if($Previous -and -not $ExactPrevious -and $latestOwners.ContainsKey($Store)){$Previous=$latestOwners[$Store]}
 $manager=Join-Path $Root 'manager';$supervisor=Join-Path $Root 'supervisor';New-Item -ItemType Directory -Path $Root|Out-Null
 $target=Join-Path $OutDir $(if($App -eq 'fixture'){'fixture.exe'}else{'second.exe'})
 & (Join-Path $OutDir 'controller.exe') --manager (Join-Path $OutDir 'manager.exe') --client (Join-Path $OutDir "$App-client.dll") --fixture $target --mode $Mode --store $Store --manager-output $manager --output $supervisor --kill-at $Kill --previous $Previous --scenario $Scenario *> (Join-Path $Root 'supervisor-console.log')
 $exit=$LASTEXITCODE;if(-not(Test-Path (Join-Path $supervisor 'summary.json'))){throw "Supervisor exited $exit without summary; retain native stderr"};$s=Get-Content -Raw (Join-Path $supervisor 'summary.json')|ConvertFrom-Json
 $result=[pscustomobject]@{app=$App;mode=$Mode;point=$Kill;root=$Root;manager=$manager;supervisor=$supervisor;certificate=(Join-Path $supervisor 'owner.fvc');managerPid=$s.managerPid;managerCreated=$s.managerCreated;managerExit=$s.managerExit;controllerExit=$exit}
 $operations.Add($result);if($exit -ne 0){throw "Supervisor protocol/cleanup failed; preserve summary $supervisor"};$latestOwners[$Store]=$result.certificate;return $result
}
function Safe-Case([string]$Label,[string]$Kind,[scriptblock]$Body){try{& $Body;$passed=@($checks|Where-Object{$_.name -like "$Label.*" -and -not $_.passed}).Count -eq 0;$cases.Add([pscustomobject]@{case=$Label;kind=$Kind;passed=$passed})}catch{$errors.Add([pscustomobject]@{case=$Label;error=$_.ToString();scriptStack=$_.ScriptStackTrace});Check "$Label.evidence-complete" $false;$cases.Add([pscustomobject]@{case=$Label;kind=$Kind;passed=$false})}}
function Complete-Restore([string]$Label,[string]$App,[object]$Save,[object]$Restore,[object[]]$SourceRows,[bool]$Failure=$false){
 $m=Crash-Audit $Restore.manager $Restore.supervisor "$Label.owner" $false
 if($Restore.managerExit -ne 0){
  Check "$Label.manager-normal-return" $false
  $primary=@($m+@(Optional-Rows (Join-Path $Restore.manager 'recovery.jsonl'))|Where-Object type -in @('experiment_failure','recovery_failure'))
  $detail=if($primary.Count){$primary[-1]|ConvertTo-Json -Compress}else{'no structured primary error; inspect native stderr'}
  throw "Restore failed before dependent evidence: $detail"
 }
 if($Failure){$closed=@($m|Where-Object {$_.type -eq 'unpublished_window_discarded' -and $_.view -eq 'RB' -and $_.nativeCloseAndAbsenceConfirmed});$native=@($m|Where-Object type -eq 'discard_window_native_result');Check "$Label.failed-window-native-close-and-absence" ($closed.Count -eq 1 -and @($native|Where-Object {$_.operation -eq 'send' -and $_.api -eq 'SendMessageTimeoutW' -and $_.error -eq 0 -and $_.exit -eq 0}).Count -eq 1 -and @($native|Where-Object {$_.operation -eq 'observe' -and $_.api -eq 'validate_target_window' -and $_.error -eq 1400 -and $_.exit -eq 2}).Count -eq 1)}
 $normal=Audit $Restore.manager "$Label.native";Check "$Label.manager-normal-return" (@($m|Where-Object{$_.type -eq 'restart_complete' -and $_.passed}).Count -eq 1)
 Check "$Label.old-owner-independently-rechecked" (@(Rows (Join-Path $Restore.manager 'recovery.jsonl')|Where-Object{$_.type -eq 'predecessor_verified' -and $_.allExited -and $_.allDesktopsAbsent}).Count -eq 1)
 Check-Restore $Label $App $Save.manager $Restore.manager $SourceRows $normal $Failure
 return ,$m
}
Safe-Case 'ownership-lock-refusal' 'controller-failure-summary' {
 $root=Join-Path $ResultsDir 'ownership-lock-refusal';$store=Join-Path $root 'store';New-Item -ItemType Directory -Path ($store+'.owners')|Out-Null
 $lock=[IO.File]::Open(($store+'.owners/lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
 try{& (Join-Path $OutDir 'controller.exe') --manager (Join-Path $OutDir 'manager.exe') --client (Join-Path $OutDir 'fixture-client.dll') --fixture (Join-Path $OutDir 'fixture.exe') --mode restore --store $store --manager-output (Join-Path $root 'manager') --output (Join-Path $root 'supervisor') *> (Join-Path $root 'console.log');$exit=$LASTEXITCODE}finally{$lock.Dispose()}
 $s=Get-Content -Raw (Join-Path $root 'supervisor/summary.json')|ConvertFrom-Json;Check 'ownership-lock-refusal.precise-primary-error-no-application' ($exit -ne 0 -and -not $s.applicationsStarted -and $s.managerPid -eq 0 -and $s.api -eq 'CreateFileW.session_owner_lock' -and $s.error -eq 32 -and -not(Test-Path (Join-Path $root 'manager')))
}
$pre=@('staging-half:S0.fvs','staging-verified:S0.fvs','published:S0.fvs','artifact-published:A-v1','published:SA.fvs','published:SB.fvs','staging-half:session.fvs','staging-verified:session.fvs')
foreach($app in @('fixture','recorder')){
 $baseSave=$null;$baseRows=@();$baseStore='';$partial=''
 for($repeat=1;$repeat -le $Repeats;++$repeat){
  $index=0;foreach($point in $pre){$index++;$label="$app-pre-$index-$repeat";$root=Join-Path $ResultsDir $label
   Safe-Case $label 'uncommitted-rejection' {
    $store=Join-Path $root 'store';$save=Run-Supervised $app save (Join-Path $root 'save') $store $point
    $source=Crash-Audit $save.manager $save.supervisor "$label.kill" $true
    Check "$label.no-committed-session" (-not(Test-Path (Join-Path $store 'session.fvs')))
    $fixed=InventoryText $store;$restore=Run-Supervised $app restore (Join-Path $root 'reject') $store '' $save.certificate
    $m=Crash-Audit $restore.manager $restore.supervisor "$label.reject" $false;$failure=@(Rows (Join-Path $restore.manager 'recovery.jsonl')|Where-Object type -eq 'recovery_failure')
    Check "$label.precise-uncommitted-refusal" ($restore.managerExit -ne 0 -and $failure.Count -eq 1 -and $failure[0].api -eq 'recovery.session_uncommitted' -and @($m|Where-Object type -eq 'view_published').Count -eq 0)
    Check "$label.orphan-completed-versions-unchanged" ((InventoryText $store) -ceq $fixed)
    $q=@(Rows (Join-Path $save.supervisor 'supervisor.jsonl')|Where-Object type -eq 'partial_quarantined')
    Check "$label.actual-partial-when-injected" ($point -notlike 'staging-*' -or $q.Count -eq 1)
    if($point -eq 'staging-verified:S0.fvs' -and $repeat -eq 1){$script:capturedPartial=$q[0].file}
   }
  }
  foreach($point in @('published:session.fvs','session-committed')){$suffix=if($point -eq 'session-committed'){'logged'}else{'rename'};$label="$app-committed-$suffix-$repeat";$root=Join-Path $ResultsDir $label
   Safe-Case $label 'committed-crash-restored' {
    $store=Join-Path $root 'store';$save=Run-Supervised $app save (Join-Path $root 'save') $store $point;$source=Crash-Audit $save.manager $save.supervisor "$label.kill" $true
    foreach($name in @('S0','SA','SB','session')){Check-Packet (Join-Path $store "$name.fvs") "$label.$name"}
    Check "$label.AB-alive-at-forced-death" (@($source|Where-Object type -eq 'process_exited'|Where-Object name -in @('A','B')).Count -eq 0 -and @($source|Where-Object type -eq 'snapshot_disk_published').Count -eq 3)
    $fixed=InventoryText $store;$restore=Run-Supervised $app restore (Join-Path $root 'restore') $store '' $save.certificate
    $null=Complete-Restore "$label.restored" $app $save $restore $source
    Check "$label.fixed-store-byte-identical" ((InventoryText $store) -ceq $fixed)
    if($point -eq 'session-committed' -and $repeat -eq 1){$script:committedSave=$save;$script:committedRows=$source;$script:committedStore=$store}
   }
  }
 }
 if(-not(Get-Variable committedSave -Scope Script -ErrorAction SilentlyContinue)){continue};$baseSave=$script:committedSave;$baseRows=$script:committedRows;$baseStore=$script:committedStore;if(Get-Variable capturedPartial -Scope Script -ErrorAction SilentlyContinue){$partial=$script:capturedPartial}
 foreach($point in @('view-resources:RA','view-published:RA','view-published:RB')){$label="$app-recrash-"+($point -replace ':','-');$root=Join-Path $ResultsDir $label
  Safe-Case $label 'restore-crash-then-new-operation' {
   $fixed=InventoryText $baseStore;$interrupted=Run-Supervised $app restore (Join-Path $root 'interrupted') $baseStore $point $baseSave.certificate
   $prior=Crash-Audit $interrupted.manager $interrupted.supervisor "$label.second-kill" $true
   Check "$label.interrupted-Views-accurate" (@($prior|Where-Object type -eq 'view_published').Count -eq $(if($point -eq 'view-resources:RA'){0}elseif($point -eq 'view-published:RA'){1}else{2}))
   Check "$label.commit-survives-second-crash" ((InventoryText $baseStore) -ceq $fixed)
   $fresh=Run-Supervised $app restore (Join-Path $root 'explicit-new-operation') $baseStore '' $interrupted.certificate
   $null=Complete-Restore "$label.new-operation" $app $baseSave $fresh $baseRows
   Check "$label.new-independent-manager-instance" ($fresh.managerCreated -gt $interrupted.managerCreated -and $fresh.managerPid -ne $interrupted.managerPid)
   Check "$label.no-fixed-data-mutation" ((InventoryText $baseStore) -ceq $fixed)
  }
 }
 foreach($fault in @('extra-partial','foreign-generation','truncated','missing-session','stale-owner','wrong-session','missing-owner-proof','resource-failure')){$label="$app-fault-$fault";$root=Join-Path $ResultsDir $label
  Safe-Case $label 'recovery-fault-or-isolation' {
   $store=$baseStore;$fixed=InventoryText $baseStore;$scenario='positive'
   if($fault -notin @('resource-failure','stale-owner')){New-Item -ItemType Directory -Path $root|Out-Null;$store=Join-Path $root 'copy';Copy-Item -LiteralPath $baseStore -Destination $store -Recurse
    switch($fault){
     missing-owner-proof {New-Item -ItemType Directory -Path ($store+'.owners')|Out-Null;$record=Get-ChildItem -LiteralPath ($baseStore+'.owners') -Filter '*.owner'|Select-Object -First 1;Copy-Item -LiteralPath $record.FullName -Destination ($store+'.owners')}
     extra-partial {if(-not $partial){throw 'Actual staging-verified partial not available'};Copy-Item -LiteralPath $partial -Destination (Join-Path $store (Split-Path $partial -Leaf))}
     truncated {$path=Join-Path $store 'SA.fvs';$b=[IO.File]::ReadAllBytes($path);[IO.File]::WriteAllBytes($path,$b[0..([int]($b.Length/2))])}
     missing-session {Remove-Item -LiteralPath (Join-Path $store 'session.fvs')}
    }
   }elseif($fault -eq 'resource-failure'){$scenario='resource-failure'};if($fault -eq 'foreign-generation'){$scenario='foreign-generation'}
   $before=InventoryText $store;$previous=$baseSave.certificate
   if($fault -eq 'wrong-session'){$previous=Join-Path $ResultsDir "$app-committed-rename-1/save/supervisor/owner.fvc"}
   $restore=Run-Supervised $app restore (Join-Path $root 'restore') $store '' $previous $scenario -ExactPrevious:($fault -in @('stale-owner','wrong-session'))
   if($fault -in @('extra-partial','foreign-generation','resource-failure')){$null=Complete-Restore $label $app $baseSave $restore $baseRows ($fault -eq 'resource-failure')}
   else{$m=Crash-Audit $restore.manager $restore.supervisor $label $false;$recovery=@(Optional-Rows (Join-Path $restore.manager 'recovery.jsonl'));$primary=@($m+$recovery|Where-Object type -in @('experiment_failure','recovery_failure'));$api=switch($fault){truncated {'snapshot.blob_length'};missing-session {'recovery.session_uncommitted'};stale-owner {'recovery.stale_owner_certificate'};wrong-session {'recovery.owner_session_mismatch'};missing-owner-proof {'recovery.owner_cleanup_unconfirmed'}};Check "$label.refused-before-View" ($restore.managerExit -ne 0 -and @($primary|Where-Object api -eq $api).Count -eq 1 -and @($m|Where-Object type -eq 'view_published').Count -eq 0)}
   if($fault -eq 'foreign-generation'){$events=Rows (Join-Path $restore.supervisor 'supervisor.jsonl');$probe=@($events|Where-Object type -eq 'foreign_generation_probe_created');$ignored=@($events|Where-Object type -eq 'foreign_partial_ignored');Check "$label.same-PID-different-birth-never-quarantined" ($probe.Count -eq 1 -and @($ignored|Where-Object path -eq $probe[0].file).Count -eq 1 -and (Test-Path -LiteralPath $probe[0].file));$unchanged=@(Inventory $store|Where-Object file -ne (Split-Path $probe[0].file -Leaf))|ConvertTo-Json -Compress -Depth 4;Check "$label.fixed-files-unchanged-with-foreign-generation" ($unchanged -ceq $before)}
   if($fault -ne 'foreign-generation'){Check "$label.store-unchanged" ((InventoryText $store) -ceq $before)};Check "$label.original-version-unchanged" ((InventoryText $baseStore) -ceq $fixed)
  }
 }
 Remove-Variable committedSave,committedRows,committedStore,capturedPartial -Scope Script -ErrorAction SilentlyContinue
}
Check 'test-source-fixed' ((Sha $PSCommandPath) -eq $testSha)
$body=$errors.Count -eq 0 -and @($checks|Where-Object{-not $_.passed}).Count -eq 0;$regression='not_run'
if(-not $SkipRegression){try{& (Join-Path $PSScriptRoot '../r5-003/test.ps1') -ResultsDir (Join-Path $ResultsDir 'reg3')|Tee-Object -FilePath (Join-Path $ResultsDir 'regression-console.log');$old=Get-Content -Raw (Join-Path $ResultsDir 'reg3/summary.json')|ConvertFrom-Json;$regression=if($old.allEvidenceChecksPassed -and $old.regressions -eq 'passed'){'passed'}else{'failed'};Check 'R5-003-and-complete-nested-regressions' ($regression -eq 'passed')}catch{$regression='failed';$errors.Add([pscustomobject]@{case='regression';error=$_.ToString()});Check 'R5-003-and-complete-nested-regressions' $false}}
$okay=$errors.Count -eq 0 -and @($checks|Where-Object{-not $_.passed}).Count -eq 0
[ordered]@{allEvidenceChecksPassed=$okay;crashRecoveryVerified=$body;committedRecovery=@($cases|Where-Object kind -eq 'committed-crash-restored');uncommittedRefusals=@($cases|Where-Object kind -eq 'uncommitted-rejection');interruptedRestore=@($cases|Where-Object kind -eq 'restore-crash-then-new-operation');regressions=$regression;gateClosed=$false;hostRebootRecoveryVerified=$false;powerLossRecoveryVerified=$false;manualHostTest='not_executed';checks=$checks;cases=$cases;operations=$operations;errors=$errors}|ConvertTo-Json -Depth 10|Set-Content (Join-Path $ResultsDir 'summary.json') -Encoding utf8
if(-not $okay){throw 'R5-004 evidence failed; native failures retained'}
Write-Output 'R5_004_MANAGER_CRASH_RECOVERY_PASSED'
