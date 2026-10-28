param([Parameter(Mandatory)][string]$ResultsDir)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
if(Test-Path $ResultsDir){throw 'Preserve previous evidence'};New-Item -ItemType Directory $ResultsDir|Out-Null
. "$PSScriptRoot/wait-pattern.ps1"
function New-TestRow([hashtable]$v){[pscustomobject]$v}
$start=New-TestRow @{type='diagnostic_started';pid=10;tid=20;created=100}
$gui=@($start,(New-TestRow @{type='thread_context';tid=30;created=300;result=1;error=0}),(New-TestRow @{type='wct_query_begin';tid=20;created=200}),(New-TestRow @{type='wait_chain';pid=10;tid=20;result=1;error=0;count=3}),(New-TestRow @{type='wait_node';queryTid=20;index=0;kind=8;status=3;pid=10;tid=20}),(New-TestRow @{type='wait_node';queryTid=20;index=1;kind=6;status=6;name=''}),(New-TestRow @{type='wait_node';queryTid=20;index=2;kind=8;status=3;pid=10;tid=30}))
$worker=@((New-TestRow @{type='wct_query_begin';tid=30;created=300}),(New-TestRow @{type='wait_chain';pid=10;tid=30;result=1;error=0;count=3}),(New-TestRow @{type='wait_node';queryTid=30;index=0;kind=8;status=3;pid=10;tid=30}),(New-TestRow @{type='wait_node';queryTid=30;index=1;kind=5;status=6;name='COM-object'}),(New-TestRow @{type='wait_node';queryTid=30;index=2;kind=8;status=5;pid=99;tid=0}))
$linked=$gui+$worker
$unrelated=@($linked|ForEach-Object{$x=$_|ConvertTo-Json -Compress|ConvertFrom-Json;if($x.type -eq 'wct_query_begin' -and $x.tid -eq 30){$x.tid=40};if($x.type -eq 'wait_chain' -and $x.tid -eq 30){$x.tid=40};if($x.type -eq 'wait_node' -and $x.queryTid -eq 30){$x.queryTid=40;if($x.index -eq 0){$x.tid=40}};$x})
$truncated=$linked[0..($linked.Count-2)]
$badCreated=@($linked|ForEach-Object{$x=$_|ConvertTo-Json -Compress|ConvertFrom-Json;if($x.type -eq 'wct_query_begin' -and $x.tid -eq 20){$x.created=201};$x})
$three=$gui+@((New-TestRow @{type='wct_query_begin';tid=40;created=400}),(New-TestRow @{type='wait_chain';pid=10;tid=40;result=1;error=0;count=2}),(New-TestRow @{type='wait_node';queryTid=40;index=0;kind=8;status=3;pid=10;tid=40}),(New-TestRow @{type='wait_node';queryTid=40;index=1;kind=5;status=6;name='unrelated-COM'}),(New-TestRow @{type='wct_query_begin';tid=50;created=500}),(New-TestRow @{type='wait_chain';pid=10;tid=50;result=1;error=0;count=2}),(New-TestRow @{type='wait_node';queryTid=50;index=0;kind=8;status=3;pid=10;tid=50}),(New-TestRow @{type='wait_node';queryTid=50;index=1;kind=8;status=5;pid=99;tid=0}))
$failed=@($linked|ForEach-Object{$x=$_|ConvertTo-Json -Compress|ConvertFrom-Json;if($x.type -eq 'wait_chain' -and $x.tid -eq 30){$x.result=0;$x.error=5};$x})
$reused=@($linked|ForEach-Object{$x=$_|ConvertTo-Json -Compress|ConvertFrom-Json;if($x.type -eq 'wct_query_begin' -and $x.tid -eq 30){$x.created=301};$x})
$checks=@(foreach($c in @(@{name='linked';rows=$linked;expected=$true},@{name='unrelated-query-nodes';rows=$unrelated;expected=$false},@{name='three-unconnected-queries';rows=$three;expected=$false},@{name='truncated';rows=$truncated;expected=$false},@{name='wrong-gui-instance';rows=$badCreated;expected=$false},@{name='failed-worker-query';rows=$failed;expected=$false},@{name='same-tid-different-worker-instance';rows=$reused;expected=$false})){$a=Get-LinkedTextServiceWait $c.rows 10 100 20 200;[ordered]@{name=$c.name;artificial=$true;actual=$a;passed=($a.sameWaitingPattern -eq $c.expected)}})
$okay=@($checks|Where-Object{-not $_.passed}).Count -eq 0
[ordered]@{passed=$okay;nativeProcessesStarted=$false;checks=$checks}|ConvertTo-Json -Depth 8|Set-Content "$ResultsDir/summary.json" -Encoding utf8
if(-not $okay){throw 'Linked wait-chain classification failed'}
