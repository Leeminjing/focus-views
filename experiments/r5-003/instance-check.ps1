function Test-DifferentProcessInstance([object]$Before,[object]$After){
 if($null -eq $Before -or $null -eq $After){return $false}
 foreach($value in @($Before,$After)){
  if($value.PSObject.Properties.Name -notcontains 'pid' -or $value.PSObject.Properties.Name -notcontains 'created'){return $false}
  if([uint64]$value.pid -eq 0 -or [uint64]$value.created -eq 0){return $false}
 }
 return [uint64]$Before.pid -ne [uint64]$After.pid -or [uint64]$Before.created -ne [uint64]$After.created
}
