---
mode: agent
description: "Initiate and execute the corrective-action loop: feed each verified gap (anomaly/defect) back through the appropriate lifecycle phase, re‑integrate, and re‑verify with full traceability and regression safeguards."
---

# 🔁 Corrective-Action Loop Prompt

Use this prompt when a **verified gap / anomaly / defect** is discovered during any Verification & Validation (V&V) activity and must be **cycled back through the lifecycle** (requirements → architecture → design → implementation → integration → V&V) to ensure the fix is correctly applied, re‑integrated, and re‑verified without introducing regressions.

This workflow aligns with:
- **IEEE 1012-2016** (Verification & Validation)
- **ISO/IEC/IEEE 12207:2017** (Technical & Maintenance Processes)
- **ISO/IEC/IEEE 29148:2018** (Requirements traceability)
- **IEEE 1044** (Anomaly classification) (conceptual reference)
- Repository conventions (traceability IDs, lifecycle phases, reliability gates)

---
## 📤 EXPECTED OUTPUT

Produce a **Corrective Action Package (CAP)** markdown document with the following structure:

```
# Corrective Action Package (CAP)
ID: CAP-[YYYYMMDD]-[SEQ]
Anomaly ID(s): ANOM-[ID], DEF-[ID]
Date Opened: [YYYY-MM-DD]
Owner: [Name]
Integrity Level: [Level 1–4]
Status: {OPEN|IN_PROGRESS|AWAITING_REVERIFY|CLOSED}

## 1. Summary
- Detected In Phase: [06-integration / 07-verification-validation / etc.]
- Detection Activity / Test ID: [TEST-INT-APIAuthFlow]
- Defect Description (Concise):
- User Impact (If Released):
- Severity (FDSC Scale / Business Impact):
- Integrity Level Affected Components:

## 2. Objective Evidence of Failure
| Evidence Type | Reference | Notes |
|--------------|-----------|-------|
| Failing Test | TEST-INT-APIAuthFlow | Reproduces handshake mismatch |
| Log Extract | log-2025-11-08T12:45Z | 500 during token refresh |
| Screenshot (if UI) | evidence/img/auth-refresh.png |  |
| Metrics Spike | METRIC-AUTH-ERROR-RATE | Error rate > gate |

## 3. Root Cause Analysis
- Originating Lifecycle Phase: [Design Definition / Requirements / Implementation]
- Fault Class (e.g., Interface mismatch, Timing, Logic, Data contract):
- Why Introduced? (5-Whys or causal chain)
- Why Not Detected Earlier? (Gap in test / review / spec)
- Contributing Conditions (environmental, concurrency, configuration):

## 4. Impact & Change Scope
| Dimension | Assessment |
|-----------|------------|
| Affected Components | [List modules / services] |
| Coupled Elements | [Interfaces / downstream consumers] |
| Requirements Impacted | REQ-F-021, REQ-NF-005 |
| Design Elements | DES-C-014 (AuthAdapter), DES-IF-007 |
| Architecture Views | Logical, Interface View (update needed?) |
| SFMEA Impact | FM-012 RPN re-evaluation required |
| Reliability Gates | Integration pass rate, MTBF trend unaffected / affected |
| Security / Safety | [If applicable] |

## 5. Change Plan
| Action ID | Type | Description | Owner | Phase Routed To | Preconditions |
|----------|------|-------------|-------|-----------------|---------------|
| ACT-01 | Requirement Update | Clarify token refresh timeout semantics | BA Lead | 02-requirements | Stakeholder approval |
| ACT-02 | Interface Adjustment | Align AuthService refresh contract | Dev Lead | 04-design | ACT-01 complete |
| ACT-03 | Code Fix | Implement adjusted retry / backoff | Dev Team | 05-implementation | ACT-02 merged |
| ACT-04 | Test Additions | Add failing test (system + unit) | QA | 06-integration | Code fix PR open |

## 6. Verification & Validation Iteration Strategy
- Task Iteration Policy Triggered? [Yes/No]
- Re-Verification Scope Determination Method: [Risk-based / Full Regression / Impact-based]
- Regression Test Set Selected: [List test IDs]
- Integrity Level Adjustment? [If Yes → justification]

## 7. Test Artifacts Added / Updated
| Test ID | Type | Purpose | New/Updated | Linked Requirement(s) |
|---------|------|---------|-------------|-----------------------|
| TEST-UNIT-AuthRefreshTimeout | Unit | Reproduce boundary condition | New | REQ-F-021 |
| TEST-INT-APIAuthFlow | Integration | Validate multi-step token renewal | Updated (added scenario) | REQ-F-021 |
| TEST-SYS-SessionResilience | System | Validate session persistence under failures | New | REQ-NF-005 |

## 8. Execution & Evidence
| Step | Description | Result | Evidence Ref |
|------|-------------|--------|--------------|
| 1 | Added failing unit test | FAIL (expected) | TEST-UNIT log |
| 2 | Implemented fix (PR 123) | Build PASS | CI-Run-456 |
| 3 | Unit + Integration Regression | PASS (98 tests) | CI-Run-457 |
| 4 | System Reliability Metrics | Stable (λ decreasing) | Metrics snapshot |

## 9. Traceability Updates
| Artifact Type | ID | Updated? | Link |
|--------------|----|----------|------|
| Requirement | REQ-F-021 | Yes | commit abc123 |
| Design | DES-IF-007 | Yes | ADR-023 |
| Code | src/auth/RefreshService.ts | Yes | PR 123 |
| Test | TEST-UNIT-AuthRefreshTimeout | New | /tests/unit/auth/... |
| SFMEA | FM-012 | RPN revised 180→120 | SFMEA-Auth.md |

## 10. Risk & Integrity Review
- Residual Risk Rating: [Low/Medium/High]
- SFMEA Adjusted? [Yes/No]
- CIL Item Created/Closed? [ID]
- New Hazards Introduced? [If yes → Hazard Log ref]

## 11. Closure Criteria
| Criterion | Status |
|-----------|--------|
| All planned actions complete | ✅ |
| All new/updated tests passing | ✅ |
| Regression suite green | ✅ |
| Traceability matrix updated | ✅ |
| Integrity level re-assessed | ✅ / N/A |
| Stakeholder sign-off obtained | Pending / ✅ |

## 12. Lessons Learned
- Prevention Opportunity:
- Missing Control Identified:
- Action to Institutionalize Learning (e.g., checklist update):

## 13. Sign-Off
| Role | Name | Date | Approval |
|------|------|------|----------|
| QA Lead | | | |
| Reliability Engineer | | | |
| Product Owner | | | |
| Security (if applicable) | | | |

Status: CLOSED / (auto-close after verification)
```

