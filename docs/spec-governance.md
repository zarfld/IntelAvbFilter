# Spec Governance & Standards Compliance

> **⚠️ DEPRECATED**: This document describes the **legacy file-based governance workflow**.
>
> **Current Approach**: Use **GitHub Issues** as the authoritative artifacts with governance via:
> - Issue templates for ISO/IEC/IEEE standards compliance
> - Labels and milestones for lifecycle tracking
> - Project boards for workflow management
> - Branch protection and required reviews for PRs
>
> See: [GitHub Issues Workflow](improvement_ideas/using_github_issues_as_infrastructure_for_REQ_Tracability.md)
>
> This document is kept for historical reference only.

---

This document defines how Markdown specifications act as the authoritative software engineering artifacts while remaining compliant with ISO/IEC/IEEE standards (29148, 12207, 42010, 1016, 1012) using a spec-driven + AI-assisted workflow.

## Objectives
- Treat Markdown specs as primary source-of-truth (no divergent Word/PDF copies)
- Preserve required information items & traceability demanded by standards
- Enable continuous evolution with version control & review independence
- Provide auditable evidence for verification & validation

## Standards Mapping
| Standard | Required Content / Process | Governance Mechanism |
|----------|---------------------------|----------------------|
| ISO/IEC/IEEE 29148:2018 | Stakeholder needs, requirement attributes (ID, rationale, priority, verification method) | Requirements spec YAML front matter + REQ sections with structured fields |
| ISO/IEC/IEEE 42010:2011 | Stakeholders, concerns, viewpoints, views, rationale | `architecture-spec.md`, `views/`, ADR template with rationale & alternatives |
| IEEE 1016-2009 | Multiple design views, design decisions, interfaces | Architecture + (future) detailed design specs, interface sections with IDs |
| ISO/IEC/IEEE 12207:2017 | Lifecycle process evidence | Phase folders + CI workflow stages mapping to processes |
| IEEE 1012-2016 | V&V planning, independence, evidence | Review checklist, acceptance tests, traceability matrix, reviewer role separation |

## Authoritative Artifact Model
```
Markdown Spec (Requirements / Architecture / Design)
  ↓ (AI compile prompts)
Code + Tests + Docs
  ↓ (CI execution)
Verification Evidence (test results, coverage, reviews)
```

## Mandatory YAML Front Matter Fields
| Field | Applies To | Purpose |
|-------|------------|---------|
| specType | all | Classification for schema selection |
| version | all | Configuration control (semantic) |
| status | all | draft | review | approved lifecycle |
| author | all | Accountability |
| date | all | Change tracking baseline |
| traceability.requirements / stakeholderRequirements | architecture/design | Upstream linkage |
| integrityLevel (optional) | safety/security sensitive | Drives independence requirements |

## Traceability Chain Enforcement
```
Stakeholder Requirement (StR-*)
  → System / Software Requirement (REQ-*)
    → Architecture Component / Pattern (ARC-C-*, ARC-P-*)
      → Design Element (DES-*)
        → Implementation Artifact (SRC-*, path)
          → Test Case (TEST-*)
            → Evidence (Test Result / Coverage / Report)
```

### Traceability Rules
| Rule | Description | Enforcement |
|------|-------------|-------------|
| R1 | Every REQ-* must have at least one acceptance criterion | Spec schema & lint script |
| R2 | Every REQ-* must map to ≥1 ARC-C-* OR ADR-* | Traceability matrix CI check |
| R3 | Every ADR-* must reference ≥1 REQ-* OR QA-SC-* | ADR template validation |
| R4 | Every QA-SC-* must reference ≥1 REQ-NF-* | Scenario template validation |
| R5 | Every TEST-* references ≥1 REQ-* OR QA-SC-* | Test scaffolding generator |
| R6 | No orphan components (ARC-C-*) | Matrix orphan scan |

## Review & Approval Workflow
| Stage | Action | Independence Requirement |
|-------|--------|--------------------------|
| Authoring | Spec created/updated | N/A |
| Lint/Normalize | Run lint prompt & structural validator | Automated |
| Technical Review | Peer architect/lead reviews | Different person than author |
| Compliance Review (if integrityLevel ≥ 3) | Standards/QA reviewer signs off | Independent group |
| Merge | PR approval required | ≥1 approver |
| Post-Merge | CI evidence archived | Automated |

### CODEOWNERS (Recommended)
Example (not yet added):
```
03-architecture/ @architect-team
02-requirements/ @product-owner @requirements-analysts
spec-kit-templates/ @architecture-governance
```

## Integrity Levels (Simplified)
| Level | Description | Additional Controls |
|-------|-------------|---------------------|
| 1 | Low impact | Standard workflow |
| 2 | Moderate impact | Dual review |
| 3 | High impact (financial / regulated) | Independent V&V reviewer + evidence bundle |
| 4 | Critical (safety) | Formal verification placeholder (future) |

