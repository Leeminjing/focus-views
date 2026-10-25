param([Parameter(Mandatory)][string]$ResultsDir)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
if(Test-Path $ResultsDir){throw 'Fresh inventory directory required'};New-Item -ItemType Directory $ResultsDir|Out-Null
$commands=[Collections.Generic.List[object]]::new()
foreach($provider in @('Microsoft-Windows-Kernel-Process','Microsoft-Windows-Kernel-Memory','Microsoft-Windows-Kernel-Object')){
 & logman query providers $provider *> (Join-Path $ResultsDir ($provider+'.txt'));$code=$LASTEXITCODE
 $commands.Add([pscustomobject]@{tool='logman';arguments=@('query','providers',$provider);exitCode=$code;available=($code -eq 0)})
}
& wevtutil gp Microsoft-Windows-Kernel-Process /ge:true /gm:true *> (Join-Path $ResultsDir kernel-process-metadata.txt);$commands.Add([pscustomobject]@{tool='wevtutil';arguments=@('gp','Microsoft-Windows-Kernel-Process','/ge:true','/gm:true');exitCode=$LASTEXITCODE})
& whoami /groups *> (Join-Path $ResultsDir caller-groups.txt)
& whoami /priv *> (Join-Path $ResultsDir caller-privileges.txt)
$identity=[Security.Principal.WindowsIdentity]::GetCurrent();$principal=[Security.Principal.WindowsPrincipal]::new($identity)
$image=Join-Path $PSScriptRoot '../r3-005b-001/out/r305a.exe';$acl=Get-Acl $image
[ordered]@{time=(Get-Date -Format o);baseCommit=(& git -C $PSScriptRoot rev-parse HEAD);os=[Environment]::OSVersion.VersionString;callerSid=$identity.User.Value;elevated=$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator);performanceLogUserTokenMember=$principal.IsInRole([Security.Principal.SecurityIdentifier]::new('S-1-5-32-559'));fixture=[IO.Path]::GetFullPath($image);fixtureSha256=(Get-FileHash $image).Hash.ToLowerInvariant();fixtureSddl=$acl.Sddl;commands=$commands;systemSettingsChanged=$false}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $ResultsDir inventory.json) -Encoding utf8
