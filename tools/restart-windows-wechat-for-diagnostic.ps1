param(
    [Parameter(Mandatory = $true)]
    [string]$WeixinExecutable
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$executable = Convert-Path -LiteralPath $WeixinExecutable
$existing = @(Get-Process Weixin -ErrorAction SilentlyContinue)
foreach ($process in $existing) {
    Stop-Process -Id $process.Id -Force -ErrorAction Stop
}

for ($attempt = 0; $attempt -lt 100; $attempt += 1) {
    if (@(Get-Process Weixin -ErrorAction SilentlyContinue).Count -eq 0) { break }
    Start-Sleep -Milliseconds 100
}
if (@(Get-Process Weixin -ErrorAction SilentlyContinue).Count -ne 0) {
    throw "Weixin processes did not exit"
}

$commandLine = '"' + $executable + '"'
$created = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $commandLine }
if ($created.ReturnValue -ne 0 -or $created.ProcessId -le 0) {
    throw ("Weixin start failed with code " + $created.ReturnValue)
}

$started = $null
for ($attempt = 0; $attempt -lt 150; $attempt += 1) {
    $started = Get-Process -Id $created.ProcessId -ErrorAction SilentlyContinue
    if ($started) { break }
    Start-Sleep -Milliseconds 100
}
if (-not $started) { throw "Started Weixin process was not found" }

Write-Output ("STOPPED_COUNT=" + $existing.Count)
Write-Output ("STARTED_PID=" + $started.Id)
Write-Output ("STARTED_PATH=" + $executable)
