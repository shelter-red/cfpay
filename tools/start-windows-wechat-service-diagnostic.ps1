param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,
    [Parameter(Mandatory = $true)]
    [int]$ProcessId,
    [string]$RunDirectory = "$env:LOCALAPPDATA\CFPay\WeChatServiceDiagnostic"
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$source = Convert-Path -LiteralPath $BuildDirectory
$sourceHost = Join-Path $source "cfpay-wechat-host.exe"
$sourceDll = Join-Path $source "cfpay-hook-diagnostic.dll"
if (-not (Test-Path -LiteralPath $sourceHost)) { throw "Diagnostic host is missing" }
if (-not (Test-Path -LiteralPath $sourceDll)) { throw "Diagnostic DLL is missing" }

$target = Get-CimInstance Win32_Process -Filter "ProcessId=$ProcessId"
if (-not $target -or $target.Name -ine "Weixin.exe") { throw "Target process is not Weixin.exe" }

try {
    $loadedObserver = Get-Process -Id $ProcessId -Module -ErrorAction Stop |
        Where-Object { $_.ModuleName -ieq "cfpay-hook-observer.dll" } |
        Select-Object -First 1
    if ($loadedObserver) { throw "Target already contains an observer DLL" }
} catch {
    if ($_.Exception.Message -eq "Target already contains an observer DLL") { throw }
}

[System.IO.Directory]::CreateDirectory($RunDirectory) | Out-Null
$hostProgram = Join-Path $RunDirectory "cfpay-wechat-host.exe"
$observerDll = Join-Path $RunDirectory "cfpay-hook-observer.dll"
$outputPath = Join-Path $RunDirectory "diagnostic-output.jsonl"
$errorPath = Join-Path $RunDirectory "diagnostic-error.log"

Copy-Item -LiteralPath $sourceHost -Destination $hostProgram -Force
Copy-Item -LiteralPath $sourceDll -Destination $observerDll -Force
foreach ($path in @($outputPath, $errorPath)) {
    if ([System.IO.File]::Exists($path)) { [System.IO.File]::Delete($path) }
}

$commandLine = 'cmd.exe /d /s /c ""' + $hostProgram + '" --run-weixin ' + $ProcessId +
    ' "' + $observerDll + '" 1>"' + $outputPath + '" 2>"' + $errorPath + '""'
$created = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $commandLine }
if ($created.ReturnValue -ne 0 -or $created.ProcessId -eq 0) {
    throw ("Diagnostic host launch failed; win32=" + $created.ReturnValue)
}
$hostProcessId = [int]$created.ProcessId

$active = $false
for ($attempt = 0; $attempt -lt 300; $attempt += 1) {
    Start-Sleep -Milliseconds 100
    $process = Get-Process -Id $hostProcessId -ErrorAction SilentlyContinue
    if ($null -eq $process) { break }
    if (Test-Path -LiteralPath $outputPath) {
        $active = [bool](Select-String -LiteralPath $outputPath -SimpleMatch '"detailCode":"DIAGNOSTIC_ACTIVE"' -Quiet)
        if ($active) { break }
    }
}

if (-not $active) {
    $errorText = ""
    if (Test-Path -LiteralPath $errorPath) {
        $errorValue = Get-Content -LiteralPath $errorPath -Raw -Encoding UTF8
        if ($null -ne $errorValue) { $errorText = $errorValue.Trim() }
    }
    $outputText = ""
    if (Test-Path -LiteralPath $outputPath) {
        $outputValue = Get-Content -LiteralPath $outputPath -Raw -Encoding UTF8
        if ($null -ne $outputValue) { $outputText = $outputValue.Trim() }
    }
    $hostExited = $null -eq (Get-Process -Id $hostProcessId -ErrorAction SilentlyContinue)
    throw ("Diagnostic observer did not become active; hostExited=" + $hostExited +
           "; output=" + $outputText + "; error=" + $errorText)
}

Write-Output "DIAGNOSTIC_STARTED=1"
Write-Output ("HOST_PID=" + $hostProcessId)
Write-Output ("TARGET_PID=" + $ProcessId)
Write-Output ("OUTPUT_PATH=" + $outputPath)
Write-Output ("ERROR_PATH=" + $errorPath)
