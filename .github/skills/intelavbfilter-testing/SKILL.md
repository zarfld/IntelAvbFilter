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
| `tests/unit/hardware/test_hw_state.c` | TC-PCI-LAT-003 | Per-adapter latency sweep — runs on all adapters |
| `tests/unit/ioctl/test_ioctl_simple.c` | inline | "MULTI-ADAPTER: tests all enumerated adapters" |
| `tests/unit/ioctl/test_minimal_ioctl.c` | inline | Iterates all Intel adapters; SKIPs per-adapter on STATUS_NOT_SUPPORTED |
| `tests/integration/multi_adapter/test_multidev_adapter_enum.c` | TC-MULTIDEV-001.1 | Iterates all adapters |
| Phase 2.5 device-agnostic tests | `test_ptp_getset`, `test_hw_ts_ctrl`, … | Runner iterates all adapters; driver returns STATUS_NOT_SUPPORTED for unsupported NICs |

### Adapter-agnostic capability tests (the majority)

**The vast majority of tests in this repo are adapter-agnostic and adapter-family-agnostic.** They test *capabilities* or *functionalities* through the generic public IOCTL interface — they do not know or care which NIC family is installed.

- A capability test runs on **every adapter enumerated** by `IOCTL_AVB_ENUM_ADAPTERS`.
- It calls the relevant IOCTL and the driver determines whether the feature is supported.
- If the feature is not supported: driver returns `STATUS_NOT_SUPPORTED` → test emits `SKIP:` for that adapter.
- If the feature is supported: driver returns success or a specific error → test emits `PASS:` or `FAIL:`.

This is the **only** correct way to handle capability differences. The test never pre-filters by device ID. The HAL inside the driver is responsible for knowing what each NIC supports; tests must not duplicate that knowledge.

```c
// ✅ CORRECT — adapter-agnostic capability probe
DWORD bytes;
BOOL ok = DeviceIoControl(h, IOCTL_AVB_GET_CAPABILITIES, &req, sizeof(req),
                          &caps, sizeof(caps), &bytes, NULL);
if (!ok) {
    DWORD err = GetLastError();
    if (err == ERROR_NOT_SUPPORTED) {   // driver mapped STATUS_NOT_SUPPORTED here
        printf("  SKIP: TC-TAS-CAP-001/adapter:%u: TAS not supported "
               "(VID=0x%04X DID=0x%04X)\n", idx, req.vendor_id, req.device_id);
        continue;
    }
    printf("  FAIL: TC-TAS-CAP-001/adapter:%u: IOCTL_AVB_GET_CAPABILITIES failed "
           "(err=%lu)\n", idx, err);
    g_failed++; continue;
}
// feature is supported — run the full test
run_tas_test(h, idx, &caps);

// ❌ WRONG — test should never pre-filter by device ID
if (req.device_id == 0x125B || req.device_id == 0x15F2) {  // ← FORBIDDEN
    run_tas_test(h, idx, &caps);
} else {
    printf("  SKIP: TC-TAS-CAP-001/adapter:%u: not an i226/i225\n", idx);
}
```

**Why no device-ID filtering in tests:**

- Mixed configurations (i210 + i219 + i226 simultaneously) are a primary test scenario.
- Hardcoded IDs break when new NICs are added without updating every test.
- The driver's HAL already knows what is supported — tests calling the generic interface get the right answer automatically.
- Behaviour comparison across adapter types is only possible if every test exercises every present adapter through the same code path.

### Adapter-family-specific tests (the exception, not the rule)

A small number of tests (currently one per supported family: `avb_test_i210.exe`, `avb_test_i219.exe`, `avb_test_i226.exe`) validate family-specific internal behaviour that the generic interface does not expose. These are the exception.

- They MUST still iterate over **all adapters of their target family** — family-scoped ≠ single-adapter.
  - *If the system has two i226 ports, `avb_test_i226.exe` must run on both.*
- `AvbEnumerateAdapters()` loops until no more family members are found.
- If no adapter of that family is present → all TC cases SKIP (genuine N/A, not a failure).
- They complement, but do not replace, the adapter-agnostic tests that already covered the generic surface.

### Checklist when writing a new hardware test

- [ ] Test uses `IOCTL_AVB_ENUM_ADAPTERS` loop — does NOT hard-code `index = 0`
- [ ] **No device-ID or family checks inside the test** — capability discovery is done via IOCTL, not `if (device_id == 0x…)`
- [ ] Driver returns `STATUS_NOT_SUPPORTED` for unsupported features → test emits `SKIP:` and continues
- [ ] Any other IOCTL error emits `FAIL:` — never disguised as `SKIP:`
- [ ] Prerequisite failures emit `FAIL:`, not `SKIP:`
- [ ] Output includes `/adapter:N` suffix with VID/DID in each `PASS:`/`FAIL:`/`SKIP:` line
- [ ] Test declares `"MULTI-ADAPTER: tests all enumerated adapters"` in its header `printf`
- [ ] All-skip (no hardware at all) treated as skip, not failure at the binary level
- [ ] Any single-adapter failure sets non-zero exit code for the binary

---

## SKIP vs FAIL Semantics — Critical

> **Tests exist to make problems visible. Hiding a problem behind SKIP is a test defect.**

