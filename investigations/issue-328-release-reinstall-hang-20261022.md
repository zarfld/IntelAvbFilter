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

## Third reproduction: 2026-10-24 13:04, UT-CORR-009 service stop under FullSuite (`FullSuite_20261024_113438.log`)

Analysed 2026-10-26 from logs and the System event log; no dumps examined. [HW] = host log / event observation, [SRC] = source inspection, [INF] = inference.

**Trigger.** The FullSuite blind `*.exe` loop (`tools/archive/deprecated/FullSuite.ps1:300-305`, still active at `686450c`) ran the **Unsafe** `test_ptp_phc_lifecycle.exe`:
- Debug at 12:16:19: completed in 31 s, UT-CORR-009 PASS.
- Release at 13:03:58, through `Run-Tests-Elevated.ps1`, with no safety gate and no `AVB_HIL_AUTHORIZE_UNSAFE`. [HW]

The loaded driver was Release `B286A621…6B4F` (capture metadata `installedDriverSha256`). UT-CORR-009 calls `ControlService(SERVICE_CONTROL_STOP)` and then `system("… Install-Driver-Elevated.ps1 -Configuration Debug -Action Reinstall")`. [SRC `test_ptp_phc_lifecycle.c:44,62-64`]

| Time | Observation | Source |
|---|---|---|
| 13:04:09 | I219-LM (e1dexpress) and I210 (e1rexpress) event 27 "link disconnected": the filter detached from both | System log [HW] |
| 13:04:09 – 13:37:12 | **No event at all from I226 (e2fnexpress)**; the host has no usable network (UCNET eth0 → 127.0.0.1 at 13:04:19, NTP DNS failure at 13:10:24) | System log, DbgView [HW] |
| 13:33:58 | `[TIMEOUT] elevated test process PID=11000 still running after 1800s --- terminated` | FullSuite log line 11754 [HW] |
| 13:33:58 – 13:35 | The legacy loop continued `test_ptp_phc_stability`, `test_qav_cbs`, `test_registry_diagnostics`, `test_rx_timestamp`, … against a driver in an unknown stop state. **These results are invalid.** | FullSuite log [HW] |
| 13:37:01 | The LocalSuites Debug install precondition reports `Driver service: not installed`, so the service had already been deleted before its own uninstall step | FullSuite log ~13760 [HW] |
| 13:37:10 – 13:37:17 | The install disables all Ethernet adapters (`MS_IntelAvbFilter binding not found`). I226 event 27, then link up on all three; service RUNNING with Debug `45A45636…` | System log, FullSuite log [HW] |
| ~14:03 | The LocalSuites Release test build fails with `LNK1168` on `build\tools\avb_test\x64\Release\test_ptp_phc_lifecycle.exe` (cannot open for writing). The whole Release configuration was `[STOPPED] test build failed (Release)`, **no Release suite ran**. | FullSuite log 22274, 23458 [HW] |
| 2026-10-26 | The exe is writable again and no test process is running | [HW] |

**CORRECTION 2026-10-27. This is NOT the 10-22 signature.** I earlier read it as the test blocking in its own service stop, the same as 10-22. That was wrong. The installer transcript `%TEMP%\install-driver-20261024_130403.log` (Debug, the hard-coded configuration; started 13:04:04, last write **2026-10-26 20:56:41**) shows:
- the precondition check reports `Driver service: Stopped`, so the old test's `ControlService(STOP)` of the Release driver completed within about 5 s;
- the uninstall then completed: ETW unregistered, `sc stop`, `sc delete`, `Disabling: Ethernet, Ethernet 2, Ethernet 3`, netcfg removal;
- the install started: build "OLDER than installed" (a `Read-Host` that returned in the RunAs window), `.sys` copied, ETW registered;
- the transcript **ends at `netcfg.exe -l IntelAvbFilter.inf -c s -i MS_IntelAvbFilter`**. Step 4 (re-enable adapters) was never reached.

