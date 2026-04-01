# Test Plan: NDIS LWF Coverage Framework

**Document ID**: TEST-PLAN-NDIS-LWF-COVERAGE  
**Created**: 2026-03-25  
**Status**: 📋 Planned — implementation in progress  
**Phase**: 07-verification-validation  
**Standards**: IEEE 1012-2016, ISO/IEC/IEEE 12207:2017  
**Related Issues**: #265 (TEST-COVERAGE-001), #98 (REQ-NF-BUILD-CI-001)

---

## Background: Why "X% line coverage" is wrong for an NDIS LWF

OpenCppCoverage and similar user-mode instrumentation tools cannot cross the kernel boundary.
`IntelAvbFilter.sys` runs in kernel mode; `DeviceIoControl` is the boundary.
Coverage of the test harness `.exe` only measures the test code, not the driver implementation.

The correct coverage model for a kernel NDIS LWF is a combination of six pillars:

```
Coverage = static rule coverage
         + lifecycle/state coverage
         + datapath coverage
         + control-path coverage
         + dynamic fault coverage
         + HLK/NDISTest compliance coverage
```

---

## Coverage Pillars

### Pillar 1 — Static Rule Coverage

**Description**: PREfast/OACR (`/analyze`), SDV, CodeQL, DVL generation.  
SDV is meaningful for NDIS LWF when callbacks are declared with NDIS role types and
`Ndis.h`/`Ntddk.h` are included. Relevant SDV rulesets: default NDIS rules +
`Irql_Filter_Driver_Function` + registration/deregistration rules.

**What to measure**: Every implemented callback is visible to SDV; relevant NDIS rules run
clean or are explicitly triaged; DVL is clean for the release certification standard.

**Current State**:

| Gate | Status | Notes |
|---|---|---|
| MSVC `/analyze` (PREfast) in CI | ✅ Added | `static-analysis-prefast` job in `ci-standards-compliance.yml` (`/p:RunCodeAnalysis=true`, `continue-on-error: true` phase-in) |
| SDV run + DVL artifact | ❌ Not in CI | Manual only; no committed DVL file |
| CodeQL CI quality gate | ✅ Added | `analyze` job in `codeql-driver.yml` — MSDT `@development` branch, bundled CodeQL, uploads SARIF to GitHub Security tab; good for PR feedback, **NOT valid for WHCP DVL** |
| CodeQL WHCP certification job | ✅ Added 2026-04-01 | `analyze-whcp` job in `codeql-driver.yml` — pinned CodeQL 2.15.4 + MSDT WHCP_22H2 + `windows-drivers@1.0.13` + `cpp-queries@0.9.0`; runs `suites/windows_driver_recommended.qls` + `suites/windows_driver_mustfix.qls` (pack 1.0.13 naming); generates DVL via `msbuild /t:dvl`; triggers on `master`/`release/*` and `workflow_dispatch` |
| CodeQL on submodules | ✅ Partial | `external/windows_driver_samples` + `external/intel_mfd` have own workflows |

**WHCP CodeQL version matrix used** (Win11 22H2 target / WHCP_22H2 branch):  
| Component | Version | Why |
|---|---|---|
| CodeQL CLI | 2.15.4 | Required for WHCP_22H2 with VS 17.8+ |
| MSDT branch | `WHCP_22H2` | Must match target Windows version |
| `microsoft/windows-drivers` pack | 1.0.13 | Pinned per WHCP_22H2 matrix |
| `microsoft/cpp-queries` pack | 0.9.0 | Required for mustfix CWE checks (CWE-190, CWE-120, CWE-327, etc.) |
| Query suite | `suites/windows_driver_recommended.qls` | Superset of `windows_driver_mustfix.qls` + `windows_driver_mustrun.qls` (pack 1.0.13 naming under `suites/`) |
| Exit gate | `suites/windows_driver_mustfix.qls` violations → fail | Violations block WHCP certification |

**Acceptance Criteria**:
- [x] `msbuild /p:RunCodeAnalysis=true` CI job added (`static-analysis-prefast`); baseline findings surfaced — resolve before removing `continue-on-error`
- [ ] SDV run completes with no `Defect` findings; results committed to `test-evidence/sdv-results-*.xml`
- [x] `analyze` job: `codeql-driver.yml` CI quality gate using MSDT `codeql-config.yml@development` (PR feedback)
- [x] `analyze-whcp` job: WHCP-grade analysis added 2026-04-01; runs `suites/windows_driver_recommended.qls` + `suites/windows_driver_mustfix.qls` (pack 1.0.13) with pinned CLI + WHCP_22H2 packs; generates DVL
- [x] `suites/windows_driver_mustfix.qls` violations fail the `analyze-whcp` job (blocks WHCP cert on any blocking finding)
- [ ] DVL artifact (from `whcp-codeql-artifacts-N`) committed to `test-evidence/dvl-YYYYMMDD.DVL.XML` before each release cut
- [ ] `suites/windows_driver_mustfix.qls` zero violations confirmed on most recent `analyze-whcp` run

