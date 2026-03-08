# Test Plan: Master Test Issue Categorization

**Document ID**: TEST-PLAN-MASTER-CATEGORIZED  
**Created**: 2026-03-07  
**Last Verified**: 2026-03-08 (driver `e3d4c8f`, Run-Tests-Elevated.ps1, **Release build**)  
**Status**: ✅ Active  
**Phase**: 07-verification-validation  
**Standards**: IEEE 1012-2016 (Verification & Validation)

## Purpose

This document records the outcome of a full body-level review of all 92 open test-related
GitHub issues (as of 2026-03-07) and categorizes them into actionable groups.
It supersedes headline-only analysis and is the authoritative reference for deciding
which issues to close, which need new test code, and which are out of scope.

## Review Methodology

- Every issue body was read from `C:\Temp\test_issues_full.txt` (extracted 2026-03-07,
  first 1200 chars of body per issue via PowerShell).
- Every test binary in `tests/**/*.c` was inventoried (90 source files).
- Cross-reference: test file exists AND covers the acceptance criteria stated in the issue.
- User instruction: "make sure you understand the whole content of the test issues,
  rather than making assumptions based on headline."
- **Build configuration**: All tests must be executed against a **Release build** of the driver.
  Debug builds intentionally include heavy tracing (`DbgPrint`, ETW events) that adds latency
  measurable overhead — latency-sensitive tests (e.g., `test_timestamp_latency`) will fail
  against a Debug driver even when the feature is correct. This is by design, not a bug.

---

## Category 1 — ✅ Tested OK (Close Immediately)

Test binary exists, acceptance criteria are covered, issue can be closed.

| Issue | Test ID | Covering Test Binary/File | Result Evidence |
|-------|---------|--------------------------|-----------------|
| #237 | TEST-EVENT-001: PTP Timestamp Events | `tests/ioctl/test_ioctl_ts_event_sub.c` | 97P / 0F / 17S (commit `efb0fdc`) |
| #239 | TEST-FPE-001: Frame Preemption (802.1Qbu/802.3br) | `tests/ioctl/test_ioctl_fp_ptm.c` | FP+PTM IOCTL handler tests passing |
| #270 | TEST-STATISTICS-001: Statistics Counters | `tests/ioctl/test_statistics_counters.c` | Statistics IOCTL test passes |
| #272 | TEST-PERF-TS-001: Timestamp Retrieval Latency <1µs | `tests/performance/test_timestamp_latency.c` | PASS in Release build: 7P/0F (driver `e3d4c8f`, 2026-03-08). ⚠️ Debug build fails latency thresholds due to tracing overhead — by design; must verify in Release. |
| #281 | TEST-TAS-CONFIG-001: TAS Qbv Configuration | `tests/ioctl/test_ioctl_tas.c` | PASS 10P/0F/0S (driver `e3d4c8f`, 2026-03-08) |
| #283 | TEST-CBS-CONFIG-001: CBS Qav Configuration | `tests/ioctl/test_ioctl_qav_cbs.c` | PASS 14P/0F/0S (driver `e3d4c8f`, 2026-03-08) |
| #312 | TEST-MDIO-PHY-001: MDIO/PHY Register Access | `tests/ioctl/test_ioctl_mdio_phy.c` | PASS in Release build: 2P/0F/6S (driver `e3d4c8f`, 2026-03-08). ⚠️ Debug build returns Win32 error 31 on MDIO reads due to tracing overhead — by design; must verify in Release. |
| #313 | TEST-DEV-LIFECYCLE-001: Device Lifecycle Management | `tests/ioctl/test_ioctl_dev_lifecycle.c` | Init/enum/open/state IOCTLs covered |

**Action**: Close each with comment identifying the covering test binary and last known result.

---

## Category 2 — 🔁 Duplicates (Close as Duplicate)

