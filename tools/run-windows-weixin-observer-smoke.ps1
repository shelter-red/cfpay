param(
    [Parameter(Mandatory = $true)]
    [string]$HostProgram,
    [Parameter(Mandatory = $true)]
    [string]$ObserverDll,
    [Parameter(Mandatory = $true)]
    [int]$ProcessId,
    [ValidateRange(1, 60)]
    [int]$DurationSeconds = 10,
    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

function Read-SharedText {
    param([Parameter(Mandatory = $true)][string]$Path)

    $stream = [System.IO.FileStream]::new(
        $Path,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::ReadWrite
    )
    $reader = [System.IO.StreamReader]::new($stream)
    try {
        return $reader.ReadToEnd()
    } finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

$sourceHost = Convert-Path -LiteralPath $HostProgram
$sourceDll = Convert-Path -LiteralPath $ObserverDll
$runDirectory = Join-Path $env:LOCALAPPDATA ("CFPay\wechat-smoke-" + [DateTime]::UtcNow.ToString("yyyyMMdd-HHmmss"))
[System.IO.Directory]::CreateDirectory($runDirectory) | Out-Null
$localHost = Join-Path $runDirectory "cfpay-wechat-host.exe"
$localDll = Join-Path $runDirectory "cfpay-hook-observer.dll"
$rawOutput = Join-Path $runDirectory "observer.raw.jsonl"
$rawError = Join-Path $runDirectory "observer.stderr.log"
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $runDirectory "observer.sanitized.jsonl"
}

Copy-Item -LiteralPath $sourceHost -Destination $localHost -Force
Copy-Item -LiteralPath $sourceDll -Destination $localDll -Force

$target = Get-Process -Id $ProcessId -ErrorAction Stop
if ($target.ProcessName -ne "Weixin") {
    throw "Target process is not Weixin.exe"
}
$modules = @($target.Modules | ForEach-Object { $_.ModuleName })
if ($modules -notcontains "Weixin.dll") {
    throw "Target process does not contain Weixin.dll"
}
$observerInitiallyLoaded = $modules -contains "cfpay-hook-observer.dll"

$runner = $null
$hookActive = $false
$stopAttempted = $false
try {
    $runner = Start-Process -FilePath $localHost `
        -ArgumentList @("--run-weixin", $ProcessId, $localDll) `
        -RedirectStandardOutput $rawOutput `
        -RedirectStandardError $rawError `
        -WindowStyle Hidden `
        -PassThru

    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($runner.HasExited) { break }
        if (Test-Path -LiteralPath $rawOutput) {
            $content = Read-SharedText -Path $rawOutput
            if ($content.Contains('"detailCode":"HOOK_ACTIVE"')) {
                $hookActive = $true
                break
            }
        }
        Start-Sleep -Milliseconds 200
    }
    if (-not $hookActive) {
        throw "Observer did not report HOOK_ACTIVE within 15 seconds"
    }

    Start-Sleep -Seconds $DurationSeconds
    $stopOutput = @(& $localHost --stop-weixin $ProcessId 2>&1)
    $stopExitCode = $LASTEXITCODE
    $stopAttempted = $true
    if ($stopExitCode -ne 0) {
        throw ("Observer stop command failed: " + ($stopOutput -join " "))
    }

    if (-not $runner.WaitForExit(15000)) {
        throw "Streaming host did not exit after observer cleanup"
    }
} finally {
    if ($runner -ne $null -and -not $stopAttempted) {
        $loaded = @(Get-Process -Id $ProcessId -Module -ErrorAction SilentlyContinue |
            Where-Object { $_.ModuleName -eq "cfpay-hook-observer.dll" }).Count -gt 0
        if ($loaded) {
            & $localHost --stop-weixin $ProcessId 2>&1 | Out-Null
        }
    }
    if ($runner -ne $null -and -not $runner.HasExited) {
        Stop-Process -Id $runner.Id -Force -ErrorAction SilentlyContinue
    }
}

$rawLines = @([System.IO.File]::ReadAllLines($rawOutput))
$records = @()
foreach ($line in $rawLines) {
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    try { $records += ($line | ConvertFrom-Json) } catch {}
}
$statuses = @($records | Where-Object { $_.type -eq "status" })
$eventCount = @($records | Where-Object { $_.type -eq "event" }).Count
$accountCount = @($records | Where-Object { $_.type -eq "account" }).Count
$activeCount = @($statuses | Where-Object { $_.detailCode -eq "HOOK_ACTIVE" }).Count
$stoppedCount = @($statuses | Where-Object { $_.detailCode -eq "HOOK_STOPPED" }).Count
$targetAlive = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
$observerLoaded = @(Get-Process -Id $ProcessId -Module -ErrorAction SilentlyContinue |
    Where-Object { $_.ModuleName -eq "cfpay-hook-observer.dll" }).Count -gt 0

if ($activeCount -lt 1 -or $stoppedCount -lt 1 -or $null -eq $targetAlive) {
    throw "Smoke verification did not observe active, stopped, and live-target conditions"
}

$sanitized = [System.Collections.Generic.List[string]]::new()
foreach ($status in $statuses) {
    $sanitized.Add(($status | ConvertTo-Json -Compress))
}
$summary = [ordered]@{
    type = "summary"
    durationSeconds = $DurationSeconds
    observerInitiallyLoaded = $observerInitiallyLoaded
    statusCount = $statuses.Count
    eventCount = $eventCount
    accountCount = $accountCount
    weixinAlive = ($null -ne $targetAlive)
    observerModuleLoaded = $observerLoaded
}
$sanitized.Add(($summary | ConvertTo-Json -Compress))
$outputDirectory = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($outputDirectory)) {
    [System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
}
[System.IO.File]::WriteAllLines($OutputPath, $sanitized, [System.Text.UTF8Encoding]::new($false))
[System.IO.File]::Delete($rawOutput)
if (Test-Path -LiteralPath $rawError) { [System.IO.File]::Delete($rawError) }

Write-Output "SMOKE_STATUS=PASS"
Write-Output ("PROCESS_ID=" + $ProcessId)
Write-Output ("DURATION_SECONDS=" + $DurationSeconds)
Write-Output ("OBSERVER_INITIALLY_LOADED=" + $observerInitiallyLoaded)
Write-Output ("EVENT_COUNT=" + $eventCount)
Write-Output ("ACCOUNT_COUNT=" + $accountCount)
Write-Output ("WEIXIN_ALIVE=" + ($null -ne $targetAlive))
Write-Output ("OBSERVER_MODULE_LOADED=" + $observerLoaded)
Write-Output ("SANITIZED_OUTPUT=" + $OutputPath)
