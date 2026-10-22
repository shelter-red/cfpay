param(
    [Parameter(Mandatory = $true)]
    [string]$ConfigPath,
    [string]$ProxyUrl = ""
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

function Require-ConfigValue([object]$config, [string]$name) {
    $value = $config.$name
    if ($null -eq $value -or [string]::IsNullOrWhiteSpace([string]$value)) {
        throw "Missing production config value: $name"
    }
    return [string]$value
}

function New-RandomBase64Url([int]$byteCount) {
    $bytes = New-Object byte[] $byteCount
    $generator = [System.Security.Cryptography.RandomNumberGenerator]::Create()
    try { $generator.GetBytes($bytes) } finally { $generator.Dispose() }
    return [Convert]::ToBase64String($bytes).TrimEnd("=").Replace("+", "-").Replace("/", "_")
}

function Get-KeyFingerprint([string]$value) {
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($value)
        $digest = $sha256.ComputeHash($bytes)
        return ([BitConverter]::ToString($digest).Replace("-", "").ToLowerInvariant()).Substring(0, 12)
    } finally {
        $sha256.Dispose()
    }
}

$resolvedConfigPath = Convert-Path -LiteralPath $ConfigPath
$config = Get-Content -LiteralPath $resolvedConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
$baseUrl = (Require-ConfigValue $config "baseUrl").TrimEnd("/")
if (-not $baseUrl.StartsWith("https://", [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Production baseUrl must use HTTPS"
}
$adminUsername = Require-ConfigValue $config "adminUsername"
$adminPassword = Require-ConfigValue $config "adminPassword"
$session = New-Object Microsoft.PowerShell.Commands.WebRequestSession

function Invoke-CfpayJson {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Method,
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [string]$JsonBody = ""
    )

    $parameters = @{
        Uri = $baseUrl + $Path
        Method = $Method
        WebSession = $session
        UseBasicParsing = $true
        TimeoutSec = 30
    }
    if ($ProxyUrl) { $parameters.Proxy = $ProxyUrl }
    if ($JsonBody) {
        $parameters.ContentType = "application/json"
        $parameters.Body = $JsonBody
    }
    try {
        $response = Invoke-WebRequest @parameters
    } catch {
        throw "$Method $Path failed"
    }
    if ([int]$response.StatusCode -lt 200 -or [int]$response.StatusCode -ge 300) {
        throw "$Method $Path returned HTTP $([int]$response.StatusCode)"
    }
    return $response.Content | ConvertFrom-Json
}

$loginPayload = @{ username = $adminUsername; password = $adminPassword } | ConvertTo-Json -Compress
$login = Invoke-CfpayJson -Method "POST" -Path "/admin-api/login" -JsonBody $loginPayload
if ($login.ok -ne $true) { throw "Production admin login failed" }
$settings = Invoke-CfpayJson -Method "GET" -Path "/admin-api/settings"

$newMerchantKey = New-RandomBase64Url 32
$updatePayload = [ordered]@{
    merchantId = [string]$settings.merchantId
    merchantKey = $newMerchantKey
    monitorSecret = ""
    wechatQrImageUrl = [string]$settings.wechatQrImageUrl
    alipayQrImageUrl = [string]$settings.alipayQrImageUrl
    orderTtlMinutes = [int]$settings.orderTtlMinutes
    amountDirection = [string]$settings.amountDirection
    maxAmountAdjustments = [int]$settings.maxAmountAdjustments
} | ConvertTo-Json -Compress
$updated = Invoke-CfpayJson -Method "PUT" -Path "/admin-api/settings" -JsonBody $updatePayload
if ($updated.ok -ne $true) { throw "Production merchant key rotation failed" }

$config.merchantId = [string]$settings.merchantId
$config.merchantKey = $newMerchantKey
$utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($resolvedConfigPath, (($config | ConvertTo-Json -Depth 8) + "`n"), $utf8WithoutBom)

Write-Output "MERCHANT_KEY_ROTATED=1"
Write-Output ("KEY_FINGERPRINT=" + (Get-KeyFingerprint $newMerchantKey))
Write-Output "LOCAL_CONFIG_UPDATED=1"
