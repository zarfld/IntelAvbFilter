# #364 Case A: FilterPause and FilterRestart, no schedule

Status 2026-11-06: **Stage 0 passed. Stage 1 reached the end of the dry run on the I226, with the official wrapper verdict `INCONCLUSIVE_RESTORATION`, exit 1, stamp `20261106_151348`. No FilterPause or FilterRestart has ever been triggered.** Stage 2 is not authorized: this file is its plan, for review. See "Results", "Case A plan" and "Blockers". Governing issue: #364. Evidence class of any run: `LOCAL_DIAGNOSTIC`, never hardware RED or GREEN. No relationship to #328 is claimed.

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

Written before any run, so the evidence cannot shape its own reading. Not yet exercised: no trace on this machine holds event 10312.

| Source | Gives | Does not give |
|---|---|---|
| Microsoft Learn, *Filter Module States and Operations*, *Pausing a Filter Module*, *Starting a Filter Module*, retrieved 2026-11-06 | Six states: Detached, Attaching, Paused, Restarting, Running, Pausing. Transitions: Detached to Attaching to Paused to Restarting to Running, Running to Pausing to Paused, Paused to Detached. Paused means the pause is complete, Running means the restart is complete | Any numeric value, any ETW behaviour, whether overlying drivers or the miniport pause too |
| Microsoft Learn, `NdisFRestartFilter`, retrieved 2026-11-06 | "schedules a pause and then a restart operation for the specified filter module" | What follows a failed restart |
| Provider metadata, build 26300 | Event 10312: "Filter %1 entering state %2", `State` a UInt8, plus `FilterInstanceName`, `MiniportIfGuid` | A value map, and whether "entering" is logged before or after the transition |
| Hardware | Nothing yet | Everything above still needs observation |

The documents conflict on one point, and it is not resolved here: the states page says a failed restart returns the module to Paused, while *Starting a Filter Module* says NDIS detaches a module that does not restart.

**Rule.**

1. **Scope.** Only event 10312, with `FilterInstanceName` exactly equal to the tool's `instance` line and `MiniportIfGuid` equal to the target's interface GUID. Other instances on the target are recorded, never counted. 10103 has no instance name, so it can support and never establish. Event 10302 is excluded.
2. **Window.** From the tool's `trigger-issued` marker to `run-end`, ordered by ETW timestamp. Events on the target outside the window are reported as a finding (`outsideWindow`). The markers come from the tool's own provider. Stage 1 never enabled it, so all four Stage 1 traces hold none, against three in Stage 0; `-CaseA` enables it and refuses to trigger if that fails.
3. **Values are positions, never names.** No number is read as a state from a document or from memory. Values are written `a`, `b`, `c`, `d` in order of appearance until a binding exists.
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

**Invariants.** The original result and the cleanup verdict are always both reported. Nothing restores the PHC. `COMPLETE_RESTORATION` is out of reach today, because it needs PHC continuity. A NIC cycle is never restoration by itself and adds its own PHC reset and a new context. The lock is held from before the first check until after the last. Every cleanup step runs in the `finally` of the one before it, as in Stage 1.

| Outcome | Detected by | Test-owned action | Cleanup verdict |
|---|---|---|---|
| `NOT_TRIGGERED` | No counter moved | Read-only post-checks. #364 is BLOCKED until another trigger exists (T5) | From post-checks; the tool still prints one |
| `RESUMED_BY_TRAFFIC` | +1, +1, frames seen | Read-only post-checks. **No cycle** | `CLEANUP_FAILED` if the PHC did not advance, else at best `INCONCLUSIVE_RESTORATION` |
| `PAUSE_ONLY` | No Restart entry in 60 s | Read-only post-checks and full evidence. **The failure is the finding: nothing recovers it automatically** | `CLEANUP_FAILED` or `INCONCLUSIVE_RESTORATION`, by what the checks show |
| `RESTART_ENTERED`, frames not seen | Probe failed | Same | `CLEANUP_FAILED` |
| `CONTEXT_REPLACED` | Attach, detach or a falling counter | Read-only post-checks. Stop the session | By the checks. Whether to rebind or reinstall is Q's decision |
| `OVERLAPPED` | Deltas not attributable | Read-only post-checks. Stop the session | `INCONCLUSIVE_RESTORATION` at best |
| Another adapter changed | `other adapters changed` above 0 | Read-only post-checks. Stop the session. Never touch that adapter | `CLEANUP_FAILED` |
| Tool timeout or crash | Exit code, no `result` line | Wrapper kills the tool and runs every later step. NDIS may still finish | `INCONCLUSIVE_RESTORATION` or worse. The lifecycle outcome stays unknown |
| Ctrl+C | Same | Same chain, as proved by the Stage 1 interrupt cases offline | Same |
| Wrapper killed outright | No `DRY RUN` line, lock left | The tool ends on its own bounds, about 80 s. The next HIL run takes over the lock and stops. `logman stop AvbI364S1 -ets` closes the trace | `INCONCLUSIVE_RESTORATION` |
| Bugcheck or hang | Session lost | None possible. Evidence up to the last flushed file. A reboot is Q's | Unknown |

