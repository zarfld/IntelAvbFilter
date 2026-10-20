# HIL orchestration: CI → deploy-staging → hil.yml (2026-10-20, iteration 5)

Offline only. Not committed, not pushed. No runner, service, driver, workflow dispatch or HIL change/run.
Base `8a383d0`. Replaces iteration 4 (HIL jobs inside `ci-standards-compliance.yml`), which Q rejected.

## Design

Two authoritative workflows:

| Workflow | Trigger | Physical HIL |
|---|---|---|
| `ci-standards-compliance.yml` ("CI - Standards Compliance & Quality Gates") | push / pull_request / schedule / workflow_dispatch (unchanged) | none (hosted only) |
| `hil.yml` ("HIL - Hardware-in-the-Loop") | `workflow_run` of the CI workflow, `types: [completed]` — nothing else | yes |

`hil.yml` jobs (workflow_run always executes the copy of hil.yml on master):

| Job | Runner | Needs | Runs when | Does |
|---|---|---|---|---|
| authorize | ubuntu-latest, `actions: read`, `contents: read` | — | guard: repo = zarfld/IntelAvbFilter ∧ event = workflow_run ∧ upstream event = push ∧ head_branch = master ∧ conclusion = success ∧ head_repository = zarfld/IntelAvbFilter | Python, event file + API only: payload fields; API run equals payload (id, attempt, head_sha, event, branch, path, status, conclusion, repos); jobs of that attempt contain exactly one `Deploy to Staging (Phase 08 - Transition)` with status completed **and conclusion success**; master protected; head_sha reachable from master. Outputs `authorized`, `head_sha`, `upstream_run_id`, `upstream_run_attempt`; any failure → exit 1 |
| hil-unit | `[self-hosted, Windows, i226]`, env `hil`, `contents: read`, Debug/Release, max-parallel 1 | authorize | guard ∧ `authorized == 'true'` (and authorize succeeded) | inline preflight (before any repository code) → checkout `head_sha` → HEAD check → prerequisites → Build-Tests → Test-DriverPreFlight → run identity → Run-Tests-CI HardwareMandatory, HardwareUnit → upload `hil-evidence-unit-<cfg>` |
| hil-integration | same | authorize, hil-unit | same (both hil-unit legs succeeded) | same, Run-Tests-CI HardwareIntegration → `hil-evidence-integration-<cfg>` |
| hil-acceptance | ubuntu-latest, `contents: read`, `checks: write` | authorize, hil-unit, hil-integration | `!cancelled()` ∧ guard ∧ authorized | Python verdict: both stages success; all 4 bundles present; identity runId = this run, sha = headRevision = authorized head_sha, upstreamRunId = authorized run, job/config match; each expected suite JUnit has ≥ 1 pass and 0 failures; no JUnit outside a bundle. Publishes JUnit (dorny) and traceability/coverage-gap summary |

