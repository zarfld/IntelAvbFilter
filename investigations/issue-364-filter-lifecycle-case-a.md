# #364 Case A: FilterPause and FilterRestart, no schedule

Status 2026-11-06: **Stage 0 passed. Stage 1 reached the end of the dry run on the I226, with the official wrapper verdict `INCONCLUSIVE_RESTORATION`, exit 1, stamp `20261106_151348`. No FilterPause or FilterRestart has ever been triggered.** The operator starts Case A; see "Results", "Case A plan" and "Blockers". Governing issue: #364. Evidence class of any run: `LOCAL_DIAGNOSTIC`, never hardware RED or GREEN. No relationship to #328 is claimed.

## Facts

Source facts, read from the code, unless marked otherwise.

| # | Fact | Where |
|---|---|---|
| F1 | `IOCTL_FILTER_RESTART_ONE_INSTANCE` returns success when no instance matches | `src/device.c:654-658`: `Status` stays `STATUS_SUCCESS` |
| F2 | It also discards `NdisFRestartFilter`'s status, so a refused request returns success | `src/device.c:662`; the prototype returns `NDIS_STATUS` |
| F3 | Instance lookup is a prefix compare, and stops at the first match | `filterFindFilterModule`, `src/device.c:1295` |
| F4 | `IOCTL_FILTER_RESTART_ALL` does nothing | `src/device.c:647` |
| F5 | `FilterPauseCount` and `PauseRestartGeneration` move at FilterPause entry | `src/filter.c`, FilterPause |
| F6 | `FilterRestartCount` moves at FilterRestart entry, before the bring-up | `src/filter.c:870`, bring-up at `:1051` |
| F7 | `pFilter->State` becomes `FilterRunning` before the bring-up | `src/filter.c:1008` |
| F8 | FilterRestart discards the bring-up result | `src/filter.c:1051`: `(void)AvbBringUpHardware` |
| F9 | A failed re-init leaves `hw_state` at `PTP_READY`: `AvbMarkPtpReady` only raises it and the failure path lowers nothing | `src/avb_integration_fixed.c:173`, `:575` |
| F10 | `TxPackets`, `RxPackets` and `TxBytes` have no increment site in `src`, so they are not traffic evidence | `grep stats_tx_packets src` |
| F11 | AVB IOCTLs have no filter-state gate, so MMIO can race the bring-up | no `->State` check in `avb_integration_fixed.c` or the AVB path of `device.c` |
| F12 | `OPEN_ADAPTER_BY_LUID` only sets `FsContext`. `GET_CLOCK_CONFIG`, `GET_STATISTICS`, `GET_HW_STATE` and `QUERY_HW_SNAPSHOT` only read | `src/device.c:1234`, `src/avb_integration_fixed.c` |
| F13 | An unbound handle falls back to an adapter search for AVB IOCTLs, so the tool binds every handle it uses for them. The trigger's handle stays unbound: filter IOCTLs do not take that path | `src/device.c:771-800` |
| F14 | `FilterRunType` is 0 in the INF, marked Optional | `IntelAvbFilter.inf` |
| F15 | `QUERY_HW_SNAPSHOT` defines TIMINCA, TSAUXC, timestamp control, target time, TQAVCTRL and RAL0/RAH0. It does not cover BaseT, cycle or window registers. **Observed on the I226:** SRRCTL0 to 3 and TQAVCTRL read `NOT_IN_SSOT` (state 2), so TQAVCTRL there comes only from TC-TSNREG-001 | `include/avb_ioctl.h:211-224`; `logs/ssot_register_validation_test_20261106_151202.log` |
| F16 | Instance names are `{interface GUID}-{filter GUID}-0000`. Seen on the I210, and on the I226 as `{5989BA55-D568-47A4-AFBF-B47F27E1B435}-{3F74AE86-14F9-4E79-9445-5B1E52CCD192}-0000`: first GUID is its `InterfaceGuid`, second is `NetCfgInstanceId` in the INF | `logs/dbgview_test_ptp_phc_stability.exe_20260430_123620.log` line 2014; `logs/diag-i364-dryrun-20261106_151348.log` |
| F17 | `Install-Driver.ps1 -Action Reinstall` disables **every adapter with the filter bound**, runs `netcfg -u`, deletes all cached driver packages, and has a path that offers `Restart-Computer -Force` | `tools/setup/Install-Driver.ps1:249`, `:305-340` |
| F18 | #350 requires no MMIO unless the filter is Running | #350 Technical Constraints |
| F19 | Each AVB IOCTL, statistics included, briefly takes `FilterListLock` and the remove lock. FilterPause takes neither. FilterRestart takes `FilterListLock` only when the control device was destroyed, so not on a normal restart. Polling therefore shares no lock with the normal path, by source reading only, and still costs CPU | `src/device.c`, `src/filter.c:1016` |

Documented NDIS behaviour, from Microsoft Learn, retrieved 2026-11-05:

- `NdisFRestartFilter` "schedules a pause and then a restart operation for the specified filter module". It returns success if it started the operation.
- A driver-stack restart runs miniport, then filters bottom-up, then protocols.
- If a filter module does not restart, NDIS detaches it, and a mandatory filter terminates the stack.
- **Not documented:** whether a single `NdisFRestartFilter` call reaches `MiniportPause` or `MiniportRestart`. None of the pages read mentions a miniport reset or `MiniportHaltEx`.

Provider metadata, read from this machine with `wevtutil gp` and `logman query providers`:

| Event | Message | Keyword |
|---|---|---|
| 10312 | Filter entering state. Fields: `IfGuid`, `State` (UInt8), `Location`, `MiniportIfGuid`, `MiniportAdapterName`, `FilterInstanceName`, `FilterFriendlyName` | `0x100` |
| 10103 | Filter entering state. Fields: `IfGuid`, `State`, `Location` | `0x100` |
| 10043 | Miniport is in a state. Fields: `IfGuid`, `IfIndex`, `NetLuid`, `Action` (UInt32) | `0x100` |
| 10007 | Calling miniport reset | `0x4000`, `0x4` |
| 10058 | Halt the miniport | `0x4000`, `0x10` |
| 10050 | Miniport restart failed | `0x4` |
| 10104 to 10107 | Media connect, disconnect, oper status | `0x200` |

**UNVERIFIED:** that any of these fire for this trigger. `State` and `Action` are bare numbers with no published value map, so their meaning has to be read from an observed sequence. "Entering" a state is not obviously the same as completing it. The `Microsoft-Windows-NDIS/Operational` and `/Diagnostic` channels are disabled and hold no history, so no past event can show this. Limit of that reading: `wevtutil gp` listed no value map on 2026-11-06, but that output may not export maps at all, so only a real decoded event settles how `State` renders.

Machine state, read 2026-11-06 15:45, build 26300: three adapters are Up with default routes through the same gateway, so an observer cannot be inferred from routing: `Ethernet` I219-LM ifIndex 16, `Ethernet 2` I210 ifIndex 13, `Ethernet 3` I226-LM ifIndex 8. Which adapter carries management is in "Management path (RealVNC)". The driver is usable again: Debug `C1A98CF9...` installed and equal to the build. The earlier reinstall failures stay unexplained: one at 20:51 on 2026-11-05 stayed on disabling an adapter for about 15 minutes until terminated, and one at 21:07 offered a reboot with "installation will auto-complete", which `Install-Driver.ps1:246` shows is an unimplemented TODO. No cause is claimed.

## Theories

Each predicts something a run can contradict.

| # | Theory | Predicts |
|---|---|---|
| T1 | Source reading: the bring-up reseeds the PHC | `phc DISCONTINUOUS`, implied PHC zero just after the Restart entry |
| T2 | The path differs from the source reading | PHC keeps advancing, drift near 0 |
| T3 | NDIS restarts the whole stack, miniport included | Miniport state events on the target |
| T4 | NDIS restarts the filter only | Filter state events, no miniport events |
| T5 | The trigger reaches nothing | `NOT_TRIGGERED`: #364 is BLOCKED until another trigger is found |
| T6 | The miniport resets or the stack is rebuilt | Event 10007 or 10058, link events, `CONTEXT_REPLACED` |
| T7 | Restart runs but the bring-up fails silently | Traffic resumes with TIMINCA, TSAUXC or the PHC wrong, and `hw_state` unchanged (F9) |

## What must be established, and what can establish it

| Required | Evidence available today | Strength |
|---|---|---|
| Exact instance | The record starts with `{interface GUID}-`, exactly one does, and no record is a prefix of it (TC-MOCK-014); deltas on the target only; `FilterInstanceName` and `MiniportIfGuid` in event 10312 | Layout hardware-observed on the I210 and I226 (F16). Event 10312 unproven |
| FilterPause entered | `FilterPauseCount`, `PauseRestartGeneration` | Direct |
| FilterPause completed | FilterRestart runs only from Paused, per the NDIS state table; filter state event | Inferred; direct only if ETW fires |
| FilterRestart entered | `FilterRestartCount` | Direct |
| FilterRestart completed | No driver signal. Frames sent by a protocol and seen on the observer after the entry; filter state event | Inferred; direct only if ETW fires |
| Bring-up succeeded | No signal (F8, F9). The clock lines show values; the Debug log has `intel_init() returned` | **Gap** |
| Other adapters untouched | Per-adapter statistics deltas and oper status; ETW for all adapters | Counters direct, for what they count |
| PHC | Paired PHC and host-time windows before and after | `DISCONTINUOUS` can be claimed. Continuity cannot: no hardware tolerance exists |
| TX and observer | Tagged frames, source MAC checked, on the observer proved for that adapter | Direct, broadcast only |
| Driver and hardware consistency | tasArmed, `hw_state`, capabilities, snapshot NET_LUID; TC-TSNREG-001 before and after for what the snapshot lacks (F15) | Partial: F9 |
| Filter, miniport or hardware origin | Not separable: the filter's write hides any earlier reset | **Gap**, see below |

A `RESUMED_BY_TRAFFIC` outcome alone is not lifecycle proof. It needs the NDIS events to agree.

## Minimum observability

1. **No driver change, used by this run:** the `Microsoft-Windows-NDIS` ETW capture below. Offline, its schema is enough: 10312 names the instance and the miniport. Whether it fires is not known, so it is not yet sufficient evidence.
2. **Only if ETW stays silent or falls short**, and only with a governing issue and your approval; no new IOCTL is proposed:
   - completion counters beside the entry counters in `AVB_DRIVER_STATISTICS`, moved at the last line of FilterPause and FilterRestart;
   - the last `NdisFRestartFilter` and bring-up status, which closes F2, F8 and F9.
   That is an ABI change. Until then F2, F8 and F9 are recorded gaps, and no run may claim that bring-up succeeded. In a Debug driver, `intel_init() returned` in the DebugView log is partial evidence where the capture keeps the line.
3. **Origin:** #348 already specifies the decisive measurement, a read of SYSTIM immediately before the seed. `trace_tx_state` does not print it today, so that driver change has to happen under #348, with a capture that does not lose the line. Case A cannot replace it. It can falsify the source reading (T2), and it can show whether the miniport was involved at all.

## The diagnostic

`tests/diagnostic/tas_gate_investigation.c --lifecycle-a [--dry-run | --passive]`. No adapter is named anywhere.

- **Discovery:** every adapter from `IOCTL_AVB_GET_ADAPTER_IDENTITY`. A run that cannot enumerate them all does not start.
- **Eligibility:** capabilities only, in `tas_plan_classify`. `TAS_ADAPTER_FACTS` has no vendor, device or family field, so identity cannot reach the decision.

| Facts | Class | Effect on the run |
|---|---|---|
| Identity or `GET_HW_STATE` unreadable, or hardware capability word empty | `INCONCLUSIVE` | Non-passing |
| Hardware without TAS | `SKIP_UNSUPPORTED` | Not a target; can still observe |
| Hardware TAS, state below `PTP_READY` | `INCONCLUSIVE` | Non-passing: the driver reports a partial set there |
| Hardware TAS, driver without it | `SKIP_NOT_IMPLEMENTED` | Non-passing, never "unsupported" |
| TAS, but PHC or registers not measurable | `BLOCKED` | Non-passing |
| TAS, no link | `SKIP` | Runtime not-applicable |
| TAS, link, no separate management link | `BLOCKED` | Non-passing |
| Everything present | `ELIGIBLE` | Runs |

