# Pre-merge HIL gate for PR #344 — investigation (2026-10-30)

Status (updated 2026-10-30): owner chose **Option A** (PR #344 is the bootstrap). Pre-merge path + regression tests
implemented in #344 and verified OFFLINE only (see "Implementation" below). **Physical HIL of the final candidate:
NOT RUN (pending, operator-executed).** Ruleset unchanged, nothing merged.  The "Blocker" section below records why
PR #344 itself must be the bootstrap.

## FACTS (verified this session; master = 6f666d8, PR #344 head = c7b759c, 146 ahead / 0 behind)

| # | Fact | Evidence |
|---|---|---|
| F1 | CI run 41033563584 = `pull_request` on c7b759c, all gates success, `Deploy to Staging (…)` **skipped** | GitHub API jobs list |
| F2 | deploy-staging runs only for `push` to master / `v*` tags | `.github/workflows/ci-standards-compliance.yml:1581-1583` (PR) / `:1592` (master) |
| F3 | Even on a PR it would stamp `SourceRevision=$GITHUB_SHA` = synthetic `refs/pull/N/merge` commit, not the head SHA; authorize would (correctly) reject it | `ci-standards-compliance.yml:1636`, `hil.yml:228` |
| F4 | The executing `hil.yml` is master's: every job `if:` + authorize `want` require `workflow_run.event=='push' && head_branch=='master'` | master `hil.yml:35,89,211,445,674`; PR `hil.yml:45,99-100,268,477,687` |
| F5 | master `hil.yml` is an older architecture (hil-unit / hil-integration, no driver deployment, unsuffixed deploy job name) incompatible with #344's staging | `git show 6f666d8:.github/workflows/hil.yml` |
| F6 | No merge gate exists: ruleset 25536988 has no `required_status_checks`; classic protection disabled; `hil` environment has no protection rules | `/rules/branches/master`, `/branches/master`, `/environments` |
| F7 | F6 contradicts #98 ("Require CI checks to pass before merge") and closed #244 ("✅ Failed builds block PR merge") | issue bodies |
| F8 | #132 ADR-TEST-001 is **Proposed**; says "E2E on merge to master" | issue body lines 5, 308, 373 |
| F9 | HIL history: 54 runs, 45 skipped, 9 failure (last 2026-10-21, all at `authorize`); no master-push CI since 2026-10-21 | Actions API |
| F10 | `test_hil_trust_boundary.ps1` on c7b759c: 40/40 PASS. It encodes "same-repo PR → HIL runner = violation" (REAL-001/005/007/009/011/012) | local run, exit 0 |
| F11 | Absent on master: `tools/ci/Invoke-HilDeployment.ps1`, `tools/lib/{HilLock,DriverPackage,PhcRestoration}.ps1`, `tools/test/LocalSuites.ps1` | `git cat-file` vs 6f666d8 |
| F12 | #344's runner observes hardware state / restoration through new driver IOCTLs 68/69/70, introduced by #344's driver changes | `tools/test/Run-Tests-CI.ps1:1000,1152-1153`; `include/avb_ioctl.h`, `src/device.c` |
| F13 | Repo-level self-hosted runner + repository-level signing secrets: any write-access user can already reach the runner / secrets via a branch `push` workflow (residual risk, not fixable in YAML) | workflow model; `ci-standards-compliance.yml` signing step |

## Owner decisions (2026-10-30)

- Two-stage bootstrap; the bootstrap PR's exact revision must pass physical HIL (Debug + Release, full evidence) before merge; any exception only concerns how GitHub receives the status.
- Automatic pre-merge HIL only for same-repository, **OWNER**-authored PRs; document the external-contributor limitation.
- Pre-merge HIL mandatory **and** post-merge master HIL kept; amend #132 (proposed body prepared, not applied — no GitHub write access here).

## Blocker

A bootstrap = master + "only HIL workflow infrastructure + regression tests" cannot pass physical HIL with the required
evidence (driver identity/SHA256, cleanup/restoration evidence):

- with master's tooling (F11) there is no staged-package deployment identity, machine lock or restoration evidence;
- with #344's tooling the restoration/state evidence depends on IOCTLs 68/69/70 (F12), which only #344's driver provides,
  so on master's driver the state is unobservable → INCONCLUSIVE, never PASS.

The infrastructure is therefore not separable from #344's driver change without building a new hybrid runner revision.

## Implementation (offline-verified; physical HIL pending)

- `ci-standards-compliance.yml` deploy-staging: also for same-repository OWNER PRs into master; checks out
  `pull_request.head.sha`, verifies HEAD (step `staged-revision`), stamps/self-checks the manifest with that SHA.
- `hil.yml`: guards accept master push or same-repo PR; authorize PR branch (`PR_AUTHOR_ASSOCIATIONS = {'OWNER'}`,
  open, base master, API head == CI head, head ref, API PR linkage, up to date except README); outputs `mode`,
  `pr_number`, `deny_reasons`; acceptance output `verdict`; new hosted `hil-gate` posting `HIL / pre-merge gate`.
- `test_hil_trust_boundary.ps1`: AuthorizedPR only behind authorize (FX-024..026), REAL-007/008/009/011 updated,
  REAL-012 +30 PR authorization cases, REAL-017 gate verdict (24 cases) + status post (6 cases).
- Evidence: RED 38/44 (REAL-007/008/009/011/012/017) → GREEN 44/44; 8/8 single-safeguard mutations caught; all 14
  `tests/unit/ci/*.ps1` + `test_hil_safety_gate.ps1` + `test_native_exit_codes.ps1` exit 0; no testcase ID removed.

## Designed pre-merge path (as investigated)

- CI (PR-side): deploy-staging also for same-repo `pull_request`; checkout `pull_request.head.sha`, verify HEAD, stamp that SHA.
- hil.yml (master-side) authorize, PR branch: exactly one linked PR; open; base master/this repo; head repo = this repo;
  `pulls/N.head.sha == workflow_run.head_sha` (stale → deny); `author_association == OWNER`; master tip ancestor of head or
  ahead only by README-only commits (tested tree == merged tree); plus all existing staging/manifest checks unchanged.
- Physical jobs unchanged (lock, deployment verification, restoration, evidence, Debug then Release).
- New hosted `hil-gate` (always(), no PR checkout): commit status `HIL / pre-merge gate` on head SHA, success only for
  acceptance PASSED, else failure with the unmet prerequisite (+ `hil-gate-verdict.json`).
- Ruleset (owner): required status check `HIL / pre-merge gate` (source GitHub Actions) + require up-to-date branches.
- Tests: REAL-012 PR cases (fork, stale head, wrong SHA, failed CI, skipped/failed staging, wrong artifact, closed PR,
  non-OWNER, behind master) and gate-verdict cases; same-repo OWNER PR moves from Reachable to Authorized-through-authorize.
