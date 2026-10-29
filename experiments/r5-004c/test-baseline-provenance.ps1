param([Parameter(Mandatory)][string]$ResultsDir)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
if(Test-Path $ResultsDir){throw 'Fresh audit test evidence required'};New-Item -ItemType Directory $ResultsDir|Out-Null
. "$PSScriptRoot/../r4-001-gate-001/lifetimes.ps1"
$repo=(& git -C $PSScriptRoot rev-parse --show-toplevel);$copy="$ResultsDir/private-repo"
& git clone --shared --no-checkout $repo $copy *> "$ResultsDir/clone.log";if($LASTEXITCODE){throw 'Private audit repository preparation failed'}
& git -C $copy checkout HEAD -- experiments/r3-004/input.cpp experiments/r3-004/runtime.cpp
if($LASTEXITCODE){throw 'Private approved source checkout failed'}
$checks=@();$path='experiments/r3-004/input.cpp';$base='2ec1d2b'
$approved=@(Get-ApprovedInputDiagnosticSource $copy)
$a=Test-GateBaseline $copy $base @($path) $approved
$checks+=[ordered]@{name='exact-approved-committed-source-accepted';passed=($approved -contains $path -and $a.passed);audit=$a}
Add-Content "$copy/$path" '// artificial unapproved change, not compiled'
$a=Test-GateBaseline $copy $base @($path) @(Get-ApprovedInputDiagnosticSource $copy)
$checks+=[ordered]@{name='later-input-source-change-still-rejected';passed=(-not $a.passed -and $a.unexpected -contains $path);audit=$a}
& git -C $copy checkout HEAD -- $path
Add-Content "$copy/experiments/r3-004/runtime.cpp" '// artificial unrelated change, not compiled'
$a=Test-GateBaseline $copy $base @($path,'experiments/r3-004/runtime.cpp') @(Get-ApprovedInputDiagnosticSource $copy)
$checks+=[ordered]@{name='other-old-implementation-change-still-rejected';passed=(-not $a.passed -and $a.unexpected -contains 'experiments/r3-004/runtime.cpp');audit=$a}
$okay=@($checks|Where-Object{-not $_.passed}).Count -eq 0
[ordered]@{passed=$okay;synthetic=$true;nativeOperationsStarted=$false;approvedCommit='1ef786b92d808fe543edbbd80e40c47ae9bb160a';checks=$checks}|ConvertTo-Json -Depth 7|Set-Content "$ResultsDir/summary.json"
if(-not $okay){throw 'Approved-source baseline guard test failed'}
