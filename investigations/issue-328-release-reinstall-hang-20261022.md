# #328 — Release reinstall hang during archived FullSuite, 2026-10-22 14:25 (reproduction)

Status: **Evidence analysis complete (read-only). Reproduces the 2026-10-20 I226 unload-hang signature. Root cause NOT proven.**
Live hang state was **not preserved**: operator-initiated orderly reboot 14:40:22 (System 1074, `wmiprvse`, Kernel-Power 109 "Reboot"; no bugcheck 41/6008/1001).
Host DESKTOP-RSUO2LV. Source HEAD `9526092` (clean except untracked `external/intel_avb.worktrees/`).

Evidence classes: **[HW]** hardware/driver observation from today's logs (single run); **[SRC]** source inspected; **[INF]** inference.

## Driver identity

| Item | Value | Class |
|---|---|---|
| Debug build #296 (FullSuite Debug phase) | `IntelAvbFilter.sys` 255,768 B, SHA256 `7682BA7995B3E718A7513AE50A47A609E10312A3DA92222E6E52E0841C4D255F` | [HW] FullSuite log |
| Release build #297 (built 14:23, never installed) | 77,584 B, SHA256 `E6E056F79C359C5A7900EAEEA481C6CAD8D66BF55EB2C0F7C29218BC75C9078F` | [HW] FullSuite log |
| Loaded at hang (14:25–14:29) | Debug #296 (Release install never passed `Stopping service...`) | [HW] |
| Installed now (after 15:02 reinstall, boot 14:46:11) | System32 `IntelAvbFilter.sys` = Debug #296 SHA above | [HW] 15:55 read |

## Where the Release install stopped

FullSuite transcript (UTF-16): `Uninstalling Driver (Method: netcfg)` → `Unregistering ETW manifest` → `[OK]` → **`Stopping service...`** (14:25:12) → no further output → `TerminatingError(): "Die Pipeline wurde beendet."` at 14:29:07 (**operator cancellation**, not a test verdict).
FullSuite then continued into the Release test loop at 14:29:07 (`avb_comprehensive_test.exe_20261022_142907`) **with the Debug #296 driver in an unload state** ⇒ every Release-phase result after 14:29:07 is **invalid evidence** (wrong driver, stopping stack) and must not be counted.

## DbgView `dbgview_install-driver-reinstall_20261022_142511.log` (14:25:12.534 – 14:29:07.586, 18,721 lines)

| ctx | Adapter | Last RX | State | Class |
|---|---|---|---|---|
| `FFFFD80171A0C000` | (I219 or I210) | 14:25:14.609 | `state=4` then gone | [HW] |
| `FFFFD80180758000` | (I219 or I210) | 14:25:14.593 | `state=4` then gone | [HW] |
| `FFFFD8017EDF3000` | **I226-LM 0x125B** (`AvbPostTimestampEvent [Adapter 0x125B]` with this ctx) | **14:29:07.144** | `state=4` = `FilterRunning` (`src/filter.h:298`) throughout, 4,196 RX cycles | [HW] |

- First `[#328-PAUSE-DROP] … g_FilterStopping=1` at 14:25:15.141 ⇒ `FilterUnload` ran (`g_FilterStopping` is set only at `src/filter.c:1191`). 4,157 drops, all on the I226 ctx after 14:25:14.6. [HW]+[SRC]
- No FilterPause/FilterDetach/`FilterUnload: COMPLETE` line was captured. DbgView is lossy (2026-10-20 F1) ⇒ absence is not proof. [HW]
- 14:31:34 retry: I219 and I210 log link-down; **I226 logs no link-down** (its stack could not halt) [HW System log]. Shutdown 14:40:31 → 14:46:01 took 5.5 min (consistent with a stuck stack, UNVERIFIED).

⇒ **Same signature as 2026-10-20 F5/F6**: on FilterUnload, I219/I210 detach within ~2 s; NDIS never pauses the I226 module; RX black-holed (C1).

## I226 send-completion gauge (instrumented `OutstandingSendNBLs`, can over-count — 2026-10-20 F10)

Parsed per DEV (not per adapter index — see "adapter index instability"):

| Time | Test | I226 0x125B | I219 0x15BB |
|---|---|---|---|
| 13:44–13:45 | (several) | 0 → 0 (no delta) | returns to 0 after every test |
| 13:45:33 | `ptp_clock_control_test` | 0 → **1** (never drains again) | — |
| 13:52:52–13:53:08 | `test_build_sign`, `test_cleanup_archive`, `test_clock_config` | 3→2, 4→3, 5→4 (rises *between* tests) | 0→9, 0→21, — |
| 13:53:46 | `test_device_idle_regression` | 45 → 176 | — |
| 14:07:28 | `test_ioctl_offset` | 674 → 0 (gauge reset path) | — |
| 14:17:40 | `test_tsn_ioctl_handlers_um` | 52 → 55 | — |
| 14:17:48 | `test_ts_event_sub` | 235 → 1645 | — |
| 14:23:06 | `tsauxc_toggle_test` | 2141 → 2145 | — |

I219 drains to 0 every time; I226 accumulates monotonically from 13:45:33 ⇒ I226 send completions stopped being returned (or the gauge over-counts, F10). Onset bound: after 13:44:54, at or before 13:45:38. [HW]

## I226 TSN/TAS register writes before onset (tool read-backs, I226 0x125B)

