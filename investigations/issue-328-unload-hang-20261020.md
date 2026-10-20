# #328 — `sc stop IntelAvbFilter` hang, 2026-10-20 09:26:34 (live HIL incident)

Status: **Phase A (read-only) complete — root cause NOT yet proven.** Live state preserved.
Host: DESKTOP-RSUO2LV, Windows 10.0.26200. Source HEAD `09c0efd` (clean except untracked `external/intel_avb.worktrees/`).

## Installed / loaded driver identity

| Item | Value | Status |
|---|---|---|
| ImagePath | `DriverStore\FileRepository\intelavbfilter.inf_amd64_aacfcc2bad97daa2\IntelAvbFilter.sys` | CONFIRMED |
| SHA256 (DriverStore == System32 == `build\x64\Debug\IntelAvbFilter\IntelAvbFilter.sys`) | `9A098B715BC6FF13DFE2AF0FE016E3816938B80A349E81CAFA4B2F19C00908E1`, 255768 B, 08:50:01 | CONFIRMED |
| PE TimeDateStamp / SizeOfImage | `0x6AD70F18` / `0x41000` | CONFIRMED |
| PDB | RSDS `{FA4E5501-ACEF-4D25-B31B-BD4793F6549C}` age 0x71, private symbols, `symchk` PDB Matched TRUE | CONFIRMED |
| Loaded image == this file | DriverEntry 08:51:53 after file write 08:50:01; no reinstall since | LIKELY (confirm with `lmvm` in a live dump) |
| Release #279 (`build\x64\Release`, 77584 B, 09:24:54) | installer transcript stops at `Stopping service...` | NOT installed — CONFIRMED |

Copy of sys+pdb preserved outside the build tree (a Debug rebuild would overwrite the PDB):
`C:\Users\User\AppData\Local\Temp\claude\c--Users-User-IntelAvbFilter\ea816af6-4891-4f64-b858-63a665120a22\scratchpad\evidence_build278\`

## Context → adapter map (09:26 run)

| ctx | filter instance | Adapter | Evidence | Status |
|---|---|---|---|---|
| `FFFFE78C78231000` | `FFFFE78C795CFD60` | I219-LM 0x15BB (`Ethernet`, e1d.sys 12.19.2.57) | `OPEN_ADAPTER targetFilter=…795CFD60 DID=0x15BB` (09:07:15) + FilterPause pairing at 09:26:34.540 | CONFIRMED |
| `FFFFE78C7E5D8000` | `FFFFE78C787EBD60` (by elimination) | I210 0x1533 (`Ethernet 2`, e1r.sys 14.1.24.0) | I210 re-attached as `787EBD60` after 09:06:44 detach; ctx alloc line lost | LIKELY |
| `FFFFE78C7FC53000` | `FFFFE78C79AD8D70` | I226-LM 0x125B (`Ethernet 3`, e2fn.sys 2.1.5.7) | `CONTEXT_ALLOC 7FC53000` + `HwID DEV=0x125B` 09:06:38.104 | CONFIRMED |

## FACTS (verified)

1. **DbgView captures are lossy.** `dbgview_test_lifecycle_coverage…090630.log` holds 5384 lines for sequence numbers 147…30898 (~80 % missing). The 09:26 capture has 606 sequence gaps, incl. `2000..2061` (62 msgs) between 09:26:34.555 and 09:26:34.901. Persisted DbgView filters are `*` / blank, so this is not an include filter. ⇒ *Absence of a probe line is not evidence that code did not execute.*
2. 09:26:34.533 FilterUnload → `g_FilterStopping=1` → `IntelAvbFilterDeregisterDevice()` → `NdisFDeregisterFilterDriver`. `FilterUnload: COMPLETE` never seen.
3. 09:26:34.540 FilterPause I219 (ctx 78231000); 09:26:34.555 FilterDetach I219, remove lock returned, AvbStopTimers entered/exited. Next 62 messages lost.
4. Last RX for I210 ctx 7E5D8000: 09:26:34.543 (drop path).
5. I226 ctx 7FC53000 logs `RX-CYCLE state=4` (FilterRunning) + `PAUSE-DROP` continuously through 11:12:33 (109,109 lines) ⇒ **NDIS never called FilterPause for the I226 module.**
6. `GetIfTable2` at 11:23:12 (filter-module interfaces): `Ethernet 3-IntelAvbFilter NDIS LightWeight Filter-0000` present; **no IntelAvbFilter module on `Ethernet` (I219) or `Ethernet 2` (I210)**. No interface has the Paused flag.
   ⇒ I219 and I210 FilterDetach **completed**; the original "stalled in AvbCleanupDevice" localization is **DISPROVEN**.
7. Source-bound ping to GW 157.247.1.9 (11:20): from I219 .71 2/2 OK; from I210 .120 2/2 OK; from I226 .12 fail (local "destination unreachable" = ARP unanswered, consistent with RX drop).
8. I226 Sent counters frozen (SentUnicast 5886, SentBroadcast 2231, SentMulticast 179687) 11:21:03 → 11:21:31, while I219 / I210 multicast advanced.
9. FullSuite `[LCY]`: the **instrumented** I226 gauge `OutstandingSendNBLs` climbs monotonically 49 → 1649 (first seen at `test_ptp_crosstimestamp`, last at `test_ptp_phc_stability --case UT-CORR-006`). Largest jumps: `test_zero_polling_overhead` +487 (09:14:54–09:15:31), `test_ptp_event_latency` +875 (09:15:35–09:19:05). No other adapter reports any growth. I226 `OidRequestCount == OidCompleteCount` at the same points.
10. `stats_outstanding_send_nbls` writers: `filter.c:2053` (+n in FilterSendNetBufferLists), `filter.c:1829` (−n on entry to FilterSendNetBufferListsComplete, clamped at 0), `avb_integration_fixed.c:4707` (reset = 0).
    **CORRECTION (Q review, 2026-10-20):** the gauge **can over-count**. In FilterSendNetBufferLists the increment (`filter.c:2053`) precedes the `#if DBG` branch (`filter.c:2060-2086`) that, when `pFilter->State != FilterRunning`, completes the NBLs directly with `NdisFSendNetBufferListsComplete` (`filter.c:2079`); those NBLs never reach our FilterSendNetBufferListsComplete, so they are never decremented. `DBG=1` is defined for build #278 (`IntelAvbFilter.vcxproj:119`). The RX gauge has the same pattern (`filter.c:2357` increment; `#if DBG` not-Running branch `filter.c:2366-2380` returns without decrement).
    ⇒ **1649 is NOT a proven count of pending NBLs.** It is consistent with real missing completions *or* with accounting artifacts from sends during non-Running states (pause/restart/lifecycle transitions), or a mix. Only a dump can give the real pending count.
