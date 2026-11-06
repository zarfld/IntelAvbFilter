# #364 Case A: FilterPause and FilterRestart, no schedule

Status 2026-11-06: **Stage 0 ran and passed. Stage 1 ran but never reached the driver, so no adapter, trigger or lifecycle event has been exercised.** Stage 2 is not authorized. See "Results" and "Blockers". Governing issue: #364. Evidence class of any run: `LOCAL_DIAGNOSTIC`, never hardware RED or GREEN. No relationship to #328 is claimed.

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

**UNVERIFIED:** that any of these fire for this trigger. `State` and `Action` are bare numbers with no published value map, so their meaning has to be read from an observed sequence. "Entering" a state is not obviously the same as completing it. The `Microsoft-Windows-NDIS/Operational` and `/Diagnostic` channels are disabled and hold no history, so no past event can show this.

Machine state, read 2026-11-06: three adapters are Up with the same default gateway, so an observer cannot be inferred from routing. The driver is not usable: the service is not installed, the binding still reads enabled, and `IntelAvbFilter.sys` in System32 is `5BA3FCBE...`. A reinstall at 20:51 stayed on disabling an adapter for about 15 minutes until terminated, and one at 21:07 offered a reboot with "installation will auto-complete", which `Install-Driver.ps1:246` shows is an unimplemented TODO. No cause is claimed.

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
- No repetition count. One run, then the evidence decides what is next.

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

**Stage 1: did not run.** Preflight, both register checks and the dry run all stopped at `\\.\IntelAvbFilter` not found (error 2), the `#328 sentinel`, with the service `Running`. No adapter was discovered and nothing was mutated. "Adapter state unchanged" and "0 lifecycle events" therefore prove nothing. The I226 name and the polling resolution remain unknown. That run also used the DID-based commands this design replaces, so it cannot be repeated as it was.

## Driver precondition: HIL environment blocker

Reinstallation is not authorized. The diagnosis below only reads. It addresses the driver only as the precondition for TAS verification.

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

## Operator steps

Not authorized yet, and blocked until the driver is usable. Run from an elevated PowerShell at the repository root. This tool is `Diagnostic`, with no runner entry, so it is started directly, as #364 specifies. No command names an adapter.

The procedure guarantees, on every way out:

- the HIL lock is held from before the first check until after the last verification, so all checks run in this process, where the runner accepts it as `self`;
- the post-state checks run even when the precheck fails, the dry run fails, or it times out;
- the test result and the cleanup verdict are printed separately, and neither replaces the other;
- per-adapter classes are printed as found, never reduced to one word;
- no NIC cycle runs, and none is treated as restoration. `CLEANUP_FAILED` stays failed until you decide on recovery.

**1. Build and identity.** Build with the repository tools. Do not build again before step 2 ends.

```powershell
.\tools\build\Build-Tests.ps1 -Configuration Debug
$stamp = Get-Date -Format yyyyMMdd_HHmmss
Start-Transcript -Path ".\logs\diag-i364-transcript-$stamp.txt"
Get-Process Runner.Worker -ErrorAction SilentlyContinue
.\tools\test\Test-DriverPreFlight.ps1
"preflight exit=$LASTEXITCODE"
$m = Get-Content .\build\tools\avb_test\x64\Debug\test-build-manifest.json -Raw | ConvertFrom-Json
$e = $m.entries | Where-Object name -eq 'tas_gate_investigation'
"manifest=$($m.provenanceMode) rev=$($m.sourceRevision) dirty=$($m.sourceDirty) toolMatch=$((Get-FileHash .\build\tools\avb_test\x64\Debug\tas_gate_investigation.exe).Hash -eq $e.sha256)"
```

Pass: nothing from `Runner.Worker`, `preflight exit=0`, `manifest=clean-commit`, `dirty=False`, `toolMatch=True`. Stop on `ERROR_FILE_NOT_FOUND`: the device node is missing.

**2. Precheck, dry run on every eligible adapter, postcheck, all under one lock.** No trigger and no NIC administration. Effects per eligible adapter: about 3 s of tagged broadcast and ICMP for each observer candidate tried, a non-promiscuous capture, and 200 ms of statistics polling. The dry run is bounded at 300 s and is ended if it overruns.

