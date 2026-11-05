# #328 isolated TAS experiment: safety assessment and transition analysis

Test: `tests/hardware/test_tas_isolated_arm.c`, TC-TASARM-001..003. Offline logic: TC-MOCK-012.

Evidence labels: **[S]** source-confirmed, **[H]** observed on HIL, **[T]** hypothesis. Linux behaviour is a source fact about Linux. It is not proof of Windows or I226 behaviour.

The device-node disappearance (Scenario A) and the unload hang (Scenario B) are separate unresolved scenarios. This test does not unload the driver or stop the service, so it does not address either directly.

## Why this test exists

`test_tsn_ioctl_handlers` sends TAS, FP and PTM in one run, so TAS has never been isolated [S]. The stalls of 2026-11-03 and 2026-11-04 followed runs of those tools [H]. No armed-state register or per-queue measurement exists. Healthy-baseline values, including `ENDQT=0`, say nothing about gate behaviour.

## Safety classification

`Unsafe` in `$HilTestSafety`, the existing class for NIC administration, because restoration disables and enables a device node. It carries the #328 hazard annotation. It is a coverage-gap row, so automatic suites report it as not executed.

## Mutation scope

| Mutation | Source | Restored by |
|---|---|---|
| `TQAVCTRL` TRANSMIT_MODE and ENHANCED_QAV set | `setup_tas` | NIC cycle, to be proven |
| `QBVCYCLET` = 1 ms | `setup_tas` | NIC cycle, to be proven |
| Driver `tas_armed`, `last_tas_config` | SETUP_TAS handler | New adapter context, to be proven |
| Broadcast UDP, 20 per second, 64-byte payload, discard port | Test | Ends with the test |
| Filter detach and attach, MAC and PHY reinitialised | NIC cycle | Compared afterwards |
| PTP state, possibly including the PHC | NIC cycle | Compared afterwards; may not be restored |

No FP, PTM, unload, service stop, reinstall or raw register write. Only the adapter under test is cycled, found by NET_LUID through its PnP instance, never by name or model.

## Prerequisites (BLOCKED, no mutation, if unmet)

Debug driver, because `READ_REGISTER` exists only there. Handle bound by NET_LUID. Link up, IPv4 present. Another adapter up with a gateway, which is the existing independent management link rule. `tasArmed=0`. All 42 baseline registers readable. The TX probe healthy before arming, otherwise it could not detect a change.

## Baseline capture

42 registers: mode, buffers, scheduling, per-queue windows, `TXQCTL`, CBS, `TCTL`, `TDLEN`, `TXDCTL`, `TIMINCA`, `TSAUXC`, `TSYNCTXCTL`, `TSYNCRXCTL`, `RAL0`, `RAH0`. Also identity, statistics, PHC and QPC, and a TX window.

## TX progress and completions

Samples before, during (0, 1, 2, 5, 10, 30 s) and after restoration: `TDH`/`TDT` per queue, `TQAVCTRL`, `BaseT`, both cycle registers, the send gauge, and OS out, discard and error counters.

Limits: the driver exposes no per-queue completion count. `TDH` shows fetch progress, not completion. The gauge is adapter-wide and a lower bound. OS counters come from the miniport, so they are not independent wire evidence. TX health is therefore a conjunction: head moved, rings settled, gauge did not grow, OS counters rose.

## Restoration and verdicts

`disable_tas` returns `-ENOTSUP`, so a NIC cycle is the only path today [S]. A previous recovery is not proof that it restores everything. TC-TASARM-003 measures it.

| Verdict | Condition |
|---|---|
| Restored (`PASS`) | Cycle completed, same LUID, DID and MAC, new adapter context proven, `tasArmed=0`, TSN mode clear, all 42 registers equal, PHC continuous, link up, TX probe healthy |
| `CLEANUP_FAILED` (demonstrably unsuccessful) | Cycle failed or exceeded its bound, adapter did not return, still armed, TSN mode set, any register differs, PHC discontinuous, or TX unhealthy after the cycle |
| `INCONCLUSIVE_RESTORATION` (could not be measured) | New context unproven, a register unreadable, nothing compared, PHC unreadable, link down so no TX probe |

Unreadable state is unmeasured, so it gives `INCONCLUSIVE_RESTORATION`, never `CLEANUP_FAILED`. Link and traffic returning are never sufficient. A demonstrated failure outranks missing evidence. Even `PASS` covers observed state only: internal scheduler, FIFO and retry-buffer state are not visible.

`FilterAttachCount` is not used as proof of a cycle: it lives in the adapter context and is destroyed at detach. A lower `IoctlCount` shows a new context.

A PHC reset by the cycle would be a real unrestored change and is reported as `CLEANUP_FAILED`. The test does not reseed the PHC, because that would add a mutation.

## Specific risk: prolonged I226 hang

Earlier adapter disables took from 5 s to about 18 minutes to release while sends were stuck, and one unload stalled for about 14 h 43 min [H]. Disable is bounded at 120 s and enable at 60 s, on a worker thread, so a stall ends in `CLEANUP_FAILED` with evidence instead of a killed test. Those bounds limit the test only, not Windows: the PnP operation can continue long after the test returns, with the adapter in an unknown state. After a timed-out disable the test does not attempt enable. An abort after arming reports `INCONCLUSIVE_RESTORATION`. VNC runs over the I219, which is not touched. A reboot is an operator decision and is never automatic. This risk applies to this experiment only.

## Can configuration precede TX enable?

| Step | Linux `igc` [S] | Windows [S unless marked] |
|---|---|---|
| Decide | `igc_tsn_offload_apply`: mode change schedules `reset_task` | No filter-initiated miniport reset found in the documentation read |
| Quiesce | `igc_down`: RX off, carrier off, queues stopped, `TCTL.EN` off, NAPI off | NDIS pause order, driven by the OS, not the filter |
| Reset | `igc_reset`: MAC reset | Inside e2fn; a NIC cycle is the only trigger we have |
| Program TSN | `igc_tsn_reset` inside `igc_reset`, before the rings restart | **Unknown:** no documented hook between MAC reset and TX enable |
| Start | `igc_up`, rings enabled | `FilterAttach`, then NDIS restart, miniport first, filters bottom-up |

