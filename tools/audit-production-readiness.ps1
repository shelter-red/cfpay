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

$resolvedConfigPath = Convert-Path -LiteralPath $ConfigPath
$config = Get-Content -LiteralPath $resolvedConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
$baseUrl = (Require-ConfigValue $config "baseUrl").TrimEnd("/")
if (-not $baseUrl.StartsWith("https://", [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Production baseUrl must use HTTPS"
}
$adminPath = Require-ConfigValue $config "adminPath"
$adminUsername = Require-ConfigValue $config "adminUsername"
$adminPassword = Require-ConfigValue $config "adminPassword"
$merchantKey = Require-ConfigValue $config "merchantKey"
$session = New-Object Microsoft.PowerShell.Commands.WebRequestSession

function Invoke-CfpayRequest {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Method,
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [string]$JsonBody = "",
        [string]$FormBody = "",
        [int[]]$ExpectedStatus = @(200),
        [int]$TimeoutSec = 30
    )

    $parameters = @{
        Uri = $baseUrl + $Path
        Method = $Method
        WebSession = $session
        UseBasicParsing = $true
        TimeoutSec = $TimeoutSec
    }
    if ($ProxyUrl) { $parameters.Proxy = $ProxyUrl }
    if ($JsonBody) {
        $parameters.ContentType = "application/json"
        $parameters.Body = $JsonBody
    } elseif ($FormBody) {
        $parameters.ContentType = "application/x-www-form-urlencoded"
        $parameters.Body = $FormBody
    }

    $status = 0
    $content = ""
    try {
        $response = Invoke-WebRequest @parameters
        $status = [int]$response.StatusCode
        $content = [string]$response.Content
    } catch [System.Net.WebException] {
        $errorResponse = $_.Exception.Response
        if ($null -eq $errorResponse) {
            $requestPath = ($Path -split "\?", 2)[0]
            throw "$Method $requestPath failed"
        }
        $status = [int]$errorResponse.StatusCode
        $reader = New-Object System.IO.StreamReader($errorResponse.GetResponseStream())
        try { $content = $reader.ReadToEnd() } finally { $reader.Dispose() }
    }

    if ($ExpectedStatus -notcontains $status) {
        $requestPath = ($Path -split "\?", 2)[0]
        throw "$Method $requestPath returned unexpected HTTP status $status"
    }

    $body = $null
    if (-not [string]::IsNullOrWhiteSpace($content)) {
        try { $body = $content | ConvertFrom-Json } catch { }
    }
    return [PSCustomObject]@{ Status = $status; Body = $body }
}

$health = Invoke-CfpayRequest -Method "GET" -Path "/api/health"
$adminEntry = Invoke-CfpayRequest -Method "GET" -Path ("/" + [Uri]::EscapeDataString($adminPath))
$loginPayload = @{ username = $adminUsername; password = $adminPassword } | ConvertTo-Json -Compress
$login = Invoke-CfpayRequest -Method "POST" -Path "/admin-api/login" -JsonBody $loginPayload
if ($login.Body.ok -ne $true) { throw "Production admin login failed" }

$adminSession = Invoke-CfpayRequest -Method "GET" -Path "/admin-api/session"
$settings = Invoke-CfpayRequest -Method "GET" -Path "/admin-api/settings"
$qrCodes = Invoke-CfpayRequest -Method "GET" -Path "/admin-api/qr-codes"
$alipayStatus = Invoke-CfpayRequest -Method "GET" -Path "/admin-api/alipay/status"
$monitors = Invoke-CfpayRequest -Method "GET" -Path "/admin-api/monitors"
$orders = Invoke-CfpayRequest -Method "GET" -Path "/admin-api/orders?limit=200"
$alipayQuery = Invoke-CfpayRequest -Method "POST" -Path "/admin-api/alipay/query" -TimeoutSec 60

$merchantId = [string]$settings.Body.merchantId
if ([string]::IsNullOrWhiteSpace($merchantId)) { throw "Production merchant ID is missing" }
$missingOrder = "audit-" + [Guid]::NewGuid().ToString("N")
$epayForm = "act=order&pid=" + [Uri]::EscapeDataString($merchantId) +
    "&key=" + [Uri]::EscapeDataString($merchantKey) +
    "&out_trade_no=" + [Uri]::EscapeDataString($missingOrder)
$epayQuery = Invoke-CfpayRequest -Method "POST" -Path "/api.php" -FormBody $epayForm -ExpectedStatus @(401, 404)

$qrProviders = @($qrCodes.Body.items | ForEach-Object { [string]$_.provider })
$orderItems = @($orders.Body.items)
$alipayPaidOrderCount = @($orderItems | Where-Object { $_.provider -eq "alipay" -and $_.status -eq "PAID" }).Count
$wechatPaidOrderCount = @($orderItems | Where-Object { $_.provider -eq "wxpay" -and $_.status -eq "PAID" }).Count
$alipayExpiredOrderCount = @($orderItems | Where-Object { $_.provider -eq "alipay" -and $_.status -eq "EXPIRED" }).Count
$wechatExpiredOrderCount = @($orderItems | Where-Object { $_.provider -eq "wxpay" -and $_.status -eq "EXPIRED" }).Count
$callbackSuccessOrderCount = @($orderItems | Where-Object { $_.notify_status -eq "SUCCESS" }).Count
$healthOk = $health.Status -eq 200 -and $health.Body.ok -eq $true
$adminSessionOk = $adminSession.Body.ok -eq $true
$gatewayMatches = [string]$settings.Body.gatewayUrl -eq $baseUrl
$merchantConfigured = $settings.Body.merchantKeyConfigured -eq $true
$monitorSecretConfigured = $settings.Body.monitorSecretConfigured -eq $true
$wechatQrConfigured = $qrProviders -contains "wxpay"
$alipayQrConfigured = $qrProviders -contains "alipay"
$alipayConfigured = $alipayStatus.Body.configured -eq $true
$alipayPollerConfigured = $alipayStatus.Body.poller.configured -eq $true
$alipayQueryOk = $alipayQuery.Status -eq 200 -and $alipayQuery.Body.ok -eq $true
$epayCredentialAccepted = $epayQuery.Status -eq 404
$auditOk = $healthOk -and $adminEntry.Status -eq 200 -and $adminSessionOk -and $gatewayMatches -and
    $merchantConfigured -and $monitorSecretConfigured -and $wechatQrConfigured -and $alipayQrConfigured -and
    $alipayConfigured -and $alipayPollerConfigured -and $alipayQueryOk -and $epayCredentialAccepted

$result = [ordered]@{
    health = $healthOk
    adminEntryStatus = $adminEntry.Status
    adminSession = $adminSessionOk
    gatewayMatches = $gatewayMatches
    merchantConfigured = $merchantConfigured
    monitorSecretConfigured = $monitorSecretConfigured
    wechatQrConfigured = $wechatQrConfigured
    alipayQrConfigured = $alipayQrConfigured
    alipayConfigured = $alipayConfigured
    alipaySource = [string]$alipayStatus.Body.source
    alipayPollerConfigured = $alipayPollerConfigured
    alipayQueryStatus = $alipayQuery.Status
    alipayQueryOk = $alipayQueryOk
    alipayLedgerTotal = [int]$alipayQuery.Body.totalSize
    monitorDeviceCount = @($monitors.Body.devices).Count
    monitorComponentCount = @($monitors.Body.components).Count
    pendingOrderCount = @($orderItems | Where-Object { $_.status -eq "PENDING" }).Count
    paidOrderCount = @($orderItems | Where-Object { $_.status -eq "PAID" }).Count
    alipayPaidOrderCount = $alipayPaidOrderCount
    wechatPaidOrderCount = $wechatPaidOrderCount
    alipayExpiredOrderCount = $alipayExpiredOrderCount
    wechatExpiredOrderCount = $wechatExpiredOrderCount
    callbackSuccessOrderCount = $callbackSuccessOrderCount
    alipayClosedLoopEvidence = $alipayPaidOrderCount -gt 0 -and $callbackSuccessOrderCount -gt 0
    epayAuthenticatedMissingOrderStatus = $epayQuery.Status
    epayCredentialAccepted = $epayCredentialAccepted
    readOnlyAudit = if ($auditOk) { "ok" } else { "failed" }
}

$result | ConvertTo-Json -Depth 4
if (-not $auditOk) { exit 2 }
