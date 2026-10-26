function Test-CheckpointQueueEvidence([object[]]$Rows,[object[]]$Events,[string]$View){
 $captures=@($Rows|Where-Object{$_.type -eq 'raw_capture' -and $_.view -eq $View})
 $guards=@($Events|Where-Object event -eq 'checkpoint_guard_final')
 # A resource rejection before raw_capture must not invent a checkpoint.
 # Every real capture needs exactly one owner-thread, empty-queue observation.
 $passed=$guards.Count -eq $captures.Count -and @($guards|Where-Object{$_.queued -or $_.inputClosed -ne 1}).Count -eq 0
 [pscustomobject]@{passed=$passed;captures=$captures.Count;guards=$guards.Count}
}
