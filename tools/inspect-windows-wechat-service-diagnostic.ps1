param(
    [int]$ProcessId = 0,
    [string]$RunDirectory = "$env:LOCALAPPDATA\CFPay\WeChatServiceDiagnostic"
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$targets = @(Get-Process Weixin -ErrorAction SilentlyContinue)
if ($ProcessId -gt 0) {
    $targets = @($targets | Where-Object { $_.Id -eq $ProcessId })
}

foreach ($target in $targets) {
    Write-Output ("TARGET_PID=" + $target.Id)
    try {
        $module = $target.Modules |
            Where-Object { $_.ModuleName -ieq "cfpay-hook-observer.dll" } |
            Select-Object -First 1
        if ($module) {
            Write-Output "OBSERVER_LOADED=1"
            Write-Output ("OBSERVER_PATH=" + $module.FileName)
        } else {
            Write-Output "OBSERVER_LOADED=0"
        }
    } catch {
        Write-Output ("OBSERVER_ERROR=" + $_.Exception.Message)
    }
}

$hosts = @(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -eq "cfpay-wechat-host.exe" -and $_.CommandLine -match "--run-weixin"
})
foreach ($hostProcess in $hosts) {
    Write-Output ("HOST_PID=" + $hostProcess.ProcessId)
    Write-Output ("HOST_COMMAND=" + $hostProcess.CommandLine)
}
if ($hosts.Count -eq 0) { Write-Output "HOST_PID=" }

$outputPath = Join-Path $RunDirectory "diagnostic-output.jsonl"
$errorPath = Join-Path $RunDirectory "diagnostic-error.log"
Write-Output ("OUTPUT_PATH=" + $outputPath)
if (Test-Path -LiteralPath $outputPath) {
    Get-Content -LiteralPath $outputPath -Encoding UTF8 -Tail 8 | ForEach-Object {
        Write-Output ("OUTPUT=" + $_)
    }
} else {
    Write-Output "OUTPUT_MISSING=1"
}

Write-Output ("ERROR_PATH=" + $errorPath)
if (Test-Path -LiteralPath $errorPath) {
    Get-Content -LiteralPath $errorPath -Encoding UTF8 -Tail 8 | ForEach-Object {
        Write-Output ("ERROR=" + $_)
    }
} else {
    Write-Output "ERROR_MISSING=1"
}
