param([string]$OutDir=(Join-Path $PSScriptRoot 'out'),[string]$ResultsDir='D:/fv/c/focus1')
$ErrorActionPreference='Stop';$OutDir=[IO.Path]::GetFullPath($OutDir)
if(Test-Path $ResultsDir){throw 'Preserve previous results; use new directory'};New-Item -ItemType Directory -Path $ResultsDir|Out-Null
$boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime;$results=@()
Copy-Item "$OutDir/build.json" "$ResultsDir/build.json"
foreach($app in @('fixture','recorder')){foreach($mode in @('inline','deferred')){
 $name="$app-$mode";$root="$ResultsDir/$name";$errorText=''
 try{& (Join-Path $PSScriptRoot '../r5-004b/test-one.ps1') -OutDir $OutDir -Application $app -ResultsDir $root -Scenario relocation-missing-reference -InputScenarioPrefix ($mode+':') *> "$ResultsDir/$name-console.log"}catch{$errorText=$_.ToString()}
 if(-not(Test-Path "$root/summary.json")){$results+=[pscustomobject]@{case=$name;passed=$false;error=$errorText;missingSummary=$true;naturalCleanupTimeoutObserved=$false};continue}
 $summary=Get-Content -Raw "$root/summary.json"|ConvertFrom-Json
 $events=if(Test-Path "$root/restore/manager/controller.jsonl"){@(Get-Content "$root/restore/manager/controller.jsonl"|ForEach-Object {$_|ConvertFrom-Json})}else{@()}
 $trace=@($events|Where-Object type -eq 'cleanup_phase');$stages=@($trace.stage)
 $expected=if($mode -eq 'inline'){@(1,2,5,6,7,8)}else{@(1,2,3,4,5,6,7,8)}
 $first=@($trace|Where-Object stage -eq 2);$continued=@($trace|Where-Object stage -eq 4);$entry=@($trace|Where-Object stage -eq 5)
 $exact=($stages -join ',') -ceq ($expected -join ',')
 $thread=$first.Count -eq 1 -and @($trace|Where-Object {$_.tid -ne $first[0].tid}).Count -eq 0
 $context=if($mode -eq 'deferred'){$first.Count -eq 1 -and $continued.Count -eq 1 -and $entry.Count -eq 1 -and $continued[0].rsp -eq $first[0].rsp -and $continued[0].ssp -eq $first[0].ssp -and $entry[0].ssp -eq $first[0].ssp -and $entry[0].rip -eq $continued[0].rip}else{$true}
 $results+=[pscustomobject]@{case=$name;passed=($summary.passed -and $exact -and $thread -and $context -and -not $errorText);error=$errorText;naturalCleanupTimeoutObserved=(@($events|Where-Object {$_.type -eq 'abort_native_window_cleanup' -and -not $_.verified -and -not $_.processExited -and $_.error -eq 0}).Count -eq 1 -and $stages -contains 6 -and $stages -notcontains 7);nativeChecksPassed=$summary.passed;exactPhases=$exact;sameGUIThread=$thread;preservedContextAndHardwareSsp=$context;stages=$stages}
 Write-Output "$name $($results[-1].passed)"
}}
$sameBoot=$boot -eq (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
$okay=$sameBoot -and @($results|Where-Object {-not $_.passed}).Count -eq 0
[ordered]@{passed=$okay;sameBoot=$sameBoot;boot=$boot;cases=$results;historicalNaturalFailureReproduced=(@($results|Where-Object naturalCleanupTimeoutObserved).Count -gt 0);cleanupCausalFixVerified=$false;fullR5004Passed=$false;gateClosed=$false}|ConvertTo-Json -Depth 7|Set-Content "$ResultsDir/summary.json" -Encoding utf8
if(-not $okay){throw 'Candidate focus/safety evidence failed'}
