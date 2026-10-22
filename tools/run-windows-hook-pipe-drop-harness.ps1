param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,
    [int]$Iterations = 10
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$source = Convert-Path -LiteralPath $BuildDirectory
$runDirectory = Join-Path $env:LOCALAPPDATA "cfpay-hook-pipe-drop-harness"
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
    & $hostProgram --test-pipe-drop $targetProcess.Id $dll
    if ($LASTEXITCODE -ne 0) {
        if (-not $targetProcess.HasExited) { $targetProcess.Kill() }
        throw "Pipe drop command failed in iteration $iteration"
    }

    $targetProcess.WaitForExit(15000) | Out-Null
    $targetProcess.Refresh()
    if (-not $targetProcess.HasExited -or $targetProcess.ExitCode -ne 0) {
        if (-not $targetProcess.HasExited) { $targetProcess.Kill() }
        throw "Observer did not recover from pipe drop in iteration $iteration"
    }
    Write-Output ("ITERATION=" + $iteration + " PID=" + $targetProcess.Id + " EXIT_CODE=" + $targetProcess.ExitCode)
}

Write-Output ("COMPLETED_ITERATIONS=" + $Iterations)
