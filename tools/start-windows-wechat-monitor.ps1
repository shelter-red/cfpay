param(
    [string]$ConfigPath = "config\wechat-monitor.local.json",
    [string]$RepositoryPath = "",
    [switch]$CheckOnly
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$repository = if ($RepositoryPath) { Convert-Path -LiteralPath $RepositoryPath } else { Split-Path -Parent $PSScriptRoot }
$config = if ([System.IO.Path]::IsPathRooted($ConfigPath)) { $ConfigPath } else { Join-Path $repository $ConfigPath }
$monitor = Join-Path $repository "dist\windows-wechat-monitor\cfpay-wechat-monitor.exe"
if (-not (Test-Path -LiteralPath $monitor)) { throw "Native monitor is missing: $monitor" }
if (-not (Test-Path -LiteralPath $config)) { throw "Monitor config is missing: $config" }
$arguments = if ($CheckOnly) {
    '--check --config "' + $config + '"'
} else {
    '--config "' + $config + '"'
}

$process = Start-Process -FilePath $monitor -WorkingDirectory (Split-Path -Parent $monitor) `
    -ArgumentList $arguments -Wait:$CheckOnly -PassThru
if ($CheckOnly) { exit $process.ExitCode }
