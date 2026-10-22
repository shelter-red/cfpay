[Console]::OutputEncoding = [Text.Encoding]::UTF8

$namePattern = "WeChat|Weixin|WXWork"
$processes = Get-CimInstance Win32_Process |
  Where-Object { $_.Name -match $namePattern -or $_.ExecutablePath -match $namePattern } |
  Sort-Object ProcessId

Write-Output "PROCESSES_BEGIN"
foreach ($item in $processes) {
  $version = $null
  if ($item.ExecutablePath -and (Test-Path $item.ExecutablePath)) {
    $version = (Get-Item $item.ExecutablePath).VersionInfo.FileVersion
  }
  $window = Get-Process -Id $item.ProcessId -ErrorAction SilentlyContinue
  Write-Output ("PID=" + $item.ProcessId)
  Write-Output ("NAME=" + $item.Name)
  Write-Output ("PATH=" + $item.ExecutablePath)
  Write-Output ("VERSION=" + $version)
  Write-Output ("WINDOW=" + $window.MainWindowTitle)
  Write-Output ("COMMAND=" + $item.CommandLine)
  try {
    $modules = (Get-Process -Id $item.ProcessId -Module -ErrorAction Stop |
      Where-Object { $_.ModuleName -match "WeChat|Weixin|Wx|Message|Contact|Payment|Pay|SQLite" } |
      Select-Object -ExpandProperty FileName -Unique)
    foreach ($module in $modules) { Write-Output ("MODULE=" + $module) }
  } catch {
    Write-Output ("MODULE_ERROR=" + $_.Exception.Message)
  }
  Write-Output "PROCESS_END"
}
Write-Output "PROCESSES_END"

Write-Output "INSTALLS_BEGIN"
$uninstallRoots = @(
  "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*",
  "HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*",
  "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*"
)
Get-ItemProperty $uninstallRoots -ErrorAction SilentlyContinue |
  Where-Object { $_.DisplayName -match $namePattern } |
  ForEach-Object {
    Write-Output ("DISPLAY_NAME=" + $_.DisplayName)
    Write-Output ("DISPLAY_VERSION=" + $_.DisplayVersion)
    Write-Output ("INSTALL_LOCATION=" + $_.InstallLocation)
  }
Write-Output "INSTALLS_END"

Write-Output "TOOLS_BEGIN"
foreach ($tool in @("frida.exe", "frida-ps.exe", "x64dbg.exe", "windbg.exe", "cdb.exe", "python.exe", "node.exe")) {
  $command = Get-Command $tool -ErrorAction SilentlyContinue
  Write-Output ($tool + "=" + $command.Source)
}
Write-Output "TOOLS_END"
