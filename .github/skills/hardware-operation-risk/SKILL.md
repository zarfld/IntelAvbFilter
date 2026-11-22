
---
name: hardware-operation-risk
description: >-
  Use when an agent encounters an unsafe, mutating,
  destructive, disruptive, or potentially hazardous
  hardware operation; register writes; HIL execution
  refusals; safety gates; fault injection; or hardware
  defect reproduction.
user-invocable: true
---

# Hardware Operation Risk Governance

## Purpose

Enable evidence-driven hardware development and HIL-TDD
without unjustified restrictions on required operations.

Risk classification describes the effects of an
operation. It does not independently prohibit execution.

## Governing authority

- Follow accepted GitHub requirements and decisions.
- For IntelAvbFilter, load `intelavbfilter-testing`.
- Preserve the canonical runner and evidence contract.
- Respect the task's authorized scope.
- Do not introduce competing execution policies.

## 1. Classify the actual operation

Determine:

- The intended functional purpose.
- The hardware capabilities required.
- The registers or resources affected.
- Whether effects are transient or persistent.
- Whether the operation is reversible.
- Whether it may interrupt device or host operation.
- The required recovery and verification strategy.
- The existing authorization scope.

Distinguish:

- FUNCTIONAL_MUTATION
- INITIALIZATION_OR_RECONFIGURATION
- DISRUPTIVE_LIFECYCLE
- FAULT_INJECTION
- PERSISTENT_OR_IRREVERSIBLE
- UNKNOWN_EFFECT

These categories describe operational effects.
They do not replace existing runner safety classes.

## 2. Apply execution policy

A required, applicable hardware testcase SHALL NOT
be blocked merely because it writes registers,
modifies device state, reproduces a hardware defect,
or carries an Unsafe classification.

Normal hardware register writes and initialization
are expected parts of driver functionality.

Run applicable tests through the authorized HIL
environment and canonical test runner.

Do not introduce new approval requirements solely
because the operation has a mutating classification.

Separate authorization is required where the
operation exceeds the established execution scope,
especially for persistent or irreversible actions.

Unknown effects require investigation. Uncertainty
must not become a permanent coverage exemption.

## 3. Testcase lifecycle responsibility

Each applicable testcase owns:

1. Prerequisite setup and verification.
2. Independent pre-operation state evidence.
3. Execution of the specified operation.
4. Functional assertions and actual verdicts.
5. Bounded cleanup and recovery attempts.
6. Independent final-state verification.
7. Preservation of all original evidence.

If setup fails, report the actual setup failure.
Do not claim dependent assertions were executed.

A functional failure is valid HIL RED evidence.
Do not suppress it because the operation was mutating.

A cleanup or restoration failure remains nonpassing.
Continue independently executable testcases, each
establishing its own prerequisites and baseline.

Never claim an unverified machine state is restored.

## 4. Mandatory blocking-defect handling

When a required testcase is not executed:

1. Record the exact refusal and its origin.
2. Distinguish runner refusal, testcase setup failure,
   capability absence, missing authorization, and
   actual execution failure.
3. Identify the governing requirement and TEST issue.
4. Determine whether the refusal is justified by
   a concrete, independently verifiable condition.
5. Search GitHub Issues for an existing root-cause defect.
6. Attach new occurrence evidence to an existing defect,
   or create a new issue for a distinct actionable cause.
7. Preserve the incomplete/nonpassing coverage verdict.

The defect record must identify:

- Testcase and governing issue.
- Expected operation and actual blocking mechanism.
- Source revision and executable identity.
- Intended and deployed driver identity.
- Target hardware identity and capability evidence.
- Exact result, log and originating code location.
- Whether execution was attempted.
- Setup, mutation, cleanup and final-state evidence.
- Root-cause hypothesis and required correction.

Do not fabricate an issue link. If issue creation is
unavailable, report DEFECT-PENDING with the evidence.

## 5. Prohibited workarounds

Never make a required test appear successful by:

- Removing it from the test inventory.
- Reclassifying it as ReadOnly or unsupported.
- Weakening assertions or acceptance criteria.
- Adding arbitrary adapter/model exemptions.
- Introducing operator-only or separate test lanes
  solely to avoid a legitimate hardware operation.
- Replacing physical RED/GREEN evidence with mocks.
- Treating BLOCKED as proof of hardware correctness.
- Silently bypassing real authorization boundaries.

## 6. Review and completion

A blocking defect is corrected only when:

- The unjustified veto is removed.
- Existing hardware authorization remains valid.
- Regression tests demonstrate correct dispatch.
- The physical operation is actually attempted.
- Functional and cleanup outcomes are retained.
- Required Debug/Release evidence is verified.
- The original governing TEST retains its coverage.
- The responsible issue and PR traceability are intact.

A passing offline test is not physical HIL evidence.
