param([string]$OutDir=(Join-Path $PSScriptRoot 'out-probe'))
$ErrorActionPreference='Stop'
$previous=Get-Content -Raw (Join-Path $PSScriptRoot '../r5-004b/out-final/build.json')|ConvertFrom-Json
$OutDir=[IO.Path]::GetFullPath($OutDir);New-Item -ItemType Directory -Path $OutDir -Force|Out-Null
$compiler=Join-Path $previous.msvc 'bin/Hostx64/x64/cl.exe';$oldPath=$env:PATH;$oldInclude=$env:INCLUDE;$oldLib=$env:LIB
try {
 $env:PATH="$(Split-Path $compiler);$oldPath";$env:INCLUDE=@((Join-Path $previous.msvc 'include'),(Join-Path $previous.sdk "Include/$($previous.sdkVersion)/ucrt"),(Join-Path $previous.sdk "Include/$($previous.sdkVersion)/shared"),(Join-Path $previous.sdk "Include/$($previous.sdkVersion)/um"))-join ';';$env:LIB=@((Join-Path $previous.msvc 'lib/x64'),(Join-Path $previous.sdk "Lib/$($previous.sdkVersion)/ucrt/x64"),(Join-Path $previous.sdk "Lib/$($previous.sdkVersion)/um/x64"))-join ';'
 & (Join-Path $previous.msvc 'bin/Hostx64/x64/ml64.exe') /nologo /c "/Fo$OutDir/trap.obj" (Join-Path $PSScriptRoot 'probe-trap.asm');if($LASTEXITCODE -ne 0){throw 'Probe assembler failed'}
 foreach($name in @('probe','probe-target')){
  $objects=@();if($name -eq 'probe-target'){$objects+="$OutDir/trap.obj"}
  & $compiler /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT /GS /guard:cf /utf-8 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 (Join-Path $PSScriptRoot "$name.cpp") @objects (Join-Path $PSScriptRoot '../r5-004b/out-final/kernel.lib') user32.lib gdi32.lib ole32.lib windowscodecs.lib bcrypt.lib imm32.lib advapi32.lib "/Fo$OutDir/$name.obj" "/Fe$OutDir/$name.exe" /link /DYNAMICBASE /NXCOMPAT /CETCOMPAT /guard:cf /INCREMENTAL:NO /DEBUG:FULL
  if($LASTEXITCODE -ne 0){throw "Probe build failed: $name"}
 }
 [ordered]@{sourceHead=(& git -C $PSScriptRoot rev-parse HEAD);security='/GS /guard:cf /CETCOMPAT /DYNAMICBASE';sources=@(Get-ChildItem $PSScriptRoot -File|Where-Object Name -like '*probe*'|ForEach-Object{[ordered]@{file=$_.Name;sha256=(Get-FileHash $_.FullName).Hash.ToLowerInvariant()}});binaries=@(Get-ChildItem $OutDir -Filter *.exe|ForEach-Object{[ordered]@{file=$_.Name;sha256=(Get-FileHash $_.FullName).Hash.ToLowerInvariant()}})}|ConvertTo-Json -Depth 5|Set-Content "$OutDir/build.json" -Encoding utf8
}finally{$env:PATH=$oldPath;$env:INCLUDE=$oldInclude;$env:LIB=$oldLib}
