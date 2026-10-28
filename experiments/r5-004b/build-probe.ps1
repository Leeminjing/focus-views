param([string]$OutDir=(Join-Path $PSScriptRoot 'out'))
$ErrorActionPreference='Stop'
$previous=Get-Content -Raw (Join-Path $PSScriptRoot '../r5-003/out/build.json')|ConvertFrom-Json
$compiler=Join-Path $previous.msvc 'bin/Hostx64/x64/cl.exe'
$OutDir=[IO.Path]::GetFullPath($OutDir);New-Item -ItemType Directory -Path $OutDir -Force|Out-Null
$oldPath=$env:PATH;$oldInclude=$env:INCLUDE;$oldLib=$env:LIB
try {
 $env:PATH="$(Split-Path $compiler);$oldPath"
 $env:INCLUDE=@((Join-Path $previous.msvc 'include'),(Join-Path $previous.sdk "Include/$($previous.sdkVersion)/ucrt"),(Join-Path $previous.sdk "Include/$($previous.sdkVersion)/shared"),(Join-Path $previous.sdk "Include/$($previous.sdkVersion)/um"))-join ';'
 $env:LIB=@((Join-Path $previous.msvc 'lib/x64'),(Join-Path $previous.sdk "Lib/$($previous.sdkVersion)/ucrt/x64"),(Join-Path $previous.sdk "Lib/$($previous.sdkVersion)/um/x64"))-join ';'
 & $compiler /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT /GS /guard:cf /utf-8 /DUNICODE /D_UNICODE (Join-Path $PSScriptRoot 'load-probe.cpp') "/Fo$OutDir/load-probe.obj" "/Fe$OutDir/load-probe.exe" /link /DYNAMICBASE /NXCOMPAT /CETCOMPAT /guard:cf /INCREMENTAL:NO
 if($LASTEXITCODE -ne 0){throw 'Loader probe build failed'}
}finally{$env:PATH=$oldPath;$env:INCLUDE=$oldInclude;$env:LIB=$oldLib}
