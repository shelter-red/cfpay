$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$python = "C:\Python314\python.exe"

if (-not (Test-Path -LiteralPath $python)) {
    throw "Windows Python was not found at $python"
}

Write-Output "Python executable: $python"
& $python --version
& $python -m pip install --disable-pip-version-check --upgrade frida-tools

Write-Output "Installed package versions:"
& $python -c "import frida; print('frida=' + frida.__version__)"
$userBase = (& $python -m site --user-base).Trim()
$fridaCli = Join-Path $userBase "Python314\Scripts\frida.exe"

if (-not (Test-Path -LiteralPath $fridaCli)) {
    throw "Frida CLI was not found at $fridaCli"
}

Write-Output "Frida CLI: $fridaCli"
& $fridaCli --version