The filter first gets control at `FilterAttach`. NetAdapterCx documentation places TX queue creation late in power-up, so a window may exist, but nothing documents it. `[#328-TX-WINDOW]` measures `TCTL`, `TXDCTL0`, `TDLEN0` and `TQAVCTRL` at `FilterAttach` and `FilterRestart` in Debug builds.

| Observation | Meaning |
|---|---|
| TX disabled at `FilterAttach` | A window exists; ownership and e2fn tolerance remain open [T] |
| TX enabled at `FilterAttach` | No filter window; a compliant transition needs miniport cooperation [T] |

Writing e2fn-owned registers remains outside documented NDIS contracts, even with a window.

## First armed run, 2026-11-05

One run: I226 0x125B, Debug driver F3CC845E, commit 7512c10. Local diagnostic evidence, not a closed requirement.

| Test | Verdict | Observed |
|---|---|---|
| `TC-TASARM-001` | FAIL | `TQAVCTRL=0x9`, cycle 1 ms, `BaseT=0`, shadow cycle 0, although `SETUP_TAS` returned success |
| `TC-TASARM-002` | FAIL | TX flowed about 5 s, then `TDH0` froze at 1361 while `TDT0` reached 1616; gauge 668, `osOut` frozen, no OS errors or discards |
| `TC-TASARM-003` | `CLEANUP_FAILED` | Cycle completed, 42 registers equal, `tasArmed=0`, TX healthy, but the PHC restarted (46.5 s to 9.3 s, drift -85.8 s) |

Limits: the armed registers beyond `TQAVCTRL`, `BaseT` and the cycle registers were not sampled. The gauge returning to 0 proves nothing about draining, because the cycle created a new context. A NIC disable with 668 outstanding sends did not stall, but it is not a driver unload, so Scenario B is not refuted. DbgView lost about 83 % of messages, so missing `[#328-TX-WINDOW]` lines prove nothing.

Next: the default run repeats this with dense sampling and armed register dumps. `--armed-period-ms 200` lowers the rate. A stall near 5 s follows time; one near 17 s follows the frame count. Neither result names a mechanism on its own.

## Frame-count result, 2026-11-05

Same driver family, Debug, I226 0x125B. Two further runs at e8821bc, driver 4358F5A3, confirmed the stall.

| Run | Period | Onset | `TDH0` advance | `osOut` advance |
|---|---|---|---|---|
| 7512c10 | 50 ms | about 5 s | 84 | 84 |
| e8821bc | 50 ms | about 5 s | 87 | 85 |
| e8821bc | 200 ms | about 17 s | 86 | 85 |

The stall follows the count, not time. Arming changes `TQAVCTRL` and `QBVCYCLET` only. `STQT` and `ENDQT` stay 0 while armed. `TXDCTL0`, `TCTL` and the PHC rate show no change at the stall. `TDT - TDH` reached exactly 255 at 50 ms while the gauge kept rising. The stall count did not change at 5 frames per second, so whatever fills does not drain measurably.

None of this shows whether the frames reached the wire. `TDH` and `osOut` are host-side.

### Competing hypotheses and `--stall-probe` predictions

| Hypothesis | Frames to stall as payload grows | Bytes to stall | Frames seen on the wire |
|---|---|---|---|
| Closed or misprogrammed gates | Fall | Constant after per-frame overhead | None after arming, at every size |
| Packet buffer not released | Fall | Constant after per-frame overhead | All, up to the stall |
| Descriptor or ring limit | Constant unless descriptors per frame change | Rise with size | All, up to the stall |
| Missing completion or miniport limit | Not tied to size | Not tied to size | All; completions fall behind before fetching stops |

Do not assume 85 descriptors means 85 frames in the 20 KB buffer: per-frame buffer overhead is unmeasured and 85 frames of 106 bytes is about 9 KB. Sizes 64, 512 and 1400 fit capacity and overhead and leave one point to check the fit.

Wire evidence limits: capture on another NIC in this host is supplementary. It needs a baseline, enforced by the probe before `SETUP_TAS`. ICMP is supplementary only; it needs a verified route because all adapters have equal default routes, and each echo uses a descriptor.

### PHC restoration, separate defect

`init_ptp` in `devices/intel_i226_impl.c` writes `SYSTIM=1` unconditionally on every `AvbBringUpHardware`, so on every `FilterAttach` and `FilterRestart`. The I210 path guards this. Source-confirmed for the filter; whether hardware or the miniport also resets the PHC is undetermined, because the write hides any earlier reset.

## Stall probe result, 2026-11-05

Commit 73773d4 (test code a5b786c), Debug driver FB64A096, I226 0x125B, one run per size. Local diagnostic evidence.

| Payload | Frame bytes | Baseline wire seen | Armed frames sent | Armed wire seen | `TDH0` advance at stall | ICMP baseline | ICMP armed |
|---|---|---|---|---|---|---|---|
| 64 | 106 | 82 of 82 | 479 | 0 | 87 | 11/11 | 0/23 |
| 512 | 554 | 81 of 81 | 484 | 0 | 22 | 11/11 | 0/23 |
| 1400 | 1442 | 82 of 82 | 384 | 0 | 9 | 10/10 | 0/24 |

Both observers matched every baseline frame. The first armed frame was already missing in every run, while `TDH` advanced and the gauge stayed 0.

Against the predictions:

| Hypothesis | Size scaling | Wire | Result |
|---|---|---|---|
| Closed or misprogrammed gates | Matches | Matches | Consistent |
| Packet buffer not released | Matches | No frames seen | Refuted as the sole cause: nothing left the adapter to release |
| Descriptor or ring limit | Stall changes 87, 22, 9 | No frames seen | Refuted |
| Missing completion or miniport limit | Size-dependent | No frames seen | Refuted as the primary cause |

