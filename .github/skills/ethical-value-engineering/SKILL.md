---
name: ethical-value-engineering
description: >-
  Assess and engineer ethical values during system conception, requirements,
  architecture, design, verification, and change management using an
  IEEE 7000-2021-aligned process. Use when a proposed feature, autonomous-agent
  action, deployment, data flow, control mechanism, or operational change could
  materially affect people, privacy, autonomy, safety, accountability,
  transparency, sustainability, or conflicting stakeholder values; when asked
  for an ethical-impact review, value-based requirement, ethical trade-off,
  Case for Ethics, or IEEE 7000 applicability/conformance assessment. Integrate
  with the repository's established issue-first lifecycle and evidence rules.
---

# Ethical Value Engineering

## Purpose and boundaries

Apply IEEE 7000-2021's value-based engineering processes to a defined **system of interest (SOI)**. Assess both the system being developed and, when relevant, the autonomous development agents and automation that operate on it. Keep those two SOIs and their evidence separate.

- Treat IEEE 7000 as a process for eliciting, prioritizing, realizing, and explaining stakeholder values. It does not prescribe a universal set of ethical priorities, a specific ethical algorithm, or the technical behavior of other IEEE protocols.
- Distinguish **standard requirement**, **existing repository policy**, **proposed policy**, **evidence**, **inference**, and **open question**. Do not claim conformity to IEEE 7000 from partial use of this skill.
- Preserve the original task, existing safety constraints, requirements, acceptance criteria, tests, issue IDs, and lifecycle gates. An ethical review does not authorize their modification.
- Treat assessments as **read-only by default**. Create or edit Issues, files, implementations, CI, runners, hardware configurations, or external communications only when the user explicitly authorizes that action under existing repository rules.

## Resolve authority before work

1. Read the current repository-wide agent instructions, applicable phase instructions, skills, governing GitHub Issues, and approved decisions. Resolve conflicting directions explicitly; never silently choose the most convenient one.
2. In repositories using the shared template, treat **GitHub Issues as the lifecycle single source of truth (SSOT)** for StR, REQ-F/REQ-NF, ADR/ARC-C/QA-SC, implementation, and TEST. Documents and reports are derived views. Use the existing `lifecycle-traceability` skill for issue taxonomy, labels, exact links, and issue operations; never invent a parallel ethics issue taxonomy or traceability register.
3. Identify authorized scope, decision-maker, stakeholders, phase, and acceptance criteria. Classify user-supplied material as requirement, restriction, correction, evidence, background, question, or proposal. Do not silently elevate remembered or external context into binding requirements.
4. If current Issues or mandatory instructions cannot be retrieved, identify the missing authority, continue only with clearly labeled provisional analysis, and stop before authoritative lifecycle or implementation decisions.

## Procedure

### 1. Screen applicability and define context (IEEE 7000 Clause 7)

- Describe the SOI, intended use, lifecycle stage, interfaces, dependencies, context of deployment, and system boundaries.
- Distinguish the product's behavior from the development agent's own actions and permissions. Consider scale, misuse, failure, and foreseeable changes in context.
- Identify direct and indirect stakeholders and any potentially underrepresented stakeholders. Record which perspectives were actually consulted and which are inferred; never invent consultation or stakeholder approval.
- Determine whether the proposed change could materially alter stakeholder values, human control, privacy, safety, accountability, environmental effects, or distribution of benefits and harms.
- For an apparently negligible impact, produce a brief **screened: no material ethical impact identified** finding with facts, rationale, and uncertainty. This finding does not waive normal requirements, tests, or safety gates. When impact or context is unclear, keep assessment open.

### 2. Elicit benefits, harms, and values (Clause 8)

For material concerns, assess at least these complementary perspectives:

- **Consequences:** Who benefits or is harmed, under normal operation, foreseeable failure, misuse, and widespread deployment?
- **Duties and rights:** Which obligations, commitments, rights, organizational policies, or stakeholder expectations may be promoted or undermined?
- **Agency and professional conduct:** Could the design weaken human autonomy, informed choice, trust, professional judgment, or responsible operator behavior?
- **Context:** Are other local, cultural, environmental, legal, accessibility, or domain-specific perspectives relevant?

Distinguish stakeholder statements from the agent's hypotheses. Organize relevant values into coherent groups without collapsing materially different interests. Identify value conflicts (for example, privacy versus diagnostic transparency, or automation versus operator control). Do not assume that any single value universally outranks the others.

### 3. Prioritize values with accountable humans (Clause 8)

- Present proposed priorities, supporting reasons, uncertainty, conflicting values, and credible alternatives.
- Identify whose approval is required under current repository and stakeholder governance.
- Record actual decisions, rationale, authority, and unresolved objections in the governing Issue(s) once authorized. Until then, label proposed priorities **provisional**.
- Never decide a contested material value priority or accept residual ethical risk on behalf of the maintainer, affected stakeholders, or an operator.

### 4. Derive verifiable requirements (Clause 9)

- Translate approved high-priority values into ethical-value requirements (EVRs) and, where appropriate, existing REQ-F/REQ-NF requirements with measurable or otherwise reviewable acceptance criteria.
- Preserve traceability from the relevant stakeholder concern to the system requirement, ADR/ARC-C or design, implementation, and verification evidence. Reuse canonical Issue types and `lifecycle-traceability` syntax; do not create a new `EVR` issue type unless the maintainer expressly adopts it.
- Specify context, assumptions, constraints, risk treatment intent, verification method, and evidence needed. An aspirational statement such as "the system is trustworthy" is not an acceptance criterion.
- Analyze conflicts with existing technical, safety, reliability, performance, interoperability, privacy, and compatibility requirements. Do not weaken an established requirement to make an ethical review pass.
- Propose Issue changes separately from implemented ones. Do not assert that a requirement exists until it has been accepted and recorded in the authoritative Issue lifecycle.

