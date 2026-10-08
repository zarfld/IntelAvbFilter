---
name: engineering-governance
description: Use when planning, designing, implementing, reviewing, refactoring, optimizing, debugging, discussing engineering trade-offs, applying standards/XP/DDD/real-time methodology, or interpreting the repository's working principles. Preserves the full pre-P0.1 repository-wide Copilot guidance as detailed reference material without keeping it in every chat turn.
user-invocable: true
---

# Engineering Governance

This skill preserves the **full detailed rationale, examples, heuristics, standards guidance, lifecycle guidance, working principles, critical-rule explanations, question guidance, and historical repository policy** that previously lived in the 51 KB always-on `.github/copilot-instructions.md`.

The concise rules in the current `.github/copilot-instructions.md` are normative and always active. The detailed historical wording is stored unchanged in [`reference.md`](./reference.md). Moving that material into this skill is a context optimization only; it does **not** make the underlying principles optional.

## When to load the reference

Consult the relevant section(s) of `reference.md` when a task needs more than the concise repository-wide rule, especially for:

- interpreting **Slow is Fast**, **No Excuses**, **No Shortcuts**, or **Clarify First**;
- standards-compliance or lifecycle decisions;
- XP/TDD, DDD, real-time, quality, security, resilience, performance, operability, or maintainability trade-offs;
- deciding whether an action is a shortcut, YAGNI, overengineering, or necessary engineering work;
- architecture/design/review/refactoring decisions where the examples and heuristics materially help;
- status/communication/root-cause guidance;
- questions about the original instruction intent or wording.

Do not load the entire reference merely for trivial repository navigation or a simple factual lookup. Use progressive disclosure: read the relevant heading/section, apply it, and keep the main context focused.

## Preserved reference index

The reference contains the original detailed sections, including:

1. Core philosophy: Slow is Fast / No Excuses / No Shortcuts / Clarify First
2. Complementary engineering philosophies
3. Primary objectives
4. Applicable standards and XP/DDD/real-time practices
5. Nine lifecycle phases
6. GitHub Issue traceability workflow and examples
7. General guidelines for requirements, coding, review, and documentation
8. Critical Always Do / Never Do rules
9. Clarification-question guidance
10. Issue-driven development and success criteria
11. Related guidance files
12. Issue-corruption recovery workflow

## Interaction with narrower skills

When a narrower skill exists, use it for the executable procedure and this skill for governing rationale:

- TDD → `test-driven-development`
- testing → `intelavbfilter-testing` / `run-tests`
- verification → `verification-before-completion`
- debugging → `systematic-debugging`
- SSOT/magic numbers → `ssot-and-magic-numbers`
- GitHub issue integrity/recovery → `github-issue-integrity`
- lifecycle traceability syntax/workflow → `lifecycle-traceability`

If a concise rule and the legacy reference appear to conflict, do not silently choose a convenient interpretation. Prefer the current explicit repository rule/CI contract, report the discrepancy when material, and preserve the historical text as evidence of intent.