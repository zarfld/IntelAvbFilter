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

# Build the temp wrapper script BEFORE the try/catch block.
# Using individual strings avoids PS5.1 parser issues with here-strings
# adjacent to other constructs in the same script file.
$scriptPath     = Join-Path $PSScriptRoot 'Install-Driver.ps1'
$transcriptPath = Join-Path $env:TEMP "install-driver-$(Get-Date -Format yyyyMMdd_HHmmss).log"
$tempScript     = [System.IO.Path]::GetTempFileName() -replace '\.tmp$', '.ps1'

# Write temp wrapper: captures transcript AND preserves exit code across Stop-Transcript.
# Lines using single quotes produce literal $-variables for the child script.
# Lines using double quotes expand $scriptPath/$transcriptPath/$Configuration/$Action here.
$tempLines = @(
    "Start-Transcript -Path '$transcriptPath' -Force | Out-Null",
    '$_installExitCode = 0',
    'try {',
    "    & '$scriptPath' -Configuration $Configuration -$Action",
    '    $_installExitCode = $LASTEXITCODE',
    '    if ($null -eq $_installExitCode) { $_installExitCode = 0 }',
    '} catch {',
    '    Write-Host "EXCEPTION during Install-Driver.ps1: $_" -ForegroundColor Red',
    '    $_installExitCode = 1',
    '} finally {',
    '    Stop-Transcript | Out-Null',
    '}',
    'exit $_installExitCode'
)
[System.IO.File]::WriteAllText($tempScript, ($tempLines -join "`r`n"), [System.Text.Encoding]::UTF8)

try {
    $arguments = @(
        '-NoProfile'
        '-ExecutionPolicy'
        'Bypass'
        '-File'
        $tempScript
    )

    # -- Optional: start DebugView kernel capture -----------------------------------
    $dbgViewProc = $null
    if ($CaptureDbgView) {
        $dbgViewScript = Join-Path $repoRoot '.github\skills\DbgView\Start-DbgViewCapture.ps1'
        if (Test-Path $dbgViewScript) {
            $dbgLogStem = "dbgview_install-driver-$($Action.ToLower())"
            Write-Host "[DbgView] Starting kernel capture (stem: $dbgLogStem)..." -ForegroundColor Cyan
            $dbgViewProc = & $dbgViewScript -LogName $dbgLogStem
            if ($dbgViewProc) {
                Write-Host "[DbgView] Started PID=$($dbgViewProc.Id)" -ForegroundColor Green
                Write-Host "[DbgView] Capturing on PID=$($dbgViewProc.Id)" -ForegroundColor Green
                Start-Sleep -Milliseconds 500
            }
        } else {
            Write-Warning "[DbgView] Start script not found at '$dbgViewScript'. Skipping capture."
        }
    }

    # -- Launch elevated child and capture its exit code ------------------------
    # -PassThru is required so we can read ExitCode after -Wait.
    # Distinguish three failure modes:
    #   - UAC cancelled / launch failed ($childProc is null or ExitCode unavailable)
    #   - Install-Driver.ps1 failed (childProc.ExitCode != 0)
    #   - Success (childProc.ExitCode == 0)
    $childProc = Start-Process powershell -Verb RunAs -ArgumentList $arguments -Wait -PassThru -ErrorAction SilentlyContinue
    $installExitCode = if ($null -eq $childProc) {
        Write-Host "WARNING: Elevated process did not start -- UAC may have been cancelled or launch failed" -ForegroundColor Yellow
        -1
    } elseif ($null -eq $childProc.ExitCode) {
        Write-Host "WARNING: Elevated process exit code unavailable (process object invalid)" -ForegroundColor Yellow
        -1
    } else {
        $childProc.ExitCode
    }

    # -- Stop DebugView if we started it ---------------------------------------
    if ($dbgViewProc) {
        $stopScript = Join-Path $repoRoot '.github\skills\DbgView\Stop-DbgViewCapture.ps1'
        if (Test-Path $stopScript) {
            & $stopScript -ProcessId $dbgViewProc.Id
        } else {
            Stop-Process -Id $dbgViewProc.Id -Force -ErrorAction SilentlyContinue
            Write-Host "[DbgView] Stopped PID=$($dbgViewProc.Id) (Dbgview)" -ForegroundColor Green
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
        if ($installExitCode -eq -1) {
            Write-Host "WARNING: No transcript and no elevated process -- UAC was likely cancelled" -ForegroundColor Yellow
        } else {
            Write-Host "WARNING: No transcript captured (script may have crashed before Start-Transcript)" -ForegroundColor Yellow
        }
    }

    Remove-Item $tempScript -Force -ErrorAction SilentlyContinue

    # Propagate the install result to the caller.
    # Exit codes:
    #   0   = success
    #   1   = Install-Driver.ps1 reported failure
    #  -1   = UAC cancelled or process launch failed (treated as error by caller)
    if ($installExitCode -ne 0) {
        if ($installExitCode -eq -1) {
            Write-Host "ERROR: Elevated install did not complete -- UAC cancelled or launch failed" -ForegroundColor Red
        } else {
            Write-Host "ERROR: Install-Driver.ps1 exited with code $installExitCode" -ForegroundColor Red
        }
        exit 1
    }

} catch {
    Write-Host "ERROR: Failed to launch elevated install script" -ForegroundColor Red
    Write-Host "  $_" -ForegroundColor Yellow
    exit 1
}
