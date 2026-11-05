<#
.SYNOPSIS
  Non-interactive smoke harness for freeciv-sr.exe.

.DESCRIPTION
  Drives freeciv-sr.exe with a scripted sequence of stdin commands and
  captures stdout + stderr + _srlog.txt. Appends a final `quit` so the
  child always exits cleanly. Use to verify command-dispatch paths
  without the manual launcher + copy-paste loop.

.PARAMETER Commands
  Ordered list of command lines to feed to stdin (each is a sr_cmd
  verb; the harness appends `quit` at the end automatically).

.PARAMETER IntervalSec
  Seconds to pause between command lines. Give `begin` enough time for
  server-spawn + initial packet round before the next command fires.

.PARAMETER TimeoutSec
  Hard cap on WaitForExit after `quit` is sent; kill the child if it
  hasn't exited by then. Keeps CI-style runs from hanging.

.EXAMPLE
  .\test-sr.ps1 'newedit 80 50' 'begin' -IntervalSec 4 -TimeoutSec 45
#>
param(
  [Parameter(ValueFromRemainingArguments=$true)] [string[]]$Commands,
  [int]$IntervalSec = 3,
  [int]$TimeoutSec  = 30
)

$ErrorActionPreference = 'Stop'

$root = $PSScriptRoot
$dist = Join-Path $root 'dist-sr'
$env:FREECIV_DATA_PATH = Join-Path $root 'data'
$srlog = Join-Path $dist '_srlog.txt'
Remove-Item $srlog -ErrorAction SilentlyContinue

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName               = Join-Path $dist 'freeciv-sr.exe'
$psi.WorkingDirectory       = $dist
$psi.RedirectStandardInput  = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError  = $true
$psi.UseShellExecute        = $false
$psi.CreateNoWindow         = $true

$p = [System.Diagnostics.Process]::Start($psi)

# Collect stdout / stderr asynchronously so large bursts don't dead-
# lock the child writing into a full pipe buffer while we're sleeping.
$stdoutBuf = [System.Text.StringBuilder]::new()
$stderrBuf = [System.Text.StringBuilder]::new()
$onOut = {
  param($sender, $e)
  if ($null -ne $e.Data) { [void]$Event.MessageData.AppendLine($e.Data) }
}
Register-ObjectEvent -InputObject $p -EventName OutputDataReceived `
  -MessageData $stdoutBuf -Action $onOut | Out-Null
Register-ObjectEvent -InputObject $p -EventName ErrorDataReceived `
  -MessageData $stderrBuf -Action $onOut | Out-Null
$p.BeginOutputReadLine()
$p.BeginErrorReadLine()

foreach ($cmd in $Commands) {
  Write-Host ("[harness] >> " + $cmd)
  $p.StandardInput.WriteLine($cmd)
  $p.StandardInput.Flush()
  Start-Sleep -Seconds $IntervalSec
}
Write-Host "[harness] >> quit"
$p.StandardInput.WriteLine('quit')
$p.StandardInput.Flush()
$p.StandardInput.Close()

if (-not $p.WaitForExit($TimeoutSec * 1000)) {
  Write-Host "[harness] !! timeout ${TimeoutSec}s; killing pid $($p.Id)"
  $p.Kill()
  $p.WaitForExit(5000) | Out-Null
}
Start-Sleep -Milliseconds 250  # let async readers drain

Write-Host "`n--- stdout (pid $($p.Id), exit $($p.ExitCode)) ---"
Write-Host $stdoutBuf.ToString().TrimEnd()
Write-Host "`n--- stderr ---"
Write-Host $stderrBuf.ToString().TrimEnd()
Write-Host "`n--- _srlog.txt ---"
if (Test-Path $srlog) {
  Get-Content $srlog
} else {
  Write-Host "(no _srlog.txt)"
}
