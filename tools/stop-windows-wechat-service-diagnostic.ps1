param(
    [Parameter(Mandatory = $true)]
    [int]$ProcessId,
    [string]$RunDirectory = "$env:LOCALAPPDATA\CFPay\WeChatServiceDiagnostic"
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$hostProgram = Join-Path $RunDirectory "cfpay-wechat-host.exe"
if (-not (Test-Path -LiteralPath $hostProgram)) { throw "Diagnostic host is missing" }

& $hostProgram --stop-weixin $ProcessId
$exitCode = $LASTEXITCODE
Write-Output ("DIAGNOSTIC_STOP_EXIT=" + $exitCode)
if ($exitCode -ne 0) { throw "Diagnostic observer did not stop cleanly" }
