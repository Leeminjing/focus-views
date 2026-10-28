param([string]$OutDir=(Join-Path $PSScriptRoot 'out'),[string]$Config=(Join-Path $PSScriptRoot '../r5-004c-wait/results/wait001/build.json'))
$ErrorActionPreference='Stop';$b=Get-Content -Raw $Config|ConvertFrom-Json;$OutDir=[IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Path $OutDir -ErrorAction Stop|Out-Null
$compiler=Join-Path $b.msvc 'bin/Hostx64/x64/cl.exe';$oldPath=$env:PATH;$oldInclude=$env:INCLUDE;$oldLib=$env:LIB
try{
 $env:PATH="$(Split-Path $compiler);$oldPath";$env:INCLUDE=@((Join-Path $b.msvc 'include'),(Join-Path $b.sdk "Include/$($b.sdkVersion)/ucrt"),(Join-Path $b.sdk "Include/$($b.sdkVersion)/shared"),(Join-Path $b.sdk "Include/$($b.sdkVersion)/um"))-join ';';$env:LIB=@((Join-Path $b.msvc 'lib/x64'),(Join-Path $b.sdk "Lib/$($b.sdkVersion)/ucrt/x64"),(Join-Path $b.sdk "Lib/$($b.sdkVersion)/um/x64"))-join ';'
 & $compiler /nologo /std:c++20 /EHsc /W4 /WX /O2 /MT /GS /guard:cf /utf-8 /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /Zi "/Fd$OutDir/compiler.pdb" "/Fo$OutDir/probe.obj" "$PSScriptRoot/probe.cpp" "/Fe$OutDir/hook-probe.exe" user32.lib bcrypt.lib /link /DYNAMICBASE /NXCOMPAT /CETCOMPAT /guard:cf /INCREMENTAL:NO /DEBUG:FULL
 if($LASTEXITCODE){throw 'Native hook install probe build failed'}
 [ordered]@{time=(Get-Date -Format o);sourceHead=(& git -C $PSScriptRoot rev-parse HEAD);boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime;msvc=$b.msvc;sdk=$b.sdk;sdkVersion=$b.sdkVersion;compiler=(Get-Item $compiler).VersionInfo.FileVersion;sourceSha256=(Get-FileHash "$PSScriptRoot/probe.cpp").Hash.ToLowerInvariant();exeSha256=(Get-FileHash "$OutDir/hook-probe.exe").Hash.ToLowerInvariant();mitigations='/GS /guard:cf /CETCOMPAT /DYNAMICBASE /NXCOMPAT';sourceInputUnchanged=$true}|ConvertTo-Json -Depth 5|Set-Content "$OutDir/build.json" -Encoding utf8
}finally{$env:PATH=$oldPath;$env:INCLUDE=$oldInclude;$env:LIB=$oldLib}
