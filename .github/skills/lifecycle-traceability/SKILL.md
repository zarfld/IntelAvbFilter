---
name: lifecycle-traceability
description: Use for any GitHub issue, pull request, test, architecture artifact, requirement, or traceability change involving StR, REQ-F/REQ-NF, ADR, ARC-C, QA-SC, TEST, parent/child links, Verified-by links, or Fixes/Implements relationships. Enforces GitHub Issues as lifecycle SSOT and the repository's current exact relationship syntax.
user-invocable: true
---

# Lifecycle Traceability

## Source of truth

GitHub Issues are authoritative for current lifecycle state and relationships for:

- Stakeholder Requirements (StR)
- Functional / Non-Functional Requirements (REQ-F / REQ-NF)
- Architecture Decisions / Components / Quality Scenarios (ADR / ARC-C / QA-SC)
- Test Cases (TEST)

Derived Markdown reports/matrices are evidence and views; they do not override current Issue state.

Before creating or modifying lifecycle artifacts, query the relevant GitHub Issues and use their issue numbers.

## Canonical relationship syntax

Use the repository's current templates/path-specific instructions as the executable contract.

### Child → parent / dependency traceability

For non-StR issue parent relationships, the current REQ template requires:

```markdown
## Traceability
- Traces to:  #123 (parent issue)
```

Preferred spelling/casing is exactly `Traces to:` with the `#N` issue reference. Do not substitute `Links to`, `Parent`, `Implements`, or other verbs for this relationship.

### TEST → verified requirement

TEST issues use the distinct **verification** relationship, not `Traces to` as a replacement:

```markdown
## Traceability
- **Verifies**: #123 (REQ-F/REQ-NF being verified)
```

The current `.github/ISSUE_TEMPLATE/07-test-case.yml` explicitly requires `Verifies` for tested requirements and says `Traces to` is the wrong relationship for that purpose. Test source files also use `Verifies: #N` according to `.github/instructions/tests.instructions.md`.

A TEST issue can legitimately contain both relationships when they mean different things, for example:

```markdown
- Traces to: #233 (parent TEST plan)
- **Verifies**: #62 (requirement being verified)
```

### Requirement → TEST backward relationship

When adding a backward link from an existing requirement/parent issue to a TEST issue, use the repository convention:

```markdown
- Verified by: #XXX (TEST-...: description)
```

For this operation, **also load and obey `github-issue-integrity`**. Adding a backward link is append-only with respect to unrelated existing issue content.

### Pull requests

Every PR implementing tracked work must use an issue relationship such as:

```markdown
Fixes #123
```

or, when closure is not intended:

```markdown
Implements #123
```

Keep PRs/commits linked to the governing issue(s), and pass repository traceability validation before completion.

## Lifecycle relationship requirements

- StR is root-level and is exempt from a parent-link requirement.
- REQ-F/REQ-NF must trace to their parent StR using `Traces to: #N`.
- ADR must trace to the requirements it satisfies.
- ARC-C must trace to governing ADRs/requirements as required by its phase instructions.
- TEST must identify the requirements it **verifies** using the TEST template syntax.
- Tests in source must reference the requirement(s) they verify.
- PRs must link to implementing issue(s).
- Maintain backward links when repository workflow requires them, but do not rewrite parent bodies opportunistically.

## Current contract beats stale examples

The preserved legacy repository-wide reference contains historical examples that are internally inconsistent around TEST relationships (some examples show `Traces to` while the TEST regex/text says `Verifies`). Do **not** reproduce that inconsistency. For current work:

1. prefer the active issue template and applicable `*.instructions.md` file;
2. use `Verifies` for a TEST → requirement verification relationship;
3. use `Traces to` for parent/lineage relationships;
4. if CI behavior and current templates ever disagree, inspect the validator and report the discrepancy instead of inventing a new syntax.

This normalization preserves the intended semantic relationships while preventing the known legacy contradiction from being propagated.

## Validation checklist

Before declaring traceability work complete:

- confirm every referenced issue exists and is the intended artifact;
- confirm relationship direction and verb are semantically correct;
- confirm the exact current repository syntax/template is used;
- confirm no unrelated issue content was changed;
- confirm required bidirectional links are present where applicable;
- run/inspect the relevant traceability CI validation or equivalent repository check.