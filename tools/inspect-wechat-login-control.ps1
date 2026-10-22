param(
    [int]$ProcessId = 0,
    [switch]$Invoke,
    [switch]$SummaryOnly
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [Text.Encoding]::UTF8
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class CfpayNativeChildren {
    public delegate bool EnumWindowProc(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumWindowProc callback, IntPtr parameter);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr window, StringBuilder value, int length);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr window, StringBuilder value, int length);
    public static string Describe(IntPtr parent) {
        var lines = new StringBuilder();
        EnumChildWindows(parent, delegate(IntPtr window, IntPtr unused) {
            var className = new StringBuilder(128);
            var text = new StringBuilder(256);
            GetClassName(window, className, className.Capacity);
            GetWindowText(window, text, text.Capacity);
            lines.Append(className).Append("|").Append(text).AppendLine();
            return true;
        }, IntPtr.Zero);
        return lines.ToString();
    }
}
"@
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class CfpayNativeGeometry {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out RECT rect);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr window);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr window, StringBuilder value, int length);
    public static string ClassName(IntPtr window) {
        var value = new StringBuilder(128);
        GetClassName(window, value, value.Capacity);
        return value.ToString();
    }
    public static string Client(IntPtr window) {
        RECT rect;
        if (!GetClientRect(window, out rect)) return "";
        return (rect.Right - rect.Left) + "," + (rect.Bottom - rect.Top);
    }
}
"@

$process = if ($ProcessId -gt 0) {
    Get-Process -Id $ProcessId -ErrorAction Stop
} else {
    Get-Process Weixin -ErrorAction Stop | Select-Object -First 1
}
if ($process.MainWindowHandle -eq 0) { throw "Weixin main window is missing" }

$root = [Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
$windowRect = $root.Current.BoundingRectangle
$all = $root.FindAll([Windows.Automation.TreeScope]::Descendants, [Windows.Automation.Condition]::TrueCondition)
$condition = New-Object Windows.Automation.PropertyCondition(
    [Windows.Automation.AutomationElement]::NameProperty,
    ([string]([char]0x8fdb) + [char]0x5165 + [char]0x5fae + [char]0x4fe1))
$buttons = @($root.FindAll([Windows.Automation.TreeScope]::Descendants, $condition)) |
    Where-Object { $_.Current.ControlType.ProgrammaticName -eq "ControlType.Button" }
$switchName = [string]([char]0x5207) + [char]0x6362 + [char]0x8d26 + [char]0x53f7
$scanName = [string]([char]0x626b) + [char]0x7801 + [char]0x767b + [char]0x5f55
$qrName = [string]([char]0x4e8c) + [char]0x7ef4 + [char]0x7801
$switchCount = @($all | Where-Object { $_.Current.Name -eq $switchName }).Count
$scanCount = @($all | Where-Object { $_.Current.Name -eq $scanName }).Count
$qrCount = @($all | Where-Object { $_.Current.Name -eq $qrName }).Count
$button = if ($buttons.Count -eq 1) { $buttons[0] } else { $null }
$patterns = @()
if ($button) {
    foreach ($pattern in @(
        [Windows.Automation.InvokePattern]::Pattern,
        [Windows.Automation.SelectionItemPattern]::Pattern
    )) {
        $patternObject = $null
        if ($button.TryGetCurrentPattern($pattern, [ref]$patternObject)) { $patterns += $pattern.ProgrammaticName }
    }
}
$rect = if ($button) { $button.Current.BoundingRectangle } else { $null }
Write-Output ("PROCESS_ID=" + $process.Id)
Write-Output ("WINDOW_CLASS=" + $root.Current.ClassName)
Write-Output ("WINDOW_RECT=" + [Math]::Round($windowRect.X) + "," + [Math]::Round($windowRect.Y) + "," +
    [Math]::Round($windowRect.Width) + "," + [Math]::Round($windowRect.Height))
Write-Output ("CLIENT_SIZE=" + [CfpayNativeGeometry]::Client($process.MainWindowHandle))
Write-Output ("WINDOW_ENABLED=" + [CfpayNativeGeometry]::IsWindowEnabled($process.MainWindowHandle))
Write-Output ("NATIVE_CLASS=" + [CfpayNativeGeometry]::ClassName($process.MainWindowHandle))
Write-Output ("LOGIN_BUTTON_COUNT=" + $buttons.Count)
Write-Output ("SWITCH_ACCOUNT_COUNT=" + $switchCount)
Write-Output ("SCAN_LOGIN_COUNT=" + $scanCount)
Write-Output ("QR_LABEL_COUNT=" + $qrCount)
if ($button) {
    Write-Output ("BUTTON_ENABLED=" + $button.Current.IsEnabled)
    Write-Output ("BUTTON_OFFSCREEN=" + $button.Current.IsOffscreen)
    Write-Output ("BUTTON_RECT=" + [Math]::Round($rect.X) + "," + [Math]::Round($rect.Y) + "," +
        [Math]::Round($rect.Width) + "," + [Math]::Round($rect.Height))
} else {
    Write-Output "BUTTON_ENABLED="
    Write-Output "BUTTON_OFFSCREEN="
    Write-Output "BUTTON_RECT="
}
Write-Output ("PATTERNS=" + ($patterns -join ","))
if (-not $SummaryOnly) {
    Write-Output "NATIVE_CHILDREN_BEGIN"
    [CfpayNativeChildren]::Describe($process.MainWindowHandle).TrimEnd()
    Write-Output "NATIVE_CHILDREN_END"
    Write-Output "UIA_DESCENDANTS_BEGIN"
    foreach ($element in $all) {
        $name = $element.Current.Name
        $type = $element.Current.ControlType.ProgrammaticName -replace "ControlType\.", ""
        $bounds = $element.Current.BoundingRectangle
        if ($name -or $type -in @("Button", "Hyperlink", "Text", "Image")) {
            Write-Output ($type + "|" + $name + "|" + [Math]::Round($bounds.X) + "," + [Math]::Round($bounds.Y) + "," +
                [Math]::Round($bounds.Width) + "," + [Math]::Round($bounds.Height))
        }
    }
    Write-Output "UIA_DESCENDANTS_END"
}
if ($Invoke -and $button) {
    $invokeObject = $null
    if (-not $button.TryGetCurrentPattern([Windows.Automation.InvokePattern]::Pattern, [ref]$invokeObject)) {
        throw "Login button does not expose InvokePattern"
    }
    $invokeObject.Invoke()
    Write-Output "INVOKE=1"
}
