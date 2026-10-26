param([string]$OutDir=(Join-Path $PSScriptRoot 'out'),[Parameter(Mandatory)][string]$EvidenceDir)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$OutDir=[IO.Path]::GetFullPath($OutDir);$EvidenceDir=[IO.Path]::GetFullPath($EvidenceDir)
if(Test-Path -LiteralPath $EvidenceDir){throw 'New dependency evidence directory required'}
New-Item -ItemType Directory -Path $EvidenceDir|Out-Null
$build=Get-Content -Raw (Join-Path $OutDir 'build.json')|ConvertFrom-Json
if($build.queryTransport -ne 'acknowledged-ring'){throw 'Explicit acknowledged-ring build required'}
$targetRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../r5-002/out'))
if($targetRoot -eq $OutDir){throw 'Distinct input-profile source required'}
$protected=@('manager.exe','fixture.exe','legacy.exe','second.exe','kernel.lib','target.lib')
$before=@{};foreach($file in $protected){$before[$file]=(Get-FileHash (Join-Path $targetRoot $file)).Hash.ToLowerInvariant()}
$changes=@();$applied=[Collections.Generic.List[string]]::new();$backups=@()
# Validate and back up the complete pair and manifest before the first mutation.
foreach($name in @('helper.exe','runtime304.dll')){
 $target=Join-Path $targetRoot $name;$input=Join-Path $OutDir $name
 $expected=@($build.binaries|Where-Object file -eq $name)[0].sha256
 if((Get-FileHash $input).Hash.ToLowerInvariant() -ne $expected){throw 'Built input source identity mismatch'}
 $old=(Get-FileHash $target).Hash.ToLowerInvariant();$backup=Join-Path $EvidenceDir $name;Copy-Item -LiteralPath $target -Destination $backup
 if((Get-FileHash $backup).Hash.ToLowerInvariant() -ne $old){throw 'Dependency backup mismatch'}
 $changes+=[ordered]@{file=$name;before=$old;after=$expected;source=$input;backup=$backup}
 $backups+=[ordered]@{file=$name;sha256=$old;backup=$backup}
}
$reuse=Join-Path $targetRoot 'reuse.json';$reuseHash=(Get-FileHash $reuse).Hash.ToLowerInvariant();Copy-Item -LiteralPath $reuse -Destination (Join-Path $EvidenceDir 'reuse.json')
$backups+=[ordered]@{file='reuse.json';sha256=$reuseHash;backup=(Join-Path $EvidenceDir 'reuse.json')}
try{
 foreach($entry in $changes){$target=Join-Path $targetRoot $entry.file;Copy-Item -LiteralPath $entry.source -Destination $target -Force;$applied.Add($entry.file)
  if((Get-FileHash $target).Hash.ToLowerInvariant() -ne $entry.after){throw 'Input dependency copy differs'}
 }
 $manifest=@(Get-Content -Raw $reuse|ConvertFrom-Json)
 foreach($entry in $manifest){if($entry.file -in @('helper.exe','runtime304.dll')){$entry.source=Join-Path $OutDir $entry.file;$entry.sha256=(Get-FileHash $entry.source).Hash.ToLowerInvariant();$entry.identical=$true}}
 $manifest|ConvertTo-Json -Depth 6|Set-Content -LiteralPath $reuse -Encoding utf8;$applied.Add('reuse.json')
 $unchanged=@();foreach($file in $protected){$after=(Get-FileHash (Join-Path $targetRoot $file)).Hash.ToLowerInvariant();if($after -ne $before[$file]){throw "Recovery/app binary changed: $file"};$unchanged+=[ordered]@{file=$file;sha256=$after;unchanged=$true}}
 [ordered]@{profile='acknowledged-ring';sourceHead=(& git -C $PSScriptRoot rev-parse HEAD);affectedExperiment='R5-002';oldNestedDependenciesModified=$false;controllerAssertionsModified=$false;channelAbiChanged=$false;inputApiExpanded=$false;dependencyChanges=$changes;recoveryAndApplicationBinaries=$unchanged}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $EvidenceDir 'summary.json') -Encoding utf8
}catch{
 $primary=$_.ToString();$rollbackErrors=@()
 foreach($entry in $backups){try{$target=Join-Path $targetRoot $entry.file;if((Get-FileHash $target).Hash.ToLowerInvariant() -ne $entry.sha256){Copy-Item -LiteralPath $entry.backup -Destination $target -Force};if((Get-FileHash $target).Hash.ToLowerInvariant() -ne $entry.sha256){throw 'Rollback bytes differ'}}catch{$rollbackErrors+=$_.ToString()}}
 [ordered]@{profilePublished=$false;primaryError=$primary;appliedBeforeFailure=@($applied);rollbackConfirmed=($rollbackErrors.Count -eq 0);rollbackErrors=$rollbackErrors;backups=$backups}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $EvidenceDir 'failure.json') -Encoding utf8
 throw
}
Write-Output 'REGRESSION_INPUT_PROFILE_PREPARED'