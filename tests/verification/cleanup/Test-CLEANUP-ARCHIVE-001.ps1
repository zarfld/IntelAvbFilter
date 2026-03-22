# Test-CLEANUP-ARCHIVE-001.ps1
# Verifies: #294 (TEST-CLEANUP-ARCHIVE-001: Obsolete File Archival / Repo Organization)
# Implements: #293 (REQ-NF-CLEANUP-002: Archive Obsolete/Redundant Source Files)
# Traces to: #293
#
# Purpose: Prevent the repo from accumulating obsolete/redundant source files.
#   - Verifies archive directories exist with READMEs
#   - Verifies known-archived files are present in archive (not accidentally deleted)
#   - Scans active source tree for dirty naming patterns (_old, _backup, _fixed_new, etc.)
#   - Verifies deprecated tools are in tools/archive/deprecated/
#   - Verifies no archived files have leaked back into active source paths
#
# Run: .\Test-CLEANUP-ARCHIVE-001.ps1
# Run (verbose): .\Test-CLEANUP-ARCHIVE-001.ps1 -Verbose

param(
    [switch]$Verbose
)

$ErrorActionPreference = 'Continue'
$RepoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))

Write-Host "=== TEST-CLEANUP-ARCHIVE-001: Repo Archival & Organization ===" -ForegroundColor Cyan
Write-Host "Repo root: $RepoRoot" -ForegroundColor Gray
Write-Host ""

$Pass = 0
$Fail = 0
$Skip = 0
$Failures = @()

function Report-Pass($tc, $msg) {
    Write-Host "  PASS  $tc`: $msg" -ForegroundColor Green
    $script:Pass++
}

function Report-Fail($tc, $msg) {
    Write-Host "  FAIL  $tc`: $msg" -ForegroundColor Red
    $script:Fail++
    $script:Failures += "$tc`: $msg"
}

function Report-Skip($tc, $msg) {
    Write-Host "  SKIP  $tc`: $msg" -ForegroundColor Yellow
    $script:Skip++
}

# ---------------------------------------------------------------------------
# TC-CLEANUP-001: Archive directories exist with README.md
# ---------------------------------------------------------------------------
Write-Host "--- TC-CLEANUP-001: Archive directories have README.md ---" -ForegroundColor Cyan

$ArchiveDirs = @(
    "archive\filter_old",
    "archive\backup",
    "archive\avb_integration_old",
    "tools\archive\deprecated"
)

foreach ($Dir in $ArchiveDirs) {
    $FullDir = Join-Path $RepoRoot $Dir
    if (Test-Path $FullDir) {
        $ReadmePath = Join-Path $FullDir "README.md"
        if (Test-Path $ReadmePath) {
            Report-Pass "TC-CLEANUP-001" "$Dir\README.md exists"
        } else {
            Report-Fail "TC-CLEANUP-001" "$Dir exists but README.md is missing"
        }
    } else {
        Report-Fail "TC-CLEANUP-001" "Archive directory missing: $Dir"
    }
}

# ---------------------------------------------------------------------------
# TC-CLEANUP-002: Known-archived files are present in archive (not lost)
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "--- TC-CLEANUP-002: Archived files are present in archive ---" -ForegroundColor Cyan

$ArchivedFiles = @(
    "archive\filter_old\filter_august.c",
    "archive\filter_old\filter_august.txt",
    "archive\backup\temp_filter_7624c57.c",
    "archive\backup\intel_tsn_enhanced_implementations.c",
    "archive\backup\tsn_hardware_activation_investigation.c",
    "archive\avb_integration_old\avb_integration.c",
    "archive\avb_integration_old\avb_context_management.c",
    "archive\avb_integration_old\avb_integration_fixed_backup.c",
    "archive\avb_integration_old\avb_integration_fixed_complete.c",
    "archive\avb_integration_old\avb_integration_fixed_new.c",
    "archive\avb_integration_old\avb_integration_hardware_only.c"
)

foreach ($RelPath in $ArchivedFiles) {
    $FullPath = Join-Path $RepoRoot $RelPath
    if (Test-Path $FullPath) {
        Report-Pass "TC-CLEANUP-002" "Archived file present: $RelPath"
    } else {
        Report-Fail "TC-CLEANUP-002" "Archived file MISSING from archive: $RelPath"
    }
}

