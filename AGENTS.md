# IntelAvbFilter Agent Contract

This file contains the **small cross-agent contract** that should remain effective for AI agents working anywhere in this repository. The former full `AGENTS.md` content has been preserved unchanged as the explicit `StandardsComplianceAdvisor` custom agent at `.github/agents/standards-compliance-advisor.agent.md`.

## Always-on agent principles

- Follow applicable IEEE/ISO/IEC lifecycle, requirements, architecture, design, and V&V standards together with the repository's XP/TDD methodology.
- Apply the four core principles: **Slow is Fast**, **No Excuses**, **No Shortcuts**, and **Clarify First — Never Code on Guesses**.
- Prefer simple, explicit, small, reviewable changes; use evidence and rapid feedback; reuse before reinventing; maintain one source of truth; curate rather than accumulate.
- GitHub Issues are the lifecycle SSOT for StR, REQ-F/REQ-NF, ADR/ARC-C/QA-SC, and TEST state. Query Issues rather than reconstructing current state from derived Markdown.
- Do not start implementation/design/testing without the governing issue and required traceability.
- For implementation/behavior changes, use TDD: failing test first, minimal passing implementation, refactor while green, then verify before completion.
- Do not guess requirements. Surface ambiguity/conflicts, obtain concrete acceptance criteria/examples, and capture clarifications persistently.
- Prefer objective proof over claims: tests, CI, experiments, measurements, logs, traces, or other reproducible evidence.
- Handle failures defensively; validate inputs; preserve security; measure before optimization; design kernel/real-time paths for bounded deterministic behavior.
- Communicate blockers, mistakes, and bad news early; provide options/mitigations, use objective status, and avoid blame.

## Existing GitHub issue integrity — overriding rule

When adding a backward `Verified by:` link to an existing issue, **only add that requested line**. Read the current body first and preserve every unrelated character/section/relationship. Do not rewrite, normalize, reorganize, delete, or repair other content without explicit user direction. If corruption is suspected, follow `github-issue-integrity` and restore from known-good GitHub history rather than inventing a repair.

## Routing

Use the most specific repository guidance instead of expanding this file:

- phase/file-specific work → `.github/instructions/*.instructions.md`
- implementation/bugfix/refactor → `test-driven-development`
- verification/completion → `verification-before-completion`
- debugging → `systematic-debugging`
- testing → `intelavbfilter-testing` / `run-tests`
- SSOT/magic numbers → `ssot-and-magic-numbers`
- GitHub issue mutation/recovery → `github-issue-integrity`
- issue/PR/test traceability → `lifecycle-traceability`
- detailed working-principle rationale/examples → `engineering-governance`
- cross-phase standards/lifecycle advice → `.github/agents/standards-compliance-advisor.agent.md`

The concise repository-wide rules in `.github/copilot-instructions.md` are normative for Copilot and complement this cross-agent contract. Do not treat guidance moved into a skill/agent as deleted or optional; load it when the task matches its scope.