Fit of `TDH` advance to `N * (frame + k) = C` from the two smaller sizes: k about 46 bytes, C about 13.2 KB, which predicts 8.9 descriptors at 1400 bytes against 9 observed. This treats one descriptor as one test frame; a few ICMP and background frames are mixed in, and per-frame buffer behaviour is unmeasured. It does not match the 20 KB TXPB0 field, and the reason is unknown.

Frames counted by `osOut`, and sends completed back to the filter, include frames that never reached the wire.

Limits: same-host capture is supplementary. Observer liveness during the armed window is not independently shown; it stands on ICMP agreeing. Which programming omission keeps egress closed is not established; `STQT`/`ENDQT` of 0, `BaseT` of 0 and shadow cycle 0 are candidates only.

Correction: `TDT - TDH` is not a fixed 255. Four runs reached 255, the 64 byte probe reached 261. At 1400 bytes sends began failing (99 errors) after about 24 s, once the stack backed up.

### Capture topology, verified 2026-11-05

The I226 is the device under test, the I210 the passive capture adapter, and the I219 stays out of the experiment for management. All three connect to one Milan-certified switch (operator statement).

Verified on this host: the I210 individually matched 245 of 245 tagged I226 broadcast frames across the three probe baselines. Neither adapter has a VLAN ID set, there is no team or bridge, and the I210 has resolved the I226 MAC by ARP. The switch port configuration was not read; broadcast visibility is proven only by the baseline observation, which the probe repeats before every `SETUP_TAS`.

Assumptions: switched unicast is not expected on the I210, so only broadcast is correlated, by source MAC, run nonce and sequence number. No switch setting is changed. The capture opens non-promiscuous. The capture is same-host supplementary evidence. The I219 reports `PromiscuousMode=True`; the source is unknown and it does not affect this experiment.

The three probe runs captured on both the I219 and the I210, and each observer matched only baseline frames. Runs from this point capture on the I210 alone and count all captured frames and all I226-sourced frames per window, which closes the armed-window liveness gap.

### Exploratory investigation versus regression evidence

Decision 2026-11-05: generic Debug-only `WRITE_REGISTER` is available for investigating register behaviour before implementation. It is not used in automated HIL regression tests, test-only branches or registered Unsafe entries. Established behaviour is implemented through dedicated, purpose-specific IOCTLs and verified by hardware TDD.

`tests/diagnostic/tas_gate_investigation.c` is that investigation. It is classified `Diagnostic`, has no runner entry and no testcase verdicts, and is started by the operator. Its output is `LOCAL_DIAGNOSTIC` evidence and never counts as Debug or Release HIL regression evidence. `test_tas_isolated_arm` contains no register writes.

Candidates, applied in order until tagged frames reach the I210: A per-queue windows (`STQT=0`, `ENDQT=cycle`); B shadow cycle, cycle and base time at the next boundary. `FUTSCDDIS` and a future base are not used, because upstream warns of a TX hang on I226 around them. Each step stays below the observed fill level, so a stalled ring cannot hide an opening window.

### Gate investigation result, 2026-11-05 (LOCAL_DIAGNOSTIC)

Run of `tas_gate_investigation` at 60104b4, Debug driver FB64A096, I226 0x125B, I210 capture. One run. Not HIL regression evidence.

| Step | Sent | Wire seen | Seen since arming | ICMP total |
|---|---|---|---|---|
| Baseline | 49 | 48 | - | 4/4 |
| `SETUP_TAS` only | 24 | 0 | 0 | 4/6 |
| A: `STQT=0`, `ENDQT=cycle` | 17 | 0 | 0 | 4/7 |
| B: shadow cycle, cycle, base time, on top of A | 17 | 17 | 58 of 58 | 7/10 |

After B, every frame held since arming reached the I210, so those frames were held and not dropped. The capture stayed alive throughout. TDH0 tracked TDT0 in every step, at a frame count below the fill level.

Which register mattered is not established: B was written on top of A, in one step. `QBVCYCLET_S` and `BaseT` were written together, so the run cannot separate them, and it cannot show whether A is needed. Windows covered the whole cycle, so nothing here shows the adapter obeys a real gate pattern. The NIC cycle returned 13 compared registers to baseline; that run compared no others, and did not measure PHC or TX.

`--apply <w,s,b>` writes chosen groups from a fresh NIC cycle, which removes carry-over between candidates. Run `wsb`, `sb`, `wb` and `ws` to separate them.

### Register groups separated, 2026-11-05 (LOCAL_DIAGNOSTIC)

`tas_gate_investigation --apply`, uncommitted tool on top of 60104b4, Debug driver FB64A096, I226 0x125B, I210 capture. One run per case, each from a fresh NIC cycle. `ws` has not run.

| Case | Windows | Shadow | BaseT | Step frames on wire | Seen since arming |
|---|---|---|---|---|---|
| `wsb` | yes | yes | yes | 17 of 17 | 41 of 41 |
| `wb` | yes | no | yes | 17 of 17 | 41 of 41 |
| `sb` | no | yes | yes | 0 of 17 | 2 of 42 |
| Candidate A of the earlier run | yes | no | no | 0 of 17 | 0 |

Observed: with windows and `BaseT`, egress opened without the shadow cycle. Without windows it stayed closed, apart from 2 held frames that left. Windows alone stayed closed. `BASET_L` read back moving in exact multiples of the 1 ms cycle in all three cases, including `sb`. `BASET_H` read 0 until `BASET_L` was written.

Inference: `BaseT` starts the schedule and the windows give it open time. Direct evidence for `BaseT` needs `ws`. Windows spanned the whole cycle, so obedience to a real gate pattern remains unproven. `baseT - PHC` after the writes was -200 to -365 us, so the base was at or just past the PHC by then; whether it was still ahead when written is unknown.

