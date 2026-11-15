---
specType: component-design
standard: IEEE 1016-2009
component: TAS lifecycle IOCTL contract (proposed)
phase: 04-design
version: 0.6.0
author: Claude Code, for Q
date: "2026-11-15"
status: DRAFT, derived artifact. Review baseline only, see #365. ADR-TSN-002 (#362) accepted 2026-11-14; requirements filed, not accepted; ADR-TSN-003 (#363) Proposed. Query code allocated (#434); query handler not implemented; nothing hardware verified
review: 04-design/reviews/DES-REVIEW-002-tas-ioctl-issue-ssot.md
traceability:
  design_issue: "#365 (DES-C-TAS-010), body status: review baseline, not approved"
  implementation:
    - "#434 (IOCTL_AVB_TAS_QUERY_HW code and TC-ABI-025, merged into feat/357-tas-activate-master as f108ee8; Implements #350)"
  architecture:
    - "#142 (ARC-C-IOCTL-005: IOCTL Dispatcher)"
    - "#100 (ARC-C-I225-008: Intel i225/i226 Device Implementation)"
  decisions:
    - "#91 (ADR-PERF-002), conflicts: see section 2"
    - "#92 (ADR-ARCH-003: Strategy Pattern)"
    - "#122 (ADR-SEC-001: Debug vs Release boundaries)"
    - "#133 (ADR-TSN-001: TSN Feature Prioritization)"
    - "#362 (ADR-TSN-002), Accepted 2026-11-14 (revision v7, decisions 1 to 6). Not accepted with it: the provisional structure sizes, #363, the #352 amendment, #357's implementation and evidence"
    - "#363 (ADR-TSN-003), Proposed"
  requirements:
    - "#50 (REQ-F-TAS-001: Time-Aware Shaper Configuration), open"
    - "#9 (REQ-F-TAS-001: Time-Aware Scheduler, IOCTL 26), reopened 2026-11-05, status:backlog"
    - "#64 (REQ-F-IOCTL-VERSIONING-001)"
    - "#45, #52 (register and device-op locking)"
    - "#113 (QA-SC-PERF-002)"
    - "#392 (QA-SC-TAS-001), status:in-progress"
    - "#350 (REQ-F-TAS-STATE-001), status:backlog"
    - "#351 (REQ-F-TAS-VALIDATE-001), status:backlog"
    - "#352 (REQ-F-TAS-ERROR-001), status:backlog"
    - "#353 (REQ-NF-TAS-OBSERVE-001), status:backlog"
    - "#354 (REQ-NF-TAS-BUFFER-001), status:backlog"
    - "#355 (REQ-F-TAS-LEGACY-001), status:backlog"
    - "#356 (REQ-F-TAS-NDIS-001), status:backlog"
    - "#357 (REQ-F-TAS-ACTIVATE-001), status:backlog"
    - "#358 (REQ-F-TAS-REPLACE-001), status:backlog"
    - "#359 (REQ-F-TAS-OPENALL-001), status:backlog"
    - "#360 (REQ-F-TAS-DISABLE-001), status:backlog"
    - "#361 (REQ-F-TAS-RESTORE-001), status:backlog"
  defects:
    - "#348 (BUG: I226 init_ptp overwrites SYSTIM)"
    - "#349 (BUG: TAS schedule survived a NIC cycle)"
  investigations:
    - "#364 (INV-TAS-EG18)"
  tests:
    - "#366 to #384 (TEST-TAS-API-001 to 019, cases TC-TASAPI-001 to 019), designed only"
---

# DES-C-TAS-010: TAS lifecycle IOCTL contract

**Design only.** This document changes no driver code, test, runner entry, existing IOCTL or governance file.

**GitHub Issues are authoritative.** This file is derived from them. It defines no requirement and takes no architecture decision. Everything here that would bind implementation is a proposal until the governing issues are accepted. They were filed on 2026-11-05; `04-design/reviews/DES-REVIEW-002-tas-ioctl-issue-ssot.md` records the review and its filing. `TAS-DR-nn` are design rules, not requirements. IOCTL codes are allocated only by #362 decision 1 (section 3.2); every other code named here is a candidate, not a reservation.

#362 is accepted, and its decision 1 allocates the query code, which is merged (#434). The governing requirements (#350 to #361) are still filed, not accepted. Each requirement's acceptance still needs its own hardware evidence, in Debug and Release (#362 decision 6).

