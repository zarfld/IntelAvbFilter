# Stop-DbgViewCapture.ps1
# Stops DebugView after automated tests.
#
# Usage:
#   # Stop the specific instance started by Start-DbgViewCapture.ps1:
#   $proc = .\.github\skills\DbgView\Start-DbgViewCapture.ps1 -LogName "avb_i210_test"
#   # ... run tests ...
#   .\.github\skills\DbgView\Stop-DbgViewCapture.ps1 -ProcessId $proc.Id
#
#   # Stop all running DebugView processes (no PID needed):
#   .\.github\skills\DbgView\Stop-DbgViewCapture.ps1
#
# Skill: dbgview-capture  (.github/skills/DbgView/SKILL.md)

param(
    # PID of the DebugView process to stop.
    # When omitted, ALL running Dbgview* processes are stopped.
    [int] $ProcessId = 0
)

$ErrorActionPreference = 'Stop'

if ($ProcessId -gt 0) {
    # ── Stop specific instance ─────────────────────────────────────────────────
    $proc = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    if ($proc) {
        Stop-Process -Id $ProcessId -Force
        Write-Host "[DbgView] Stopped PID=$ProcessId ($($proc.Name))" -ForegroundColor Green
    } else {
        Write-Warning "[DbgView] No process found with PID=$ProcessId (already exited?)"
    }
} else {
    # ── Stop all DebugView instances ───────────────────────────────────────────
    $procs = Get-Process -Name "Dbgview*" -ErrorAction SilentlyContinue
    if ($procs) {
        foreach ($p in $procs) {
            Stop-Process -Id $p.Id -Force
            Write-Host "[DbgView] Stopped PID=$($p.Id) ($($p.Name))" -ForegroundColor Green
        }
    } else {
        Write-Host "[DbgView] No running DebugView processes found." -ForegroundColor Yellow
    }
}