Restoration: all 42 registers equal in `wsb` and `sb`, 49 of 49 frames on the wire after the cycle in all three cases, PHC not continuous in all three. In `wb` the only difference was `TSYNCRXCTL` 0x18 to 0x19: bit 0 is `RXTT`, a read-only status bit in the SSOT, and `EN` and `TYPE` did not change. That exit 1 is a comparison artefact, not a configuration change.

Fresh NIC cycles raise background traffic: `descFetched` in the baseline step rose from 53 to 353 and 370. The fill assumption held only loosely, so `sb` reached its stall during the run. A full buffer did not stop `wb` flushing.

### `ws` result and documented requirements, 2026-11-05 (LOCAL_DIAGNOSTIC)

`ws`: windows and shadow cycle written, `BaseT` not. `BASET` stayed 0. 0 of 16 step frames and 0 of 40 since arming reached the I210, with the capture alive. So with windows and a shadow cycle, egress stayed closed until `BaseT` was written, on one run. Combined with `wb` and `sb`, both windows and `BaseT` were needed in every case tried. One run per case.

From the software manual (paraphrased, the manual is confidential):
- The transmit mode is set at initialization and cannot change during dynamic operation. Upstream resets the adapter on any Legacy to TSN change and cites the same section. Today's `SETUP_TAS` and any in-place disarm both go against this.
- TSN mode uses one packet buffer per enabled queue, which may relate to the 13 KB estimate. Not measured.
- Qbv is enabled by both cycle registers being non-zero, yet egress opened with `QBVCYCLET_S=0`. Behaviour and documentation differ here; shadow programming stays required.
- After `BaseT` low is written, the device advances `BaseT` by the cycle at each boundary, which matches the observed readbacks.
- Explicit setting: shadow, cycle, read `SYSTIM`, write `BaseT` high then low for a time in the future, with `Copy_H` and `Auto_Set` clear. Autonomous setting exists for whole-second aligned cycles. The upstream comment restricts a future base on I226 to `FutScdDis`; the two sources differ.
- A gap is required between `EndQT` of active queues and the end of the cycle.

### Gate pattern and disarm experiments

`--pattern <startMs> <endMs>`: capture times are host-side, so a 1 ms cycle cannot be resolved. A 1 s cycle with one window can. Two runs with the window moved by 400 ms are the control: arrivals must move with it, which needs no absolute alignment. Frames sent while closed must arrive near the window start and frames sent while open with little delay. Baseline frames calibrate capture latency and its spread. Sub-millisecond gate accuracy is out of scope and stays unverified.

`--disarm`: writes the original values back in upstream's order, without a NIC cycle, then measures TX past the fill level, PHC, registers and driver state. A good result shows observed behaviour against the documented guideline, not a supported sequence.

### Gate pattern and disarm results, 2026-11-05 (LOCAL_DIAGNOSTIC)

Debug driver FB64A096, I226 0x125B, I210 capture, one run per case, uncommitted tool. Capture is same-host supplementary evidence.

| Measure | Window 200-400 ms | Window 600-800 ms |
|---|---|---|
| Frames sent with the schedule running | 193 | 193 |
| Arrived inside the window, within uncertainty | 193 | 193 |
| Arrived outside | 0 | 0 |
| Lost, reordered | 0, 0 | 0, 0 |
| Arrival phase, ms | 200.05 to 401.95 | 600.06 to 795.34 |
| Sent while open: median delay | 0.35 ms | 0.38 ms |
| Sent while closed: frames, median delay | 151, 386.8 ms | 154, 399.0 ms |
| Sent while closed: arrival after window start, median | 0.31 ms | 0.49 ms |
| Uncertainty | 7.1 ms | 10.2 ms |

Moving the window moved the arrivals. Frames sent while closed were held and released at the window start, and frames sent while open passed with little delay. So the adapter followed a periodic schedule at about 10 ms resolution, on a 1 s cycle with one window shared by all queues.

Not verified: sub-millisecond gate accuracy, a 1 ms cycle with a real pattern, per-queue independence, the gate end edge beyond the uncertainty, `Strict_Cycle` and `Strict_End`, and any window taken from the `SETUP_TAS` gate list, which the driver ignores.

Disarm without a NIC cycle: `TQAVCTRL`, `STQT`, `ENDQT`, `QBVCYCLET_S` and `QBVCYCLET` returned to baseline. Writes to `BASET_H` and `BASET_L` were ignored and `BaseT` kept changing. 97 of 97 frames reached the I210 over 6 s, past the fill level, with gauge 0 and occupancy 0. PHC rate stayed 1.02930 before and after, so no reset. `DISARM_TAS` returned not supported and `tasArmed` stayed 1. Legacy TX behaviour returned, on one run, but `BaseT` was not restored, buffer partitioning is unobservable, and re-arming was not tried. The manual does not allow this transition during dynamic operation.

PHC rate: 1.029311 measured, 1.02931 in every run including across resets. The manual's mean hard-coded step is 3.2 ns at 312.5 MHz and `TIMINCA` adds to it, so 0x18000000 predicts 1.029297. Schedules therefore run 2.93 % fast in real time. Separate defect.

The relationship between these results and the #328 unload hang is not established.

### Re-arm after in-place disarm, 2026-11-05 (LOCAL_DIAGNOSTIC)

`--rearm 3`, Debug driver FB64A096, one run, three cycles, no NIC cycle between them.

Each cycle: disarm returned registers to baseline except `BaseT`, whose writes had no effect. Legacy TX 97 of 97 on the wire, gauge 0, occupancy 0. `SETUP_TAS` succeeded and `tasArmed` stayed 1 throughout. After the schedule, all frames reached the wire.

Unlike the first arm, frames were not held after re-arm. With `ENDQT=0`, `QBVCYCLET_S=0` and `BaseT` live, 97 of 97 reached the wire in all three cycles, where the first arm held 0 of 97. So re-arm is not equivalent to a first arm. `BaseT` stayed live from the first schedule until the NIC cycle.