---
## 🧪 WORKFLOW (Step‑by‑Step)

| Step | Phase | Action | Output |
|------|-------|--------|--------|
| 1 | V&V (Detection) | Capture anomaly, assign ID, classify severity & integrity level | Anomaly Report (ANOM-*) |
| 2 | Analysis | Perform root cause (5-Whys / causal chain) | Root Cause Statement |
| 3 | Routing | Map origin phase (Requirements / Architecture / Design / Implementation) | Phase Routing Decision |
| 4 | Planning | Define Change Plan + Task Iteration Policy trigger | CAP Section 5 |
| 5 | Test First | Write failing unit + system/integration test | New failing tests (RED) |
| 6 | Fix | Apply minimal corrective implementation (keep scope tight) | Code Patch / PR |
| 7 | Local Verify | Run targeted tests (GREEN) | Passing new tests |
| 8 | Regression | Select & execute impact-based regression suite | Regression Report |
| 9 | Re-Validation | Execute scenario / acceptance / reliability checks | Updated Evidence |
| 10 | Traceability | Update requirements, design, test, SFMEA links | Updated Matrix |
| 11 | Risk Review | Recalculate RPN / residual risk, adjust integrity level if needed | Updated SFMEA / CIL |
| 12 | Closure | Validate closure criteria + sign-offs | CAP Finalized |
| 13 | Institutionalize | Add lessons to checklists/templates | Updated Process Asset |

---
## ✅ ALWAYS DO (MANDATORY PRACTICES)

