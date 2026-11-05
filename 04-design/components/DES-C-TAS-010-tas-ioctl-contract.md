---
specType: component-design
standard: IEEE 1016-2009
component: TAS lifecycle IOCTL contract (proposed)
phase: 04-design
version: 0.3.0
author: Claude Code, for Q
date: "2026-11-05"
status: DRAFT, derived artifact. v0.2 accepted as review baseline only. Not approved, not implemented
review: 04-design/reviews/DES-REVIEW-002-tas-ioctl-issue-ssot.md
traceability:
  architecture:
    - "#142 (ARC-C-IOCTL-005: IOCTL Dispatcher)"
    - "#100 (ARC-C-I225-008: Intel i225/i226 Device Implementation)"
  decisions:
    - "#91 (ADR-PERF-002), conflicts: see section 2"
    - "#92 (ADR-ARCH-003: Strategy Pattern)"
    - "#122 (ADR-SEC-001: Debug vs Release boundaries)"
    - "#133 (ADR-TSN-001: TSN Feature Prioritization)"
  requirements:
    - "#50 (REQ-F-TAS-001: Time-Aware Shaper Configuration), open"
    - "#9 (REQ-F-TAS-001: Time-Aware Scheduler, IOCTL 26), closed"
    - "#64 (REQ-F-IOCTL-VERSIONING-001)"
    - "#45, #52 (register and device-op locking)"
    - "#113 (QA-SC-PERF-002)"
  proposed_not_filed:
    - "REQ-F-TAS-ACTIVATE-001 and the others in DES-REVIEW-002"
    - "DES-C-TAS-010 design issue"
    - "ADR-TSN-002, ADR-TSN-003"
---

# DES-C-TAS-010: TAS lifecycle IOCTL contract

**Design only.** This document changes no driver code, test, runner entry, existing IOCTL or governance file.

**GitHub Issues are authoritative.** This file is derived from them. It defines no requirement and takes no architecture decision. Everything here that would bind implementation is a proposal until the issues in `04-design/reviews/DES-REVIEW-002-tas-ioctl-issue-ssot.md` are filed and reviewed. `TAS-DR-nn` are design rules, not requirements. IOCTL codes are candidates, not reservations.

Implementation stays blocked until the governing requirements and ADRs are reviewed.

| Tag | Meaning |
|---|---|
| **HW** | Observed on hardware, with run count |
| **DOC** | Intel I225/I226 software manual, paraphrased |
| **LINUX** | Linux `igc` behaviour. A reference, not a requirement |
| **INFERENCE** | Reasoned from evidence, not directly measured |
| **UNVERIFIED** | No evidence yet |

## 1. Evidence and its limits

All **HW** evidence comes from one setup. Every statement is limited to it:

| Limit | Value |
|---|---|
| Hardware | One I226-LM, `0x125B`, `REV_04` |
| Driver | Debug, `FB64A0966D836CFB…`, Debug-only register interfaces |
| Evidence class | `LOCAL_DIAGNOSTIC`, not automated HIL |
| Observer | I210 same-host software capture. Supplementary, millisecond resolution |
| Link and load | 1 Gbit/s, low frame rate, one active TX ring (ring 0) |
| Windows | Identical on all four queues in every run |
| Buffers | **Default `TXPBSIZE` (`0x04000014`) and `DTXMXPKTSZ` (`0x98`) in every run**, not the TSN values the manual recommends (EG-03) |
| PHC | Runs 2.93 % fast, so all times are PHC-domain and not wall-clock |
| First schedule | Always followed the existing `SETUP_TAS`, except one mode-last run |

Logs are in `C:\Users\User\cqv\i328\evidence\tasarm-20261105`, with full hashes in `SHA256SUMS.txt`. The record is `investigations/issue-328-tas-isolated-arm-20261104.md`.

| ID | Observation | Runs | Evidence |
|---|---|---|---|
| E1 | Windows plus a future `BaseT` replace a running schedule at the intended boundary, no NIC cycle, no `FutScdDis` | 6 of 6 dedicated; 16 of 16 counting all window changes | `90669b29`, `05664aa3`, `6aff008e`, `98c94f7e`, `664d44e3`, `d9ad1e0d`, `1e02a965`, `2a2f4feb` |
| E2 | Same-cycle replacement needs no shadow or cycle write | 1 run | `664d44e3` |
| E3 | A changed cycle applied with shadow, cycle and future `BaseT`; `BaseT` then stepped at the new cycle | 1 run | `98c94f7e` |
| E4 | Routes without a future `BaseT` do not replace | 0 of 6 | `26c4f42e`, `94b0b522`, `d93dbddb`, `def48094`, `0de0d3b4` |
| E5 | Register reads cannot separate pending from active. On a running schedule `BASET_H` keeps showing the old value | Every run | as E1 |
| E6 | `FutScdDis` set: held frames release around programming and traffic flows until `BaseT`, then gating. Clear: frames held until the first window after `BaseT`, up to 4.76 s | 2 of 2, 1 of 1 | `dc8cb4b7`, `c84b08c1`, `d7024d5c` |
| E7 | `TXQCTL.Strict_End` takes effect by the next window; windows wait for `BaseT` | 3 of 3 runs, both directions | `2a2f4feb`, `d9ad1e0d`, `1e02a965` |
| E8 | `Strict_End` holds a frame that cannot fit and blocks the queue behind it; fitting frames pass | 2 of 2 valid runs | `d9ad1e0d`, `1e02a965` |
| E9 | `TQAVCTRL` 9→0→9 passes frames at 0 and resumes the same schedule | 1 run | `305a5ef3` |
| E10 | In-place disarm: frames pass, windows, shadow, cycle and mode can be written back, `BaseT` writes are ignored and it keeps advancing | 1 run, plus 3 cycles | `c4009a76`, `1f355629` |
| E11 | PHC shows no step across schedule writes, replacements or `TQAVCTRL` changes | Observed cases | as E1, E9, E10 |
| E12 | A NIC cycle resets the PHC. Driver `init_ptp` reseeds `SYSTIM` | Every cycle | Issue 8 draft |
| E13 | A NIC cycle clears schedule registers | 40 of 41 comparisons | Register logs |
| E14 | One cycle left the full schedule with `tasArmed=0`. TX held about 11 minutes until the PHC reached the surviving `BaseT`. `Disable-NetAdapter` then recovered it | 1 case, 1 recovery | `d9ad1e0d`, `949a8c0d`, `558451d5`, `c9f1d2f7` |
| E15 | Whole-cycle windows by future `BaseT` pass frames at low rate; a later replacement re-gates | 1 run | `6aff008e` |
| E16 | Periodic gating at 1 s and 125 ms | Repeated at 1 s, 1 run at 125 ms | `b9febbc0` |

**The #328 unload hang remains unexplained.** Nothing here shows a link to it.

**Strict_End evidence sits outside the manual's window rule.** **DOC** 8.12.12 sets a minimum window. The 400 ns run and 5 µs large-frame stages were below it. They remain valid observations of hardware behaviour but are not a supported configuration (TAS-DR-05).

## 2. Conflicts that bind this design

The full report is in `DES-REVIEW-002`. Four conflicts directly constrain the design:

| Issue | Says | Effect here |
|---|---|---|
| `#91` ADR-PERF-002, accepted | Conflict with the miniport is mitigated by restricting the filter to PTP registers `0x0B600-0x0B700` | **All TAS registers lie outside that range.** Filter-side TSN programming needs a superseding decision: proposed ADR-TSN-002 |
| `#50` | 256 gate entries, 1 ms margin, disable before reconfiguring, atomic spin-lock commit | Differs from evidence; amended through new REQ issues, not here |
| `#9` | Up to 64 entries, cycle 1 µs to 1 s | Differs from `#50` and **DOC** |
| `#45`, `#52` | All device ops inside spin locks | Section 9.4 |
| `#9`, `#206`, `#281` | Closed as verified on a test with no traffic or wire check | Not used as evidence of gating. See DES-REVIEW-002 |

**Local definitions must not be used.** `devices/intel_i226_impl.c` defines `I226_STQT(i)`, `I226_ENDQT(i)`, `I226_TXQCTL(i)` and `I226_TQAVCTRL_FUTSCDDIS` at values that differ from the SSOT (`0x3324`, `0x3334`, `0x3344`, bit 7). Implementation must use `intel-ethernet-regs/gen/i226_regs.h` only.