The 10-24 hang is therefore **in the netcfg filter install, with all three adapters disabled and the I226 never reporting its link-down**, and the Release driver already reported stopped. Mapping (adapters list, 10-24): Ethernet = I219 15BB, Ethernet 2 = I210 1533, Ethernet 3 = I226 125B. The I219/I210 link-down at 13:04:09 is their disable. The I226 disable did not complete: no event until 13:37:12.

**What the earlier TSN-mode history does not explain (open question).** The legacy loop's TSN tools (`avb_test_i226*`, `test_ioctl_tas`, …) ran earlier in this FullSuite under the 12:26 Release install. Whether they ran before 13:04 under that install, and whether TQAVCTRL was non-zero at 13:04, is **not yet determined**; the Release build has no DbgPrint. T1-TAS stays LIKELY, not proven.

**Infrastructure defects exposed (all [SRC] unless marked).**
1. `Run-Tests-Elevated.ps1:163` `Stop-Process -Id $childProc.Id` kills only the wrapper PowerShell. `Run-Tests.ps1`, the test exe and the test's own `Install-Driver-Elevated.ps1` child survive the timeout. [INF] An orphaned UT-CORR-009 chain deleting the service before 13:37 and holding the Release exe at ~14:03 explains both `Driver service: not installed` and `LNK1168`. **Now supported by the transcript:** the chain ran `sc delete` at about 13:04 and stayed alive until 2026-10-26 20:56:41, its last transcript write.
2. `test_ptp_phc_lifecycle.c:62-64` hard-coded `-Configuration Debug`, so a Release UT-CORR-009 reinstalled the Debug driver.
   - 2026-10-27: corrected in place in `test_ptp_phc_lifecycle.c`.
     - **Provenance:** the configuration comes from matching the installed file's SHA-256 against each installer source. The loaded build flag (`GET_VERSION`) must agree. Otherwise the test is BLOCKED and the installer is never started.
     - **Single bounded mutation path:** the test's own `ControlService(STOP)` and the `system()` call are removed. `Install-Driver.ps1 -NonInteractive -Reinstall` runs in a kill-on-close job, bounded at 300 s, with its output preserved live in `logs\ut_corr_009_install_<cfg>_<ts>.log`. The previous route, Install-Driver-Elevated, starts its child with RunAs and that child is not `-NonInteractive`, so the STOP_PENDING branch (`Read-Host` / `Restart-Computer`) could wait unbounded and invisibly.
     - **Separate verdicts:** the functional verdict (PASS/FAIL) and the restoration verdict are reported separately. Restoration comes from pre/post observation (file SHA-256, SCM state, control device, `GET_VERSION`, adapter multiset), never from the installer exit code: verified, `[CLEANUP_FAILED]` (observed difference), or `[INCONCLUSIVE_RESTORATION]` (not observable).
     - **#328 condition:** a timeout after `Stopping service...` prints a `[#328]` line with the evidence paths. No retry, no forced unload, no reboot.
   - Evidence classes:
     - The defect was demonstrated by the existing chain on HIL (this section).
     - TEST-HILGATE-082 is a **STATIC** source contract, not runtime evidence: RED with 9 reasons, then GREEN. The suite is 83/83; TEST-NOWAIT 1/1 and TEST-HILSUITE 18/18; the file compiles at /W4.
     - Runtime behaviour: **HIL verification pending**.
3. The FullSuite full `*.exe` loop is **intentional** (Q, 2026-10-26: FullSuite must always run every test, failing ones included). It therefore also runs Unsafe entries such as UT-CORR-009. The defect is in containment, not in the loop: a hanging test must not corrupt the results of later tests (see 1), and UT-CORR-009 must restore the driver it found (see 2).
4. One locked artifact (`LNK1168`) aborts the entire Release configuration. This contradicts contract C ("a build or suite failure must not prevent later independent tests or suites from running"). LocalSuites behaviour, reported only.

