param([string]$OutDir=(Join-Path $PSScriptRoot 'out'),[Parameter(Mandatory)][string]$ResultsDir,[string]$OriginalBackup)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
if(Test-Path -LiteralPath $ResultsDir){throw 'New profile-test ResultsDir required'}
New-Item -ItemType Directory -Path $ResultsDir|Out-Null
$target=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../r5-002/out'));$names=@('helper.exe','runtime304.dll','reuse.json')
if($OriginalBackup){foreach($name in $names){Copy-Item -LiteralPath (Join-Path $OriginalBackup $name) -Destination (Join-Path $target $name) -Force}}
$before=@{};foreach($name in $names){$before[$name]=(Get-FileHash (Join-Path $target $name)).Hash.ToLowerInvariant()}
$lock=[IO.File]::Open((Join-Path $target 'runtime304.dll'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
$rejected=$false
try{
 try{& (Join-Path $PSScriptRoot 'prepare-regression-input.ps1') -OutDir $OutDir -EvidenceDir (Join-Path $ResultsDir 'locked-dll');throw 'Expected native sharing violation missing'}
 catch{$rejected=Test-Path (Join-Path $ResultsDir 'locked-dll/failure.json');$primary=$_.ToString()}
}finally{$lock.Dispose()}
if(-not $rejected){throw 'Dependency transaction did not preserve its real failure'}
$failure=Get-Content -Raw (Join-Path $ResultsDir 'locked-dll/failure.json')|ConvertFrom-Json
$same=$true;foreach($name in $names){if((Get-FileHash (Join-Path $target $name)).Hash.ToLowerInvariant() -ne $before[$name]){$same=$false}}
$passed=$failure.rollbackConfirmed -and $same -and @($failure.appliedBeforeFailure).Count -eq 1 -and $failure.appliedBeforeFailure[0] -eq 'helper.exe' -and -not(Test-Path (Join-Path $ResultsDir 'locked-dll/summary.json'))
[ordered]@{passed=$passed;realDllReadLock=$true;firstCopyCompleted=$failure.appliedBeforeFailure;rollbackByteIdentical=$same;primaryError=$failure.primaryError;profilePublished=$false}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $ResultsDir 'summary.json') -Encoding utf8
if(-not $passed){throw 'Dependency rollback test failed'}
& (Join-Path $PSScriptRoot 'prepare-regression-input.ps1') -OutDir $OutDir -EvidenceDir (Join-Path $ResultsDir 'normal-install')
Write-Output 'INPUT_PROFILE_TRANSACTION_PASSED'
