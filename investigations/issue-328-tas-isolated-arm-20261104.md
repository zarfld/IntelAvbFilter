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
