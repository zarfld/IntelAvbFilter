# #364 Case A: FilterPause and FilterRestart, no schedule

Status 2026-11-05: **offline preparation only. Nothing here has run on hardware, and hardware execution is not authorized.** No trigger was issued and no NIC was administered. See "Blockers" before any step below. Governing issue: #364. Evidence class of any run: `LOCAL_DIAGNOSTIC`, never hardware RED or GREEN. No relationship to #328 is claimed.

## Facts

Source facts, read from the code, unless marked otherwise.

| # | Fact | Where |
|---|---|---|
| F1 | `IOCTL_FILTER_RESTART_ONE_INSTANCE` returns success when no instance matches | `src/device.c:654-658`: `Status` stays `STATUS_SUCCESS` |
| F2 | It also discards `NdisFRestartFilter`'s status, so a refused request returns success | `src/device.c:661`; the prototype returns `NDIS_STATUS` |
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
| F15 | `QUERY_HW_SNAPSHOT` covers TIMINCA, TSAUXC, timestamp control, target time, TQAVCTRL and RAL0/RAH0. It does not cover BaseT, cycle or window registers | `include/avb_ioctl.h:211-224` |
| F16 | Instance names are `{interface GUID}-{filter GUID}-0000`. Seen once, on the I210: first GUID is its `InterfaceGuid`, second is `NetCfgInstanceId` in the INF. The I226 name has not been seen | `logs/dbgview_test_ptp_phc_stability.exe_20260430_123620.log` line 2014 |
| F17 | `Install-Driver.ps1 -Action Reinstall` disables **every adapter with the filter bound**, runs `netcfg -u`, deletes all cached driver packages, and has a path that offers `Restart-Computer -Force` | `tools/setup/Install-Driver.ps1:249`, `:305-340` |
| F18 | #350 requires no MMIO unless the filter is Running | #350 Technical Constraints |

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

**UNVERIFIED:** that any of these fire for this trigger. `State` and `Action` are bare numbers with no published value map, so their meaning has to be read from an observed sequence. "Entering" a state is not obviously the same as completing it. The `Microsoft-Windows-NDIS/Operational` and `/Diagnostic` channels are disabled and hold no history, so no past event can show this.

Machine state, read 2026-11-05: I219-LM "Ethernet", I210 "Ethernet 2" and I226-LM "Ethernet 3" are all Up at 1 Gbps with the same default gateway. The installed driver is `5BA3FCBE...`, identical to the local Debug build, service registered 16:50:23 (event 7045) and running from DriverStore `intelavbfilter.inf_amd64_33c1d489bc014e60`. Source under `src`, `devices` and `include` matches `HEAD`. A file on disk does not prove the loaded image.

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
| Exact instance | The record starts with `{interface GUID}-`, exactly one does, and no record is a prefix of it (TC-MOCK-014); deltas on the target only; `FilterInstanceName` and `MiniportIfGuid` in event 10312 | Layout seen once (F16), never on the I226: the dry run confirms it |
| FilterPause entered | `FilterPauseCount`, `PauseRestartGeneration` | Direct |
| FilterPause completed | FilterRestart runs only from Paused, per the NDIS state table; filter state event | Inferred; direct only if ETW fires |
| FilterRestart entered | `FilterRestartCount` | Direct |
| FilterRestart completed | No driver signal. Frames sent by a protocol and seen on the observer after the entry; filter state event | Inferred; direct only if ETW fires |
| Bring-up succeeded | No signal (F8, F9). The clock lines show values; the Debug log has `intel_init() returned` | **Gap** |
| Other adapters untouched | Per-adapter statistics deltas and oper status; ETW for all adapters | Counters direct, for what they count |
| PHC | Paired PHC and host-time windows before and after | `DISCONTINUOUS` can be claimed. Continuity cannot: no hardware tolerance exists |
| TX and observer | Tagged frames, source MAC checked, on the I210 | Direct, broadcast only |
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

`tests/diagnostic/tas_gate_investigation.c --lifecycle-a --observer <did> [--dry-run]`, with `AVB_HIL_TARGET_DID` naming the target.

