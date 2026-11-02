---
name: run-tests
description: "Use when running, executing, or invoking any IntelAvbFilter test, suite or test runner (Run-Tests-CI.ps1, Run-Tests-Elevated.ps1, Run-Tests.ps1, LocalSuites.ps1, Build-Tests.ps1), or any .exe under build/tools/; also when reading, interpreting, or claiming test results. Testing semantics and verdict rules live in intelavbfilter-testing."
---

# Running Tests in IntelAvbFilter

Invocation mechanics only. What a verdict **means**, which tests apply, multi-adapter rules, restoration and evidence requirements are defined by the canonical contract [`intelavbfilter-testing`](../intelavbfilter-testing/SKILL.md). Every parameter below exists in the script's current `param()` block — re-check the script before using anything not listed here.

## Hard rules

- **Never run a test `.exe` directly** from `build\tools\avb_test\x64\<Config>\`. That bypasses build provenance, the HIL safety registry, adapter baseline, post-test state check, timeouts and logging.
- **Never claim a result without reading the results document or log** produced by this run.
- **Hardware execution, driver install/reinstall and anything elevated are run by Q** unless Q has explicitly authorized you. Give the exact command, prerequisites, expected mutations, stop conditions and evidence paths; wait for the output.

## Execution paths

| Context | Entry point | Hardware | Gates applied |
|---|---|---|---|
| GitHub-hosted CI (`ci-standards-compliance.yml`, `windows-latest`, Debug + Release) | `Run-Tests-CI.ps1 -Suite Unit` / `-Suite Integration` | none — hardware-independent tests only | build provenance, verdict contract |
| Self-hosted HIL CI (`hil.yml`, `[self-hosted, Windows, i226]`, `hil-debug` then `hil-release`) | `Run-Tests-CI.ps1 -Suite HardwareUnit / HardwareIntegration / HardwareMutation / HardwareMandatory` after authorized deployment of the staged driver package | real NICs + deployed driver | provenance, deployment identity, safety registry, baseline/state check, halt propagation (`-PriorResultsDir`) |
| Local complete run, Debug **and** Release | `tools\archive\deprecated\LocalSuites.ps1` (elevated; builds, reinstalls driver, preflight, four suites per configuration) | real NICs | same runner gates; evidence class `local` / `local-pre-commit` |
| Local single entry, gated | `Run-Tests-Elevated.ps1 -Suite <Hardware*> -TestName '<entry>'` | real NICs | same runner gates as HIL CI |
| Local single binary, legacy | `Run-Tests-Elevated.ps1 -TestName <name>` → `Run-Tests.ps1 -TestExecutable` | real NICs | **none of the HIL gates** — diagnostic use only, never gate or GREEN evidence for a hardware test |

`Run-Tests-CI.ps1` is **not** "hardware-independent only": `-Suite Unit/Integration/All` are; the four `Hardware*` suites are the HIL gates.

## Parameters (verified against the scripts)

`tools/test/Run-Tests-CI.ps1` — runs unattended; for `Hardware*` suites start it from an elevated shell.

| Parameter | Notes |
|---|---|
| `-Configuration Debug\|Release` | default `Debug` |
| `-Suite Unit\|Integration\|All\|HardwareUnit\|HardwareIntegration\|HardwareMandatory\|HardwareMutation` | default `Unit` |
| `-Tests <entry>[,...]` | overrides the suite list; names **without** `.exe`; an entry may carry args, e.g. `'test_ptp_phc_stability --case UT-CORR-005'` |
| `-TestTimeoutSeconds <n>` | default per suite (Unit 60, Integration 120, HardwareUnit 300, HardwareIntegration 600, Mandatory/Mutation 300); per-entry overrides never lower it |
| `-ResultsJson <path>` | machine-readable results (`avb-runner-results/1`); default `logs\results-<Suite>-<Config>-<timestamp>.json` |
| `-PriorResultsDir <dir>` | carries an earlier halt so later Init/Mutating entries stay BLOCKED |
| `-HilLockWaitSeconds <n>` | attended runs only; default 60 |
| `-FailFast` | stops after the first failure — **not** for gate or completeness evidence |
| `-LogsDir` | sandbox/self-test use only; ordinary HIL runs must not pass it |
| `-CaptureDbgView` | rejected on purpose; use `Run-Tests-Elevated.ps1 -CaptureDbgView` |

`tools/test/Run-Tests-Elevated.ps1` — local wrapper; elevates via UAC, transcript to `logs\`, caller terminal shows almost nothing.

| Parameter | Notes |
|---|---|
| `-Suite HardwareUnit\|HardwareIntegration\|HardwareMandatory\|HardwareMutation` | routes `-TestName` through `Run-Tests-CI.ps1` with all gates; requires `-TestName` |
| `-TestName <entry>` | with `-Suite`: the runner entry; without: legacy single binary (`.exe` appended if missing) |
| `-TestArgs <args>` | appended to the entry |
| `-Configuration Debug\|Release` | default `Debug` |
| `-ResultsJson <path>` | with `-Suite` only |
| `-CaptureDbgView` | DebugView kernel capture; the test does not run if the capture cannot be confirmed |
| `-TimeoutSeconds <n>` | bound for the elevated child, default 1800; expiry = TIMEOUT |
| `-LogFile <path>` | default `logs\<TestName>_<timestamp>.log` |
| `-Full` | legacy `Run-Tests.ps1 -Full` (no HIL gates) |

`tools/archive/deprecated/LocalSuites.ps1` — despite the folder name, the current operator-driven complete local run: `-Configurations Debug,Release` (default both), `-NoReproduce`, `-ReportDir`.

`tools/build/Build-Tests.ps1` — `-Configuration`, `-TestName <target>`, `-ShowDetails`, `-BuildDiagnosticTools`, `-RequireDriverSha256`.

`tools/test/Run-Tests.ps1` (legacy dispatcher) — `-Configuration`, `-Quick`, `-Full`, `-SkipHardwareCheck`, `-CollectLogs`, `-TestExecutable`, `-TestArgs`, `-HardwareOnly`, `-SecureBootCheck`. It applies no safety registry and no provenance gate.

## Commands

Offline (any Windows dev box, no driver, no elevation):

```powershell
.\tools\build\Build-Tests.ps1 -Configuration Debug
.\tools\test\Run-Tests-CI.ps1 -Configuration Debug -Suite Unit
```

Single hardware entry through the gates (Q runs it; UAC prompt):

```powershell
.\tools\build\Build-Tests.ps1 -Configuration Debug -TestName test_ptp_phc_stability
.\tools\test\Run-Tests-Elevated.ps1 -Configuration Debug -Suite HardwareIntegration -TestName 'test_ptp_phc_stability' -ResultsJson .\logs\results-single.json -CaptureDbgView
```

Complete Debug and Release HIL run (Q runs it from an elevated shell at the repository root; reinstalls the driver per configuration):

```powershell
.\tools\archive\deprecated\LocalSuites.ps1 *>&1 | Tee-Object -FilePath ".\logs\FullSuite_$(Get-Date -Format 'yyyyMMdd_HHmmss').log"
```

## Reading results

1. Prefer the **results document** (`-ResultsJson` / `results-*.json`, LocalSuites report in `logs\LocalSuites_<timestamp>\`): top level `schema`, `evidenceClass`, `overall`, `exitCode`, `counts`, `adapters`, `cleanupIncidents`, `records[]`; per record `verdict`, `executed`, `exitCode`, `artifactSha256`, `safetyClass`, `targetDid`, `logPath`, and per-testcase `testcases[]` with adapter index/DID.
2. Otherwise read the transcript/log in `logs\`; testcase lines are `[VERDICT] TC-ID/adapter:N: …` (tags parsed by `tools/lib/TestVerdict.ps1`).
3. State counts for **every** verdict category (PASSED, FAILED, FAILED(TIMEOUT), INFRA_FAILURE, CLEANUP_FAILED, INCONCLUSIVE(_RESTORATION), INCOMPLETE, BLOCKED(SAFETY/PROVENANCE), COVERAGE-GAP, SKIPPED) and the overall verdict (`PASSED`, `FAILED`, `INCOMPLETE`, `NOT_VERIFIED`, `NO_TESTS`).
4. Exit code 0 alone proves nothing; an `INCOMPLETE` or `NOT_VERIFIED` hardware suite is non-passing.

```powershell
$r = Get-ChildItem .\logs -Filter 'results-*.json' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
$doc = Get-Content $r.FullName -Raw | ConvertFrom-Json
$doc.overall; $doc.evidenceClass; $doc.counts
$doc.records | Select-Object name, verdict, executed, exitCode, targetDid, logPath
```

If a document's shape differs from this (schema change), trust the document and say so.

## Driver identity before hardware runs

Hardware results count only for the driver build under test. Before claiming anything, establish installed == intended driver SHA256 (`tools\test\Test-DriverPreFlight.ps1`; HIL CI: `hil-run-identity.json`). `Get-Service IntelAvbFilter` = Running or a file timestamp is **not** identity evidence. Driver (re)installation (`tools\setup\Install-Driver-Elevated.ps1`) is an elevated, state-changing action — Q runs it.

## Failure triage

For each non-passing row: exact message, testcase + adapter, configuration, classification (driver defect / test defect / infrastructure / provenance / safety block / coverage gap), and whether restoration was verified. Never "most tests pass" — cite numbers and the artifact you read.
