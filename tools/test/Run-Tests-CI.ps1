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

    # Machine-readable results document (schema avb-runner-results/1): one record per result row with verdict,
    # exit code, artifact, arguments, class, target, log path and per-testcase verdicts.  Written atomically when
    # the run completes.  Default: <logs>\results-<Suite>-<Configuration>-<timestamp>.json.
    [Parameter(Mandatory=$false)]
    [string]$ResultsJson = "",

    # Evidence root of earlier suites of the same HIL job (hil.yml: RUNNER_TEMP\hil-evidence).  A halt there
    # (TIMEOUT without terminated tree / INFRA / stateHalt / a run that died) halts this run: Init/Mutating/Unsafe
    # BLOCKED.  Cleanup results of earlier suites never halt (Q 2026-10-28).
    [Parameter(Mandatory=$false)]
    [string]$PriorResultsDir = "",

    # Bounded wait for the machine lock when no owner context of this execution holds it (attended runs only; under
    # GitHub Actions the job's deploy step must already own it - never acquired here).
    [Parameter(Mandatory=$false)]
    [int]$HilLockWaitSeconds = 60,

    # NOT SUPPORTED here: this runner never captured DebugView output although it accepted the switch.  Kernel
    # capture for one test is tools\test\Run-Tests-Elevated.ps1 -TestName <test> -CaptureDbgView.  The switch is
    # kept only to reject it loudly instead of silently running without a capture.
    [Parameter(Mandatory=$false)]
    [switch]$CaptureDbgView
)

$ErrorActionPreference = 'Stop'

if ($CaptureDbgView) {
    Write-Host "[FAIL] Run-Tests-CI.ps1 does not capture DebugView output (-CaptureDbgView is not supported here)." -ForegroundColor Red
    Write-Host "       Use: .\tools\test\Run-Tests-Elevated.ps1 -TestName <test> -CaptureDbgView -Configuration $Configuration" -ForegroundColor Red
    exit 1
}

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
    if ($script:IsSandbox) { return $true }   # sandbox never touches the device (see $IsSandbox)
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
. (Join-Path $PSScriptRoot '..\lib\NativeExit.ps1')    # exit codes via $global:LASTEXITCODE (immune to caller shadowing)
. (Join-Path $PSScriptRoot '..\lib\SourceDigest.ps1')  # build-input digest (local pre-commit provenance)
. (Join-Path $PSScriptRoot '..\lib\TestVerdict.ps1')   # shared verdict vocabulary (library; tools/lib)
. (Join-Path $PSScriptRoot '..\lib\HilLock.ps1')       # machine lock + machine-wide HIL safety record
. (Join-Path $PSScriptRoot '..\lib\PhcRestoration.ps1') # pure PHC/QPC restoration evaluator (D1 no-mutation verification)

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
    "test_i210_device_ops",    # production I210 frequency ops + init contract vs datasheet 8.15.11 (offline, fake register file; #3 #149 #326 #343)
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
    "ps1:tests/unit/ioctl/test_hil_safety_gate.ps1",            # TEST-HILGATE-001..031: HIL safety gate + state integrity (sandboxed, no driver; #343)
    "ps1:tests/unit/ci/test_hil_binary_provenance.ps1",         # TEST-HILPROV: build manifest + dispatch provenance gate (sandboxed, no driver; #328 F23)
    "ps1:tests/unit/ci/test_runner_results_json.ps1",           # TEST-RESULTSJSON: machine-readable runner results (sandboxed, no driver)
    "ps1:tests/unit/build/test_native_exit_codes.ps1",          # TEST-NATIVE-EXIT: exit codes survive nested script/native calls (FullSuite_20261021 blank codes)
    "ps1:tests/unit/ci/test_hil_suite_safety.ps1",              # TEST-HILSUITE: automatic HIL suites cannot reach TSN configuration or nested execution (was never run by CI - TEST-COMPLETE-003)
    "ps1:tests/unit/ci/test_hil_trust_boundary.ps1",            # TEST-HILTRUST: HIL workflow trust boundary, acceptance verdicts (was never run by CI - TEST-COMPLETE-003; needs python)
    "ps1:tests/unit/ci/test_hil_safety_record.ps1",
    "ps1:tests/unit/ci/test_phc_restoration_evaluator.ps1",    # TEST-PHCEVAL: pure PHC/QPC restoration evaluator (synthetic characterization; not hardware evidence)             # TEST-SAFETYREC: machine-wide HIL safety record, fail-closed, explicit verified clearing (offline)
    "ps1:tests/unit/ci/test_artifact_completeness.ps1",         # TEST-COMPLETE: every build target / test script is a runner entry or classified (offline; RED until classified)
    "ps1:tests/unit/ci/test_local_suites.ps1",                  # TEST-LOCALSUITES: local pre-commit HIL orchestration + DbgView reproduction contract (offline)
    "ps1:tests/unit/ci/test_hil_baseline_evidence.ps1",         # TEST-HILBASE: baseline evidence producer, verdict rules, never writes the safety record (offline, synthetic; D3)
    "ps1:tests/unit/ci/test_tx_timestamp_probe.ps1",            # TEST-TXPROBE: TC-CORR-TX-RX-001 phase-1 probe interpretation + non-destructive construction (offline; D4)
    "ps1:tests/unit/ci/test_ci_driver_deploy.ps1",              # TEST-CIDEPLOY: staged package / unattended install / lock / crash evidence orchestration (offline)
    "ps1:tests/unit/ci/test_agent_testing_guidance.ps1",        # TEST-AGENTGUIDE: agent testing-contract wiring, skill frontmatter, runner params in guidance (offline, static; not agent-behaviour evidence)
    "ps1:tests/unit/ci/test_codeql_driver_config.ps1",          # TEST-CODEQLCFG: pinned CodeQL driver config + inventory + workflow wiring (offline, static; not CodeQL resolution evidence)
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
    "tsn_ioctl_test",                 # same source as test_tsn_ioctl_handlers_um, built by tsn_ioctl_test.mak
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

# Build targets (Build-Tests.ps1 Output name) and tests/unit test scripts that are deliberately NOT runner entries
# (tests/unit/ci/test_artifact_completeness.ps1).  Value: 'Diagnostic: <justification>' | 'Supporting: <consumer ...>' |
# 'Obsolete: <evidence ...>'.  Anything neither listed here nor a runner entry is Unknown and fails the gate.
# Never execute a diagnostic tool merely because it is built.  Empty 2026-10-21: every artifact still needs review.
$TestArtifactClassification = [ordered]@{
}

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
    "test_ioctl_routing",              # IOCTL routing through the filter (read-only IOCTLs)
    "test_ioctl_simple",               # basic open/close cycle
    "test_ioctl_abi",                  # IOCTL ABI backward-compat (TEST-ABI-001)
    # TX timestamp provenance + V1/V2 ABI (REQ-F-PTP-007, #199, #317)
    "test_tx_timestamp_provenance",    # Section L (logic) + Section H (FastIo V1/V2 canary,
                                       # self-comparison, masquerade, cross-handle isolation)
    "test_tx_timestamp_retrieval",     # TX timestamp FIFO retrieval; per-adapter results
    # PHC query / availability
    "test_ioctl_phc_query",            # IOCTL_AVB_GET_TIMESTAMP read-only
    "test_ioctl_xstamp",               # cross-timestamp PHC---QPC (VV-CORR-003)
    "test_ioctl_phc_crosstimestamp_v2", # V2 provenance per adapter; INCONCLUSIVE=FAIL (#340)
    # ptp_clock_control_test, test_rx_timestamp, test_rx_timestamp_complete: see $HardwareMandatoryCoverageGaps
    # Multi-adapter isolation
    "test_all_adapters",               # enumerate + open all adapters
    "test_multidev_adapter_enum",      # per-adapter VID/DID identity
    "test_multi_adapter_phc_sync",     # independent PHC per adapter
    # Send-path + provenance
    "test_send_ptp_debug"              # IOCTL_AVB_TEST_SEND_PTP diagnostic (PRE_SEND_PHC)
)

# ===========================
# HardwareMutation --- PHC/TAS hardware-mutating tests (isolated lane)
# ===========================
# These tests write to PHC epoch, clock increment, or TAS GCL registers.
# Run in a SEPARATE CI job AFTER HardwareMandatory, with continue-on-error in CI
# to avoid blocking on mutation instability.
# Recovery: each mutation test must restore state before exit; CI records
# cleanup results separately; they never halt the mutation lane (Q 2026-10-28).
#
# EXCLUDED: driver reinstall (UT-CORR-009), system sleep (test_s3_sleep_wake).
$HardwareMutationTests = @(
    # Every eligible automatic mutation entry.  Empty until UT-CORR-005/006 are hardware-verified in their own
    # executable (decision B); the suite is then INCOMPLETE under GitHub Actions, never PASSED.
)
# Attended-only (decision A, Q 2026-10-21): PHC epoch reset and frequency adjustment.  --case selects the sub-test,
# but the executable also contains the UT-CORR-009 driver lifecycle path (process creation), so under GitHub Actions
# each entry is a declared coverage gap (NOT EXECUTED, run INCOMPLETE; TEST-HILSUITE-REAL-003 unchanged); outside it
# the entry is appended to the HardwareMutation run.  Removed only after B: UT-CORR-009 split out and 005/006
# hardware-verified including restoration.  Reasons must not end in a parenthesis.
$HardwareMutationAttendedOnly = [ordered]@{
    'test_ptp_phc_stability --case UT-CORR-005' = 'Mutating:Target: SET_TIMESTAMP 1 ms plus restore; UT-CORR-009 split into test_ptp_phc_lifecycle 2026-10-22, TEST-HILSUITE-REAL-003; attended runs only until UT-CORR-005 is hardware-verified, decision B'
    'test_ptp_phc_stability --case UT-CORR-006' = 'Mutating:Target: ADJUST_FREQUENCY +1 ns plus restore; UT-CORR-009 split into test_ptp_phc_lifecycle 2026-10-22, TEST-HILSUITE-REAL-003; attended runs only until UT-CORR-006 is hardware-verified, decision B'
}

# ===========================
# Hardware-dependent test lists (self-hosted runner only)
# ===========================
# Unit-level tests that require the driver device node (\\.\.\IntelAvbFilter) or Intel NIC.
# Safe to run on an automated self-hosted runner -- none of these hibernate or sleep the PC.
#
# NOT EXECUTED yet, declared in $HardwareIntegrationCoverageGaps (every run reports them as required, NOT EXECUTED):
#   test_s3_sleep_wake          -- TC-S3-002 calls SetSuspendState (host S3; no unattended wake / recovery facility yet).
#   test_vv_corr_001_stability  -- VV-CORR-001 24-hour PHC stability monitor (no long-duration lane yet; #317 Track E).
#   test_hot_plug               -- disables/enables the NIC through devcon (#262).  Unsafe in $HilTestSafety (#343).
#   test_lifecycle_coverage     -- CM_Disable_DevNode / CM_Enable_DevNode on the adapter.  Unsafe (#343).
#   Unsafe entries run only via an explicit -Tests list with AVB_HIL_AUTHORIZE_UNSAFE naming the exact
#   entry, outside GitHub Actions, on an operator-attended machine.
# Mutating and Mutating:Target entries are listed in $HardwareUnitCoverageGaps (automatic HIL coverage gaps).
$HardwareUnitTests = @(
    "test_cleanup_archive",        # checks build output against installed driver
    "test_ioctl_simple",           # opens \\.\IntelAvbFilter via CreateFile
    "test_ioctl_routing",          # IOCTL routing through the filter (ENUM/OPEN/READ_REGISTER/GET_CLOCK_CONFIG)
    "test_ioctl_trace",            # DeviceIoControl trace IOCTLs
    "test_minimal_ioctl.exe",
    "test_clock_config.exe",
    "test_clock_working",
    "test_direct_clock",

    "test_hal_performance",        # HAL timer/DMA performance -- needs NIC
    "test_atdecc_event",           # ATDECC event subscription via driver IOCTL
    "test_atdecc_aen_protocol",    # ATDECC AEN protocol -- driver IOCTL path
    "test_ioctl_version",          # IOCTL_AVB_GET_VERSION to driver
    "test_mdio_phy",
    "test_dev_lifecycle",
    "test_ts_event_sub",
    "test_ptp_getset",
    "test_power_management",       # open->PHC->close->200ms Sleep->reopen (no system sleep)
    "test_tx_timestamp_provenance",  # Section L: pure logic + ABI static_assert (no device needed)
                                     # Section H SKIPs gracefully when driver is absent.
    "test_ptp_freq",
    "test_ptp_freq_complete",
    "test_hw_ts_ctrl",
    "test_rx_timestamp",
    "test_rx_timestamp_complete",
    "test_ioctl_target_time",       # IOCTL target time management -- (#199)
    "test_qav_cbs",
    "test_ioctl_tas",
    "test_ioctl_fp_ptm",
    # Phase A: IOCTL functional tests (REQ #2-#13, previously missing from CI)
    "test_tx_timestamp_retrieval", # TX timestamp retrieval -- REQ-F-IOCTL-TS-001 (#35)
    "test_ioctl_phc_query",        # PHC query IOCTL -- REQ-F-PTP-PHC (#192)
    "test_ioctl_offset",
    "test_ioctl_phc_epoch",        # PHC epoch management -- (#196)
    "test_ioctl_phc_monotonicity",  # PHC monotonicity check -- (#197)
    "test_ioctl_xstamp",           # cross-timestamp (PTP/system correlation) -- (#198)
    "test_ioctl_xstamp_v2",        # V2 ABI + HIL reachability (#340)
    "test_ioctl_abi",
    "test_ioctl_access_control",    # IOCTL access control enforcement -- (#341)
    "test_ioctl_buffer_fuzz",       # IOCTL buffer fuzzing -- (#342)
    "test_tsn_ioctl_handlers",
    "ptp_clock_control_test",
    "test_ptp_crosstimestamp",
    "test_ioctl_phc_crosstimestamp_v2",  # PHC cross-timestamp V2 ABI (#340)
    "test_ioctl_abi",              # IOCTL ABI backward-compatibility
    # test_tsn_ioctl_handlers removed (#328 F23): its build entry is Disabled (KM placeholder); a stale binary
    # of it ran here as ReadOnly and issued SETUP_TAS/FP/PTM.  The UM test test_tsn_ioctl_handlers_um stays
    # classified Mutating (not exact-target-aware -> BLOCKED by the safety gate).
    # Phase B: PHC/QPC coherence + VV coverage (added 2026-03-29)
    "test_ptp_crosstimestamp",     # cross-domain timestamp correlation -- VV-CORR-003
    "test_zero_polling_overhead",  # interrupt-driven delivery, zero-poll -- #310
    "test_lifecycle_coverage",
    # Phase C: Additional unit tests registered 2026-04-01 (built but previously absent from CI)
    "test_hw_state",               # hardware state enum/OID query -- REQ-F-HWCTX-001 (#18)
    "test_statistics_counters",    # IOCTL statistics counters -- REQ-NF-DIAG-001 (#270)
    "test_event_log",
    "test_build_sign",             # build & code-signing verification -- (#243)
    "test_compat_win11",           # Windows 11 compatibility smoke -- (#256)
    "test_win7_stub",              # OS compat Win10+ assertion -- (#258)
    "test_eee_lpi",
    "test_pfc_pause",
    "test_srp_interface",
    "test_vlan_tag",                # VLAN tag handling -- (#343)
    "test_vlan_pcp_tc_mapping",     # VLAN PCP to TC mapping -- (#344)
    "test_security_validation",
    "test_ioctl_launch_time",
    "test_avtp_tu_bit_events",     # AVTP TU-bit change events -- (#175)
    "test_ptp_event_latency",      # PTP event ring-buffer latency -- (#177)
    "test_event_latency_4ch",      # 4-channel multi-observer event latency -- (#179)
    "test_event_nf_zero_polling",  # NF CPU-overhead budget quantification -- (#241, TEST-EVENT-NF-002)
    "test_event_nf_latency",       # NF IOCTL-path latency P99 under adversarial load -- (#245, TEST-EVENT-NF-001)
    "test_gptp_phc_interface",
    "test_send_ptp_debug",         # IOCTL_AVB_TEST_SEND_PTP diagnostic -- (#51)
    "test_timestamp_latency",      # TX/RX timestamp latency <1us -- (#272)
    "test_perf_regression",        # performance regression baseline -- (#225)
    "test_ndis_fastpath_latency",  # NDIS IOCTL fast-path latency P99<10us -- (#286)
    "test_hot_plug",
    "test_error_recovery",
    "test_multi_adapter_phc_sync",  # multi-adapter PHC sync/independence -- (#208)
    # Provenance regression (TEST-PROV-L01..L05 logic + TEST-PROV-H01..H05 device contract)
    "test_tx_timestamp_provenance"  # V1/V2 IOCTL ABI, FastIo canary, cross-handle isolation
)

# Integration-level tests that require the driver device node or Intel NIC.
# Safe to run on an automated self-hosted runner.
$HardwareIntegrationTests = @(
    "ssot_register_validation_test",
    "test_tsn_ioctl_handlers_um",
    "tsn_ioctl_test",                 # same source as test_tsn_ioctl_handlers_um, built by tsn_ioctl_test.mak

    "test_all_adapters",              # enumerates all Intel AVB adapters
    "test_multidev_adapter_enum",     # multi-device adapter enumeration
    "test_device_register_access",    # direct device register read/write
    "test_ndis_send_path",            # NDIS send path -- needs NIC
    "test_ndis_receive_path",         # NDIS receive path -- needs NIC
    "test_hw_state_machine",
    "test_lazy_initialization",       # lazy init via driver device open
    "test_registry_diagnostics",      # registry diagnostics -- driver service required
    "test_ptp_corr_extended",
    # Phase B: extended cross-domain / correlation tests (added 2026-03-29)
    "test_vv_corr_003_crossdomain",   # cross-domain V&V validation -- VV-CORR-003
    # Phase C: Additional integration tests registered 2026-04-01 (built but previously absent from CI)
    "test_ptp_001",                   # TEST-PTP-001 HW timestamp correlation for gPTP -- (#238 #149)
    "test_ptp_corr",                  # PTP HW correlation IT-CORR-001..004 -- (#199)
    "test_ptp_phc_stability",         # UT-CORR-007/008 TX-PHC correlation (#149 #317); no args -> UT-CORR-009 reinstall unreachable
    "test_ptp_001",                   # TEST-PTP-001 HW timestamp correlation for gPTP -- (#238 #149)
    "ptp_clock_control_production",
    "ptp_ioctl_latency_test",
    "hw_timestamping_control",
    "rx_timestamping",
    "tsauxc_toggle_test",
    "test_gptp_daemon_coexist",
    "avb_test_i219",
    "avb_test_portability",
    "avb_test_i217",
    "avb_test_i225",
    "avb_test_i226_um"
    # Phase D/E/F (avb_test_i219, avb_test_portability, avb_test_i217/i225/i226_um): see $HardwareIntegrationCoverageGaps
    "test_hardware_detection"         # HW capability detection & register access safety -- (#44 #45, #287 #288)
)

# ===========================
# Automatic HIL coverage gaps
# ===========================
# HardwareMandatory, HardwareUnit and HardwareIntegration run unattended on every trusted master push
# (.github/workflows/hil.yml, after CI and its deploy-staging succeeded).  They list ReadOnly and Init entries only:
# a Mutating entry would be BLOCKED (a failure on every run), and a Mutating:Target entry changes adapter state
# that an unattended runner cannot restore when cleanup fails (test_ptp_freq_complete left I226 TIMINCA changed
# on 2026-10-20).  Every entry removed for that reason is listed here with its reason; the runner reports each
# one as a skipped TC-HIL-COVERAGE-GAP-001 case, so the gap is visible in JUnit and never counted as a pass.
# The entries stay classified in $HilTestSafety and run only through an explicit -Tests list on an attended run.
# Reasons must not end in a parenthesis (the JUnit converter strips a trailing "(...)" from case names).
$HardwareMandatoryCoverageGaps = [ordered]@{
    'test_minimal_ioctl'           = 'Init: TEMPORARY POLICY EXCLUSION 2026-10-21 - INIT_DEVICE re-initializes the targeted adapter PTP block, I226 SYSTIM := 1 and TIMINCA rewritten, I219 TIMINCA rewritten per driver source, UNVERIFIED on hardware; post-test check misses the PHC time discontinuity; INIT on an unbound handle hits the global default adapter, which depends on the previous test'
    'ptp_clock_control_test'     = 'Mutating: TC-5b ADJUST_FREQUENCY writes TIMINCA, plus SET_TIMESTAMP and INIT_DEVICE; contradicts the non-mutating Mandatory contract; split into read-only and mutating executables tracked in #340'
    'test_rx_timestamp'          = 'Mutating: SET_RX_TIMESTAMP, SET_QUEUE_TIMESTAMP; not exact-target-aware'
    'test_rx_timestamp_complete' = 'Mutating: SET_RX_TIMESTAMP, SET_QUEUE_TIMESTAMP; not exact-target-aware'
}
$HardwareUnitCoverageGaps = [ordered]@{
}
$HardwareIntegrationCoverageGaps = [ordered]@{
    # Formerly only commented out of every list (silently absent); declared so every run reports them NOT EXECUTED.
    'test_vv_corr_001_stability'    = 'ReadOnly: TEMPORARY POLICY EXCLUSION 2026-10-21 - VV-CORR-001 requires a 24-hour run, default --hours 24; no long-duration HIL lane exists and shortening the run would change its acceptance criterion; #317 Track E'
    'test_s3_sleep_wake'            = 'Unsafe: TC-S3-002 calls SetSuspendState, host S3 transition; no unattended wake facility such as an RTC wake timer or remote power control, and no recovery path if the host does not resume'
    'test_hot_plug'                 = 'Unsafe: disables and re-enables the NIC through an external device-manager tool, #262; requires an independent management link and verified recovery before unattended execution, the NIC may carry the runner connection'
    'test_ptp_phc_lifecycle'        = 'Unsafe: UT-CORR-009 stops the IntelAvbFilter service and reinstalls the driver by process creation, split from test_ptp_phc_stability 2026-10-22; attended runs only, AVB_HIL_AUTHORIZE_UNSAFE naming this entry; #149 #317'
    'test_lifecycle_coverage'       = 'Unsafe: disables and re-enables the adapter device node through the configuration manager API; requires an independent management link and verified recovery before unattended execution, the NIC may carry the runner connection'
}