```powershell
. .\tools\lib\HilLock.ps1
$tool = (Resolve-Path .\build\tools\avb_test\x64\Debug\tas_gate_investigation.exe).Path
$etl = "$PWD\logs\diag-i364-stage1-$stamp.etl"; $dry = "$PWD\logs\diag-i364-dryrun-$stamp.log"
$statePre = ".\logs\diag-i364-state-pre-$stamp.txt"; $statePost = ".\logs\diag-i364-state-post-$stamp.txt"
$preRc = $null; $dryRc = 'NOT_RUN'; $postRc = $null; $traced = $false; $p = $null; $life = 'unknown'; $diff = 'unknown'
$lock = Enter-AvbHilLock -Kind 'local' -Label 'i364 dry run' -WaitSeconds 60
if (-not $lock.Ok) { "STOP lock not acquired: $($lock.Why)" } elseif ($lock.PreviousOwner) { [void](Exit-AvbHilLock -Owner $lock.Owner); "STOP a stale lock was taken over, so machine state is unknown" } else {
    try {
        try {
            Get-NetAdapter | Sort-Object InterfaceGuid | Format-Table InterfaceGuid, Status, MediaConnectionState, LinkSpeed, PromiscuousMode -AutoSize | Out-File $statePre
            $global:LASTEXITCODE = 0
            .\tools\test\Run-Tests-CI.ps1 -Configuration Debug -Suite HardwareIntegration -Tests ssot_register_validation_test -ResultsJson ".\logs\diag-i364-tsnreg-pre-$stamp.json" *>&1 | Tee-Object -FilePath ".\logs\diag-i364-tsnreg-pre-$stamp.log"
            $preRc = $LASTEXITCODE
            if ($preRc -eq 0) {
                logman create trace AvbI364S1 -p Microsoft-Windows-NDIS '0x4316' '0x5' -o $etl -ct perf -ets
                $traced = ($LASTEXITCODE -eq 0)
                $p = Start-Process -FilePath $tool -ArgumentList '--lifecycle-a --dry-run' -NoNewWindow -PassThru -RedirectStandardOutput $dry -RedirectStandardError "$dry.stderr"
                $null = $p.Handle
                if ($p.WaitForExit(300000)) { $dryRc = $p.ExitCode } else { $dryRc = 'TIMEOUT' }
            }
        } finally {
            if ($p -and -not $p.HasExited) { $p.Kill(); $null = $p.WaitForExit(10000) }
            if ($traced) { logman stop AvbI364S1 -ets; tracerpt $etl -o "$etl.xml" -of XML -y | Out-Null }
            Get-NetAdapter | Sort-Object InterfaceGuid | Format-Table InterfaceGuid, Status, MediaConnectionState, LinkSpeed, PromiscuousMode -AutoSize | Out-File $statePost
            $global:LASTEXITCODE = 0
            .\tools\test\Run-Tests-CI.ps1 -Configuration Debug -Suite HardwareIntegration -Tests ssot_register_validation_test -ResultsJson ".\logs\diag-i364-tsnreg-post-$stamp.json" *>&1 | Tee-Object -FilePath ".\logs\diag-i364-tsnreg-post-$stamp.log"
            $postRc = $LASTEXITCODE
            $diff = @(Compare-Object (Get-Content $statePre) (Get-Content $statePost)).Count
            if ($traced -and (Test-Path "$etl.xml")) { $life = @(Select-String -Path "$etl.xml" -Pattern '<EventID[^>]*>(10312|10103|10007|10058|10050)<').Count }
        }
    } finally {
        $rel = Exit-AvbHilLock -Owner $lock.Owner
        "lock released ok=$($rel.Ok) $($rel.Why)"
    }
    if (Test-Path $dry) { Select-String -Path $dry -Pattern ' plan luid=| observer proved | fact register | adapter result | summary ' | ForEach-Object Line }
    $cleanup = if ($postRc -eq 0 -and $diff -eq 0 -and $life -eq 0) { 'NO_CHANGE_OBSERVED (registers and adapter state only, PHC not judged)' } elseif (($diff -is [int] -and $diff -gt 0) -or ($life -is [int] -and $life -gt 0) -or ($preRc -eq 0 -and $null -ne $postRc -and $postRc -ne 0)) { 'CLEANUP_FAILED' } else { 'INCONCLUSIVE_RESTORATION' }
    "TEST RESULT    precheck=$preRc dryRun=$dryRc"
    "CLEANUP RESULT postcheck=$postRc stateDifferences=$diff lifecycleEvents=$life verdict=$cleanup"
}
Stop-Transcript
$stamp
```

