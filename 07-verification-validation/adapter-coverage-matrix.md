# Adapter Coverage Matrix

**Phase**: 07-Verification & Validation
**Standards**: IEEE 1012-2016 (Verification & Validation)
**Date**: 2026-04-10
**Status**: Draft — based on test run `full_test_suite_20260410_192359.log`

## Context

System under test has three physical Intel adapters:

| Adapter | DID | Driver Index | MAC |
|---------|-----|:---:|-----|
| Intel I226-LM | 0x125B | 0 | F0:B2:B9:31:55:DB |
| Intel I210 | 0x1533 | 1 | C4:62:37:0B:B5:6E |
| Intel I219-LM | 0x15B7 | 2 | E4:54:E8:61:38:E3 |

Capability constants defined in `external/intel_avb/lib/intel.h` (prefix `INTEL_CAP_*`):

| Bit | Mask | Constant |
|-----|------|----------|
| 0 | 0x001 | `INTEL_CAP_BASIC_1588` |
| 1 | 0x002 | `INTEL_CAP_ENHANCED_TS` |
| 2 | 0x004 | `INTEL_CAP_TSN_TAS` |
| 3 | 0x008 | `INTEL_CAP_TSN_FP` |
| 4 | 0x010 | `INTEL_CAP_PCIe_PTM` |
| 5 | 0x020 | `INTEL_CAP_2_5G` |
| 6 | 0x040 | `INTEL_CAP_MDIO` |
| 7 | 0x080 | `INTEL_CAP_MMIO` |
| 8 | 0x100 | `INTEL_CAP_EEE` |

## Adapter Capability Bitmask (reported by driver at runtime)

| Adapter | DID | Caps | B1588 | ENH_TS | TAS | FP | PTM | 2.5G | MDIO | MMIO | EEE |
|---------|-----|------|:-----:|:------:|:---:|:--:|:---:|:----:|:----:|:----:|:---:|
| I226-LM | 0x125B | 0x000001BF | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✗ | ✓ | ✓ |
| I210    | 0x1533 | 0x00000083 | ✓ | ✓ | ✗ | ✗ | ✗ | ✗ | ✗ | ✓ | ✗ |
| I219-LM | 0x15B7 | 0x000000C3 | ✓ | ✓ | ✗ | ✗ | ✗ | ✗ | ✓ | ✓ | ✗ |

> **Note**: I226 has no MDIO bit (0x040 is clear in 0x1BF). I219 has no 2_5G or EEE bits (0x020, 0x100 are clear in 0x0C3). Datasheet confirms I219 EEE is a link-layer feature not exposed via `INTEL_CAP_EEE` in driver.

---

## Table 1 — Actual Execution Coverage

Derived from log `full_test_suite_20260410_192359.log` and source-code analysis.

Legend: `✅ PASS` `❌ FAIL` `⚠️ WRONG` (ran on wrong adapter) `⬜ NOT RUN` `— N/A`

