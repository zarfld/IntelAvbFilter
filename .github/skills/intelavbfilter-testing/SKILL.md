---
name: intelavbfilter-testing
description: >-
  Mandatory IntelAvbFilter testing and TDD-HIL engineering skill.
  Use when creating, modifying, reviewing, debugging, building,
  executing or interpreting tests, test runners, CI/HIL workflows,
  hardware capabilities, adapter discovery, test verdicts, hardware
  state restoration, or verification evidence. Enforces real hardware
  RED/GREEN TDD, capability-driven testing of all supported adapters,
  complete test inventory, cleanup invariants, traceability and
  evidence-backed reporting.
when_to_use: >-
  Activate for any IntelAvbFilter task involving tests, TDD, HIL,
  verification, test failures, Debug/Release suites, test coverage,
  test infrastructure, CI test gates, driver regression testing,
user-invocable: true
disable-model-invocation: false
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

# TASK: Establish authoritative IntelAvbFilter Testing and TDD-HIL Skill

## ROLE

You are a senior Windows NDIS/kernel-driver, IEEE 1588/PTP, TSN, hardware-abstraction and hardware-in-the-loop verification engineer.

You are also responsible for correctly integrating repository-specific agent skills into Claude Code and GitHub Copilot.

## OBJECTIVE

Create and integrate an effective, enforceable testing and TDD-HIL skill for IntelAvbFilter.

The skill must prevent previously observed violations of repository testing rules and ensure that Claude:

1. Loads and follows the applicable authoritative instructions.
2. Reuses the existing testing, building, reporting and CI infrastructure.
3. Follows real hardware RED/GREEN TDD.
4. Tests every applicable testcase on every available supported adapter.
5. Preserves complete test inventory and result accountability.
6. Establishes, restores and verifies hardware state.
7. Distinguishes genuine unsupported hardware from driver, test and infrastructure failures.
8. Reports actual hardware evidence and never manufactures successful results.

This task concerns agent guidance and its integration. Do not modify production driver behavior or execute hardware-mutating operations as part of creating the skill.

## CONTEXT

Previous review of remote branch `fix/hil-ci-suite-20261021` at commit `4b2af29` identified:

- `AGENTS.md` already routes testing to `intelavbfilter-testing` and `run-tests`.
- `.github/skills/intelavbfilter-testing/SKILL.md` is a valid entry point but contains incomplete mandatory invariants.
- Its `reference.md` preserves historical instructions that include outdated adapter-specific examples and runner assumptions.
- `.github/skills/run-tests/SKILL.md` incorrectly describes CI as hardware-independent, despite the real self-hosted HIL workflow.
- `Run-Tests-CI.ps1` currently contains explicit coverage-gap lists and DID-based targeting mechanisms.
- No root `CLAUDE.md` or `.claude/skills/` appeared in that remote branch.

Your local checkout may contain more recent, uncommitted changes.

First inspect `git status`, branch, HEAD, relevant local modifications and repository guidance. Preserve all existing work.

Do not assume the remote snapshot matches the current checkout.

## REQUIRED SOURCE INSPECTION

Read the applicable portions of:

- `AGENTS.md`
- `.github/copilot-instructions.md`
- `.github/instructions/tests.instructions.md`
- `.github/docs/TESTS-INSTRUCTIONS-REFERENCE.md`
- `.github/skills/intelavbfilter-testing/SKILL.md`
- `.github/skills/intelavbfilter-testing/reference.md`
- `.github/skills/run-tests/SKILL.md`
- `.github/skills/test-driven-development/SKILL.md`
- `.github/skills/verification-before-completion/SKILL.md`
- `.github/skills/lifecycle-traceability/SKILL.md`
- `tools/build/Build-Tests.ps1`
- `tools/test/Run-Tests-CI.ps1`
- `tools/test/Run-Tests.ps1`
- `tools/test/Run-Tests-Elevated.ps1`
- The active HIL workflows and suite definitions
- Relevant existing test fixtures, manifests, evaluators and result parsers

Consult other relevant guidance discovered through these files.

Determine which documents are current, historical, conflicting or incomplete.

Do not treat currently defective executable behavior as authoritative over accepted requirements.

## REQUIRED STRUCTURE

1. Establish `.github/skills/intelavbfilter-testing/SKILL.md` as the canonical cross-agent repository testing contract.
2. Create `.claude/skills/intelavbfilter-testing/SKILL.md` as a Claude-native entry point that loads and applies the canonical contract. Avoid duplicating the complete contract.
3. Create a minimal root `CLAUDE.md` containing `@AGENTS.md` and Claude-specific instructions requiring the testing skill for testing, TDD, HIL, test-runner and test-CI work.
4. Correct obsolete or conflicting execution guidance in the existing `run-tests` skill.
5. Clearly distinguish historical reference material from current normative rules.
6. Preserve existing test infrastructure and traceability SSOT.

Use valid Claude Code / Agent Skills YAML frontmatter.

