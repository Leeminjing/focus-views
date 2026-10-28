param([Parameter(Mandatory)][string]$OutDir,[Parameter(Mandatory)][string]$Profile,[Parameter(Mandatory)][string]$ResultsDir)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$OutDir=[IO.Path]::GetFullPath($OutDir);$ResultsDir=[IO.Path]::GetFullPath($ResultsDir)
if(Test-Path -LiteralPath $ResultsDir){throw 'New evidence root required'}
New-Item -ItemType Directory -Path $ResultsDir|Out-Null
$profileHash=(Get-FileHash $Profile).Hash;$boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime;$cases=@()
$binaries=@('manager.exe','second.exe','kernel.lib','abort-runtime.dll','abort-helper.exe','diagnose.exe','trim-client.dll','close-probe.exe')
$before=@($binaries|ForEach-Object{[ordered]@{file=$_;sha256=(Get-FileHash "$OutDir/$_").Hash.ToLowerInvariant()}})
foreach($mode in @('normal','checkpoint')){
 $root=Join-Path $ResultsDir $mode;New-Item -ItemType Directory -Path "$root/store"|Out-Null
 Copy-Item -LiteralPath $Profile -Destination "$root/store/resource.bin"
 $errorText='';$exit=-1
 try{& "$OutDir/controller.exe" --manager "$OutDir/manager.exe" --client "$OutDir/trim-client.dll" --fixture "$OutDir/second.exe" --mode save --store "$root/store" --manager-output "$root/manager" --output "$root/supervisor" --kill-at '' --previous '' --scenario ('profile-'+$mode) *> "$root/console.log";$exit=$LASTEXITCODE}catch{$errorText=$_.ToString()}
 $supervisor=if(Test-Path "$root/supervisor/summary.json"){Get-Content -Raw "$root/supervisor/summary.json"|ConvertFrom-Json}else{$null}
 $rows=if(Test-Path "$root/manager/controller.jsonl"){@(Get-Content "$root/manager/controller.jsonl"|ForEach-Object{$_|ConvertFrom-Json})}else{@()}
 $complete=@($rows|Where-Object type -eq 'trim_complete')
 $diagnostic=if($mode -eq 'normal'){"$root/manager/normal-diagnostic.jsonl"}else{"$root/manager/trim/cleanup-diagnostic.jsonl"}
 $wct=if(Test-Path $diagnostic){@(Get-Content $diagnostic|ForEach-Object{$_|ConvertFrom-Json})}else{@()}
 $nodes=@($wct|Where-Object type -eq 'wait_node');$queries=@($wct|Where-Object type -eq 'wait_chain');$workers=@($queries|ForEach-Object{$_.tid}|Select-Object -Unique)
 $pattern=@($nodes|Where-Object kind -eq 6).Count -gt 0 -and @($nodes|Where-Object kind -eq 5).Count -gt 0 -and @($nodes|Where-Object{$_.kind -eq 8 -and $_.status -eq 5 -and $_.tid -eq 0}).Count -gt 0 -and $workers.Count -ge 2
 $cases+=[ordered]@{mode=$mode;controllerExit=$exit;error=$errorText;supervisor=$supervisor;nativeCleanupPassed=($complete.Count -eq 1 -and $complete[0].nativeCleanupPassed);naturalWaitPatternRetained=$pattern;workerQueries=$workers;profileUnchanged=((Get-FileHash "$root/store/resource.bin").Hash -eq $profileHash);snapshotOrSessionPresent=@(Get-ChildItem "$root/store" -Filter *.fvs -File).Count -gt 0}
}
$sameBuild=@($before|Where-Object{(Get-FileHash "$OutDir/$($_.file)").Hash.ToLowerInvariant() -ne $_.sha256}).Count -eq 0
$clean=@($cases|Where-Object{$null -eq $_.supervisor -or -not $_.supervisor.cleanupVerified -or -not $_.supervisor.protocolPassed -or -not $_.profileUnchanged -or $_.snapshotOrSessionPresent}).Count -eq 0
$nativePassed=@($cases|Where-Object{-not $_.nativeCleanupPassed}).Count -eq 0
[ordered]@{diagnosticSequenceCompleted=$clean;sameBuild=$sameBuild;sameBoot=($boot -eq (Get-CimInstance Win32_OperatingSystem).LastBootUpTime);nativeCleanupPassed=$nativePassed;cleanupCausalFixVerified=$false;fullR5004Passed=$false;gateClosed=$false;profileSha256=$profileHash;binaries=$before;cases=$cases}|ConvertTo-Json -Depth 9|Set-Content "$ResultsDir/summary.json" -Encoding utf8
if(-not $clean -or -not $sameBuild -or -not $nativePassed){throw 'Native cleanup remains unconfirmed; preserve diagnostic and physical cleanup results separately'}
