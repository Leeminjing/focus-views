param(
    [string]$OutDir = (Join-Path $PSScriptRoot 'out'),
    [string]$ResultsDir = (Join-Path $PSScriptRoot ('results\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))),
    [ValidateSet('minimal','all')][string]$Scenario = 'all',
    [ValidateRange(0,30)][int]$HoldSeconds = 0,
    [string]$R303OutDir = (Join-Path $PSScriptRoot '../r3-003/out'),
    [string]$R302OutDir = (Join-Path $PSScriptRoot '../r3-002/out')
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.Drawing
$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if (Test-Path -LiteralPath $ResultsDir) { throw 'ResultsDir must be new; preserve previous evidence.' }
New-Item -ItemType Directory -Path $ResultsDir | Out-Null
$taskExe=[IO.Path]::GetFullPath((Join-Path $OutDir 'r304.exe'))
$taskRun=Join-Path $ResultsDir 'run'
$taskChecks=[Collections.Generic.List[object]]::new()
function Check([string]$Name,[bool]$Okay) { $taskChecks.Add([pscustomobject]@{name=$Name;passed=$Okay}); Write-Output "evidence $Name=$Okay" }
function Lines([string]$Path) { @(Get-Content -LiteralPath $Path -Encoding utf8 | Where-Object { $_ } | ForEach-Object { $_ | ConvertFrom-Json }) }
function High($Value,[int]$Mask=32768) { ($Value -band $Mask) -ne 0 }
$taskSources=@('channel.hpp','iat.hpp','runtime.cpp','input.cpp','controller.cpp','observer.cpp','build.ps1','test.ps1','../r3/fixture.cpp','../r3/bridge.cpp','../r3/protocol.hpp')
$taskIdentity=foreach ($taskSource in $taskSources) {
    [ordered]@{file=$taskSource;sha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $taskSource)).Hash.ToLowerInvariant()}
}
$taskPrincipal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
[ordered]@{time=(Get-Date -Format o);os=[Environment]::OSVersion.VersionString;architecture=$env:PROCESSOR_ARCHITECTURE
    elevated=$taskPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator);baseCommit=(& git -C $PSScriptRoot rev-parse HEAD)
    sourceFiles=$taskIdentity;exeSha256=(Get-FileHash -LiteralPath $taskExe).Hash.ToLowerInvariant()
    dllSha256=(Get-FileHash -LiteralPath (Join-Path $OutDir 'runtime304.dll')).Hash.ToLowerInvariant()
    manualHostTest=$(if ($HoldSeconds) {'not_verified'} else {'not_executed'});systemSettingsChanged=$false;driver=$false;guiRestore=$false
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $ResultsDir 'identity.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $OutDir 'build.json') -Destination (Join-Path $ResultsDir 'build.json')
& $taskExe --output $taskRun --scenario $Scenario --hold-seconds $HoldSeconds 2>&1 | Tee-Object -FilePath (Join-Path $ResultsDir 'console.log')
$taskExit=$LASTEXITCODE
$taskEvents=Lines (Join-Path $taskRun 'controller.jsonl')
$taskComplete=@($taskEvents | Where-Object type -eq 'experiment_complete')
Check 'controller-completed-with-no-failed-assertions' ($taskExit -eq 0 -and $taskComplete.Count -eq 1 -and $taskComplete[0].failedAssertions -eq 0)
Check 'R3-and-GUI-restore-not-claimed' ($taskComplete.Count -eq 1 -and -not $taskComplete[0].R3Complete -and -not $taskComplete[0].guiRestore)
$taskFails=@($taskEvents | Where-Object { $_.type -eq 'capability' -and $_.status -eq 'fail' })
$taskExpectedFails=@('native.private_async_mouse','native.private_cursor')
if ($Scenario -eq 'all') { $taskExpectedFails+=@('negative.foreign-window-rejected','negative.destroyed-capture-invalidates','negative.omitted-up-invalidates','negative.uninstall-refused') }
Check 'native-and-negative-capability-failures-stay-fail' ($taskFails.Count -eq $taskExpectedFails.Count -and @($taskFails | Where-Object name -NotIn $taskExpectedFails).Count -eq 0)
$taskInstances=@($taskEvents | Where-Object type -in 'instance_ready','instance_rejected')
foreach ($taskInstance in $taskInstances) {
    $taskView=$taskInstance.name
    $taskInput=Lines (Join-Path $taskRun "$taskView-input.jsonl")
    $taskFixture=Lines (Join-Path $taskRun "$taskView-events.jsonl")
    Check "$taskView.real-query-thread" (@($taskFixture | Where-Object { $_.queryThread -ne $taskInstance.state.tid -or $_.state.pid -ne $taskInstance.state.pid }).Count -eq 0)
    $taskInstall=@($taskInput | Where-Object stage -eq 'hook_installed')
    if ($taskInstance.type -eq 'instance_rejected') {
        Check "$taskView.no-ready-or-input-on-install-failure" ($taskInstall.Count -eq 0 -and @($taskInput | Where-Object stage -eq 'accepted').Count -eq 0)
        continue
    }
    $taskIat=$taskInstall[0]
    Check "$taskView.checked-module-and-page-protection" ($taskIat.module -eq $taskExe -and $taskIat.nativeModule -match '\\USER32\.dll$' -and
        $taskIat.replacementModule -match '\\runtime304\.dll$' -and $taskIat.pageBefore -eq $taskIat.pageAfter -and $taskIat.cursorPageBefore -eq $taskIat.cursorPageAfter -and $taskIat.slot -ne $taskIat.cursorSlot)
    Check "$taskView.two-imports-installation" $(if ($taskView -eq 'native') { $taskIat.slotAfter -eq $taskIat.native -and $taskIat.cursorAfter -eq $taskIat.cursorNative -and -not $taskIat.iatInstalled }
        else { $taskIat.slotAfter -eq $taskIat.replacement -and $taskIat.cursorAfter -eq $taskIat.cursorReplacement -and $taskIat.pinned -eq 1 })
    # Reconstruct each accepted version independently from the action, then check
    # the application's real intercepted queries against that version's ledger.
    $taskState=@{left=0;ctrl=0;shift=0;lc=0;rc=0;ls=0;rs=0;xkey=0;cursorX=0;cursorY=0;sequence=0}
    $taskLedger=@{'0'=$taskState.Clone()}; $taskVersion=0; $taskOkay=$true
    foreach ($taskE in @($taskInput | Where-Object stage -eq 'effective')) {
        $taskVersion++
        if ($taskE.kind -eq 5) { foreach ($taskK in @('left','ctrl','shift','lc','rc','ls','rs','xkey')) { $taskState[$taskK]=0 } }
        elseif ($taskE.kind -eq 3) {
            if ($taskE.action -eq 2) { $taskState.left=1 } elseif ($taskE.action -eq 3) { $taskState.left=0 }
            $taskState.cursorX=$taskE.cursorX; $taskState.cursorY=$taskE.cursorY
        } elseif ($taskE.kind -eq 1) {
            $taskK=switch ($taskE.vk) {162 {'lc'} 163 {'rc'} 160 {'ls'} 161 {'rs'} 88 {'xkey'} default {$null}}
            if ($taskK) { $taskState[$taskK]=[int]$taskE.down }
        }
        $taskState.ctrl=$taskState.lc -bor $taskState.rc; $taskState.shift=$taskState.ls -bor $taskState.rs
        $taskState.sequence=$taskE.sequence
        if ($taskE.version -ne $taskVersion -or $taskE.left -ne $taskState.left -or $taskE.ctrl -ne $taskState.ctrl -or $taskE.shift -ne $taskState.shift) { $taskOkay=$false }
        $taskLedger[[string]$taskVersion]=$taskState.Clone()
    }
    foreach ($taskQ in @($taskInput | Where-Object stage -eq 'virtual_query')) {
        if ($taskQ.pid -ne $taskInstance.state.pid -or $taskQ.tid -ne $taskInstance.state.tid) { $taskOkay=$false }
        if (-not $taskQ.valid) { if ($taskQ.result -ne 0) { $taskOkay=$false }; continue }
        $taskAt=$taskLedger[[string]$taskQ.version]
        if (-not $taskAt -or $taskQ.sequence -ne $taskAt.sequence -or $taskQ.module -ne $taskIat.image -or $taskQ.caller -lt $taskIat.image -or $taskQ.caller -ge ($taskIat.image+$taskIat.imageSize)) { $taskOkay=$false; continue }
        if ($taskQ.api -eq 'GetCursorPos') {
            if ($taskQ.result -ne 1 -or $taskQ.cursorX -ne $taskAt.cursorX -or $taskQ.cursorY -ne $taskAt.cursorY) { $taskOkay=$false }
        } else {
            $taskK=switch ($taskQ.vk) {1 {'left'} 17 {'ctrl'} 16 {'shift'} 162 {'lc'} 163 {'rc'} 160 {'ls'} 161 {'rs'} 88 {'xkey'} default {$null}}
            $taskExpected=if ($taskK -and $taskAt[$taskK]) {-32768} else {0}
            if ($taskQ.result -ne $taskExpected -or [bool]$taskQ.supported -ne [bool]$taskK) { $taskOkay=$false }
        }
    }
    Check "$taskView.actual-queries-match-accepted-action-ledger" $taskOkay
    if ($taskView -in 'A','B') {
        $taskHooks=@($taskInput | Where-Object { $_.stage -eq 'hook' -and $_.removed })
        $taskOkay=$true; $taskLast=0; $taskLeft=0
        foreach ($taskH in $taskHooks) {
            if ($taskH.sequence -ne ++$taskLast) { $taskOkay=$false }
            if ($taskH.kind -eq 3) {
                if ($taskH.action -eq 2) { $taskLeft=1 } elseif ($taskH.action -eq 3) { $taskLeft=0 }
                if ((High $taskH.queueLeft) -ne [bool]$taskLeft -or (High $taskH.keyboardLeft 128) -ne [bool]$taskLeft -or
                    $taskH.target -notin @($taskInstance.state.top,$taskInstance.state.edit,$taskInstance.state.button,$taskInstance.state.drag)) { $taskOkay=$false }
            } elseif ($taskH.kind -eq 5) { $taskLeft=0 }
        }
        Check "$taskView.route-targets-and-dequeue-button-order" $taskOkay
        Check "$taskView.peeks-non-consuming" (@($taskInput | Where-Object { $_.stage -eq 'hook' -and -not $_.removed -and $_.applied }).Count -eq 0)
        Check "$taskView.actual-import-call-instructions" (@($taskEvents | Where-Object { $_.type -eq 'callsite' -and $_.view -eq $taskView -and $_.matched -and $_.api -eq 'GetCursorPos' }).Count -gt 0 -and
            @($taskEvents | Where-Object { $_.type -eq 'callsite' -and $_.view -eq $taskView -and -not $_.matched }).Count -eq 0)
    }
    if ($taskView -in 'native','A','B','close-held') {
        Check "$taskView.normal-close-restores-both-imports" (@($taskInput | Where-Object { $_.stage -eq 'channel_closed' -and $_.health -eq 0 -and $_.cleanupDone -eq 1 -and
            $_.slotAfter -eq $_.native -and $_.cursorAfter -eq $_.cursorNative -and $_.pageBefore -eq $_.pageAfter -and $_.cursorPageBefore -eq $_.cursorPageAfter -and $_.pinned -eq 0 }).Count -eq 1)
    }
}
$taskA=Lines (Join-Path $taskRun 'A-events.jsonl'); $taskB=Lines (Join-Path $taskRun 'B-events.jsonl')
Check 'actual-drag-procedure-sees-outside-coordinate-and-capture' (@($taskA | Where-Object { $_.event -eq 'drag_move' -and $_.state.dragX -eq 626 -and $_.state.dragY -eq 146 -and $_.state.capture -eq $_.state.drag -and
    (High $_.state.GetKeyStateLeft) -and (High $_.state.GetAsyncKeyStateLeft) -and (High $_.state.keyboardLeft 128) }).Count -gt 0)
Check 'B-native-button-produces-command' (@($taskB | Where-Object event -eq 'button_clicked').Count -gt 0)
if ($Scenario -eq 'all') {
    Check 'burst-separates-accepted-up-from-dequeued-down-in-fixture' (@($taskA | Where-Object { $_.event -eq 'drag_down' -and (High $_.state.GetKeyStateLeft) -and -not (High $_.state.GetAsyncKeyStateLeft) }).Count -gt 0)
    $taskHung=Lines (Join-Path $taskRun 'hung-thread-input.jsonl')
    Check 'hung-cleanup-explicitly-unconfirmed' (@($taskHung | Where-Object stage -eq 'invalid_channel_cleanup_unconfirmed').Count -gt 0 -and @($taskHung | Where-Object stage -eq 'channel_closed').Count -eq 0)
    $taskRefusal=Lines (Join-Path $taskRun 'uninstall-refusal-input.jsonl')
    Check 'uninstall-refusal-retains-live-runtime-and-failure' (@($taskRefusal | Where-Object { $_.stage -eq 'channel_invalid' -and $_.error -eq 5 -and $_.pinned -eq 1 -and $_.iatInstalled -eq 1 }).Count -eq 1)
}
foreach ($taskCapture in @($taskEvents | Where-Object type -eq 'capture')) {
    $taskBitmap=[Drawing.Bitmap]::new((Join-Path $taskRun ($taskCapture.name+'.png')))
    $taskTick=0; $taskPixels=$true
    try {
        for ($taskBit=0;$taskBit -lt 16;$taskBit++) {
            $taskPixel=$taskBitmap.GetPixel(($taskCapture.clientDx+28+10*$taskBit),($taskCapture.clientDy+356))
            $taskWhite=$taskPixel.R -gt 240 -and $taskPixel.G -gt 240 -and $taskPixel.B -gt 240
            $taskBlack=$taskPixel.R -lt 15 -and $taskPixel.G -lt 15 -and $taskPixel.B -lt 15
            if (-not ($taskWhite -or $taskBlack)) { $taskPixels=$false }
            if ($taskWhite) { $taskTick=$taskTick -bor (1 -shl $taskBit) }
        }
        if ($taskCapture.name -eq 'A-inside-held') {
            $taskDot=$taskBitmap.GetPixel(($taskCapture.clientDx+180),($taskCapture.clientDy+260))
            Check 'A.drag-actually-painted-at-routed-location' ($taskDot.B -gt 120 -and $taskDot.R -lt 100)
        }
    } finally { $taskBitmap.Dispose() }
    $taskPaints=@(Lines (Join-Path $taskRun ($taskCapture.view+'-events.jsonl')) | Where-Object {
        $_.event -in 'wm_paint_end','wm_printclient','wm_print_end' -and $_.state.lastDrawUpdate -eq $taskTick -and $_.tickMs -ge $taskCapture.apiBeginMs -and $_.tickMs -le $taskCapture.apiEndMs })
    Check ($taskCapture.name+'.independent-PNG-and-native-paint') ($taskPixels -and $taskTick -eq $taskCapture.tick -and $taskCapture.error -eq 0 -and $taskCapture.afterDraws -gt $taskCapture.beforeDraws -and $taskPaints.Count -gt 0)
}
if ($Scenario -eq 'all') {
    $taskRegression=Join-Path $ResultsDir 'r303-regression'
    & (Join-Path $PSScriptRoot '../r3-003/test.ps1') -OutDir $R303OutDir -R302OutDir $R302OutDir -ResultsDir $taskRegression
    $taskPrevious=Get-Content -LiteralPath (Join-Path $taskRegression 'summary.json') -Raw | ConvertFrom-Json
    Check 'full-R3-003-and-nested-R3-002-regression' ($taskPrevious.processExit -eq 0 -and $taskPrevious.allEvidenceChecksPassed)
}
$taskCreated=@($taskEvents | Where-Object type -eq 'process_created'); $taskExited=@($taskEvents | Where-Object type -eq 'process_exited')
Check 'every-owned-process-confirmed-exited' ($taskCreated.Count -eq $taskExited.Count -and @($taskCreated | Where-Object { $_.pid -notin $taskExited.pid }).Count -eq 0)
$taskDesktops=@($taskEvents | Where-Object type -eq 'desktop_created'); $taskReleased=@($taskEvents | Where-Object type -eq 'desktop_released')
Check 'every-owned-desktop-released' ($taskDesktops.Count -eq $taskReleased.Count -and @($taskReleased | Where-Object closed -ne $true).Count -eq 0)
Check 'no-lifecycle-cleanup-errors' (@($taskEvents | Where-Object type -eq 'cleanup_error').Count -eq 0)
$taskForbidden=foreach ($taskSource in $taskSources | Where-Object { $_ -match '\.(cpp|hpp)$' }) {
    Select-String -LiteralPath (Join-Path $PSScriptRoot $taskSource) -Pattern '\b(SendInput|SwitchDesktop|SetCursorPos|SetForegroundWindow|AttachThreadInput|SetWindowText[AW]?)\s*\(|\bEM_SETSEL\b'
}
Check 'no-host-input-or-business-state-setters' (@($taskForbidden).Count -eq 0)
$taskUnchanged=@(& git -C $PSScriptRoot diff 791a547 -- ../r1 ../r2 ../r3 ../r3-002 ../r3-003 ../common)
Check 'all-existing-experiments-unchanged' ($taskUnchanged.Count -eq 0)
$taskOkay=@($taskChecks | Where-Object passed -ne $true).Count -eq 0
[ordered]@{scenario=$Scenario;processExit=$taskExit;allEvidenceChecksPassed=$taskOkay;checks=$taskChecks
    assertions=@($taskEvents | Where-Object type -eq 'assertion');capabilities=@($taskEvents | Where-Object type -eq 'capability')
    manualHostTest=$(if ($HoldSeconds) {'not_verified'} else {'not_executed'});R3Complete=$false;guiRestore=$false;R1R2Rerun=$false
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $ResultsDir 'summary.json') -Encoding utf8
if (-not $taskOkay) { throw 'R3-004 evidence failed; inspect retained logs and summary.' }
Write-Output 'R3_004_EVIDENCE_PASSED (only owned GUI thread / left button / controlled window tree / named EXE imports)'