Add `integrityLevel` in YAML to escalate required gates.

## Evidence Capture
| Evidence Type | Source | Location |
|---------------|--------|----------|
| Test Results | CI (unit, integration, acceptance) | Artifacts / `07-verification-validation/test-results/` |
| Coverage | CI reports | Codecov / artifact |
| ADR Decisions | `03-architecture/decisions` | Repo history |
| Review Approvals | PR metadata | Git history |
| Traceability Matrix | Generated script | `07-verification-validation/traceability/` |

## Automation Components (Planned / Current)
| Component | Status | Purpose |
|-----------|--------|---------|
| Spec Schema Validation | Implemented | Enforce mandatory YAML keys |
| Spec Index Generation (spec_parser) | Implemented | Canonical inventory of governed IDs & refs |
| Traceability Matrix Script | Implemented | Build JSON → Markdown matrix |
| Traceability JSON (build_trace_json) | Implemented | Machine-readable forward/back links + metrics |
| Requirement Test Skeleton Generator | Implemented | Ensure every REQ-* has at least placeholder test |
| Orphan Detector | Implemented (trace scripts) | Fail build on orphaned IDs |
| Integrity Level Gate | Planned | Dynamic job matrix (extra checks) |
| Evidence Bundler | Planned | Collect & zip compliance artifacts |

## Governance Anti-Patterns
| Anti-Pattern | Risk | Mitigation |
|-------------|------|-----------|
| Free-form specs (missing IDs) | Lost traceability | Schema validation |
| Specs updated after code without recompile | Divergence | PR checklist + CI check for spec timestamp > code timestamp (future) |
| Large ADR decisions in commits without ADR file | Lost rationale | Pre-commit hook + CI grep reminder |
| Ambiguous quality attributes ("fast", "secure") | Non-testable NFRs | Scenario template with metrics |

## PR Checklist (Suggested)
Add to PR template:
- [ ] Spec updated (or N/A)
- [ ] Traceability matrix regenerated
- [ ] New/changed requirements have acceptance criteria
- [ ] ADR added/updated (if architectural change)
- [ ] QA scenarios updated (if NFR impacted)
- [ ] Integrity level requirements satisfied

## Roadmap

| Item | Priority | Notes |
|------|---------|-------|
| Integrity-level conditional jobs | Medium | Matrix strategy in GitHub Actions |
| Evidence bundler | Medium | Zip artifacts + spec snapshot |
| Formal verification hook (future) | Low | Placeholder for critical systems |
| ADR impact analysis automation | Medium | Diff-driven detection of architectural change |
| Spec/code drift detection | Medium | Timestamp / semantic hash comparison |

## Automated Generation Stage

The CI job `spec-generation` (after `spec-validation`) performs deterministic generation of:

1. `build/spec-index.json` – authoritative list of all governed IDs (requirements, architecture, ADRs, QA scenarios, tests) with references.
2. `build/traceability.json` – forward/backward link graph + coverage metrics by ID prefix.
3. `05-implementation/tests/generated/` placeholder requirement test skeletons for any REQ-* lacking explicit tests.

### Guarantees

- Idempotent: re-running without spec edits makes no changes.
- Non-destructive: generated tests live under `tests/generated`; promoting a test involves copying & deleting the generated file.
- Deterministic ordering: stable JSON output for diff minimization (facilitates caching & code review clarity).

### Failure Modes / Mitigations

| Risk | Mitigation |
|------|------------|
| Duplicate IDs across specs | First wins, duplicates ignored + future enhancement: warning |
| Missing spec index (parser error) | Job fails, blocking downstream quality gates |
| Large test explosion | Placeholder tests are lightweight; future cap or grouping strategy possible |

### Usage in Local Workflow

Developers can run:

```bash
python scripts/generators/spec_parser.py
python scripts/generators/build_trace_json.py
python scripts/generators/gen_tests.py
```

to preview artifacts prior to opening a PR.

### Traceability Coverage Metrics

`traceability.json` includes per-prefix coverage allowing dashboards to highlight unlinked items (e.g., REQ without design refs). Threshold-based gating can be added later (e.g., enforce ≥90% REQ linkage).

## References
- ISO/IEC/IEEE 29148:2018 (Requirements)
- ISO/IEC/IEEE 42010:2011 (Architecture Description)
- IEEE 1016-2009 (Design Descriptions)
- ISO/IEC/IEEE 12207:2017 (Lifecycle Processes)
- IEEE 1012-2016 (V&V)
- ATAM / SAAM (Scenario-based architecture evaluation)

---
**Core Principle**: The spec is the system. Code is a compilation artifact; tests and evidence continuously validate spec fidelity.
