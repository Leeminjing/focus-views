param([string]$OutDir=(Join-Path $PSScriptRoot 'out'),[string]$Source=(Join-Path $PSScriptRoot '../r5-004b/out-final'),[string]$ReuseCandidate='')
$ErrorActionPreference='Stop';$OutDir=[IO.Path]::GetFullPath($OutDir)
if($ReuseCandidate){New-Item -ItemType Directory -Path $OutDir -ErrorAction Stop|Out-Null;Get-ChildItem -LiteralPath $ReuseCandidate -File|ForEach-Object{Copy-Item -LiteralPath $_.FullName -Destination $OutDir}}
else{& "$PSScriptRoot/../r5-004c/build.ps1" -OutDir $OutDir -Source $Source}
$build=Get-Content -Raw "$OutDir/build.json"|ConvertFrom-Json
$compiler=Join-Path $build.msvc 'bin/Hostx64/x64/cl.exe';$oldPath=$env:PATH;$oldInclude=$env:INCLUDE;$oldLib=$env:LIB
try{
 $env:PATH="$(Split-Path $compiler);$oldPath";$env:INCLUDE=@((Join-Path $build.msvc 'include'),(Join-Path $build.sdk "Include/$($build.sdkVersion)/ucrt"),(Join-Path $build.sdk "Include/$($build.sdkVersion)/shared"),(Join-Path $build.sdk "Include/$($build.sdkVersion)/um"))-join ';';$env:LIB=@((Join-Path $build.msvc 'lib/x64'),(Join-Path $build.sdk "Lib/$($build.sdkVersion)/ucrt/x64"),(Join-Path $build.sdk "Lib/$($build.sdkVersion)/um/x64"))-join ';'
 $flags=@('/nologo','/std:c++20','/EHsc','/W4','/WX','/O2','/MT','/GS','/guard:cf','/utf-8','/DUNICODE','/D_UNICODE','/D_WIN32_WINNT=0x0A00','/DFOCUS_VIEWS_CRASH_EXPERIMENT','/DFOCUS_VIEWS_STOPPOINT_EXPERIMENT','/DFOCUS_VIEWS_IMAGE_RELOCATION_EXPERIMENT','/Zi',"/Fd$OutDir/compiler.pdb")
 foreach($name in @('diagnose','trim-client','close-probe','profile')){
  & $compiler @flags /c "/Fo$OutDir/$name.obj" "$PSScriptRoot/$name.cpp";if($LASTEXITCODE){throw "Compile $name failed"}
  $link=@('/nologo');if($name -eq 'trim-client'){$link+='/LD'};$link+="$OutDir/$name.obj";$link+=@("$OutDir/store.obj","$OutDir/kernel.lib",'user32.lib','gdi32.lib','ole32.lib','windowscodecs.lib','bcrypt.lib','imm32.lib','advapi32.lib','dbghelp.lib');$target=if($name -eq 'trim-client'){'trim-client.dll'}else{"$name.exe"}
  & $compiler @link "/Fe$OutDir/$target" /link /DYNAMICBASE /NXCOMPAT /CETCOMPAT /guard:cf /INCREMENTAL:NO /DEBUG:FULL;if($LASTEXITCODE){throw "Link $name failed"}
 }
 $sources=@('diagnose.cpp','trim-client.cpp','observe.hpp','close-probe.cpp','profile.cpp','profile.hpp','build.ps1','../r5-003/clients/restart.hpp','../r5-003/instance-check.ps1','../r5-004/verification.ps1')
 [ordered]@{time=(Get-Date -Format o);sourceHead=(& git -C $PSScriptRoot rev-parse HEAD);boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime;msvc=$build.msvc;sdk=$build.sdk;sdkVersion=$build.sdkVersion;sources=@($sources|ForEach-Object{[ordered]@{file=$_;sha256=(Get-FileHash "$PSScriptRoot/$_").Hash.ToLowerInvariant()}});binaries=@(Get-ChildItem $OutDir -File|Where-Object Extension -in @('.exe','.dll','.lib')|ForEach-Object{[ordered]@{file=$_.Name;sha256=(Get-FileHash $_.FullName).Hash.ToLowerInvariant()}});cleanupEntryUnchanged=$true;kernelSha256=(Get-FileHash "$OutDir/kernel.lib").Hash.ToLowerInvariant()}|ConvertTo-Json -Depth 6|Set-Content "$OutDir/wait-build.json" -Encoding utf8
}finally{$env:PATH=$oldPath;$env:INCLUDE=$oldInclude;$env:LIB=$oldLib}
