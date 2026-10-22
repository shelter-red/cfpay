param(
    [Parameter(Mandatory = $true)]
    [string]$MonitorPath,
    [Parameter(Mandatory = $true)]
    [string]$ConfigPath
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class CfpayAccountRefreshUi {
    public delegate bool EnumWindowProc(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr parent, int id);
    [DllImport("user32.dll", EntryPoint = "SendMessageW", CharSet = CharSet.Unicode)]
    public static extern IntPtr SendMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr window, StringBuilder value, int length);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr window, StringBuilder value, int length);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowProc callback, IntPtr parameter);

    public static string Text(IntPtr window) {
        var value = new StringBuilder(1024);
        GetWindowText(window, value, value.Capacity);
        return value.ToString();
    }

    public static string ComboSelection(IntPtr combo) {
        const uint CB_GETCURSEL = 0x0147;
        const uint CB_GETLBTEXT = 0x0148;
        const uint CB_GETLBTEXTLEN = 0x0149;
        int index = SendMessage(combo, CB_GETCURSEL, IntPtr.Zero, IntPtr.Zero).ToInt32();
        if (index < 0) return "";
        int length = SendMessage(combo, CB_GETLBTEXTLEN, (IntPtr)index, IntPtr.Zero).ToInt32();
        if (length < 0) return "";
        var buffer = Marshal.AllocHGlobal((length + 1) * 2);
        try {
            SendMessage(combo, CB_GETLBTEXT, (IntPtr)index, buffer);
            return Marshal.PtrToStringUni(buffer) ?? "";
        } finally {
            Marshal.FreeHGlobal(buffer);
        }
    }

    public static int ComboCount(IntPtr combo) {
        return SendMessage(combo, 0x0146, IntPtr.Zero, IntPtr.Zero).ToInt32();
    }

    public static int ComboSelectedIndex(IntPtr combo) {
        return SendMessage(combo, 0x0147, IntPtr.Zero, IntPtr.Zero).ToInt32();
    }

    public static IntPtr FindDialog(uint expectedProcessId) {
        IntPtr result = IntPtr.Zero;
        EnumWindows(delegate(IntPtr window, IntPtr unused) {
            uint processId;
            GetWindowThreadProcessId(window, out processId);
            if (processId != expectedProcessId) return true;
            var className = new StringBuilder(64);
            GetClassName(window, className, className.Capacity);
            if (className.ToString() == "#32770") {
                result = window;
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }
}
"@

$sourceMonitor = Convert-Path -LiteralPath $MonitorPath
$config = Convert-Path -LiteralPath $ConfigPath
$runDirectory = Join-Path $env:LOCALAPPDATA "cfpay-account-refresh-ui-test"
[System.IO.Directory]::CreateDirectory($runDirectory) | Out-Null
$monitor = Join-Path $runDirectory ([System.IO.Path]::GetFileName($sourceMonitor))
Copy-Item -LiteralPath $sourceMonitor -Destination $monitor -Force
$process = Start-Process -FilePath $monitor -ArgumentList @('--config', '"' + $config + '"') -PassThru
try {
    $window = [IntPtr]::Zero
    for ($attempt = 0; $attempt -lt 100; $attempt += 1) {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
        if ($process.HasExited) { throw "Monitor exited before UI verification" }
        if ($process.MainWindowHandle -ne 0) {
            $window = $process.MainWindowHandle
            break
        }
    }
    if ($window -eq [IntPtr]::Zero) { throw "Monitor window was not created" }

    $combo = [CfpayAccountRefreshUi]::GetDlgItem($window, 1008)
    $button = [CfpayAccountRefreshUi]::GetDlgItem($window, 1009)
    if ($combo -eq [IntPtr]::Zero -or $button -eq [IntPtr]::Zero) { throw "Account refresh controls are missing" }

    $selection = [CfpayAccountRefreshUi]::ComboSelection($combo)
    $hasNicknameLabel = $selection.Contains([char]0xff08) -and $selection.EndsWith([string][char]0xff09)
    $buttonText = [CfpayAccountRefreshUi]::Text($button)
    $buttonTextValid = $buttonText -eq ([string]([char]0x91cd) + [char]0x65b0 + [char]0x83b7 + [char]0x53d6)
    Write-Output ("ACCOUNT_OPTION_COUNT=" + [CfpayAccountRefreshUi]::ComboCount($combo))
    Write-Output ("ACCOUNT_SELECTED_INDEX=" + [CfpayAccountRefreshUi]::ComboSelectedIndex($combo))
    Write-Output ("ACCOUNT_SELECTION_LENGTH=" + $selection.Length)
    Write-Output ("ACCOUNT_SELECTION_HAS_LEFT_PAREN=" + [int]$selection.Contains([char]0xff08))
    Write-Output ("ACCOUNT_SELECTION_HAS_RIGHT_PAREN=" + [int]$selection.EndsWith([string][char]0xff09))
    if (-not $hasNicknameLabel) { throw "Selected account does not include a nickname label" }
    if (-not $buttonTextValid) { throw "Account refresh button text is invalid" }

    [CfpayAccountRefreshUi]::PostMessage($button, 0x00f5, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    $dialog = [IntPtr]::Zero
    for ($attempt = 0; $attempt -lt 100; $attempt += 1) {
        Start-Sleep -Milliseconds 100
        $dialog = [CfpayAccountRefreshUi]::FindDialog([uint32]$process.Id)
        if ($dialog -ne [IntPtr]::Zero) { break }
    }
    if ($dialog -eq [IntPtr]::Zero) { throw "Account refresh result dialog was not shown" }
    [CfpayAccountRefreshUi]::PostMessage($dialog, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null

    Write-Output "ACCOUNT_LABEL_HAS_NICKNAME=1"
    Write-Output "ACCOUNT_REFRESH_BUTTON=1"
    Write-Output "ACCOUNT_REFRESH_DIALOG=1"
} finally {
    if (-not $process.HasExited) {
        $process.Refresh()
        if ($process.MainWindowHandle -ne 0) {
            [CfpayAccountRefreshUi]::PostMessage($process.MainWindowHandle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
            Start-Sleep -Milliseconds 300
            if (-not $process.HasExited) {
                [CfpayAccountRefreshUi]::PostMessage($process.MainWindowHandle, 0x0111, [IntPtr]2004, [IntPtr]::Zero) | Out-Null
            }
        }
        if (-not $process.WaitForExit(10000)) { Stop-Process -Id $process.Id -Force }
    }
}
