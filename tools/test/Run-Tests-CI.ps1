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
    # "HardwareMandatory" = safe subset of HardwareUnit that MUST pass on every supported HIL runner.
    #                       Missing binary = FAIL.  Driver loss mid-suite = FAIL (not cascading SKIP).
    #                       Use this suite for the mandatory CI quality gate.
    # "HardwareMutation"  = PHC/TAS mutation tests (epoch-reset, freq-adj) that modify hardware state.
    #                       Run in an isolated lane after HardwareMandatory; continue-on-error in CI.
    #
    # SAFETY NOTE: test_s3_sleep_wake (TC-S3-002) sends the machine into S3 sleep via
    # SetSuspendState. It is intentionally absent from ALL automated suites including
    # HardwareMandatory and HardwareMutation. Run it manually on a dedicated test machine.
    [Parameter(Mandatory=$false)]
    [ValidateSet("Unit", "Integration", "All", "HardwareUnit", "HardwareIntegration",
                 "HardwareMandatory", "HardwareMutation")]
    [string]$Suite = "Unit",

    # Override the test list entirely (names without .exe extension).
    [Parameter(Mandatory=$false)]
    [string[]]$Tests = @(),

    # Stop after first failing test instead of running all.
    [Parameter(Mandatory=$false)]
    [switch]$FailFast,

    # Per-test timeout in seconds.  0 = no timeout (use for suites without hardware).
    # Default 300 s (5 min) for hardware suites; 60 s for unit-only suites.
    [Parameter(Mandatory=$false)]
    [int]$TestTimeoutSeconds = 0,

    # Override for the logs output directory.
    # Default is <repo-root>/logs.
    # The sentinel behavioral test passes an isolated tmpDir here so its JUnit
    # output cannot contaminate or be confused with real HIL evidence.
    # Ordinary HIL executions must NOT pass this parameter; CI leaves it unset.
    [Parameter(Mandatory=$false)]
    [string]$LogsDir = "",

    # Start DebugView kernel capture around the test run and save to a log file.
    # Requires .github\skills\DbgView\DebugView\Dbgview.exe (run tools\setup\Install-DbgView.ps1 once).
    [Parameter(Mandatory=$false)]
    [switch]$CaptureDbgView
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

