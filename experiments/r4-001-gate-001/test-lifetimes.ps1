param([string]$Output=(Join-Path $PSScriptRoot 'out/lifetime-tests.json'))
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'lifetimes.ps1')
$checks=[Collections.Generic.List[object]]::new()
function Assert-Audit($Name,$Rows,$Expected){$audit=Test-OwnedLifetimes $Rows;$checks.Add([pscustomobject]@{name=$Name;expected=$Expected;passed=($audit.passed -eq $Expected);audit=$audit})}
function Row($Type,$ProcessId,$Tick){[pscustomobject]@{type=$Type;pid=$ProcessId;tickMs=$Tick}}
$create=Row 'process_created' 7 1;$exit=Row 'process_exited' 7 2
Assert-Audit 'sequential-PID-reuse' @($create,$exit,(Row 'process_created' 7 3),(Row 'process_exited' 7 4)) $true
Assert-Audit 'overlap-rejected' @($create,(Row 'process_created' 7 2),$exit) $false
Assert-Audit 'missing-exit-rejected' @($create) $false
Assert-Audit 'duplicate-exit-rejected' @($create,$exit,$exit) $false
Assert-Audit 'time-reversed-rejected' @($create,(Row 'process_exited' 7 0)) $false
$desktop=[pscustomobject]@{type='desktop_created';name='owned'}
Assert-Audit 'Desktop-leak-rejected' @($desktop) $false
Assert-Audit 'wrong-Desktop-close-rejected' @($desktop,[pscustomobject]@{type='desktop_closed';name='other';closed=$true}) $false
Assert-Audit 'Desktop-failed-close-rejected' @($desktop,[pscustomobject]@{type='desktop_closed';name='owned';closed=$false}) $false
$historic=Join-Path $PSScriptRoot '../r4-001/results/20261025-r4001-resident/c001-regression/ABC-1/controller.jsonl'
$rows=@(Get-Content $historic|ForEach-Object{$_|ConvertFrom-Json})
$created=@($rows|Where-Object type -eq process_created);$exited=@($rows|Where-Object type -eq process_exited)
$legacy=@($created|Where-Object{$pidValue=$_.pid;@($exited|Where-Object pid -eq $pidValue).Count -ne 1}).Count -eq 0
$checks.Add([pscustomobject]@{name='historical-legacy-assertion-reproduced-red';passed=(-not $legacy)})
Assert-Audit 'historical-76-instances-cleaned-in-order' $rows $true
[ordered]@{passed=(@($checks|Where-Object{-not $_.passed}).Count -eq 0);checks=$checks}|ConvertTo-Json -Depth 9|Set-Content $Output -Encoding utf8
if(@($checks|Where-Object{-not $_.passed}).Count){throw 'Lifetime audit regression'}
Write-Output "LIFETIME_CHECKS_PASSED=$($checks.Count)"
