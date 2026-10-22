param(
  [string]$ScriptPath
)
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$configuration = Get-NetIPConfiguration |
  Where-Object {
    $_.IPv4DefaultGateway -and
    $_.IPv4Address -and
    $_.NetAdapter.Status -eq "Up" -and
    $_.InterfaceAlias -notmatch "vEthernet|WSL|Tunnel|Meta|VPN|Loopback" -and
    $_.IPv4Address.IPAddress -notmatch "^(127\.|169\.254\.|198\.(18|19)\.)"
  } |
  Select-Object -First 1
if (-not $configuration) { throw "No LAN IPv4 address with a default gateway was found" }
$lanIp = $configuration.IPv4Address.IPAddress
$url = "http://${lanIp}:8790"
$listeners = Get-NetTCPConnection -LocalPort 8790 -State Listen -ErrorAction SilentlyContinue
foreach ($listener in $listeners) {
  Stop-Process -Id $listener.OwningProcess -Force -ErrorAction SilentlyContinue
}
if ($listeners) { Start-Sleep -Seconds 1 }
$node = (Get-Command node.exe -ErrorAction Stop).Source
$commandLine = '"' + $node + '" "' + $ScriptPath + '"'
$result = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $commandLine }
Write-Output ("SERVER_RETURN=" + $result.ReturnValue)
Write-Output ("SERVER_PID=" + $result.ProcessId)
Start-Sleep -Seconds 3
& curl.exe --noproxy "*" --connect-timeout 10 -sS -o NUL -w "HTTP_STATUS=%{http_code}`n" $url
Write-Output ("LAN_URL=" + $url)
$openLine = 'cmd.exe /c start "" "' + $url + '"'
$opened = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $openLine }
Write-Output ("OPEN_RETURN=" + $opened.ReturnValue)
Write-Output ("OPEN_PID=" + $opened.ProcessId)