## Debug vs Release contrast, 2026-10-24 and 2026-10-27 (logs and System event log; analysed 2026-10-27)

**Facts [HW]:**
- **Same test sequence before each stop on 10-24.** The Debug stop (12:16:19, OK in 31 s) and the Release stop (13:03:58, hang) followed the same FullSuite loop sequence.
  - TSN tools ran under each install: Debug 11:43–12:08, Release 12:32–12:56. That includes `avb_test_i226_um`, where SETUP_TAS, SETUP_FP and SETUP_PTM were all accepted on I226 0x125B. `test_ioctl_fp_ptm` ended with FP "TC0-TC2 preemptable" and PTM enabled at 8 ns. `test_ioctl_launch_time` also ran. `test_ioctl_tas` blocked itself. Under Debug, the raw-MMIO TAS tools `chatgpt5_i226_tas_validation`, `corrected_i226_tas_test` and `enhanced_tas_investigation` also ran, at 11:43–11:44.
  - Then `test_lifecycle_coverage` disabled and re-enabled all three NICs. I226 link down/up at 12:09:36 (Debug) and 12:57:19 (Release).
  - Then the same non-TSN tests ran until UT-CORR-009.
  - No TSN-configuring tool ran after the NIC cycle in either window.
- **Successful stops** (10-24 11:36, 12:16, 12:26; 10-27 09:19, 09:58, 10:08): I219/I210 report link-down first. The I226 follows **6–9 s later**, after the 7045 install event. In the 13:04 hang the I226 reported nothing until its adapter was disabled at 13:37.
- **2026-10-27 09:58:47, corrected UT-CORR-009, Debug `CB43F1FF…`, FullSuite legacy path:**
  - PASS, restoration verified, I226 down at +7 s.
  - The adapter enumeration order changed across the reload (15BB,1533,125B → 125B,1533,15BB). This confirms the multiset comparison.
  - Evidence class: LOCAL, legacy path (diagnostic, not gate evidence). Only the success path ran.

**Theory update:**
- **T1-TAS** (TSN state left by the tools blocks the I226 pause): **weakened for 10-24**, conditional on the 12:57 NIC disable/enable having reset the I226 TSN registers. That reset is UNVERIFIED: the I226 TQAVCTRL is not in the SSOT, and Release has no READ_REGISTER. The Debug window had the same or more TSN activity and its stop succeeded.
- **T-REL** (a Release-specific driver path prevents the I226 module from completing pause/detach): **revised 2026-10-27 after the transcript correction above.** The 10-24 Release stop *reported* `Stopped`, and the hang came later, in the adapter disable / netcfg install. T-REL is UNSUPPORTED for 10-24 as a stop hang. It stays open for 10-22 14:25 ("Stopping service..."). Whether the Release driver image had actually unloaded and detached from the I226 by 13:04:04 is UNVERIFIED: Release prints nothing.
- **T-RACE** (new): a filter detach still in progress on the I226 races the installer's PnP disable / netcfg binding change. In every successful stop the I226 reports link-down 6–9 s after the I219/I210, i.e. it is the slowest stack. In 10-24 13:04 the installer disabled all adapters about 5 s after the stop. Debug timing (DbgPrint) differs from Release, which would explain a Debug pass on the same sequence. Discriminator: the corrected UT-CORR-009 no longer issues its own stop. The installer's `sc stop` + 2 s sleep + `sc delete` precede the disable, so its timing differs again.
- **T-NETCFG** (new): `netcfg -i` blocks on the I226's pending disable (the binding engine waits for the device stack) or on the INetCfg write lock. To discriminate: what the 10-24 13:37 install did differently (it also disabled adapters and ran `netcfg -i`, and succeeded while the 13:04 chain was still hung, so the write lock alone is unlikely).
- **T-I226-HALT** (new): the e2fnexpress miniport halt/disable of the I226 blocks. Against it: the 12:57 `test_lifecycle_coverage` disable/enable of the I226 completed with the same TSN history.
- Still to be determined: whether any Release-driver stop has ever completed *before* an adapter disable (pair 7045 events with `%TEMP%\install-driver-*.log` configurations: 40 transcripts since 10-21, 28 Debug and 12 Release).