| Stage | State on 2026-11-15 |
|---|---|
| Proposed | #350 to #361 (`status:backlog`), #363 (`status:backlog`, Proposed), #365 (review baseline, not approved). The v1 query layout of section 12 (536 bytes, provisional per #362 decision 1) |
| Accepted | #362 ADR-TSN-002, decisions 1 to 6 (2026-11-14). It does not accept the provisional structure sizes, #363, the #352 amendment, or #357's implementation and evidence |
| Implemented | `IOCTL_AVB_TAS_QUERY_HW` = 0x0017602C, the code only, with its ABI pin TC-ABI-025 (#434, f108ee8). The driver does not serve it: it still completes with `STATUS_INVALID_DEVICE_REQUEST`. `IOCTL_AVB_TAS_ACTIVATE` exists on the #357 integration branch as function 71 with the 80-byte request (#362, implementation status) |
| Hardware Verified | None. #366 to #384 record Hardware RED and GREEN as not verified |

#365 carries the label `status:completed`, set by automation. That label is not acceptance: DES-REVIEW-002, D1.

| Tag | Meaning |
|---|---|
| **HW** | Observed on hardware, with run count, under the conditions recorded in section 1 |
| **DOC** | Intel I225/I226 software manual, paraphrased |
| **LINUX** | Linux `igc` behaviour. A reference, not a requirement |
| **SOURCE** | What the source code at a named revision does, for example which register it writes. Not evidence of what the hardware retains |
| **STATIC** | Compile-time layout check. Proves sizes and offsets on the compilers used; proves no behaviour |
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
| E12 | A NIC cycle resets the PHC. Driver `init_ptp` reseeds `SYSTIM` | Every cycle | #348 |
| E13 | A NIC cycle clears schedule registers | 40 of 41 comparisons | Register logs |
| E14 | One cycle left the full schedule with `tasArmed=0`. TX held about 11 minutes until the PHC reached the surviving `BaseT`. `Disable-NetAdapter` then recovered it | 1 case, 1 recovery | `d9ad1e0d`, `949a8c0d`, `558451d5`, `c9f1d2f7` |
| E15 | Whole-cycle windows by future `BaseT` pass frames at low rate; a later replacement re-gates | 1 run | `6aff008e` |
| E16 | Periodic gating at 1 s and 125 ms | Repeated at 1 s, 1 run at 125 ms | `b9febbc0` |

**The #328 unload hang remains unexplained.** Nothing here shows a link to it.

### 1.1 Observations used by the query contract (2026-11-15)

Each row holds only under its recorded conditions. None is a reply of the query, and none is hardware GREEN for #350: the query has never run.

| ID | Observation | Conditions and artifact | Class |
|---|---|---|---|
| E17 | `DTXMXPKTSZ` (0x0355C) read `0x00000098` in the identified Debug captures | Section 1 setup, Debug register reads. `test_tas_isolated_arm.exe_20261105_063525.log` lines 10 and 90 (SHA256 `56c8447cfb08333f841d9f78e089dc69a5d171e6b09eaa93f5ec45a9eee15221`), `ssot_register_validation_test.exe_20261105_063510.log` line 106 (SHA256 `0cf4a20dc18c574a2c76d8b7388023608e2711eafd0bb5d705ca2526911a73bc`), both in `tasarm-20261105` | **HW**, raw value only; these citations are not a count of independent runs. Decoded with the SSOT mask (9 bits) and `_SCALE` 64 it would be 152 × 64 = 9728 bytes: **INFERENCE** from the SSOT definition, not a query reply |
| E18 | `ENDQT` read 0 at the baseline, where **DOC** lists a 1 s default | Recovery record 2026-11-05 (E14 evidence `c9f1d2f7`) | **HW**. The SSOT records it as the observed baseline (section 8) |
| E19 | In the E14 surviving-schedule capture: `QBVCYCLET_S` and `QBVCYCLET` both `0x3B9ACA00`, windows non-baseline, `TQAVCTRL` `0x00000009`, `tasArmed=0` | `tasarm-20261105/tsnreg-after-matrix-20261105_134715.log` lines 103–121, SHA256 `558451d5c56c0960a0801c292bf9f14af6c9701f2f94717ab2862eeb93189002`; investigation `issue-328-tas-isolated-arm-20261104.md`, E14 case | **HW**, 1 capture of the E14 case, not an independent experiment. These register readings do not independently prove a running or active schedule |
| E20 | `QBVCYCLET_S` read 0 in the identified baseline/post-cycle captures; the same isolated-arm log also reports shadow 0 during its `SETUP_TAS` sequence | `test_tas_isolated_arm.exe_20261105_063525.log` lines 17, 63–71 and 97; `ssot_register_validation_test.exe_20261105_063510.log` line 113; hashes as E17 | **HW**, scoped to these captures. Other captures, including E19, have a non-zero shadow. **SOURCE:** `SETUP_TAS` does not write the shadow (section 3.6); this does not establish its general retention or progression |
| E21 | `IOCTL_AVB_TAS_ACTIVATE` (function 71, 80-byte request): commit, refusal of a second activation, refusal on a read-only handle, and wire gating seen by an independent I210 capture. Restoration inconclusive, because no register query existed | 2026-11-10 and 2026-11-11, revision 860afa7, Debug driver `734A677F…`, Release driver `98F5ADEE…`, I226-LM 0x125B REV_04 (#362, implementation status) | **HW**, `LOCAL_DIAGNOSTIC`, not acceptance evidence. Void as merge evidence after the move to 0x80C (#362) |

The paired non-zero cycle/shadow readout is identified in E19. It belongs to the existing E14 surviving-schedule case and is not independent proof of a running schedule or of retention after an initial activation. The query contract does not depend on either inference (section 12.4).

**Strict_End evidence sits outside the manual's window rule.** **DOC** 8.12.12 sets a minimum window. The 400 ns run and 5 µs large-frame stages were below it. They remain valid observations of hardware behaviour but are not a supported configuration (TAS-DR-05).

## 2. Conflicts that bind this design

The full report is in `DES-REVIEW-002`. Four conflicts directly constrain the design:

| Issue | Says | Effect here |
|---|---|---|
| `#91` ADR-PERF-002, accepted | Conflict with the miniport is mitigated by restricting the filter to PTP registers `0x0B600-0x0B700` | **All TAS registers lie outside that range.** Filter-side TSN programming needed an explicit decision: #362 ADR-TSN-002, **accepted 2026-11-14**, extends `#91` with one named exception for the TSN schedule registers and does not supersede it |
| `#50` | 256 gate entries, 1 ms margin, disable before reconfiguring, atomic spin-lock commit | Differs from evidence; amended by the `#50` Amendment Record and #350 to #361, not here |
| `#9` | Up to 64 entries, cycle 1 µs to 1 s | Differs from `#50` and **DOC** |
| `#45`, `#52` | All device ops inside spin locks | Section 9.4 |
| `#9`, `#206`, `#281` | Were closed as verified on a test with no traffic or wire check. Reopened 2026-11-05, `status:backlog` | Not used as evidence of gating. See DES-REVIEW-002 |

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

### 3.2 Operations and code allocation

**Allocated by #362 decision 1 (accepted).** New functions are allocated in the vendor range 0x800 to 0xFFF, under `FILE_DEVICE_PHYSICAL_NETCARD` and `METHOD_BUFFERED`. No new code is allocated below 0x800. The function allocations and access bits are accepted; the request layouts stay provisional.

| Operation | Code | State |
|---|---|---|
| `IOCTL_AVB_TAS_QUERY_HW` | `CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, 0x80B, METHOD_BUFFERED, FILE_READ_ACCESS)` = `0x0017602C` | Defined in `include/avb_ioctl.h` and pinned by TC-ABI-025, with TC-ABI-001 uniqueness (#434, f108ee8). **No handler:** the driver completes it with `STATUS_INVALID_DEVICE_REQUEST`. Layout: section 12, provisional |
| `IOCTL_AVB_TAS_ACTIVATE` | `CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, 0x80C, METHOD_BUFFERED, FILE_WRITE_ACCESS)` = `0x0017A030` | Not released. Exists on the #357 integration branch as function 71 (`0x0017811C`) with the 80-byte request. Moves to 0x80C in one coordinated #357 change, together with the versioned request and the #352 completion model; 0x80C is never released with the 80-byte request (#362) |
| `TAS_REPLACE`, `TAS_DISABLE_TO_LEGACY` | None | Not allocated: #362 decides no other new code. Return to legacy is not part of the initial implementation (#362 decision 2; #360 decided separately) |

Functions 39 and 43 are not reused (#362). The candidate numbers 71 to 74 of version 0.5.0 are withdrawn.

Why the operations are separate (unchanged rationale):

| Operation | Why it is needed | Could it merge? |
|---|---|---|
| Query | Read-only state in Debug and Release. Must be separable from mutation | **No.** A read-only operation stays distinct from mutating ones, and its access bit states that (#362 decision 3) |
| Activate | Needs a clean adapter, sets TSN mode, takes `pre_activation` | With REPLACE, technically yes |
| Replace | Needs a running schedule, never changes mode, requires a future grid-aligned target | With ACTIVATE, technically yes |
| Disable to legacy | Leaves TSN mode. Different struct, leaves residue | Not in the initial implementation (#362 decision 2) |

| Alternative | For | Against | Assessment |
|---|---|---|---|
| Four codes | One code per operation is the repo pattern; each maps to one requirement and RED/GREEN test; a wrong-state call cannot silently take the other path | More dispatcher entries | Recommended in 0.5.0. #362 allocated separate query and activation codes; the others are not allocated |
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
| `SETUP_TAS` while a schedule is managed or present | Refused, section 3.6. ABI unchanged; behaviour differs only in states a legacy-only client cannot reach |
| `GET_TAS_STATE` during a contract schedule | `armed` is derived, section 4.5 |
| `DISARM_TAS` during a contract schedule | Unchanged: `STATUS_NOT_SUPPORTED` on I226, writes nothing |
| Dispatcher | `src/device.c` allowlist gains entries at implementation. `test_ioctl_abi.c` already lists `IOCTL_AVB_TAS_QUERY_HW` (TC-ABI-001, TC-ABI-025) |
| Access | Decided for the new codes by #362 decision 3: 0x80B carries `FILE_READ_ACCESS`, 0x80C `FILE_WRITE_ACCESS`; both use `CTL_CODE`, not `_NDIS_CONTROL_CODE`. Enforcement for the released codes 26 and 66 and the control device's access policy are tracked separately (#362, #73) |

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

**Status of this block (2026-11-15):**

- The query structures of version 0.5.0 (`AVB_TAS_REGS` 112 bytes, `AVB_TAS_LIMITS` 56, `AVB_TAS_OP_RECORD` 48, `AVB_TAS_QUERY_REQUEST` 496) are replaced by the v1 query layout in section 12.2: 536 bytes. `AVB_TAS_QUEUE_WINDOW` and `AVB_TAS_SCHEDULE` are unchanged and shared.
- `AVB_TAS_OP_REQUEST` and `AVB_TAS_DISABLE_REQUEST` below are the 0.5.0 drafts. #362 records the current proposal as an operation request of 144 bytes and an operation record of 72 bytes, both provisional. They are settled in the coordinated #357 change that follows #352, not here. Disable is not part of the initial implementation (#362 decision 2).

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

/* AVB_TAS_REGS, AVB_TAS_LIMITS, AVB_TAS_OP_RECORD and the query request: see section 12.2 (v1, 536 bytes) */
```

### 3.6 Legacy `SETUP_TAS` interoperability

`SETUP_TAS` writes `TQAVCTRL` and `QBVCYCLET`, so left alone it could change the cycle of a live schedule, or resume a dormant one (E9). The ABI stays; a new check runs before any write. **This is a design proposal, governed by REQ-F-TAS-LEGACY-001: code 26 does not change until that requirement is accepted.** Accepted #362 keeps released codes' values and meanings and lists a refusal by `SETUP_TAS` on a record that is not clean as an alternative that "needs its own decision"; it records the remaining overwrite case as an accepted risk.

**`BaseT` is not a decision input.** It advances on its own and stays non-zero after an in-place disarm (E10), so it cannot show that a contract schedule exists. It is reported only.

| Input | Role | Why |
|---|---|---|
| Operation claim | Decides | Closes the window between check and write |
| Lifecycle record, `origin` and slots | Decides | The only proof a schedule is contract-managed |
| `STQT`, `ENDQT` against the family baseline | Decides | `SETUP_TAS` never writes them, so a legacy-only client cannot change them |
| `QBVCYCLET_S` | Decides | Same. A schedule always sets it |
| `TQAVCTRL` | Names the class only | A dormant schedule at mode 0 resumes if mode returns (E9) |
| `QBVCYCLET` | Not an input | `SETUP_TAS` itself writes it, so it cannot separate the cases |
| `BaseT` | Reported only | Above |
| Generation | Effect, not input | Legacy has no generation field. Success increments it, so a stale contract request fails |

Taken under the per-adapter claim, before any write, first match wins:

| # | Condition | Class | `SETUP_TAS` | NTSTATUS | Verdict if unexpected |
|---|---|---|---|---|---|
| 1 | Claim held by another operation | Operation in progress | Refused | `STATUS_DEVICE_BUSY` | `BLOCKED` |
| 2 | Record shows a contract schedule, in any status, including `INCONSISTENT` or `UNKNOWN` | Managed | Refused | `STATUS_INVALID_DEVICE_STATE` | `BLOCKED` |
| 3 | Filter not Running, or an input register unreadable | Unknown | Refused | `STATUS_DEVICE_NOT_READY` | `BLOCKED` |
| 4 | Windows non-baseline **and** shadow non-zero, no record | Schedule present, running or dormant | Refused | `STATUS_INVALID_DEVICE_STATE` | `BLOCKED` |
| 5 | Exactly one of those, no record | Indeterminate, perhaps a partial write | Refused | `STATUS_INVALID_DEVICE_STATE` | `BLOCKED` |
| 6 | TSN on, windows baseline, shadow 0 | Legacy footprint | **As today** | As today | As today |
| 7 | TSN off, windows baseline, shadow 0, `BaseT` non-zero | Residue after in-place disarm | **As today**; residue reported | As today | As today |
| 8 | TSN off, all baseline | Clean | **As today** | As today | As today |

Rows 6 to 8 are the only states a legacy-only client can reach, so existing clients see no change. Rows 4 and 5 exist for a schedule whose record was lost, as in E14. That is how one client is kept from overwriting another's schedule without trusting `BaseT`.

Family baseline for windows accepts `STQT = 0` with `ENDQT` at 0 (seen on hardware) or the 1 s default (**DOC**, **LINUX**), so a miniport using the default does not fall into row 5.

| Item | Definition |
|---|---|
| Detail | The legacy struct cannot carry it, and error completions drop the output buffer. The raw NTSTATUS is the signal |
| Visibility to the owner | `legacy_conflicts` in `TAS_QUERY` increments. `generation` does not change on refusal |
| Successful `SETUP_TAS` | `origin = SETUP_TAS`, `lifecycle = LEGACY_ARMED`, generation increments |
| Reverse direction | `TAS_ACTIVATE` and `TAS_REPLACE` on `origin = SETUP_TAS` fail `STATE_CONFLICT` |
| Exit from row 2 | The owner's contract operation, or a NIC cycle (a new context), verified by register reads. A schedule lost while its record remains (`DRIVER_ARMED_HW_OFF`) stays refused until then |
| Exit from rows 4, 5 | A NIC cycle verified by register reads. No legacy override exists: that would need an ABI change |

| Other path | Writes TSN registers today? | Treatment |
|---|---|---|
| `DISARM_TAS` | No on I226 | None needed |
| `SETUP_QAV` | No, recorded stub | Joins this check if it gains register writes |
| `SET_LAUNCH_TIME` | No, stub | Same |
| `SETUP_FP` | `FP_CONFIG` only | Not a schedule register; preemption with Qbv is refused separately |
| Debug `WRITE_REGISTER` | Yes, anything | Debug-only and not blocked. `TAS_QUERY` reports the mismatch |

**Effect on diagnostics:** `tas_gate_investigation --rearm` and `--after-rearm` restore windows and shadow before `SETUP_TAS`, so they reach row 7 and stay allowed. **Automated tests are unaffected.**

## 4. State model

The driver reports facts and activation status separately. They are read fresh from hardware on every `TAS_QUERY` and at the start of every mutating operation, and compared with the driver's record. No status comes from the record alone.

The v1 query encodes this model as section 12 states: numeric values in 12.2, status precedence and current/next attribution in 12.4. Where 12.4 is narrower than this section (for example, v1 never returns `INFERRED_ACTIVE` while link events are unobserved), 12.4 states what v1 does. This section is not changed by it.

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

State on 2026-11-15: the #357 integration branch enforces `AVB_TAS_MIN_LEAD_NS` = 100 ms (`include/avb_ioctl.h`, **SOURCE**). #362 lists 1.5 s as proposed and not established by measurement, and leaves the lead time open. The v1 query reports the lead the driver enforces, with `min_margin_state` `NOT_ESTABLISHED` (section 12.2). No value is decided here.

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

`DISABLE_TO_LEGACY` conflicts with guidance. **DOC** 7.5.2.1 says the mode is set at initialisation and not during operation, **LINUX** resets the adapter here, and the `disable_tas` source comment notes that the miniport owns `TQAVCTRL`. It therefore stays `NOT_IMPLEMENTED`: accepted #362 (decision 2) leaves it out of the initial implementation, and #360 is decided separately. In legacy mode the windows are ignored, yet a later `TQAVCTRL` write resumes the same schedule (E9), so leaving TSN mode does not erase it. Until EG-14, `ACTIVATE` after it is refused.

**Baseline** is a family constant from the SSOT, not a value captured at attach, because E14 shows attach can see a dirty adapter. **HW** read `ENDQT` as 0 at baseline where **DOC** lists a 1 s default, so the table must reflect hardware, and that difference is recorded.

The SSOT now generates these as **observed baselines** (intel-ethernet-regs 21fa1bc, consumed since #431): `I226_{TQAVCTRL,BASET_L,BASET_H,QBVCYCLET,QBVCYCLET_S,STQT,ENDQT,TXQCTL}_BASELINE`, with `QBVCYCLET` `0x3B9ACA00` and all others 0, `ENDQT` included (E18).

- They are observed values, not reset values.
- `I226_ENDQT_BASELINE_ALT` (`0x3B9ACA00`) is the documented value. It was not observed on this I226 and applies only under the windows-baseline rule of section 3.6.
- `DTXMXPKTSZ` has no baseline. It is always read and decoded, never substituted.
- A register that matches its baseline never proves a clean adapter, restoration or working TX. Those come from the test's own captures (#361).

PHC reset is reported separately from register restoration, so "registers restored, PHC reset" stays visible.

### 8.1 NDIS lifecycle

Current behaviour comes from `src/filter.c`. Proposed rules are for ADR-TSN-002.

| Event | What the driver does today | Proposed rule | Status |
|---|---|---|---|
| `FilterAttach` | New context, so `tas_armed` is 0; `AvbBringUpHardware` | Read TSN registers read-only. Non-baseline: `origin = FOUND_AT_ATTACH`. No write, no automatic cleanup | E14 is the case this catches |
| `FilterPause` | Stops timers because MMIO on powered-down hardware risks a bugcheck; no wait on NBLs | No TSN write. Mutating operations fail `NOT_READY` until Running. `TAS_QUERY` does **no MMIO** unless Running: `hw_mode = UNREADABLE`, slots `UNKNOWN` | Rule UNVERIFIED |
| Held frames during Pause | Passthrough, so NDIS completes the pause when sends return | **Unknown** whether closed gates or a `Strict_End` block delay pause. No automatic gate opening. EG-13 | UNVERIFIED; link to #328 unproven |
| `FilterRestart` | `AvbBringUpHardware`, and `init_ptp` reseeds `SYSTIM` | **Hazard:** the context survives, but the PHC can jump back under a live schedule, recreating the E14 hold with no NIC cycle. Reconcile before any operation: PHC moved back gives `PHC_RESET`, `INCONSISTENT`. Fix belongs to #348 | Hazard by source reading, EG-18 |
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
4. `lookup_result` is `FOUND` with the record, `NOT_FOUND`, or `NOT_REQUESTED`. Until the #352 store exists, the v1 query answers a non-zero id with `NOT_IMPLEMENTED`, never `NOT_FOUND` (section 12.2).

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
| Legacy conflict, on `SETUP_TAS` only | Per section 3.6, no record, `legacy_conflicts` increments | Unchanged, nothing written | `BLOCKED` |
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

All tests are **hardware** tests using dedicated IOCTLs only, with no `READ_REGISTER` or `WRITE_REGISTER`. Each discovers capability, proves its pre-state, owns setup and NIC-cycle cleanup, and proves restoration by `TAS_QUERY`, measured PHC and TX on the wire. They reuse `tests/hardware/tas_wire_observer.h`; no new harness is proposed. Existing tests, classifications, verdicts and runner rows are unchanged, and nothing adds a gate or global blocking. Runner rows for the new tests are added at implementation. **Every implemented operation needs Hardware RED and Hardware GREEN in Debug and Release.** A mock or offline test is supplementary and cannot establish GREEN.

### 11.1 Evidence status

**All 19 tests are design artifacts, and none has a recorded Hardware RED or GREEN.** A `LOCAL_DIAGNOSTIC` characterisation is not RED: it used Debug register tools and not the dedicated IOCTL under test. These stages describe evidence. They are not runner verdicts and replace none.

State on 2026-11-15: `tests/hardware/test_tas_activate.c` on the #357 integration branch carries case TC-TASAPI-004 (`--hold`, #369) and TC-TASAPI-005 (`--pass`, #370) for function 71. Its status assertion depends on this query and is not implemented. No testcase of #366, #367, #368 or #381 exists as code. They are implemented on their own branches, one at a time (section 12.7).

| Stage | Meaning | State of all 19 today |
|---|---|---|
| Designed | Specified here and in TEST issues #366 to #384 | **Yes** |
| RED pending | Test not yet run against the unimplemented or failing behaviour | **Yes** |
| Hardware RED verified | Test ran on hardware and failed for the expected reason, with log and driver SHA256 | **No** |
| GREEN pending | Implementation or passing run outstanding | **Yes** |
| Hardware GREEN verified | Test passed on real hardware, Debug and Release, with installed SHA256 | **No** |

Three tests also wait on other work: TC-TASAPI-005 on ADR-TSN-003, TC-TASAPI-011 on the separate #360 decision (#362 decision 2), and TC-TASAPI-019 on the EG-18 findings (#364).

### 11.2 Matrix

| Requirement, filed, not accepted | ADR (#362 accepted, #363 Proposed) | Design | Operation | TEST issue and case | Hardware behaviour to prove | Prior characterisation, not RED |
|---|---|---|---|---|---|---|
| #350 REQ-F-TAS-STATE-001 | #362 | 4, 12 | `TAS_QUERY` | #366 TC-TASAPI-001 | Clean adapter reads `NONE`, `OFF`, advisory `restoration` `COMPLETE`; read-only. The test compares its own captures; the advisory value is not proof of a clean adapter (12.5) | None |
| #351 REQ-F-TAS-VALIDATE-001 | #362 | 5 | All | #367 TC-TASAPI-002 | Non-TAS adapter `SKIP_UNSUPPORTED`; unbound handle refused; tiers reported | None |
| #351 REQ-F-TAS-VALIDATE-001 | #362 | 5.2 | ACTIVATE, REPLACE | #368 TC-TASAPI-003 | Each invalid request refused, `TAS_QUERY` and TX unchanged | None |
| #357 REQ-F-TAS-ACTIVATE-001 | #362, #363 | 6 | ACTIVATE, hold | #369 TC-TASAPI-004 | Frames held to first window after `BaseT`, then gated; `PENDING` then `INFERRED_ACTIVE` | E6, EG-02 |
| #357 REQ-F-TAS-ACTIVATE-001 | #363 | 6.2 | ACTIVATE, pass | #370 TC-TASAPI-005 | Frames flow until `BaseT`, then gated. **After ADR-TSN-003 only** | E6, EG-02 |
| #358 REQ-F-TAS-REPLACE-001 | #362 | 7 | REPLACE, same cycle | #371 TC-TASAPI-006 | Wire moves at target, not before | E1, E2 |
| #358 REQ-F-TAS-REPLACE-001 | #362 | 7 | REPLACE, changed cycle | #372 TC-TASAPI-007 | New cycle on the wire at target | E3 |
| #358 REQ-F-TAS-REPLACE-001 | #362 | 7.3 | REPLACE, too near | #373 TC-TASAPI-008 | Refused, schedule unchanged on the wire | E4 |
| #351 REQ-F-TAS-VALIDATE-001 | #362 | 5 | ACTIVATE, Strict_End | #374 TC-TASAPI-009 | Fitting frame passes; frame above declared size held, released on a wider window | E8 |
| #359 REQ-F-TAS-OPENALL-001 | #362 | 8 | REPLACE, `OPEN_ALL` | #375 TC-TASAPI-010 | Flat arrivals, no PHC step, `OPEN_ALL` and TSN mode reported, later REPLACE re-gates | E15 |
| #360 REQ-F-TAS-DISABLE-001 | #362 | 8 | `TAS_DISABLE_TO_LEGACY` | #376 TC-TASAPI-011 | Frames pass at once, PHC continuous, `RESIDUE`. **Only after #360 is decided** (#362 decision 2) | E10 |
| #361 REQ-F-TAS-RESTORE-001 | #362 | 8 | Restoration | #377 TC-TASAPI-012 | After NIC cycle: `COMPLETE`, TX on the wire, PHC reset reported | E13 |
| #355 REQ-F-TAS-LEGACY-001 | #362 | 3.6 | Interplay | #378 TC-TASAPI-013 | After `SETUP_TAS`: `LEGACY_ARMED`, contract operations refused | None |
| #355 REQ-F-TAS-LEGACY-001 | #362 | 3.6 | `SETUP_TAS` decision table | #382 TC-TASAPI-017 | Rows 1, 2, 6, 7, 8: refused with exact NTSTATUS while managed, wire unchanged, `legacy_conflicts` up, generation unchanged. Clean and residue cases behave as today | E9, E10 |
| #355 REQ-F-TAS-LEGACY-001 | #362 | 4.5 | `GET_TAS_STATE` | #383 TC-TASAPI-018 | `armed` follows the table in 4.5 for each reachable state | None |
| #352 REQ-F-TAS-ERROR-001 | #362 | 9.1, 9.2 | Failure detail | #379 TC-TASAPI-014 | Real refused call: record found on the same handle, `NOT_FOUND` on a second handle and on an I210-bound handle | None |
| #352 REQ-F-TAS-ERROR-001 | #362 | 9.3 | Concurrency | #380 TC-TASAPI-015 | Two threads race REPLACE: one wins, the other `BUSY` or `STATE_CONFLICT`, wire shows one schedule | None |
| #350 REQ-F-TAS-STATE-001 | #362 | 4.1 | All | #381 TC-TASAPI-016 | `HARDWARE_VERIFIED` never returned in any test, with PHC past `BaseT` | None |
| #356 REQ-F-TAS-NDIS-001 | #362 | 8.1 | Pause, Restart | #384 TC-TASAPI-019 | Mutating refused while Paused, no MMIO in `TAS_QUERY`, reconcile after Restart reports what hardware shows | None. Depends on EG-18 |
| #353 REQ-NF-TAS-OBSERVE-001 | #362 | 12 | `TAS_QUERY` | #366 TC-TASAPI-001; TC-TASAPI-004, 006 in Release, not linked to #353 on GitHub (DES-REVIEW-002, D6) | Same results, Release driver SHA256 recorded. **Ring pointers are not in v1** (12.6): #353 and #392 stay open for them | None |
| #354 REQ-NF-TAS-BUFFER-001 | #362 | EG-03 | Buffer configuration | No TEST issue. To be defined after EG-03 | Buffer values reported, behaviour at sustained load | EG-03 |

**Mock-only paths stay UNVERIFIED.** These cannot be driven on hardware by dedicated IOCTLs today: `READBACK_MISMATCH`, `MMIO_FAILED`, `ROLLBACK_*`, the race in `ACTIVATION_AMBIGUOUS`, `HW_ARMED_NO_RECORD`, `BASE_FAR_AHEAD`, and rows 3, 4 and 5 of the `SETUP_TAS` table. Their mocks must reproduce E14's recorded register values, cannot establish HIL GREEN, and must be labelled UNVERIFIED in test output and in the issue. `HW_ARMED_NO_RECORD` gains HIL evidence only if EG-04 finds a trigger.

## 12. Release observability

**Need:** in Release, with no `READ_REGISTER` or `WRITE_REGISTER`, a test must see mode, cycle, `BaseT`, windows, flags, ring pointers and the PHC, close enough in time to reconcile them.

| Option | Assessment |
|---|---|
| Extend `QUERY_HW_SNAPSHOT` | **Rejected.** 280 bytes pinned by `TC-ABI-022` and hard-coded in `Run-Tests-CI.ps1`; has only `TQAVCTRL` |
| New snapshot version | **Rejected.** Same callers, and every test would interpret state itself |
| Extend `GET_TAS_STATE` | **Rejected.** Changes an existing ABI, and it is documented as driver-level only |
| `READ_REGISTER` in Release | **Rejected.** `#122` removes it from Release. Generic `WRITE_REGISTER` stays Debug-only |
| **`TAS_QUERY`** | **Chosen.** Allocated as `IOCTL_AVB_TAS_QUERY_HW` by #362 decision 1 (accepted). One purpose, fixed register set, no caller-supplied offsets, read-only, bound handles only. Layout: 12.2, provisional |

Register offsets come from `intel-ethernet-regs` through a new device op, per `#92`, not literals. Nothing in the set has a read side effect; `TXSTMP` registers are excluded because reading them changes state. `revision_id` needs to reach `TAS_QUERY`, and implementation must confirm how the driver obtains it. Already available in Release: `GET_STATISTICS` (outstanding sends), `GET_CLOCK_CONFIG` (PHC), the I210 observer.

### 12.0 Status of the v1 query contract

**Source of truth:** the "Query v1 contract" section of #365. Sections 12.0 to 12.9 mirror it; where they differ, #365 governs.

This is the frozen v1 contract of 2026-11-15. It completes the r5 proposal of 2026-11-11 for the query only, and keeps r5's layout and offsets.

| Part | State |
|---|---|
| Code, access, `METHOD_BUFFERED`, versioned, read-only, includes `DTXMXPKTSZ` | **Accepted** (#362 decision 1) |
| Layout, 536 bytes, and every value below | **Proposed.** #362 keeps the size provisional until this contract is accepted under #365 |
| Sizes and offsets | **STATIC** verified on x86 and x64 (12.8) |
| Behaviour on hardware | **UNVERIFIED.** No handler exists and the query has never run (12.7) |

Q's decisions of 2026-11-11 are cited as:

- **3A:** snapshot rules;
- **3B:** advisory restoration;
- **3C:** the v1 register set.

Two acquisition paths are cited as dependencies:

- **K8:** link events and link speed;
- **K9:** the PCI revision.

**C6** is the per-adapter retention of commit timing, independent of the #352 store.

### 12.1 Request and validation

Inputs are only `version`, `size` and `lookup_client_op_id`. Every other byte is output and is overwritten. The checks run in this order. On any error, `IoStatus.Information` is 0 and nothing is written.

| # | Condition | Raw NTSTATUS | Basis |
|---|---|---|---|
| 0 | Handle opened without read access | `STATUS_ACCESS_DENIED`, from the I/O manager | #362 decision 3 |
| 0 | Driver without a handler for the code (today) | `STATUS_INVALID_DEVICE_REQUEST` | `src/device.c` default |
| 1 | Open instance not bound by `OPEN_ADAPTER` or `OPEN_ADAPTER_BY_LUID`. Refused in `src/device.c` before the fallback adapter search, lazy initialization, context use or MMIO | `STATUS_INVALID_DEVICE_STATE` | #353 ("the query is refused"), #367 ("exact NTSTATUS"); same gate as code 70 (`src/device.c`) |
| 2 | System buffer missing, or input or output length below 536 | `STATUS_BUFFER_TOO_SMALL` | 3.4 |
| 3 | `version` not 1 | `STATUS_REVISION_MISMATCH` | 3.4 |
| 4 | `size` not 536 | `STATUS_INVALID_PARAMETER` | 3.4 |
| 5 | The bound adapter's discovered capabilities lack `INTEL_CAP_TSN_TAS` | `STATUS_NOT_SUPPORTED` | 5.1, 9.5 ("only if the capability is known absent") |
| 5 | Capability present, but the family's device ops provide no query register map. Today that is the I225: `i225_ops` is not registered (`devices/intel_device_registry.c`), and `gen/i225_regs.h` defines no v1 register. This follows from the implementation state, not from the I225 activation restriction | `STATUS_NOT_IMPLEMENTED` | 5.1 `NOT_IMPLEMENTED` |
| 5 | Map present but missing a v1 register | Served. That register is invalid with `SNAP_RSN_REG_UNMAPPED` | |
| 6 | Otherwise | `STATUS_SUCCESS`, `Information` 536 | |

- **No `STATUS_DEVICE_NOT_READY`:** the query joins the dispatch prologue's observation allow-list, as `GET_HW_STATE` does (`src/avb_integration_fixed.c`). It never initializes the adapter.
- **No `ProbeForWrite`** on the system buffer.
- **Never blocking:** while a mutating operation holds the per-adapter claim, `flags` carries `OP_IN_PROGRESS` and the slots are not advanced from a mid-sequence read (#362 concurrency).
- **Classification:** clients classify by the raw NTSTATUS. `STATUS_NOT_IMPLEMENTED` and `STATUS_INVALID_DEVICE_REQUEST` both map to Win32 1.
- **Support** is decided from discovered state, never from the device model. Tests discover it from the reply and the `GET_HW_STATE` capabilities.

### 12.2 Layout and values

Universal rules:

| Rule | Statement |
|---|---|
| U1 | Every enumerated out field uses 0 = not reported. A successful reply never carries 0 in `hw_read`, `hw_mode`, `current_status`, `next_status`, `restoration`, `lookup_result` or `dtx_max_frame_state` |
| U2 | A numeric field where 0 is meaningful is valid only when its validity indicator says so |
| U3 | No hardware field is filled from the driver record, and no record field from hardware (#362 state authority) |
| U4 | Times are PHC nanoseconds. Explicit padding only; the x86 and x64 layouts are identical |

```c
typedef struct AVB_TAS_REGS {                  /* hardware, decision 3C */
    avb_u32 tqavctrl, baset_l, baset_h, qbvcyclet, qbvcyclet_s;   /* read_failed bits 0-4 */
    avb_u32 stqt[4], endqt[4], txqctl[4];                         /* bits 5-8, 9-12, 13-16 */
    avb_u32 dtxmxpktsz;                                           /* bit 17 */
    avb_u32 read_failed;  /* bit set = value invalid (read failed or not attempted), value then 0 */
    avb_u32 consistency;  /* AVB_TAS_CONS_BASET_TORN 0x1 */
} AVB_TAS_REGS;                                                   /* 80 */
typedef struct AVB_TAS_LIMITS {
    avb_u32 queues, windows_per_queue, hw_cycle_min_ns, hw_cycle_max_ns;
    avb_u64 min_margin_ns;
    avb_u32 min_margin_state, tier_hold, tier_pass, tier_strict_end, tier_strict_cycle, tier_per_queue;
} AVB_TAS_LIMITS;                                                 /* 48 */
typedef struct AVB_TAS_OP_RECORD {             /* #352 record wire format; production and storage are #352's */
    avb_u64 client_op_id, net_luid, context_epoch, generation_after, phc_before_commit_ns, phc_after_commit_ns;
    avb_u32 operation, result, ntstatus, stage, hw_effect, op_flags;
} AVB_TAS_OP_RECORD;                                              /* 72 */
typedef struct AVB_TAS_COMMIT_INFO {           /* C6 */
    avb_u64 phc_before_commit_ns, phc_after_commit_ns; avb_u32 state, reserved;
} AVB_TAS_COMMIT_INFO;                                            /* 24 */

typedef struct AVB_TAS_QUERY_HW_REQUEST {
    avb_u32 version;              /*   0 in  1 */
    avb_u32 size;                 /*   4 in  536 */
    avb_u64 lookup_client_op_id;  /*   8 in  0 = no lookup */
    avb_u64 net_luid;             /*  16 out */
    avb_u16 vendor_id, device_id; /*  24 out */
    avb_u16 revision_id;          /*  28 out */
    avb_u16 revision_state;       /*  30 out */
    avb_u32 hw_read, hw_mode, origin, gate_policy;               /*  32 out */
    avb_u32 current_status, current_reasons;                     /*  48 out */
    avb_u32 next_status, next_reasons;                           /*  56 out */
    avb_u32 snapshot_reasons, restoration, flags, lookup_result; /*  64 out */
    avb_u32 link_speed_mbps, legacy_conflicts;                   /*  80 out */
    avb_u64 context_epoch, generation;                           /*  88 out */
    avb_u64 phc_before_ns, phc_after_ns;                         /* 104 out */
    avb_u32 dtx_max_frame_bytes, dtx_max_frame_state;            /* 120 out */
    AVB_TAS_LIMITS limits;                                       /* 128 out */
    AVB_TAS_SCHEDULE current;                                    /* 176 out, record */
    AVB_TAS_SCHEDULE next;                                       /* 256 out, record */
    AVB_TAS_REGS regs;                                           /* 336 out, hardware */
    AVB_TAS_OP_RECORD lookup;                                    /* 416 out */
    AVB_TAS_COMMIT_INFO current_commit;                          /* 488 out */
    AVB_TAS_COMMIT_INFO next_commit;                             /* 512 out */
} AVB_TAS_QUERY_HW_REQUEST;                                      /* 536 */
```

Every out field is valid only on `STATUS_SUCCESS`.

| Off | Field | Meaning | Valid when | Not reported or unavailable | Source or dependency |
|---|---|---|---|---|---|
| 0 | `version`, `size` | Echo: 1, 536 | Always | | #350 |
| 16 | `net_luid` | Bound adapter | Always, never 0 | | Context |
| 24 | `vendor_id`, `device_id` | Identity, for corroboration only | Always | | Context |
| 28 | `revision_id` | PCI revision | `revision_state` = `VALID` (1) | 0, with `revision_state` 0 | **K9**: the acquisition path is designed only after a hardware observation |
| 32 | `hw_read` | `READ` 1, `NOT_RUNNING` 2, `PARTIAL` 3, `NO_MMIO` 4 | Always | | #350, 3A |
| 36 | `hw_mode` | From `TQAVCTRL.TRANSMIT_MODE`: `OFF` 1, `TSN` 2, `UNREADABLE` 3 | Always | `UNREADABLE` when `TQAVCTRL` is invalid | #350 |
| 40 | `origin` | Record: `NONE` 1, `SETUP_TAS` 2, `CONTRACT` 3, `FOUND_AT_ATTACH` 4 | Not 0 | 0 until the record tracks origin; today `tas_armed` does not separate code 26 from the activation | Per-adapter record; `FOUND_AT_ATTACH` needs the attach-time read (#362) |
| 44 | `gate_policy` | Record: `NONE` 1, `SCHEDULED` 2, `OPEN_ALL` 3 | Not 0 | 0, as for `origin` | `OPEN_ALL` only through #359 |
| 48 | `current_status`, `current_reasons` | 4.1 status: `NONE` 1, `PROGRAMMED` 2, `PENDING` 3, `INFERRED_ACTIVE` 4, `HARDWARE_VERIFIED` 5 (never returned), `INCONSISTENT` 6, `UNKNOWN` 7; slot reasons below | Always | `UNKNOWN` with its reason (12.4) | #350, C6 |
| 56 | `next_status`, `next_reasons` | As above, by 12.4 | Always | Never `NONE` without observed hardware. v1 has no next record (#358) | #350; #358 for a next record |
| 64 | `snapshot_reasons` | Bits below | Always | | #350 |
| 68 | `restoration` | Advisory, 12.5 | Always | `NO_SSOT_BASELINE`, `UNKNOWN` | SSOT observed baselines |
| 72 | `flags` | `OP_IN_PROGRESS` 0x1; `UNVERIFIED_CONFIG_ACTIVE` 0x2, never set in v1 (no opt-in exists); `PHC_RESET_SEEN` 0x4, sticky in the context | Always | | Claim; PHC tracking |
| 76 | `lookup_result` | `NOT_REQUESTED` 1, `FOUND` 2, `NOT_FOUND` 3, `NOT_IMPLEMENTED` 4 | Always | v1: id 0 gives 1; a non-zero id gives 4, never 3 | #352 store |
| 80 | `link_speed_mbps` | Speed used for the window minimum | Not 0 | 0 = unknown | **K8** (#351) |
| 84 | `legacy_conflicts` | Refused `SETUP_TAS` count | Not `0xFFFFFFFF` | `0xFFFFFFFF` until #355 | #355 |
| 88 | `context_epoch`, `generation` | #352 | Not 0 | 0 = not reported | #352 |
| 104 | `phc_before_ns`, `phc_after_ns` | Bracket around the register block | The matching `PHC_*_INVALID` bit clear | 0, with the bit set | #350, 3A |
| 120 | `dtx_max_frame_bytes`, `dtx_max_frame_state` | `DTXMXPKTSZ` decoded with the SSOT mask and `_SCALE` (64); the raw value stays in `regs`. State: `DECODED` 1, `READ_FAILED` 2, `NO_SSOT_SCALE` 3, `NOT_READ` 4 | Bytes valid only when `DECODED` (0 is then a decoded value) | Bytes 0 | #350, #362 decision 1 |
| 128 | `limits.queues`, `windows_per_queue` | 4; 1 (TAS-DR-03) | Not 0 | | Constants |
| 136 | `limits.hw_cycle_min_ns`, `hw_cycle_max_ns` | From SSOT field definitions only | Not 0 | 0: the SSOT cycle field carries no units | SSOT |
| 144 | `limits.min_margin_ns`, `min_margin_state` | The lead the driver enforces now (100 ms); state `NOT_ESTABLISHED` 1 (`ESTABLISHED` 2 not used) | State not 0 | | 7.2 |
| 156 | `limits.tier_*` | Tier values of 5.1: `HW_UNSUPPORTED` 1, `NOT_IMPLEMENTED` 2, `IMPLEMENTED_UNVERIFIED` 3, `IMPLEMENTED_VERIFIED` 4 | Not 0 | 0 until the #351 table exists. `tier_pass` = 2 under #363 option C | **#351** |
| 176 | `current` | Record of the committed schedule | `current_commit.state` is `RETAINED`, `PHC_DISCONTINUOUS` or `HW_MISMATCH` | All 0 | C6, fed by the activation (#357) |
| 256 | `next` | As above, with `next_commit` | Same | v1: all 0 | #358 |
| 336 | `regs` | Raw v1 register set (12.3) | Per `read_failed` bit | Bits set; values 0. `NOT_RUNNING` and `NO_MMIO` set bits 0-17 | #350, 3A |
| 416 | `lookup` | #352 record | `lookup_result` = `FOUND` | All 0. v1 is never `FOUND` | Production and storage are #352's |
| 488 | `current_commit` | Commit bracket and state: `NONE` 1, `RETAINED` 2, `PHC_DISCONTINUOUS` 3, `HW_MISMATCH` 4, `LOST` 5 | State not 0 | Bracket 0 when `NONE` or `LOST` | C6 storage under #350, fed by #357 |
| 512 | `next_commit` | As above | | v1: `NONE` | #358 |

| Snapshot reason | Bit | Slot reason | Bit |
|---|---|---|---|
| `NOT_RUNNING` | 0x001 | `HW_ARMED_NO_RECORD` | 0x0001 |
| reserved, never set | 0x002 | `DRIVER_ARMED_HW_OFF` | 0x0002 |
| `READ_FAILED` | 0x004 | `BASE_FAR_AHEAD` | 0x0004 |
| `READ_INCONSISTENT` | 0x008 | `WINDOW_MISMATCH` | 0x0008 |
| `PHC_DISCONTINUITY` (backward move in this context; forward jumps undetected, #348) | 0x010 | `CYCLE_MISMATCH` | 0x0010 |
| `EVENT_UNRECONCILED` | 0x020 | `TXQCTL_MISMATCH` | 0x0020 |
| `NO_MMIO` | 0x040 | `LINK_SPEED_CHANGED` | 0x0040 |
| `PHC_BEFORE_INVALID` | 0x080 | `PHC_RESET_SINCE_COMMIT` | 0x0080 |
| `PHC_AFTER_INVALID` | 0x100 | `ACTIVATION_AMBIGUOUS` | 0x0100 |
| `REG_UNMAPPED` | 0x200 | `COMMIT_INFO_LOST` | 0x0200 |
| | | `TRANSITION_DURING_SNAPSHOT` | 0x0400 |
| | | `LINK_EVENTS_UNOBSERVED` | 0x0800 |
| | | `SLOT_UNATTRIBUTED` | 0x1000 |

A semantic change to any field raises `AVB_TAS_QUERY_VERSION` (3.4).

### 12.3 Snapshot rules and register set (decisions 3A, 3C)

| Rule | Statement |
|---|---|
| Offsets | From `intel-ethernet-regs/gen/i226_regs.h` only, decoded through the generated helpers. No `READ_REGISTER`, no driver constants |
| Pass | One bounded pass: `phc_before_ns`, the register block in a fixed order, `phc_after_ns`. The block is a sequence of reads inside that bracket, never an atomic state. Temporal progression compares the retained target `current.base_time_ns` with the bracket, never with one PHC value or the live `regs.baset_*` readback |
| Tear check | Only `BASET` can tear. It is read H, L, H; a difference is repeated once. If it persists: `BASET_TORN` and `READ_INCONSISTENT` |
| Read failure | The register's `read_failed` bit is set and its value is 0, with `hw_read` `PARTIAL`. Never filled from the record |
| Not Running | No MMIO: `hw_read` `NOT_RUNNING`, hardware fields 0, `hw_mode` `UNREADABLE`, slots `UNKNOWN`. Record fields are still reported as record |
| No MMIO access | BAR not mapped or `INTEL_CAP_MMIO` clear: `hw_read` `NO_MMIO`. Nothing is read and no initialization is started |
| Writes | None. No claim is taken, and the query never blocks |
| v1 set | `TQAVCTRL`, `BASET_L`, `BASET_H`, `QBVCYCLET`, `QBVCYCLET_S`, `STQT0-3`, `ENDQT0-3`, `TXQCTL0-3`, `DTXMXPKTSZ` |
| Excluded from v1 | `TDH`, `TDT`, `TXPBSIZE`, `TCTL`. They go to a separately versioned diagnostic extension (12.6) |

### 12.4 Status precedence and current/next attribution

Both slots use the same precedence. The first match wins, and the reasons masks carry every applicable bit.

| Step | Condition | Status |
|---|---|---|
| S1 | The slot's inputs are not observable: `hw_read` `NOT_RUNNING` or `NO_MMIO`; an input register invalid (`read_failed`, `REG_UNMAPPED`); `READ_INCONSISTENT`; `PHC_BEFORE_INVALID` or `PHC_AFTER_INVALID`; `EVENT_UNRECONCILED`; `OP_IN_PROGRESS` | `UNKNOWN` |
| S2 | Any `INCONSISTENT` slot reason, including `PHC_RESET_SINCE_COMMIT` | `INCONSISTENT` |
| S3 | Any `UNKNOWN` slot reason: `COMMIT_INFO_LOST`, `ACTIVATION_AMBIGUOUS`, `TRANSITION_DURING_SNAPSHOT`, `LINK_EVENTS_UNOBSERVED` | `UNKNOWN` |
| S4 | 4.1 progression | `NONE`, `PROGRAMMED`, `PENDING`, `INFERRED_ACTIVE` |

`NONE` exists only in S4, so it always rests on observed hardware.

**Current versus next.** Attribution comes from the record, the retained commit timing and the field-specific reconciliation in the table below.

- It never rests on one register, and never on a comparison of every register.
- Only fields with a contract-defined expected value are compared, through their SSOT masks.
- Raw values always stay in `regs`, and S1 applies first.
- Information that cannot be established gives `UNKNOWN` with its reason. `INCONSISTENT` is used only for a contradiction the table defines.

| Group | Compared field | Expected value from | A contradiction gives | Untouched bits, autonomous change |
|---|---|---|---|---|
| `TQAVCTRL` | `TRANSMIT_MODE` only (`FUTSCDDIS` only for a PASS record; none in v1) | Record (6.1; mode bits only, #362 decision 4). No record: `OFF` (3.6, "names the class") | Record and mode off: `DRIVER_ARMED_HW_OFF`. No record and TSN on: `HW_ARMED_NO_RECORD` | Other bits are not compared, because no operation contract defines their expected value. That includes `ENHANCED_QAV`, which the activation source sets. Their relevance and retention are UNVERIFIED; they are reported raw |
| `STQT0-3`, `ENDQT0-3` | `START_TIME`, `END_TIME` | Record windows. No record: the family baseline (`STQT` 0; `ENDQT` 0, or `_ALT` only under 3.6) | `WINDOW_MISMATCH`; with no record, `HW_ARMED_NO_RECORD` | Retention after the write is UNVERIFIED |
| `QBVCYCLET` | `QBV_CYCLE_TIME`, with a record only | Record cycle. No record: not an input (3.6). The observed baseline is not used here | `CYCLE_MISMATCH`, including a `SETUP_TAS` overwrite (4.3) | |
| `QBVCYCLET_S` | `SHADOW_QBVCYCLE`, **without a record only** | No record: the observed baseline 0 (3.6, "shadow non-zero") | No record: `HW_ARMED_NO_RECORD` | **Not compared under a record:** retention after the commit is not established for every device |
| `BASET_L`, `BASET_H` | None | None: reported only (3.6) | | Advances on its own (E3, E10) and shows the old value on a running schedule (E5). Used only for `BASE_FAR_AHEAD` against the PHC bracket (4.3) and for `BASET_TORN`. Never compared with the target |
| `TXQCTL0-3` | Strict fields, only if the record carries them | The v1 activation request carries none, so nothing is compared | `TXQCTL_MISMATCH`, only then | A field with no expected value is not a disagreement |
| `DTXMXPKTSZ` | None | None | | Read and decoded only. No baseline, never substituted |

| Case | Record | Hardware, per the table | `current` | `next` |
|---|---|---|---|---|
| R1 | Present | No compared field contradicts | S2 to S4 | `NONE` |
| R2 | Present | A compared field contradicts | `INCONSISTENT` (`*_MISMATCH` or `DRIVER_ARMED_HW_OFF`) | `UNKNOWN`, `SLOT_UNATTRIBUTED` |
| R3 | None | TSN on, or windows off their baseline, or shadow non-zero (3.6 rows 4 to 6; 4.3) | `INCONSISTENT`, `HW_ARMED_NO_RECORD` (#350) | `UNKNOWN`, `SLOT_UNATTRIBUTED` |
| R4 | None | TSN off, windows at baseline, shadow 0. `BaseT` may be non-zero (residue, 3.6 row 7) | `NONE` | `NONE` |

- **`next` is the tracked lifecycle slot.** A next schedule exists there only with a next record (`TAS_REPLACE`, #358; none in v1).
- **`next` `NONE`** means no tracked next schedule and no unexplained compared state. It does not prove that the hardware holds no unrecorded state; such state surfaces through R2 and R3.
- **Regression case.** One initial activation, no replacement, a retained current record, and `QBVCYCLET_S` non-zero as written. Result: R1, so `next` `NONE` with `next_reasons` 0, no mismatch from the shadow, and `regs.qbvcyclet_s` reported as read.

S4 detail:

- **`PENDING`** needs all of the following:
  - `commit.state` = `RETAINED`;
  - the commit classified in time (7.3);
  - `phc_after_ns` valid and below the retained target `current.base_time_ns`;
  - mode TSN;
  - no compared field contradicting the record.
- **`INFERRED_ACTIVE`** needs every 4.1 condition. Otherwise the slot is `UNKNOWN` with its reason:

  | Missing condition | Reason |
  |---|---|
  | Commit timing missing | `COMMIT_INFO_LOST` |
  | Commit not classified in time | `ACTIVATION_AMBIGUOUS` |
  | Retained target `current.base_time_ns` inside the PHC bracket | `TRANSITION_DURING_SNAPSHOT` |
  | "No link or power event since commit" not established | `LINK_EVENTS_UNOBSERVED` |

  **While K8 is unverified, v1 never returns `INFERRED_ACTIVE`.**
- **Never:** readback alone never moves a slot past `PROGRAMMED`. `BaseT` alone is never evidence. `HARDWARE_VERIFIED` is never returned (#381).

### 12.5 Restoration (advisory, decision 3B)

`restoration` is computed only from the SSOT observed baselines (section 8). The driver keeps no table of its own.

It never establishes a clean adapter, restoration or working TX. #361 and the tests decide those from the test's own pre-test and post-cleanup captures.

| Value | Meaning |
|---|---|
| `COMPLETE` 1 | TSN off, and every restoration-set register equals its observed baseline. `ENDQT`: 0, or `_ALT` only under 3.6 |
| `PARTIAL_OPEN_ALL` 2 | TSN on, and every window covers the cycle |
| `PARTIAL_RESIDUE` 3 | TSN off, and some restoration-set register differs |
| `NONE` 4 | TSN on, otherwise |
| `UNKNOWN` 5 | An input register is invalid |
| `NO_SSOT_BASELINE` 6 | The family's SSOT has no observed baselines |

The restoration set is the v1 register set without `DTXMXPKTSZ`, so `BASET_L` and `BASET_H` are included. PHC continuity stays a separate fact (#361).

### 12.6 Evidence boundary and open coverage

What each row of 12.4 rests on. None of it is hardware GREEN for #350. Every comparison stays UNVERIFIED on hardware until the query's own replies are captured under #366 and #369.

| Group | **SOURCE**: what the #357 activation writes (`devices/i226_tas_activate.h`) | **HW**: observed, under section 1 conditions | UNVERIFIED |
|---|---|---|---|
| `TQAVCTRL` | Read-modify-write that sets `TRANSMIT_MODE` and `ENHANCED_QAV` | Gating under `SETUP_TAS`-started schedules (E1 to E7). E21 for the activation itself. 6.1: the designed sequence has never run | Retention after the activation; meaning of the uncompared bits |
| `STQT`, `ENDQT` | `START_TIME`, `END_TIME` per queue | `ENDQT` 0 at the baseline (E18). E10: written back during one in-place disarm (1 run, 3 cycles) | Retention after the activation; any other device |
| `QBVCYCLET`, `QBVCYCLET_S` | Both cycle fields, shadow first | E3: shadow, cycle and future `BaseT` applied a changed cycle (1 run). E19: shadow non-zero in a surviving schedule (1 case). E20: shadow 0 under `SETUP_TAS` | Shadow retention after the commit on every device |
| `BASET_L`, `BASET_H` | `BASET_H` raw seconds, then the `BASE_TIME_L` field | E3 stepped at the new cycle (1 run). E5: old value on a running schedule. E10: kept advancing after an in-place disarm (1 run) | General progression behaviour |
| `TXQCTL` | Not written by the activation | E7: `Strict_End` took effect by the next window (3 runs, `SETUP_TAS` path) | Anything about `TXQCTL` under the activation |
| `DTXMXPKTSZ` | Never written; read only | E17: raw `0x00000098` in every reading, with Debug register reads | The query's decoded value on the HIL adapter (#366); any other device or configuration |

**Requirement coverage that stays open:**

| Item | Why it stays open |
|---|---|
| Ring pointers | #353, #392 and section 12 ("Need") require ring pointers; v1 excludes `TDH` and `TDT` (3C), and code 70 has none either. #366 can verify every other #353 field. #353 and #392 are **not fully verified** until the separately versioned diagnostic extension exists. Neither requirement is rewritten here |
| #351 dependencies | Tiers (0 until the #351 table), `revision_id` (K9), `link_speed_mbps` (K8). Their interim values are truthful reporting, **not acceptance evidence**. The tier part of #367 stays not GREEN until #351 |
| #369 status part | The status progression (`PENDING`, then `INFERRED_ACTIVE`) cannot pass while K8 is open. #369 keeps its status and its independent wire-gating assertions and reports the status part not GREEN |
| #350 | Stays incomplete while its `PENDING` and `INFERRED_ACTIVE` evidence (through #369) and the K8 and C6 evidence are missing |

A GREEN clean-query subset (#366, #381, the unbound part of #367) completes neither #350 nor #353 nor #392.

### 12.7 Verification and order of work

Existing TEST issues are reused. No new TEST issue is created for the query.

| TEST | Verifies | What it checks against the query | State on 2026-11-15 |
|---|---|---|---|
| #366 TC-TASAPI-001 | #350, #353 | Clean adapter. Bound by LUID; two replies agree on every schedule register; PHC and statistics consistent; the I210 sees TX unaffected. `NONE`, `OFF`, advisory `COMPLETE`. `DTXMXPKTSZ` raw and decoded, recorded as a measurement | Not written. Hardware RED planned: the driver without a handler answers `STATUS_INVALID_DEVICE_REQUEST` |
| #381 TC-TASAPI-016 | #350 | `HARDWARE_VERIFIED` never returned in a valid, executed query. An unsupported or unexecuted query never counts as passing | Not written |
| #367 TC-TASAPI-002 | #351 | Unbound handle refused with exactly `STATUS_INVALID_DEVICE_STATE` and no lazy initialization. `SKIP_UNSUPPORTED` or `SKIP_NOT_IMPLEMENTED` from discovered capability. Tiers per #351 | Not written. The tier part waits for #351 |
| #368 TC-TASAPI-003 | #351 | The query before and after each invalid request: registers and TX unchanged | Not written. RED only if the assertion fails before the handler; no manufactured RED |
| #369 TC-TASAPI-004 | #357, #350 | Existing `test_tas_activate.c --hold`: status progression and the independent wire assertions. After the activation: `next` `NONE`, `next_reasons` 0, no `CYCLE_MISMATCH`. `regs.qbvcyclet_s` recorded as an observation; the test never asserts that a device retains it | Status sub-check `SKIP_NOT_IMPLEMENTED` today |

#377 stays with #361. #379 and #380 stay with #352.

**Order of work.** One branch at a time, each merged into the #357 integration branch before the next starts:

1. This design change, owned by #365.
2. #350: the structures and values of 12.2 in `include/avb_ioctl.h`, with ABI pins and negative controls. No handler.
3. #366, #381 and #367, each on its own branch, with hardware RED against the driver without a handler.
4. #350: the handler, with offline mocks that include R1 to R4 and the regression case.
5. #368, then #369.

No issue closes on the merge of code or of a RED test.

**How the tests run:**

- Hardware TDD runs one compiled testcase at a time, in Debug, through `tools/test/Run-Tests-Elevated.ps1 -TestName <entry> -CaptureDbgView`. Release runs only when the Release driver is installed.
- Acceptance of each requirement still needs its own hardware evidence in Debug and Release (#362 decision 6), with full provenance.
- Offline and mock results are supplementary and never establish GREEN.

### 12.8 Layout verification (STATIC)

This section establishes sizes and offsets only. It establishes no behaviour.

The C_ASSERTs of the proposed header (SHA256 `93f349a4…`) were compiled on 2026-11-15, outside the repository, with MSVC `cl /W4 /WX /c` against Windows SDK 10.0.26100.0 headers, for x64 and x86.

| Check | x64 | x86 |
|---|---|---|
| Positive: all sizes (16, 80, 80, 48, 72, 24, 536), 15 offsets, `IOCTL_AVB_TAS_QUERY_HW` = `0x0017602C` | Compiles, exit 0 | Compiles, exit 0 |
| Negative: size 528 | C2118, exit 2 | C2118, exit 2 |
| Negative: `lookup` at 408 | C2118, exit 2 | C2118, exit 2 |
| Negative: `regs` at 344 | C2118, exit 2 | C2118, exit 2 |
| Negative: `revision_state` at 32 | C2118, exit 2 | C2118, exit 2 |

Each negative control fails on its own assertion line, so the positive build's assertions are live. The ABI pins enter the repository with the header change (step 2 in 12.7).

### 12.9 Not approved through this layout

Their bytes are fixed and their "not reported" encodings are defined above. Their meaning is decided elsewhere:

| Item | Decided by |
|---|---|
| Production, retention and storage of the operation record, `context_epoch` and `generation` | #352 |
| Tier values, including `tier_hold` | #351 |
| The lead time | EG-01, and the coordinated #357 change |
| The opt-in for an unverified configuration | #363, #351 |
| The activation request and the record shape of `current` and `next` | The coordinated #357 change |
| `origin`, `gate_policy`, `legacy_conflicts` | #355, #359 |

## 13. Open decisions

| # | Decision | Owner | State |
|---|---|---|---|
| O1 | `SETUP_TAS` on a live schedule | Q | **Decided (D4):** must not silently overwrite. Section 3.6 is the proposal, for REQ-F-TAS-LEGACY-001 |
| O2 | `tas_armed` meaning | Q | **Decided (D5):** derived from a richer record. Section 4.5 is the proposal, for ADR-TSN-002 |
| O3 | Four codes or `TAS_APPLY` | Q, via ADR-TSN-002 | **Decided by #362 decision 1:** separate query (0x80B) and activation (0x80C) codes. No other code is allocated |
| O4 | Is `DISABLE_TO_LEGACY` allowed in production | Q, via ADR-TSN-002 | **#362 decision 2:** not part of the initial implementation; #360 is decided separately |
| O5 | Pre-activation mechanism | Q, via ADR-TSN-003 | Open. Run EG-02 for M1 and M2 first |
| O6 | Should mutating codes require `FILE_WRITE_ACCESS` | Q, after EG-17 | **Decided for the new codes by #362 decision 3:** 0x80C write, 0x80B read. Released codes and the device policy are tracked separately |
| O7 | Should `BaseT` non-zero alone refuse `SETUP_TAS` | Q | **Decided: no.** `BaseT` is reported only. Section 3.6 uses windows, shadow and the record |

## 14. Evidence gaps

Priority order. **Blocks** names only the operation or claim affected. Everything else proceeds.

| ID, priority | Governing issue | Existing | Missing | Experiment | Expected | Acceptance | Blocks |
|---|---|---|---|---|---|---|---|
| EG-03, P0 | #354 REQ-NF-TAS-BUFFER-001 | Default `TXPBSIZE`, `DTXMXPKTSZ` throughout. **DOC** and **LINUX**: 5 KB per queue, 1600 bytes, `RXPBSIZE` changed too | Behaviour under the documented configuration; whether a filter may change it | (1) Read-only: adapter settings and miniport behaviour. (2) `LOCAL_DIAGNOSTIC`: EG-02 and EG-06 under default and documented values, if (1) finds a safe way | Same gating with no stall, or a measured difference | Measured comparison at sustained rate; decision recorded | **Any production-ready claim** (D5). Not diagnostics or contract review |
| EG-02, P1 | #357 REQ-F-TAS-ACTIVATE-001, #363 ADR-TSN-003 | Future base always after `SETUP_TAS` | Section 6.1 from a clean adapter; M1, M2 compared | `LOCAL_DIAGNOSTIC`: exact order, `HOLD`, M1, M2 | HOLD held to first window; PASS flows, then gated | 3 runs each, 0 lost, 0 outside window after `BaseT`, clean restoration | **ACTIVATE** |
| EG-09, P1 | #353 REQ-NF-TAS-OBSERVE-001 | Debug only | Release behaviour | All TC-TASAPI in Release after implementation | Same verdicts | Debug and Release GREEN with installed SHA256 | **Contract acceptance** |
| EG-04, P1 | #349 | 40 of 41 clean, 1 survival, 1 recovery | Trigger; recovery repeatability | Instrumented only; no blind repeats without a trigger hypothesis | Rare reproduction with timeline, or a trigger | Trigger or bound; recovery shown 3 times | `COMPLETE` guarantee only. Reporting covers it |
| EG-01, P2 | #358 REQ-F-TAS-REPLACE-001, `#113` | Lowest 0.935 s | Lower margin; in-kernel commit time | Step margin 500, 200, 100, 50 ms, 10 replacements each, via dedicated IOCTL | At target down to a limit, then refused or missed | 10 of 10 at chosen margin; hold time measured | Margins below 1 s only |
| EG-05, P2 | #350 REQ-F-TAS-STATE-001 | Only the wire proves it. `TEST_SEND_PTP` timestamps are pre-send | A hardware signal | (a) Hardware TX timestamp of a kernel-sent frame against the window. (b) Commit off-grid and see whether `BaseT` adopts the new grid after target | Signal changes only when the schedule did | Agrees with the I210 in 20 of 20, including a deliberately missed commit | `HARDWARE_VERIFIED` only |
| EG-06, P2 | #359 REQ-F-TAS-OPENALL-001 | Whole-cycle at low rate. **LINUX** cites a specification-update item and `RETX_CTL` | Behaviour at load | Sustained MTU frames, with `end == cycle` and with a gap | No loss or stall with the chosen gap | 0 lost, TDH keeps moving, 10 minutes | `OPEN_ALL` at load; gap value |
| EG-07, P2 | #351 REQ-F-TAS-VALIDATE-001 | None | `Strict_Cycle` effect | Matrix with windows that respect TAS-DR-05 | Frame finishing past cycle end held | 2 runs, fitting controls pass | `Strict_Cycle` tier only |
| EG-08, P2 | #351 REQ-F-TAS-VALIDATE-001 | 125 ms, ±30 ms host uncertainty | Accuracy; shorter cycles | Hardware RX timestamps on the I210 with PHC correlation | Uncertainty below 10 % of window | Stated uncertainty, 0 outside window | Tier of other cycles; any accuracy claim |
| EG-10, P2 | #351 REQ-F-TAS-VALIDATE-001 | `REV_04` only | I226-V, I226-IT, other steppings, I225 | EG-02 and replace tests on each | Same, or a recorded difference | Per-device GREEN | Those devices' tier |
| EG-17, P2 | #353 REQ-NF-TAS-OBSERVE-001, `#142` | `FILE_ANY_ACCESS`; default device ACL; no check found in `device.c` | Who can open and mutate | Review with `test_ioctl_access_control`; unelevated open test | ACL behaviour known | Decision O6 | Mutating codes' final definition |
| EG-11, P3 | #351 REQ-F-TAS-VALIDATE-001 | Identical windows; only ring 0 carries traffic | Independence | Needs traffic on a second ring; feasibility unknown | Queues gate separately | 2 runs | Per-queue tier only |
| EG-12, P3 | #350 REQ-F-TAS-STATE-001 | None | Link flap, speed change, miniport reset, S3 with a live schedule | Each event, then `TAS_QUERY` and wire | Report matches hardware | Each event classified | Nothing. Reports `UNKNOWN` |
| EG-13, P2 | #356 REQ-F-TAS-NDIS-001 | Source comment only; cycles so far came after a drain | Pause, Detach and Unload with frames held. Link to #328 unproven | Under a Scenario B test plan: NIC cycle with frames held behind closed gates, bounded | Characterised: completes or stalls | Recorded outcome, Debug and Release | Unload and Pause guarantees only |
| EG-18, P2 | #356 REQ-F-TAS-NDIS-001, #364 | Source only: `FilterRestart` reaches `init_ptp`. No hardware observation | Five questions below | Cases A to E below | Competing outcomes below | Cases A to D, 3 runs each; each question answered yes, no or unknown per case | Any claim that a schedule survives Pause/Restart |
| EG-14, P3 | #360 REQ-F-TAS-DISABLE-001 | Past-`BaseT` re-arm kept the old schedule | Future `BaseT` after mode off | `DISABLE_TO_LEGACY`, future `BaseT`, then mode on | New schedule applies | 2 runs | `ACTIVATE` after legacy |
| EG-15, P3 | #358 REQ-F-TAS-REPLACE-001 | None | Overlapping, off-grid and changed-cycle-miss effects | Each case separately | Characterised | Recorded outcome | Those cases only |
| EG-16, P3 | Various | 1 run each: E2, E3, E9, E15 | Repeatability | Covered by TC-TASAPI | Same | GREEN | Nothing beyond those tests |

### 14.1 EG-18 investigation

A separate hardware evidence gap with its own issue, #364. **No relationship to #328 is claimed without hardware evidence.**

| # | Question | Measured by |
|---|---|---|
| Q1 | Does a schedule survive Pause/Restart, with the same window and phase? | All TSN registers; arrival phase on the I210 before and after |
| Q2 | Is the PHC continuous? | Paired PHC and QPC reads across the event; rate before and after; `BaseT` minus PHC |
| Q3 | What happens to outstanding sends? | Gauge, TDH, TDT, occupancy, pause duration, per-frame arrival |
| Q4 | Do hardware and driver state agree afterwards? | `tasArmed`, all TSN registers, TC-TSNREG-001 |
| Q5 | Does a later unload behave differently? | Completion, duration, stage reached. Separately authorised |

**Trigger:** `IOCTL_FILTER_RESTART_ONE_INSTANCE` (`NdisFRestartFilter`), an existing path with no device removal. First confirm through `FilterPauseCount` and `PauseRestartGeneration` that it reaches Pause and Restart and leaves the miniport unreset. If not, EG-18 is `BLOCKED` until another trigger is found.

| Case | Setup | Purpose |
|---|---|---|
| A | No TAS | Control |
| B | `SETUP_TAS` only, frames held | Legacy hold |
| C | Schedule running, gauge 0 | Clean live schedule |
| D | Schedule running, closed window, frames held, gauge above 0 | Outstanding sends |
| E | Case A, C or D, then unload, one at a time | Q5. An unload stall may need an operator reboot |

Competing outcomes: the PHC reseeds and the schedule stays, giving the E14 hold; the PHC reseeds and the miniport clears the schedule; the PHC does not reseed, because the path differs from source reading; Pause is delayed by held frames; none of these.

Method: `LOCAL_DIAGNOSTIC`, Debug-only, extending `tests/diagnostic/tas_gate_investigation.c`, never a runner entry. Every step is bounded, and each run proves its pre-state and restoration. Nothing runs during this governance phase.

## 15. Not in this design

Production code, new header entries, test files, runner changes, Debug diagnostics in automated tests, a fix for the surviving schedule, PHC reseed or TIMINCA fixes, and any statement about the #328 unload hang.