**Adapter cycling is a separate operation, prepared here and not authorized (Q 2026-11-06).** Automatic cycling after `PAUSE_ONLY` or a failed transmit probe was refused: the failure may be the evidence Case A exists to capture. No Case A code path cycles an adapter, and W8 stays unimplemented. If it is ever authorized:

| Aspect | Design |
|---|---|
| Applicability | Only after Case A ended, its evidence is written and checksummed, and Q reviewed it. Only for `PAUSE_ONLY` or a failed transmit probe. Never after `RESUMED_BY_TRAFFIC`, which would add a PHC reset for nothing. Started by a separate operator command under the HIL lock |
| Target only | Identified by NET_LUID and interface GUID from that run's evidence. Refused if the management check does not read `MANAGEMENT_NOT_ON_TARGET`, or if the identity no longer resolves to exactly one adapter |
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

- The active session's media flows through the I226: address binding, asymmetric traffic and a Reachable neighbor entry on ifIndex 8. The entry is also Reachable on ifIndex 16, so this is not conclusive. `vncserver`'s process I/O counters proved uninformative: they do not capture its socket sends here.
- Case A on the I226 would therefore touch that session, and its media is background traffic during the transition, which Q3 has to treat as a confound.
- The 2026-11-05 recovery did not use VNC. RealVNC could not resolve names until 07:05, NIC links returned at 07:08:37 to 07:08:43, the session was unlocked at 07:14:09, and the first VNC connection came at 07:32:50. What restored the adapters is not in these logs.

**UNRESOLVED**

- Packet-level proof for one session (shortest diagnostic in "Case A operator procedure").
- Which adapter the next session binds, and whether that choice can be steered. Bindings of earlier sessions were not recorded.
- Whether a reconnect is automatic or by hand.
- What exists outside the network: physical console, or any independent out-of-band management.

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

**Recovery prerequisites.**

| Class | Item |
|---|---|
| Verified provision | Reconnect after a transient interruption, usually within 90 s, 29 of 37 |
| Verified provision | RealVNC restarts with the machine, Auto service, seen on 2026-11-05 |
| Verified provision | `-CaseA` refuses when the session's own sockets are on a target adapter or cannot be attributed (W4, offline only) |
| Unresolved | Recovery with all networking down: needs Q's answer on physical or out-of-band access |
| Unresolved | Recovery from a stuck NDIS stack: needs a reboot, and whether one can be forced remotely is unknown |
| Genuine blocker for the first disruptive run | Q's authorization after reviewing this strategy |
| Genuine blocker for the first disruptive run | The session must not be on the target when the run starts. The session that was active at the reading would be refused, by design |
| Not a blocker | Source work, offline tests, read-only hardware diagnostics, runbook and rule development |

During a run nothing depends on the session: the tool and wrapper run on their bounds, write every artifact locally and release the lock. A lost session changes no step: no retry, no second trigger, and no administration of any adapter.

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

**Not authorized.** The wrapper mode exists and is verified offline only: `tests/unit/ci/test_tas_stage1_orchestration.ps1`, 241 of 241, 74 of 74 mutants killed, at commit `36e0124`. That is no hardware evidence, and its trace fixtures stay UNVERIFIED until a real trace exists. Run that test from a process with a console: without one the interrupt cases fail, because the Ctrl+C never arrives.

| # | State | Change to `Invoke-TasStage1DryRun.ps1` |
|---|---|---|
| W1 | Done | `-CaseA` switch. Without it the tool stays `--dry-run` |
| W2 | Done | 10312 and 10103 are expected in this mode. 10007, 10058 and 10050 stay stop events |
| W3 | Done | Correlation reads the tool's `result:` line. The tool's cleanup verdict stands beside the wrapper's, the worse one wins. A run with a trigger never reads `NO_CHANGE_OBSERVED` |
| W4 | Done | Management check on `vncserver`'s own sockets, immediately before the trigger. Fails closed: `MANAGEMENT_ON_TARGET` and `MANAGEMENT_UNATTRIBUTED` both stop it |
| W5 | Done | DbgView through `.github\skills\DbgView\Start-DbgViewCapture.ps1` is required. No confirmed capture, no trigger |
| W6 | Done | Marker provider enabled, or no trigger. The decoded trace is reduced to the rule's sequence, class and claim |
| W7 | Done | SHA256 of every artifact, written last |
| W8 | **Not implemented** | Adapter cycling was refused as proposed. See "Cleanup and recovery" |

A stop before the trigger still runs every post-check, and exits 2.

The pre and post failure-transition rule stays as it is. Because the I219 line flips, a run can read `INCONCLUSIVE_RESTORATION` as Stage 1 did, or `CLEANUP_FAILED` for a pass then a fail (lines 192 and 193, read from the code, not yet seen on hardware). The `FAILED LINE` output shows which line caused it, and that verdict stands with its cause recorded.

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

