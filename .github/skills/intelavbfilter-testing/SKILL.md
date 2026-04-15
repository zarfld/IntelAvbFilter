````skill
---
name: intelavbfilter-testing
description: "Use when analyzing, planning, or interpreting testing in this NDIS kernel driver repo — test categories, which tests need Intel NIC hardware vs. run anywhere, C test output patterns (PASS:/FAIL:/[PASS]/[FAIL]), prerequisites for hardware tests, linking TC-IDs to GitHub requirement issues, or deciding what to run in CI vs. local dev."
---

# IntelAvbFilter Testing — Repo-Specific Reference

## When to Use

- Deciding which tests to run for a given change
- Asked to "make sure the tests cover this requirement" for this repo
- Reading or describing results from a `logs/*.log` output
- Wondering whether a test needs a physical Intel NIC or can run in CI
- Adding new tests and needing to know where they belong
- Linking a test failure to a GitHub requirement issue

**NOT for:** invocation mechanics (use the `run-tests` skill), DbgView capture setup (use the `DbgView` skill), IEEE traceability templates (use `tests.instructions.md`).

---

## Test Suite Taxonomy

The test suite is divided into phases executed in order by `-Quick` / `-Full`. Each phase targets a different quality concern.

| Phase | Mode | Hardware needed? | Tests |
|---|---|---|---|
| **-1: SSOT / Standards Compliance** | `-Quick`, `-Full` | No | `Test-SSOT-001..004-*.ps1` (PowerShell) |
| **0: Quick smoke** | `-Quick` | Yes — Intel NIC + driver | ~14 EXEs: `test_multidev_adapter_enum`, `test_ndis_send_path`, `ptp_clock_control_test`, `avb_diagnostic_test`, … |
| **Full integration** | `-Full` | Yes | All Quick tests + extended hardware/integration suites |
| **Single test** | `-TestName <name>.exe` | Depends on test | Any binary individually |

**CI / headless jobs** → `Run-Tests-CI.ps1 -Suite Unit` — hardware-independent tests only; hardware tests are skipped with `[SKIP]`.

---

## Hardware Requirement Map

### No hardware needed (safe in CI, on any Windows machine)

| Test / Script | TC-IDs | Verifies (GitHub issue) |
|---|---|---|
| `Test-SSOT-001-NoDuplicates.ps1` | TEST-SSOT-001 | #24 (REQ-NF-SSOT-001), ADR #123 → Issue #301 |
| `Test-SSOT-002-CI.ps1` | TEST-SSOT-002 | Issue #300 |
| `Test-SSOT-003-IncludePattern.ps1` | TEST-SSOT-003 | Issue #302 |
| `Test-SSOT-004-Completeness.ps1` | TEST-SSOT-004 | Issue #303 |
| `test_scripts_consolidate.exe` | TC-SCRIPTS-001..005 | Issue #27 / #292 |
| `test_cleanup_archive.exe` | TC-CLEANUP-ARCHIVE-* | Issue #293 / #294 |
| `quick_diagnostics.exe` | TC-DIAG-* | No hardware IOCTLs |

Run all SSOT tests standalone:
```powershell
.\tests\verification\ssot\Run-All-SSOT-Tests.ps1
```

### Hardware required — Intel NIC + driver installed

All tests that open `\\.\IntelAvbFilter` (IOCTL surface):

| Binary | NIC target | Notes |
|---|---|---|
| `avb_test_i210_um.exe` | i210 | Main AVB integration test |
| `avb_test_i219.exe` | i219 | Device-specific variant |
| `avb_test_i226.exe` | i226 | Device-specific variant |
| `ptp_clock_control_test.exe` | Any Intel | PTP set/get round-trip |
| `ptp_clock_control_production_test.exe` | Any Intel | Extended production scenarios |
| `tsauxc_toggle_test.exe` | Any Intel | TS Aux Clock enable/disable cycle |
| `test_multidev_adapter_enum.exe` | Any Intel | Multi-adapter enumeration |
| `test_ndis_send_path.exe` | Any Intel | Send-path smoke |
| `test_ndis_receive_path.exe` | Any Intel | Receive-path smoke |
| `test_tx_timestamp_retrieval.exe` | Any Intel | TX hardware timestamp validation |
| `test_hw_state_machine.exe` | Any Intel | Hardware state transitions |
| `test_lazy_initialization.exe` | Any Intel | Lazy-init correctness |
| `test_registry_diagnostics.exe` | Any Intel | Registry path & defaults |

---

## Multi-Adapter Rule (MANDATORY)

> **Adapter-dependent tests MUST run on ALL supported adapters present in the system — not just the first one found.**

### Why this rule exists

A test system may have two or more Intel NICs simultaneously (e.g., i210 + i219, i219 + i226, or two i226 ports). Running only on one adapter hides:

