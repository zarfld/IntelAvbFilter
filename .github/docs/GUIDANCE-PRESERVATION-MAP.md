# P0.1 Guidance Preservation Map

**Purpose:** shrink always-on Copilot/agent context **without deleting the methodology that was deliberately encoded in the repository**.

This refactor treats the previous guidance as intentional. Content is either kept as a concise always-on invariant, moved to task-scoped guidance, or preserved unchanged as a detailed reference/custom agent. No phase-specific instruction file is removed or weakened by this change.

## Before / after architecture

### Before

- `.github/copilot-instructions.md` — ~51 KB repository-wide, always-on for Copilot
- `AGENTS.md` — ~39 KB agent instructions, also broadly active
- phase-specific `*.instructions.md`
- specialist agents
- specialist skills

The first two files duplicated substantial standards, lifecycle, traceability, philosophy, and workflow material on every applicable turn.

### After

- `.github/copilot-instructions.md` — concise normative Copilot invariants + routing
- `AGENTS.md` — concise cross-agent contract + routing
- `.github/agents/standards-compliance-advisor.agent.md` — **unchanged copy of the former full `AGENTS.md`**
- `.github/skills/engineering-governance/reference.md` — **unchanged copy of the former full `.github/copilot-instructions.md`**
- `.github/skills/engineering-governance/SKILL.md` — progressive-disclosure access to detailed working principles/rationale
- `.github/skills/github-issue-integrity/SKILL.md` — high-risk existing-issue mutation and corruption-recovery rules
- `.github/skills/lifecycle-traceability/SKILL.md` — lifecycle SSOT and relationship syntax
- existing phase instructions/agents/skills remain in place

## Preservation matrix

| Previous guidance | New effective location | Activation / reason |
|---|---|---|
| Core identity: IEEE/ISO/IEC + XP | `.github/copilot-instructions.md`, `AGENTS.md` | Must constrain essentially all work |
| Slow is Fast | concise invariant in both always-on files; detailed explanation in `engineering-governance/reference.md` | Principle remains always active; examples/rationale load only when useful |
| No Excuses | same | Same |
| No Shortcuts | same | Same |
| Clarify First — Never Code on Guesses | same; reinforced in requirements rules | Same |
| Make it work/right/fast; simplicity; small change; explicitness; feedback; build/run; automate pain; boring tech; strong opinions weakly held; Boy Scout; reuse/SSOT/curation | concise list in `.github/copilot-instructions.md`; full wording in `engineering-governance/reference.md` | Behavioral invariant stays global, explanatory expansion becomes on-demand |
| Standards tables and detailed lifecycle descriptions | concise standards list + lifecycle router globally; former full text preserved in governance reference and `StandardsComplianceAdvisor`; existing phase instructions unchanged | Detailed phase data should load with the phase/agent, not every turn |
| XP/DDD/real-time detailed practices | concise invariants globally; detailed reference + existing phase/implementation/test skills | Context-specific detail remains available when relevant |
| GitHub Issues lifecycle SSOT | `.github/copilot-instructions.md`, `AGENTS.md`, `lifecycle-traceability` | Critical state/source rule remains always active |
| Exact traceability workflow/examples | `lifecycle-traceability` + existing issue templates/path instructions; original wording also preserved unchanged in governance reference | Loads when issue/PR/test traceability is involved |
| Backward `Verified by:` append-only safety rule | repeated explicitly in both always-on files + full procedure in `github-issue-integrity` | High-risk rule intentionally remains always active and gets a dedicated procedure |
| Issue corruption recovery workflow | concise pointer globally + full operational procedure in `github-issue-integrity`; original wording preserved in reference | Rare workflow no longer consumes every turn but remains discoverable/automatic for issue-recovery tasks |
| TDD absolute rule | concise global invariant + existing `test-driven-development` skill | Existing dedicated skill already provides detailed executable procedure |
| Test execution / verification | concise routing + existing `run-tests`, `intelavbfilter-testing`, `verification-before-completion` skills | Existing task-specific guidance retained |
| Systematic debugging | routing to existing `systematic-debugging` skill | No reason to duplicate full debugging procedure globally |
| SSOT/magic-number rules | concise SSOT invariant + existing `ssot-and-magic-numbers` skill | Existing specialist guidance retained |
| General quality/security/resilience/performance/documentation rules | concise normative groups in `.github/copilot-instructions.md`; full historical examples in governance reference | All unique behavioral prohibitions remain globally represented without explanatory repetition |
| Real-time ISR/bounded-execution/timing measurement rules | concise global kernel/RT section; full details preserved in governance reference and phase guidance | Critical safety behavior remains active |
| Honest status, feedback, Five Whys, team-not-blame, estimates-vs-promises | concise global communication section; full rationale in governance reference | Behavioral rule remains active |
| Clarification question examples/template | requirements/clarification invariants globally; full examples preserved in governance reference | Exact examples are on-demand detail |
| `StandardsComplianceAdvisor` role/deliverables | former `AGENTS.md` copied unchanged to `.github/agents/standards-compliance-advisor.agent.md` | Becomes an explicit specialist agent instead of a 39 KB global payload |

## Traceability wording normalization

The previous repository-wide file contains a historical contradiction for TEST relationships: it shows a `Verifies` regex/required relationship in some places while other examples incorrectly show `Traces to` as the TEST verification relation.

This refactor does **not** silently discard that history: the old file is preserved byte-for-byte in `engineering-governance/reference.md`.

For executable current guidance, `lifecycle-traceability` follows the repository's active sources:

- `.github/ISSUE_TEMPLATE/07-test-case.yml` explicitly requires `Verifies` for TEST → requirement relationships and states that `Traces to` is the wrong relationship for that purpose.
- `.github/instructions/tests.instructions.md` likewise uses `Verifies: #N` in test-source examples.
- `Traces to` remains the parent/lineage relationship.
- The preserved `StandardsComplianceAdvisor` agent contains historical traceability examples; treat `lifecycle-traceability` + templates/CI validator as the executable contract.

If the validator later disagrees with active templates, agents must inspect/report the conflict rather than inventing syntax.

## What this P0.1 change deliberately does NOT do

- It does not delete or shorten the nine phase-specific instruction files.
- It does not redesign specialist agents other than preserving the former root advisor as an explicit custom agent.
- It does not remove existing skills or prompts.
- It does not change requirements, architecture, implementation, or test lifecycle state.
- It does not treat any previous instruction as "generic filler" merely because it is verbose.
- It does not yet perform the separate P0.2 instruction-discovery/activation cleanup (including updating existing agent-discovery docs that still refer to the pre-P0.1 root `AGENTS.md` advisor).

## Review checklist

Before merging this refactor, verify:

- [ ] former `AGENTS.md` blob is preserved unchanged at `.github/agents/standards-compliance-advisor.agent.md`
- [ ] former `.github/copilot-instructions.md` blob is preserved unchanged at `.github/skills/engineering-governance/reference.md`
- [ ] critical backward-link issue rule remains in both always-on files
- [ ] GitHub Issues SSOT remains in both always-on files
- [ ] Clarify First and TDD remain explicit always-on constraints
- [ ] real-time/kernel safety rules remain globally visible
- [ ] all previous unique Always-Do / Never-Do behaviors are represented by concise global rules or an explicitly routed skill
- [ ] phase-specific instructions remain unchanged
- [ ] existing specialist skills/agents remain unchanged
- [ ] new skill descriptions are specific enough for Copilot to auto-load them when relevant

The design goal is **progressive disclosure, not policy reduction**.