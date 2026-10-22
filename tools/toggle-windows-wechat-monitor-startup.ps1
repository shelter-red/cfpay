param(
    [string]$InstallDirectory = "$env:LOCALAPPDATA\CFPay\WeChatMonitor",
    [switch]$Enable
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$startupDirectory = [Environment]::GetFolderPath("Startup")
$activePath = Join-Path $startupDirectory "CFPay WeChat Monitor.lnk"
$disabledPath = $activePath + ".disabled"

if ($Enable) {
    $monitorPath = Join-Path $InstallDirectory "cfpay-wechat-monitor.exe"
    $configPath = Join-Path $InstallDirectory "config.json"
    if (-not (Test-Path -LiteralPath $monitorPath)) { throw "Installed monitor is missing" }
    if (-not (Test-Path -LiteralPath $configPath)) { throw "Installed monitor config is missing" }
    if (Test-Path -LiteralPath $disabledPath) { Remove-Item -LiteralPath $disabledPath -Force }
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($activePath)
    $shortcut.TargetPath = $monitorPath
    $shortcut.Arguments = '--tray --config "' + $configPath + '"'
    $shortcut.WorkingDirectory = $InstallDirectory
    $shortcut.WindowStyle = 7
    $shortcut.Save()
    Write-Output "STARTUP_ENABLED=1"
    exit 0
}

if (Test-Path -LiteralPath $activePath) {
    Move-Item -LiteralPath $activePath -Destination $disabledPath -Force
}
Write-Output "STARTUP_ENABLED=0"