**CI Layer**: Per-PR / CI static gates (build-time, no hardware required).

---

### Pillar 2 — Lifecycle/State Coverage

**Description**: Prove each required NDIS LWF lifecycle callback ran in the real NDIS-driven
sequences. NDIS lifecycle constraints (from MSDN):
- `FilterAttach`: required; starts Attaching state; must NOT send/receive/OID/status from here
- `FilterRestart`: required; called from Paused state
- `FilterDetach`: required; called when module is Paused; runs at PASSIVE_LEVEL

**Instrumentation already in driver** (`IOCTL_AVB_GET_STATISTICS`):

| Counter | Struct field | Status |
|---|---|---|
| FilterAttach invocations | `AVB_DRIVER_STATISTICS::FilterAttachCount` | ✅ Exists |
| FilterPause invocations | `AVB_DRIVER_STATISTICS::FilterPauseCount` | ✅ Exists (ABI 2.0, `filter.c` line 688) |
| FilterRestart invocations | `AVB_DRIVER_STATISTICS::FilterRestartCount` | ✅ Exists (ABI 2.0, `filter.c` line 817) |
| FilterDetach invocations | `AVB_DRIVER_STATISTICS::FilterDetachCount` | ✅ Exists (ABI 2.0, `filter.c` line 1030) |
| Outstanding send NBLs at Pause | `AVB_DRIVER_STATISTICS::OutstandingSendNBLs` | ✅ Exists (ABI 2.0, per-NBL counting fixed 2026-03-27) |
| Outstanding receive NBLs at Pause | `AVB_DRIVER_STATISTICS::OutstandingReceiveNBLs` | ✅ Exists (ABI 2.0, uses `InterlockedAdd64` with NBL count) |
| Pause/Restart generation number | `AVB_DRIVER_STATISTICS::PauseRestartGeneration` | ✅ Exists (ABI 2.0, `filter.c` line 689) |

**Acceptance Criteria for each scenario**:
```
After install/bind:
  delta FilterAttachCount  == 1
  delta FilterRestartCount == 1

After disable-NIC/enable-NIC:
  delta FilterPauseCount   == 1
  delta FilterRestartCount >= 1

After uninstall/unbind:
  delta FilterPauseCount   == 1
  delta FilterDetachCount  == 1
  OutstandingSendNBLs   == 0  (invariant: must be zero at Detach)
  OutstandingReceiveNBLs == 0

After pause/restart under load:
  delta FilterPauseCount   == 1
  delta FilterRestartCount == 1
  OutstandingSendNBLs   == 0
  OutstandingReceiveNBLs == 0
  delta PauseRestartGeneration == 1
```

**CI Layer**: Automated runtime smoke tests (requires installed driver + NIC; local + lab).

---

### Pillar 3 — Datapath Coverage

**Description**: Prove send, send-completion, receive, and return paths were exercised,
including NDIS-documented edge conditions:
- `FilterReceiveNetBufferLists` may be invoked for loopback
- May run at IRQL ≤ DISPATCH_LEVEL  
- Behavior differs when `NDIS_RECEIVE_FLAGS_RESOURCES` is set
- If `FilterReturnNetBufferLists` not provided, cannot originate receive indications
- `FilterStatus` required alongside `FilterReturnNetBufferLists`

**Instrumentation in driver** (existing + gap):

| Counter | Struct field | Status |
|---|---|---|
| TX packet count | `AVB_DRIVER_STATISTICS::TxPackets` | ✅ Exists |
| RX packet count | `AVB_DRIVER_STATISTICS::RxPackets` | ✅ Exists |
| TX byte count | `AVB_DRIVER_STATISTICS::TxBytes` | ✅ Exists |
| RX byte count | `AVB_DRIVER_STATISTICS::RxBytes` | ✅ Exists |
| Outstanding send NBLs | `AVB_DRIVER_STATISTICS::OutstandingSendNBLs` | ✅ Exists (per-NBL counting fixed 2026-03-27) |
| Outstanding receive NBLs | `AVB_DRIVER_STATISTICS::OutstandingReceiveNBLs` | ✅ Exists (uses `InterlockedAdd64` with `NumberOfNetBufferLists`) |
| NBLs with NDIS_RECEIVE_FLAGS_RESOURCES | — | ❌ Missing |
| Packet disposition: pass-through | — | ❌ Missing |
| Packet disposition: modified | — | ❌ Missing |
| Packet disposition: dropped | — | ❌ Missing |
| Packet disposition: cloned/copied | — | ❌ Missing |
| Loopback NBLs received | — | ❌ Missing |