## 3. Why these operations

### 3.1 Existing TAS IOCTLs

| Code | Contract today | Version field | Behaviour on hardware | Clients in the repo | Can it carry the new contract? |
|---|---|---|---|---|---|
| 26 `SETUP_TAS` | `AVB_TAS_REQUEST` wraps `tsn_tas_config` from the `intel_avb` library | None | Writes `TQAVCTRL` and cycle only; ignores `base_time` and gate list; frames then hold; returns success | `test_ioctl_tas.c` (TC-TAS-001..010), `test_tas_isolated_arm.c`, `comprehensive_ioctl_test.c`, device tests, `intel_avb` library tests | **No.** No version, no operation identity. Fixing behaviour would change what those tests and TC-TASARM observe |
| 65 `GET_TAS_STATE` | Driver record only, by documented design | None | `armed` and last config | `tas_restore.h`, `tas_gate_investigation.c`, TC-TSNREG-001 | **No.** Documented as non-authoritative; fixed layout |
| 66 `DISARM_TAS` | No input | None | `STATUS_NOT_SUPPORTED` on I226, a recorded contract gap | `tas_restore.h` classifies this exact status | **No.** Has no input to select behaviour; `tas_restore.h` depends on the status |
| 70 `QUERY_HW_SNAPSHOT` | Versioned, family-independent, 280 bytes | Yes | Read-only; has `TQAVCTRL` only | `Run-Tests-CI.ps1`, TC-ABI-022 | **No.** Size is pinned and hard-coded in the runner (section 12) |

`#64` requires an API version in IOCTL structures. Codes 26, 65 and 66 predate that and have none.

### 3.2 Candidate operations

Candidate codes 71 to 74 are free today. **They are not finalized:** a code is reserved only when the ADR and REQ issues are accepted.

| Candidate | Why it is needed | Could it merge? |
|---|---|---|
| `TAS_QUERY` (74) | Read-only state in Debug and Release. Must be separable from mutation | **No.** A read-only operation should stay distinct from mutating ones |
| `TAS_ACTIVATE` (71) | Needs a clean adapter, sets TSN mode, takes `pre_activation` | With REPLACE, technically yes |
| `TAS_REPLACE` (72) | Needs a running schedule, never changes mode, requires a future grid-aligned target | With ACTIVATE, technically yes |
| `TAS_DISABLE_TO_LEGACY` (73) | Leaves TSN mode. Different struct, leaves residue | Depends on ADR-TSN-002. **If it is not accepted, this operation is dropped** |

| Alternative | For | Against | Assessment |
|---|---|---|---|
| Four codes | One code per operation is the repo pattern; each maps to one requirement and RED/GREEN test; a wrong-state call cannot silently take the other path | More dispatcher entries | **Recommended, for ADR review** |
| One `TAS_APPLY` with an `operation` field, plus `TAS_QUERY` | Fewer codes, one struct | Precedent is weak (only PFC aliases a code); per-operation access control is impossible | Viable. Not required by anything found |
| Change 26, 65, 66 | No new codes | Breaks existing clients and tests | **Rejected** |

`OPEN_ALL` is not a disable and has no code of its own. It is a `TAS_REPLACE` with a whole-cycle schedule.

### 3.3 Compatibility

| Case | Behaviour |
|---|---|
| Existing client, new driver | Codes 26, 65, 66, 70: same structures, sizes and behaviour. TC-ABI pins unchanged |
| New client, old driver | Unknown code fails with `STATUS_INVALID_DEVICE_REQUEST`. Verdict `SKIP_NOT_IMPLEMENTED` from the raw NTSTATUS, because the Win32 mapping is lossy |
| Discovery | `IOCTL_VERSION.Minor` and `AVB_IOCTL_ABI_VERSION` minor rise on addition, per `#64`. No major bump |
| 32-bit and 64-bit clients | Fixed-width fields, no pointers, 8-byte natural alignment. Layouts pinned by `TC-ABI` at implementation |
| `SETUP_TAS` while a schedule is live | Refused, section 3.6. ABI unchanged; behaviour differs only in a state that no existing client can reach |
| `GET_TAS_STATE` during a contract schedule | `armed` is derived, section 4.5 |
| `DISARM_TAS` during a contract schedule | Unchanged: `STATUS_NOT_SUPPORTED` on I226, writes nothing |
| Dispatcher | `src/device.c` allowlist and `test_ioctl_abi.c` code list gain entries at implementation |
| Access | `_NDIS_CONTROL_CODE` fixes `FILE_ANY_ACCESS`, while `#50` specifies `FILE_WRITE_ACCESS`. Mutating codes should require write access. The device ACL needs review first (EG-17) |

### 3.4 Structure versioning

The rule follows `AVB_HW_SNAPSHOT_REQUEST`, `AVB_OPEN_BY_LUID_REQUEST` and `#64`:

| Rule | Behaviour |
|---|---|
| Header | Every struct starts with `avb_u32 version; avb_u32 size;` |
| Wrong version | `STATUS_REVISION_MISMATCH`, as in the snapshot and identity IOCTLs. Driver writes nothing |
| Wrong size | `STATUS_INVALID_PARAMETER` for a size that does not match the version; `STATUS_BUFFER_TOO_SMALL` for a short buffer |
| Reserved fields, unknown flags | Must be 0. Otherwise refused, so a flag added later cannot be silently ignored by an old driver |
| New fields | New version constant and new size. Old versions stay served |
| `AVB_REQUEST_HEADER` | Not used: the newest versioned IOCTLs do not use it either |

### 3.5 Proposed structures

For review only; not added to `include/avb_ioctl.h`. Times are PHC nanoseconds. Register access is SSOT-derived at implementation.

```c
#define AVB_TAS_QUEUES 4u

typedef struct AVB_TAS_QUEUE_WINDOW {
    avb_u32 start_ns;         /* relative to BaseT */
    avb_u32 end_ns;           /* relative to BaseT, exclusive */
    avb_u32 max_frame_bytes;  /* largest frame this queue will carry, sizes the minimum window */
    avb_u32 flags;            /* AVB_TAS_Q_STRICT_END, AVB_TAS_Q_STRICT_CYCLE */
} AVB_TAS_QUEUE_WINDOW;                                     /* 16 bytes */

typedef struct AVB_TAS_SCHEDULE {
    avb_u64 base_time_ns;     /* absolute PHC time */
    avb_u32 cycle_ns;
    avb_u32 reserved;         /* must be 0 */
    AVB_TAS_QUEUE_WINDOW queue[AVB_TAS_QUEUES];
} AVB_TAS_SCHEDULE;                                         /* 80 bytes */

/* TAS_ACTIVATE and TAS_REPLACE */
typedef struct AVB_TAS_OP_REQUEST {
    avb_u32 version, size;
    avb_u64 net_luid;            /* in: must equal the bound adapter */
    avb_u64 client_op_id;        /* in: non-zero, unique per handle */
    avb_u64 expected_generation; /* in: 0 for ACTIVATE, else from TAS_QUERY */
    avb_u64 context_epoch;       /* in: from TAS_QUERY */
    avb_u32 flags;               /* in: AVB_TAS_F_OPEN_ALL, AVB_TAS_F_ACCEPT_UNVERIFIED */
    avb_u32 pre_activation;      /* in, ACTIVATE: required, no default */
    AVB_TAS_SCHEDULE schedule;   /* in */
    avb_u64 generation, driver_op_seq, phc_commit_ns;       /* out */
    avb_u32 result, reserved;    /* out, success only: see section 9.1 */
} AVB_TAS_OP_REQUEST;                                       /* 160 bytes */

typedef struct AVB_TAS_DISABLE_REQUEST {
    avb_u32 version, size;
    avb_u64 net_luid, client_op_id, expected_generation, context_epoch;   /* in */
    avb_u64 generation, driver_op_seq, phc_commit_ns;       /* out */
    avb_u32 result, restoration; /* out */
} AVB_TAS_DISABLE_REQUEST;                                  /* 72 bytes */

typedef struct AVB_TAS_REGS {
    avb_u32 tqavctrl, baset_l, baset_h, qbvcyclet, qbvcyclet_s;
    avb_u32 stqt[4], endqt[4], txqctl[4];
    avb_u32 tdh[4], tdt[4];
    avb_u32 txpbsize, dtxmxpktsz;
    avb_u32 read_failed_mask;    /* non-zero: the block is not trustworthy */
} AVB_TAS_REGS;                                             /* 112 bytes */

typedef struct AVB_TAS_LIMITS {
    avb_u32 queues, windows_per_queue;
    avb_u32 hw_cycle_min_ns, hw_cycle_max_ns;   /* hardware capability, not the tested set */
    avb_u64 min_margin_ns;
    avb_u32 tier_strict_end, tier_strict_cycle, tier_per_queue, tier_pass_ungated;
    avb_u32 verified_cycle_count;
    avb_u32 verified_cycle_ns[3];
} AVB_TAS_LIMITS;                                           /* 56 bytes */

typedef struct AVB_TAS_OP_RECORD {
    avb_u64 client_op_id, driver_op_seq, generation_after, phc_ns;
    avb_u32 operation, result, ntstatus, stage;
} AVB_TAS_OP_RECORD;                                        /* 48 bytes */

typedef struct AVB_TAS_QUERY_REQUEST {
    avb_u32 version, size;
    avb_u64 lookup_client_op_id;   /* in: 0 for none */
    avb_u64 net_luid;              /* out */
    avb_u16 vendor_id, device_id, revision_id, reserved0;
    avb_u32 capabilities, hw_mode, origin, gate_policy;
    avb_u32 current_status, next_status, inconsistency, restoration;
    avb_u32 flags, lookup_result, link_speed_mbps, legacy_conflicts;
    avb_u32 lifecycle, reserved1;  /* lifecycle: section 4.4 summary */
    avb_u64 context_epoch, generation;
    avb_u64 phc_before_ns, phc_after_ns;   /* bracket the register reads */
    AVB_TAS_LIMITS limits;
    AVB_TAS_SCHEDULE current, next;
    AVB_TAS_REGS regs;
    AVB_TAS_OP_RECORD lookup;
} AVB_TAS_QUERY_REQUEST;                                    /* 496 bytes */
```