- **Every eligible adapter runs**, each with its own instance, observer, baseline, result and cleanup verdict. Results never collapse: `tas_plan_exit` keeps any failure visible, and a run with nothing eligible proves nothing.
- **Observer:** any other adapter with link, tried lowest NET_LUID first. Being listed proves nothing: only seeing that target's baseline frames makes it the observer. No capability bit applies, since capture is host-side.
- **Identity:** NET_LUID binds and correlates. DIDs are printed for the reader and decide nothing.
- Dedicated IOCTLs only: no `READ_REGISTER` or `WRITE_REGISTER`. TDH, TDT and occupancy are therefore not measured in Case A, so Q3 stands on the gauge and the wire here.
- Pure logic in `tests/ioctl/tas_lifecycle_eval.h`, offline cases TC-MOCK-014 to 023 in `test_tas_restore_contract`. Those cases say nothing about hardware.
- Every wait is bounded. The trigger runs on its own handle and thread, and polling starts before it, so a blocked or fast call cannot stop or outrun the polling.
- Every way out after the trigger prints the lifecycle result and a cleanup verdict side by side. Neither replaces the other.
- No MMIO from before the trigger until the lifecycle is shown (F11, F18). Statistics, which touch no hardware, are polled throughout. Frames follow only a Restart entry, and the clock and snapshot follow only those frames. Short of that the run stops with those facts unmeasured, which is never a pass.
- PHC: the tool claims `DISCONTINUOUS` when the clock did not advance, and otherwise `NOT_JUDGED_NO_TOLERANCE`. `TAS_PHC_CONTINUOUS` needs a hardware-derived tolerance, which `tas_phc_fact` can never supply, so exit 0 never means continuity.
- No NIC cycle: it would add a second PHC reset and a new context to the evidence.
- One targeted run first, then the evidence decides what is next. #364 acceptance still says "Cases A to D, 3 runs each, with logs and checksums", and one run does not meet it.

## Capability gaps

Reported, not worked around. No VID or DID fallback exists for any of them.

| # | Gap | Effect | Belongs to |
|---|---|---|---|
| G1 | No interface maps a filter instance to an adapter. The match relies on the name starting with the interface GUID, seen once (F16) | A different layout refuses the adapter, as it should | #364, #356 |
| G2 | Driver `INTEL_CAP_TSN_TAS` means only that `setup_tas` exists. Nothing advertises ACTIVATE, REPLACE, open-all or disable per operation | "Eligible" means Case A may run, not that a schedule can | #350, #351 |
| G3 | No capability expresses revision or support tier | Evidence stays limited to the hardware it ran on | #351 |
| G4 | No family-neutral schedule interface. Cases B, C and D use `tas_gate_investigation.c` raw modes on `gen/i226_regs.h`, gated by DID at `main` | Cases B to D cannot be capability-driven until `TAS_QUERY` and `TAS_ACTIVATE` exist | #353, #357 |
| G5 | `QUERY_HW_SNAPSHOT` lacks BaseT, cycle and windows | Register fact is partial | #353 |
| G6 | No capability says a filter restart is available or safe on an adapter | Trigger applicability is shown only by running it | #356, #364 |
| G7 | No "capabilities valid" flag. An empty word stands in for it | A zero word is `INCONCLUSIVE`, never unsupported | #350 |
| G8 | Observer candidates come from driver enumeration, so an adapter without the filter cannot observe | May report no observer where one exists | #364 |
| G9 | Runner `Mutating:Target` selects by DID, so identical adapters stay `TC-INFRA-ADAPTER-003` BLOCKED, although NET_LUID is unique | #384's runner registration inherits this | #384 |

Also found, not changed: `tests/hardware/test_tas_isolated_arm.c:807` and `tas_gate_investigation.c` `main` pick the capture adapter by I210 DID, and `tests/hardware/tas_wire_observer.h` documents one. The 21 issues #349, #364 and #366 to #384 name "I210 observer" in their text. Their amendment is proposed separately, never made silently.

## Timing: what a run may claim

| Claim | Source | Status |
|---|---|---|
| Entry observed | `FilterPauseCount`, `FilterRestartCount` | Observed by the driver, not by NDIS |
| Completion inferred | Pause: NDIS restarts only from Paused. Restart: frames through the whole stack on the observer | Inference from documented order |
| Completion observed | NDIS state event on this `FilterInstanceName`, read against a rule written down before the run | Only this establishes it. The tool cannot see it |
| Interval length | Bracketed polls: `lowerUs` and `upperUs`, entry to entry | Includes Pausing, so it overstates Paused |
| Paused interval | NDIS state events, if they fire | Not available from polls |

**Resolution and uncertainty.**
- QPC here is 10 MHz, 0.1 us per tick, read 2026-11-05.
- That is not the resolution of a measurement. An edge is known only to its bracket: `widthUs` in the `edge` lines, set by the poll gap, and printed for each run with the mean and worst gap.
- The dry run measures the gap with nothing happening. A gap measured during the event can be worse, because the machine is busy then.
- An ETW timestamp shares the trace clock with the markers. Placing a poll against an NDIS event carries the marker's own bracket, `qpcAfter - qpcBefore`, 6 to 38 us with no session attached on 2026-11-05, plus the poll's bracket. This must be re-read from a real trace.

**A missed interval is not an absent one.** Counters are cumulative, so an interval the polls fell between still shows in the deltas. `NOT_RESOLVED_NEVER_ABSENT` gives an upper bound and says nothing else. If the deltas show no Pause, that is `NOT_TRIGGERED` within the bound, a statement about entry only.

**Does the instrumentation change the transition?** By source, polling shares no lock with the normal path (F19). That is a reading, not a measurement, so `--passive` repeats the run with one poll a second. Comparing NDIS event spacing between the two runs is the only measurement of this, and it needs the events to fire. One pair of runs can show a large effect, not prove there is none.

## #384: proving a call ran inside the Paused interval

A failed Paused-state assertion is authoritative only if the test independently proves the operation ran inside the interval. An IOCTL response alone proves nothing, because the call may have run after Restart.

- **Proof by counters:** a poll that finished before the call started shows Pause entered and Restart not, and a poll that started after the call finished shows the same. `provenInsideCalls` applies this rule to the polls themselves, so Case A shows whether the interval is reachable at all, using a harmless read.
- **Limit:** that proves entry to entry, which includes Pausing. #356 says Paused, and DES-C-TAS-010 refuses mutating operations "until Running", which covers both. Proving strictly Paused needs an NDIS state event before the call starts.
- **If the interval cannot be hit or measured:** report the evidence gap and propose a testability solution under #384 or #356. Do not weaken the assertion, and do not add a driver test hook without approval.
- **Cleanup does not depend on any of this.** A test whose call landed outside the interval still owns cleanup, verifies restoration, and keeps both verdicts.

## Results

Evidence: `logs\diag-i364-*-20261105_204526*`, `LOCAL_DIAGNOSTIC`, driver `5BA3FCBE...`, tool at `bd58922`. Preflight and tool logs are UTF-16, as `Tee-Object` writes them in PowerShell 5.1.

**Stage 0: PASS.** `enabled=1`, three markers, `status=0`, and all three in the trace with the exact QPC values and ETW timestamps. Bracket 21.3 to 24.8 us with a session attached, and ETW time within 2 us of the tool's host time. The trace reported 0 events lost. No NDIS events occurred, so this proves the pipeline, not that NDIS emits for a restart.

**Case A, 3 runs, 2026-11-06, Debug driver `C1A98CF9F62D2400...`, `LOCAL_DIAGNOSTIC`, `provenance.verified` true on every run.** Checksums: `logs/i364-casea-checksums-20261106.txt`, 28 files.

| | Run 1, 20:38 | Run 2, 20:58 | Run 3, 20:59 |
|---|---|---|---|
| Commit | `d203b85` | `1a93f56` | `1a93f56` |
| Lifecycle | `RESUMED_BY_TRAFFIC` | same | same |
| Sequence | 5, 2, 3, 4 | same | same |
| Trigger to first NDIS event | 4.22 ms | 7.36 ms | 0.75 ms |
| Pausing to Paused | 14.0 us | 15.2 us | 12.2 us |
| **Paused to Restarting** | **24.1 us** | **24.7 us** | **13.7 us** |
| Restarting to Running | 15.19 ms | 16.05 ms | 13.71 ms |
| Polled entry to entry | 33.4 to 83.4 us | 40.2 to 45.1 us | 25.2 to 29.9 us |
| `provenInsideCalls` | 20 | 29 | 19 |
| Frames after restart | 49 of 49 | 49 of 49 | 49 of 49 |
| PHC before, after | 21165.57 s, 9.30 s | 1220.80 s, 9.29 s | 45.07 s, 9.31 s |
| `TRGTTIML0` | 0xB1B to 0xCE9 | 0xCE9 to 0x9DF | **unchanged**, 0x9DF |
| Tool exit | 1 | 1 | 2 |
| `TC-CASEA-001`, `002`, `003` | PASS, PASS, `CLEANUP_FAILED` | same | same |

Every run had 0 lost events, 0 reset, halt or failed-restart events, other adapters unchanged and gauge 0. No NIC link event occurred in any of them.

| #364 question, Case A | Answer |
|---|---|
| Q1 schedule survives | Not applicable: no schedule |
| Q2 PHC continuous | **No**, 3 of 3 |
| Q3 outstanding sends | Gauge 0 before and after, traffic resumed, Paused 13.7 to 24.7 us. TDH, TDT and occupancy during the interval: unknown |
| Q4 hardware and driver agree | tasArmed 0, capabilities and `hw_state` unchanged, 3 of 3. Limited by F9, and `TRGTTIML0` changed in 2 of 3 |

Three-run conclusions:

- The PHC rate between runs on one epoch is 1.02932 over 1177 s and 1.02934 over 35 s, so 2.93 % fast reproduces.
- The computed seed sits within about a millisecond of Running in all three runs, while the delay before NDIS started varied from 0.75 to 7.36 ms. So it follows the bring-up, not the trigger. Still inferred: #348's read before the seed is missing.
- `TRGTTIML0` does not change on every restart, so it is not a fixed effect of the bring-up. Mechanism unknown.
- RealVNC logged one dropped RTP datagram at 20:59:02.386, inside run 3's restart (FilterPause 20:59:02.376, `intel_init` returned .390). This is the first observation independent of the socket tables that links the media path to the I226. The session stayed connected in all three runs.

**First run in detail, 2026-11-06 20:38, `test_tas_lifecycle_case_a`, commit `d203b85`, Debug driver `C1A98CF9F62D2400...`, I226 0x125B REV_04, 1 Gbit/s, `LOCAL_DIAGNOSTIC`, `provenance.verified` true.** Evidence: `logs/test_tas_lifecycle_case_a_20261106_203816.log`, its DbgView log, `logs/diag-i364-casea-20261106_203827.etl`. One run of the three #364 requires. Never #384 RED or GREEN.

| Verdict | Result |
|---|---|
| `TC-CASEA-001` lifecycle | PASS: `RESUMED_BY_TRAFFIC`, other adapters changed 0 of 2 |
| `TC-CASEA-002` NDIS agreement | PASS: `SEQUENCE_CONSISTENT` |
| `TC-CASEA-003` restoration | **`CLEANUP_FAILED`**: PHC reset, and one register changed |
| Runner | FAILED, exit 1. `TC-INFRA-STATE-001` and `TC-INFRA-SESSION-002` cleanup FAILED on `TRGTTIML0` |

**Hardware-observed:**

- The trigger reached the filter: `FilterPauseCount` +1, `FilterRestartCount` +1, generation +1, attach and detach 0. The IOCTL returned after 0.8 ms and the first NDIS event followed at +4.2 ms, so the call only schedules, as documented. T5 is refuted.
- `State` has names: the provider renders 5 Pausing, 2 Paused, 3 Restarting, 4 Running, with `Location` 1, 2, 3 and 5. The earlier statement that no value map exists was wrong: `wevtutil gp` simply does not export it.
- **One restart moves four filters.** All four on the I226 went 5, 2, 3, 4: pause top-down (WFP, QoS, Npcap, IntelAvbFilter), restart bottom-up, all inside 19.9 ms. The other adapters' filters did not move.
- No 10007, 10058, 10050 or 10043, no link event, attach and detach 0: no miniport reset, halt or failed restart was reported. That supports T4 over T3 and T6. NDIS emits nothing here that shows the miniport itself was untouched, so this is absence of evidence.

