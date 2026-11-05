# DES-REVIEW-002: TAS IOCTL contract against the GitHub Issues SSOT

**Date**: 2026-11-05, updated after filing the same day. **Reviewed**: `04-design/components/DES-C-TAS-010-tas-ioctl-contract.md`, v0.2 and v0.3. v0.2 is accepted as a review baseline only.

**Status**: derived report. GitHub Issues are authoritative. Q filed the package on 2026-11-05 as #348 to #384, with 30 follow-up actions; section 7 records the check. **Filed is not accepted:** no requirement or ADR below is accepted, implemented or hardware verified.

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
| C18 | Code | `devices/intel_i226_impl.c` defines `STQT`, `ENDQT`, `TXQCTL` offsets and `FUTSCDDIS` differently from the SSOT | A new implementation would write the wrong registers | Use the SSOT only. Local draft Issue 6 |
| C19 | #31, #44, #64 | #44 and #64 cite #31 as StR-004 NDIS integration, but #31 is StR-005, Windows Management Service | Parent links unreliable | Comment only. New drafts do not use #31 |

### 1.1 State after filing, 2026-11-05

The table above is the review as written. This is where each conflict stands now.

| Conflicts | State |
|---|---|
| C1, C5 | New issues use unique IDs. Duplicates stay and are cited by number; recorded in the `#9` comment and the `#50` Amendment Record |
| C3, C4 | Governed by #351; `#50` Amendment Record rows 3 and 4 |
| C6 | **Open.** `#206` still says `Verifies: #8`; the `#206` comment records the mismatch. Editing needs Q's direction |
| C7, C8, C9 | Per-criterion comments posted; `#9`, `#206`, `#281` reopened, `status:backlog` |
| C10 | Comment on `#91`. #362 proposes the extension. **Not resolved until #362 is accepted** |
| C11, C16, C19 | Comments on `#142`, `#113`, `#44`, `#64`. No body edits |
| C13 | **Open.** #352 constrains the commit to the per-device lock. Check at implementation |
| C14 | New structures carry a version; applies at implementation |
| C15 | **Open.** #362 OPEN item 3 |
| C17 | No edit to `#133`. #362 links it as `Related to` |
| C2, C12 | **Open.** Derived documents not corrected: D12 |
| C18 | **Open.** No issue filed: D11 |

## 2. Requirement issues, as filed

All of these are filed and `status:backlog`. No requirement is accepted. Dependencies are copied from the issue bodies and those govern; the blockers proposed in this review before filing live on as the evidence gaps in DES-C-TAS-010 section 14.

| Draft | Issue | ID | Scope | Traces to | Dependencies as filed |
|---|---|---|---|---|---|
| B8 | #348 | BUG: `init_ptp` overwrites `SYSTIM` | PHC reseed | #2 | None listed |
| B16 | #349 | BUG: schedule survived a NIC cycle | Surviving schedule | #50 | None listed |
| R1 | #357 | REQ-F-TAS-ACTIVATE-001 | First schedule on a clean adapter, explicit pre-activation behaviour | #33, #1, #50 | #350, #351, #362, #363 |
| R2 | #358 | REQ-F-TAS-REPLACE-001 | Replace a schedule by a future boundary, without a NIC cycle | #33, #1, #50 | #350, #351, #352, #362 |
| R3 | #359 | REQ-F-TAS-OPENALL-001 | Open every gate, stated as not restoration | #33, #50 | #358 |
| R4 | #360 | REQ-F-TAS-DISABLE-001 | Leave TSN mode in place, if ADR-TSN-002 accepts it | #33, #50 | #350, #362 |
| R5 | #361 | REQ-F-TAS-RESTORE-001 | Report restoration from hardware, complete or partial | #33, #50 | #350 |
| R6 | #350 | REQ-F-TAS-STATE-001 | Programmed, pending, inferred, verified, inconsistent, unknown | #33, #1, #50 | #351, #362 |
| R7 | #351 | REQ-F-TAS-VALIDATE-001 | Validation and support tiers | #33, #44, #56, #50 | #44, #56 |
| R8 | #352 | REQ-F-TAS-ERROR-001 | Operation identity, failure retrieval, concurrency | #33, #52, #50 | #45, #52 |
| R9 | #353 | REQ-NF-TAS-OBSERVE-001 | Release observability without raw register access | #33, #122, #50 | None listed |
| R10 | #354 | REQ-NF-TAS-BUFFER-001 | Buffer configuration before any production-ready claim | #33, #50 | None listed |
| R11 | #355 | REQ-F-TAS-LEGACY-001 | Existing ABI preserved; legacy client cannot overwrite a managed schedule | #33, #64, #50 | #350, #352, #362 |
| R12 | #356 | REQ-F-TAS-NDIS-001 | TAS behaviour across Attach, Pause, Restart, Detach, Unload, power | #33, #36, #50 | #348, #350, #362 |
| EG18 | #364 | INV-TAS-EG18 | Effect of Pause and Restart on TAS, PHC and sends | #356 | None listed |
| D1 | #365 | DES-C-TAS-010 | Design issue for the contract, review baseline | #142, #100, #362, #363, #357, #358, #350, #355, #356 | Implementation blocked until accepted |
| T | #366 to #384 | TEST-TAS-API-001..019 | One per TC-TASAPI case, number = 365 + case | The requirement each verifies | Designed only |

