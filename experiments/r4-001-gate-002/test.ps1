param([string]$ResultsDir=(Join-Path $PSScriptRoot ('results/'+(Get-Date -Format 'yyyyMMdd-HHmmss'))),[ValidateRange(1,256)][int]$Count=256)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Creation experiment must retain its non-administrator token; elevated tracing belongs in a separate collector.'}
$ResultsDir=[IO.Path]::GetFullPath($ResultsDir);if(Test-Path $ResultsDir){throw 'Fresh ResultsDir required'}
New-Item -ItemType Directory $ResultsDir|Out-Null
$localTrace=Join-Path $PSScriptRoot ('out/traces/'+(Split-Path $ResultsDir -Leaf));if(Test-Path $localTrace){throw 'Fresh local trace path required'};New-Item -ItemType Directory -Force $localTrace|Out-Null
$probe=Join-Path $PSScriptRoot 'out/probe.exe';$trace=Join-Path $PSScriptRoot 'out/trace.exe';$fixture=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../r3-005b-001/out/r305a.exe'))
$checks=[Collections.Generic.List[object]]::new();$cases=[Collections.Generic.List[object]]::new();$ranges=[Collections.Generic.List[string]]::new()
$collector=$null;$traceReady=$null;$collectorExit=$null;$decodeExit=$null;$failure=$null
function Check($Name,$Okay){$checks.Add([pscustomobject]@{name=$Name;passed=[bool]$Okay});Write-Host "$Name=$Okay"}
function Rows($Path){@(Get-Content -LiteralPath $Path|Where-Object{$_}|ForEach-Object{$_|ConvertFrom-Json})}
function Start-Native($Exe,[string[]]$Arguments){
 $info=[Diagnostics.ProcessStartInfo]::new($Exe);$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in $Arguments){$info.ArgumentList.Add($argument)}
 [Diagnostics.Process]::Start($info)
}
function Finish-Native($Process,$Prefix,[int]$Seconds){
 $until=(Get-Date).AddSeconds($Seconds)
 while(-not $Process.WaitForExit(100)){if((Get-Date) -ge $until){$Process.Kill();$Process.WaitForExit(5000)|Out-Null;throw "Owned diagnostic helper exceeded deadline: $Prefix"}}
 $Process.StandardOutput.ReadToEnd()|Set-Content ($Prefix+'-stdout.txt') -Encoding utf8;$Process.StandardError.ReadToEnd()|Set-Content ($Prefix+'-stderr.txt') -Encoding utf8
 return $Process.ExitCode
}
try{
 foreach($file in @('build.json','build.log','probe-loadconfig.txt','trace-loadconfig.txt')){Copy-Item (Join-Path $PSScriptRoot "out/$file") (Join-Path $ResultsDir $file)}
 $ready=Join-Path $localTrace ready.json;$stop=Join-Path $localTrace stop;$etl=Join-Path $localTrace process.etl
 $collector=Start-Native $trace @('--mode','record','--etl',$etl,'--ready',$ready,'--stop',$stop,'--log',(Join-Path $ResultsDir 'trace-controller.jsonl'),'--seconds','180','--parent',"$PID")
 $until=(Get-Date).AddSeconds(10);while(-not(Test-Path $ready) -and -not $collector.HasExited -and (Get-Date) -lt $until){Start-Sleep -Milliseconds 25}
 if(Test-Path $ready){$traceReady=Get-Content $ready -Raw|ConvertFrom-Json;Copy-Item $ready (Join-Path $ResultsDir 'trace-ready.json')}
 Check 'collector-reported-permission-result' ($null -ne $traceReady)
 # A partial trace is intentional diagnostic scope, never a fallback creation.
 # Kernel-Process denial remains visible even when memory/markers are available.
 foreach($mode in @('own','fixture','own-debug','fixture-debug')){
  $log=Join-Path $ResultsDir ($mode+'.jsonl')
  $process=Start-Native $probe @('--mode',$mode,'--fixture',$fixture,'--count',"$Count",'--log',$log)
  $code=Finish-Native $process (Join-Path $ResultsDir $mode) 70
  $rows=Rows $log;$start=@($rows|Where-Object type -eq case_start);$done=@($rows|Where-Object type -eq case_complete);$errors=@($rows|Where-Object type -eq case_error);$refused=@($rows|Where-Object type -eq create_failed)
  $created=@($rows|Where-Object type -eq process_created);$exited=@($rows|Where-Object type -eq process_exited)
  $closes=@($rows|Where-Object type -eq owned_handle_closed);$desktops=@($rows|Where-Object type -eq desktop_closed)
  $balanced=$created.Count -eq $exited.Count -and @($created|Where-Object{$n=$_.sequence;@($exited|Where-Object sequence -eq $n).Count -ne 1}).Count -eq 0
  Check "$mode.actual-case-identity" ($start.Count -eq 1 -and $start[0].mode -eq $mode -and $start[0].planned -eq $Count)
  Check "$mode.completed-or-explicit-first-refusal" ($done.Count -eq 1 -and $errors.Count -eq 0 -and (($code -eq 0 -and $done[0].completed -eq $Count -and $refused.Count -eq 0) -or ($code -eq 2 -and $refused.Count -eq 1 -and $refused[0].sequence -eq $created.Count+1 -and $done[0].creationRefused)))
  Check "$mode.all-successfully-created-children-exited-and-handles-closed" ($balanced -and $closes.Count -eq 2*$created.Count -and @($closes|Where-Object{-not $_.closed}).Count -eq 0 -and $desktops.Count -eq 1 -and $desktops[0].closed)
  Check "$mode.no-retry-after-failed-create" ($refused.Count -le 1 -and ($refused.Count -eq 0 -or @($rows|Where-Object{$_.type -eq 'before_create' -and $_.sequence -gt $refused[0].sequence}).Count -eq 0))
  Check "$mode.failed-API-output-ownership-not-assumed" ($refused.Count -eq 0 -or ($refused[0].reportedPid -eq 0 -and $refused[0].reportedThread -eq 0 -and $refused[0].outputProcessHandle -eq 0 -and $refused[0].outputThreadHandle -eq 0))
  if($start.Count){$end=$rows[-1].utc;$ranges.Add("$($start[0].pid) $($start[0].utc) $end")}
  foreach($child in $created){$exit=@($exited|Where-Object sequence -eq $child.sequence);if($exit.Count){$ranges.Add("$($child.pid) $($child.beginUtc) $($exit[0].utc)")}}
  $cases.Add([pscustomobject]@{mode=$mode;pid=$process.Id;exitCode=$code;planned=$Count;created=$created.Count;exited=$exited.Count;completed=$done;refusals=$refused;caseErrors=$errors})
  Write-Host "$mode exit=$code created=$($created.Count)"
 }
}catch{$failure=$_.ToString();$_|Out-String|Set-Content (Join-Path $ResultsDir 'run-error.txt');Check 'diagnostic-run-completed-without-harness-error' $false}
finally{
 if($null -ne $collector){
  try{
   [IO.File]::WriteAllText((Join-Path $localTrace stop),'stop')
   $collectorExit=Finish-Native $collector (Join-Path $ResultsDir collector) 10
  }catch{
   $_|Out-String|Set-Content (Join-Path $ResultsDir 'collector-cleanup-error.txt');Check 'collector-clean-stop' $false
   & $trace --mode stop-owned --etl (Join-Path $localTrace process.etl) *> (Join-Path $ResultsDir 'stop-owned.jsonl')
  }
 }
 try{
  $rangeFile=Join-Path $ResultsDir 'pid-lifetimes.txt';[IO.File]::WriteAllLines($rangeFile,$ranges,[Text.Encoding]::ASCII)
  $etl=Join-Path $localTrace process.etl
  if(Test-Path $etl){
   $decoder=Start-Native $trace @('--mode','decode','--etl',$etl,'--ranges',$rangeFile,'--log',(Join-Path $ResultsDir 'related-system-events.jsonl'))
   $decodeExit=Finish-Native $decoder (Join-Path $ResultsDir decoder) 30
   [ordered]@{localOnly=$true;publicExport='related-system-events.jsonl';path=$etl;sha256=(Get-FileHash $etl).Hash.ToLowerInvariant();bytes=(Get-Item $etl).Length;includesUnrelatedRawEvents=$true;reason='Only PID/time-filtered events and numeric memory samples are exported; original ETL stays local in ignored out/'}|ConvertTo-Json|Set-Content (Join-Path $ResultsDir 'etl-manifest.json') -Encoding utf8
   Check 'owned-ETL-decoded' ($decodeExit -eq 0)
   $decoded=Rows (Join-Path $ResultsDir 'related-system-events.jsonl');$stats=@($decoded|Where-Object type -eq etw_decode_complete)
   Check 'ETW-marker-correlation-pipeline-observed' ($stats.Count -eq 1 -and ($null -eq $traceReady -or $traceReady.markerEnableStatus -ne 0 -or $stats[0].markers -gt 0))
  }
  $traceRows=Rows (Join-Path $ResultsDir 'trace-controller.jsonl');$cleanup=@($traceRows|Where-Object type -eq trace_cleanup_query)
  Check 'owned-trace-session-confirmed-absent' (($null -ne $traceReady -and $traceReady.startStatus -ne 0) -or ($cleanup.Count -eq 1 -and $cleanup[0].absent))
 }catch{$_|Out-String|Set-Content (Join-Path $ResultsDir 'decode-error.txt');Check 'trace-evidence-and-cleanup-readable' $false}
 $inputs=@('common.hpp','probe.cpp','trace.cpp','build.ps1','test.ps1','../r3/protocol.hpp','../r4-001-gate-001/launch-diagnostics.hpp')
 [ordered]@{time=(Get-Date -Format o);baseCommit=(& git -C $PSScriptRoot rev-parse HEAD);os=[Environment]::OSVersion.VersionString;elevated=$false;sources=@($inputs|ForEach-Object{@{file=$_;sha256=(Get-FileHash (Join-Path $PSScriptRoot $_)).Hash.ToLowerInvariant()}});probeSha256=(Get-FileHash $probe).Hash.ToLowerInvariant();collectorSha256=(Get-FileHash $trace).Hash.ToLowerInvariant();fixtureSha256=(Get-FileHash $fixture).Hash.ToLowerInvariant();systemSettingsChanged=$false}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $ResultsDir 'identity.json') -Encoding utf8
 $newRefusals=@($cases|ForEach-Object{$_.refusals}|Where-Object api -eq CreateProcessW)
 [ordered]@{diagnosticEvidenceChecksPassed=(@($checks|Where-Object{-not $_.passed}).Count -eq 0);functionalRegression='historical_pass_at_c55efb0_not_rerun';creationRefusalReproduced=($newRefusals.Count -gt 0);allCreateAttemptsSucceeded=($cases.Count -eq 4 -and @($cases|Where-Object exitCode -ne 0).Count -eq 0);processProviderAvailable=($null -ne $traceReady -and $traceReady.processEnableStatus -eq 0);collectorExit=$collectorExit;decodeExit=$decodeExit;rootCauseConfirmed=$false;issueResolved=$false;gateClosed=$false;checks=$checks;cases=$cases;error=$failure;SA_SB_restore=$false;R4Complete=$false}|ConvertTo-Json -Depth 10|Set-Content (Join-Path $ResultsDir 'summary.json') -Encoding utf8
}
$summary=Get-Content (Join-Path $ResultsDir summary.json) -Raw|ConvertFrom-Json
if(-not $summary.diagnosticEvidenceChecksPassed){throw 'Diagnostic evidence/cleanup failed; preserve all logs'}
if(-not $summary.processProviderAvailable -or -not $summary.allCreateAttemptsSucceeded){Write-Host 'DIAGNOSTIC_COMPLETE_GATE_REMAINS_OPEN';exit 2}
Write-Host 'DIAGNOSTIC_COMPLETE_ROOT_CAUSE_UNCONFIRMED'