# ===========================
# HIL safety registry (#343) --- enforced by the runner before every Hardware* test
# ===========================
# Key = the exact suite entry (binary + arguments).  An entry missing from this table is BLOCKED.
# Classes (from the comment- and string-stripped source, including local headers):
#   ReadOnly        no IOCTL that changes adapter/PHC/TSN state (may open, read, timestamp, transmit)
#   Init            IOCTL_AVB_INIT_DEVICE only (re-runs device init; I226 reseeds SYSTIM) --- runs, state verified
#   Mutating        changes PHC/TSN/PHY state but cannot be pointed at one exact adapter --- BLOCKED
#   Mutating:Target changes state only on the adapter named by AVB_HIL_TARGET_DID (AvbTestSelectTarget or the
#                   shared AvbEnumerateAdapters filter) --- run once per supported, enabled adapter
#   Unsafe          NIC / driver administration (disable, enable, restart, reinstall) --- BLOCKED unless
#                   AVB_HIL_AUTHORIZE_UNSAFE names the exact entry; never under GitHub Actions
#   Unknown         no reviewable source --- BLOCKED
# The table is checked against the suite lists and the sources by tests/unit/ioctl/test_hil_safety_gate.ps1.
# Init entries under GitHub Actions (unattended HIL): temporary policy exclusion 2026-10-21 (Q, Decision A).
# INIT_DEVICE re-initializes the targeted adapter's PTP block (I226: SYSTIM := 1, TIMINCA rewritten; I219: TIMINCA
# rewritten), unbound-handle callers hit whichever adapter the previous test opened, and the post-test state check
# does not detect a PHC time discontinuity.  Attended runs (outside GitHub Actions) keep running Init entries.
$HilInitUnattendedApproved = $false

# Hazard holds (Q 2026-10-22: lists are authoritative; an unresolved, evidenced unsafe hardware condition blocks only
# the affected entries, visibly, at dispatch).  Key = registry key.  Removed only when the defect is hardware-verified fixed.
$Hold328Tsn = '#328 SETUP_TAS/FP/PTM put the e2fn-owned I226 into TSN transmit mode without a restorable gate schedule, driver setup_tas defect, TQAVCTRL 0x00000009 read back 2026-10-22 13:44 before I226 send completions stopped and the next driver unload hung, investigations/issue-328-release-reinstall-hang-20261022.md; leaving TSN mode needs an adapter reset, held until the driver TAS/FP restore is fixed and hardware-verified'
$HilHazardHolds = @{
    'avb_test_portability'       = $Hold328Tsn
    'avb_test_i217'              = $Hold328Tsn
    'avb_test_i225'              = $Hold328Tsn
    'avb_test_i226_um'           = $Hold328Tsn
    'test_ioctl_tas'             = $Hold328Tsn
    'test_ioctl_fp_ptm'          = $Hold328Tsn
    'test_tsn_ioctl_handlers_um' = $Hold328Tsn
    'test_tsn_ioctl_handlers'    = $Hold328Tsn
    'tsn_ioctl_test'             = $Hold328Tsn
}

$HilTestSafety = @{
    # --- ReadOnly ---
    'test_cleanup_archive'             = 'ReadOnly'
    'test_ioctl_simple'                = 'ReadOnly'
    'test_ioctl_trace'                 = 'ReadOnly'
    'test_hal_performance'             = 'ReadOnly'
    'test_atdecc_event'                = 'ReadOnly'
    'test_atdecc_aen_protocol'         = 'ReadOnly'
    'test_ioctl_version'               = 'ReadOnly'
    'test_power_management'            = 'ReadOnly'
    'test_tx_timestamp_provenance'     = 'ReadOnly'
    'test_tx_timestamp_retrieval'      = 'ReadOnly'
    'test_ioctl_phc_query'             = 'ReadOnly'
    'test_ioctl_phc_epoch'             = 'ReadOnly'
    'test_ioctl_xstamp'                = 'ReadOnly'
    'test_ioctl_xstamp_v2'             = 'ReadOnly'
    'test_ioctl_abi'                   = 'ReadOnly'   # static sizeof/code checks; no DeviceIoControl call
    'test_ptp_crosstimestamp'          = 'ReadOnly'
    'test_zero_polling_overhead'       = 'ReadOnly'   # launches "wpr.exe -status" only
    'test_hw_state'                    = 'ReadOnly'
    'test_statistics_counters'         = 'ReadOnly'   # "devcon restart" appears only in a SKIP message
    'test_build_sign'                  = 'ReadOnly'
    'test_compat_win11'                = 'ReadOnly'
    'test_win7_stub'                   = 'ReadOnly'
    'test_avtp_tu_bit_events'          = 'ReadOnly'
    'test_ptp_event_latency'           = 'ReadOnly'
    'test_event_latency_4ch'           = 'ReadOnly'
    'test_event_nf_zero_polling'       = 'ReadOnly'
    'test_event_nf_latency'            = 'ReadOnly'
    'test_send_ptp_debug'              = 'ReadOnly'
    'test_timestamp_latency'           = 'ReadOnly'
    'test_perf_regression'             = 'ReadOnly'
    'test_ndis_fastpath_latency'       = 'ReadOnly'
    'test_multi_adapter_phc_sync'      = 'ReadOnly'
    'test_ioctl_phc_crosstimestamp_v2' = 'ReadOnly'
    'test_all_adapters'                = 'ReadOnly'
    'test_multidev_adapter_enum'       = 'ReadOnly'
    'test_device_register_access'      = 'ReadOnly'
    'test_ndis_send_path'              = 'ReadOnly'
    'test_ndis_receive_path'           = 'ReadOnly'
    'test_lazy_initialization'         = 'ReadOnly'
    'test_registry_diagnostics'        = 'ReadOnly'
    'test_vv_corr_003_crossdomain'     = 'ReadOnly'
    'test_ptp_001'                     = 'ReadOnly'   # ADJUST_FREQUENCY appears only in comments/printf text
    'test_ptp_corr'                    = 'ReadOnly'
    'test_ptp_phc_stability'           = 'ReadOnly'   # no args = UT-CORR-007/008 only; 005/006 need --case; UT-CORR-009 is test_ptp_phc_lifecycle
    'test_hardware_detection'          = 'ReadOnly'
    'ssot_register_validation_test'    = 'ReadOnly'   # GET_DEVICE_INFO, GET_HW_STATE, READ_REGISTER on the default handle only; no write
    # --- Init (IOCTL_AVB_INIT_DEVICE) ---
    'test_clock_config'                = 'Init'
    'test_clock_working'               = 'Init'
    'test_direct_clock'                = 'Init'
    'test_dev_lifecycle'               = 'Init'       # "devcon" appears only in a SKIP message
    'test_minimal_ioctl'               = 'Init'
    'test_ioctl_routing'               = 'ReadOnly'   # INIT_DEVICE only a GetIoctlName() case label; sends ENUM/OPEN/READ_REGISTER/GET_CLOCK_CONFIG
    'test_hw_state_machine'            = 'Init'
    # --- Mutating, exact-target-aware ---
    'test_ptp_freq'                    = 'Mutating:Target'   # AvbTestSelectTarget
    'test_ptp_freq_complete'           = 'Mutating:Target'   # AvbTestSelectTarget
    'test_ptp_phc_stability --case UT-CORR-005' = 'Mutating:Target'   # AvbTestSelectTargetIndex
    'test_ptp_phc_stability --case UT-CORR-006' = 'Mutating:Target'   # AvbTestSelectTargetIndex
    'test_qav_cbs'                     = 'Mutating:Target'   # shared AvbEnumerateAdapters/AvbOpenAdapter only
    'avb_test_i217'                    = 'Mutating:Target'   # shared AvbEnumerateAdapters/AvbOpenAdapter only
    'avb_test_i219'                    = 'Mutating:Target'   # shared AvbEnumerateAdapters/AvbOpenAdapter only
    'avb_test_i225'                    = 'Mutating:Target'   # shared AvbEnumerateAdapters/AvbOpenAdapter only
    'avb_test_i226_um'                 = 'Mutating:Target'   # shared AvbEnumerateAdapters/AvbOpenAdapter only
    'avb_test_portability'             = 'Mutating:Target'   # shared AvbEnumerateAdapters/AvbOpenAdapter only
    # --- Mutating, not target-aware (BLOCKED until they honour AVB_HIL_TARGET_DID) ---
    'test_mdio_phy'                    = 'Mutating'   # MDIO_WRITE
    'test_ts_event_sub'                = 'Mutating'   # SET_HW/RX/QUEUE_TIMESTAMP, SET_TARGET_TIME
    'test_ptp_getset'                  = 'Mutating'   # SET_TIMESTAMP, SET_HW_TIMESTAMPING
    'test_hw_ts_ctrl'                  = 'Mutating'   # SET_HW_TIMESTAMPING
    'test_rx_timestamp'                = 'Mutating'   # SET_RX_TIMESTAMP, SET_QUEUE_TIMESTAMP
    'test_rx_timestamp_complete'       = 'Mutating'   # SET_RX_TIMESTAMP, SET_QUEUE_TIMESTAMP
    'test_ioctl_target_time'           = 'Mutating'   # SET_TARGET_TIME, SET_HW_TIMESTAMPING
    'test_ioctl_tas'                   = 'Mutating'   # SETUP_TAS, DISARM_TAS
    'test_ioctl_fp_ptm'                = 'Mutating'   # SETUP_FP, SETUP_PTM
    'test_ioctl_offset'                = 'Mutating'   # PHC_OFFSET_ADJUST
    'test_ioctl_phc_monotonicity'      = 'Mutating'   # PHC_OFFSET_ADJUST
    'test_ioctl_access_control'        = 'Mutating'   # PHC_OFFSET_ADJUST
    'test_ioctl_buffer_fuzz'           = 'Mutating'   # ADJUST_FREQUENCY, PHC_OFFSET_ADJUST
    'ptp_clock_control_test'           = 'Mutating'   # SET_TIMESTAMP, ADJUST_FREQUENCY, INIT_DEVICE
    'test_event_log'                   = 'Mutating'   # ADJUST_FREQUENCY
    'test_eee_lpi'                     = 'Mutating'   # EEE_ENABLE/DISABLE
    'test_pfc_pause'                   = 'Mutating'   # PFC_ENABLE
    'test_srp_interface'               = 'Mutating'   # SRP_REGISTER/DEREGISTER_STREAM
    'test_vlan_tag'                    = 'Mutating'   # VLAN_ENABLE/DISABLE
    'test_vlan_pcp_tc_mapping'         = 'Mutating'   # SETUP_QAV
    'test_security_validation'         = 'Mutating'   # SET_TIMESTAMP
    'test_ioctl_launch_time'           = 'Mutating'   # SET_LAUNCH_TIME
    'test_gptp_phc_interface'          = 'Mutating'   # ADJUST_FREQUENCY, PHC_OFFSET_ADJUST
    'test_error_recovery'              = 'Mutating'   # ADJUST_FREQUENCY, PHC_OFFSET_ADJUST
    'test_tsn_ioctl_handlers_um'       = 'Mutating'   # SETUP_TAS/FP/PTM, INIT_DEVICE
    'test_tsn_ioctl_handlers'          = 'Mutating'   # same source as test_tsn_ioctl_handlers_um; hazard-held (#328 F23: stale 2026-04-08 binary issued SETUP_TAS/FP/PTM)
    'tsn_ioctl_test'                   = 'Mutating'   # same source as test_tsn_ioctl_handlers_um (tsn_ioctl_test.mak); hazard-held
    'test_ptp_corr_extended'           = 'Mutating'   # SET_HW_TIMESTAMPING
    'ptp_clock_control_production'     = 'Mutating'   # SET_TIMESTAMP, ADJUST_FREQUENCY
    'ptp_ioctl_latency_test'           = 'Mutating'   # SET_TIMESTAMP, INIT_DEVICE
    'hw_timestamping_control'          = 'Mutating'   # SET_HW_TIMESTAMPING
    'rx_timestamping'                  = 'Mutating'   # SET_RX_TIMESTAMP, SET_QUEUE_TIMESTAMP
    'tsauxc_toggle_test'               = 'Mutating'   # SET_HW_TIMESTAMPING
    'test_gptp_daemon_coexist'         = 'Mutating'   # ADJUST_FREQUENCY, PHC_OFFSET_ADJUST
    # --- Unsafe (NIC administration; removed from every suite list) ---
    'test_hot_plug'                    = 'Unsafe'     # devcon enable/disable via CreateProcess
    'test_lifecycle_coverage'          = 'Unsafe'     # CM_Disable_DevNode / CM_Enable_DevNode
    'test_ptp_phc_lifecycle'           = 'Unsafe'     # UT-CORR-009: ControlService STOP + Install-Driver-Elevated.ps1 Reinstall (split from test_ptp_phc_stability)
    'test_s3_sleep_wake'               = 'Unsafe'     # SetSuspendState (host S3); GET_CLOCK_CONFIG / GET_HW_STATE otherwise
    # --- ReadOnly (24-hour run; declared gap) ---
    'test_vv_corr_001_stability'       = 'ReadOnly'   # ENUM/OPEN_ADAPTER, GET_TIMESTAMP, GET_HW_STATE, TEST_SEND_PTP
}

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

# Verification class (not a new lane): every suite, HardwareMutation included, is mandatory verification
# (tools/lib/TestVerdict.ps1).  Mandatory + SKIP_NOT_IMPLEMENTED = INCOMPLETE, mandatory + INCONCLUSIVE = nonpassing.
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
# Per-entry timeout (Get-AvbTestTimeoutSeconds) where a test's own bounded waits or its measured runtime exceed the suite
# default; it never lowers the suite timeout and never changes the test's acceptance criteria.  Every value is derived
# from the test source or from a recorded measurement.  Keys are the suite-list entries exactly as listed.
$HilTestTimeoutSeconds = [ordered]@{
    # Per adapter: TC-AEN-002 30 s + TC-AEN-005 60 s + TC-AEN-006 31 s of deadlines/sleeps (tests/atdecc/
    # test_atdecc_aen_protocol.c) = 121 s; 3 adapters on the HIL host = 363 s, killed at 300 s on adapter 3 in
    # LocalSuites_20261021_173805 .. 20261022_094632.  420 s = 363 s + 16 %.  TC-AEN-001 adds 80 s per adapter only with
    # AVB_TEST_HAS_ATDECC_CONTROLLER=1 (not set on the HIL host; it would need 603 s).
    'test_atdecc_aen_protocol' = 420
    # 83 sandboxed runner cases.  Killed at the 60 s Unit timeout (FAILED(TIMEOUT)) in every Unit run of 2026-10-22 ..
    # 2026-10-27 (8 runs), so the suite never completed in the chain.  Measured on the HIL host 2026-10-27: 199.6 s and
    # 202.3 s (83/83).  300 s = 202.3 s + 48 % for slower GitHub-hosted Unit runners (their runtime: UNVERIFIED).
    'ps1:tests/unit/ioctl/test_hil_safety_gate.ps1' = 300
}

# ===========================
# Paths
# ===========================
$scriptDir   = $PSScriptRoot                                        # tools/test
$repoRoot    = Split-Path (Split-Path $scriptDir -Parent) -Parent  # repository root
$testExeDir  = Join-Path $repoRoot "build\tools\avb_test\x64\$Configuration"

# -LogsDir isolates a run's evidence: the behavioral sentinel tests use it, and the automatic HIL jobs in
# .github/workflows/hil.yml pass a per-suite directory under RUNNER_TEMP (emptied by the runner for every job)
# so a persistent self-hosted workspace can never contribute an earlier run's logs.  Without it: <repo>/logs.
if ($LogsDir -and $LogsDir.Trim() -ne "") {
    $logsDir = $LogsDir
} else {
    $logsDir = Join-Path $repoRoot "logs"
}

