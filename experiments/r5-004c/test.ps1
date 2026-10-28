param([string]$OutDir=(Join-Path $PSScriptRoot 'out'),[string]$ResultsDir='D:/fv/c/r1',[switch]$SkipRegression,[int]$LegacyBarrierPid=0,[long]$LegacyBarrierCreatedTicks=0)
$ErrorActionPreference='Stop';$OutDir=[IO.Path]::GetFullPath($OutDir);$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(Test-Path $ResultsDir){throw 'New results root required'};New-Item -ItemType Directory -Path $ResultsDir|Out-Null
$boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime;$results=[Collections.Generic.List[object]]::new()
$barrier=$null
if($LegacyBarrierPid){$barrier=Get-Process -Id $LegacyBarrierPid -ErrorAction Stop;if($barrier.StartTime.Ticks -ne $LegacyBarrierCreatedTicks){throw 'Legacy build barrier process instance mismatch'};[void]$barrier.Handle}
$fingerprint=Get-Content -Raw "$OutDir/build.json"
Copy-Item "$OutDir/build.json" "$ResultsDir/build.json"
function Stage([string]$Name,[scriptblock]$Run,[string]$Summary){
 $errorText='';try{& $Run *> "$ResultsDir/$Name-console.log"}catch{$errorText=$_.ToString()}
 $data=if(Test-Path $Summary){Get-Content -Raw $Summary|ConvertFrom-Json}else{$null}
 $key=if($Name -in @('cleanup','crash','legacy')){'allEvidenceChecksPassed'}else{'passed'}
 $flag=$null -ne $data -and $data.PSObject.Properties.Name -contains $key -and $data.$key -eq $true
 if($Name -eq 'legacy'){$flag=$flag -and $data.regressions -eq 'passed'}
 $okay=$null -ne $data -and -not $errorText -and $flag
 $results.Add([pscustomobject]@{name=$Name;passed=$okay;error=$errorText;summary=$Summary});Write-Output "$Name $okay"
}
# Explicitly run the cleanup matrix; the historical gate entry alone omits it.
Stage focus {& "$PSScriptRoot/test-focus.ps1" -OutDir $OutDir -ResultsDir "$ResultsDir/focus"} "$ResultsDir/focus/summary.json"
Stage cleanup {& "$PSScriptRoot/test-cleanup.ps1" -OutDir $OutDir -ResultsDir "$ResultsDir/cleanup" -SkipRegression} "$ResultsDir/cleanup/summary.json"
Stage relocation {& "$PSScriptRoot/../r5-004b/test.ps1" -OutDir $OutDir -ResultsDir "$ResultsDir/relocation"} "$ResultsDir/relocation/summary.json"
Stage crash {& "$PSScriptRoot/../r5-004b/test-gate.ps1" -OutDir $OutDir -ResultsDir "$ResultsDir/crash" -SkipRegression} "$ResultsDir/crash/summary.json"
$regression='not_run'
if(-not $SkipRegression){
 Stage legacy {
  if($barrier){$end=[DateTime]::UtcNow.AddMinutes(45);while(-not $barrier.WaitForExit(1000)){if([DateTime]::UtcNow -ge $end){throw 'Previous owned legacy runner has not exited; do not overwrite its builds'}};$barrier.Dispose()}
  $build=Get-Content -Raw "$OutDir/build.json"|ConvertFrom-Json
  & "$PSScriptRoot/../r5-003/build.ps1" -MsvcRoot $build.msvc -SdkRoot $build.sdk -SdkVersion $build.sdkVersion *> "$ResultsDir/legacy-build.log"
  & "$PSScriptRoot/../r5-003/test.ps1" -ResultsDir "$ResultsDir/reg3"
 } "$ResultsDir/reg3/summary.json"
 $regression=if($results[-1].passed){'passed'}else{'failed'}
}
$sameBuild=(Get-Content -Raw "$OutDir/build.json") -ceq $fingerprint
$sameBoot=$boot -eq (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
$passed=@($results|Where-Object {-not $_.passed}).Count -eq 0 -and $sameBuild -and $sameBoot
[ordered]@{candidateFunctionalVerificationPassed=$passed;sameFinalBuild=$sameBuild;sameBoot=$sameBoot;boot=$boot;regressions=$regression;stages=$results;cleanupCausalFixVerified=$false;fullR5004Passed=$false;gateClosed=$false;manualHostTest='not_executed';hostRebootRecoveryVerified=$false}|ConvertTo-Json -Depth 7|Set-Content "$ResultsDir/summary.json" -Encoding utf8
if(-not $passed){throw 'Candidate combined verification failed; inspect individual preserved errors'}
