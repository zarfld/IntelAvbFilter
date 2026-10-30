---
name: intelavbfilter-testing
description: >-
  Canonical IntelAvbFilter testing and TDD-HIL contract. Use for any task that
  creates, modifies, reviews, builds, runs, debugs or interprets tests; TDD and
  bugfix verification; HIL and CI test gates (hil.yml, Run-Tests-CI.ps1 suites);
  driver regression testing; test runners, timeouts and exit codes; adapter
  discovery and multi-adapter execution; hardware capabilities and
  NOT_SUPPORTED / NOT_IMPLEMENTED verdicts; test failures and coverage gaps;
  hardware-state baseline, cleanup and restoration; Debug/Release suite
  completeness; and any claim of RED, GREEN or verification evidence.
user-invocable: true
disable-model-invocation: false
---

# IntelAvbFilter Testing and TDD-HIL Contract (canonical)

This file is the **single normative cross-agent testing contract** for IntelAvbFilter.

| Consumer | How it reaches this file |
|---|---|
| GitHub Copilot | discovers `.github/skills/intelavbfilter-testing/` directly; `AGENTS.md`, `.github/copilot-instructions.md` §6 and `.github/instructions/tests.instructions.md` route here |
| Claude Code | `.claude/skills/intelavbfilter-testing/SKILL.md` (thin loader, no rules of its own) reads and applies this file; root `claude.md` imports `AGENTS.md` and requires the skill |

Companion files in this directory:

- [`reference.md`](./reference.md) — **HISTORICAL, non-normative.** Preserved previous skill text. Its adapter-specific binaries, runner commands and "CI has no hardware" statements are superseded by this file and by [`run-tests`](../run-tests/SKILL.md).
- [`scenarios.md`](./scenarios.md) — activation and behaviour scenarios for reviewing this contract. They are review aids, not evidence that the contract works.

**Guidance is not enforcement.** This contract tells an agent what to do. Only the executable anchors named below (runner gates, offline tests, CI) enforce anything, and only for what they actually check. Never report a rule as enforced because it is written here.

## 0. Authority and precedence

1. Accepted requirements in GitHub Issues (lifecycle SSOT) and Q's recorded decisions.
2. This contract.
3. The executable contracts it names — they define *current* behaviour and the canonical vocabulary:
   - verdicts: [`docs/testing/test-verdict-contract.md`](../../../docs/testing/test-verdict-contract.md), `tests/common/avb_verdict.h`, `tools/lib/TestVerdict.ps1`, runner statuses `ConvertTo-AvbRunnerVerdict` and overall `Get-AvbRunOverall` in `tools/test/Run-Tests-CI.ps1`;
   - suites, safety classes, coverage gaps, hazard holds: `$UnitTests` / `$Hardware*Tests`, `$HilTestSafety`, `$Hardware*CoverageGaps`, `$HilHazardHolds` in `tools/test/Run-Tests-CI.ps1`;
   - traceability syntax: current issue templates, CI validators and the `lifecycle-traceability` skill.
4. Historical material (`reference.md`, dated analyses under `tools/test/*.md`, generated inventories) — never normative.

Executable behaviour that contradicts an accepted requirement is a **defect to report**, not the authority. On any conflict: name both sources, state which one you follow and why, and ask Q when the choice is not already decided.

## Routing

| Need | Skill / file |
|---|---|
| Exact runner invocation, parameters, logs | [`run-tests`](../run-tests/SKILL.md) |
| TDD cycle mechanics | [`test-driven-development`](../test-driven-development/SKILL.md) |
| Completion / GREEN claims | [`verification-before-completion`](../verification-before-completion/SKILL.md) |
| Issue / TEST relationship syntax | [`lifecycle-traceability`](../lifecycle-traceability/SKILL.md) |
| Cross-phase V&V artifact planning | [`testing-lifecycle-guidance`](../testing-lifecycle-guidance/SKILL.md) |
| Register constants / magic numbers | [`ssot-and-magic-numbers`](../ssot-and-magic-numbers/SKILL.md) |
| Kernel log capture | [`DbgView`](../DbgView/SKILL.md) |

## A. Dynamic adapter discovery

