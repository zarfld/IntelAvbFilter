# Run-Tests-CI.ps1
# CI-specific test runner for GitHub Actions and other headless CI environments.
#
# RATIONALE -- Three-script test architecture:
#   Run-Tests-Elevated.ps1  LOCAL DEV only -- spawns elevated shell via Start-Process -Verb RunAs.
#                           Interactive UAC required; caller terminal shows nothing (logs only).
#   Run-Tests.ps1           ADMIN DEV / full suite -- checks driver service, Intel hardware,
#                           device node. Requires the driver to be installed and hardware present.
#   Run-Tests-CI.ps1        CI use -- GitHub runner is already Administrator; no hardware/driver
#                           expected; hardware-independent tests only; no interactive prompts.
#
# Implements: #27 (REQ-NF-SCRIPTS-001: Consolidated Script Architecture)
# See also: tools/test/Run-Tests-Elevated.ps1, tools/test/Run-Tests.ps1

param(
    [Parameter(Mandatory=$false)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    # Suite selects the built-in test list.
    # "Unit"              = fast hardware-independent unit tests (GitHub-hosted CI)
    # "Integration"       = hardware-independent integration tests (GitHub-hosted CI; currently empty)
    # "All"               = Unit + Integration combined
    # "HardwareUnit"      = unit-level tests that need Intel NIC + driver (self-hosted runner only)
    # "HardwareIntegration" = integration-level tests that need Intel NIC + driver (self-hosted runner only)
    #
    # SAFETY NOTE: test_s3_sleep_wake (TC-S3-002) sends the machine into S3 sleep via
    # SetSuspendState. It is intentionally absent from ALL automated suites including
    # HardwareUnit and HardwareIntegration. Run it manually on a dedicated test machine.
    [Parameter(Mandatory=$false)]
    [ValidateSet("Unit", "Integration", "All", "HardwareUnit", "HardwareIntegration")]
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
# No admin check here -- by design.
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
# Lifecycle Metrics Snapshot — SSOT: tools/test/Lib-AvbLifecycle.ps1
# ===========================
# Gracefully skipped when driver is absent (Unit/Integration suites on GitHub-hosted runners).
# Active on HardwareUnit/HardwareIntegration suites where the driver is installed.
. (Join-Path $PSScriptRoot 'Lib-AvbLifecycle.ps1')

# ===========================
# Built-in hardware-independent test lists
# ===========================
$UnitTests = @(
    # Pure static/logic tests -- no driver device, no hardware, run fine on CI.
    "test_register_constants",
    "test_magic_numbers",
    "test_scripts_consolidate",
    "test_hal_unit",
    "test_hal_errors",
    "test_ioctl_abi"       # IOCTL ABI stability: code uniqueness + struct sizes (TEST-ABI-001, #265)
)

# These tests open \\.\IntelAvbFilter or exercise hardware paths; they require
# the driver to be installed and will always fail on a runner without the NIC.
# NOT added to $UnitTests / $IntegrationTests -- documented here for visibility.
$HardwareDependentTests = @(
    "test_cleanup_archive",        # checks build output structure against installed driver
    "test_ioctl_simple",           # opens \\.\IntelAvbFilter via CreateFile
    "test_ioctl_routing",          # DeviceIoControl -- driver must be loaded
    "test_ioctl_trace",            # DeviceIoControl trace IOCTLs
    "test_minimal_ioctl",          # minimal IOCTL round-trip to driver
    "test_clock_config",           # hardware clock register access
    "test_clock_working",          # PTP clock hardware test
    "test_direct_clock",           # direct clock register read/write
    "test_hal_performance",        # HAL timer/DMA performance -- needs NIC
    "test_atdecc_event",           # ATDECC event subscription via driver IOCTL
    "test_atdecc_aen_protocol",    # ATDECC AEN protocol -- driver IOCTL path
    "test_ioctl_version",          # IOCTL_AVB_GET_VERSION to driver
    "test_mdio_phy",               # MDIO PHY register access via driver
    "test_dev_lifecycle",          # device open/close lifecycle -- needs driver node
    "test_ts_event_sub",           # timestamp event subscription via driver
    "test_ptp_getset",             # PTP get/set time via driver IOCTL
    # Integration-level tests -- all require driver + hardware
    "ssot_register_validation_test",  # SSOT register validation via device access
    "test_tsn_ioctl_handlers_um",     # TSN IOCTL user-mode handlers -- driver required
    "test_all_adapters",              # enumerates all Intel AVB adapters
    "test_multidev_adapter_enum",     # multi-device adapter enumeration
    "test_device_register_access",    # direct device register read/write
    "test_ndis_send_path",            # NDIS send path -- needs NIC
    "test_ndis_receive_path",         # NDIS receive path -- needs NIC
    "test_hw_state_machine",          # hardware state machine transitions
    "test_lazy_initialization",       # lazy init via driver device open
    "test_registry_diagnostics"       # registry diagnostics -- driver service required
)

# All integration-level tests open the driver device or enumerate hardware.
# None can pass on a CI runner without the driver installed.
# Listed in $HardwareDependentTests below; $IntegrationTests is intentionally empty.
$IntegrationTests = @()

# ===========================
# Hardware-dependent test lists (self-hosted runner only)
# ===========================
# Unit-level tests that require the driver device node (\\.\.\IntelAvbFilter) or Intel NIC.
# Safe to run on an automated self-hosted runner -- none of these hibernate or sleep the PC.
#
# EXCLUDED from ALL automated suites (unsafe or long-running):
#   test_s3_sleep_wake          -- TC-S3-002 calls SetSuspendState (hibernates host, needs button press).
#   test_vv_corr_001_stability  -- VV-CORR-001 24-hour PHC stability monitor. Run manually on
#                                  a dedicated lab machine only. See Issue #317 Track E.
$HardwareUnitTests = @(
    "test_cleanup_archive",        # checks build output against installed driver
    "test_ioctl_simple",           # opens \\.\IntelAvbFilter via CreateFile
    "test_ioctl_routing",          # DeviceIoControl -- driver must be loaded
    "test_ioctl_trace",            # DeviceIoControl trace IOCTLs
    "test_minimal_ioctl",          # minimal IOCTL round-trip to driver
    "test_clock_config",           # hardware clock register access
    "test_clock_working",          # PTP clock hardware test
    "test_direct_clock",           # direct clock register read/write
    "test_hal_performance",        # HAL timer/DMA performance -- needs NIC
    "test_atdecc_event",           # ATDECC event subscription via driver IOCTL
    "test_atdecc_aen_protocol",    # ATDECC AEN protocol -- driver IOCTL path
    "test_ioctl_version",          # IOCTL_AVB_GET_VERSION to driver
    "test_mdio_phy",               # MDIO PHY register access via driver
    "test_dev_lifecycle",          # device open/close lifecycle -- needs driver node
    "test_ts_event_sub",           # timestamp event subscription via driver
    "test_ptp_getset",             # PTP get/set time via driver IOCTL
    "test_power_management",       # open->PHC->close->200ms Sleep->reopen (no system sleep)
    # Phase A: IOCTL functional tests (REQ #2-#13, previously missing from CI)
    "test_ptp_freq",               # PTP frequency adjustment -- REQ-F-PTP-002 (#3)
    "test_ptp_freq_complete",      # PTP frequency adjustment extended -- REQ-F-PTP-002 (#3)
    "test_hw_ts_ctrl",             # hardware timestamp control -- REQ-F-PTP-003 (#5)
    "test_rx_timestamp",           # RX timestamping -- REQ-F-PTP-004 (#6)
    "test_rx_timestamp_complete",  # RX timestamping extended -- REQ-F-PTP-004 (#6)
    "test_ioctl_target_time",      # target time/aux timestamp -- REQ-F-PTP-005 (#7)
    "test_qav_cbs",                # Credit-Based Shaper config -- REQ-F-QAV-001 (#8)
    "test_ioctl_tas",              # Time-Aware Shaper config -- REQ-F-TAS-001 (#9)
    "test_ioctl_fp_ptm",           # Frame Preemption & PTM -- REQ-F-FP-001 (#11)
    "test_tx_timestamp_retrieval", # TX timestamp retrieval -- REQ-F-IOCTL-TS-001 (#35)
    "test_ioctl_phc_query",        # PHC query IOCTL -- REQ-F-PTP-PHC (#192)
    "test_ioctl_offset",           # PHC offset adjustment -- (#194)
    "test_ioctl_phc_epoch",        # PHC epoch management -- (#196)
    "test_ioctl_phc_monotonicity", # PHC monotonicity guarantee -- (#197)
    "test_ioctl_xstamp",           # cross-timestamp (PTP/system correlation) -- (#198)
    "test_ioctl_abi",              # IOCTL ABI backward-compatibility
    "test_ioctl_access_control",   # IOCTL access-control (privilege enforcement)
    "test_ioctl_buffer_fuzz",      # IOCTL buffer-boundary fuzzing (security)
    "test_tsn_ioctl_handlers",     # TSN IOCTL handlers (kernel-side, non-um)
    # Phase B: PHC/QPC coherence + VV coverage (added 2026-03-29)
    "ptp_clock_control_test",      # PHC/QPC coherence TC-5a/TC-5b -- #238
    "test_ptp_crosstimestamp",     # cross-domain timestamp correlation -- VV-CORR-003
    "test_zero_polling_overhead",  # interrupt-driven delivery, zero-poll -- #310
    "test_lifecycle_coverage",     # Pillar-2 NDIS LWF lifecycle counters
    # Phase C: Additional unit tests registered 2026-04-01 (built but previously absent from CI)
    "test_hw_state",               # hardware state enum/OID query -- REQ-F-HWCTX-001 (#18)
    "test_statistics_counters",    # IOCTL statistics counters -- REQ-NF-DIAG-001 (#270)
    "test_event_log",              # Windows Event Log integration -- (#269)
    "test_build_sign",             # build & code-signing verification -- (#243)
    "test_compat_win11",           # Windows 11 compatibility smoke -- (#256)
    "test_win7_stub",              # OS compat Win10+ assertion -- (#258)
    "test_eee_lpi",                # IEEE 802.3az EEE/LPI [TDD-RED] -- (#223)
    "test_pfc_pause",              # IEEE 802.1Qbb PFC [TDD-RED] -- (#219)
    "test_srp_interface",          # IEEE 802.1Qat SRP [TDD-RED] -- (#211)
    "test_vlan_tag",               # IEEE 802.1Q VLAN insert/strip [TDD-RED] -- (#213)
    "test_vlan_pcp_tc_mapping",    # VLAN PCP->TC CBS mapping -- (#276)
    "test_security_validation",    # security validation & vuln scan -- (#226)
    "test_ioctl_launch_time",      # launch time offload IOCTL -- (#209)
    "test_avtp_tu_bit_events",     # AVTP TU-bit change events -- (#175)
    "test_ptp_event_latency",      # PTP event ring-buffer latency -- (#177)
    "test_event_latency_4ch",      # 4-channel multi-observer event latency -- (#179)
    "test_gptp_phc_interface",     # gPTP PHC interface contract -- (#210)
    "test_send_ptp_debug",         # IOCTL_AVB_TEST_SEND_PTP diagnostic -- (#51)
    "test_timestamp_latency",      # TX/RX timestamp latency <1us -- (#272)
    "test_perf_regression",        # performance regression baseline -- (#225)
    "test_ndis_fastpath_latency",  # NDIS IOCTL fast-path latency P99<10us -- (#286)
    "test_hot_plug",               # PnP hot-plug filter restart -- (#262)
    "test_error_recovery",         # error recovery / no-BSOD on malformed input -- (#215)
    "test_multi_adapter_phc_sync"  # multi-adapter PHC sync/independence -- (#208)
)

# Integration-level tests that require the driver device node or Intel NIC.
# Safe to run on an automated self-hosted runner.
$HardwareIntegrationTests = @(
    "ssot_register_validation_test",  # SSOT register validation via device access
    "test_tsn_ioctl_handlers_um",     # TSN IOCTL user-mode handlers -- driver required
    "test_all_adapters",              # enumerates all Intel AVB adapters
    "test_multidev_adapter_enum",     # multi-device adapter enumeration
    "test_device_register_access",    # direct device register read/write
    "test_ndis_send_path",            # NDIS send path -- needs NIC
    "test_ndis_receive_path",         # NDIS receive path -- needs NIC
    "test_hw_state_machine",          # hardware state machine transitions
    "test_lazy_initialization",       # lazy init via driver device open
    "test_registry_diagnostics",      # registry diagnostics -- driver service required
    # Phase B: extended cross-domain / correlation tests (added 2026-03-29)
    "test_ptp_corr_extended",         # extended PTP correlation -- duration ~5 min
    "test_vv_corr_003_crossdomain",   # cross-domain V&V validation -- VV-CORR-003
    # Phase C: Additional integration tests registered 2026-04-01 (built but previously absent from CI)
    "test_ptp_001",                   # TEST-PTP-001 HW timestamp correlation for gPTP -- (#238 #149)
    "test_ptp_corr",                  # PTP HW correlation IT-CORR-001..004 -- (#199)
    "test_ptp_phc_stability",         # PHC stability UT-CORR-005..009; ~1K samples@2ms -- (#317)
    "ptp_clock_control_production",   # PTP clock production test -- (#238)
    "ptp_ioctl_latency_test",         # PTP IOCTL latency & jitter -- (#321 #322 #323 #324)
    "hw_timestamping_control",        # HW timestamping control integration
    "rx_timestamping",                # RX timestamping integration
    "tsauxc_toggle_test",             # TSAUXC register toggle (TSN)
    "test_gptp_daemon_coexist",       # OpenAvnu gPTP daemon coexistence; SKIPs if no daemon -- (#240)
    # Phase D: i219-specific compatibility tests (added to close #261)
    "avb_test_i219",                  # I219 compat: detection, caps, PTP, monotonicity, variant matrix -- (#261 #76 #114)
    # Phase E: cross-adapter portability (QA-SC-PORT-001, Issue #114)
    "avb_test_portability",           # capability-gate verification across all present Intel adapters -- (#114)
    # Phase F: device-specific tests (I217/I225/I226 — SKIP-safe on machines without hardware)
    "avb_test_i217",                  # I217 caps (BASIC_1588|ENHANCED_TS|MDIO|EEE), TSN NOT_SUPPORTED -- (#114)
    "avb_test_i225",                  # I225 caps (full TSN, MDIO, NO EEE) -- (#114)
    "avb_test_i226_um"                # I226 caps (full TSN, MDIO, EEE) -- (#114)
)

# ===========================
# Resolve which tests to run
# ===========================
if ($Tests.Count -gt 0) {
    $TestList = $Tests
} else {
    $TestList = switch ($Suite) {
        "Unit"                  { $UnitTests }
        "Integration"           { $IntegrationTests }
        "All"                   { $UnitTests + $IntegrationTests }
        "HardwareUnit"          { $HardwareUnitTests }
        "HardwareIntegration"   { $HardwareIntegrationTests }
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
  Hardware tests: $( if ($Suite -like 'Hardware*') { 'ENABLED (self-hosted runner with Intel NIC)' } else { 'SKIPPED (no Intel NIC expected on CI runner)' } )
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
        Write-Skip "$exeName (binary not found -- not built or not applicable)"
        $skippedTests++
        $results += [PSCustomObject]@{ Name = $exeName; Status = "SKIPPED"; Exit = "N/A" }
        continue
    }

    $totalTests++
    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    # Filename format matches Convert-Logs-To-JUnit.ps1 expectation: <name>.exe_YYYYMMDD_HHMMSS.log
    $logFile   = Join-Path $logsDir "${exeName}_${timestamp}.log"

    Write-Host "`n  => $exeName" -ForegroundColor Cyan

    # --- Lifecycle snapshot BEFORE ---
    $lcyBefore = Get-AvbLifecycleSnapshot

    # Run the binary, tee to log, capture exit code.
    # NOTE: Override ErrorActionPreference to Continue in a child scope.
    # PS 5.1 with EAP=Stop converts any stderr output from a native .exe into a
    # terminating NativeCommandError — which would crash the entire test runner.
    & {
        $ErrorActionPreference = 'Continue'
        & $exePath 2>&1 | Tee-Object -FilePath $logFile
    }
    $exitCode = $LASTEXITCODE

    # --- Lifecycle snapshot AFTER + diff ---
    $lcyAfter = Get-AvbLifecycleSnapshot
    $lcyAnomalies = Write-LifecycleDiff -Before $lcyBefore -After $lcyAfter -Label $TestName
    if ($lcyAnomalies) {
        Write-Host "  [WARN:LCY] Lifecycle anomaly detected (underflow/error/OID imbalance) -- see [LCY] lines above" -ForegroundColor Yellow
    }

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
  CI Test Summary -- $Suite suite ($Configuration)
================================================================
"@ -ForegroundColor Cyan

$results | Format-Table -AutoSize

Write-Host "  Passed : $passedTests" -ForegroundColor Green
Write-Host "  Failed : $failedTests" -ForegroundColor $(if ($failedTests -gt 0) { "Red" } else { "Green" })
Write-Host "  Skipped: $skippedTests (not built)" -ForegroundColor DarkGray
Write-Host "  Total  : $totalTests ran" -ForegroundColor Cyan
Write-Host ""

# ===========================
# JUnit XML report
# ===========================
$junitScript = Join-Path $scriptDir 'Convert-Logs-To-JUnit.ps1'
if (Test-Path $junitScript) {
    $runDateStr = Get-Date -Format 'yyyyMMdd'
    $junitOut   = Join-Path $logsDir "junit-${Suite}-${Configuration}-${runDateStr}.xml"
    Write-Host ""
    Write-Step "Generating JUnit XML report..."
    & $junitScript -LogsDir $logsDir -RunDate $runDateStr -OutputFile $junitOut -ErrorAction SilentlyContinue
    if ($LASTEXITCODE -ne 0) {
        Write-Note "JUnit conversion returned non-zero (may be no logs yet -- skipped tests only)"
    }
    if (Test-Path $junitOut) {
        Write-Ok "JUnit XML: $junitOut"
        if ($env:GITHUB_ACTIONS -eq 'true') {
            "JUNIT_XML=$junitOut" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
        }
    }
}

if ($failedTests -eq 0 -and $passedTests -eq 0) {
    Write-Note "No tests ran (all skipped -- check that Build-Tests.ps1 ran first)"
    exit 0
}

if ($failedTests -gt 0) {
    Write-Fail "OVERALL: $failedTests test(s) FAILED"
    exit 1
}

Write-Ok "OVERALL: All $passedTests test(s) PASSED"
exit 0
