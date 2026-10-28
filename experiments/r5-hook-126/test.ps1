param([Parameter(Mandatory)][string]$OutDir,[Parameter(Mandatory)][string]$ResultsDir,
 [string[]]$Dll=@((Join-Path $PSScriptRoot '../r3-005b-001/out/runtime304.dll'),(Join-Path $PSScriptRoot '../r3-005a-cet-002/out/runtime304.dll')))
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$OutDir=[IO.Path]::GetFullPath($OutDir);$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(Test-Path -LiteralPath $ResultsDir){throw 'New evidence directory required; no retry or overwrite'}
New-Item -ItemType Directory -Path $ResultsDir|Out-Null
$boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime
$exe=Join-Path $OutDir 'hook-probe.exe';$exeHash=(Get-FileHash $exe).Hash;$scriptHash=(Get-FileHash $PSCommandPath).Hash;$cases=@()
foreach($path in $Dll){
 $index=$cases.Count+1;$root=Join-Path $ResultsDir "case-$index";$exit=-1;$errorText='';$before=''
 try{$path=[IO.Path]::GetFullPath($path);$before=(Get-FileHash -LiteralPath $path).Hash
  & $exe --role supervisor --dll $path --output $root *> "$ResultsDir/case-$index-console.log";$exit=$LASTEXITCODE
 }catch{$errorText=$_.ToString()}
 $summary=if(Test-Path "$root/summary.json"){Get-Content -Raw "$root/summary.json"|ConvertFrom-Json}else{$null}
 $rows=if(Test-Path "$root/install.jsonl"){@(Get-Content "$root/install.jsonl"|ForEach-Object{$_|ConvertFrom-Json})}else{@()}
 $complete=@($rows|Where-Object type -eq 'install_complete');$owner=@($rows|Where-Object type -eq 'export_owner')
 $load=@($rows|Where-Object type -eq 'LoadLibraryW');$proc=@($rows|Where-Object type -eq 'GetProcAddress');$hook=@($rows|Where-Object type -eq 'SetWindowsHookExW.thread_only')
 $cleanup=$null -ne $summary -and $summary.PSObject.Properties.Name -contains 'jobEmpty' -and $summary.jobEmpty -and $summary.ownedHandlesClosed -and $summary.desktopAbsent -and -not $summary.installerForced -and -not $summary.targetForced
 $unchanged=$before -and (Test-Path $path) -and (Get-FileHash -LiteralPath $path).Hash -eq $before
 $passed=$exit -eq 0 -and -not $errorText -and $cleanup -and $unchanged -and $complete.Count -eq 1 -and $complete[0].firstError -eq 0 -and $complete[0].accepted -and $complete[0].noopDispatched -and $complete[0].unhookConfirmed -and $complete[0].moduleReleased -and $owner.Count -eq 1 -and $owner[0].sameModule -and $load.Count -eq 1 -and $load[0].error -eq 0 -and $proc.Count -eq 1 -and $proc[0].error -eq 0 -and $hook.Count -eq 1 -and $hook[0].error -eq 0
 $cases+=[ordered]@{path=$path;dllSha256=$before;controllerExit=$exit;error=$errorText;cleanupVerified=$cleanup;fileUnchanged=$unchanged;installationPassed=$passed;supervisor=$summary;apiStages=$rows}
}
$sameBoot=$boot -eq (Get-CimInstance Win32_OperatingSystem).LastBootUpTime
$sameBuild=(Get-FileHash $exe).Hash -eq $exeHash -and (Get-FileHash $PSCommandPath).Hash -eq $scriptHash
$passed=$cases.Count -gt 0 -and $sameBoot -and $sameBuild -and @($cases|Where-Object{-not $_.installationPassed}).Count -eq 0
[ordered]@{boot=$boot;exeSha256=$exeHash;scriptSha256=$scriptHash;sameBoot=$sameBoot;sameBuild=$sameBuild;cases=$cases;probePassed=$passed;historical126Reproduced=(@($cases|Where-Object{@($_.apiStages|Where-Object{$_.type -eq 'SetWindowsHookExW.thread_only' -and $_.error -eq 126}).Count -gt 0}).Count -gt 0);rootCauseConfirmed=$false;causalFixVerified=$false;inputCapabilityClaimed=$false;fullLegacyRegressionRerun=$false;fullR5004Passed=$false;gateClosed=$false}|ConvertTo-Json -Depth 12|Set-Content "$ResultsDir/summary.json" -Encoding utf8
if(-not $passed){throw 'Hook installation or independent cleanup failed; original stage/error is retained'}