| Time | Test (dispatched by FullSuite blind `*.exe` loop) | Observation | Class |
|---|---|---|---|
| 13:41:36 | Debug reinstall (adapter disable/enable) | succeeded | [HW] |
| 13:42:44 | `avb_i226_test` | "TAS Enabled: NO" | [HW] |
| 13:42:52 | `avb_multi_adapter_test` | "TAS enable write successful", "TAS GATE LIST PROGRAMMING: SUCCESS" (raw writes) | [HW] |
| 13:43:44 | `avb_test_i226_um` | `SETUP_TAS` accepted (status 0) → driver `setup_tas` sets TQAVCTRL TSN\|EQAV (2026-10-20 F24) | [HW]+[SRC] |
| 13:43:59 | `avb_test_portability` | `TC-PORT-004/adapter:0 … SETUP_TAS accepted` (adapter 0 = I226 at that time) | [HW] |
| 13:44:07 | `chatgpt5_i226_tas_validation` | `TQAVCTRL_BEFORE (0x3570) = 0x00000009` (TSN mode already on); writes TQAVCTRL=`0x00800009` (bit 23 — **not** igc FUTSCDDIS 0x80, A2 table), QBVCYCLET(_S)=1 ms, BASET_H/L, `0x3300`, `0x3340–0x334C`, `0x3380–0x338C` (addresses that per igc are **not** STQT/ENDQT/TXQCTL); no restore | [HW]+[SRC] |
| 13:44:22 | `corrected_i226_tas_test` | same register family rewritten; `TQAVCTRL_FINAL = 0x00800009`; no restore | [HW] |
| 13:44:30 | `critical_prerequisites_investigation` | writes `0x08600–0x08614` ("TAS_CTRL/GATE") on I226 | [HW] |
| 13:45:33 | `ptp_clock_control_test` | first I226 send that never completes | [HW] |
| 14:17:33 | **stale** `test_tsn_ioctl_handlers.exe` (2026-04-08, SHA256 `34F23B24…950A`, identical to 2026-10-20 F23; Build-Tests entry Disabled) | `SETUP_TAS`, `SETUP_FP`, `SETUP_PTM` SUCCESS 0x00000000 | [HW] |
| 14:25:15 | Release reinstall `FilterUnload` | I226 never paused → hang | [HW] |

Comparison: the 13:41 Debug reinstall (before these tests) unloaded cleanly in ~22 s. LocalSuites (gated runner, 12:47–13:35) reinstalled without a hang; the gated lanes do not dispatch these TSN/raw-MMIO tools (coverage gaps / unregistered). [HW]

## Theories (updated)

| # | Theory | Status | For | Against / missing |
|---|---|---|---|---|
| T1-TAS | TSN transmit mode on the e2fn-owned I226 without a valid gate schedule (driver `setup_tas` and/or raw writes by legacy TAS tools) stops I226 TX → send NBLs never complete → NDIS cannot pause the I226 stack → `NdisFDeregisterFilterDriver` blocks | **LIKELY (2nd independent occurrence), NOT PROVEN** | TQAVCTRL=0x9 read back 13:44:07; onset of non-draining I226 sends ≤ 1.5 min later; only I226 affected; identical unload signature both days | true STQT/ENDQT (igc 0x3324/0x3334) values never read; no live dump of the hang; no NDIS STUCK_NBL dump checked for today (needs elevated listing); no controlled single-variable reproduction |
| T1-RAW | Raw MMIO writes by legacy tools to registers of unknown meaning (`0x3300`, `0x3340–0x338C`, `0x8600–0x8614`, TQAVCTRL bit 23) are the TX-blocking action, independent of driver `setup_tas` | POSSIBLE | writes precede onset; addresses unverified | not separated from T1-TAS (both happened within 70 s) |
| T1b | Gauge growth is a DBG accounting artifact (F10) | POSSIBLE for part | DBG build | I219 with the same code drains to 0 |
| T-STALE | The stale Disabled binary is required for the hang | **UNLIKELY** today | it ran (14:17) | onset at 13:45:33 precedes it by 32 min |

## Adapter index instability (separate infrastructure defect)

Driver enumeration index ↔ DEV mapping changed during the run: at 13:46 `Adapter0 = DEV_125B, Adapter2 = DEV_15BB`; at 14:17 `Adapter0 = DEV_15BB, Adapter2 = DEV_125B` (LCY blocks). Anything keyed by adapter index (`perf_baseline_*_ai<N>.dat`, `-AdapterIndex`, `TC-PORT-004/adapter:<N>`) is not stable across a run. [HW]

## Infrastructure finding: the archived FullSuite blind loop (`9526092`)

`tools/archive/deprecated/FullSuite.ps1` now runs **every `*.exe` in the test output directory** through `Run-Tests-Elevated.ps1` (blacklist: `test_s3_sleep_wake.exe`, `test_vv_corr_001_stability.exe` only). That path has no provenance and no safety classification (TEST-HILPATH-005, RED since before `4826c74`). It executed: stale binaries absent from the current manifest (`test_tsn_ioctl_handlers.exe` 2026-04-08; `test_i210_frequency_ops.exe`, `test_i210_timinca_codec.exe` 2026-10-19 — not produced by today's Build-Tests), Disabled targets, and raw-MMIO TSN tools against the live I226 with no restoration. This is the dispatch path that preceded both #328 reproductions.

## Evidence still obtainable (elevated, read-only)

1. `C:\Windows\LiveKernelReports\**` and `C:\Windows\Minidump` listing for 2026-10-22 (NDIS `0x15E` STUCK_NBL live dump between 13:45 and 14:40?).
2. I226 TSN registers **now** (after reboot + reinstall; expected reset values) — read-only `READ_REGISTER` of TQAVCTRL 0x3570, igc STQT(0..3) 0x3324+4i, ENDQT(0..3) 0x3334+4i, TXQCTL(0..3) 0x3344+4i, BASET 0x3314/0x3318, QBVCYCLET 0x331C — to establish the post-reset baseline for a later controlled reproduction.