The Claude-facing skill description must cover test creation, test modification, TDD, bugfix verification, HIL, CI testing, driver testing, test runners, failures, cleanup, capabilities and test-result interpretation.

Allow automatic skill invocation and manual `/intelavbfilter-testing` invocation.

Do not grant additional tool permissions merely to make the skill convenient.

## MANDATORY CONTRACT

### A. Dynamic adapter discovery

Every HIL machine can have a different adapter configuration.

- Discover ALL available supported adapters dynamically.
- Execute EVERY applicable testcase on EVERY adapter advertising the required capabilities.
- Never hardcode expected DIDs, adapter models, adapter counts, adapter indices or machine-specific target lists.
- Never choose applicability, expected behavior, setup, assertions or cleanup based on model, VID, DID, name or adapter family.
- Adapter identity is permitted only for reliable hardware targeting, correlation and reporting.
- Distinguish adapters with identical DIDs using unique, verified PnP/PCI identity or other independently verified mapping.
- Verify that testcase iterations actually target different adapter instances.
- Correct hexadecimal identifier parsing at the appropriate infrastructure layer and require regression coverage.
- Keep device-specific hardware knowledge in the driver/HAL and capability verification layer.

Capability advertisement must itself be verifiable. An incorrectly advertised capability is a defect, not a legitimate excuse to remove test coverage.

### B. Verdict correctness

- Missing hardware capability: explicit evidence-backed NOT_SUPPORTED for the affected testcase/adapter.
- Advertised capability with malfunction: FAILED.
- Missing prerequisite: explicit setup/infrastructure failure unless independently proven to be a genuine unsupported capability.
- Missing executable, deployment, permissions, runner or driver interface: failure or blocked coverage, never hardware NOT_SUPPORTED.
- Missing driver implementation: expose the implementation gap. Preserve any repository-defined SKIP_NOT_IMPLEMENTED classification without treating it as a passing or genuinely unsupported hardware result.
- Timeout, unresolved cleanup, inconclusive evidence and nonexecution cannot satisfy a mandatory test gate.
- Preserve the repository's canonical machine-readable verdict taxonomy; do not invent parallel schemes.

All failures must propagate into the relevant suite and overall completeness verdict.

### C. Complete test inventory

The objective is complete execution and accounting, including failing tests.

- Reconcile test sources, registrations, build manifest, produced executables, dispatch lists, testcase execution and final reports.
- Every active testcase must be built and accounted for.
- Both Debug and Release configurations require explicit evidence.
- Missing tests must never disappear from reports.
- Preserve every failure and every coverage gap.
- Do not create permanent test exclusions, allowlists or redundant verification lanes.
- Do not shorten acceptance criteria merely to make execution fit an existing timeout.
- Reuse established unit, integration, acceptance, hardware and CI classifications.
- A build or suite failure must not automatically prevent subsequent independent tests from executing.

### D. TDD with real hardware

For hardware-dependent behavior changes:

1. Identify the governing issue, acceptance criteria and test traceability.
2. Define the observable behavior and supported hardware capabilities.
3. Create or update the failing testcase first.
4. Verify actual RED on physical applicable hardware.
5. Make the smallest implementation change.
6. Verify actual GREEN against the intended driver build.
7. Run the relevant regression tests and complete required suites.
8. Report exact outcomes and remaining gaps.

Offline fixtures and mocks may validate pure software logic, parsers, ABI handling and orchestration.

They are supplementary evidence only.

Never claim hardware RED/GREEN based on mocked devices, synthetic register values, a successful compilation or an offline-only test.

If hardware execution has not occurred, state that HIL verification is pending.

### E. Hardware-state restoration

Every state-changing test must:

- Establish and verify its prerequisites.
- Capture an independently meaningful initial baseline.
- Identify actual hardware mutations, including implicit IOCTL/open/initialization side effects.
- Execute the intended operation.
- Restore the initial relevant hardware state on success, failure, exception and timeout.
- Independently verify restoration against real hardware.
- Preserve cleanup evidence even when the primary assertion failed.

Account for time-varying hardware state, PHC progression and reset-dependent registers; define meaningful before/after invariants rather than requiring equality of naturally changing counters.

Do not claim verified restoration when the available driver interface cannot observe or restore the relevant state.

Such a limitation is an interface/design problem requiring an explicit engineering decision.

Distinguish:

- Failure before any mutation with independently proven unchanged state.
- Test failure followed by verified restoration.
- Cleanup failure or uncertain restoration.

Continue independent tests after ordinary failures.

If hardware restoration is not independently verified, block subsequent unsafe mutations that could compound the problem, preserve all evidence and account for every blocked case.

Do not convert a genuine cleanup failure into PASS, SKIP or a cleared safety record.

Do not infer physical hardware damage merely from an UNBASELINED record.

### F. Timeouts and unattended execution

