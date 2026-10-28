# Standalone suites against prebuilt binaries. Also usable when a prior driver
# failed before entering either suite; never reruns its native minimal cases.
param([Parameter(Mandatory)][string]$InputOutDir,[Parameter(Mandatory)][string]$CetOutDir,[Parameter(Mandatory)][string]$ResultsDir)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
if(Test-Path $ResultsDir){throw 'New results required'};New-Item -ItemType Directory $ResultsDir|Out-Null
. "$PSScriptRoot/verification.ps1";Assert-InputDiagnosticBuild $InputOutDir;Assert-InputDiagnosticBuild $CetOutDir
$files=@(foreach($dir in @($InputOutDir,$CetOutDir)){foreach($name in @('build.log','loadconfig.txt','build.json','r305a.exe','runtime304.dll')){$p=Join-Path $dir $name;[ordered]@{path=$p;sha256=(Get-FileHash $p).Hash}}})
$boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime;$cases=@()
foreach($name in @('b001','cet002')){
 $errorText=''
 try{if($name -eq 'b001'){& "$PSScriptRoot/../r3-005b-001/test.ps1" -InstallDiagnostics -SkipRegression -OutDir $InputOutDir -ResultsDir "$ResultsDir/$name" *> "$ResultsDir/$name-console.log"}
 else{& "$PSScriptRoot/../r3-005a-cet-002/test.ps1" -InstallDiagnostics -OutDir $CetOutDir -ResultsDir "$ResultsDir/$name" *> "$ResultsDir/$name-console.log"}}catch{$errorText=$_.ToString()}
 $s=if(Test-Path "$ResultsDir/$name/summary.json"){Get-Content "$ResultsDir/$name/summary.json" -Raw|ConvertFrom-Json}else{$null}
 $cases+=[ordered]@{suite=$name;error=$errorText;summary=$s;passed=($null -ne $s -and $s.allEvidenceChecksPassed -and -not $errorText)}
 $cases|ConvertTo-Json -Depth 12|Set-Content "$ResultsDir/stages.json" -Encoding utf8
}
$sameBuild=@($files|Where-Object{(Get-FileHash $_.path).Hash -ne $_.sha256}).Count -eq 0;$sameBoot=$boot -eq (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
$passed=$sameBuild -and $sameBoot -and @($cases|Where-Object{-not $_.passed}).Count -eq 0
[ordered]@{passed=$passed;boot=$boot;sameBuild=$sameBuild;sameBoot=$sameBoot;frozenInputs=$files;cases=$cases;fullNestedRegressionExecuted=$false;cleanupCausalFixVerified=$false;fullR5004Passed=$false;gateClosed=$false}|ConvertTo-Json -Depth 15|Set-Content "$ResultsDir/summary.json" -Encoding utf8
if(-not $passed){throw 'Affected diagnostic suites failed; preserve exact summaries'}
