param([string]$ResultsDir='D:/fv/b/m1',[string]$OutDir=(Join-Path $PSScriptRoot 'out'))
$ErrorActionPreference='Stop';$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(Test-Path -LiteralPath $ResultsDir){throw 'New matrix evidence directory required'}
New-Item -ItemType Directory -Path $ResultsDir|Out-Null
Copy-Item (Join-Path $OutDir 'build.json') "$ResultsDir/build.json"
Copy-Item (Join-Path $OutDir 'reuse.json') "$ResultsDir/reuse.json"
$cases=[Collections.Generic.List[object]]::new()
foreach($app in @('fixture','recorder')){
 $scenarios=@(@{name='positive';args=@{}},@{name='legacy-same-base';args=@{Legacy=$true;SameBase=$true}})
 foreach($fault in @('relocation-missing-return','relocation-bad-offset','relocation-missing-reference','relocation-wrong-image')){$scenarios+=@{name=$fault;args=@{Scenario=$fault}}}
 foreach($point in @('view-resources:RA','view-published:RA')){$scenarios+=@{name=($point -replace ':','-');args=@{KillRestore=$point}}}
 foreach($scenario in $scenarios){
  $label="$app-$($scenario.name)";$output=Join-Path $ResultsDir $label;$arguments=$scenario.args;$errorText=''
  try {& (Join-Path $PSScriptRoot 'test-one.ps1') -Application $app -OutDir $OutDir -ResultsDir $output @arguments *> "$ResultsDir/$label-console.log"}catch{$errorText=$_.ToString()}
  $summary=if(Test-Path "$output/summary.json"){Get-Content -Raw "$output/summary.json"|ConvertFrom-Json}else{$null}
  $cases.Add([pscustomobject]@{name=$label;passed=($null -ne $summary -and $summary.passed -and -not $errorText);error=$errorText;checks=if($summary){$summary.checks.Count}else{0};aslr=if($summary){$summary.aslrRelocationVerified}else{$false}})
  Write-Output "$label $($cases[-1].passed)"
 }
}
$passed=@($cases|Where-Object {-not $_.passed}).Count -eq 0
[ordered]@{passed=$passed;aslrRelocationVerified=$passed;fullR5004Passed=$false;regressions='not_run';gateClosed=$false;cases=$cases;checks=($cases|Measure-Object checks -Sum).Sum;sourceHead=(& git -C $PSScriptRoot rev-parse HEAD);manualHostTest='not_executed';hostRebootRecoveryVerified=$false}|ConvertTo-Json -Depth 7|Set-Content "$ResultsDir/summary.json" -Encoding utf8
if(-not $passed){throw "R5-004b matrix failed: $ResultsDir/summary.json"}
