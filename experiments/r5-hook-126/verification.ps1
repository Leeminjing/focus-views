function Assert-InputDiagnosticBuild([string]$OutDir) {
 $b=Get-Content -Raw (Join-Path $OutDir 'build.json')|ConvertFrom-Json
 if($b.PSObject.Properties.Name -notcontains 'inputInstallDiagnostics' -or -not $b.inputInstallDiagnostics){throw 'Explicit diagnostic build required'}
 if($b.inputSourceSha256 -ne (Get-FileHash "$PSScriptRoot/../r3-004/input.cpp").Hash -or $b.diagnosticHeaderSha256 -ne (Get-FileHash "$PSScriptRoot/install-diagnostics.hpp").Hash){throw 'Diagnostic inputs differ from the frozen build'}
}
