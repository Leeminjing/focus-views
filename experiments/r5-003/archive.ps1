param([Parameter(Mandatory)][string]$Source,[Parameter(Mandatory)][string]$Archive,[string]$OutDir=(Join-Path $PSScriptRoot 'out'),[switch]$VerifyExisting)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
if(-not ('R5003ArchiveHash' -as [type])){Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Security.Cryptography;
using System.Threading.Tasks;
public static class R5003ArchiveHash {
 public static string File(string path) {
  using(var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read,1048576,FileOptions.SequentialScan))
  using(var sha=SHA256.Create()) return Convert.ToHexString(sha.ComputeHash(stream)).ToLowerInvariant();
 }
 public static string[] Files(string[] paths) {
  var hashes=new string[paths.Length];
  Parallel.For(0,paths.Length,new ParallelOptions { MaxDegreeOfParallelism=8 }, i=>hashes[i]=File(paths[i]));
  return hashes;
 }
}
'@}
$archiverSha256=[R5003ArchiveHash]::File($PSCommandPath)
$Source=(Resolve-Path -LiteralPath $Source).Path;$Archive=[IO.Path]::GetFullPath($Archive);$OutDir=[IO.Path]::GetFullPath($OutDir)
if((Test-Path -LiteralPath $Archive) -and -not $VerifyExisting){throw 'Preserve existing archives'}
if($VerifyExisting -and -not(Test-Path -LiteralPath $Archive)){throw 'Missing existing archive'}
if(Test-Path -LiteralPath ($Archive+'.manifest.json')){throw 'Preserve existing verified manifests'}
if($Archive.StartsWith($Source.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Archive must be outside raw source tree'}
$name=Split-Path $Source -Leaf;$parent=Split-Path $Source -Parent
$files=@(Get-ChildItem -LiteralPath $Source -Recurse -File);$hashes=[R5003ArchiveHash]::Files([string[]]@($files.FullName))
$entries=@(for($i=0;$i -lt $files.Count;$i++){[ordered]@{file=([IO.Path]::GetRelativePath($parent,$files[$i].FullName)).Replace('\','/');bytes=$files[$i].Length;sha256=$hashes[$i]}})
Write-Output "SOURCE_HASHED $name files=$($entries.Count)"
if(-not $VerifyExisting){& tar.exe --options 'xz:compression-level=6,xz:threads=4' -cJf $Archive -C $parent $name;if($LASTEXITCODE -ne 0){throw 'Native tar creation failed'}}
$verify=Join-Path $OutDir ('verify-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $verify|Out-Null
& tar.exe -xJf $Archive -C $verify;if($LASTEXITCODE -ne 0){throw "Extraction failed, retain $verify"}
if(@(Get-ChildItem -LiteralPath (Join-Path $verify $name) -Recurse -File).Count -ne $entries.Count){throw 'Archive count differs'}
$restored=[R5003ArchiveHash]::Files([string[]]@($entries|ForEach-Object{[IO.Path]::Combine($verify,$_.file)}))
$totalBytes=[long]0
for($i=0;$i -lt $entries.Count;$i++){if($restored[$i] -ne $entries[$i].sha256){throw "Archive byte mismatch $($entries[$i].file)"};$path=[IO.Path]::Combine($verify,$entries[$i].file);if(([IO.FileInfo]::new($path)).Length -ne $entries[$i].bytes){throw 'Archive length mismatch'};$totalBytes+=[long]$entries[$i].bytes}
$compression=if($VerifyExisting){'Existing native xz stream, independently extracted and rehashed'}else{'xz level 6 / 4 threads'}
[ordered]@{file=(Split-Path $Archive -Leaf);sha256=[R5003ArchiveHash]::File($Archive);bytes=(Get-Item $Archive).Length;rawFiles=$entries.Count;rawBytes=$totalBytes;everyEntryVerified=$true;tool=(& tar.exe --version);compression=$compression;hashReader='SHA256 / FileStream 1MiB SequentialScan / 8 independent readers';archiverSha256=$archiverSha256;entries=$entries}|ConvertTo-Json -Depth 6|Set-Content ($Archive+'.manifest.json') -Encoding utf8
# Preserve the extraction for inspection; no recursive cleanup is needed.
Write-Output "ARCHIVE_VERIFIED $Archive files=$($entries.Count)"
