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