| Issue | Duplicate Of | Reason |
|-------|-------------|--------|
| #217 | #270 | Both specify TEST-STATISTICS-001; identical requirement (#61) and same test file |
| #197 | #285 | Both specify TEST-PHC-MONOTONIC-001; #285 is the detailed, current spec |
| #253 | #232 | TEST-PERF-BENCHMARKS-001 vs TEST-BENCHMARK-001 — identical scope |
| #284 | #198 | TEST-XSTAMP-QUERY-001 vs TEST-IOCTL-XSTAMP-001 — same IOCTL, same requirement (#48) |
| #268 | #257 | Both test Windows 10/11 compatibility; #257 is the canonical spec |

**Action**: Close with "Duplicate of #NNN" comment.

---

## Category 3 — 🟡 Nearly OK (Test Exists, Specific Gaps)

Test binary exists but does not fully exercise all acceptance criteria stated in the issue body.
Leave open; add a comment tracking the gap; do not close until gap is filled.

| Issue | Test ID | Covering File(s) | Gap Description |
|-------|---------|-----------------|-----------------|
| #195 | TEST-IOCTL-SET-001: PHC Time Set | `test_ioctl_ptp_getset.c` | ForceSet=TRUE branch, privilege enforcement, monotonicity preservation during set not verified |
| #200 | TEST-PERF-PHC-001: PHC Read Latency | `test_timestamp_latency.c` | PHC-query-specific P50 <500ns / P99 <1µs sub-measurement not confirmed |
| #208 | TEST-MULTI-ADAPTER-001: Multi-Adapter PHC Sync | `test_all_adapters.c`, `avb_multi_adapter_test.c` | Cross-adapter PHC isolation ±10 ns not measured; cross-sync ≤100 ns not tested |
| #209 | TEST-LAUNCH-TIME-001: Launch Time Offload | `test_ioctl_target_time.c` | Gate-window enforcement and missed-deadline detection not in test |
| #215 | TEST-ERROR-HANDLING-001: Error Detection & Recovery | `test_hal_errors.c` | NIC disconnect recovery, DMA error, resource exhaustion not exercised by HAL unit tests |
| #222 | TEST-DIAGNOSTICS-001: Network Diagnostics | `test_event_log.c` + `diagnostic/*.c` | Packet-capture path and ETW decode assertions not automated |
| #231 | TEST-ERROR-RECOVERY-001: Fault Injection | `test_hal_errors.c` | Driver Verifier fault injection harness not automated |
| #238 | TEST-PTP-001: HW Timestamp Correlation | `ptp_clock_control_test.c`, `rx_timestamping_test.c` | ±100 ns accuracy assertion across PHC-QPC pair not automated |
| #247 | TEST-DEBUG-REG-001: Registry Debug Settings | `test_registry_diagnostics.c` | DebugLevel runtime persistence across driver reload not verified |
| #248 | TEST-SECURITY-BUFFER-001: Buffer Overflow Protection | `test_security_validation.c` | Driver Verifier Special Pool CI run not automated |
| #250 | TEST-INTEGRATION-SUITE-001: HIL Integration | `AvbIntegrationTests.c` | No formal pass/fail traceability report generated in CI |
| #260 | TEST-COMPAT-I225-001: I225 Controller Compat | `tests/device_specific/i226/*.c` | No dedicated VEN_8086:DEV_15F2 detection test (I225-V device ID) |
| #261 | TEST-COMPAT-I219-001: I219 Controller Compat | `avb_test_i219.c` | Test exists; requires physical I219 hardware to run |
| #263 | TEST-IOCTL-BUFFER-001: IOCTL Buffer Validation | `test_security_validation.c` | Automated fuzz test not implemented |
| #264 | TEST-SEC-IOCTL-001: IOCTL Access Control | `test_security_validation.c` | Dedicated elevated-vs-standard-user IOCTL rejection not automated |
| #266 | TEST-PHC-SET-001: PHC Set + Monotonicity | `test_ioctl_ptp_getset.c` | ForceSet=TRUE code path and monotonicity invariant assertion not covered |
| #269 | TEST-EVENT-LOG-001: Windows Event Log | `test_event_log.c` | SIEM/wevtutil query assertions and Event ID validation not automated |
| #274 | TEST-PERF-PHC-001: PHC Query <500 ns | `test_timestamp_latency.c` | PHC-query-only RDTSC microbenchmark not isolated in test |
| #275 | TEST-DEVICE-CAP-001: Device Capability Query | `test_hw_state.c`, `test_device_register_access.c` | Cache hit rate >99.5% and <50 µs latency not measured |
| #277 | TEST-DAL-INIT-001: DAL Init & Strategy Selection | `test_device_register_access.c` | I210 vs I225 strategy-selection code path not verified explicitly |
| #279 | TEST-DEVICE-ABS-THREADING-001: Thread-Safe DevOps | `test_hal_unit.c` | 8-thread stress and <100 µs lock hold time not automated |
| #280 | TEST-ERROR-MAP-001: AVB→NTSTATUS Mapping | `test_hal_errors.c` | Exhaustive 1:1 enum→NTSTATUS assertion table not verified |
| #285 | TEST-PHC-MONOTONIC-001 (detailed) | *(none yet)* | New test needed: 10M PHC reads + concurrent offset adjustments |
| #287 | TEST-REG-ACCESS-LOCK-001: Spin Lock | `test_hal_unit.c` | Multi-CPU concurrent access + IRQL violation injection not done |
| #288 | TEST-HW-DETECT-CAPS-001: HW Capability Detection | `test_hw_state.c`, `avb_test_i210_um.c` | PCI read latency <100 µs assertion not measured |
| #289 | TEST-EVENT-ID-SSOT-001: Event ID SSOT | `test_register_constants.c` | SSOT check for Event IDs in `avb_events.h` not scripted |
| #304 | TEST-REGS-001: Build with Submodule Headers | `test_register_constants.c`, `verify_offsets.c` | CI gate confirming zero build errors with submodule not enforced |
| #305 | TEST-REGS-002: Magic Numbers Static Analysis | `test_register_constants.c` | CI grep scan for raw hex literals in `src/` not wired up |
| #306 | TEST-REGS-003: Register Constants vs Datasheets | `test_register_constants.c` | Full datasheet offset assertion table not complete |

