param([string]$ResultsDir=(Join-Path $PSScriptRoot 'results/load-001'))
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $ResultsDir){throw 'Preserve prior observations: output must not exist'}
New-Item -ItemType Directory -Path $ResultsDir|Out-Null
$source=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../r5-003/out/fixture.exe'))
$copy=Join-Path ([IO.Path]::GetFullPath($ResultsDir)) 'fixture.exe'
Copy-Item -LiteralPath $source -Destination $copy
$sha=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$copySha=(Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash
if($sha -ne $copySha){throw 'Copy differs from original EXE'}
& (Join-Path $PSScriptRoot 'out/load-probe.exe') $source $copy 2>&1|Tee-Object -FilePath (Join-Path $ResultsDir 'loader.log')
$exit=$LASTEXITCODE
[ordered]@{source=$source;copy=$copy;sourceSha256=$sha;copySha256=$copySha;identical=$true;exit=$exit;differentBasesVerified=($exit -eq 0);snapshotRestoreVerified=$false;method='CopyFile bytes, normal CreateProcessW loader; no relink, mitigation change or retry';sourceHead=(& git -C $PSScriptRoot rev-parse HEAD)}|ConvertTo-Json|Set-Content (Join-Path $ResultsDir 'summary.json') -Encoding utf8
if($exit -ne 0){throw "Loader observation failed ($exit); no retry"}
