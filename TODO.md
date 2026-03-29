# Intel AVB Filter Driver - TODO

**Last Updated**: March 2026 (build 345, driver v1.0.193.x, 2026-03-29)

## 🟢 Current Phase: Post-Validation — Filling Remaining Gaps

I226-LM hardware validation is substantially complete. The CI runner has 6×Intel I226-LM
adapters. The hardware-dependent suite (HardwareUnit + HardwareIntegration) covers 60+
tests. Remaining work is targeted gap-filling, CI hardening, and production sign-off.

---

## 🔴 Active Failures to Fix

### #269 — ETW Event Log (10P / **1F**)
- **Problem**: Driver does not emit Event ID 1 to Windows Event Log on initialization;
  ETW manifest not installed/registered.
- **Action**: Register ETW manifest at driver load; add `mc.exe`-generated header to driver
  build; add EventWrite call in `DriverEntry`; re-verify TC-ETW-001.
- **Issue**: [#269](https://github.com/zarfld/IntelAvbFilter/issues/269)
### #209 â Missed-Deadline Detection (TC-TARGET-005: **FAIL** â confirmed driver gap)
- **Problem**: Driver silently accepts a target time set 1 second in the past without returning
  an error or raising an event. `IOCTL_AVB_SET_TX_LAUNCH_TIME` does not validate that
  `launch_time_ns > current_phc_ns`; missed-deadline rejection logic is absent in the driver.
- **Action**: Add past-target guard in `AvbSetTxLaunchTime()` (or equivalent ioctl handler);
  return `STATUS_INVALID_PARAMETER` or a domain error code; re-verify TC-TARGET-005.
- **Issue**: [#209](https://github.com/zarfld/IntelAvbFilter/issues/209)

### #288 â PCI Read Latency on Adapters 3-5 (TC-PCI-LAT-001: **marginal FAIL**)
- **Problem**: P99 IOCTL read latency on adapters 3, 4, 5 measured at 103-107 Âµs,
  marginally above the 100 Âµs test threshold. Adapters 0-2 pass cleanly. The concurrent
  VV-CORR-001 stability run (PID 6604) may be consuming PCIe bandwidth; alternatively
  adapters 3-5 are on a higher-latency PCIe slot or bus segment.
- **Action**: Re-run `test_hw_state.exe` after VV-CORR-001 finishes (~19:57 tonight) on an
  otherwise idle machine. If failures persist, audit PCIe slot topology or relax P99 threshold
  to 150 Âµs with a note; if they clear, document as load artifact and mark #288 DONE.
- **Issue**: [#288](https://github.com/zarfld/IntelAvbFilter/issues/288)
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
| ~~[#199](https://github.com/zarfld/IntelAvbFilter/issues/199)~~ | ~~TX/RX PHC correlation â delta assertion between TX and RX PHC timestamps not automated~~ | ✅ **DONE 2026-03-29** â TC-CORR-TX-RX-001 added to `test_tx_timestamp_retrieval.c`; **42/42 PASS** |
| [#209](https://github.com/zarfld/IntelAvbFilter/issues/209) | ~~Launch-time gate-window enforcement and missed-deadline detection missing~~ | TC-TARGET-006/007 added, **PASS**; **TC-TARGET-005 FAIL** confirmed â driver silently accepts past target time, missed-deadline rejection absent (see Active Failures) |
| [#222](https://github.com/zarfld/IntelAvbFilter/issues/222) | Packet-capture path and ETW decode assertions not automated | Extend diagnostic test suite |
| ~~[#234](https://github.com/zarfld/IntelAvbFilter/issues/234)~~ | ~~AVTP diagnostic counter: seq-gap/late-ts threshold trigger and event payload assertions~~ | ✅ **DONE 2026-03-29** â UT-011..015 added to `test_avtp_tu_bit_events.c`; **20/20 PASS** |
| ~~[#238](https://github.com/zarfld/IntelAvbFilter/issues/238)~~ | ~~±100 ns accuracy assertion across PHC-QPC pair not automated~~ | ✅ **DONE 2026-03-29** — TC-5a/TC-5b added to `ptp_clock_control_test.c`; 12/12 PASS |
| ~~[#247](https://github.com/zarfld/IntelAvbFilter/issues/247)~~ | ~~DebugLevel runtime persistence across driver reload not verified~~ | ✅ **DONE 2026-03-29** â TC-DEBUG-REG-005/006 added to `test_registry_diagnostics.c`; **9/9 PASS** (REG-005 SKIP needs debug driver; REG-006 deferred during VV-CORR-001) |
| [#250](https://github.com/zarfld/IntelAvbFilter/issues/250) | HIL integration — no formal pass/fail traceability report generated in CI | Add CI step to emit structured report |
| ~~[#260](https://github.com/zarfld/IntelAvbFilter/issues/260)~~ | ~~I225-V device ID (VEN_8086:DEV_15F2) detection test missing~~ | ✅ **DONE 2026-03-29** â TC-DEV-I225-001 added to `test_multidev_adapter_enum.c`; **6/6 PASS** (SKIP counted as PASS â no I225 HW on CI runner) |
| [#288](https://github.com/zarfld/IntelAvbFilter/issues/288) | PCI read latency <100 Âµs assertion â ~~not measured~~ | TC-PCI-LAT-001/002/003 added to `test_hw_state.c`; **6/9 PASS** â **adapters 3-5 FAIL** P99 103-107 Âµs > 100 Âµs limit (marginal; possible load artifact from concurrent VV-CORR-001 â see Active Failures) |

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

- [x] ~~Enable MSVC `/analyze` (PREfast) in CI~~ — ✅ **DONE** (`static-analysis-prefast` job in
  `ci-standards-compliance.yml`; `continue-on-error: true` phase-in until baseline = 0)
- [ ] Run SDV and commit DVL artifact — `test-evidence/dvl-*.xml` required before each release
- [x] ~~Wire CodeQL for main driver source~~ — ✅ **DONE** (`codeql-driver.yml` uses
  `microsoft/windows-driver-developer-supplemental-tools`)
- [ ] Commit SDV results to `test-evidence/sdv-results-*.xml`
- [ ] Remove `continue-on-error: true` from PREfast job once C6xxx baseline reaches zero

From **TEST-PLAN-NDIS-LWF-COVERAGE** (Pillar 2 — Lifecycle Coverage):

- [x] ~~Add missing statistics counters to `AVB_DRIVER_STATISTICS` struct~~ — ✅ **DONE**
  (all 7 fields: FilterAttach/Pause/Restart/Detach, OutstandingSend/ReceiveNBLs, PauseRestartGeneration)
- [x] ~~Write lifecycle smoke test~~ — ✅ **DONE** (`test_lifecycle_coverage.exe` built;
  added to `HardwareUnitTests` suite in `Run-Tests-CI.ps1` 2026-03-29)

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