| Test Executable | I226 (idx 0) | I210 (idx 1) | I219 (idx 2) | Root Cause of Gap |
|----------------|:------------:|:------------:|:------------:|-------------------|
| `avb_test_i210_um.exe` | ⚠️ PASS | ⬜ NOT RUN | ⬜ NOT RUN | Pattern A: no `IOCTL_AVB_OPEN_ADAPTER` — silently binds to I226 (index 0) |
| `avb_test_i219.exe` | ⬜ NOT RUN | ⬜ NOT RUN | ❌ FAIL (Error 3) | Pattern A: `CreateFile` returns ERROR_PATH_NOT_FOUND — device not opened |
| `avb_i226_test.exe` | ✅ PASS | ⬜ NOT RUN | ⬜ NOT RUN | `IOCTL_AVB_ENUM_ADAPTERS` index=0 only, no `OPEN_ADAPTER`, no loop |
| `avb_i226_advanced_test.exe` | ✅ PASS | ⬜ NOT RUN | ⬜ NOT RUN | Uses `OPEN_ADAPTER` DID=0x125B hardcoded; no enumeration |
| `avb_multi_adapter_test.exe` | ✅ PASS | ❌ FAIL (err 433) | ✅ PASS | `IOCTL_AVB_OPEN_ADAPTER` returns error 433 for I210; I210 PTP samples also inconsistent |
| `avb_test_um.exe` | ✅ PASS | ⬜ NOT RUN | ⬜ NOT RUN | No `OPEN_ADAPTER`; index 0 default |
| `test_hw_state_machine.exe` | ✅ PASS | ✅ PASS | ✅ PASS | **Best pattern** — per-adapter handle + `OPEN_ADAPTER` loop over all 3 |
| `test_ioctl_qav_cbs.exe` | ✅ PASS | ⬜ NOT RUN | ⬜ NOT RUN | Pattern A (`CreateFileW`, no `OPEN_ADAPTER`) |
| `test_ioctl_ptp_getset.exe` | ✅ PASS | ⬜ NOT RUN | ⬜ NOT RUN | Pattern A (`CreateFileA`, no `OPEN_ADAPTER`) |
| `test_ioctl_ptp_freq.exe` | ✅ PASS | ⬜ NOT RUN | ⬜ NOT RUN | Pattern A |
| `test_ioctl_rx_timestamp.exe` | ✅ PASS | ⬜ NOT RUN | ⬜ NOT RUN | Pattern A |
| `test_ioctl_hw_ts_ctrl.exe` | ✅ PASS | ⬜ NOT RUN | ⬜ NOT RUN | Pattern A |
| `test_ioctl_ts_event_sub.exe` | ✅ PASS | ❌ FAIL (err 433) | ✅ PASS | Pattern B (correct enumeration) but I210 `OPEN_ADAPTER` → error 433 (driver bug) |
| `comprehensive_ioctl_test.exe` | ✅ PASS | ⬜ NOT RUN | ⬜ NOT RUN | Pattern A; has `HAS_CAP()` macro but opens I226 only |
| `test_magic_numbers.exe` | — | — | — | ❌ FAIL: 6 hex literal violations in source (adapter-agnostic scan) |
| `test_ndis_receive_path.exe` | ⚠️ 4/6 | ⬜ NOT RUN | ⬜ NOT RUN | 2/6 FAILED; Pattern A; no I210/I219 coverage |
| `test_tx_timestamp_retrieval.exe` | — | — | — | ⬜ SKIPPED — binary not present in `build\tools\avb_test\x64\Debug\` |

### Coverage Summary

- **I226**: 13 of 15 adapter-exercising tests executed (87%) — but 2/6 NDIS sub-tests failed
- **I210**: 3 of 15 executed (20%) — `test_hw_state_machine` ✅, `avb_multi_adapter` ❌, `test_ioctl_ts_event_sub` ❌
- **I219**: 3 of 15 executed (20%) — `test_hw_state_machine` ✅, `avb_multi_adapter` ✅, `test_ioctl_ts_event_sub` ✅

Only **1 test** (`test_hw_state_machine`) exercises all three adapters. The gap is **entirely structural** (Pattern A), not a capability limitation.

---

## Table 2 — Capability Eligibility

Analytical — derived from runtime capability bitmasks decoded against `INTEL_CAP_*` bit definitions, cross-referenced with Intel adapter datasheets (NotebookLM: https://notebooklm.google.com/notebook/5bcc7d84-81a5-43b7-a629-5622901872a2).

Legend: `✅ ELIGIBLE` `⛔ NOT ELIGIBLE` `〰️ AGNOSTIC` (no adapter-specific caps required) `⚠️ PARTIAL` (core eligible; sub-tests need cap gate)

| Test | Min Required Caps | I226 | I210 | I219 | Notes |
|------|------------------|:----:|:----:|:----:|-------|
| `avb_test_i210_um` | BASIC_1588 + MMIO | ⛔ target | ✅ | ⛔ target | I210-specific (CTRL/STATUS/PTP regs); refactor to open I210 by DID |
| `avb_test_i219` | BASIC_1588 + MMIO | ⛔ target | ⛔ target | ✅ | I219-specific; fix Error 3 and target I219 by DID=0x15B7 |
| `avb_i226_test` | TSN_TAS + TSN_FP + PCIe_PTM | ✅ | ⛔ | ⛔ | I226-only feature set; TAS/FP/PTM absent on I210/I219 per datasheet |
| `avb_i226_advanced_test` | EEE + PCIe_PTM + MDIO-paging | ✅ | ⛔ | ⛔ | I226-only; I219 MDIO paging uses different protocol (page 800/801) |
| `avb_multi_adapter_test` | BASIC_1588 | ✅ | ✅ | ✅ | All adapters eligible; fix error 433 driver bug first |
| `avb_test_um` | BASIC_1588 + ENH_TS (core) | ✅ | ✅ | ✅ | Cap-gated: TAS/FP/PTM/MDIO skipped for I210/I219 — refactor to iterate all adapters |
| `test_hw_state_machine` | BASIC_1588 | ✅ | ✅ | ✅ | Already correct; no change needed |
| `test_ioctl_qav_cbs` | BASIC_1588 + ENH_TS | ✅ | ✅ | ⚠️ PARTIAL | I219 is conformance-only CBS per datasheet; basic QAV IOCTLs run, advanced skip |
| `test_ioctl_ptp_getset` | BASIC_1588 + ENH_TS | ✅ | ✅ | ✅ | All 3 share these caps; note I219 is 2-step only (no 1-step timestamp) |
| `test_ioctl_ptp_freq` | BASIC_1588 + ENH_TS | ✅ | ✅ | ✅ | Frequency adjust valid on all 3; I219 2-step only does not affect freq tests |
| `test_ioctl_rx_timestamp` | BASIC_1588 + ENH_TS | ✅ | ✅ | ✅ | RX timestamp eligible on all 3 |
| `test_ioctl_hw_ts_ctrl` | BASIC_1588 + ENH_TS | ✅ | ✅ | ✅ | All 3 eligible; I219 lacks TRGTTIM/AUXSTMP regs — gate those sub-tests |
| `test_ioctl_ts_event_sub` | BASIC_1588 | ✅ | ✅ | ✅ | All 3 eligible; already enumerates all — fix I210 driver error 433 |
| `comprehensive_ioctl_test` | BASIC_1588 + ENH_TS (core) | ✅ | ⚠️ PARTIAL | ⚠️ PARTIAL | TAS/FP/PTM IOCTLs already cap-gated via `HAS_CAP()`; add adapter loop |
| `test_magic_numbers` | — | 〰️ | 〰️ | 〰️ | Source-level scan only; adapter-agnostic |
| `test_ndis_receive_path` | MMIO | ✅ | ✅ | ✅ | All 3 eligible; investigate 2/6 failures first |
| `test_tx_timestamp_retrieval` | BASIC_1588 + ENH_TS | ✅ | ✅ | ✅ | Build binary first; then run on all 3 |

### Key Finding

12 of 15 adapter-exercising tests are capability-eligible on **all three** adapters. The coverage gap is entirely due to Pattern A (no `IOCTL_AVB_OPEN_ADAPTER`), not missing hardware capability.

---

## Gap Summary and Remediation Priority

| Priority | Issue | Affected Tests | Action |
|:--------:|-------|---------------|--------|
| P0 | Driver bug: `IOCTL_AVB_OPEN_ADAPTER` returns error 433 for I210 | `avb_multi_adapter`, `test_ioctl_ts_event_sub` | Investigate driver IOCTL handler |
| P1 | `avb_test_i219.exe` Error 3 on device open | `avb_test_i219` | Fix device path / privilege issue |
| P2 | Create `tests/common/avb_test_common.h` + `.c` | All Pattern A tests | Extract best pattern from `test_ioctl_ts_event_sub.c` + `test_hw_state_machine.c` |
| P3 | Upgrade 5 IOCTL tests from Pattern A to B | `ptp_getset`, `ptp_freq`, `rx_timestamp`, `hw_ts_ctrl`, `qav_cbs` | Use `avb_test_common` helpers |
| P3 | Fix `avb_test_i210_um.c` to open I210 by DID | `avb_test_i210_um` | `OPEN_ADAPTER` DID=0x1533 |
| P4 | Fix 6 magic number violations | `test_magic_numbers` | Replace literals in `intel_i219_impl.c` + `avb_integration_fixed.c` |
| P4 | Build missing binary | `test_tx_timestamp_retrieval` | Add to build system |
| P5 | Add `comprehensive_ioctl_test` adapter loop | `comprehensive_ioctl_test` | Add enumeration + per-adapter iteration |

---

*Tables generated from: `logs/full_test_suite_20260410_192359.log`, source-code analysis of test `.c` files, and Intel adapter datasheets.*
*Last updated: 2026-04-10*
