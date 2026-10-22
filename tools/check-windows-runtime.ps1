param([string]$Url)
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$node = Get-Command node.exe -ErrorAction SilentlyContinue
if ($node) { Write-Output ("NODE=" + $node.Source) } else { Write-Output "NODE=" }
$internet = Get-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings'
Write-Output ("PROXY_ENABLE=" + $internet.ProxyEnable)
Write-Output ("PROXY_SERVER=" + $internet.ProxyServer)
Write-Output ("PROXY_OVERRIDE=" + $internet.ProxyOverride)
if ($Url) {
  & curl.exe --noproxy "*" --connect-timeout 5 -sS -o NUL -w "CURL_STATUS=%{http_code}`n" $Url
}