# Test-AvbDriverHealth: verifies that the IntelAvbFilter driver is RUNNING,
# the device node is accessible, and a minimal health IOCTL succeeds.
#
# Issue #328: a RUNNING service can coexist with an inaccessible control device.
# Checking service state alone is NOT sufficient.
#
# Protocol:
#  1. Confirm service is in Running state.
#  2. Open \\.\IntelAvbFilter with FILE_SHARE_READ|WRITE (same as HIL clients).
#  3. Execute IOCTL_AVB_GET_VERSION (0x172000) --- read-only, no side effects.
#  4. Close the handle.
#  5. Return $false on any unexpected failure; log Win32 error separately.
#
# Deliberate non-actions:
#  - Never auto-restart the service to conceal a failure.
#  - Never retry: a failure here must be surfaced immediately.
#  - ACCESS_DENIED = hard $false (privilege problem, not transient).
#
# Callers: invoked before the HardwareMandatory suite and after every test
# in Hardware* suites.
function Test-AvbDriverHealth {
    param([string]$AfterTest = '')
    $tag = if ($AfterTest) { " after $AfterTest" } else { '' }

    # ------ 1. Service state ------------------------------------------------------------------------------------------------------------------------------------------------------------
    $svc = Get-Service -Name 'IntelAvbFilter' -ErrorAction SilentlyContinue
    if ($null -eq $svc) {
        Write-Host "  [INFRA] IntelAvbFilter service not found$tag" -ForegroundColor Red
        return $false
    }
    if ($svc.Status -eq 'StopPending') {
        Write-Host "  [INFRA] IntelAvbFilter STOP_PENDING$tag --- stuck; requires reboot" -ForegroundColor Red
        return $false
    }
    if ($svc.Status -ne 'Running') {
        Write-Host "  [INFRA] IntelAvbFilter is $($svc.Status)$tag --- not Running (NOT auto-restarted)" -ForegroundColor Red
        return $false
    }

    # ------ 2 + 3 + 4. Device node open + IOCTL_AVB_GET_VERSION + close ---------------------------
    # Define once; -ErrorAction SilentlyContinue suppresses the error if already defined.
    Add-Type -ErrorAction SilentlyContinue -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class AvbHealth {
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr CreateFile(
        string lpFileName, uint dwDesiredAccess, uint dwShareMode,
        IntPtr lpSec, uint dwCreationDisposition,
        uint dwFlagsAndAttributes, IntPtr hTemplate);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool DeviceIoControl(
        IntPtr hDevice, uint dwIoControlCode,
        IntPtr lpInBuffer, uint nInBufferSize,
        IntPtr lpOutBuffer, uint nOutBufferSize,
        out uint lpBytesReturned, IntPtr lpOverlapped);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr hObject);
    public static readonly IntPtr INVALID = new IntPtr(-1);
    public const uint GENERIC_READ_WRITE = 0xC0000000u;
    public const uint SHARE_RW          = 3u;
    public const uint OPEN_EXISTING     = 3u;
    public const uint FLAG_NORMAL       = 0x80u;
    public const uint IOCTL_AVB_GET_VERSION = 0x00172000u;
}
'@
    $h = [AvbHealth]::CreateFile('\\.\IntelAvbFilter',
          [AvbHealth]::GENERIC_READ_WRITE, [AvbHealth]::SHARE_RW,
          [IntPtr]::Zero, [AvbHealth]::OPEN_EXISTING,
          [AvbHealth]::FLAG_NORMAL, [IntPtr]::Zero)

    if ($h -eq [AvbHealth]::INVALID) {
        $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
        $desc = switch ($err) {
            2   { "ERROR_FILE_NOT_FOUND: device node does not exist" }
            5   { "ERROR_ACCESS_DENIED: privilege gap - runner may not be elevated" }
            default { "Win32 error $err" }
        }
        Write-Host ("  [INFRA] Cannot open \\.\IntelAvbFilter{0}: {1} (#328 sentinel)" -f $tag, $desc) `
              -ForegroundColor Red
        return $false
    }

    $bytesReturned = [uint32]0
    $buf = [System.Runtime.InteropServices.Marshal]::AllocHGlobal(256)
    $ok  = $false
    $err2 = 0
    try {
        $ok = [AvbHealth]::DeviceIoControl(
            $h, [AvbHealth]::IOCTL_AVB_GET_VERSION,
            [IntPtr]::Zero, [uint32]0, $buf, [uint32]256,
            [ref]$bytesReturned, [IntPtr]::Zero)
        if (-not $ok) {
            # Capture Win32 error immediately -- CloseHandle in finally{} can overwrite it.
            $err2 = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
        }
    } finally {
        [AvbHealth]::CloseHandle($h) | Out-Null
        [System.Runtime.InteropServices.Marshal]::FreeHGlobal($buf)
    }

    if (-not $ok) {
        Write-Host ("  [INFRA] IOCTL_AVB_GET_VERSION failed{0}: Win32={1}" -f $tag, $err2) `
              -ForegroundColor Red
        return $false
    }

    return $true
}

