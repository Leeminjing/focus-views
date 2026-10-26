param([Parameter(Mandatory)][string]$ResultsDir,[string[]]$Groups=@('fixture','legacy','recorder','checkpoint-guard','r5-001b-regression'),[string]$OutDir=(Join-Path $PSScriptRoot 'out'))
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$ResultsDir=[IO.Path]::GetFullPath($ResultsDir);$OutDir=[IO.Path]::GetFullPath($OutDir)
foreach($group in $Groups){
 $source=Join-Path $ResultsDir $group
 if(-not(Test-Path (Join-Path $source 'summary.json'))){throw "Missing completed summary: $group"}
 $destination=Join-Path $ResultsDir ($group+'-raw.tar.xz')
 if(Test-Path $destination){throw 'Do not overwrite an evidence archive'}
 & tar.exe -cJf $destination -C $ResultsDir $group
 if($LASTEXITCODE -ne 0){throw 'Native tar creation failed'}
 $verify=Join-Path $OutDir ('archive-verify-'+[guid]::NewGuid().ToString('N'))
 New-Item -ItemType Directory $verify|Out-Null
 & tar.exe -xJf $destination -C $verify
 if($LASTEXITCODE -ne 0){throw "Extraction failed; preserve $verify"}
 $entries=[Collections.Generic.List[object]]::new()
 $files=@(Get-ChildItem -LiteralPath $source -Recurse -File)
 if(@(Get-ChildItem -LiteralPath (Join-Path $verify $group) -Recurse -File).Count -ne $files.Count){throw 'Archive file count differs'}
 foreach($file in $files){
  $relative=[IO.Path]::GetRelativePath($ResultsDir,$file.FullName)
  $hash=(Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant()
  if($hash -ne (Get-FileHash -LiteralPath (Join-Path $verify $relative)).Hash.ToLowerInvariant()){throw "Archive byte mismatch: $relative"}
  $entries.Add([pscustomobject]@{file=$relative.Replace('\','/');bytes=$file.Length;sha256=$hash})
 }
 [ordered]@{file=($group+'-raw.tar.xz');sha256=(Get-FileHash $destination).Hash.ToLowerInvariant();bytes=(Get-Item $destination).Length;rawFiles=$entries.Count;rawBytes=($entries|Measure-Object bytes -Sum).Sum;everyEntryVerified=$true;format='native Windows tar/xz';tool=(& tar.exe --version);scriptSha256=(Get-FileHash $PSCommandPath).Hash.ToLowerInvariant();entries=$entries}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $ResultsDir ($group+'-archive-manifest.json')) -Encoding utf8
 Copy-Item (Join-Path $source 'summary.json') (Join-Path $ResultsDir ($group+'-summary.json'))
 if(Test-Path (Join-Path $source 'identity.json')){Copy-Item (Join-Path $source 'identity.json') (Join-Path $ResultsDir ($group+'-identity.json'))}
 $resolved=(Resolve-Path -LiteralPath $verify).Path
 if(-not $resolved.StartsWith($OutDir.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Verification cleanup escapes owned OutDir'}
 Remove-Item -LiteralPath $resolved -Recurse -Force
 Write-Output "TAR_ARCHIVE_VERIFIED $group files=$($entries.Count) bytes=$((Get-Item $destination).Length)"
}
