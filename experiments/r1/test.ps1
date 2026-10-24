param(
    [string]$OutDir = (Join-Path $PSScriptRoot 'out'),
    [string]$ResultsDir = (Join-Path $PSScriptRoot ('results\' + (Get-Date -Format 'yyyyMMdd-HHmmss')))
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$taskController = Join-Path $OutDir 'controller.exe'
$taskFixture = Join-Path $OutDir 'fixture.exe'
if (-not (Test-Path -LiteralPath $taskController) -or -not (Test-Path -LiteralPath $taskFixture)) { throw 'Run build.ps1 first.' }
New-Item -ItemType Directory -Path $ResultsDir -Force | Out-Null
$taskCases = @(
    @{name='positive'; code=0; marker='R1_CONTROLLED_EXPERIMENT_PASS restores=2'},
    @{name='omit-memory'; code=2; marker='RESULT_MISMATCH'},
    @{name='omit-stack'; code=2; marker='RESULT_MISMATCH'},
    @{name='omit-register'; code=2; marker='RESULT_MISMATCH'},
    @{name='omit-flags'; code=2; marker='RESULT_MISMATCH'},
    @{name='address-conflict'; code=3; marker='api=VirtualAllocEx code=0x1e7'},
    @{name='build-mismatch'; code=3; marker='BUILD_MISMATCH restore rejected before writes'},
    @{name='timeout'; code=4; marker='api=WaitForDebugEvent code=0x5b4'},
    @{name='unexpected-exception'; code=1; marker='api=debug_exception code=0xe0421001'}
)
$taskResults = @()
foreach ($taskCase in $taskCases) {
    $taskDeadline = if ($taskCase.name -eq 'timeout') { 1000 } else { 10000 }
    $taskLog = @(& $taskController --fixture $taskFixture --case $taskCase.name --timeout-ms $taskDeadline 2>&1)
    $taskExit = $LASTEXITCODE
    $taskText = $taskLog -join "`n"
    $taskLog | Set-Content -LiteralPath (Join-Path $ResultsDir ($taskCase.name + '.log')) -Encoding UTF8
    $taskCreated = @([regex]::Matches($taskText, '(?m)^PROCESS_CREATED pid=(\d+)') | ForEach-Object { $_.Groups[1].Value })
    $taskTerminated = @([regex]::Matches($taskText, '(?m)^PROCESS_TERMINATED_CONFIRMED pid=(\d+)') | ForEach-Object { $_.Groups[1].Value })
    $taskCleanupOkay = $taskCreated.Count -gt 0 -and $taskCreated.Count -eq $taskTerminated.Count
    foreach ($taskPid in $taskCreated) { if ($taskPid -notin $taskTerminated) { $taskCleanupOkay = $false } }
    $taskPassed = $taskExit -eq $taskCase.code -and $taskText.Contains($taskCase.marker) -and $taskCleanupOkay -and -not $taskText.Contains('CLEANUP_FAILED')
    if ($taskCase.name -eq 'positive') {
        $taskPassed = $taskPassed -and @([regex]::Matches($taskText, '(?m)^TASK_INIT role=source ')).Count -eq 1 -and
            @([regex]::Matches($taskText, '(?m)^CHECKPOINT role=source ')).Count -eq 1 -and
            @([regex]::Matches($taskText, '(?m)^EVENT_COUNTS role=restore init=0 checkpoint=0 result=1')).Count -eq 2
        $taskExitIndex = $taskText.IndexOf('SOURCE_EXIT_CONFIRMED_BEFORE_RESTORE')
        $taskTargetIndex = $taskText.IndexOf('PROCESS_CREATED', $taskText.IndexOf('PROCESS_CREATED') + 1)
        $taskPassed = $taskPassed -and $taskExitIndex -ge 0 -and $taskTargetIndex -gt $taskExitIndex
    } else {
        $taskPassed = $taskPassed -and -not $taskText.Contains('R1_CONTROLLED_EXPERIMENT_PASS')
    }
    $taskResults += [pscustomobject]@{ case=$taskCase.name; expectedExit=$taskCase.code; actualExit=$taskExit;
        harnessPassed=$taskPassed; computationSucceeded=($taskExit -eq 0); createdPids=$taskCreated; terminatedPids=$taskTerminated }
    Write-Output "$($taskCase.name): exit=$taskExit expected=$($taskCase.code) harness_pass=$taskPassed"
}
$taskOs = Get-CimInstance Win32_OperatingSystem
$taskIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
$taskPrincipal = [Security.Principal.WindowsPrincipal]::new($taskIdentity)
$taskSummary = [ordered]@{
    time = (Get-Date -Format o); os = $taskOs.Caption; osVersion = $taskOs.Version; osBuild = $taskOs.BuildNumber
    processor = @((Get-CimInstance Win32_Processor).Name); architecture = $env:PROCESSOR_ARCHITECTURE
    elevated = $taskPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    build = Get-Content -LiteralPath (Join-Path $OutDir 'build.json') -Raw | ConvertFrom-Json
    sourceHashes = @(Get-ChildItem -LiteralPath $PSScriptRoot -File | Where-Object { $_.Extension -in '.cpp','.hpp','.asm','.ps1' } | Get-FileHash -Algorithm SHA256 | Select-Object Path,Hash)
    sharedSourceHashes = @(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot '../common/native.hpp') -Algorithm SHA256 | Select-Object Path,Hash)
    executableHashes = @(Get-FileHash -LiteralPath $taskController,$taskFixture -Algorithm SHA256 | Select-Object Path,Hash)
    cases = $taskResults; allHarnessChecksPassed = @($taskResults | Where-Object { -not $_.harnessPassed }).Count -eq 0
}
$taskSummary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $ResultsDir 'summary.json') -Encoding UTF8
Write-Output "RESULTS $ResultsDir"
if (-not $taskSummary.allHarnessChecksPassed) { throw 'R1 test harness failed; inspect raw logs.' }
Write-Output 'ALL_HARNESS_CHECKS_PASSED (negative cases remain failed/rejected computations)'
