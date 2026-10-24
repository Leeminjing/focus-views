param(
    [string]$OutRoot=(Join-Path $PSScriptRoot 'out'),
    [string]$ResultsDir=(Join-Path $PSScriptRoot ('results/'+(Get-Date -Format 'yyyyMMdd-HHmmss'))),
    [ValidateRange(1,10)][int]$Repeats=2
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(Test-Path -LiteralPath $ResultsDir){throw 'A new ResultsDir is required; preserve previous evidence.'}
New-Item -ItemType Directory -Path $ResultsDir | Out-Null
Add-Type -TypeDefinition 'public static class Cet001Receipt { public static ulong Compute(ulong r,ulong l,ulong m,ulong o) { unchecked { ulong v=(r^l)+m; return ((v<<13)|(v>>51))^o; } } }'
$checks=[Collections.Generic.List[object]]::new()
$cases=[Collections.Generic.List[object]]::new()
function Check([string]$Name,[bool]$Okay) { $checks.Add([pscustomobject]@{name=$Name;passed=$Okay});Write-Output "$Name=$Okay" }
function Lines([string]$Path) { @(Get-Content -LiteralPath $Path -Encoding utf8 | Where-Object {$_} | ForEach-Object { $_ | ConvertFrom-Json }) }
function U64([byte[]]$Bytes,[long]$Offset) { [BitConverter]::ToUInt64($Bytes,[int]$Offset) }
$inputs=@('controller.cpp','fixture.cpp','protocol.hpp','traps.asm','build.ps1','test.ps1','../r3-005a/controller.cpp','../r3-005a/fixture.cpp','../r3-005a/traps.asm','../r3-005a/test.ps1','../r3-004/runtime.cpp','../r3-004/channel.hpp','../r3-004/iat.hpp','../r3-004/input.cpp','../r3-004/observer.cpp','../r3/bridge.cpp','../r3/protocol.hpp','../common/native.hpp','../r1/snapshot.hpp')
$sourceIdentity=@($inputs | ForEach-Object { [ordered]@{file=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $_)).Hash.ToLowerInvariant()} })
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
[ordered]@{time=(Get-Date -Format o);baseCommit=(& git -C $PSScriptRoot rev-parse HEAD);os=[Environment]::OSVersion.VersionString;architecture=$env:PROCESSOR_ARCHITECTURE;elevated=$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator);sources=$sourceIdentity;systemSettingsChanged=$false;CETDisabled=$false;GSDisabled=$false;driver=$false;manualHostTest='not_executed'} | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $ResultsDir 'identity.json') -Encoding utf8
# Separate shell: old harness exits 2 intentionally and remains byte-for-byte unchanged.
$shell=(Get-Process -Id $PID).Path
& $shell -NoProfile -File (Join-Path $PSScriptRoot '../r3-005a/test.ps1') -Scenario ret-probe -ResultsDir (Join-Path $ResultsDir 'original-ret-probe') 2>&1 | Tee-Object -FilePath (Join-Path $ResultsDir 'original-ret-probe-console.log')
$baselineExit=$LASTEXITCODE
$baseline=Get-Content (Join-Path $ResultsDir 'original-ret-probe/summary.json') -Raw | ConvertFrom-Json
Check 'original-red-capable-loop-retains-actual-failure' ($baselineExit -eq 2 -and $baseline.processExit -eq 2 -and $baseline.evidenceChecksPassed -and -not $baseline.experimentPassed)

