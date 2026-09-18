---
name: intelavbfilter-bugfix
description: "Use when investigating or fixing a defect in IntelAvbFilter, especially NDIS, IOCTL, PTP/TSN, MMIO, timing, crash, power-state, multi-adapter, or controller-specific failures. Orchestrates issue traceability, reproducible diagnosis, regression-test-first TDD, and verification on the Intel NICs physically installed in the HIL machine."
---

# IntelAvbFilter Bug Fixing

Fix the verified root cause and leave behind a regression test that proves the defect on the pre-fix implementation and the correction on the relevant physical Intel adapter(s).

## Load the repository guidance

Use the existing skills instead of recreating their rules:

- `systematic-debugging` for evidence gathering, root-cause analysis, and one-hypothesis-at-a-time investigation.
- `test-driven-development` for the verified RED -> minimal GREEN -> REFACTOR cycle.
- `intelavbfilter-testing` for test taxonomy, hardware execution, adapter iteration, and PASS/FAIL/SKIP semantics. Read its reference when writing or interpreting hardware tests.
- `run-tests` for canonical build/run commands and mandatory raw-log inspection.
- `verification-before-completion` before any fixed/passing/complete claim.
- `lifecycle-traceability` for the governing issue, TEST issue/TC-ID, requirement links, and PR relationship.
- `github-issue-integrity` before editing an existing issue body or adding a backward `Verified by:` link.
- `dbgview-capture` when kernel debug output is required for manual reproduction or diagnosis.

Follow `AGENTS.md`, `.github/copilot-instructions.md`, and all path-specific instructions for files being changed. If guidance conflicts, stop and surface the conflict; do not silently choose the easier rule.

## Non-negotiable gates

1. Identify the governing bug/requirement issue and concrete expected behavior before implementation. Do not invent acceptance criteria.
2. Reproduce the reported behavior before changing production behavior. Preserve exact environment and adapter evidence.
3. Enumerate the Intel NICs currently installed in the HIL machine. Never assume the README's historical runner inventory is still present.
4. Write or adapt the smallest appropriate automated regression test before the fix and observe it fail for the reported defect.
5. Establish a root-cause chain before implementing the correction: `trigger -> invalid hardware/software state -> propagation -> externally visible failure`.
6. Make the smallest justified production-code change. Keep unrelated cleanup, renaming, formatting, dependency updates, and redesign out of the fix.
7. Run the same regression test against the corrected build on the same physical adapter that produced RED.
8. Run the applicable installed-adapter matrix and relevant regression suites. The affected adapter may not be counted as GREEN if it skipped.
9. Repeat the original real-world reproduction after automated GREEN.
10. Report commands, adapter identities, driver/build identity, PASS/FAIL/SKIP counts, and log paths. Evidence is required; assertions are not evidence.

Diagnostic instrumentation is allowed only when needed to locate the failure. Keep it behavior-neutral, identify it as an experiment, and revert it before the RED/GREEN fix unless it is independently required and tested.

## Authorization and machine safety

Read-only discovery, source inspection, and ordinary builds are non-disruptive. Installing or rebinding a filter driver can interrupt networking; announce that effect before doing it when HIL execution is in scope.

Require explicit user authorization before any reboot, test-signing change, Secure Boot change, registry mutation, Driver Verifier change, forced sleep/S3 cycle, firmware change, or other machine-wide/persistent action. Establish a recovery path before Driver Verifier or crash-reproduction work. Do not run disruptive HIL steps over the only remote connection unless recovery access is confirmed.

## Workflow

### 1. Triage and freeze the baseline

Record:

- governing issue and acceptance criteria;
- observed versus expected behavior;
- first known bad and last known good versions when known;
- current commit, branch, dirty-worktree state, configuration, OS build, WDK/toolchain, and driver package identity;
- repeatability, severity, crash code/dump path, and likely blast radius;
- whether the suspected path is generic, controller-specific, per-adapter, multi-adapter, timing-sensitive, or power-state-dependent.

Preserve unrelated user changes. Do not begin by editing the suspicious function.

### 2. Discover the actual HIL inventory

