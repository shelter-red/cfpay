param([string]$ExecutablePath = "")

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$targetPath = ""
if ($ExecutablePath) {
    $targetPath = (Resolve-Path -LiteralPath $ExecutablePath).Path
}

$processes = @(
    Get-Process -Name "Weixin" -ErrorAction SilentlyContinue |
        Sort-Object Id |
        ForEach-Object {
            $processPath = ""
            try { $processPath = $_.Path } catch {}
            if (-not $targetPath -or ($processPath -and $processPath.Equals($targetPath, [System.StringComparison]::OrdinalIgnoreCase))) {
                [PSCustomObject]@{
                    pid = $_.Id
                    hasMainWindow = ($_.MainWindowHandle -ne 0)
                    path = $processPath
                }
            }
        }
)

ConvertTo-Json -InputObject $processes -Compress
