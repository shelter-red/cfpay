param([int]$ProcessId = 0, [int]$Limit = 500)
[Console]::OutputEncoding = [Text.Encoding]::UTF8
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

if ($ProcessId -le 0) {
  $candidate = Get-Process Weixin -ErrorAction SilentlyContinue |
    Where-Object { $_.MainWindowHandle -ne 0 } |
    Select-Object -First 1
  if (-not $candidate) { throw "Weixin main window was not found" }
  $ProcessId = $candidate.Id
}

$process = Get-Process -Id $ProcessId -ErrorAction Stop
$root = [Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
$walker = [Windows.Automation.TreeWalker]::RawViewWalker
$queue = New-Object System.Collections.Queue
$queue.Enqueue(@($root, 0))
$count = 0

while ($queue.Count -gt 0 -and $count -lt $Limit) {
  $entry = $queue.Dequeue()
  $element = $entry[0]
  $depth = [int]$entry[1]
  try {
    $current = $element.Current
    $rect = $current.BoundingRectangle
    Write-Output (("  " * $depth) +
      "TYPE=" + $current.ControlType.ProgrammaticName +
      "|NAME=" + ($current.Name -replace "[\r\n]+", " ") +
      "|AUTOID=" + $current.AutomationId +
      "|CLASS=" + $current.ClassName +
      "|FRAMEWORK=" + $current.FrameworkId +
      "|RECT=" + [Math]::Round($rect.X) + "," + [Math]::Round($rect.Y) + "," +
      [Math]::Round($rect.Width) + "," + [Math]::Round($rect.Height))
  } catch {
    Write-Output (("  " * $depth) + "ERROR=" + $_.Exception.Message)
  }
  $count += 1
  $child = $walker.GetFirstChild($element)
  while ($child) {
    $queue.Enqueue(@($child, $depth + 1))
    $child = $walker.GetNextSibling($child)
  }
}

Write-Output ("COUNT=" + $count)