- All unattended test processes must have bounded execution.
- No unbounded interactive prompts, including `system("pause")`.
- A testcase timeout is a recorded failure or incomplete result.
- Verify process termination, outstanding operations and relevant device state before continuing after a timeout.
- Normal suite failure must not automatically abort later independent suites.
- If an optional interactive interruption is offered after ordinary failure, the default after 30 seconds is continue.
- CI must run unattended.
- Preserve actual process exit codes through PowerShell wrappers, pipelines and nested scripts.

### G. Provenance and hardware evidence

Never claim GREEN without actual evidence identifying:

- Source commit/worktree state.
- Build configuration and test executable identity.
- Intended and deployed driver build identity.
- Sufficient evidence of the running driver identity.
- HIL host and uniquely identified adapter.
- Reported relevant capabilities.
- Exact testcase, result and failure reason.
- Prerequisite and mutation classification.
- Initial state, operations and cleanup outcome.
- Log, trace and result artifact paths.

Service RUNNING or installed-file existence alone does not prove device accessibility or the identity of the loaded implementation.

Separate current execution evidence from historical/imported logs.

### H. Existing infrastructure and authority

Reuse current scripts, fixtures, capabilities, CI taxonomy, traceability outputs, logs and manifests.

Do not introduce another framework, redundant matrix, parallel runner, permanent skip policy or independent source of truth.

Preserve `intel-ethernet-regs` as the authoritative register-definition source.

Do not introduce register constants in tests or duplicate device knowledge outside the established abstraction.

GitHub Issues remain the lifecycle SSOT. Preserve exact repository traceability syntax, including the distinction between `Traces to`, `Verifies` and `Verified by`.

### I. Elevated execution and hardware authorization

When elevated hardware execution is required:

- Inspect the actual script and its supported parameters first.
- Provide exact, copy-paste-ready PowerShell commands.
- State prerequisites, expected mutations, cleanup behavior, timeout/stop conditions and evidence paths.
- Let the user perform elevated execution when required.
- Wait for the actual results before claiming GREEN.

Never initiate unauthorized driver replacement, restart, installation, adapter disablement, firmware modification or safety-record clearing.

## SPECIFIC ANTI-PATTERNS TO ADDRESS

Explicitly incorporate lessons from these previous failures:

- Repeatedly testing the same bound adapter while supposedly iterating all adapters.
- Double hexadecimal conversion of device IDs.
- Mistaking missing driver implementation for unsupported hardware.
- Treating a running Windows service as proof that the control device works.
- Timestamp assertions comparing stale or identical values instead of a real transaction.
- Incorrectly declaring cleanup unsafe when failure occurred before mutation and unchanged state was independently verified.
- Assuming a read-only test is harmless without examining actual IOCTL and initialization effects.
- Losing results through PowerShell exit-code handling.
- Converting previous logs into newly reported passes.
- Excluding tests whose cleanup or execution mechanism still needs repair.
- Disabling hardware tests or introducing separate lanes to obtain green CI.
- Running HIL against a different driver build than the change under test.
- Failing to compile or dispatch registered testcases.

## VALIDATION

Perform offline validation of the new and updated guidance.

Check:

1. YAML frontmatter parses correctly.
2. Claude discovers the skill.
3. Automatic invocation is enabled.
4. `AGENTS.md` is included in Claude's effective project instructions.
5. All referenced files exist.
6. No conflicting normative instructions remain.
7. No deprecated examples are presented as canonical commands.
8. No test-runner command assumes parameters that its current implementation lacks.
9. No duplicated normative testing contract was created.
10. Existing repository static validation infrastructure is reused.

Include realistic activation/behavior scenarios:

- Adding a PTP HIL testcase.
- Testing two adapters with identical DIDs.
- Testing a new hardware capability.
- Running complete Debug and Release suites.
- A failed testcase with successful restoration.
- A failed testcase with uncertain restoration.
- A missing test executable.
- Driver reports NOT_IMPLEMENTED despite advertised support.
- A HIL run using a stale driver.
- A test script containing fixed DIDs.

Also include negative activation cases for unrelated repository tasks.

Do not simulate hardware results as proof of this skill's effectiveness.

## SCOPE AND DELIVERABLES

For this iteration:

- Change instruction, skill and directly related guidance files only.
- Add minimal offline skill/contract validation using existing tooling if appropriate.
- Do not change driver code, active hardware test behavior or CI gates merely to complete this guidance task.
- Report active test/runner compliance violations separately with exact file/function references.
- Preserve uncommitted user work.
- Do not commit, push or perform hardware changes without explicit authorization.

Provide:

1. Skill file locations and complete frontmatter.
2. How Claude automatically discovers and loads the skill.
3. How Copilot uses the same canonical testing contract.
4. Updated guidance files and the conflicts corrected.
5. Offline validation commands and actual results.
6. Concrete remaining runner/test violations requiring separate fixes.
7. Any unresolved decisions that cannot be justified from existing authoritative evidence.

Do not claim the testing policy is enforced merely because the documentation exists. Distinguish model guidance from executable CI/test enforcement.
