param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDirectory,
    [Parameter(Mandatory = $true)]
    [string]$ConfigPath,
    [string]$InstallDirectory = "$env:LOCALAPPDATA\CFPay\WeChatMonitor",
    [switch]$CheckOnly,
    [switch]$SkipStartup,
    [switch]$StartNow,
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$startupDirectory = [Environment]::GetFolderPath("Startup")
$shortcutPath = Join-Path $startupDirectory "CFPay WeChat Monitor.lnk"
$programsDirectory = [Environment]::GetFolderPath("Programs")
$setupShortcutPath = Join-Path $programsDirectory "CFPay WeChat Monitor Setup.lnk"

if ($Uninstall) {
    if (Test-Path -LiteralPath $shortcutPath) { Remove-Item -LiteralPath $shortcutPath -Force }
    if (Test-Path -LiteralPath $setupShortcutPath) { Remove-Item -LiteralPath $setupShortcutPath -Force }
    if (Test-Path -LiteralPath $InstallDirectory) { Remove-Item -LiteralPath $InstallDirectory -Recurse -Force }
    if (Test-Path -LiteralPath $shortcutPath) { throw "Startup shortcut still exists after uninstall" }
    if (Test-Path -LiteralPath $setupShortcutPath) { throw "Setup shortcut still exists after uninstall" }
    if (Test-Path -LiteralPath $InstallDirectory) { throw "Install directory still exists after uninstall" }
    Write-Output "UNINSTALLED=1"
    exit 0
}

$package = Convert-Path -LiteralPath $PackageDirectory
$sourceConfigPath = Convert-Path -LiteralPath $ConfigPath
$requiredFiles = @(
    "cfpay-wechat-monitor.exe"
)
foreach ($filename in $requiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $package $filename))) { throw "Package file is missing: $filename" }
}

$runningMonitors = @(Get-CimInstance Win32_Process -Filter "Name='cfpay-wechat-monitor.exe'" -ErrorAction SilentlyContinue |
    Where-Object { $_.ExecutablePath -and $_.ExecutablePath.StartsWith($InstallDirectory, [System.StringComparison]::OrdinalIgnoreCase) })
$weixinProcesses = @(Get-Process Weixin -ErrorAction SilentlyContinue)
$binaryUpgradeRequired = $false
foreach ($filename in $requiredFiles) {
    $sourceFile = Join-Path $package $filename
    $destinationFile = Join-Path $InstallDirectory $filename
    if (-not (Test-Path -LiteralPath $destinationFile)) {
        $binaryUpgradeRequired = $true
        continue
    }
    $sourceHash = (Get-FileHash -LiteralPath $sourceFile -Algorithm SHA256).Hash
    $destinationHash = (Get-FileHash -LiteralPath $destinationFile -Algorithm SHA256).Hash
    if ($sourceHash -ne $destinationHash) { $binaryUpgradeRequired = $true }
}
$cleanUpgradeReady = $runningMonitors.Count -eq 0 -and (-not $binaryUpgradeRequired -or $weixinProcesses.Count -eq 0)

$config = Get-Content -LiteralPath $sourceConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
$existingConfigPath = Join-Path $InstallDirectory "config.json"
if (-not $config.expectedReceiverUsername -and (Test-Path -LiteralPath $existingConfigPath)) {
    $existingConfig = Get-Content -LiteralPath $existingConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($existingConfig.expectedReceiverUsername -match '^wxid_[A-Za-z0-9_-]+$') {
        $config.expectedReceiverUsername = $existingConfig.expectedReceiverUsername
    }
}
if ($config.baseUrl -notmatch '^https://') { throw "baseUrl must use HTTPS" }
if (-not $config.monitorId -or $config.monitorId -notmatch '^[A-Za-z0-9._-]{1,80}$') { throw "monitorId is invalid" }
if (-not $config.monitorSecret -or $config.monitorSecret.Length -lt 32) { throw "monitorSecret must contain at least 32 characters" }
if ($config.expectedReceiverUsername -and $config.expectedReceiverUsername -notmatch '^wxid_[A-Za-z0-9_-]+$') {
    throw "expectedReceiverUsername must be a concrete wxid value"
}
if (-not $config.weixinExecutablePath -or -not (Test-Path -LiteralPath $config.weixinExecutablePath)) { throw "weixinExecutablePath is invalid" }
if ([System.IO.Path]::GetFileName($config.weixinExecutablePath) -ine "Weixin.exe") { throw "weixinExecutablePath must point to Weixin.exe" }
if (-not $config.weixinDllPath -or -not (Test-Path -LiteralPath $config.weixinDllPath)) { throw "weixinDllPath is invalid" }