Pass, from what the tool found, not from a list of expected hardware:

- `TEST RESULT precheck=0 dryRun=0` and `summary adapters=N eligible=M exit=0`, with M at least 1
- one `plan` line per enumerated adapter, each with a class
- for every `class=ELIGIBLE`: `observer proved`, `fact register ... changed=0 unreadable=0`, and `adapter result ... exit=0`
- `CLEANUP RESULT postcheck=0 stateDifferences=0 lifecycleEvents=0 verdict=NO_CHANGE_OBSERVED`

Each result stands on its own. A failed test with a clean postcheck is a test failure, and a passing test with `CLEANUP_FAILED` is still a cleanup failure. `SKIP_UNSUPPORTED` and `SKIP` are per-adapter results, never a pass for that adapter. `INCONCLUSIVE`, `BLOCKED` and `SKIP_NOT_IMPLEMENTED` make the run exit 1.

The verdict covers what is measured: registers through the register check, adapter state, and NDIS lifecycle events. PHC continuity is not judged.

## Blockers

Open before any further hardware execution:

1. **Driver.** A HIL environment blocker: the service is gone and the network component remains. Reinstallation is not authorized, and it needs the recovery path below.
2. **Recovery path.** The reinstall took management down with it, and stayed on one adapter for about 15 minutes. A path that works with the management adapter down must exist first.
3. **Case A cleanup.** It must own a defined, executable strategy before Stage 2. Manual recovery is a fallback, not a substitute. Original failure and restoration verdict stay separate, and a NIC cycle alone does not prove restoration.
4. **ETW emission.** Whether a filter-initiated restart emits these events needs a filter state transition. The lowest-risk check is a passive session across an operation already authorized for another reason. Without emission, completion stays inferred and is not claimed.
5. **Reading the events.** The `State` sequence must be read on the target's `FilterInstanceName`, with an interpretation written down before the run.
6. **Status gaps.** F2, F8 and F9 stay gaps. You decide whether Case A runs with them recorded, or a governing issue comes first.
7. **SSOT.** The #364 correction is applied, and its observer wording is replaced.

Not blockers, but limits on what any result may say: no PHC continuity claim until a hardware-derived tolerance exists; TDH, TDT and occupancy are unmeasured in Case A; the origin of a PHC reset stays open until the #348 measurement exists.

## Case scope

Case A is the control. Case B stays required by #364: it does not need to gate the first formulation of #384's Expected Results if Cases A, C and D supply enough evidence, but #364 is not closed, and its acceptance not claimed, while Case B or any other required case is outstanding.

### Cases B, C and D

Allowed as explicitly approved `LOCAL_DIAGNOSTIC` investigations, using raw registers where necessary, because no family-neutral schedule interface exists yet (G4). Each one:

- is authorized individually before any hardware execution;
- selects adapters from verified capabilities, never DID, name or vendor. The raw modes of this tool still pick adapters by DID in `main`, so that must be replaced before any of these cases runs;
- verifies the register layout and hardware support before access. `QUERY_HW_SNAPSHOT` reports the family's SSOT offset per register, so the compiled map can be checked against the driver with no identity involved. It covers `TQAVCTRL`, not BaseT, cycle or windows (G5), so how the rest is verified is still open;
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
- Cleanup is owed on every outcome. The tool prints the original result and the cleanup verdict together. Steps 7 and 8 verify state, and both results are kept. An unexpected result is never a reason to skip them.
- Nothing here is automatic. The tool never administers a NIC, and an unexpected state is examined before any recovery is chosen.
- If you choose a NIC cycle because TX did not recover: the affected adapter only, `Disable-NetAdapter`, 5 s, `Enable-NetAdapter`, then TC-TSNREG-001 and TX on its observer. Never all adapters, and never the one carrying management. A NIC cycle adds its own PHC reset and a new context.
- If disable blocks, do not enable. A reboot is your decision.

## Open risks and evidence gaps

- If an adapter's instance does not follow the observed layout, that adapter is refused, and targeting needs another mapping (G1).
- `RESUMED_BY_TRAFFIC` rests on documented NDIS order. Whether that order holds for a filter-initiated restart is itself undocumented.
- The trigger path has no client and no prior use in this repository. This is its first run on this stack.
- Cases B, C, D and E stay separate steps. Their Pause risk is higher than #364 states.
