param(
    [string]$OutDir = (Join-Path $PSScriptRoot 'out'),
    [string]$ResultsDir = (Join-Path $PSScriptRoot ('results\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))),
    [ValidateSet('minimal','all','exit')][string]$Scenario = 'all',
    [string]$R302OutDir = (Join-Path $PSScriptRoot '../r3-002/out')
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ResultsDir = [IO.Path]::GetFullPath($ResultsDir)
if (Test-Path -LiteralPath $ResultsDir) { throw 'Use a fresh ResultsDir; never overwrite evidence.' }
New-Item -ItemType Directory -Path $ResultsDir | Out-Null
$taskExe = [IO.Path]::GetFullPath((Join-Path $OutDir 'r303.exe'))
$taskRun = Join-Path $ResultsDir 'run'
$taskChecks = [Collections.Generic.List[object]]::new()
function Check([string]$Name, [bool]$Okay) {
    $taskChecks.Add([pscustomobject]@{name=$Name;passed=$Okay}); Write-Output "evidence $Name=$Okay"
}
function Lines([string]$Path) { @(Get-Content -LiteralPath $Path -Encoding UTF8 | Where-Object { $_ } | ForEach-Object { $_ | ConvertFrom-Json }) }
function High($Value, [int]$Mask=32768) { ($Value -band $Mask) -ne 0 }
$taskSources = @('channel.hpp','iat.hpp','runtime.cpp','input.cpp','observer.cpp','controller.cpp','build.ps1','test.ps1','../r3/fixture.cpp','../r3/bridge.cpp','../r3/protocol.hpp')
$taskIdentity = foreach ($taskSource in $taskSources) {
    [ordered]@{file=$taskSource;sha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $taskSource) -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$taskPrincipal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
[ordered]@{time=(Get-Date -Format o);os=[Environment]::OSVersion.VersionString;architecture=$env:PROCESSOR_ARCHITECTURE
    elevated=$taskPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator);baseCommit=(& git -C $PSScriptRoot rev-parse HEAD)
    sourceFiles=$taskIdentity;exeSha256=(Get-FileHash -LiteralPath $taskExe -Algorithm SHA256).Hash.ToLowerInvariant()
    dllSha256=(Get-FileHash -LiteralPath (Join-Path $OutDir 'runtime303.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
    APIInterposition=$true;systemSettingsChanged=$false;driver=$false;guiRestore=$false;manualHostTest='not_executed'
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $ResultsDir 'identity.json') -Encoding UTF8
Copy-Item -LiteralPath (Join-Path $OutDir 'build.json') -Destination (Join-Path $ResultsDir 'build.json')
& $taskExe --output $taskRun --scenario $Scenario 2>&1 | Tee-Object -FilePath (Join-Path $ResultsDir 'console.log')
$taskExit = $LASTEXITCODE
$taskEvents = Lines (Join-Path $taskRun 'controller.jsonl')
$taskComplete = @($taskEvents | Where-Object type -eq 'experiment_complete')
Check 'experiment-completed' ($taskExit -eq 0 -and $taskComplete.Count -eq 1 -and $taskComplete[0].failedAssertions -eq 0)
Check 'controller-assertions-pass' (@($taskEvents | Where-Object { $_.type -eq 'assertion' -and -not $_.passed }).Count -eq 0)
$taskExpectedFails = @()
if ($Scenario -eq 'all') { $taskExpectedFails = @('native.async_private_Ctrl','injected_restore_refusal.healthy_unload') }
elseif ($Scenario -eq 'minimal') { $taskExpectedFails = @('native.async_private_Ctrl') }
$taskCapFails = @($taskEvents | Where-Object { $_.type -eq 'capability' -and $_.status -eq 'fail' })
Check 'actual-failures-stay-fail' ($taskCapFails.Count -eq $taskExpectedFails.Count -and @($taskCapFails | Where-Object { $_.name -notin $taskExpectedFails }).Count -eq 0)
Check 'R3-and-GUI-restore-not-claimed' ($taskComplete.Count -eq 1 -and -not $taskComplete[0].R3Complete -and -not $taskComplete[0].guiRestore)
$taskInstances = @($taskEvents | Where-Object type -in 'instance_ready','instance_rejected')
foreach ($taskInstance in $taskInstances) {
    $taskView = $taskInstance.name
    $taskInput = Lines (Join-Path $taskRun "$taskView-input.jsonl")
    $taskFixture = Lines (Join-Path $taskRun "$taskView-events.jsonl")
    Check "$taskView.native-query-thread" (@($taskFixture | Where-Object { $_.queryThread -ne $taskInstance.state.tid -or $_.state.pid -ne $taskInstance.state.pid }).Count -eq 0)
    Check "$taskView.no-callback-failures" (@($taskFixture | Where-Object event -eq 'callback_failure').Count -eq 0)
    $taskInstall = @($taskInput | Where-Object stage -eq 'hook_installed')
    if ($taskInstance.type -eq 'instance_rejected') {
        Check "$taskView.never-published-ready" ($taskInstance.health -eq -1 -and $taskInstall.Count -eq 0 -and @($taskInput | Where-Object stage -eq 'accepted').Count -eq 0)
    } else {
        $taskIat = $taskInstall[0]
        Check "$taskView.EXE-native-replacement-module-identity" ($taskIat.module -eq $taskExe -and $taskIat.nativeModule -match '\\USER32\.dll$' -and
            $taskIat.replacementModule -match '\\runtime303\.dll$' -and $taskIat.image -ne $taskIat.runtimeImage -and $taskIat.pageBefore -eq $taskIat.pageAfter)
        Check "$taskView.exact-import-installation" (($taskView -eq 'native' -and $taskIat.slotAfter -eq $taskIat.native -and $taskIat.iatInstalled -eq 0) -or
            ($taskView -ne 'native' -and $taskIat.slotAfter -eq $taskIat.replacement -and $taskIat.iatInstalled -eq 1 -and $taskIat.pinned -eq 1))
    }
    $taskEffective = @($taskInput | Where-Object stage -eq 'effective')
    $taskLedger = @{ '0'=@{ctrl=0;shift=0;leftCtrl=0;rightCtrl=0;leftShift=0;rightShift=0;x=0;sequence=0} }
    $taskState = @{ctrl=0;shift=0;leftCtrl=0;rightCtrl=0;leftShift=0;rightShift=0;x=0;sequence=0}
    $taskVersion = 0; $taskOkay = $true
    foreach ($taskEffectiveEvent in $taskEffective) {
        $taskVersion++
        if ($taskEffectiveEvent.version -ne $taskVersion) { $taskOkay = $false }
        if ($taskEffectiveEvent.kind -eq 5) { foreach ($taskKey in @('ctrl','shift','leftCtrl','rightCtrl','leftShift','rightShift','x')) { $taskState[$taskKey]=0 } }
        else {
            $taskKeyName = switch ($taskEffectiveEvent.vk) { 162 {'leftCtrl'} 163 {'rightCtrl'} 160 {'leftShift'} 161 {'rightShift'} 88 {'x'} default { $null } }
            if ($taskKeyName) { $taskState[$taskKeyName] = [int]$taskEffectiveEvent.down }
        }
        $taskState.ctrl = [int]($taskState.leftCtrl -or $taskState.rightCtrl); $taskState.shift = [int]($taskState.leftShift -or $taskState.rightShift)
        $taskState.sequence = $taskEffectiveEvent.sequence
        foreach ($taskKey in @('ctrl','shift','leftCtrl','rightCtrl','leftShift','rightShift','x','sequence')) {
            if ($taskEffectiveEvent.$taskKey -ne $taskState[$taskKey]) { $taskOkay = $false }
        }
        $taskLedger[[string]$taskVersion] = $taskState.Clone()
    }
    Check "$taskView.acceptance-state-version-ledger" $taskOkay
    $taskQueries = @($taskInput | Where-Object stage -eq 'virtual_query')
    $taskOkay = $true
    foreach ($taskQuery in $taskQueries) {
        if ($taskQuery.pid -ne $taskInstance.state.pid -or $taskQuery.tid -ne $taskInstance.state.tid) { $taskOkay = $false }
        if ($taskQuery.valid) {
            $taskStateAtRead = $taskLedger[[string]$taskQuery.version]
            if (-not $taskStateAtRead -or $taskQuery.sequence -ne $taskStateAtRead.sequence -or $taskQuery.module -ne $taskInstall[0].image -or
                $taskQuery.caller -lt $taskQuery.module -or $taskQuery.caller -ge ($taskQuery.module+$taskInstall[0].imageSize)) { $taskOkay=$false; continue }
            $taskKeyName = switch ($taskQuery.vk) { 17 {'ctrl'} 16 {'shift'} 162 {'leftCtrl'} 163 {'rightCtrl'} 160 {'leftShift'} 161 {'rightShift'} 88 {'x'} default { $null } }
            $taskExpected = if ($taskKeyName -and $taskStateAtRead[$taskKeyName]) { -32768 } else { 0 }
            if ($taskQuery.result -ne $taskExpected -or [bool]$taskQuery.supported -ne [bool]$taskKeyName) { $taskOkay = $false }
        } elseif ($taskQuery.result -ne 0) { $taskOkay = $false }
    }
    Check "$taskView.actual-API-queries-match-accepted-version" $taskOkay
    if ($taskView -in 'A','B') {
        Check "$taskView.original-EXE-IAT-calls-observed" ($taskQueries.Count -gt 0 -and @($taskQueries | Where-Object { $_.supported -and $_.valid }).Count -gt 0)
        $taskSites = @($taskEvents | Where-Object { $_.type -eq 'callsite' -and $_.view -eq $taskView })
        Check "$taskView.live-callsite-instructions-reference-IAT" ($taskSites.Count -ge 8 -and @($taskSites | Where-Object {
            -not $_.matched -or $_.opcode0 -ne 255 -or $_.opcode1 -ne 21 -or $_.referencedSlot -ne $_.expectedSlot }).Count -eq 0)
    }
    if ($taskView -in 'native','A','B') {
        Check "$taskView.normal-unload-restores-IAT-and-protection" (@($taskInput | Where-Object {
            $_.stage -eq 'iat_restored' -and $_.slotAfter -eq $_.native -and $_.pageBefore -eq $_.pageAfter -and $_.iatInstalled -eq 0 -and $_.pinned -eq 0 }).Count -eq 1)
    }
    if ($taskView -eq 'exit-pending') {
        $taskInvalid = @($taskInput | Where-Object stage -eq 'channel_invalid')[0]
        Check 'exit.primary-error-preserved-and-address-space-gone' ($taskInvalid.error -eq 1067 -and $taskInvalid.primaryApi -eq 'owned_target.exit' -and
            $taskInvalid.targetGone -eq 1 -and $taskInvalid.pinned -eq 0 -and $taskInvalid.iatInstalled -eq 0 -and @($taskInput | Where-Object stage -eq 'channel_closed').Count -eq 0)
    }
}
if ($Scenario -ne 'exit') {
    $taskDown = @($taskEvents | Where-Object { $_.type -eq 'probe' -and $_.name -eq 'accepted-down-before-message-dequeue' })[0]
    Check 'async-effective-before-key-message-consumption' ((High $taskDown.asyncCtrl) -and -not (High $taskDown.queueCtrl) -and
        $taskDown.consumed -eq 0 -and $taskDown.version -eq 1 -and $taskDown.sequence -eq 1 -and (High $taskDown.fixtureAsyncCtrl))
    Check 'native-dynamic-path-not-claimed-virtualized' (-not (High $taskDown.nativeDynamicCtrl))
}
if ($Scenario -eq 'all') {
    $taskA = Lines (Join-Path $taskRun 'A-events.jsonl')
    Check 'original-fixture-observes-different-async-and-queue-times' (@($taskA | Where-Object {
        $_.event -eq 'control_before' -and $_.message -eq 256 -and $_.wParam -eq 17 -and
        (High $_.state.GetKeyStateCtrl) -and -not (High $_.state.GetAsyncKeyStateCtrl)
    }).Count -gt 0)
    $taskRefused = Lines (Join-Path $taskRun 'unload-failure-input.jsonl')
    $taskRefusedLast = @($taskRefused | Where-Object stage -eq 'channel_invalid')[0]
    Check 'refused-unload-keeps-DLL-pinned-and-channel-invalid' ($taskRefusedLast.error -eq 5 -and $taskRefusedLast.iatInstalled -eq 1 -and
        $taskRefusedLast.pinned -eq 1 -and @($taskRefused | Where-Object stage -eq 'channel_closed').Count -eq 0)
    $taskRegression = Join-Path $ResultsDir 'r302-regression'
    & (Join-Path $PSScriptRoot '../r3-002/test.ps1') -OutDir $R302OutDir -ResultsDir $taskRegression
    $taskOldSummary = Get-Content -LiteralPath (Join-Path $taskRegression 'summary.json') -Raw | ConvertFrom-Json
    Check 'full-R3-002-regression' ($taskOldSummary.allEvidenceChecksPassed -and $taskOldSummary.processExit -eq 0)
}
$taskCreated = @($taskEvents | Where-Object type -eq 'process_created'); $taskExited = @($taskEvents | Where-Object type -eq 'process_exited')
Check 'all-owned-processes-confirmed-exited' ($taskCreated.Count -eq $taskExited.Count -and @($taskCreated | Where-Object { $_.pid -notin $taskExited.pid }).Count -eq 0)
$taskDesktops = @($taskEvents | Where-Object type -eq 'desktop_created'); $taskReleased = @($taskEvents | Where-Object type -eq 'desktop_released')
Check 'all-owned-desktops-released' ($taskDesktops.Count -eq $taskReleased.Count -and @($taskReleased | Where-Object closed -ne $true).Count -eq 0)
Check 'no-cleanup-errors' (@($taskEvents | Where-Object type -eq 'cleanup_error').Count -eq 0)
$taskUnchanged = @(& git -C $PSScriptRoot diff 38857dd -- ../r3 ../r3-002 ../common ../r1 ../r2)
Check 'existing-experiment-source-and-baselines-unchanged' ($taskUnchanged.Count -eq 0)
$taskCalls = foreach ($taskSource in @('controller.cpp','input.cpp','runtime.cpp','iat.hpp','observer.cpp')) {
    Select-String -LiteralPath (Join-Path $PSScriptRoot $taskSource) -Pattern '\b(SendInput|SwitchDesktop|SetCursorPos|SetForegroundWindow|AttachThreadInput|SetWindowText[AW]?)\s*\(|\bEM_SETSEL\b'
}
Check 'no-host-input-or-business-state-setters' (@($taskCalls).Count -eq 0)
$taskOkay = @($taskChecks | Where-Object passed -ne $true).Count -eq 0
[ordered]@{scenario=$Scenario;processExit=$taskExit;allEvidenceChecksPassed=$taskOkay;checks=$taskChecks
    assertions=@($taskEvents | Where-Object type -eq 'assertion');capabilities=@($taskEvents | Where-Object type -eq 'capability')
    manualHostTest='not_executed';R3Complete=$false;guiRestore=$false;R1R2Rerun=$false
} | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $ResultsDir 'summary.json') -Encoding UTF8
if (-not $taskOkay) { throw 'R3-003 evidence failed; retain logs and inspect summary.' }
Write-Output 'R3_003_EVIDENCE_PASSED (only the declared owned EXE IAT path and key high bits)'
