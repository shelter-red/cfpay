param(
    [string]$InstallDirectory = "$env:LOCALAPPDATA\CFPay\WeChatMonitor"
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$monitor = Join-Path (Convert-Path -LiteralPath $InstallDirectory) "cfpay-wechat-monitor.exe"
$processes = @(Get-CimInstance Win32_Process -Filter "Name='cfpay-wechat-monitor.exe'" |
    Where-Object { $_.ExecutablePath -ieq $monitor })
if ($processes.Count -eq 0) {
    Write-Output "MONITOR_ALREADY_STOPPED=1"
    exit 0
}

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class CfpayMonitorStop {
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
            if (owner != processId) return true;
            StringBuilder name = new StringBuilder(128);
            GetClassName(window, name, name.Capacity);
            if (name.ToString() == "CFPayWeChatMonitorWindow") {
                sent = PostMessage(window, 0x0111, (IntPtr)2004, IntPtr.Zero) || sent;
            } else if (name.ToString() == "CFPayWeChatConfigWindow") {
                sent = PostMessage(window, 0x0010, IntPtr.Zero, IntPtr.Zero) || sent;
            }
            return true;
        }, IntPtr.Zero);
        return sent;
    }
}
"@

foreach ($process in $processes) {
    [CfpayMonitorStop]::RequestExit([uint32]$process.ProcessId) | Out-Null
}

for ($attempt = 0; $attempt -lt 150; $attempt += 1) {
    Start-Sleep -Milliseconds 200
    $remaining = @(Get-CimInstance Win32_Process -Filter "Name='cfpay-wechat-monitor.exe'" |
        Where-Object { $_.ExecutablePath -ieq $monitor })
    if ($remaining.Count -eq 0) {
        Write-Output "MONITOR_STOPPED=1"
        exit 0
    }
}

throw "Installed monitor did not stop gracefully"