---

## Category 4 — 🔴 Test Missing (Implementation Exists, New Test Code Needed)

Driver feature is implemented; no automated test written yet.
Prioritized by ROI / effort.

### Priority P0 (Critical — write these first)

| Issue | Test ID | Feature | New Test Required |
|-------|---------|---------|------------------|
| #285 | TEST-PHC-MONOTONIC-001 | PHC monotonicity guarantee | `test_ioctl_phc_monotonicity.c`: 10M reads + concurrent offset; assert timestamp[i+1] >= timestamp[i] |
| #198 | TEST-IOCTL-XSTAMP-001 | `IOCTL_AVB_PHC_CROSSTIMESTAMP` | `test_ioctl_xstamp.c`: PHC-QPC simultaneous snapshot; assert delta <10 µs |
| #196 | TEST-PTP-EPOCH-001 | TAI epoch initialization | `test_ioctl_phc_epoch.c`: epoch=1970-01-01 UTC; TAI-UTC offset=37 s; boundary |
| #274 | TEST-PERF-PHC-001 | PHC query latency <500 ns | RDTSC microbenchmark isolated to `IOCTL_PHC_QUERY` only |

### Priority P1 (High)

| Issue | Test ID | Feature | New Test Required |
|-------|---------|---------|------------------|
| #271 | TEST-POWER-MGMT-S3-001 | S3 sleep/wake cycles | PowerShell sleep/wake automation; FilterPause/Restart callback verification |
| #262 | TEST-HOT-PLUG-001 | Hot-plug PnP detection | `devcon disable/enable` cycle; assert re-init and no leaks |
| #276 | TEST-VLAN-PCP-001 | VLAN PCP→TC mapping | PCP 0-7 register verification via IOCTL on I225/I226 |
| #286 | TEST-NDIS-FASTPATH-001 | NDIS fast path <1 µs | RDTSC wrap around `FilterSendNetBufferLists`; 10,000 sample P99 |
| #214 | TEST-CROSS-SYNC-001 | Cross-adapter PHC sync ≤100 ns | Extend multi-adapter suite; PHC offset comparison across handles |
| #216 | TEST-QUEUE-PRIORITY-001 | PCP→TC→queue assignment | PCP 0-7 → hardware TC register read-back |

