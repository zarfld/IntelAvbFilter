param(
    [Parameter(Mandatory=$true)]
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration,
    
    [Parameter(Mandatory=$true)]
    [ValidateSet('InstallDriver', 'Reinstall', 'UninstallDriver')]
    [string]$Action
)

$ErrorActionPreference = 'Stop'

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

    Start-Process powershell -Verb RunAs -ArgumentList $arguments -Wait

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