# Sandbox mode (offline runner tests only, e.g. tests/unit/ioctl/test_hil_safety_gate.ps1):
# adapter/PHC state comes from the JSON file AVB_TEST_ADAPTER_STATE_FILE, the driver health check and
# lifecycle snapshots are stubbed, and ONLY fake ps1 tests outside the repository may execute.
# It requires an explicit -LogsDir outside <repo>/logs so sandbox evidence can never mix with HIL evidence.
$IsSandbox = $false
if ($env:AVB_TEST_SANDBOX -eq '1') {
    $defaultLogsFull = [System.IO.Path]::GetFullPath((Join-Path $repoRoot 'logs')).TrimEnd('\')
    $logsFull        = [System.IO.Path]::GetFullPath($logsDir).TrimEnd('\')
    if (-not ($LogsDir -and $LogsDir.Trim() -ne '') -or $logsFull.StartsWith($defaultLogsFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        Write-Fail "AVB_TEST_SANDBOX=1 requires -LogsDir outside $defaultLogsFull --- refusing to run (no test executed)"
        exit 1
    }
    $IsSandbox = $true
}
# Sandbox runs never export HIL identity or JUnit paths into the real CI job environment.
$ExportGitHubEnv = (($env:GITHUB_ACTIONS -eq 'true') -and -not $IsSandbox)

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
        if ($ExportGitHubEnv) {
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
        if ($ExportGitHubEnv) {
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
        if ($ExportGitHubEnv) {
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
# HIL safety gate + state integrity (#343)
# ===========================
# OBSERVATION ONLY: the runner never enables, disables, restarts or rebinds an adapter, a binding or the
# driver.  A supported adapter that is unavailable is reported (BLOCKED / INFRA_FAILURE) and left as found.
#
#   baseline   supported-DID set from the SSOT (AvbGetIntelDeviceType + intel_pci_ids.h), PnP status /
#              problem code, NetAdapter admin status, IntelAvbFilter binding, and per-adapter TIMINCA/TSAUXC
#              via ENUM_ADAPTERS -> OPEN_ADAPTER -> GET_CLOCK_CONFIG (read-only; the same OPEN_ADAPTER loop the
#              lifecycle snapshot already performs).
#   gate       before every Hardware* test, from $HilTestSafety (see the table above the suite resolution).
#   post-test  after every executed test, also after a timeout kill: compare with the baseline (settle up to
#              $HilSettleSeconds); a difference - link state and TSAUXC included (P0 2026-10-21; previously only
#              warnings) - is cleanup FAILED (TC-INFRA-STATE-001), no halt (Q 2026-10-28).  After a non-ReadOnly test the
#              restoration of what the runner cannot observe (PHC time/rate/offset, TSN/PHY/timestamp-control state)
#              is cleanup NOT_VERIFIED (TC-INFRA-STATE-002): never assumed restored, reported independently of the
#              functional verdict, blocks no later test (Q 2026-10-28; Add-AvbCleanupResult).
# Per-entry mutation matrix (P0 2026-10-21): the hardware/system state a non-ReadOnly entry may modify, from its source
# and hardware evidence.  Vocabulary: ADAPTER TIMINCA TSAUXC PHC_TIME PHC_RATE TAS FP PTM QAV PFC EEE VLAN MDIO
# TS_CONTROL TARGET_TIME DRIVER_LIFECYCLE.  The runner itself re-reads only $HilVerifiableState; an entry whose declared
# state is a subset of it and re-reads equal to the baseline has VERIFIED restoration.  Undeclared entries, or state the
# runner cannot observe, are cleanup NOT_VERIFIED (never a halt; Q 2026-10-28 superseded option 1).  Widen
# $HilVerifiableState only together with a new independent check; never declare less than an entry really modifies.
$HilVerifiableState = @('ADAPTER', 'TIMINCA', 'TSAUXC')
# Register groups re-read through IOCTL_AVB_QUERY_HW_SNAPSHOT (bound adapter, read-only): a group is verifiable for an item
# only when EVERY register of it was read VALID before and after on every adapter the item may touch (Get-AvbHilVerifiableState);
# a changed register is always an observed difference.  TS_CONTROL = everything SET_HW_TIMESTAMPING (enable_packet_timestamping:
# TSYNCRXCTL/TSYNCTXCTL; TSAUXC is re-read above), SET_RX_TIMESTAMP (RXPBSIZE) and SET_QUEUE_TIMESTAMP (SRRCTL0..3; queue >= 4 is
# rejected) write.  TARGET_TIME is NOT a group: set_target_time also writes FREQOUT0 (I210), which the snapshot does not read.
$HilRegisterGroups = [ordered]@{ TS_CONTROL = @('TSYNCTXCTL', 'TSYNCRXCTL', 'RXPBSIZE', 'SRRCTL0', 'SRRCTL1', 'SRRCTL2', 'SRRCTL3') }
$HilPhcReads = 5            # paired PHC/QPC reads per adapter in a pre-/post-test PHC window (evaluator minimum: 3)
$HilPhcTolerance = @{}      # DID -> hardware-derived tolerance (Get-AvbHilPhcTolerances); empty = PHC never verified
$HilMutatedState = [ordered]@{
    # none declared yet: every real non-ReadOnly entry is treated as modifying unobserved state
}
$IsHardwareSuite     = ($Suite -like 'Hardware*')
$HilSettleSeconds    = 10
if ($IsSandbox -and $env:AVB_TEST_STATE_SETTLE_SECONDS -match '^\d+$') { $HilSettleSeconds = [int]$env:AVB_TEST_STATE_SETTLE_SECONDS }
$stateHalt           = ''      # non-empty: every later non-ReadOnly test is BLOCKED
$HilTargets          = @()     # exact-target DIDs ('0x125B') for Mutating:Target entries
$HilSupported        = @{}
$HilBaseline         = $null
$UnderGitHubActions  = ($env:GITHUB_ACTIONS -eq 'true')
$HilAuthorizedUnsafe = @()
if ($env:AVB_HIL_AUTHORIZE_UNSAFE) {
    $HilAuthorizedUnsafe = @($env:AVB_HIL_AUTHORIZE_UNSAFE -split '[;,]' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
}

function Format-AvbDid { param([int]$Value) return ('0x{0:X4}' -f $Value) }

# A number is the device id itself: an unquoted 0x125B on a command line arrives as the integer 4699 and must not be
# re-read as the hex text '4699' (2026-10-22 baseline/TX-probe runs targeted 0x4699/0x5427/0x5563).  Text is hex.
function ConvertTo-AvbDid {
    param([object]$Text)
    if ($Text -is [int] -or $Text -is [long] -or $Text -is [uint16] -or $Text -is [uint32]) {
        if ([long]$Text -ge 0 -and [long]$Text -le 0xFFFF) { return (Format-AvbDid ([int]$Text)) }
        throw "invalid PCI device id $Text (outside 0x0000..0xFFFF)"
    }
    if ([string]$Text -match '^\s*(0x)?([0-9A-Fa-f]{1,4})\s*$') { return (Format-AvbDid ([Convert]::ToInt32($Matches[2], 16))) }
    throw "invalid PCI device id '$Text'"
}

# Supported adapters = every non-commented 'case INTEL_DEV_*' of AvbGetIntelDeviceType(), resolved through
# include/intel_pci_ids.h.  Any parse problem throws: a silent empty set would make every adapter "unsupported".
function Get-AvbHilSupportedDids {
    $src = Join-Path $repoRoot 'src\avb_integration_fixed.c'
    $hdr = Join-Path $repoRoot 'include\intel_pci_ids.h'
    foreach ($f in $src, $hdr) { if (-not (Test-Path -LiteralPath $f)) { throw "SSOT file missing: $f" } }
    $m = [regex]::Match([System.IO.File]::ReadAllText($src), '(?s)\bintel_device_type_t\s+AvbGetIntelDeviceType\s*\([^)]*\)\s*\{(.*?)\r?\n\}')
    if (-not $m.Success) { throw "AvbGetIntelDeviceType() definition not found in $src" }
    $body  = [regex]::Replace($m.Groups[1].Value, '(?s)/\*.*?\*/', ' ')
    $body  = [regex]::Replace($body, '//[^\r\n]*', ' ')
    $names = @([regex]::Matches($body, '\bcase\s+(INTEL_DEV_\w+)\s*:') | ForEach-Object { $_.Groups[1].Value })
    if ($names.Count -eq 0) { throw "no 'case INTEL_DEV_*' label parsed from AvbGetIntelDeviceType()" }
    $defs = @{}
    foreach ($d in [regex]::Matches([System.IO.File]::ReadAllText($hdr), '(?m)^\s*#define\s+(INTEL_DEV_\w+)\s+0x([0-9A-Fa-f]{4})U?\b')) {
        $defs[$d.Groups[1].Value] = Format-AvbDid ([Convert]::ToInt32($d.Groups[2].Value, 16))
    }
    $out = @{}
    foreach ($n in $names) {
        if (-not $defs.ContainsKey($n)) { throw "$n is used in AvbGetIntelDeviceType() but not defined in $hdr" }
        $out[$defs[$n]] = $n
    }
    return $out
}

# AVB_HWREG_* id -> register name, from include/avb_ioctl.h (SSOT of the QUERY_HW_SNAPSHOT ids).  SRRCTL1/2 are the ids
# between SRRCTL0 and SRRCTL3 ("queues 0..3 = 6..9" in the header).  A missing header or id throws: the runner never
# guesses which register an entry describes.
function Get-AvbHwRegNames {
    $hdr = Join-Path $script:repoRoot 'include\avb_ioctl.h'
    if (-not (Test-Path -LiteralPath $hdr)) { throw "SSOT file missing: $hdr" }
    $out = @{}
    foreach ($d in [regex]::Matches([System.IO.File]::ReadAllText($hdr), '(?m)^\s*#define\s+AVB_HWREG_([A-Z0-9]+)\s+(\d+)u\b')) {
        if ($d.Groups[1].Value -eq 'COUNT') { continue }
        $out[[int]$d.Groups[2].Value] = $d.Groups[1].Value
    }
    $q0 = @($out.Keys | Where-Object { $out[$_] -eq 'SRRCTL0' }); $q3 = @($out.Keys | Where-Object { $out[$_] -eq 'SRRCTL3' })
    if ($q0.Count -ne 1 -or $q3.Count -ne 1 -or ($q3[0] - $q0[0]) -ne 3) { throw "AVB_HWREG_SRRCTL0/SRRCTL3 not parsed as a 4-queue range from $hdr" }
    $out[$q0[0] + 1] = 'SRRCTL1'; $out[$q0[0] + 2] = 'SRRCTL2'
    return $out
}

# Snapshot registers of one clock entry as an ordered name -> uint32 map (hardware: ordered dictionary; sandbox / session
# JSON: object properties).  Absent = nothing read.
function Get-AvbHilRegs {
    param($Entry)
    $m = [ordered]@{}
    if ($null -eq $Entry -or -not $Entry.PSObject.Properties['Registers'] -or $null -eq $Entry.Registers) { return $m }
    $r = $Entry.Registers
    if ($r -is [System.Collections.IDictionary]) { foreach ($k in $r.Keys) { $m[[string]$k] = [uint32]$r[$k] } }
    else { foreach ($p in $r.PSObject.Properties) { $m[$p.Name] = [uint32]$p.Value } }
    return $m
}

# State groups this item's restoration can be verified for: $HilVerifiableState plus every $HilRegisterGroups group whose
# registers were ALL read VALID in the baseline AND now on every adapter the item may touch (its target, else every
# readable adapter).  Fail closed: no clock state, a different adapter set or one missing register -> group not verifiable.
function Get-AvbHilVerifiableState {
    param($Base, $Cur, [string]$Target)
    $v = @($HilVerifiableState)
    if ($null -eq $Base -or $null -eq $Cur -or -not $Base.ClockOk -or -not $Cur.ClockOk) { return $v }
    $bl = @($Base.Clock | Where-Object { $_.Ok -and (-not $Target -or $_.Did -eq $Target) })
    $cl = @($Cur.Clock  | Where-Object { $_.Ok -and (-not $Target -or $_.Did -eq $Target) })
    if ($bl.Count -eq 0 -or $bl.Count -ne $cl.Count) { return $v }
    foreach ($g in $HilRegisterGroups.Keys) {
        $all = $true
        foreach ($e in @($bl) + @($cl)) {
            $r = Get-AvbHilRegs $e
            foreach ($n in $HilRegisterGroups[$g]) { if (-not $r.Contains($n)) { $all = $false } }
        }
        if ($all) { $v += $g }
    }
    return $v
}

function New-AvbHilAdapter {
    param([string]$InstanceId, [string]$PnpStatus, [int]$Problem, [string]$AdminStatus, [string]$MediaStatus,
          [string]$FilterBound, [string]$Name)
    $did = ''
    if ($InstanceId -match 'DEV_([0-9A-Fa-f]{4})') { $did = Format-AvbDid ([Convert]::ToInt32($Matches[1], 16)) }
    return [pscustomobject]@{ InstanceId = $InstanceId; Did = $did; PnpStatus = $PnpStatus; Problem = $Problem
                              AdminStatus = $AdminStatus; MediaStatus = $MediaStatus; FilterBound = $FilterBound; Name = $Name }
}

# Per-adapter TIMINCA/TSAUXC: ENUM_ADAPTERS -> OPEN_ADAPTER -> GET_CLOCK_CONFIG (read IOCTLs; OPEN_ADAPTER side effects:
# issue #343, documented at IOCTL_AVB_OPEN_ADAPTER in src/avb_integration_fixed.c).  -PhcReads N: N GET_CLOCK_CONFIG reads
# per adapter, each SYSTIM read (ops->get_systime) bracketed by QPC - the paired reads of a PHC window for
# tools/lib/PhcRestoration.ps1 (Samples: qpcBefore, qpcAfter, phcNs).  TIMINCA/TSAUXC are those of the first read.
# -PhcReadIntervalMs: spacing between the reads, so the window has a length over which a PHC rate and an extrapolated
# offset can be bounded (docs/testing/phc-restoration-evaluator.md, open measurement requirements 2-4).  Back-to-back
# reads spanned 0.45-1.4 ms on hardware (logs/hil-baseline/hil-baseline-evidence-20261022_214259.json): rate uncertainty
# >= 1.4e6 ppb without any mutation, so no restoration could ever be verified.  The reads stay on the adapter this
# handle opened (OPEN_ADAPTER binds FileObject->FsContext, src/device.c).
function Get-AvbHilClockState {
    param([int]$PhcReads = 1, [int]$PhcReadIntervalMs = 250)
    $res = @{ Ok = $false; Error = ''; Entries = @() }
    if (-not ([System.Management.Automation.PSTypeName]'AvbHilIoctl').Type) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class AvbHilIoctl {
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr CreateFile(string name, uint access, uint share, IntPtr sec, uint disp, uint flags, IntPtr tmpl);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool DeviceIoControl(IntPtr h, uint code, byte[] inBuf, uint inLen, byte[] outBuf, uint outLen, out uint ret, IntPtr ov);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr h);
}
'@
    }
    # Observer only: GET_ADAPTER_IDENTITY (NET_LUID per enumeration cursor) + OPEN_ADAPTER_BY_LUID, which binds a handle
    # without switching the driver's global context and without PTP initialization.  Never OPEN_ADAPTER: it runs
    # AvbEnsureDeviceReady on the adapter it opens (I210: clears TSAUXC bit 31; after a filter re-attach: full intel_init)
    # and makes it the global context - the observer would repair or rewrite the state it reports and change which
    # adapter an unbound test handle reaches (TEST-HILGATE-091).  No fallback: an identity failure is unobservable state.
    $IOCTL_IDENT = [uint32]0x00170110; $IOCTL_BYLUID = [uint32]0x00170114; $IOCTL_CLOCK = [uint32]0x001700B4   # fn 68 / 69 / 45
    $IOCTL_SNAP = [uint32]0x00170118   # fn 70 IOCTL_AVB_QUERY_HW_SNAPSHOT (TC-ABI-022)
    $regNames = Get-AvbHwRegNames
    $openDev = { [AvbHilIoctl]::CreateFile('\\.\IntelAvbFilter', [uint32]3221225472, [uint32]3, [IntPtr]::Zero, [uint32]3, [uint32]0x80, [IntPtr]::Zero) }
    $h = & $openDev
    if ($h -eq [IntPtr](-1)) {
        $res.Error = "open \\.\IntelAvbFilter failed (Win32 $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error()))"
        return $res
    }
    try {
        $ret = [uint32]0
        # AVB_ADAPTER_IDENTITY_REQUEST (48 bytes): version@0 index@4 net_luid@8 if_index@16 count@20 vendor@24 device@26 mac@32 status@40
        $idents = @(); $count = 1
        for ($i = 0; $i -lt $count; $i++) {
            $q = New-Object byte[] 48
            [System.Array]::Copy([System.BitConverter]::GetBytes([uint32]1), 0, $q, 0, 4)   # AVB_ADAPTER_IDENTITY_VERSION
            [System.Array]::Copy([System.BitConverter]::GetBytes([uint32]$i), 0, $q, 4, 4)
            if (-not [AvbHilIoctl]::DeviceIoControl($h, $IOCTL_IDENT, $q, 48, $q, 48, [ref]$ret, [IntPtr]::Zero)) {
                $res.Error = "GET_ADAPTER_IDENTITY($i) failed (Win32 $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error()))"
                return $res
            }
            $count = [System.BitConverter]::ToUInt32($q, 20)
            if ($count -eq 0) { break }
            $st = [System.BitConverter]::ToUInt32($q, 40)
            if ($st -ne 0) { $res.Error = ("GET_ADAPTER_IDENTITY({0}) status 0x{1:X8} (adapter count {2}; enumeration changed while observing)" -f $i, $st, $count); return $res }
            $idents += [pscustomobject]@{ Index = $i; LuidValue = [System.BitConverter]::ToUInt64($q, 8); IfIndex = [System.BitConverter]::ToUInt32($q, 16)
                                          Didv = [System.BitConverter]::ToUInt16($q, 26); Mac = (@($q[32..37] | ForEach-Object { '{0:X2}' -f $_ }) -join '-') }
        }
        $dupLuid = @($idents | Group-Object LuidValue | Where-Object { $_.Count -gt 1 })
        if ($dupLuid.Count -gt 0) { $res.Error = "GET_ADAPTER_IDENTITY: NET_LUID reported for more than one adapter ($(($dupLuid | ForEach-Object { '0x{0:X16}' -f [uint64]$_.Name }) -join ', '))"; return $res }
        $entries = @()
        foreach ($idn in $idents) {
            $i = $idn.Index; $didv = $idn.Didv; $did = Format-AvbDid $didv; $luid = ('0x{0:X16}' -f $idn.LuidValue)
            $hb = & $openDev
            if ($hb -eq [IntPtr](-1)) {
                $entries += [pscustomobject]@{ Index = $i; Did = $did; Luid = $luid; IfIndex = $idn.IfIndex; Mac = $idn.Mac; Ok = $false; Timinca = 0; Tsauxc = 0
                                               Error = "open \\.\IntelAvbFilter for LUID $luid failed (Win32 $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error()))" }
                continue
            }
            try {
            # AVB_OPEN_BY_LUID_REQUEST (24 bytes): version@0 net_luid@8 matches@16 status@20
            $o = New-Object byte[] 24
            [System.Array]::Copy([System.BitConverter]::GetBytes([uint32]1), 0, $o, 0, 4)   # AVB_OPEN_BY_LUID_VERSION
            [System.Array]::Copy([System.BitConverter]::GetBytes([uint64]$idn.LuidValue), 0, $o, 8, 8)
            if (-not [AvbHilIoctl]::DeviceIoControl($hb, $IOCTL_BYLUID, $o, 24, $o, 24, [ref]$ret, [IntPtr]::Zero)) {
                $entries += [pscustomobject]@{ Index = $i; Did = $did; Luid = $luid; IfIndex = $idn.IfIndex; Mac = $idn.Mac; Ok = $false; Timinca = 0; Tsauxc = 0
                                               Error = "OPEN_ADAPTER_BY_LUID($luid) failed (Win32 $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error()), matches $([System.BitConverter]::ToUInt32($o, 16)))" }
                continue
            }
            $samples = @(); $first = $null; $readErr = ''
            for ($r = 0; $r -lt [Math]::Max(1, $PhcReads); $r++) {
                if ($r -gt 0) { Start-Sleep -Milliseconds $PhcReadIntervalMs }
                $c = New-Object byte[] 24   # AVB_CLOCK_CONFIG: systim u64, timinca, tsauxc, clock_rate_mhz, status
                $q0 = [System.Diagnostics.Stopwatch]::GetTimestamp()
                $clockOk = [AvbHilIoctl]::DeviceIoControl($hb, $IOCTL_CLOCK, $c, 24, $c, 24, [ref]$ret, [IntPtr]::Zero)
                $q1 = [System.Diagnostics.Stopwatch]::GetTimestamp()
                if (-not $clockOk) { $readErr = "GET_CLOCK_CONFIG failed (Win32 $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error()))"; break }
                $status = [System.BitConverter]::ToUInt32($c, 20)
                if ($status -ne 0) { $readErr = ("GET_CLOCK_CONFIG status 0x{0:X8}" -f $status); break }
                if ($null -eq $first) { $first = $c }
                $samples += ,([ordered]@{ qpcBefore = [long]$q0; qpcAfter = [long]$q1; phcNs = [long][System.BitConverter]::ToUInt64($c, 0) })
            }
            if ($readErr) {
                $entries += [pscustomobject]@{ Index = $i; Did = $did; Luid = $luid; IfIndex = $idn.IfIndex; Mac = $idn.Mac; Ok = $false; Timinca = 0; Tsauxc = 0; Error = $readErr; Samples = @() }
                continue
            }
            # Register snapshot of the SAME bound adapter.  Only VALID entries of this adapter (device ID and NET_LUID
            # echoed) are kept; TIMINCA/TSAUXC come from GET_CLOCK_CONFIG above.  Any failure leaves Registers empty:
            # nothing built on a register can then be verified (fail closed), and RegistersError says why.
            $regs = [ordered]@{}; $regErr = ''
            $sn = New-Object byte[] 280   # AVB_HW_SNAPSHOT_REQUEST: 24-byte header + 16 x AVB_HWREG_ENTRY (id, offset, value, state)
            [System.Array]::Copy([System.BitConverter]::GetBytes([uint32]1), 0, $sn, 0, 4)   # AVB_HW_SNAPSHOT_VERSION
            if (-not [AvbHilIoctl]::DeviceIoControl($hb, $IOCTL_SNAP, $sn, 280, $sn, 280, [ref]$ret, [IntPtr]::Zero)) {
                $regErr = "QUERY_HW_SNAPSHOT failed (Win32 $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error()))"
            } elseif ([System.BitConverter]::ToUInt32($sn, 20) -ne 0) {
                $regErr = ("QUERY_HW_SNAPSHOT status 0x{0:X8}" -f [System.BitConverter]::ToUInt32($sn, 20))
            } elseif ([System.BitConverter]::ToUInt16($sn, 18) -ne $didv) {
                $regErr = ("QUERY_HW_SNAPSHOT reports DID 0x{0:X4}, bound DID {1}" -f [System.BitConverter]::ToUInt16($sn, 18), $did)
            } elseif ([System.BitConverter]::ToUInt64($sn, 8) -ne $idn.LuidValue) {
                $regErr = ("QUERY_HW_SNAPSHOT: snapshot net_luid 0x{0:X16} is not the bound LUID {1} (handle re-bound or adapter re-enumerated)" -f [System.BitConverter]::ToUInt64($sn, 8), $luid)
            } else {
                for ($k = 0; $k -lt [Math]::Min(16, [System.BitConverter]::ToUInt32($sn, 4)); $k++) {
                    $at = 24 + 16 * $k
                    $rid = [int][System.BitConverter]::ToUInt32($sn, $at)
                    if ([System.BitConverter]::ToUInt32($sn, $at + 12) -ne 1 -or -not $regNames.Contains($rid)) { continue }   # AVB_HWREG_STATE_VALID only
                    if ($regNames[$rid] -in @('TIMINCA', 'TSAUXC')) { continue }
                    $regs[$regNames[$rid]] = [System.BitConverter]::ToUInt32($sn, $at + 8)
                }
            }
            $entries += [pscustomobject]@{ Index = $i; Did = $did; Luid = $luid; IfIndex = $idn.IfIndex; Mac = $idn.Mac; Ok = $true; Error = ''
                                           Timinca = [System.BitConverter]::ToUInt32($first, 8); Tsauxc = [System.BitConverter]::ToUInt32($first, 12)
                                           Samples = $samples; Registers = $regs; RegistersError = $regErr }
            } finally {
                [void][AvbHilIoctl]::CloseHandle($hb)
            }
        }
        $res.Entries = $entries
        $res.Ok = $true
    } finally {
        [void][AvbHilIoctl]::CloseHandle($h)
    }
    return $res
}

function Get-AvbHilSandboxState {
    param([int]$PhcReads = 1)
    $st = @{ Ok = $true; Error = ''; Adapters = @(); ClockOk = $true; ClockError = ''; Clock = @() }
    $sf = $env:AVB_TEST_ADAPTER_STATE_FILE
    if (-not $sf -or -not (Test-Path -LiteralPath $sf)) {
        $st.Ok = $false; $st.ClockOk = $false; $st.Error = "sandbox state file not found: '$sf'"; $st.ClockError = $st.Error
        return $st
    }
    $j = Get-Content -LiteralPath $sf -Raw | ConvertFrom-Json
    $st.Adapters = @(foreach ($a in @($j.adapters)) {
        New-AvbHilAdapter ([string]$a.InstanceId) ([string]$a.PnpStatus) ([int]$a.Problem) ([string]$a.AdminStatus) ([string]$a.MediaStatus) ([string]$a.FilterBound) ([string]$a.Name)
    })
    # Optional PhcOffsetNs: the sandbox PHC runs at the QPC rate with that offset (a fake "sets the PHC" by changing it);
    # absent = no PHC samples, i.e. no PHC window (exactly like a hardware read that returned no SYSTIM).
    $f = [double][System.Diagnostics.Stopwatch]::Frequency
    $st.Clock = @(foreach ($c in @($j.clock)) {
        $samples = @()
        if ($c.PSObject.Properties['PhcOffsetNs']) {
            for ($r = 0; $r -lt [Math]::Max(1, $PhcReads); $r++) {
                if ($r -gt 0) { Start-Sleep -Milliseconds 2 }
                $q0 = [System.Diagnostics.Stopwatch]::GetTimestamp(); $q1 = $q0 + 10
                $samples += ,([ordered]@{ qpcBefore = [long]$q0; qpcAfter = [long]$q1; phcNs = [long]([double]($q0 + 5) * 1e9 / $f) + [long]$c.PhcOffsetNs })
            }
        }
        $sc = [pscustomobject]@{ Index = [int]$c.Index; Did = (ConvertTo-AvbDid ([string]$c.Did)); Ok = [bool]$c.Ok
                                 Timinca = [uint32]$c.Timinca; Tsauxc = [uint32]$c.Tsauxc; Error = ''; Samples = $samples
                                 Registers = (Get-AvbHilRegs $c); RegistersError = $(if ($c.PSObject.Properties['Registers']) { '' } else { 'sandbox: no snapshot registers' }) }
        # Optional identity (as the hardware observer reports it): Luid, InstanceId.  Absent = enumeration position only.
        foreach ($f in 'Luid', 'InstanceId') { if ($c.PSObject.Properties[$f]) { $sc | Add-Member -NotePropertyName $f -NotePropertyValue ([string]$c.$f) } }
        $sc
    })
    return $st
}

function Get-AvbHilState {
    param([int]$PhcReads = 1)
    if ($script:IsSandbox) { return (Get-AvbHilSandboxState -PhcReads $PhcReads) }
    $st = @{ Ok = $true; Error = ''; Adapters = @(); ClockOk = $false; ClockError = ''; Clock = @() }
    $pnp = @()
    try {
        $pnp = @(Get-PnpDevice -Class Net -PresentOnly -ErrorAction Stop | Where-Object { $_.InstanceId -match '^PCI\\VEN_8086&DEV_[0-9A-Fa-f]{4}' })
    } catch {
        $st.Ok = $false; $st.Error = "Get-PnpDevice failed: $($_.Exception.Message)"
    }
    $na = @{}
    foreach ($n in @(Get-NetAdapter -IncludeHidden -ErrorAction SilentlyContinue)) {
        if ($n.PnPDeviceID) { $na[([string]$n.PnPDeviceID).ToUpperInvariant()] = $n }
    }
    $bind = @{}
    foreach ($b in @(Get-NetAdapterBinding -ComponentID MS_IntelAvbFilter -IncludeHidden -ErrorAction SilentlyContinue)) {
        $bind[[string]$b.Name] = [string]$b.Enabled
    }
    $list = @()
    foreach ($d in $pnp) {
        $n = $na[([string]$d.InstanceId).ToUpperInvariant()]
        $problem = -1   # unknown = unhealthy, never silently 0
        if ($null -ne $d.ConfigManagerErrorCode) { $problem = [int]$d.ConfigManagerErrorCode }
        $admin = 'Unknown'; $media = 'Unknown'; $fb = 'Unknown'; $name = ''
        if ($null -ne $n) {
            $admin = [string]$n.AdminStatus; $media = [string]$n.Status; $name = [string]$n.Name
            if ($bind.ContainsKey($name)) { $fb = $bind[$name] }
        }
        $list += New-AvbHilAdapter ([string]$d.InstanceId) ([string]$d.Status) $problem $admin $media $fb $name
    }
    $st.Adapters = $list
    $ck = Get-AvbHilClockState -PhcReads $PhcReads
    # Physical identity: the driver's base-miniport NET_LUID mapped to exactly one Intel PnP instance (Get-NetAdapter
    # NetLuid).  No or several matches -> no InstanceId -> IdentityVerified stays false (never guessed from the DID).
    $byLuid = @{}
    foreach ($n in @($na.Values)) {
        $lk = '0x{0:X16}' -f [uint64]$n.NetLuid
        if (-not $byLuid.ContainsKey($lk)) { $byLuid[$lk] = @() }
        $byLuid[$lk] += [string]$n.PnPDeviceID
    }
    foreach ($e in @($ck.Entries)) {
        $inst = ''
        if ($e.Luid -and $byLuid.ContainsKey($e.Luid) -and @($byLuid[$e.Luid]).Count -eq 1) { $inst = @($byLuid[$e.Luid])[0] }
        $e | Add-Member -NotePropertyName InstanceId -NotePropertyValue $inst -Force
    }
    $st.ClockOk = $ck.Ok; $st.ClockError = $ck.Error; $st.Clock = $ck.Entries
    return $st
}

# Returns @{ Diffs = enforced differences (text, unchanged semantics); Warn = observed-only differences;
#           Items = the same findings structured: Kind diff|unobservable, Key (incident key = adapter|component|expected|observed),
#           Adapter, Component, Expected, Observed, IdentityVerified, Text }.
# Adapter identity: PnP instance id for adapter state; for clock registers the base-miniport NET_LUID the observer read
# with IOCTL_AVB_GET_ADAPTER_IDENTITY ('luid:<NET_LUID>'), verified when it maps to exactly one PnP instance.  An entry
# without a LUID (sandbox fakes) keeps 'drv:<DID>#<driver index>' - an enumeration position, NOT a physical identity
# (IdentityVerified = $false; global safety semantics apply).  Base and current in different modes never pair (fail closed).
function Get-AvbHilEntryField {
    param($Entry, [string]$Name)
    if ($Entry -is [System.Collections.IDictionary]) { if ($Entry.Contains($Name)) { return $Entry[$Name] }; return $null }
    if ($Entry.PSObject.Properties[$Name]) { return $Entry.$Name }
    return $null
}
function Get-AvbHilAdapterKey {
    param($Entry)
    $l = Get-AvbHilEntryField $Entry 'Luid'
    if ($l) { return "luid:$l" }
    return ("drv:{0}#{1}" -f (Get-AvbHilEntryField $Entry 'Did'), (Get-AvbHilEntryField $Entry 'Index'))
}
function Get-AvbHilAdapterLabel {
    param($Entry)
    $l = Get-AvbHilEntryField $Entry 'Luid'; $inst = Get-AvbHilEntryField $Entry 'InstanceId'
    if ($l) { return ("DID {0} (LUID {1}{2})" -f (Get-AvbHilEntryField $Entry 'Did'), $l, $(if ($inst) { ", $inst" } else { ', no unique PnP instance' })) }
    return ("DID {0} (driver index {1})" -f (Get-AvbHilEntryField $Entry 'Did'), (Get-AvbHilEntryField $Entry 'Index'))
}
function Test-AvbHilAdapterIdentityVerified {
    param($Entry)
    return [bool]((Get-AvbHilEntryField $Entry 'Luid') -and (Get-AvbHilEntryField $Entry 'InstanceId'))
}
function Compare-AvbHilState {
    param($Base, $Cur)
    $diffs = @(); $warn = @(); $items = @()
    $add = { param($Kind, $Adapter, $Comp, $Exp, $Obs, $Text, $Verified)
             $script:__cmpItems += ,([pscustomobject]@{ Kind = $Kind; Key = ("{0}|{1}|{2}|{3}" -f $Adapter, $Comp, $Exp, $Obs); Adapter = $Adapter; Component = $Comp
                                                        Expected = $Exp; Observed = $Obs; IdentityVerified = $Verified; Text = $Text }) }
    $script:__cmpItems = @()
    if (-not $Cur.Ok) {
        $diffs += "adapter state not observable: $($Cur.Error)"
        & $add 'unobservable' 'all-adapters' 'adapter-state' 'observable' 'unobservable' $diffs[-1] $false
    } else {
        $bMap = @{}; foreach ($a in $Base.Adapters) { $bMap[$a.InstanceId] = $a }
        $cMap = @{}; foreach ($a in $Cur.Adapters)  { $cMap[$a.InstanceId] = $a }
        foreach ($k in @($bMap.Keys)) {
            if (-not $cMap.ContainsKey($k)) { $diffs += "${k}: adapter no longer present"; & $add 'diff' "pnp:$k" 'presence' 'present' 'absent' $diffs[-1] $true; continue }
            foreach ($f in 'PnpStatus', 'Problem', 'AdminStatus', 'FilterBound') {
                if ([string]$bMap[$k].$f -ne [string]$cMap[$k].$f) { $diffs += ("{0}: {1} {2} -> {3}" -f $k, $f, $bMap[$k].$f, $cMap[$k].$f); & $add 'diff' "pnp:$k" $f ([string]$bMap[$k].$f) ([string]$cMap[$k].$f) $diffs[-1] $true }
            }
            # Enforced (P0 2026-10-21): a link that does not return within the settle window is a state change.
            if ([string]$bMap[$k].MediaStatus -ne [string]$cMap[$k].MediaStatus) {
                $diffs += ("{0}: link {1} -> {2}" -f $k, $bMap[$k].MediaStatus, $cMap[$k].MediaStatus)
                & $add 'diff' "pnp:$k" 'MediaStatus' ([string]$bMap[$k].MediaStatus) ([string]$cMap[$k].MediaStatus) $diffs[-1] $true
            }
        }
        foreach ($k in @($cMap.Keys)) { if (-not $bMap.ContainsKey($k)) { $diffs += "${k}: adapter appeared"; & $add 'diff' "pnp:$k" 'presence' 'absent' 'present' $diffs[-1] $true } }
    }
    if ($Base.ClockOk) {
        if (-not $Cur.ClockOk) {
            $diffs += "PHC clock state not observable: $($Cur.ClockError)"
            & $add 'unobservable' 'all-adapters' 'clock-state' 'observable' 'unobservable' $diffs[-1] $false
        } else {
            $bc = @{}; foreach ($c in $Base.Clock) { $bc[(Get-AvbHilAdapterKey $c)] = $c }
            $cc = @{}; foreach ($c in $Cur.Clock)  { $cc[(Get-AvbHilAdapterKey $c)] = $c }
            foreach ($k in @($bc.Keys)) {
                $b = $bc[$k]; $lb = Get-AvbHilAdapterLabel $b
                if (-not $cc.ContainsKey($k)) { $diffs += "${lb}: no longer enumerated by the driver"; & $add 'diff' $k 'enumeration' 'enumerated' 'absent' $diffs[-1] (Test-AvbHilAdapterIdentityVerified $b); continue }
                $c = $cc[$k]; $lc = Get-AvbHilAdapterLabel $c; $vf = (Test-AvbHilAdapterIdentityVerified $b) -and (Test-AvbHilAdapterIdentityVerified $c)
                if ($b.Ok -and -not $c.Ok) { $diffs += ("{0}: clock config no longer readable ({1})" -f $lc, $c.Error); & $add 'unobservable' $k 'clock-config' 'readable' 'unreadable' $diffs[-1] $vf; continue }
                if ($b.Ok -and $c.Ok -and $b.Timinca -ne $c.Timinca) {
                    $diffs += ("{0}: TIMINCA 0x{1:X8} -> 0x{2:X8}" -f $lc, $b.Timinca, $c.Timinca)
                    & $add 'diff' $k 'TIMINCA' ('0x{0:X8}' -f [uint32]$b.Timinca) ('0x{0:X8}' -f [uint32]$c.Timinca) $diffs[-1] $vf
                }
                if ($b.Ok -and $c.Ok -and $b.Tsauxc -ne $c.Tsauxc) {
                    $diffs += ("{0}: TSAUXC 0x{1:X8} -> 0x{2:X8}" -f $lc, $b.Tsauxc, $c.Tsauxc)
                    & $add 'diff' $k 'TSAUXC' ('0x{0:X8}' -f [uint32]$b.Tsauxc) ('0x{0:X8}' -f [uint32]$c.Tsauxc) $diffs[-1] $vf
                }
                # Snapshot registers read VALID both times: a change is an observed difference.  A register read only once is
                # no difference - it only keeps its group unverifiable (Get-AvbHilVerifiableState).
                if ($b.Ok -and $c.Ok) {
                    $bR = Get-AvbHilRegs $b; $cR = Get-AvbHilRegs $c
                    foreach ($rn in @($bR.Keys)) {
                        if (-not $cR.Contains($rn) -or $bR[$rn] -eq $cR[$rn]) { continue }
                        $diffs += ("{0}: {1} 0x{2:X8} -> 0x{3:X8}" -f $lc, $rn, $bR[$rn], $cR[$rn])
                        & $add 'diff' $k $rn ('0x{0:X8}' -f $bR[$rn]) ('0x{0:X8}' -f $cR[$rn]) $diffs[-1] $vf
                    }
                }
            }
            foreach ($k in @($cc.Keys)) {
                if (-not $bc.ContainsKey($k)) { $diffs += ("{0}: newly enumerated by the driver" -f (Get-AvbHilAdapterLabel $cc[$k])); & $add 'diff' $k 'enumeration' 'absent' 'enumerated' $diffs[-1] (Test-AvbHilAdapterIdentityVerified $cc[$k]) }
            }
        }
    }
    $items = @($script:__cmpItems); $script:__cmpItems = @()
    return @{ Diffs = $diffs; Warn = $warn; Items = $items }
}

# Incident resolution (pure).  $Open: ordered map incident key -> @{ IncidentId; Key; observations }.  An item whose key
# matches an OPEN incident is an additional observation of it ('confirmed'); otherwise a new incident ('opened').
# Closed (verified-restored) incidents are never in $Open, so a recurrence opens a new incident.
function Resolve-AvbHilIncident {
    param($Open, $Item, [string]$Check, [string]$Record, [string]$IncidentId = '')
    $obs = [ordered]@{ check = $Check; record = $Record; utc = (Get-Date).ToUniversalTime().ToString('o'); observed = $Item.Observed }
    if ($Open.Contains($Item.Key)) {
        $inc = $Open[$Item.Key]; $inc.observations = @(@($inc.observations) + @($obs))
        return @{ IncidentId = $inc.IncidentId; Role = 'confirmed'; Key = $Item.Key }
    }
    $id = if ($IncidentId) { $IncidentId } else { [guid]::NewGuid().ToString() }
    $Open[$Item.Key] = @{ IncidentId = $id; Key = $Item.Key; Adapter = $Item.Adapter; Component = $Item.Component; Expected = $Item.Expected
                          Observed = $Item.Observed; IdentityVerified = $Item.IdentityVerified; observations = @($obs) }
    return @{ IncidentId = $id; Role = 'opened'; Key = $Item.Key }
}

# Records a failure-class verdict that the runner itself determined (counted, shown, and written as a log
# the JUnit converter turns into a <failure>).
# ---------------------------------------------------------------------------------------------------------
# Machine-readable results (-ResultsJson).  Every result row keeps the console columns Name/Status/Exit and adds
# what a consumer needs without parsing console text: entry, artifact + SHA256, exact arguments, safety class,
# target DID, whether the process was started, the observed exit code ($null = none observed, never success),
# log path, reason and the per-testcase verdict lines (testcase ID + adapter).  Work-item context comes from
# $script:AvbItem (set by the dispatch loop; $null outside it).
# ---------------------------------------------------------------------------------------------------------
$script:AvbItem = $null

# Runner status -> results verdict.  Distinct categories are kept distinct; an unknown status throws (a silent
# default would turn a new status into a pass).
function ConvertTo-AvbRunnerVerdict {
    param([string]$Status)
    switch -Exact ($Status) {
        'PASSED'                      { return 'PASSED' }
        'FAILED'                      { return 'FAILED' }
        'FAILED(SKIP_ONLY)'           { return 'FAILED' }
        'FAILED(TIMEOUT)'             { return 'FAILED(TIMEOUT)' }
        'FAILED(INCONCLUSIVE)'        { return 'INCONCLUSIVE' }
        'INCOMPLETE(NOT_IMPLEMENTED)' { return 'INCOMPLETE' }
        'FAILED(INFRA)'               { return 'INFRA_FAILURE' }
        'FAILED(BUILD)'               { return 'INFRA_FAILURE' }
        'INFRA_FAILURE'               { return 'INFRA_FAILURE' }
        'CLEANUP_FAILED'              { return 'CLEANUP_FAILED' }
        'INCONCLUSIVE_RESTORATION'    { return 'INCONCLUSIVE_RESTORATION' }
        'FAILED_SETUP'                { return 'FAILED_SETUP' }
        'BLOCKED'                     { return 'BLOCKED(SAFETY)' }
        'BLOCKED(SAFETY)'             { return 'BLOCKED(SAFETY)' }
        'BLOCKED(PROVENANCE)'         { return 'BLOCKED(PROVENANCE)' }
        'BLOCKED(TESTCASE)'           { return 'BLOCKED(TESTCASE)' }   # executed; no FAIL; BLOCKED testcases (exit 3, confirmed by the lines)
        'SKIPPED'                     { return 'SKIPPED' }
        'SKIPPED(INFRA)'              { return 'SKIPPED' }
        'COVERAGE-GAP'                { return 'COVERAGE-GAP' }
    }
    throw "Unknown runner status '$Status' - map it in ConvertTo-AvbRunnerVerdict (tools/test/Run-Tests-CI.ps1)"
}

# -Check: a runner check attributed to the current item (post-test state, driver health); not a test execution.
function Add-AvbResult {
    param([string]$Name, [string]$Status, $Exit, [string]$Reason = '', [string]$LogPath = '', [string[]]$Lines = @(), [switch]$Check, [object[]]$Incidents = @())
    $script:results += [PSCustomObject]@{ Name = $Name; Status = $Status; Exit = $Exit }
    $it = $script:AvbItem
    $executed = ($null -ne $it -and $it.Executed -and -not $Check)
    $r = [ordered]@{
        name = $Name; entry = $(if ($it) { $it.Entry } else { $Name }); suite = $Suite; configuration = $Configuration
        verdict = (ConvertTo-AvbRunnerVerdict $Status); status = $Status; executed = $executed; check = [bool]$Check
        exitCode = $(if ($executed) { $it.ProcessExit } else { $null })
        artifact = $(if ($it) { $it.Artifact } else { $null }); artifactSha256 = $(if ($it) { $it.Sha256 } else { $null })
        arguments = $(if ($it) { $it.Arguments } else { '' }); safetyClass = $(if ($it) { $it.Class } else { '' })
        targetDid = $(if ($it -and $it.Target) { $it.Target } else { $null })
        reason = $Reason; logPath = $(if ($LogPath) { $LogPath } elseif ($executed) { $it.LogPath } else { $null })
        startedUtc = $(if ($executed) { $it.StartedUtc } else { $null }); durationSeconds = $(if ($executed) { $it.DurationSeconds } else { $null })
        testcases = @()
        incidents = @($Incidents | ForEach-Object { [ordered]@{ incidentId = $_.IncidentId; role = $_.Role; key = $_.Key } })
    }
    $tcLines = if ($Lines.Count -gt 0) { $Lines } elseif ($executed -and $null -ne $it.Output) { @($it.Output -split "`r?`n") } else { @() }
    $r.testcases = Get-AvbTestcaseVerdicts -Lines $tcLines   # already an array (unary-comma return); @() would nest it
    if ($executed) {
        # Unresolved cleanup of earlier tests of this run that this test followed: visible, never silently inherited; the
        # test establishes its own setup (Q 2026-10-28).
        $r['precedingCleanup'] = @($script:AvbCleanup | Where-Object { $_.outcome -in @('FAILED', 'NOT_VERIFIED') } |
                                   ForEach-Object { [ordered]@{ record = $_.record; outcome = $_.outcome; reason = $_.reason } })
        $it.Record = $r   # the post-test check attaches this test's cleanup result
    }
    $script:AvbResultRecords += ,$r
}

# Cleanup is mandatory lifecycle work, reported independently of functional verdicts (Q 2026-10-28, binding): it never
# changes a testcase verdict, never fails the functional suite result and never blocks a later test.  Outcome per executed
# item: SUCCESS (restoration verified by independent re-read), FAILED (a difference was observed), NOT_VERIFIED (what may
# have changed is not observable - never SUCCESS, never FAILED without evidence), NOT_REQUIRED (nothing to restore).
# Attempted: the runner never restores anything; cleanup belongs to the test, which the runner does not observe.
# -NoItem: a suite-level (session) check, attributed to no test.  -Incidents: session-record incident references.
function Add-AvbCleanupResult {
    param([string]$Display, [string]$Class, [string]$Required, [string]$Outcome, [string]$Reason, [string[]]$Observations = @(),
          [string]$Testcase = 'TC-INFRA-STATE-002', [string]$Subject = '', [switch]$NoItem, [object[]]$Incidents = @())
    $it = if ($NoItem) { $null } else { $script:AvbItem }
    $c = [ordered]@{
        record = $Display; entry = $(if ($it) { $it.Entry } else { $Display }); targetDid = $(if ($it -and $it.Target) { $it.Target } else { $null })
        safetyClass = $Class; testcase = $Testcase; required = $Required; attempted = 'UNKNOWN'; attemptedBy = 'the test (not observed by the runner)'
        outcome = $Outcome; adapters = @($(if ($it -and $it.Target) { $it.Target } elseif ($script:HilBaseline -and $script:HilBaseline.Clock) { @($script:HilBaseline.Clock | ForEach-Object { $_.Did }) }) | Where-Object { $_ })
        observations = @($Observations); reason = $Reason; evidence = @(@($(if ($it) { $it.LogPath }), $script:ResultsJsonPath) | Where-Object { $_ })
        incidents = @($Incidents | ForEach-Object { [ordered]@{ incidentId = $_.IncidentId; role = $_.Role; key = $_.Key } })
    }
    $script:AvbCleanup += ,$c
    if ($it -and $it.Record) { $it.Record['cleanup'] = $c }
    $color = if ($Outcome -in @('FAILED', 'NOT_VERIFIED')) { 'Yellow' } else { 'DarkGray' }
    Write-Host ("  [CLEANUP:{0}] {1}: {2} ({3}): {4}{5} --- reported separately; the functional verdict is unchanged and later tests are not blocked" -f $Outcome, $Testcase,
                $(if ($Subject) { $Subject } else { "after $Display" }), $Class, $Reason, (Format-HilIncidentRefs $Incidents)) -ForegroundColor $color
}

# Atomic write (temp file + rename) of the results document; returns the path.
function Write-AvbResultsJson {
    param([int]$ExitCode, [string]$Overall)
    $records = @($script:AvbResultRecords)
    $counts = [ordered]@{}
    foreach ($v in 'PASSED', 'FAILED', 'FAILED(TIMEOUT)', 'BLOCKED(PROVENANCE)', 'BLOCKED(SAFETY)', 'BLOCKED(TESTCASE)', 'SKIPPED', 'COVERAGE-GAP',
                   'INCOMPLETE', 'INCONCLUSIVE', 'INFRA_FAILURE', 'CLEANUP_FAILED', 'INCONCLUSIVE_RESTORATION', 'FAILED_SETUP') {
        $counts[$v] = @($records | Where-Object { $_.verdict -eq $v }).Count
    }
    # One persistent discrepancy = one incident, whatever number of independent checks observed it (Decision B).  Since
    # Q 2026-10-28 the observations are cleanup results (Add-AvbCleanupResult), never CLEANUP_FAILED result rows.
    $cleanRecs = @($script:AvbCleanup | Where-Object { $_.outcome -eq 'FAILED' })
    $incIds = @($cleanRecs | ForEach-Object { @($_.incidents) } | Where-Object { $_ } | ForEach-Object { $_.incidentId } | Sort-Object -Unique)
    $cleanupIncidents = @(foreach ($id in $incIds) {
        $obsRecs = @($cleanRecs | Where-Object { @($_.incidents | Where-Object { $_.incidentId -eq $id }).Count -gt 0 })
        [ordered]@{ incidentId = $id; key = @(@($obsRecs[0].incidents) | Where-Object { $_.incidentId -eq $id })[0].key
                    observations = @($obsRecs | ForEach-Object { $r = $_; [ordered]@{ record = $r.record; role = @(@($r.incidents) | Where-Object { $_.incidentId -eq $id })[0].role } }) } })
    $src = $script:SourceIdentity
    $evidence = if ($script:UnderGitHubActions -and -not $script:IsSandbox) { 'ci' }
                elseif ($script:IsSandbox) { 'sandbox' }
                elseif ($src -and $src.Mode -eq 'local' -and $src.Dirty) { 'local-pre-commit' }
                else { 'local' }
    $adapters = @()
    if ($script:HilBaseline -and $script:HilBaseline.Adapters) {
        $adapters = @($script:HilBaseline.Adapters | ForEach-Object { [ordered]@{ instanceId = $_.InstanceId; did = $_.Did; name = $_.Name; filterBound = $_.FilterBound } })
    }
    $doc = [ordered]@{
        schema = 'avb-runner-results/1'; producer = 'tools/test/Run-Tests-CI.ps1'
        suite = $Suite; configuration = $Configuration; evidenceClass = $evidence; host = $env:COMPUTERNAME
        startedUtc = $script:AvbRunStartedUtc; finishedUtc = (Get-Date).ToUniversalTime().ToString('o')
        github = $(if ($script:UnderGitHubActions) { [ordered]@{ runId = $env:GITHUB_RUN_ID; runAttempt = $env:GITHUB_RUN_ATTEMPT; sha = $env:GITHUB_SHA
                                                                 expectedRevision = $env:AVB_HIL_EXPECTED_REVISION; workflow = $env:GITHUB_WORKFLOW } } else { $null })
        source = $(if ($src) { [ordered]@{ ok = $src.Ok; revision = $src.Revision; dirty = $src.Dirty; mode = $src.Mode; digest = $src.Digest; error = $src.Error } } else { $null })
        buildManifest = $(if ($script:BuildManifest) { [ordered]@{ ok = $script:BuildManifest.Ok; path = $script:BuildManifest.Path; error = $script:BuildManifest.Error } } else { $null })
        driverIdentity = [ordered]@{ source = 'environment (HIL_DRIVER_*), not verified by this runner'; version = $env:HIL_DRIVER_VERSION; sha256 = $env:HIL_DRIVER_SHA256 }
        adapters = $adapters; stateHalt = $script:stateHalt
        machineSafety = $(if ($script:HilSafetyState) { [ordered]@{ path = $script:HilSafetyPath; stateAtStart = $script:HilSafetyState.State; why = $script:HilSafetyState.Why } } else { $null })
        executedCount = @($records | Where-Object { $_.executed }).Count; notExecutedCount = @($records | Where-Object { -not $_.executed -and -not $_.check }).Count
        counts = $counts; cleanupObservations = $cleanRecs.Count; cleanupIncidents = $cleanupIncidents
        # Independent of counts/overall (Q 2026-10-28): one cleanup result per executed Hardware* item.
        cleanupCounts = $(& { $cc = [ordered]@{}; foreach ($o in 'SUCCESS', 'FAILED', 'NOT_VERIFIED', 'NOT_ATTEMPTED', 'NOT_REQUIRED') { $cc[$o] = @($script:AvbCleanup | Where-Object { $_.outcome -eq $o }).Count }; $cc })
        cleanup = @($script:AvbCleanup)
        overall = $Overall; exitCode = $ExitCode; records = $records
    }
    $dir = Split-Path -Parent $script:ResultsJsonPath
    if ($dir -and -not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $tmp = "$($script:ResultsJsonPath).tmp-$PID"
    ($doc | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $tmp -Encoding UTF8
    Move-Item -LiteralPath $tmp -Destination $script:ResultsJsonPath -Force
    return $script:ResultsJsonPath
}

# Final exit of a completed run: results document first, then the exit code it records.
function Complete-AvbRun {
    param([int]$Code, [string]$Overall)
    if ($script:RunMarkerPath) { Remove-Item -LiteralPath $script:RunMarkerPath -Force -ErrorAction SilentlyContinue }
    if ($script:HilLockAcquired) {
        $rel = Exit-AvbHilLock -Owner $script:HilLockAcquired -Path $script:HilLockPath
        if ($rel.Ok) { Write-Note "Machine lock released" } else { Write-Fail "machine lock NOT released: $($rel.Why)" }
    }
    try {
        $p = Write-AvbResultsJson -ExitCode $Code -Overall $Overall
        Write-Note "Results JSON: $p"
        if ($ExportGitHubEnv) { "AVB_RESULTS_JSON=$p" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8 }
    } catch {
        Write-Fail "results JSON could not be written: $($_.Exception.Message) --- run reported as failed"
        exit 1
    }
    exit $Code
}

function Add-HilFailureRecord {
    param([string]$Display, [string]$Status, [string]$Stem, [string]$Line, [switch]$Check, [object[]]$Incidents = @())
    $script:failedTests++
    $script:totalTests++
    Write-Host "  $Line" -ForegroundColor Red
    $ts = Get-Date -Format 'yyyyMMdd_HHmmss'
    $p = Join-Path $script:logsDir ("{0}.exe_{1}.log" -f $Stem, $ts)
    $i = 2
    while (Test-Path -LiteralPath $p) { $p = Join-Path $script:logsDir ("{0}~{1}.exe_{2}.log" -f $Stem, $i, $ts); $i++ }
    $Line | Set-Content -LiteralPath $p -Encoding utf8
    Add-AvbResult -Name $Display -Status $Status -Exit 'N/A' -Reason $Line -LogPath $p -Lines @($Line) -Check:$Check -Incidents $Incidents
}

# Halt carried over from earlier suites of the same HIL job: '' or the reason.  Same stop rule as LocalSuites
# (Get-AvbStopReason): FAILED(TIMEOUT) / INFRA_FAILURE / CLEANUP_FAILED record, a stateHalt, plus an unreadable results
# document or a run-in-progress.json marker (that suite's runner died before completing: state unverified).
# Only this run's own results document is excluded - never a directory: LocalSuites' -PriorResultsDir lies inside the
# runner's logs directory (TEST-HILGATE-069).  A run-in-progress.json found here is never this run's own: it is written
# after this check, so it belongs to an earlier suite whose runner died.
function Get-AvbPriorHalt {
    param([string]$Dir, [string]$OwnResultsPath = '')
    if (-not (Test-Path -LiteralPath $Dir)) { return '' }
    $own = if ($OwnResultsPath) { [System.IO.Path]::GetFullPath($OwnResultsPath) } else { $null }
    $why = @()
    # Name filter in Where-Object: Windows PowerShell 5.1 ignores -Include together with -LiteralPath, which made every
    # file in the directory (LocalSuites' console-*.log transcripts, its report, repro-*.json) a "results document".
    foreach ($f in @(Get-ChildItem -LiteralPath $Dir -Recurse -File -ErrorAction Stop | Where-Object { $_.Name -like 'results-*.json' -or $_.Name -eq 'run-in-progress.json' })) {
        if ($own -and [string]::Equals($f.FullName, $own, [System.StringComparison]::OrdinalIgnoreCase)) { continue }
        if ($f.Name -eq 'run-in-progress.json') { $why += "$($f.Directory.Name) did not complete (runner process ended without results; hardware state unverified)"; continue }
        try { $d = Get-Content -LiteralPath $f.FullName -Raw | ConvertFrom-Json } catch { $why += "unreadable results document $($f.Name)"; continue }
        if ($d.stateHalt) { $why += "$($d.suite): state-integrity halt ($($d.stateHalt))" }
        foreach ($r in @($d.records)) {
            # D2, revised (Q 2026-10-28): a timeout whose process tree was terminated (TERMINATED_STATE_VERIFIED / TERMINATED,
            # any class) stays FAILED in its own suite but is no halt; without a disposition (older runner, termination not
            # evidenced) or with HALT (the tree did not terminate) it is.
            if ($r.verdict -eq 'FAILED(TIMEOUT)' -and $r.PSObject.Properties['timeoutDisposition'] -and
                $r.timeoutDisposition -in @('TERMINATED_STATE_VERIFIED', 'TERMINATED')) { continue }
            # CLEANUP_FAILED / INCONCLUSIVE_RESTORATION rows (a test's own verdict, or an older runner's cleanup row) are
            # cleanup observations, not loss of control: they never carry over (Q 2026-10-28).
            if ($r.verdict -in @('FAILED(TIMEOUT)', 'INFRA_FAILURE')) { $why += "$($d.suite): $($r.name)=$($r.verdict)" }
        }
    }
    return ($why -join '; ')
}

# Pure overall decision.  A required test not executed (coverage gap) or a testcase INCOMPLETE (SKIP_NOT_IMPLEMENTED)
# without any failure is INCOMPLETE, never PASSED; failures stay FAILED.
function Get-AvbRunOverall {
    param([bool]$IsHardwareSuite, [int]$Passed, [int]$Failed, [int]$Incomplete, [int]$CoverageGaps)
    if ($Failed -gt 0) { return @{ Overall = 'FAILED'; Code = 1 } }
    if ($Incomplete -gt 0 -or $CoverageGaps -gt 0) { return @{ Overall = 'INCOMPLETE'; Code = 1 } }
    if ($Passed -eq 0) {
        if ($IsHardwareSuite) { return @{ Overall = 'NOT_VERIFIED'; Code = 1 } }   # skipped-only is not hardware verification
        return @{ Overall = 'NO_TESTS'; Code = 0 }
    }
    return @{ Overall = 'PASSED'; Code = 0 }
}

# Correlates every 'diff' item of a check with the cleanup incidents of the SESSION record (Add-AvbHilSessionIncident; Q
# 2026-10-28 option b) - never the machine safety record, never a gate.  Each observation keeps time, check, record,
# suite/configuration, the observed value and the evidence; a new incident also keeps its origin (test, cleanup outcome).
# Without a usable session record the correlation is kept in memory for this run only (said so).  Returns the references.
function Register-HilIncidents {
    param([object[]]$Items, [string]$Check, [string]$Record, [string]$Outcome = 'FAILED')
    $refs = @()
    $evidence = @(@($(if ($script:AvbItem -and $Check -eq 'per-test') { $script:AvbItem.LogPath }), $script:ResultsJsonPath) | Where-Object { $_ })
    foreach ($it in @($Items | Where-Object { $_.Kind -eq 'diff' })) {
        $obs = [ordered]@{ utc = (Get-Date).ToUniversalTime().ToString('o'); check = $Check; record = $Record; suite = $Suite; configuration = $Configuration
                           observed = $it.Observed; text = $it.Text; evidence = $evidence }
        if ($script:HilSession) {
            $origin = [ordered]@{ record = $Record; entry = $(if ($script:AvbItem -and $Check -eq 'per-test') { $script:AvbItem.Entry }); check = $Check
                                  suite = $Suite; configuration = $Configuration; cleanupOutcome = $Outcome; evidence = $evidence }
            $p = Add-AvbHilSessionIncident -Path $script:HilSessionPath -Item $it -Observation $obs -Origin $origin
            if ($p.Ok) { $refs += ,@{ IncidentId = $p.IncidentId; Role = $p.Role; Key = $it.Key }; continue }
            Write-Host "  [CLEANUP] incident for '$($it.Text)' not persisted in the session record: $($p.Why) --- kept for this run only" -ForegroundColor Yellow
        }
        $refs += ,(Resolve-AvbHilIncident -Open $script:HilIncidents -Item $it -Check $Check -Record $Record)
    }
    return ,$refs
}
function Format-HilIncidentRefs([object[]]$Refs) {
    if (@($Refs).Count -eq 0) { return '' }
    return (' --- ' + ((@($Refs) | ForEach-Object { "incident $($_.IncidentId) $($_.Role)" }) -join '; '))
}

function Set-HilHalt {
    # -Verdict: a safety event caused by the current item; it is persisted in the machine-wide safety record
    # (tools/lib/HilLock.ps1) so every later run on this machine stays halted until an explicit verified recovery.
    param([string]$Reason, [string]$Verdict = '', $Last = $null)
    if ($Verdict -and $script:IsHardwareSuite -and $script:HilSafetyPath) {
        $it = $script:AvbItem
        $ev = [ordered]@{ verdict = $Verdict; scope = 'GLOBAL'
            hardware = [ordered]@{ did = $(if ($it) { $it.Target }); identityVerified = $false; note = 'adapter identity by DID only (no MAC/PCI correlation yet)' }
            trigger  = [ordered]@{ testcase = $(if ($it) { $it.Entry }); executable = $(if ($it) { Split-Path -Leaf ([string]$it.Artifact) }); arguments = $(if ($it) { $it.Arguments })
                                   suite = $Suite; configuration = $Configuration; runId = $env:GITHUB_RUN_ID; runAttempt = $env:GITHUB_RUN_ATTEMPT; job = $env:GITHUB_JOB; pid = $PID; reason = $Reason }
            initialState = $script:HilBaseline; lastKnownState = $Last
            evidence = @(@($(if ($it) { $it.LogPath }), $script:ResultsJsonPath) | Where-Object { $_ }) }
        $pr = Add-AvbHilSafetyEvent -Path $script:HilSafetyPath -SafetyEvent $ev
        if ($pr.Ok) { Write-Host "  [SAFETY-RECORD] $Verdict persisted (event $($pr.EventId)); Init/Mutating stay BLOCKED on this machine until a verified recovery clears it" -ForegroundColor Red }
        else { Write-Host "  [SAFETY-RECORD] NOT persisted in the record: $($pr.Why)" -ForegroundColor Red }
    }
    if ($script:stateHalt) { return }
    $script:stateHalt = $Reason
    Write-Host "  [HALT] state-integrity halt: $Reason --- every later Init/Mutating/Unsafe test is BLOCKED; ReadOnly tests continue" -ForegroundColor Red
}

function Get-HilBinName {
    param([string]$Entry)
    if ($Entry.StartsWith('ps1:')) { return [System.IO.Path]::GetFileNameWithoutExtension($Entry.Substring(4)) }
    return (($Entry -split '\s+', 2)[0] -replace '\.exe$', '')
}

# The suite lists are authoritative (Q 2026-10-22): a listed 'x.exe' is the test 'x'.  Registry/mutation-matrix key =
# binary name without '.exe' plus the arguments, so a spelling never makes a listed test unclassified (BLOCKED).
function ConvertTo-AvbHilEntryKey {
    param([string]$Entry)
    if ($Entry.StartsWith('ps1:')) { return $Entry }
    $p = $Entry -split '\s+', 2
    $b = $p[0] -replace '\.exe$', ''
    if ($p.Count -gt 1) { return "$b $($p[1])" }
    return $b
}

# Built artifact of a listed entry: the Build-Tests Output recorded under that Name in the test-build manifest
# (hw_timestamping_control -> hw_timestamping_control_test.exe); without a manifest record '<name>.exe'.
function Resolve-AvbHilArtifactName {
    param([string]$BinName, $Manifest)
    $n = $BinName -replace '\.exe$', ''
    if ($null -ne $Manifest -and $null -ne $Manifest.ByName -and $Manifest.ByName.ContainsKey($n)) {
        $a = [string]$Manifest.ByName[$n].artifact
        if ($a) { return $a }
    }
    return "$n.exe"
}

# Sandbox: only ps1 fakes outside the repository may execute; their class comes from AVB_TEST_SANDBOX_CLASSES.
function Test-HilSandboxFake {
    param([string]$Entry)
    if (-not $Entry.StartsWith('ps1:')) { return $false }
    $p = $Entry.Substring(4).Replace('/', '\')
    if (-not [System.IO.Path]::IsPathRooted($p)) { return $false }
    $root = [System.IO.Path]::GetFullPath($script:repoRoot).TrimEnd('\') + '\'
    return (-not [System.IO.Path]::GetFullPath($p).StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase))
}

# Declared mutated state of an entry (sandbox: AVB_TEST_SANDBOX_MUTATES 'bin=STATE,STATE;...'); @() = undeclared.
function Get-AvbHilMutatedState {
    param([string]$Entry)
    if ($script:IsSandbox) {
        $bin = Get-HilBinName $Entry
        foreach ($pair in (([string]$env:AVB_TEST_SANDBOX_MUTATES) -split ';')) {
            if ($pair -match '^\s*([^=]+?)\s*=\s*(\S+)\s*$' -and $Matches[1] -eq $bin) { return @($Matches[2] -split ',') }
        }
        return @()
    }
    $k = ConvertTo-AvbHilEntryKey $Entry
    if ($HilMutatedState.Contains($k)) { return @($HilMutatedState[$k]) }
    return @()
}

# Per-entry timeout: the larger of the suite timeout and $HilTestTimeoutSeconds[entry] (never lowers it).  Sandbox:
# AVB_TEST_SANDBOX_TIMEOUTS ('bin=seconds;...').
function Get-AvbTestTimeoutSeconds {
    param([string]$Entry, [int]$SuiteSeconds)
    $own = 0
    if ($script:IsSandbox) {
        $bin = Get-HilBinName $Entry
        foreach ($pair in (([string]$env:AVB_TEST_SANDBOX_TIMEOUTS) -split ';')) {
            if ($pair -match '^\s*([^=]+?)\s*=\s*(\d+)\s*$' -and $Matches[1] -eq $bin) { $own = [int]$Matches[2] }
        }
    } elseif ($HilTestTimeoutSeconds.Contains($Entry)) { $own = [int]$HilTestTimeoutSeconds[$Entry] }
    return [Math]::Max($SuiteSeconds, $own)
}

# Stop-AvbProcessTree (timed-out test + every descendant, verified): tools/lib/NativeExit.ps1, shared with
# Run-Tests-Elevated.ps1.

# PHC window of one adapter (Get-AvbHilState -PhcReads N) in the input format of tools/lib/PhcRestoration.ps1.  Adapter
# identity = Get-AvbHilAdapterKey (NET_LUID, or the enumeration position without one): the same identity
# Compare-AvbHilState uses for clock registers.
function ConvertTo-AvbHilPhcWindow {
    param($Entry)
    return [ordered]@{
        provenance = [ordered]@{ source = $(if ($script:IsSandbox) { 'synthetic' } else { 'hardware' }); host = $env:COMPUTERNAME; did = $Entry.Did
                                 adapterIdentity = (Get-AvbHilAdapterKey $Entry); capturedUtc = (Get-Date).ToUniversalTime().ToString('o')
                                 driverSha256 = $env:HIL_DRIVER_SHA256 }
        qpcFrequency = [System.Diagnostics.Stopwatch]::Frequency; timinca = [uint32]$Entry.Timinca; tsauxc = [uint32]$Entry.Tsauxc
        tsControl = $null   # timestamp-control state is not observed by the runner
        samples = @($Entry.Samples)
    }
}

# Hardware-derived PHC tolerances (DID -> tolerance object for Get-AvbPhcRestorationVerdict) from the evidence document of
# the machine safety record's operator-approved baseline (phcTolerance[]).  None defined = empty: every PHC judgement is
# then INCONCLUSIVE_RESTORATION (the evaluator never invents a threshold).
function Get-AvbHilPhcTolerances {
    param($SafetyRecord)
    $map = @{}
    if ($null -eq $SafetyRecord -or -not $SafetyRecord.PSObject.Properties['baseline'] -or $null -eq $SafetyRecord.baseline) { return $map }
    $ev = [string]$SafetyRecord.baseline.evidence
    if (-not $ev -or -not (Test-Path -LiteralPath $ev -PathType Leaf)) { return $map }
    try { $doc = Get-Content -LiteralPath $ev -Raw | ConvertFrom-Json } catch { return $map }
    if (-not $doc.PSObject.Properties['phcTolerance']) { return $map }
    foreach ($t in @($doc.phcTolerance)) { if ($t -and $t.did) { $map[[string]$t.did] = $t } }
    return $map
}

# D1 (Q 2026-10-22): a non-ReadOnly test that stopped at a failed precondition is NOT a restoration case when the runner
# itself verifies that nothing changed (observable adapter/TIMINCA/TSAUXC equality is checked by the caller first).
# Requires: no timeout; no cleanup-failure output; every testcase line FAILED_SETUP claiming "no state modified"; and the
# PHC of every adapter RESTORATION_VERIFIED by tools/lib/PhcRestoration.ps1 between the pre-test and post-test windows
# against its hardware-derived tolerance (a SET_TIMESTAMP, an offset or rate change, a PTP re-initialization by
# OPEN_ADAPTER - issue #343, SYSTIM reset - breaks it; I210 TSAUXC bit 31 clearing by OPEN_ADAPTER is caught by the
# TSAUXC comparison).  Not observable and therefore not claimed: the driver-global g_AvbContext switch of OPEN_ADAPTER
# (the runner's own observation does it too) and the last_ndis_tx_record of TEST_SEND_PTP (volatile driver state,
# rewritten by every send).  @{ Ok; Claimed; Why }.
function Test-AvbHilNoMutation {
    param([string]$Output, [bool]$TimedOut, $PreState, $PostState, [hashtable]$Tolerances = @{})
    $tcs = Get-AvbTestcaseVerdicts -Lines @($Output -split "`r?`n")   # already an array (unary-comma return); @() would nest it
    $claimed = ($tcs.Count -gt 0 -and @($tcs | Where-Object { $_.Verdict -ne 'FAILED_SETUP' -or $_.Detail -notmatch 'no state modified' }).Count -eq 0)
    if (-not $claimed) { return @{ Ok = $false; Claimed = $false; Why = '' } }
    if ($TimedOut) { return @{ Ok = $false; Claimed = $true; Why = 'timed out' } }
    if ($Output -match '(?m)^\s*\[CLEANUP_FAILED\]|RESTORE FAILED') { return @{ Ok = $false; Claimed = $true; Why = 'cleanup failure reported' } }
    if ($null -eq $PreState -or -not $PreState.ClockOk -or $null -eq $PostState -or -not $PostState.ClockOk) {
        return @{ Ok = $false; Claimed = $true; Why = 'PHC clock state not observable before and after the test' }
    }
    $after = @{}; foreach ($c in @($PostState.Clock)) { $after["$($c.Did)#$($c.Index)"] = $c }
    $parts = @(); $ok = $true; $n = 0; $observed = $false
    foreach ($b in @($PreState.Clock | Where-Object { $_.Ok })) {
        $a = $after["$($b.Did)#$($b.Index)"]
        if ($null -eq $a -or -not $a.Ok) { $ok = $false; $parts += "$($b.Did): clock config not readable after the test"; continue }
        $v = Get-AvbPhcRestorationVerdict -Before (ConvertTo-AvbHilPhcWindow $b) -After (ConvertTo-AvbHilPhcWindow $a) -Tolerance $Tolerances[[string]$b.Did] -Testcase 'TC-INFRA-STATE-003'
        $n++
        if ($v.Verdict -ne 'RESTORATION_VERIFIED') { $ok = $false }
        if ($v.Verdict -eq 'CLEANUP_FAILED') { $observed = $true }   # a change measured, not merely unobservable
        $parts += ("{0}: {1} ({2})" -f $b.Did, $v.Verdict, (@($v.Reasons) -join '; '))
    }
    if ($n -eq 0) { $ok = $false; $parts += 'no adapter with a readable clock' }
    return @{ Ok = $ok; Claimed = $true; Why = ($parts -join ' | '); Observed = $observed }
}

function Get-AvbHilTestClass {
    param([string]$Entry)
    if ($script:IsSandbox) {
        $bin = Get-HilBinName $Entry
        foreach ($pair in (([string]$env:AVB_TEST_SANDBOX_CLASSES) -split ';')) {
            if ($pair -match '^\s*([^=]+?)\s*=\s*(\S+)\s*$' -and $Matches[1] -eq $bin) { return $Matches[2] }
        }
        return ''
    }
    $k = ConvertTo-AvbHilEntryKey $Entry
    if ($HilTestSafety.ContainsKey($k)) { return $HilTestSafety[$k] }
    return ''
}

# Pure decision: 'Run' | 'Block' (+ Reason).  A Mutating:Target item runs only with an exact target DID.
function Get-AvbHilGateDecision {
    param([string]$Entry, [string]$Class, [string]$Target, [string]$Halt, [int]$TargetCount,
          [string[]]$AuthorizedUnsafe, [bool]$UnderGitHubActions, [bool]$InitUnattendedApproved = $false, [hashtable]$HazardHolds = @{})
    # Evidence-referenced hardware hazard: never executed, whatever class, target, attendance or authorization.
    if ($null -ne $HazardHolds -and $HazardHolds.ContainsKey($Entry)) { return @{ Action = 'Block'; Reason = "hazard hold: $($HazardHolds[$Entry])" } }
    $haltReason = "state-integrity halt ($Halt)"
    switch ($Class) {
        'ReadOnly' { return @{ Action = 'Run'; Reason = '' } }
        'Init' {
            if ($Halt) { return @{ Action = 'Block'; Reason = $haltReason } }
            if ($UnderGitHubActions -and -not $InitUnattendedApproved) {
                return @{ Action = 'Block'; Reason = 'Init (INIT_DEVICE re-initializes the adapter PHC: I226 SYSTIM reset and TIMINCA rewrite, I219 TIMINCA rewrite; target may depend on the previous test) is not approved for unattended HIL (effects source-derived, UNVERIFIED on hardware) --- run it attended, outside GitHub Actions' }
            }
            return @{ Action = 'Run'; Reason = '' }
        }
        'Mutating:Target' {
            if ($Halt) { return @{ Action = 'Block'; Reason = $haltReason } }
            if (-not $Target) {
                if ($TargetCount -eq 0) { return @{ Action = 'Block'; Reason = 'no supported, enabled adapter is available as exact target (see TC-INFRA-ADAPTER-*)' } }
                return @{ Action = 'Block'; Reason = 'Mutating:Target entry without an exact target DID' }
            }
            return @{ Action = 'Run'; Reason = '' }
        }
        'Mutating' {
            if ($Halt) { return @{ Action = 'Block'; Reason = $haltReason } }
            return @{ Action = 'Block'; Reason = 'Mutating test is not exact-target-aware (does not honour AVB_HIL_TARGET_DID); it would change PHC/TSN/PHY state on whichever adapter the driver routes it to' }
        }
        'Unsafe' {
            if ($UnderGitHubActions) {
                return @{ Action = 'Block'; Reason = 'Unsafe (NIC/driver administration); AVB_HIL_AUTHORIZE_UNSAFE is ignored under GitHub Actions' }
            }
            if ($AuthorizedUnsafe -notcontains $Entry) {
                return @{ Action = 'Block'; Reason = 'Unsafe (NIC/driver administration); requires AVB_HIL_AUTHORIZE_UNSAFE to name this exact entry on an operator-attended run' }
            }
            if ($Halt) { return @{ Action = 'Block'; Reason = $haltReason } }
            return @{ Action = 'Run'; Reason = '' }
        }
        'Unknown' { return @{ Action = 'Block'; Reason = 'classified Unknown (no reviewable source)' } }
        ''        { return @{ Action = 'Block'; Reason = 'unclassified: no entry in the runner safety registry ($HilTestSafety)' } }
        default   { return @{ Action = 'Block'; Reason = "invalid safety class '$Class'" } }
    }
}

function Add-HilBlocked {
    param([string]$Entry, [string]$Display, [string]$Stem, [string]$Reason)
    # Every blocked item names its intended adapter (a fanned-out Mutating:Target item is one per exact target DID).
    $tgt = if ($script:AvbItem -and $script:AvbItem.Target) { "; intended target $($script:AvbItem.Target)" } else { '' }
    Add-HilFailureRecord -Display $Display -Status 'BLOCKED(SAFETY)' -Stem $Stem `
        -Line ("[BLOCKED] TC-SAFETY-GATE-001: {0}: {1} --- not executed{2}" -f $Entry, $Reason, $tgt)
}

# A declared automatic-HIL coverage gap ($Hardware*CoverageGaps): a REQUIRED test that is not executed because its
# fixture/restoration or infrastructure is missing.  Never a skip, never green: BLOCKED (JUnit failure) and the run is
# INCOMPLETE (Get-AvbRunOverall).  P0 full HIL coverage, 2026-10-21.
# Gap rows that still apply to this run: a listed entry (spelling normalized) is executed or gate-blocked with its own
# reason, never ALSO a NOT EXECUTED coverage gap (the suite lists are authoritative, Q 2026-10-22).
function Get-AvbHilEffectiveGaps {
    param($GapTable, [string[]]$TestList)
    $listed = @($TestList | ForEach-Object { ConvertTo-AvbHilEntryKey $_ })
    $eff = [ordered]@{}
    foreach ($g in @($GapTable.Keys)) { if ($listed -notcontains (ConvertTo-AvbHilEntryKey $g)) { $eff[$g] = $GapTable[$g] } }
    return $eff
}

function Add-HilCoverageGap {
    param([string]$Entry, [string]$Reason)
    $script:coverageGapTests++
    $line = "[BLOCKED] TC-HIL-COVERAGE-GAP-001: {0} NOT EXECUTED by automatic HIL (required coverage missing) --- {1}" -f $Entry, $Reason
    Write-Host "  $line" -ForegroundColor Red
    $ts = Get-Date -Format 'yyyyMMdd_HHmmss'
    $p = Join-Path $script:logsDir ("{0}.exe_{1}.log" -f $Entry, $ts)
    $i = 2
    while (Test-Path -LiteralPath $p) { $p = Join-Path $script:logsDir ("{0}~{1}.exe_{2}.log" -f $Entry, $i, $ts); $i++ }
    $line | Set-Content -LiteralPath $p -Encoding utf8
    Add-AvbResult -Name $Entry -Status 'COVERAGE-GAP' -Exit 'N/A' -Reason $Reason -LogPath $p -Lines @($line)
}

# ---------------------------------------------------------------------------------------------------------
# Build provenance (#328 investigation F23: a stale 8-April binary of a Disabled build entry was executed as
# "ReadOnly" during a state-integrity HALT and changed I226 TSN state).
# In a Hardware* suite an artifact executes only if tools/build/Build-Tests.ps1's test-build-manifest.json for
# THIS configuration records it as `built` by an enabled entry and its SHA256 matches.  Checked after the
# artifact is found and BEFORE the safety class / HALT rules; the artifact is never executed to decide.
# Repository-tracked ps1 tests run from source and are exempt; every other artifact (test exe, ps1 outside the
# repository, sandbox fake) needs a manifest record.  This is a correctness/provenance check, not a security
# boundary against malicious repository code (runner trust boundary: investigations/hil-runner-trust-boundary).
# ---------------------------------------------------------------------------------------------------------
function Get-AvbManifestProp {
    param($Object, [string]$Name)
    if ($null -eq $Object) { return $null }
    if ($Object.PSObject.Properties[$Name]) { return $Object.$Name }
    return $null
}

function Read-AvbBuildManifest {
    param([string]$Path, [string]$Configuration)
    $res = @{ Ok = $false; Error = ''; ByArtifact = @{}; ByName = @{}; Path = $Path; Revision = $null }
    if (-not (Test-Path -LiteralPath $Path)) {
        $res.Error = "test build manifest not found: $Path (build with tools/build/Build-Tests.ps1 -Configuration $Configuration)"
        return $res
    }
    try { $m = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json }
    catch { $res.Error = "test build manifest unreadable: $Path ($($_.Exception.Message))"; return $res }
    if ((Get-AvbManifestProp $m 'schemaVersion') -ne 1 -or $null -eq (Get-AvbManifestProp $m 'entries')) {
        $res.Error = "test build manifest has an unsupported schema: $Path"
        return $res
    }
    $mCfg = Get-AvbManifestProp $m 'configuration'
    if ($mCfg -ne $Configuration) {
        $res.Error = "test build manifest is for configuration '$mCfg' but this run is '$Configuration': $Path"
        return $res
    }
    $ambiguous = @()
    foreach ($e in @(Get-AvbManifestProp $m 'entries')) {
        $a = [string](Get-AvbManifestProp $e 'artifact')
        if (-not $a) { continue }
        $k = $a.ToLowerInvariant()
        if ($res.ByArtifact.ContainsKey($k)) { $ambiguous += $k } else { $res.ByArtifact[$k] = $e }
        $nm = [string](Get-AvbManifestProp $e 'name')
        if ($nm) { $res.ByName[$nm] = $e }   # entry name -> artifact (Resolve-AvbHilArtifactName); authorization stays ByArtifact
    }
    foreach ($k in $ambiguous) { $res.ByArtifact[$k] = $null }   # recorded by two entries -> never authorized
    $res.Ok = $true
    return $res
}

# Source identity of the checkout under test, computed once per suite: @{ Ok; Revision; Dirty; Mode; Digest; DigestError; Error }.
# Dirty = tracked modifications.  Mode 'ci' (GitHub Actions: clean authorized commit only) or 'local' (operator runs:
# the build-input digest, tools/lib/SourceDigest.ps1, also covers untracked inputs and attributes uncommitted builds).
# Sandbox runs take it from AVB_TEST_SANDBOX_SOURCE_REVISION / _DIRTY ('0' | '1') / _MODE (default 'ci') / _DIGEST;
# the variables are ignored otherwise.
function Get-AvbSourceIdentity {
    $res = @{ Ok = $false; Revision = $null; Dirty = $null; Mode = 'ci'; Digest = $null; DigestError = ''; Error = '' }
    if ($script:IsSandbox) {
        if (-not $env:AVB_TEST_SANDBOX_SOURCE_REVISION -or $env:AVB_TEST_SANDBOX_SOURCE_DIRTY -notmatch '^[01]$') {
            $res.Error = 'sandbox source identity not provided (AVB_TEST_SANDBOX_SOURCE_REVISION / _DIRTY)'
            return $res
        }
        $res.Revision = $env:AVB_TEST_SANDBOX_SOURCE_REVISION
        $res.Dirty = ($env:AVB_TEST_SANDBOX_SOURCE_DIRTY -eq '1')
        if ($env:AVB_TEST_SANDBOX_SOURCE_MODE -eq 'local') { $res.Mode = 'local' }
        if ($env:AVB_TEST_SANDBOX_SOURCE_DIGEST) { $res.Digest = $env:AVB_TEST_SANDBOX_SOURCE_DIGEST } else { $res.DigestError = 'sandbox digest not provided' }
        $res.Ok = $true
        return $res
    }
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        Reset-AvbExitCode
        $rev = & git -C $script:repoRoot rev-parse HEAD 2>$null
        if (-not (Test-AvbExitSuccess (Get-AvbExitCode)) -or -not $rev) { $res.Error = 'git rev-parse HEAD failed'; return $res }
        Reset-AvbExitCode
        $st = & git -C $script:repoRoot status --porcelain --untracked-files=no 2>$null
        if (-not (Test-AvbExitSuccess (Get-AvbExitCode))) { $res.Error = 'git status failed'; return $res }
        $res.Revision = ([string]$rev).Trim()
        $res.Dirty = [bool]$st
        if ($env:GITHUB_ACTIONS -ne 'true') {
            $res.Mode = 'local'
            $d = Get-AvbBuildInputDigest -RepoRoot $script:repoRoot
            if ($d.Ok) { $res.Digest = $d.Digest } else { $res.DigestError = $d.Error }
        }
        # Under GitHub Actions the checkout must be exactly the authorized commit.  .github/workflows/hil.yml runs on
        # workflow_run, where GITHUB_SHA is the default-branch tip, not the CI run that was verified; it passes the
        # authorized upstream head_sha as AVB_HIL_EXPECTED_REVISION, and a workflow_run without it fails closed.
        if ($env:GITHUB_ACTIONS -eq 'true') {
            $expected = $env:GITHUB_SHA
            $what = 'GITHUB_SHA'
            if ($env:AVB_HIL_EXPECTED_REVISION -or $env:GITHUB_EVENT_NAME -eq 'workflow_run') {
                $expected = $env:AVB_HIL_EXPECTED_REVISION
                $what = 'AVB_HIL_EXPECTED_REVISION'
                if ([string]$expected -cnotmatch '^[0-9a-f]{40}$') {
                    $res.Error = "AVB_HIL_EXPECTED_REVISION '$expected' is not a 40-hex commit id (required for workflow_run)"
                    return $res
                }
            }
            if ($expected -and $res.Revision -cne $expected) {
                $res.Error = "checked-out HEAD $($res.Revision) is not the authorized commit $what=$expected"
                return $res
            }
        }
        $res.Ok = $true
    } catch {
        $res.Error = "git unavailable: $($_.Exception.Message)"
    } finally {
        $ErrorActionPreference = $prevEap
    }
    return $res
}

# Pure decision on an existing artifact: 'Run' | 'Block' (+ Reason).
# A matching SHA256 is necessary but never sufficient.  CI (GitHub Actions, the default): the artifact must have been
# built from the clean, committed revision that is checked out now, and the checkout must still be clean.  Local
# operator runs ($Source.Mode 'local'): the artifact must have been built at the same HEAD from EXACTLY the current
# build inputs (build-input digest, tools/lib/SourceDigest.ps1) - uncommitted changes are allowed, any change after
# the build blocks.  Local evidence is never clean-commit CI evidence.
function Get-AvbHilProvenanceDecision {
    param([string]$ArtifactPath, $Manifest, $Source)
    if (-not $Manifest.Ok) { return @{ Action = 'Block'; Reason = $Manifest.Error } }
    if ($null -eq $Source -or -not $Source.Ok) {
        return @{ Action = 'Block'; Reason = "current source revision unavailable ($(if ($Source) { $Source.Error } else { 'not computed' })); artifacts cannot be attributed" }
    }
    $localMode = ($Source.ContainsKey('Mode') -and $Source.Mode -eq 'local')
    if ($Source.Dirty -and -not $localMode) {
        return @{ Action = 'Block'; Reason = "checkout $($Source.Revision) has uncommitted tracked changes; test binaries cannot be attributed to a source revision" }
    }
    $leaf = [System.IO.Path]::GetFileName($ArtifactPath)
    $k = $leaf.ToLowerInvariant()
    if (-not $Manifest.ByArtifact.ContainsKey($k)) {
        return @{ Action = 'Block'; Reason = "artifact '$leaf' is not recorded in the test build manifest (not produced by the current build)" }
    }
    $e = $Manifest.ByArtifact[$k]
    if ($null -eq $e) { return @{ Action = 'Block'; Reason = "artifact '$leaf' is recorded by more than one build entry (ambiguous)" } }
    $name   = Get-AvbManifestProp $e 'name'
    $reason = Get-AvbManifestProp $e 'reason'
    if ((Get-AvbManifestProp $e 'enabled') -ne $true) {
        return @{ Action = 'Block'; Reason = "build entry '$name' is Disabled ($reason); artifact '$leaf' is a leftover" }
    }
    $status = Get-AvbManifestProp $e 'status'
    if ($status -ne 'built') {
        return @{ Action = 'Block'; Reason = "build entry '$name' status is '$status' ($reason); artifact '$leaf' was not produced by the current build" }
    }
    $sha = [string](Get-AvbManifestProp $e 'sha256')
    if (-not $sha) { return @{ Action = 'Block'; Reason = "build entry '$name' records no SHA256 for '$leaf'" } }
    $bRev = [string](Get-AvbManifestProp $e 'sourceRevision')
    if ($localMode) {
        $bDigest = [string](Get-AvbManifestProp $e 'sourceDigest')
        if (-not $bDigest) {
            return @{ Action = 'Block'; Reason = "artifact '$leaf' has no build-input digest in the manifest (rebuild with tools/build/Build-Tests.ps1)" }
        }
        if (-not $Source.Digest) {
            return @{ Action = 'Block'; Reason = "current build-input digest unavailable ($($Source.DigestError)); artifact '$leaf' cannot be attributed" }
        }
        if ($bDigest -ne $Source.Digest) {
            return @{ Action = 'Block'; Reason = "build inputs changed after artifact '$leaf' was built (digest $bDigest at build, $($Source.Digest) now); rebuild before testing" }
        }
    } elseif ((Get-AvbManifestProp $e 'sourceDirty') -ne $false) {
        return @{ Action = 'Block'; Reason = "artifact '$leaf' was built from an uncommitted or unknown source state (revision '$bRev')" }
    }
    if (-not $bRev -or $bRev -ne $Source.Revision) {
        return @{ Action = 'Block'; Reason = "artifact '$leaf' was built from revision '$bRev' but the checkout is '$($Source.Revision)'" }
    }
    $actual = (Get-FileHash -LiteralPath $ArtifactPath -Algorithm SHA256).Hash
    if ($actual -ne $sha.ToUpperInvariant()) {
        return @{ Action = 'Block'; Reason = "SHA256 mismatch for '$leaf': artifact $actual, manifest $sha" }
    }
    return @{ Action = 'Run'; Reason = '' }
}

function Test-AvbHilNeedsProvenance {
    param([string]$Entry, [string]$ArtifactPath)
    if (-not $Entry.StartsWith('ps1:')) { return $true }
    $root = [System.IO.Path]::GetFullPath($script:repoRoot).TrimEnd('\') + '\'
    return (-not [System.IO.Path]::GetFullPath($ArtifactPath).StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase))
}

function Invoke-HilBaseline {
    Write-Step "HIL safety gate: adapter + PHC baseline (observation only; the runner never enables, disables or rebinds an adapter)"
    try {
        $script:HilSupported = Get-AvbHilSupportedDids
    } catch {
        Add-HilFailureRecord -Display 'HIL-BASELINE' -Status 'INFRA_FAILURE' -Stem 'TC-INFRA-ADAPTER' `
            -Line "[INFRA_FAILURE] TC-INFRA-ADAPTER-000: supported-adapter set unavailable: $($_.Exception.Message)"
        Set-HilHalt 'supported-adapter set unavailable'
        return
    }
    $b = Get-AvbHilState
    $script:HilBaseline = $b
    if (-not $b.Ok) {
        Add-HilFailureRecord -Display 'HIL-BASELINE' -Status 'INFRA_FAILURE' -Stem 'TC-INFRA-STATE' `
            -Line "[INFRA_FAILURE] TC-INFRA-STATE-000: adapter state not observable at baseline: $($b.Error)"
        Set-HilHalt 'adapter state not observable at baseline'
    }
    if (-not $b.ClockOk) {
        Add-HilFailureRecord -Display 'HIL-BASELINE' -Status 'INFRA_FAILURE' -Stem 'TC-INFRA-STATE' `
            -Line "[INFRA_FAILURE] TC-INFRA-STATE-000: PHC clock state not observable at baseline: $($b.ClockError)"
        Set-HilHalt 'PHC clock state not observable at baseline'
    }
    $healthy = @()
    $n = 0
    $supportedPresent = 0
    foreach ($a in $b.Adapters) {
        if (-not $a.Did -or -not $script:HilSupported.ContainsKey($a.Did)) {
            Write-Host ("  [INFO] unsupported Intel adapter DID {0} {1}: not in AvbGetIntelDeviceType --- not a test target" -f $a.Did, $a.InstanceId) -ForegroundColor DarkGray
            $n++
            continue
        }
        $supportedPresent++
        $why = @(); $blocked = $false
        if ($a.Problem -eq 22) { $why += 'disabled (problem 22)'; $blocked = $true }
        elseif ($a.Problem -ne 0) { $why += "PnP problem $($a.Problem)" }
        if ($a.PnpStatus -ne 'OK' -and $a.Problem -ne 22) { $why += "PnP status $($a.PnpStatus)" }
        if ($a.AdminStatus -eq 'Down') { $why += 'administratively down'; $blocked = $true }
        elseif ($a.AdminStatus -ne 'Up') { $why += "admin status $($a.AdminStatus)" }
        if ($a.FilterBound -ne 'True') { $why += "IntelAvbFilter binding $($a.FilterBound)" }
        if ($why.Count -gt 0) {
            $tag = if ($blocked) { 'BLOCKED' } else { 'INFRA_FAILURE' }
            Add-HilFailureRecord -Display ("adapter {0}" -f $a.Did) -Status $tag -Stem 'TC-INFRA-ADAPTER' `
                -Line ("[{0}] TC-INFRA-ADAPTER-001/adapter:{1}: DID {2} ({3}) {4}: {5} --- supported adapter unavailable; the runner never enables or rebinds adapters, restore it manually to regain coverage" -f `
                       $tag, $n, $a.Did, $script:HilSupported[$a.Did], $a.InstanceId, ($why -join ', '))
        } else {
            $healthy += $a.Did
        }
        $n++
    }
    if ($supportedPresent -eq 0) {
        Add-HilFailureRecord -Display 'HIL-BASELINE' -Status 'INFRA_FAILURE' -Stem 'TC-INFRA-ADAPTER' `
            -Line "[INFRA_FAILURE] TC-INFRA-ADAPTER-005: no supported Intel adapter present (supported set: AvbGetIntelDeviceType)"
    }
    $dups = @($healthy | Group-Object | Where-Object { $_.Count -gt 1 } | ForEach-Object { $_.Name })
    foreach ($d in $dups) {
        Add-HilFailureRecord -Display ("adapter {0}" -f $d) -Status 'BLOCKED' -Stem 'TC-INFRA-ADAPTER' `
            -Line ("[BLOCKED] TC-INFRA-ADAPTER-003: DID {0} is present more than once --- exact targeting by device id is impossible; it is not a Mutating:Target target" -f $d)
    }
    $targets = @()
    if ($b.ClockOk) {
        foreach ($d in @($healthy | Sort-Object -Unique)) {
            if ($dups -contains $d) { continue }
            $ce = @($b.Clock | Where-Object { $_.Did -eq $d })
            if ($ce.Count -eq 0) {
                Add-HilFailureRecord -Display ("adapter {0}" -f $d) -Status 'INFRA_FAILURE' -Stem 'TC-INFRA-ADAPTER' `
                    -Line ("[INFRA_FAILURE] TC-INFRA-ADAPTER-002: DID {0} is enabled in PnP but not enumerated by the driver (filter not attached)" -f $d)
                continue
            }
            if (-not $ce[0].Ok) {
                Add-HilFailureRecord -Display ("adapter {0}" -f $d) -Status 'BLOCKED' -Stem 'TC-INFRA-ADAPTER' `
                    -Line ("[BLOCKED] TC-INFRA-ADAPTER-004: DID {0} clock config not readable ({1}) --- a mutation on it could not be verified; it is not a Mutating:Target target" -f $d, $ce[0].Error)
                continue
            }
            $targets += $d
        }
    }
    $script:HilTargets = $targets
    $tlist = if ($targets.Count -gt 0) { $targets -join ', ' } else { '(none)' }
    Write-Note "HIL exact targets for Mutating:Target tests: $tlist"
}

# Timed-out item (D2, revised Q 2026-10-28): the only loss of controlled execution here is a process tree that did NOT
# terminate (an operation may still be outstanding): disposition HALT, persisted halt.  A terminated tree keeps
# FAILED(TIMEOUT) as the test's own verdict and blocks nothing (TERMINATED_STATE_VERIFIED: ReadOnly with the observable
# state unchanged; TERMINATED: otherwise - its restoration is reported as cleanup).  Get-AvbPriorHalt reads the same value.
function Set-HilTimeoutDisposition {
    param([string]$Display, [string]$Class, [bool]$StateVerified, $Last)
    $term = if ($script:AvbItem) { $script:AvbItem.Termination } else { $null }
    $disp = if (-not ($term -and $term.Ok)) { 'HALT' } elseif ($Class -eq 'ReadOnly' -and $StateVerified) { 'TERMINATED_STATE_VERIFIED' } else { 'TERMINATED' }
    if ($script:AvbItem -and $script:AvbItem.TimeoutRecord) { $script:AvbItem.TimeoutRecord['timeoutDisposition'] = $disp }
    if ($disp -eq 'HALT') {
        Set-HilHalt ("$Display ($Class) timed out and its process tree did not terminate: {0}" -f $(if ($term) { $term.Detail } else { 'termination not observed' })) -Verdict 'FAILED(TIMEOUT)' -Last $Last
    } else {
        Write-Host ("  [STATE] after {0} ({1}): FAILED(TIMEOUT) kept; process tree terminated ({2}) --- later tests not halted" -f $Display, $Class, $term.Detail) -ForegroundColor Yellow
    }
}
# After every executed test (also after a timeout kill).
function Invoke-HilPostTestCheck {
    param([string]$Display, [string]$Class, [bool]$TimedOut, [string]$Output)
    if ($null -eq $script:HilBaseline) { return }
    $cur = Get-AvbHilState
    $cmp = Compare-AvbHilState $script:HilBaseline $cur
    $first = @($cmp.Diffs)
    $waited = 0
    while (@($cmp.Diffs).Count -gt 0 -and $waited -lt $script:HilSettleSeconds) {
        Start-Sleep -Seconds 1
        $waited++
        $cur = Get-AvbHilState
        $cmp = Compare-AvbHilState $script:HilBaseline $cur
    }
    foreach ($w in @($cmp.Warn)) { Write-Host "  [WARN:STATE] after ${Display}: $w (observed, not enforced)" -ForegroundColor Yellow }
    if (@($cmp.Diffs).Count -gt 0) {
        # An observed difference is a cleanup FAILED of the test that left it: reported separately with its evidence and
        # correlated in the session record - never a functional failure row, a halt or a machine-safety event (Q 2026-10-28).
        $refs = Register-HilIncidents -Items $cmp.Items -Check 'per-test' -Record $Display
        Add-AvbCleanupResult -Display $Display -Class $Class -Required $(if ($Class -eq 'ReadOnly') { 'NO' } else { 'YES' }) -Outcome 'FAILED' -Testcase 'TC-INFRA-STATE-001' `
            -Reason ("state differs from the baseline: {0}; the runner made no corrective change" -f (@($cmp.Diffs) -join '; ')) -Observations @($cmp.Diffs) -Incidents $refs
        $script:HilBaseline = $cur   # later differences are attributed to later tests
        if ($TimedOut) { Set-HilTimeoutDisposition -Display $Display -Class $Class -StateVerified $false -Last $cur }
        return
    }
    if ($first.Count -gt 0) {
        Write-Host ("  [WARN:STATE] after {0}: transient difference settled back to the baseline within {1}s: {2}" -f $Display, $waited, ($first -join '; ')) -ForegroundColor Yellow
    }
    if ($TimedOut) { Set-HilTimeoutDisposition -Display $Display -Class $Class -StateVerified $true -Last $cur }
    if ($Class -eq 'ReadOnly') {
        if ($TimedOut -and -not ($script:AvbItem -and $script:AvbItem.Termination -and $script:AvbItem.Termination.Ok)) {
            Add-AvbCleanupResult -Display $Display -Class $Class -Required 'NO' -Outcome 'NOT_VERIFIED' -Reason 'process tree did not terminate after the timeout; later state changes cannot be excluded'
        } else {
            Add-AvbCleanupResult -Display $Display -Class $Class -Required 'NO' -Outcome 'NOT_REQUIRED' -Reason 'ReadOnly; observable state equals the baseline'
        }
    }
    if ($Class -ne 'ReadOnly') {        # Restoration is verified only when everything the entry declares it modifies was re-read above and equals the
        # baseline.  Otherwise it is cleanup NOT_VERIFIED (missing evidence, distinct from an observed difference = FAILED):
        # reported separately, never a functional failure, never a halt (Q 2026-10-28, binding; supersedes the 2026-10-21
        # global halt, option 1).
        # The test's own cleanup report: a cleanup line ([CLEANUP:FAILED] / [CLEANUP:NOT_VERIFIED]; test_ptp_phc_lifecycle,
        # test_ptp_phc_stability, test_ioctl_tas) or the older CLEANUP_FAILED / RESTORE FAILED text.  Kept as observations.
        $selfLines = @(if (-not $TimedOut) { [regex]::Matches([string]$Output, '(?m)^\s*(\[CLEANUP:(FAILED|NOT_VERIFIED)\]|\[CLEANUP_FAILED\]).*$|^.*RESTORE FAILED.*$') | ForEach-Object { $_.Value.Trim() } })
        $selfFail = (-not $TimedOut) -and ($Output -match '(?m)^\s*\[CLEANUP:FAILED\]|^\s*\[CLEANUP_FAILED\]|RESTORE FAILED')
        $selfNotVerified = (-not $TimedOut) -and -not $selfFail -and ($Output -match '(?m)^\s*\[CLEANUP:NOT_VERIFIED\]')
        $declared = @(if ($script:AvbItem) { Get-AvbHilMutatedState $script:AvbItem.Entry })
        $verifiableNow = @(Get-AvbHilVerifiableState -Base $script:HilBaseline -Cur $cur -Target $(if ($script:AvbItem -and $script:AvbItem.Target) { [string]$script:AvbItem.Target } else { '' }))
        $unobserved = @($declared | Where-Object { $verifiableNow -notcontains $_ })
        $required = if ($declared.Count -gt 0) { 'YES' } else { 'UNKNOWN' }
        $scope = if ($declared.Count -gt 0) { "declared modified state: $($declared -join ', ')$(if ($unobserved.Count) { "; not observable by the runner: $($unobserved -join ', ')" })" } else { 'modified state not declared (unknown)' }
        if ($declared.Count -gt 0 -and $unobserved.Count -eq 0 -and -not $TimedOut) {
            Write-Host ("  [STATE] after {0}: restoration verified by independent re-read ({1})" -f $Display, ($declared -join ', ')) -ForegroundColor Green
            if ($selfFail) { Add-AvbCleanupResult -Display $Display -Class $Class -Required $required -Outcome 'FAILED' -Observations $selfLines -Reason "the test reported its own cleanup as failed ($scope)" }
            elseif ($selfNotVerified) { Add-AvbCleanupResult -Display $Display -Class $Class -Required $required -Outcome 'NOT_VERIFIED' -Observations $selfLines -Reason "the runner's re-read is equal, but the test reported its own restoration as not verified ($scope)" }
            else { Add-AvbCleanupResult -Display $Display -Class $Class -Required $required -Outcome 'SUCCESS' -Reason "restoration verified by independent re-read ($scope)" }
            return
        }
        # D1: not a restoration case when the test stopped at a failed precondition and the runner verifies no change.
        $nm = Test-AvbHilNoMutation -Output $Output -TimedOut $TimedOut -PreState $script:HilPreTest -PostState (Get-AvbHilState -PhcReads $HilPhcReads) -Tolerances $script:HilPhcTolerance
        if ($nm.Ok) {
            Write-Host ("  [STATE] after {0} ({1}): no state change --- FAILED_SETUP before any mutation (test claim) independently verified: adapter/TIMINCA/TSAUXC equal to the baseline, PHC restoration evaluator per adapter: {2}" -f $Display, $Class, $nm.Why) -ForegroundColor Yellow
            Add-AvbCleanupResult -Display $Display -Class $Class -Required 'NO' -Outcome 'NOT_REQUIRED' -Reason "FAILED_SETUP before any mutation, no change independently verified: $($nm.Why)"
            return
        }
        $why = "restoration not independently verified ($scope) --- the runner observes adapter presence/PnP/admin/link/binding, TIMINCA/TSAUXC and the QUERY_HW_SNAPSHOT registers read VALID (verifiable groups now: $($verifiableNow -join ', ')); PHC time continuity, rate and offset and TAS/FP/PTM/Qav/CBS/PFC/EEE/VLAN/MDIO/target-time state are not observed" +
               $(if ($nm.Claimed) { " --- the test's FAILED_SETUP no-mutation claim is NOT verified: $($nm.Why)" } else { '' }) +
               $(if ($selfFail) { ' --- the test reported its own cleanup as failed' } elseif ($selfNotVerified) { ' --- the test reported its own restoration as not verified' } else { '' })
        $outcome = if ($selfFail -or $nm.Observed) { 'FAILED' } else { 'NOT_VERIFIED' }
        Add-AvbCleanupResult -Display $Display -Class $Class -Required $required -Outcome $outcome -Observations $selfLines -Reason $why
    }
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
$AvbResultRecords = @()
$AvbCleanup       = @()   # one cleanup result per executed Hardware* item (Add-AvbCleanupResult), independent of verdicts
$AvbRunStartedUtc = (Get-Date).ToUniversalTime().ToString('o')
$ResultsJsonPath  = if ($ResultsJson) { [System.IO.Path]::GetFullPath($ResultsJson) }
                    else { Join-Path $logsDir ("results-{0}-{1}-{2}.json" -f $Suite, $Configuration, (Get-Date -Format 'yyyyMMdd_HHmmss_fff')) }

# Expose driver build type to test_perf_regression and other tests that key
# their baseline on (device_id x driver_build)
$env:AVB_DRIVER_BUILD = $Configuration
Write-Note "AVB_DRIVER_BUILD=$Configuration (baseline context for perf regression)"

$HilSafetyPath = $null; $HilSafetyState = $null; $SourceIdentity = $null
if ($IsHardwareSuite) {
    # Machine-wide safety record: HALTED or UNAVAILABLE (missing/corrupt/foreign/inaccessible) = fail closed for every
    # Init/Mutating test of this run; ReadOnly tests continue.  Sandbox: AVB_TEST_HIL_SAFETY_FILE if set (exactly that
    # file), else a CLEAR fixture record in the sandbox logs directory; the %ProgramData% record is never touched.
    if ($IsSandbox) {
        $HilSafetyPath = if ($env:AVB_TEST_HIL_SAFETY_FILE) { $env:AVB_TEST_HIL_SAFETY_FILE } else {
            $sp = Join-Path $logsDir 'sandbox-hil-safety.json'
            if (-not (Test-Path -LiteralPath $sp)) {   # sandbox fixture only: a CLEAR record with a synthetic, labelled baseline
                ([ordered]@{ schema = 'avb-hil-safety/1'; host = $env:COMPUTERNAME; status = 'CLEAR'; events = @()
                             baseline = [ordered]@{ evidence = 'SANDBOX FIXTURE - not hardware evidence'; operator = 'sandbox'; authorization = 'offline sandbox'; via = 'SANDBOX' }
                             history = @([ordered]@{ utc = (Get-Date).ToUniversalTime().ToString('o'); operation = 'SANDBOX_FIXTURE'; operator = 'sandbox' }) } |
                    ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $sp -Encoding UTF8 }
            $sp }
    } else { $HilSafetyPath = Get-AvbHilSafetyPath }

    # Elevation is verified on this process's own token (sandbox: AVB_TEST_SANDBOX_ELEVATED, '0' = not elevated).
    # Necessary, never sufficient: lock ownership, the CLEAR safety record and the session baseline are checked independently.
    $HilElevated = if ($IsSandbox) { $env:AVB_TEST_SANDBOX_ELEVATED -ne '0' } else {
        ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) }
    if (-not $HilElevated) { Set-HilHalt 'not elevated: this process token is not an elevated Administrator (elevation is necessary, never sufficient)' }

    # Exclusive hardware access: the machine lock must be OWNED by this execution (Resolve-AvbHilLockContext: same CI
    # run/attempt/job, or this process / an OS ancestor holds it) - its mere existence never authorizes.  Attended:
    # acquire it (bounded wait) when no owner context holds it, release it at completion.  GitHub Actions: never acquired
    # here (the job's deploy step owns it); otherwise fail closed.  Not owned -> every Init/Mutating test BLOCKED.
    $HilLockPath = if ($IsSandbox -and $env:AVB_TEST_HIL_LOCK_FILE) { $env:AVB_TEST_HIL_LOCK_FILE } elseif ($IsSandbox) { Join-Path $logsDir 'sandbox-hil.lock' } else { Get-AvbHilLockPath }
    $HilGitHubContext = if ($UnderGitHubActions) { @{ runId = $env:GITHUB_RUN_ID; runAttempt = $env:GITHUB_RUN_ATTEMPT; job = $env:GITHUB_JOB } } else { $null }
    $HilLockAcquired = $null
    $lockCtx = Resolve-AvbHilLockContext -Path $HilLockPath -GitHub $HilGitHubContext
    if ($lockCtx.Owned) {
        Write-Note "Machine lock owned by this execution ($($lockCtx.How): $($lockCtx.Owner.kind) '$($lockCtx.Owner.label)')"
    } elseif (-not $UnderGitHubActions) {
        $acq = Enter-AvbHilLock -Kind 'local' -Label "Run-Tests-CI $Suite $Configuration" -Path $HilLockPath -WaitSeconds $HilLockWaitSeconds
        if ($acq.Ok) { $HilLockAcquired = $acq.Owner; Write-Note "Machine lock acquired by this run ($HilLockPath); released at completion" }
        else { Set-HilHalt "machine lock not owned by this execution: $($lockCtx.Why); not acquired within ${HilLockWaitSeconds}s: $($acq.Why)" }
    } else {
        Set-HilHalt "machine lock not owned by this execution: $($lockCtx.Why) (an unattended run never acquires it)"
    }

    # Policy reconciliation before the record is read (Q 2026-10-29): safety events persisted by the former rule for a
    # cleanup outcome (CLEANUP_FAILED / INCONCLUSIVE_RESTORATION) are reclassified as cleanup diagnostics - idempotent,
    # history and cleanup outcome kept (tools/lib/HilLock.ps1).  Only with verified lock ownership and elevation; loss-of-
    # control events keep the halt.  Same step for LocalSuites and hil.yml, both of which run this script.
    $HilLockOwner = if ($lockCtx.Owned) { $lockCtx.Owner } else { $HilLockAcquired }
    if ($HilLockOwner -and $HilElevated) {
        $SourceIdentity = Get-AvbSourceIdentity
        if ($SourceIdentity.Ok) {
            $reBy = "Run-Tests-CI $Suite/$Configuration pid $PID" + $(if ($UnderGitHubActions) { " GitHub run $env:GITHUB_RUN_ID attempt $env:GITHUB_RUN_ATTEMPT job $env:GITHUB_JOB" } else { ' (attended)' })
            $reRev = $SourceIdentity.Revision + $(if ($SourceIdentity.Dirty) { " + tracked changes (build-input digest $($SourceIdentity.Digest))" } else { '' })
            $rc = Invoke-AvbHilSafetyReclassification -Path $HilSafetyPath -LockOwner $HilLockOwner -LockPath $HilLockPath -SourceRevision $reRev -InitiatedBy $reBy
            if ($rc.Changed) { Write-Host ("  [SAFETY-RECORD] reclassified {0} cleanup-only safety event(s) as cleanup diagnostics (cleanup outcome kept, restoration not claimed): {1}; pre-image {2}; status {3}" -f @($rc.Events).Count, (@($rc.Events) -join ', '), $rc.Backup, $rc.State) -ForegroundColor Yellow }
            elseif (-not $rc.Ok) { Write-Host "  [SAFETY-RECORD] reclassification not applied: $($rc.Why)" -ForegroundColor Yellow }
        } else { Write-Note "Safety-record reclassification not attempted: source identity unavailable ($($SourceIdentity.Error))" }
    }
    $HilSafetyState = Read-AvbHilSafetyState -Path $HilSafetyPath
    Write-Note "Machine safety record: $($HilSafetyState.State) ($HilSafetyPath) $($HilSafetyState.Why)"
    $HilPhcTolerance = Get-AvbHilPhcTolerances -SafetyRecord $HilSafetyState.Record
    Write-Note ("PHC restoration tolerances (hardware-derived, approved baseline evidence): {0}" -f $(if ($HilPhcTolerance.Count) { ($HilPhcTolerance.Keys | Sort-Object) -join ', ' } else { 'none - a PHC judgement is INCONCLUSIVE_RESTORATION' }))
}

# One session comparison (start or end of a suite) against the ORIGINAL session baseline: every check stays a visible,
# recorded result.  Differences are cleanup FAILED observations (TC-INFRA-SESSION-001 start / -002 end) correlated with
# the session record's incidents; components the check could not read are cleanup NOT_VERIFIED (TC-INFRA-SESSION-003)
# and never hide or close an incident.  A fully observable comparison closes the incidents it no longer shows (RESOLVED /
# SUPERSEDED, kept as history).  Never a functional failure row, a halt or a machine-safety event (Q 2026-10-28).
function Invoke-HilSessionCheck {
    param($Cmp, [string]$Phase, $Last)
    $diffItems = @($Cmp.Items | Where-Object { $_.Kind -eq 'diff' }); $unobs = @($Cmp.Items | Where-Object { $_.Kind -eq 'unobservable' })
    $null = Add-AvbHilSessionCheck -Path $script:HilSessionPath -Check ([ordered]@{ utc = (Get-Date).ToUniversalTime().ToString('o'); suite = $Suite; configuration = $Configuration; phase = $Phase
                                                                                   diffs = @($diffItems | ForEach-Object { $_.Text }); unobservable = @($unobs | ForEach-Object { $_.Text }) })
    $tc = if ($Phase -eq 'start') { 'TC-INFRA-SESSION-001' } else { 'TC-INFRA-SESSION-002' }
    $disp = "HIL-SESSION[$Suite/$Configuration $Phase]"
    $where = "at $Phase of $Suite/$Configuration"
    if ($diffItems.Count -gt 0) {
        $refs = Register-HilIncidents -Items $diffItems -Check "session-$Phase" -Record $disp
        Add-AvbCleanupResult -Display $disp -Class 'session' -Required 'YES' -Outcome 'FAILED' -Testcase $tc -Subject $where -NoItem -Incidents $refs -Observations @($diffItems | ForEach-Object { $_.Text }) `
            -Reason ("state differs from the original session baseline ({0}, captured {1}): {2}" -f $script:HilSession.sessionId, $script:HilSession.createdUtc, (@($diffItems | ForEach-Object { $_.Text }) -join '; '))
    }
    if ($unobs.Count -gt 0) {
        Add-AvbCleanupResult -Display "$disp[UNVERIFIED]" -Class 'session' -Required 'UNKNOWN' -Outcome 'NOT_VERIFIED' -Testcase 'TC-INFRA-SESSION-003' -Subject $where -NoItem `
            -Observations @($unobs | ForEach-Object { $_.Text }) -Reason ("session check could not read: {0} --- open incidents stay open" -f (@($unobs | ForEach-Object { $_.Text }) -join '; '))
        return
    }
    $cl = Close-AvbHilSessionIncidents -Path $script:HilSessionPath -Items @($Cmp.Items) -Observation ([ordered]@{ utc = (Get-Date).ToUniversalTime().ToString('o'); check = "session-$Phase"; suite = $Suite; configuration = $Configuration })
    foreach ($c in @($cl.Closed)) {
        Write-Host ("  [CLEANUP:{0}] {1}: {2}: incident {3} {4} against the original session baseline ({5}); kept as history" -f $c.Status, $tc, $where, $c.IncidentId, $c.Status.ToLowerInvariant(), $c.Key) -ForegroundColor Green
    }
}
# Before every non-ReadOnly item: the lock must still be owned by this execution.
function Test-AvbHilLockStillOwned {
    if ($script:HilLockAcquired) { return (Test-AvbHilLockHeld -Owner $script:HilLockAcquired -Path $script:HilLockPath) }
    $c = Resolve-AvbHilLockContext -Path $script:HilLockPath -GitHub $script:HilGitHubContext
    return @{ Ok = $c.Owned; Why = $c.Why }
}

if ($IsHardwareSuite) {
    Invoke-HilBaseline
    if ($HilSafetyState.State -ne 'CLEAR') { Set-HilHalt "machine safety record $($HilSafetyState.State): $($HilSafetyState.Why)" }
    # A halt of an earlier suite of the same HIL job carries over: its damaged state must not become this run's
    # clean baseline for further mutation (P0 2026-10-21 sec. 2).  ReadOnly tests continue (same as an in-run halt).
    if ($PriorResultsDir) {
        $priorHalt = Get-AvbPriorHalt -Dir $PriorResultsDir -OwnResultsPath $ResultsJsonPath
        if ($priorHalt) { Set-HilHalt "prior suite: $priorHalt" }
    }
    # In-memory incident correlation, used only when no session record is available (Register-HilIncidents).
    $HilIncidents = [ordered]@{}
    # Original SESSION baseline (tools/lib/HilLock.ps1): read / compared / created only by a run that OWNS the lock.
    # Created only on a CLEAR machine without any halt; a different session's file is archived, never overwritten, and its
    # unresolved cleanup incidents are carried.  The session record is diagnostic evidence, never an authorization
    # mechanism (Q 2026-10-28): an unusable file is preserved and reported as cleanup NOT_VERIFIED, blocks nothing; a new record is
    # announced as captured from the current state - never as verified clean hardware.
    $HilSessionPath = if ($IsSandbox -and $env:AVB_TEST_HIL_SESSION_FILE) { $env:AVB_TEST_HIL_SESSION_FILE } elseif ($IsSandbox) { Join-Path $logsDir 'sandbox-hil-session.json' } else { Get-AvbHilSessionPath }
    $HilSession = $null; $HilSessionNote = ''
    $sessOwner = if ($HilLockAcquired) { $HilLockAcquired } elseif ($lockCtx.Owned) { $lockCtx.Owner } else { $null }
    if ($null -eq $sessOwner) {
        $HilSessionNote = 'no session baseline: this run does not own the machine lock (session baseline not read or written)'
    } else {
        $sid = Get-AvbHilSessionId -LockOwner $sessOwner -GitHub $HilGitHubContext
        $rs = Read-AvbHilSession -Path $HilSessionPath
        if ($rs.State -eq 'UNAVAILABLE') {
            # Infrastructure: session-level restoration cannot be verified in this run.  Reported, never "clean", never a
            # functional row or count (Q 2026-10-28, second correction; was a FAILED_SETUP result row).
            Add-AvbCleanupResult -Display "HIL-SESSION[$Suite/$Configuration start]" -Class 'session' -Required 'UNKNOWN' -Outcome 'NOT_VERIFIED' -Testcase 'TC-INFRA-SESSION-000' `
                -Subject "at start of $Suite/$Configuration" -NoItem -Observations @($rs.Why) `
                -Reason ("infrastructure: session baseline {0} unusable: {1} --- preserved unchanged; no session comparison and no persisted cleanup incidents in this run, so session-level restoration is not verified (never assumed clean)" -f $HilSessionPath, $rs.Why)
        } elseif ($rs.State -eq 'OK' -and $rs.Session.sessionId -eq $sid) {
            $HilSession = $rs.Session
            $cmpS = Compare-AvbHilState $HilSession.baseline $script:HilBaseline
            Invoke-HilSessionCheck -Cmp $cmpS -Phase 'start' -Last $script:HilBaseline
        } elseif ($HilSafetyState.State -eq 'CLEAR' -and -not $stateHalt -and $script:HilBaseline -and $script:HilBaseline.Ok) {
            $HilSession = New-AvbHilSession -Path $HilSessionPath -SessionId $sid -Owner $sessOwner -Baseline $script:HilBaseline `
                -Provenance ([ordered]@{ suite = $Suite; configuration = $Configuration; runner = 'tools/test/Run-Tests-CI.ps1'; pid = $PID; driverSha256 = $env:HIL_DRIVER_SHA256
                                         previousSession = $(if ($rs.State -eq 'OK') { $rs.Session.sessionId } else { $null }) })
            Write-Note "Original session baseline ($sid) at $HilSessionPath$(if ($rs.State -eq 'OK') { "; previous session $($rs.Session.sessionId) archived" }): session baseline captured from the current state --- not a verification that the hardware is clean"
            $carried = @($HilSession.cleanupIncidents)
            if ($carried.Count -gt 0) {
                Write-Host ("  [CLEANUP:OPEN] {0} unresolved cleanup incident(s) carried from session {1}: {2}" -f $carried.Count, $rs.Session.sessionId, (($carried | ForEach-Object { "$($_.incidentId) ($($_.key))" }) -join '; ')) -ForegroundColor Yellow
            }
        } else {
            $HilSessionNote = "no session baseline established: machine safety $($HilSafetyState.State)$(if ($stateHalt) { "; halt: $stateHalt" }) - a snapshot never establishes a clean state after an unresolved failure"
        }
    }
    if ($HilSessionNote) { Write-Note $HilSessionNote }

    # Left behind only if this process dies before Complete-AvbRun; a later suite then halts (Get-AvbPriorHalt).  Next to
    # the results document: hil.yml per-suite LogsDir, LocalSuites' report directory (its -PriorResultsDir).
    $RunMarkerPath = Join-Path (Split-Path -Parent $ResultsJsonPath) 'run-in-progress.json'
    ([ordered]@{ suite = $Suite; configuration = $Configuration; pid = $PID; startedUtc = $AvbRunStartedUtc } | ConvertTo-Json) |
        Set-Content -LiteralPath $RunMarkerPath -Encoding UTF8
}

$BuildManifest = $null
if ($IsHardwareSuite) {
    $BuildManifest = Read-AvbBuildManifest -Path (Join-Path $testExeDir 'test-build-manifest.json') -Configuration $Configuration
    if ($BuildManifest.Ok) {
        Write-Note ("Build provenance: {0} ({1} artifact record(s))" -f $BuildManifest.Path, $BuildManifest.ByArtifact.Count)
    } else {
        Write-Host "  [BLOCKED] build provenance unavailable: $($BuildManifest.Error) --- every non-repository test artifact is BLOCKED" -ForegroundColor Red
    }
    if (-not $SourceIdentity) { $SourceIdentity = Get-AvbSourceIdentity }   # already determined for the reclassification step
    if ($SourceIdentity.Ok) {
        Write-Note ("Source identity: {0} (tracked changes: {1}; provenance mode: {2}; build-input digest: {3})" -f $SourceIdentity.Revision, $SourceIdentity.Dirty,
                    $SourceIdentity.Mode, $(if ($SourceIdentity.Digest) { $SourceIdentity.Digest } else { "n/a $($SourceIdentity.DigestError)" }))
        if ($SourceIdentity.Mode -eq 'local' -and $SourceIdentity.Dirty) {
            Write-Host "  [LOCAL-EVIDENCE] uncommitted working tree: results are LOCAL pre-commit evidence bound to the build-input digest, NOT clean-commit CI evidence" -ForegroundColor Yellow
        }
    } else {
        Write-Host "  [BLOCKED] source identity unavailable: $($SourceIdentity.Error) --- every non-repository test artifact is BLOCKED" -ForegroundColor Red
    }
}

# Declared automatic-HIL coverage gaps of this suite (not with an explicit -Tests list).
$coverageGapTests = 0
if ($IsHardwareSuite -and $Tests.Count -eq 0) {
    $gapTable = $null
    if ($Suite -eq 'HardwareMandatory')       { $gapTable = $HardwareMandatoryCoverageGaps }
    elseif ($Suite -eq 'HardwareUnit')        { $gapTable = $HardwareUnitCoverageGaps }
    elseif ($Suite -eq 'HardwareIntegration') { $gapTable = $HardwareIntegrationCoverageGaps }
    if ($null -ne $gapTable) {
        $effGaps = Get-AvbHilEffectiveGaps -GapTable $gapTable -TestList $TestList
        foreach ($g in @($effGaps.Keys)) { Add-HilCoverageGap -Entry $g -Reason $effGaps[$g] }
    }
    # Attended-only mutation entries: NOT EXECUTED gap under GitHub Actions, executed on an attended run.
    if ($Suite -eq 'HardwareMutation') {
        foreach ($g in @($HardwareMutationAttendedOnly.Keys)) {
            if ($UnderGitHubActions) { Add-HilCoverageGap -Entry $g -Reason $HardwareMutationAttendedOnly[$g] }
            else { $TestList = @($TestList) + @($g) }
        }
    }
}

# Work items: a Mutating:Target entry becomes one item per exact target DID (AVB_HIL_TARGET_DID).
$WorkItems = @()
foreach ($t in $TestList) {
    if ($IsHardwareSuite -and (Get-AvbHilTestClass $t) -eq 'Mutating:Target' -and $HilTargets.Count -gt 0) {
        foreach ($d in $HilTargets) { $WorkItems += [pscustomobject]@{ Entry = $t; Target = $d } }
    } else {
        $WorkItems += [pscustomobject]@{ Entry = $t; Target = '' }
    }
}

Write-Step "Running $($TestList.Count) tests ($($WorkItems.Count) work items)"

$driverLost = $false   # set when driver becomes inaccessible mid-suite

foreach ($WorkItem in $WorkItems) {
    $TestName  = $WorkItem.Entry
    $targetDid = $WorkItem.Target
    $failFastStop = $false
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
        $exeName = Resolve-AvbHilArtifactName -BinName $binName -Manifest $BuildManifest
    }
    $exePath = if ($isPsTest) { $psScriptPath } else { Join-Path $testExeDir $exeName }
    $itemStem = $binName -replace '\.(exe|ps1)$',''
    if ($targetDid) {
        $exeName  = "$exeName@$targetDid"
        $itemStem = "$itemStem@$($targetDid.Substring(2))"
    }
    $script:AvbItem = @{ Entry = $TestName; Artifact = $exePath; Arguments = $testArgs; Target = $targetDid; Class = ''; Sha256 = $null
                         Executed = $false; ProcessExit = $null; LogPath = $null; StartedUtc = $null; DurationSeconds = $null; Output = $null }

    # MANDATORY SUITE: driver loss = hard FAIL for #328 sentinel, not cascade SKIP.
    if ($driverLost) {
        if ($IsMandatorySuite) {
            Write-Fail "$exeName [INFRA-FAIL] driver lost --- #328 sentinel: remaining mandatory tests FAIL"
            $failedTests++
            $totalTests++
            Add-AvbResult -Name $exeName -Status "FAILED(INFRA)" -Exit -1
        } else {
            Write-Skip "$exeName (INFRA: driver lost -- skipping to prevent cascade failures)"
            $skippedTests++
            Add-AvbResult -Name $exeName -Status "SKIPPED(INFRA)" -Exit "N/A"
        }
        continue
    }

    # A known hardware entry never runs outside a Hardware* suite: there is no baseline or gate there.
    if (-not $IsHardwareSuite -and $HilTestSafety.ContainsKey((ConvertTo-AvbHilEntryKey $TestName)) -and (($UnitTests + $IntegrationTests) -notcontains $TestName)) {
        Add-HilBlocked -Entry $TestName -Display $exeName -Stem $itemStem -Reason 'hardware test requested outside a Hardware* suite (no adapter baseline or safety gate)'
        continue
    }
    if ($IsSandbox -and -not (Test-HilSandboxFake $TestName)) {
        Add-HilBlocked -Entry $TestName -Display $exeName -Stem $itemStem -Reason 'sandbox mode: only fake ps1 tests outside the repository may execute'
        continue
    }

    if (-not (Test-Path $exePath)) {
        if ($IsMandatorySuite) {
            Write-Fail "$exeName [BUILD-FAIL] binary not found at $exePath --- mandatory test suite requires this binary"
            $failedTests++
            $totalTests++
            Add-AvbResult -Name $exeName -Status "FAILED(BUILD)" -Exit "N/A"
        } else {
            Write-Skip "$exeName (binary not found -- not built or not applicable)"
            $skippedTests++
            Add-AvbResult -Name $exeName -Status "SKIPPED" -Exit "N/A"
        }
        continue
    }

    # Build provenance gate (#328 F23): before the safety class and HALT rules, never by executing the artifact.
    if ($IsHardwareSuite -and (Test-AvbHilNeedsProvenance -Entry $TestName -ArtifactPath $exePath)) {
        $prov = Get-AvbHilProvenanceDecision -ArtifactPath $exePath -Manifest $BuildManifest -Source $SourceIdentity
        if ($prov.Action -ne 'Run') {
            Add-HilFailureRecord -Display $exeName -Status 'BLOCKED(PROVENANCE)' -Stem $itemStem `
                -Line ("[BLOCKED] TC-SAFETY-GATE-002: {0}: provenance: {1} --- not executed" -f $TestName, $prov.Reason)
            continue
        }
    }

    # HIL safety gate (#343): decided by the runner before the process is started.
    $hilClass = ''
    if ($IsHardwareSuite) {
        $hilClass = Get-AvbHilTestClass $TestName
        $script:AvbItem.Class = $hilClass
        if ($hilClass -ne 'ReadOnly' -and -not $stateHalt) {
            $still = Test-AvbHilLockStillOwned
            if (-not $still.Ok) { Set-HilHalt "machine lock lost during execution before ${exeName}: $($still.Why)" -Verdict 'LOCK_LOST' }
        }
        $gate = Get-AvbHilGateDecision -Entry (ConvertTo-AvbHilEntryKey $TestName) -Class $hilClass -Target $targetDid -Halt $stateHalt `
                    -TargetCount $HilTargets.Count -AuthorizedUnsafe $HilAuthorizedUnsafe -UnderGitHubActions $UnderGitHubActions `
                    -InitUnattendedApproved $HilInitUnattendedApproved -HazardHolds $HilHazardHolds
        if ($gate.Action -ne 'Run') {
            Add-HilBlocked -Entry $TestName -Display $exeName -Stem $itemStem -Reason $gate.Reason
            continue
        }
    }

    $totalTests++
    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $logStem = $itemStem
    $logFile = Join-Path $logsDir "${logStem}.exe_${timestamp}.log"

    Write-Host "`n  => $exeName$(if ($testArgs) { " $testArgs" } else { '' })$(if ($hilClass) { "  [$hilClass]" } else { '' })" -ForegroundColor Cyan

    # Pre-test state of a non-ReadOnly test: the PHC continuity reference for a verified no-mutation result (D1).
    $script:HilPreTest = if ($IsHardwareSuite -and $hilClass -ne 'ReadOnly' -and $null -ne $script:HilBaseline) { Get-AvbHilState -PhcReads $HilPhcReads } else { $null }

    # --- Lifecycle snapshot BEFORE ---
    $lcyBefore = if ($IsSandbox) { $null } else { Get-AvbLifecycleSnapshot }

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
    # Exact target only for Mutating:Target work items; never inherited by any other test.
    $psi.EnvironmentVariables.Remove('AVB_HIL_TARGET_DID')
    if ($targetDid) { $psi.EnvironmentVariables['AVB_HIL_TARGET_DID'] = $targetDid }
    $script:AvbItem.Sha256 = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
    $script:AvbItem.LogPath = $logFile
    $script:AvbItem.StartedUtc = (Get-Date).ToUniversalTime().ToString('o')
    $avbStopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $proc = [System.Diagnostics.Process]::Start($psi)
    $script:AvbItem.Executed = $true

    # Capture output in background threads, stream to console and log.
    $outLines = [System.Collections.Generic.List[string]]::new()
    $outJob = $proc.StandardOutput.ReadToEndAsync()
    $errJob = $proc.StandardError.ReadToEndAsync()

    $timedOut  = $false
    $itemTimeoutSeconds = Get-AvbTestTimeoutSeconds -Entry $TestName -SuiteSeconds $TestTimeoutSeconds
    if ($itemTimeoutSeconds -ne $TestTimeoutSeconds) { Write-Note "$exeName per-test timeout ${itemTimeoutSeconds}s (suite ${TestTimeoutSeconds}s)" }
    $timeoutMs = $itemTimeoutSeconds * 1000
    if (-not $proc.WaitForExit($timeoutMs)) {
        $script:AvbItem.Termination = Stop-AvbProcessTree -Process $proc
        $timedOut = $true
        Write-Fail "$exeName TIMEOUT after ${itemTimeoutSeconds}s --- $($script:AvbItem.Termination.Detail)"
    }
    $outText  = $outJob.Result
    $errText  = $errJob.Result
    $exitCode = if ($timedOut) { -999 } else { $proc.ExitCode }

    # Write captured output to log and display on console.
    $combined = if ($errText) { $outText + "`n" + $errText } else { $outText }
    $script:AvbItem.ProcessExit = $(if ($timedOut) { $null } else { $proc.ExitCode })
    $script:AvbItem.DurationSeconds = [Math]::Round($avbStopwatch.Elapsed.TotalSeconds, 3)
    $script:AvbItem.Output = $combined
    $combined | Out-File -FilePath $logFile -Encoding utf8
    $combined -split "`n" | ForEach-Object { Write-Host "    $_" }

    # --- Lifecycle snapshot AFTER + diff ---
    $lcyAfter = if ($IsSandbox) { $null } else { Get-AvbLifecycleSnapshot }
    $lcyAnomalies = Write-LifecycleDiff -Before $lcyBefore -After $lcyAfter -Label $TestName
    if ($lcyAnomalies) {
        Write-Host "  [WARN:LCY] Lifecycle anomaly detected (underflow/error/OID imbalance) -- see [LCY] lines above" -ForegroundColor Yellow
    }

    # Verdict gate: an executable exit code cannot express SKIP_NOT_IMPLEMENTED / INCONCLUSIVE,
    # so read them from the log (same vocabulary the JUnit converters use).
    $vc = Get-AvbVerdictCounts -Lines @($combined -split "`r?`n")

    if ($timedOut) {
        $failedTests++
        Add-AvbResult -Name $exeName -Status "FAILED(TIMEOUT)" -Exit -999
        $script:AvbItem.TimeoutRecord = $script:AvbResultRecords[-1]   # timeoutDisposition set by Invoke-HilPostTestCheck
    } elseif ($vc.FailedSetup -gt 0) {
        # Required initial conditions not established (any exit code): kept distinct from a test failure.
        $failedTests++
        Write-Fail "$exeName FAILED_SETUP ($($vc.FailedSetup) testcase(s)) --- required initial conditions not established"
        Add-AvbResult -Name $exeName -Status "FAILED_SETUP" -Exit $exitCode
    } elseif ($vc.InconclusiveRestoration -gt 0) {
        $failedTests++
        Write-Fail "$exeName INCONCLUSIVE_RESTORATION ($($vc.InconclusiveRestoration) testcase(s)) --- the test could not verify its own restoration"
        # The test's own testcase verdict (restoration asserted by the test itself) stays; it is no loss of control and
        # blocks no later test (Q 2026-10-28).
        Add-AvbResult -Name $exeName -Status "INCONCLUSIVE_RESTORATION" -Exit $exitCode
    } elseif (($exitCode -eq 0) -and -not $IsOptionalVerification -and $vc.Inconclusive -gt 0) {
        $failedTests++
        Write-Fail "$exeName INCONCLUSIVE ($($vc.Inconclusive) testcase(s)) --- insufficient evidence on mandatory verification"
        Add-AvbResult -Name $exeName -Status "FAILED(INCONCLUSIVE)" -Exit 0
    } elseif (($exitCode -eq 0) -and -not $IsOptionalVerification -and $vc.NotImplemented -gt 0) {
        $incompleteTests++
        Write-Fail "$exeName INCOMPLETE ($($vc.NotImplemented) testcase(s) SKIP_NOT_IMPLEMENTED) --- driver operation missing on mandatory verification"
        Add-AvbResult -Name $exeName -Status "INCOMPLETE(NOT_IMPLEMENTED)" -Exit 0
    } elseif ($exitCode -eq 0) {
        $passedTests++
        Write-Ok "$exeName PASSED"
        if ($vc.NotImplemented -gt 0 -or $vc.Inconclusive -gt 0) {
            Write-Note "$exeName (optional verification): $($vc.NotImplemented) SKIP_NOT_IMPLEMENTED, $($vc.Inconclusive) INCONCLUSIVE --- reported, not verified"
        }
        Add-AvbResult -Name $exeName -Status "PASSED" -Exit 0
    } elseif ($exitCode -eq 2 -and $IsMandatorySuite) {
        # Exit 2 = SKIP_ONLY from test_tx_timestamp_provenance (driver absent / all H tests skipped).
        # On a mandatory HIL suite the driver MUST be present --- SKIP_ONLY means
        # Section H never ran, so V1/V2 canary, cross-handle isolation, etc. were not exercised.
        Write-Fail "$exeName [HIL-FAIL] exit=2 SKIP_ONLY on mandatory suite --- driver not accessible or Section H not exercised"
        $failedTests++
        Add-AvbResult -Name $exeName -Status "FAILED(SKIP_ONLY)" -Exit $exitCode
    } elseif ($exitCode -eq 2) {
        # Exit 2 = SKIP_ONLY on a non-mandatory suite (Unit/Integration/HardwareUnit).
        # Section L passed; Section H skipped because driver is absent.
        # Expected on GitHub-hosted runners and any machine without the driver installed.
        $skippedTests++
        Write-Skip "$exeName (exit=2 SKIP_ONLY: Section L passed, Section H skipped -- driver absent)"
        Add-AvbResult -Name $exeName -Status "SKIPPED" -Exit $exitCode
    } elseif ($exitCode -eq 3) {
        # Exit 3 = no functional assertion failed, but BLOCKED and/or INCONCLUSIVE testcases (test_ioctl_tas,
        # test_ptp_freq_complete; tests/common/avb_test_common.h AVB_TEST_EXIT_BLOCKED).  Confirmed against the testcase
        # lines - a contradiction (a FAIL / INFRA_FAILURE line, or neither verdict) stays FAILED.  Always nonpassing.
        $failedTests++
        $x3 = "FAIL=$($vc.Fail) INFRA_FAILURE=$($vc.InfraFailure) BLOCKED=$($vc.Blocked) INCONCLUSIVE=$($vc.Inconclusive)"
        if ($vc.Fail -eq 0 -and $vc.InfraFailure -eq 0 -and $vc.Blocked -gt 0) {
            Write-Fail "$exeName BLOCKED ($($vc.Blocked) testcase(s) BLOCKED, $($vc.Inconclusive) INCONCLUSIVE, no functional FAIL) --- prerequisite or safety constraint prevented execution"
            Add-AvbResult -Name $exeName -Status "BLOCKED(TESTCASE)" -Exit $exitCode -Reason "exit 3 confirmed by the testcase lines ($x3)"
        } elseif ($vc.Fail -eq 0 -and $vc.InfraFailure -eq 0 -and $vc.Inconclusive -gt 0) {
            Write-Fail "$exeName INCONCLUSIVE ($($vc.Inconclusive) testcase(s), no functional FAIL) --- insufficient evidence on mandatory verification"
            Add-AvbResult -Name $exeName -Status "FAILED(INCONCLUSIVE)" -Exit $exitCode -Reason "exit 3 confirmed by the testcase lines ($x3)"
        } else {
            Write-Fail "$exeName FAILED (exit 3 claims no functional failure, contradicted by the testcase lines: $x3)"
            Add-AvbResult -Name $exeName -Status "FAILED" -Exit $exitCode -Reason "exit 3 (no functional FAIL claimed) contradicted by the testcase lines ($x3)"
        }
    } else {
        $failedTests++
        Write-Fail "$exeName FAILED (exit $(Format-AvbExitCode $exitCode))"
        Add-AvbResult -Name $exeName -Status "FAILED" -Exit $exitCode

        if ($FailFast) {
            Write-Note "-FailFast: stopping after first failure (after the health and state checks)"
            $failFastStop = $true
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
        # Write a synthetic log file so Convert-Logs-To-JUnit / Parse-TestResults
        # can emit a <failure> element for this infrastructure failure in the JUnit XML.
        # TC-INFRA-HEALTH-001 matches the TC-* pattern accepted by Parse-TestResults.
        # Written before the record and attached to it: the suite JUnit contains only record logs.
        $infraLogTs  = Get-Date -Format 'yyyyMMdd_HHmmss'
        $infraLogFile = Join-Path $logsDir ("TC-INFRA-HEALTH.exe_{0}.log" -f $infraLogTs)
        @"
[INFRA_FAILURE] TC-INFRA-HEALTH-001: $infraMsg
"@ | Set-Content $infraLogFile -Encoding utf8
        Add-AvbResult -Name "$exeName[INFRA-HEALTH]" -Status "FAILED(INFRA)" -Exit -2 -Check -LogPath $infraLogFile
    }

    # Post-test adapter/PHC state integrity (#343) --- also after a timeout kill.
    if ($IsHardwareSuite) {
        Invoke-HilPostTestCheck -Display $exeName -Class $hilClass -TimedOut $timedOut -Output $combined
    }
    if ($failFastStop) { break }
}
$script:AvbItem = $null

# End of suite: compare with the ORIGINAL session baseline (detects changes that escaped per-test cleanup).
if ($IsHardwareSuite -and $HilSession) {
    $endState = Get-AvbHilState
    $cmpE = Compare-AvbHilState $HilSession.baseline $endState
    Invoke-HilSessionCheck -Cmp $cmpE -Phase 'end' -Last $endState
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
if ($coverageGapTests -gt 0) {
    Write-Host "  Coverage gaps (required, NOT executed): $coverageGapTests" -ForegroundColor Red
}
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
    # Exactly the logs of THIS run's records (never every same-date log of the logs directory) and the run identity the
    # results document records, so every JUnit is attributable to one run, configuration, suite, revision and driver.
    $runLogs = @($script:AvbResultRecords | ForEach-Object { $_.logPath } | Where-Object { $_ } | Sort-Object -Unique)
    $srcId = $script:SourceIdentity
    $runIdentity = [ordered]@{
        run_started_utc = $script:AvbRunStartedUtc; host = $env:COMPUTERNAME
        source_revision = $(if ($srcId) { $srcId.Revision } else { '' }); source_digest = $(if ($srcId) { $srcId.Digest } else { '' })
        source_dirty = $(if ($srcId) { [string]$srcId.Dirty } else { '' })
        driver_sha256 = [string]$env:HIL_DRIVER_SHA256; github_run = $(if ($script:UnderGitHubActions) { "$env:GITHUB_RUN_ID/$env:GITHUB_RUN_ATTEMPT" } else { '' })
        results_json = [string]$script:ResultsJsonPath
    }
    $junitArgs = @{ LogsDir = $logsDir; RunDate = $runDateStr; OutputFile = $junitOut
                    Configuration = $Configuration; SuiteName = $Suite; LogFiles = $runLogs; RunIdentity = $runIdentity }
    if ($IsOptionalVerification) { $junitArgs.OptionalVerification = $true }
    Reset-AvbExitCode
    & $junitScript @junitArgs -ErrorAction SilentlyContinue
    if (-not (Test-AvbExitSuccess (Get-AvbExitCode))) {
        Write-Note "JUnit conversion returned non-zero (may be no logs yet -- skipped tests only)"
    }
    if (Test-Path $junitOut) {
        Write-Ok "JUnit XML: $junitOut"
        if ($ExportGitHubEnv) {
            "JUNIT_XML=$junitOut" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
        }
    }
}

$ov = Get-AvbRunOverall -IsHardwareSuite $IsHardwareSuite -Passed $passedTests -Failed $failedTests -Incomplete $incompleteTests -CoverageGaps $coverageGapTests
if ($failedTests -gt 0)      { Write-Fail "OVERALL: $failedTests test(s) FAILED" }
if ($incompleteTests -gt 0)  { Write-Fail "OVERALL: $incompleteTests test(s) INCOMPLETE (SKIP_NOT_IMPLEMENTED on mandatory verification)" }
if ($coverageGapTests -gt 0) { Write-Fail "OVERALL: $coverageGapTests required test(s) NOT EXECUTED (coverage gap) --- $Suite is INCOMPLETE" }
switch ($ov.Overall) {
    'NOT_VERIFIED' { Write-Fail "OVERALL: no hardware test PASSED in $Suite --- this is not hardware verification" }
    'NO_TESTS'     { Write-Note "No tests ran (all skipped -- check that Build-Tests.ps1 ran first)" }
    'PASSED'       { Write-Ok "OVERALL: All $passedTests test(s) PASSED" }
}
Complete-AvbRun -Code $ov.Code -Overall $ov.Overall
