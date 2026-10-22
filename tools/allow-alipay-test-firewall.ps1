param([string]$MarkerPath)
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$ruleName = "CFPay Alipay Test 8790"
$existing = Get-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue
if (-not $existing) {
  New-NetFirewallRule -DisplayName $ruleName -Direction Inbound -Action Allow -Protocol TCP -LocalPort 8790 -Profile Private -RemoteAddress LocalSubnet | Out-Null
}
Set-Content -Path $MarkerPath -Value "ok" -Encoding ASCII
