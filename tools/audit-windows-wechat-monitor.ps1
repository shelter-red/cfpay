param(
    [string]$InstallDirectory = "$env:LOCALAPPDATA\CFPay\WeChatMonitor",
    [string]$PackageDirectory = ""
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

function Safe-CommandLine([string]$value) {
    if (-not $value) { return "" }
    return $value -replace '(?i)(monitorSecret|secret)(=|\s+)[^\s"]+', '$1$2***'
}

$processes = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @("cfpay-wechat-monitor.exe", "node.exe") -and
    ($_.Name -eq "cfpay-wechat-monitor.exe" -or $_.CommandLine -match 'wechat-monitor')
})
foreach ($process in $processes) {
    Write-Output ("PROCESS=" + $process.ProcessId + "|" + $process.Name + "|" + $process.ExecutablePath + "|" +
        (Safe-CommandLine $process.CommandLine))
}
if ($processes.Count -eq 0) { Write-Output "PROCESS=" }

$files = if (Test-Path -LiteralPath $InstallDirectory) {
    @(Get-ChildItem -LiteralPath $InstallDirectory -File | Select-Object -ExpandProperty Name | Sort-Object)
} else {
    @()
}
Write-Output ("INSTALL_DIRECTORY=" + $InstallDirectory)
Write-Output ("INSTALL_FILES=" + ($files -join ","))

$configPath = Join-Path $InstallDirectory "config.json"
if (Test-Path -LiteralPath $configPath) {
    $config = Get-Content -LiteralPath $configPath -Raw -Encoding UTF8 | ConvertFrom-Json
    Write-Output ("CONFIG_BASE_URL=" + $config.baseUrl)
    Write-Output ("CONFIG_MONITOR_ID=" + $config.monitorId)
    Write-Output ("CONFIG_SECRET_CONFIGURED=" + [bool]($config.monitorSecret -and $config.monitorSecret.Length -ge 32))
    Write-Output ("CONFIG_RECEIVER_CONFIGURED=" + [bool]$config.expectedReceiverUsername)
    Write-Output ("CONFIG_WEIXIN_EXE=" + $config.weixinExecutablePath)
    Write-Output ("CONFIG_OBSERVER_DLL=" + $config.observerDllPath)
    Write-Output ("CONFIG_HAS_LEGACY_HOST=" + [bool]$config.hostPath)
    Write-Output ("CONFIG_HAS_LEGACY_LOCATOR=" + [bool]$config.locatorScriptPath)
} else {
    Write-Output "CONFIG_MISSING=1"
}

$statePath = Join-Path $env:LOCALAPPDATA "CFPay\wechat-monitor-state.json"
if (Test-Path -LiteralPath $statePath) {
    $state = Get-Content -LiteralPath $statePath -Raw -Encoding UTF8 | ConvertFrom-Json
    Write-Output ("STATE_PENDING=" + @($state.pending).Count)
    Write-Output ("STATE_DELIVERED=" + @($state.delivered.PSObject.Properties).Count)
    Write-Output ("STATE_RECEIVER_CANDIDATES=" + @($state.receiverCandidates).Count)
} else {
    Write-Output "STATE_MISSING=1"
}

$startupDirectory = [Environment]::GetFolderPath("Startup")
$programsDirectory = [Environment]::GetFolderPath("Programs")
$shell = New-Object -ComObject WScript.Shell
foreach ($entry in @(
    @{ Name = "STARTUP"; Path = (Join-Path $startupDirectory "CFPay WeChat Monitor.lnk") },
    @{ Name = "SETUP"; Path = (Join-Path $programsDirectory "CFPay WeChat Monitor Setup.lnk") }
)) {
    if (Test-Path -LiteralPath $entry.Path) {
        $shortcut = $shell.CreateShortcut($entry.Path)
        Write-Output ($entry.Name + "_SHORTCUT=" + $shortcut.TargetPath + "|" + $shortcut.Arguments)
    } else {
        Write-Output ($entry.Name + "_SHORTCUT=")
    }
}
$disabledStartupPath = Join-Path $startupDirectory "CFPay WeChat Monitor.lnk.disabled"
Write-Output ("STARTUP_DISABLED=" + (Test-Path -LiteralPath $disabledStartupPath))

$weixinProcesses = @(Get-Process Weixin -ErrorAction SilentlyContinue)
$loaded = 0
foreach ($process in $weixinProcesses) {
    try {
        $observerModule = $process.Modules | Where-Object { $_.ModuleName -ieq "cfpay-hook-observer.dll" } | Select-Object -First 1
        if ($observerModule) {
            $loaded += 1
            Write-Output ("OBSERVER_LOADED_PID=" + $process.Id)
            Write-Output ("OBSERVER_LOADED_PATH=" + $observerModule.FileName)
            Write-Output ("OBSERVER_LOADED_SHA256=" + (Get-FileHash -LiteralPath $observerModule.FileName -Algorithm SHA256).Hash.ToLowerInvariant())
        }
    } catch {
    }
}
Write-Output ("WEIXIN_PROCESS_COUNT=" + $weixinProcesses.Count)
Write-Output ("OBSERVER_LOADED_COUNT=" + $loaded)

if ($PackageDirectory) {
    $packageObserver = Join-Path (Convert-Path -LiteralPath $PackageDirectory) "cfpay-hook-observer.dll"
    Write-Output ("PACKAGE_OBSERVER_SHA256=" + (Get-FileHash -LiteralPath $packageObserver -Algorithm SHA256).Hash.ToLowerInvariant())
}
