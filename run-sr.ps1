$ErrorActionPreference = 'Continue'
$root = 'C:\GIT\sodi\freeciv-3.2.4'
$dist = Join-Path $root 'dist-sr'

$env:FREECIV_DATA_PATH = Join-Path $root 'data'

# Mirror the exe's stderr to a log file so we can read fc_assert / warning
# output after the fact, even if something scrolls off the PS window.
$logPath = Join-Path $dist '_runtime.log'
Remove-Item $logPath -ErrorAction SilentlyContinue

Set-Location $dist
Write-Host "Launching freeciv-sr.exe inline (this PowerShell window is the console)."
Write-Host "  - Live sr_log output appears below as you operate the client."
Write-Host "  - Type commands at the client's CLI prompt; NVDA reads this window."
Write-Host "  - Post-mortem: $dist\_srlog.txt  (always), $dist\_runtime.log (stderr only)."
Write-Host "  - Press Ctrl+Q inside the client to quit cleanly."
Write-Host ""

# -NoNewWindow shares this PS console with the child. That means:
#   (a) stdout from the sr_log tee appears live in this window,
#   (b) no separate console window exists to steal focus from our
#       Win32 pickers (the bug that made the leader-name picker
#       appear "stuck" before),
#   (c) this window persists after the child exits, so the user can
#       scroll back through output.
$startArgs = @{
  FilePath              = '.\freeciv-sr.exe'
  NoNewWindow           = $true
  PassThru              = $true
  RedirectStandardError = $logPath
}
if ($args.Count -gt 0) { $startArgs['ArgumentList'] = $args }
$p = Start-Process @startArgs
Write-Host ("Launched pid " + $p.Id + ". Waiting for it to exit...")
$p.WaitForExit()
Write-Host ("freeciv-sr.exe exited with code " + $p.ExitCode + ".")
Write-Host ""
Write-Host "Press Enter to close this window..."
[void][Console]::ReadLine()