14. **NDIS self-generated live dump** `C:\Windows\LiveKernelReports\NDIS-20261020-0916.dmp` (1,330,487,296 B, 09:16:16) — written **during `test_ptp_event_latency` (09:15:35–09:19:05)**, ~2 min after the I226 TIMINCA mutation (`test_ptp_freq_complete@125B` 09:14:14) and ~10 min **before** the unload. Trigger not yet known (needs `!analyze -v`). Independent of our instrumentation. (Found by Q via elevated E1.)
15. CI-phase timeline from log file times (all 2026-10-20): `test_ptp_freq@125B` 09:14:11 → `test_ptp_freq_complete@125B` 09:14:14 → TC-INFRA-STATE CLEANUP_FAILED 09:14:29 → `test_tsn_ioctl_handlers` [ReadOnly] 09:14:39 → TC-INFRA-STATE 09:14:54 → `test_ptp_crosstimestamp` 09:14:54 → `test_zero_polling_overhead` 09:14:54–09:15:31 → `test_hw_state` 09:15:31 → `test_ptp_event_latency` 09:15:35–09:19:05 (NDIS dump 09:16:16) → `test_event_latency_4ch` 09:19:06 → `test_event_nf_zero_polling` 09:19:37 → … → last tests 09:24:0x.
11. `test_lifecycle_coverage` uses `CM_Disable_DevNode` (miniport halt/re-init). I226 re-attached 09:06:38 ⇒ the leak started **after** 09:06:38. `test_ioctl_tas` ran 08:58:50 (before), so its TAS state was LIKELY cleared.
12. RX drop path (`filter.c:2397-2412`) returns NBLs with `NdisFReturnNetBufferLists` when CAN_PEND; otherwise leaves ownership with caller. Correct per NDIS contract.
13. Kernel debugging not enabled (SystemStartOptions has no DEBUG). `kd.exe` and `winxp\ndiskd.dll` installed. CrashDumpEnabled=3. No NDIS / Kernel-PnP System events since 09:20; NDIS/Operational empty. LiveKernelReports not readable without elevation.

## THEORIES (ranked)

