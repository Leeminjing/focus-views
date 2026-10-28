param([string]$OutDir=(Join-Path $PSScriptRoot 'out'))
$ErrorActionPreference='Stop';$build=Get-Content -Raw (Join-Path $PSScriptRoot 'out-final/build.json')|ConvertFrom-Json
$compiler=Join-Path $build.msvc 'bin/Hostx64/x64/cl.exe';$oldPath=$env:PATH;$oldInclude=$env:INCLUDE;$oldLib=$env:LIB
try{
 $env:PATH="$(Split-Path $compiler);$oldPath";$env:INCLUDE=@((Join-Path $build.msvc 'include'),(Join-Path $build.sdk "Include/$($build.sdkVersion)/ucrt"),(Join-Path $build.sdk "Include/$($build.sdkVersion)/shared"),(Join-Path $build.sdk "Include/$($build.sdkVersion)/um"))-join ';';$env:LIB=@((Join-Path $build.msvc 'lib/x64'),(Join-Path $build.sdk "Lib/$($build.sdkVersion)/ucrt/x64"),(Join-Path $build.sdk "Lib/$($build.sdkVersion)/um/x64"))-join ';'
 & $compiler /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT /GS /guard:cf /utf-8 (Join-Path $PSScriptRoot 'dump-reader.cpp') "/Fo$OutDir/dump-reader.obj" "/Fe$OutDir/dump-reader.exe" dbgeng.lib /link /DYNAMICBASE /NXCOMPAT /CETCOMPAT /guard:cf
 if($LASTEXITCODE -ne 0){throw 'Dump reader build failed'}
}finally{$env:PATH=$oldPath;$env:INCLUDE=$oldInclude;$env:LIB=$oldLib}
