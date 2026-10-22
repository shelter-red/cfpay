$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$installer = Join-Path $env:TEMP "vs_BuildTools.exe"
$elevatedScript = Join-Path $env:TEMP "cfpay-install-buildtools-elevated.ps1"
$marker = Join-Path $env:TEMP "cfpay-buildtools-result.json"
$installPath = "C:\BuildTools"
$downloadUrl = "https://aka.ms/vs/17/release/vs_BuildTools.exe"

if ([System.IO.File]::Exists($marker)) {
    [System.IO.File]::Delete($marker)
}

if (-not (Test-Path -LiteralPath $installer)) {
    Write-Output "Downloading Visual Studio Build Tools bootstrapper"
    Invoke-WebRequest -Uri $downloadUrl -OutFile $installer -UseBasicParsing
}

$inner = @"
`$ErrorActionPreference = "Stop"
`$installer = "$installer"
`$marker = "$marker"
`$arguments = @(
    "--quiet",
    "--wait",
    "--norestart",
    "--nocache",
    "--installPath", "$installPath",
    "--add", "Microsoft.VisualStudio.Workload.VCTools",
    "--includeRecommended"
)
try {
    `$process = Start-Process -FilePath `$installer -ArgumentList `$arguments -Wait -PassThru
    `$result = @{ exitCode = `$process.ExitCode; completed = `$true }
} catch {
    `$result = @{ exitCode = -1; completed = `$false; error = `$_.Exception.Message }
}
`$result | ConvertTo-Json -Compress | Set-Content -LiteralPath `$marker -Encoding UTF8
"@

Set-Content -LiteralPath $elevatedScript -Value $inner -Encoding ASCII
Write-Output "Launching elevated installer"
Start-Process -FilePath "powershell.exe" -Verb RunAs -ArgumentList @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", ('"' + $elevatedScript + '"')
) -Wait | Out-Null

if (-not (Test-Path -LiteralPath $marker)) {
    throw "Elevated installer did not write its result marker; UAC may have been cancelled"
}

$result = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
Write-Output ("Installer result: " + ($result | ConvertTo-Json -Compress))
if (-not $result.completed -or ($result.exitCode -ne 0 -and $result.exitCode -ne 3010)) {
    throw "Visual Studio Build Tools installation failed with exit code $($result.exitCode)"
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$installation = & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json |
    ConvertFrom-Json |
    Select-Object -First 1
if (-not $installation) {
    throw "vswhere could not find the installed MSVC toolchain"
}

$vcTools = Get-ChildItem -LiteralPath (Join-Path $installation.installationPath "VC\Tools\MSVC") -Directory |
    Sort-Object Name -Descending |
    Select-Object -First 1
$compiler = Join-Path $vcTools.FullName "bin\Hostx64\x64\cl.exe"
if (-not (Test-Path -LiteralPath $compiler)) {
    throw "MSVC compiler was not found at $compiler"
}

Write-Output ("Installation path: " + $installation.installationPath)
Write-Output ("Installation version: " + $installation.installationVersion)
Write-Output ("MSVC compiler: " + $compiler)
& $compiler 2>&1 | Select-Object -First 3
