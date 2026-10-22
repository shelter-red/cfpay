$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

function Show-Command([string]$Name) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) {
        Write-Output ($Name + "=" + $command.Source)
    } else {
        Write-Output ($Name + "=")
    }
}

foreach ($name in @("cl.exe", "link.exe", "cmake.exe", "ninja.exe", "msbuild.exe", "git.exe")) {
    Show-Command $name
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
Write-Output ("vswhere=" + $(if (Test-Path -LiteralPath $vswhere) { $vswhere } else { "" }))
if (Test-Path -LiteralPath $vswhere) {
    & $vswhere -all -products * -format json
}

$kitsRoot = "HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots"
if (Test-Path $kitsRoot) {
    $kits = Get-ItemProperty $kitsRoot
    Write-Output ("KitsRoot10=" + $kits.KitsRoot10)
}
