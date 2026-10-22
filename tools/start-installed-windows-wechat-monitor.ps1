param(
    [string]$InstallDirectory = "$env:LOCALAPPDATA\CFPay\WeChatMonitor",
    [switch]$Restart
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$monitor = Join-Path $InstallDirectory "cfpay-wechat-monitor.exe"
$config = Join-Path $InstallDirectory "config.json"
if (-not (Test-Path -LiteralPath $monitor)) { throw "Installed monitor is missing" }
if (-not (Test-Path -LiteralPath $config)) { throw "Installed monitor config is missing" }

$existing = @(Get-CimInstance Win32_Process -Filter "Name='cfpay-wechat-monitor.exe'" |
    Where-Object { $_.ExecutablePath -ieq $monitor -and $_.CommandLine -match '--tray' })
if ($existing.Count -gt 0) {
    if (-not $Restart) {
        Write-Output ("MONITOR_ALREADY_RUNNING=1")
        Write-Output ("MONITOR_PID=" + $existing[0].ProcessId)
        exit 0
    }

    Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class CfpayMonitorWindow {
    public delegate bool EnumWindowProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowProc p, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    public static bool RequestExit(uint processId) {
        bool sent = false;
        EnumWindows(delegate(IntPtr window, IntPtr unused) {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            StringBuilder name = new StringBuilder(128);
            GetClassName(window, name, name.Capacity);
            if (owner == processId && name.ToString() == "CFPayWeChatMonitorWindow") {
                sent = PostMessage(window, 0x0111, (IntPtr)2004, IntPtr.Zero);
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return sent;
    }
}
"@
    foreach ($process in $existing) {
        if (-not [CfpayMonitorWindow]::RequestExit([uint32]$process.ProcessId)) {
            throw "Installed monitor window was not found for graceful restart"
        }
    }
    for ($attempt = 0; $attempt -lt 150; $attempt += 1) {
        Start-Sleep -Milliseconds 200
        $remaining = @(Get-CimInstance Win32_Process -Filter "Name='cfpay-wechat-monitor.exe'" |
            Where-Object { $_.ExecutablePath -ieq $monitor -and $_.CommandLine -match '--tray' })
        if ($remaining.Count -eq 0) { break }
    }
    if ($remaining.Count -ne 0) { throw "Installed monitor did not stop gracefully" }
}

$command = '"' + $monitor + '" --tray --config "' + $config + '"'
$started = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $command }
if ($started.ReturnValue -ne 0 -or -not $started.ProcessId) {
    throw "Installed monitor could not be started: $($started.ReturnValue)"
}
Start-Sleep -Seconds 2
$running = Get-CimInstance Win32_Process -Filter "ProcessId=$($started.ProcessId)"
if (-not $running) { throw "Installed monitor exited during startup" }

Write-Output "MONITOR_STARTED=1"
Write-Output ("MONITOR_PID=" + $started.ProcessId)
