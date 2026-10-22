param([string]$Url = "http://localhost:8790")
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$url = $Url
$response = Invoke-WebRequest -UseBasicParsing -Uri $url -TimeoutSec 10
Write-Output ("HTTP_STATUS=" + [int]$response.StatusCode)
$commandLine = 'cmd.exe /c start "" "' + $url + '"'
$result = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $commandLine }
Write-Output ("OPEN_RETURN=" + $result.ReturnValue)
Write-Output ("OPEN_PID=" + $result.ProcessId)
