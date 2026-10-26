param(
 [string]$OutDir=(Join-Path $PSScriptRoot 'out'),
 [string]$ResultsDir=(Join-Path $PSScriptRoot ('results/'+(Get-Date -Format 'yyyyMMdd-HHmmss'))),
 [ValidateRange(1,3)][int]$Repeats=2,[switch]$SkipRegression
)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$OutDir=[IO.Path]::GetFullPath($OutDir);$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(Test-Path -LiteralPath $ResultsDir){throw 'New ResultsDir required; preserve raw evidence.'}
New-Item -ItemType Directory -Path $ResultsDir|Out-Null
$checks=[Collections.Generic.List[object]]::new();$errors=[Collections.Generic.List[object]]::new()
function Check([string]$Name,[bool]$Okay){$checks.Add([pscustomobject]@{name=$Name;passed=$Okay});Write-Output "$Name=$Okay"}
$managerHash=(Get-FileHash (Join-Path $OutDir 'manager.exe')).Hash
$kernelHash=(Get-FileHash (Join-Path $OutDir 'kernel.lib')).Hash
try{
 & (Join-Path $PSScriptRoot 'test-guard.ps1') -OutDir $OutDir -ResultsDir (Join-Path $ResultsDir 'checkpoint-guard') | Tee-Object -FilePath (Join-Path $ResultsDir 'checkpoint-guard-console.log')
 $guard=Get-Content (Join-Path $ResultsDir 'checkpoint-guard/summary.json') -Raw|ConvertFrom-Json
 Check 'checkpoint-owned-paint-and-unknown-rejection' $guard.allEvidenceChecksPassed
}catch{$errors.Add([pscustomobject]@{stage='checkpoint-guard';error=$_.ToString()});Check 'checkpoint-owned-paint-and-unknown-rejection' $false}
foreach($application in @('fixture','legacy','recorder')){
 try{
  & (Join-Path $PSScriptRoot 'test-app.ps1') -OutDir $OutDir -ResultsDir (Join-Path $ResultsDir $application) -Application $application -Repeats $Repeats | Tee-Object -FilePath (Join-Path $ResultsDir "$application-console.log")
  $result=Get-Content (Join-Path $ResultsDir "$application/summary.json") -Raw|ConvertFrom-Json
  Check "$application-full-matrix" $result.allEvidenceChecksPassed
 }catch{$errors.Add([pscustomobject]@{stage=$application;error=$_.ToString()});Check "$application-full-matrix" $false}
 Check "$application-exact-same-kernel" ((Get-FileHash (Join-Path $OutDir 'kernel.lib')).Hash -eq $kernelHash -and (Get-Content (Join-Path $ResultsDir "$application/identity.json") -Raw|ConvertFrom-Json).kernelSha256 -eq $kernelHash.ToLowerInvariant())
 Check "$application-exact-same-manager" ((Get-FileHash (Join-Path $OutDir 'manager.exe')).Hash -eq $managerHash)
}
foreach($entry in @(Get-Content (Join-Path $OutDir 'reuse.json') -Raw|ConvertFrom-Json)){
 Check ("unchanged-"+$entry.file) ($entry.identical -and (Get-FileHash (Join-Path $OutDir $entry.file)).Hash.ToLowerInvariant() -eq $entry.sha256 -and (Get-FileHash $entry.source).Hash.ToLowerInvariant() -eq $entry.sha256)
}
$oldDiff=@(& git -C $PSScriptRoot diff HEAD -- ../r5-001a ../r4-003-001 ../r4-002a ../r4-001 ../r3-005c-001 ../r3-005b-001 ../r3-005a-cet-002)
Check 'old-runtime-and-experiments-unchanged' ($oldDiff.Count -eq 0)
$boundary=Get-Content (Join-Path $OutDir 'link-boundaries.json') -Raw|ConvertFrom-Json
Check 'thin-manager-links-no-app-implementation' ($boundary.manager.Count -eq 1 -and $boundary.manager[0] -eq 'manager_main.cpp')
$recorder=Get-ChildItem (Join-Path $PSScriptRoot 'recorder') -File|Select-String -Pattern '\bWork\b|fixture_contract|app/fixture|app/application'
Check 'second-app-has-independent-schema' (@($recorder).Count -eq 0)
# Deliberately use the wrong explicit client. A missing contract export must refuse,
# before creating any target or publishing any View; not an implicit adapter fallback.
$wrong=Join-Path $ResultsDir 'wrong-client'
& (Join-Path $OutDir 'manager.exe') --client (Join-Path $OutDir 'fixture-client.dll') --fixture (Join-Path $OutDir 'second.exe') --output $wrong --scenario positive *> (Join-Path $ResultsDir 'wrong-client-console.log')
$code=$LASTEXITCODE;$log=Get-Content (Join-Path $ResultsDir 'wrong-client-console.log') -Raw
Check 'wrong-client-refused-before-target-launch' ($code -ne 0 -and $log.Contains('metadata export missing') -and -not (Test-Path (Join-Path $wrong 'source')))
$regression='not_run'
if(-not $SkipRegression){
 try{
  & (Join-Path $PSScriptRoot '../r5-001b/test.ps1') -ResultsDir (Join-Path $ResultsDir 'r5-001b-regression') | Tee-Object -FilePath (Join-Path $ResultsDir 'r5-001b-regression-console.log')
  $old=Get-Content (Join-Path $ResultsDir 'r5-001b-regression/summary.json') -Raw|ConvertFrom-Json
  Check 'R5-001b-R5-001a-and-nested-R4-003-full-regression' $old.allEvidenceChecksPassed
  $regression=if($old.allEvidenceChecksPassed){'passed'}else{'failed'}
 }catch{$regression='failed';$errors.Add([pscustomobject]@{stage='old-regression';error=$_.ToString()});Check 'R5-001b-R5-001a-and-nested-R4-003-full-regression' $false}
}
$okay=$errors.Count -eq 0 -and @($checks|Where-Object{-not $_.passed}).Count -eq 0
[ordered]@{allEvidenceChecksPassed=$okay;crossApplicationPassed=$okay;oldRegressions=$regression;kernelModified=$true;managerSha256=$managerHash.ToLowerInvariant();kernelSha256=$kernelHash.ToLowerInvariant();legacyFixedChainCompatible=$okay;fourFrameRestorePassed=$okay;arbitraryDepthSupported=$false;generalGUIRestore=$false;projectComplete=$false;gateClosed=$false;manualHostTest='not_executed';checks=$checks;errors=$errors}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $ResultsDir 'summary.json') -Encoding utf8
if(-not $okay){throw 'R5-002 gate failed; all primary errors preserved, no operation retries.'}
Write-Output 'R5_002_DECLARED_EXECUTION_PASSED'
