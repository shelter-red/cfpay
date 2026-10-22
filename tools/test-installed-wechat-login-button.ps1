param(
    [Parameter(Mandatory = $true)]
    [int]$ProcessId,
    [string]$InstallDirectory = "$env:LOCALAPPDATA\CFPay\WeChatMonitor",
    [string]$MonitorPath = "",
    [ValidateSet("button", "direct", "switch")]
    [string]$Mode = "button"
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [Text.Encoding]::UTF8

$monitor = if ($MonitorPath) { Convert-Path -LiteralPath $MonitorPath } else { Join-Path $InstallDirectory "cfpay-wechat-monitor.exe" }
if (-not (Test-Path -LiteralPath $monitor)) { throw "Installed monitor is missing" }
$test = Start-Process -FilePath $monitor -ArgumentList @("--self-test-login-ui", $Mode, $ProcessId) -Wait -PassThru
Write-Output ("LOGIN_" + $Mode.ToUpperInvariant() + "_SELF_TEST_EXIT=" + $test.ExitCode)
if ($test.ExitCode -ne 0) { throw ("C++ login " + $Mode + " self-test failed") }
Write-Output ("LOGIN_" + $Mode.ToUpperInvariant() + "_SELF_TEST=ok")
