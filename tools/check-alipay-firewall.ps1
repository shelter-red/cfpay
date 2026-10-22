[Console]::OutputEncoding = [Text.Encoding]::UTF8
$rule = Get-NetFirewallRule -DisplayName "CFPay Alipay Test 8790" -ErrorAction SilentlyContinue
if ($rule) {
  Write-Output ("FIREWALL_FOUND=" + $rule.Enabled)
  $rule | Get-NetFirewallPortFilter | Select-Object Protocol, LocalPort | Format-Table -AutoSize
  $rule | Get-NetFirewallAddressFilter | Select-Object RemoteAddress | Format-Table -AutoSize
} else {
  Write-Output "FIREWALL_FOUND=False"
}