| Time from `trigger-issued` | Event |
|---|---|
| +0.8 ms | Trigger returned |
| +4.21 ms | First Pausing (WFP) |
| +4.26 ms, +4.28 ms | IntelAvbFilter Pausing, Paused |
| +4.23 to +4.28 ms | FilterPause entry, by polls |
| +4.30 ms | IntelAvbFilter Restarting |
| +4.31 ms | FilterRestart entry, by polls |
| +19.48 ms | IntelAvbFilter Running |
| +19.85 ms | Last filter Running (WFP) |

- **NDIS-reported Paused interval: 24.1 us**, bracket 13.1 us. The polled entry-to-entry window was 33.4 to 83.4 us, as expected wider. 22 polls read the interval open, and `provenInsideCalls` is 20, so a read can be proven inside it (#384).
- Restarting took 15.2 ms, and the kernel log shows it as `AvbPlatformInit`, entered at 20:38:32.124 and returned at .139. Filters above ours stayed paused until then, so this bring-up holds the whole adapter's stack for 15 ms.
- Traffic resumed: 49 of 49 frames on the observer, `sendErrors=0`, gauge 0. Frames follow the restart only, so nothing shows frame behaviour inside the interval.
- **PHC reset, in Case A: 21165.57 s before, 9.30 s after.** `DISCONTINUOUS`. T2 is refuted and T1's prediction holds. TIMINCA and TSAUXC unchanged.
- **`TRGTTIML0` changed, 0x00000B1B to 0x00000CE9,** not predicted, and stable in the register checks either side. The only writer found in the source is `SET_TARGET_TIME`, which was not called. Earlier incidents show the same register moving, so this is not new, and the mechanism is unknown.
- All other 81 values compared before and after in the register checks are equal.
- **RealVNC, session on the target (`MANAGEMENT_ON_TARGET`, 1 of 2 bound sockets): no disconnect in the RealVNC log.** One run, a 24 us interval. It says nothing about a stuck pause.
- I219 line (#389): passed in both register checks, 32 ms each. No transition this time.
- Reporting correction of `b836e5a`: verified on hardware with `provenance.verified` true, 23 of 23 registers compared. Debug only.

**Inferred:**

- The reseed is the filter's own `init_ptp` write. With the measured rate the seed falls at about +19 ms, inside `AvbPlatformInit`, with no reset reported. The placement moves by several ms within the plausible rate, and #348's decisive read of SYSTIM before the seed is still missing.

**New finding, outside #364: the I226 PHC runs 2.9 % fast.** Measured without any assumption about a seed: 1.02932 over 26.6 s between two runner samples, and 1.02923 over about 5.4 h between two logged clock reads, with the host clock as reference, at `TIMINCA` 0x18000000. #343 covers the I210 only and #320 is an open test, so no issue covers it. It explains why the implied PHC zero lies 245 ms before the trigger. No cause is claimed.

**Stage 1, 2026-11-05: did not run.** Preflight, both register checks and the dry run all stopped at `\\.\IntelAvbFilter` not found (error 2), the `#328 sentinel`, with the service `Running`. No adapter was discovered and nothing was mutated. "Adapter state unchanged" and "0 lifecycle events" therefore prove nothing. That run also used the DID-based commands this design replaces, so it cannot be repeated as it was.

**Stage 1, 2026-11-06 14:07 and 14:16, stamps `20261106_140738` and `20261106_141612`:** stopped at the unreadable snapshot, `QUERY_HW_SNAPSHOT` Win32 998. That defect is fixed and HIL GREEN in Debug only (`logs/ssot_register_validation_test_20261106_151202.log`); Release is not verified.

**Stage 1, 2026-11-06 15:13, stamp `20261106_151348`.** `Invoke-TasStage1DryRun.ps1`, HEAD `9526898`, Debug driver `C1A98CF9F62D2400...`, `LOCAL_DIAGNOSTIC`. **Official verdict, kept exactly as recorded and not to be rerun for a better exit code (Q 2026-11-06): `INCONCLUSIVE_RESTORATION`, `DRY RUN NOT COMPLETED`, exit 1.** No FilterPause or FilterRestart was performed. None of this is complete system restoration, and none of it is #384 RED or GREEN.

| Dimension | Result | Standing |
|---|---|---|
| Wrapper verdict | `INCONCLUSIVE_RESTORATION`, exit 1 | Official |
| Capability-driven discovery | 3 adapters classified: 0x125B `ELIGIBLE`, 0x1533 and 0x15BB `SKIP_UNSUPPORTED` | Hardware-observed |
| I226 eligibility and instance lookup | Exactly one instance matched (F16) | Hardware-observed |
| I226 dry-run tool | `exit=0`, `dry run complete` | Tool result only |
| I219 observer | 49 of 49 tagged frames seen, 0 send errors, on the wire observer `Ethernet` | Hardware-observed |
| I226 register observation | 11 compared, 0 changed, 0 unreadable | Those 11 registers only |
| Polling | 177,885 polls, mean gap 1.1 us, worst 48.4 us, at rest with a trace attached. Driver `IoctlCount` on 0x125B rose by 177,902 | Hardware-observed; says nothing about gaps during a transition |
| Wrapper register comparison | 82 register values in the pre and post checks, 0 differences. Phases 3 and 4, TDH and TDT samples excluded | Those values only |
| Adapter state, NDIS lifecycle events | 0 differences, 0 events, trace closed | No transition occurred, so this does not show that the events would fire |
| I219 pre and post transition | Failed in the pre-check, passed in the post-check: `goneFailures=1`, the single reason for the verdict | **Unresolved, governed by #389** |
| Runner | pre exit 1, post exit 0 | The failure stays FAILED |
| Restoration | Only the dimensions above were compared. PHC not judged, TDH and TDT not compared | Never a restoration claim |

The transition rule is deliberate: line 193 of the wrapper refuses a clean result when a failure disappears, pinned by mutant `clean-gone`. It stays. The I219 line read FAIL 5 times and PASS 3 times across the 8 pre and post logs of the four runs of 2026-11-06. It also passed in a pre-check, before that run's tool started, so it can flip without a dry run. Every nonzero SYSTIM value in those logs is a multiple of 16,000,000, the low 24 bits of `TIMINCA`. That does not establish a cause, and a PASS on that line does not establish a healthy PHC. #389 owns both questions, and this investigation does not touch them.

**NDIS power-resume events, bounded comparison, closed.** Event `10302` (task Power), every occurrence on `NetLuidIndex` 32774, which is the target's NET_LUID index:

| Stamp | Tool outcome | `10302` count | Reason 54, receive | Reason 52, transmit | Span |
|---|---|---|---|---|---|
| `20261105_204526` | no driver | 0 | 0 | 0 | - |
| `20261106_140738` | stopped at the snapshot | 17 | 16 | 1 | 0.66 s |
| `20261106_141612` | stopped at the snapshot | 9 | 9 | 0 | 0.85 s |
| `20261106_151348` | complete | 157 | 102 | 55 | 5.94 s |

The count grows with what the tool did on the target, but the cause is unknown: three traces show correlation at most. No other adapter shows the event, and no trace holds 10312, 10103, 10007, 10058 or 10050. They are not FilterPause or FilterRestart evidence, and they show no concrete dependency of Case A, so no separate investigation is opened. Case A keeps them in the trace and excludes them from every state rule. What they do show: this session receives events from this provider. A competing explanation found later: a RealVNC session can bind its media socket to the target's address (see "Management path (RealVNC)"), so background traffic on the target may contribute. Neither explanation is established.

## Driver precondition: HIL environment blocker

**Resolved 2026-11-06:** the driver is installed through the canonical scripts. Kept as the record of the 2026-11-05 failure, whose cause is still not established. The diagnosis below only reads. It addresses the driver only as the precondition for TAS verification.

What is established, from the System log, `install-driver-20261105_205100.log` and `Install-Driver.ps1:224-345`:

- `Uninstall-Driver` runs: unregister the ETW manifest, `sc stop`, `sc delete`, clean the store, then disable every bound adapter, then `netcfg -u`.
- At 20:51:10 the first adapter links dropped. The transcript ends on disabling the next adapter, and the run ended at 21:06:59 with a terminated pipeline. So the service was deleted and `netcfg -u` never ran.
- The 21:07 run offered a reboot with auto-complete, which is an unimplemented TODO at `Install-Driver.ps1:246`, and the machine rebooted at 21:07:47.
- Now: `netcfg -q MS_IntelAvbFilter` says installed, the component key points at `oem137.inf`, every binding reads enabled, the service key does not exist, no device link exists, and the driver is not loaded. `oem137.inf` and the 16:47 driver are intact, and the file's signature is valid.

Not established: why the adapter disable did not return, and why the device node was missing at 20:45, before either reinstall. No cause is claimed, and #328 is not debugged here.

```powershell
$dstamp = Get-Date -Format yyyyMMdd_HHmmss
Start-Transcript -Path ".\logs\diag-i364-driverstate-$dstamp.txt"
"--- service"; sc.exe qc IntelAvbFilter; Test-Path 'HKLM:\SYSTEM\CurrentControlSet\Services\IntelAvbFilter'
"--- network component"; netcfg -q MS_IntelAvbFilter
Get-NetAdapterBinding -ComponentID MS_IntelAvbFilter | Format-Table Name, Enabled, DisplayName -AutoSize
$class = 'HKLM:\SYSTEM\CurrentControlSet\Control\Network\{4D36E974-E325-11CE-BFC1-08002BE10318}'
Get-ChildItem $class | ForEach-Object { $c = Get-ItemProperty $_.PSPath; if ($c.ComponentId -match 'intelavb') { "$($_.PSChildName) ComponentId=$($c.ComponentId) InfPath=$($c.InfPath)" } }
"--- device link"
Add-Type -Namespace I364Diag -Name K -MemberDefinition '[DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] public static extern uint QueryDosDevice(string name, System.Text.StringBuilder target, int max);'
$target = New-Object System.Text.StringBuilder 1024
"QueryDosDevice length=$([I364Diag.K]::QueryDosDevice('IntelAvbFilter', $target, 1024)) target=$target"
"--- loaded driver"; driverquery /fo csv | Select-String 'IntelAvb'
"--- package"
$drv = "$env:SystemRoot\System32\drivers\IntelAvbFilter.sys"
Get-Item $drv | Format-List FullName, Length, LastWriteTime
(Get-FileHash $drv).Hash
(Get-AuthenticodeSignature $drv).Status
$inf = (Get-ChildItem $class | ForEach-Object { Get-ItemProperty $_.PSPath } | Where-Object ComponentId -match 'intelavb').InfPath
"component inf=$inf"; Select-String -Path "$env:SystemRoot\INF\$inf" -Pattern 'DriverVer'
Get-ChildItem "$env:SystemRoot\System32\DriverStore\FileRepository" -Directory -Filter 'intelavbfilter*' | Measure-Object | Select-Object -ExpandProperty Count
"--- signing mode"; bcdedit /enum '{current}' | Select-String 'testsigning|nointegritychecks'
"--- pending reboot"; Test-Path 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Component Based Servicing\RebootPending'
"--- adapters"; Get-NetAdapter | Format-Table InterfaceDescription, Status, MediaConnectionState, LinkSpeed, PromiscuousMode -AutoSize
"--- events"
Get-WinEvent -FilterHashtable @{LogName='System';StartTime=(Get-Date).AddDays(-2)} | Where-Object { $_.Id -in 41,109,1001,1074,6005,6006,6008 -or $_.ProviderName -match 'e1rexpress|e1dexpress|e2fexpress|igc' -or $_.Message -match 'IntelAvb|Intel AVB' } | Select-Object -First 60 | Format-Table TimeCreated, Id, ProviderName, Message -Wrap -AutoSize | Out-String -Width 180
"--- install transcripts"; Get-ChildItem "$env:TEMP\install-driver-*.log" | Sort-Object LastWriteTime -Descending | Select-Object -First 3 | Format-Table Name, Length, LastWriteTime -AutoSize
"--- setupapi tail"; Get-Content "$env:SystemRoot\INF\setupapi.dev.log" -Tail 4000 | Select-String 'IntelAvb|Section start 2026' | Select-Object -Last 25
"--- preflight"; .\tools\test\Test-DriverPreFlight.ps1
"preflight exit=$LASTEXITCODE"
Stop-Transcript
$dstamp
```

Pass means only that the picture is complete. It restores nothing, and the expected result today is `preflight exit=1`.

## State interpretation rule

Written before any run, so the evidence could not shape its own reading. First exercised by Case A on 2026-11-06, where it held.

| Source | Gives | Does not give |
|---|---|---|
| Microsoft Learn, *Filter Module States and Operations*, *Pausing a Filter Module*, *Starting a Filter Module*, retrieved 2026-11-06 | Six states: Detached, Attaching, Paused, Restarting, Running, Pausing. Transitions: Detached to Attaching to Paused to Restarting to Running, Running to Pausing to Paused, Paused to Detached. Paused means the pause is complete, Running means the restart is complete | Any numeric value, any ETW behaviour, whether overlying drivers or the miniport pause too |
| Microsoft Learn, `NdisFRestartFilter`, retrieved 2026-11-06 | "schedules a pause and then a restart operation for the specified filter module" | What follows a failed restart |
| Provider metadata, build 26300 | Event 10312: "Filter %1 entering state %2", `State` a UInt8, plus `FilterInstanceName`, `MiniportIfGuid` | A value map, and whether "entering" is logged before or after the transition |
| Hardware, Case A 2026-11-06 | 5 Pausing, 2 Paused, 3 Restarting, 4 Running, rendered by the provider, on all four filters of the I226 | Attach and detach values, and whether "entering" precedes the transition |

The documents conflict on one point, and it is not resolved here: the states page says a failed restart returns the module to Paused, while *Starting a Filter Module* says NDIS detaches a module that does not restart.

**Rule.**

1. **Scope.** Only event 10312, with `FilterInstanceName` exactly equal to the tool's `instance` line and `MiniportIfGuid` equal to the target's interface GUID. Other instances on the target are recorded, never counted. 10103 has no instance name, so it can support and never establish. Event 10302 is excluded.
2. **Window.** From the tool's `trigger-issued` marker to `run-end`, ordered by ETW timestamp. Events on the target outside the window are reported as a finding (`outsideWindow`). The markers come from the tool's own provider. Stage 1 never enabled it, so all four Stage 1 traces hold none, against three in Stage 0; `test_tas_lifecycle_case_a` enables it and does not trigger if that fails.
3. **Names come from the provider, not from a document or from memory.** The trace renders them. The rule still compares positions, so an unknown build cannot change the result.
4. **Binding a value to a state name** needs an observed sequence on our own instance, on one Windows build, in which the documented transitions place it: attach gives Attaching, Paused, Restarting, Running, and detach gives Pausing, Paused, Detached. All instances of our filter in that trace must agree, and the binding is recorded with its build as `OBSERVED_BINDING`, never as a published map. Without it a run can be consistent with the documented order, but it names no state.
5. **Anchors.** The driver counters are independent of ETW and place the sequence. Each ETW time is converted through the markers, so it carries the marker bracket, 21.3 to 24.8 us with a session attached (Stage 0), plus the poll's `widthUs`.
6. **Classes.**

| Class | Condition | May claim |
|---|---|---|
| `SEQUENCE_CONSISTENT` | Exactly four events `a b c d`, adjacent values differ, the Pause edge lies between the 1st and 3rd events and the Restart edge after the 2nd, and `a` is not `b` or `c` | The run agrees with Running, Pausing, Paused, Restarting, Running. State names only with a binding |
| `SEQUENCE_FAILED_RESTART` | The sequence ends on the value that held the Paused position, or a detach value or counter appears | Restart did not complete. Which documented path it took is reported, not assumed |
| `SEQUENCE_UNEXPECTED` | Any other non-empty sequence on the target | Raw sequence only. No Paused claim |
| `NOT_EMITTED` | No 10312 on the target, and the provider delivered events in this trace | Completion stays inferred (`tas_lifecycle_eval.h`) |
| `NO_WINDOW` | No `trigger-issued` marker in the trace, or no QPC rate | No ordering against the markers, so no sequence is read |
| `TRACE_SILENT` | No provider event at all, or events lost | No ETW statement in either direction |

7. **An event alone never proves a transition.** A state claim needs all of: the rule above, counter deltas of exactly +1 Pause, +1 Restart, +1 generation on the target and 0 on the others, and traffic through the whole stack after the Restart entry. If they disagree, the disagreement is the result.

## Case A plan

One targeted run on every adapter the tool classifies `ELIGIBLE`. For review; nothing here is authorized.

1. **Trigger and scope.** `IOCTL_FILTER_RESTART_ONE_INSTANCE` with the exact instance name, once per eligible adapter, on its own handle and thread, after a proved baseline. No schedule, `tasArmed=0`, no NIC cycle, no register write. Bounds: trigger return 5 s, Restart entry 60 s, settle 5 s, frames 3 s. The call schedules work in NDIS, so ending the tool does not stop it.
2. **Paused interval.** Defined as the time between NDIS reporting the Paused position and NDIS reporting the Restarting position on the target instance (rule, positions 2 and 3), only under `SEQUENCE_CONSISTENT`, as `upper` and `lower` with the bracket from rule 5. What "entering" means is unverified, so it is reported as NDIS-reported time. The polled `entryToEntry` window is a separate number: it includes Pausing and overstates Paused. With no events, only the polled bound exists and no Paused interval is claimed. `NOT_RESOLVED_NEVER_ABSENT` keeps its meaning.
3. **State values.** The rule above.
4. **Proof of entering and leaving.**

| Transition | Direct evidence | Independent evidence | Without it |
|---|---|---|---|
| Entered Pausing | `FilterPauseCount` +1, generation +1, target only | 10312, position 1 | Entry only |
| Reached Paused | 10312, position 2 | FilterRestart runs only from Paused, per the documented table | Inferred |
| Left Paused | `FilterRestartCount` +1 | 10312, position 3 | Entry only |
| Reached Running | 10312, position 4 | Frames sent by a protocol after the Restart entry, seen on the observer | Inferred, `RESUMED_BY_TRAFFIC` |

   `pFilter->State` is not evidence: it is set before the bring-up (F7) and it is not exposed.
5. **Observer.** Proved by traffic before the trigger, as in Stage 1, where 0x15BB saw 49 of 49. Its capture is Npcap inside the tool, running across the transition. Frames follow the Restart entry only (F11, F18), so there is no frame-level view inside the interval. If the capture dies, `tx_probed` stays false and the result cannot be `RESUMED_BY_TRAFFIC`. The observer is judged by frames only: its PHC plays no part, so #389 does not affect it. It must stay out of the transition: `other adapters changed=0` is required.
6. **Management unavailable.** See "Management path (RealVNC)".
7. **Test-owned cleanup.** See "Cleanup and recovery".
8. **PHC.** Paired PHC and QPC windows before and after, `report_phc` `driftNs`, and the implied PHC zero against the NDIS events. `DISCONTINUOUS` is claimed only when the PHC did not advance. Otherwise `NOT_JUDGED_NO_TOLERANCE`, and a printed `driftNs` of any size is a measurement, not a verdict. A tolerance can only come from `phcTolerance` in approved baseline evidence (`Get-AvbHilPhcTolerances`). `New-AvbHilBaselineEvidence.ps1` characterizes and deliberately derives none. No tolerance is invented here, so no run can reach `TAS_PHC_CONTINUOUS`. **Expected by source reading (#348, T1):** the PHC is reseeded, which gives tool exit 2 and `cleanup=CLEANUP_FAILED` on an otherwise healthy run. That is a finding, not a failed experiment, and it cannot be reversed.
9. **Per-adapter evidence.** One `plan` and one `adapter result` line per adapter, and own instance, observer, counters, facts and cleanup verdict per eligible adapter. Today that is 0x125B. Non-targets stay `SKIP_UNSUPPORTED`, are watched through their counters, and are never reported as passing. Evidence stays limited to the adapter, revision, driver build and link speed it ran on.
10. **Operator commands and artifacts.** See "Case A operator procedure".

Not answered by Case A: Q1 (no schedule exists), most of Q3 (TDH, TDT and occupancy are unmeasured, so gauge and wire only), Q5, whether a bring-up succeeded, and the origin of a PHC reset.

## Cleanup and recovery

**What Case A can leave behind.** No schedule and no register write are involved. The tool owns its handles, socket and capture, and the operating system releases them when it exits. What remains is what NDIS and the driver did: a reseeded PHC, a filter stuck short of Running, a detached filter (F14: Optional, so NDIS keeps the stack without it, per the documents), a stalled transmit path, or an effect on another adapter.

**Invariants.** The original result and the cleanup verdict are always both reported. Nothing restores the PHC. `COMPLETE_RESTORATION` is out of reach today, because it needs PHC continuity. A NIC cycle is never restoration by itself and adds its own PHC reset and a new context. The lock is held from before the first check until after the last.

| Outcome | Detected by | Test-owned action | Cleanup verdict |
|---|---|---|---|
| `NOT_TRIGGERED` | No counter moved | Read-only post-checks. #364 is BLOCKED until another trigger exists (T5) | From post-checks; the tool still prints one |
| `RESUMED_BY_TRAFFIC` | +1, +1, frames seen | Read-only post-checks. **No cycle** | `CLEANUP_FAILED` if the PHC did not advance, else at best `INCONCLUSIVE_RESTORATION` |
| `PAUSE_ONLY` | No Restart entry in 60 s | Read-only post-checks and full evidence. **The failure is the finding: nothing recovers it automatically** | `CLEANUP_FAILED` or `INCONCLUSIVE_RESTORATION`, by what the checks show |
| `RESTART_ENTERED`, frames not seen | Probe failed | Same | `CLEANUP_FAILED` |
| `CONTEXT_REPLACED` | Attach, detach or a falling counter | Read-only post-checks. Stop the session | By the checks. Whether to rebind or reinstall is Q's decision |
| `OVERLAPPED` | Deltas not attributable | Read-only post-checks. Stop the session | `INCONCLUSIVE_RESTORATION` at best |
| Another adapter changed | `other adapters changed` above 0 | Read-only post-checks. Stop the session. Never touch that adapter | `CLEANUP_FAILED` |
| Tool timeout or crash | Exit code, no `result` line | The runner ends the process tree at its timeout and runs its post-test check. NDIS may still finish | `INCONCLUSIVE_RESTORATION` or worse. The lifecycle outcome stays unknown |
| Ctrl+C | Same | The console handler closes the trace. The runner post-test check still runs | Same |
| Runner killed outright | No results document, lock left | The next HIL run takes over the lock and stops. The next Case A closes a trace left open | `INCONCLUSIVE_RESTORATION` |
| Bugcheck or hang | Session lost | None possible. Evidence up to the last flushed file. A reboot is Q's | Unknown |

**Adapter cycling is a separate operation, prepared here and not authorized (Q 2026-11-06).** Automatic cycling after `PAUSE_ONLY` or a failed transmit probe was refused: the failure may be the evidence Case A exists to capture. No Case A code path cycles an adapter, and W8 stays unimplemented. If it is ever authorized:

| Aspect | Design |
|---|---|
| Applicability | Only after Case A ended, its evidence is written and checksummed, and Q reviewed it. Only for `PAUSE_ONLY` or a failed transmit probe. Never after `RESUMED_BY_TRAFFIC`, which would add a PHC reset for nothing. Started by a separate operator command under the HIL lock |
| Target only | Identified by NET_LUID and interface GUID from that run's evidence. Refused if the identity no longer resolves to exactly one adapter. Where the management session runs is reported, as in W4 |
| Bounded | `Disable-NetAdapter` in a separate process, with a wait bound. **A timeout only ends the waiting: it does not cancel a kernel operation, and killing the process may not return while the operation is blocked.** So the bound decides what happens next, nothing more |
| If NDIS hangs | Record `BLOCKED`. No enable, no retry, no further NDIS operation on any adapter. A reboot is Q's decision. Direct precedent: on 2026-11-05 the transcript ends on disabling `Ethernet 3`, the I226 target. Its link never dropped, and that call was still blocked about 15 minutes later when the pipeline was terminated. The two adapters before it had dropped within a second |
| Independent evidence | Before and after: `Get-NetAdapter` status, PnP status and problem code, driver counters, NDIS events 10058 and 10007, TC-TSNREG-001, TC-HWSNAP-001, and frames on a proved observer |
| Verdicts | The original failure stays. Restoration is reported separately. A cycle resets the PHC (#348, 3 of 3 after a NIC cycle), so the result is `CLEANUP_FAILED` under D9, never `COMPLETE_RESTORATION`. A blocked disable is `CLEANUP_FAILED`, with the adapter state stated as unknown |

## Management path (RealVNC)

Read-only evidence from 2026-11-06. Addresses and connection identifiers stay in git-ignored `logs/i364-management-path-20261106.md`, because this file is public.

**VERIFIED**

- RealVNC Server 7.16: service `vncserver`, Auto, Running, with `vncagent` and `vncserverui` in the console session. There is no RDP session, and no established connection on 5900.
- Control path: one established TCP connection from `vncserver` to the RealVNC cloud on 443, from the I219's address, ifIndex 16, LUID `0x0006008000000000`, up since the 14:55:58 reinstall.
- Connections arrive through hosted rendezvous. **The type varies per connection:** of 63 since 2026-11-02, 35 were direct and 28 relayed, and direct peers were on the LAN or public.
- The session active at the reading is a direct LAN peer. Its only address-bound `vncserver` UDP socket is on **the I226 target's address**, ifIndex 8, LUID `0x0006008006000000`, created in the same second the session connected.
- `Find-NetRoute` to that peer answers the I219. **So routing alone gives the wrong adapter for this session.**
- In 20 s the I226 sent 1.2 MB and received 116 KB with no TCP connection on its address. The only other sockets there were SSDP and NetBIOS.
- All three adapters share a /22, a gateway, interface metric 25 and the filter binding. So none of them is outside the filter, and an adapter-specific failure can move traffic only as far as Windows re-selects it.
- `separate_management_link` only shows that another link exists. Stage 1 reported it as true regardless of where the session ran.
- A session dies with the address it is bound to: 37 disconnects read `send ... 10022`. The next connection came within 90 s in 29 of them. The three on 2026-11-06 fall within 24 s of an install start.

**INFERRED**

- The active session's media flows through the I226: address binding, asymmetric traffic and a Reachable neighbor entry on ifIndex 8. **A bound socket is not observed traffic:** the entry is also Reachable on ifIndex 16, so this stays inferred until packets are seen. `vncserver`'s process I/O counters proved uninformative: they do not capture its socket sends here.
- If so, Case A on the I226 would touch that session, and its media is background traffic during the transition, which Q3 has to treat as a confound.

**UNRESOLVED**

- Packet-level proof for one session (shortest diagnostic in "Case A operator procedure").
- Which adapter the next session binds, and whether that choice can be steered. Bindings of earlier sessions were not recorded.
- Whether a reconnect is automatic or by hand.
- Physical console or out-of-band availability: **unknown**. Windows session events do not establish it, and no governing requirement mandates it, so nothing here depends on it.
- What restored the adapters on 2026-11-06 at 07:08. It is not in these logs.

**Failure domains, for Case A on the I226.** The session column depends on where that session is bound, so both are shown.

| Scenario | Session bound to target | Session bound elsewhere | Cloud path on the I219 | Basis |
|---|---|---|---|---|
| Healthy pause and restart | Media stalls for the interval | Unaffected | Unaffected | Inferred from the documents: sends are refused while paused. Duration unknown until measured |
| Stuck in pause, target only | Stalls, then times out as `10060` | Unaffected | Unaffected | Inferred. Whether a reconnect binds the dead path again is unresolved |
| Filter detached | Continues without the filter, per the documents | Unaffected | Unaffected | Documented for an Optional filter (F14), unobserved |
| NDIS or stack-wide hang | Lost | **Possibly lost** | **Possibly lost** | Unknown. 2026-11-05 cannot settle it, because the other two adapters had just been disabled |
| All Windows networking down | Lost | Lost | Lost, names stop resolving | Verified on 2026-11-05: about 10.7 h without VNC, and a reboot did not restore it because the install had left adapters disabled |
| Bugcheck or machine hang | Lost | Lost | Lost | After a reboot RealVNC started by itself on 2026-11-05 (verified). Case A disables nothing, so networking should return (inferred). A hang needs a power cycle, which RealVNC cannot give |

Another NIC is no independent recovery: all three carry the same filter in the same NDIS stack.

**Recovery provisions.**

| Class | Item |
|---|---|
| Verified provision | Reconnect after a transient interruption, usually within 90 s, 29 of 37 |
| Verified provision | RealVNC restarts with the machine, Auto service, seen on 2026-11-05 |
| Hardware-observed | The test reports where the session runs and continues |
| Unknown, not required | Recovery with all networking down, or from a stuck NDIS stack |

**A lost RealVNC connection is an accepted consequence and is not a test failure (Q 2026-11-06).** It is no blocker, no stop condition and no verdict input. The run is a local process in the console session: it depends on no viewer, runs on its own bounds, writes every artifact locally and releases the lock, so results and original failures are there after reconnecting. A lost session changes no step: no retry, no second trigger, and no administration of any adapter.

The reverse holds as well: **a session that returns proves nothing.** Failure of the target to recover stays a finding, failed cleanup stays `CLEANUP_FAILED`, unverifiable restoration stays inconclusive, and a driver or NDIS hang stays visible with whatever evidence exists. No verdict reads the management session.

## PHC effects and restoration

| Class | What belongs here | Verdict | Source |
|---|---|---|---|
| Expected lifecycle effect | Pause and Restart counters move; sends refused while paused; `AvbBringUpHardware` runs at FilterRestart | Not a failure in itself | NDIS documents; `src/filter.c:1051` |
| PHC continuity violation | The PHC is reseeded: `init_ptp` writes `SYSTIML=1`, `SYSTIMH=0` with no guard on the I226, while the I210 path never re-seeds. **Expected by source does not mean correct:** #348 is an open bug, and #356 names the risk | `DISCONTINUOUS`, tool exit 2, `CLEANUP_FAILED` | `devices/intel_i226_impl.c`, `devices/intel_i210_impl.c`; #348: 3 of 3 after a NIC cycle. **Unverified after a pause and restart** |
| Cleanup implementation failure | The test fails to detect or report something it owns: registers changed, TX not shown, another adapter touched, evidence missing, cleanup steps skipped | `CLEANUP_FAILED`, or `INCONCLUSIVE_RESTORATION` when unmeasured | `tas_lifecycle_cleanup`; wrapper chain |
| Irreversible change | A reseeded PHC. #348: do not write the numeric value back as restoration. So no cleanup can undo it, and later tests prove their own prerequisites | Stays failed; never redefined as success because it was expected | #348; D9 |

The verdict taxonomy does not change. If the PHC advanced, the fact is `NOT_JUDGED_NO_TOLERANCE`, the verdict is at best `INCONCLUSIVE_RESTORATION`, and any `driftNs` is a measurement. No tolerance is invented. #348's own path is the only source: characterize on hardware, then evaluate with `tools/lib/PhcRestoration.ps1`.

**SSOT conflict, for Q's decision; nothing is changed here.** As published today, #361 says restoration is complete when schedule registers read baseline and TSN mode is off, with PHC discontinuity reported separately, and #377 expects "Restoration complete ... PHC reset reported separately". Q's decision D9 (2026-11-05), `tas_restoration_outcome`, `tas_lifecycle_cleanup` and TC-TASARM-003 say a PHC reset prevents complete restoration and is `CLEANUP_FAILED`. The code follows D9, so the published acceptance text is out of step with it. Proposed: publish the D9 amendment of #361 and #377 that was prepared on 2026-11-05 and has not landed, as a traceable SSOT change.


## F2, F8 and F9

Exact definitions are in "Facts". Lines re-read at HEAD 2026-11-06: `src/device.c:662`, `src/filter.c:1051`, `src/avb_integration_fixed.c:173` and `:575`. For F9, the comment at `:575` covers a first bring-up, where the state is still below `PTP_READY`. The only writes that lower it are in the failure path of FilterAttach and in FilterDetach (`src/filter.c:666`, `:1135`), so nothing lowers it across a pause and restart.

| Gap | Definition | Prevents | Closed by | Class |
|---|---|---|---|---|
| F2 | `IOCTL_FILTER_RESTART_ONE_INSTANCE` discards `NdisFRestartFilter`'s status, so a refused request returns success | Telling a refused trigger from one that reached nothing | The status made observable, under a governing issue and approval, or a driver fix | **Observation gap.** Safe: the tool ignores the return value, and no counter moving reads `NOT_TRIGGERED` either way. Only the reason stays unknown |
| F8 | FilterRestart discards the bring-up result: `(void)AvbBringUpHardware` | Any claim that bring-up succeeded | The status made observable under a governing issue. Partial today: `intel_init() returned` in a Debug log | **Observation gap.** Limits the claim. It does not make the run less safe, because the run does not depend on that result. Register and clock facts still show a visible failure (T7) |
| F9 | A failed re-init leaves `hw_state` at `PTP_READY`: `AvbMarkPtpReady` only raises it and the failure path lowers nothing | Reading unchanged `hw_state` as health, so the `consistency agrees` fact is weaker than its name | A driver change under a governing issue | **Observation gap for Case A.** A possible product defect for #356, whose reconcile behaviour #384 tests, so it needs a decision before #384 GREEN, not before Case A |

None of the three prevents safe execution of Case A. Each only limits what a result may say, and they stay recorded in every result. No decision is made here on whether a governing issue should be opened.

## Case A operator procedure

**Case A is the compiled test `test_tas_lifecycle_case_a` (Q 2026-11-06): tests are compiled and live in the build folder, so the prepared runner finds them.** It is registered `Unsafe` in the coverage-gap table, and started by name:

```powershell
.\tools\test\Run-Tests-Elevated.ps1 -Suite HardwareIntegration -TestName test_tas_lifecycle_case_a -CaptureDbgView
```

The runner owns the machine lock, baseline, post-test state check, provenance and the DbgView capture. The test owns its ETW session, written to `logs\diag-i364-casea-<stamp>.etl`, and prints the `State` sequence, the management report and three verdicts per adapter: `TC-CASEA-001` lifecycle, `TC-CASEA-002` NDIS agreement, `TC-CASEA-003` restoration. A run exits 1 even when healthy, because restoration has no PHC tolerance and stays a restoration verdict. Run `ssot_register_validation_test` before and after it for the TSN register comparison, which uses registers this test does not read.

Conversion: `Invoke-TasStage1DryRun.ps1`, its orchestration test and the `--lifecycle-a` mode of `tas_gate_investigation` were removed after this test ran on hardware. Their history is in git. #364's Method section still says "Not a runner entry": that text needs a correction with the other #364 changes.

**Packet-level management check, read-only, optional.** Elevated, while connected through the session in question. It only counts packets on the session's own UDP port, per component. `pktmon filter remove` clears every filter, so skip this if `pktmon filter list` shows any.

```powershell
pktmon filter list
$p = (Get-NetUDPEndpoint -OwningProcess (Get-Process vncserver).Id | Where-Object { $_.LocalAddress -notin '0.0.0.0', '::' }).LocalPort
"port=$p"
pktmon filter add AvbVnc -t UDP -p $p
pktmon start --capture --counters-only
Start-Sleep -Seconds 10
pktmon counters
pktmon stop
pktmon filter remove
```

Expected if the inference holds: packet counts on the I226 and near zero on the others. An empty `port=` means this session has no address-bound UDP socket.

**Optional calibration, no trigger.** Only around a driver reinstall that is already needed for another reason, never one done for this: Q 2026-11-06. It would close most of the `State` question with no new risk. It is the lowest-risk check Blocker 4 asked for. Q runs it around that install, elevated, at the repository root:

```powershell
$s = Get-Date -Format yyyyMMdd_HHmmss
logman create trace AvbI364Cal -p Microsoft-Windows-NDIS 0x4316 0x5 -o "$PWD\logs\diag-i364-calibration-$s.etl" -ct perf -ets
.\tools\setup\Install-Driver-Elevated.ps1 -Configuration Debug -Action Reinstall -CaptureDbgView
logman stop AvbI364Cal -ets
tracerpt "$PWD\logs\diag-i364-calibration-$s.etl" -o "$PWD\logs\diag-i364-calibration-$s.etl.xml" -of XML -y
$s
```

Use the configuration that install needs. Expected: 10312 on all three instances, in the documented attach and detach orders, which gives the binding of rule 4. If no 10312 appears on our instances, Case A will probably be `NOT_EMITTED`, and that is known before any trigger. This reinstall disables every bound adapter (F17), so it carries its own risk, whether or not it is traced.

Read the result per adapter. A lifecycle outcome, a cleanup verdict and a sequence class are three separate results, and none replaces another. Stop conditions apply, and no second case or repeat follows without review.

## From Case A to #384

Issue state read 2026-11-06: #348, #349, #350, #351, #353, #356, #357, #361, #362, #363 and #377 are open. Only #364 and #384 were re-read in full.

| Step | Needs | Gives | Stops if |
|---|---|---|---|
| 1. Compiled test | `test_tas_lifecycle_case_a` | A run the operator starts by name | - |
| 2. Calibration, optional | A reinstall already needed, never one done for this | `State` binding, emission | - |
| 3. Case A, first run | Step 1 | Trigger reach, T1 to T7, Q2, Q4, part of Q3, reachability of the interval through `provenInsideCalls` | `NOT_TRIGGERED`: #364 and therefore #384 are BLOCKED until another trigger exists |
| 4. Case A to 3 runs, plus a `--passive` pair | Step 3 reviewed | #364 acceptance for A, and whether polling changed the transition | - |
| 5. Cases C and D | The seven-point safety analysis still owed, `tas_gate_investigation` under the orchestration, raw modes selecting by capability rather than DID | Q1 and Q3 under a schedule, which is what #384's scenario needs | - |
| 6. Case B | `test_tas_lifecycle_case_a --case-b`, see "Case B" | Required for #364, need not gate step 7 | - |
| 7. #384 Expected Results | Findings of A, C and D; Q's SSOT write | The Expected Results #384 defers to #364, and feeds #356, #362, #348 | - |
| 8. #384 test, RED | TAS query (#350, #353) and activation (#357), governed by #362 and #363; #384 forbids raw registers | Authoritative hardware RED. Case A results can never serve as it | Interval not provably reachable: report the gap and propose a testability solution under #384 or #356, without weakening the assertion |
| 9. #384 GREEN | #356 behaviour, a decision on F9, restoration from #361, the PHC outcome of #348, Debug and Release | Hardware GREEN | - |

Branch scope (Q 2026-11-06): this branch stops at step 6 plus #364's acceptance. Steps 7 to 9 belong to #384, on its own branch, after #364 is merged. #364 may prepare evidence for #384 but does not absorb its implementation.

#384 and #364 still name an I210 observer, while Stage 1 proved an I219 one. The correction is prepared, and publishing it is Q's.

## Blockers

Sorted by what actually stops Case A.

**Conditions for a Case A run:** the operator starts it by name. The runner gates it, and the test stops before the trigger without a trace. Nothing about the management session is a condition.

**Done:** the reporting correction of `b836e5a` is hardware-verified in Debug with `provenance.verified` true (`logs/results-HardwareIntegration-Debug-20261106_203759_742.json`).

**Resolved:** the driver; the first Case A run; the `State` rule, exercised once.

**Not blockers, but limits on what a result may say:** ETW emission (absent events give `NOT_EMITTED`, and completion stays inferred); F2, F8 and F9; no PHC continuity claim until a hardware-derived tolerance exists; TDH, TDT and occupancy unmeasured in Case A; the origin of a PHC reset, open until the #348 measurement exists; #389 and the transition it causes.

**For Q to decide, not a safety matter:** whether the #364 correction and observer wording must be published before the first run, or may follow it.

## Case B

Q decided on 2026-11-07: Case B uses the dedicated `SETUP_TAS` IOCTL, in the compiled test. No `WRITE_REGISTER`. `IOCTL_AVB_TAS_ACTIVATE` stays out of #364, on `feat/357-tas-activate`.

**Setup.** `test_tas_lifecycle_case_a --case-b`, on eligible adapters. Pre-state as in Case A, plus every TSN register readable and TSN transmit mode clear. The I226-LM register map is the only one with evidence, so another eligible adapter is BLOCKED with no register access. `SETUP_TAS` uses the same request as `test_tas_isolated_arm`, because the evidence that it holds frames came from that request. Then 8 s of tagged frames, past the 84 to 87 frames after which the ring stalled at 64 bytes.

**TC-CASEB-001.** Armed, frames accepted, none on the wire, observer alive. Anything else is `FAILED_SETUP` and the trigger is not issued.

**What differs from Case A after the trigger.** Case A takes Running from frames on the wire, which a hold withholds. The gauge cannot stand in: `FilterSendNetBufferLists` raises it before it checks the filter state, and the Debug path for a paused filter completes the frames without lowering it. So the run stops its trace after the settle time, decodes it, and reads registers only with a consistent NDIS sequence, anchored by the driver counters, with Restart entered and no other adapter moved. That permits a read and claims no transition. A second trace covers the rest, and both go to the same verdicts.

With the reads open, the ring tail is read before and after the probe frames. Frames on the wire or a moved tail count as traffic through the stack. That the tail shows this is an inference, and it is the only evidence available while the hold lasts. Without either, TC-CASEB-002 is INCONCLUSIVE, never PASS.

**Answers.** Q1: TSN mode and `registersKept`, held against after. Q2: PHC pairs. Q3: `releasedOnWire`, gauge before and after, TDH and TDT, NDIS Paused time. Q4: tasArmed against TSN mode. Restoration compares with the pre-state, and the test cannot release a hold, so TC-CASEB-004 is expected to be `CLEANUP_FAILED`.

**Stop conditions** are those below. No Restart entry within 60 s closes the reads.

**Recovery after a run, by the operator.** `Ethernet 3` is the I226 and does not carry management on this host. The block stops at the first failure, so enable never follows a failed disable:

```powershell
& {
    $a = 'Ethernet 3'
    Disable-NetAdapter -Name $a -Confirm:$false -ErrorAction Stop
    if ((Get-NetAdapter -Name $a).Status -ne 'Disabled') { throw "$a is not Disabled: stopping, not enabling" }
    Start-Sleep 5
    Enable-NetAdapter -Name $a -Confirm:$false -ErrorAction Stop
    $end = (Get-Date).AddSeconds(30)
    while ((Get-NetAdapter -Name $a).Status -ne 'Up' -and (Get-Date) -lt $end) { Start-Sleep 1 }
    Get-NetAdapter -Name $a | Format-List Name, Status, LinkSpeed
}
```

If disable blocks or the block stops, do nothing further to the adapter: a reboot is your decision. A NIC cycle restored TX after `SETUP_TAS` in the #328 runs, but it has not been run after a Restart under a hold, so it is not presumed to restore anything.

**Post-cycle evidence.** `test_tas_lifecycle_case_a --dry-run` records registers, PHC pairs, tasArmed and tagged traffic on the observer, and issues no trigger. It reports a nonzero tasArmed instead of refusing. It judges nothing: the values are compared with the run's pre-state in analysis, and TC-CASEB-004 stands as reported. No global halt follows: the next run proves its own pre-state and refuses a set TSN mode.

**Machine state before the first run, 2026-11-07 09:06.** CI HIL run 42343612433, for `a237029`, failed at `Run HardwareUnit`, `Run HardwareIntegration` and the Release deploy. Afterwards `MS_IntelAvbFilter` was absent from the bindings and network components, against positive controls on the Microsoft filters; `\\.\IntelAvbFilter` did not exist; SCM still reported the service Running on the CI build `3CD50DC5A32CA6C5`. No netcfg process, no HIL lock, no pending rename, adapters Up. The Worker log inspects a `netcfg` process at job end on this run and on 42298562266, and does not show what became of it. Whether the image stays loaded is unknown: the unelevated module list failed its control. The canonical reinstall ran from the same stage at 05:19 and finished in 22 s, 1 of 1.

### Case B run 1

`LOCAL_DIAGNOSTIC`, `ef7d682`, Debug driver `1B4F299C7E70AA68`, I226 0x125B REV_04, stamp `20261107_091050`, runner exit 1. Evidence: `logs/i364-evidence/caseb-run1/`.

| Test | Verdict |
|---|---|
| TC-CASEB-001 | PASS: 129 accepted, 0 on the wire, tasArmed 1, observer alive |
| TC-CASEB-002 | BLOCKED: no counter moved. Its reason text then said no trigger exists, which was wrong for this case and is corrected; the verdict is unchanged |
| TC-CASEB-003 | INCONCLUSIVE: no NDIS state event |
| TC-CASEB-004 | CLEANUP_FAILED |
| TC-INFRA-STATE-001, SESSION-002 | CLEANUP_FAILED: `TSYNCRXCTL` 0x18 to 0x19 |

With gauge 62, `FilterPause` was not entered in 60 s and no filter on the stack changed state. Case A's first `Pausing` comes 2 ms after the same trigger. 9 s later 0 of 49 probe frames reached the wire, gauge 228, 0 of 129 held frames released. No NDIS reset or halt event. So the read gate stayed closed, and Q1, Q2 and Q4 are unknown for this run. Q3: Pause not entered within the bound, held frames not released. Cause unknown: the handler discards the status of `NdisFRestartFilter`, and the trace keywords excluded Protocol, which this run's successor adds.

**Post-cycle, operator NIC cycle, `--dry-run` at 09:18:** 0 of 40 TSN registers differ from the pre-state; tasArmed 0; 49 of 49 tagged frames on the observer, gauge 0, ring idle. PHC 290.18 s, where continuous running from 167.19 s gives about 600 s, so the cycle reset it. Registers, TAS and TX returned; PHC continuity did not, so restoration is not verified and TC-CASEB-004 stands. This is one cycle.

**Evidence incident.** The two per-test logs `test_tas_lifecycle_case_a.exe_20261107_091059.log` and `..._091810.log` read normally until 09:15, appeared empty at 09:18:55 and were gone at 09:19:25. The first is listed in the run's checksum manifest as `DDD435D9...`, so that row can no longer be verified. Their output survives in the runner logs. Cause unknown: nothing in the runner, its libraries or scheduled tasks removes them. Later evidence is copied to `logs/i364-evidence/` at once.

### Case B drained run 1

`--case-b-drained` holds about 24 frames, so the gauge is 0 at the trigger, and has that as setup.

`LOCAL_DIAGNOSTIC`, `ecc8348`, Debug driver `1B4F299C7E70AA68`, stamp `20261107_093615`. Evidence: `logs/i364-evidence/caseb-drained-run1/`. TC-CASEB-001 PASS, 002 PASS, 003 PASS, 004 `CLEANUP_FAILED`; TC-INFRA-STATE-001 and SESSION-002 `CLEANUP_FAILED` on `TRGTTIML0` 0x9EC to 0xC79.

| | `--case-b`, run 1 | `--case-b-drained`, run 1 |
|---|---|---|
| Held, frames on the wire | 0 of 129 | 0 of 24 |
| Gauge at the trigger | 62 | 0 |
| Pause entered | no, 60 s | about 8 ms after the trigger |
| NDIS sequence | none | 5, 2, 3, 4, Paused 28.7 us, Running about 15.7 ms after Pausing |

| Q | Drained run 1 |
|---|---|
| Q1 | Yes: TSN mode kept, 40 of 40 registers kept. No schedule in the sense of Case C, and the hold outlasts the Restart: 0 of 48 probes and 0 of 24 held frames on the wire |
| Q2 | No: 1417.92 to 9.32 s, DISCONTINUOUS. Seed now 9.29 to 9.33 s, 5 of 5, with and without `SETUP_TAS` |
| Q3 | With gauge 0, Pause did not wait. Ring tail moved 762 to 823, so frames came through the stack; TDH stalled at 104 from the start here, 102 in the other run, despite the Restart |
| Q4 | Yes: tasArmed 1, TSN mode 1, hwState 3, capabilities unchanged |

So TSN mode alone does not stop the lifecycle, in this run. Outstanding sends are the leading explanation, not proved: frame count and PHC also differed between the runs, and that the tail stands for traffic is an inference. The Protocol keyword gave no events here, so the protocol-first theory is untested until `--case-b` repeats with it.

### Case B, three runs per variant

`LOCAL_DIAGNOSTIC`, Debug driver `1B4F299C7E70AA68`, I226 0x125B REV_04, 2026-11-07, sources `ef7d682`, `ecc8348` and `81f89d2`. Evidence and checksums under `logs/i364-evidence/`.

| | `--case-b`, runs 1 to 3 | `--case-b-drained`, runs 1 to 3 |
|---|---|---|
| Held, frames on the wire | 0 of 129, 128, 128 | 0 of 24, 24, 24 |
| Gauge at the trigger | 62, 78, 76 | 0, 0, 0 |
| Pause entered | none in 60 s, 3 of 3 | 8.1, 2.1, 17.2 ms after the trigger |
| NDIS sequence | no event, 3 of 3 | 5, 2, 3, 4; Paused 28.7, 27.9, 15.4 us |
| TC-CASEB-001 / 002 / 003 / 004 | PASS / BLOCKED / INCONCLUSIVE / CLEANUP_FAILED | PASS / PASS / PASS / CLEANUP_FAILED |

| Q | Case B |
|---|---|
| Q1 | Yes, from the drained runs: TSN mode and 40 of 40 registers kept, identical in all three; the hold outlasts the Restart, 0 of 48 probes each time. Unknown for gauge above 0: no Pause happened |
| Q2 | No, from the drained runs: reseeded to 9.32, 9.33, 9.34 s. Unknown for gauge above 0 |
| Q3 | Pause ran 3 of 3 with gauge 0 and never entered 3 of 3 with gauge above 0. A correlation; the cause is unproven while the driver discards the status of `NdisFRestartFilter`. In 2 of 3 runs with gauge above 0, frames accepted after the trigger never reached this filter |
| Q4 | Yes, from the drained runs, 3 of 3. Unknown for gauge above 0 |

The Protocol keyword produced no event in any of the five runs that had it, held or not, so it cannot settle where the stall sits on this stack. `TRGTTIML0` changed in 3 of 3 drained runs: the runner's TC-INFRA-STATE-001 and SESSION-002 stand as failures and belong to the separate investigation.

**Operator NIC cycle:** 5 of 5 times, 0 of 40 registers differ from the run's pre-state, tasArmed 0, tagged frames on the observer, PHC reset. Registers, TAS and TX return; PHC continuity does not, so restoration is never verified. The final dry run at 21:04 is not counted: CI and a reinstall came before it.

**Incident, held adapter left to CI.** The operator blocks cycled the adapter before each run, not after, so drained run 3's hold stayed until 21:04. HIL run 42359406277, for `81f89d2`, took the free lock at 10:21 and its Debug job failed at the driver deploy after 3 min 14 s, with a leftover `netcfg` inspected at job end. Its unit and integration tests never ran, so that CI result says nothing about `81f89d2`. An uninstall during an I226 hold is the #328 hang signature; this is one occurrence, recorded for #328, not claimed. From now on a block runs the test first and recovers after it, so no hold outlives a block.

### Restart status, 2026-11-10

The driver discarded the status of `NdisFRestartFilter` and reported success for a request that matched no instance. `--probe-restart-status`, TC-CASEA-004, sends one request for a name no instance has, after checking every instance with the driver's own matching rule, and requires not found with every adapter's counters unchanged.

| Run | Source, driver | Result |
|---|---|---|
| RED, 2 runs | `a35c482`, Debug `1E064870` | FAIL: success for no instance, counters unchanged |
| GREEN | `74bae7b`, Debug `2DAB5F08` | PASS: error 1168, counters unchanged |
| GREEN | `74bae7b`, Release `C3950B3C` | PASS: error 1168, counters unchanged |
| Case A | `74bae7b`, Debug | 001, 002 PASS, 003 CLEANUP_FAILED, as before; status word 0x00000000, 4 bytes |
| Case B, run 4 | `74bae7b`, Debug | 001 PASS, 002 BLOCKED, 003 INCONCLUSIVE, 004 CLEANUP_FAILED; **`NdisFRestartFilter` returned NDIS_STATUS_SUCCESS**, then no Pause entry in 60 s, gauge 87 to 134 |

So NDIS accepted the restart under a hold, one run, and the stall comes after that. The theory that the call failed is refuted for this run; why the stack does not pause stays open.

The dry run after run 4's NIC cycle: tasArmed 0, 49 of 49 frames, 1 of 40 TSN registers different, `TSYNCRXCTL` 0x19 to 0x18, the receive timestamp status bit. Its INCONCLUSIVE came from one snapshot register changing during the dry run itself, with nothing written: the run did not say which, and its text wrongly said it stopped early. Both are fixed, and the dry run now names the register. That a register moves with nothing written bears on the `TRGTTIML0` investigation.

**Which register moves.** With the dry run naming it, Debug driver `BC8ECF6B`: in one of two read-only dry runs, snapshot id 4, `TSYNCRXCTL`, went 0x19 to 0x18 with nothing written; in the other nothing moved. Bit 0 there is a receive timestamp status bit, set and cleared by the hardware, so comparing it as restorable state is a wrong invariant, in the dry run and in the runner's TC-INFRA-STATE-001 alike. `TRGTTIML0` did not move in either dry run. It moved after a real Restart: drained runs 1 to 3 and 2 of 3 Case A runs, each with a PHC reseed. The driver writes it only in `i226_set_target_time`, reached only through `SET_TARGET_TIME`, never on the bring-up path. Hardware reacting to the `SYSTIM` write, the miniport, or an earlier `SET_TARGET_TIME` remain open; none is tested. Both stay recorded failures for the separate investigation.

**Driver identity defect, found 14:37.** After a Release to Debug reinstall, the service's `ImagePath` pointed at the Release package in the DriverStore, which netcfg preferred because its stamp was newer; `System32\drivers` held the Debug file, which is what the runner hashes, so the runner reported a Debug driver while Release ran. The install log warned that the build was older and went on. The test's own `GET_VERSION` check refused both dry runs, so no result was attributed to the wrong driver. Rebuilding Debug gave it the newer stamp and fixed the state. The runner's driver identity and the install script's handling are infrastructure defects for separate issues.

**For #328, not this issue.** The canonical reinstall from Release back to Debug at 13:17 took 53 minutes, where every other reinstall today took about 20 s. The System log shows the adapters going down at 13:17:27 and nothing until the service install at 14:10:11, so the wait sits in the uninstall's `netcfg -u` of the Release driver, with all adapters disabled and no TAS armed under Release. One local occurrence of the symptom CI's Release deploys show.

**Dry run with no NIC cycle, 09:38.** About 80 s after the Restart: 0 of 96 frames on the wire, gauge 173 then 226. It exposed two defects in the dry run, both fixed: it stopped before printing registers, tasArmed and PHC, and it reported PASS with nothing proved. That PASS stands in the 09:38 log and is no evidence.

## Cases C and D: safety analysis and plan

Written 2026-11-10, before any C or D code or run, as Q required, and corrected the same day: the first version imposed a read-only cleanup rule that the repository does not have. Testing skill section E requires every state-changing test to restore the state it changed, on success, failure, exception and timeout, and to verify restoration independently against hardware; the driver owns the disable, disarm and restore mechanisms. Design basis: the shared schedule of `logs/tas-case-cd-design-20261106.md`, 1 s cycle, window 200 to 400 ms on every queue, future BaseT, TQAVCTRL 0x09, no FutScdDis, written through `tas_gate_investigation`'s Debug-only WRITE_REGISTER.

Labels: **observed** is hardware evidence on I226-LM REV_04 with its count; **predicted** follows from observations and has not been seen; **unverified** is a mechanism with no hardware evidence for this use.

Correction, 2026-11-10: this section first said the PHC reseed was observed 11 of 11. Counted from the run logs it is 9 of 9: six Case A runs and three drained Case B runs, 9.251 to 9.340 s. The other figures were not affected.

### Three phases, every way out

| Phase | Kind | Contents | Ends with |
|---|---|---|---|
| 1. Evidence | Read-only | The original verdicts; registers, tasArmed, PHC pairs, ring, gauge and TX, each only where the read gate allows; NDIS trace closed and decoded | Every file written and checksummed **before** phase 2 starts |
| 2. Cleanup and recovery | Mutating, classified Unsafe | Stop sending. Then the recovery mechanism, below. Its own outcome is recorded: done, failed, timed out, not attempted | Its record, kept whatever phase 3 finds |
| 3. Restoration verification | Read-only | Against the phase 0 baseline: the 40 TSN registers, tasArmed, tagged TX on the proved observer, PHC continuity and driver-hardware agreement, each reported on its own | `RESTORATION_VERIFIED` only with all four facts of #361; else `CLEANUP_FAILED` or `INCONCLUSIVE_RESTORATION` |

The original result never changes because of phase 2 or 3, and neither phase is skipped because phase 1 failed. No global halt follows a cleanup failure: the next run proves its own pre-state.

**Recovery mechanism.** #364's established, operator-controlled procedure, the checked block of Case B, run straight after the test, followed by the dry run as phase 3. Another mechanism needs explicit approval: the two candidates are evaluated below.

### `restore()` in `tas_gate_investigation`, evaluated

| Aspect | Finding |
|---|---|
| What it does | Finds the target's devnode from its NET_LUID through the interface GUID and `PnPInstanceId`; `CM_Disable_DevNode`, then `CM_Enable_DevNode`, each on its own thread; then reads the 42-register table, tasArmed and the PHC and compares them with the baseline taken before the first write |
| Bounds | Disable 120 s, enable 60 s, adapter return 60 s. A disable that does not return is never followed by enable, and says a reboot is the operator's decision. The abandoned call's state is static, so a late return cannot corrupt the stack |
| Preconditions | Debug driver; a baseline taken before the first write, with TQAVCTRL and BaseT reading 0; the NET_LUID resolving to exactly one devnode; management on another adapter |
| Evidence for the mechanism | **Observed** in the #328 runs: the cycle cleared `SETUP_TAS` holds and restored 42 registers, PHC reset each time; a schedule survived 1 of 41 cycles (#349) and a second cleared it, 1 of 1. **Observed** in Case B: the operator's `Disable-NetAdapter` path recovered 5 of 5 holds, gauge up to 954. **Unverified**: `CM_Disable_DevNode` after a Restart under a live schedule |
| Risk 1 | A disable could block on a stack that cannot pause. Case B showed the restart path stalling under held frames; the disable path did not stall in 5 of 5, but under a schedule that is unverified. The bound limits the wait, not the kernel operation |
| Risk 2 | An enable that times out leaves the target disabled. Management is elsewhere, so the host stays reachable; the adapter needs the operator |
| Risk 3, verdict accuracy | It compares `TSYNCRXCTL` for equality, but bit 0 is a status bit the hardware sets and clears: a false difference, failing safe. And it judges the PHC with a fixed 2 s tolerance its own comment calls unsupported; after a cycle the PHC is always reset, so the false continuity it could claim is unlikely, but the rule is not hardware-derived. Under D9 its "restored" exit can never be complete restoration |
| Risk 4 | It checks registers once after the cycle; a schedule that survives (#349) is reported, and nothing cycles again |
| Classification | NIC administration: Unsafe, operator-started, inside a Diagnostic tool |

Conclusion: a usable test-owned recovery mechanism for C and D once explicitly approved, provided its verdict feeds phase 3 rather than replacing it, and Risk 3 is reported, not trusted. Until approval the operator block is used, and the tool's phase 3 reads stay the same.

**In-place release with an open schedule.** Writing a whole-cycle-open window with a new future BaseT replaced a running schedule 6 of 6 and released held frames (**observed**, without a Restart in between). After a reseed, whether a BaseT nearer than the kept one replaces it is **unverified**. TSN mode stays set and BaseT cannot be cleared in place, so it is never restoration; at most it ends a hold before the cycle. Not used without approval.

### Cleanup capabilities the driver and hardware lack

| Missing | Effect on C and D | Governing issue |
|---|---|---|
| Return to legacy transmit mode without a NIC cycle: I226 `disable_tas` returns `-ENOTSUP`, so `DISARM_TAS` is not supported | No in-place restoration; only a NIC cycle leaves TSN mode | #360 REQ-F-TAS-DISABLE-001, decided by #362 ADR-TSN-002 |
| Open all gates through a dedicated IOCTL | An in-place release needs raw writes | #359 REQ-F-TAS-OPENALL-001 |
| Replace a running schedule through a dedicated IOCTL | Same | #358 REQ-F-TAS-REPLACE-001 |
| PHC kept across Restart: `init_ptp` writes SYSTIM | PHC continuity, so complete restoration, is unreachable | #348 |
| Restoration reported from hardware | Phase 3 relies on Debug-only READ_REGISTER | #361 REQ-F-TAS-RESTORE-001, #353 REQ-NF-TAS-OBSERVE-001 |
| A NIC cycle always clearing a schedule | Recovery can need a second cycle | #349, verified by #377 |

### The seven points

**1. Failure scenarios: PHC reset, BaseT and an active gate.**

| Scenario | Status | Evidence | Consequence |
|---|---|---|---|
| Restart reseeds the PHC | Observed | 9 of 9 Restarts, to 9.25 to 9.34 s, with and without `SETUP_TAS` | Expected in every run |
| The schedule survives Restart | Unverified for a schedule | Under `SETUP_TAS` all 40 TSN registers survived, 3 of 3 | Assumed for safety |
| Reseed below BaseT with the schedule kept | Predicted | From the two rows above | TX held until the PHC reaches BaseT, as in #349 |
| No reseed, or one above BaseT | Not observed | | The schedule runs on; no extra hold |
| Pause waits for held frames | Observed under `SETUP_TAS`; predicted under a schedule | No Pause in 60 s with gauge above 0, 4 of 4, after NDIS accepted the restart, 1 of 1 | Under a running schedule the held frames should leave at the next window and the stall end within one closed interval. Case D tests this |
| A window write on a running schedule | Observed | Read back but did not apply, 29 s and more; only a future BaseT replaced a schedule, 6 of 6 | Readback never shows the schedule in force |
| Schedule survives a NIC cycle | Observed | 1 of 41 cycles, #349; a second cycle cleared it, 1 of 1 | Recovery can need two cycles |

**2. Upper bound on a hold, and its evidence. Predicted.** After a reseed to R with BaseT kept at B, frames are held until the PHC reaches B, then until the next window opens: at most (B − R) / 1.0293 + 1 s of host time. R is 9.25 to 9.34 s, 9 of 9; the rate 1.0293 is from 3 runs. B is set 2 to 3 s ahead of the PHC when the schedule is written, so the bound depends on the PHC at that moment. **Rule: the tool refuses to write a schedule when the PHC reads above 60 s**, which keeps the bound under (63 − 9.25) / 1.0293 + 1 ≈ 53 s. That is a prediction from 9 reseeds, not a guarantee: a reseed elsewhere invalidates it, which point 3 detects. A fresh NIC cycle leaves the PHC near 9.3 s, so a run started straight after one passes this rule.

**3. Detecting a stalled schedule.** After the trigger the tool sends tagged frames every 50 ms for at least the computed bound plus 2 s, and reports TX as stalled when none reaches the observer for longer than one cycle past the predicted release. Supporting, never deciding: gauge rising, TDH frozen while TDT moves, BASET_H and BASET_L against the PHC. The observer must keep capturing other frames meanwhile, or the run is INCONCLUSIVE rather than stalled.

**4. Bounded test-owned cleanup on success, failure and timeout.** The three phases above, on every way out, with the trigger thread stopped within 5 s first. Phase 2 uses the operator block until another mechanism is approved. The verdict is `CLEANUP_FAILED` while TSN mode stays set, and at best `CLEANUP_FAILED` after a cycle, because the PHC is reset (D9, observed 9 of 9).

**5. A NIC or PnP operation that outlives its thread.** The operator block stops at the first failure, and a `Disable-NetAdapter` that does not return is never followed by enable. `restore()` behaves the same way through its bounded threads, if approved. In both, a timeout ends the waiting, not the kernel operation: further NDIS operations wait for Q, and a reboot is Q's decision.

**6. Recovery if the management session fails.** Management runs over the I219, `Ethernet`; only the I226, `Ethernet 3`, is mutated, and every run reports where the session runs. Nothing in the tool depends on the session, and all evidence is written locally. If the session drops, the run ends on its own bounds and the adapter stays as left until someone runs the recovery block. Physical access is UNKNOWN and is not assumed.

**7. STOP and FAILED_SETUP conditions.**

| Condition | Verdict | Before any write? |
|---|---|---|
| Driver not Debug, by `GET_VERSION` | BLOCKED | Yes |
| Eligible adapter without register evidence, not I226-LM | BLOCKED, no register access | Yes |
| tasArmed set, TSN mode set, or BaseT nonzero | BLOCKED, adapter not clean | Yes |
| Observer not proved | BLOCKED | Yes |
| PHC above 60 s | FAILED_SETUP | Yes |
| Phase 1 arrivals outside 200 to 400 ms, or a gap | FAILED_SETUP, no trigger | No: the schedule is written |
| Case C: gauge not 0 after a quiet cycle | FAILED_SETUP, no trigger | No |
| Case D: gauge 0, or a held frame seen, before the trigger | FAILED_SETUP, no trigger | No |
| HIL lock held, or a CI job running | the wrapper does not start | Yes |
| No Restart entry within 60 s, the trigger not back within 5 s, the observer silent, `CONTEXT_REPLACED`, NDIS 10007 or 10058 | the run stops and reports; no global halt | No |

### Build plan

1. Pure rules first, with offline cases and mutants: the PHC to window phase map, the hold bound and the PHC rule, the Case C and D precondition classes, and stall detection.
2. `tas_gate_investigation`: adapter selection by capabilities, as `tas_plan_build` does, instead of the device IDs still in `main`; the register map limited to I226-LM as evidence.
3. The lifecycle trigger and measurement restored from `1a93f56^`, the old `--lifecycle-a` path, for `--case-c` and `--case-d` only.
4. The wrapper restored from `1a93f56^` for those two modes: readiness, lock, DbgView, NDIS trace, post checks and checksums. Its driver check must hash the service's `ImagePath`, which the runner does not.
5. Its offline test restored without the 139 s: the interrupt cases waited on fixed sleeps, and those are the first thing to measure.
6. Three runs each: the test, then the operator block as phase 2, then the dry run as phase 3. The tool itself implements phases 1 and 3 and records the outcome of phase 2.

### Layered cleanup, Q's decision of 2026-11-10

This replaces the operator block as phase 2 for C and D. The operator stays the final recovery path, never a silent substitute.

| Layer | Owner | When | Mechanism | Record |
|---|---|---|---|---|
| Primary | `tas_gate_investigation` | Every executable exit path after its first register write: success, failed setup, stop condition, timeout inside the tool | `restore()`: `CM_Disable_DevNode` and `CM_Enable_DevNode` on the target devnode, bounded threads, no enable after a failed disable | A cleanup record file beside the log, written before and after the cycle, plus a `PRIMARY_CLEANUP` verdict line |
| Fallback | The #364 wrapper | Only when the fallback rule below says so | `Disable-NetAdapter` and `Enable-NetAdapter` on the target only, bounded, no enable after a failed disable: the path that recovered 5 of 5 Case B holds | A `FALLBACK_CLEANUP` verdict, including `NOT_NEEDED` and `REFUSED` with the reason |
| Verification | The wrapper, in a separate process | After the last cleanup layer, whatever it did | The read-only dry run through the runner, compared with the tool's pre-state record: TSN registers, tasArmed, tagged TX on the observer, PHC continuity, driver and hardware agreement | A `RESTORATION` verdict from the four facts of #361 |
| Final | The operator | Only when both layers failed or a layer refused for safety | The Case B block, or a reboot, Q's decision | The wrapper's report names what is left |

**Fallback rule.** One decision from the primary record and the adapter's state, as a pure function with offline cases and mutants:

| Primary record | Adapter | Fallback |
|---|---|---|
| Cycle done, verification shows the pre-state | any | `NOT_NEEDED` |
| Cycle done, verification shows TSN state left (#349) | Up | `RUN`: a second cycle cleared #349 once |
| Disable or enable did not return | any | `REFUSED`: an NDIS operation may still be outstanding, so nothing overlaps it; operator |
| No record, or a record without its closing line: the tool crashed, was killed or timed out | Disabled, in transition, or problem code set | `REFUSED`, same reason |
| Same | Up, no problem code | `RUN` |
| The tool never wrote a register | any | `NOT_NEEDED`: nothing to restore; verification still runs |

**Verdicts, kept apart.** Test, the C or D test cases; `PRIMARY_CLEANUP` DONE, FAILED, TIMED_OUT or NOT_REACHED; `FALLBACK_CLEANUP` NOT_NEEDED, DONE, FAILED, TIMED_OUT or REFUSED; `RESTORATION` VERIFIED, `INCONCLUSIVE_RESTORATION` or `CLEANUP_FAILED`. With the PHC reset on every Restart and every cycle, `RESTORATION` cannot reach VERIFIED today (D9, #348); the other three facts are still reported.

**Hardware verification of both layers, failure paths included.** Each needs its own run, all on the I226 with operator authorization per run:

| Path | How it is reached | Expected |
|---|---|---|
| Primary succeeds | An ordinary C run | Primary DONE, fallback NOT_NEEDED, verification against the pre-state |
| Primary skipped | A Debug-only `--inject-cleanup skip` in the tool: it writes its record as not reached and exits | Fallback RUN and DONE |
| Tool killed mid-run | `--inject-cleanup crash`: the tool ends itself after the schedule write, leaving an open record | Fallback RUN when the adapter is Up |
| Overlap refused | `--inject-cleanup hang`: the tool records a disable that did not return, without starting one | Fallback REFUSED, operator named |
| Schedule left after the cycle | Cannot be forced: #349 appeared once in 41 cycles | Covered offline by the rule; on hardware only if it occurs |

The injections exist only to exercise the cleanup layers; they never change a C or D test verdict, and each run reports which one was used.

## Activation runs for #357

Made on `4ed976e`, Debug driver `6FB73E9E392C0499`, I226 0x125B, one run each. They are #357 evidence, kept here until that issue resumes. Checksums: `logs/i357-activate-checksums-20261107.txt`.

| Mode | Lead | Before BaseT | In window | Near edge, outside, not seen | Frames |
|---|---|---|---|---|---|
| HOLD | 2.77 s | 0 | 96 | 0 | 96 |
| PASS | 2.13 s | 34 | 63 | 0 | 97 |

Each ran on a fresh reinstall, and PASS's activation required a clean adapter, so the reinstall cleared HOLD's schedule, 1 of 1. A fourth Case A followed through the runner, same driver: `RESUMED_BY_TRAFFIC`, sequence 5, 2, 3, 4, Paused 15.0 us, PHC 103.34 to 9.33 s. The reseed value is now 9.29 to 9.33 s in 4 of 4, which bounds a BaseT hold for Cases C and D when the PHC is small. That bound is a measurement, not a guarantee.

When #357 resumes: the #364 branch reverted the merge, so merging `feat/357-tas-activate` alone brings nothing back. Revert `fd7ac2a` and `8ac01dd` on that branch after merging master.

## Case scope

Case A is the control. Case B stays required by #364: it does not need to gate the first formulation of #384's Expected Results if Cases A, C and D supply enough evidence, but #364 is not closed, and its acceptance not claimed, while Case B or any other required case is outstanding.

### Cases B, C and D

Allowed as explicitly approved `LOCAL_DIAGNOSTIC` investigations, using raw registers where necessary, because no family-neutral schedule interface exists yet (G4). Each one:

- is authorized individually before any hardware execution;
- selects adapters from verified capabilities, never DID, name or vendor. The raw modes of this tool still pick adapters by DID in `main`, so that must be replaced before any of these cases runs;
- verifies the register layout and hardware support before access. `QUERY_HW_SNAPSHOT` reports the family's SSOT offset per register, so the compiled map can be checked against the driver with no identity involved. It does not cover BaseT, cycle or windows (G5), and on the I226 not `TQAVCTRL` either (F15), so how those are verified is still open;
- stays separate from the authoritative TAS API tests, and is never presented as #384 RED or GREEN;
- keeps full evidence, safety prerequisites and test-owned restoration, on every way out.

## Stop conditions

The tool ends on its own bounds. On any of these the run stops, and the state is examined before the next step. This is no global halt (Q 2026-11-07): each later run proves its own prerequisites.

- no Restart entry within 60 s;
- the trigger does not return within 5 s;
- the observer stops seeing frames;
- `CONTEXT_REPLACED`, or event 10007 or 10058;
- a bugcheck.

## Recovery

- Nothing restores the PHC. If it was reseeded, it stays reseeded, and later tests must prove their own prerequisites.
- Cleanup is owed on every outcome. The tool prints the original result and the cleanup verdict together, the post-checks verify state, and both results are kept. An unexpected result is never a reason to skip them. The full strategy is in "Cleanup and recovery".
- The tool never administers a NIC, and an unexpected state is examined before any recovery is chosen.
- If a NIC cycle is chosen because TX did not recover: the affected adapter only, `Disable-NetAdapter`, 5 s, `Enable-NetAdapter`, then TC-TSNREG-001 and TX on its observer. Never all adapters, and never the one carrying management. A NIC cycle adds its own PHC reset and a new context.
- If disable blocks, do not enable. A reboot is your decision.

## Open risks and evidence gaps

- If an adapter's instance does not follow the observed layout, that adapter is refused, and targeting needs another mapping (G1).
- `RESUMED_BY_TRAFFIC` rests on documented NDIS order. Whether that order holds for a filter-initiated restart is itself undocumented.
- The trigger path has no client and no prior use in this repository. This is its first run on this stack.
- Cases B, C, D and E stay separate steps. Their Pause risk is higher than #364 states.