PHC showed no step across three cycles: drift over wall time stayed 2.930 % at every check.

Candidate model, inference only: `BaseT` unset holds TX; `BaseT` live with shadow 0 means no gating, so frames pass freely; both set applies the windows. It fits every case so far and matches the manual's condition of both cycle registers being non-zero. If it holds, `wb` opening meant gating was off, so the shadow is required for scheduling. Next: a pattern without the shadow, and a pattern after re-arm.

Tagged frame sequence 1 was missing from two baselines; unexplained.

Baseline sequence 1: missing in all 3 tool baselines after 18 minutes or more of idle, present in all 6 after under 10 minutes. After 9.5 minutes idle the first frame arrived 48 ms late instead of about 0.5 ms. Small sample, cause unknown, and one observer cannot say which side it is. These frames stay counted in the baseline. The tool now prints the first baseline frames of every run.

### Pattern without shadow, after re-arm, and with mode last, 2026-11-05 (LOCAL_DIAGNOSTIC)

Debug driver FB64A096, I226 0x125B, I210 capture, 1 s cycle, window 200-400 ms relative to `BaseT`, one run each. Phase uses the `BaseT` read from the register.

| Run | Frames | Lost | Arrivals by 100 ms bin from `BaseT` | Frames in window | Sent closed, median delay |
|---|---|---|---|---|---|
| No shadow | 193 | 0 | 9, 2, 165, 17, 0, 0, 0, 0, 0, 0 | 182 | 382.4 ms |
| After re-arm | 195 | 0 | 23, 19, 20, 22, 17, 19, 20, 19, 18, 18 | 43 | 0.4 ms |
| Mode last | 194 | 0 | 5, 12, 155, 19, 0, 0, 0, 0, 0, 3 | 184 | 378.2 ms |

No shadow: gating applied with `QBVCYCLET_S=0`. The earlier model is refuted: `wb` opening did not mean gating was off. Shadow programming stays required by the manual, but the measured effect is not established.

After re-arm: arrivals are flat across the cycle and match the send phases, delay 0.4 ms. Registers read `TQAVCTRL=9`, windows and both cycle registers as programmed. `BaseT` writes had no effect and the old grid continued. So egress works, periodic scheduling does not, and neither the registers nor the driver show it. An in-place `TQAVCTRL=0` disarm therefore failed as a reversible operation in this run.

Mode last: gating applied from a first arm without `SETUP_TAS`. `BaseT` written in Legacy was accepted and advanced before `TQAVCTRL` was written. Driver `tasArmed` stayed 0 with hardware in TSN mode. No frames were held before arming.

In no-shadow and mode-last, the frames outside the window all belong to the first cycle and left near a cycle boundary instead of the window start. The cause is unknown.

Sequence 1: the first frame after about 12 minutes idle was delayed 48.07 ms, matching 48.35 ms in the earlier run, and sequence 1 was present. All frames in these runs arrived.

### Mode toggle alone and soft disarm, 2026-11-05 (LOCAL_DIAGNOSTIC)

Debug driver FB64A096, I226 0x125B, I210 capture, 1 s cycle, one run each. `BaseT` low read 0 throughout, so phase is PHC modulo 1 s.

| Run, segment | Registers say | Frames | Lost | Arrivals by 100 ms bin | Sent closed, median delay |
|---|---|---|---|---|---|
| Toggle-only, before | 200-400 | 193 | 0 | 0, 0, 173, 20, 0, 0, 0, 0, 0, 0 | 386.9 ms |
| Toggle-only, after | 200-400 | 192 | 0 | 0, 0, 172, 20, 0, 0, 0, 0, 0, 0 | 393.6 ms |
| Soft disarm, armed | 200-400 | 193 | 0 | 5, 1, 169, 18, 0, 0, 0, 0, 0, 0 | 379.6 ms |
| Soft disarm, opened | 0-1000 | 192 | 0 | 0, 0, 173, 19, 0, 0, 0, 0, 0, 0 | 284.6 ms, max 777 ms |
| Soft disarm, moved | 600-800 | 192 | 0 | 0, 0, 174, 18, 0, 0, 0, 0, 0, 0 | not meaningful, all still at 200-400 |

Toggle-only: `TQAVCTRL` 9 to 0 to 9 alone kept the schedule, with the same phase, so the toggle by itself did not cause the ungated state seen after re-arm. Egress passed during the interval, 17 of 17.

Soft disarm: window writes read back correctly and had no effect on a running schedule for at least 29 s. Frames were held up to 777 ms with registers reading fully open. So opening windows does not disarm, and the proposed soft disarm failed. Register readback of `STQT`/`ENDQT` does not show the schedule in force.

Both predictions made before these runs were wrong: toggle-only was expected as the likely cause, and soft disarm as a working route.

First cycle: in the armed segment of the soft-disarm run, frames sent before the boundary were held until 2.4 ms after it, then three frames sent at 34, 97 and 160 ms passed with about 0.5 ms delay, outside the window. That fits the first-cycle frames seen in the no-shadow and mode-last runs. Inference only: gates may be open from the first boundary until the first window ends.

PHC: no step across the toggle or the window writes, drift 2.93 %. The NIC cycle reset it, exit 2 in both runs. Registers 42 of 42 equal. After the cycle, TX 49 of 49 and 48 of 49; `firstMissingSeq=651` is the second frame after the cycle, once.

Sequence 1: present after 4791 s idle, frame 0 delayed 57.5 ms. This is a counterexample to the earlier pattern of sequence 1 missing after long idle. The first-frame delay after idle was seen at 48.35, 48.07 and 57.50 ms.

Next: what makes a new window reach a running schedule: `--move-cycle`, `--move-base`, `--move-toggle`.

### Correction: the after-rearm run, 2026-11-05

The earlier reading of `--after-rearm` was wrong. The tool's first schedule in that run was the 1 ms cycle with windows open over the whole cycle, not the 1 s pattern, and it was never replaced.