### 3.6 Legacy `SETUP_TAS` interoperability

`SETUP_TAS` writes `TQAVCTRL` and `QBVCYCLET`, so left alone it would silently change the cycle of a live schedule. The ABI stays; a new check runs before any write. **This is a design proposal: code 26 does not change until the governing requirement and ADR are accepted.**

| Condition at `SETUP_TAS` entry | Behaviour | Why |
|---|---|---|
| No contract schedule, `BaseT` reads 0 | **Exactly as today**, including repeated `SETUP_TAS` | `SETUP_TAS` never writes `BaseT`, so all existing flows stay here |
| `origin = CONTRACT`, or a contract operation in progress | Refused, nothing written | The schedule is managed |
| `BaseT` non-zero with no contract record | Refused, nothing written | `SETUP_TAS` cannot have written it. It may be a contract schedule whose record was lost at detach, as in E14 |

Conflict semantics:

| Item | Definition |
|---|---|
| NTSTATUS | `STATUS_INVALID_DEVICE_STATE`. Verdict `BLOCKED`, an unmet prerequisite |
| Detail | The legacy struct cannot carry it and error completions drop the output buffer. So the raw NTSTATUS is the signal |
| Visibility to the schedule owner | `legacy_conflicts` in `TAS_QUERY` increments, and `generation` does not change |
| Successful `SETUP_TAS` | `origin = SETUP_TAS`, `lifecycle = LEGACY_ARMED`, generation increments, so a stale contract request fails |
| Reverse direction | `TAS_ACTIVATE` and `TAS_REPLACE` on `origin = SETUP_TAS` fail `STATE_CONFLICT` |
| Race | `SETUP_TAS` takes the same per-adapter claim as contract operations (section 9.3) |

| Other path | Writes TSN registers today? | Treatment |
|---|---|---|
| `DISARM_TAS` | No on I226 | None needed |
| `SETUP_QAV` | No, recorded stub | Joins the conflict check if it gains register writes |
| `SET_LAUNCH_TIME` | No, stub | Same |
| `SETUP_FP` | `FP_CONFIG` only | Not a schedule register; preemption with Qbv is refused separately |
| Debug `WRITE_REGISTER` | Yes, anything | Debug-only and not blocked. `TAS_QUERY` reports the mismatch as `INCONSISTENT` |

**Known effect on diagnostics:** `tas_gate_investigation --rearm` and `--after-rearm` call `SETUP_TAS` with a live `BaseT`, so they would be refused once this applies. No automated test does this: `SETUP_TAS` is the only automated path into this state, and it never writes `BaseT`. Test coverage and verdicts are unchanged.

## 4. State model

The driver reports facts and activation status separately. They are read fresh from hardware on every `TAS_QUERY` and at the start of every mutating operation, and compared with the driver's record. No status comes from the record alone.

### 4.1 Activation status

Each of the two slots, `current` and `next`, carries one status:

| Status | Requires | Not sufficient |
|---|---|---|
| `NONE` | No schedule in this slot | |
| `PROGRAMMED` | Registers written and read back, commit not completed | Treated as active |
| `PENDING` | Commit classified in time (section 7.3); PHC before target | Readback of `BaseT`: on a running schedule it shows the old value |
| `INFERRED_ACTIVE` | **All of:** commit classified in time; PHC past target; PHC not moved backward since commit; registers match the record; TSN mode on; no link or power event since commit | **PHC ≥ `BaseT` alone** |
| `HARDWARE_VERIFIED` | A hardware mechanism validated per family and accepted by ADR, that shows *this* schedule took effect | Write success, readback match, PHC ≥ `BaseT`, or any combination. **Never returned in v1**, and no v1 code path sets it |
| `INCONSISTENT` | Driver and hardware disagree: see `inconsistency` | |
| `UNKNOWN` | Cannot be determined: ambiguous commit, unreadable registers, or a post-event state not yet re-established | |

Wire proof belongs to tests. An external observer attests behaviour; the driver does not report it.

### 4.2 Hardware and origin facts

| Field | Values | Source |
|---|---|---|
| `hw_mode` | `OFF`, `TSN`, `UNREADABLE` | `TQAVCTRL` |
| `origin` | `NONE`, `SETUP_TAS`, `CONTRACT`, `FOUND_AT_ATTACH` | Driver record and registers |
| `gate_policy` | `NONE`, `SCHEDULED`, `OPEN_ALL` | Driver record |
| `restoration` | `COMPLETE`, `PARTIAL_OPEN_ALL`, `PARTIAL_RESIDUE`, `NONE`, `UNKNOWN` | Registers against the family baseline |
| `flags` | `OP_IN_PROGRESS`, `UNVERIFIED_CONFIG_ACTIVE`, `PHC_RESET_SEEN` | Driver |

`lifecycle` summarises these for a reader who wants one word. It never replaces them.

### 4.3 Transitions of a slot

| From | Event | To |
|---|---|---|
| `NONE` | Writes and readback done | `PROGRAMMED` |
| `PROGRAMMED` | Commit classified in time | `PENDING` |
| `PROGRAMMED` | Failure, rollback complete | `NONE` |
| `PROGRAMMED` | Rollback failed | `INCONSISTENT` |
| `PENDING` | All conditions in 4.1 hold | `INFERRED_ACTIVE`; `next` moves to `current` |
| `PENDING` | Commit ambiguous | `UNKNOWN` |
| `INFERRED_ACTIVE` | Registers disagree, PHC moved back, mode changed | `INCONSISTENT` |
| `INFERRED_ACTIVE` | Link or power event | `UNKNOWN` until reconciled |
| `UNKNOWN` | Successful `TAS_REPLACE` with a future target | `PENDING` |
| `INCONSISTENT` | Schedule registers read baseline and TSN off, after a NIC cycle owned by the test or operator | `NONE` |

`INCONSISTENT` reasons form a bitmask:

| Reason | Trigger | Seen |
|---|---|---|
| `HW_ARMED_NO_RECORD` | TSN mode or schedule registers set, no driver record | E14 |
| `DRIVER_ARMED_HW_OFF` | Record says armed, hardware not in TSN mode | UNVERIFIED |
| `BASE_FAR_AHEAD` | `BaseT` more than two cycles ahead of PHC with no pending record | E14 |
| `PHC_RESET` | PHC moved backward since commit | E12 |
| `WINDOW_MISMATCH`, `CYCLE_MISMATCH` | Registers differ from record, including a `SETUP_TAS` overwrite | UNVERIFIED |
| `TXQCTL_MISMATCH` | Flags differ from record | UNVERIFIED |
| `LINK_SPEED_CHANGED` | A window no longer meets the minimum at the current speed | UNVERIFIED |
| `READ_FAILED` | A register could not be read | UNVERIFIED |