Inline preflight (identical in both physical jobs; GitHub variables, no workstation folder): repository, event
`workflow_run`, `GITHUB_WORKFLOW_REF = …/hil.yml@refs/heads/master`, `authorized = true`, head_sha 40-hex,
**runner elevated** (required: the control device admits Administrators only, #73), `HIL_HALT` ≠ true,
service IntelAvbFilter Running, installed driver SHA256 = `HIL_AUTHORIZED_DRIVER_SHA256`, no bugcheck since
`HIL_BUGCHECK_ACK`, DEV_125B/1533/15BB PnP OK and link Up.

Run-Tests-CI binding: under workflow_run `GITHUB_SHA` is the master tip, not the verified CI commit. The physical
jobs export `AVB_HIL_EXPECTED_REVISION = head_sha`; `Get-AvbSourceIdentity` requires HEAD = that value (40-hex)
and fails closed on workflow_run without it. Build manifest / per-artifact provenance, safety gate, coverage gaps,
JUnit and traceability are the existing Run-Tests-CI infrastructure (one dispatcher).

## Iteration 4 → 5 (exact source changes vs 8a383d0)

| File | Change |
|---|---|
| `.github/workflows/ci-standards-compliance.yml` | Rebuilt from HEAD: +11/−193. Kept only hosted fixes: dead `matrix.hardware == true` steps and `continue-on-error` removed; traceability uploads `test-evidence/…` → `logs/…` (current run); hardware-test-evidence labelled HISTORICAL. **Removed** iteration-4 jobs unit-tests-hil, integration-tests-hil, hil-test-report and all HIL needs/verdict changes. `deploy-staging` byte-identical to HEAD. |
| `.github/workflows/hil.yml` | Rewritten: workflow_run, authorize / hil-unit / hil-integration / hil-acceptance (above). |
| `tools/test/Run-Tests-CI.ps1` | `AVB_HIL_EXPECTED_REVISION` binding; automatic suites ReadOnly/Init only + `$Hardware*CoverageGaps` reported as `TC-HIL-COVERAGE-GAP-001`; Hardware* run with zero passes exits 1; comments. |
| `tools/hil-trusted/*`, `tests/unit/ci/test_hil_preflight.ps1` | **Deleted** (runner-local hook for a non-admin runner; superseded by hosted authorization + inline preflight + GitHub variables). |
| `tests/unit/ci/test_hil_trust_boundary.ps1` | workflow_run model (7 upstream contexts incl. same-repo PR *from* master), FX-001/015/021–023, REAL-001…015 rewritten; YAML loader reads BOM files (`utf-8-sig`). |
| `tests/unit/ci/test_hil_binary_provenance.ps1` | TEST-HILPROV-040 wording, new 041 (workflow_run revision binding). |
| `tests/unit/ioctl/test_hil_safety_gate.ps1` | 032–036 (automatic suites ReadOnly/Init, gaps justified, TSN/irreversible gaps, gap JUnit, zero-pass failure). |
| `tests/unit/ci/test_hil_suite_safety.ps1` | suites read from every workflow `-Suite Hardware*`; HardwareIntegration approved. |

## Tests (offline)

| Test | Result |
|---|---|
| test_hil_trust_boundary.ps1 | 39/39 — REAL-012 executes hil.yml's authorize script against a localhost fixture API (22 cases: granted, granted with paginated jobs; denied for failed CI, deploy-staging skipped / failed / running / absent / other SHA, PR, fork PR from its master, feature branch, tag, foreign repository, other workflow with the CI name, API SHA differs, stale attempt, master unprotected, SHA not on master, API error, malformed SHA, action ≠ completed, event ≠ workflow_run) and the acceptance script against fixture evidence (12 cases: complete; unit failed; integration skipped; bundle missing; leftover run id; other revision; HEAD ≠ sha; other CI run; failing JUnit; skip-only JUnit; suite JUnit missing; stray JUnit) |
| mutation check (15 workflow mutations) | 15/15 detected; M6 (extra workflow_dispatch trigger) by REAL-009 — the job guard still keeps dispatch off the runner. M1 first revealed a missing model context (same-repo PR from master), now covered |
| test_hil_binary_provenance.ps1 | 41/41 |
| test_hil_safety_gate.ps1 | 35/35 |
| test_hil_suite_safety.ps1 | **17/18** — REAL-003 RED (decision, below) |
| test_junit_verdict_normalization.ps1 | 13/13 |
| test_hil_dispatch_paths.ps1 | 3/10 — unchanged intentional characterization of legacy launchers |
| test_health_sentinel.ps1 | **4/7 — pre-existing on HEAD** (fake BLOCKED by TC-SAFETY-GATE-002 since e85fb52) |
| Validate-PS1Syntax (changed files) | OK |

Phase coverage: upstream failure (REAL-011/012/015, FX-023) · skipped/failed/missing deploy-staging (REAL-012) · PR,
fork PR, same-repo PR from master (REAL-001, FX-022) · foreign repository (REAL-005/012) · wrong ref/tag (REAL-005/012)
· wrong/stale SHA or attempt (REAL-012, HILPROV-041) · stale evidence (REAL-012 acceptance, HILPROV) · no repository
code before preflight (REAL-002) · no dispatch (REAL-009/014) · TAS/FP/PTM, lifecycle, irreversible PHC excluded
(HILGATE-032–034, HILSUITE-002).

## Coverage gaps

Unchanged from iteration 4: automatic HIL runs HardwareMandatory 14, HardwareUnit 38, HardwareIntegration 13
(ReadOnly/Init). Excluded and reported every run: 8 previously executed (test_ptp_freq, test_ptp_freq_complete,
test_qav_cbs, avb_test_i217/i219/i225/i226_um, avb_test_portability) and 33 gate-BLOCKED tests; HardwareMutation,
NIC lifecycle, S3, 24 h tests never automatic. Structural: HIL exercises the operator-installed driver (hash pinned),
not a driver built from head_sha; traceability-coverage (CI workflow) does not include HIL results (published by
hil-acceptance instead); no executable acceptance suite beyond the hardware verdict.

## Administrator trust boundary (existing elevated runner)

- Any code executing in a physical job has Administrator on the workstation (driver, services, files, persistence).
  The boundary is therefore *which code can be scheduled there*, enforced by GitHub, not by the workstation.
- hil.yml: definition always from master (workflow_run); hosted authorization before any physical job; upstream
  must be a successful master push CI with a successful deploy-staging; head_sha must be on protected master.
- GitHub-enforced controls (Q): **branch protection on master** (authorize denies without it; PR + review, no force
  push/deletion, include administrators); **fork-PR workflow approval** for all outside collaborators;
  write access limited to trusted maintainers. Residual risk: a repository-level runner in a user-owned repository
  cannot be restricted to one workflow — any collaborator with write access can push a workflow on another branch
  that targets the runner labels. The static test keeps the repository's own workflows clean; it cannot prevent that.
- Open from the earlier audit: `CodexSandboxUsers` read access to `actions-runner\.credentials*`.

## GitHub settings to inspect / set (not recreated)

- Environment `hil`: no required reviewers or wait timer (unattended); deployment branches must allow `master`
  (workflow_run jobs run on the default branch).
- Variables (repository or `hil` environment): `HIL_AUTHORIZED_DRIVER_SHA256` **required**; `HIL_BUGCHECK_ACK`
  (ISO 8601 'o') and `HIL_HALT` optional.
- Branch protection on master: required (see above).
- `hil.yml` takes effect only once it is on master (workflow_run reads the default branch).

## GO / NO-GO: **NO-GO** (design ready; activation blocked)

1. Hosted Unit is red on master (run #496; believed: test_health_sentinel regression from my e85fb52) → CI never
   succeeds → deploy-staging never succeeds → HIL never starts. Fix proposed (sentinel fixture: manifest record +
   sandbox source identity), not applied.
2. master unprotected → authorize denies by design.
3. `HIL_AUTHORIZED_DRIVER_SHA256` not set → preflight denies by design.
4. TEST-HILSUITE-REAL-003: `test_ptp_phc_stability` binary contains a driver-reinstall path reachable only with
   `--case UT-CORR-009` / `--allow-lifecycle` — evidence-bound exception or coverage gap: Q to decide.
5. Unverified until live: jobs API `attempts/{n}/jobs` content after a partial re-run; `protected` for rulesets;
   runner support for Node 24 actions; `vars` resolution from the `hil` environment.
6. Risk: #328 send-path issue unresolved; automatic HIL exercises the send path after every successful master CI.
