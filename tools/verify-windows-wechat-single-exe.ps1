param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDirectory,
    [string]$WeixinDllPath,
    [string]$InstallerPath,
    [switch]$VerifyUi
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$package = Convert-Path -LiteralPath $PackageDirectory
$files = @(Get-ChildItem -LiteralPath $package -File | Select-Object -ExpandProperty Name | Sort-Object)
$expected = @("cfpay-wechat-monitor.exe")
if (($files -join "|") -ne ($expected -join "|")) {
    throw "Package must contain only cfpay-wechat-monitor.exe; the observer DLL is embedded and extracted at runtime"
}
Get-CimInstance Win32_Process -Filter "Name='cfpay-wechat-monitor.exe'" -ErrorAction SilentlyContinue |
    Where-Object { $_.ExecutablePath -like (Join-Path $env:TEMP "cfpay-single-exe-*") } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }

$staging = Join-Path $env:TEMP ("cfpay-single-exe-" + [Guid]::NewGuid().ToString("N"))
[System.IO.Directory]::CreateDirectory($staging) | Out-Null
$monitorProcess = $null
$configProcess = $null
try {
    foreach ($filename in $expected) {
        Copy-Item -LiteralPath (Join-Path $package $filename) -Destination (Join-Path $staging $filename) -Force
    }
    $monitor = Join-Path $staging "cfpay-wechat-monitor.exe"
    $observer = Join-Path $staging "cfpay-hook-observer.dll"
    if (-not (Test-Path -LiteralPath $monitor)) {
        throw "Required native file is missing: cfpay-wechat-monitor.exe"
    }

    $selfTest = Start-Process -FilePath $monitor -ArgumentList "--self-test" -PassThru
    if (-not $selfTest.WaitForExit(30000)) {
        $selfTest.Kill()
        throw "Native monitor self-test timed out"
    }
    if ($selfTest.ExitCode -ne 0) { throw "Native monitor self-test failed: $($selfTest.ExitCode)" }
    if (-not (Test-Path -LiteralPath $observer)) {
        throw "Embedded observer DLL was not extracted next to the single EXE"
    }

    if ($WeixinDllPath) {
        $dll = Convert-Path -LiteralPath $WeixinDllPath
        $stdout = Join-Path $env:TEMP ("cfpay-host-stdout-" + [Guid]::NewGuid().ToString("N") + ".log")
        $stderr = Join-Path $env:TEMP ("cfpay-host-stderr-" + [Guid]::NewGuid().ToString("N") + ".log")
        $arguments = '--internal-host --inspect-weixin "' + $dll + '"'
        $inspection = Start-Process -FilePath $monitor -ArgumentList $arguments -RedirectStandardOutput $stdout `
            -RedirectStandardError $stderr -PassThru
        if (-not $inspection.WaitForExit(60000)) {
            $inspection.Kill()
            throw "Embedded host inspection timed out"
        }
        $inspection.WaitForExit()
        $inspection.Refresh()
        $inspectionExitCode = $inspection.ExitCode
        $output = Get-Content -LiteralPath $stdout -Raw -Encoding UTF8
        if (($null -ne $inspectionExitCode -and $inspectionExitCode -ne 0) -or
            $output -notmatch '"detailCode":"TARGET_VALIDATED"') {
            throw "Embedded host inspection failed: exit=$inspectionExitCode, output=$output"
        }
        Remove-Item -LiteralPath $stdout, $stderr -Force -ErrorAction SilentlyContinue
    }

    if ($InstallerPath) {
        if (-not $WeixinDllPath) { throw "WeixinDllPath is required for installer verification" }
        $installer = Convert-Path -LiteralPath $InstallerPath
        $weixinExe = Join-Path (Split-Path -Parent (Split-Path -Parent $dll)) "Weixin.exe"
        $installerConfigPath = Join-Path $staging "installer-config.json"
        $installerConfig = [ordered]@{
            baseUrl = "https://pay.example.com"
            monitorId = "wm_0123456789abcdef01234567"
            monitorSecret = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
            expectedReceiverUsername = ""
            weixinExecutablePath = $weixinExe
            weixinDllPath = $dll
            observerDllPath = $observer
        }
        [System.IO.File]::WriteAllText(
            $installerConfigPath,
            ($installerConfig | ConvertTo-Json -Depth 4),
            (New-Object System.Text.UTF8Encoding($false)))
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $installer `
            -PackageDirectory $staging -ConfigPath $installerConfigPath -CheckOnly
        if ($LASTEXITCODE -ne 0) { throw "Native package installer check failed: $LASTEXITCODE" }
    }

    if ($VerifyUi) {
        Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class CfpayNativeWindow {
    public delegate bool EnumWindowProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowProc p, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h, EnumWindowProc p, IntPtr l);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr h);
    public static int CountChildClass(IntPtr parent, string expected) {
        int count = 0;
        EnumChildWindows(parent, delegate(IntPtr child, IntPtr unused) {
            StringBuilder name = new StringBuilder(128);
            GetClassName(child, name, name.Capacity);
            if (String.Equals(name.ToString(), expected, StringComparison.OrdinalIgnoreCase)) { count += 1; }
            return true;
        }, IntPtr.Zero);
        return count;
    }
    public static int CountPopupMenuItems(uint processId) {
        int count = -1;
        EnumWindows(delegate(IntPtr window, IntPtr unused) {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            StringBuilder name = new StringBuilder(128);
            GetClassName(window, name, name.Capacity);
            if (owner == processId && name.ToString() == "#32768") {
                IntPtr menu = SendMessage(window, 0x01E1, IntPtr.Zero, IntPtr.Zero);
                count = GetMenuItemCount(menu);
                return false;
            }
            return true;
        }, IntPtr.Zero);
        return count;
    }
}
"@
        function Wait-MainWindow([System.Diagnostics.Process]$process) {
            for ($attempt = 0; $attempt -lt 50; $attempt += 1) {
                Start-Sleep -Milliseconds 200
                $process.Refresh()
                if ($process.MainWindowHandle -ne 0) { return $process.MainWindowHandle }
            }
            throw "Native window did not appear"
        }
        $monitorProcess = Start-Process -FilePath $monitor `
            -ArgumentList ('--config "' + (Join-Path $staging "missing.json") + '"') -PassThru
        $monitorHandle = Wait-MainWindow $monitorProcess
        if ([CfpayNativeWindow]::CountChildClass($monitorHandle, "Button") -lt 6 -or
            [CfpayNativeWindow]::CountChildClass($monitorHandle, "Edit") -lt 5) {
            throw "Combined monitor and configuration controls are incomplete"
        }
        [CfpayNativeWindow]::PostMessage($monitorHandle, 0x8001, [IntPtr]::Zero, [IntPtr]0x0205) | Out-Null
        Start-Sleep -Milliseconds 500
        $menuItemCount = [CfpayNativeWindow]::CountPopupMenuItems($monitorProcess.Id)
        if ($menuItemCount -lt 4) { throw "Tray context menu controls are incomplete" }
        [CfpayNativeWindow]::PostMessage($monitorHandle, 0x001F, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null

        $configProcess = Start-Process -FilePath $monitor `
            -ArgumentList ('--config-window --config "' + (Join-Path $staging "config.json") + '"') -PassThru
        if (-not $configProcess.WaitForExit(10000)) {
            $configProcess.Kill()
            throw "Second configuration process did not exit through the singleton path"
        }
        if ($monitorProcess.HasExited) { throw "Main monitor exited while restoring the combined window" }

        [CfpayNativeWindow]::PostMessage($monitorHandle, 0x0111, [IntPtr]2004, [IntPtr]::Zero) | Out-Null
        if (-not $monitorProcess.WaitForExit(30000)) {
            $monitorProcess.Kill()
            throw "Monitor did not exit through the tray command"
        }
    }
} finally {
    if ($configProcess -and -not $configProcess.HasExited) { $configProcess.Kill() }
    if ($monitorProcess -and -not $monitorProcess.HasExited) { $monitorProcess.Kill() }
    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Output "SINGLE_EXE_PACKAGE=ok"
Write-Output "SELF_TEST=ok"
if ($WeixinDllPath) { Write-Output "EMBEDDED_HOST=ok" }
if ($InstallerPath) { Write-Output "INSTALLER_CHECK=ok" }
if ($VerifyUi) { Write-Output "NATIVE_UI=ok" }