### 4.4 Lifecycle summary

Derived, first match wins:

| `lifecycle` | Condition | Meaning |
|---|---|---|
| `UNKNOWN` | Any slot `UNKNOWN`, or not safe to read hardware | Cannot be determined |
| `INCONSISTENT` | Any slot `INCONSISTENT`, or `origin = FOUND_AT_ATTACH` | Driver and hardware disagree |
| `LEGACY_ARMED` | `origin = SETUP_TAS` | Armed by code 26. No schedule applied |
| `PROGRAMMED` | `next` or `current` is `PROGRAMMED` | Written, not committed |
| `PENDING` | `next` or `current` is `PENDING` | Committed, target ahead. An existing schedule may still run |
| `OPEN_ALL` | `current` is `INFERRED_ACTIVE`, `gate_policy = OPEN_ALL` | Whole-cycle schedule inferred running, TSN mode on. **Not restoration** |
| `INFERRED_ACTIVE` | `current` is `INFERRED_ACTIVE` | Schedule inferred running |
| `RESIDUE` | `hw_mode = OFF`, non-baseline registers | Left TSN mode, residue remains |
| `NONE` | `hw_mode = OFF`, baseline | Clean |

A slot can be `PENDING` with `gate_policy = OPEN_ALL`: `OPEN_ALL` describes *what* the schedule does, and the status describes *how far* it got. Readback never moves a slot past `PROGRAMMED`. Reaching `PENDING` needs commit timing, and reaching `INFERRED_ACTIVE` needs all the conditions in 4.1.

### 4.5 `tas_armed` review

**Finding: `tas_armed` cannot remain the source of truth.** One bit cannot express pending, open-all, residue, inconsistent or unknown. Its current writers are `SETUP_TAS` on success and `DISARM_TAS` on a successful disable or on a device without TAS. On I226 `DISARM_TAS` never clears it. Its reader is `GET_TAS_STATE`. `tas_restore.h` and TC-TSNREG-001 depend on what it returns, so the field and its ABI stay.

**Proposal:** one lifecycle record in `AVB_DEVICE_CONTEXT` is the source, covering origin, policy, both slots, generation and the operation store. `tas_armed` becomes a derived value, set only by one helper when the record changes:

| Lifecycle | `tas_armed` | Why |
|---|---|---|
| `NONE`, `RESIDUE` | 0 | TSN mode off. Matches TC-TSNREG-001 |
| `LEGACY_ARMED` | 1 | As today |
| `PROGRAMMED`, `PENDING`, `INFERRED_ACTIVE`, `OPEN_ALL` | 1 | TSN mode on, so `tas_restore.h` must not see a clean adapter. `OPEN_ALL` is not disarmed |
| `INCONSISTENT` with `origin = FOUND_AT_ATTACH` | **0** | No driver record. **Setting 1 would hide the disagreement that TC-TSNREG-001 caught in E14** |
| `INCONSISTENT` or `UNKNOWN` with a contract record | 1 | Never claim clean while state is in doubt |

`last_tas_config` for a contract schedule holds base and cycle, with gate arrays zero, since it has no gate list. Its header comment ("armed by IOCTL_AVB_SETUP_TAS") needs updating at implementation; the struct does not change. `armed = 1` keeps meaning "driver believes TAS is set", never "hardware verified". `tas_restore.h` handles contract states the same way it handles legacy arming, through `DISARM_TAS` returning `STATUS_NOT_SUPPORTED`.

## 5. Support tiers and validation

### 5.1 Support tiers

Three different answers must never collapse into one, and a tested value is not a hardware limit:

| Tier | Meaning | Refused with | Verdict |
|---|---|---|---|
| `HW_UNSUPPORTED` | The hardware lacks it | `STATUS_NOT_SUPPORTED` | `SKIP_UNSUPPORTED` |
| `NOT_IMPLEMENTED` | Hardware can, driver does not | `STATUS_NOT_IMPLEMENTED` | `SKIP_NOT_IMPLEMENTED` |
| `IMPLEMENTED_UNVERIFIED` | Driver can, no hardware evidence for this device | `STATUS_REQUEST_NOT_ACCEPTED`, unless the caller sets `AVB_TAS_F_ACCEPT_UNVERIFIED`; then `UNVERIFIED_CONFIG_ACTIVE` is reported | `INCONCLUSIVE` |
| `IMPLEMENTED_VERIFIED` | Hardware RED/GREEN evidence exists for this device | Accepted | Test decides |

| Feature | Hardware capability | Evidence today | Proposed v1 tier |
|---|---|---|---|
| Cycle time | **DOC** 31.25 µs to 1 s, permitted table | 125 ms, 500 ms, 1 s (LOCAL_DIAGNOSTIC only) | Tested values `VERIFIED` after HIL GREEN; other table values `IMPLEMENTED_UNVERIFIED` |
| Values outside the table | Not permitted by **DOC** | None | `INVALID_CONFIG` |
| Identical windows on all queues | Yes | Every run | `VERIFIED` after HIL GREEN |
| Per-queue windows | **DOC** yes | None | `IMPLEMENTED_UNVERIFIED` |
| `Strict_End` | **DOC** yes | 2 runs, below the window rule | `IMPLEMENTED_UNVERIFIED` until TC-TASAPI-009 |
| `Strict_Cycle` | **DOC** yes | None | `IMPLEMENTED_UNVERIFIED` |
| Pre-activation `HOLD` | Yes | 1 run | `IMPLEMENTED_UNVERIFIED` until EG-02 |
| Pre-activation `PASS_UNGATED` | Depends on the mechanism (section 6.2) | 2 of 2 by `FutScdDis`, `REV_04` | `NOT_IMPLEMENTED` until ADR-TSN-003 |
| Other I226 devices or revisions | Capability advertised | None | `IMPLEMENTED_UNVERIFIED`, **not** unsupported |
| I225 | **LINUX** rejects future base | None | `NOT_IMPLEMENTED` |
| Adapters without `INTEL_CAP_TSN_TAS` | None | Advertised capability | `HW_UNSUPPORTED` |

### 5.2 Validation

Validation runs before any register write, so a rejected request changes nothing.

| ID | Rule | Basis | Failure |
|---|---|---|---|
| TAS-DR-01 | Handle bound; `net_luid` matches; header valid; reserved fields and unknown flags 0 | Section 3.4 | Section 3.4 |
| TAS-DR-02 | Tier check per feature, device and revision | Section 5.1 | Per tier |
| TAS-DR-03 | One window per queue. No gate list is accepted | **DOC** registers, **LINUX** | `INVALID_CONFIG` |
| TAS-DR-04 | `cycle_ns` within hardware range and on the permitted table. 64-bit arithmetic throughout | **DOC** | `INVALID_CONFIG` |
| TAS-DR-05 | `start_ns < end_ns ≤ cycle_ns`. Window greater than 1.1 × transmit time of max(512 bytes, `max_frame_bytes` plus overhead) at current link speed | **DOC** 8.12.12 | `INVALID_CONFIG` |
| TAS-DR-06 | `end_ns < cycle_ns` with a gap, except `OPEN_ALL` | **DOC** 7.5.2.9.2, EG-06 | `INVALID_CONFIG` |
| TAS-DR-07 | Base seconds fit `BASET_H`; nanoseconds part fits 30 bits; `base + n × cycle` does not overflow | **DOC** 8.12.7-8 | `INVALID_CONFIG` |
| TAS-DR-08 | Base at least `min_margin_ns` after a PHC read. REPLACE: also on the running grid; off-grid is `IMPLEMENTED_UNVERIFIED` | E1 | `DEADLINE_TOO_NEAR` |
| TAS-DR-09 | `AVB_TAS_Q_STRICT_*` set at ACTIVATE; REPLACE keeps them, because `TXQCTL` acts before `BaseT` (E7) | E7, E8 | `UNSUPPORTED_COMBINATION` |
| TAS-DR-10 | Cycle extension, launch time on the same queue, and preemption with Qbv rejected in v1 | No evidence | `UNSUPPORTED_COMBINATION` |
| TAS-DR-11 | One pending commit per adapter | No evidence on overlap | `BUSY` |
| TAS-DR-12 | `expected_generation` and `context_epoch` match | Section 9 | `STATE_CONFLICT` |
| TAS-DR-13 | Link up, speed known, `hw_state ≥ AVB_HW_PTP_READY` | Existing handler | `NOT_READY` |
| TAS-DR-14 | `pre_activation` set explicitly at ACTIVATE. There is no default | D3 | `INVALID_CONFIG` |

