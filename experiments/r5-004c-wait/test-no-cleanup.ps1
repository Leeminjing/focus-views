param([Parameter(Mandatory)][string]$OutDir,[Parameter(Mandatory)][string]$Profile,[Parameter(Mandatory)][string]$ResultsDir,[switch]$DirectWindow)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'assessment.ps1')
. (Join-Path $PSScriptRoot 'wait-pattern.ps1')
$OutDir=[IO.Path]::GetFullPath($OutDir);$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(Test-Path -LiteralPath $ResultsDir){throw 'New results root required'}
New-Item -ItemType Directory -Path $ResultsDir|Out-Null
$boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime;$hash=(Get-FileHash $Profile).Hash;$scriptHash=(Get-FileHash $PSCommandPath).Hash;$assessmentHash=(Get-FileHash "$PSScriptRoot/assessment.ps1").Hash
$bins=@('manager.exe','second.exe','trim-client.dll','abort-runtime.dll','abort-helper.exe','close-probe.exe','diagnose.exe','kernel.lib')
$identity=@($bins|ForEach-Object{[ordered]@{file=$_;sha256=(Get-FileHash "$OutDir/$_").Hash.ToLowerInvariant()}})
$modes=if($DirectWindow){@('normal-new-window-direct')}else{@('profile-normal','normal-no-cleanup-runtime')};$cases=@()
foreach($mode in $modes){
 $root=Join-Path $ResultsDir $mode;New-Item -ItemType Directory -Path "$root/store"|Out-Null;Copy-Item -LiteralPath $Profile -Destination "$root/store/resource.bin"
 $errorText='';$exit=-1
 try{& "$OutDir/controller.exe" --manager "$OutDir/manager.exe" --client "$OutDir/trim-client.dll" --fixture "$OutDir/second.exe" --mode save --store "$root/store" --manager-output "$root/manager" --output "$root/supervisor" --kill-at '' --previous '' --scenario $mode *> "$root/console.log";$exit=$LASTEXITCODE}catch{$errorText=$_.ToString()}
 $s=if(Test-Path "$root/supervisor/summary.json"){Get-Content -Raw "$root/supervisor/summary.json"|ConvertFrom-Json}else{$null}
 $rows=if(Test-Path "$root/manager/controller.jsonl"){@(Get-Content "$root/manager/controller.jsonl"|ForEach-Object{$_|ConvertFrom-Json})}else{@()}
 $complete=@($rows|Where-Object type -eq 'trim_complete');$installation=@($rows|Where-Object type -eq 'cleanup_runtime_installation')
 $moduleStages=@($rows|Where-Object type -eq 'trim_module_observation');$helpers=@($rows|Where-Object{$_.type -eq 'process_created' -and $_.name -like '*abort-helper*'})
 $absent=$mode -ne 'profile-normal';$expectedStage=if($absent){'cleanup-runtime-skipped'}else{'cleanup-runtime-ready'}
 $last=@($moduleStages|Where-Object stage -eq $expectedStage)
 $attested=$installation.Count -eq 1 -and $installation[0].enabled -eq (-not $absent) -and $installation[0].registryCreated -eq (-not $absent) -and $installation[0].prepareCalled -eq (-not $absent) -and $last.Count -eq 1 -and $last[0].complete -and $last[0].cleanupRuntimeModules -eq $(if($absent){0}else{1}) -and $helpers.Count -eq $(if($absent){0}else{1})
 $diag="$root/manager/normal-diagnostic.jsonl";$wct=if(Test-Path $diag){@(Get-Content $diag|ForEach-Object{$_|ConvertFrom-Json})}else{@()}
 $prepared=@($rows|Where-Object type -eq 'trim_prepared');$pattern=[pscustomobject]@{sameWaitingPattern=$false;status='insufficient_evidence';reason='not-sampled-or-unconfirmed-target';links=$null}
 if($prepared.Count -eq 1){$target=$prepared[0];$gui=@($rows|Where-Object{$_.type -eq 'trim_thread_observation' -and $_.pid -eq $target.pid -and $_.tid -eq $target.tid -and $_.error -eq 0});if($gui.Count){$pattern=Get-LinkedTextServiceWait $wct $target.pid $target.created $target.tid $gui[-1].created}}
 $outcome=Get-NormalCloseOutcome $exit $s $complete
 $cases+=[ordered]@{mode=$mode;controllerExit=$exit;error=$errorText;supervisor=$s;trimComplete=$complete;evidenceComplete=$outcome.evidenceComplete;operationPassed=$outcome.operationPassed;installationAttested=$attested;installation=$installation;moduleStages=$moduleStages;cleanupHelperCount=$helpers.Count;nativeCleanupPassed=$outcome.nativeCloseObserved;sameWaitingPattern=$pattern.sameWaitingPattern;waitingEvidence=$pattern;profileUnchanged=((Get-FileHash "$root/store/resource.bin").Hash -eq $hash)}
}
$sameBuild=@($identity|Where-Object{(Get-FileHash "$OutDir/$($_.file)").Hash.ToLowerInvariant() -ne $_.sha256}).Count -eq 0
$evidence=$sameBuild -and $boot -eq (Get-CimInstance Win32_OperatingSystem).LastBootUpTime -and (Get-FileHash $PSCommandPath).Hash -eq $scriptHash -and (Get-FileHash "$PSScriptRoot/assessment.ps1").Hash -eq $assessmentHash -and @($cases|Where-Object{-not $_.installationAttested -or -not $_.profileUnchanged -or -not $_.evidenceComplete -or $_.error}).Count -eq 0
$native=@($cases|Where-Object{-not $_.nativeCleanupPassed}).Count -eq 0
$operation=$evidence -and @($cases|Where-Object{-not $_.operationPassed}).Count -eq 0
[ordered]@{evidenceComplete=$evidence;operationPassed=$operation;sameBuild=$sameBuild;boot=$boot;testSha256=$scriptHash;assessmentSha256=$assessmentHash;binaries=$identity;cases=$cases;nativeCleanupPassed=$native;cleanupCausalFixVerified=$false;fullR5004Passed=$false;gateClosed=$false;manualHostTest='not_executed'}|ConvertTo-Json -Depth 10|Set-Content "$ResultsDir/summary.json" -Encoding utf8
if(-not $operation){throw 'Normal-close operation failed; inspect manager exit, first error and independent cleanup'}
