param(
    [Parameter(Mandatory = $true)]
    [string]$HostProgram,
    [Parameter(Mandatory = $true)]
    [string]$WeixinDll
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$env:PYTHONIOENCODING = "utf-8"

$sourceHost = Convert-Path -LiteralPath $HostProgram
$targetDll = Convert-Path -LiteralPath $WeixinDll
$runDirectory = Join-Path $env:LOCALAPPDATA "cfpay-wechat-host"
[System.IO.Directory]::CreateDirectory($runDirectory) | Out-Null
$localHost = Join-Path $runDirectory "cfpay-wechat-host.exe"
Copy-Item -LiteralPath $sourceHost -Destination $localHost -Force

& $localHost --inspect-weixin $targetDll
exit $LASTEXITCODE
