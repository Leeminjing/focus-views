param([string]$Source=(Join-Path $PSScriptRoot '../r5-004b/out-final'),[string]$OutDir=(Join-Path $PSScriptRoot 'out'))
$ErrorActionPreference='Stop';$Source=[IO.Path]::GetFullPath($Source);$OutDir=[IO.Path]::GetFullPath($OutDir)
$build=Get-Content -Raw "$Source/build.json"|ConvertFrom-Json
New-Item -ItemType Directory -Path $OutDir -Force|Out-Null
foreach($name in @('manager.exe','controller.exe','fixture.exe','second.exe','kernel.lib','native.obj','store.obj','runtime304.dll','helper.exe')){Copy-Item -LiteralPath "$Source/$name" -Destination "$OutDir/$name" -Force}
$compiler=Join-Path $build.msvc 'bin/Hostx64/x64/cl.exe';$oldPath=$env:PATH;$oldInclude=$env:INCLUDE;$oldLib=$env:LIB
try{
 $env:PATH="$(Split-Path $compiler);$oldPath";$env:INCLUDE=@((Join-Path $build.msvc 'include'),(Join-Path $build.sdk "Include/$($build.sdkVersion)/ucrt"),(Join-Path $build.sdk "Include/$($build.sdkVersion)/shared"),(Join-Path $build.sdk "Include/$($build.sdkVersion)/um"))-join ';';$env:LIB=@((Join-Path $build.msvc 'lib/x64'),(Join-Path $build.sdk "Lib/$($build.sdkVersion)/ucrt/x64"),(Join-Path $build.sdk "Lib/$($build.sdkVersion)/um/x64"))-join ';'
 $flags=@('/nologo','/std:c++20','/EHsc','/W4','/WX','/O2','/MT','/GS','/guard:cf','/utf-8','/DUNICODE','/D_UNICODE','/D_WIN32_WINNT=0x0A00','/DFOCUS_VIEWS_CRASH_EXPERIMENT','/DFOCUS_VIEWS_STOPPOINT_EXPERIMENT','/DFOCUS_VIEWS_IMAGE_RELOCATION_EXPERIMENT','/Zi',"/Fd$OutDir/compiler.pdb")
 & (Join-Path $build.msvc 'bin/Hostx64/x64/ml64.exe') /nologo /c "/Fo$OutDir/runtime-sp.obj" (Join-Path $PSScriptRoot 'runtime-sp.asm');if($LASTEXITCODE -ne 0){throw 'Candidate assembler failed'}
 foreach($name in @('fixture','recorder','abort-helper','abort-runtime','diagnose')){
  $unit=if($name -in @('fixture','recorder')){"$name-client.cpp"}else{"$name.cpp"}
  & $compiler @flags /c "/Fo$OutDir/$name.obj" (Join-Path $PSScriptRoot $unit);if($LASTEXITCODE -ne 0){throw "Diagnostic compile $name"}
  $dll=$name -notin @('abort-helper','diagnose');$target=if($name -in @('fixture','recorder')){"$name-client.dll"}elseif($dll){"$name.dll"}else{"$name.exe"}
  $link=@('/nologo');if($dll){$link+='/LD'};$link+="$OutDir/$name.obj";if($name -in @('fixture','recorder')){$link+=@("$OutDir/store.obj","$OutDir/kernel.lib")}else{$link+="$OutDir/native.obj"};if($name -eq 'abort-runtime'){$link+="$OutDir/runtime-sp.obj"};$link+=@('user32.lib','gdi32.lib','ole32.lib','windowscodecs.lib','bcrypt.lib','imm32.lib','advapi32.lib','dbghelp.lib',"/Fe$OutDir/$target",'/link','/DYNAMICBASE','/NXCOMPAT','/CETCOMPAT','/guard:cf','/INCREMENTAL:NO','/DEBUG:FULL');& $compiler @link;if($LASTEXITCODE -ne 0){throw "Diagnostic link $name"}
 }
 Copy-Item -LiteralPath "$Source/build.json" -Destination "$OutDir/reused-execution-build.json" -Force
 [ordered]@{kind='R5-004c-dual-mode-candidate';msvc=$build.msvc;sdk=$build.sdk;sdkVersion=$build.sdkVersion;sourceHead=(& git -C $PSScriptRoot rev-parse HEAD);recoveryKernelSha256=(Get-FileHash "$OutDir/kernel.lib").Hash.ToLowerInvariant();executionReusedByteIdentical=$true;defaultFlow='deferred';controlFlow='inline:';noSourceExecutionChange=$true;compiler=(Get-Item $compiler).VersionInfo.FileVersion;sourceInputs=@(Get-ChildItem $PSScriptRoot -File|Where-Object Extension -in @('.hpp','.cpp','.asm','.ps1')|ForEach-Object{[ordered]@{file=$_.Name;sha256=(Get-FileHash $_.FullName).Hash.ToLowerInvariant()}});binaries=@(Get-ChildItem $OutDir -File|Where-Object Extension -in @('.exe','.dll')|ForEach-Object{[ordered]@{file=$_.Name;sha256=(Get-FileHash $_.FullName).Hash.ToLowerInvariant()}})}|ConvertTo-Json -Depth 6|Set-Content "$OutDir/build.json" -Encoding utf8
 Copy-Item -LiteralPath "$Source/reuse.json" -Destination "$OutDir/reuse.json" -Force;Write-Output 'R5_004C_CANDIDATE_BUILD_OK'
}finally{$env:PATH=$oldPath;$env:INCLUDE=$oldInclude;$env:LIB=$oldLib}
