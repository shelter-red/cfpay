param([int]$ProcessId = 0)
[Console]::OutputEncoding = [Text.Encoding]::UTF8
if ($ProcessId -gt 0) {
  $process = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
  if ($process) { Write-Output ("PROCESS=" + $process.ProcessName) } else { Write-Output "PROCESS=" }
}
Get-NetTCPConnection -LocalPort 8790 -State Listen -ErrorAction SilentlyContinue |
  Select-Object LocalAddress, LocalPort, OwningProcess |
  Format-Table -AutoSize
