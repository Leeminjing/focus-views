# Reuse the tested R5-003 read-only evidence oracles, not its experiment driver.
. (Join-Path $PSScriptRoot '../r4-001-gate-001/lifetimes.ps1')
. (Join-Path $PSScriptRoot '../r5-003/instance-check.ps1')
Add-Type -AssemblyName System.Drawing
$verificationSource=Join-Path $PSScriptRoot '../r5-003/test.ps1'
$verificationTokens=$null;$verificationErrors=$null
$verificationAst=[Management.Automation.Language.Parser]::ParseFile($verificationSource,[ref]$verificationTokens,[ref]$verificationErrors)
if($verificationErrors.Count){throw 'R5-003 verification source cannot be parsed'}
$verificationNames=@('Check','Rows','Primary','Sha','Same','Inventory','InventoryText','Audit','Check-Packet','Check-Restore')
foreach($statement in $verificationAst.EndBlock.Statements){
 if($statement -is [Management.Automation.Language.FunctionDefinitionAst] -and $statement.Name -in $verificationNames){. ([scriptblock]::Create($statement.Extent.Text))}
 if($statement -is [Management.Automation.Language.IfStatementAst] -and $statement.Extent.Text.Contains('public static class RestartReceipt')){. ([scriptblock]::Create($statement.Extent.Text))}
}
foreach($name in $verificationNames){if(-not(Get-Command $name -CommandType Function -ErrorAction SilentlyContinue)){throw "Missing reused verification $name"}}
function Optional-Rows([string]$Path){if(Test-Path -LiteralPath $Path){Rows $Path}}
function Crash-Audit([string]$Manager,[string]$Supervisor,[string]$Label,[bool]$Killed){
 $s=Get-Content -Raw -LiteralPath (Join-Path $Supervisor 'summary.json')|ConvertFrom-Json
 $events=Rows (Join-Path $Supervisor 'supervisor.jsonl');$m=@(Optional-Rows (Join-Path $Manager 'controller.jsonl'))
 $launch=@($events|Where-Object type -eq 'manager_launch_attempt');Check "$Label.explicit-standard-handle-inheritance" ($launch.Count -eq 1 -and $launch[0].inheritHandles -and $launch[0].explicitInheritedHandleCount -eq 3)
 Check "$Label.supervisor-protocol" $s.protocolPassed;Check "$Label.physical-cleanup" $s.cleanupVerified
 $pins=@($events|Where-Object type -eq 'job_instance_pinned');$exits=@($events|Where-Object type -eq 'instance_exit_confirmed')
 Check "$Label.all-pinned-instance-identities-exited" ($pins.Count -eq $exits.Count -and @($pins|Where-Object{$pin=$_;@($exits|Where-Object{$_.pid -eq $pin.pid -and $_.created -eq $pin.created}).Count -ne 1}).Count -eq 0)
 Check "$Label.job-empty-before-release" (@($events|Where-Object type -eq 'job_after_manager_exit').Count -eq 1 -and $s.cleanupVerified)
 $created=@($m|Where-Object type -eq 'desktop_created');$absent=@($events|Where-Object type -eq 'desktop_absent_confirmed')
 Check "$Label.every-created-desktop-absent" ($created.Count -eq $absent.Count -and @($created|Where-Object{$d=$_;@($absent|Where-Object name -eq $d.name).Count -ne 1}).Count -eq 0)
 Check "$Label.owned-handle-closes" (@($events|Where-Object type -eq 'owned_handle_closed').Count -eq $pins.Count+9)
 Check "$Label.no-failed-native-assertions" (@($m|Where-Object{$_.type -eq 'assertion' -and -not $_.passed}).Count -eq 0)
 if($Killed){
  Check "$Label.GUI-owner-log-required" ((Test-Path -LiteralPath (Join-Path $Manager 'controller.jsonl')) -and $created.Count -gt 0)
  Check "$Label.native-TerminateProcess-not-exception" ($s.forcedTermination -and $s.managerExit -eq [Convert]::ToUInt32('E5040001',16) -and @($events|Where-Object{$_.type -eq 'terminate_manager' -and $_.api -eq 'TerminateProcess' -and $_.pid -eq $s.managerPid -and $_.created -eq $s.managerCreated}).Count -eq 1)
  $point=@(Rows (Join-Path $Manager 'fault-points.jsonl')|Where-Object type -eq 'synchronous_crash_point');Check "$Label.deterministic-native-point" ($point.Count -eq 1 -and $point[0].point -eq $s.point -and $point[0].pid -eq $s.managerPid)
  Check "$Label.manager-did-not-run-cleanup-tail" (@($m|Where-Object type -eq 'restart_complete').Count -eq 0)
  $active=@{};foreach($row in $m){if($row.type -eq 'process_created'){if($active.ContainsKey([string]$row.pid)){throw 'Overlapping manager PID lifecycle'};$active[[string]$row.pid]=$row}else{if($row.type -eq 'process_exited'){$active.Remove([string]$row.pid)}}}
  Check "$Label.live-at-death-covered-by-job-proof" (@($active.Values|Where-Object{$p=$_;@($pins|Where-Object pid -eq $p.pid).Count -ne 1}).Count -eq 0)
 }else{Check "$Label.not-forced" (-not $s.forcedTermination)}
 foreach($q in @($events|Where-Object type -eq 'partial_quarantined')){Check "$Label.quarantine-byte-identity" ((Sha $q.file) -eq $q.sha256 -and -not(Test-Path -LiteralPath $q.source) -and $q.neverPromoted)}
 return ,$m
}
