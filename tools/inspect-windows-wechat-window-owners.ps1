param(
    [int]$MainProcessId = 0
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class CfpayWindowOwners {
    public delegate bool EnumWindowProc(IntPtr window, IntPtr parameter);

    [StructLayout(LayoutKind.Sequential)]
    public struct Rect {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowProc callback, IntPtr parameter);

    [DllImport("user32.dll")]
    public static extern bool EnumChildWindows(IntPtr parent, EnumWindowProc callback, IntPtr parameter);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern int GetClassName(IntPtr window, StringBuilder value, int length);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr window, out Rect rect);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr window);

    private static string Describe(IntPtr window, string scope) {
        uint processId;
        GetWindowThreadProcessId(window, out processId);
        var className = new StringBuilder(256);
        GetClassName(window, className, className.Capacity);
        Rect rect;
        GetWindowRect(window, out rect);
        return "WINDOW=" + window.ToInt64().ToString("x") +
            "|SCOPE=" + scope +
            "|PID=" + processId +
            "|CLASS=" + className.ToString() +
            "|VISIBLE=" + IsWindowVisible(window) +
            "|RECT=" + rect.Left + "," + rect.Top + "," + (rect.Right - rect.Left) + "," + (rect.Bottom - rect.Top);
    }

    public static string Enumerate(uint mainProcessId, IntPtr mainWindow, uint[] targetProcessIds) {
        var targets = new HashSet<uint>(targetProcessIds);
        var lines = new List<string>();
        EnumWindows(delegate(IntPtr window, IntPtr unused) {
            uint processId;
            GetWindowThreadProcessId(window, out processId);
            if (targets.Contains(processId)) lines.Add(Describe(window, "TOP"));
            return true;
        }, IntPtr.Zero);
        if (mainWindow != IntPtr.Zero) {
            EnumChildWindows(mainWindow, delegate(IntPtr window, IntPtr unused) {
                uint processId;
                GetWindowThreadProcessId(window, out processId);
                if (targets.Contains(processId)) lines.Add(Describe(window, "MAIN_DESCENDANT"));
                return true;
            }, IntPtr.Zero);
        }
        return string.Join(Environment.NewLine, lines);
    }
}
"@

$targets = @(Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $_.ProcessName -in @("Weixin", "WeChatAppEx")
})
if ($MainProcessId -le 0) {
    $main = $targets | Where-Object { $_.ProcessName -eq "Weixin" -and $_.MainWindowHandle -ne 0 } | Select-Object -First 1
} else {
    $main = Get-Process -Id $MainProcessId -ErrorAction Stop
}
if ($null -eq $main -or $main.MainWindowHandle -eq 0) { throw "Weixin main window was not found" }

$processIds = [uint32[]]@($targets | Select-Object -ExpandProperty Id)
Write-Output ("MAIN_PROCESS_ID=" + $main.Id)
Write-Output ("MAIN_WINDOW=" + $main.MainWindowHandle.ToString("x"))
[CfpayWindowOwners]::Enumerate([uint32]$main.Id, $main.MainWindowHandle, $processIds)