- Discover **all** supported adapters at run time: tests through `IOCTL_AVB_ENUM_ADAPTERS` / `AvbEnumerateAdapters()` (`tests/common/avb_test_common.h`); the runner through `Get-AvbHilSupportedDids` (derived from `AvbGetIntelDeviceType()` + `include/intel_pci_ids.h`).
- Execute every applicable testcase on **every** adapter that advertises the required capability. Never hard-code expected DIDs, models, adapter counts, adapter indices or machine-specific target lists — not in tests, runners, workflows or evidence expectations.
- Applicability, expected behaviour, setup, assertions and cleanup are never chosen by VID, DID, model, name or family. Call the generic interface; the driver/HAL decides support. Device knowledge lives in the driver/HAL and in capability verification, not in tests.
- Adapter identity is allowed **only** for targeting, correlation and reporting (`AVB_HIL_TARGET_DID`, `/adapter:N`, `did=0x…`).
- Adapters with identical DIDs must be distinguished by a unique, verified PnP instance / PCI location or another independently verified mapping. Until that exists, the runner reports `TC-INFRA-ADAPTER-003` **BLOCKED** — that is missing coverage, never "not applicable".
- Prove that iterations hit **different** adapter instances (distinct identity in each per-adapter line, or per-adapter state evidence). Re-opening the same bound/default adapter N times is one adapter tested N times.
- Hex identifiers are parsed once, at the infrastructure boundary, by `ConvertTo-AvbDid` (Run-Tests-CI.ps1). An unquoted `0x125B` arrives in PowerShell as the integer 4699 and must not be re-read as hex text. Any new parsing path needs a regression test (precedent: `tests/unit/ci/test_hil_baseline_evidence.ps1`).
- An incorrectly advertised capability is a **defect** (`FAIL`), never a reason to remove coverage.

## B. Verdict correctness

Use the repository taxonomy exactly; do not invent parallel schemes. Mapping of this contract's terms:

| Situation | Canonical verdict | Gate effect |
|---|---|---|
| Capability for **that operation** evidenced absent, driver returns `STATUS_NOT_SUPPORTED` | `SKIP_UNSUPPORTED` (per adapter, with evidence) | green for that adapter |
| Capability advertised/present but operation fails, wrong payload, or `NOT_SUPPORTED` | `FAIL` | red |
| Capability not determinable from an authoritative per-operation contract | `INCONCLUSIVE` | red (mandatory) |
| Driver returns `STATUS_NOT_IMPLEMENTED` | `SKIP_NOT_IMPLEMENTED` → runner `INCOMPLETE` | non-passing; implementation gap stays visible |
| Unmet prerequisite / unsafe precondition | `BLOCKED` / `FAILED_SETUP` | red |
| Missing executable, deployment, permissions, driver interface, runner error | `INFRA_FAILURE`, `FAILED(BUILD)`, `BLOCKED(PROVENANCE)` | red — **never** hardware unsupported |
| Timeout | `FAILED(TIMEOUT)` | red, halts later mutations |
| Cleanup failed / restoration unverifiable | `CLEANUP_FAILED` / `INCONCLUSIVE_RESTORATION` | red, halts later mutations |
| Required test not executed | `COVERAGE-GAP` (`TC-HIL-COVERAGE-GAP-001`) | suite `INCOMPLETE` |
| Hardware suite with zero passes | overall `NOT_VERIFIED` | non-passing |

- `SKIP` without suffix is only a runtime not-applicable (e.g. link down), never a capability claim.
- `GetLastError()` is lossy for `METHOD_BUFFERED` errors; verdicts that depend on the exact status capture the raw NTSTATUS (`avb_verdict.h`).
- Every per-adapter failure propagates to the binary exit code, the suite verdict and the overall verdict. Mixed adapter results are never collapsed into success.
- Timeout, unresolved cleanup, inconclusive evidence and non-execution never satisfy a mandatory gate.

## C. Complete test inventory

