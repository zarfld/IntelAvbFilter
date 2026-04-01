# pre-commit-update-evidence.ps1
#
# Pre-commit hook entry point: converts latest test logs to JUnit XML and
# stages the generated test-evidence files so they are included in the commit.
#
# Invoked by .pre-commit-config.yaml when src/*.c or src/*.h files are staged.
# Standard "auto-fixer" pattern: exits 1 when files were updated so pre-commit
# aborts and prompts the user to review the staged evidence, then re-commit.
#
# Implements: #265 (TEST-COVERAGE-001)

$ErrorActionPreference = 'Stop'

$scriptDir = $PSScriptRoot
$repoRoot  = Split-Path (Split-Path $scriptDir -Parent) -Parent

$converter = Join-Path $scriptDir 'Convert-Logs-To-JUnit.ps1'
if (-not (Test-Path $converter)) {
    Write-Error "Convert-Logs-To-JUnit.ps1 not found at: $converter"
    exit 1
}

# Capture hash of evidence files before conversion
$evidenceDir = Join-Path $repoRoot 'test-evidence'
$before = @{}
if (Test-Path $evidenceDir) {
    Get-ChildItem $evidenceDir -Filter 'hardware-results-*.xml' |
        ForEach-Object { $before[$_.Name] = (Get-FileHash $_.FullName).Hash }
    Get-ChildItem $evidenceDir -Filter 'traceability-summary-*.md' |
        ForEach-Object { $before[$_.Name] = (Get-FileHash $_.FullName).Hash }
}

# Run the conversion (exits 1 if test failures found — we propagate that)
& $converter
$convExit = $LASTEXITCODE

if ($convExit -ne 0) {
    Write-Host ""
    Write-Host "[pre-commit] Convert-Logs-To-JUnit.ps1 exited $convExit (test failures in logs)" -ForegroundColor Red
    exit $convExit
}

# Detect and stage new or changed evidence files
$changed = [System.Collections.Generic.List[string]]::new()
if (Test-Path $evidenceDir) {
    Get-ChildItem $evidenceDir -Filter 'hardware-results-*.xml' | ForEach-Object {
        $h = (Get-FileHash $_.FullName).Hash
        if (-not $before.ContainsKey($_.Name) -or $before[$_.Name] -ne $h) {
            $changed.Add($_.FullName)
        }
    }
    Get-ChildItem $evidenceDir -Filter 'traceability-summary-*.md' | ForEach-Object {
        $h = (Get-FileHash $_.FullName).Hash
        if (-not $before.ContainsKey($_.Name) -or $before[$_.Name] -ne $h) {
            $changed.Add($_.FullName)
        }
    }
}

if ($changed.Count -gt 0) {
    Write-Host ""
    Write-Host "[pre-commit] Test evidence updated — staging:" -ForegroundColor Yellow
    foreach ($f in $changed) {
        $rel = Resolve-Path -Relative $f
        Write-Host "  $rel" -ForegroundColor Yellow
        git add -- $f
    }
    Write-Host ""
    Write-Host "[pre-commit] Re-run 'git commit' to include the updated test evidence." -ForegroundColor Yellow
    exit 1
}

Write-Host "[pre-commit] Test evidence unchanged — no files to stage." -ForegroundColor Cyan
exit 0
