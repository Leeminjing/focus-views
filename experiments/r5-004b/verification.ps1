# Reuse every existing lifecycle, CPU/CET, Undo, GDI and file oracle.
. (Join-Path $PSScriptRoot '../r5-004/verification.ps1')
function Check-Packet([string]$Path,[string]$Label){
 $b=[IO.File]::ReadAllBytes($Path);$length=[BitConverter]::ToUInt64($b,24)
 $sha=[Security.Cryptography.SHA256]::Create();try{$actual=[Convert]::ToHexString($sha.ComputeHash($b,0,$b.Length-32)).ToLowerInvariant()}finally{$sha.Dispose()}
 $stored=[Convert]::ToHexString($b[($b.Length-32)..($b.Length-1)]).ToLowerInvariant()
 Check "$Label.packet-v2-length-and-SHA256" ($b.Length -eq 64+$length -and [BitConverter]::ToUInt32($b,8) -eq 2 -and [BitConverter]::ToUInt32($b,12) -eq 0x8664 -and $actual -eq $stored)
}