- Reconcile sources → `Build-Tests.ps1` targets → build manifest → produced executables → runner registry / suite lists / gap tables → executed testcases → results document → report. `tests/unit/ci/test_artifact_completeness.ps1` (TEST-COMPLETE) enforces the build-target ↔ registry part; per-run reconciliation is the runner's results document (`-ResultsJson`, schema `avb-runner-results/1`).
- Every active testcase is built and accounted for; a missing executable is a failure row, never silence.
- **Debug and Release each need their own evidence** (hil.yml `hil-debug` and `hil-release`; local: `LocalSuites.ps1 -Configurations Debug,Release`).
- Never de-list a test, add a permanent exclusion/allowlist, or add a separate lane to get green. A test that cannot run yet is **repaired** or stays a visible coverage-gap row with its reason.
- Do not shorten acceptance criteria (duration, sample count, tolerance) to fit a timeout; declare the gap instead (precedent: `test_vv_corr_001_stability`).
- Reuse the existing Unit / Integration / Hardware{Unit,Integration,Mutation,Mandatory} classification and the safety classes ReadOnly / Init / Mutating / Mutating:Target / Unsafe / Unknown.
- A build or suite failure must not prevent later independent tests or suites from running (hil.yml runs every suite after a successful deployment).

## D. TDD with real hardware

For hardware-dependent behaviour changes:

1. Identify the governing issue, acceptance criteria and TEST traceability (`Verifies: #N`).
2. Define the observable behaviour and the per-operation capability it depends on.
3. Write or update the failing testcase first.
4. Observe **actual RED on physical applicable hardware** (or reuse existing validated HIL evidence that demonstrably applies — cite it).
5. Make the smallest implementation change.
6. Observe **actual GREEN against the intended driver build** (see G).
7. Run the relevant regressions and the required suites, Debug and Release.
8. Report exact outcomes and every remaining gap.

Offline fixtures, mocks and sandboxed runner tests validate pure logic, parsers, ABI and orchestration only. They are supplementary. Hardware-behaviour fixtures must trace to observed evidence (adapter, driver build, configuration, values, artifact) or be marked **UNVERIFIED**. Never claim hardware RED/GREEN from a mock, synthetic register values, a compile, or an offline test. If HIL has not run, say **"HIL verification pending"** / BLOCKED.

## E. Hardware-state establishment and restoration

Every state-changing test must:

- establish and verify its prerequisites;
- capture an independently meaningful baseline **before** the first mutation;
- identify every real mutation, including implicit ones from `OPEN_ADAPTER` (re-points the global default adapter), `INIT_DEVICE` (re-initialises the PTP block) and other open/initialisation side effects — "read-only" is a claim to be proven from the IOCTLs actually sent, not assumed;
- execute the operation;
- restore the relevant state on success, failure, exception and timeout;
- verify restoration **independently against hardware**, preserving cleanup evidence even when the primary assertion failed.

Use invariants that fit time-varying state (PHC keeps running, counters move, some registers reset): compare rate/offset/configuration, not equality of a running clock. The runner currently re-reads only adapter enumeration, `TIMINCA` and `TSAUXC`, and `$HilMutatedState` is empty — so any other mutated state is **unobserved**, and restoration of it cannot be claimed. That is an interface/design gap needing an explicit engineering decision, not a pass.

Distinguish and report exactly one of:

1. failed **before** any mutation, unchanged state independently shown → ordinary failure, no safety halt;
2. failed, restoration **verified** → ordinary failure;
3. cleanup failed or restoration uncertain → `CLEANUP_FAILED` / `INCONCLUSIVE_RESTORATION`.

After (1) or (2), continue independent tests. After (3), later unsafe mutations are blocked (runner `stateHalt`, `-PriorResultsDir`, machine safety record `%ProgramData%\IntelAvbFilter-hil-safety.json` via `tools/lib/HilLock.ps1`); every blocked case stays accounted for. Never convert a cleanup failure into PASS/SKIP or clear the safety record without verified evidence and Q's authorization. An UNBASELINED record means "state unknown", not "hardware damaged".

## F. Timeouts and unattended execution

- Every unattended process is bounded (`Run-Tests-CI.ps1 -TestTimeoutSeconds` / per-entry `$HilTestTimeoutSeconds`; `Run-Tests-Elevated.ps1 -TimeoutSeconds`, default 1800).
- No unbounded prompts (`system("pause")`, `getchar()`, `Read-Host`); guarded by `tests/unit/ioctl/test_no_interactive_waits.ps1` (TEST-NOWAIT-001).
- A timeout is a recorded failure; verify process termination, outstanding I/O and device state before continuing.
- An optional operator interruption after an ordinary failure defaults to **continue** after 30 s (`Read-AvbTimedChoice`). CI is fully unattended.
- Preserve real exit codes through wrappers, pipelines and nested scripts (`tools/lib/NativeExit.ps1`; `tests/unit/build/test_native_exit_codes.ps1`). A missing exit code is a failure.