### Links to existing issues

Relationship verbs follow the existing vocabulary: `Traces to`, `Verified by`, `Verifies`, `Related to`, and the ADR `Superseded By` field. Requirement templates define no supersession verb, and **none is invented here**.

| Issue | Action | State |
|---|---|---|
| #50 | Stays open as the parent. Traceability links and an Amendment Record appended | Done. Original text retained |
| #9 | Verification scope comment, then reopened by Q's later decision (section 5) | Done |
| #113 | Comment noting the new REQ issues | Done |
| #206, #281 | Verification scope comment, then reopened (section 5) | Done |
| #91, #44, #64, #142 | Comments only | Done |
| #142, #100 | Add links when the design is accepted | Not due: #365 is not accepted. #365 already traces to both |

**Decided by Q:** #50 stays open as the parent. Its Amendment Record of 2026-11-05 holds a 17-row table of outdated, contradicted and unverified statements. Nothing in #50 is removed.

## 3. ADR issues, as filed

Both are filed with Decision Status **Proposed**. Neither is accepted.

| Draft | Issue | ID | Decision | Traces to | Relation to existing ADRs |
|---|---|---|---|---|---|
| A1 | #362 | ADR-TSN-002 | Filter-side TSN ownership, hardware-state authority, lifecycle responsibilities. OPEN at acceptance: IOCTL surface, return to legacy mode, `FILE_WRITE_ACCESS` | #350 to #361, #50 | Extends #91 for TSN registers, does not supersede it. `Related to` #133, #92, #122 |
| A2 | #363 | ADR-TSN-003 | Pre-activation behaviour and `FutScdDis` compatibility. Decided after the activation sequence is measured | #357 | `Related to` #362 |

`Superseded By` fits #91 only if it is fully replaced. It is not, since its PTP decision stands. A1 is an explicit extension, stating what stays valid and what is extended, and #91 gets a comment.

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

## 5. Historical verification scope

`test_ioctl_tas.c` has no traffic, wire or register reads. It binds a TAS-capable adapter, submits configurations and checks status. The driver path reads neither the gate list nor the base time (0 matches in `devices/intel_i226_impl.c` and `src/intel_kernel_real.c`).

| Issue | Closure says | Actual scope | Criteria not met | Recommendation |
|---|---|---|---|---|
| #9 | Completed | IOCTL 26 reaches the device, sets mode and cycle | `BASET`, windows, activation at base time, gates opening, validation scenarios | **Reopen** |
| #206 | 10 of 10 passed | IOCTL path. TC-TAS cases are not the UT-TAS cases specified; all 8 boxes unticked | Gate timing, isolation, integration, V&V | **Reopen**, or close as not verified |
| #281 | Verified, all criteria covered | IOCTL path. Closure cites IOCTLs that do not exist and a readback the test does not do | Validation, atomic commit, readback | **Reopen** |

**Q decided to reopen all three.** Done 2026-11-05: per-criterion comments posted, then all three reopened, now `status:backlog`. Historical results are preserved. The TC-TAS tests stay valid for the IOCTL path and are not changed. Some criteria cannot be met as written, such as 256 entries, and are retired through the new TEST issues.

## 6. v0.3 review results

| Point | Result | Open item |
|---|---|---|
| Requirement → ADR → Design → Test | Real on GitHub. DES-C-TAS-010 section 11 now carries issue numbers. #350 to #361 and #366 to #384 agree in both directions; #354 has no TEST by design | D3 to D7 |
| Unique identifiers | Across all 373 issues on 2026-11-05 each new ID appears once. The only duplicate TAS IDs are the known REQ-F-TAS-001 and TEST-TAS-CONFIG-001 | D13, for this document's own ID |
| Existing IOCTL compatibility | Codes 26, 65, 66, 70 keep their ABI. Refusal applies only in states a legacy-only client cannot reach, decided from record, windows and shadow | None. O7 closed: `BaseT` is reported only. `--rearm`, `--after-rearm` stay allowed |
| Concurrency and generation | Per-adapter claim, generation, epoch, operation store defined; `SETUP_TAS` joins the claim | Lock placement to confirm against #45 |
| Adapter capability | Tiers per feature, device, revision; hardware range separate from tested values | `revision_id` source to confirm |
| Setup, cleanup, restoration | Section 10; NDIS rules in 8.1 | EG-13, EG-18 |
| Release observability | `TAS_QUERY`; no raw access; `#122` respected | Paused-state rule untested |
| RED/GREEN acceptance | 19 tests **designed only**. Hardware RED verified: 0. Hardware GREEN verified: 0. Mock-only paths stay UNVERIFIED | Earlier wording implying RED evidence is withdrawn |

No change is made to HIL tests, runner behaviour or global blocking. No production code is written in this phase.

## 7. Filing result