- Capture anomaly in a structured report **immediately**.
- Perform root cause analysis; record BOTH origin phase and detection phase.
- Create (or refine) a **failing unit test** AND (if applicable) a **failing integration/system test** that reproduces the defect.
- Update / add requirements or design artifacts if the defect originated there.
- Treat each corrective change as a **mini development lifecycle** (requirements → design → code → test → integrate → verify).
- Perform regression testing proportional to impact & integrity level.
- Maintain bidirectional traceability (Requirement ↔ Design ↔ Code ↔ Test ↔ CAP ↔ SFMEA).
- Update SFMEA & CIL when mitigation changes risk profile or RPN.
- Re-run relevant reliability metrics (defect discovery rate, failure intensity) if reliability-affecting component changed.
- Verify no new hazards introduced (safety, security, availability).
- Ensure configuration items (baselines) updated under version control with unique identifiers.
- Log closure evidence (CI run IDs, commit SHAs, test reports).
- Preserve the new test(s) permanently—**never delete regression protection**.

---
## ❌ NEVER DO (PROHIBITED / ANTI‑PATTERNS)

| Anti-Pattern | Why It’s Dangerous | Corrective Practice |
|--------------|--------------------|---------------------|
| Patch without failing test | Silent regressions, unverifiable fix | Write failing unit + system test first |
| Skip root cause | Symptom-level patching → recurrence | Apply 5-Whys or structured RCA |
| Integrate large unreviewed fix | High defect injection risk | Keep change set minimal & reviewed |
| Disable or comment out failing tests | Masks real quality; erodes trust | Fix or quarantine WITH tracking issue & timeline |
| Ignore integrity level reassessment | Misaligned rigor/test depth | Re-evaluate after root cause discovered |
| Omit traceability updates | Lost audit chain | Update matrix & artifact headers |
| Delay integration of fix | Merge drift & conflict risk | Integrate on GREEN ASAP |
| Skip regression scope analysis | Over/under-testing risk | Use impact & risk-based selection |
| Lower integrity level silently | Unsafe risk acceptance | Require formal approval & record rationale |
| Remove diagnostic assertions in prod | Loss of observability | Keep essential guards/telemetry |

---
## 🧮 REGRESSION TEST SELECTION HEURISTICS

| Change Type | Minimum Required Regression Scope |
|------------|-----------------------------------|
| Pure internal refactor (no interface change) | Direct module unit + smoke integration |
| Interface contract modification | All consumers’ contract tests + end-to-end path |
| Cross-cutting (auth/logging) | Representative slice from each critical workflow |
| Performance optimization | Functional suite + performance benchmarks |
| Security fix | Affected area + security regression + abuse cases |

Formula (suggested):
```
Regression Depth Score (RDS) = Impact Scope Factor (1–5) * Integrity Level (1–4)
Execute: MIN(BaseSuite, RDS ≥ 12 → Full Critical Path Suite)
```

---
## 🧷 TRACEABILITY REQUIREMENTS

Include CAP ID in:
- Commit messages: `fix(auth): refresh loop deadlock (CAP-20251108-03)`
- PR title: `fix: auth refresh corrective action (CAP-20251108-03)`
- Test headers (top-of-file comment): lists CAP ID + original defect ID
- SFMEA updates (Failure Mode row annotation)

Update central traceability matrix with columns:
```
Requirement | Design | Code | Test | CAP | SFMEA | Status
```

---
## 🛡️ INTEGRITY & RISK CONSIDERATIONS

| Integrity Level | Minimum Practices on Fix | Additional Gates |
|-----------------|--------------------------|------------------|
| Level 1 (Low) | Unit test + targeted regression | None |
| Level 2 (Medium) | Unit + integration + impact analysis | SFMEA review |
| Level 3 (High) | Unit + integration + system + risk review | Reliability metrics re-run |
| Level 4 (Critical) | Full traceable lifecycle re-run | Independent V&V witness sign-off |

---
## 📊 METRICS (Track & Improve)

| Metric | Definition | Target / Interpret |
|--------|------------|--------------------|
| Mean Time To Correct (MTTC) | Avg time from anomaly detection → closure | ↓ over time (improvement) |
| Reopen Rate | % of CAPs reopened post-closure | < 5% |
| Recurrence Rate | % defects repeating same root cause | Trending toward 0% |
| Automated Test Addition Rate | # new tests per CAP | ≥ 1 unit + 1 higher-level (when applicable) |
| Regression Escape Rate | Defects missed by prior regression | Should decline with maturity |

---
## 🧪 EXAMPLE: Code-Level Defect