Strict_End meaning for callers: **DOC** allows a frame from a flagged queue only if it is expected to finish inside the window. **HW** E8 adds that a non-fitting frame at the head blocks the whole queue until the window or flag changes. `max_frame_bytes` is the caller's promise. The driver cannot enforce it on passing traffic, so this hazard is documented, not prevented.

## 6. Initial setup: `TAS_ACTIVATE`

Preconditions: `hw_mode = OFF`, `origin = NONE`, baseline registers. Otherwise `STATE_CONFLICT`, nothing written.

### 6.1 Sequence

| Step | Action | Notes |
|---|---|---|
| 1 | Read PHC, recheck margin | Nothing written yet |
| 2 | Write `STQT`, `ENDQT`, `TXQCTL` per queue | Mode not yet set |
| 3 | Write `TQAVCTRL` per selected mechanism | Frames may hold from here |
| 4 | Write `QBVCYCLET_S`, then `QBVCYCLET` | **DOC** explicit order |
| 5 | Read PHC, recheck margin | |
| 6 | Write `BASET_H`, then `BASET_L` with `Copy_H` and `Auto_Set` clear | Commit point |
| 7 | Read PHC, classify (section 7.3) | |
| 8 | Read back every written register | `BASET_H` only after step 6: it read 0 before that in all runs |
| 9 | Status `PENDING` | Never active here |

**This exact sequence has never run.** Every future-`BaseT` run began from `SETUP_TAS`. EG-02 must prove it before `ACTIVATE` leaves `NOT_IMPLEMENTED`.

### 6.2 Pre-activation: behaviour is separate from mechanism

The caller selects **behaviour**. The family selects a **mechanism**. There is no default.

| Behaviour | Meaning |
|---|---|
| `AVB_TAS_PRE_HOLD` | Frames held until the first window after `BaseT`. **HW** 4.76 s hold, 0 lost. Long holds risk software timeouts: **LINUX** rejects this on I225 |
| `AVB_TAS_PRE_PASS_UNGATED` | Traffic flows until `BaseT`, then gated |

| Mechanism for `PASS_UNGATED` | Documented? | Evidence | Open point |
|---|---|---|---|
| M1 `FutScdDis` (`TQAVCTRL` bit 7, with `BASET_L` zero write) | **No:** the held manual marks bit 7 Reserved. Rests on **LINUX** | 2 of 2 on `REV_04` (E6) | Compatibility across revisions and with the miniport |
| M2 Open first: `OPEN_ALL` schedule, then `REPLACE` with a future target | Yes, uses documented registers only | Parts only (E1, E15), never as a sequence | Needs a first `BaseT` choice and a run |
| M3 Mode last: schedule first, `TQAVCTRL` last | Yes, but **DOC** discourages mode change in operation | 1 run with past `BaseT`, gating correct | Never run with a future `BaseT` |

The bit's use on `REV_04` is recorded as evidence only. **`PASS_UNGATED` stays `NOT_IMPLEMENTED` until ADR-TSN-003 selects a mechanism**, and it is not a production default.

### 6.3 Failure and rollback

| Failure point | Rollback | Result |
|---|---|---|
| Before step 3 | Rewrite baseline windows and `TXQCTL` | `NONE` |
| Steps 3-5 | `TQAVCTRL`, cycle, shadow and windows written back (E10) | `NONE` if readback matches |
| Step 6 onward | `BaseT` cannot be cleared in place (E10) | `PARTIAL_RESIDUE`, `ROLLBACK_PARTIAL` |
| Rollback write fails | Stop | `INCONSISTENT`, `ROLLBACK_FAILED` |

## 7. Schedule replacement: `TAS_REPLACE`

Preconditions: `current` is `INFERRED_ACTIVE`, or `UNKNOWN` to re-establish a known schedule. `origin = CONTRACT`. Generation and epoch match.

### 7.1 Sequences

| Case | Writes, in order | Evidence |
|---|---|---|
| Same cycle | Windows, `BASET_H`, `BASET_L` | 1 run for exactly this; 5+ with shadow and cycle rewritten |
| Changed cycle | Windows, `QBVCYCLET_S`, `QBVCYCLET`, `BASET_H`, `BASET_L` | 1 run |
| `AVB_TAS_F_OPEN_ALL` | Windows `0` to `cycle` on all queues, `BASET_H`, `BASET_L` | 1 run, low rate |

During `next = PENDING`, `regs` shows the new windows while `current` holds the running schedule. `BASET_H` is excluded from readback verification, because on a running schedule it shows the running value (E5).

### 7.2 Minimum margin

`min_margin_ns` is a reported limit, not a constant in callers. **Provisional value: 1 s** (UNVERIFIED below the lowest observed 0.935 s), and at least two running cycles. `#50`'s 1 ms has no supporting evidence. The 34 ms seen for one `BaseT` write came from user-mode diagnostics making separate IOCTLs with logging on, so it does not size an in-kernel commit. EG-01 measures that.

### 7.3 Commit classification

The driver reads the PHC immediately before (`t0`) and after (`t1`) the `BASET_L` write:

| Condition | Conclusion | Action |
|---|---|---|
| `t1 + guard < target` | Written in time | `next = PENDING` |
| `t0 ≥ target`, cycle unchanged | Landed late. Old schedule continues (E4, 1 run) | Rewrite previous windows, `DEADLINE_MISSED` |
| `t0 ≥ target`, cycle changed | Effect unknown (EG-15) | `UNKNOWN`, `ACTIVATION_AMBIGUOUS` |
| Otherwise | Cannot tell | `UNKNOWN`, `ACTIVATION_AMBIGUOUS`, windows not rewritten |

A missed deadline is a failure and must not be masked by windows reading back as new, which is the silent case in E4. Recovery from `UNKNOWN` is a new `TAS_REPLACE` with a future target.

### 7.4 Partial writes

Writes before the commit do not take effect on a running schedule (E4). A failure before `BASET_L` is recovered by rewriting previous values. If that readback fails: `INCONSISTENT`, `ROLLBACK_FAILED`.

## 8. Opening the gates, leaving TSN mode, and restoration

Three different things must stay distinct:

| | `OPEN_ALL` | `DISABLE_TO_LEGACY` | NIC cycle |
|---|---|---|---|
| Operation | `TAS_REPLACE` with `AVB_TAS_F_OPEN_ALL` | `TAS_DISABLE_TO_LEGACY` | Owned by test or operator, **never the driver** |
| Hardware mode after | **TSN** | Legacy | Legacy if verified |
| Schedule after | Whole-cycle, still running | Windows, shadow and cycle written back; `BaseT` keeps advancing | Baseline, 40 of 41 |
| Takes effect | At target, about the margin later | At once | After link-up |
| PHC | Continuous | Continuous in observed cases | **Reset** (E12) |
| Evidence | 1 run, low rate | 1 run plus 3 cycles | 40 of 41 |
| `restoration` | `PARTIAL_OPEN_ALL` | `PARTIAL_RESIDUE` | `COMPLETE` **only after registers are read** |
| Is it restoration? | **No.** Every gate open is not restored hardware | **No.** Partial | Only if verified |

`DISABLE_TO_LEGACY` conflicts with guidance. **DOC** 7.5.2.1 says the mode is set at initialisation and not during operation, **LINUX** resets the adapter here, and the `disable_tas` source comment notes that the miniport owns `TQAVCTRL`. It therefore stays `NOT_IMPLEMENTED` unless ADR-TSN-002 accepts it. In legacy mode the windows are ignored, yet a later `TQAVCTRL` write resumes the same schedule (E9), so leaving TSN mode does not erase it. Until EG-14, `ACTIVATE` after it is refused.

**Baseline** is a family constant from the SSOT, not a value captured at attach, because E14 shows attach can see a dirty adapter. **HW** read `ENDQT` as 0 at baseline where **DOC** lists a 1 s default, so the table must reflect hardware, and that difference is recorded.

PHC reset is reported separately from register restoration, so "registers restored, PHC reset" stays visible.

### 8.1 NDIS lifecycle

Current behaviour comes from `src/filter.c`. Proposed rules are for ADR-TSN-002.

