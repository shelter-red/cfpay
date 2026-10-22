param(
    [string]$InstallRoot = ""
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$processes = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @("Weixin.exe", "WeChatAppEx.exe", "cfpay-wechat-monitor.exe")
})
$byId = @{}
foreach ($process in $processes) { $byId[[int]$process.ProcessId] = $process }

function Get-Role([object]$process) {
    $command = [string]$process.CommandLine
    if ($process.Name -eq "cfpay-wechat-monitor.exe") {
        if ($command -match "--internal-host") { return "cfpay-hook-host" }
        return "cfpay-monitor"
    }
    if ($process.Name -eq "WeChatAppEx.exe") {
        if ($command -match "--wmpf-appid=([^\s]+)") { return "wmpf-app:" + $Matches[1] }
        if ($command -match "--wmpf-render-type=([^\s]+)") { return "wmpf-render:" + $Matches[1] }
        if ($command -match "--type=([^\s]+)") { return "wmpf:" + $Matches[1] }
        return "wmpf-main"
    }
    if ($command -match "--type=([^\s]+)") { return "weixin:" + $Matches[1] }
    return "weixin-main"
}

function Get-KeyModules([int]$processId) {
    try {
        return @(
            Get-Process -Id $processId -Module -ErrorAction Stop |
                Where-Object {
                    $_.ModuleName -in @("Weixin.dll", "wxpublic.dll", "wxutility.dll", "wxocr.dll", "cfpay-hook-observer.dll")
                } |
                Select-Object -ExpandProperty ModuleName -Unique |
                Sort-Object
        )
    } catch {
        return @()
    }
}

foreach ($process in ($processes | Sort-Object CreationDate, ProcessId)) {
    $parent = $byId[[int]$process.ParentProcessId]
    $parentRole = if ($null -eq $parent) { "external" } else { Get-Role $parent }
    $modules = Get-KeyModules ([int]$process.ProcessId)
    $observerLoaded = $modules -contains "cfpay-hook-observer.dll"
    $pathMatches = $true
    if ($InstallRoot -and $process.Name -eq "Weixin.exe") {
        $pathMatches = ([string]$process.ExecutablePath).StartsWith($InstallRoot, [System.StringComparison]::OrdinalIgnoreCase)
    }
    Write-Output (
        "PROCESS=" + $process.ProcessId +
        "|PPID=" + $process.ParentProcessId +
        "|ROLE=" + (Get-Role $process) +
        "|PARENT_ROLE=" + $parentRole +
        "|CREATED=" + $process.CreationDate +
        "|PATH_MATCH=" + $pathMatches +
        "|OBSERVER=" + $observerLoaded +
        "|MODULES=" + ($modules -join ",")
    )
}
