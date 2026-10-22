param(
    [Parameter(Mandatory = $true)]
    [string]$ExecutablePath
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [Text.Encoding]::UTF8

$executable = Convert-Path -LiteralPath $ExecutablePath
if ([System.IO.Path]::GetFileName($executable) -ine "Weixin.exe") {
    throw "ExecutablePath must point to Weixin.exe"
}

$processes = @(Get-CimInstance Win32_Process -Filter "Name='Weixin.exe'")
foreach ($process in $processes) {
    if ($process.ExecutablePath -and $process.ExecutablePath -ine $executable) {
        throw "A Weixin.exe process uses an unexpected path"
    }
}
foreach ($process in $processes) {
    Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue
}
for ($attempt = 0; $attempt -lt 100; $attempt += 1) {
    if (-not (Get-Process Weixin -ErrorAction SilentlyContinue)) {
        Write-Output "WEIXIN_STOPPED=1"
        exit 0
    }
    Start-Sleep -Milliseconds 200
}
throw "Weixin.exe processes did not stop"
