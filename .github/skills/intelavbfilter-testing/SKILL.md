---
name: intelavbfilter-testing
description: "Use when analyzing, planning, writing, or interpreting tests in IntelAvbFilter: hardware-vs-CI test selection, multi-adapter execution, PASS/FAIL/SKIP semantics, TC-ID output, and repository-specific test taxonomy."
---

# IntelAvbFilter Testing

This is the valid Agent Skills entry point for repository-specific testing guidance.

Before substantive testing decisions, **consult and apply [`reference.md`](./reference.md)**. That file preserves the complete previous testing skill byte-for-byte; it was moved behind this entry point because the old `SKILL.md` was accidentally wrapped in a Markdown code fence and therefore did not expose valid top-level skill frontmatter.

## Routing

- Test invocation mechanics → `run-tests`
- Cross-phase verification/validation artifact planning → `testing-lifecycle-guidance`
- Requirement/TEST issue syntax → `lifecycle-traceability`
- TDD implementation cycle → `test-driven-development`
- Completion evidence → `verification-before-completion`
- Repo-specific test semantics/hardware behavior → this skill + `reference.md`

## Mandatory repository-specific invariants

- Adapter-dependent tests exercise **all supported adapters present**, not only adapter index 0.
- Capability tests stay adapter-family agnostic: call the generic driver interface and let the driver return `STATUS_NOT_SUPPORTED`; do not pre-filter tests by hard-coded device ID.
- `SKIP` is reserved for genuine not-applicable conditions such as absent required hardware or an explicit unsupported capability. Driver/open/setup/prerequisite/correctness errors are `FAIL`, not `SKIP`.
- A failure on any tested adapter must remain visible and produce failing test status; do not collapse mixed adapter results into success.
- Per-adapter results retain the repository's TC-ID/adapter identity convention so failures can be attributed to the specific NIC.
- Hardware-independent tests belong in CI where practical; hardware integration evidence must not be fabricated from simulation-only results.
- GitHub Issues remain lifecycle SSOT for requirement and TEST relationships; use the current templates/validators for relationship syntax.

If the preserved reference conflicts with current issue templates, CI validators, or accepted repository instructions, report the conflict and use the current executable contract rather than silently inventing behavior.