# ---------------------------------------------------------------------------
# TC-CLEANUP-003: No archived files have leaked back into active source paths
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "--- TC-CLEANUP-003: Archived files not in active source paths ---" -ForegroundColor Cyan

# These are the filenames (not paths) that must not appear in active source
$ArchivedFileNames = @(
    "filter_august.c",
    "temp_filter_7624c57.c",
    "intel_tsn_enhanced_implementations.c",
    "tsn_hardware_activation_investigation.c",
    "avb_context_management.c",
    "avb_integration_fixed_backup.c",
    "avb_integration_fixed_complete.c",
    "avb_integration_fixed_new.c",
    "avb_integration_hardware_only.c"
)

# Active source directories (exclude archive/ and build/ and external/)
$ActiveSourcePaths = @(
    (Join-Path $RepoRoot "src")
    $RepoRoot  # root .c/.h files
)

$Leaked = @()
foreach ($Name in $ArchivedFileNames) {
    # Search repo root *.c *.h (non-recursive, just root level)
    $RootMatch = Get-ChildItem -Path $RepoRoot -Filter $Name -File 2>$null | Where-Object { $_.DirectoryName -eq $RepoRoot }
    if ($RootMatch) { $Leaked += $RootMatch.FullName }

    # Search src/ recursively
    $SrcDir = Join-Path $RepoRoot "src"
    if (Test-Path $SrcDir) {
        $SrcMatch = Get-ChildItem -Path $SrcDir -Recurse -Filter $Name -File 2>$null
        if ($SrcMatch) { $Leaked += $SrcMatch.FullName }
    }
}

if ($Leaked.Count -eq 0) {
    Report-Pass "TC-CLEANUP-003" "No archived files found in active source paths"
} else {
    foreach ($LeakPath in $Leaked) {
        $Rel = $LeakPath.Substring($RepoRoot.Length + 1)
        Report-Fail "TC-CLEANUP-003" "Archived file leaked into active sources: $Rel"
    }
}

# ---------------------------------------------------------------------------
# TC-CLEANUP-004: Active source tree has no dirty naming patterns
# Patterns that indicate code rot: *_old.c, *_backup.c, *_v2.c, *_new.c,
# *_fixed_new.c, *_temp.c, *_final.c, *_copy.c, *2.c (digit suffix)
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "--- TC-CLEANUP-004: No dirty naming patterns in active sources ---" -ForegroundColor Cyan

$DirtyPatterns = @(
    '*_old.c', '*_old.h',
    '*_backup.c', '*_backup.h',
    '*_v2.c', '*_v2.h', '*_v3.c',
    '*_new.c', '*_new.h',
    '*_fixed_new.c', '*_fixed_complete.c',
    '*_temp.c', '*_temp.h',
    '*_final.c', '*_final.h',
    '*_copy.c', '*_copy.h',
    '*2.c', '*2.h',
    '*_old2.c', '*_backup2.c'
)

# Directory segments to exclude (any path containing these segments is skipped)
$ScanExcludes = @("archive", "build", "external", ".git", ".venv", "node_modules", "intel-ethernet-regs", "intel_avb", "wil")

$DirtyFiles = @()
foreach ($Pattern in $DirtyPatterns) {
    # Use Where-Object -like to match against the long filename only
    # (Get-ChildItem -Filter uses the Windows API which also matches 8.3 short names causing false positives)
    $Found = Get-ChildItem -Path $RepoRoot -Recurse -File 2>$null |
        Where-Object {
            if ($_.Name -notlike $Pattern) { return $false }
            $Rel = $_.FullName.Substring($RepoRoot.Length + 1)
            $Parts = $Rel -split '[\\\/]'
            foreach ($Ex in $ScanExcludes) {
                if ($Parts -contains $Ex) { return $false }
            }
            return $true
        }
    if ($Found) { $DirtyFiles += $Found }
}

if ($DirtyFiles.Count -eq 0) {
    Report-Pass "TC-CLEANUP-004" "No dirty naming patterns found in active source tree"
} else {
    foreach ($F in ($DirtyFiles | Sort-Object FullName)) {
        $Rel = $F.FullName.Substring($RepoRoot.Length + 1)
        Report-Fail "TC-CLEANUP-004" "Dirty-named file in active sources: $Rel"
    }
}

# ---------------------------------------------------------------------------
# TC-CLEANUP-005: tools/archive/deprecated contains expected sub-categories
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "--- TC-CLEANUP-005: tools/archive/deprecated structure intact ---" -ForegroundColor Cyan

