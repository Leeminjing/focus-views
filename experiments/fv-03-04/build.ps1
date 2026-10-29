param([string]$Source='C:/fv-c/cm/candidate',[string]$OutDir='C:/fv-live/bin')
$ErrorActionPreference='Stop'
$Source=[IO.Path]::GetFullPath($Source);$OutDir=[IO.Path]::GetFullPath($OutDir)
if(Test-Path "$OutDir/build.json"){throw 'Use a fresh build directory; tested binaries must not be overwritten'}
$build=Get-Content -Raw "$Source/build.json"|ConvertFrom-Json
New-Item -ItemType Directory -Path $OutDir -Force|Out-Null
$reuse=@('manager.exe','controller.exe','fixture.exe','second.exe','kernel.lib','native.obj','store.obj','runtime304.dll','helper.exe','abort-helper.exe','abort-runtime.dll','diagnose.exe')
foreach($name in $reuse){Copy-Item -LiteralPath "$Source/$name" -Destination "$OutDir/$name"}
$compiler=Join-Path $build.msvc 'bin/Hostx64/x64/cl.exe'
$oldPath=$env:PATH;$oldInclude=$env:INCLUDE;$oldLib=$env:LIB
$repo=(& git -C $PSScriptRoot rev-parse --show-toplevel)
function SourceInputs{
 $paths=@(& git -C $repo ls-files '*.hpp' '*.cpp' '*.inl' '*.asm' '*.ps1')
 $paths+=@(Get-ChildItem $PSScriptRoot -File|ForEach-Object{"experiments/fv-03-04/$($_.Name)"})
 @($paths|Sort-Object -Unique|ForEach-Object{[ordered]@{path=$_;sha256=(Get-FileHash "$repo/$_").Hash.ToLowerInvariant()}})
}
$sourceInputs=SourceInputs
try{
 $env:PATH="$(Split-Path $compiler);$oldPath"
 $env:INCLUDE=@("$($build.msvc)/include","$($build.sdk)/Include/$($build.sdkVersion)/ucrt","$($build.sdk)/Include/$($build.sdkVersion)/shared","$($build.sdk)/Include/$($build.sdkVersion)/um")-join ';'
 $env:LIB=@("$($build.msvc)/lib/x64","$($build.sdk)/Lib/$($build.sdkVersion)/ucrt/x64","$($build.sdk)/Lib/$($build.sdkVersion)/um/x64")-join ';'
 $flags=@('/nologo','/std:c++20','/EHsc','/W4','/WX','/O2','/MT','/GS','/guard:cf','/utf-8','/DUNICODE','/D_UNICODE','/D_WIN32_WINNT=0x0A00','/Zi',"/Fd$OutDir/compiler.pdb")
 $macros=@('/DFOCUS_VIEWS_CRASH_EXPERIMENT','/DFOCUS_VIEWS_STOPPOINT_EXPERIMENT','/DFOCUS_VIEWS_IMAGE_RELOCATION_EXPERIMENT','/DFOCUS_VIEWS_RESTORED_INTERACTION_EXPERIMENT')
 foreach($unit in @('fixture-client','recorder-client','auxiliary')){
  $extra=if($unit -eq 'auxiliary'){@()}else{$macros}
  & $compiler @flags @extra /c "/Fo$OutDir/$unit.obj" "$PSScriptRoot/$unit.cpp"
  if($LASTEXITCODE){throw "Compile failed: $unit"}
  $link=@('/nologo');if($unit -ne 'auxiliary'){$link+='/LD'}
  $link+="$OutDir/$unit.obj";if($unit -ne 'auxiliary'){$link+="$OutDir/store.obj"};$link+="$OutDir/kernel.lib"
  $target=if($unit -eq 'auxiliary'){"$unit.exe"}else{"$unit.dll"}
  $link+=@('user32.lib','gdi32.lib','ole32.lib','windowscodecs.lib','bcrypt.lib','imm32.lib','advapi32.lib','dbghelp.lib',"/Fe$OutDir/$target",'/link','/DYNAMICBASE','/NXCOMPAT','/CETCOMPAT','/guard:cf','/INCREMENTAL:NO','/DEBUG:FULL')
  & $compiler @link;if($LASTEXITCODE){throw "Link failed: $unit"}
 }
 if(($sourceInputs|ConvertTo-Json -Compress) -cne ((SourceInputs)|ConvertTo-Json -Compress)){throw 'Source changed while building; discard this candidate for acceptance'}
 Copy-Item "$Source/build.json" "$OutDir/reused-execution-build.json"
 [ordered]@{kind='FV-03-FV-04-restored-interaction';sourceHead=(& git -C $repo rev-parse HEAD);msvc=$build.msvc;sdk=$build.sdk;sdkVersion=$build.sdkVersion;reusedSource=$Source;reused=@($reuse|ForEach-Object{[ordered]@{file=$_;sha256=(Get-FileHash "$OutDir/$_").Hash.ToLowerInvariant();unchanged=((Get-FileHash "$OutDir/$_").Hash -eq (Get-FileHash "$Source/$_").Hash)}});inputs=$sourceInputs;binaries=@(Get-ChildItem $OutDir -File|Where-Object Extension -in @('.exe','.dll','.lib','.obj')|ForEach-Object{[ordered]@{file=$_.Name;sha256=(Get-FileHash $_.FullName).Hash.ToLowerInvariant()}})}|ConvertTo-Json -Depth 8|Set-Content "$OutDir/build.json" -Encoding utf8
}finally{$env:PATH=$oldPath;$env:INCLUDE=$oldInclude;$env:LIB=$oldLib}