### 5. Analyze risk, control, and design alternatives (Clause 10)

- Identify credible harms, affected values, causes, likelihood, severity, detectability where relevant, uncertainty, and potential benefits. Use qualitative assessments when evidence does not support numerical estimates.
- Compare alternatives, including avoiding, reducing, monitoring, and explicitly accepting risk through authorized governance. Document rejected alternatives and reasons.
- Identify system elements, owners, interfaces, and actual controllability; distinguish technical controls from organizational or procedural controls.
- For autonomous development agents, assess permission boundaries, data provenance, exposure of secrets or personal data, prompt-injection risks, irreversible actions, auditability, human override, and escalation paths. These are **proposed operational controls inspired by IEEE 7000**, not direct mandates from its informative Annex F.
- Identify residual risks and the person or body authorized to accept them. If a material unresolved risk or authority conflict affects implementation, pause the affected action and request a decision; do not block unrelated authorized read-only analysis.

### 6. Verify and validate without weakening evidence (Clauses 9-10)

- Separate verification of a technical requirement from stakeholder validation of ethical acceptability. Supply evidence for each claim.
- Link every accepted, testable requirement to the existing TEST mechanism. Preserve tests and report failures, inconclusive results, unimplemented functionality, unsupported hardware, and missing evidence according to the repository's current verdict policies.
- For hardware-dependent behavior, use actual hardware-in-the-loop (HIL) observations. Documentation-only assumptions, simulations, or mocks are not hardware proof. Hardware-derived fixtures may support hardware-free CI when authorized by existing policy.
- Maintain setup, safety, restoration, and cleanup requirements. Never suppress failing tests, dilute their scope, alter baselines, or execute unauthorized hardware mutations in the name of ethical risk reduction.
- Label results **observed**, **derived**, **proposed**, **untested**, or **blocked** as appropriate. A planned test, draft control, or green documentation check is not implementation or HIL evidence.

### 7. Preserve appropriate transparency (Clause 11)

- Record the assessment's scope, contributors, known evidence, alternatives, ethical priorities, requirements, decision authority, risk controls, results, and unresolved concerns in retrievable authoritative records when authorized.
- Present conclusions in language appropriate to each stakeholder group. Be truthful about limitations, disagreements, and uncertainty.
- Share only information the recipient is authorized to receive. Redact sensitive logs, machine details, credentials, and personal information; do not publish secrets to obtain transparency. Follow the applicable privacy and retention rules.
- Treat the **Case for Ethics** (informative Annex I) as a logical, auditable view assembled from existing Issues and evidence. Do not create another competing SSOT.

### 8. Reassess on material change (Clause 10)

Reopen the relevant assessment when the context of use, impacted stakeholder group, autonomy level, data handling, control assumptions, observed failures, regulatory constraints, or residual risk materially changes. Preserve prior decisions and evidence rather than overwriting history.

## Required output contract

For a substantive review, report:

1. **Scope and authority:** SOI, context, task boundary, governing Issue IDs, decision authority, sources reviewed, and missing sources.
2. **Applicability:** material concerns or a justified no-material-impact finding; distinguish facts from assumptions.
3. **Stakeholders and values:** consulted versus inferred stakeholders; benefits, harms, duties, agency, priorities, and value conflicts.
4. **Requirements and traceability:** existing authoritative Issue references, proposed changes, acceptance criteria, and verification methods.
5. **Risk and design:** alternatives, controls, residual risks, uncertainty, and unresolved decisions.
6. **Evidence:** actual tests, HIL where required, stakeholder validation, and gaps; never invent a PASS verdict.
7. **Disposition:** what can proceed under existing authorization, what requires human approval, and the next smallest legitimate step.

For a clearly minor change, use a concise screening finding instead of generating empty forms. Keep non-applicability and incomplete assessments explicit; neither constitutes IEEE 7000 conformance.

## Stop and escalation rules

Stop the affected action and request direction when:

- The change requires Issue creation/modification, implementation, hardware activity, or communication beyond granted authority.
- A relevant governing Issue, accepted requirement, applicable instruction, or evidence source is unavailable or contradictory and the uncertainty could change the decision.
- Important stakeholders disagree, approval is absent, or a material risk has no authorized owner or acceptance.
- The proposed action would weaken a requirement, test scope, CI gate, safety safeguard, evidence standard, or restoration invariant.
- A claim of IEEE 7000 conformance is requested without an evidence-backed assessment of the outcomes or tasks across Clauses 7-11, using the conformance options described in Clause 4.

Continue permitted read-only investigation and clearly separate findings from blocked or unauthorized changes.

## Standards interpretation and non-goals

- IEEE 7000-2021: Clause 7 (context and control), Clause 8 (values and priorities), Clause 9 (ethical requirements), Clause 10 (risk-based design), Clause 11 (transparency). Clause 4 defines conformance criteria. Annex F (AI control) and Annex I (Case for Ethics) are informative.
- This skill is **IEEE 7000-aligned guidance**, not proof that a project, agent, or product conforms to that standard or is ethically acceptable.
- Apply IEEE 7001 (autonomous-system transparency) and IEEE 7002 (privacy engineering) only when their editions, scope, and requirements are separately established; do not silently attribute their provisions to IEEE 7000.
- Do not substitute ethical process analysis for protocol conformance (for example IEEE 1588, IEEE 802.1AS, IEEE 1722), hardware validation, cybersecurity engineering, applicable law, or authorized human judgment.
