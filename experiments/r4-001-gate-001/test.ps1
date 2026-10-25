param([string]$ResultsDir=(Join-Path $PSScriptRoot ('results/'+(Get-Date -Format 'yyyyMMdd-HHmmss'))))
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(Test-Path -LiteralPath $ResultsDir){throw 'Use a new ResultsDir; preserve prior evidence'}
New-Item -ItemType Directory -Path $ResultsDir|Out-Null
$started=Get-Date;$checks=[Collections.Generic.List[object]]::new();$runs=[Collections.Generic.List[object]]::new()
function Check($Name,$Passed){$checks.Add([pscustomobject]@{name=$Name;passed=[bool]$Passed});Write-Output "$Name=$Passed"}
function Run-Test($Name,$Script,[string[]]$Arguments){
 & pwsh -NoProfile -File $Script @Arguments *> (Join-Path $ResultsDir ($Name+'-console.log'))
 $code=$LASTEXITCODE;$runs.Add([pscustomobject]@{name=$Name;exitCode=$code;script=$Script;arguments=$Arguments});Write-Host "$Name exit=$code";return $code
}
function Read-Summary($Dir){Get-Content (Join-Path $Dir 'summary.json') -Raw|ConvertFrom-Json}
try{
 & (Join-Path $PSScriptRoot 'system-diagnostics.ps1') -Since $started.AddMinutes(-5) -Output (Join-Path $ResultsDir 'system-before.json')
 & (Join-Path $PSScriptRoot 'test-lifetimes.ps1') -Output (Join-Path $ResultsDir 'lifetime-tests.json')
 Check 'ordered-lifetimes-positive-and-negative-controls' ((Get-Content (Join-Path $ResultsDir 'lifetime-tests.json') -Raw|ConvertFrom-Json).passed)
 foreach($kind in @('early-close','correct-owner')){
  $argsProbe=@();if($kind -eq 'early-close'){$argsProbe=@('--early-close')}
  & (Join-Path $PSScriptRoot 'out/handle-probe.exe') @argsProbe | Set-Content (Join-Path $ResultsDir ("handle-$kind.jsonl")) -Encoding utf8
  Check "handle-ownership-$kind-control" ($LASTEXITCODE -eq 0)
 }
 $denied=Join-Path $ResultsDir 'denied-launch'
 $status=Run-Test 'denied-launch' (Join-Path $PSScriptRoot '../r3-005b-001/test.ps1') @('-ResultsDir',$denied,'-Repeats','1','-SkipRegression','-GateDeniedLaunch')
 $d=Read-Summary $denied;$rows=@(Get-Content (Join-Path $denied 'positive-1/controller.jsonl')|ForEach-Object{$_|ConvertFrom-Json})
 $failure=@($rows|Where-Object type -eq 'process_launch_failed');$audit=Get-Content (Join-Path $denied 'positive-1/lifetime-audit.json') -Raw|ConvertFrom-Json
 Check 'real-CreateProcess-denial-keeps-failed-summary-and-other-cases' ($status -ne 0 -and -not $d.allEvidenceChecksPassed -and $d.cases.Count -eq 9 -and $d.caseProcessingErrors.Count -eq 1 -and $failure.Count -eq 1 -and $failure[0].error -eq 5 -and $failure[0].api -eq 'CreateProcessW' -and $audit.passed -and -not(Test-Path (Join-Path $denied 'positive-1/S0-data.bin')) -and @($rows|Where-Object type -eq view_published).Count -eq 0)
 foreach($stage in @(@('b001-standalone','r3-005b-001'),@('c001-full','r3-005c-001'),@('r4-full','r4-001'))){
  $name=$stage[0];$experiment=$stage[1];$dir=Join-Path $ResultsDir $name
  $code=Run-Test $name (Join-Path $PSScriptRoot "../$experiment/test.ps1") @('-ResultsDir',$dir)
  $summary=Read-Summary $dir
  Check "$name-complete-script-passed" ($code -eq 0 -and $summary.allEvidenceChecksPassed)
  if($name -eq 'b001-standalone'){Check "$name-CET002-executed-and-passed" ($summary.CET002Regression -eq 'passed')}
  if($name -eq 'c001-full'){Check "$name-nested-b001-and-CET002-passed" ($summary.B001AndCET002Regression -eq 'passed')}
  if($name -eq 'r4-full'){Check "$name-nested-c001-b001-and-CET002-passed" ($summary.C001Regression -eq 'passed')}
 }
}catch{
 $_|Out-String|Set-Content (Join-Path $ResultsDir 'gate-error.txt');Check 'gate-ran-without-unhandled-diagnostic-error' $false
}finally{
 try{& (Join-Path $PSScriptRoot 'system-diagnostics.ps1') -Since $started -Output (Join-Path $ResultsDir 'system-after.json')}catch{$_|Out-String|Set-Content (Join-Path $ResultsDir 'system-diagnostics-error.txt');Check 'system-diagnostics-complete' $false}
 $inputs=@('test.ps1','lifetimes.ps1','test-lifetimes.ps1','launch-diagnostics.hpp','courier.hpp','handle-probe.cpp','launch-probe.cpp','build.ps1','system-diagnostics.ps1')
 [ordered]@{time=(Get-Date -Format o);baseCommit=(& git -C $PSScriptRoot rev-parse HEAD);sources=@($inputs|ForEach-Object{@{file=$_;sha256=(Get-FileHash (Join-Path $PSScriptRoot $_)).Hash.ToLowerInvariant()}});probeSha256=(Get-FileHash (Join-Path $PSScriptRoot 'out/handle-probe.exe')).Hash.ToLowerInvariant()}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $ResultsDir 'identity.json') -Encoding utf8
 $okay=@($checks|Where-Object{-not $_.passed}).Count -eq 0
 [ordered]@{allEvidenceChecksPassed=$okay;checks=$checks;runs=$runs;historicalCreateProcess5Cause='unconfirmed; separate debug-handle misuse reproduced and fixed';systemSettingsChanged=$false;SA_SB_restore=$false;R4Complete=$false;manualHostTest='not_executed'}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $ResultsDir 'summary.json') -Encoding utf8
}
if(-not $okay){throw 'R4-001-GATE-001 failed; preserve all stage evidence'}
Write-Output 'R4_001_GATE_001_FULL_REGRESSION_PASSED'
