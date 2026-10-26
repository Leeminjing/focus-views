param([Parameter(Mandatory)][string]$Source,[Parameter(Mandatory)][string]$Archive,[string]$OutDir=(Join-Path $PSScriptRoot 'out'))
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$Source=(Resolve-Path -LiteralPath $Source).Path;$Archive=[IO.Path]::GetFullPath($Archive);$OutDir=[IO.Path]::GetFullPath($OutDir)
if(Test-Path -LiteralPath $Archive){throw 'Preserve existing archives'}
if($Archive.StartsWith($Source.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Archive must be outside raw source tree'}
$name=Split-Path $Source -Leaf;$parent=Split-Path $Source -Parent
$files=@(Get-ChildItem -LiteralPath $Source -Recurse -File);$entries=@($files|ForEach-Object{[ordered]@{file=([IO.Path]::GetRelativePath($parent,$_.FullName)).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()}})
& tar.exe -cJf $Archive -C $parent $name;if($LASTEXITCODE -ne 0){throw 'Native tar creation failed'}
$verify=Join-Path $OutDir ('verify-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $verify|Out-Null
& tar.exe -xJf $Archive -C $verify;if($LASTEXITCODE -ne 0){throw "Extraction failed, retain $verify"}
if(@(Get-ChildItem -LiteralPath (Join-Path $verify $name) -Recurse -File).Count -ne $entries.Count){throw 'Archive count differs'}
foreach($entry in $entries){if((Get-FileHash -LiteralPath (Join-Path $verify $entry.file)).Hash.ToLowerInvariant() -ne $entry.sha256){throw "Archive byte mismatch $($entry.file)"}}
[ordered]@{file=(Split-Path $Archive -Leaf);sha256=(Get-FileHash $Archive).Hash.ToLowerInvariant();bytes=(Get-Item $Archive).Length;rawFiles=$entries.Count;rawBytes=($entries|Measure-Object bytes -Sum).Sum;everyEntryVerified=$true;tool=(& tar.exe --version);entries=$entries}|ConvertTo-Json -Depth 6|Set-Content ($Archive+'.manifest.json') -Encoding utf8
# Preserve the extraction for inspection; no recursive cleanup is needed.
Write-Output "ARCHIVE_VERIFIED $Archive files=$($entries.Count)"