**Next evidence (read-only):**
1. Pair every 7045 event since 10-08 with the configuration being replaced and the I226 link-down lag, to look for a completed Release stop.
2. Diff the pause, send-complete and unload paths for `DBG` / `NDEBUG` conditionals.
3. The Release UT-CORR-009 of the FullSuite run in progress (10-27, Release installed 10:09) is the next hardware data point. If it hangs, the corrected test must report `[#328]` after 300 s.

## 2026-10-27 10:47:46: Release reinstall completes after the 10-24 TSN history is replicated (FullSuite_20261027_091718, legacy loop)

**Facts [HW]:**
- Release driver `E516B4FE…`, loaded build flag Release, identical to the Release installer source.
- The same history as 10-24 13:04 under this Release install:
  - `avb_test_i226_um` at 10:14:38/10:14:42, with SETUP_TAS, SETUP_FP and SETUP_PTM accepted on I226 0x125B;
  - `test_hot_plug` 10:36:37, `test_ioctl_fp_ptm` 10:37:53, `test_ioctl_launch_time` 10:38:01, `test_ioctl_tas` 10:39:11;
  - the `test_lifecycle_coverage` NIC cycle at 10:39:56.
- Corrected UT-CORR-009: `Install-Driver.ps1 -NonInteractive -Reinstall`, exit 0, phase complete.
  - Installer log: "Stopping service..." → "Disabling: Ethernet / Ethernet 2 / Ethernet 3" → netcfg removal → "Build is newer" → `netcfg -i` "[OK] netcfg installation completed" → Step 4 re-enable → "Installation complete!".
  - PASS, restoration verified (`logs\test_ptp_phc_lifecycle.exe_20261027_104746.log`, `logs\ut_corr_009_install_Release_20261027_104750.log`).
- System log: I219/I210 link-down 10:47:56, 7045 at 10:48:01, **I226 link-down 10:48:02 (+6 s)**, all three links up by 10:48:09. This is the normal signature.

**Differences from 10-24 13:04 (the hang):**
1. **Stop timing:** there, the stop came from the test's own `ControlService(STOP)`, and the installer disabled the adapters about 5 s later. Here, the installer's `sc stop` + 2 s sleep + `sc delete` + driver-store cleanup come before the disable.
2. **Downgrade prompt:** there, a Debug build was installed over Release (hard-coded configuration), "Build is OLDER" triggered a `Read-Host` (which returned) in an interactive RunAs child. Here, the same Release build was reinstalled with no prompt.
3. Elevated wrapper vs direct `-NonInteractive` child.

**Theory status (one observation, an anecdote):**
- **T-REL** as "a Release driver stop/reinstall hangs": **refuted as a universal claim** by this run. A Release-specific defect that is conditional on timing stays possible.
- **T1-TAS** (TSN history alone): **further weakened**. The same TSN history and NIC cycle preceded a successful Release reinstall.
- **T-RACE** (a still-detaching I226 stack meets the PnP disable / netcfg change): **consistent**, since difference 1 is exactly that timing. Not proven.
- **T-NETCFG / downgrade path** (difference 2: a different package over the loaded driver, after a prompt): **open**.
- **10-22 14:25** ("Stopping service..." with a monotonic I226 `OutstandingSendNBLs`) remains a separate, unexplained stop-phase hang.

**Discriminating experiment (requires Q's authorization; mutating):** reproduce difference 1 alone. That means `ControlService(STOP)` followed within about 5 s by the installer's adapter disable, under Release, with everything else equal. It would be done through the existing chain only once Q decides how. The corrected UT-CORR-009 no longer produces that timing.