**Scenarios to prove**:
- Pass-through traffic (no AVB filtering active)
- AVB-filtered traffic (packet modified/tagged)
- Loopback traffic
- `NDIS_RECEIVE_FLAGS_RESOURCES`-flagged receive (no copy allowed — must return immediately)
- Teardown under load: zero outstanding NBLs at Pause completion

**CI Layer**: Automated runtime + lab hardware tests.

---

### Pillar 4 — Control-Path Coverage

**Description**: OID requests, OID completions, direct OID requests, status indications,
and NetPnP/power events.

**Callbacks registered** in `src/filter.c`:
- `FChars.OidRequestHandler = FilterOidRequest`
- `FChars.OidRequestCompleteHandler = FilterOidRequestComplete`
- `FChars.NetPnPEventHandler = FilterNetPnPEvent`
- `FChars.StatusHandler = FilterStatus`

**Instrumentation in driver** (existing + gap):

| Counter | Struct field | Status |
|---|---|---|
| Total IOCTL dispatch calls | `AVB_DRIVER_STATISTICS::IoctlCount` | ✅ Exists |
| IOCTL errors | `AVB_DRIVER_STATISTICS::ErrorCount` | ✅ Exists |
| OID requests dispatched | `AVB_DRIVER_STATISTICS::OidRequestCount` | ✅ Exists (ABI 2.0, `filter.c` line 1176) |
| OID completions | `AVB_DRIVER_STATISTICS::OidCompleteCount` | ✅ Exists (ABI 2.0, `filter.c` line 1376) |
| Outstanding OID count | `AVB_DRIVER_STATISTICS::OutstandingOids` | ✅ Exists (ABI 2.0, inc/dec paired) |
| FilterStatus indications | `AVB_DRIVER_STATISTICS::FilterStatusCount` | ✅ Exists (ABI 2.0, `filter.c` line 1477) |
| FilterNetPnPEvent calls | `AVB_DRIVER_STATISTICS::FilterNetPnPCount` | ✅ Exists (ABI 2.0, `filter.c` line 1616) |

**Scenarios to prove** (from MSDN direct-OID documentation):
- OID success path
- OID pending path (NDIS_STATUS_PENDING + completion callback)
- OID invalid / not-supported
- OID buffer-too-short
- OID resources failure
- Generic OID failure
- NetPnPEvent: pause/restart notification
- NetPnPEvent: power state change (D0 ↔ D3)
- Status indication: link up / link down passthrough

**Additional acceptance criterion**: `OutstandingOIDs == 0` at Pause completion.

**CI Layer**: Automated runtime smoke tests.

---

### Pillar 5 — Dynamic Fault Coverage (Driver Verifier)

**Description**: Driver Verifier provides runtime fault injection and consistency checking.
Microsoft recommends using it throughout development, on test machines only, with a kernel
debugger attached. When DV catches a violation it deliberately bugchecks; analyze with
`!analyze -v` and `!verifier`.

For NDIS-specific runtime visibility:
- `!ndiskd.filterdriver` — shows registered filter-driver info
- `!ndiskd.filter` — shows active LWF instances

**Evidence to commit** after each DV run:
- DV settings used (enabled checks bitmask)
- Scenario exercised (e.g., bind/unbind under load, pause/restart, traffic stress)
- Result: clean (no bugcheck) or defect found (attach dump excerpt)

**Committed to**: `test-evidence/dv-run-YYYYMMDD.md` (manual; no CI automation possible —
GitHub-hosted runners cannot run kernel mode code)

**Acceptance Criteria**:
- [ ] At least one DV run per release cut with `Special Pool`, `Pool Tracking`, `Force IRQL Checking`, `I/O Verification` enabled
- [ ] DV run covers: install, full traffic pass, pause/restart, uninstall
- [ ] Zero bugchecks attributed to `IntelAvbFilter.sys`
- [ ] Evidence file committed before release

**CI Layer**: Lab only — local evidence committed to `test-evidence/`.

---

### Pillar 6 — HLK / NDISTest Compliance Coverage

**Description**: HLK content for LWF includes **NDISTest 6.5 - LWF Logo** test, which
validates lightweight-filter requirements and NDIS specification compliance using NDISTest
virtual miniports.