From the log: `BaseT` low stepped in whole milliseconds and tracked the PHC, 22.272 s at PHC 22.2721 s and 37.274 s at PHC 37.2727 s, while `QBVCYCLET` read 1 s. So the cycle in force stayed 1 ms for at least 15 s. The flat arrivals came from that schedule, and the phase of 274 ms reported earlier was not a 1 s grid. These statements above are withdrawn: that gating was lost after re-arm, and that an in-place `TQAVCTRL=0` disarm failed as a reversible operation. The two disarm hypotheses that followed are superseded.

Candidate model, inference from one run per case: the first schedule started after a NIC cycle stays in force until the next NIC cycle. Writes to windows, shadow, cycle and `BaseT` read back but do not change it. `TQAVCTRL=0` passes frames freely, and `TQAVCTRL=9` resumes the same schedule. It fits toggle-only, soft disarm, after-rearm and `--rearm`. Not tested: moving a running schedule with the full upstream sequence, or with windows written at `TQAVCTRL=0`.

### Moving a running schedule, 2026-11-05 (LOCAL_DIAGNOSTIC)

Debug driver FB64A096, I226 0x125B, I210 capture, 1 s cycle, one run each. Each run started a 200-400 ms schedule, then tried to move it to 600-800 ms. All register writes read back as intended, except `BaseT`.

| Run | Before the move | After the move | In 600-800 ms |
|---|---|---|---|
| `--move-cycle`: windows, shadow, cycle | 191 frames, 9, 22, 141, 19, 0... | 193 frames, 0, 0, 174, 19, 0... | 0 |
| `--move-base`: the same plus a past `BaseT` | 171 frames, 4, 12, 138, 17, 0... | 192 frames, 0, 0, 173, 19, 0... | 0 |
| `--move-toggle`: windows at `TQAVCTRL=0` | 168 frames, 0, 0, 149, 18, 1, 0... | 193 frames, 0, 0, 173, 19, 1, 0... | 0 |

No route moved the schedule, 0 lost in all segments. The 22 in the `--move-cycle` second bin are an edge effect: the median release was 0.10 ms after window start, inside the 1.09 ms uncertainty.

`--move-base` wrote the latest past boundary. `BaseT` read back unchanged. Upstream writes the next boundary, so that route is not yet covered, nor upstream's TXQCTL writes.

Host stalls: send gaps up to 405 ms in `--move-base` and 305 ms in `--move-toggle`, so accepted frames fell to 171 and 168, and uncertainty rose to 24 ms and 5.83 ms. One frame in each `--move-toggle` segment fell in the 400 ms bin, one of them at 406.1 ms, outside the uncertainty. The gate end edge stays unverified.

Exit 1 in `--move-toggle`: after the NIC cycle `TCTL` read 0xA40400FA and `TSYNCRXCTL` 0x58, baseline 0xA403F0FA and 0x19. Neither value appears in any other log. `TCTL` differs only in `COLD`, 0x40 against 0x3F. 0x40 is the documented default and upstream programs 63 during link setup. Link-up took 8 s in this cycle, against 3 to 4 s in the other 15 today, and the tool reads about 5 s after the device returns, so that read fell at about link-up. Candidate, not proven: the read came before link setup finished. `TSYNCRXCTL` bit 6 is reserved in the manual and stays unexplained. The cause of the 8 s is unknown, and a link to `--move-toggle` is neither shown nor excluded. Next run reads again after the post-cycle TX.

First-cycle frames outside the window appeared in 5 of 5 runs where the arm landed after the window end (write phases 551 to 847 ms), and in none of 4 where it landed before or inside the window (1, 133, 194, 741 ms). Inference only.

After the NIC cycle, the second frame was missing in 3 of the last 4 runs and in none of the 12 before. It was always the second frame, and `tx_restored` accepts it. The tool now prints those frames.

Sequence 1: present after 448 s idle, frame 0 delayed 51.94 ms. Delays after idle so far: 48.35, 48.07, 57.50, 51.94 ms.

PHC: no step in any run, drift 2.930 %. The NIC cycle reset it.

### Upstream review of future `BaseT` and `FutScdDis`, 2026-11-05 (SOURCE, no hardware evidence)

Local copy of upstream `igc`, fetched 2026-11-04, and the netdev patches of 2023-01-03.

- Hang warning: `igc_main.c:6366-6373`, in `validate_schedule`. A future `BaseT` holds all packets until then, "causing a lot of TX Hangs", so future schedules are rejected, with the note that this no longer applies to i226. The check still applies to i225.
- `FutScdDis`: `igc_tsn.c:643-649`. Set only for a future base on i226 when `BASET_H` and `BASET_L` read zero, because "Tx won't hang if a GCL is already running".
- `BASET_L` zero write: `igc_tsn.c:669-676`. "Future base time is only supported when FutScdDis bit is enabled and only active for re-configuration", so zero is written first to create that scenario.
- Patch 1, Muhammad Husaini Zulkifli, "igc: remove I226 Qbv BaseTime restriction": bit 7 lets i226 schedule a future base without "the Tx hang timeout issue".
- Patch 2, Tan Tee Min, "igc: enable Qbv configuration for 2nd GCL": lets i226 configure a second GCL without resetting the adapter. The cover letter says this is "for new i226 steps". Which steps is not stated. This adapter reports PCI REV_04.
- `igc_main.c:6514-6516` counts a change with a past base on a running schedule as `qbv_config_change_errors`, so `--move-base` was outside upstream's supported path.
- No upstream text found states what happens to a running schedule when `BaseT` is written.
- The manual revision held here does not describe this bit. The section upstream cites covers the first packet of a cycle, so the upstream citation cannot be checked against it.

Interpretation of "TX hang", not stated by upstream: Linux has a 5 s watchdog (`igc_main.c:7196`) and a software unit-hang check (`igc_main.c:3249-3261`), so the warning may mean software timeouts on held packets and not a hardware fault. Unproven.