## G. Provenance and evidence

Never claim GREEN without evidence identifying: source commit and worktree state; build configuration; test executable identity (manifest SHA256); intended **and** deployed driver identity (`Test-DriverPreFlight.ps1`, hil.yml `hil-run-identity.json`); evidence of the running driver; HIL host and uniquely identified adapter; reported capabilities; exact testcase, verdict and reason; safety class and mutation classification; baseline, operations and cleanup outcome; log / results / trace paths.

- Service `RUNNING` or an installed file proves neither device accessibility nor which image is loaded (hil.yml itself records "loaded image not hashed").
- A HIL run against a driver other than the change under test is not evidence for that change.
- Results carry an evidence class (`ci`, `local`, `local-pre-commit`, `sandbox`). Keep current execution evidence separate from historical/imported logs; never re-label an old log as a new pass.

## H. Existing infrastructure and authority

- Reuse the current scripts, fixtures, capability contract, verdict taxonomy, results documents and traceability outputs. Do not add another framework, matrix, runner, lane, skip policy or source of truth.
- `intel-ethernet-regs` (submodule, `gen/*_regs.h`) is the only register-definition source; no register constants in tests (`Test-REGS-002-MagicNumberDetection.ps1`).
- GitHub Issues are the lifecycle SSOT. TEST → requirement is `Verifies: #N`; parent lineage is `Traces to: #N`; backward links are `Verified by: #N` and are added append-only (`github-issue-integrity`).

## I. Elevated execution and hardware authorization

- Read the actual script and its `param()` block before proposing a command; use only parameters that exist (see `run-tests`).
- Give Q exact, copy-paste-ready PowerShell, plus prerequisites, expected mutations, cleanup behaviour, timeout/stop conditions and evidence paths.
- Let Q run elevated commands when elevation is needed; wait for the real results before claiming anything.
- Never initiate, without explicit authorization: driver install/replace/restart, adapter disable/enable, firmware change, `AVB_HIL_AUTHORIZE_UNSAFE`, S3/reboot, or safety-record clearing.

## Anti-patterns (observed failures → required behaviour)

| Observed failure | Required behaviour | Executable guard today |
|---|---|---|
| Same bound adapter re-tested while "iterating all adapters" | Distinct adapter identity per iteration, proven in output | none generic — review |
| Double hex conversion of DIDs (`0x4699` targeted) | One parser (`ConvertTo-AvbDid`), regression-tested | `test_hil_baseline_evidence.ps1` (unquoted DID case) |
| Missing driver implementation reported as unsupported hardware | `SKIP_NOT_IMPLEMENTED` → `INCOMPLETE` | `avb_verdict.h`, `test_verdict_contract` |
| Running service taken as proof the control device works | Device-node + identity + functional evidence | `Test-DriverPreFlight.ps1` (partial) |
| Timestamp assertion on stale/identical values | Assert a real new transaction (fresh sample, provenance) | `test_tx_corr_verdict` (TX provenance only) |
| Cleanup declared unsafe although failure preceded mutation and state was shown unchanged | Classify per E(1) | runner state check (limited observables) |
| "Read-only" assumed without reading IOCTL/init effects | Classify from IOCTLs actually sent | `test_hil_safety_gate.ps1` (registry vs sources) |
| Results lost through PowerShell exit-code handling | `NativeExit.ps1`; missing code = failure | `test_native_exit_codes.ps1` |
| Old logs reported as new passes | Evidence class + timestamps + identity | results document `evidence` field |
| Tests excluded because cleanup/execution needs repair | Repair, or keep a visible gap row | `test_artifact_completeness.ps1`, gap tables |
| Hardware tests disabled / separate lane for green CI | Forbidden | review only |
| HIL against a different driver build | Deployed == staged SHA256 before suites | hil.yml deploy + acceptance |
| Registered tests not compiled or not dispatched | Inventory reconciliation (C) | `test_artifact_completeness.ps1` |

## Reporting

Report per testcase and adapter: `TC-ID/adapter:N`, verdict, reason, DID/instance, configuration, evidence class and artifact path; then suite and overall verdicts with counts for every verdict category. State plainly what was **not** run and why. Never write "tests pass" without citing the results document or log you read in this session.
