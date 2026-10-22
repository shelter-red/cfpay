param(
  [string]$RuleScript,
  [string]$MarkerPath
)
[Console]::OutputEncoding = [Text.Encoding]::UTF8
if (Test-Path $MarkerPath) { Remove-Item $MarkerPath -Force }
$arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $RuleScript + '" -MarkerPath "' + $MarkerPath + '"'
Start-Process powershell.exe -Verb RunAs -Wait -ArgumentList $arguments
if (Test-Path $MarkerPath) {
  Write-Output "FIREWALL_RULE=ok"
} else {
  Write-Output "FIREWALL_RULE=not-confirmed"
}