So the earlier statement that a NIC cycle is likely needed for a new schedule was premature. Three runs rule out neither a future `BaseT` on a running schedule nor `FutScdDis` with the zero write.

`--future-base` review: it matches upstream's condition for the first schedule: `BaseT` zero, future base, no `FutScdDis`. It differs in that the hold is 1 to 2 s, under the 5 s watchdog, frames are already held from `SETUP_TAS`, upstream resets the adapter on the mode change and programs before traffic, and the miniport's hang logic is unknown. It can show when a first schedule starts. It cannot reproduce a hang. A comparison needs both `--future-base` and `--future-base-futscd`.

Tool additions: `schedule` lines with all schedule registers and the PHC at each pattern start and end; a post-cycle timeline of link state, `COLD` and `TSYNCRXCTL` on its own handle, with host times and marks; `--move-futscd` and `--future-base-futscd`. Exit codes and thresholds are unchanged.

### Replacing a running schedule with a future `BaseT`, 2026-11-05 (LOCAL_DIAGNOSTIC)

Debug driver FB64A096, I226 0x125B REV_04, I210 capture, 1 s cycle, one run. Windows, shadow, cycle and `BaseT` = 1885.000000000 s written at PHC 1883.33 s, no `FutScdDis`, no `TXQCTL` flags, no NIC cycle.

| PHC second | In 200-400 ms | In 600-800 ms | Elsewhere |
|---|---|---|---|
| 1883 | 2 | 0 | 0 |
| 1884 | 15 | 0 | 0 |
| 1885 | 0 | 22 | 0 |
| 1886 to 1895 | 0 | 154 | 0 |

The new schedule took effect. Nothing arrived in 600-800 ms at 1884 and nothing in 200-400 ms at 1885, so activation lies between 1884.8 and 1885.2 s, which contains the intended boundary. Host timing cannot bracket it closer in this schedule. 193 of 193 frames arrived, 0 reordered. The longest hold was 1151.9 ms, across the transition. The phase stayed on the old grid.

Four separate facts:
- Programmed: all writes returned ok.
- Retained: windows, shadow and cycle read back as written from the moment of the write, about 1.7 s before they took effect.
- `BaseT` readback: `BASET_H` read back the old 0x75C, not 0x75D, and kept advancing to 0x76B. The pending value was never visible.
- Active: shown only by frame arrivals.

So register reads cannot separate a pending schedule from an active one, or from a write that will never apply.

The fixed-schedule model is refuted. The only difference from `--move-base` is `BaseT` in the future, not the past, which matches upstream. Stepping is not an obstacle for this route on REV_04. `FutScdDis` was not needed. Not isolated: whether the shadow and cycle writes are required, and whether a cycle change applies.

`--move-toggle` repeat: 0 of 194 frames in 600-800 ms, 175 and 19 in the 200 and 300 bins. Same result in 2 of 2 runs. Exit 2 this time.

Restoration in both runs: 42 of 42 registers equal at the first read and after the post-cycle TX, TX 49 of 49, PHC reset by the cycle as before, exit 2. PHC showed no step across the replacement, drift 2.930 %. Descriptors: fetched 206 and occupancy 0 in the moved segment; `SETUP_TAS` occupancy 16 and gauge 11 in the toggle run, the first nonzero values in that step in the runs reviewed.

Post-cycle timeline, sampled every 50 ms:

| Run | Before link | Link-up after enable | At link-up |
|---|---|---|---|
| `--move-future` | `COLD` 0x040, `TSYNCRXCTL` 0x18 | 3.15 s | `COLD` 0x03F, 0x19 |
| `--move-toggle` repeat | `COLD` 0x040, `TSYNCRXCTL` 0x18 | 3.45 s | `COLD` 0x03F, 0x19 |

`COLD` changed in the same sample as link-up in 2 of 2 runs, which supports the earlier exit 1 being a read before link setup. It was not reproduced, as link-up took 3 s in both, so that exit 1 stands as recorded. `TSYNCRXCTL` bit 6 was not seen in any sample and stays unexplained and unclassified. Bit 0 also changed during TX, 0x18 then 0x19 within 62 ms, once.

First-frame delay and loss: after 1787 s idle, frame 0 was 137.6 ms late and seq 1 and 2 were lost; both were sent before frame 0 arrived. Six earlier delays of 41.5 to 57.5 ms were below the 62 ms send period and lost nothing. Candidate only, from one case: frames sent during that delay are lost. Two post-cycle delays, 41.5 and 52.5 ms, came with link up for only seconds, so long idle is not required. One observer cannot say where it happens.

First-cycle stray frames: 6 of 6 arms after the window end, 0 of 5 before or inside it. Inference.

### Repeat, whole-cycle open, and cycle change by future `BaseT`, 2026-11-05 (LOCAL_DIAGNOSTIC)

Debug driver FB64A096, I226 0x125B REV_04, I210 capture, no `FutScdDis`, no NIC cycle during the schedule changes, `TQAVCTRL` 9 throughout.

| Run | Written at PHC | `BaseT` | New schedule | Last old arrivals | From the boundary | Lost |
|---|---|---|---|---|---|---|
| `--move-future` repeat | 437.41 s | 439 s | 600-800 ms | 438: 200-400 only | 439 on: 600-800 only | 0 of 193 |
| `--move-open-future`, open | 33.05 s | 35 s | 0-1000 ms | 34: 200-400 only | 35 on: flat, median delay 0.57 ms | 0 of 194 |
| `--move-open-future`, re-arm | 48.10 s | 50 s | 600-800 ms | 49: flat | 50 on: 600-800 only | 0 of 193 |
| `--move-cycle-future` | 33.07 s | 34 s | 500 ms, 100-300 ms | 33: 200-400 only | 34 on: 100-300 and 600-800 of each second | 0 of 193 |

With the first `--move-future`, 5 of 5 future-`BaseT` replacements took effect in 4 runs, each at the intended boundary. Routes without a future `BaseT` applied in 0 of 6 attempts: windows alone (2), windows with shadow and cycle (1), past `BaseT` (1), windows at `TQAVCTRL=0` (2).

