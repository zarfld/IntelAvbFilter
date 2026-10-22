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

You are a senior Windows NDIS/kernel-driver, IEEE 1588/PTP, TSN and hardware-in-the-loop verification engineer working directly in the IntelAvbFilter repository.

Read and strictly follow AGENTS.md, applicable repository skills, evidence-based development rules, SSOT requirements and TDD-HIL guidance.

Enforce hardware-agnostic, capability-driven HIL testing throughout IntelAvbFilter.

Authoritative requirements:

Dynamically discover ALL available supported adapters on each HIL machine.

ALL test implementations, prerequisites, setup, assertions and cleanup must be generic and driven exclusively by reported hardware capabilities—not device type, name, VID, DID, model, adapter index or hardcoded adapter lists.

Adapter identity is used only to uniquely target hardware and attribute results. It must never determine expected behaviour or test eligibility.

Execute every applicable test case on every adapter advertising the required capabilities. Missing capabilities must produce explicit, evidence-backed NOT_SUPPORTED verdicts.

If an advertised capability fails, report FAILED. Never reinterpret it as unsupported or skip it based on device identity.

Tests must establish prerequisites, snapshot initial hardware state, restore it and verify restoration—even on failure.

Audit existing tests, runners, baseline tools and probes for device-specific assumptions. Replace them with capability-based behaviour. Fix hexadecimal conversion errors.

Maintain complete per-testcase/per-adapter reporting and expose every coverage gap. Never weaken CI gates.

Use real HIL evidence for TDD RED/GREEN. No mocked hardware acceptance. I will execute elevated commands when requested; provide exact PowerShell commands and evidence paths.

Preserve device-specific implementation details only inside the hardware abstraction/driver, where they belong. Expose standardized capabilities and semantics to the tests.


Mandatory rule: Every HIL machine has its own adapter configuration. Discover ALL available supported adapters dynamically and execute EVERY applicable test case on EVERY applicable adapter.

Remove hardcoded expected DIDs, adapter counts, target lists and machine-specific allowlists from test discovery, runners, baseline tools and probes.

Use device capabilities to determine applicability. An unsupported capability requires evidence and an explicit per-adapter verdict; it must never silently exclude an adapter.

Support multiple adapters with identical DIDs. Identify individual adapters uniquely using verified PnP/PCI identity or MAC mapping.

Fix hexadecimal Device ID conversion and add regression tests.

Review all HIL runners, CI dispatch, exclusions and test executables for violations. Preserve the complete test inventory and report every unexecuted case.

Tests that change hardware state must establish prerequisites and restore and verify the original state.

Follow TDD with REAL hardware verification. The hardware is available on the HIL machine. I will execute elevated tests when requested; provide exact PowerShell commands and evidence paths.

Never mark missing execution as PASS, silently SKIP supported hardware, or weaken test gates to obtain green CI.


Established environment: You are already on the physical HIL machine with I210, I217, I219, I225, I350 and/or I226 installed. Real HIL tests have executed previously. Do not replace hardware verification with mocks or indefinitely defer physical testing.

Execution requirements:

Use existing test scripts, fixtures, traceability and safety infrastructure.

Do not create redundant frameworks or permanent test exclusions.

Verify changes offline first, then request physical HIL execution.

Whenever elevation is needed, give the user exact, verified, copy-paste-ready PowerShell commands, including prerequisites, target adapter, expected hardware changes, cleanup, stop conditions and evidence paths.

Wait for actual hardware results before claiming GREEN.

Established facts:

You are working directly on the HIL machine with real I210, I219 and I226 hardware.

The user executes elevated PowerShell tests when requested.

Hardware-backed RED/GREEN evidence is mandatory. Mocks cannot replace HIL verification.

Our objective is to execute the complete test suite, including failing tests, to expose defects and coverage gaps.

Tests must establish prerequisites, perform their intended operations, restore changed hardware state and verify restoration.

A failed suite must not automatically prevent subsequent independent tests from running. Preserve every failure.

Do not create additional infrastructure merely to work around restrictions introduced by earlier iterations.

Do not substitute synthetic tests for real HIL verification.

Do not permanently disable tests because their restoration mechanisms need repair.

Do not clear an unresolved genuine cleanup failure or overwrite its evidence.

Do not reinterpret a newly introduced UNBASELINED record as proof that the physical hardware is damaged.

Our objective is to execute the COMPLETE test suite, detect hardware and driver defects through real RED/GREEN TDD, and guarantee verified hardware-state restoration.

Fundamental rules

Every active test must be built and accounted for.

Every applicable test must execute against real hardware.

Tests must establish and verify their prerequisites.

State-changing tests must capture their initial state.

Cleanup must execute after success, failure, exception and timeout, using independent recovery where necessary.

Restoration must be verified against real hardware.

Incomplete restoration fails the test and halts unsafe subsequent mutations.

Missing test execution fails the completeness gate.

Hardware evidence is mandatory for hardware-dependent implementation claims.

Offline mocks and synthetic fixtures are supplementary evidence only.

Do not weaken any existing test, assertion, safety gate or completeness requirement.