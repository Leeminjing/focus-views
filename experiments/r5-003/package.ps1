param([string]$OutDir=(Join-Path $PSScriptRoot 'out'),[Parameter(Mandatory)][string]$ResultsDir)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$OutDir=[IO.Path]::GetFullPath($OutDir);$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
$build=Get-Content -Raw (Join-Path $OutDir 'build.json')|ConvertFrom-Json
foreach($entry in $build.sourceInputs){if((Get-FileHash (Join-Path $PSScriptRoot $entry.file)).Hash.ToLowerInvariant() -ne $entry.sha256){throw "Compiled source changed: $($entry.file)"}}
foreach($entry in $build.binaries){if((Get-FileHash (Join-Path $OutDir $entry.file)).Hash.ToLowerInvariant() -ne $entry.sha256){throw "Compiled binary changed: $($entry.file)"}}
$summary=Get-Content -Raw (Join-Path $ResultsDir 'summary.json')|ConvertFrom-Json
if($summary.managerSha256 -ne (Get-FileHash (Join-Path $OutDir 'manager.exe')).Hash.ToLowerInvariant() -or $summary.kernelSha256 -ne (Get-FileHash (Join-Path $OutDir 'kernel.lib')).Hash.ToLowerInvariant()){throw 'Tested/build identity mismatch'}
$files=@('manager.exe','fixture.exe','second.exe','fixture-client.dll','recorder-client.dll','helper.exe','runtime304.dll','codec_test.exe','kernel.lib','snapshot-store.lib','build.json','reuse.json')
$destination=Join-Path $ResultsDir 'r5-003-windows-x64.zip';if(Test-Path $destination){throw 'Do not overwrite native run package'}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::Open($destination,[IO.Compression.ZipArchiveMode]::Create)
try{foreach($file in $files){[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,(Join-Path $OutDir $file),$file,[IO.Compression.CompressionLevel]::Optimal)|Out-Null}}finally{$zip.Dispose()}
$zip=[IO.Compression.ZipFile]::OpenRead($destination);$entries=@()
try{foreach($entry in $zip.Entries){$stream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create();try{$hash=[Convert]::ToHexString($sha.ComputeHash($stream)).ToLowerInvariant()}finally{$stream.Dispose();$sha.Dispose()};if($hash -ne (Get-FileHash (Join-Path $OutDir $entry.FullName)).Hash.ToLowerInvariant()){throw 'Package entry differs'};$entries+=[ordered]@{file=$entry.FullName;bytes=$entry.Length;sha256=$hash}}}finally{$zip.Dispose()}
[ordered]@{file='r5-003-windows-x64.zip';sha256=(Get-FileHash $destination).Hash.ToLowerInvariant();bytes=(Get-Item $destination).Length;sourceAndBinaryIdentityVerified=$true;entries=$entries}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $ResultsDir 'binary-package.json') -Encoding utf8
Write-Output 'R5_003_PACKAGE_VERIFIED'