| # | Theory | Status | For | Against | Missing observation |
|---|---|---|---|---|---|
| T1 | e2fn.sys / I226 TX path stopped completing send NBLs after 09:06:38; NDIS cannot complete the **pause of the protocol bindings above** the I226 filter stack, so it never reaches our FilterPause/FilterDetach for the I226 module → `NdisFDeregisterFilterDriver` blocks | LIKELY / POSSIBLE — **not corroborated** | F5, F6, F8; only I226 affected; I219/I210 with identical LWF stacks detached normally; NDIS self-dump during I226 activity (F14, trigger unknown) | Frozen TX counters are also explained by already-paused protocols; the gauge (F9) can over-count via the DBG direct-completion path (F10 correction); exact holder (miniport vs. lower LWF) unproven | `!analyze -v` of the 09:16 NDIS dump; `!ndiskd.netadapter`/`miniport` state for I226; `!ndiskd.mopen` per binding (Pausing + outstanding sends); `!ndiskd.pendingnbls`; FilterUnload thread stack; real pending count vs. `stats_outstanding_send_nbls` and `MS_FILTER.State` history |
| T1b | Part or all of the 1649 gauge value is an accounting artifact of the DBG not-Running branch (`filter.c:2060-2086`) | POSSIBLE | F10 correction; DBG=1 in build #278 | Branch only fires for sends while our module is not Running; how often the I226 module passed through Pausing/Paused/Restarting after 09:06:38 is unknown (DbgView lossy) | Real pending NBL count from dump vs. gauge; count of FilterPause/FilterRestart on I226 after 09:06:38 |
| T1a | Trigger of T1 = I226 HW state mutated by HIL tests (TIMINCA 0x18000000↔0x08000000 by `test_ptp_freq_complete@0x125B` and ReadOnly `test_tsn_ioctl_handlers`; TSYNCTXCTL writes by `hw_timestamping_control` 09:07:30; `SETUP_TAS accepted` on adapter 2 by `avb_test_portability@125B`) | POSSIBLE | TIMINCA mutation documented; onset after 09:06:38 coincides with mutating I226 tests | Onset not time-resolved; portability TAS at LCY line 10402 is *after* the climb started | I226 TX queue / TSN register snapshot (TXDCTL, TQAVCTRL, BASET, QBVCYCLET, TSYNCTXCTL, TDH/TDT) from the dump; per-test onset |
| T2 | A lower LWF (below IntelAvbFilter) holds the I226 send NBLs | UNLIKELY | — | Same LWFs on I210/I219, no growth there | Stack order + pending NBL owner from dump |
| T3 | Pending OID through our I226 module blocks the pause | POSSIBLE (low) | — | Request==Complete counts mid-suite | `!ndiskd.oid` |
| T4 | Hang inside AvbCleanupDevice / FilterDetach (I219) | **DISPROVEN** | original snapshot | F6, F7 | — |
| T5 | `g_AvbContextListLock` deadlock in AvbContextListRemove | **DISPROVEN** for this hang | — | F6; a DISPATCH spin for 2 h would trigger 0x133 | — |
| T6 | Subscription-lock contention in cleanup | DISPROVEN for this hang | — | F6 | — |
| T7 | RX drop path prevents NDIS quiescence | NOT SUPPORTED | — | F12; I219/I210 paused with the same path | — |
| T8 | Control-device deregistration before NdisFDeregisterFilterDriver | Unrelated finding (not blocking) | — | — | — |

## Contributing / unrelated defects observed (not root cause)

- **C1 (impact amplifier):** `g_FilterStopping` is global and set before any stack is paused; any stack NDIS has not yet paused black-holes **all** RX for the whole duration (I226 has been RX-dead since 09:26:34).
- **C2 (diagnosability):** FilterUnload deregisters the control device before `NdisFDeregisterFilterDriver`, removing the IOCTL statistics channel exactly when it is needed.
- **C3 (diagnosability):** DbgView capture is lossy under the per-packet RX probes; `[#328-RX-CYCLE]` / `INDICATE` DL_ERROR probes flood the buffer.
- **C4 (misleading rationale):** comment at `filter.c:2386-2394` claims NDIS waits for an RX-quiescent window before FilterPause; that is not the NDIS model.

## HIL safety findings (separate from the hang)

- S1 `test_ptp_freq_complete.exe@0x125B` [Mutating:Target]: CLEANUP_FAILED, I226 TIMINCA `0x18000000 → 0x08000000` (FullSuite line 5187), HALT.
- S2 `test_tsn_ioctl_handlers.exe` [ReadOnly]: TC-INFRA-STATE-001, I226 TIMINCA `0x08000000 → 0x18000000` — a ReadOnly-classified test mutates PHC state.
- S3 `test_lifecycle_coverage` uses `CM_Disable_DevNode` on all three NICs incl. the connectivity adapters (I210/I219), executed outside the Run-Tests-CI classification gate (DbgView-era phase of FullSuite).
- S4 Mutating I226 tests ran while the I226 send-completion gauge was already abnormal; no gate checks `OutstandingSendNBLs`.

## Not yet examined

- GitHub Actions run 39379255772 (separate context) — not analyzed.
- October 19 DbgView captures — not compared.