| Event | What the driver does today | Proposed rule | Status |
|---|---|---|---|
| `FilterAttach` | New context, so `tas_armed` is 0; `AvbBringUpHardware` | Read TSN registers read-only. Non-baseline: `origin = FOUND_AT_ATTACH`. No write, no automatic cleanup | E14 is the case this catches |
| `FilterPause` | Stops timers because MMIO on powered-down hardware risks a bugcheck; no wait on NBLs | No TSN write. Mutating operations fail `NOT_READY` until Running. `TAS_QUERY` does **no MMIO** unless Running: `hw_mode = UNREADABLE`, slots `UNKNOWN` | Rule UNVERIFIED |
| Held frames during Pause | Passthrough, so NDIS completes the pause when sends return | **Unknown** whether closed gates or a `Strict_End` block delay pause. No automatic gate opening. EG-13 | UNVERIFIED; link to #328 unproven |
| `FilterRestart` | `AvbBringUpHardware`, and `init_ptp` reseeds `SYSTIM` | **Hazard:** the context survives, but the PHC can jump back under a live schedule, recreating the E14 hold with no NIC cycle. Reconcile before any operation: PHC moved back gives `PHC_RESET`, `INCONSISTENT`. Fix belongs to the Issue 8 draft | Hazard by source reading, EG-18 |
| `FilterDetach` | Removal lock drains in-flight IOCTLs, then context freed | Detach cannot fail and writes no TSN register. Contract record and operation store are lost, and `context_epoch` changes | Source |
| `FilterUnload` | Sets `g_FilterStopping`, deregisters | No TSN write. Unload never claims restoration. Source comment says TAS state survives unload; not verified | EG-13 |
| S3/S4 resume | Restart path re-initialises the PHC | Reconcile: registers may be lost. Baseline with a contract record gives `DRIVER_ARMED_HW_OFF` | EG-12 |
| Miniport reset, link change | No handling | Reconcile on next query. Slots become `UNKNOWN` | EG-12 |

Every contract IOCTL holds the existing `ioctl_remove_lock` through the dispatcher, so it cannot race `FilterDetach`.

## 9. Errors, identity and concurrency

### 9.1 Retrieving failure detail

An error completion does not return the output buffer (`docs/testing/test-verdict-contract.md`). So `result` in the output struct is meaningful **on success only**. Failure detail comes from `TAS_QUERY`:

1. The client puts a non-zero `client_op_id` in the request.
2. The driver records it on entry, with `result = IN_PROGRESS`.
3. On any `DeviceIoControl` failure, the client captures the raw NTSTATUS, then calls `TAS_QUERY` on **the same handle** with `lookup_client_op_id`.
4. `lookup_result` is `FOUND` with the record, `NOT_FOUND`, or `NOT_REQUESTED`.

| Situation | What the client has |
|---|---|
| Header, size, version or unbound-handle failure | NTSTATUS only. No operation started, so no record exists |
| Failure after that point | NTSTATUS, plus a record with result, stage and generation |
| `NOT_FOUND` after a mutating call | **Outcome unknown.** The client must read state and never assume either way |
| Detach or re-attach | Records are lost; `context_epoch` changes, and a stale request fails `STATE_CONFLICT` |

### 9.2 Results cannot cross operations or handles

| Risk | Protection |
|---|---|
| Two operations on one handle | Keyed by `client_op_id`. A repeated id while in the store: `STATUS_DUPLICATE_OBJECTID`, no record |
| Another handle reads a result | Record stores its issuing file object; a different handle gets `NOT_FOUND` |
| File object pointer reuse | Records for a handle are cleared at `IRP_MJ_CLEANUP`, so a new handle cannot inherit them |
| Different adapters | Store is per adapter context. `net_luid` in the output identifies the adapter |
| Handle rebound to another adapter | Lookup uses the new adapter: `NOT_FOUND`, and `net_luid` shows why |
| New context after re-attach | `context_epoch` differs |
| Store full | Fixed 8 records, oldest finished record evicted; `IN_PROGRESS` never evicted. Eviction reads as `NOT_FOUND` |

### 9.3 Concurrency

| Rule | Behaviour |
|---|---|
| One mutating operation per adapter, any handle | `InterlockedCompareExchange` claim at entry. Second caller gets `BUSY` at once and never waits |
| Stale view | `expected_generation` is a compare-and-swap. `STATE_CONFLICT` if another operation came first |
| Ordering | `driver_op_seq` increases per adapter context, for every recorded operation |
| Generation | Starts at 1 in each context. Increments on each successful contract operation and each successful `SETUP_TAS`. Never on failure, `TAS_QUERY` or a refused `SETUP_TAS` |
| Epoch | New random 64-bit value at each `FilterAttach`. Stale epoch: `STATE_CONFLICT` |
| Restart | `stats_pause_restart_generation` already counts Pause. A contract request across a Pause/Restart is revalidated from fresh hardware reads |
| `TAS_QUERY` during an operation | Never blocks. `OP_IN_PROGRESS` is set and the slot statuses are not advanced from a mid-sequence read |
| Different adapters | Independent |
| Removal or detach during an operation | Fails with the existing not-connected or not-ready status. Record lost with the context |

### 9.4 Locking

`#45` and `#52` require register access and device ops inside per-device spin locks. `#50` asks for one lock hold over the commit. So the commit sequence (PHC read, `BaseT` writes, PHC read) runs inside the existing per-device lock with no logging or sleeping, at a bounded hold time measured by EG-01 against `#113`. Validation and readback stay outside it. The claim in 9.3 is separate from the spin lock, so `BUSY` never spins. Implementation must confirm the current code holds that lock: the existing `SETUP_TAS` path shows none, which is a possible gap against `#45`.

### 9.5 Results

| Result | NTSTATUS | State after | Verdict if unexpected |
|---|---|---|---|
| `OK` | `STATUS_SUCCESS` | `PENDING` or as stated | n/a |
| `IN_PROGRESS` | n/a, record only | | n/a |
| `INVALID_CONFIG` | `STATUS_INVALID_PARAMETER` | Unchanged, nothing written | `FAIL` |
| `HW_UNSUPPORTED` | `STATUS_NOT_SUPPORTED`, only if the capability is known absent | Unchanged | `SKIP_UNSUPPORTED`; on an advertising adapter, `FAIL` |
| `NOT_IMPLEMENTED` | `STATUS_NOT_IMPLEMENTED` | Unchanged | `SKIP_NOT_IMPLEMENTED` |
| `NOT_HW_VERIFIED` | `STATUS_REQUEST_NOT_ACCEPTED` | Unchanged | `INCONCLUSIVE` |
| `UNSUPPORTED_COMBINATION` | `STATUS_INVALID_PARAMETER` | Unchanged | `FAIL` |
| `NOT_READY` | `STATUS_DEVICE_NOT_READY` | Unchanged | `BLOCKED` |
| `BUSY` | `STATUS_DEVICE_BUSY` | Unchanged | `BLOCKED` |
| `STATE_CONFLICT` | `STATUS_INVALID_DEVICE_STATE` | Unchanged | `BLOCKED` |
| `DEADLINE_TOO_NEAR` | `STATUS_INVALID_PARAMETER` | Unchanged | `FAIL` |
| `DEADLINE_MISSED` | `STATUS_IO_TIMEOUT`, not the success-class `STATUS_TIMEOUT` | Old schedule, windows rewritten | `FAIL` |
| `ACTIVATION_AMBIGUOUS` | `STATUS_IO_TIMEOUT` | `UNKNOWN` | `FAIL` |
| `READBACK_MISMATCH` | `STATUS_DEVICE_DATA_ERROR` | Rolled back or `INCONSISTENT` | `FAIL` |
| `MMIO_FAILED` | `STATUS_IO_DEVICE_ERROR` | Rolled back or `INCONSISTENT` | `FAIL` |
| `ROLLBACK_PARTIAL`, `ROLLBACK_FAILED` | `STATUS_UNSUCCESSFUL` | `PARTIAL_RESIDUE` or `INCONSISTENT` | `CLEANUP_FAILED` in cleanup, else `FAIL` |
| `INCONSISTENT_STATE` | `STATUS_INVALID_DEVICE_STATE` | `INCONSISTENT`, nothing written | `FAIL` |
| Legacy conflict, on `SETUP_TAS` only | `STATUS_INVALID_DEVICE_STATE`, no record, `legacy_conflicts` increments | Unchanged, nothing written | `BLOCKED` |
| `RESTORATION_INCOMPLETE` | `STATUS_UNSUCCESSFUL` | As reported | `CLEANUP_FAILED` |

