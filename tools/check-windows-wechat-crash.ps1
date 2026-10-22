param([int]$Minutes = 30)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$startTime = (Get-Date).AddMinutes(-1 * $Minutes)
$events = Get-WinEvent -FilterHashtable @{
    LogName = "Application"
    StartTime = $startTime
} -ErrorAction SilentlyContinue | Where-Object {
    ($_.ProviderName -eq "Application Error" -or
     $_.ProviderName -eq "Windows Error Reporting" -or
     $_.ProviderName -eq ".NET Runtime") -and
    $_.Message -match "Weixin|WeChat"
} | Sort-Object TimeCreated

Write-Output ("START_TIME=" + $startTime.ToString("o"))
Write-Output ("MATCH_COUNT=" + @($events).Count)
foreach ($event in $events) {
    Write-Output "EVENT_BEGIN"
    Write-Output ("TIME=" + $event.TimeCreated.ToString("o"))
    Write-Output ("PROVIDER=" + $event.ProviderName)
    Write-Output ("ID=" + $event.Id)
    Write-Output ("LEVEL=" + $event.LevelDisplayName)
    Write-Output ("MESSAGE=" + ($event.Message -replace "[\r\n]+", " | "))
    Write-Output "EVENT_END"
}
