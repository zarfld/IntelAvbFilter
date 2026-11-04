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

Link and traffic returning are never sufficient. A demonstrated failure outranks missing evidence. Even `PASS` covers observed state only: internal scheduler, FIFO and retry-buffer state are not visible.

`FilterAttachCount` is not used as proof of a cycle: it lives in the adapter context and is destroyed at detach. A lower `IoctlCount` shows a new context.

A PHC reset by the cycle would be a real unrestored change and is reported as `CLEANUP_FAILED`. The test does not reseed the PHC, because that would add a mutation.

## Specific risk: prolonged I226 hang

Earlier adapter disables took from 5 s to 18 minutes to release while sends were stuck [H]. Disable is bounded at 120 s and enable at 60 s, on a worker thread, so a stall ends in `CLEANUP_FAILED` with evidence instead of a killed test. The PnP operation may continue after that. VNC runs over the I219, which is not touched. A reboot is an operator decision and is never automatic. This risk applies to this experiment only.

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
