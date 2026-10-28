param([string]$ResultsDir='D:/fv/b/red1')
$ErrorActionPreference='Stop'
$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(Test-Path -LiteralPath $ResultsDir){throw 'New evidence directory required'}
New-Item -ItemType Directory -Path $ResultsDir|Out-Null
$bin=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../r5-004a/out'))
$copy=Join-Path $ResultsDir 'copied-fixture.exe';Copy-Item -LiteralPath (Join-Path $bin 'fixture.exe') -Destination $copy
$sha=(Get-FileHash (Join-Path $bin 'fixture.exe')).Hash
if((Get-FileHash $copy).Hash -ne $sha){throw 'EXE identity differs'}
$store=Join-Path $ResultsDir 'store'
function Phase($Mode,$App,$Previous){
 $root=Join-Path $ResultsDir $Mode;New-Item -ItemType Directory -Path $root|Out-Null
 & "$bin/controller.exe" --manager "$bin/manager.exe" --client "$bin/fixture-client.dll" --fixture $App --mode $Mode --store $store --manager-output "$root/manager" --output "$root/supervisor" --kill-at '' --previous $Previous --scenario positive *> "$root/console.log"
 if($LASTEXITCODE -ne 0){throw "Supervisor failed: $root/supervisor/summary.json"}
 return Get-Content -Raw "$root/supervisor/summary.json"|ConvertFrom-Json
}
$verdict=[ordered]@{sameExeSha256=$sha;aslrRelocationVerified=$false;baselineRefusalObserved=$false}
try {
 $save=Phase save "$bin/fixture.exe" '';if($save.managerExit -ne 0){throw 'Save failed'}
 $restore=Phase restore $copy "$ResultsDir/save/supervisor/owner.fvc"
 $savedRows=Get-Content "$ResultsDir/save/manager/controller.jsonl"|ConvertFrom-Json
 $rows=Get-Content "$ResultsDir/restore/manager/controller.jsonl"|ConvertFrom-Json
 $target=@($rows|Where-Object {$_.type -eq 'runtime_prepared' -and $_.view -eq 'RA'})
 $failure=@($rows|Where-Object type -eq 'view_restore_rejected')
 $verdict.baselineRefusalObserved=($restore.managerExit -eq 2 -and $failure.Count -eq 1 -and $failure[0].api -eq 'restore.build_or_image_identity' -and $target.Count -eq 1)
 $verdict.restoreManagerExit=$restore.managerExit;$verdict.targetBase=$target[0].image
 $verdict.sourceBase=@($savedRows|Where-Object {$_.type -eq 'runtime_prepared' -and $_.view -eq 'A'})[0].image
 $verdict.differentActualBases=($verdict.sourceBase -ne $verdict.targetBase)
 $verdict.refusal=$failure
 if(-not $verdict.baselineRefusalObserved){throw 'Expected old-kernel image guard was not reached'}
}catch{$verdict.error=$_.ToString();throw}finally{$verdict|ConvertTo-Json -Depth 6|Set-Content "$ResultsDir/summary.json" -Encoding utf8}