### The only valid reasons to SKIP

A test case may emit `SKIP:` **only** when:

1. **No Intel adapters are present in the system at all** — there is nothing to test against (binary-level skip only).
2. **The driver returned `STATUS_NOT_SUPPORTED`** for the IOCTL under test — the hardware genuinely does not implement the feature. The test must still have attempted the IOCTL; it must not pre-filter by device ID.

```c
// ✅ CORRECT — hardware lacks this feature
if (status == STATUS_NOT_SUPPORTED) {
    printf("  SKIP: TC-TAS-001/adapter:%u: TAS not supported "
           "(VID=0x%04X DID=0x%04X)\n", idx, req.vendor_id, req.device_id);
    continue;   // expected — skip is OK
}
```

### Everything else is FAIL

| Situation | Correct verdict | Rationale |
|---|---|---|
| Feature is supported but driver returns wrong value | `FAIL` | Implementation bug |
| `IOCTL_AVB_OPEN_ADAPTER` returns any error | `FAIL` | Driver/implementation problem |
| `CreateFileA("\\\\.\\IntelAvbFilter")` fails | `FAIL` | Driver not running — real problem |
| A prerequisite step (clock set, init, etc.) fails | `FAIL` | Prerequisite failure **is** a bug |
| Timestamp outside tolerance | `FAIL` | Correctness failure |
| Driver returns unrecognized error code | `FAIL` | Must handle all cases |

```c
// ❌ WRONG — disguising a real error as a skip
if (!DeviceIoControl(h, IOCTL_AVB_SET_TIME, ...)) {
    printf("  SKIP: TC-PTP-002/adapter:%u: IOCTL failed\n", idx);
}

// ✅ CORRECT — expose the error
if (!DeviceIoControl(h, IOCTL_AVB_SET_TIME, ...)) {
    printf("  FAIL: TC-PTP-002/adapter:%u: IOCTL_AVB_SET_TIME failed (err=%lu)\n",
           idx, GetLastError());
    return 1;
}
```

### Prerequisites not met → FAIL, not SKIP

If a prerequisite step fails (open adapter, enable feature, set clock), all dependent test cases in that adapter iteration must report `FAIL`. The prerequisite failure **is the bug**; hiding it with SKIP destroys the diagnostic signal.

```c
// ❌ WRONG
if (!setup_adapter(h, idx)) {
    printf("  SKIP: TC-ABC-001/adapter:%u: setup failed\n", idx);
    return;
}

// ✅ CORRECT
if (!setup_adapter(h, idx)) {
    printf("  FAIL: TC-ABC-001/adapter:%u: adapter setup failed "
           "— prerequisite error (err=%lu)\n", idx, GetLastError());
    g_failed++;
    return 1;
}
```

### Capability vs. correctness: the key distinction

| Scenario | Driver action | Test verdict |
|---|---|---|
| Feature **not supported** by this NIC family | Return `STATUS_NOT_SUPPORTED` | `SKIP` |
| Feature **supported**, but implementation is wrong | Return error or wrong data | `FAIL` |
| Feature **supported**, prerequisites not met | Return error (implementation must guard) | `FAIL` |
| Feature **supported** and works correctly | Return success + correct data | `PASS` |

The driver itself is responsible for returning `STATUS_NOT_SUPPORTED` for capabilities it does not have. If a supported feature's *prerequisite* (e.g., link up, adapter open) is not met, the driver must return a specific error — not `STATUS_NOT_SUPPORTED`. Tests must surface that error as `FAIL`.

### Decision tree

```
About to run TC-XXX on adapter N:
  ├─ No adapter of required family present in system?
  │    → SKIP  (genuine N/A — no hardware to test)
  │
  ├─ IOCTL returned STATUS_NOT_SUPPORTED?
  │    → SKIP  (feature not implemented in this NIC)
  │
  ├─ Any other IOCTL error / driver failure?
  │    → FAIL  (driver/implementation problem)
  │
  └─ IOCTL succeeded but result is wrong / out of tolerance?
       → FAIL  (correctness problem)
```

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
| Using `SKIP:` when an IOCTL returns an unexpected error | Hides an implementation bug | `SKIP:` is only valid for `STATUS_NOT_SUPPORTED` — anything else is `FAIL:` |
| Using `SKIP:` when a prerequisite step fails | Prerequisite failure is a bug, not N/A | `FAIL:` with the prerequisite error; do not hide it |
| Stopping family-specific test at first matching adapter | Misses second NIC of same family | Loop `AvbEnumerateAdapters()` to exhaustion, run on each adapter of the family |
| `if (device_id == 0x125B) run_feature_test()` — ID-gating in test code | Breaks on new NICs; duplicates HAL knowledge; prevents mixed-config comparison | Always call the IOCTL and let the driver return `STATUS_NOT_SUPPORTED`; never pre-filter by ID |
| Treating missing feature as `SKIP:` without driver confirmation | Test assumes what driver supports — assumption ≠ proof | Only `STATUS_NOT_SUPPORTED` from the driver earns a `SKIP:`; test must still call the IOCTL |
| Writing a "generic" test that only runs against one adapter type silently | Other adapters get no coverage for this feature | Enumerate all, probe via IOCTL, log per-adapter result; let the driver decide |
````