At the start of each HIL session, collect current evidence from Windows rather than copying an old matrix. Use stable identities: PnP instance ID and PCI `VEN_8086&DEV_xxxx` (plus subsystem/revision when relevant). Adapter names and enumeration order are not stable identities.

Useful read-only discovery commands include:

```powershell
git rev-parse --verify HEAD
Get-ComputerInfo | Select-Object WindowsProductName, WindowsVersion, OsBuildNumber
Get-Service IntelAvbFilter -ErrorAction SilentlyContinue | Select-Object Name, Status

Get-PnpDevice -Class Net -PresentOnly |
  Where-Object { $_.FriendlyName -match 'Intel' } |
  Select-Object FriendlyName, InstanceId, Status

Get-NetAdapter -IncludeHidden |
  Where-Object { $_.InterfaceDescription -match 'Intel' } |
  Select-Object Name, InterfaceDescription, InterfaceGuid, ifIndex, Status, LinkSpeed, DriverInformation

Get-CimInstance Win32_PnPSignedDriver -Filter "DeviceClass='NET'" |
  Where-Object { $_.DeviceName -match 'Intel' } |
  Select-Object DeviceName, DeviceID, DriverProviderName, DriverVersion, DriverDate, InfName
```

Also use the repository's existing system/precondition checks where applicable. Reconcile their enumeration with Windows discovery. If they disagree, investigate before testing.

Build a session matrix with one row per physical adapter:

| Stable adapter identity | Controller/DID | Intel miniport version | Link/state | Intended role |
| --- | --- | --- | --- | --- |
| PnP instance ID | I210/I219/I225/I226/etc. | observed version | observed state | affected target or control |

Select:

- every adapter named by the report;
- every installed supported controller touched by a shared HAL/NDIS/MMIO change;
- at least one unaffected control controller when available;
- multiple same-model adapters when the defect may involve indexing, lifetime, shared state, ordering, or concurrency.

An absent controller is an explicit coverage gap, never an assumed pass.

### 3. Reproduce the original defect on hardware

Use the user's real path first: same controller, link topology, configuration, traffic, timing, and operation sequence as far as practical. Pin every result to the stable adapter identity.

Capture:

- exact commands and steps;
- driver/filter and Intel miniport identities;
- raw test log, DbgView/ETW trace where relevant, and crash dump for bugchecks;
- observed values and timing rather than only `failed`;
- attempts and failure rate for intermittent defects;
- comparison with a control adapter when available.

Infrastructure errors, stale binaries, missing elevation, a stopped service, or a broken test harness do not reproduce the product defect. Fix the environment or classify it separately.

### 4. Minimize and choose the regression boundary

Reduce the failure to the lowest boundary that still detects the real defect:

| Failure source | Preferred permanent test |
| --- | --- |
| pure calculation/state transition | unit test |
| IOCTL/ABI/driver boundary | user-mode integration test |
| controller registers or PHC behavior | per-adapter HIL test |
| NDIS/link/event interaction | HIL integration test |
| multi-adapter state isolation | multi-adapter HIL test |
| S3/reload/rebind lifecycle | controlled HIL lifecycle test |
| bugcheck/race | deterministic stress or fault-injection test plus dump evidence |

A defect discovered through HIL may have a fast unit/SIL regression test, but that does not replace final HIL verification when the fix touches hardware-facing behavior.

### 5. Create and verify RED

Before changing production behavior:

1. Add the smallest regression test describing desired externally observable behavior.
2. Give it the repository's required TC-ID and issue/requirement traceability.
3. For hardware tests, enumerate and explicitly open applicable adapters; emit a verdict tied to each stable adapter identity.
4. Build the test through the repository's canonical test build path.
5. Run it through the canonical elevated runner and read the raw log.
6. Confirm the affected physical adapter fails for the expected assertion and observed defect.

Valid RED is:

```text
defective driver build + new regression test + affected physical NIC = FAIL for the expected reason
```

The following are not RED:

- compilation failure, test crash, setup failure, or wrong adapter;
- a failure caused by stale installed driver bits;
- an unrelated existing failure;
- a test that passes immediately;
- an all-SKIP run;
- a threshold chosen after seeing results without a governing requirement.

Preserve the RED command, raw failure line, log path, commit, installed driver identity, and adapter PnP/PCI identity.