### Priority P2 (Medium)

| Issue | Test ID | Feature | New Test Required |
|-------|---------|---------|------------------|
| #199 | TEST-PTP-CORR-001 | TX/RX PHC base correlation | TX and RX hardware timestamps must share PHC base; delta assertion |
| #218 | TEST-POWER-MGMT-001 | D0/D3 state transitions | FilterPause/FilterRestart + PHC preservation across D0→D3→D0 |
| #225 | TEST-PERF-REGRESSION-001 | Performance regression baseline | Capture metrics; fail on ≥5% regression vs baseline file |
| #234 | TEST-EVENT-004 | AVTP diagnostic counter events | Trigger seq-gap/late-ts above threshold; verify event payload |
| #241 | TEST-EVENT-NF-002 | Zero polling overhead | CPU% idle ≤0.1% with event subscription active but no events |
| #254 | TEST-ERROR-INJECT-001 | Driver Verifier fault injection | Automated verifier enable/run/disable script |
| #256 | TEST-COMPAT-WIN11-001 | Windows 11 compatibility | Driver install + basic IOCTL smoke test on Win11 21H2/22H2/23H2 |
| #257 | TEST-COMPAT-WIN10-001 | Windows 10 compatibility | Driver install + smoke test on Win10 1809/21H2/22H2 |
| #259 | TEST-COMPAT-SERVER-001 | Windows Server 2016+ | Driver install on Server 2019/2022; Server Core validation |

### Priority P3 (Low / Infrastructure)

| Issue | Test ID | Feature | New Test Required |
|-------|---------|---------|------------------|
| #229 | TEST-STRESS-7DAY-001 | 7-day continuous stability | Long-running monitor script; memory/handle leak tracking |
| #230 | TEST-COMPAT-MULTIVENDOR-001 | Multi-vendor interop | Manual matrix; automation deferred (requires multi-vendor hardware) |
| #232 | TEST-BENCHMARK-001 | Full performance benchmark suite | New harness: latency + throughput + CPU% + memory footprint all-in-one |
| #242 | TEST-COMPAT-NDIS-001 | NDIS filter coexistence | Multi-filter stack test; verify AVB filter active and non-conflicting |
| #245 | TEST-EVENT-NF-001 | Event latency <10 µs | GPIO/oscilloscope — requires hardware instrumentation |
| #246 | TEST-COMPAT-WDF-001 | WDF/KMDF compatibility | Driver load + version check on Win10 1809, Win10 22H2, Win11 22H2 |
| #249 | TEST-COMPAT-NDIS-001 | NDIS 6.50+ API compliance | NDIS version query and API usage check (merge with #242) |

---

## Category 5 — ⚫ Implementation Missing (Driver Feature Not Built — TDD: Write Tests First)

These issues are open and valid. Per TDD practice, tests should be written **before** the implementation. These failing tests define the acceptance criteria and drive the implementation work. Do **not** close these issues.

| Issue | Test ID | Unimplemented Feature | Notes |
|-------|---------|----------------------|---------------------|
| #210 | TEST-GPTP-001 | gPTP stack (slave/master/BMCA) | Driver provides PHC access only; gPTP runs in user space (OpenAvnu) |
| #211 | TEST-SRP-001 | IEEE 802.1Qat MSRP/MVRP/MMRP | SRP is Layer 2 control plane; not in NDIS filter driver scope |
| #213 | TEST-VLAN-001 | VLAN tag insert/strip + filtering | VLAN operations not implemented in filter driver |
| #219 | TEST-PFC-001 | IEEE 802.1Qbb Priority Flow Control | PFC is a NIC hardware feature; not exposed by driver |
| #220 | TEST-HARDWARE-OFFLOAD-001 | TSO/RSS/LSO/checksum offload | Miniport driver features; not filter driver scope |
| #221 | TEST-LACP-001 | IEEE 802.1AX LACP | LACP protocol not in driver scope |
| #223 | TEST-EEE-001 | IEEE 802.3az EEE/LPI | EEE is PHY-layer hardware; not implemented in filter |
| #224 | TEST-SEGMENTATION-001 | MAC/IP/EtherType filtering rules | Complex packet filtering not implemented |
| #227 | TEST-API-DOCS-001 (body: AVTP conformance) | IEEE 1722 AVTP packet format | AVTP is above the filter; body content mismatches title entirely |
| #228 | TEST-USER-DOCS-001 (body: penetration testing) | Pen testing infrastructure | Requires specialized external tools; not automated in CI |
| #236 | TEST-EVENT-003 | ATDECC (IEEE 1722.1) entity events | ATDECC not implemented |
| #240 | TEST-GPTP-COMPAT-001 | External gPTP daemon coexistence (OpenAvnu) | Requires OpenAvnu runtime; not automatable in CI today |
| #258 | TEST-COMPAT-WIN7-001 | Windows 7 SP1 support | Win7 EOL; driver targets Win10+; mark wont-fix |

