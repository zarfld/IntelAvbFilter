# DES-REVIEW-002: TAS IOCTL contract against the GitHub Issues SSOT

**Date**: 2026-11-05. **Reviewed**: `04-design/components/DES-C-TAS-010-tas-ioctl-contract.md`.

**Status**: derived report. GitHub Issues are authoritative. **Nothing here has been filed or changed on GitHub.** This session has no `gh` and no token, and read issues through the public API only. Issue bodies ready for filing are held locally for Q.

Issues read for this review: #1, #9, #29, #30, #33, #41, #44, #45, #50, #52, #56, #64, #91, #92, #100, #113, #122, #133, #134, #142, #206, #281. Numbers below are as read on 2026-11-05 and must be re-read immediately before any edit.

## 1. Conflict report

| # | Issues | Conflict | Effect | Proposed resolution |
|---|---|---|---|---|
| C1 | #9, #50 | Both titled REQ-F-TAS-001. #9 closed, #50 open. Different parents: #9 to #1, #30; #50 to #33, #44, #29 | The ID does not identify one requirement. #113 traces to both | **No renumbering.** Cite by issue number. Add a disambiguation comment to both. New requirements get unique IDs |
| C2 | #41 | Is REQ-F-PTP-EPOCH-001. Local `02-requirements/COMPLETENESS-AUDIT-REPORT.md` calls it REQ-F-TAS-GATE-001 | Derived document wrong | Do not cite #41 for TAS. Correct the derived report separately |
| C3 | #9, #50, #100 | Gate entries: 64 "hardware limit" in #9, 256 "hardware limit" in #50, 64 slots in #100. Registers provide one window per queue | None of these limits matches the register set | New REQ states one window per queue. #9 and #50 are left unedited apart from links |
| C4 | #9, #50 | Minimum cycle 1 µs in #9. Manual floor 31.25 µs | Validation differs | REQ-F-TAS-VALIDATE-001 |
| C5 | #206, #281 | Two tests named TEST-TAS-CONFIG-001 | Test ID not unique | Cite by number. New tests get unique IDs |
| C6 | #206, #8 | #206 says `Verifies: #8 (REQ-F-TAS-001…)`, but #8 is REQ-F-QAV-001. #100 also calls #8 REQ-F-TSN-TAS | Traceability points at the wrong requirement | Comment only. Editing needs Q's explicit direction under `github-issue-integrity` |
| C7 | #9 | Closed completed, with `Verified by: TEST-TAS-SCHEDULE-001 (to be created)`. No issue carries that title. #206 and #281 are named TEST-TAS-CONFIG-001 | Closed without its named verification | Q to decide; see C9 |
| C8 | #50, #281 | #281 is closed and cited by #50 as verification, while #50 is open and `status:backlog`. #281 specifies `IOCTL_TAS_CONFIGURE 0x9C40A0A0`, which does not exist in `include`, `src` or `tests`. Its closing comment cites `IOCTL_AVB_SET_TAS_CONFIG` and `IOCTL_AVB_GET_TAS_CONFIG`, which do not exist either | Closure does not match the code | Q to decide; see C9 |
| C9 | #9, #206, #281 | Closed on `test_ioctl_tas.c` passing. That test has no traffic or wire check. Handler code writes `TQAVCTRL` and cycle only, and **HW** shows `SETUP_TAS` applies no gate schedule and holds frames | "Verified" means the IOCTL path, not gating. The acceptance text claims more than was shown | **Do not reopen automatically.** Comment recording what was verified. Reopen or keep closed is Q's decision |
| C10 | #91 | Accepted. Conflict with the miniport mitigated by restricting the filter to PTP registers `0x0B600-0x0B700` | **All TAS registers are outside that range.** Existing `SETUP_TAS` is already outside it | ADR-TSN-002 must address this. #91's decision text is not edited |
| C11 | #142 | Says `Implements #133 (ADR-SSOT-001)` and `#134 (ADR-SEC-001)`, but #133 is ADR-TSN-001, #134 is ADR-MULTI-001, and ADR-SEC-001 is #122. Also `#30 (StR-IOCTL-API)`, `#33 (REQ-F-IOCTL-CLOSE-001)` differ from those issues | Component traces are unreliable | Comment only |
| C12 | `DES-C-IOCTL-005` | Header cites `#91 (REQ-NF-SEC-IOCTL-001)`, but #91 is ADR-PERF-002 | Derived document wrong | Correct separately |
| C13 | #50, #45, #52 | #50 wants all registers in one spin-lock hold. Existing `SETUP_TAS` path shows no lock | Possible gap against #45 | Check at implementation |
| C14 | #50, #64 | #50 specifies no output buffer and no version. #64 requires a version in all IOCTL structures | New structs must follow #64 | New structs carry version |
| C15 | #50, code | #50 specifies `FILE_WRITE_ACCESS`; `_NDIS_CONTROL_CODE` uses `FILE_ANY_ACCESS` for all codes | Access model undefined | EG-17 |
| C16 | #113 | Target "8-slot schedule" assumes a gate list | Scenario wording does not fit one window per queue | Comment; scenario stays valid as a latency target |
| C17 | #133 | Status Proposed with TAS "Not Started", while #9 is closed completed | Status drift | ADR-TSN-002 traces to it; no edit |