An unverifiable active schedule is not an error: `INFERRED_ACTIVE` states exactly that. `INCONCLUSIVE` and `NOT_HW_VERIFIED` use existing verdict semantics; no verdict changes.

## 10. Responsibilities

| Concern | Driver | Test | Operator or runner |
|---|---|---|---|
| Validating requests | Owns | Exercises | |
| Programming and rollback | Owns | | |
| Reporting state honestly | Owns | Reads | |
| Proving pre-state | Reports through `TAS_QUERY` | Owns: every test proves its own prerequisites | |
| Traffic and wire observation | | Owns, using the I210 observer | |
| Cleanup and NIC cycle | Does not cycle the NIC | Owns | Runner keeps Safety, gap and timeout rows |
| Restoration verdict | | Owns: `TAS_QUERY`, PHC, TX on the wire | |
| `CLEANUP_FAILED` | | Reports. Never blocks later tests | Stays visible |
| Reboot | | | Operator decision only |
| Surviving state | Reports `INCONSISTENT` | Reports; no automatic fix | |

## 11. Requirements, design and HIL traceability

All tests are **hardware** tests using dedicated IOCTLs only, with no `READ_REGISTER` or `WRITE_REGISTER`. Each discovers capability, proves its pre-state, owns setup and NIC-cycle cleanup, and proves restoration by `TAS_QUERY`, measured PHC and TX on the wire. They reuse `tests/hardware/tas_wire_observer.h`; no new harness is proposed. Existing tests, classifications, verdicts and runner rows are unchanged, and nothing adds a gate or global blocking. Runner rows for the new tests are added at implementation. **Every implemented operation needs a hardware RED and a real HIL GREEN in Debug and Release.** RED evidence is the listed `LOCAL_DIAGNOSTIC` record plus the test failing against the unimplemented IOCTL.

| Proposed requirement | ADR | Design | Operation | HIL test | Hardware behaviour proved | RED evidence |
|---|---|---|---|---|---|---|
| REQ-F-TAS-STATE-001 | ADR-TSN-002 | 4, 12 | `TAS_QUERY` | TC-TASAPI-001 | Clean adapter reads `NONE`, `OFF`, `COMPLETE`; read-only | Not implemented |
| REQ-F-TAS-VALIDATE-001 | ADR-TSN-002 | 5 | All | TC-TASAPI-002 | Non-TAS adapter `SKIP_UNSUPPORTED`; unbound handle refused; tiers reported | Not implemented |
| REQ-F-TAS-VALIDATE-001 | ADR-TSN-002 | 5.2 | ACTIVATE, REPLACE | TC-TASAPI-003 | Each invalid request refused, `TAS_QUERY` and TX unchanged | Not implemented |
| REQ-F-TAS-ACTIVATE-001 | ADR-TSN-002, 003 | 6 | ACTIVATE, hold | TC-TASAPI-004 | Frames held to first window after `BaseT`, then gated; `PENDING` then `INFERRED_ACTIVE` | E6, EG-02 |
| REQ-F-TAS-ACTIVATE-001 | ADR-TSN-003 | 6.2 | ACTIVATE, pass | TC-TASAPI-005 | Frames flow until `BaseT`, then gated. **After ADR-TSN-003 only** | E6, EG-02 |
| REQ-F-TAS-REPLACE-001 | ADR-TSN-002 | 7 | REPLACE, same cycle | TC-TASAPI-006 | Wire moves at target, not before | E1, E2 |
| REQ-F-TAS-REPLACE-001 | ADR-TSN-002 | 7 | REPLACE, changed cycle | TC-TASAPI-007 | New cycle on the wire at target | E3 |
| REQ-F-TAS-REPLACE-001 | ADR-TSN-002 | 7.3 | REPLACE, too near | TC-TASAPI-008 | Refused, schedule unchanged on the wire | E4 |
| REQ-F-TAS-VALIDATE-001 | ADR-TSN-002 | 5 | ACTIVATE, Strict_End | TC-TASAPI-009 | Fitting frame passes; frame above declared size held, released on a wider window | E8 |
| REQ-F-TAS-OPENALL-001 | ADR-TSN-002 | 8 | REPLACE, `OPEN_ALL` | TC-TASAPI-010 | Flat arrivals, no PHC step, `OPEN_ALL` and TSN mode reported, later REPLACE re-gates | E15 |
| REQ-F-TAS-DISABLE-001 | ADR-TSN-002 | 8 | `TAS_DISABLE_TO_LEGACY` | TC-TASAPI-011 | Frames pass at once, PHC continuous, `RESIDUE`. **After ADR-TSN-002 only** | E10 |
| REQ-F-TAS-RESTORE-001 | ADR-TSN-002 | 8 | Restoration | TC-TASAPI-012 | After NIC cycle: `COMPLETE`, TX on the wire, PHC reset reported | E13 |
| REQ-F-TAS-LEGACY-001 | ADR-TSN-002 | 3.6 | Interplay | TC-TASAPI-013 | After `SETUP_TAS`: `LEGACY_ARMED`, contract operations refused | Not implemented |
| REQ-F-TAS-LEGACY-001 | ADR-TSN-002 | 3.6 | `SETUP_TAS` on a live schedule | TC-TASAPI-017 | Refused with exact NTSTATUS, nothing written, wire unchanged, `legacy_conflicts` up. Plain `SETUP_TAS` on a clean adapter behaves as today | Not implemented |
| REQ-F-TAS-LEGACY-001 | ADR-TSN-002 | 4.5 | `GET_TAS_STATE` | TC-TASAPI-018 | `armed` follows the table in 4.5 for each reachable state | Not implemented |
| REQ-F-TAS-ERROR-001 | ADR-TSN-002 | 9.1, 9.2 | Failure detail | TC-TASAPI-014 | Real refused call: record found on the same handle, `NOT_FOUND` on a second handle and on an I210-bound handle | Not implemented |
| REQ-F-TAS-ERROR-001 | ADR-TSN-002 | 9.3 | Concurrency | TC-TASAPI-015 | Two threads race REPLACE: one wins, the other `BUSY` or `STATE_CONFLICT`, wire shows one schedule | Not implemented |
| REQ-F-TAS-STATE-001 | ADR-TSN-002 | 4.1 | All | TC-TASAPI-016 | `HARDWARE_VERIFIED` never returned in any test, with PHC past `BaseT` | Not implemented |
| REQ-F-TAS-NDIS-001 | ADR-TSN-002 | 8.1 | Pause, Restart | TC-TASAPI-019 | Mutating refused while Paused, no MMIO in `TAS_QUERY`, reconcile after Restart reports what hardware shows | EG-18 |
| REQ-NF-TAS-OBSERVE-001 | ADR-TSN-002 | 12 | `TAS_QUERY` | TC-TASAPI-001, 004, 006 in Release | Same results, Release driver SHA256 recorded | Not implemented |
| REQ-NF-TAS-BUFFER-001 | ADR-TSN-002 | EG-03 | Buffer configuration | To be defined after EG-03 | Buffer values reported, behaviour at sustained load | EG-03 |

**Mock-only paths stay UNVERIFIED.** These cannot be driven on hardware by dedicated IOCTLs today: `READBACK_MISMATCH`, `MMIO_FAILED`, `ROLLBACK_*`, the race in `ACTIVATION_AMBIGUOUS`, `HW_ARMED_NO_RECORD` and `BASE_FAR_AHEAD`. Their mocks must reproduce E14's recorded register values, cannot establish HIL GREEN, and must be labelled UNVERIFIED in test output and in the issue. `HW_ARMED_NO_RECORD` gains HIL evidence only if EG-04 finds a trigger.

## 12. Release observability

**Need:** in Release, with no `READ_REGISTER` or `WRITE_REGISTER`, a test must see mode, cycle, `BaseT`, windows, flags, ring pointers and the PHC, close enough in time to reconcile them.

| Option | Assessment |
|---|---|
| Extend `QUERY_HW_SNAPSHOT` | **Rejected.** 280 bytes pinned by `TC-ABI-022` and hard-coded in `Run-Tests-CI.ps1`; has only `TQAVCTRL` |
| New snapshot version | **Rejected.** Same callers, and every test would interpret state itself |
| Extend `GET_TAS_STATE` | **Rejected.** Changes an existing ABI, and it is documented as driver-level only |
| `READ_REGISTER` in Release | **Rejected.** `#122` removes it from Release. Generic `WRITE_REGISTER` stays Debug-only |
| **`TAS_QUERY`** | **Proposed.** One purpose, fixed register set, no caller-supplied offsets, read-only, bound handles only |