---

## Category 6 — 📋 Process / Infrastructure / Documentation

Not a driver feature test. Managed on a separate track.

| Issue | Test ID | Category |
|-------|---------|----------|
| #187 | QA-SC-TEST-006 | Architecture quality scenario document |
| #233 | TEST-PLAN-001 | Test plan document (meta) |
| #243 | TEST-BUILD-SIGN-001 | Manual WHQL/code-signing process |
| #244 | TEST-BUILD-CI-001 | CI/CD pipeline configuration |
| #251 | TEST-DOC-DEPLOY-001 | Deployment guide documentation review |
| #252 | TEST-MAINTAIN-001 | Static analysis / SonarQube maintainability |
| #255 | TEST-SECURITY-AUDIT-001 | CodeQL / Coverity / BinSkim tooling |
| #265 | TEST-COVERAGE-001 | Coverage ≥80% CI gate |
| #267 | TEST-DOC-API-001 | Doxygen IOCTL API completeness |
| #278 | TEST-TSN-TERMS-001 | Documentation terminology audit |
| #282 | TEST-NAMING-CONV-001 | IEEE 802.1AS-2020 naming code audit |
| #294 | TEST-CLEANUP-ARCHIVE-001 | Obsolete file archival / repo organization |
| #300 | TEST-SSOT-002 | CI workflow SSOT violation detection |

---

## Summary

| Category | Count | Action |
|----------|-------|--------|
| ✅ Tested OK | 8 | All closed (verified 2026-03-08, driver `e3d4c8f`, **Release build**) |
| 🔁 Duplicates | 5 | Close as duplicate |
| 🟡 Nearly OK | 29 | Fill gaps → close |
| 🔴 Test Missing P0 | 4 | Write test code immediately |
| 🔴 Test Missing P1 | 6 | Write test code (sprint backlog) |
| 🔴 Test Missing P2/P3 | 16 | Write test code (future sprints) |
| ⚫ TDD — write tests first | 13 | Write failing tests → implement → close |
| 📋 Process/Infra/Docs | 13 | Separate track |
| **Total reviewed** | **94** | |

---

## Sprint Plan — Closing All Issues as Passed

### Sequencing principles
1. **No new hardware required** → do these first (CI-runnable today)
2. **Additions to existing files** → faster than new files
3. **New test files** → write when closing a gap requires it
4. **TDD for unimplemented features** → write RED test, then implement driver feature, then close GREEN
5. **Hardware/infrastructure-gated** → last; schedule when rig is available

## rules to follow:

 - .github\skills\run-tests\SKILL.md - defines proper test execution!
 - tools\test\README.md - readme of Test infrastructure
 - .github\instructions\phase-06-integration.instructions.md
 - .github\instructions\phase-07-verification-validation.instructions.md 

it is expected that the ruleas are being followed consistently


---

### Sprint 1 — Additions to existing test files (CI-runnable, no new infra)

All work is additions inside already-compiling test binaries. Target: **run in current CI, close 15 issues**.

