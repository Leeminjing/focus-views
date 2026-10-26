param([Parameter(Mandatory)][string]$Manifest,[Parameter(Mandatory)][string]$Destination)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$Manifest=(Resolve-Path -LiteralPath $Manifest).Path;$Destination=[IO.Path]::GetFullPath($Destination)
if(Test-Path -LiteralPath $Destination){throw 'Preserve existing destination'}
$base=Split-Path $Manifest -Parent;$m=Get-Content -Raw -LiteralPath $Manifest|ConvertFrom-Json
if($m.parts.Count -lt 1 -or $m.parts.Count -gt 64 -or $m.sha256 -notmatch '^[0-9a-f]{64}$'){throw 'Invalid multipart manifest'}
$paths=@();$total=[long]0
foreach($part in $m.parts){
 if([IO.Path]::GetFileName($part.file) -cne $part.file -or $part.bytes -le 0 -or $part.bytes -gt 64MB -or $part.sha256 -notmatch '^[0-9a-f]{64}$'){throw 'Invalid archive part'}
 $path=Join-Path $base $part.file
 if((Get-Item -LiteralPath $path).Length -ne $part.bytes -or (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -ne $part.sha256){throw "Part identity mismatch: $($part.file)"}
 $paths+=$path;$total+=[long]$part.bytes
}
if($total -ne $m.bytes){throw 'Archive length mismatch'}
$staging=$Destination+'.joining-'+[guid]::NewGuid().ToString('N')
$output=[IO.File]::Open($staging,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{foreach($path in $paths){$inputStream=[IO.File]::OpenRead($path);try{$inputStream.CopyTo($output,1MB)}finally{$inputStream.Dispose()}};$output.Flush($true)}finally{$output.Dispose()}
if((Get-Item -LiteralPath $staging).Length -ne $m.bytes -or (Get-FileHash -LiteralPath $staging).Hash.ToLowerInvariant() -ne $m.sha256){throw "Archive identity mismatch; retain private staging $staging"}
Move-Item -LiteralPath $staging -Destination $Destination
Write-Output "ARCHIVE_JOIN_VERIFIED $Destination"
