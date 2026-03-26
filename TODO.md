# Intel AVB Filter Driver - TODO

**Last Updated**: March 2026 (build 345, driver v1.0.193.x)

## 🟢 Current Phase: Post-Validation — Filling Remaining Gaps

I226-LM hardware validation is substantially complete. The CI runner has 6×Intel I226-LM
adapters and runs 113 tests; 106/113 pass (93.8%). Remaining work is targeted gap-filling,
CI hardening, and production sign-off.

---

## 🔴 Active Failures to Fix

### #269 — ETW Event Log (10P / **1F**)
- **Problem**: Driver does not emit Event ID 1 to Windows Event Log on initialization;
  ETW manifest not installed/registered.
- **Action**: Register ETW manifest at driver load; add `mc.exe`-generated header to driver
  build; add EventWrite call in `DriverEntry`; re-verify TC-ETW-001.
- **Issue**: [#269](https://github.com/zarfld/IntelAvbFilter/issues/269)

### #178 / #241 — Zero Polling Overhead TC-ZP-002 (**6F** — known HW constraint)
- **Situation**: EITR0 = 33,024 µs interrupt coalescing (NIC default set by NDIS miniport).
  NDIS LWF has no IOCTL to change EITR0 — this is a genuine hardware/architecture finding,
  not a bug.
- **Action**: Update test spec to document this as an observed platform constraint, not a
  failure; mark TC-ZP-002 as SKIP-HARDWARE-LIMIT with explanatory message.
- **Issues**: [#178](https://github.com/zarfld/IntelAvbFilter/issues/178),
  [#241](https://github.com/zarfld/IntelAvbFilter/issues/241)

### #271 — S3 Sleep/Wake PHC Preservation (TC-S3-002: **unconfirmed**)
- **Problem**: Test triggered real S3 sleep during run; wake-up PHC preservation result
  unconfirmed. `FilterRestart` S3 PHC fix is coded (`AvbBringUpHardware` called to
  re-program TIMINCA/SYSTIML/TSAUXC/TSYNCRXCTL) but not yet verified on a controlled
  S3 cycle.
- **Action**: Re-run on a dedicated machine with WoL configured and a serial console for
  post-wake log capture.
- **Issue**: [#271](https://github.com/zarfld/IntelAvbFilter/issues/271)

---

## 🟡 Tests Exist — Incomplete Coverage (Cat. 3)

These tests pass what they cover but do not yet exercise all acceptance criteria.

| Issue | Gap | Action |
|-------|-----|--------|
| [#199](https://github.com/zarfld/IntelAvbFilter/issues/199) | TX/RX PHC correlation — delta assertion between TX and RX PHC timestamps not automated | Extend `test_tx_timestamp_retrieval.c` |
| [#209](https://github.com/zarfld/IntelAvbFilter/issues/209) | Launch-time gate-window enforcement and missed-deadline detection missing | Extend `test_ioctl_target_time.c` |
| [#222](https://github.com/zarfld/IntelAvbFilter/issues/222) | Packet-capture path and ETW decode assertions not automated | Extend diagnostic test suite |
| [#234](https://github.com/zarfld/IntelAvbFilter/issues/234) | AVTP diagnostic counter: seq-gap/late-ts threshold trigger and event payload assertions | Extend `test_avtp_tu_bit_events.c` |
| [#238](https://github.com/zarfld/IntelAvbFilter/issues/238) | ±100 ns accuracy assertion across PHC-QPC pair not automated | Extend `ptp_clock_control_test.c` |
| [#247](https://github.com/zarfld/IntelAvbFilter/issues/247) | DebugLevel runtime persistence across driver reload not verified | Extend `test_registry_diagnostics.c` |
| [#250](https://github.com/zarfld/IntelAvbFilter/issues/250) | HIL integration — no formal pass/fail traceability report generated in CI | Add CI step to emit structured report |
| [#260](https://github.com/zarfld/IntelAvbFilter/issues/260) | I225-V device ID (VEN_8086:DEV_15F2) detection test missing | Add device-specific test or expand existing |
| [#288](https://github.com/zarfld/IntelAvbFilter/issues/288) | PCI read latency <100 µs assertion not measured | Extend `test_hw_state.c` |

---

## 🔵 Hardware-Gated — Waiting for Physical Equipment

These tests have written code but need specific hardware to run the critical test cases.

| Issue | Equipment Needed | Blocked Test Cases |
|-------|------------------|--------------------|
| [#177](https://github.com/zarfld/IntelAvbFilter/issues/177) | GPIO oscilloscope | TC-PTP-LAT-001 (event latency <500 ns end-to-end) |
| [#179](https://github.com/zarfld/IntelAvbFilter/issues/179) | 4-channel oscilloscope (e.g. MDO3000) | TC-LAT4-001/003/004 (per-channel jitter measurement) |
| [#176](https://github.com/zarfld/IntelAvbFilter/issues/176) | ATDECC controller + AVB stream source | TC-AEN-001/003/004 (entity announcement over wire) |
| [#261](https://github.com/zarfld/IntelAvbFilter/issues/261) | Intel I219 adapter | Full `avb_test_i219.exe` suite |
| [#260](https://github.com/zarfld/IntelAvbFilter/issues/260) | Intel I225 adapter | DEV_15F2 detection + I225-specific timing |

---

## 🔧 CI / Infrastructure Gaps

From **TEST-PLAN-NDIS-LWF-COVERAGE** (Pillar 1 — Static Analysis):

- [ ] Enable MSVC `/analyze` (PREfast) in CI — add `<RunCodeAnalysis>true</RunCodeAnalysis>` to
  `.vcxproj`; zero Level-4 warnings required before green
- [ ] Run SDV and commit DVL artifact — `test-evidence/dvl-*.xml` required before each release
- [ ] Wire CodeQL for main driver source using
  `microsoft/windows-driver-developer-supplemental-tools` action
- [ ] Commit SDV results to `test-evidence/sdv-results-*.xml`

From **TEST-PLAN-NDIS-LWF-COVERAGE** (Pillar 2 — Lifecycle Coverage):

- [ ] Add missing statistics counters to `AVB_DRIVER_STATISTICS` struct:
  `FilterPauseCount`, `FilterRestartCount`, `FilterDetachCount`,
  `OutstandingSendNBLs`, `OutstandingReceiveNBLs`, `PauseRestartGeneration`
- [ ] Write lifecycle smoke test that exercises Attach → Restart → Pause → Detach sequence
  and asserts each counter incremented correctly

---

## 📋 Scheduled — No Test Yet Written (Cat. 4)

### Priority P2 (Sprint 5)

| Issue | Feature | Notes |
|-------|---------|-------|
| [#257](https://github.com/zarfld/IntelAvbFilter/issues/257) | Windows 10 compatibility (1809 / 21H2 / 22H2) | Driver install + smoke test on three OS variants |
| [#259](https://github.com/zarfld/IntelAvbFilter/issues/259) | Windows Server 2016 / 2019 / 2022 | Server Core validation included |

### Priority P3 (Future Sprints)

| Issue | Feature | Notes |
|-------|---------|-------|
| [#229](https://github.com/zarfld/IntelAvbFilter/issues/229) | 7-day continuous stability | Long-running monitor; memory and handle leak tracking |
| [#232](https://github.com/zarfld/IntelAvbFilter/issues/232) | Full performance benchmark suite | Latency + throughput + CPU% + memory footprint harness |
| [#242](https://github.com/zarfld/IntelAvbFilter/issues/242) | NDIS filter coexistence | Multi-filter stack; AVB filter active and non-conflicting |
| [#245](https://github.com/zarfld/IntelAvbFilter/issues/245) | Event latency <10 µs (NF) | GPIO + oscilloscope required |
| [#246](https://github.com/zarfld/IntelAvbFilter/issues/246) | WDF/KMDF compat on Win10 1809 / 22H2 / Win11 | Version query + API check |

---

## 🚀 Production Hardening

- [ ] WHQL / EV code-signing for production release (test-signed only today)
- [ ] INF + CAT package for GitHub Release v0.1
- [ ] Resolve #269 (ETW manifest) — blocks Event Viewer integration for end users
- [ ] Stress test sign-off (#229) — prerequisite for "stable" release tag

---

## ⛔ Explicitly NOT Planned

### Simulation / Fake Modes
- Will NOT add hardware simulation, fake timestamps, or stub implementations  
- **Reason**: No false advertising — production code paths use real hardware only

### Legacy Controller Support
- Will NOT support Intel 82574L (lacks AVB/TSN hardware features)
- Will NOT support non-Intel Ethernet controllers

### Out-of-Scope Protocol Stacks
- Will NOT implement LACP (802.1AX) — [#221](https://github.com/zarfld/IntelAvbFilter/issues/221)
- Will NOT implement TSO/RSS/LSO/checksum offload — [#220](https://github.com/zarfld/IntelAvbFilter/issues/220) (miniport features, not LWF)
- Will NOT implement AVTP framing (IEEE 1722) — LWF operates at control-plane only

### Alternative Architectures
- Will NOT implement as standalone miniport driver
- Will NOT implement as user-mode library  
- **Reason**: NDIS 6.30 lightweight filter is the correct architecture for this use case