## 2. Proposed requirement issues

IDs below did not appear as issue titles when searched on 2026-11-05. Re-check before filing. **Filing order matters:** requirements first, then ADRs, then test issues, because each traces to the one before.

| Draft | Proposed ID | Scope | Traces to | Blocks on |
|---|---|---|---|---|
| R1 | REQ-F-TAS-ACTIVATE-001 | First schedule on a clean adapter, explicit pre-activation behaviour | #33, #1, #50 | ADR-TSN-002, EG-02 |
| R2 | REQ-F-TAS-REPLACE-001 | Replace a schedule by a future boundary, without a NIC cycle | #33, #1, #50 | ADR-TSN-002 |
| R3 | REQ-F-TAS-OPENALL-001 | Open every gate, stated as not restoration | #33, #50 | R2 |
| R4 | REQ-F-TAS-DISABLE-001 | Leave TSN mode in place, if ADR-TSN-002 accepts it | #33, #50 | ADR-TSN-002 |
| R5 | REQ-F-TAS-RESTORE-001 | Report restoration from hardware, complete or partial | #33, #50 | R6 |
| R6 | REQ-F-TAS-STATE-001 | Programmed, pending, inferred, verified, inconsistent, unknown | #33, #1, #50 | None |
| R7 | REQ-F-TAS-VALIDATE-001 | Validation and support tiers | #33, #44, #56, #50 | None |
| R8 | REQ-F-TAS-ERROR-001 | Operation identity, failure retrieval, concurrency | #33, #52, #50 | None |
| R9 | REQ-NF-TAS-OBSERVE-001 | Release observability without raw register access | #33, #122 | None |
| R10 | REQ-NF-TAS-BUFFER-001 | Buffer configuration before any production-ready claim | #33, #50 | EG-03 |

### Links to existing issues

Relationship verbs follow the existing vocabulary: `Traces to`, `Verified by`, `Verifies`, `Related to`, and the ADR `Superseded By` field. Requirement templates define no supersession verb, and **none is invented here**.

| Issue | Action | Rule |
|---|---|---|
| #50 | Keep open as the parent. Append `- Related to: #N (…)` lines under Traceability for each new REQ, and add a comment listing which statements are amended | `github-issue-integrity`: fetch immediately before, append only, compare, change nothing else |
| #9 | Comment only: closed historical record of IOCTL 26 as implemented; the ID is shared with #50, cite by number | No body edit |
| #113 | Comment noting the new REQ issues | No body edit |
| #206, #281 | Comment recording what was verified (C9) | No body edit |
| #142, #100 | Add links when the design is accepted | Fetch first |

**Decision needed from Q:** D1 asks for supersession or amendment links. This review proposes **amendment**, since #50 still states the parent capability and #281 cites it. If Q prefers supersession, #50 could close when the new REQs are verified.

## 3. Proposed ADR issues

| Draft | ID | Decision | Traces to | Relation to existing ADRs |
|---|---|---|---|---|
| A1 | ADR-TSN-002 | Filter-side TSN ownership, hardware-state authority, lifecycle responsibilities | R1 to R9, #50 | Narrows #91 for TSN registers; refines #133; depends on #92, #122 |
| A2 | ADR-TSN-003 | Pre-activation behaviour and `FutScdDis` compatibility | R1 | Depends on A1 |

The ADR template's `Superseded By` field fits #91 only if #91 is fully replaced. It is not, since its PTP decision stands. So A1 states the exception and #91 gets a comment. If Q prefers, a new ADR could supersede #91.

## 4. Review of the contract

| Topic | v0.1 | v0.2 |
|---|---|---|
| Authority | `TAS-R-nn` read as requirements | `TAS-DR-nn` design rules; requirements live in issues |
| D2 | `DISABLE` with two modes | `OPEN_ALL` is a replacement, not a disable; `DISABLE_TO_LEGACY` is separate, pending ADR |
| D3 | `PASS_BEFORE_BASE` recommended default | No default. Behaviour separate from mechanism; `NOT_IMPLEMENTED` until ADR-TSN-003 |
| D4 | Unverified mapped to `NOT_IMPLEMENTED`, implying missing work | Four tiers; hardware range reported apart from tested values; `INCONCLUSIVE` verdict |
| D5 | EG-03 P1 | EG-03 P0 for any production-ready claim |
| State | One enum mixing lifecycle and evidence | Slot status plus separate hardware facts |
| Inference | PHC past target | Six conditions, and PHC alone is explicitly insufficient |
| Failure detail | `last_*` per adapter, so concurrent callers could read each other's result | Keyed by `client_op_id`, per adapter, per handle |
| Concurrency | `BUSY` only | Claim, generation compare-and-swap, epoch, non-blocking query |
| ABI | Codes presented as chosen | Codes are candidates, alternatives compared, drop path stated |
| Locking | Not addressed | Reconciled with #45, #52 |

**Corrected claim:** v0.1 used the 34 ms `BaseT` write to size a commit guard. That figure came from user-mode diagnostics issuing separate IOCTLs with logging on.
