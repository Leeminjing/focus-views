param([Parameter(Mandatory)][string]$ResultsDir)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archives=@()
$groups=@('fixture','legacy','recorder','r5-001b-regression')
if(Test-Path (Join-Path $ResultsDir 'checkpoint-guard')){$groups+=@('checkpoint-guard')}
foreach($group in $groups){
 $source=Join-Path $ResultsDir $group
 if(-not(Test-Path (Join-Path $source 'summary.json'))){throw "Missing completed summary: $group"}
 $destination=Join-Path $ResultsDir ($group+'-raw.zip')
 if(Test-Path $destination){throw 'Do not overwrite evidence archive'}
 $entries=[Collections.Generic.List[object]]::new();$zip=[IO.Compression.ZipFile]::Open($destination,[IO.Compression.ZipArchiveMode]::Create)
 try{
  foreach($file in Get-ChildItem -LiteralPath $source -Recurse -File){
   $name=([IO.Path]::GetRelativePath($ResultsDir,$file.FullName)).Replace('\','/')
   [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$file.FullName,$name,[IO.Compression.CompressionLevel]::Optimal)|Out-Null
   $entries.Add([pscustomobject]@{file=$name;bytes=$file.Length;sha256=(Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant()})
  }
 }finally{$zip.Dispose()}
 $zip=[IO.Compression.ZipFile]::OpenRead($destination);$expected=@{};foreach($item in $entries){$expected[$item.file]=$item}
 try{
  if($zip.Entries.Count -ne $entries.Count){throw 'Archive entry count mismatch'}
  foreach($entry in $zip.Entries){$stream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create();try{$hash=[BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose();$stream.Dispose()};if($hash -ne $expected[$entry.FullName].sha256){throw "Archive mismatch: $($entry.FullName)"}}
 }finally{$zip.Dispose()}
 $manifest=[ordered]@{file=($group+'-raw.zip');sha256=(Get-FileHash $destination).Hash.ToLowerInvariant();bytes=(Get-Item $destination).Length;rawFiles=$entries.Count;rawBytes=($entries|Measure-Object bytes -Sum).Sum;everyEntryVerified=$true;entries=$entries}
 $manifest|ConvertTo-Json -Depth 6|Set-Content (Join-Path $ResultsDir ($group+'-archive-manifest.json')) -Encoding utf8
 $archives+=[ordered]@{file=$manifest.file;sha256=$manifest.sha256;bytes=$manifest.bytes;rawFiles=$manifest.rawFiles;rawBytes=$manifest.rawBytes;everyEntryVerified=$true}
 Copy-Item (Join-Path $source 'summary.json') (Join-Path $ResultsDir ($group+'-summary.json'))
 if(Test-Path (Join-Path $source 'identity.json')){Copy-Item (Join-Path $source 'identity.json') (Join-Path $ResultsDir ($group+'-identity.json'))}
 Write-Output "ARCHIVE_VERIFIED $group files=$($entries.Count) bytes=$($manifest.bytes)"
}
$archives|ConvertTo-Json -Depth 5|Set-Content (Join-Path $ResultsDir 'archives.json') -Encoding utf8
