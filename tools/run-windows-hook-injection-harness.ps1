param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,
    [int]$Iterations = 10
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$env:PYTHONIOENCODING = "utf-8"

$source = Convert-Path -LiteralPath $BuildDirectory
$runDirectory = Join-Path $env:LOCALAPPDATA "cfpay-hook-injection-harness"
[System.IO.Directory]::CreateDirectory($runDirectory) | Out-Null

$dll = Join-Path $runDirectory "cfpay-hook-observer.dll"
$target = Join-Path $runDirectory "cfpay-hook-test-target.exe"
$hostProgram = Join-Path $runDirectory "cfpay-wechat-host.exe"
Copy-Item -LiteralPath (Join-Path $source "cfpay-hook-observer.dll") -Destination $dll -Force
Copy-Item -LiteralPath (Join-Path $source "cfpay-hook-test-target.exe") -Destination $target -Force
Copy-Item -LiteralPath (Join-Path $source "cfpay-wechat-host.exe") -Destination $hostProgram -Force

for ($iteration = 1; $iteration -le $Iterations; $iteration += 1) {
    $output = Join-Path $runDirectory ("observer-output-" + $iteration + ".jsonl")
    if ([System.IO.File]::Exists($output)) {
        [System.IO.File]::Delete($output)
    }

    $targetProcess = Start-Process -FilePath $target -ArgumentList @("--wait-injection", $output) -PassThru
    Start-Sleep -Milliseconds 200
    & $hostProgram --test-inject $targetProcess.Id $dll
    $hostExitCode = $LASTEXITCODE
    if ($hostExitCode -ne 0) {
        if (-not $targetProcess.HasExited) { $targetProcess.Kill() }
        throw "Host injection failed in iteration $iteration with exit code $hostExitCode"
    }

    $targetProcess.WaitForExit(15000) | Out-Null
    $targetProcess.Refresh()
    if (-not $targetProcess.HasExited -or $targetProcess.ExitCode -ne 0) {
        if (-not $targetProcess.HasExited) { $targetProcess.Kill() }
        throw "Target failed in iteration $iteration"
    }

    if (-not (Test-Path -LiteralPath $output)) {
        throw "Observer output was not created in iteration $iteration"
    }
    $records = @(Get-Content -LiteralPath $output -Encoding UTF8 | ForEach-Object { $_ | ConvertFrom-Json })
    $eventCount = @($records | Where-Object { $_.type -eq "event" }).Count
    $activeCount = @($records | Where-Object { $_.detailCode -eq "HOOK_ACTIVE" }).Count
    $stoppedCount = @($records | Where-Object { $_.detailCode -eq "HOOK_STOPPED" }).Count
    if ($eventCount -ne 3 -or $activeCount -ne 1 -or $stoppedCount -ne 1) {
        throw "Unexpected observer output in iteration $iteration"
    }
    Write-Output ("ITERATION=" + $iteration + " PID=" + $targetProcess.Id + " EXIT_CODE=" + $targetProcess.ExitCode)
}

Write-Output ("COMPLETED_ITERATIONS=" + $Iterations)

& $hostProgram --test-inject $PID $dll
if ($LASTEXITCODE -ne 11) {
    throw "Restricted loader accepted a non-test process"
}
Write-Output "NON_TEST_PROCESS_REJECTED=1"
