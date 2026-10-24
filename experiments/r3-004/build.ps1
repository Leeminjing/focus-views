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
$taskOldPath = $env:PATH; $taskOldInclude = $env:INCLUDE; $taskOldLib = $env:LIB
try {
    $env:PATH = "$(Split-Path $taskCompiler);$taskOldPath"
    $env:INCLUDE = @((Join-Path $MsvcRoot 'include'), (Join-Path $SdkRoot "Include\$SdkVersion\ucrt"),
        (Join-Path $SdkRoot "Include\$SdkVersion\shared"), (Join-Path $SdkRoot "Include\$SdkVersion\um")) -join ';'
    $env:LIB = @((Join-Path $MsvcRoot 'lib\x64'), (Join-Path $SdkRoot "Lib\$SdkVersion\ucrt\x64"),
        (Join-Path $SdkRoot "Lib\$SdkVersion\um\x64")) -join ';'
    $taskCommon = @('/nologo','/std:c++20','/EHsc','/W4','/WX','/O2','/MT','/utf-8','/D_WIN32_WINNT=0x0A00','/DUNICODE','/D_UNICODE')
    & $taskCompiler @taskCommon /LD (Join-Path $PSScriptRoot 'runtime.cpp') "/Fo$(Join-Path $OutDir 'runtime.obj')" "/Fe$(Join-Path $OutDir 'runtime304.dll')" user32.lib /link /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT /CETCOMPAT
    if ($LASTEXITCODE -ne 0) { throw 'Runtime DLL build failed' }
    $taskObjects = @()
    foreach ($taskUnit in @('controller','input','observer','fixture','bridge')) {
        $taskObject = Join-Path $OutDir ($taskUnit + '.obj')
        & $taskCompiler @taskCommon /c "/Fo$taskObject" $(if ($taskUnit -in 'fixture','bridge') { Join-Path $PSScriptRoot ('../r3/' + $taskUnit + '.cpp') } else { Join-Path $PSScriptRoot ($taskUnit + '.cpp') })
        if ($LASTEXITCODE -ne 0) { throw "Compile failed: $taskUnit" }
        $taskObjects += $taskObject
    }
    & $taskCompiler /nologo @taskObjects "/Fe$(Join-Path $OutDir 'r304.exe')" user32.lib gdi32.lib ole32.lib windowscodecs.lib bcrypt.lib /link /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT /CETCOMPAT
    if ($LASTEXITCODE -ne 0) { throw 'Link failed' }
    [ordered]@{ time=(Get-Date -Format o); architecture='x64'; compilerVersion=(Get-Item $taskCompiler).VersionInfo.FileVersion
        msvc=$MsvcRoot; sdk=$SdkRoot; sdkVersion=$SdkVersion; runtime='static /MT'; systemSettingsChanged=$false
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutDir 'build.json') -Encoding UTF8
    Write-Output "BUILD_OK $OutDir\r304.exe"
} finally { $env:PATH=$taskOldPath; $env:INCLUDE=$taskOldInclude; $env:LIB=$taskOldLib }
