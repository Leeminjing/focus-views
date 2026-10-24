param(
    [string]$MsvcRoot,
    [string]$SdkRoot,
    [string]$SdkVersion,
    [string]$OutDir = (Join-Path $PSScriptRoot 'out'),
    [string]$SourceDir = $PSScriptRoot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Use an existing developer prompt, a normal VS installation, or explicit roots.
# No downloads, package installation, registry changes, or machine-wide settings.
if (-not $MsvcRoot -and $env:VCToolsInstallDir) { $MsvcRoot = $env:VCToolsInstallDir }
if (-not $MsvcRoot) {
    $taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $taskVswhere) {
        $taskVsRoot = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($taskVsRoot) {
            $MsvcRoot = (Get-ChildItem (Join-Path $taskVsRoot 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
        }
    }
}
if (-not $SdkRoot -and $env:WindowsSdkDir) { $SdkRoot = $env:WindowsSdkDir }
if (-not $SdkRoot) { $SdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10' }
if (-not $SdkVersion) {
    if (-not (Test-Path -LiteralPath (Join-Path $SdkRoot 'Include'))) { throw 'Windows SDK not found; pass -SdkRoot.' }
    $SdkVersion = (Get-ChildItem (Join-Path $SdkRoot 'Include') -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).Name
}
if (-not $MsvcRoot) { throw 'MSVC x64 tools not found; pass -MsvcRoot (directory containing bin/include/lib).' }
$MsvcRoot = (Resolve-Path -LiteralPath $MsvcRoot).Path
$SdkRoot = (Resolve-Path -LiteralPath $SdkRoot).Path
$taskCompiler = Join-Path $MsvcRoot 'bin\Hostx64\x64\cl.exe'
$taskAssembler = Join-Path $MsvcRoot 'bin\Hostx64\x64\ml64.exe'
foreach ($taskTool in @($taskCompiler, $taskAssembler)) {
    if (-not (Test-Path -LiteralPath $taskTool)) { throw "Missing tool: $taskTool" }
}
$OutDir = [IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

$SourceDir = (Resolve-Path -LiteralPath $SourceDir).Path
$taskHashSources = @(Get-ChildItem -LiteralPath $SourceDir -File | Where-Object { $_.Extension -in '.cpp','.hpp','.asm','.ps1' } | Sort-Object Name)
$taskHashSources += Get-Item (Join-Path $PSScriptRoot '../common/native.hpp'), $PSCommandPath
$taskHashMaterial = $taskHashSources | ForEach-Object { (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
$taskSha = [Security.Cryptography.SHA256]::Create()
try {
    $taskBuildId = [BitConverter]::ToString($taskSha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($taskHashMaterial -join '')))).Replace('-','').ToLowerInvariant()
} finally { $taskSha.Dispose() }
[IO.File]::WriteAllText((Join-Path $OutDir 'build_id.hpp'), "#pragma once`n#define R1_BUILD_ID `"$taskBuildId`"`n", [Text.Encoding]::ASCII)

$taskOldPath = $env:PATH
$taskOldInclude = $env:INCLUDE
$taskOldLib = $env:LIB
try {
    $env:PATH = "$(Split-Path $taskCompiler);$taskOldPath"
    $env:INCLUDE = @((Join-Path $MsvcRoot 'include'), (Join-Path $SdkRoot "Include\$SdkVersion\ucrt"),
        (Join-Path $SdkRoot "Include\$SdkVersion\shared"), (Join-Path $SdkRoot "Include\$SdkVersion\um")) -join ';'
    $env:LIB = @((Join-Path $MsvcRoot 'lib\x64'), (Join-Path $SdkRoot "Lib\$SdkVersion\ucrt\x64"),
        (Join-Path $SdkRoot "Lib\$SdkVersion\um\x64")) -join ';'
    $taskMetadata = [ordered]@{
        time = (Get-Date -Format o); msvc = $MsvcRoot; sdk = $SdkRoot; sdkVersion = $SdkVersion
        compilerVersion = (Get-Item -LiteralPath $taskCompiler).VersionInfo.FileVersion
        buildId = $taskBuildId; architecture = 'x64'; runtime = 'static /MT'; systemSettingsChanged = $false
    }
    $taskMetadata | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutDir 'build.json') -Encoding UTF8
    & $taskAssembler /nologo /c "/Fo$(Join-Path $OutDir 'workload.obj')" (Join-Path $SourceDir 'workload.asm')
    if ($LASTEXITCODE -ne 0) { throw "ml64 failed: $LASTEXITCODE" }
    $taskCommon = @('/nologo','/std:c++20','/EHsc','/W4','/WX','/O2','/MT','/utf-8','/D_WIN32_WINNT=0x0A00',"/I$OutDir")
    & $taskCompiler @taskCommon "/Fo$(Join-Path $OutDir 'fixture.obj')" "/Fe$(Join-Path $OutDir 'fixture.exe')" (Join-Path $SourceDir 'fixture.cpp') (Join-Path $OutDir 'workload.obj') /link /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT /CETCOMPAT
    if ($LASTEXITCODE -ne 0) { throw "fixture build failed: $LASTEXITCODE" }
    & $taskCompiler @taskCommon "/Fo$(Join-Path $OutDir 'controller.obj')" "/Fe$(Join-Path $OutDir 'controller.exe')" (Join-Path $SourceDir 'controller.cpp') bcrypt.lib /link /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT /CETCOMPAT
    if ($LASTEXITCODE -ne 0) { throw "controller build failed: $LASTEXITCODE" }
    Write-Output "BUILD_OK out=$OutDir build=$taskBuildId"
} finally {
    $env:PATH = $taskOldPath
    $env:INCLUDE = $taskOldInclude
    $env:LIB = $taskOldLib
}
