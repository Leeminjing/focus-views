param([string]$OutDir=(Join-Path $PSScriptRoot 'out'))
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$cfg=Get-Content (Join-Path $PSScriptRoot '../r3-005b-001/out/build.json') -Raw|ConvertFrom-Json
$compiler=Join-Path $cfg.msvc 'bin/Hostx64/x64/cl.exe';$OutDir=[IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Force $OutDir|Out-Null
$beforePath=$env:PATH;$beforeInclude=$env:INCLUDE;$beforeLib=$env:LIB
try{
 $env:PATH="$(Split-Path $compiler);$beforePath"
 $env:INCLUDE=@((Join-Path $cfg.msvc 'include'),(Join-Path $cfg.sdk "Include/$($cfg.sdkVersion)/ucrt"),(Join-Path $cfg.sdk "Include/$($cfg.sdkVersion)/shared"),(Join-Path $cfg.sdk "Include/$($cfg.sdkVersion)/um"))-join ';'
 $env:LIB=@((Join-Path $cfg.msvc 'lib/x64'),(Join-Path $cfg.sdk "Lib/$($cfg.sdkVersion)/ucrt/x64"),(Join-Path $cfg.sdk "Lib/$($cfg.sdkVersion)/um/x64"))-join ';'
 foreach($unit in @('probe','trace')){
  & $compiler /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT /GS /guard:cf /utf-8 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 (Join-Path $PSScriptRoot ($unit+'.cpp')) "/Fo$OutDir/$unit.obj" "/Fe$OutDir/$unit.exe" user32.lib advapi32.lib psapi.lib ole32.lib tdh.lib /link /CETCOMPAT /DYNAMICBASE /NXCOMPAT
  if($LASTEXITCODE -ne 0){throw "Build failed: $unit"}
  & (Join-Path (Split-Path $compiler) 'dumpbin.exe') /loadconfig "$OutDir/$unit.exe"|Set-Content "$OutDir/$unit-loadconfig.txt" -Encoding utf8
 }
 [ordered]@{time=(Get-Date -Format o);compilerVersion=(Get-Item $compiler).VersionInfo.FileVersion;msvc=$cfg.msvc;sdk=$cfg.sdk;sdkVersion=$cfg.sdkVersion;flags='/std:c++20 /O2 /MT /GS /guard:cf /CETCOMPAT';systemSettingsChanged=$false}|ConvertTo-Json|Set-Content "$OutDir/build.json" -Encoding utf8
}finally{$env:PATH=$beforePath;$env:INCLUDE=$beforeInclude;$env:LIB=$beforeLib}
