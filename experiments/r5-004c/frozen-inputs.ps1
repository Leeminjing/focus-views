# Experimental verification input inventory. Captured after all builds/profile
# preparation, checked before and after each stage. No files are rewritten here.
function Get-CombinationInputs([string[]]$BuildDirs,[string]$Profile,[string]$PreparedProfile){
 $repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
 $paths=@(foreach($relative in (& git -C $repo ls-files --cached --others --exclude-standard experiments)){
  if($relative -notmatch '/results/' -and [IO.Path]::GetExtension($relative) -in @('.ps1','.cpp','.hpp','.h','.asm','.py')){Join-Path $repo $relative}
 })
 foreach($dir in @($BuildDirs|Select-Object -Unique)){
  if(-not(Test-Path "$dir/build.json")){throw "Missing frozen build $dir"}
  $paths+=@(Get-ChildItem $dir -File|Where-Object{($_.Extension -in @('.exe','.dll','.lib','.obj','.pdb','.json','.map','.asm','.txt') -or $_.Name -eq 'build.log') -and $_.Name -notmatch '^r5004b-'}|ForEach-Object FullName)
 }
 $paths+=@([IO.Path]::GetFullPath($Profile),[IO.Path]::GetFullPath($PreparedProfile))
 @(foreach($path in @($paths|Sort-Object -Unique)){$f=Get-Item -LiteralPath $path;[ordered]@{path=$f.FullName;length=$f.Length;sha256=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()}})
}
function Test-CombinationInputs([object[]]$Inputs){
 $different=@(foreach($f in $Inputs){try{$i=Get-Item -LiteralPath $f.path;if($i.Length -ne $f.length -or (Get-FileHash -LiteralPath $f.path).Hash.ToLowerInvariant() -ne $f.sha256){$f.path}}catch{$f.path}})
 [pscustomobject]@{passed=($different.Count -eq 0);checked=$Inputs.Count;changed=$different}
}
function Test-ProfileImageBinding([string]$Profile,[string]$Image){
 try{
  $info=Get-Item -LiteralPath $Profile;if($info.Length -lt 76 -or $info.Length -gt 1048576){throw 'Profile length'}
  $bytes=[IO.File]::ReadAllBytes([IO.Path]::GetFullPath($Profile));$body=$bytes[0..($bytes.Length-33)]
  $checksum=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([byte[]]$body))
  $trailer=[Convert]::ToHexString([byte[]]$bytes[($bytes.Length-32)..($bytes.Length-1)])
  $embedded=[Convert]::ToHexString([byte[]]$bytes[8..39]);$actual=(Get-FileHash -LiteralPath $Image).Hash
  $valid=[BitConverter]::ToUInt64($bytes,0) -eq [Convert]::ToUInt64('3153455254494157',16) -and $bytes.Length -eq 76+[BitConverter]::ToUInt32($bytes,40) -and $checksum -eq $trailer -and $embedded -eq $actual
  [pscustomobject]@{passed=$valid;profileImageSha256=$embedded;actualImageSha256=$actual;checksumValid=($checksum -eq $trailer)}
 }catch{[pscustomobject]@{passed=$false;error=$_.ToString()}}
}
