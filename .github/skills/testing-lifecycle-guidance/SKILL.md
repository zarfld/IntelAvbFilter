---
name: testing-lifecycle-guidance
description: "Use when deciding which testing artifacts belong in which software lifecycle phase, planning verification/validation work across phases, or interpreting IEEE 1012 lifecycle-parallel testing guidance."
user-invocable: true
---

# Testing Lifecycle Guidance

Use this skill for **cross-phase testing methodology and artifact timing**, not for routine test invocation.

Before making substantive decisions about test planning/design/execution across lifecycle phases, consult [`reference.md`](./reference.md). It preserves the complete pre-P0.2 `TESTING-LIFECYCLE-INTEGRATION.md` guidance byte-for-byte; moving it into this skill is an activation/context-routing change only and does not make that methodology optional when relevant.

## Routing

- Running tests → `run-tests`
- Repo-specific test taxonomy, hardware prerequisites, PASS/FAIL/SKIP semantics → `intelavbfilter-testing`
- TDD implementation loop → `test-driven-development`
- Completion evidence → `verification-before-completion`
- GitHub TEST/requirement relationship syntax → `lifecycle-traceability`
- Cross-phase testing artifacts and IEEE 1012 lifecycle integration → this skill + `reference.md`

Current repository-wide rules, issue templates, CI validators, and accepted lifecycle contracts override historical examples if a genuine conflict exists. Report the conflict rather than silently changing semantics.
