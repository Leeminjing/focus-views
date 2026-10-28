# Only a separate operation after the original failure is fixed and all owned
# resources are independently confirmed gone. Never retries a Snapshot.
function Get-CleanupFollowupDecision([bool]$CleanupFailed,[bool]$PhysicalCleanupConfirmed,[string]$OriginalBoot,[string]$CurrentBoot,[bool]$AlreadyAttempted) {
 $sameBoot=$OriginalBoot -ceq $CurrentBoot
 [pscustomobject]@{run=($CleanupFailed -and $PhysicalCleanupConfirmed -and $sameBoot -and -not $AlreadyAttempted);cleanupFailed=$CleanupFailed;physicalCleanupConfirmed=$PhysicalCleanupConfirmed;sameBoot=$sameBoot;alreadyAttempted=$AlreadyAttempted}
}
function Test-DirectWindowFollowup([object]$Pair) {
 if($null -eq $Pair -or -not $Pair.evidenceComplete -or $Pair.cases.Count -ne 2){return $false}
 return @($Pair.cases|Where-Object{-not $_.sameWaitingPattern -or -not $_.installationAttested -or $null -eq $_.supervisor -or -not $_.supervisor.cleanupVerified}).Count -eq 0
}
