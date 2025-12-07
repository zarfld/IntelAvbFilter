---
title: "Operational Profile (OP) Template"
specType: operational-profile
version: 1.0.0
status: template
author: template-system
date: 2025-11-05
description: "Template for authoring an IEEE 1633-aligned Operational Profile to drive reliability testing."
phase: 05-implementation
---

## Operational Profile (OP)

> Purpose: Define user/customer operations, modes, states, transitions, and their relative frequencies to support reliability planning, testing, and estimation (IEEE 1633 4.4, 5.1.1.3, 5.4; Annex F examples).

---

## 1. Metadata

```yaml
specType: operational-profile
standard: IEEE 1633-2016
phase: 05-implementation
status: draft
version: 0.1.0
author: [Your Name]
domain: [Product/Application Domain]
```

---

## 2. Actors and User Segments

- Primary actors (roles)
- User segments and proportion of overall use (e.g., Professionals 28%, Small Biz 42%, etc.)

---

## 3. Operations and Modes

List the top-level operations and modes with relative frequency.

| Operation/Mode | Description | Relative Frequency |
|----------------|-------------|--------------------|
| [Op/Mode] | [What it does] | [e.g., 48.6%] |

---

## 4. Mission Profiles

Define representative mission times/durations and sequences used for reliability calculations.

- Mission time(s): [e.g., 8h]
- Sequence archetypes: [Typical flows]

---

## 5. Behavioral Model (States/Transitions)

Represent the behavior as a Markov Chain Usage Model (MCUM) or equivalent FSM:

- States: [List of states]
- Transitions: [Event/action names]
- Constraints: [Guards/conditions]

Example transition entry:

```yaml
state: Stationary
on: Press Floor 1 Up
next: MovingUp
frequency: normal  # very-often|often|normal|seldom|rare (tool-specific; normalized per-state)
validation:
  expectedOutputs:
    - Arrive at Floor 1 Up
    - Stop at Floor 1
```

Attach/Link a model file if using a modeling tool.

---

## 6. Transition Frequencies and Normalization

- Assign relative frequencies to transitions per state; ensure outgoing transitions per state sum to 1.0 after normalization.
- Note any environment- or customer-specific variants (profiles A/B/C).

---

## 7. Coverage Targets

- Structural model coverage targets: states (%), transitions (%)
- Structural code coverage targets: statement, branch/decision, MCDC (if applicable)

---

## 8. Test Generation Mapping

- Method to generate tests from the OP (tooling, adapters)
- Mapping of abstract actions to executable test functions (self-contained adapters)
- Sample:

```yaml
abstractAction: Press Cabin Floor 1
adapter: tests/adapters/elevator/press_cabin_floor_1.ts
verifications:
  - assert currentDirection in [Up, Down, Stationary]
  - assert high/low floor selections updated per spec
```

---

## 9. Data Collection for Reliability Estimation

- Execution/duty time measurement method
- Failure logging format and severity classification
- Laplace/shape checks for trend (U/N/S-shaped) and stability

---

## 10. Variants and Evolution

- Incremental releases and how OP changes are versioned
- Impact analysis procedure for OP changes (update reliability test plan accordingly)

---

## 11. References

- Requirements (reliability/availability)
- Architecture quality scenarios
- SRPP
