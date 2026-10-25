param([string]$OutDir=(Join-Path $PSScriptRoot 'out'))
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$config=Get-Content (Join-Path $PSScriptRoot '../r3-005b-001/out/build.json') -Raw|ConvertFrom-Json
$compiler=Join-Path $config.msvc 'bin/Hostx64/x64/cl.exe'
$OutDir=[IO.Path]::GetFullPath($OutDir);New-Item -ItemType Directory -Force $OutDir|Out-Null
$beforePath=$env:PATH;$beforeInclude=$env:INCLUDE;$beforeLib=$env:LIB
try{
 $env:PATH="$(Split-Path $compiler);$beforePath"
 $env:INCLUDE=@((Join-Path $config.msvc 'include'),(Join-Path $config.sdk "Include/$($config.sdkVersion)/ucrt"),(Join-Path $config.sdk "Include/$($config.sdkVersion)/shared"),(Join-Path $config.sdk "Include/$($config.sdkVersion)/um"))-join ';'
 $env:LIB=@((Join-Path $config.msvc 'lib/x64'),(Join-Path $config.sdk "Lib/$($config.sdkVersion)/ucrt/x64"),(Join-Path $config.sdk "Lib/$($config.sdkVersion)/um/x64"))-join ';'
 foreach($unit in @('handle-probe','launch-probe')){
  & $compiler /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT /GS /guard:cf /utf-8 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 (Join-Path $PSScriptRoot ($unit+'.cpp')) "/Fo$OutDir/$unit.obj" "/Fe$OutDir/$unit.exe" user32.lib advapi32.lib /link /CETCOMPAT /DYNAMICBASE /NXCOMPAT
  if($LASTEXITCODE -ne 0){throw "Probe build failed: $unit"}
 }
}finally{$env:PATH=$beforePath;$env:INCLUDE=$beforeInclude;$env:LIB=$beforeLib}
