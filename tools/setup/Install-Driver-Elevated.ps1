param(
    [Parameter(Mandatory=$true)]
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration,
    
    [Parameter(Mandatory=$true)]
    [ValidateSet('InstallDriver', 'Reinstall', 'UninstallDriver')]
    [string]$Action,

    # Start DebugView kernel capture around the test run and save to a log file.
    # Requires .github\skills\DbgView\DebugView\Dbgview.exe (run tools\setup\Install-DbgView.ps1 once).
    [Parameter(Mandatory=$false)]
    [switch]$CaptureDbgView
)

$ErrorActionPreference = 'Stop'
$repoRoot   = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent

try {
    $scriptPath     = Join-Path $PSScriptRoot 'Install-Driver.ps1'
    $transcriptPath = Join-Path $env:TEMP "install-driver-$(Get-Date -Format yyyyMMdd_HHmmss).log"

    # Write a tiny wrapper that enables transcript capture and calls the real script
    $tempScript = [System.IO.Path]::GetTempFileName() -replace '\.tmp$', '.ps1'
    @"
Start-Transcript -Path '$transcriptPath' -Force | Out-Null
try {
    & '$scriptPath' -Configuration $Configuration -$Action
} catch {
    Write-Host "EXCEPTION: `$_" -ForegroundColor Red
    exit 1
} finally {
    Stop-Transcript | Out-Null
}
"@ | Set-Content $tempScript

    $arguments = @(
        '-NoProfile'
        '-ExecutionPolicy'
        'Bypass'
        '-File'
        $tempScript
    )

    # ── Optional: start DebugView kernel capture ───────────────────────────────────
    $dbgViewProc = $null
    if ($CaptureDbgView) {
        $dbgViewScript = Join-Path $repoRoot '.github\skills\DbgView\Start-DbgViewCapture.ps1'
        if (Test-Path $dbgViewScript) {
            # Derive log stem from test name or suite label
            $dbgLogStem = "dbgview_install-driver-$($Action.ToLower())"
            Write-Host "[DbgView] Starting kernel capture (stem: $dbgLogStem)..." -ForegroundColor Cyan
            $dbgViewProc = & $dbgViewScript -LogName $dbgLogStem
            if ($dbgViewProc) {
                Write-Host "[DbgView] Capturing on PID=$($dbgViewProc.Id)" -ForegroundColor Green
                Start-Sleep -Milliseconds 500   # give DbgView time to open the log file
            }
        } else {
            Write-Warning "[DbgView] Start script not found at '$dbgViewScript'. Skipping capture."
        }
    }

    Start-Process powershell -Verb RunAs -ArgumentList $arguments -Wait

    # ── Stop DebugView if we started it ───────────────────────────────────────────
    if ($dbgViewProc) {
        $stopScript = Join-Path $repoRoot '.github\skills\DbgView\Stop-DbgViewCapture.ps1'
        if (Test-Path $stopScript) {
            & $stopScript -ProcessId $dbgViewProc.Id
        } else {
            Stop-Process -Id $dbgViewProc.Id -Force -ErrorAction SilentlyContinue
            Write-Host "[DbgView] Stopped PID=$($dbgViewProc.Id)" -ForegroundColor Green
        }
    }
    # Display transcript so output is visible in this (non-elevated) window
    if (Test-Path $transcriptPath) {
        Write-Host ""
        Write-Host "=== Elevated install output ===" -ForegroundColor Cyan
        Get-Content $transcriptPath |
            Where-Object { $_ -notmatch "^Windows PowerShell transcript" -and
                           $_ -notmatch "^(Start|End) time:|^(Username|RunAs user|Configuration|Machine):" } |
            Write-Host
    } else {
        Write-Host "WARNING: No transcript captured (UAC may have been denied or script crashed before Start-Transcript)" -ForegroundColor Yellow
    }

    Remove-Item $tempScript -Force -ErrorAction SilentlyContinue

} catch {
    Write-Host "ERROR: Failed to launch elevated script" -ForegroundColor Red
    Write-Host "  $_" -ForegroundColor Yellow
    exit 1
}
