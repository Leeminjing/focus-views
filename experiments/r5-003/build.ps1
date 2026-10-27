param([string]$MsvcRoot, [string]$SdkRoot, [string]$SdkVersion,[switch]$LegacyQueryLog,
    [string]$OutDir = (Join-Path $PSScriptRoot 'out'))
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
# Separate GUI build; existing R1/R2 tools and experiments are not modified.
if (-not $MsvcRoot -and $env:VCToolsInstallDir) { $MsvcRoot = $env:VCToolsInstallDir }
if (-not $MsvcRoot) {
    $taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $taskVswhere) {
        $taskVs = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($taskVs) { $MsvcRoot = (Get-ChildItem (Join-Path $taskVs 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName }
    }
}
if (-not $MsvcRoot) { throw 'MSVC x64 not found; pass -MsvcRoot.' }
if (-not $SdkRoot -and $env:WindowsSdkDir) { $SdkRoot = $env:WindowsSdkDir }
if (-not $SdkRoot) { $SdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10' }
if (-not $SdkVersion) { $SdkVersion = (Get-ChildItem (Join-Path $SdkRoot 'Include') -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).Name }
$MsvcRoot = (Resolve-Path -LiteralPath $MsvcRoot).Path
$SdkRoot = (Resolve-Path -LiteralPath $SdkRoot).Path
$OutDir = [IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$taskCompiler = Join-Path $MsvcRoot 'bin\Hostx64\x64\cl.exe'
$taskOldPath=$env:PATH;$taskOldInclude=$env:INCLUDE;$taskOldLib=$env:LIB
try {
 $env:PATH="$(Split-Path $taskCompiler);$taskOldPath"
 $env:INCLUDE=@((Join-Path $MsvcRoot 'include'),(Join-Path $SdkRoot "Include/$SdkVersion/ucrt"),(Join-Path $SdkRoot "Include/$SdkVersion/shared"),(Join-Path $SdkRoot "Include/$SdkVersion/um"))-join ';'
 $env:LIB=@((Join-Path $MsvcRoot 'lib/x64'),(Join-Path $SdkRoot "Lib/$SdkVersion/ucrt/x64"),(Join-Path $SdkRoot "Lib/$SdkVersion/um/x64"))-join ';'
 $reuse=@()
 foreach($name in @('kernel.lib','fixture.exe','second.exe')){
  $source=Join-Path $PSScriptRoot "../r5-002/out/$name";$target=Join-Path $OutDir $name
  $sha=(Get-FileHash $source).Hash.ToLowerInvariant()
  if(-not(Test-Path -LiteralPath $target) -or $sha -ne (Get-FileHash $target).Hash.ToLowerInvariant()){Copy-Item -LiteralPath $source -Destination $target -Force}
  if($sha -ne (Get-FileHash $target).Hash.ToLowerInvariant()){throw 'Immutable dependency mismatch'}
  $reuse+=[ordered]@{file=$name;sha256=$sha;source=$source;identical=$true}
 }
 $reuse|ConvertTo-Json|Set-Content (Join-Path $OutDir 'reuse.json') -Encoding utf8
 $common=@('/nologo','/std:c++20','/EHsc','/W4','/WX','/O2','/MT','/GS','/guard:cf','/Gy','/utf-8','/D_WIN32_WINNT=0x0A00','/DUNICODE','/D_UNICODE','/Zi',"/Fd$(Join-Path $OutDir 'compiler.pdb')")
 $groups=[ordered]@{manager=@('manager_main.cpp');fixture=@('clients/fixture_client.cpp');recorder=@('clients/recorder_client.cpp');store=@('runtime/store.cpp');codec_test=@('codec_test.cpp');helper=@('../r5-001a/helper_main.cpp','../r5-001a/runtime/bridge.cpp','../r4-001/input.cpp','../r3-004/observer.cpp');input_runtime=@('../r4-001/runtime.cpp')}
 $objects=@{}
 foreach($group in $groups.Keys){$objects[$group]=@();foreach($unit in $groups[$group]){
  $obj=Join-Path $OutDir ($group+'-'+[IO.Path]::GetFileNameWithoutExtension($unit)+'.obj')
  $options=$common;if(-not $LegacyQueryLog -and $group -in @('helper','input_runtime')){$options+=@('/DFOCUS_VIEWS_QUERY_STREAM')}
  & $taskCompiler @options /c "/Fo$obj" (Join-Path $PSScriptRoot $unit)
  if($LASTEXITCODE -ne 0){throw "Compile failed $unit"};$objects[$group]+=$obj
 }}
 $libs=@('user32.lib','gdi32.lib','ole32.lib','windowscodecs.lib','bcrypt.lib','imm32.lib','advapi32.lib')
 & (Join-Path $MsvcRoot 'bin/Hostx64/x64/lib.exe') /nologo "/OUT:$(Join-Path $OutDir 'snapshot-store.lib')" @($objects.store)
 if($LASTEXITCODE -ne 0){throw 'Snapshot store archive failed'}
 foreach($client in @('fixture','recorder')){
  & $taskCompiler /nologo /LD @($objects[$client]) (Join-Path $OutDir 'snapshot-store.lib') (Join-Path $OutDir 'kernel.lib') "/Fe$(Join-Path $OutDir ($client+'-client.dll'))" @libs /link /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT /CETCOMPAT /guard:cf /DEBUG:FULL
  if($LASTEXITCODE -ne 0){throw "Client link failed $client"}
 }
 & $taskCompiler /nologo /LD @($objects.input_runtime) "/Fe$(Join-Path $OutDir 'runtime304.dll')" user32.lib /link /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT /CETCOMPAT /guard:cf /DEBUG:FULL
 if($LASTEXITCODE -ne 0){throw 'Diagnostic-stream input DLL failed'}
 foreach($exe in @('manager','codec_test','helper')){
  & $taskCompiler /nologo @($objects[$exe]) (Join-Path $OutDir 'snapshot-store.lib') (Join-Path $OutDir 'kernel.lib') "/Fe$(Join-Path $OutDir ($exe+'.exe'))" @libs /link /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT /CETCOMPAT /guard:cf /DEBUG:FULL
  if($LASTEXITCODE -ne 0){throw "Link failed $exe"}
 }
 $inputs=@('manager_main.cpp','clients/fixture_client.cpp','clients/recorder_client.cpp','clients/restart.hpp','runtime/store.hpp','runtime/store.cpp','runtime/query-stream.hpp','codec_test.cpp','build.ps1','../r5-002/clients/host.hpp','../r5-002/clients/fixture_model.hpp','../r5-002/clients/recorder_model.hpp','../r5-002/clients/observation.hpp','../r5-002/clients/command.hpp','../r4-001/runtime.cpp','../r4-001/input.cpp','../r5-001a/helper_main.cpp','../r5-001a/runtime/bridge.cpp','../r3-004/observer.cpp','../r3-004/channel.hpp')
 [ordered]@{time=(Get-Date -Format o);sourceHead=(& git -C $PSScriptRoot rev-parse HEAD);system=[Environment]::OSVersion.VersionString;msvc=$MsvcRoot;sdk=$SdkRoot;sdkVersion=$SdkVersion;compilerVersion=(Get-Item $taskCompiler).VersionInfo.FileVersion;security='/GS /guard:cf /CETCOMPAT';queryTransport=$(if($LegacyQueryLog){'legacy-total-quota'}else{'acknowledged-ring'});systemSettingsChanged=$false;sourceInputs=@($inputs|ForEach-Object{[ordered]@{file=$_;sha256=(Get-FileHash (Join-Path $PSScriptRoot $_)).Hash.ToLowerInvariant()}});binaries=@(Get-ChildItem $OutDir -File|Where-Object Extension -in @('.exe','.dll','.lib')|ForEach-Object{[ordered]@{file=$_.Name;sha256=(Get-FileHash $_.FullName).Hash.ToLowerInvariant()}})}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $OutDir 'build.json') -Encoding utf8
 Write-Output 'R5_003_BUILD_OK'
} finally {$env:PATH=$taskOldPath;$env:INCLUDE=$taskOldInclude;$env:LIB=$taskOldLib}