### 6. Establish root cause

Use evidence to state one falsifiable hypothesis at a time. Compare failing and working controllers/paths, trace values across user-mode -> IOCTL -> dispatcher -> context -> HAL -> MMIO/NDIS, and identify the first incorrect transition.

Do not confuse these categories:

- driver defect;
- regression-test defect;
- unsupported hardware capability;
- Intel miniport/OS behavior outside the filter's control;
- lab/topology/configuration problem;
- requirement or tolerance defect.

Changing a test threshold or converting FAIL to SKIP is a requirements decision, not a bug fix. Do it only with explicit evidence and traceable acceptance-criteria clarification.

### 7. Implement minimal GREEN

Implement only the confirmed root-cause correction. Then:

1. build the affected test and driver;
2. install/reload the new driver through the repository's canonical scripts;
3. verify that the loaded `IntelAvbFilter.sys` is the new build using repository tooling such as `tools/development/verify-driver-version.ps1` plus file/version/hash evidence as appropriate;
4. run the exact RED test through the canonical elevated runner;
5. inspect the raw log and confirm the same affected adapter now passes.

If GREEN fails, do not stack speculative changes. Return to diagnosis with the new evidence. After three failed correction hypotheses, stop and discuss whether the architecture or test model is wrong.

### 8. Verify the physical-adapter matrix

Run the targeted regression test on every applicable installed Intel NIC.

- The RED adapter must PASS; SKIP is not success.
- A controller may SKIP only under the repository's valid hardware-SKIP rules, with the attempted operation and returned unsupported status recorded.
- An unexpected error or failed prerequisite is FAIL, not SKIP.
- For generic/shared-path changes, test all installed supported controller families.
- For per-adapter state, lifetime, event, or context changes, test multiple adapters together and vary selection/order when practical.
- For timing/performance fixes, compare distributions and the governed limit; do not rely on one sample.
- For power-state fixes, perform the controlled lifecycle only after explicit authorization.

Record per-adapter counts. Aggregate exit code alone is insufficient because some test binaries or all-SKIP runs can mislead.

### 9. Regression, refactor, and real-world verification

Progress from narrow to broad:

1. new regression test;
2. affected component/IOCTL/controller tests;
3. quick suite;
4. relevant integration and multi-adapter suites;
5. full practical suite and repository quality/traceability gates proportionate to the change.

Refactor only while green and rerun the targeted HIL matrix after meaningful refactoring. Finally repeat the original manual/system reproduction on the affected adapter. An automated model passing does not prove the user's actual path is repaired.

### 10. Completion report

Use an evidence table, not a generic `fixed` statement:

| Evidence | Required content |
| --- | --- |
| Scope | governing issue, acceptance criteria, commit/configuration |
| Environment | OS/toolchain, installed filter build, Intel miniport versions |
| Inventory | stable PnP/PCI identity for every installed Intel NIC considered |
| Reproduction | original steps, affected adapter, attempts/failure rate, raw artifact |
| RED | exact test/command, expected failure line, adapter, log path |
| Root cause | trigger -> invalid state -> propagation -> symptom |
| Change | minimal production files and why each was necessary |
| GREEN | same test/command and same adapter, counts, log path |
| HIL matrix | per-adapter PASS/FAIL/SKIP counts and justified gaps |
| Regression | suite commands, fresh counts, analysis/static/traceability gates |
| Real-world check | original scenario result after the fix |
| Residual risk | unavailable cards, unrun disruptive tests, intermittent uncertainty |

Do not claim HIL validation if commands were not executed on physical hardware during the current fix. Say exactly what remains unverified and which installed or missing controller is needed.

## Exception: automation is genuinely impractical

Treat this as a documented exception, not a shortcut:

1. reproduce the defect and preserve deterministic evidence;
2. explain precisely why an automated reproduction is unsafe or impractical;
3. add the nearest feasible automated regression test at the root-cause boundary;
4. perform controlled manual HIL verification on the affected physical NIC;
5. record steps, repetitions, raw evidence, limitations, and residual risk.

Crashes, timing, firmware behavior, and hardware dependence often change the test boundary; they do not remove the need for regression evidence.
