# Only a separate operation after the original failure is fixed and all owned
# resources are independently confirmed gone. Never retries a Snapshot.
function Get-CleanupFollowupDecision([bool]$CleanupFailed,[bool]$PhysicalCleanupConfirmed,[object]$OriginalBoot,[object]$CurrentBoot,[bool]$AlreadyAttempted) {
 # ConvertFrom-Json may materialize an ISO timestamp as DateTime. Compare the
 # full UTC ticks, retaining subsecond precision, rather than culture strings.
 $sameBoot=$false
 try{if($null -ne $OriginalBoot -and $null -ne $CurrentBoot){$sameBoot=([DateTimeOffset]$OriginalBoot).UtcTicks -eq ([DateTimeOffset]$CurrentBoot).UtcTicks}}catch{$sameBoot=$false}
 [pscustomobject]@{run=($CleanupFailed -and $PhysicalCleanupConfirmed -and $sameBoot -and -not $AlreadyAttempted);cleanupFailed=$CleanupFailed;physicalCleanupConfirmed=$PhysicalCleanupConfirmed;sameBoot=$sameBoot;alreadyAttempted=$AlreadyAttempted}
}
function Test-DirectWindowFollowup([object]$Pair) {
 if($null -eq $Pair -or -not $Pair.evidenceComplete -or $Pair.cases.Count -ne 2){return $false}
 return @($Pair.cases|Where-Object{-not $_.sameWaitingPattern -or $_.PSObject.Properties.Name -notcontains 'waitingEvidence' -or $_.waitingEvidence.status -ne 'matched' -or $null -eq $_.waitingEvidence.links -or -not $_.installationAttested -or $null -eq $_.supervisor -or -not $_.supervisor.cleanupVerified}).Count -eq 0
}