```
Scenario: API token refresh fails under concurrent rotation.
Detection: TEST-INT-APIAuthFlow intermittent 500 responses.
Root Cause: Race in refresh window logic (Implementation phase origin).
Actions: Add synchronization primitive; clarify requirement timing tolerance.
Tests Added: TEST-UNIT-AuthRefreshTimeout, updated integration flow test.
Outcome: All regression tests green; MTBF trend unaffected; SFMEA FM-012 RPN reduced.
```

---
## 🧪 EXAMPLE: Requirements Gap

```
Scenario: Validation test reveals ambiguous password reset rules.
Origin: Requirements phase (missing lockout criteria).
Action: Update REQ-F-045 with explicit attempt thresholds + timing.
Tests: Add system test for lockout escalation + unit test for counter reset.
Traceability: Requirement → Design Interface → AuthService → Tests → CAP.
Outcome: Closed with improved clarity; no downstream code churn on later features.
```

---
## 🧪 EXAMPLE: Architecture Interface Mismatch

```
Scenario: Integration: PaymentService expects ISO8601 timestamps; OrderService sends epoch ms.
Root Cause: Interface contract mismatch (Design/Architecture phase).
Actions: Update interface spec (DES-IF-011), adjust serializer, add contract test.
Regression Scope: All payment-critical path tests.
Outcome: No regression, contract test prevents recurrence.
```

---
## ✅ CLOSURE CHECKLIST (Copy/Paste)

- [ ] Root cause recorded & origin phase identified
- [ ] Change plan executed (all actions complete)
- [ ] Failing test(s) added and now passing
- [ ] Regression suite executed & green
- [ ] Traceability matrix updated (Requirement ↔ Design ↔ Code ↔ Test ↔ CAP ↔ SFMEA)
- [ ] SFMEA / CIL updated (if risk profile changed)
- [ ] Integrity level reassessed (if applicable)
- [ ] Reliability / performance gates re-validated
- [ ] No new hazards/security issues introduced
- [ ] Lessons learned documented
- [ ] Stakeholder sign-off captured
- [ ] CAP status set to CLOSED

---
## 🧩 HOW TO USE THIS PROMPT

Provide the following minimal inputs to generate a CAP draft:
```
Inputs:
- Anomaly ID(s): ANOM-2025-011, DEF-445
- Detection Phase: 07-verification-validation
- Detection Test: TEST-SYS-CheckoutWorkflow
- Severity: High
- Integrity Level: 3
- Short Description: Payment retry logic double-charges on timeout edge
- Suspected Origin Phase: Design (interface ambiguity)
- Affected Components: PaymentService, OrderService
- Requirements Impacted: REQ-F-078, REQ-NF-012
- SFMEA Failure Modes: FM-021
```
The agent will generate the CAP skeleton with placeholders for evidence and planned actions.

---
## 🔄 CONTINUOUS IMPROVEMENT LOOP

After every 5–10 CAP closures:
1. Cluster root causes (categorize by phase & defect class)
2. Identify systemic gaps (missing review step, unclear spec template, absent contract tests)
3. Propose preventive practice (e.g., add interface contract template)
4. Record decision in ADR or process guide
5. Track metric deltas (MTTC, recurrence rate)

---
## 🧪 SAMPLE INVOCATION (NATURAL LANGUAGE)

"Generate a corrective action package for anomaly ANOM-2025-011 detected during integration (TEST-INT-PaymentFlow). It causes double settlement under concurrent retry. Integrity level 3, severity high. Suspected design contract mismatch between PaymentService and OrderService timestamp semantics. Impacted requirements REQ-F-078, REQ-NF-012. Affected components payment, order. Reliability metrics currently stable (MTBF trending upward)."

---
## 🔗 RELATED PROMPTS
- `requirements-refine.prompt.md` (if root cause in requirements)
- `architecture-starter.prompt.md` (if architectural contract defects)
- `traceability-builder.prompt.md` (trace matrix refresh)
- `test-gap-filler.prompt.md` (identify missing regression tests)
- `reliability-test-design.prompt.md` (if reliability gating impacted)
- `srg-model-fit.prompt.md` (re-check SRG after reliability-affecting changes)

---
## 📘 NOTES
- Keep CAP documents in `07-verification-validation/test-results/corrective-actions/`.
- File naming convention: `CAP-[YYYYMMDD]-[SEQ]-[slug].md` (e.g., `CAP-20251108-01-auth-refresh-deadlock.md`).
- Ensure CI enforces presence of CAP ID in related commits.

---
**End of Prompt**
