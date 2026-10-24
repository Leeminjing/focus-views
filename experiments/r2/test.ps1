param(
    [string]$OutDir = (Join-Path $PSScriptRoot 'out'),
    [string]$ResultsDir = (Join-Path $PSScriptRoot ('results\' + (Get-Date -Format 'yyyyMMdd-HHmmss')))
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$taskController = [IO.Path]::GetFullPath((Join-Path $OutDir 'controller.exe'))
$taskFixture = [IO.Path]::GetFullPath((Join-Path $OutDir 'fixture.exe'))
$ResultsDir = [IO.Path]::GetFullPath($ResultsDir)
if (-not (Test-Path -LiteralPath $taskController) -or -not (Test-Path -LiteralPath $taskFixture)) { throw 'Run build.ps1 first.' }
New-Item -ItemType Directory -Path $ResultsDir -Force | Out-Null
$taskCases = @(
    @{ name='memory'; code=0; marker='R2_001_MEMORY_PASS' },
    @{ name='files'; code=0; marker='R2_002_FILE_PASS' },
    @{ name='shared-open'; code=2; marker='FILE_POSITION_OR_ALIAS_MISMATCH role=B expected=2 actual=4' },
    @{ name='split-alias'; code=2; marker='FILE_POSITION_OR_ALIAS_MISMATCH role=A expected=4 actual=2' },
    @{ name='omit-position'; code=2; marker='FILE_POSITION_OR_ALIAS_MISMATCH role=A expected=2 actual=0' },
    @{ name='omit-content'; code=2; marker='FILE_CONTENT_MISMATCH role=A' },
    @{ name='partial-create'; code=3; marker='REJECTION_PATH_VERIFIED case=partial-create' },
    @{ name='unsupported-resource'; code=3; marker='REJECTION_PATH_VERIFIED case=unsupported-resource' },
    @{ name='reject-async'; code=3; marker='UNSUPPORTED_MANAGED_REQUEST role=A op=20' },
    @{ name='reject-map'; code=3; marker='UNSUPPORTED_MANAGED_REQUEST role=A op=21' },
    @{ name='reject-lock'; code=3; marker='UNSUPPORTED_MANAGED_REQUEST role=A op=22' },
    @{ name='reject-device'; code=3; marker='UNSUPPORTED_MANAGED_REQUEST role=A op=23' },
    @{ name='reject-unknown'; code=3; marker='UNSUPPORTED_MANAGED_REQUEST role=A op=24' }
)
$taskResults = @()
foreach ($taskCase in $taskCases) {
    $taskCaseFiles = Join-Path $ResultsDir ('files-' + $taskCase.name)
    $taskLog = @(& $taskController --fixture $taskFixture --case $taskCase.name --workdir $taskCaseFiles 2>&1)
    $taskExit = $LASTEXITCODE
    $taskText = $taskLog -join "`n"
    $taskLog | Set-Content -LiteralPath (Join-Path $ResultsDir ($taskCase.name + '.log')) -Encoding UTF8
    $taskCreated = @([regex]::Matches($taskText, '(?m)^PROCESS_CREATED role=(\w+) pid=(\d+)') | ForEach-Object { $_.Groups[1].Value + ':' + $_.Groups[2].Value })
    $taskTerminated = @([regex]::Matches($taskText, '(?m)^PROCESS_TERMINATED_CONFIRMED role=(\w+) pid=(\d+)') | ForEach-Object { $_.Groups[1].Value + ':' + $_.Groups[2].Value })
    $taskCleanupOkay = $taskCreated.Count -gt 0 -and $taskCreated.Count -eq $taskTerminated.Count
    foreach ($taskChild in $taskCreated) { if ($taskChild -notin $taskTerminated) { $taskCleanupOkay = $false } }
    $taskPassed = $taskExit -eq $taskCase.code -and $taskText.Contains($taskCase.marker) -and $taskCleanupOkay -and -not $taskText.Contains('CLEANUP_FAILED')
    if ($taskCase.code -eq 0) {
        $taskPassed = $taskPassed -and @([regex]::Matches($taskText, '(?m)^TASK_INIT role=source ')).Count -eq 1 -and
            @([regex]::Matches($taskText, '(?m)^CHECKPOINT role=source ')).Count -eq 1 -and
            @([regex]::Matches($taskText, '(?m)^SIMULTANEOUSLY_ALIVE A=\d+ B=\d+ C=\d+')).Count -ge 2 -and
            @([regex]::Matches($taskText, '(?m)^EVENT_COUNTS role=[ABC] init=0 checkpoint=0')).Count -eq 3
        $taskPassed = $taskPassed -and $taskText.IndexOf('SOURCE_EXIT_CONFIRMED_BEFORE_RESTORE') -lt $taskText.IndexOf('VIEW_PUBLISHED role=A')
        # Confirm a result from A is dispatched during B's creation sequence.
        $taskBCreate = $taskText.IndexOf('PROCESS_CREATED role=B')
        $taskBPublish = $taskText.IndexOf('VIEW_PUBLISHED role=B')
        $taskASlice = $taskText.Substring($taskBCreate, $taskBPublish - $taskBCreate)
        $taskPassed = $taskPassed -and $taskASlice.Contains('HALT role=A')
        if ($taskCase.name -eq 'files') {
            foreach ($taskVersion in @(@('source','0123456789'),@('A','0123AA6789'),@('B','0123456BB9'),@('C','0123CC6789'))) {
                $taskFile = Join-Path $taskCaseFiles ($taskVersion[0] + '\file-0.bin')
                $taskPassed = $taskPassed -and [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($taskFile)) -eq $taskVersion[1]
            }
        }
    } else {
        $taskPassed = $taskPassed -and -not $taskText.Contains('R2_002_FILE_PASS') -and -not $taskText.Contains('R2_001_MEMORY_PASS')
    }
    if ($taskCase.name -in 'partial-create','unsupported-resource') {
        $taskPassed = $taskPassed -and -not $taskText.Contains('VIEW_PUBLISHED role=B') -and
            $taskText.Contains('NO_HALF_PUBLISHED_B A_CONTINUES C_FROM_S0') -and
            $taskText.Contains('FILE_ROLLBACK role=B handles=0 files=0') -and
            -not (Test-Path -LiteralPath (Join-Path $taskCaseFiles 'B\file-0.bin'))
    }
    $taskResults += [pscustomobject]@{ case=$taskCase.name; expectedExit=$taskCase.code; actualExit=$taskExit;
        harnessPassed=$taskPassed; experimentSucceeded=($taskExit -eq 0); created=$taskCreated; terminated=$taskTerminated }
    Write-Output "$($taskCase.name): exit=$taskExit expected=$($taskCase.code) harness_pass=$taskPassed"
    # Stage two must not proceed unless the memory-only stage really passed.
    if ($taskCase.name -eq 'memory' -and -not $taskPassed) { break }
}
$taskOs = Get-CimInstance Win32_OperatingSystem
$taskPrincipal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
$taskShared = @('../common/native.hpp','../r1/snapshot.hpp','../r1/build.ps1') | ForEach-Object { Join-Path $PSScriptRoot $_ }
$taskRemaining = @(Get-CimInstance Win32_Process -Filter "Name='fixture.exe'" | Where-Object { $_.ExecutablePath -eq $taskFixture })
$taskSummary = [ordered]@{
    time=(Get-Date -Format o); os=$taskOs.Caption; osVersion=$taskOs.Version; osBuild=$taskOs.BuildNumber
    architecture=$env:PROCESSOR_ARCHITECTURE; processor=@((Get-CimInstance Win32_Processor).Name)
    elevated=$taskPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    baseCommit=(& git -C $PSScriptRoot rev-parse HEAD); build=(Get-Content (Join-Path $OutDir 'build.json') -Raw | ConvertFrom-Json)
    sourceHashes=@(Get-ChildItem -LiteralPath $PSScriptRoot -File | Where-Object { $_.Extension -in '.cpp','.hpp','.asm','.ps1' } | Get-FileHash -Algorithm SHA256 | Select-Object Path,Hash)
    sharedSourceHashes=@(Get-FileHash -LiteralPath $taskShared -Algorithm SHA256 | Select-Object Path,Hash)
    executableHashes=@(Get-FileHash -LiteralPath $taskController,$taskFixture -Algorithm SHA256 | Select-Object Path,Hash)
    evidenceFileHashes=@(Get-ChildItem -LiteralPath $ResultsDir -Recurse -File -Filter '*.bin' | Get-FileHash -Algorithm SHA256 | Select-Object Path,Hash)
    cases=$taskResults; remainingFixtureProcesses=$taskRemaining.Count
    allHarnessChecksPassed=($taskResults.Count -eq $taskCases.Count -and @($taskResults | Where-Object { -not $_.harnessPassed }).Count -eq 0 -and $taskRemaining.Count -eq 0)
}
$taskSummary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $ResultsDir 'summary.json') -Encoding UTF8
Write-Output "RESULTS $ResultsDir"
if (-not $taskSummary.allHarnessChecksPassed) { throw 'R2 harness failed; inspect raw logs.' }
Write-Output 'ALL_HARNESS_CHECKS_PASSED (negative experiments remain failed/rejected)'
