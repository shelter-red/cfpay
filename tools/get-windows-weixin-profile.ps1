param([string]$ExecutablePath = "")

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$result = [ordered]@{
    nickname = ""
    accounts = @()
}

function Convert-CodePointsToText([int[]]$CodePoints) {
    return -join ($CodePoints | ForEach-Object { [char]$_ })
}

$genericNames = @(
    "weixin",
    "wechat",
    "avatar",
    "profile",
    "account",
    "user",
    (Convert-CodePointsToText @(0x5FAE, 0x4FE1)),
    (Convert-CodePointsToText @(0x5934, 0x50CF)),
    (Convert-CodePointsToText @(0x8D26, 0x53F7)),
    (Convert-CodePointsToText @(0x4E2A, 0x4EBA, 0x4FE1, 0x606F))
)

$loginRoot = Join-Path $env:APPDATA "Tencent\xwechat\login"
if (Test-Path -LiteralPath $loginRoot) {
    $result.accounts = @(
        Get-ChildItem -LiteralPath $loginRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^wxid_[A-Za-z0-9_-]+$' } |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 5 |
            ForEach-Object {
                [PSCustomObject]@{
                    username = $_.Name
                    lastSeenAt = [DateTimeOffset]$_.LastWriteTime
                }
            }
    )
}

try {
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes

    $targetPath = ""
    if ($ExecutablePath -and (Test-Path -LiteralPath $ExecutablePath)) {
        $targetPath = (Resolve-Path -LiteralPath $ExecutablePath).Path
    }

    $processes = @(
        Get-Process -Name "Weixin" -ErrorAction SilentlyContinue |
            Where-Object {
                if ($_.MainWindowHandle -eq 0) { return $false }
                if (-not $targetPath) { return $true }
                try { return $_.Path.Equals($targetPath, [System.StringComparison]::OrdinalIgnoreCase) } catch { return $false }
            } |
            Sort-Object Id
    )

    foreach ($process in $processes) {
        $root = [System.Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
        if (-not $root) { continue }

        $bestName = ""
        $bestScore = -1
        $elements = $root.FindAll(
            [System.Windows.Automation.TreeScope]::Descendants,
            [System.Windows.Automation.Condition]::TrueCondition
        )
        foreach ($element in $elements) {
            try {
                $automationId = [string]$element.Current.AutomationId
                $className = [string]$element.Current.ClassName
                $helpText = [string]$element.Current.HelpText
                $markers = ($automationId + " " + $className + " " + $helpText).ToLowerInvariant()
                if ($markers -notmatch "avatar|profile|account|user|head") { continue }

                $name = ([string]$element.Current.Name).Trim()
                if (-not $name -or $name.Length -gt 128) { continue }
                if ($genericNames -contains $name.ToLowerInvariant()) { continue }

                $score = 1
                if ($automationId -match "(?i)avatar|profile|account") { $score += 4 }
                if ($className -match "(?i)avatar|profile|account") { $score += 2 }
                if ($helpText -match "(?i)avatar|profile|account") { $score += 1 }
                if ($score -gt $bestScore) {
                    $bestName = $name
                    $bestScore = $score
                }
            } catch {}
        }

        if ($bestName) {
            $result.nickname = $bestName
            break
        }

        $windowTitle = ([string]$process.MainWindowTitle).Trim()
        if ($windowTitle -and $windowTitle.Length -le 128 -and $genericNames -notcontains $windowTitle.ToLowerInvariant()) {
            $result.nickname = $windowTitle
            break
        }
    }
} catch {}

[PSCustomObject]$result | ConvertTo-Json -Compress
