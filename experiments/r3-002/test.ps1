param(
    [string]$OutDir = (Join-Path $PSScriptRoot 'out'),
    [string]$ResultsDir = (Join-Path $PSScriptRoot ('results\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))),
    [ValidateSet('modifier','all')][string]$Scenario = 'all',
    [ValidateRange(0,30)][int]$HoldSeconds = 0
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Drawing
$ResultsDir = [IO.Path]::GetFullPath($ResultsDir)
if (Test-Path -LiteralPath $ResultsDir) { throw 'Use a new ResultsDir; previous evidence is immutable.' }
New-Item -ItemType Directory -Path $ResultsDir | Out-Null
$taskExe = [IO.Path]::GetFullPath((Join-Path $OutDir 'r302.exe'))
$taskRun = Join-Path $ResultsDir 'run'
$taskChecks = [Collections.Generic.List[object]]::new()
$taskManualStatus = if ($HoldSeconds) { 'not_verified' } else { 'not_executed' }
function Check([string]$Name, [bool]$Okay) {
    $taskChecks.Add([pscustomobject]@{name=$Name;passed=$Okay})
    Write-Output "evidence $Name=$Okay"
}
function Lines([string]$Path) { @(Get-Content -LiteralPath $Path -Encoding UTF8 | Where-Object { $_ } | ForEach-Object { $_ | ConvertFrom-Json }) }
function High($Value, [int]$Mask) { ($Value -band $Mask) -ne 0 }

$taskSources = @('channel.hpp','runtime.cpp','input.cpp','controller.cpp','build.ps1','test.ps1','../r3/fixture.cpp','../r3/bridge.cpp','../r3/protocol.hpp')
$taskIdentity = foreach ($taskSource in $taskSources) {
    $taskPath = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot $taskSource)).Path
    [ordered]@{file=$taskSource;sha256=(Get-FileHash -LiteralPath $taskPath -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$taskPrincipal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
[ordered]@{
    time=(Get-Date -Format o); os=[Environment]::OSVersion.VersionString; architecture=$env:PROCESSOR_ARCHITECTURE
    elevated=$taskPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    baseCommit=(& git -C $PSScriptRoot rev-parse HEAD); sourceFiles=$taskIdentity
    executableSha256=(Get-FileHash -LiteralPath $taskExe -Algorithm SHA256).Hash.ToLowerInvariant()
    runtimeSha256=(Get-FileHash -LiteralPath (Join-Path $OutDir 'runtime.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
    driver=$false; systemSettingsChanged=$false; guiSnapshotRestore=$false; manualHostTest=$taskManualStatus
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $ResultsDir 'identity.json') -Encoding UTF8
Copy-Item -LiteralPath (Join-Path $OutDir 'build.json') -Destination (Join-Path $ResultsDir 'build.json')
& $taskExe --output $taskRun --scenario $Scenario --hold-ms ($HoldSeconds * 1000) 2>&1 | Tee-Object -FilePath (Join-Path $ResultsDir 'console.log')
$taskExit = $LASTEXITCODE
$taskEvents = Lines (Join-Path $taskRun 'controller.jsonl')
$taskCompleted = @($taskEvents | Where-Object type -eq 'experiment_complete')
Check 'experiment-completed-with-assertions' ($taskExit -eq 0 -and $taskCompleted.Count -eq 1 -and $taskCompleted[0].failedAssertions -eq 0)
$taskExpectedFailures = @('baseline.queue_ctrl_consistency','unhooked.queue_ctrl_consistency','injected_missing_update.queue_ctrl_consistency')
if ($Scenario -eq 'all') { $taskExpectedFailures += @('baseline.native_shift_selection','injected_cleanup_timeout.release_confirmed') }
$taskFailures = @($taskEvents | Where-Object { $_.type -eq 'capability' -and $_.status -eq 'fail' })
Check 'expected-capability-failures-remain-fail' ($taskFailures.Count -eq $taskExpectedFailures.Count -and @($taskFailures | Where-Object { $_.name -notin $taskExpectedFailures }).Count -eq 0)
Check 'evidence-assertions-separate-from-capabilities' (@($taskEvents | Where-Object { $_.type -eq 'assertion' -and -not $_.passed }).Count -eq 0)
Check 'R3-not-claimed' ($taskCompleted.Count -eq 1 -and -not $taskCompleted[0].R3Complete -and -not $taskCompleted[0].guiRestore)

$taskReady = @($taskEvents | Where-Object type -eq 'instance_ready')
Check 'same-fixture-binary-distinct-owned-desktops' ($taskReady.Count -eq 3 -and @($taskReady.state.desktop | Select-Object -Unique).Count -eq 3)
foreach ($taskView in @('baseline','A','B')) {
    $taskReadyView = @($taskReady | Where-Object name -eq $taskView)[0]
    $taskInput = Lines (Join-Path $taskRun "$taskView-input.jsonl")
    $taskFixture = Lines (Join-Path $taskRun "$taskView-events.jsonl")
    $taskHook = @($taskInput | Where-Object stage -eq 'hook')
    $taskRemoved = @($taskHook | Where-Object removed -eq 1)
    $taskAccepted = @($taskInput | Where-Object stage -eq 'accepted')
    $taskOkay = $true; $taskExpectedSequence = 1
    foreach ($taskItem in $taskRemoved) {
        if ($taskItem.sequence -ne $taskExpectedSequence) { $taskOkay = $false }
        $taskExpectedSequence++
        if ($taskItem.pid -ne $taskReadyView.state.pid -or $taskItem.tid -ne $taskReadyView.state.tid -or $taskItem.error -ne 0) { $taskOkay = $false }
    }
    Check "$taskView.unique-contiguous-removal-on-target-thread" $taskOkay
    $taskOkay = @($taskFixture | Where-Object { $_.queryThread -ne $_.state.tid -or $_.state.tid -ne $taskReadyView.state.tid }).Count -eq 0
    Check "$taskView.original-query-thread" $taskOkay
    Check "$taskView.no-callback-failure" (@($taskFixture | Where-Object event -eq 'callback_failure').Count -eq 0)
    $taskBatches = @($taskEvents | Where-Object { $_.type -eq 'batch' -and $_.name -eq $taskView -and $_.processed })
    $taskOkay = $true
    foreach ($taskBatch in $taskBatches) {
        $taskBatchRemoved = @($taskRemoved | Where-Object { $_.sequence -ge $taskBatch.firstSequence -and $_.sequence -le $taskBatch.lastSequence })
        if ($taskBatchRemoved.Count -ne $taskBatch.count -or @($taskInput | Where-Object { $_.stage -eq 'processed' -and $_.consumed -eq $taskBatch.lastSequence }).Count -ne 1) { $taskOkay = $false }
    }
    Check "$taskView.accepted-dequeued-processed-distinguished" $taskOkay
    Check "$taskView.all-accepted-have-removal-or-explicit-failure" ($taskAccepted.Count -eq $taskRemoved.Count -or
        ($taskView -eq 'A' -and $Scenario -eq 'all' -and $taskAccepted.Count -eq ($taskRemoved.Count+2) -and @($taskInput | Where-Object stage -eq 'channel_invalid').Count -eq 1))

    # Reconstruct expected per-event high bits from immutable submitted packets,
    # then compare both real query APIs. A peek must observe the previous state.
    $taskDesired = @{}; $taskOkay = $true; $taskPeeks = 0
    foreach ($taskItem in $taskHook) {
        if ($taskItem.removed -and $taskItem.applied) {
            if ($taskItem.kind -eq 5) { $taskDesired.Clear() }
            elseif ($taskItem.kind -eq 1) { $taskDesired[[int]$taskItem.vk] = [bool]$taskItem.down }
        }
        if (-not $taskItem.removed) { $taskPeeks++; if ($taskItem.applied) { $taskOkay = $false } }
        $taskCtrl = $taskDesired[162] -or $taskDesired[163]; $taskShift = $taskDesired[160] -or $taskDesired[161]
        if ((High $taskItem.ctrl 32768) -ne $taskCtrl -or (High $taskItem.keyboardCtrl 128) -ne $taskCtrl -or
            (High $taskItem.shift 32768) -ne $taskShift -or (High $taskItem.keyboardShift 128) -ne $taskShift -or
            (High $taskItem.leftCtrl 128) -ne [bool]$taskDesired[162] -or (High $taskItem.rightCtrl 128) -ne [bool]$taskDesired[163] -or
            (High $taskItem.leftShift 128) -ne [bool]$taskDesired[160] -or (High $taskItem.rightShift 128) -ne [bool]$taskDesired[161]) { $taskOkay = $false }
    }
    Check "$taskView.per-event-real-queue-table-and-peek-state" ($taskOkay -and $taskPeeks -ge 2)
    $taskPeekGroups = @($taskHook | Where-Object removed -eq 0 | Group-Object sequence)
    Check "$taskView.repeated-peek-witness" (@($taskPeekGroups | Where-Object Count -ge 2).Count -gt 0)

    # A hook return is not control completion: independently match ordered native
    # control_before/control_after observations from the unchanged fixture.
    $taskKeys = @($taskRemoved | Where-Object kind -eq 1)
    $taskBefore = @($taskFixture | Where-Object { $_.event -eq 'control_before' -and $_.message -in 256,257 })
    $taskAfter = @($taskFixture | Where-Object { $_.event -eq 'control_after' -and $_.message -in 256,257 })
    $taskExtra = if ($taskView -eq 'baseline') { 1 } else { 0 }
    $taskOkay = $taskBefore.Count -eq ($taskKeys.Count+$taskExtra) -and $taskAfter.Count -eq $taskBefore.Count
    for ($taskIndex=0; $taskIndex -lt $taskKeys.Count -and $taskIndex -lt $taskBefore.Count; $taskIndex++) {
        $taskKey = $taskKeys[$taskIndex]; $taskPre = $taskBefore[$taskIndex]; $taskPost = $taskAfter[$taskIndex]
        $taskGeneric = if ($taskKey.vk -in 162,163) { 17 } elseif ($taskKey.vk -in 160,161) { 16 } else { $taskKey.vk }
        $taskMessage = if ($taskKey.down) { 256 } else { 257 }
        if ($taskPre.wParam -ne $taskGeneric -or $taskPre.message -ne $taskMessage -or $taskPre.tickMs -lt $taskKey.tickMs -or
            $taskPost.tickMs -lt $taskPre.tickMs -or (High $taskPre.state.GetKeyStateCtrl 32768) -ne (High $taskKey.ctrl 32768) -or
            (High $taskPre.state.keyboardCtrl 128) -ne (High $taskKey.keyboardCtrl 128) -or
            (High $taskPre.state.GetKeyStateShift 32768) -ne (High $taskKey.shift 32768) -or
            (High $taskPost.state.keyboardShift 128) -ne (High $taskKey.keyboardShift 128)) { $taskOkay = $false }
    }
    Check "$taskView.original-control-processing-matches-hook" $taskOkay
    if ($taskView -eq 'baseline') { Check 'baseline.never-updates-keyboard-table' (@($taskHook | Where-Object applied -ne 0).Count -eq 0) }
}

foreach ($taskCapture in @($taskEvents | Where-Object type -eq 'capture')) {
    $taskPath = Join-Path $taskRun ($taskCapture.name+'.png')
    $taskBitmap = [Drawing.Bitmap]::new($taskPath)
    $taskTick = 0; $taskPixels = $true
    try {
        for ($taskBit=0; $taskBit -lt 16; $taskBit++) {
            $taskPixel = $taskBitmap.GetPixel(($taskCapture.clientDx+28+10*$taskBit),($taskCapture.clientDy+356))
            $taskWhite = $taskPixel.R -gt 240 -and $taskPixel.G -gt 240 -and $taskPixel.B -gt 240
            $taskBlack = $taskPixel.R -lt 15 -and $taskPixel.G -lt 15 -and $taskPixel.B -lt 15
            if (-not ($taskWhite -or $taskBlack)) { $taskPixels = $false }
            if ($taskWhite) { $taskTick = $taskTick -bor (1 -shl $taskBit) }
        }
    } finally { $taskBitmap.Dispose() }
    $taskRole = $taskCapture.name -replace '-selection$',''
    $taskPaints = @(Lines (Join-Path $taskRun "$taskRole-events.jsonl") | Where-Object {
        $_.event -in 'wm_paint_end','wm_printclient','wm_print_end' -and $_.state.lastDrawUpdate -eq $taskTick -and
        $_.tickMs -ge $taskCapture.apiBeginMs -and $_.tickMs -le $taskCapture.apiEndMs
    })
    Check ($taskCapture.name+'.independent-pixel-and-paint-provenance') ($taskPixels -and $taskTick -eq $taskCapture.tick -and
        $taskCapture.error -eq 0 -and $taskCapture.afterDraws -gt $taskCapture.beforeDraws -and $taskPaints.Count -gt 0)
}
$taskCreated = @($taskEvents | Where-Object type -eq 'process_created'); $taskExited = @($taskEvents | Where-Object type -eq 'process_exited')
Check 'every-owned-process-confirmed-exited' ($taskCreated.Count -eq $taskExited.Count -and @($taskCreated | Where-Object { $_.pid -notin $taskExited.pid }).Count -eq 0)
$taskDesktops = @($taskEvents | Where-Object type -eq 'desktop_created'); $taskReleased = @($taskEvents | Where-Object type -eq 'desktop_released')
Check 'every-owned-desktop-released' ($taskDesktops.Count -eq $taskReleased.Count -and @($taskReleased | Where-Object closed -ne $true).Count -eq 0)
Check 'no-cleanup-errors' (@($taskEvents | Where-Object type -eq 'cleanup_error').Count -eq 0)

$taskNativeSources = @('controller.cpp','input.cpp','runtime.cpp','channel.hpp','../r3/fixture.cpp','../r3/bridge.cpp','../r3/protocol.hpp')
$taskForbidden = '\b(SendInput|SwitchDesktop|SetCursorPos|SetForegroundWindow|AttachThreadInput|SetWindowText[AW]?|EM_SETSEL)\s*\('
$taskBadCalls = foreach ($taskFile in $taskNativeSources) { Select-String -LiteralPath (Join-Path $PSScriptRoot $taskFile) -Pattern $taskForbidden }
Check 'no-host-input-or-business-setters' (@($taskBadCalls).Count -eq 0)
$taskOutside = foreach ($taskFile in $taskNativeSources | Where-Object { $_ -ne 'runtime.cpp' }) {
    Select-String -LiteralPath (Join-Path $PSScriptRoot $taskFile) -Pattern '\bSetKeyboardState\s*\('
}
Check 'only-runtime-DLL-updates-native-keyboard-table' (@($taskOutside).Count -eq 0)
$taskBaselineClean = @(& git -C $PSScriptRoot diff 9703ae9 -- ../r3 ../common ../r1 ../r2)
Check 'R3-001-and-existing-experiments-unchanged' ($taskBaselineClean.Count -eq 0)
$taskAll = @($taskChecks | Where-Object passed -ne $true).Count -eq 0
[ordered]@{ scenario=$Scenario; processExit=$taskExit; allEvidenceChecksPassed=$taskAll; checks=$taskChecks
    capabilityResults=@($taskEvents | Where-Object type -eq 'capability'); assertions=@($taskEvents | Where-Object type -eq 'assertion'); manualHostTest=$taskManualStatus
    R3Complete=$false; guiRestore=$false
} | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $ResultsDir 'summary.json') -Encoding UTF8
if (-not $taskAll) { throw 'R3-002 evidence checks failed; inspect summary and raw logs.' }
Write-Output 'R3_002_EVIDENCE_CHECKS_PASSED (only the declared managed queue-keyboard subset)'
