param([Parameter(Mandatory)][string]$OutDir,[Parameter(Mandatory)][string]$BaselineStore,[Parameter(Mandatory)][string]$ResultsDir)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
if(Test-Path -LiteralPath $ResultsDir){throw 'Identity evidence directory must be new'}
. (Join-Path $PSScriptRoot 'instance-check.ps1')
& (Join-Path $OutDir 'identity_test.exe') --store $BaselineStore --fixture (Join-Path $OutDir 'fixture.exe') --manager (Join-Path $OutDir 'manager.exe') --output $ResultsDir
$nativeExit=$LASTEXITCODE
$before=[pscustomobject]@{pid=42;created=[uint64]100}
$rows=@(
 [pscustomobject]@{name='same-PID-new-created';passed=(Test-DifferentProcessInstance $before ([pscustomobject]@{pid=42;created=[uint64]101}))},
 [pscustomobject]@{name='same-instance';passed=(-not(Test-DifferentProcessInstance $before $before))},
 [pscustomobject]@{name='missing-created';passed=(-not(Test-DifferentProcessInstance $before ([pscustomobject]@{pid=43})))},
 [pscustomobject]@{name='unknown-created';passed=(-not(Test-DifferentProcessInstance $before ([pscustomobject]@{pid=43;created=0})))}
)
$native=@(Get-Content (Join-Path $ResultsDir 'identity.jsonl')|ForEach-Object{$_|ConvertFrom-Json})
$passed=$nativeExit -eq 0 -and @($native|Where-Object{$_.type -eq 'check' -and -not $_.passed}).Count -eq 0 -and @($rows|Where-Object{-not $_.passed}).Count -eq 0
[ordered]@{passed=$passed;nativeExit=$nativeExit;syntheticIdentityDeclarations=$true;naturalPidReuseClaimed=$false;nativeChecks=@($native|Where-Object type -eq 'check');scriptChecks=$rows;sourceHead=(& git -C $PSScriptRoot rev-parse HEAD)}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $ResultsDir 'summary.json') -Encoding utf8
if(-not $passed){throw 'Instance identity regression failed; retain native first error'}
