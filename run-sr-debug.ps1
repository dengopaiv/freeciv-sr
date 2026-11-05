$ErrorActionPreference = 'Continue'
$root = $PSScriptRoot
$dist = Join-Path $root 'dist-sr'

$env:FREECIV_DATA_PATH = Join-Path $root 'data'

# --Fatal makes Freeciv assertions raise SIGABRT instead of silently
# printing and continuing. The SEH filter + exchndl.dll then produce a
# symbolized freeciv-sr.RPT next to the exe.
$logPath = Join-Path $dist '_runtime.log'
Remove-Item $logPath -ErrorAction SilentlyContinue

Set-Location $dist
Write-Host "Launching freeciv-sr.exe with --Fatal (debug mode)."
Write-Host "  - On assert failure the process aborts and exchndl.dll"
Write-Host "    writes a stack-traced report to freeciv-sr.RPT."
Write-Host "  - Open the Game menu with Alt+G, then S for Start, then B for Begin."
Write-Host "  - Press Ctrl+Q to quit cleanly."
Write-Host ""

$argList = @('--Fatal') + $args
$p = Start-Process -FilePath .\freeciv-sr.exe `
  -ArgumentList $argList `
  -NoNewWindow `
  -PassThru `
  -RedirectStandardError $logPath
Write-Host ("Launched pid " + $p.Id + ". Waiting for it to exit...")
$p.WaitForExit()
Write-Host ("freeciv-sr.exe exited with code " + $p.ExitCode + ".")
Write-Host ""
Write-Host "--- last 40 lines of runtime log ---"
if (Test-Path $logPath) {
  Get-Content $logPath -Tail 40
} else {
  Write-Host "(no log file)"
}

$rpt = Join-Path $dist 'freeciv-sr.RPT'
if (Test-Path $rpt) {
  Write-Host ""
  Write-Host "*** freeciv-sr.RPT written: $rpt ***"
  Write-Host "Paste its contents into the chat for triage."
}

Write-Host ""
Write-Host "Press Enter to close this window..."
[void][Console]::ReadLine()
