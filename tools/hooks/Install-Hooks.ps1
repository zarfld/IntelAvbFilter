#Requires -Version 5.1
<#
.SYNOPSIS
    Installs the pre-commit hook that enforces SSOT and magic-number rules.

.DESCRIPTION
    Copies tools/hooks/pre-commit into .git/hooks/ so Git runs it before
    every commit.

    Enforced checks:
      SSOT-001  No duplicate IOCTL definitions outside include/avb_ioctl.h
      SSOT-003  All C/H files use the SSOT header include path
      SSOT-004  SSOT header contains all required IOCTLs
      REGS-002  No raw magic-number hex literals in src/ or devices/

.EXAMPLE
    # Run once after cloning the repo:
    powershell -NoProfile -ExecutionPolicy Bypass -File tools/hooks/Install-Hooks.ps1

.NOTES
    Uninstall:  Remove-Item .git\hooks\pre-commit
    Bypass:     git commit --no-verify  (emergency use only)
#>

$ErrorActionPreference = 'Stop'

$RepoRoot      = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$GitHooksDir   = Join-Path $RepoRoot '.git\hooks'
$SourceHook    = Join-Path $PSScriptRoot 'pre-commit'
$TargetHook    = Join-Path $GitHooksDir  'pre-commit'

Write-Host ""
Write-Host "Installing pre-commit hook..." -ForegroundColor Cyan
Write-Host "  Source : $SourceHook"        -ForegroundColor Gray
Write-Host "  Target : $TargetHook"        -ForegroundColor Gray
Write-Host ""

# Validate prerequisites
if (-not (Test-Path $GitHooksDir)) {
    Write-Host "ERROR: .git/hooks directory not found." -ForegroundColor Red
    Write-Host "       Make sure you are inside a Git repository." -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $SourceHook)) {
    Write-Host "ERROR: Source hook not found: $SourceHook" -ForegroundColor Red
    exit 1
}

# Back up any existing hook
if (Test-Path $TargetHook) {
    $backupName = "pre-commit.backup.$(Get-Date -Format 'yyyyMMddHHmmss')"
    $backupPath = Join-Path $GitHooksDir $backupName
    Copy-Item $TargetHook -Destination $backupPath -Force
    Write-Host "  Backed up existing hook -> .git/hooks/$backupName" -ForegroundColor Yellow
}

# Install
Copy-Item $SourceHook -Destination $TargetHook -Force

# Git for Windows needs the execute bit set so bash will run the file.
# The simplest reliable way is via git itself.
Push-Location $RepoRoot
try {
    & git update-index --chmod=+x "tools/hooks/pre-commit" 2>$null
} catch { }
Pop-Location

Write-Host "  Installed: .git/hooks/pre-commit" -ForegroundColor Green
Write-Host ""
Write-Host "Enforcement summary:" -ForegroundColor Cyan
Write-Host "  SSOT-001  No duplicate IOCTL definitions    (tests/verification/ssot/)" -ForegroundColor Gray
Write-Host "  SSOT-003  SSOT include-path compliance      (tests/verification/ssot/)" -ForegroundColor Gray
Write-Host "  SSOT-004  SSOT header completeness          (tests/verification/ssot/)" -ForegroundColor Gray
Write-Host "  REGS-002  No magic-number hex literals      (tests/verification/regs/)" -ForegroundColor Gray
Write-Host ""
Write-Host "Triggered on: commits staging .c / .h / .cpp files in" -ForegroundColor Gray
Write-Host "              src/, devices/, include/, tests/, or tools/" -ForegroundColor Gray
Write-Host ""
Write-Host "Uninstall : Remove-Item .git\hooks\pre-commit" -ForegroundColor DarkGray
Write-Host "Bypass    : git commit --no-verify  (emergency use only)" -ForegroundColor DarkGray
Write-Host ""
exit 0
