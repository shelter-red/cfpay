param(
    [Parameter(Mandatory = $true)]
    [string]$ExecutablePath
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$executable = Convert-Path -LiteralPath $ExecutablePath
if ([System.IO.Path]::GetFileName($executable) -ine "Weixin.exe") {
    throw "ExecutablePath must point to Weixin.exe"
}

$processes = @(Get-CimInstance Win32_Process -Filter "Name='Weixin.exe'")
foreach ($process in $processes) {
    if ($process.ExecutablePath -and $process.ExecutablePath -ine $executable) {
        throw "A Weixin.exe process uses an unexpected path: $($process.ExecutablePath)"
    }
}

foreach ($process in $processes) {
    Stop-Process -Id $process.ProcessId -Force -ErrorAction SilentlyContinue
}
for ($attempt = 0; $attempt -lt 50; $attempt += 1) {
    if (-not (Get-Process Weixin -ErrorAction SilentlyContinue)) { break }
    Start-Sleep -Milliseconds 200
}
if (Get-Process Weixin -ErrorAction SilentlyContinue) {
    throw "Weixin.exe processes did not stop"
}

$quotedExecutable = '"' + $executable + '"'
$started = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $quotedExecutable }
if ($started.ReturnValue -ne 0 -or -not $started.ProcessId) {
    throw "Weixin.exe could not be started: $($started.ReturnValue)"
}

for ($attempt = 0; $attempt -lt 100; $attempt += 1) {
    Start-Sleep -Milliseconds 200
    $running = Get-CimInstance Win32_Process -Filter "Name='Weixin.exe'" |
        Where-Object { $_.ExecutablePath -ieq $executable }
    if ($running) {
        Write-Output ("WEIXIN_RESTARTED=1")
        Write-Output ("INITIAL_PID=" + $started.ProcessId)
        Write-Output ("PROCESS_COUNT=" + @($running).Count)
        exit 0
    }
}

throw "Weixin.exe did not remain running after restart"
