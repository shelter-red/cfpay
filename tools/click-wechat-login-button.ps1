param(
    [int]$ProcessId = 0
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class CfpayLoginClick {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extraInfo);
    public const uint LeftDown = 0x0002;
    public const uint LeftUp = 0x0004;
}
"@

$candidates = if ($ProcessId -gt 0) {
    @(Get-Process -Id $ProcessId -ErrorAction Stop)
} else {
    @(Get-Process Weixin -ErrorAction SilentlyContinue)
}
$candidate = $candidates |
    Where-Object { $_.MainWindowHandle -ne 0 -and $_.Path -and $_.Path.EndsWith("\Weixin.exe", [System.StringComparison]::OrdinalIgnoreCase) } |
    Select-Object -First 1
if (-not $candidate) { throw "Weixin login window was not found" }

$root = [Windows.Automation.AutomationElement]::FromHandle($candidate.MainWindowHandle)
$buttonName = [string]([char]0x8fdb) + [char]0x5165 + [char]0x5fae + [char]0x4fe1
$condition = New-Object Windows.Automation.PropertyCondition(
    [Windows.Automation.AutomationElement]::NameProperty,
    $buttonName)
$buttons = @($root.FindAll([Windows.Automation.TreeScope]::Descendants, $condition)) |
    Where-Object { $_.Current.ControlType.ProgrammaticName -eq "ControlType.Button" }
if ($buttons.Count -ne 1) { throw "Expected exactly one Enter Weixin button, found $($buttons.Count)" }

$button = $buttons[0]
$rectangle = $button.Current.BoundingRectangle
if ($rectangle.Width -le 0 -or $rectangle.Height -le 0) { throw "Login button has no visible bounds" }
$x = [int][Math]::Round($rectangle.X + ($rectangle.Width / 2))
$y = [int][Math]::Round($rectangle.Y + ($rectangle.Height / 2))
[CfpayLoginClick]::SetForegroundWindow($candidate.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 150
[CfpayLoginClick]::SetCursorPos($x, $y) | Out-Null
[CfpayLoginClick]::mouse_event([CfpayLoginClick]::LeftDown, 0, 0, 0, [UIntPtr]::Zero)
[CfpayLoginClick]::mouse_event([CfpayLoginClick]::LeftUp, 0, 0, 0, [UIntPtr]::Zero)
Write-Output "LOGIN_BUTTON_CLICKED=1"
Write-Output ("PROCESS_ID=" + $candidate.Id)