| Result pattern | Diagnosis |
|---|---|
| All adapters fail | General driver problem |
| One adapter fails, others pass | That NIC has a hardware defect |
| All adapters of type X fail, type Y passes | Adapter-specific software bug (wrong register map, caps flag, etc.) |

Without multi-adapter runs the test suite cannot produce that differentiation.

### How it is implemented

Tests must enumerate adapters via `IOCTL_AVB_ENUM_ADAPTERS` and iterate. The canonical pattern used in this repo:

```c
// 1. Open discovery handle (no adapter selected yet)
HANDLE disc = CreateFileA("\\\\.\\IntelAvbFilter", ...);

// 2. Enumerate all installed Intel adapters
for (UINT32 idx = 0; ; idx++) {
    AVB_ENUM_REQUEST req = { .index = idx };
    if (!DeviceIoControl(disc, IOCTL_AVB_ENUM_ADAPTERS, ...))
        break;  // no more adapters

    // 3. Open per-adapter handle and target it
    HANDLE h = CreateFileA("\\\\.\\IntelAvbFilter", ...);
    AVB_OPEN_REQUEST open = { .vendor_id = req.vendor_id,
                               .device_id = req.device_id,
                               .index = idx };
    DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER, ...);

    // 4. Run the test on this adapter
    run_my_test(h, idx);   // emit TC-XXX/adapter:N output lines
    CloseHandle(h);
}
```

**Do NOT** hard-code `index = 0` or stop after the first successful `OPEN_ADAPTER`.

### Per-adapter TC-ID output format

Emit one result line per adapter per test case:

```
  PASS: TC-PTP-GETSET-001/adapter:0: gPTP set/get roundtrip OK (i210, VID=0x8086 DID=0x1533)
  PASS: TC-PTP-GETSET-001/adapter:1: gPTP set/get roundtrip OK (i219, VID=0x8086 DID=0x15B8)
  FAIL: TC-PTP-GETSET-001/adapter:2: offset 5.3ms exceeds ±1ms  (i226, VID=0x8086 DID=0x125B)
```

Format: `<VERB>: <TC-ID>/adapter:<N>: <description> (<NIC model, VID, DID>)`

The `Run-Tests.ps1` JUnit XML parser understands the `/adapter:N` suffix:
- all adapters SKIPped → `<skipped/>` (genuine N/A)
- any adapter FAILed → `<failure/>` (CI-blocking)
- mixed skip/pass → PASS + PARTIAL COVERAGE note (soft warning)

### Tests that already implement this pattern

| Test file | TC-IDs | Notes |
|---|---|---|
| `tests/unit/hardware/test_hw_state.c` | TC-PCI-LAT-003 | "Per-adapter latency sweep" |
| `tests/unit/ioctl/test_ioctl_simple.c` | inline | "MULTI-ADAPTER: tests all enumerated adapters" |
| `tests/unit/ioctl/test_minimal_ioctl.c` | inline | "Tests all 6 Intel I226-V adapters" |
| `tests/integration/multi_adapter/test_multidev_adapter_enum.c` | TC-MULTIDEV-001.1 | Iterates all adapters |
| Phase 2.5 device-agnostic tests | `test_ptp_getset`, `test_hw_ts_ctrl`, … | Runner iterates adapters |

### Tests that are adapter-family specific (intentionally single-adapter)

Phase 5 (`avb_test_i210.exe`, `avb_test_i219.exe`, `avb_test_i226.exe`) uses
`AvbEnumerateAdapters()` internally to find *its* NIC and SKIPs if not present.
This is correct — they test family-specific capabilities and must stay
family-scoped. They complement, not replace, the device-agnostic multi-adapter runs.

### Checklist when writing a new adapter-dependent test

- [ ] Test uses `IOCTL_AVB_ENUM_ADAPTERS` loop — does NOT hard-code `index = 0`
- [ ] Output includes `/adapter:N` suffix with VID/DID in each `PASS:`/`FAIL:` line
- [ ] Test declares `"MULTI-ADAPTER: tests all enumerated adapters"` in its header `printf`
- [ ] All-skip is treated as skip (no hardware present), not as failure
- [ ] Any single-adapter failure sets a non-zero exit code for the binary

---

## Test Output Patterns

### C binary output (`printf` from C test macros)

```
  PASS: TC-AVB-001: gPTP set/get round-trip within tolerance
  PASS: TC-AVB-002: TX timestamp monotonic
  FAIL: TC-AVB-003: offset within ±1ms  (actual: 5.3ms)
  SKIP: TC-AVB-009: link-state toggle (AVB_ADAPTER_NAME not set)
```

Format: `  <VERB>: <TC-ID>: <description>`
Verbs: `PASS`, `FAIL`, `SKIP`

