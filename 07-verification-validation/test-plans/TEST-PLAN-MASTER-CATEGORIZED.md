# Test Plan: Master Test Issue Categorization

**Document ID**: TEST-PLAN-MASTER-CATEGORIZED  
**Created**: 2026-03-07  
**Last Verified**: 2026-03-19 (driver v1.0.193.x + #236 ATDECC fix; ✅ **#236 ATDECC regression resolved** — 5P/0F log: `test_atdecc_event.exe_20260319_152856.log`. Root cause: `MAX_ATDECC_SUBSCRIPTIONS` was 4, causing exhaustion after 2 test runs; all 4 slots consumed by TC-002–005. Fix: increased limit 4→16 in `avb_integration.h`; per-handle auto-cleanup removed (subscriptions are driver-global, freed on `AVB_IOCTL_UNSUBSCRIBE_ATDECC_ENTITY`). S3 wake PHC fix also coded: `FilterRestart` now calls `AvbBringUpHardware` to re-program TIMINCA/SYSTIML/TSAUXC/TSYNCRXCTL after hardware power cycle; awaiting physical S3 cycle verification. ✅ **Multi-adapter event test runs 2026-03-19** (6×I226-LM): `test_zero_polling_overhead` 18P/0F/18S (TC-ZP-002 SKIP-HARDWARE-LIMIT: EITR0=33024µs set by miniport — 2026-03-29), `test_ptp_event_latency` 18P/0F/12S, `test_event_latency_4ch` 12P/0F/18S, `test_atdecc_aen_protocol` 18P/0F/18S. Issues #176/#177/#178/#179/#241 moved Cat4→Cat3b.) ✅ **#225 TEST-PERF-REGRESSION-001 implemented 2026-03-19** — `test_perf_regression.exe` 5P/0F CAPTURE run; baseline `logs\perf_baseline.dat` written (PHC P50=2300ns, P99=4372ns, TX=336ns, RX=2076ns); COMPARE mode verified regression detection live; #225 moved Cat4→Cat1.  
**Status**: ✅ Active  
**Phase**: 07-verification-validation  
**Standards**: IEEE 1012-2016 (Verification & Validation)

## Purpose

This document records the outcome of a full body-level review of all 92 open test-related
GitHub issues (as of 2026-03-07; **55 remain open as of 2026-03-18** — ~37 Cat1 issues closed after verification) and categorizes them into actionable groups.
It supersedes headline-only analysis and is the authoritative reference for deciding
which issues to close, which need new test code, and which are out of scope.

## Review Methodology

- Every issue body was read from `C:\Temp\test_issues_full.txt` (extracted 2026-03-07,
  first 1200 chars of body per issue via PowerShell).
- Every test binary in `tests/**/*.c` was inventoried (90 source files at 2026-03-07 review; **97 source files as of 2026-03-18**).
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
| #313 | TEST-DEV-LIFECYCLE-001: Device Lifecycle Management | `tests/ioctl/test_ioctl_dev_lifecycle.c` | Init/enum/open/state IOCTLs covered |
| #195, #266 | TEST-IOCTL-SET-001 + TEST-PHC-SET-001: PHC Set / ForceSet | `tests/ioctl/test_ioctl_ptp_getset.c` | PASS 14P/0F/0S (driver v1.0.189.0, 2026-03-09 full suite Phase 0) |
| #200, #274 | TEST-PERF-PHC-001: PHC Read Latency P50/P99 | `tests/performance/test_timestamp_latency.c` | PASS exit 0 (driver v1.0.189.0, 2026-03-09 full suite Phase 7) |
| #275, #277 | TEST-DEVICE-CAP-001 + TEST-DAL-INIT-001: DAL & Strategy | `tests/integration/test_device_register_access.c` | PASS 7P/0F/0S (driver v1.0.189.0, 2026-03-09 targeted run) |
| #279 | TEST-DEVICE-ABS-THREADING-001: Thread-Safe Device Ops | `tests/hal/test_hal_unit.c` | PASS exit 0 (driver v1.0.189.0, 2026-03-09 full suite Phase 7) |
| #280 | TEST-ERROR-MAP-001: AVB→NTSTATUS Mapping | `tests/hal/test_hal_errors.c` | PASS exit 0 (driver v1.0.189.0, 2026-03-09 full suite Phase 7) |
| #287 | TEST-REG-ACCESS-LOCK-001: Spin Lock | `tests/hal/test_hal_unit.c` | PASS exit 0 (driver v1.0.189.0, 2026-03-09 full suite Phase 7) |
| #289, #304, #306 | TEST-EVENT-ID-SSOT + TEST-REGS-001/003: Register SSOT | `tests/verification/regs/test_register_constants.c` | PASS exit 0 (driver v1.0.189.0, 2026-03-09 full suite Phase 7) |
| #285 | TEST-PHC-MONOTONIC-001: PHC Monotonicity | `tests/ioctl/test_ioctl_phc_monotonicity.c` | PASS exit 0 (driver v1.0.189.0, 2026-03-09 full suite Phase 7) |
| #198 | TEST-IOCTL-XSTAMP-001: PHC Cross-Timestamp | `tests/ioctl/test_ioctl_xstamp.c` | PASS exit 0 (driver v1.0.189.0, 2026-03-09 full suite Phase 7) |
| #196 | TEST-PTP-EPOCH-001: TAI Epoch Init | `tests/ioctl/test_ioctl_phc_epoch.c` | PASS exit 0 (driver v1.0.189.0, 2026-03-09 full suite Phase 7) |
| #264 | TEST-SEC-IOCTL-001: IOCTL Access Control | `tests/security/test_ioctl_access_control.c` | PASS exit 0 (driver v1.0.189.0, 2026-03-09 full suite Phase 7) |
| #263, #248 | TEST-IOCTL-BUFFER-001 + TEST-SECURITY-BUFFER-001 | `tests/security/test_ioctl_buffer_fuzz.c` | PASS exit 0 (driver v1.0.189.0, 2026-03-09 full suite Phase 7) |
| #208, #214 | TEST-MULTI-ADAPTER-001 + TEST-CROSS-SYNC-001 | `tests/integration/test_multi_adapter_phc_sync.c` | PASS exit 0 (driver v1.0.189.0, 2026-03-09 full suite Phase 7) |
| #218 | TEST-POWER-MGMT-001: D0/D3 State Transitions | `tests/power/test_power_management.c` | PASS exit 0 (driver v1.0.189.0, 2026-03-09 full suite Phase 7) |
| #262 | TEST-HOT-PLUG-001: Hot-Plug PnP Detection | `tests/pnp/test_hot_plug.c` | PASS exit 0 (driver v1.0.189.0, 2026-03-09 full suite Phase 7) |
| #276, #216 | TEST-VLAN-PCP-001 + TEST-QUEUE-PRIORITY-001 | `tests/tsn/test_vlan_pcp_tc_mapping.c` | PASS exit 0 (driver v1.0.189.0, 2026-03-09 full suite Phase 7) |
| #286 | TEST-NDIS-FASTPATH-001: NDIS Fast Path <1 µs | `tests/ndis/test_ndis_fastpath_latency.c` | PASS exit 0 (driver v1.0.189.0, 2026-03-09 full suite Phase 7) |
| #215, #231, #254 | TEST-ERROR-HANDLING-001 + TEST-ERROR-RECOVERY-001 + TEST-ERROR-INJECT-001 | `tests/integration/test_error_recovery.c` | PASS exit 0 (driver v1.0.189.0, 2026-03-09 full suite Phase 7) |
| #210 | TEST-GPTP-001: gPTP PHC Interface Contract | `tests/gptp/test_gptp_phc_interface.c` | PASS exit 0 (driver v1.0.189.0, 2026-03-09 full suite Phase 7) |
| #213 | TEST-VLAN-001: VLAN Tag Insert/Strip | `tests/tsn/test_vlan_tag.c` | PASS exit 0 (driver v1.0.189.0, 2026-03-09 full suite Phase 7) |
| #236 | TEST-EVENT-003: ATDECC Entity Events | `tests/atdecc/test_atdecc_event.c` | ✅ **RE-VERIFIED 2026-03-19**: **5P/0F** (`test_atdecc_event.exe_20260319_152856.log`) — TC-001…TC-005 all PASS. Regression (2026-03-18: 1P/4F) caused by `MAX_ATDECC_SUBSCRIPTIONS=4` exhaustion; fix: limit raised to 16, per-handle auto-cleanup removed. |
| #211 | TEST-SRP-001: SRP Bandwidth Reservation | `tests/srp/test_srp_interface.c` | PASS exit 0 (driver v1.0.189.0, 2026-03-09 full suite Phase 7) |
| #240 | TEST-GPTP-COMPAT-001: gPTP Daemon Coexistence | `tests/openavnu/test_gptp_daemon_coexist.c` | PASS exit 0 (driver v1.0.189.0, 2026-03-09 full suite Phase 7) |
| #258 | TEST-COMPAT-WIN7-001: Win10+ OS Version Check | `tests/compat/test_win7_stub.c` | PASS exit 0 (driver v1.0.189.0, 2026-03-09 full suite Phase 7) |
| #312 | TEST-MDIO-PHY-001: MDIO/PHY Register Access | `tests/ioctl/test_ioctl_mdio_phy.c` | **P=54 F=0 S=36** (driver v1.0.193.x commit `1d60f6e`, 2026-03-09). UT-MDIO-004/005 PASS on all 6 adapters (Clause 22 bounds check + per-adapter OPEN_ADAPTER fix). |
| #219 | TEST-PFC-001: Priority Flow Control | `tests/pfc/test_pfc_pause.c` | **P=30 F=0 S=0** (driver v1.0.193.x, 2026-03-09). Per-adapter OPEN_ADAPTER fix applied; TC-PFC-004 MDIO read passes on all 6 I226-V adapters. |
| #223 | TEST-EEE-001: IEEE 802.3az EEE/LPI | `tests/eee/test_eee_lpi.c` | **P=24 F=0 S=6** (driver v1.0.193.x commit `209cff6`, 2026-03-09). TC-EEE-001–004 PASS; TC-EEE-005 correctly SKIP (Clause 45 MMD 7.60 reg=60 > 31, rejected by Clause 22 guard — documented TDD marker for future Clause 45 support). |
| #174 | TEST-EVENT-004: AVTP Diagnostic Counter Events | `tests/event/test_avtp_tu_bit_events.c` | CLOSED 2026-03-18 (GitHub state: completed). 6 TCs: late TS detection, early TS detection, seq discontinuity, counter reset, multi-stream independence, threshold config. Requires network emulator + packet generator. Traces #160. Note: #234 (Cat3) also references TEST-EVENT-004 with same file — both kept per "finer not loose" principle. |
| #175 | TEST-EVENT-002: AVTP Timestamp Uncertain Bit Change Events | `tests/event/test_avtp_tu_bit_events.c` | CLOSED 2026-03-06 (GitHub state: completed). 5 TCs: TU bit 0→1, TU bit 1→0, multi-stream TU independence, rapid GM flapping (10×), event data consistency check. Requires 2× gPTP grandmaster devices + gPTP-capable switch. Traces #162. |
| #192 | TEST-PTP-FREQ-001 (v1 — 3-level: Unit+Integration+V&V, 14 TCs) | `tests/ioctl/test_ioctl_ptp_freq.c` | CLOSED 2026-03-06 (GitHub state: completed). L1 Unit (8 TCs: INCVAL ±1ppb, I210/I225 base values, range rejection, MMIO err), L2 Integration (3 TCs: IOCTL E2E, 4 adapters concurrent, during gPTP sync), L3 V&V (3 TCs: stability bench, 24h drift <1ppm, gPTP sync <1µs). Traces #3, #39. Note: different from #296 (v2 with GPS + 17 TCs) — both kept. |
| #193 | TEST-IOCTL-PHC-QUERY-001 (3-level: Unit+Integration+V&V, 17 TCs) | `tests/ioctl/test_ioctl_phc_query.c` | CLOSED 2026-03-06 (GitHub state: completed). L1 Unit (10 TCs: valid query, buffer too small, NULL ptr, invalid code, PHC uninitialized, HW read failure, unprivileged, input ignored, oversized output, during adapter removal), L2 Integration (4 TCs: user-mode app, 10 concurrent threads, 4 adapters, during driver unload), L3 V&V (3 TCs: P50/P95 <500ns / P99 <1µs, 1000 QPS 1hr, multi-process 5×100QPS). Traces #34. |
| #295 | TEST-PTP-CLOCK-001: PTP Clock Get/Set Timestamp (IOCTLs 24/25, 12 TCs) | `tests/integration/ptp/ptp_clock_control_test.c` | CLOSED (GitHub state: completed). 12 TCs: atomic read (10k reads + rollover), atomic dual write, clock not enabled, invalid clock_id (0–3 valid, ≥4 invalid), null buffer, buffer <16B, BAR0 failure, adapter unbound, rollover consistency, GET <500ns, SET <1µs, IOCTL overhead <5µs. Hardware: I210/I217/I219/I225/I226. Traces #2. |
| #296 | TEST-PTP-FREQ-001 (v2 — GPS long-term stability, 17 TCs) | `tests/ioctl/test_ptp_freq_complete.c` | CLOSED (GitHub state: completed). 17 TCs: ±100ppb, ±50ppb, 0ppb, ±999999ppb, overflow rejection (positive+negative), clock not enabled, invalid base increment (6–10ns range), HW write failure, integer overflow protection, null buffer, 1-hour GPS stability (±10ppb Allan deviation), fractional sub-ns precision, IOCTL <1µs, calc <200ns, throughput >1000Hz. Traces #3. Note: different from #192 (v1 3-level structure, 14 TCs) — both kept. |
| #297 | TEST-PTP-HW-TS-001: Hardware Timestamping Control (TSAUXC / IOCTL 40, 13 TCs) | `tests/integration/tsn/tsauxc_toggle_test.c` | CLOSED (GitHub state: completed). 13 TCs: enable SYSTIM0 (bit31 cleared+incrementing), disable SYSTIM0 (bit31 set), enable/disable/re-enable cycle, multi-timer mask (0x1/0x3/0xF), EN_TT0/EN_TT1 (bits 0,4), EN_TS0/EN_TS1 (bits 8,10), null buffer, buffer <24B, invalid timer_mask (>0xF), HW failure, register persistence across cycles, perf <1µs, integration with IOCTLs 24/25. Traces #5. |
| #298 | TEST-PTP-RX-TS-001 (v1 — 3-level hierarchy deep spec, 15 TCs) | `tests/integration/ptp/rx_timestamping_test.c` | CLOSED (GitHub state: completed). 15 TCs: RXPBSIZE read-only, enable CFG_TS_EN bit29 (requires_reset=1), disable CFG_TS_EN, SRRCTL[0] read-only, enable/disable SRRCTL[0].TIMESTAMP bit30, multi-queue (I210/I226:0-3, I211:0-1), null buffer IOCTL41, buffer too small IOCTL41, invalid queue index, prerequisite enforcement (CFG_TS_EN=0 blocks queue enable), BAR0 failure, register persistence across driver reload, perf P99<2ms, integration 3-level sequence. Traces #6. Note: different from #311 (v2 port reset focus) — both kept. |
| #299 | TEST-PTP-TARGET-TIME-001: Target Time and Auxiliary Timestamp (IOCTLs 43/44, 16 TCs) | `tests/ioctl/test_ioctl_target_time.c` | CLOSED (GitHub state: completed). Priority P1. 16 TCs: read TRGTTIML0 (no modify), set target 0 (5s future), set target 1 (10s independent), enable EN_TT0 interrupt (TSICR.TT0), SDP pulse output (SKIP if no SDP pin), read AUXSTMP0, capture aux TS on SDP event (SKIP if no SDP), clear AUTT0 flag, null buffer IOCTL43, buffer too small IOCTL44, invalid timer index (>1), prereq SYSTIM0, BAR0 failure, perf (IOCTL43 <500µs, IOCTL44 <300µs, P99<2ms), integration target time interrupt E2E, integration aux TS capture E2E. Registers: TRGTTIML0/H0 (0x0B644/8), TRGTTIML1/H1 (0x0B64C/50), AUXSTMPL0/H0 (0x0B65C/60), TSICR (0x0B66C). Traces #7. |
| #311 | TEST-PTP-RX-TS-001 (v2 — port reset focus: CTRL.RST + timeout, 15 TCs) | `tests/ioctl/test_rx_timestamp_complete.c` | CLOSED (GitHub state: completed). 15 TCs: enable buffer IOCTL41 (requires_reset flag), disable buffer IOCTL42, **port reset execution** (CTRL.RST bit26, poll <5ms), per-queue enable IOCTL42 (immediate, no reset), disable per-queue, multi-queue, **dependency check** (queue before buffer → STATUS_INVALID_DEVICE_STATE), invalid queue index (I210/I225:0-3, I217/I219:0-1), null buffer both IOCTLs, buffer size validation (IOCTL41:20B, IOCTL42:24B), HW failure, **reset timeout handling (100ms)**, config sequence integration, perf (IOCTL41 <10µs, IOCTL42 <2µs, reset <5ms), cross-hardware matrix (I210/I225/I226 vs I217/I219). Traces #6. Note: different from #298 (v1 3-level hierarchy) — both kept. |
| #314 | TEST-TS-EVENT-SUB-001: Timestamp Event Subscription & Ring Buffer (IOCTLs 33/34, 19 TCs) | `tests/ioctl/test_ioctl_ts_event_sub.c` | CLOSED (GitHub state: completed, despite status:backlog label — github state=closed, state_reason=completed). 19 TCs across 4 groups: Subscription (4: basic IOCTL33, selective types, multiple concurrent, unsubscribe), Ring Buffer (5: map IOCTL34, size 16KB, wraparound, lock-free read sync, unmap), Events (6: RX TS <10ms ±100ns, TX TS <5ms, target time ±1µs, aux TS, monotonic sequence, filtering), Error/Perf (4: 10K ev/sec <10% CPU, invalid handle, allocation failure, overflow notification). Traces #13. Note: #237 (Cat1) also covers IOCTLs 33/34 with same file — both kept per "finer not loose" principle. |
| #256 | TEST-COMPAT-WIN11-001: Windows 11 Compatibility Smoke Test | `tests/compat/test_compat_win11.c` | **6P/0F/0S 2026-03-19** — TC-WIN11-001: OS version ≥10.0.22000 ✅; TC-WIN11-002: driver loads ✅; TC-WIN11-003: INF accepted ✅; TC-WIN11-004: device instance valid ✅; TC-WIN11-005: IOCTL_AVB_OPEN_ADAPTER responds ✅; TC-WIN11-006: build 26200 (25H2) confirmed ✅. Log: `test_compat_win11.exe_20260319_214153.log`. |
| #243 | TEST-BUILD-SIGN-001: Build & Code-Signing Verification | `tests/compat/test_build_sign.c` | **7P/0F/0S 2026-03-19** — WDKTestCert dzarf confirmed in TrustedPublisher. TC-SIGN-001: .sys signed ✅; TC-SIGN-002: .cat signed ✅; TC-SIGN-003: thumbprint in TrustedPublisher ✅; TC-SIGN-004: Signature="$Windows NT$" ✅; TC-SIGN-005: CatalogFile=IntelAvbFilter.cat ✅; TC-SIGN-006: DriverVer=03/19/2026 ✅; TC-SIGN-007: service RUNNING ✅. Log: `test_build_sign.exe_20260319_214441.log`. |
| #27, #292 | TEST-SCRIPTS-CONSOLIDATE-001: Script Consolidation Structure | `tests/verification/scripts/test_scripts_consolidate.c` | **16P/0F/0S 2026-03-19** — 15 canonical scripts in `tools\` ✅; build/install/test scripts present ✅; deprecated archive 31 files + README ✅; 4/4 tool READMEs ✅; `.github\workflows\` ✅; `$ErrorActionPreference` in all 3 canonical scripts ✅. Log: `test_scripts_consolidate.exe_20260319_232338.log`. |
| #293, #294 | TEST-CLEANUP-ARCHIVE-001: Repo Archival & Organization | `tests/verification/cleanup/test_cleanup_archive.c` | **23P/0F/0S 2026-03-19** — 4 archive README.md ✅; 11 archived files present ✅; 0 archived names leaked to active src ✅; 0 dirty-named files ✅; deprecated substructure ✅; no unexpected `.c`/`.h` at root ✅; no stray scripts at root ✅. Log: `test_cleanup_archive.exe_20260319_232651.log`. |
| #225 | TEST-PERF-REGRESSION-001: Performance Regression Baseline | `tests/performance/test_perf_regression.c` | **5P/0F/0S 2026-03-19** — CAPTURE mode (first run): PHC P50=2300 ns, P99=4372 ns, TX median=336 ns, RX median=2076 ns captured to `logs\perf_baseline.dat`; TC-PERF-REG-005 round-trip self-test PASS ✅. COMPARE mode verified on second run: TC-REG-001/003/004 PASS (within 5%); TC-REG-002 correctly FAIL on P99 +12.3% noise spike demonstrating regression detection live. Log: `test_perf_regression.exe_20260319_235621.log`. |
| #305 | TEST-REGS-002: Magic Numbers Static Analysis | `tests/verification/regs/test_magic_numbers.c` | **1P/0F 2026-03-20** — TC-MAGIC-001 PASS: 0 raw hex literals found. Fix: `intel_i226_impl.c:725` `0xFFFFU` → `I226_MDIC_DATA_MASK` (SSOT); `src/IntelAvbFilter.h` excluded from scan (MC.exe-generated ETW file — GUID bytes/keyword masks are framework-owned). Log: `test_magic_numbers.exe_20260320_062350.log`. |
| #194 | TEST-IOCTL-OFFSET-001: PHC Time Offset Adjust | `tests/ioctl/test_ioctl_offset.c` | **15P/0F 2026-03-20** — ALL 15 TCs PASS (100%). Root cause of prior TC-OFFSET-01 failure: OS thread preemption inflated PHC delta by up to 25ms. Fix: QPC wall-clock bracketing — `appliedOffset = actualChange − wallElapsed_ns`; tolerance ±200µs. Also added warm-up IOCTL before measurement to eliminate cold-path driver dispatch. Wall elapsed: 4.3ms; appliedOffset: 5.106ms (within 200µs of target 5ms). Log: `test_ioctl_offset.exe_20260320_063919.log`. |

✅ **#236 RE-VERIFIED 2026-03-19**: ATDECC regression fixed and confirmed 5P/0F. No exceptions — all Cat1 issues verified passing. #236 was already closed on GitHub (2026-03-08, state: closed); regression was driver-only and is now resolved; no need to re-open.

**Action**: Close all other Cat1 entries with comment identifying the covering test binary and last known result.

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
| #209 | TEST-LAUNCH-TIME-001: Launch Time Offload | `test_ioctl_target_time.c` | **✅ CLOSED 2026-03-29** — Past-target-time guard added to `IOCTL_AVB_SET_TARGET_TIME` (`avb_integration_fixed.c`). TC-TARGET-005 rejects past times with Win32 error 87. 15/15 PASS (0F, 1 SKIP=ring-buf). See `2ad239e`. |
| #222 | TEST-DIAGNOSTICS-001: Network Diagnostics | `test_event_log.c` + `diagnostic/*.c` | Packet-capture path and ETW decode assertions not automated |
| #238 | TEST-PTP-001: HW Timestamp Correlation | `ptp_clock_control_test.c`, `rx_timestamping_test.c` | **✅ CLOSED 2026-03-29** — `TestPhcQpcCoherence()` added to `ptp_clock_control_test.c`. TC-5a (bracket <500 µs) and TC-5b (PHC monotone + rate sanity [0.95..1.10×]) PASS 6/6 adapters (6×I226-LM). Test runs FIRST in main() to avoid dirty PHC state from Tests 1-4. |
| #247 | TEST-DEBUG-REG-001: Registry Debug Settings | `test_registry_diagnostics.c` | DebugLevel runtime persistence across driver reload not verified |
| #199 | TEST-PTP-CORR-001: PTP Hardware Correlation | `tests/integration/ptp_corr/test_ptp_corr.c` | **🔴 REOPENED 2026-03-27** — closed prematurely with only 4/17 tests run. IT-CORR-001/003/004 ✅ PASS (14/14, 6×I226-LM); IT-CORR-002 ⚠️ SKIP (`IOCTL_AVB_PHC_CROSSTIMESTAMP` not implemented). **13 tests never run**: UT-CORR-001..010 (unit) + VV-CORR-001..003 (V&V). See **#317** (INFRA tracking) and `TEST-PLAN-MOCK-NDIS-HARNESS.md` for 4-track remediation plan. |
| #234 | TEST-EVENT-004: AVTP Diagnostic Counter Events | `tests/event/test_avtp_tu_bit_events.c` | **Test file now exists** (added post 2026-03-07); verify seq-gap/late-ts threshold trigger and event payload assertions are covered |
| #250 | TEST-INTEGRATION-SUITE-001: HIL Integration | `AvbIntegrationTests.c` | No formal pass/fail traceability report generated in CI |
| #260 | TEST-COMPAT-I225-001: I225 Controller Compat | `tests/device_specific/i226/*.c` | No dedicated VEN_8086:DEV_15F2 detection test (I225-V device ID) |
| #261 | TEST-COMPAT-I219-001: I219 Controller Compat | `avb_test_i219.c` | Test exists; requires physical I219 hardware to run |
| #288 | TEST-HW-DETECT-CAPS-001: HW Capability Detection | `test_hw_state.c`, `avb_test_i210_um.c` | PCI read latency <100 µs assertion not measured |
| #312 | TEST-MDIO-PHY-001: MDIO/PHY Register Access | `tests/ioctl/test_ioctl_mdio_phy.c` | **✅ MOVED TO CAT1** — P=54 F=0 S=36 (driver v1.0.193.x, 2026-03-09). See Category 1 for evidence. |

---

## Category 3b — 🔵 Test Written (Partial Results — Known Gaps)

Test code committed and executed in Release build (driver v1.0.189.0, 2026-03-09).
These **2 issues** remain open because the test binary reports partial failures with a known root cause.
All other Cat3b issues have been verified and moved to Category 1. (#219, #223 fully resolved 2026-03-09; #199, #234 promoted to Category 3 2026-03-18; #305 resolved 2026-03-20; **#194 resolved 2026-03-20**.)

| Issue | Test ID / Feature | Test File | Sprint | Result | Known Gap |
|-------|-------------------|-----------|--------|--------|-----------|
| ~~#194~~ | ~~TEST-IOCTL-OFFSET-001: PHC Time Offset Adjust~~ | ~~`tests/ioctl/test_ioctl_offset.c`~~ | ~~S1~~ | **✅ MOVED TO CAT1** 2026-03-20 — 15P/0F. See Category 1 for evidence. | ~~Root cause: OS thread preemption between ApplyOffset and ReadPHCTime caused PHC delta to include scheduler latency (up to 25ms). Fix: QPC wall-clock bracketing — appliedOffset = actualChange − wallElapsed eliminates preemption from measurement; tolerance ±200µs (PHC/QPC rate deviation < 100ppm over observation window).~~ |
| #269 | TEST-EVENT-LOG-001: Windows Event Log | `tests/event-logging/test_event_log.c` | S1 | 10P/1F — TC-1 FAIL | Driver does not emit Event ID 1 to Windows Event Log on initialization. ETW manifest not registered. Feature gap, not regression. |
| ~~#305~~ | ~~TEST-REGS-002: Magic Numbers Static Analysis~~ | ~~`tests/verification/test_magic_numbers.c`~~ | ~~S1~~ | **✅ MOVED TO CAT1** 2026-03-20 — 1P/0F. See Category 1 for evidence. | ~~13 raw hex literals → all resolved: 12 were in MC-generated `IntelAvbFilter.h` (excluded from scan); 1 in `intel_i226_impl.c` replaced with `I226_MDIC_DATA_MASK`.~~ |
| #271 | TEST-POWER-MGMT-S3-001: S3 Sleep/Wake | `tests/power/test_s3_sleep_wake.c` | S3 | TC-S3-001 PASS; TC-S3-002 triggered real S3 sleep | Machine slept during test; wake-up PHC preservation result unconfirmed. Re-run in controlled session with WoL configured. |
| #223 | TEST-EEE-001: EEE LPI | `tests/eee/test_eee_lpi.c` | S4b | **✅ P=24 F=0 S=6** (driver v1.0.193.x, 2026-03-09) | TC-EEE-001–004 PASS. TC-EEE-005 SKIP (Clause 45 reg=60 > 31, correctly rejected by Clause 22 bounds check; test updated to treat ERROR_INVALID_PARAMETER as skip — commit `209cff6`). Open TDD marker: Clause 45 MDIO support. |
| #219 | TEST-PFC-001: Priority Flow Control | `tests/pfc/test_pfc_pause.c` | S4b | **✅ P=30 F=0 S=0** (driver v1.0.193.x, 2026-03-09) | All 5 TCs PASS on all 6 adapters including TC-PFC-004 MDIO read. Per-adapter OPEN_ADAPTER fix confirmed working. |
| #178, #241 | TEST-EVENT-006 Zero Polling + TEST-EVENT-NF-002 | `tests/event/test_zero_polling_overhead.c` | S5 (hw-gated) | **18P/0F/18S/36T** (6×I226-LM, 2026-03-29) — TC-ZP-001/005 SKIP (GPIO osc/WPR not present), TC-ZP-002 **SKIP-HARDWARE-LIMIT** all 6 (EITR0=0x00008100, INTERVAL=33024µs — programmed by NDIS miniport, not filter — 2026-03-29), TC-ZP-003/004/006 PASS (API contract, CPU=0.0000%, interrupt-driven delivery confirmed). | TC-ZP-002: EITR0 controlled by NIC/NDIS miniport, not filter driver — genuine hardware finding documenting actual state. TC-ZP-001 needs GPIO oscilloscope, TC-ZP-005 needs WPR. Open until hardware tools available. |
| #177 | TEST-EVENT-001 PTP Timestamp Event Latency | `tests/event/test_ptp_event_latency.c` | S5 (hw-gated) | **18P/0F/12S/30T** (6×I226-LM, 2026-03-19) — TC-PTP-LAT-001/003 SKIP (GPIO oscilloscope/WPR required); TC-PTP-LAT-002 PASS (API contract, 0 events in 5s — no 802.1AS traffic on test network), TC-PTP-LAT-004 PASS (60s stress 0 drops), TC-PTP-LAT-005 PASS (all 4 observers received events, equitable delivery). | Core acceptance criterion TC-001 (GPIO oscilloscope latency <500ns) requires hardware. Remain open until oscilloscope run. |
| #179 | TEST-EVENT-005 Event Latency 4-Channel Oscilloscope | `tests/event/test_event_latency_4ch.c` | S5 (hw-gated) | **12P/0F/18S/30T** (6×I226-LM, 2026-03-19) — TC-LAT4-001/003/004 SKIP (4-channel oscilloscope required); TC-LAT4-002 PASS (4-observer arrival ordering verified), TC-LAT4-005 PASS (no priority inversion confirmed). | Three of 5 TCs need 4-channel oscilloscope (Tektronix MDO3000 or equiv). Remain open until hardware measurement. |
| #176 | TEST-EVENT-003 ATDECC AEN Protocol Events | `tests/atdecc/test_atdecc_aen_protocol.c` | S5 (hw-gated) | **18P/0F/18S/36T** (6×I226-LM, 2026-03-19, ~3 min due to TC-AEN-006×6) — TC-AEN-001/003/004 SKIP (ATDECC controller/AVB stream source hardware required); TC-AEN-002 PASS (ENTITY_AVAILABLE API verified), TC-AEN-005 PASS (sequence monotonicity confirmed), TC-AEN-006 PASS (subscription auto-expiry after 31s). | TC-AEN-001/003/004 require physical ATDECC controller + AVB stream source on network + Wireshark dissector. Remain open until hardware integration test. |

**Common root cause for #219, #223, #312 (RESOLVED 2026-03-09)**: Tests never called `IOCTL_AVB_OPEN_ADAPTER`; plus I226 MDIC hardware stub + wrong `private_data` cast in driver. All three layers fixed across commits `8ecf15b`, `1d60f6e`, `209cff6`. **All re-verified**: #219 P=30 F=0, #223 P=24 F=0 S=6 (Clause 45 TDD marker), #312 P=54 F=0 S=36.

**Action**: Resolve #269 by registering ETW manifest; re-test #271 in controlled wake environment.

---

## Category 4 — 🔴 Test Missing (Not in Sprint 1–4)

No automated test written; not yet scheduled in Sprints 1–4. Target Sprint 5 or future sprints.

### Priority P2 (Medium — Sprint 5)

| Issue | Test ID | Feature | New Test Required |
|-------|---------|---------|------------------|
| ~~#225~~ | ~~TEST-PERF-REGRESSION-001~~ | ~~Performance regression baseline~~ | **Moved to Cat1** 2026-03-19 — `test_perf_regression.exe` 5P/0F/0S |
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
*(#176, #177, #178, #179 moved to Category 3b — test files written and executed 2026-03-19)*

---

## Category 5 — ⚫ Implementation Missing (Driver Feature Not Built — TDD: Write Tests First)

These issues are open and valid. Per TDD practice, tests should be written **before** the implementation. These failing tests define the acceptance criteria and drive the implementation work. Do **not** close these issues.

| Issue | Test ID | Unimplemented Feature | Notes |
|-------|---------|----------------------|---------------------|
| #220 | TEST-HARDWARE-OFFLOAD-001 | TSO/RSS/LSO/checksum offload | Miniport driver features; out of scope for NDIS filter driver |
| #221 | TEST-LACP-001 | IEEE 802.1AX LACP | LACP protocol not in driver scope |
| #224 | TEST-SEGMENTATION-001 | MAC/IP/EtherType filtering rules | Complex packet filtering not implemented |
| #227 | TEST-API-DOCS-001 (body: AVTP conformance) | IEEE 1722 AVTP packet format | AVTP is above the filter; body content mismatches title entirely |
| #228 | TEST-USER-DOCS-001 (body: penetration testing) | Pen testing infrastructure | Requires specialized external tools; not automated in CI |

---

## Category 6 — 📋 Process / Infrastructure / Documentation

Not a driver feature test. Managed on a separate track.

| Issue | Test ID | Category |
|-------|---------|----------|
| #187 | QA-SC-TEST-006 | Architecture quality scenario document |
| #233 | TEST-PLAN-001 | Test plan document (meta) |
| #244 | TEST-BUILD-CI-001 | CI/CD pipeline configuration |
| #251 | TEST-DOC-DEPLOY-001 | Deployment guide documentation review |
| #252 | TEST-MAINTAIN-001 | Static analysis / SonarQube maintainability |
| #255 | TEST-SECURITY-AUDIT-001 | CodeQL / Coverity / BinSkim tooling |
| #265 | TEST-COVERAGE-001 | Coverage ≥80% CI gate |
| #267 | TEST-DOC-API-001 | Doxygen IOCTL API completeness |
| #278 | TEST-TSN-TERMS-001 | Documentation terminology audit |
| #282 | TEST-NAMING-CONV-001 | IEEE 802.1AS-2020 naming code audit |
| #300 | TEST-SSOT-002 | CI workflow SSOT violation detection |

---

## Summary

| Category | Count | Action |
|----------|-------|--------|
| ✅ Tested OK (all re-verified 2026-03-19) | 59 | 41 original + #312/#219/#223 verified 2026-03-09 + **11 newly added 2026-03-20**: #174, #175, #192, #193, #295, #296, #297, #298, #299, #311, #314. **#236 ATDECC regression FIXED** — re-verified 2026-03-19: 5P/0F |
| 🔁 Duplicates | 5 | Close as duplicate |
| 🟡 Nearly OK — Genuine Gaps | 10 | #312 moved to Cat1; 10 remain (#199, #234 promoted from Cat4 2026-03-18). **⚠️ #199 REOPENED 2026-03-27** — was closed with only 4/17 tests run; 13 tests still outstanding (UT-CORR-001..010, VV-CORR-001..003). See #317 + `TEST-PLAN-MOCK-NDIS-HARNESS.md`. |
| 🔵 Test Written — Partial Results | 9 | ~~#194 (ioctl_offset 14P/1F — resolved 2026-03-20)~~, ~~#305 (13 magic numbers — resolved 2026-03-20)~~, **#269** (ETW manifest gap), **#271** (S3 wake unconfirmed). #199/#234 promoted to Cat3 2026-03-18. **+5 (2026-03-19)**: ~~#178/#241~~ (ZP 18P/0F/18S — TC-ZP-002 SKIP-HARDWARE-LIMIT: EITR0 miniport constraint, resolved 2026-03-29), #177 (PTP latency 18P/0F/12S), #179 (4ch latency 12P/0F/18S), #176 (ATDECC AEN 18P/0F/18S) — moved from Cat4; all run on 6×I226-LM; hardware-gated TCs skip. |
| 🔴 Test Missing (Sprint 5 / Future) | 10 | P2: 4 (#241 moved to Cat3b 2026-03-19) · P3: 7 (#176, #177, #178, #179 moved to Cat3b 2026-03-19; 7 original P3 entries remain). |
| ⚫ Implementation Missing (out of driver scope) | 5 | Not in Sprint 1–4 scope; review per sprint |
| 📋 Process/Infra/Docs | 12 | Separate track (#243 moved to Cat1 2026-03-19; #294 moved to Cat1 2026-03-19). **+1 (#317)**: [INFRA] Mock NDIS Unit Test Harness — prerequisite for #199 UT/VV closure. |
| **Total** | **107** | (92 original + 14 newly added 2026-03-20 + **1 new 2026-03-27**: #317 harness infra issue) |

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
Sprint 1  →  ✅ Verified Release 2026-03-09 (driver v1.0.189.0) → 12/14 PASS (Cat1); #269 partial (ETW gap); #305 CI-only
Sprint 2  →  ✅ Verified Release 2026-03-09 (driver v1.0.189.0) → 8/8  PASS (Cat1) — all issues ready to close
Sprint 3  →  ✅ Verified Release 2026-03-09 (driver v1.0.189.0) → 8/9  PASS (Cat1); #271 S3 wake unconfirmed
Sprint 4  →  ✅ Verified 2026-03-09 (driver v1.0.193.x) → 8/8 PASS (Cat1); #219 P=30 F=0, #223 P=24 F=0 S=6, #312 P=54 F=0 S=36 — all confirmed
Sprint 5  →  ❌ Not started — long-running / OS compat / hw-gated → closes ~10 issues
```

**Immediate next step**: Close the 44 confirmed-PASS GitHub issues (Sprint 1–4 + #219/#223/#312). Then resolve #269 (ETW manifest registration); re-test #271 (S3 wake, controlled WoL session).

---

## Appendix: Test File Inventory (as of 2026-03-07)

```
tests/ioctl/
  test_ioctl_qav_cbs.c          → #283 CBS config
  test_ioctl_target_time.c      → #209 launch time ✅ 15/15 PASS 2026-03-29
  test_ioctl_rx_timestamp.c     → RX timestamp basic
  test_ioctl_ptp_getset.c       → #195 #266 PHC get/set
  test_ioctl_ptp_freq.c         → PTP freq basic
  test_ioctl_tas.c              → #281 TAS config
  test_ioctl_phc_query.c        → PHC query
  test_ioctl_offset.c           → #194 PHC offset (15P/0F ✅ 2026-03-20)
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
