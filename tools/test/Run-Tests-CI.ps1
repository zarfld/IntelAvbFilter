# Run-Tests-CI.ps1
# CI-specific test runner for GitHub Actions and other headless CI environments.
#
# RATIONALE — Three-script test architecture:
#   Run-Tests-Elevated.ps1  LOCAL DEV only — spawns elevated shell via Start-Process -Verb RunAs.
#                           Interactive UAC required; caller terminal shows nothing (logs only).
#   Run-Tests.ps1           ADMIN DEV / full suite — checks driver service, Intel hardware,
#                           device node. Requires the driver to be installed and hardware present.
#   Run-Tests-CI.ps1        CI use — GitHub runner is already Administrator; no hardware/driver
#                           expected; hardware-independent tests only; no interactive prompts.
#
# Implements: #27 (REQ-NF-SCRIPTS-001: Consolidated Script Architecture)
# See also: tools/test/Run-Tests-Elevated.ps1, tools/test/Run-Tests.ps1

param(
    [Parameter(Mandatory=$false)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    # Suite selects the built-in hardware-independent test list.
    # "Unit" = fast unit tests (unit-tests CI job)
    # "Integration" = integration-level tests (integration-tests CI job)
    # "All" = both lists combined
    [Parameter(Mandatory=$false)]
    [ValidateSet("Unit", "Integration", "All")]
    [string]$Suite = "Unit",

    # Override the test list entirely (names without .exe extension).
    [Parameter(Mandatory=$false)]
    [string[]]$Tests = @(),

    # Stop after first failing test instead of running all.
    [Parameter(Mandatory=$false)]
    [switch]$FailFast
)

$ErrorActionPreference = 'Stop'

# ===========================
# No admin check here — by design.
#
# Run-Tests.ps1 (the full local runner) requires admin because it accesses the
# driver device node (\\.\IntelAvbFilter) and runs hardware checks.
#
# Run-Tests-CI.ps1 only runs hardware-INDEPENDENT user-mode test executables
# that have no driver or device dependency, so admin is NOT required.
#
# GitHub Actions Windows runners run with a non-elevated token by default even
# though the account is in the Administrators group.  Checking IsInRole here
# would cause an immediate exit-1 on every CI run.
# ===========================

# ===========================
# Helper functions
# ===========================
function Write-Step   { param([string]$m) Write-Host "`n==> $m" -ForegroundColor Cyan }
function Write-Ok     { param([string]$m) Write-Host "[OK]   $m" -ForegroundColor Green }
function Write-Fail   { param([string]$m) Write-Host "[FAIL] $m" -ForegroundColor Red }
function Write-Skip   { param([string]$m) Write-Host "[SKIP] $m" -ForegroundColor DarkGray }
function Write-Note   { param([string]$m) Write-Host "[INFO] $m" -ForegroundColor Yellow }

# ===========================
# Built-in hardware-independent test lists
# ===========================
$UnitTests = @(
    # Pure static/logic tests — no driver device, no hardware, run fine on CI.
    "test_register_constants",
    "test_magic_numbers",
    "test_scripts_consolidate",
    "test_hal_unit",
    "test_hal_errors"
)

# These tests open \\.\IntelAvbFilter or exercise hardware paths; they require
# the driver to be installed and will always fail on a runner without the NIC.
# NOT added to $UnitTests / $IntegrationTests — documented here for visibility.
$HardwareDependentTests = @(
    "test_cleanup_archive",        # checks build output structure against installed driver
    "test_ioctl_simple",           # opens \\.\IntelAvbFilter via CreateFile
    "test_ioctl_routing",          # DeviceIoControl — driver must be loaded
    "test_ioctl_trace",            # DeviceIoControl trace IOCTLs
    "test_minimal_ioctl",          # minimal IOCTL round-trip to driver
    "test_clock_config",           # hardware clock register access
    "test_clock_working",          # PTP clock hardware test
    "test_direct_clock",           # direct clock register read/write
    "test_hal_performance",        # HAL timer/DMA performance — needs NIC
    "test_atdecc_event",           # ATDECC event subscription via driver IOCTL
    "test_atdecc_aen_protocol",    # ATDECC AEN protocol — driver IOCTL path
    "test_ioctl_version",          # IOCTL_AVB_GET_VERSION to driver
    "test_mdio_phy",               # MDIO PHY register access via driver
    "test_dev_lifecycle",          # device open/close lifecycle — needs driver node
    "test_ts_event_sub",           # timestamp event subscription via driver
    "test_ptp_getset"              # PTP get/set time via driver IOCTL
)

$IntegrationTests = @(
    "ssot_register_validation_test",
    "test_tsn_ioctl_handlers_um",
    "test_all_adapters",
    "test_multidev_adapter_enum",
    "test_device_register_access",
    "test_ndis_send_path",
    "test_ndis_receive_path",
    "test_hw_state_machine",
    "test_lazy_initialization",
    "test_registry_diagnostics"
)

# ===========================
# Resolve which tests to run
# ===========================
if ($Tests.Count -gt 0) {
    $TestList = $Tests
} else {
    $TestList = switch ($Suite) {
        "Unit"        { $UnitTests }
        "Integration" { $IntegrationTests }
        "All"         { $UnitTests + $IntegrationTests }
    }
}

# ===========================
# Paths
# ===========================
$scriptDir   = $PSScriptRoot                                        # tools/test
$repoRoot    = Split-Path (Split-Path $scriptDir -Parent) -Parent  # repository root
$testExeDir  = Join-Path $repoRoot "build\tools\avb_test\x64\$Configuration"
$logsDir     = Join-Path $repoRoot "logs"

if (-not (Test-Path $logsDir)) {
    New-Item -ItemType Directory -Path $logsDir -Force | Out-Null
}

# ===========================
# Banner
# ===========================
Write-Host @"

================================================================
  Intel AVB Filter Driver - CI Test Runner
  Configuration : $Configuration
  Suite         : $Suite
  Test binaries : $testExeDir
  Log output    : $logsDir
  Hardware tests: SKIPPED (no Intel NIC expected on CI runner)
================================================================
"@ -ForegroundColor Cyan

# Detect common CI environments and note them
if ($env:GITHUB_ACTIONS -eq "true") {
    Write-Note "Running on GitHub Actions (runner: $($env:RUNNER_NAME), OS: $($env:RUNNER_OS))"
} elseif ($env:TF_BUILD -eq "True") {
    Write-Note "Running on Azure Pipelines"
} elseif ($env:CI -eq "true") {
    Write-Note "Running in a generic CI environment"
} else {
    Write-Note "Running locally in CI mode (hardware checks suppressed)"
}

# ===========================
# Run tests
# ===========================
$totalTests  = 0
$passedTests = 0
$failedTests = 0
$skippedTests = 0
$results     = @()

Write-Step "Running $($TestList.Count) hardware-independent tests"

foreach ($TestName in $TestList) {
    $exeName = if ($TestName.EndsWith(".exe")) { $TestName } else { "$TestName.exe" }
    $exePath = Join-Path $testExeDir $exeName

    if (-not (Test-Path $exePath)) {
        Write-Skip "$exeName (binary not found — not built or not applicable)"
        $skippedTests++
        $results += [PSCustomObject]@{ Name = $exeName; Status = "SKIPPED"; Exit = "N/A" }
        continue
    }

    $totalTests++
    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $logFile   = Join-Path $logsDir "$($TestName)_CI_$timestamp.log"

    Write-Host "`n  => $exeName" -ForegroundColor Cyan

    # Run the binary, tee to log, capture exit code.
    & $exePath 2>&1 | Tee-Object -FilePath $logFile
    $exitCode = $LASTEXITCODE

    if ($exitCode -eq 0 -or $null -eq $exitCode) {
        $passedTests++
        Write-Ok "$exeName PASSED"
        $results += [PSCustomObject]@{ Name = $exeName; Status = "PASSED"; Exit = 0 }
    } else {
        $failedTests++
        Write-Fail "$exeName FAILED (exit $exitCode)"
        $results += [PSCustomObject]@{ Name = $exeName; Status = "FAILED"; Exit = $exitCode }

        if ($FailFast) {
            Write-Note "-FailFast: stopping after first failure"
            break
        }
    }
}

# ===========================
# Summary
# ===========================
Write-Host @"

================================================================
  CI Test Summary — $Suite suite ($Configuration)
================================================================
"@ -ForegroundColor Cyan

$results | Format-Table -AutoSize

Write-Host "  Passed : $passedTests" -ForegroundColor Green
Write-Host "  Failed : $failedTests" -ForegroundColor $(if ($failedTests -gt 0) { "Red" } else { "Green" })
Write-Host "  Skipped: $skippedTests (not built)" -ForegroundColor DarkGray
Write-Host "  Total  : $totalTests ran" -ForegroundColor Cyan
Write-Host ""

if ($failedTests -eq 0 -and $passedTests -eq 0) {
    Write-Note "No tests ran (all skipped — check that Build-Tests.ps1 ran first)"
    exit 0
}

if ($failedTests -gt 0) {
    Write-Fail "OVERALL: $failedTests test(s) FAILED"
    exit 1
}

Write-Ok "OVERALL: All $passedTests test(s) PASSED"
exit 0