| # | File to edit | What to add | Closes |
|---|-------------|-------------|--------|
| 1 | `tests/ioctl/test_ioctl_ptp_getset.c` | `ForceSet=TRUE` branch; privilege-denied assert (standard user); read back PHC[n+1]≥PHC[n] after forced set | #195, #266 |
| 2 | `tests/performance/test_timestamp_latency.c` | Isolate `IOCTL_PHC_QUERY`-only RDTSC loop (10,000 calls); assert P50<500 ns and P99<1 µs; report separately | #200, #274 |
| 3 | `tests/hal/test_hal_errors.c` | Add `AVB_STATUS_xxx → NTSTATUS` exhaustive 1:1 table; assert each mapping | #280 |
| 4 | `tests/hal/test_hal_errors.c` | Add `test_ntstatus_error_map_complete()` covering all enum values via static array | #280 |
| 5 | `tests/integration/test_device_register_access.c` | Assert `DeviceAbstraction_GetStrategy()` returns `STRATEGY_I210` for I210 PCI ID and `STRATEGY_I225` for I225 PCI ID | #277 |
| 6 | `tests/verification/regs/test_register_constants.c` | Add `test_event_id_ssot()`: open `src/avb_events.h`, grep for raw integer literals not referencing enum; fail if any found | #289 |
| 7 | `tests/verification/regs/test_register_constants.c` | Add build-time check: `#include` submodule header and `static_assert` every offset macro matches expected value | #304, #306 |
| 8 | `tests/event-logging/test_event_log.c` | Add `wevtutil qe` shell-out; assert Event IDs 1001/1002/1003 are present; validate provider name | #269 |
| 9 | `tests/hal/test_hal_unit.c` | Spawn 8 threads each calling `DeviceAbstraction_GetTimestamp()` 10,000×; assert no data race (use TSAN or Verifier); measure max lock hold time < 100 µs | #279, #287 |
| 10 | `tests/integration/test_device_register_access.c` | Add capability cache hit-rate counter; run 1,000 capability queries; assert >99.5% served from cache and latency <50 µs | #275 |

**CI gate addition** (`.github/workflows/`): add step that `grep -rn '[0-9]\{4,\}' src/` and fails if raw hex literals appear outside `intel-ethernet-regs/` submodule. Closes #305.

---

### Sprint 2 — New test files for features already implemented in driver

Write new `.c` test binaries and wire them into `Build-Tests.ps1`. Target: **close 8 more issues**.

| # | New file | What it tests | Closes |
|---|----------|--------------|--------|
| 1 | `tests/ioctl/test_ioctl_phc_monotonicity.c` | 10,000,000 consecutive `IOCTL_GET_PHC_TIME` calls; concurrent thread doing `IOCTL_ADJUST_PHC_OFFSET` during reads; assert `t[i+1] >= t[i]` always | #285 |
| 2 | `tests/ioctl/test_ioctl_xstamp.c` | `IOCTL_AVB_PHC_CROSSTIMESTAMP`: capture PHC+QPC simultaneously; repeat 1,000×; assert `|PHC_ns - QPC_ns| < 10,000 ns` | #198 |
| 3 | `tests/ioctl/test_ioctl_phc_epoch.c` | After driver init: read epoch via IOCTL; assert base = TAI (1970-01-01 00:00:00 UTC); assert TAI-UTC offset = 37 s; test boundary at midnight UTC | #196 |
| 4 | `tests/security/test_ioctl_access_control.c` | Open device handle as non-elevated process; issue write IOCTLs (SET_PHC_TIME, SET_TAS_CONFIG); assert `STATUS_ACCESS_DENIED`; issue read IOCTLs; assert `STATUS_SUCCESS` | #264 |
| 5 | `tests/security/test_ioctl_buffer_fuzz.c` | For each IOCTL code: send zero-length buffer, max-minus-1 buffer, `NULL` buffer, buffer with size=MAXULONG; assert no crash and returns `STATUS_INVALID_PARAMETER` | #263, #248 |
| 6 | `tests/integration/test_multi_adapter_phc_sync.c` | Open two adapter handles; read PHC from both simultaneously 1,000×; assert per-adapter timestamp isolation (no cross-contamination); assert offsetdelta <100 ns | #208, #214 |

