[Console]::OutputEncoding = [Text.Encoding]::UTF8
Get-NetIPConfiguration |
  Where-Object { $_.IPv4Address } |
  ForEach-Object {
    [PSCustomObject]@{
      Alias = $_.InterfaceAlias
      IPv4 = ($_.IPv4Address.IPAddress -join ',')
      Gateway = ($_.IPv4DefaultGateway.NextHop -join ',')
      Status = $_.NetAdapter.Status
      Description = $_.NetAdapter.InterfaceDescription
    }
  } |
  Format-Table -AutoSize