**Run, only after explicit authorization and with the conditions in "Blockers" met:**

```powershell
Set-Location C:\Users\User\IntelAvbFilter
.\tools\build\Build-Tests.ps1 -Configuration Debug
.\tests\diagnostic\Invoke-TasStage1DryRun.ps1 -CaseA
"case a exit=$LASTEXITCODE"
```

Do not rebuild the driver. The tree must be clean at the commit that was built. Exit: 0 tool exit 0; 3 tool exit 1 or 2; 1 evidence incomplete; 2 trigger not issued. Return the `evidence stamp` line.

| Artifact, `logs\diag-i364-*-<stamp>.*` | Holds |
|---|---|
| `result` .txt | Per-adapter lines, lifecycle, cleanup, sequence class, verdict |
| `dryrun` .log and .stderr | Full tool output, including `edge`, `window`, `fact` and `claims` lines |
| `stage1` .etl and .etl.xml | Raw and decoded NDIS trace, markers |
| `tsnreg-pre`, `tsnreg-post` .log and .json | Register checks and runner results |
| `state-pre`, `state-post` .txt | Adapter state |
| `dbgview` .log, `transcript` .txt | Kernel log, full session |
| `checksums` .txt | SHA256 of everything above |

Read the result per adapter. A lifecycle outcome, a cleanup verdict and a sequence class are three separate results, and none replaces another. Stop conditions apply, and no second case or repeat follows without review.

## From Case A to #384

Issue state read 2026-11-06: #348, #349, #350, #351, #353, #356, #357, #361, #362, #363 and #377 are open. Only #364 and #384 were re-read in full.

| Step | Needs | Gives | Stops if |
|---|---|---|---|
| 1. Review the plan | Q: recovery strategy, session not on the target | A run Q can authorize | - |
| 2. Calibration, optional | A reinstall already needed, never one done for this | `State` binding, emission | - |
| 3. Case A, first run | Step 1, separate authorization | Trigger reach, T1 to T7, Q2, Q4, part of Q3, reachability of the interval through `provenInsideCalls` | `NOT_TRIGGERED`: #364 and therefore #384 are BLOCKED until another trigger exists |
| 4. Case A to 3 runs, plus a `--passive` pair | Step 3 reviewed | #364 acceptance for A, and whether polling changed the transition | - |
| 5. Cases C and D | The seven-point safety analysis still owed, individual authorization, raw modes selecting by capability rather than DID | Q1 and Q3 under a schedule, which is what #384's scenario needs | - |
| 6. Case B | Individual authorization | Required for #364, need not gate step 7 | - |
| 7. #384 Expected Results | Findings of A, C and D; Q's SSOT write | The Expected Results #384 defers to #364, and feeds #356, #362, #348 | - |
| 8. #384 test, RED | TAS query (#350, #353) and activation (#357), governed by #362 and #363; #384 forbids raw registers | Authoritative hardware RED. Case A results can never serve as it | Interval not provably reachable: report the gap and propose a testability solution under #384 or #356, without weakening the assertion |
| 9. #384 GREEN | #356 behaviour, a decision on F9, restoration from #361, the PHC outcome of #348, Debug and Release | Hardware GREEN | - |

Branch scope (Q 2026-11-06): this branch stops at step 6 plus #364's acceptance. Steps 7 to 9 belong to #384, on its own branch, after #364 is merged. #364 may prepare evidence for #384 but does not absorb its implementation.

#384 and #364 still name an I210 observer, while Stage 1 proved an I219 one. The correction is prepared, and publishing it is Q's.

## Blockers

Sorted by what actually stops Case A.

**True conditions for the first disruptive run:**

1. **Authorization.** Stage 2 is not authorized. Q decides after reviewing the recovery strategy.
2. **Recovery understood.** Q knows what recovers the machine with all networking down (see "Management path (RealVNC)").
3. **Session not on the target.** `-CaseA` enforces this at run time, and refuses otherwise.
4. **Hardware check of the reporting correction** of `b836e5a`, which is still offline-only.

**Resolved:** the driver; W1 to W7, offline. **Written, not yet exercised:** the `State` rule and the wrapper's Case A mode.

**Not a condition for development:** the recovery question. Source work, offline tests, read-only diagnostics and runbook work continue without it (Q 2026-11-06).

**Not blockers, but limits on what a result may say:** ETW emission (absent events give `NOT_EMITTED`, and completion stays inferred); F2, F8 and F9; no PHC continuity claim until a hardware-derived tolerance exists; TDH, TDT and occupancy unmeasured in Case A; the origin of a PHC reset, open until the #348 measurement exists; #389 and the transition it causes.

**For Q to decide, not a safety matter:** whether the #364 correction and observer wording must be published before the first run, or may follow it.

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

The tool ends on its own bounds. Stop the session on any of these, and do not start another case:

- no Restart entry within 60 s;
- the trigger does not return within 5 s;
- management connectivity lost;
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