if ($CheckOnly) {
    Write-Output ("RUNNING_MONITOR_COUNT=" + $runningMonitors.Count)
    Write-Output ("WEIXIN_PROCESS_COUNT=" + $weixinProcesses.Count)
    Write-Output ("BINARY_UPGRADE_REQUIRED=" + [int]$binaryUpgradeRequired)
    Write-Output ("CLEAN_UPGRADE_READY=" + [int]$cleanUpgradeReady)
    Write-Output "INSTALL_CHECK=ok"
    exit 0
}

if ($runningMonitors.Count -gt 0) {
    throw "CFPAY_MONITOR_STILL_RUNNING"
}
if ($binaryUpgradeRequired -and $weixinProcesses.Count -gt 0) {
    throw "CLEAN_WEIXIN_EXIT_REQUIRED"
}

[System.IO.Directory]::CreateDirectory($InstallDirectory) | Out-Null
foreach ($filename in $requiredFiles) {
    $sourceFile = Join-Path $package $filename
    $destinationFile = Join-Path $InstallDirectory $filename
    if (Test-Path -LiteralPath $destinationFile) {
        $sourceHash = (Get-FileHash -LiteralPath $sourceFile -Algorithm SHA256).Hash
        $destinationHash = (Get-FileHash -LiteralPath $destinationFile -Algorithm SHA256).Hash
        if ($sourceHash -eq $destinationHash) { continue }
    }
    Copy-Item -LiteralPath $sourceFile -Destination $destinationFile -Force
}
$config | Add-Member -NotePropertyName observerDllPath -NotePropertyValue (Join-Path $InstallDirectory "cfpay-hook-observer.dll") -Force
$config.PSObject.Properties.Remove("hostPath")
$config.PSObject.Properties.Remove("locatorScriptPath")
$config | Add-Member -NotePropertyName statePath -NotePropertyValue (Join-Path $env:LOCALAPPDATA "CFPay\wechat-monitor-state.json") -Force
$installedConfigPath = $existingConfigPath
$configJson = $config | ConvertTo-Json -Depth 8
[System.IO.File]::WriteAllText($installedConfigPath, $configJson, (New-Object System.Text.UTF8Encoding($false)))

$monitorPath = Join-Path $InstallDirectory "cfpay-wechat-monitor.exe"

$shell = New-Object -ComObject WScript.Shell
$setupShortcut = $shell.CreateShortcut($setupShortcutPath)
$setupShortcut.TargetPath = $monitorPath
$setupShortcut.Arguments = '--config-window --config "' + $installedConfigPath + '"'
$setupShortcut.WorkingDirectory = $InstallDirectory
$setupShortcut.WindowStyle = 1
$setupShortcut.Save()

if (-not $SkipStartup) {
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = $monitorPath
    $shortcut.Arguments = '--tray --config "' + $installedConfigPath + '"'
    $shortcut.WorkingDirectory = $InstallDirectory
    $shortcut.WindowStyle = 7
    $shortcut.Save()
}

if ($StartNow) {
    Start-Process -FilePath $monitorPath -WorkingDirectory $InstallDirectory `
        -ArgumentList ('--tray --config "' + $installedConfigPath + '"')
}

foreach ($filename in $requiredFiles + @("config.json")) {
    if (-not (Test-Path -LiteralPath (Join-Path $InstallDirectory $filename))) { throw "Installed file is missing: $filename" }
}
if (-not $SkipStartup -and -not (Test-Path -LiteralPath $shortcutPath)) { throw "Startup shortcut was not created" }
if (-not (Test-Path -LiteralPath $setupShortcutPath)) { throw "Setup shortcut was not created" }
Write-Output ("INSTALLED=" + $InstallDirectory)
Write-Output ("STARTUP=" + (-not $SkipStartup))
Write-Output ("SETUP_SHORTCUT=" + $setupShortcutPath)
