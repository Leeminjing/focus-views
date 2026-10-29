param([Parameter(Mandatory)][string]$Root)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$Root=[IO.Path]::GetFullPath($Root);if(Test-Path $Root){throw 'Fresh build preparation root required'};New-Item -ItemType Directory $Root|Out-Null
$repo=[IO.Path]::GetFullPath("$PSScriptRoot/../..");$tool=Get-Content "$PSScriptRoot/../r5-003/out/build.json" -Raw|ConvertFrom-Json
$legacy=@('r3-005a-cet-002','r3-005b-001','r3-005c-001','r4-001','r4-002a','r4-003-001','r5-001a','r5-001b','r5-002','r5-003')
$dirs=@($legacy|ForEach-Object{[IO.Path]::GetFullPath("$PSScriptRoot/../$_/out")})
# Do not replace binaries that another process is using. Preserve previous build
# artifacts separately; historical result directories are never touched.
$active=@(Get-CimInstance Win32_Process|Where-Object{$p=$_.ExecutablePath;$p -and @($dirs|Where-Object{$p.StartsWith($_+'\',[StringComparison]::OrdinalIgnoreCase)}).Count})
if($active.Count){throw 'An existing process still uses a legacy build directory'}
foreach($name in $legacy){$old="$PSScriptRoot/../$name/out";if(Test-Path $old){$backup="$Root/previous-builds/$name";New-Item -ItemType Directory $backup -Force|Out-Null;Get-ChildItem $old -File|Where-Object{$_.Extension -in @('.exe','.dll','.lib','.json','.pdb','.map','.asm','.txt') -or $_.Name -eq 'build.log'}|Where-Object Name -notmatch '^r5004b-'|Copy-Item -Destination $backup}}
foreach($name in $legacy){
 & "$PSScriptRoot/../$name/build.ps1" -MsvcRoot $tool.msvc -SdkRoot $tool.sdk -SdkVersion $tool.sdkVersion *> "$Root/$name-build.log"
 Copy-Item "$Root/$name-build.log" "$PSScriptRoot/../$name/out/build.log" -Force
 Write-Output "PREBUILT $name"
}
& "$PSScriptRoot/../r5-004b/build.ps1" -OutDir "$Root/execution" *> "$Root/execution-build.log"
& "$PSScriptRoot/build.ps1" -Source "$Root/execution" -OutDir "$Root/candidate" *> "$Root/candidate-build.log"
& "$PSScriptRoot/../r5-004c-wait/build.ps1" -ReuseCandidate "$Root/candidate" -OutDir "$Root/control" *> "$Root/control-build.log"
foreach($spec in @(@('b001','r3-005b-001',$false),@('cet','r3-005a-cet-002',$false),@('query','r3-005b-001',$true))){
 $name=$spec[0];$params=@{MsvcRoot=$tool.msvc;SdkRoot=$tool.sdk;SdkVersion=$tool.sdkVersion;OutDir="$Root/$name";InstallDiagnostics=$true;DiagnosticQueryFailure=[bool]$spec[2]}
 & "$PSScriptRoot/../$($spec[1])/build.ps1" @params *> "$Root/$name-build.log";Copy-Item "$Root/$name-build.log" "$Root/$name/build.log"
}
& "$PSScriptRoot/../r5-003/prepare-regression-input.ps1" -EvidenceDir "$Root/prepared-regression-input" *> "$Root/profile-console.log"
$Profile="$Root/resource.bin"
& "$Root/control/controller.exe" --manager "$Root/control/manager.exe" --client "$Root/control/recorder-client.dll" --fixture "$Root/control/second.exe" --mode save --store "$Root/profile-source" --manager-output "$Root/profile-manager" --output "$Root/profile-supervisor" --kill-at session-committed --previous '' --scenario positive *> "$Root/profile-source-console.log"
if($LASTEXITCODE -ne 0){throw 'Current-build profile source supervisor failed'}
$owner=Get-Content "$Root/profile-supervisor/summary.json" -Raw|ConvertFrom-Json
if(-not $owner.protocolPassed -or -not $owner.cleanupVerified){throw 'Profile source resources not independently cleaned'}
& "$Root/control/profile.exe" --fixture "$Root/control/second.exe" --snapshot "$Root/profile-source/S0.fvs" --output $Profile *> "$Root/profile-create-console.log"
if($LASTEXITCODE -ne 0){throw 'Current-build resource profile creation failed'}
$prepared="$Root/prepared-regression-input/summary.json"
. "$PSScriptRoot/frozen-inputs.ps1"
$dirs+=@('execution','candidate','control','b001','cet','query')|ForEach-Object{"$Root/$_"}
$inputs=Get-CombinationInputs $dirs $Profile $prepared
[ordered]@{version=1;time=[DateTimeOffset]::UtcNow.ToString('o');head=(& git -C $repo rev-parse HEAD);buildDirs=$dirs;profile=[IO.Path]::GetFullPath($Profile);preparedRegressionInput=$prepared;inputs=$inputs}|ConvertTo-Json -Depth 6|Set-Content "$Root/frozen-inputs.json"
Write-Output 'ALL_COMBINATION_INPUTS_PREPARED_AND_FROZEN'