Activation time: frames held before the whole-cycle open were released 0.23 ms after 35.000 s, with a conservative mapping uncertainty of 14.24 ms. Released at new window starts: +0.39, +0.40, +0.36 ms at 600 ms edges, and +1.75 ms at the 100 ms edge.

`--move-cycle-future`: `BASET_L` ended at 0x1DCD6500 and `BASET_H` at 0x30, so `BaseT` advanced in 500 ms steps. The value written, 34.000 s, equalled the running `BaseT` register at that moment, and it still applied.

Disarm candidate: whole-cycle windows by future `BaseT` passed frames with no gating, and a later replacement re-armed, in one run. This is functional only: `TQAVCTRL` stays 9 and schedule registers stay non-baseline until a NIC cycle. It takes effect up to two cycles later. `ENDQT` equal to the cycle breaks the manual's end gap; nothing visible in about 10 cycles at low rate, and a frame crossing the cycle end was not tested. Driver `tasArmed` was not checked in these runs.

Exit 1 in `--move-cycle-future`: the first read came 5.99 s after enable and link-up at 7.35 s, so `TCTL` read 0xA40400FA and `TSYNCRXCTL` 0x58 before link. Both returned at link-up without any tool write, and all 42 registers matched after the post-cycle TX. So this exit 1 came from the tool reading before link-up, not from TAS residue. The verdict stands as recorded. `test_tas_isolated_arm` is not exposed: it waits for link, then settles (`test_tas_isolated_arm.c:555-562`).

`COLD` was 0x040 before link and 0x03F at link-up in 5 of 5 timeline runs.

`TSYNCRXCTL` bit 6: 0x58 from the first sample, 60 ms after enable, in the one timeline run with slow link-up, 0x18 in the 4 with normal link-up. It cleared at link-up. The earlier exit 1 also read 0x58 in a slow link-up. It is correlated with slow link-up and remains undocumented and unclassified. Link-up took 7 s in 2 of 36 cycles today and 3 to 4 s in the others. Both slow cycles were the third case of a batch, but three other third cases took 3 s, so that is not an explanation.

First-frame delay against loss:

| Frame 0 delay | Frames sent before it arrived | Lost | Where |
|---|---|---|---|
| 197.1 ms | 3 | 3 (seq 1-3) | baseline, 380 s idle |
| 137.6 ms | 2 | 2 (seq 1-2) | baseline, 1787 s idle |
| 90.7 ms | 1 | 1 (seq 461) | 0.69 s after link-up |
| 41.5 to 57.5 ms, 10 cases | 0 | 0 | various |

Every frame sent before frame 0 arrived was lost, and none after it. Delays of 43.6 and 44.4 ms appeared with no idle. One observer cannot say where. EEE is enabled on the I226 and advertised on the I210; no evidence links it.

Restoration: exit 2, 2, 1. PHC no step across any replacement, drift 2.930 %, then reset by the NIC cycle. Post-cycle TX 49 of 49, 49 of 49, 47 of 48.

### Windows-only replacement and first schedule with a future `BaseT`, 2026-11-05 (LOCAL_DIAGNOSTIC)

Debug driver FB64A096, I226 0x125B REV_04, I210 capture, 1 s cycle, one run each.

`--move-windows-future`: windows and `BaseT` = 408 s written at PHC 406.36 s, no shadow or cycle write. Arrivals at 407 were only 200-400 ms and from 408 on only 600-800 ms, 0 of 193 lost. So for an unchanged cycle, windows and a future `BaseT` are enough. That makes 6 of 6 future-`BaseT` replacements.

| First schedule | `TQAVCTRL` | Frames already held from `SETUP_TAS` | Frames before `BaseT` | From `BaseT` | Lost |
|---|---|---|---|---|---|
| `--future-base`, `BaseT` 22 s, written at 20.01 s | 0x09 | Held until 22.200 s, up to 4.76 s | Held | Window 200-400 ms | 0 of 218 |
| `--future-base-futscd`, `BaseT` 20 s, written at 18.08 s | 0x89 | Released at 18.081 to 18.083 s | Passed with no gating | Window 200-400 ms | 0 of 218 |

Without `FutScdDis`, 59 frames left in one release at the first window after `BaseT`. With it, the 25 frames held since `SETUP_TAS` left at about the time of the programming, and traffic flowed until `BaseT`. This matches upstream's description. The moment of release cannot be tied to one write: `FutScdDis` went in before the `BaseT` writes, and the mapping uncertainty is about 6 ms. The hold stayed under 5 s, so this says nothing about a hang.

`BaseT` visibility differs: on a first schedule `BASET_H` read the future value after the write (0x16, 0x14). On a running schedule it did not.

Correction on `TSYNCRXCTL` bit 6: it does not track slow link-up alone. Before link it read 0x58 in 3 of 8 timeline runs: the two slow link-ups and one normal, `--future-base-futscd`, at 3.46 s. All slow link-ups so far showed it. It stays undocumented and unclassified. Slow link-ups are now 3 of 39 cycles today. One was the first case of a batch, so the third-in-batch observation does not hold.

Exit 1 in `--move-windows-future`: first read 6.03 s after enable, link-up 7.15 s, same `TCTL` and `TSYNCRXCTL` values, and all 42 registers matched after the post-cycle TX. The verdict stands as recorded.

Frame loss: frame 0 206.1 ms late with seq 1 to 3 lost, all sent before it arrived; seq 4 was sent after and arrived. That makes 4 of 4 cases with a delay beyond the send period. Delays of 58.9, 44.4 and 46.3 ms lost nothing.

Restoration: exit 1, 2, 2. Post-cycle TX 49 of 49 in all three. All 42 registers matched after the post-cycle TX. `TQAVCTRL` 0x89 was cleared by the NIC cycle. PHC no step across the replacement, drift 2.930 %.
