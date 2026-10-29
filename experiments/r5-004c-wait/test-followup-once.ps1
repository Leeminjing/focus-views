param([Parameter(Mandatory)][string]$OutDir,[Parameter(Mandatory)][string]$Profile,[Parameter(Mandatory)][string]$ResultsDir,[switch]$SerialOnly)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
if(Test-Path $ResultsDir){throw 'Fresh evidence required'};New-Item -ItemType Directory $ResultsDir|Out-Null
$entry=Join-Path $PSScriptRoot 'run-failure-followup.ps1';$checks=@();$origins=@()
function Origin([string]$Name){
 $root=[IO.Path]::GetFullPath("$ResultsDir/$Name-origin")
 & "$PSScriptRoot/../r5-004c/test-diagnostic.ps1" -OutDir $OutDir -ResultsDir $root *> "$ResultsDir/$Name-origin-console.log"
 $a=Get-Content "$root/diagnostic-assessment.json" -Raw|ConvertFrom-Json;if(-not $a.passed){throw 'Original artificial handler failed'}
 [ordered]@{boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime;synthetic=$true;originalSummarySha256=(Get-FileHash "$root/summary.json").Hash}|ConvertTo-Json|Set-Content "$root/operation-identity.json"
 return $root
}
function InvokeEntry([string]$Root,[string]$Name){
 try{& $entry -OriginRoot $Root -OutDir $OutDir -Profile $Profile -ResultsDir "$ResultsDir/$Name" *> "$ResultsDir/$Name-console.log"}catch{$_|Out-String|Set-Content "$ResultsDir/$Name-error.txt"}
 Get-Content "$ResultsDir/$Name/assessment.json" -Raw|ConvertFrom-Json
}
$origin=Origin 'serial';$origins+=$origin;$before=(Get-FileHash "$origin/summary.json").Hash
$first=InvokeEntry $origin 'first';$second=InvokeEntry $origin 'second'
$checks+=[ordered]@{name='first-real-entry-allows-one-pair';passed=($first.diagnosticChainPassed -and $first.newControlPairRuns -eq 1)}
$checks+=[ordered]@{name='second-results-directory-refused-before-windows';passed=($second.reason -eq 'already-attempted' -and $second.newControlPairRuns -eq 0 -and -not(Test-Path "$ResultsDir/second/close-controls"))}
$checks+=[ordered]@{name='original-failure-unchanged';passed=((Get-FileHash "$origin/summary.json").Hash -eq $before -and -not (Get-Content "$origin/summary.json" -Raw|ConvertFrom-Json).passed)}
if(-not $SerialOnly){
 $race=Origin 'race';$origins+=$race
 $gateName='Local\fv-followup-'+[Guid]::NewGuid().ToString('N');$gate=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$gateName)
 @'
param($Config)
$ErrorActionPreference='Stop';$c=Get-Content $Config -Raw|ConvertFrom-Json
$gate=[Threading.EventWaitHandle]::OpenExisting($c.gate)
[IO.File]::WriteAllText($c.ready,'ready')
if(-not $gate.WaitOne(30000)){throw 'Competition gate timed out'};$gate.Dispose()
& $c.entry -OriginRoot $c.origin -OutDir $c.out -Profile $c.profile -ResultsDir $c.result
'@|Set-Content "$ResultsDir/race-runner.ps1"
 $children=@()
 try{
  foreach($i in 1..2){$config="$ResultsDir/race-$i-config.json";[ordered]@{gate=$gateName;ready="$ResultsDir/race-$i-ready";entry=$entry;origin=$race;out=$OutDir;profile=$Profile;result="$ResultsDir/race-$i"}|ConvertTo-Json|Set-Content $config
   $p=Start-Process (Get-Process -Id $PID).Path -ArgumentList @('-NoProfile','-File',"`"$ResultsDir/race-runner.ps1`"",'-Config',"`"$config`"") -WindowStyle Hidden -RedirectStandardOutput "$ResultsDir/race-$i-out.log" -RedirectStandardError "$ResultsDir/race-$i-err.log" -PassThru;$children+=$p
  }
  $end=[DateTime]::UtcNow.AddSeconds(30);while(-not((Test-Path "$ResultsDir/race-1-ready") -and (Test-Path "$ResultsDir/race-2-ready"))){if([DateTime]::UtcNow -gt $end){throw 'Competing callers not ready'};Start-Sleep -Milliseconds 50}
  [void]$gate.Set();foreach($p in $children){if(-not $p.WaitForExit(180000)){throw 'Competing caller exceeded bound'}}
 }finally{$gate.Dispose();foreach($p in $children){if(-not $p.HasExited){$p.Kill();$p.WaitForExit()};$p.Dispose()}}
 $rows=@(1..2|ForEach-Object{Get-Content "$ResultsDir/race-$_/assessment.json" -Raw|ConvertFrom-Json})
 $checks+=[ordered]@{name='two-real-callers-one-winner';passed=(@($rows|Where-Object newControlPairRuns -eq 1).Count -eq 1 -and @($rows|Where-Object reason -eq 'already-attempted').Count -eq 1 -and @($rows|Where-Object diagnosticChainPassed).Count -eq 1)}
 $checks+=[ordered]@{name='winner-persistent-record-retained';passed=(@($rows|Where-Object {$_.PSObject.Properties.Name -contains 'attemptPath' -and $_.attemptPath -and (Test-Path $_.attemptPath)}).Count -eq 2)}
 # A failed independent control consumes its claim too. Use a real handler
 # source, but a deliberately absent control-build path; no API result spoof.
 $failed=Origin 'failed-control';$origins+=$failed;$savedOut=$OutDir
 try{$OutDir="$ResultsDir/absent-control-build";$bad=InvokeEntry $failed 'failed-first'}finally{$OutDir=$savedOut}
 $retry=InvokeEntry $failed 'failed-second'
 $checks+=[ordered]@{name='failed-attempt-still-consumed';passed=($bad.attemptAcquired -and -not $bad.diagnosticChainPassed -and (Test-Path $bad.attemptPath) -and $retry.reason -eq 'already-attempted' -and $retry.newControlPairRuns -eq 0 -and -not(Test-Path "$ResultsDir/failed-second/close-controls"))}
}
$passed=@($checks|Where-Object{-not $_.passed}).Count -eq 0
[ordered]@{passed=$passed;synthetic=$true;nativeEntryInvoked=$true;serialOnly=[bool]$SerialOnly;origins=$origins;checks=$checks;historicalRootCauseVerified=$false}|ConvertTo-Json -Depth 6|Set-Content "$ResultsDir/summary.json"
if(-not $passed){throw 'Follow-up once-only acceptance failed'}