- Dedicated IOCTLs only: no `READ_REGISTER` or `WRITE_REGISTER`, so no I226 register map decides anything. Identity selects adapters and nothing else. TDH, TDT and occupancy are therefore not measured in Case A, so Q3 stands on the gauge and the wire here.
- Pure logic in `tests/ioctl/tas_lifecycle_eval.h`, offline cases TC-MOCK-014 to 017 in `test_tas_restore_contract`. Those cases say nothing about hardware. The instance names in them are synthetic.
- Every wait is bounded. The trigger runs on its own handle and thread, so a blocked call cannot stop the polling.
- No MMIO from before the trigger until the lifecycle is shown (F11, F18). Statistics, which touch no hardware, are polled throughout. Frames follow only a Restart entry, and the clock and snapshot follow only those frames. Short of that the run stops with those facts unmeasured, which is never a pass.
- PHC: the tool claims `DISCONTINUOUS` when the clock did not advance, and otherwise `NOT_JUDGED_NO_TOLERANCE`. Exit 0 never means continuity.
- No NIC cycle: it would add a second PHC reset and a new context to the evidence.
- No repetition count. One run, then the evidence decides what is next.

## Operator steps

Not authorized yet. Run from an elevated PowerShell at the repository root. This tool is `Diagnostic`, with no runner entry, so it is started directly, as #364 specifies.

**1. Driver.** The required Debug driver is already installed, so no install is planned. If step 2 fails, stop: a reinstall takes the I219 and its VNC session down with every other bound adapter (F17), and needs its own authorization and recovery path.

**2. Identity and evidence folder.**

```powershell
$ev = "C:\Users\User\cqv\i364\evidence\caseA-$(Get-Date -Format yyyyMMdd_HHmmss)"
New-Item -ItemType Directory -Force $ev | Out-Null
git rev-parse HEAD | Tee-Object "$ev\identity.txt"
git status --short | Tee-Object -Append "$ev\identity.txt"
Get-FileHash "$env:SystemRoot\System32\drivers\IntelAvbFilter.sys", ".\build\x64\Debug\IntelAvbFilter\IntelAvbFilter.sys", ".\build\tools\avb_test\x64\Debug\tas_gate_investigation.exe" | Format-List Path, Hash | Tee-Object -Append "$ev\identity.txt"
.\tools\test\Test-DriverPreFlight.ps1 | Tee-Object "$ev\preflight.txt"
```

Stop if the installed hash differs from the build.

**3. Registers before.** ReadOnly. It takes the HIL lock itself, so it runs outside the lock below.

```powershell
.\tools\test\Run-Tests-Elevated.ps1 -Suite HardwareIntegration -TestName ssot_register_validation_test -ResultsJson "$ev\tsnreg-before.json"
```

**4. Dry run.** No trigger and no NIC administration. Effects: about 3 s of tagged broadcast and ICMP on the I226, and a promiscuous capture on the I210. Expected: every adapter listed, one instance for the target, `dry run complete`, exit 3.

```powershell
$env:AVB_HIL_TARGET_DID = '0x125B'
. .\tools\lib\HilLock.ps1
$lock = Enter-AvbHilLock -Kind local -Label 'i364-case-a-dry' -WaitSeconds 60
if (-not $lock.Ok) { throw "HIL lock busy: $($lock.Why)" }
try {
    .\build\tools\avb_test\x64\Debug\tas_gate_investigation.exe --lifecycle-a --observer 0x1533 --dry-run *>&1 | Tee-Object "$ev\dryrun.log"
    "exit=$LASTEXITCODE" | Tee-Object -Append "$ev\dryrun.log"
} finally {
    $null = Exit-AvbHilLock -Owner $lock.Owner
}
```

Stop on any other result. Hand back the log: that is the first real look at the instance names.

**5. Case A.** Only after reviewing the dry run. Expected effects, from source: a short I226 datapath pause; `SYSTIM` reseeded; TSAUXC, TSYNCRXCTL, TSYNCTXCTL and ETQF(3) rewritten; stale TX timestamps dropped; timers stopped and restarted. Link is expected to stay up, UNVERIFIED. I219 and I210 are not targeted.