Per Microsoft LAN test prerequisites:
- Requires specific adapter-topology setup
- Run all tests listed for the LWF driver in HLK Studio

**Acceptance Criteria**:
- [ ] NDISTest 6.5 - LWF Logo: PASS on target hardware
- [ ] HLK results package committed to `test-evidence/hlk-results-YYYYMMDD/`
- [ ] Any FAIL items triaged with documented root cause or waiver

**CI Layer**: Lab only.

---

## Implementation Roadmap

### Step 1 — Extend `AVB_DRIVER_STATISTICS` (ABI bump required)

**Files**: `include/avb_ioctl.h`, `src/filter.c`, `tests/unit/abi/test_ioctl_abi.c`

New fields to add to `_AVB_DRIVER_STATISTICS`:

```c
avb_u64 FilterPauseCount;          /* FilterPause invocations               */
avb_u64 FilterRestartCount;        /* FilterRestart invocations             */
avb_u64 FilterDetachCount;         /* FilterDetach invocations              */
avb_u64 OutstandingSendNBLs;       /* Send NBLs currently in flight         */
avb_u64 OutstandingReceiveNBLs;    /* Receive NBLs currently in flight      */
avb_u64 OidRequestCount;           /* FilterOidRequest invocations          */
avb_u64 OidCompleteCount;          /* FilterOidRequestComplete invocations  */
avb_u64 OutstandingOids;           /* OID requests currently pending        */
avb_u64 FilterStatusCount;         /* FilterStatus indications passed       */
avb_u64 FilterNetPnPCount;         /* FilterNetPnPEvent invocations         */
avb_u64 PauseRestartGeneration;    /* Monotonically increasing per cycle    */
```

Struct grows from 13 × 8 = 104 bytes → 24 × 8 = 192 bytes.  
**TC-ABI-018** must be added: `sizeof(AVB_DRIVER_STATISTICS) == 192`.

### Step 2 — Increment counters in `filter.c`

Increment entry counters at the top of each callback.  
Use `InterlockedDecrement` for Outstanding counters at completion/return paths.  
Increment `PauseRestartGeneration` at `FilterPause` entry.

### Step 3 — Add scenario assertions in hardware tests

After each scenario, call `IOCTL_AVB_GET_STATISTICS` twice (before + after), compute deltas,
assert against expected values documented in Pillars 2–4 above.

Recommended new test: `tests/hardware/test_lifecycle_coverage.c`  
Scenarios: install → traffic → pause → restart → traffic → uninstall.

### Step 4 — Enable static gates in CI

- Enable `/analyze` via separate `code-analysis` MSBuild target (WDK supports this)
- Add `codeql.yml` for main driver source using `microsoft/windows-driver-developer-supplemental-tools`
- Route PREfast XML output to `test-evidence/pca-results-*.sarif`

### Step 5 — Commit local DV and HLK evidence

- After each DV run: commit `test-evidence/dv-run-YYYYMMDD.md`
- After HLK run: commit `test-evidence/hlk-results-YYYYMMDD/` package summary

---

## CI Pipeline Layers

| Layer | Trigger | Tools | Blocking |
|---|---|---|---|
| 1 — Static gates | Per PR | MSVC `/analyze`, CodeQL, DVL check | ✅ Yes |
| 2 — Unit tests | Per PR | Build + run CI-safe test binaries | ✅ Yes |
| 3 — Smoke runtime | Nightly | Install, bind, traffic, pause, uninstall + Verifier | ❌ Evidence only |
| 4 — Lab / HLK | Per release | NDISTest 6.5 LWF Logo, sustained load | ❌ Evidence only |

---

## Engineering Bar

Coverage is **acceptable** only when all of the following are true:

1. ✅ Every implemented callback has been hit at least once in a traceable test run (counter > 0 in evidence)
2. ✅ Every implemented callback has been hit in both normal and at least one error/teardown path where applicable
3. ✅ All lifecycle transitions exercised: install, bind, unbind, disable/enable NIC, reboot, pause/restart, detach
4. ✅ Packet-path logic exercised under real load (not just single ping or one synthetic send)
5. ✅ Driver Verifier run clean for enabled checks
6. ✅ SDV/CodeQL/Code Analysis findings fixed or triaged; DVL clean for release standard
7. ✅ HLK NDISTest 6.5 - LWF Logo: PASS

---

## Traceability

- Traces to: #265 (TEST-COVERAGE-001)
- Traces to: #98 (REQ-NF-BUILD-CI-001)
- Related to: `test-evidence/hardware-results-*.xml` (Pillar 5 local evidence)
- Related to: `07-verification-validation/test-plans/TEST-PLAN-MASTER-CATEGORIZED.md`
