param(
    [Parameter(Mandatory = $true)]
    [string]$ObserverPath,
    [int]$ProcessId = 0,
    [double]$Duration = 30,
    [string]$ExpectedVersion = "4.1.15.13",
    [switch]$AllowKnownCrashRisk,
    [switch]$CheckOnly
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$env:PYTHONIOENCODING = "utf-8"

$python = "C:\Python314\python.exe"
if (-not (Test-Path -LiteralPath $python)) {
    throw "Windows Python was not found at $python"
}

$observer = Convert-Path -LiteralPath $ObserverPath
$script = [System.IO.Path]::ChangeExtension($observer, ".js")
if (-not (Test-Path -LiteralPath $script)) {
    throw "Frida script was not found at $script"
}

if ($CheckOnly) {
    Write-Output "Observer: $observer"
    Write-Output "Frida script: $script"
    & $python $observer --self-test
    exit $LASTEXITCODE
}

if (-not $AllowKnownCrashRisk) {
    throw "Runtime attach is blocked: unloading Frida 17.19.0 crashed Weixin 4.1.15.13 with 0xc0000005. Use -CheckOnly only."
}

if ($ProcessId -le 0) {
    $candidates = @()
    foreach ($process in Get-Process Weixin -ErrorAction SilentlyContinue) {
        try {
            $hasMainModule = $process.Modules |
                Where-Object { $_.ModuleName -ieq "Weixin.dll" } |
                Select-Object -First 1
            if ($hasMainModule) {
                $candidates += $process
            }
        } catch {
            Write-Output ("Skipping PID " + $process.Id + ": " + $_.Exception.Message)
        }
    }

    $selected = $candidates |
        Sort-Object @{ Expression = { if ($_.MainWindowHandle -ne 0) { 0 } else { 1 } } }, Id |
        Select-Object -First 1
    if (-not $selected) {
        throw "No Weixin process containing Weixin.dll was found"
    }
    $ProcessId = $selected.Id
}

$target = Get-Process -Id $ProcessId -ErrorAction Stop
$weixinModule = $target.Modules |
    Where-Object { $_.ModuleName -ieq "Weixin.dll" } |
    Select-Object -First 1
if (-not $weixinModule) {
    throw "PID $ProcessId does not contain Weixin.dll"
}

$actualVersion = $weixinModule.FileVersionInfo.FileVersion
if ($actualVersion -ne $ExpectedVersion) {
    throw "Unsupported Weixin.dll version $actualVersion; expected $ExpectedVersion"
}

Write-Output "Target PID: $ProcessId"
Write-Output ("Weixin.dll: " + $weixinModule.FileName)
Write-Output "Version: $actualVersion"
Write-Output "Duration: $Duration seconds"
& $python $observer --pid $ProcessId --duration $Duration --script $script
exit $LASTEXITCODE
