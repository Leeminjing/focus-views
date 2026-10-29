# Ordered event audit: a PID identifies only a currently live instance.
function Test-OwnedLifetimes([object[]]$Rows) {
 $active=@{};$desktops=@{};$issues=[Collections.Generic.List[string]]::new();$instances=[Collections.Generic.List[object]]::new();$ordinal=0;$closed=0
 foreach($row in $Rows){
  switch($row.type){
   'process_created' {
    $key=[string]$row.pid;++$ordinal
    if($active.ContainsKey($key)){$issues.Add("overlapping PID $key");continue}
    $item=[pscustomobject]@{instance=$ordinal;pid=$row.pid;createTick=$row.tickMs;exitTick=$null}
    $active[$key]=$item;$instances.Add($item)
   }
   'process_exited' {
    $key=[string]$row.pid
    if(-not $active.ContainsKey($key)){$issues.Add("unmatched/duplicate exit PID $key");continue}
    $active[$key].exitTick=$row.tickMs
    if($row.tickMs -lt $active[$key].createTick){$issues.Add("reversed time PID $key")}
    $active.Remove($key)
   }
   'desktop_created' {if($desktops.ContainsKey($row.name)){$issues.Add("overlapping Desktop $($row.name)")}else{$desktops[$row.name]=$true}}
   'desktop_closed' {
    if(-not $row.closed){$issues.Add("Desktop close failed $($row.name)")}
    if(-not $desktops.ContainsKey($row.name)){$issues.Add("unmatched Desktop close $($row.name)")}else{$desktops.Remove($row.name);++$closed}
   }
   'cleanup_error' {$issues.Add('native cleanup_error')}
  }
 }
 foreach($key in $active.Keys){$issues.Add("missing exit PID $key")}
 foreach($key in $desktops.Keys){$issues.Add("unclosed Desktop $key")}
 [pscustomobject]@{passed=($issues.Count -eq 0);issues=@($issues);instances=@($instances);closedDesktops=$closed}
}

# An exact source allowlist replaces historical whole-directory equality only for
# this gate's reviewed fixes. Historical results and every other file stay fixed.
function Test-GateBaseline([string]$Root,[string]$Base,[string[]]$Paths,[string[]]$Allowed) {
 $changed=@(& git -C $Root diff --name-only $Base -- @Paths)
 if($LASTEXITCODE -ne 0){throw 'git baseline comparison failed'}
 $unexpected=@($changed|Where-Object{$_ -notin $Allowed})
 [pscustomobject]@{passed=($unexpected.Count -eq 0);changed=$changed;allowed=$Allowed;unexpected=$unexpected}
}

# A committed, compile-gated diagnostic source is also present in default
# builds. Accept only that reviewed blob, never arbitrary future input changes.
function Get-ApprovedInputDiagnosticSource([string]$Root) {
 & git -C $Root diff --quiet 1ef786b92d808fe543edbbd80e40c47ae9bb160a -- ':(top)experiments/r3-004/input.cpp'
 if($LASTEXITCODE -eq 0){'experiments/r3-004/input.cpp'}
 elseif($LASTEXITCODE -ne 1){throw 'Approved input source comparison failed'}
}
function Get-GateAllowedSources([string]$Root=$PSScriptRoot) {
 foreach($experiment in @('r3-005a-cet-002','r3-005b-001','r3-005c-001')){
  foreach($file in @('controller.cpp','build.ps1','test.ps1')){"experiments/$experiment/$file"}
 }
 foreach($file in @('fixture.cpp','protocol.hpp','bridge.cpp','capture.cpp')){"experiments/r3-005b-001/$file"}
 Get-ApprovedInputDiagnosticSource $Root
}
