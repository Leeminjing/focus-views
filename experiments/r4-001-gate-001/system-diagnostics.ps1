param([datetime]$Since=(Get-Date).AddHours(-1),[Parameter(Mandatory)][string]$Output)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$logs=[Collections.Generic.List[object]]::new()
foreach($name in @('Microsoft-Windows-Windows Defender/Operational','Microsoft-Windows-CodeIntegrity/Operational','Microsoft-Windows-Security-Mitigations/UserMode','Microsoft-Windows-Security-Mitigations/KernelMode','Microsoft-Windows-AppLocker/EXE and DLL','Security')){
 try{
  $items=@(Get-WinEvent -FilterHashtable @{LogName=$name;StartTime=$Since} -MaxEvents 200 -ErrorAction Stop)
  $own=@($items|Where-Object{$_.ToXml() -match 'focus-views|r305a\.exe|runtime304\.dll|handle-probe\.exe'}|ForEach-Object{[pscustomobject]@{time=$_.TimeCreated;id=$_.Id;provider=$_.ProviderName;xml=$_.ToXml()}})
  $logs.Add([pscustomobject]@{log=$name;status='queried';boundedEvents=$items.Count;matchingOwnEvents=$own})
 }catch{$logs.Add([pscustomobject]@{log=$name;status='unavailable_or_no_events';error=$_.ToString()})}
}
$images=@('r3-005b-001','r3-005c-001','r4-001')|ForEach-Object{
 $path=Join-Path $PSScriptRoot "../$_/out/r305a.exe";$acl=Get-Acl -LiteralPath $path
 [pscustomobject]@{image=[IO.Path]::GetFullPath($path);sha256=(Get-FileHash $path).Hash.ToLowerInvariant();owner=$acl.Owner;sddl=$acl.Sddl}
}
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
[ordered]@{time=(Get-Date -Format o);since=$Since;os=[Environment]::OSVersion.VersionString;callerPid=$PID;callerSid=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value;elevated=$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator);images=@($images);nativeLogs=$logs;systemSettingsChanged=$false}|ConvertTo-Json -Depth 8|Set-Content $Output -Encoding utf8