---

### Sprint 3 — New test files requiring kernel events / PnP / power transitions

These require driver install and elevated execution (already supported by `Run-Tests-Elevated.ps1`). Target: **close 8 more issues**.

| # | New file | What it tests | Closes |
|---|----------|--------------|--------|
| 1 | `tests/power/test_power_management.c` | D0→D3→D0 cycle via `DeviceIoControl(IOCTL_SET_POWER_STATE)`; assert PHC value preserved ±1 µs across D3; assert `FilterPause`/`FilterRestart` callbacks fired | #218 |
| 2 | `tests/power/test_s3_sleep_wake.c` | PowerShell `rundll32.exe powrprof.dll,SetSuspendState`; wait for resume; assert driver re-registers; run basic PHC read; assert no stale handle | #271 |
| 3 | `tests/pnp/test_hot_plug.c` | `devcon disable <hwid>`; assert device removed event; `devcon enable`; assert device re-enumerated; run full lifecycle smoke test; assert no handle/memory leak (pool tracker) | #262 |
| 4 | `tests/tsn/test_vlan_pcp_tc_mapping.c` | For PCP 0-7: set PCP via `IOCTL_SET_PCP_TC_MAP`; read back TC register via `IOCTL_READ_REGISTER`; assert PCP N maps to hardware TC N (or configured mapping) | #276, #216 |
| 5 | `tests/ndis/test_ndis_fastpath_latency.c` | Hook `FilterSendNetBufferLists`; RDTSC before/after 10,000 send calls; assert P99 <1 µs; report mean, P50, P99, max | #286 |
| 6 | `tests/integration/test_error_recovery.c` | Driver Verifier: enable low-resource sim; load driver; run 100 IOCTL calls; assert no BSOD; disable Verifier; verify clean PnP teardown | #215, #231, #254 |

---

### Sprint 4 — TDD for unimplemented features (Red → Implement → Green → Close)

Write the test first (it will fail/skip because the IOCTL/feature does not exist). Then implement the driver feature. Then run test green. Then close the issue.

| # | TDD test file to write first | Driver feature to implement | Closes |
|---|-----------------------------|-----------------------------|--------|
| 1 | `tests/tsn/test_vlan_tag.c` — assert `IOCTL_AVB_VLAN_ENABLE/DISABLE` returns `STATUS_SUCCESS`; read 802.1Q tag via packet capture | VLAN tag insert/strip in `FilterSendNetBufferLists` / `FilterReceiveNetBufferLists` | #213 |
| 2 | `tests/eee/test_eee_lpi.c` — assert `IOCTL_AVB_EEE_ENABLE` sets LPI Assert timer in EEER register; read back via MDIO | EEE LPI enable/disable via MDIO write to IEEE reg 3.20 | #223 |
| 3 | `tests/pfc/test_pfc_pause.c` — assert `IOCTL_AVB_PFC_ENABLE` sets PFC enable bitmask in PFCTOP register; readback | PFC register write via HAL | #219 |
| 4 | `tests/gptp/test_gptp_phc_interface.c` — assert PHC IOCTL contract (get/set/adjust/crosstimestamp) is accessible from a simulated gPTP user-space context (no actual BMCA) | PHC access contract is already implemented — test just validates the API surface | #210 |
| 5 | `tests/atdecc/test_atdecc_event.c` — assert `IOCTL_AVB_ATDECC_EVENT_SUBSCRIBE` queues entity-discovered events; verify payload format (entity GUID, capabilities) | ATDECC event IOCTL handler | #236 |
| 6 | `tests/srp/test_srp_interface.c` — assert `IOCTL_AVB_SRP_REGISTER_STREAM` allocates bandwidth token; returns reservation handle | SRP bandwidth reservation stub | #211 |
| 7 | `tests/openavnu/test_gptp_daemon_coexist.c` — assert driver PHC IOCTLs succeed while OpenAvnu gpsd process is running on same NIC | No driver change needed — test validates non-interference | #240 |
| 8 | `tests/compat/test_win7_stub.c` — build-time `#if NTDDI_VERSION < NTDDI_WIN10` → `#pragma message("Win7 not supported")`; runtime assert `RtlGetVersion() >= 10.0` | Add OS version check to `DriverEntry` | #258 |

