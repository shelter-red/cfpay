param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$source = Convert-Path -LiteralPath $BuildDirectory
$runDirectory = Join-Path $env:LOCALAPPDATA "cfpay-login-assistant-harness"
[System.IO.Directory]::CreateDirectory($runDirectory) | Out-Null
$monitor = Join-Path $runDirectory "cfpay-wechat-monitor.exe"
$target = Join-Path $runDirectory "cfpay-hook-test-target.exe"
Copy-Item -LiteralPath (Join-Path $source "cfpay-wechat-monitor.exe") -Destination $monitor -Force
Copy-Item -LiteralPath (Join-Path $source "cfpay-hook-test-target.exe") -Destination $target -Force

function Wait-MainWindow([System.Diagnostics.Process]$process) {
    for ($attempt = 0; $attempt -lt 50; $attempt += 1) {
        Start-Sleep -Milliseconds 200
        $process.Refresh()
        if ($process.MainWindowHandle -ne 0) { return }
        if ($process.HasExited) { throw "Login test target exited early" }
    }
    throw "Login test window did not appear"
}

function Run-Mode([string]$mode) {
    $targetProcess = Start-Process -FilePath $target -ArgumentList ("--login-" + $mode) -PassThru
    try {
        Wait-MainWindow $targetProcess
        $test = Start-Process -FilePath $monitor -ArgumentList @("--self-test-login-ui", $mode, $targetProcess.Id) -PassThru
        if (-not $test.WaitForExit(30000)) {
            $test.Kill()
            throw "Login assistant self-test timed out"
        }
        if ($test.ExitCode -ne 0) { throw "Login assistant self-test failed: $($test.ExitCode)" }
        if ($mode -in @("button", "direct")) {
            $eventName = "Local\CfpayLoginButtonClicked-" + $targetProcess.Id
            $clicked = [System.Threading.EventWaitHandle]::OpenExisting($eventName)
            try {
                if (-not $clicked.WaitOne(5000)) { throw "Login button was not invoked" }
            } finally {
                $clicked.Dispose()
            }
        }
        if ($mode -eq "direct") {
            $proxyEventName = "Local\CfpayProxyButtonClicked-" + $targetProcess.Id
            $proxy = [System.Threading.EventWaitHandle]::OpenExisting($proxyEventName)
            try {
                if ($proxy.WaitOne(0)) { throw "Proxy settings region was invoked" }
            } finally {
                $proxy.Dispose()
            }
        }
        if ($mode -eq "switch") {
            $switchEventName = "Local\CfpayAccountSwitchClicked-" + $targetProcess.Id
            $switched = [System.Threading.EventWaitHandle]::OpenExisting($switchEventName)
            try {
                if (-not $switched.WaitOne(5000)) { throw "Account switch region was not invoked" }
            } finally {
                $switched.Dispose()
            }
        }
        Write-Output (("LOGIN_" + $mode.ToUpperInvariant() + "_TEST=ok"))
    } finally {
        if (-not $targetProcess.HasExited) { $targetProcess.Kill() }
        $targetProcess.WaitForExit()
    }
}

Run-Mode "button"
Run-Mode "direct"
Run-Mode "switch"
Run-Mode "qr"

$testWeixinDirectory = Join-Path $runDirectory "test-weixin"
[System.IO.Directory]::CreateDirectory($testWeixinDirectory) | Out-Null
$testWeixin = Join-Path $testWeixinDirectory "Weixin.exe"
Copy-Item -LiteralPath $target -Destination $testWeixin -Force
$oldProcess = Start-Process -FilePath $testWeixin -PassThru
try {
    Wait-MainWindow $oldProcess
    $restart = Start-Process -FilePath $monitor -ArgumentList @("--self-test-login-restart", ('"' + $testWeixin + '"')) -PassThru
    if (-not $restart.WaitForExit(30000)) {
        $restart.Kill()
        throw "Login restart self-test timed out"
    }
    if ($restart.ExitCode -ne 0) { throw "Login restart self-test failed: $($restart.ExitCode)" }
    $oldProcess.Refresh()
    if (-not $oldProcess.HasExited) { throw "Original test Weixin process is still running" }
    $matching = @(Get-CimInstance Win32_Process -Filter "Name='Weixin.exe'" | Where-Object {
        $_.ExecutablePath -and [String]::Equals($_.ExecutablePath, $testWeixin, [StringComparison]::OrdinalIgnoreCase)
    })
    if ($matching.Count -ne 1) { throw "Restarted test Weixin process was not found" }
    Write-Output "LOGIN_RESTART_TEST=ok"
} finally {
    Get-CimInstance Win32_Process -Filter "Name='Weixin.exe'" -ErrorAction SilentlyContinue | Where-Object {
        $_.ExecutablePath -and [String]::Equals($_.ExecutablePath, $testWeixin, [StringComparison]::OrdinalIgnoreCase)
    } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    if (-not $oldProcess.HasExited) { $oldProcess.Kill() }
}
