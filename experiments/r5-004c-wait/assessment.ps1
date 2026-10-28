# This is the normal-close test's verdict, not the generic supervisor protocol.
function Get-NormalCloseOutcome([int]$ControllerExit,[object]$Supervisor,[object[]]$Complete) {
 $supervisorPresent=$null -ne $Supervisor -and @('managerExit','protocolPassed','cleanupVerified'|Where-Object{$_ -notin $Supervisor.PSObject.Properties.Name}).Count -eq 0
 $completionPresent=$Complete.Count -eq 1 -and @('nativeCleanupPassed','firstApi','firstError'|Where-Object{$_ -notin $Complete[0].PSObject.Properties.Name}).Count -eq 0
 $evidence=$supervisorPresent -and $completionPresent
 $native=$completionPresent -and $Complete[0].nativeCleanupPassed -eq $true
 $passed=$evidence -and $native -and $ControllerExit -eq 0 -and $Supervisor.managerExit -eq 0 -and
  $Complete[0].firstApi -ceq '' -and $Complete[0].firstError -eq 0 -and
  $Supervisor.protocolPassed -eq $true -and $Supervisor.cleanupVerified -eq $true
 [pscustomobject]@{evidenceComplete=$evidence;nativeCloseObserved=$native;operationPassed=$passed}
}
