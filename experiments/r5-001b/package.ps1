param([string]$OutDir=(Join-Path $PSScriptRoot 'out'),[Parameter(Mandatory)][string]$ResultsDir)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$OutDir=[IO.Path]::GetFullPath($OutDir);$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
$summary=Get-Content (Join-Path $ResultsDir 'summary.json') -Raw|ConvertFrom-Json
if(-not $summary.allEvidenceChecksPassed){throw 'Cannot package failed evidence as a successful research build.'}
$current=(Get-FileHash (Join-Path $OutDir 'manager.exe')).Hash.ToLowerInvariant()
foreach($application in @('fixture','recorder')){
 $identity=Get-Content (Join-Path $ResultsDir "$application/identity.json") -Raw|ConvertFrom-Json
 if($identity.exeSha256 -ne $current){throw 'Manager differs from tested binary.'}
 foreach($source in $identity.sources){
  if((Get-FileHash (Join-Path $PSScriptRoot $source.file)).Hash.ToLowerInvariant() -ne $source.sha256){throw "Source changed after test: $($source.file)"}
 }
}
$reuse=@(Get-Content (Join-Path $OutDir 'reuse.json') -Raw|ConvertFrom-Json)
$baseline='d5a07306eb545f6aa66e0ea446c5ce8d580e0ab9'
$diff=@(& git -C $PSScriptRoot diff $baseline -- ../r5-001a)
if($LASTEXITCODE -ne 0 -or $diff.Count -ne 0){throw 'R5-001a differs from the experiment baseline; cannot claim unchanged kernel inputs.'}
$oldPackage=Get-Content (Join-Path $PSScriptRoot '../r5-001a/results/20261025-r5001a/binary-package.json') -Raw|ConvertFrom-Json
$comparisons=@(foreach($entry in $reuse){
 $old=@($oldPackage.files|Where-Object file -eq $entry.file)[0]
 $now=(Get-FileHash (Join-Path $OutDir $entry.file)).Hash.ToLowerInvariant()
 if($now -ne $entry.sha256 -or $now -ne (Get-FileHash $entry.source).Hash.ToLowerInvariant()){throw 'Reused input changed.'}
 [ordered]@{file=$entry.file;sha256=$now;matchesHistoricalPackage=($old.sha256 -eq $now)}
})
$kernelSources=@(Get-ChildItem (Join-Path $PSScriptRoot '../r5-001a/runtime') -File|Where-Object Extension -in @('.hpp','.cpp')|ForEach-Object{
 [ordered]@{file=('experiments/r5-001a/runtime/'+$_.Name);sha256=(Get-FileHash $_.FullName).Hash.ToLowerInvariant()}
})
[ordered]@{reused=$comparisons;kernelSources=$kernelSources;baselineCommit=$baseline;kernelSourcesMatchBaseline=$true;kernelModified=$false}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $ResultsDir 'kernel-reuse-audit.json') -Encoding utf8
$files=@('manager.exe','fixture-client.dll','recorder-client.dll','fixture.exe','second.exe','helper.exe','runtime304.dll','kernel.lib','target.lib','build.json','build.log','reuse.json','link-boundaries.json','manager.map','fixture_client.map','recorder_client.map','second.map','second.asm')
$archive=Join-Path $ResultsDir 'r5-001b-windows-x64.zip'
if(Test-Path -LiteralPath $archive){throw 'Preserve existing package.'}
Add-Type -AssemblyName System.IO.Compression
$zip=[IO.Compression.ZipFile]::Open($archive,[IO.Compression.ZipArchiveMode]::Create)
try{foreach($file in $files){[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,(Join-Path $OutDir $file),$file,[IO.Compression.CompressionLevel]::Optimal)|Out-Null}}finally{$zip.Dispose()}
$zip=[IO.Compression.ZipFile]::OpenRead($archive)
try{
 $manifest=@(foreach($entry in $zip.Entries){
  $stream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create()
  try{$hash=[BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}finally{$stream.Dispose();$sha.Dispose()}
  $same=$hash -eq (Get-FileHash (Join-Path $OutDir $entry.FullName)).Hash.ToLowerInvariant()
  if(-not $same){throw 'Archive content mismatch.'}
  [ordered]@{file=$entry.FullName;bytes=$entry.Length;sha256=$hash;matchesBuiltFile=$same}
 })
}finally{$zip.Dispose()}
[ordered]@{archive=[IO.Path]::GetFileName($archive);sha256=(Get-FileHash $archive).Hash.ToLowerInvariant();bytes=(Get-Item $archive).Length;allFilesMatchBuiltOutputs=$true;files=$manifest}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $ResultsDir 'binary-package.json') -Encoding utf8
Write-Output 'PACKAGE_VERIFIED'
