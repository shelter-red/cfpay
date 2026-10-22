param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,
    [int]$Iterations = 1
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$env:PYTHONIOENCODING = "utf-8"

$source = Convert-Path -LiteralPath $BuildDirectory
$runDirectory = Join-Path $env:LOCALAPPDATA "cfpay-hook-harness"
[System.IO.Directory]::CreateDirectory($runDirectory) | Out-Null

$dll = Join-Path $runDirectory "cfpay-hook-observer.dll"
$target = Join-Path $runDirectory "cfpay-hook-test-target.exe"
Copy-Item -LiteralPath (Join-Path $source "cfpay-hook-observer.dll") -Destination $dll -Force
Copy-Item -LiteralPath (Join-Path $source "cfpay-hook-test-target.exe") -Destination $target -Force

for ($iteration = 1; $iteration -le $Iterations; $iteration += 1) {
    $output = Join-Path $runDirectory ("observer-output-" + $iteration + ".jsonl")
    if ([System.IO.File]::Exists($output)) {
        [System.IO.File]::Delete($output)
    }

    $process = Start-Process -FilePath $target -ArgumentList @('"' + $dll + '"', '"' + $output + '"') -Wait -PassThru
    Write-Output ("ITERATION=" + $iteration)
    Write-Output ("EXIT_CODE=" + $process.ExitCode)
    Write-Output ("OUTPUT_PATH=" + $output)
    if ($process.ExitCode -ne 0) {
        exit $process.ExitCode
    }

    $records = @(Get-Content -LiteralPath $output -Encoding UTF8 | ForEach-Object { $_ | ConvertFrom-Json })
    $eventCount = @($records | Where-Object { $_.type -eq "event" }).Count
    $accountCount = @($records | Where-Object { $_.type -eq "account" }).Count
    $nicknameCount = @($records | Where-Object { $_.type -eq "nickname" }).Count
    $activeCount = @($records | Where-Object { $_.type -eq "status" -and $_.detailCode -eq "HOOK_ACTIVE" }).Count
    $loginRequiredCount = @($records | Where-Object { $_.type -eq "status" -and $_.detailCode -eq "WEIXIN_LOGIN_REQUIRED" }).Count
    $stoppedCount = @($records | Where-Object { $_.type -eq "status" -and $_.detailCode -eq "HOOK_STOPPED" }).Count
    if ($eventCount -ne 3 -or $accountCount -ne 4 -or $nicknameCount -lt 1 -or $activeCount -ne 1 -or $loginRequiredCount -lt 1 -or
        $stoppedCount -ne 1) {
      throw "Unexpected output counts in iteration $iteration"
    }

    if ($Iterations -eq 1) {
        Get-Content -LiteralPath $output -Encoding UTF8
    }
}

Write-Output ("COMPLETED_ITERATIONS=" + $Iterations)
