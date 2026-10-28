# Classifies linked observations, never a cause/deadlock. Old records without
# declared counts are deliberately insufficient, not silently upgraded.
function Get-LinkedTextServiceWait([object[]]$Rows,[uint64]$TargetPid,[uint64]$TargetCreated,[uint64]$GuiTid,[uint64]$GuiCreated) {
 $reason='missing-identity';$links=$null
 try{
  if(-not $TargetPid -or -not $TargetCreated -or -not $GuiTid -or -not $GuiCreated){throw 'identity'}
  $start=@($Rows|Where-Object type -eq 'diagnostic_started');$reason='diagnostic-target-instance'
  if($start.Count -ne 1 -or $start[0].pid -ne $TargetPid -or $start[0].tid -ne $GuiTid -or $start[0].created -ne $TargetCreated){throw 'identity'}
  $queries=@();$current=$null
  $ordinal=0
  foreach($row in $Rows){
   if($row.type -eq 'wct_query_begin'){$current=[pscustomobject]@{begin=$row;ordinal=$ordinal;results=@();nodes=@()};$queries+=,$current}
   elseif($row.type -eq 'wait_chain'){if($null -eq $current){throw 'orphan result'};$current.results+=,$row}
   elseif($row.type -eq 'wait_node'){if($null -eq $current){throw 'orphan node'};$current.nodes+=,$row}
   ++$ordinal
  }
  $reason='query-failed-or-incomplete'
  foreach($q in $queries){
   if($q.begin.created -le 0 -or $q.results.Count -ne 1){throw 'query'};$r=$q.results[0]
   if($r.PSObject.Properties.Name -notcontains 'count' -or $r.result -ne 1 -or $r.error -ne 0 -or $r.pid -ne $TargetPid -or $r.tid -ne $q.begin.tid -or $r.count -ne $q.nodes.Count -or $r.count -lt 1 -or $r.count -gt 16){throw 'query'}
   for($i=0;$i -lt $q.nodes.Count;++$i){if($q.nodes[$i].queryTid -ne $q.begin.tid -or $q.nodes[$i].index -ne $i){throw 'node order'}}
   if($q.nodes[0].kind -ne 8 -or $q.nodes[0].pid -ne $TargetPid -or $q.nodes[0].tid -ne $q.begin.tid){throw 'query identity'}
  }
  $g=@($queries|Where-Object {$_.begin.tid -eq $GuiTid});$reason='gui-query-instance'
  if($g.Count -ne 1 -or $g[0].begin.created -ne $GuiCreated){throw 'GUI identity'};$gn=$g[0].nodes
  $reason='gui-worker-edge'
  if($gn.Count -notin @(3,5) -or $gn[1].kind -ne 6 -or $gn[2].kind -ne 8 -or $gn[2].pid -ne $TargetPid -or $gn[2].tid -eq $GuiTid -or $gn[2].tid -le 0){throw 'worker edge'}
  $w=@($queries|Where-Object {$_.begin.tid -eq $gn[2].tid});$reason='linked-worker-query'
  if($w.Count -ne 1){throw 'worker query'};$wn=$w[0].nodes
  # A query after the GUI sample alone does not exclude a reused TID. Require
  # a pinned native observation of that worker instance before the GUI query.
  $reason='worker-instance-not-linked'
  $before=@($Rows|Select-Object -First $g[0].ordinal|Where-Object{$_.type -eq 'thread_context' -and $_.tid -eq $w[0].begin.tid -and $_.result -eq 1 -and $_.error -eq 0 -and $_.created -eq $w[0].begin.created})
  if($before.Count -ne 1){throw 'worker instance'}
  $reason='worker-com-rpcss-edge'
  if($wn.Count -ne 3 -or $wn[1].kind -ne 5 -or -not $wn[1].name -or $wn[2].kind -ne 8 -or $wn[2].status -ne 5 -or $wn[2].tid -ne 0 -or $wn[2].pid -le 0 -or $wn[2].pid -eq $TargetPid){throw 'COM edge'}
  if($gn.Count -eq 5 -and ($gn[3].kind -ne 5 -or $gn[3].name -cne $wn[1].name -or $gn[4].kind -ne 8 -or $gn[4].status -ne 5 -or $gn[4].pid -ne $wn[2].pid -or $gn[4].tid -ne 0)){throw 'inconsistent shared edge'}
  $links=[ordered]@{targetPid=$TargetPid;targetCreated=$TargetCreated;guiTid=$GuiTid;guiCreated=$GuiCreated;workerTid=$w[0].begin.tid;workerCreated=$w[0].begin.created;comName=$wn[1].name;rpcssReportedPid=$wn[2].pid;guiNodeIndices=@(0,1,2);workerNodeIndices=@(0,1,2)}
 }catch{return [pscustomobject]@{sameWaitingPattern=$false;status='insufficient_evidence';reason=$reason;links=$null}}
 [pscustomobject]@{sameWaitingPattern=$true;status='matched';reason='linked-native-observation';links=$links;rootCauseKnown=$false}
}
