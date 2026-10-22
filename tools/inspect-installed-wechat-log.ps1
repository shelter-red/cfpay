param(
    [string]$InstallDirectory = "$env:LOCALAPPDATA\CFPay\WeChatMonitor",
    [int]$Tail = 160
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [Text.Encoding]::UTF8

$configPath = Join-Path $InstallDirectory "config.json"
if (-not (Test-Path -LiteralPath $configPath)) { throw "Installed monitor config is missing" }
$config = Get-Content -LiteralPath $configPath -Raw -Encoding UTF8 | ConvertFrom-Json
Write-Output ("DEBUG_LOGGING=" + [bool]$config.debugLogging)
Write-Output ("LOG_PATH=" + [string]$config.logPath)

$logPath = [Environment]::ExpandEnvironmentVariables([string]$config.logPath)
if (-not [System.IO.Path]::IsPathRooted($logPath)) {
    $logPath = Join-Path $InstallDirectory $logPath
}
Write-Output ("RESOLVED_LOG_PATH=" + $logPath)
if (-not (Test-Path -LiteralPath $logPath)) {
    Write-Output "LOG_MISSING=1"
    exit 0
}

Write-Output "LOG_BEGIN"
Get-Content -LiteralPath $logPath -Tail $Tail -Encoding UTF8
Write-Output "LOG_END"