```powershell
. .\tools\lib\HilLock.ps1
$lock = Enter-AvbHilLock -Kind local -Label 'i364-case-a' -WaitSeconds 60
if (-not $lock.Ok) { throw "HIL lock busy: $($lock.Why)" }
$dv = .\.github\skills\DbgView\Start-DbgViewCapture.ps1 -LogFile "$ev\dbgview.log"
try {
    logman start AvbI364 -p Microsoft-Windows-NDIS 0x4316 0x5 -o "$ev\ndis.etl" -ets -ct perf
    .\build\tools\avb_test\x64\Debug\tas_gate_investigation.exe --lifecycle-a --observer 0x1533 *>&1 | Tee-Object "$ev\case-a.log"
    "exit=$LASTEXITCODE" | Tee-Object -Append "$ev\case-a.log"
} finally {
    logman stop AvbI364 -ets
    .\.github\skills\DbgView\Stop-DbgViewCapture.ps1 -ProcessId $dv.Id
    $null = Exit-AvbHilLock -Owner $lock.Owner
}
```

**6. NDIS events.** Not hardware.

```powershell
Get-WinEvent -Path "$ev\ndis.etl" -Oldest -ErrorAction Continue |
    Where-Object { $_.Id -in 10007,10043,10050,10058,10103,10104,10105,10106,10107,10312 } |
    ForEach-Object { '{0:o} {1} {2}' -f $_.TimeCreated, $_.Id, $_.ToXml() } |
    Set-Content -Encoding utf8 "$ev\ndis-events.txt"
```

**7. Registers after, then hand back the folder.**

```powershell
.\tools\test\Run-Tests-Elevated.ps1 -Suite HardwareIntegration -TestName ssot_register_validation_test -ResultsJson "$ev\tsnreg-after.json"
```

## Blockers

Open before any operator-controlled hardware execution:

1. **Recovery path.** VNC runs over the I219. Case A does not target it, and that is unproven. A path that works with the I219 down must exist first, such as console access. You decide what counts.
2. **ETW emission.** Whether a filter-initiated restart emits these events cannot be settled offline: it needs a filter state transition. The lowest-risk check is a passive session across an operation that is already authorized for another reason, adding no trigger. Without emission, Pause and Restart completion stay inferred, and lifecycle completion is not claimed.
3. **Reading the events.** Before relying on them, the `State` sequence must be read on the target's `FilterInstanceName`, ending in the same state it began. An interpretation is recorded before the run, not after.
4. **Target name.** The I226 instance name has not been seen (F16). Dry run.
5. **Status gaps.** F2, F8 and F9 stay gaps. You decide whether Case A runs with them recorded, or a governing issue comes first.
6. **SSOT.** The #364 correction is applied, so that the investigation's own text matches what it will measure.

Not blockers, but limits on what any result may say: no PHC continuity claim until a hardware-derived tolerance exists; TDH, TDT and occupancy are unmeasured in Case A, so Q3 can be answered only from the gauge and the wire; the origin of a PHC reset stays open until the #348 measurement exists.

## Stop conditions

The tool ends on its own bounds. Stop the session on any of these, and do not start another case:

- no Restart entry within 60 s;
- the trigger does not return within 5 s;
- I219 connectivity lost;
- the observer stops seeing frames;
- `CONTEXT_REPLACED`, or event 10007 or 10058;
- a bugcheck.

## Recovery

- Nothing restores the PHC. If it was reseeded, it stays reseeded, and later tests must prove their own prerequisites.
- Nothing here is automatic. The tool never administers a NIC, and an unexpected state is examined before any recovery is chosen.
- If you choose a NIC cycle because TX did not recover: I226 only, `Disable-NetAdapter`, 5 s, `Enable-NetAdapter`, then TC-TSNREG-001 and TX on the I210. Never all adapters: VNC is on the I219. A NIC cycle adds its own PHC reset and a new context.
- If disable blocks, do not enable. A reboot is your decision.

## Open risks and evidence gaps

- If the I226 instance does not follow the observed layout, the dry run refuses, and targeting needs another mapping.
- `RESUMED_BY_TRAFFIC` rests on documented NDIS order. Whether that order holds for a filter-initiated restart is itself undocumented.
- The trigger path has no client and no prior use in this repository. This is its first run on this stack.
- Cases B, C, D and E stay separate steps. Their Pause risk is higher than #364 states.
