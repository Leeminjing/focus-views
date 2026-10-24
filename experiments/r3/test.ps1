param(
    [string]$OutDir = (Join-Path $PSScriptRoot 'out'),
    [string]$ResultsDir = (Join-Path $PSScriptRoot ('results\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))),
    [int]$HoldSeconds = 0
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Drawing
$taskExe = [IO.Path]::GetFullPath((Join-Path $OutDir 'r3.exe'))
$ResultsDir = [IO.Path]::GetFullPath($ResultsDir)
if (-not (Test-Path -LiteralPath $taskExe)) { throw 'Build r3.exe first.' }
if ($HoldSeconds -lt 0 -or $HoldSeconds -gt 300) { throw 'HoldSeconds must be 0..300.' }
New-Item -ItemType Directory -Path $ResultsDir -Force | Out-Null
$taskChecks = [Collections.Generic.List[object]]::new()
function Check-Diagnostic([string]$Name, [bool]$Okay) {
    $taskChecks.Add([pscustomobject]@{ name=$Name; passed=$Okay })
    Write-Output "harness $Name=$Okay"
}
function Read-JsonLines([string]$Path) { @(Get-Content -LiteralPath $Path -Encoding UTF8 | Where-Object { $_ } | ForEach-Object { $_ | ConvertFrom-Json }) }
function Check-OwnedCleanup($Events, [string]$Prefix) {
    $taskCreated = @($Events | Where-Object type -eq 'process_created')
    $taskExited = @($Events | Where-Object type -eq 'process_exited')
    $taskOkay = $taskCreated.Count -gt 0 -and $taskCreated.Count -eq $taskExited.Count
    foreach ($taskProcess in $taskCreated) { if ($taskProcess.pid -notin @($taskExited.pid)) { $taskOkay = $false } }
    Check-Diagnostic "$Prefix.process-cleanup" $taskOkay
    $taskDesks = @($Events | Where-Object type -eq 'desktop_created')
    $taskReleased = @($Events | Where-Object type -eq 'desktop_released')
    $taskOkay = $taskDesks.Count -eq $taskReleased.Count -and @($taskReleased | Where-Object { -not $_.closed }).Count -eq 0
    foreach ($taskDesk in $taskDesks) { if ($taskDesk.desktop -notin @($taskReleased.desktop)) { $taskOkay = $false } }
    Check-Diagnostic "$Prefix.desktop-cleanup" $taskOkay
    Check-Diagnostic "$Prefix.no-cleanup-errors" (@($Events | Where-Object type -eq 'cleanup_error').Count -eq 0)
}

$taskRun = Join-Path $ResultsDir 'run'
& $taskExe --output $taskRun --hold-ms ($HoldSeconds * 1000) 2>&1 | Tee-Object -FilePath (Join-Path $ResultsDir 'console.log')
$taskExit = $LASTEXITCODE
$taskEvents = Read-JsonLines (Join-Path $taskRun 'controller.jsonl')
Check-Diagnostic 'diagnostic-completed' ($taskExit -eq 0 -and @($taskEvents | Where-Object type -eq 'diagnostic_complete').Count -eq 1)
Check-OwnedCleanup $taskEvents 'all'
$taskReady = @($taskEvents | Where-Object type -eq 'instance_ready')
Check-Diagnostic 'distinct-desktops' ($taskReady.Count -eq 2 -and $taskReady[0].state.desktop -ne $taskReady[1].state.desktop)
$taskTrace = @{}
foreach ($taskRole in @('A','B')) {
    $taskTrace[$taskRole] = Read-JsonLines (Join-Path $taskRun "$taskRole-events.jsonl")
    Check-Diagnostic "$taskRole.native-query-thread" (@($taskTrace[$taskRole] | Where-Object { $_.queryThread -ne $_.state.tid }).Count -eq 0)
    Check-Diagnostic "$taskRole.no-callback-failure" (@($taskTrace[$taskRole] | Where-Object event -eq 'callback_failure').Count -eq 0)
}
$taskImages = @()
foreach ($taskCapture in @($taskEvents | Where-Object type -eq 'capture')) {
    $taskImage = Join-Path $taskRun ($taskCapture.name + '.png')
    if (-not $taskCapture.usable) {
        Check-Diagnostic ("rejected-frame-absent." + $taskCapture.name) (-not (Test-Path -LiteralPath $taskImage) -and -not (Test-Path -LiteralPath ($taskImage + '.partial')))
        continue
    }
    $taskHash = (Get-FileHash -LiteralPath $taskImage -Algorithm SHA256).Hash.ToLowerInvariant()
    $taskBitmap = [System.Drawing.Bitmap]::new($taskImage)
    try {
        $taskTick = 0; $taskPixelsOkay = $true
        for ($taskBit=0; $taskBit -lt 16; $taskBit++) {
            $taskPixel = $taskBitmap.GetPixel(($taskCapture.clientDx+28+10*$taskBit),($taskCapture.clientDy+356))
            $taskWhite = $taskPixel.R -gt 240 -and $taskPixel.G -gt 240 -and $taskPixel.B -gt 240
            $taskBlack = $taskPixel.R -lt 15 -and $taskPixel.G -lt 15 -and $taskPixel.B -lt 15
            if (-not ($taskWhite -or $taskBlack)) { $taskPixelsOkay = $false }
            if ($taskWhite) { $taskTick = $taskTick -bor (1 -shl $taskBit) }
        }
        $taskDimensionsOkay = $taskBitmap.Width -eq $taskCapture.width -and $taskBitmap.Height -eq $taskCapture.height
    } finally { $taskBitmap.Dispose() }
    $taskPaints = @($taskTrace[$taskCapture.role] | Where-Object {
        $_.event -in 'wm_paint_end','wm_printclient','wm_print_end' -and $_.state.lastDrawUpdate -eq $taskTick -and
        $_.tickMs -ge $taskCapture.apiBeginMs -and $_.tickMs -le $taskCapture.apiEndMs
    })
    $taskOkay = $taskPixelsOkay -and $taskDimensionsOkay -and $taskTick -eq $taskCapture.printUpdate -and
        $taskHash -eq $taskCapture.sha256 -and $taskCapture.afterDraws -gt $taskCapture.beforeDraws -and $taskPaints.Count -gt 0
    Check-Diagnostic ('image-paint-provenance.'+$taskCapture.name) $taskOkay
    $taskImages += [pscustomobject]@{ name=$taskCapture.name; file=$taskImage; tick=$taskTick; hash=$taskHash; matchedPaints=$taskPaints.Count; verified=$taskOkay }
}
$taskObs = @{}
foreach ($taskObservation in @($taskEvents | Where-Object type -eq 'observation')) { $taskObs[$taskObservation.name] = $taskObservation.state }
$taskCaps = @{}
foreach ($taskCapability in @($taskEvents | Where-Object type -eq 'capability')) { $taskCaps[$taskCapability.name] = $taskCapability }
$taskCtrl = $taskObs['Ctrl-down-A']
$taskCtrlBefore = $taskObs['before-Ctrl']
$taskExpectedCtrl = $taskCtrl.ctrlDownMessages -eq ($taskCtrlBefore.ctrlDownMessages+1) -and
    ($taskCtrl.GetKeyStateCtrl -band 32768) -ne 0 -and $taskCtrl.GetKeyboardStateOk -and ($taskCtrl.keyboardCtrl -band 128) -ne 0
Check-Diagnostic 'modifier-verdict-matches-native-evidence' (($taskCaps['posted_ctrl_queue_state'].status -eq 'pass') -eq $taskExpectedCtrl)
Check-Diagnostic 'modifier-message-arrived' ($taskCtrl.ctrlDownMessages -eq ($taskCtrlBefore.ctrlDownMessages+1))
$taskSelection = $taskObs['Shift-End-selection-A']
Check-Diagnostic 'selection-verdict-matches-control' (($taskCaps['keyboard_shift_selection'].status -eq 'pass') -eq ($taskSelection.selectionStart -ne $taskSelection.selectionEnd))
Check-Diagnostic 'routing-not-claimed' ($taskCaps['native_cross_boundary_capture_routing'].status -eq 'not_verified')
Check-Diagnostic 'manual-not-fabricated' ($taskCaps['manual_host_interaction'].status -in 'not_executed','not_verified')
$taskHung = @($taskEvents | Where-Object { $_.type -eq 'bridge' -and $_.operation -eq 'capture' -and $_.timeout })
Check-Diagnostic 'PrintWindow-block-exercised' ($taskHung.Count -eq 1 -and $taskHung[0].printCalled -eq 1 -and $taskHung[0].error -eq 1460)
Check-Diagnostic 'failed-instance-rollback' (@($taskEvents | Where-Object type -eq 'instance_creation_rollback').Count -eq 1)
$taskHost = @($taskEvents | Where-Object type -eq 'host_result')[0]
Check-Diagnostic 'external-input-not-claimed-static-pass' ($taskHost.externalInputIntervals -eq 0 -or $taskCaps['static_host_observation'].status -ne 'pass')
foreach ($taskRole in @('A','B')) {
    $taskFirst = @($taskImages | Where-Object name -eq "$taskRole-initial")
    $taskLater = @($taskImages | Where-Object name -eq "$taskRole-later")
    if ($taskFirst.Count -eq 1 -and $taskLater.Count -eq 1) {
        Check-Diagnostic "$taskRole.actual-image-progress" ($taskLater[0].tick -gt $taskFirst[0].tick -and $taskLater[0].hash -ne $taskFirst[0].hash)
    } else { Check-Diagnostic "$taskRole.gdi-failure-not-hidden" ($taskCaps['gdi_printwindow'].status -ne 'pass') }
}

# Small, separately runnable reproductions of the native modifier gap and the
# capture-cancellation behavior. A capability may fail while the probe completes.
foreach ($taskScenario in @('capture','modifier','capture-recovery')) {
    $taskProbe = Join-Path $ResultsDir ('probe-'+$taskScenario)
    & $taskExe --output $taskProbe --scenario $taskScenario 2>&1 | Tee-Object -FilePath (Join-Path $ResultsDir ('probe-'+$taskScenario+'.log'))
    $taskProbeExit = $LASTEXITCODE
    $taskProbeEvents = Read-JsonLines (Join-Path $taskProbe 'controller.jsonl')
    Check-Diagnostic "probe.$taskScenario.completed" ($taskProbeExit -eq 0 -and @($taskProbeEvents | Where-Object type -eq 'probe_complete').Count -eq 1)
    Check-OwnedCleanup $taskProbeEvents "probe.$taskScenario"
}
$taskBanned = @(Get-ChildItem -LiteralPath $PSScriptRoot -File | Where-Object { $_.Extension -in '.cpp','.hpp' } | Select-String -Pattern '\b(SwitchDesktop|SendInput|SetCursorPos|SetForegroundWindow|AttachThreadInput|SetKeyboardState)\s*\(')
Check-Diagnostic 'no-forbidden-native-input-calls' ($taskBanned.Count -eq 0)
$taskRemaining = @(Get-CimInstance Win32_Process -Filter "Name='r3.exe'" | Where-Object { $_.ExecutablePath -eq $taskExe })
Check-Diagnostic 'no-owned-processes-left' ($taskRemaining.Count -eq 0)
$taskOs = Get-CimInstance Win32_OperatingSystem
$taskPrincipal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
$taskShared = @('../common/native.hpp','../r1/snapshot.hpp') | ForEach-Object { Join-Path $PSScriptRoot $_ }
[ordered]@{
    time=(Get-Date -Format o); baseCommit=(& git -C $PSScriptRoot rev-parse HEAD)
    os=$taskOs.Caption; osVersion=$taskOs.Version; osBuild=$taskOs.BuildNumber; architecture=$env:PROCESSOR_ARCHITECTURE
    elevated=$taskPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    build=(Get-Content -LiteralPath (Join-Path $OutDir 'build.json') -Raw | ConvertFrom-Json)
    sourceHashes=@(Get-ChildItem -LiteralPath $PSScriptRoot -File | Where-Object { $_.Extension -in '.cpp','.hpp','.ps1' } | Get-FileHash -Algorithm SHA256 | Select-Object Path,Hash)
    sharedSourceHashes=@(Get-FileHash -LiteralPath $taskShared -Algorithm SHA256 | Select-Object Path,Hash)
    executableHash=(Get-FileHash -LiteralPath $taskExe -Algorithm SHA256 | Select-Object Path,Hash)
    diagnosticCompleted=($taskExit -eq 0); capabilities=@($taskEvents | Where-Object type -eq 'capability'); hostObservation=$taskHost
    images=$taskImages; harnessChecks=$taskChecks.ToArray(); allHarnessChecksPassed=(@($taskChecks | Where-Object { -not $_.passed }).Count -eq 0)
    manualInteractionExecuted=$false; guiSnapshotRestoreVerified=$false; R3Complete=$false
} | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath (Join-Path $ResultsDir 'summary.json') -Encoding UTF8
Write-Output "RESULTS $ResultsDir"
if (@($taskChecks | Where-Object { -not $_.passed }).Count) { throw 'Diagnostic evidence harness failed; capability failures are separately recorded.' }
Write-Output 'DIAGNOSTIC_HARNESS_COMPLETED (native capability failures remain failures)'