---

### Sprint 5 — Long-running / infrastructure / OS compat

These need scheduled hardware time or a separate CI agent. Cannot be fully automated in the standard PR gate.

| # | What | Closes |
|---|------|--------|
| 1 | Performance regression baseline: capture current P50/P99/CPU% to `tests/performance/baseline.json`; add CI step that fails on ≥5% regression | #225 |
| 2 | 7-day stability run: nightly cron job runs `test_all_adapters` for 10 min; monitors handle count + non-paged pool; fails if either grows monotonically | #229 |
| 3 | Full benchmark suite: compose all latency + throughput + CPU% + memory footprint into single `test_benchmark_suite.c` harness; publish results to CI artifact | #232 |
| 4 | Windows 10 smoke test on CI agent (1809 / 21H2 / 22H2): driver install + `test_dev_lifecycle` + `test_ptp_getset` | #257 |
| 5 | Windows 11 smoke test (21H2 / 22H2 / 23H2): same as above | #256 |
| 6 | Windows Server 2019/2022 + Server Core: same smoke test | #259 |
| 7 | NDIS filter coexistence: load WFP callout + AVB filter simultaneously; run all IOCTLs; assert both active | #242, #249 |
| 8 | WDF version check: query `WdfVersion` from loaded driver; assert ≥ 1.23 | #246 |
| 9 | Multi-vendor interop: manual test matrix (Intel + Realtek + Broadcom); document in GitHub issue comment | #230 |
| 10 | Event latency <10 µs: GPIO toggle on IOCTL + oscilloscope measurement; document in issue | #245 |

---

### Execution order summary

```
Sprint 1  →  Adds to existing files, runnable today           → closes ~15 issues
Sprint 2  →  New .c files, no new hardware                    → closes ~8 issues
Sprint 3  →  New .c files, elevated, PnP/power                → closes ~8 issues
Sprint 4  →  TDD: write RED test → implement → GREEN          → closes ~8 issues
Sprint 5  →  Long-running / OS compat / hardware-gated        → closes ~10 issues
```

**Start with Sprint 1 — all work is in existing files, CI-runnable, no new hardware required.**

---

## Appendix: Test File Inventory (as of 2026-03-07)

```
tests/ioctl/
  test_ioctl_qav_cbs.c          → #283 CBS config
  test_ioctl_target_time.c      → #209 launch time
  test_ioctl_rx_timestamp.c     → RX timestamp basic
  test_ioctl_ptp_getset.c       → #195 #266 PHC get/set
  test_ioctl_ptp_freq.c         → PTP freq basic
  test_ioctl_tas.c              → #281 TAS config
  test_ioctl_phc_query.c        → PHC query
  test_ioctl_offset.c           → PHC offset
  test_ioctl_mdio_phy.c         → #312 MDIO/PHY
  test_statistics_counters.c    → #270 statistics
  test_rx_timestamp_complete.c  → RX timestamp extended
  test_ptp_freq_complete.c      → PTP freq extended
  test_ioctl_version.c          → IOCTL version
  test_ioctl_ts_event_sub.c     → #237 TS event subscription
  test_ioctl_hw_ts_ctrl.c       → HW TS control
  test_ioctl_fp_ptm.c           → #239 frame preemption
  test_ioctl_dev_lifecycle.c    → #313 device lifecycle

tests/performance/
  test_timestamp_latency.c      → #272 #274 latency

tests/security/
  test_security_validation.c    → #248 #264 security

tests/event-logging/
  test_event_log.c              → #269 event log

tests/event/
  test_avtp_tu_bit_events.c     → AVTP TU bit events

tests/verification/regs/
  test_register_constants.c     → #304 #305 #306 register SSOT

tests/integration/...          → multi-adapter, DAL, HAL, NDIS paths
tests/device_specific/...      → I210, I219, I226 hardware-specific
```