# ------ Test-only health-check hook ------------------------------------------------------------------------------------------------------------------------------------
# Wraps Test-AvbDriverHealth with an optional simulation override.
# PRODUCTION BEHAVIOR: $env:AVB_TEST_HEALTH_FAIL_AFTER is never set ---
#   Invoke-DriverHealthCheck is identical to Test-AvbDriverHealth.
# TEST-SENTINEL BEHAVIOR: set $env:AVB_TEST_HEALTH_FAIL_AFTER to a test name
#   (or "*" to fail after any test).  The check returns $false once and clears
#   the variable, allowing the sentinel integration test to drive the real
#   control-flow path without requiring an actual driver failure.
#
# The production Test-AvbDriverHealth function is not modified.
function Invoke-DriverHealthCheck {
    param([string]$AfterTest = '')
    $pattern = $env:AVB_TEST_HEALTH_FAIL_AFTER
    if ($pattern -and ($pattern -eq '*' -or $AfterTest -like $pattern)) {
        Write-Host "  [TEST-HOOK] Simulated health failure after '$AfterTest' (AVB_TEST_HEALTH_FAIL_AFTER=$pattern)" `
              -ForegroundColor DarkYellow
        $env:AVB_TEST_HEALTH_FAIL_AFTER = ''   # clear after triggering once
        return $false
    }
    return (Test-AvbDriverHealth -AfterTest $AfterTest)
}

# ===========================
# Lifecycle Metrics Snapshot --- SSOT: tools/test/Lib-AvbLifecycle.ps1
# ===========================
# Gracefully skipped when driver is absent (Unit/Integration suites on GitHub-hosted runners).
# Active on HardwareUnit/HardwareIntegration suites where the driver is installed.
. (Join-Path $PSScriptRoot 'Lib-AvbLifecycle.ps1')

# Canonical verdict vocabulary (PASS/FAIL/SKIP_UNSUPPORTED/SKIP_NOT_IMPLEMENTED/BLOCKED/INCONCLUSIVE),
# shared with Parse-TestResults.ps1 and Convert-Logs-To-JUnit.ps1.  See docs/testing/test-verdict-contract.md
. (Join-Path $PSScriptRoot 'TestVerdict.ps1')

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
    "test_ioctl_abi",          # IOCTL ABI stability: code uniqueness + struct sizes (TEST-ABI-001, #265)
    "test_i210_timinca_codec", # I210 TIMINCA codec golden vectors from datasheet 8.15.11 (offline, #3 #149)
    "ps1:tests/unit/ioctl/test_i210_timinca_ownership.ps1",  # TEST-TIMINCA-OWN-001..007: single I210 writer + other adapters unchanged
    "test_verdict_contract",   # status/capability/payload -> verdict contract + raw NTSTATUS capture (offline, no driver)
    "test_tx_corr_verdict",    # TC-CORR-TX-RX-001 per-sample verdict: provenance / no-timestamp / NOT_IMPLEMENTED (#35, #199)
    "ps1:tests/unit/ioctl/test_known_device_table.ps1",  # TEST-DEVTABLE-001..003: hw_state device table vs intel_pci_ids.h (#18)
    "ps1:tests/unit/ioctl/test_requirement_outcomes.ps1", # TEST-COVERAGE-VERDICT-001..007: requirement outcome classification (needs python)
    "test_tas_restore_contract", # TAS restore contract mock (#9 / #206): DISARM_TAS status classification (offline, no driver)

    # PowerShell logic tests (suffix .ps1, run via powershell.exe not compiled .exe)
    "ps1:tests/unit/ioctl/test_parse_results_aggregation.ps1",  # TEST-PROV-L06 aggregation invariants
    "ps1:tests/unit/ioctl/test_junit_verdict_normalization.ps1", # TEST-VERDICT-001..011: verdict contract in converter/parser/runner (sandboxed, no driver)
    "ps1:tests/unit/ioctl/test_no_interactive_waits.ps1",       # TEST-NOWAIT-001: no system("pause") in CI/HIL
    "ps1:tests/unit/build/test_build_tests_syspaths.ps1",       # TEST-BUILD-SYSPATHS-001: sysPaths type-safety regression
    "ps1:tests/unit/ioctl/test_health_sentinel.ps1",            # TEST-SENTINEL-001..006: #328 behavioral health sentinel
    "ps1:tests/unit/ioctl/test_preflight_sentinel.ps1",          # TEST-PREFLIGHT-001..005: preflight observational contract
    "test_idle_verdict",                                          # TC-IDLE-UNIT-001..012: idle verdict truth table (#328)
    "ps1:tests/unit/lifecycle/test_evaluate_idle_transition.ps1"  # TEST-IDLEEVAL-001..018: #328 final-verdict evaluator
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
        "test_tx_timestamp_provenance",  # Section L: pure logic + ABI static_assert (no device needed)
                                     # Section H SKIPs gracefully when driver is absent.
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
# HardwareMandatory --- safe mandatory quality gate (self-hosted runner)
# ===========================
# Tests that MUST pass on every supported HIL runner against every commit.
# Properties:
#   - No PHC/TAS hardware state mutation (safe to run back-to-back).
#   - Missing binary = FAIL (not SKIP) --- caller broke the build.
#   - Driver lost mid-suite = hard FAIL (not cascading SKIP) --- #328 sentinel.
#   - Exit code 2 (SKIP_ONLY from test_tx_timestamp_provenance Section H on a
#     no-hardware runner) is treated as infrastructure failure here --- Section H
#     must exercise the actual driver on a HIL runner.
#
# EXCLUDED: PHC mutation, TAS mutation, driver reinstall, power states.
# Run those in the HardwareMutation lane below.
$HardwareMandatoryTests = @(
    # Connectivity + device contract
    "test_ioctl_version",              # IOCTL_AVB_GET_VERSION handshake --- driver reachable
    "test_ioctl_routing",              # IOCTL routing sanity
    "test_ioctl_simple",               # basic open/close cycle
    "test_minimal_ioctl",              # minimal round-trip (regression for #328 device-node loss)
    "test_ioctl_abi",                  # IOCTL ABI backward-compat (TEST-ABI-001)
    # TX timestamp provenance + V1/V2 ABI (REQ-F-PTP-007, #199, #317)
    "test_tx_timestamp_provenance",    # Section L (logic) + Section H (FastIo V1/V2 canary,
                                       # self-comparison, masquerade, cross-handle isolation)
    "test_tx_timestamp_retrieval",     # TX timestamp FIFO retrieval; per-adapter results
    # PHC query / availability
    "test_ioctl_phc_query",            # IOCTL_AVB_GET_TIMESTAMP read-only
    "test_ioctl_xstamp",               # cross-timestamp PHC---QPC (VV-CORR-003)
    "test_ioctl_phc_crosstimestamp_v2", # V2 provenance per adapter; INCONCLUSIVE=FAIL (#340)
    "ptp_clock_control_test",          # PHC coherence TC-5a/TC-5b (read-only correlation)
                                       # CONTRACT NOTE: ptp_clock_control_test calls
                                       # AdjustFrequency (writes TIMINCA) inside TC-5b
                                       # before each adapter measurement. This is a PHC
                                       # mutation that contradicts the "non-mutating" contract
                                       # above. Tracked under #340 for future separation:
                                       # TC-5a+5b measurement should be split from Tests 1-4
                                       # and the ADJUST_FREQUENCY pre-measurement write into
                                       # separate executables (read-only vs. mutating lane).
    # Multi-adapter isolation
    "test_all_adapters",               # enumerate + open all adapters
    "test_multidev_adapter_enum",      # per-adapter VID/DID identity
    "test_multi_adapter_phc_sync",     # independent PHC per adapter
    # Send-path + provenance
    "test_send_ptp_debug",             # IOCTL_AVB_TEST_SEND_PTP diagnostic (PRE_SEND_PHC)
    # RX timestamp
    "test_rx_timestamp",               # IOCTL_AVB_GET_RX_TIMESTAMP V1/V2
    "test_rx_timestamp_complete"       # extended RX timestamp cases
)

# ===========================
# HardwareMutation --- PHC/TAS hardware-mutating tests (isolated lane)
# ===========================
# These tests write to PHC epoch, clock increment, or TAS GCL registers.
# Run in a SEPARATE CI job AFTER HardwareMandatory, with continue-on-error in CI
# to avoid blocking on mutation instability.
# Recovery: each mutation test must restore state before exit; CI records
# cleanup failures and halts the mutation lane after any CLEANUP_FAILED result.
#
# EXCLUDED: driver reinstall (UT-CORR-009), system sleep (test_s3_sleep_wake).
$HardwareMutationTests = @(
    # PHC epoch reset and frequency adjustment (UT-CORR-005/006)
    # --case ensures only the mutation sub-test runs, not the full stability suite.
    "test_ptp_phc_stability --case UT-CORR-005",  # SET_TIMESTAMP(1ms) + restore
    "test_ptp_phc_stability --case UT-CORR-006"   # ADJUST_FREQUENCY(+1ns) + restore
)

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
    "test_tx_timestamp_provenance",  # Section L: pure logic + ABI static_assert (no device needed)
                                     # Section H SKIPs gracefully when driver is absent.
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
    "test_ioctl_xstamp_v2",        # V2 ABI + HIL reachability (#340)
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
    "test_event_nf_zero_polling",  # NF CPU-overhead budget quantification -- (#241, TEST-EVENT-NF-002)
    "test_event_nf_latency",       # NF IOCTL-path latency P99 under adversarial load -- (#245, TEST-EVENT-NF-001)
    "test_gptp_phc_interface",     # gPTP PHC interface contract -- (#210)
    "test_send_ptp_debug",         # IOCTL_AVB_TEST_SEND_PTP diagnostic -- (#51)
    "test_timestamp_latency",      # TX/RX timestamp latency <1us -- (#272)
    "test_perf_regression",        # performance regression baseline -- (#225)
    "test_ndis_fastpath_latency",  # NDIS IOCTL fast-path latency P99<10us -- (#286)
    "test_hot_plug",               # PnP hot-plug filter restart -- (#262)
    "test_error_recovery",         # error recovery / no-BSOD on malformed input -- (#215)
    "test_multi_adapter_phc_sync",  # multi-adapter PHC sync/independence -- (#208)
    # Provenance regression (TEST-PROV-L01..L05 logic + TEST-PROV-H01..H05 device contract)
    "test_tx_timestamp_provenance"  # V1/V2 IOCTL ABI, FastIo canary, cross-handle isolation
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
    # Phase F: device-specific tests (I217/I225/I226 --- SKIP-safe on machines without hardware)
    "avb_test_i217",                  # I217 caps (BASIC_1588|ENHANCED_TS|MDIO|EEE), TSN NOT_SUPPORTED -- (#114)
    "avb_test_i225",                  # I225 caps (full TSN, MDIO, NO EEE) -- (#114)
    "avb_test_i226_um",               # I226 caps (full TSN, MDIO, EEE) -- (#114)
    "test_hardware_detection"         # HW capability detection & register access safety -- (#44 #45, #287 #288)
)

# ===========================
# Resolve which tests to run
# ===========================
if ($Tests.Count -gt 0) {
    $TestList = $Tests
    # $IsMandatorySuite is determined solely by $Suite, even when -Tests overrides the list.
    # This allows the behavioral sentinel integration test to inject a custom test list
    # while still exercising the HardwareMandatory control flow.
} else {
    $TestList = switch ($Suite) {
        "Unit"                  { $UnitTests }
        "Integration"           { $IntegrationTests }
        "All"                   { $UnitTests + $IntegrationTests }
        "HardwareUnit"          { $HardwareUnitTests }
        "HardwareIntegration"   { $HardwareIntegrationTests }
        "HardwareMandatory"     { $HardwareMandatoryTests }
        "HardwareMutation"      { $HardwareMutationTests }
    }
}
# HardwareMandatory enforces hard failures: binary missing = FAIL, driver loss = FAIL.
# This must be determined by Suite alone - not by whether -Tests was used.
$IsMandatorySuite = ($Suite -eq "HardwareMandatory")

# Verification class (not a new lane): every suite is mandatory verification except the
# explicitly experimental HardwareMutation lane.  Mandatory + SKIP_NOT_IMPLEMENTED = INCOMPLETE,
# mandatory + INCONCLUSIVE = nonpassing.  Optional keeps the labels but does not fail the run.
$IsOptionalVerification = ((Get-AvbSuiteVerificationClass -Suite $Suite) -eq 'optional')

# Apply default timeout per suite class when not explicitly set.
if ($TestTimeoutSeconds -eq 0) {
    $TestTimeoutSeconds = switch ($Suite) {
        "Unit"              { 60 }
        "Integration"       { 120 }
        "All"               { 120 }
        "HardwareUnit"      { 300 }
        "HardwareIntegration" { 600 }
        "HardwareMandatory" { 300 }
        "HardwareMutation"  { 300 }
        default             { 120 }
    }
}

# ===========================
# Paths
# ===========================
$scriptDir   = $PSScriptRoot                                        # tools/test
$repoRoot    = Split-Path (Split-Path $scriptDir -Parent) -Parent  # repository root
$testExeDir  = Join-Path $repoRoot "build\tools\avb_test\x64\$Configuration"

# -LogsDir may be supplied by the behavioral sentinel test for isolation.
# Production HIL runs leave it empty so the default repo logs/ directory is used.
if ($LogsDir -and $LogsDir.Trim() -ne "") {
    $logsDir = $LogsDir
} else {
    $logsDir = Join-Path $repoRoot "logs"
}

if (-not (Test-Path $logsDir)) {
    New-Item -ItemType Directory -Path $logsDir -Force | Out-Null
}

# ===========================
# Driver version + build SHA (hardware suites only)
# ===========================
if ($Suite -like 'Hardware*') {
    Write-Host ""
    Write-Host "==> Build and driver identity" -ForegroundColor Cyan

    # Checkout SHA --- the commit under test
    try {
        $gitSha = & git rev-parse HEAD 2>&1
        Write-Host "[INFO] Checkout SHA : $gitSha"
        if ($env:GITHUB_ACTIONS -eq 'true') {
            "HIL_CHECKOUT_SHA=$gitSha" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
        }
    } catch { Write-Host "[WARN] git rev-parse failed: $_" }

    # Installed driver SYS file version
    $drvPath = "$env:SystemRoot\System32\drivers\IntelAvbFilter.sys"
    if (Test-Path $drvPath) {
        $ver = (Get-Item $drvPath).VersionInfo
        $verStr = "$($ver.FileMajorPart).$($ver.FileMinorPart).$($ver.FileBuildPart).$($ver.FilePrivatePart)"
        $buildDate = (Get-Item $drvPath).LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss")
        Write-Host "[INFO] Driver version: $verStr  (SYS modified: $buildDate)"
        if ($env:GITHUB_ACTIONS -eq 'true') {
            "HIL_DRIVER_VERSION=$verStr"    | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
            "HIL_DRIVER_DATE=$buildDate"    | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
        }
    } else {
        Write-Host "[WARN] IntelAvbFilter.sys not found at $drvPath --- driver may not be installed"
    }

    # Runner NIC label (for per-NIC evidence matrix)
    $nicLabel = $env:RUNNER_LABELS -split ',' | Where-Object { $_ -match '^i[0-9]' } | Select-Object -First 1
    if (-not $nicLabel) { $nicLabel = $env:HIL_NIC_TYPE }
    if ($nicLabel) {
        Write-Host "[INFO] NIC type label: $nicLabel"
        if ($env:GITHUB_ACTIONS -eq 'true') {
            "HIL_NIC_TYPE=$nicLabel" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
        }
    }
}

# ===========================
# Banner
# ===========================
Write-Host @"

================================================================
  Intel AVB Filter Driver - CI Test Runner
  Configuration : $Configuration
  Suite         : $Suite  $( if ($IsMandatorySuite) { '  *** MANDATORY GATE ***' } )
  Test binaries : $testExeDir
  Log output    : $logsDir
  Timeout/test  : ${TestTimeoutSeconds}s
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
$totalTests   = 0
$passedTests  = 0
$failedTests  = 0
$skippedTests = 0
$incompleteTests = 0   # exit 0 but SKIP_NOT_IMPLEMENTED reported on mandatory verification
$results      = @()

# Expose driver build type to test_perf_regression and other tests that key
# their baseline on (device_id x driver_build)
$env:AVB_DRIVER_BUILD = $Configuration
Write-Note "AVB_DRIVER_BUILD=$Configuration (baseline context for perf regression)"

Write-Step "Running $($TestList.Count) tests"

$driverLost = $false   # set when driver becomes inaccessible mid-suite

foreach ($TestName in $TestList) {
    # Separate the binary name from any arguments embedded in the test name.
    # HardwareMutation entries like "test_ptp_phc_stability --case UT-CORR-005"
    # embed arguments after the binary name. Split them here so the path lookup
    # uses only the binary name and arguments are passed correctly.
    #
    # PowerShell-native tests use the prefix "ps1:" followed by a repo-relative path.
    # These are run via powershell.exe directly (not compiled .exe binaries).
    $isPsTest = $TestName.StartsWith("ps1:")
    $psScriptPath = $null
    if ($isPsTest) {
        $rawPsPath = $TestName.Substring(4).Replace('/', '\')
        # Use absolute path directly if provided; otherwise resolve relative to repo root
        $psScriptPath = if ([System.IO.Path]::IsPathRooted($rawPsPath)) {
            $rawPsPath
        } else {
            Join-Path $repoRoot $rawPsPath
        }
        $binName = [System.IO.Path]::GetFileNameWithoutExtension($psScriptPath)
        $testArgs = ''
        $exeName  = "$binName.ps1"
    } else {
        $parts   = $TestName -split '\s+', 2
        $binName = $parts[0]
        $testArgs = if ($parts.Count -gt 1) { $parts[1] } else { '' }
        $exeName = if ($binName.EndsWith(".exe")) { $binName } else { "$binName.exe" }
    }
    $exePath = if ($isPsTest) { $psScriptPath } else { Join-Path $testExeDir $exeName }

    # MANDATORY SUITE: driver loss = hard FAIL for #328 sentinel, not cascade SKIP.
    if ($driverLost) {
        if ($IsMandatorySuite) {
            Write-Fail "$exeName [INFRA-FAIL] driver lost --- #328 sentinel: remaining mandatory tests FAIL"
            $failedTests++
            $totalTests++
            $results += [PSCustomObject]@{ Name = $exeName; Status = "FAILED(INFRA)"; Exit = -1 }
        } else {
            Write-Skip "$exeName (INFRA: driver lost -- skipping to prevent cascade failures)"
            $skippedTests++
            $results += [PSCustomObject]@{ Name = $exeName; Status = "SKIPPED(INFRA)"; Exit = "N/A" }
        }
        continue
    }

    if (-not (Test-Path $exePath)) {
        if ($IsMandatorySuite) {
            Write-Fail "$exeName [BUILD-FAIL] binary not found at $exePath --- mandatory test suite requires this binary"
            $failedTests++
            $totalTests++
            $results += [PSCustomObject]@{ Name = $exeName; Status = "FAILED(BUILD)"; Exit = "N/A" }
        } else {
            Write-Skip "$exeName (binary not found -- not built or not applicable)"
            $skippedTests++
            $results += [PSCustomObject]@{ Name = $exeName; Status = "SKIPPED"; Exit = "N/A" }
        }
        continue
    }

    $totalTests++
    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $logStem = $binName -replace '\.(exe|ps1)$',''
    $logFile = Join-Path $logsDir "${logStem}.exe_${timestamp}.log"

    Write-Host "`n  => $exeName$(if ($testArgs) { " $testArgs" } else { '' })" -ForegroundColor Cyan

    # --- Lifecycle snapshot BEFORE ---
    $lcyBefore = Get-AvbLifecycleSnapshot

    # Run the test.  PowerShell scripts use powershell.exe; .exe binaries use Start-Process.
    if ($isPsTest) {
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName  = 'powershell.exe'
        $psi.Arguments = "-NoProfile -ExecutionPolicy Bypass -NonInteractive -File `"$psScriptPath`""
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError  = $true
        $psi.UseShellExecute = $false
    } else {
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName  = $exePath
        $psi.Arguments = $testArgs
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError  = $true
        $psi.UseShellExecute = $false
    }
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError  = $true
    $psi.UseShellExecute = $false
    $proc = [System.Diagnostics.Process]::Start($psi)

    # Capture output in background threads, stream to console and log.
    $outLines = [System.Collections.Generic.List[string]]::new()
    $outJob = $proc.StandardOutput.ReadToEndAsync()
    $errJob = $proc.StandardError.ReadToEndAsync()

    $timedOut  = $false
    $timeoutMs = $TestTimeoutSeconds * 1000
    if (-not $proc.WaitForExit($timeoutMs)) {
        $proc.Kill()
        $timedOut = $true
        Write-Fail "$exeName TIMEOUT after ${TestTimeoutSeconds}s"
    }
    $outText  = $outJob.Result
    $errText  = $errJob.Result
    $exitCode = if ($timedOut) { -999 } else { $proc.ExitCode }

    # Write captured output to log and display on console.
    $combined = if ($errText) { $outText + "`n" + $errText } else { $outText }
    $combined | Out-File -FilePath $logFile -Encoding utf8
    $combined -split "`n" | ForEach-Object { Write-Host "    $_" }

    # --- Lifecycle snapshot AFTER + diff ---
    $lcyAfter = Get-AvbLifecycleSnapshot
    $lcyAnomalies = Write-LifecycleDiff -Before $lcyBefore -After $lcyAfter -Label $TestName
    if ($lcyAnomalies) {
        Write-Host "  [WARN:LCY] Lifecycle anomaly detected (underflow/error/OID imbalance) -- see [LCY] lines above" -ForegroundColor Yellow
    }

    # Verdict gate: an executable exit code cannot express SKIP_NOT_IMPLEMENTED / INCONCLUSIVE,
    # so read them from the log (same vocabulary the JUnit converters use).
    $vc = Get-AvbVerdictCounts -Lines @($combined -split "`r?`n")

    if ($timedOut) {
        $failedTests++
        $results += [PSCustomObject]@{ Name = $exeName; Status = "FAILED(TIMEOUT)"; Exit = -999 }
    } elseif (($exitCode -eq 0 -or $null -eq $exitCode) -and -not $IsOptionalVerification -and $vc.Inconclusive -gt 0) {
        $failedTests++
        Write-Fail "$exeName INCONCLUSIVE ($($vc.Inconclusive) testcase(s)) --- insufficient evidence on mandatory verification"
        $results += [PSCustomObject]@{ Name = $exeName; Status = "FAILED(INCONCLUSIVE)"; Exit = 0 }
    } elseif (($exitCode -eq 0 -or $null -eq $exitCode) -and -not $IsOptionalVerification -and $vc.NotImplemented -gt 0) {
        $incompleteTests++
        Write-Fail "$exeName INCOMPLETE ($($vc.NotImplemented) testcase(s) SKIP_NOT_IMPLEMENTED) --- driver operation missing on mandatory verification"
        $results += [PSCustomObject]@{ Name = $exeName; Status = "INCOMPLETE(NOT_IMPLEMENTED)"; Exit = 0 }
    } elseif ($exitCode -eq 0 -or $null -eq $exitCode) {
        $passedTests++
        Write-Ok "$exeName PASSED"
        if ($vc.NotImplemented -gt 0 -or $vc.Inconclusive -gt 0) {
            Write-Note "$exeName (optional verification): $($vc.NotImplemented) SKIP_NOT_IMPLEMENTED, $($vc.Inconclusive) INCONCLUSIVE --- reported, not verified"
        }
        $results += [PSCustomObject]@{ Name = $exeName; Status = "PASSED"; Exit = 0 }
    } elseif ($exitCode -eq 2 -and $IsMandatorySuite) {
        # Exit 2 = SKIP_ONLY from test_tx_timestamp_provenance (driver absent / all H tests skipped).
        # On a mandatory HIL suite the driver MUST be present --- SKIP_ONLY means
        # Section H never ran, so V1/V2 canary, cross-handle isolation, etc. were not exercised.
        Write-Fail "$exeName [HIL-FAIL] exit=2 SKIP_ONLY on mandatory suite --- driver not accessible or Section H not exercised"
        $failedTests++
        $results += [PSCustomObject]@{ Name = $exeName; Status = "FAILED(SKIP_ONLY)"; Exit = $exitCode }
    } elseif ($exitCode -eq 2) {
        # Exit 2 = SKIP_ONLY on a non-mandatory suite (Unit/Integration/HardwareUnit).
        # Section L passed; Section H skipped because driver is absent.
        # Expected on GitHub-hosted runners and any machine without the driver installed.
        $skippedTests++
        Write-Skip "$exeName (exit=2 SKIP_ONLY: Section L passed, Section H skipped -- driver absent)"
        $results += [PSCustomObject]@{ Name = $exeName; Status = "SKIPPED"; Exit = $exitCode }
    } else {
        $failedTests++
        Write-Fail "$exeName FAILED (exit $exitCode)"
        $results += [PSCustomObject]@{ Name = $exeName; Status = "FAILED"; Exit = $exitCode }

        if ($FailFast) {
            Write-Note "-FailFast: stopping after first failure"
            break
        }
    }

    # After each hardware test: verify the driver is still accessible.
    # MANDATORY: driver loss = hard infrastructure failure (#328 sentinel).
    # CRITICAL: this block must increment $failedTests and $totalTests unconditionally
    # when the health check fails --- even when the preceding test PASSED --- so the
    # suite always exits non-zero on driver loss, not silently exit 0.
    if ($Suite -like 'Hardware*' -and -not (Invoke-DriverHealthCheck -AfterTest $exeName)) {
        $driverLost = $true
        # Record driver identity at the point of failure for traceability.
        $drvVer   = if ($env:HIL_DRIVER_VERSION) { $env:HIL_DRIVER_VERSION } else { 'unknown' }
        $drvSha   = if ($env:HIL_DRIVER_SHA256)  { $env:HIL_DRIVER_SHA256.Substring(0, [Math]::Min(12, $env:HIL_DRIVER_SHA256.Length)) + '...' } else { 'unknown' }
        $infraMsg = "[#328-SENTINEL] driver health check FAILED after $exeName (drv=$drvVer sha=$drvSha)"
        # Increment BOTH counters --- health check failure is a first-class test failure.
        $failedTests++
        $totalTests++
        if ($IsMandatorySuite) {
            Write-Fail "INFRA: $infraMsg --- mandatory suite aborting"
        } else {
            Write-Fail "INFRA: $infraMsg --- remaining tests will be skipped"
        }
        $results += [PSCustomObject]@{ Name = "$exeName[INFRA-HEALTH]"; Status = "FAILED(INFRA)"; Exit = -2 }

        # Write a synthetic log file so Convert-Logs-To-JUnit / Parse-TestResults
        # can emit a <failure> element for this infrastructure failure in the JUnit XML.
        # TC-INFRA-HEALTH-001 matches the TC-* pattern accepted by Parse-TestResults.
        $infraLogTs  = Get-Date -Format 'yyyyMMdd_HHmmss'
        $infraLogFile = Join-Path $logsDir ("TC-INFRA-HEALTH.exe_{0}.log" -f $infraLogTs)
        @"
[INFRA_FAILURE] TC-INFRA-HEALTH-001: $infraMsg
"@ | Set-Content $infraLogFile -Encoding utf8
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
Write-Host "  Incomplete (SKIP_NOT_IMPLEMENTED): $incompleteTests" -ForegroundColor $(if ($incompleteTests -gt 0) { "Red" } else { "Green" })
Write-Host "  Skipped: $skippedTests" -ForegroundColor DarkGray
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
    $junitArgs = @{ LogsDir = $logsDir; RunDate = $runDateStr; OutputFile = $junitOut
                    Configuration = $Configuration; SuiteName = $Suite }
    if ($IsOptionalVerification) { $junitArgs.OptionalVerification = $true }
    & $junitScript @junitArgs -ErrorAction SilentlyContinue
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

if ($failedTests -eq 0 -and $incompleteTests -eq 0 -and $passedTests -eq 0) {
    Write-Note "No tests ran (all skipped -- check that Build-Tests.ps1 ran first)"
    exit 0
}

if ($failedTests -gt 0 -or $incompleteTests -gt 0) {
    if ($failedTests -gt 0)     { Write-Fail "OVERALL: $failedTests test(s) FAILED" }
    if ($incompleteTests -gt 0) { Write-Fail "OVERALL: $incompleteTests test(s) INCOMPLETE (SKIP_NOT_IMPLEMENTED on mandatory verification)" }
    exit 1
}

Write-Ok "OVERALL: All $passedTests test(s) PASSED"
exit 0
