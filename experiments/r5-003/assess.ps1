param([Parameter(Mandatory)][string]$ResultsDir)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$ResultsDir=[IO.Path]::GetFullPath($ResultsDir);$summary=Get-Content -Raw (Join-Path $ResultsDir 'summary.json')|ConvertFrom-Json
$totals=@{controllerAssertions=0;ownedProcesses=0;desktops=0;frames=0;publishedViews=0;inputSessions=0;diskSnapshotsPublished=0;diskSnapshotsLoaded=0}
$managerProof=@();$snapshotProof=@();$probes=@()
foreach($manager in $summary.managers){
 $dir=$manager.output;if(-not(Test-Path $dir)){$dir+='-offline'}
 $rows=@(Get-Content -LiteralPath (Join-Path $dir 'controller.jsonl')|ForEach-Object{$_|ConvertFrom-Json})
 foreach($pair in @(@('controllerAssertions','assertion'),@('ownedProcesses','process_created'),@('desktops','desktop_created'),@('frames','capture'),@('publishedViews','view_published'),@('inputSessions','input_session_boundary'),@('diskSnapshotsPublished','snapshot_disk_published'),@('diskSnapshotsLoaded','snapshot_disk_loaded'))){$totals[$pair[0]]+=@($rows|Where-Object type -eq $pair[1]).Count}
 $managerProof+=[ordered]@{application=$manager.application;phase=$manager.mode;scenario=$manager.scenario;pid=$manager.pid;createdUtc=$manager.createdUtc;exited=$manager.exited;exit=$manager.exit;output=([IO.Path]::GetRelativePath($ResultsDir,$dir)).Replace('\','/');sha256=$manager.sha256}
 foreach($row in @($rows|Where-Object type -in @('snapshot_disk_published','snapshot_disk_loaded'))){$snapshotProof+=[ordered]@{phase=$manager.mode;managerPid=$manager.pid;application=$manager.application;label=$row.label;sha256=$row.sha256;sourcePid=$row.sourcePid;output=([IO.Path]::GetRelativePath($ResultsDir,$dir)).Replace('\','/')}}
 foreach($row in @($rows|Where-Object type -eq 'query_capacity_probe')){$probes+=[ordered]@{application=$manager.application;count=$row.count;capacity=$row.capacity;health=$row.health;bridgeAlive=$row.bridgeAlive}}
}
$regressions=@()
foreach($file in @(Get-ChildItem -LiteralPath (Join-Path $ResultsDir 'r5-002-regression') -Recurse -Filter summary.json -File)){
 $s=Get-Content -Raw -LiteralPath $file.FullName|ConvertFrom-Json
 $fields=@($s.PSObject.Properties.Name)
 if('allEvidenceChecksPassed' -notin $fields){continue}
 $cases=if('cases' -in $fields){$s.cases.Count}else{0};$checks=if('checks' -in $fields){$s.checks.Count}else{0}
 $regressions+=[ordered]@{file=([IO.Path]::GetRelativePath($ResultsDir,$file.FullName)).Replace('\','/');passed=$s.allEvidenceChecksPassed;cases=$cases;checks=$checks}
}
[ordered]@{normalExitCrossManagerRestorePassed=$summary.normalExitCrossManagerRestorePassed;allEvidenceChecksPassed=$summary.allEvidenceChecksPassed;scenarios=$summary.cases.Count;scenarioKinds=@($summary.cases|Group-Object kind|ForEach-Object{[ordered]@{kind=$_.Name;count=$_.Count;passed=@($_.Group|Where-Object passed).Count}});evidenceChecks=$summary.checks.Count;managerInstances=$summary.managers.Count;totals=$totals;queryProbes=$probes;regressions=$regressions;gateClosed=$false;manualHostTest='not_executed'}|ConvertTo-Json -Depth 10|Set-Content (Join-Path $ResultsDir 'statistics.json') -Encoding utf8
$managerProof|ConvertTo-Json -Depth 6|Set-Content (Join-Path $ResultsDir 'manager-identities.json') -Encoding utf8
$snapshotProof|ConvertTo-Json -Depth 6|Set-Content (Join-Path $ResultsDir 'disk-snapshot-identities.json') -Encoding utf8
$regressions|ConvertTo-Json -Depth 6|Set-Content (Join-Path $ResultsDir 'regressions.json') -Encoding utf8
Write-Output 'ASSESSMENT_RECORDED'