Checked 2026-11-05 through the public API against the filing manifest, `C:\Users\User\cqv\i328\tas-governance-filing\manifest.json`.

| Item | Result |
|---|---|
| 37 new issues | #348 to #384. Each body equals the manifest with its placeholders replaced by real numbers, plus only the planned appends |
| 30 follow-up actions | All found: 6 dependency appends, 11 `Verified by` appends, 2 edits of #50, 8 comments, 3 reopens |
| Placeholders | None left in issue bodies, comments, this file or DES-C-TAS-010 |
| Reopened | #9, #206, #281, open, `status:backlog` |
| Lifecycle state | #350 to #361 `status:backlog`; #362, #363 Proposed; #365 review baseline; #366 to #384 designed only |

**Not run:** the CI traceability jobs need `GITHUB_TOKEN`, so they did not run locally. The check above is a separate static comparison.

## 8. Open discrepancies after filing

Reported, not resolved. Nothing here reinterprets a requirement, and no issue was edited for this report.

| # | Where | Finding | Needs |
|---|---|---|---|
| D1 | #365 | Label `status:completed` while the body says "Review baseline, not approved". `project-sync.yml` matches the substring `approved`, so the label returns on every sync. Six sync comments so far. The label is not acceptance | Q: reword the body, or a separate fix to the workflow |
| D2 | #366 to #384, #281 | Bot posted "Test Requirements Missing". `issue-validation.yml` matches `Verifies: #N` but not the template's `**Verifies**: #N`, so all 19 links read as absent. The links exist | Separate fix to the regex |
| D3 | #358, #357 | #358 does not list #357, but #371 to #373 start with "Activate" and DES-C-TAS-010 section 7 requires `origin = CONTRACT`. REPLACE cannot run on hardware before ACTIVATE | Q: add the dependency |
| D4 | #357, #352 | #357 does not list #352, but ACTIVATE uses the operation id, claim and generation in DES-C-TAS-010 section 9 | Q: add the dependency, or defer those fields |
| D5 | #351, #352, #353, #354, #359, #361 | List no dependency on #362, which traces to all of them. DES-C-TAS-010 section 11.2 assigns ADR-TSN-002 to all. #365 blocks implementation until accepted. #354 is not blocked by its own text | Q: confirm the #365 rule governs |
| D6 | #353, #366 | Only #366 verifies #353. The design also names TC-TASAPI-004, 006 in Release. #366's steps do not cover an unbound handle or the absence of raw register IOCTLs in Release. #367 covers the unbound handle but verifies #351 only | Q: add `Verifies` links or a test step |
| D7 | #354 | No TEST issue. Expected until EG-03 defines one | TEST issue after EG-03 |
| D8 | #365 | Traces to 5 of the 12 requirements. #362 traces to all 12 | Q: add links if intended |
| D9 | #366 to #384, #361, #348 | All 19 tests share one cleanup: NIC cycle, then measured PHC, including the read-only #366. A NIC cycle resets the PHC (E12). #348 says `CLEANUP_FAILED` is correct for that, while #361 reports the PHC separately and still reads complete. As written, every test may end `CLEANUP_FAILED`, and a read-only test performs a mutation | Q: decide what restoration means for the PHC after a NIC cycle. Do not weaken either text silently |
| D10 | #348 | Title names FilterAttach and FilterRestart. Source also reaches `init_ptp` from `IOCTL_AVB_INIT_DEVICE` on an initialized context: `AvbBringUpHardware`, `intel_init`, `init`, with no guard. `Run-Tests-CI.ps1` records the same. Not observed on hardware in this review | Q: confirm scope of #348 |
| D11 | C18 | Draft Issue 6 in `C:\Users\User\cqv\i328\github-drafts.md` was not filed. Confirmed in source: `I226_STQT`, `I226_ENDQT`, `I226_TXQCTL` and `I226_TQAVCTRL_FUTSCDDIS` differ from the SSOT and are unused by the driver | New issue |
| D12 | C2, C12 | `02-requirements/COMPLETENESS-AUDIT-REPORT.md` and `DES-C-IOCTL-005` still carry the wrong labels | Separate correction |
| D13 | This file | The ID DES-REVIEW-002 was already used for the Phase 04 review in `DESIGN-REVIEW-SUMMARY.md`, `DESIGN-REVIEW-ACTION-ITEMS.md` and `DEVICE-SUPPORT-MATRIX.md`. No issue cites either. The name is kept here until Q decides | Q: rename or keep |
| D14 | #357, DES-C-TAS-010 | #357 lists #363 for the whole requirement, and section 11.2 lists it for TC-TASAPI-004. Section 11.1 says only TC-TASAPI-005 waits on it | Q: can HOLD proceed before #363 is accepted |
| D15 | #359, #360 | Body priority Medium, label `priority:high` | Q |
| D16 | #349 | Reporting acceptance relies on `HW_ARMED_NO_RECORD` and `BASE_FAR_AHEAD`, which section 11.2 lists as mock-only until EG-04 finds a trigger. It cannot be closed on hardware as things stand | Q: closure rule |

No causal link between any TAS finding and #328 is established. #348, #349 and #364 each state this.