$DeprecatedRoot = Join-Path $RepoRoot "tools\archive\deprecated"
$ExpectedSubfolders = @("development", "setup", "test")

if (Test-Path $DeprecatedRoot) {
    foreach ($Sub in $ExpectedSubfolders) {
        $SubPath = Join-Path $DeprecatedRoot $Sub
        if (Test-Path $SubPath) {
            $Count = (Get-ChildItem $SubPath -File).Count
            Report-Pass "TC-CLEANUP-005" "deprecated\$Sub exists ($Count files)"
        } else {
            Report-Fail "TC-CLEANUP-005" "deprecated\$Sub subfolder missing"
        }
    }
    # At least 10 deprecated scripts at root level
    $RootScripts = (Get-ChildItem $DeprecatedRoot -File).Count
    if ($RootScripts -ge 10) {
        Report-Pass "TC-CLEANUP-005" "deprecated\ root has $RootScripts script files (expected >=10)"
    } else {
        Report-Fail "TC-CLEANUP-005" "deprecated\ root has only $RootScripts files (expected >=10) -- possible accidental deletion"
    }
} else {
    Report-Fail "TC-CLEANUP-005" "tools\archive\deprecated directory missing entirely"
}

# ---------------------------------------------------------------------------
# TC-CLEANUP-006: No stray *.c/*.h files at repo root (driver sources in src/ only)
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "--- TC-CLEANUP-006: No unexpected .c/.h files at repo root ---" -ForegroundColor Cyan

# Allowed .c/.h at repo root (filter driver entry points that are legitimately at root)
$AllowedRootSources = @(
    "filter.c",
    "device.c",
    "avb_integration_fixed.c",
    "avb_hal.c",
    "ptp_hal.c",
    "avb_ndis.c"
)

$RootCFiles = Get-ChildItem -Path $RepoRoot -File 2>$null | Where-Object { $_.DirectoryName -eq $RepoRoot -and $_.Extension -in @('.c','.h') }
$StrayFiles = @()
foreach ($F in $RootCFiles) {
    if ($AllowedRootSources -notcontains $F.Name) {
        $StrayFiles += $F.Name
    }
}

if ($StrayFiles.Count -eq 0) {
    $RootSourceCount = $RootCFiles.Count
    Report-Pass "TC-CLEANUP-006" "Root has $RootSourceCount .c/.h files -- all are expected driver sources"
} else {
    foreach ($S in $StrayFiles) {
        Report-Fail "TC-CLEANUP-006" "Unexpected .c/.h at repo root: $S"
    }
}

# ---------------------------------------------------------------------------
# TC-CLEANUP-007: No stray scripts (.ps1, .py, .bat, .cmd, .sh) at repo root
# Scripts belong in tools/ (active) or tools/archive/deprecated/ (retired)
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "--- TC-CLEANUP-007: No stray scripts at repo root ---" -ForegroundColor Cyan

$StrayScripts = Get-ChildItem -Path $RepoRoot -File 2>$null | Where-Object {
    $_.DirectoryName -eq $RepoRoot -and
    $_.Extension -in @('.ps1', '.py', '.bat', '.cmd', '.sh')
}

if ($StrayScripts.Count -eq 0) {
    Report-Pass "TC-CLEANUP-007" "No stray scripts (.ps1/.py/.bat/.cmd/.sh) at repo root"
} else {
    foreach ($S in $StrayScripts) {
        Report-Fail "TC-CLEANUP-007" "Stray script at repo root: $($S.Name) -- move to tools/ or tools/archive/deprecated/"
    }
}

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "TEST-CLEANUP-ARCHIVE-001 Results" -ForegroundColor Cyan
Write-Host "  PASS: $Pass  FAIL: $Fail  SKIP: $Skip  TOTAL: $($Pass+$Fail+$Skip)" -ForegroundColor Cyan

if ($Fail -gt 0) {
    Write-Host ""
    Write-Host "FAILURES:" -ForegroundColor Red
    foreach ($F in $Failures) {
        Write-Host "  - $F" -ForegroundColor Red
    }
    Write-Host ""
    Write-Host "=== TEST-CLEANUP-ARCHIVE-001 FAILED ===" -ForegroundColor Red
    exit 1
} else {
    Write-Host ""
    Write-Host "=== TEST-CLEANUP-ARCHIVE-001 PASSED ===" -ForegroundColor Green
    exit 0
}
