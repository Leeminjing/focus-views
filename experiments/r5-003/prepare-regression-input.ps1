param([string]$OutDir=(Join-Path $PSScriptRoot 'out'),[Parameter(Mandatory)][string]$EvidenceDir,[ValidateSet('r5-001a','r5-001b','r5-002')][string[]]$Experiments=@('r5-001a','r5-001b','r5-002'))
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$OutDir=[IO.Path]::GetFullPath($OutDir);$EvidenceDir=[IO.Path]::GetFullPath($EvidenceDir)
if(Test-Path -LiteralPath $EvidenceDir){throw 'New dependency evidence directory required'}
New-Item -ItemType Directory -Path $EvidenceDir|Out-Null
$build=Get-Content -Raw (Join-Path $OutDir 'build.json')|ConvertFrom-Json
if($build.queryTransport -ne 'acknowledged-ring'){throw 'Explicit acknowledged-ring build required'}
$inputs=@();foreach($name in @('helper.exe','runtime304.dll')){$input=Join-Path $OutDir $name;$expected=@($build.binaries|Where-Object file -eq $name)[0].sha256;if((Get-FileHash $input).Hash.ToLowerInvariant() -ne $expected){throw 'Built input source identity mismatch'};$inputs+=[ordered]@{file=$name;source=$input;sha256=$expected}}
$changes=@();$backups=@();$protected=@();$targets=@();$applied=[Collections.Generic.List[string]]::new()
# Validate and back up every selected target before any mutation.
foreach($experiment in @($Experiments|Select-Object -Unique)){
 $root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../$experiment/out"));if($root -eq $OutDir){throw 'Distinct input-profile source required'}
 $backup=Join-Path $EvidenceDir $experiment;New-Item -ItemType Directory -Path $backup|Out-Null
 foreach($name in @('manager.exe','fixture.exe','legacy.exe','second.exe','kernel.lib','target.lib')){$path=Join-Path $root $name;if(Test-Path $path){$protected+=[ordered]@{experiment=$experiment;file=$name;path=$path;sha256=(Get-FileHash $path).Hash.ToLowerInvariant()}}}
 foreach($input in $inputs){$path=Join-Path $root $input.file;$old=(Get-FileHash $path).Hash.ToLowerInvariant();$copy=Join-Path $backup $input.file;Copy-Item -LiteralPath $path -Destination $copy;if((Get-FileHash $copy).Hash.ToLowerInvariant() -ne $old){throw 'Dependency backup mismatch'}
  $changes+=[ordered]@{experiment=$experiment;file=$input.file;path=$path;before=$old;after=$input.sha256;source=$input.source;backup=$copy};$backups+=[ordered]@{path=$path;sha256=$old;backup=$copy}
 }
 $reuse=Join-Path $root 'reuse.json';if(Test-Path $reuse){$hash=(Get-FileHash $reuse).Hash.ToLowerInvariant();$copy=Join-Path $backup 'reuse.json';Copy-Item -LiteralPath $reuse -Destination $copy;$backups+=[ordered]@{path=$reuse;sha256=$hash;backup=$copy};$targets+=[ordered]@{experiment=$experiment;reuse=$reuse}}
}
try{
 foreach($entry in $changes){Copy-Item -LiteralPath $entry.source -Destination $entry.path -Force;$applied.Add($entry.experiment+'/'+$entry.file);if((Get-FileHash $entry.path).Hash.ToLowerInvariant() -ne $entry.after){throw 'Input dependency copy differs'}}
 foreach($target in $targets){$manifest=@(Get-Content -Raw $target.reuse|ConvertFrom-Json);foreach($entry in $manifest){if($entry.file -in @('helper.exe','runtime304.dll')){$entry.source=Join-Path $OutDir $entry.file;$entry.sha256=(Get-FileHash $entry.source).Hash.ToLowerInvariant();$entry.identical=$true}};$manifest|ConvertTo-Json -Depth 6|Set-Content -LiteralPath $target.reuse -Encoding utf8;$applied.Add($target.experiment+'/reuse.json')}
 foreach($entry in $protected){if((Get-FileHash $entry.path).Hash.ToLowerInvariant() -ne $entry.sha256){throw "Recovery/app binary changed: $($entry.path)"}}
 [ordered]@{profile='acknowledged-ring';sourceHead=(& git -C $PSScriptRoot rev-parse HEAD);affectedExperiments=$Experiments;legacyHybridDependenciesModified=$false;controllerAssertionsModified=$false;channelAbiChanged=$false;inputApiExpanded=$false;dependencyChanges=$changes;recoveryAndApplicationBinaries=$protected}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $EvidenceDir 'summary.json') -Encoding utf8
}catch{
 $primary=$_.ToString();$rollbackErrors=@();foreach($entry in $backups){try{if((Get-FileHash $entry.path).Hash.ToLowerInvariant() -ne $entry.sha256){Copy-Item -LiteralPath $entry.backup -Destination $entry.path -Force};if((Get-FileHash $entry.path).Hash.ToLowerInvariant() -ne $entry.sha256){throw 'Rollback bytes differ'}}catch{$rollbackErrors+=$_.ToString()}}
 [ordered]@{profilePublished=$false;primaryError=$primary;appliedBeforeFailure=@($applied);rollbackConfirmed=($rollbackErrors.Count -eq 0);rollbackErrors=$rollbackErrors;backups=$backups}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $EvidenceDir 'failure.json') -Encoding utf8
 throw
}
Write-Output 'REGRESSION_INPUT_PROFILE_PREPARED'