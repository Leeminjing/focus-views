param([string]$MsvcRoot, [string]$SdkRoot, [string]$SdkVersion,
    [string]$OutDir = (Join-Path $PSScriptRoot 'out'))
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot '../r1/build.ps1') -MsvcRoot $MsvcRoot -SdkRoot $SdkRoot -SdkVersion $SdkVersion -OutDir $OutDir -SourceDir $PSScriptRoot
