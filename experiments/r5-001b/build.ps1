param([string]$MsvcRoot, [string]$SdkRoot, [string]$SdkVersion,
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
$taskCompileInputs=@('manager_main.cpp','clients/command.hpp','clients/fixture_client.cpp','clients/host.hpp','clients/observation.hpp','clients/recorder_client.cpp','recorder/contract.hpp','recorder/io.cpp','recorder/main.cpp','build.ps1','../r5-001a/app/traps.asm')
$taskHashMaterial=@($taskCompileInputs | ForEach-Object { (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot $_)).Hash }) -join ''
$taskSha=[Security.Cryptography.SHA256]::Create()
try { $taskBuildId=[BitConverter]::ToString($taskSha.ComputeHash([Text.Encoding]::UTF8.GetBytes($taskHashMaterial))).Replace('-','').ToLowerInvariant() } finally { $taskSha.Dispose() }
[IO.File]::WriteAllText((Join-Path $OutDir 'build_id.hpp'), "#pragma once`n#define R305_BUILD_ID `"$taskBuildId`"`n", [Text.Encoding]::ASCII)
$taskOldPath = $env:PATH; $taskOldInclude = $env:INCLUDE; $taskOldLib = $env:LIB
try {
    $env:PATH = "$(Split-Path $taskCompiler);$taskOldPath"
    $env:INCLUDE = @((Join-Path $MsvcRoot 'include'), (Join-Path $SdkRoot "Include\$SdkVersion\ucrt"),
        (Join-Path $SdkRoot "Include\$SdkVersion\shared"), (Join-Path $SdkRoot "Include\$SdkVersion\um")) -join ';'
    $env:LIB = @((Join-Path $MsvcRoot 'lib\x64'), (Join-Path $SdkRoot "Lib\$SdkVersion\ucrt\x64"),
        (Join-Path $SdkRoot "Lib\$SdkVersion\um\x64")) -join ';'
    $taskCommon = @('/nologo','/std:c++20','/EHsc','/W4','/WX','/O2','/MT','/GS','/guard:cf','/Gy','/utf-8','/D_WIN32_WINNT=0x0A00','/DUNICODE','/D_UNICODE',"/I$OutDir",'/Zi',"/Fd$(Join-Path $OutDir 'compiler.pdb')")
    $baseline=Join-Path $PSScriptRoot '../r5-001a/out'
    $reused=@('kernel.lib','target.lib','helper.exe','runtime304.dll','fixture.exe')
    $reuseManifest=@()
    foreach($name in $reused){
      $source=Join-Path $baseline $name;$target=Join-Path $OutDir $name
      if(-not (Test-Path -LiteralPath $source)){throw "Build R5-001a first; missing immutable input $name"}
      Copy-Item -LiteralPath $source -Destination $target -Force
      $sha=(Get-FileHash $source).Hash.ToLowerInvariant();if($sha -ne (Get-FileHash $target).Hash.ToLowerInvariant()){throw 'Reuse hash mismatch'}
      $reuseManifest+=[ordered]@{file=$name;source=$source;sha256=$sha;identical=$true}
    }
    $reuseManifest|ConvertTo-Json|Set-Content (Join-Path $OutDir 'reuse.json') -Encoding utf8
    $taskAssembler=Join-Path $MsvcRoot 'bin/Hostx64/x64/ml64.exe'
    & $taskAssembler /nologo /c "/Fo$(Join-Path $OutDir 'traps.obj')" (Join-Path $PSScriptRoot '../r5-001a/app/traps.asm')
    if($LASTEXITCODE -ne 0){throw 'Trap assembly failed'}
    $groups=[ordered]@{manager=@('manager_main.cpp');fixture_client=@('clients/fixture_client.cpp');recorder_client=@('clients/recorder_client.cpp');second=@('recorder/main.cpp','recorder/io.cpp')}
    $objects=@{}
    foreach($group in $groups.Keys){$objects[$group]=@();foreach($unit in $groups[$group]){
      $obj=Join-Path $OutDir ($group+'-'+[IO.Path]::GetFileNameWithoutExtension($unit)+'.obj');$opts=$taskCommon
      if($unit -eq 'recorder/main.cpp'){$opts=@($taskCommon|Where-Object {$_ -ne '/O2'})+@('/Od','/Ob0','/FAs',"/Fa$(Join-Path $OutDir 'second.asm')")}
      & $taskCompiler @opts /c "/Fo$obj" (Join-Path $PSScriptRoot $unit)
      if($LASTEXITCODE -ne 0){throw "Compile failed: $unit"};$objects[$group]+=$obj
    }}
    $libs=@('user32.lib','gdi32.lib','ole32.lib','windowscodecs.lib','bcrypt.lib','imm32.lib','advapi32.lib')
    foreach($client in @('fixture_client','recorder_client')){
      $dll=if($client -eq 'fixture_client'){'fixture-client.dll'}else{'recorder-client.dll'}
      & $taskCompiler /nologo /LD @($objects[$client]) (Join-Path $OutDir 'kernel.lib') "/Fe$(Join-Path $OutDir $dll)" @libs /link /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT /CETCOMPAT /guard:cf /DEBUG:FULL "/MAP:$(Join-Path $OutDir ($client+'.map'))"
      if($LASTEXITCODE -ne 0){throw "Client link failed: $client"}
    }
    & $taskCompiler /nologo @($objects.manager) "/Fe$(Join-Path $OutDir 'manager.exe')" @libs /link /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT /CETCOMPAT /guard:cf /DEBUG:FULL "/MAP:$(Join-Path $OutDir 'manager.map')"
    if($LASTEXITCODE -ne 0){throw 'Manager link failed'}
    & $taskCompiler /nologo @($objects.second) (Join-Path $OutDir 'traps.obj') (Join-Path $OutDir 'target.lib') (Join-Path $OutDir 'kernel.lib') "/Fe$(Join-Path $OutDir 'second.exe')" @libs /link /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT /CETCOMPAT /guard:cf /DEBUG:FULL "/MAP:$(Join-Path $OutDir 'second.map')"
    if($LASTEXITCODE -ne 0){throw 'Recorder link failed'}
    $groups|ConvertTo-Json -Depth 5|Set-Content (Join-Path $OutDir 'link-boundaries.json') -Encoding utf8
    foreach($exe in @('manager','fixture','second')){& (Join-Path $MsvcRoot 'bin/Hostx64/x64/dumpbin.exe') /loadconfig (Join-Path $OutDir ($exe+'.exe'))|Set-Content (Join-Path $OutDir ($exe+'-loadconfig.txt')) -Encoding utf8}
    [ordered]@{ time=(Get-Date -Format o); architecture='x64'; compilerVersion=(Get-Item $taskCompiler).VersionInfo.FileVersion
        msvc=$MsvcRoot; sdk=$SdkRoot; sdkVersion=$SdkVersion; runtime='static /MT'; systemSettingsChanged=$false; buildId=$taskBuildId; fixtureOptimization='/Od /Ob0'; security='/GS /guard:cf /CETCOMPAT'
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutDir 'build.json') -Encoding UTF8
    Write-Output "BUILD_OK $OutDir/manager.exe + reused fixture.exe + independent second.exe"
} finally { $env:PATH=$taskOldPath; $env:INCLUDE=$taskOldInclude; $env:LIB=$taskOldLib }
