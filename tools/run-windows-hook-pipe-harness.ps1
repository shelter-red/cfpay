param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,
    [int]$Iterations = 10
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$env:PYTHONIOENCODING = "utf-8"

$source = Convert-Path -LiteralPath $BuildDirectory
$runDirectory = Join-Path $env:LOCALAPPDATA "cfpay-hook-pipe-harness"
[System.IO.Directory]::CreateDirectory($runDirectory) | Out-Null

$dll = Join-Path $runDirectory "cfpay-hook-observer.dll"
$target = Join-Path $runDirectory "cfpay-hook-test-target.exe"
$hostProgram = Join-Path $runDirectory "cfpay-wechat-host.exe"
Copy-Item -LiteralPath (Join-Path $source "cfpay-hook-observer.dll") -Destination $dll -Force
Copy-Item -LiteralPath (Join-Path $source "cfpay-hook-test-target.exe") -Destination $target -Force
Copy-Item -LiteralPath (Join-Path $source "cfpay-wechat-host.exe") -Destination $hostProgram -Force

for ($iteration = 1; $iteration -le $Iterations; $iteration += 1) {
    $targetProcess = Start-Process -FilePath $target -ArgumentList "--wait-pipe-injection" -PassThru
    Start-Sleep -Milliseconds 200
    $hostLines = @(& $hostProgram --test-pipe-run $targetProcess.Id $dll)
    $hostExitCode = $LASTEXITCODE
    if ($hostExitCode -ne 0) {
        if (-not $targetProcess.HasExited) { $targetProcess.Kill() }
        throw "Host pipe mode failed in iteration $iteration with exit code $hostExitCode"
    }

    $targetProcess.WaitForExit(15000) | Out-Null
    $targetProcess.Refresh()
    if (-not $targetProcess.HasExited -or $targetProcess.ExitCode -ne 0) {
        if (-not $targetProcess.HasExited) { $targetProcess.Kill() }
        throw "Pipe target failed in iteration $iteration"
    }

    $records = @($hostLines | Where-Object { $_ -match '^\{' } | ForEach-Object { $_ | ConvertFrom-Json })
    $eventCount = @($records | Where-Object { $_.type -eq "event" }).Count
    $accountCount = @($records | Where-Object { $_.type -eq "account" }).Count
    $activeCount = @($records | Where-Object { $_.detailCode -eq "HOOK_ACTIVE" }).Count
    $loginRequiredCount = @($records | Where-Object { $_.detailCode -eq "WEIXIN_LOGIN_REQUIRED" }).Count
    $stoppedCount = @($records | Where-Object { $_.detailCode -eq "HOOK_STOPPED" }).Count
    $injectedCount = @($records | Where-Object { $_.detailCode -eq "TEST_DLL_INJECTED" }).Count
    $coverageWarnings = @($records | Where-Object { $_.detailCode -eq "THREAD_COVERAGE_INCOMPLETE" }).Count
    if ($eventCount -ne 3 -or $accountCount -ne 4 -or $activeCount -ne 1 -or $loginRequiredCount -lt 1 -or
        $stoppedCount -ne 1 -or
        $injectedCount -ne 1 -or $coverageWarnings -ne 0) {
        $hostLines | ForEach-Object { Write-Output $_ }
        throw ("Unexpected pipe output in iteration $iteration events=$eventCount accounts=$accountCount active=$activeCount stopped=$stoppedCount injected=$injectedCount coverageWarnings=$coverageWarnings")
    }
    if ($Iterations -eq 1) {
        $hostLines | ForEach-Object { Write-Output $_ }
    }
    Write-Output ("ITERATION=" + $iteration + " PID=" + $targetProcess.Id + " EXIT_CODE=" + $targetProcess.ExitCode)
}

Write-Output ("COMPLETED_ITERATIONS=" + $Iterations)

$reactivationTarget = Start-Process -FilePath $target -ArgumentList "--wait-pipe-reactivation" -PassThru
Start-Sleep -Milliseconds 200
for ($cycle = 1; $cycle -le 2; $cycle += 1) {
    $cycleLines = @(& $hostProgram --test-pipe-run $reactivationTarget.Id $dll)
    if ($LASTEXITCODE -ne 0) {
        if (-not $reactivationTarget.HasExited) { $reactivationTarget.Kill() }
        throw "Observer reactivation failed in cycle $cycle"
    }
    $cycleRecords = @($cycleLines | Where-Object { $_ -match '^\{' } | ForEach-Object { $_ | ConvertFrom-Json })
    $cycleEvents = @($cycleRecords | Where-Object { $_.type -eq "event" }).Count
    $cycleAccounts = @($cycleRecords | Where-Object { $_.type -eq "account" }).Count
    $cycleActive = @($cycleRecords | Where-Object { $_.detailCode -eq "HOOK_ACTIVE" }).Count
    $cycleLoginRequired = @($cycleRecords | Where-Object { $_.detailCode -eq "WEIXIN_LOGIN_REQUIRED" }).Count
    $cycleStopped = @($cycleRecords | Where-Object { $_.detailCode -eq "HOOK_STOPPED" }).Count
    $cycleCoverageWarnings = @($cycleRecords | Where-Object { $_.detailCode -eq "THREAD_COVERAGE_INCOMPLETE" }).Count
    if ($cycleEvents -ne 3 -or $cycleAccounts -ne 4 -or $cycleActive -ne 1 -or $cycleLoginRequired -lt 1 -or
        $cycleStopped -ne 1 -or
        $cycleCoverageWarnings -ne 0) {
        $cycleLines | ForEach-Object { Write-Output $_ }
        if (-not $reactivationTarget.HasExited) { $reactivationTarget.Kill() }
        throw ("Unexpected reactivation output in cycle $cycle events=$cycleEvents accounts=$cycleAccounts active=$cycleActive stopped=$cycleStopped coverageWarnings=$cycleCoverageWarnings")
    }
    Write-Output ("REACTIVATION_CYCLE=" + $cycle)
}
$reactivationTarget.WaitForExit(15000) | Out-Null
$reactivationTarget.Refresh()
if (-not $reactivationTarget.HasExited -or $reactivationTarget.ExitCode -ne 0) {
    if (-not $reactivationTarget.HasExited) { $reactivationTarget.Kill() }
    throw "Reactivation target failed"
}
Write-Output "REACTIVATION_CYCLES=2"

& $hostProgram --run-weixin $PID $dll
if ($LASTEXITCODE -ne 13) {
    throw "Production loader accepted a non-Weixin process"
}
Write-Output "NON_WEIXIN_PROCESS_REJECTED=1"

& $hostProgram --stop-weixin $PID
if ($LASTEXITCODE -ne 15) {
    throw "Production stop command accepted a non-Weixin process"
}
Write-Output "NON_WEIXIN_STOP_REJECTED=1"
