$ErrorActionPreference = 'Continue'
$root = $PSScriptRoot
$dist = Join-Path $root 'dist-sr'
$exe  = Join-Path $dist 'freeciv-sr.exe'
$log  = Join-Path $dist '_test.log'
$rpt  = Join-Path $dist 'freeciv-sr.RPT'

$env:FREECIV_DATA_PATH = Join-Path $root 'data'

# Clean previous artifacts so we can detect fresh ones.
Remove-Item $log -ErrorAction SilentlyContinue
Remove-Item $rpt -ErrorAction SilentlyContinue

# Commands that are safe pre-game: each hits a "Not in a game" / "No city"
# guard or is a read-only query. Excluded: worklists, msgopts (open a
# Win32 picker that piped stdin cannot dismiss).
$cmds = @(
  'help',
  'rates',
  'policies',
  'revolution',
  'citizens',
  'happiness',
  'cma',
  'rally',
  'players',
  'infra',
  'quit'
) -join [Environment]::NewLine

Set-Location $dist
Write-Host "=== freeciv-sr smoke test ==="
Write-Host "Piping $($cmds.Split([Environment]::NewLine).Count) commands to $exe ..."

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exe
$psi.WorkingDirectory = $dist
$psi.UseShellExecute = $false
$psi.RedirectStandardInput = $true
$psi.RedirectStandardError = $true
$psi.RedirectStandardOutput = $true
$psi.CreateNoWindow = $true

$proc = [System.Diagnostics.Process]::Start($psi)
$stdoutTask = $proc.StandardOutput.ReadToEndAsync()
$stderrTask = $proc.StandardError.ReadToEndAsync()

$proc.StandardInput.WriteLine($cmds)
$proc.StandardInput.Close()

$exited = $proc.WaitForExit(20000)

$stdoutText = $stdoutTask.Result
$stderrText = $stderrTask.Result
Set-Content -Path $log -Value ("=== stdout ===`n" + $stdoutText + "`n=== stderr ===`n" + $stderrText)

if (-not $exited) {
  try { $proc.Kill() } catch {}
  Write-Host "FAIL: process did not exit within 20s (killed)."
  exit 2
}

$exitCode = $proc.ExitCode
Write-Host ("Process exited with code " + $exitCode + ".")

$badLines = @()
if (Test-Path $log) {
  $badLines = Select-String -Path $log -Pattern 'fc_assert|Segmentation|access violation|exchndl|RPT|abort' -SimpleMatch:$false | ForEach-Object { $_.Line }
}

$crashed = Test-Path $rpt

Write-Host ''
Write-Host '--- last 40 log lines ---'
if (Test-Path $log) {
  Get-Content $log -Tail 40
}
Write-Host '-------------------------'

$pass = ($exitCode -eq 0) -and (-not $crashed)
if ($badLines.Count -gt 0) {
  Write-Host ''
  Write-Host 'Suspicious log lines:'
  $badLines | ForEach-Object { Write-Host ('  ' + $_) }
}

if ($pass) {
  Write-Host 'PASS'
  exit 0
} else {
  Write-Host ('FAIL: exit=' + $exitCode + ', crash_file=' + $crashed)
  exit 1
}
