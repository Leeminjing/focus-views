param([string]$OutDir=(Join-Path $PSScriptRoot 'out'))
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$old=Get-Content -Raw (Join-Path $PSScriptRoot '../r5-004/out/build.json')|ConvertFrom-Json
$compiler=Join-Path $old.msvc 'bin/Hostx64/x64/cl.exe';$previousPath=$env:PATH;$previousInclude=$env:INCLUDE;$previousLib=$env:LIB
try{$env:PATH="$(Split-Path $compiler);$previousPath";$env:INCLUDE=@((Join-Path $old.msvc 'include'),(Join-Path $old.sdk "Include/$($old.sdkVersion)/ucrt"),(Join-Path $old.sdk "Include/$($old.sdkVersion)/shared"),(Join-Path $old.sdk "Include/$($old.sdkVersion)/um"))-join ';';$env:LIB=@((Join-Path $old.msvc 'lib/x64'),(Join-Path $old.sdk "Lib/$($old.sdkVersion)/ucrt/x64"),(Join-Path $old.sdk "Lib/$($old.sdkVersion)/um/x64"))-join ';'
New-Item -ItemType Directory -Path $OutDir -Force|Out-Null
& $compiler /nologo /std:c++20 /EHsc /W4 /WX /O2 /Ob0 /MT /GS /guard:cf /Gy /utf-8 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 (Join-Path $PSScriptRoot 'minimal.cpp') "/Fo$(Join-Path $OutDir 'minimal.obj')" "/Fe$(Join-Path $OutDir 'minimal.exe')" user32.lib gdi32.lib advapi32.lib bcrypt.lib /link /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT /CETCOMPAT /guard:cf
if($LASTEXITCODE -ne 0){throw 'Minimal compile failed'}
}finally{$env:PATH=$previousPath;$env:INCLUDE=$previousInclude;$env:LIB=$previousLib}
