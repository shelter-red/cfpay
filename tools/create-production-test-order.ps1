param(
    [Parameter(Mandatory = $true)]
    [string]$ConfigPath,
    [ValidateSet("wxpay", "alipay")]
    [string]$Provider = "wxpay",
    [string]$Amount = "0.01",
    [string]$ProxyUrl = "",
    [switch]$OpenBrowser
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

function Encode-Form([string]$value) {
    return [System.Uri]::EscapeDataString($value)
}

function New-EpaySign([hashtable]$parameters, [string]$merchantKey) {
    $parts = @(
        $parameters.GetEnumerator() |
            Where-Object { $_.Key -notin @("sign", "sign_type") -and -not [string]::IsNullOrEmpty([string]$_.Value) } |
            Sort-Object Name |
            ForEach-Object { $_.Key + "=" + $_.Value }
    )
    $input = ($parts -join "&") + $merchantKey
    $md5 = [System.Security.Cryptography.MD5]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($input)
        return ([BitConverter]::ToString($md5.ComputeHash($bytes)).Replace("-", "").ToLowerInvariant())
    } finally {
        $md5.Dispose()
    }
}

$resolvedConfigPath = Convert-Path -LiteralPath $ConfigPath
$config = Get-Content -LiteralPath $resolvedConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
$baseUrl = (Require-ConfigValue $config "baseUrl").TrimEnd("/")
$merchantId = Require-ConfigValue $config "merchantId"
$merchantKey = Require-ConfigValue $config "merchantKey"
$orderNo = "wx-test-" + [DateTime]::UtcNow.ToString("yyyyMMddHHmmss") + "-" + (Get-Random -Minimum 1000 -Maximum 9999)
$parameters = [ordered]@{
    pid = $merchantId
    type = $Provider
    out_trade_no = $orderNo
    name = "CFPay WeChat Payment Test"
    money = $Amount
    device = "pc"
    sign_type = "MD5"
}
$parameters.sign = New-EpaySign $parameters $merchantKey
$form = (($parameters.GetEnumerator() | ForEach-Object {
    (Encode-Form ([string]$_.Key)) + "=" + (Encode-Form ([string]$_.Value))
}) -join "&")

$request = @{
    Uri = $baseUrl + "/mapi.php"
    Method = "POST"
    Body = $form
    ContentType = "application/x-www-form-urlencoded"
    UseBasicParsing = $true
    TimeoutSec = 30
}
if ($ProxyUrl) { $request.Proxy = $ProxyUrl }
$response = Invoke-WebRequest @request
if ([int]$response.StatusCode -ne 200) { throw "Order creation returned HTTP $([int]$response.StatusCode)" }
$body = $response.Content | ConvertFrom-Json
if ($body.code -ne 1) { throw "Order creation failed" }

if ($OpenBrowser) {
    $openLine = 'cmd.exe /c start "" "' + [string]$body.payurl + '"'
    $opened = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $openLine }
    if ($opened.ReturnValue -ne 0) { throw "Payment page could not be opened" }
}

Write-Output "ORDER_CREATED=1"
Write-Output ("OUT_TRADE_NO=" + [string]$body.out_trade_no)
Write-Output ("MONEY=" + [string]$body.money)
Write-Output ("REALLY_MONEY=" + [string]$body.really_money)
Write-Output ("PAY_URL=" + [string]$body.payurl)
Write-Output ("QR_URL=" + [string]$body.qrcode)
$browserOpened = if ($OpenBrowser) { 1 } else { 0 }
Write-Output ("BROWSER_OPENED=" + $browserOpened)