### PowerShell runner summary (written to `logs/`)

The `Run-Tests.ps1` wrapper aggregates binary output into:

```
[PASS] ptp_clock_control_test.exe
[FAIL] avb_test_i210_um.exe  (exit 1)
[SKIP] avb_test_i219.exe     (device not present)
```

And ends with:
```
====================================================================
 Test Summary
====================================================================
 Passed:  N tests
 Failed:  M tests
 Skipped: K tests
====================================================================
```

### ⚠️ Known edge case: exit code 0 ≠ all tests passed

Some binaries exit with code 0 even when individual `FAIL:` lines appear in their output.
`Run-Tests.ps1` detects this with its TC-level parser and elevates to `[FAIL]` in the summary.
**Always read the per-test section in the log, not just the summary exit code.**

---

## Prerequisites Checklist

Before running any hardware test:

- [ ] Driver installed: `Get-Service IntelAvbFilter` → Status=Running
- [ ] Device node accessible: `Test-Path '\\.\IntelAvbFilter'`
- [ ] Admin shell (or use `Run-Tests-Elevated.ps1` for auto-UAC)
- [ ] Intel NIC present and adapter enabled in Device Manager
- [ ] Driver built for target configuration (Debug/Release)

Optional but recommended:
- [ ] DbgView capturing kernel output (see `DbgView` skill; add `-CaptureDbgView` to `Run-Tests-Elevated.ps1`)

---

## Traceability: TC-IDs → GitHub Issues

### Pattern

Every TC-ID maps to a TEST issue in the GitHub repo:

```
TC-SSOT-001  ←→  GitHub issue #301  (verified by Test-SSOT-001-NoDuplicates.ps1)
TC-SSOT-002  ←→  GitHub issue #300
TC-SSOT-003  ←→  GitHub issue #302
TC-SSOT-004  ←→  GitHub issue #303
TC-SCRIPTS-001..005  ←→  Issue #292
TC-CLEANUP-ARCHIVE-*  ←→  Issues #293, #294
```

### How to find the issue for a failing TC-ID

1. Search GitHub issues: label `type:test` + TC-ID in the title
2. Test issues link to the requirement via `Traces to: #N`
3. Each test C file has a comment `// Verifies: #NNN` at the top

### When writing a new test

Follow `tests.instructions.md` — every test function must include:

```c
/* Verifies: #NNN (REQ-F-XXX-001: description)
 * Test Type: Integration
 * Priority: P1
 */
```

And create a corresponding TEST issue (`type:test` label) with `Traces to: #NNN` in the traceability section.

---

## Full Test Workflow (Local Dev)

```powershell
# 1. Build the test binary (skip if source unchanged)
.\tools\build\Build-Tests.ps1 -TestName avb_test_i210_um

# 2. Install/verify driver
.\tools\setup\Install-Driver-Elevated.ps1 -Configuration Debug -Action InstallDriver

# 3. Run test with DbgView capture (recommended)
.\tools\test\Run-Tests-Elevated.ps1 -TestName avb_test_i210_um -CaptureDbgView
# Output: logs\avb_test_i210_um_<ts>.log (transcript)
#         logs\dbgview_avb_test_i210_um_<ts>.log (kernel DbgPrint)

# 4. Read results
$log = Get-ChildItem logs -Filter "avb_test_i210_um*" |
       Sort-Object LastWriteTime -Descending | Select -First 1
Get-Content $log.FullName

# 5. Cross-reference any FAIL: TC-XXX lines with GitHub Issues
```

### CI workflow (no hardware)

```powershell
# GitHub Actions runner is Admin, no hardware present
.\tools\test\Run-Tests-CI.ps1 -Configuration Debug -Suite Unit
# Hardware tests are auto-skipped; SSOT tests run
```

---

## Common Mistakes

| Mistake | Why it fails | Fix |
|---|---|---|
| Trusting exit code 0 = all passed | Some binaries have the exit-0-on-fail bug | Always scan logs for `FAIL:` lines |
| Running `avb_test_i210_um.exe` in CI | Opens `\\.\IntelAvbFilter` → fails if driver absent | Use `Run-Tests-CI.ps1 -Suite Unit` |
| Skipping SSOT tests because "no hardware" | SSOT tests catch IOCTL definition drift — must run first | Always run Phase -1 before any hardware tests |
| Reporting "tests pass" from build success alone | Build ≠ test execution | Run tests, read the log, state actual counts |
| Adding a new test without a GitHub issue | Breaks traceability audit | Create TEST issue with `Traces to: #NNN` before or immediately after writing the test |
| Ignoring `[SKIP]` results from hardware tests in CI | Might mask a real failure on hw | Validate same run with hardware before claiming full coverage |
````
