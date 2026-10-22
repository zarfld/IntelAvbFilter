# Activation and behaviour scenarios — `intelavbfilter-testing`

Review aid for the canonical contract in [`SKILL.md`](./SKILL.md). Non-normative: when this file and `SKILL.md` differ, `SKILL.md` wins.

These scenarios describe the **expected** agent behaviour. They are not evidence that any agent behaves this way, and nothing here is hardware evidence. Activation can only be confirmed by observing a real agent session.

## Positive activation

| # | Prompt / situation | Expected behaviour (contract section) |
|---|---|---|
| 1 | "Add a PTP HIL testcase for PHC offset adjust." | Find governing issue + `Verifies: #N`; enumerate all adapters; classify the entry in `$HilTestSafety` (PHC_OFFSET_ADJUST ⇒ Mutating, must become Mutating:Target via `AvbTestSelectTarget` to run unattended); baseline/restore PHC rate+offset with drift-tolerant invariants; register in a suite list; RED on hardware before implementation; report HIL pending until Q runs it (A, D, E, I). |
| 2 | Two adapters with identical DIDs on one HIL host. | Do not treat the second as a duplicate or as N/A. Runner reports `TC-INFRA-ADAPTER-003` BLOCKED for exact targeting; ReadOnly tests must still show two distinct instances in per-adapter lines; report the missing PnP/PCI-location identity as an infrastructure gap (A). |
| 3 | New hardware capability (e.g. a new TSN offload). | Per-operation capability contract first; test calls the generic IOCTL on every adapter; `SKIP_UNSUPPORTED` only with capability evidenced absent, `INCONCLUSIVE` when undeterminable, `FAIL` when advertised but failing; no DID gating in the test (A, B). |
| 4 | "Run the complete Debug and Release suites." | Use `LocalSuites.ps1 -Configurations Debug,Release` (local, elevated, operator-run) or the hil.yml workflow; give Q the exact command; read the results documents; report every verdict category per configuration, including gaps and BLOCKED (C, G, I). |
| 5 | A testcase fails; restoration verified. | Ordinary `FAIL` with cleanup evidence; later independent tests continue; no safety halt (E case 2). |
| 6 | A testcase fails; restoration uncertain. | `CLEANUP_FAILED` / `INCONCLUSIVE_RESTORATION`; later Init/Mutating blocked via `stateHalt` / `-PriorResultsDir`; do not clear the safety record; ask Q for recovery (E case 3, I). |
| 7 | A registered test executable is missing. | `INFRA_FAILURE` / failure row with the missing path; never SKIP or "not supported"; fix the build/manifest, not the list (B, C). |
| 8 | Driver returns `STATUS_NOT_IMPLEMENTED` although the adapter advertises the capability. | `SKIP_NOT_IMPLEMENTED` → suite `INCOMPLETE`; report as driver implementation gap; never `SKIP_UNSUPPORTED`, never green (B). |
| 9 | HIL results came from a stale driver. | Not evidence for the change: compare intended vs deployed SHA256 (`Test-DriverPreFlight.ps1`, `hil-run-identity.json`); report BLOCKED(PROVENANCE)/unverified; request redeploy from Q (G). |
| 10 | A test or script contains fixed DIDs (e.g. `if (device_id == 0x1533)`). | Flag as contract violation; propose capability-driven replacement; do not extend the list of IDs; keep identity only for targeting/reporting (A). |
| 11 | "Fix this bug in the PHC code." | Activates via TDD/bugfix verification: failing test first, hardware RED/GREEN, regression suites (D). |
| 12 | "Why is HardwareMandatory INCOMPLETE?" | Read results document; explain coverage-gap / NOT_IMPLEMENTED / zero-pass rules from `Get-AvbRunOverall`; never propose de-listing (B, C). |

## Negative activation (skill should not be needed)

| # | Prompt | Why not |
|---|---|---|
| N1 | "Rename a section heading in README.md." | Documentation-only, no test or evidence impact. |
| N2 | "Add a label to the issue-validation workflow." | Issue/label tooling; use `lifecycle-traceability` / `github-issue-integrity` if links change. |
| N3 | "Explain what TIMINCA does on the I226." | Domain question; no test, run or verification claim. |
| N4 | "Update the copyright year in source headers." | Mechanical edit with no behaviour change. |
| N5 | "Draft an ADR for the event-ring design." | Architecture artifact; testing contract applies only once tests/verification are planned. |

A negative case flips to positive as soon as the task touches a test, a runner, a CI test gate, test evidence, or a claim that something works.