Register offsets come from `intel-ethernet-regs` through a new device op, per `#92`, not literals. Nothing in the set has a read side effect; `TXSTMP` registers are excluded because reading them changes state. `revision_id` needs to reach `TAS_QUERY`, and implementation must confirm how the driver obtains it. Already available in Release: `GET_STATISTICS` (outstanding sends), `GET_CLOCK_CONFIG` (PHC), the I210 observer.

## 13. Open decisions

| # | Decision | Owner | State |
|---|---|---|---|
| O1 | `SETUP_TAS` on a live schedule | Q | **Decided (D4):** must not silently overwrite. Section 3.6 is the proposal, for REQ-F-TAS-LEGACY-001 |
| O2 | `tas_armed` meaning | Q | **Decided (D5):** derived from a richer record. Section 4.5 is the proposal, for ADR-TSN-002 |
| O3 | Four codes or `TAS_APPLY` | Q, via ADR-TSN-002 | Open. Recommendation: four |
| O4 | Is `DISABLE_TO_LEGACY` allowed in production | Q, via ADR-TSN-002 | Open. Decide with EG-14 |
| O5 | Pre-activation mechanism | Q, via ADR-TSN-003 | Open. Run EG-02 for M1 and M2 first |
| O6 | Should mutating codes require `FILE_WRITE_ACCESS` | Q, after EG-17 | Open. Recommendation: yes |
| O7 | Should `BaseT` non-zero alone refuse `SETUP_TAS` when no contract record exists | Q, via REQ-F-TAS-LEGACY-001 | Open. Recommendation: yes, or a lost schedule stays unprotected |

## 14. Evidence gaps

Priority order. **Blocks** names only the operation or claim affected. Everything else proceeds.

| ID, priority | Governing issue | Existing | Missing | Experiment | Expected | Acceptance | Blocks |
|---|---|---|---|---|---|---|---|
| EG-03, P0 | REQ-NF-TAS-BUFFER-001 | Default `TXPBSIZE`, `DTXMXPKTSZ` throughout. **DOC** and **LINUX**: 5 KB per queue, 1600 bytes, `RXPBSIZE` changed too | Behaviour under the documented configuration; whether a filter may change it | (1) Read-only: adapter settings and miniport behaviour. (2) `LOCAL_DIAGNOSTIC`: EG-02 and EG-06 under default and documented values, if (1) finds a safe way | Same gating with no stall, or a measured difference | Measured comparison at sustained rate; decision recorded | **Any production-ready claim** (D5). Not diagnostics or contract review |
| EG-02, P1 | REQ-F-TAS-ACTIVATE-001, ADR-TSN-003 | Future base always after `SETUP_TAS` | Section 6.1 from a clean adapter; M1, M2 compared | `LOCAL_DIAGNOSTIC`: exact order, `HOLD`, M1, M2 | HOLD held to first window; PASS flows, then gated | 3 runs each, 0 lost, 0 outside window after `BaseT`, clean restoration | **ACTIVATE** |
| EG-09, P1 | REQ-NF-TAS-OBSERVE-001 | Debug only | Release behaviour | All TC-TASAPI in Release after implementation | Same verdicts | Debug and Release GREEN with installed SHA256 | **Contract acceptance** |
| EG-04, P1 | Issue 16 draft | 40 of 41 clean, 1 survival, 1 recovery | Trigger; recovery repeatability | Instrumented only; no blind repeats without a trigger hypothesis | Rare reproduction with timeline, or a trigger | Trigger or bound; recovery shown 3 times | `COMPLETE` guarantee only. Reporting covers it |
| EG-01, P2 | REQ-F-TAS-REPLACE-001, `#113` | Lowest 0.935 s | Lower margin; in-kernel commit time | Step margin 500, 200, 100, 50 ms, 10 replacements each, via dedicated IOCTL | At target down to a limit, then refused or missed | 10 of 10 at chosen margin; hold time measured | Margins below 1 s only |
| EG-05, P2 | REQ-F-TAS-STATE-001 | Only the wire proves it. `TEST_SEND_PTP` timestamps are pre-send | A hardware signal | (a) Hardware TX timestamp of a kernel-sent frame against the window. (b) Commit off-grid and see whether `BaseT` adopts the new grid after target | Signal changes only when the schedule did | Agrees with the I210 in 20 of 20, including a deliberately missed commit | `HARDWARE_VERIFIED` only |
| EG-06, P2 | REQ-F-TAS-OPENALL-001 | Whole-cycle at low rate. **LINUX** cites a specification-update item and `RETX_CTL` | Behaviour at load | Sustained MTU frames, with `end == cycle` and with a gap | No loss or stall with the chosen gap | 0 lost, TDH keeps moving, 10 minutes | `OPEN_ALL` at load; gap value |
| EG-07, P2 | REQ-F-TAS-VALIDATE-001 | None | `Strict_Cycle` effect | Matrix with windows that respect TAS-DR-05 | Frame finishing past cycle end held | 2 runs, fitting controls pass | `Strict_Cycle` tier only |
| EG-08, P2 | REQ-F-TAS-VALIDATE-001 | 125 ms, ±30 ms host uncertainty | Accuracy; shorter cycles | Hardware RX timestamps on the I210 with PHC correlation | Uncertainty below 10 % of window | Stated uncertainty, 0 outside window | Tier of other cycles; any accuracy claim |
| EG-10, P2 | REQ-F-TAS-VALIDATE-001 | `REV_04` only | I226-V, I226-IT, other steppings, I225 | EG-02 and replace tests on each | Same, or a recorded difference | Per-device GREEN | Those devices' tier |
| EG-17, P2 | REQ-NF-TAS-OBSERVE-001, `#142` | `FILE_ANY_ACCESS`; default device ACL; no check found in `device.c` | Who can open and mutate | Review with `test_ioctl_access_control`; unelevated open test | ACL behaviour known | Decision O6 | Mutating codes' final definition |
| EG-11, P3 | REQ-F-TAS-VALIDATE-001 | Identical windows; only ring 0 carries traffic | Independence | Needs traffic on a second ring; feasibility unknown | Queues gate separately | 2 runs | Per-queue tier only |
| EG-12, P3 | REQ-F-TAS-STATE-001 | None | Link flap, speed change, miniport reset, S3 with a live schedule | Each event, then `TAS_QUERY` and wire | Report matches hardware | Each event classified | Nothing. Reports `UNKNOWN` |
| EG-13, P2 | REQ-F-TAS-NDIS-001 | Source comment only; cycles so far came after a drain | Pause, Detach and Unload with frames held. Link to #328 unproven | Under a Scenario B test plan: NIC cycle with frames held behind closed gates, bounded | Characterised: completes or stalls | Recorded outcome, Debug and Release | Unload and Pause guarantees only |
| EG-18, P2 | REQ-F-TAS-NDIS-001, Issue 8 draft | Source reading: `FilterRestart` reseeds the PHC | Whether Pause/Restart with a live schedule moves the PHC behind `BaseT` | Pause/Restart without device removal under a live schedule, then `TAS_QUERY` and wire | Hold reproduced, or none | Recorded outcome; reconcile reports it | Any claim that a schedule survives Pause/Restart |
| EG-14, P3 | REQ-F-TAS-DISABLE-001 | Past-`BaseT` re-arm kept the old schedule | Future `BaseT` after mode off | `DISABLE_TO_LEGACY`, future `BaseT`, then mode on | New schedule applies | 2 runs | `ACTIVATE` after legacy |
| EG-15, P3 | REQ-F-TAS-REPLACE-001 | None | Overlapping, off-grid and changed-cycle-miss effects | Each case separately | Characterised | Recorded outcome | Those cases only |
| EG-16, P3 | Various | 1 run each: E2, E3, E9, E15 | Repeatability | Covered by TC-TASAPI | Same | GREEN | Nothing beyond those tests |

## 15. Not in this design

Production code, new header entries, test files, runner changes, Debug diagnostics in automated tests, a fix for the surviving schedule, PHC reseed or TIMINCA fixes, and any statement about the #328 unload hang.
