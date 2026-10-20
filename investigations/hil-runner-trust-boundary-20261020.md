# Track C — HIL self-hosted runner: trust boundary, startup, fail-closed (2026-10-20)

Read-only audit. Nothing started, stopped, disabled or reconfigured.

## FACTS

1. Repository `zarfld/IntelAvbFilter` is public.
2. Only `ci-standards-compliance.yml` targets the HIL host: jobs `unit-tests` (line 451) and `integration-tests` (line 1088), matrix `include` → `runner: [self-hosted, Windows, i226]` (Debug and Release). **Neither job has a job-level `if:`** restricting event, repository, actor or ref. Triggers: `push` (master, develop, feature/**, release/**, tags v*), `pull_request` (master, develop), `schedule` `0 2 * * 0` (Sunday 02:00 UTC), `workflow_dispatch`.
   ⇒ a `pull_request` — including from a fork — schedules the self-hosted legs on the physical HIL workstation. Remaining protection is only GitHub's fork-PR approval setting (not readable without auth; `gh` not installed).
3. Step order on the self-hosted leg (unit-tests): checkout → prerequisites check → `tools/ci/Validate-PS1Syntax.ps1` (repo code) → `tools/build/Build-Tests.ps1` (repo code, compiler) → `Run-Tests-CI.ps1 -Suite Unit` (repo code) → `Test-DriverPreFlight.ps1` → `Run-Tests-CI.ps1 -Suite HardwareUnit`. Repo-controlled code executes **before** the driver pre-flight (Q verified independently). A STOPPED driver does not make the job harmless.
4. `integration-tests` runs `HardwareIntegration`, which on 2026-10-20 applied `SETUP_TAS` to the I226 (`avb_test_portability@125B`, TC-PORT-004/adapter:2 "SETUP_TAS accepted config").
5. `unit-tests` grants `permissions: pull-requests: write, checks: write, contents: read`.
6. `ci-standards-compliance.org.yml` and `traceability-check.org.yml` are active workflows (`.yml` suffix) but run only on `ubuntu-latest`.
7. Startup — two mechanisms for one registration (`agentId 24`, pool Default):
   - Service `actions.runner.zarfld-IntelAvbFilter.DESKTOP-RSUO2LV` (`.service` present, `IsServiceConfigured: True`): Auto (Delayed), **NT AUTHORITY\NetworkService**, **Stopped** after both reboots; no Application/SCM events today.
   - Scheduled task `GitHubActionsRunner`: logon trigger for `DESKTOP-RSUO2LV\User`, `cmd.exe /c run.cmd`, **RunLevel Limited** (Medium integrity: `Administrators` is deny-only in the token). Last run 13:36:43, result `0xC000013A` (console closed/interrupted 4 s after start).
   - Workflow comment (line 719) assumes "GitHub runner is already Administrator" — true for neither mechanism as configured.
8. **ACL:** `DESKTOP-RSUO2LV\CodexSandboxUsers` has `RX` on `C:\Users\User\actions-runner` **including `.credentials` and `.credentials_rsaparams`** (runner registration key). `GITHUB_ActionsRunner_Gf90bf` has Full.
9. No queued/in-progress/waiting runs (public API). Last jobs on this runner: Worker logs 08:14:39 / 08:18:42 (run 39379255772). Runner online 12:44:02–13:35 (crash 2) without jobs.

## Proposal (not applied)

**Single authoritative startup:** the Windows service, running as a **dedicated local account** that is a member of Administrators (HIL needs elevation for driver IOCTL/service checks), `Automatic (Delayed)`, with the logon task removed. Rationale: survives reboot without interactive logon; no console that can be closed; one session per registration. Until the HIL gate below exists, keep the service **Manual** so restoration is an explicit act.

**Dedicated HIL path that cannot run untrusted code:**
1. Remove the self-hosted `include` legs from `unit-tests` / `integration-tests`; they run GitHub-hosted only.
2. New workflow `hil.yml`:
   - triggers: `workflow_dispatch` only (optionally `push` to `master` from the same repo); **never** `pull_request` / `pull_request_target`;
   - job `if: github.repository == 'zarfld/IntelAvbFilter' && github.event_name == 'workflow_dispatch'`;
   - `environment: hil` with required reviewer = owner (deployment-protection approval before any step runs on the workstation);
   - `permissions: contents: read` only;
   - `concurrency: { group: hil-DESKTOP-RSUO2LV, cancel-in-progress: false }`;
   - **step 1 = fail-closed pre-flight before any repo-built code**: driver identity (SHA256 against an authorized value), adapter/PHC baseline, runner not already in HALT; only then build/tests;
   - mutating suites gated by an explicit dispatch input listing authorized entries.
3. Runner group restricted to this repository and to `hil.yml` (runner-group "selected workflows").
4. Fix ACL so `CodexSandboxUsers` cannot read `.credentials*`.

## Characterization tests to write (offline, before any change)

- `tests/unit/ci/test_hil_trust_boundary.ps1`: parse workflows; FAIL if any job resolving to a `self-hosted` label is reachable from `pull_request`/`pull_request_target` or lacks a repository guard; FAIL if any repo-code step precedes the HIL pre-flight. Expected RED today (F2, F3).