foreach($variant in @('legacy','control-md','ehcont-md')) {
    $bin=Join-Path $OutRoot $variant
    $variantDir=Join-Path $ResultsDir $variant
    New-Item -ItemType Directory -Path $variantDir | Out-Null
    foreach($file in @('build.json','loadconfig.txt')) { Copy-Item -LiteralPath (Join-Path $bin $file) -Destination (Join-Path $variantDir $file) }
    Copy-Item -LiteralPath (Join-Path $OutRoot ($variant+'-build.log')) -Destination (Join-Path $variantDir 'build.log')
    Select-String -LiteralPath (Join-Path $bin 'r305a.map') -Pattern 'CheckpointSite|ApiSite|DiagnosticSite|ContinuationTrap|diagnostic_(leaf|middle|outer|seh_leaf)|__security_check_cookie' | ForEach-Object Line | Set-Content (Join-Path $variantDir 'symbols.txt') -Encoding utf8
    $exe=Join-Path $bin 'r305a.exe'
    $build=Get-Content (Join-Path $bin 'build.json') -Raw | ConvertFrom-Json
    $eh=$variant -eq 'ehcont-md'
    [ordered]@{exeSha256=(Get-FileHash $exe).Hash.ToLowerInvariant();runtimeSha256=(Get-FileHash (Join-Path $bin 'runtime304.dll')).Hash.ToLowerInvariant();build=$build} | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $variantDir 'binary-identity.json') -Encoding utf8
    $load=Get-Content (Join-Path $bin 'loadconfig.txt') -Raw
    Check "$variant.EHCONT-metadata-observed" (($build.EHCONT -eq $eh) -and (($load -match 'EH Continuation table present') -eq $eh))
    $imageBytes=[IO.File]::ReadAllBytes($exe)
    $pe=[BitConverter]::ToInt32($imageBytes,60)
    $preferredBase=U64 $imageBytes ($pe+48)
    $ehTargets=@()
    if($eh) {
        $table=([regex]::Split($load,'Guard EH Continuation Table'))[1]
        $table=([regex]::Split($table,'Volatile Metadata'))[0]
        $ehTargets=@([regex]::Matches($table,'(?m)^\s+([0-9A-Fa-f]{16})\s*$') | ForEach-Object { [Convert]::ToUInt64($_.Groups[1].Value,16) })
        Check "$variant.actual-EHCONT-table-not-empty" ($ehTargets.Count -gt 0)
    }
    for($repeat=1;$repeat -le $Repeats;++$repeat) {
        foreach($method in @('native-chain','native-seh','rtl-local','legacy','xstate','rtl','rtl-native')) {
            $label="$variant/$method-$repeat"
            $dir=Join-Path $variantDir "$method-$repeat"
            & $exe --output $dir --scenario $method 2>&1 | Tee-Object -FilePath (Join-Path $variantDir "$method-$repeat-console.log")
            $exit=$LASTEXITCODE
            $rows=Lines (Join-Path $dir 'controller.jsonl')
            $native=$method -in @('native-chain','native-seh','rtl-local')
            $fault=@($rows | Where-Object type -eq 'continuation_fault')
            $policy=@($rows | Where-Object type -eq 'runtime_prepared')[0].shadowPolicy
            Check "$label.CET-and-IP-validation-enabled" (($policy -band 5) -eq 5)
            $read=@($rows | Where-Object type -eq 'xstate_read')
            Check "$label.actual-CET_U-readable-and-enabled" ($read.Count -ge 2 -and @($read | Where-Object { -not $_.cetPresent -or $_.cetControl -ne 1 -or -not $_.shadowReturnReadable }).Count -eq 0)
            $assertions=@($rows | Where-Object type -eq 'assertion')
            Check "$label.instrumentation-assertions" (@($assertions | Where-Object {-not $_.passed}).Count -eq 0)
            $restored=$false
            if($native) {
                $result=@($rows | Where-Object type -eq 'native_result')
                $expect=$method -ne 'rtl-local' -or -not $eh
                Check "$label.native-control-verdict" (($expect -and $exit -eq 0 -and $result.Count -eq 1 -and $result[0].returns -eq 3 -and $result[0].result -eq $result[0].expected) -or (-not $expect -and $exit -eq 2 -and $fault.Count -eq 1 -and $fault[0].parameters[0] -eq 48))
                if($method -eq 'rtl-local' -and $eh) {
                    $captured=@((Lines (Join-Path $dir 'rtl-local-events.jsonl')) | Where-Object event -eq 'local_capture')[0]
                    $loaded=@($rows | Where-Object type -eq 'runtime_prepared')[0].image
                    Check "$label.captured-IP-not-an-EHCONT-entry" ($ehTargets -notcontains ($captured.rip-$loaded+$preferredBase))
                }
                if($expect) {
                    $before=@($read | Where-Object phase -eq 'native-ready')[0]
                    $after=@($read | Where-Object phase -eq 'native-after-return')[0]
                    Check "$label.native-SSP-balances-after-returns" ($before.ssp -eq $after.ssp)
                    if($method -eq 'rtl-local') { Check "$label.RtlRestoreContext-actually-called-once" ($result[0].restoreCalls -eq 1 -and $result[0].phase -eq 1) }
                    if($method -eq 'native-seh') { Check "$label.real-SEH-delivered-to-application" ($result[0].phase -eq 1 -and @($rows | Where-Object { $_.type -eq 'exception' -and $_.code -eq 3808804865 }).Count -eq 1) }
                }
            } else {
                $snap=@($rows | Where-Object type -eq 'snapshot')[0]
                $layout=@($rows | Where-Object type -eq 'raw_layout')[0]
                if($eh) {
                    $loaded=@($rows | Where-Object type -eq 'runtime_prepared')[0].image
                    Check "$label.saved-IP-not-an-EHCONT-entry" ($ehTargets -notcontains ($snap.rip-$loaded+$preferredBase))
                }
                $sourceDead=@($rows | Where-Object { $_.type -eq 'process_exited' -and $_.name -eq 'source' })[0]
                $target=@($rows | Where-Object { $_.type -eq 'process_created' -and $_.name -eq 'restored' })[0]
                Check "$label.source-dead-before-new-PID" ($sourceDead.pid -ne $target.pid -and [array]::IndexOf($rows,$sourceDead) -lt [array]::IndexOf($rows,$target))
                $targetHalts=@($rows | Where-Object { $_.type -eq 'halt' -and $_.view -eq 'restored' })
                Check "$label.no-business-init-or-checkpoint-replay" ($targetHalts.Count -gt 0 -and @($targetHalts | Where-Object { $_.initializations -ne 0 -or $_.checkpoints -ne 0 }).Count -eq 0)
                $stack=[IO.File]::ReadAllBytes((Join-Path $dir 'S0-active-stack.bin'))
                $targetStack=[IO.File]::ReadAllBytes((Join-Path $dir 'target-active-stack-before-continue.bin'))
                $data=[IO.File]::ReadAllBytes((Join-Path $dir 'S0-data.bin'))
                $targetData=[IO.File]::ReadAllBytes((Join-Path $dir 'target-data-before-continue.bin'))
                $cpu=[IO.File]::ReadAllBytes((Join-Path $dir 'S0-context.bin'))
                $reg=U64 $cpu $layout.r10Offset
                $locals=@($snap.localAddresses | ForEach-Object { U64 $stack ($_-$snap.rsp) })
                $expected=[Cet001Receipt]::Compute($reg,$locals[0],$locals[1],$locals[2])
                $changed=@($rows | Where-Object type -eq 'source_changed')[0].work
                Check "$label.source-return-depends-on-raw-stack-and-register" ($expected -eq $snap.expectedReceipt -and $changed.receipt -eq $expected -and $changed.returned -eq 7 -and $changed.text -ne $snap.work.text -and $changed.counter -gt $snap.work.counter)
                $refs=@($rows | Where-Object { $_.type -eq 'resource_rebind' -and $_.view -eq 'restored' })
                foreach($ref in $refs) {
                    $isData=$ref.address -ge $layout.dataAddress -and $ref.address -lt $layout.dataAddress+$data.Length
                    if($isData){$array=$data}else{$array=$stack}
                    $base=if($isData){$layout.dataAddress}else{$snap.rsp}
                    $offset=[int]($ref.address-$base)
                    Check "$label.reference-$($ref.address)-preimage" ((U64 $array $offset) -eq $ref.oldHwnd -and $ref.oldHwnd -ne $ref.newHwnd)
                    [Array]::Copy([BitConverter]::GetBytes([ulong]$ref.newHwnd),0,$array,$offset,8)
                }
                Check "$label.only-declared-HWND-bytes-rebound" ($refs.Count -eq 3 -and [Convert]::ToBase64String($stack) -eq [Convert]::ToBase64String($targetStack) -and [Convert]::ToBase64String($data) -eq [Convert]::ToBase64String($targetData))
                $complete=@($rows | Where-Object type -eq 'experiment_complete')
                $restored=$complete.Count -eq 1 -and $complete[0].primaryRestorePassed
                if(-not $restored) {
                    Check "$label.failure-not-published-or-masked" ($exit -eq 2 -and @($rows | Where-Object type -eq 'view_published').Count -eq 0 -and @($rows | Where-Object type -eq 'primary_restore_failed')[0].published -eq 0)
                    $subcode=if($method -eq 'rtl-native'){13}elseif($method -eq 'rtl' -and $eh){48}else{57}
                    Check "$label.actual-protected-continuation-fault" ($fault.Count -eq 1 -and $fault[0].exceptionCode -eq 3221226505 -and $fault[0].parameters[0] -eq $subcode)
                    if($subcode -eq 57) {
                        $last=@($read | Where-Object phase -eq 'after-continuation-fault')[0]
                        Check "$label.first-RET-and-shadow-mismatch" ($fault[0].rip -eq $snap.rip+3 -and $fault[0].rsp -eq $snap.rsp -and $fault[0].instructionHex.StartsWith('c3') -and $last.shadowReturn -ne $fault[0].normalReturn -and $last.ssp -eq $fault[0].parameters[1])
                    }
                } else { Check "$label.actual-return-and-GUI-validation" ($exit -eq 0 -and @($assertions | Where-Object name -eq 'restored.real-GUI-input').Count -eq 1) }
                if($method -eq 'xstate') {
                    $sets=@($rows | Where-Object type -eq 'xstate_set')
                    $after=@($read | Where-Object phase -eq 'after-target-owned-no-op')[0]
                    Check "$label.CET_U-no-op-accepted-without-SSP-transplant" ($sets.Count -eq 2 -and @($sets | Where-Object {-not $_.success}).Count -eq 0 -and $sets[0].ssp -eq $sets[1].ssp -and $sets[0].ssp -eq $after.ssp)
                }
                if($method -in @('rtl','rtl-native')) {
                    $call=@($rows | Where-Object type -eq 'rtl_call_site')
                    Check "$label.RtlRestoreContext-runtime-call-reached" (@($rows | Where-Object type -eq 'rtl_request').Count -eq 1 -and $call.Count -eq 1)
                    Check "$label.transfer-stack-boundary-explicit" (($snap.rsp -ge $call[0].tibStackLow -and $snap.rsp -lt $call[0].tibStackHigh) -eq ($method -eq 'rtl'))
                }
            }
            $created=@($rows | Where-Object type -eq 'process_created')
            $exited=@($rows | Where-Object type -eq 'process_exited')
            Check "$label.all-owned-processes-confirmed-dead" ($created.Count -eq $exited.Count -and @($created | Where-Object {$proc=$_;@($exited | Where-Object pid -eq $proc.pid).Count -ne 1}).Count -eq 0)
            $opened=@($rows | Where-Object type -eq 'desktop_created')
            $closed=@($rows | Where-Object type -eq 'desktop_closed')
            Check "$label.owned-desktops-closed-without-cleanup-errors" ($opened.Count -eq $closed.Count -and @($closed | Where-Object {-not $_.closed}).Count -eq 0 -and @($rows | Where-Object type -eq 'cleanup_error').Count -eq 0)
            $cases.Add([pscustomobject]@{variant=$variant;method=$method;repeat=$repeat;processExit=$exit;diagnosticOnly=$native;sourceExecutionRestored=$restored;faults=$fault})
        }
    }
}
if(Test-Path -LiteralPath (Join-Path $OutRoot 'ehcont-static-build.log')) { Copy-Item -LiteralPath (Join-Path $OutRoot 'ehcont-static-build.log') -Destination (Join-Path $ResultsDir 'ehcont-static-build-failure.log') }
$old=@(& git -C $PSScriptRoot diff d96b2e3 -- '../r1' '../r2' '../r3' '../r3-002' '../r3-003' '../r3-004' '../r3-005a' '../common')
Check 'old-experiments-and-original-evidence-unchanged' ($old.Count -eq 0)
$all=$cases.Count -eq 21*$Repeats -and @($checks | Where-Object {-not $_.passed}).Count -eq 0
$restores=@($cases | Where-Object {-not $_.diagnosticOnly})
$passed=$restores.Count -gt 0 -and @($restores | Where-Object {-not $_.sourceExecutionRestored}).Count -eq 0
[ordered]@{diagnosticEvidencePassed=$all;sourceExecutionRestorePassed=$passed;R3005aPassed=$passed;checks=$checks;cases=$cases;R1R2Rerun=$false;R3001To004Rerun=$false;manualHostTest='not_executed';originalR3005aRetProbeRerun=$true;systemSettingsChanged=$false} | ConvertTo-Json -Depth 9 | Set-Content (Join-Path $ResultsDir 'summary.json') -Encoding utf8
if(-not $all){throw 'CET001 evidence checks failed; source restore has not passed.'}
if(-not $passed){Write-Output 'CET001_SOURCE_RESTORE_FAILED; native controls and failure evidence are separately recorded';exit 2}
