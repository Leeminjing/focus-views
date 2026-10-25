param([Parameter(Mandatory)][string]$ResultsDir,[string]$BaselineDir=(Join-Path $PSScriptRoot 'results/20261025-baseline'),[string]$ControlLog=(Join-Path $PSScriptRoot 'results/20261025-controls/forced-refusal.jsonl'))
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$output=Join-Path $ResultsDir analysis.json;if(Test-Path $output){throw 'Preserve existing analysis; use a new output directory for new runs'}
$table=[Collections.Generic.List[object]]::new();$failures=[Collections.Generic.List[string]]::new()
foreach($mode in @('own','fixture','own-debug','fixture-debug')){
 $rows=@(Get-Content (Join-Path $ResultsDir ($mode+'.jsonl'))|ForEach-Object{$_|ConvertFrom-Json})
 $before=@($rows|Where-Object type -eq before_create);$after=@($rows|Where-Object type -eq after_close)
 $created=@($rows|Where-Object type -eq process_created);$exited=@($rows|Where-Object type -eq process_exited);$closed=@($rows|Where-Object type -eq owned_handle_closed)
 $debugThreads=@($rows|Where-Object type -eq debug_handle_auto_closed);$debugProcesses=@($rows|Where-Object type -eq debug_process_auto_closed)
 foreach($entry in @($debugThreads)+@($debugProcesses)){if($entry.stillValid -or $entry.error -ne 6){$failures.Add("$mode debug-event handle ownership")}}
 if($created.Count -ne $exited.Count -or $closed.Count -ne 2*$created.Count -or @($closed|Where-Object{-not $_.closed}).Count){$failures.Add("$mode owned process-info handles")}
 $delta=@(foreach($entry in $after){$b=@($before|Where-Object sequence -eq $entry.sequence)[0];$entry.resources.handleCount-$b.resources.handleCount})
 $table.Add([pscustomobject]@{mode=$mode;created=$created.Count;exited=$exited.Count;ownedHandleCloses=$closed.Count;debugThreadAutoCloses=$debugThreads.Count;debugProcessAutoCloses=$debugProcesses.Count;firstHandleCount=$before[0].resources.handleCount;lastHandleCount=$after[-1].resources.handleCount;maximumHandleDelta=($delta|Measure-Object -Maximum).Maximum;minimumHandleDelta=($delta|Measure-Object -Minimum).Minimum;firstPrivateBytes=$before[0].resources.privateBytes;lastPrivateBytes=$after[-1].resources.privateBytes})
}
$summary=Get-Content (Join-Path $ResultsDir summary.json) -Raw|ConvertFrom-Json
$id=Get-Content (Join-Path $ResultsDir identity.json) -Raw|ConvertFrom-Json
$sourceProblems=@(foreach($entry in $id.sources){if((Get-FileHash (Join-Path $PSScriptRoot $entry.file)).Hash.ToLowerInvariant() -ne $entry.sha256){$entry.file}})
if($sourceProblems.Count){$failures.Add('Current source differs from actually tested identity')}
$baseline=Get-Content (Join-Path $BaselineDir identity.json) -Raw|ConvertFrom-Json
$baselineSummary=Get-Content (Join-Path $BaselineDir summary.json) -Raw|ConvertFrom-Json
$sameFixture=$id.fixtureSha256 -eq $baseline.fixtureSha256;if(-not $sameFixture){$failures.Add('Fixture image changed between matrices')}
$control=@(Get-Content $ControlLog|ForEach-Object{$_|ConvertFrom-Json});$forced=@($control|Where-Object type -eq create_failed)
$validControl=$forced.Count -eq 1 -and $forced[0].sequence -eq 1 -and $forced[0].error -eq 5 -and $forced[0].reportedPid -eq 0 -and @($control|Where-Object type -eq process_created).Count -eq 0 -and @($control|Where-Object{$_.type -eq 'desktop_closed' -and $_.closed}).Count -eq 1
if(-not $validControl){$failures.Add('Explicit invalid-image rejection control')}
$events=@(Get-Content (Join-Path $ResultsDir related-system-events.jsonl)|ForEach-Object{$_|ConvertFrom-Json});$stats=@($events|Where-Object type -eq etw_decode_complete)[0]
[ordered]@{evidenceConsistent=($failures.Count -eq 0);failures=@($failures);functionalRegression='c55efb0 historical passed; implementation and evidence unchanged, not rerun';baselinePlanned=2000;baselineAllCreatesPassed=$baselineSummary.allCreationChecksPassed;instrumentedCases=$table;naturalCreationRefusalReproduced=$summary.creationRefusalReproduced;forcedDirectoryRejectionControlPassed=$validControl;forcedControlIsNaturalRecurrence=$false;fixtureHashUnchanged=$sameFixture;fixtureSha256=$id.fixtureSha256;processProviderAvailable=$summary.processProviderAvailable;etwDecode=$stats;rootCauseConfirmed=$false;issueResolved=$false;gateClosed=$false;systemSettingsChanged=$false}|ConvertTo-Json -Depth 8|Set-Content $output -Encoding utf8
if($failures.Count){throw 'Evidence consistency failed'}
