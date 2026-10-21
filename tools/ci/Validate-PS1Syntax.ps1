# Validate-PS1Syntax.ps1
# Validates Windows PowerShell 5.1 parser compatibility for CI-critical scripts.
#
# Run this step BEFORE building driver or tests so parser errors in infrastructure
# scripts are caught early rather than failing mid-job with a cryptic error.
#
# Uses the built-in PowerShell parser ([System.Management.Automation.Language.Parser])
# which reflects exactly what the Windows PowerShell 5.1 engine accepts at load time.
#
# Exit codes:
#   0  All listed files parse without error.
#   1  One or more files contain parser errors (printed to stdout).
#
# Usage (CI):
#   powershell -ExecutionPolicy Bypass -NonInteractive -File tools\ci\Validate-PS1Syntax.ps1
#
# Usage (local):
#   .\tools\ci\Validate-PS1Syntax.ps1
#
# Implements: REQ-NF-SCRIPTS-001 (#27) — early CI syntax gate

param(
    # Override the default list of files to validate.  Relative paths are resolved
    # against the repository root (the parent of this script's tools\ci\ directory).
    [Parameter(Mandatory=$false)]
    [string[]]$Files = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path $ScriptDir -Parent) -Parent

# Default set: every PowerShell script that the CI job directly sources or invokes.
# Update this list when new CI-critical scripts are added.
if ($Files.Count -eq 0) {
    $Files = @(
        "tools\test\Run-Tests-CI.ps1",
        "tools\test\Test-DriverPreFlight.ps1",
        "tools\build\Build-Tests.ps1",
        "tools\test\Parse-TestResults.ps1",
        "tools\test\Convert-Logs-To-JUnit.ps1",
        "tools\lib\TestVerdict.ps1",
        "tools\lib\NativeExit.ps1",
        "tools\lib\SourceDigest.ps1",
        "tools\lib\DriverPackage.ps1",
        "tools\lib\DriverDeploy.ps1",
        "tools\lib\HilLock.ps1",
        "tools\lib\HilMachineHealth.ps1",
        "tools\ci\Invoke-HilDeployment.ps1",
        "tools\setup\Install-Driver.ps1",
        "tools\build\Build-Driver.ps1",
        "tools\ci\Validate-PS1Syntax.ps1"
    )
}

Write-Host ""
Write-Host "===================================================================" -ForegroundColor Cyan
Write-Host "  PowerShell 5.1 syntax validation (tools\ci\Validate-PS1Syntax.ps1)" -ForegroundColor Cyan
Write-Host "===================================================================" -ForegroundColor Cyan
Write-Host "  PSVersion : $($PSVersionTable.PSVersion)" -ForegroundColor Gray
Write-Host "  PSEdition : $($PSVersionTable.PSEdition)" -ForegroundColor Gray
Write-Host ""

$failed = 0
$total  = 0

foreach ($relOrAbsPath in $Files) {
    $total++
    $absPath = if ([System.IO.Path]::IsPathRooted($relOrAbsPath)) {
        $relOrAbsPath
    } else {
        Join-Path $RepoRoot $relOrAbsPath
    }

    if (-not (Test-Path $absPath)) {
        Write-Host ("[MISS] {0}" -f $relOrAbsPath) -ForegroundColor Red
        $failed++
        continue
    }

    $parseErrors = $null
    $null = [System.Management.Automation.Language.Parser]::ParseFile(
        $absPath, [ref]$null, [ref]$parseErrors)

    if ($parseErrors.Count -eq 0) {
        Write-Host ("[OK]   {0}" -f $relOrAbsPath) -ForegroundColor Green
    } else {
        Write-Host ("[FAIL] {0} -- {1} parser error(s)" -f $relOrAbsPath, $parseErrors.Count) -ForegroundColor Red
        foreach ($e in $parseErrors) {
            Write-Host ("       L{0}c{1}: {2}" -f $e.Extent.StartLineNumber, $e.Extent.StartColumnNumber, $e.Message) -ForegroundColor Red
        }
        $failed++
    }
}

Write-Host ""
if ($failed -gt 0) {
    Write-Host ("FAIL: {0}/{1} file(s) have PowerShell 5.1 parser errors." -f $failed, $total) -ForegroundColor Red
    Write-Host "      Fix the errors listed above and re-run before building or testing." -ForegroundColor Red
    exit 1
}

Write-Host ("OK: {0}/{0} file(s) pass PowerShell 5.1 syntax check." -f $total) -ForegroundColor Green
exit 0
