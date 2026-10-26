param([string]$OutDir=(Join-Path $PSScriptRoot 'out'),[Parameter(Mandatory)][string]$ResultsDir)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$OutDir=[IO.Path]::GetFullPath($OutDir);$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
$identity=Get-Content (Join-Path $OutDir 'kernel-identity.json') -Raw|ConvertFrom-Json
foreach($entry in $identity.sourceInputs){if((Get-FileHash (Join-Path $PSScriptRoot $entry.file)).Hash.ToLowerInvariant() -ne $entry.sha256){throw "Source changed since build: $($entry.file)"}}
$files=@('manager.exe','fixture.exe','legacy.exe','second.exe','fixture-client.dll','legacy-client.dll','recorder-client.dll','helper.exe','runtime304.dll','kernel.lib','target.lib','build.json','build.log','kernel-identity.json','reuse.json','link-boundaries.json','manager-loadconfig.txt','fixture-loadconfig.txt','legacy-loadconfig.txt','second-loadconfig.txt','fixture_client.map','legacy_client.map','recorder_client.map','second.map','second.asm')
$dest=Join-Path $ResultsDir 'r5-002-windows-x64.zip'
if(Test-Path $dest){throw 'Do not overwrite a package'}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive=[IO.Compression.ZipFile]::Open($dest,[IO.Compression.ZipArchiveMode]::Create)
try{foreach($name in $files){[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,(Join-Path $OutDir $name),$name,[IO.Compression.CompressionLevel]::Optimal)|Out-Null}}finally{$archive.Dispose()}
$archive=[IO.Compression.ZipFile]::OpenRead($dest)
$entries=@()
try{
 foreach($entry in $archive.Entries){$stream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create();try{$hash=[BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose();$stream.Dispose()};if($hash -ne (Get-FileHash (Join-Path $OutDir $entry.FullName)).Hash.ToLowerInvariant()){throw 'Package bytes mismatch'};$entries+=[ordered]@{file=$entry.FullName;sha256=$hash;bytes=$entry.Length}}
}finally{$archive.Dispose()}
$summary=Get-Content (Join-Path $ResultsDir 'summary.json') -Raw|ConvertFrom-Json
if($summary.kernelSha256 -ne $identity.kernelSha256 -or $summary.managerSha256 -ne (Get-FileHash (Join-Path $OutDir 'manager.exe')).Hash.ToLowerInvariant()){throw 'Tested binary identity mismatch'}
[ordered]@{file='r5-002-windows-x64.zip';sha256=(Get-FileHash $dest).Hash.ToLowerInvariant();bytes=(Get-Item $dest).Length;sourceInputsUnchanged=$true;entries=$entries}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $ResultsDir 'binary-package.json') -Encoding utf8
Write-Output 'PACKAGE_VERIFIED'
