param([Parameter(Mandatory=$true)][string]$Manifest,[Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$Manifest=[IO.Path]::GetFullPath($Manifest);$OutputPath=[IO.Path]::GetFullPath($OutputPath)
if(Test-Path -LiteralPath $OutputPath){throw 'Output already exists; preserve evidence'}
$data=Get-Content -LiteralPath $Manifest -Raw|ConvertFrom-Json
if($data.sha256 -notmatch '^[a-f0-9]{64}$' -or $data.parts.Count -lt 1 -or $data.parts.Count -gt 128){throw 'Invalid transport manifest'}
$directory=Split-Path -Parent $Manifest;$parent=Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Path $parent -Force|Out-Null
$temporary=Join-Path $parent ('.archive-'+[guid]::NewGuid().ToString('N')+'.partial')
try{
 $stream=[IO.File]::Open($temporary,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
 try{foreach($part in $data.parts){
  if($part.file -notmatch '^[A-Za-z0-9._-]+$' -or $part.bytes -lt 1 -or $part.bytes -gt 33554432 -or $part.sha256 -notmatch '^[a-f0-9]{64}$'){throw 'Invalid part descriptor'}
  $source=Join-Path $directory $part.file
  if((Get-Item -LiteralPath $source).Length -ne $part.bytes -or (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $part.sha256){throw "Part integrity failure: $($part.file)"}
  $inputStream=[IO.File]::OpenRead($source);try{$inputStream.CopyTo($stream)}finally{$inputStream.Dispose()}
 };$stream.Flush($true)}finally{$stream.Dispose()}
 if((Get-Item -LiteralPath $temporary).Length -ne $data.bytes -or (Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash.ToLowerInvariant() -ne $data.sha256){throw 'Archive integrity failure'}
 [IO.File]::Move($temporary,$OutputPath)
 [ordered]@{file=$OutputPath;bytes=$data.bytes;sha256=$data.sha256;verified=$true}|ConvertTo-Json
}finally{if(Test-Path -LiteralPath $temporary){[IO.File]::Delete($temporary)}}
