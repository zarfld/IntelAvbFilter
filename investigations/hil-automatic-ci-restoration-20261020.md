# HIL orchestration: CI → deploy-staging → hil.yml

## Iteration 7 (2026-10-21): final automatic-HIL safety corrections

Offline only; no HIL execution, driver operation, runner change or workflow dispatch by the agent. Baseline at start:
`ef23036` (= `a86a791` + README bot commit). Q committed the agent's work in progress as `93079b2` and `df1ce24`
(pushed). Tested tree: `df1ce24` + uncommitted `tests/unit/ci/test_hil_suite_safety.ps1`
(blob `5df164d6`, SHA256 `50741501…bcd5cf`). **All results below are offline/synthetic — no hardware verification.**

### A — Init excluded from unattended HIL (registrations kept)

- `Run-Tests-CI.ps1`: the seven Init entries (test_ioctl_routing, test_minimal_ioctl, test_clock_config,
  test_clock_working, test_direct_clock, test_dev_lifecycle, test_hw_state_machine) moved from the automatic lists to
  `$Hardware*CoverageGaps` as dated `TEMPORARY POLICY EXCLUSION 2026-10-21` (reported as skipped
  TC-HIL-COVERAGE-GAP-001); `$HilTestSafety` classes unchanged.
- Gate (defence in depth): `Get-AvbHilGateDecision -InitUnattendedApproved` (default `$false`); under GitHub Actions an
  Init entry is BLOCKED (`$HilInitUnattendedApproved = $false`); attended runs outside GitHub Actions still run it.
- Real-pass requirement unchanged (zero-pass Hardware* run exits 1); every automatic suite keeps ReadOnly entries
  (Mandatory 12, Unit 33, Integration 11).
- Effects of INIT_DEVICE are **source-derived, UNVERIFIED on hardware** (now stated in the gap reasons and gate reason).
- RED/GREEN: test_hil_safety_gate 34/38 on `ef23036` (032, 038, 016, 039 fail; HEAD executed the Init fake
  unattended, exit 0) → 38/38. test_hil_suite_safety FX-009 (Init fixture issuing SETUP_TAS) RED on `ef23036`
  (reachable) → GREEN; FX-002's fixture class changed Init→ReadOnly because it tests helper scanning in a runnable entry.
- test_ioctl_routing evaluation (execution path, not name): sends only ENUM_ADAPTERS, OPEN_ADAPTER, READ_REGISTER,
  GET_CLOCK_CONFIG; INIT_DEVICE appears only in a name table. Qualifications: OPEN_ADAPTER re-points the driver's
  global default context (initializes an adapter only below PTP_READY — AvbEnsureDeviceReady guard, #343) and the
  test deletes `HKLM\Software\IntelAvb\LastIOCTL`. Eligible for ReadOnly with a documented exception; **not
  reclassified** (Q to decide); stays excluded.

### B — Driver identity

- Physical jobs record `authorizedDriverSha256` (job env from `vars.HIL_AUTHORIZED_DRIVER_SHA256`), the service
  `ImagePath` and its file SHA256, driver-file LastWriteTimeUtc, last boot time and `runningImageEvidence`.
- hil-acceptance PASSED only if staged == installed == operator-authorized == ImagePath file (64-hex, one value for
  all bundles); otherwise verdict **STAGED_BUILD_NOT_TESTED** (exit 1). No reinstall/restart is attempted.
- Running-driver identity limitation: the driver reports no build identity (`IOCTL_VERSION` = Major/Minor/Build/
  Revision, Revision default 0, plus a debug flag); the loaded image is not hashed. Best available evidence:
  ImagePath file == hashed file and file unchanged since boot (reported, not gated); a driver file replaced after load
  without a restart is not detectable this way.

### C — workflow_run vs update-readme-status.yml race

update-readme-status.yml runs on the same CI completion and pushes a README-only commit with GITHUB_TOKEN (no CI of
its own). authorize now accepts head_sha ≠ tip only if head_sha is an ancestor of the tip, the compare is complete and
the aggregate diff touches only `README.md` (rename sources included); authorship is not trusted. Covered offline:
both interleavings, two metadata commits (granted); superseding code, bot-authored workflow change, rename from code,
non-ancestor, incomplete compare, unreadable tip (denied). **Live:** hil.yml run #4 (39553368697, upstream CI #501
`333a993`) was denied with "master is not a protected branch; head_sha is superseded on master by a non-metadata change
in df1ce242…: tools/test/Run-Tests-CI.ps1" — the real compare API shape and the attempt-specific staging lookup
(run_attempt present for attempt 1) behaved as modelled. The metadata-only grant path has not occurred live yet.
Second-order: protecting master will block the README bot's direct push (needs PR flow or a bypass).

### D — CI evidence

- CI #499 (39544188625, `a86a791`, attempt 1): **success** — Unit ×4 (windows-latest/2022 × Debug/Release), Integration
  ×4, Acceptance, Traceability Coverage, Compliance Report and **Deploy to Staging** all pass; artifact
  `intel-avb-filter-driver-release` present; `traceability-unit-*` artifacts are 1,399 bytes each (single current-run
  summary).
- Per-test evidence for the two corrected failures is **not** publicly available: job logs (403) and artifacts (401)
  need authentication and no test-reporter check runs exist. "Run hardware-independent unit tests = success" on all
  four legs proves test_health_sentinel ran and passed; for test_scripts_consolidate it is consistent with PASS or a
  silently SKIPPED missing binary (non-mandatory suites skip missing binaries). Confirm with `gh run view 39544188625
  --log`.
- CI #501 (`333a993`) success; #500 (`93079b2`) and #502 (`df1ce24`) were running at the time of writing.
- hil.yml #3 (after #499) denied: master unprotected only; #4 see C. No physical job ran.

### Mutation campaign (offline trust-boundary/safety tests — not hardware evidence)

27 workflow mutations: 26 killed by the intended test, 1 (M6 extra dispatch trigger) killed by REAL-009 instead of
REAL-014 (the job guard still keeps dispatch off the runner); 0 survived, 0 timeout, 0 inconclusive. Runner Init gate:
2/2 killed (G1 → 016, 039; G2 → 032, 033). No leftover processes afterwards.

### Regression (offline, `df1ce24` + suite-safety fixture change)

Syntax gate OK; hosted-Unit ps1 tests 12/12 suites green (hil_safety_gate 38/38, hil_binary_provenance 41/41,
health_sentinel 7/7, …); test_hil_trust_boundary 40/40; test_hil_suite_safety 18/18; test_scripts_consolidate Debug and
Release 16/16; test_hil_dispatch_paths 3/10 (unchanged characterization).

### Remaining activation blockers

master protection (and README bot push path); `HIL_AUTHORIZED_DRIVER_SHA256`, `HIL_BUGCHECK_ACK`, `HIL_HALT=false`;
staged build must be installed by the operator for a PASSED staging verdict; runner credential ACL
(`CodexSandboxUsers` → `actions-runner\.credentials*`); unresolved #328 send-path risk; inline hil.yml authorization is
not a runner-wide boundary for the elevated runner; Init approval and test_ioctl_routing classification (Q);
controlled targeted-HIL path (claude.md rule 10) vs "no manually dispatched HIL workflow" — conflict for Q.

---

## Iteration 6 (2026-10-20): review corrections

Offline only. Not committed, not pushed. No driver operation, runner change, HIL execution or workflow dispatch.
Base: HEAD `d4de77d` (= iteration 5 working tree, committed by Q). The two-workflow design is accepted; this
iteration fixes the two real hosted Unit failures of run #496 and hardens hil.yml per review.

## 1. Hosted Unit failures (run #496, 39444619634)

### test_health_sentinel.ps1 (TEST-SENTINEL-001..003)

| | |
|---|---|
| Cause | Since `e85fb52` (build provenance), a Hardware* suite dispatches only artifacts recorded `built` with a matching SHA256 in `test-build-manifest.json` at a known source revision (TC-SAFETY-GATE-002). The sentinel ran the **real repository runner** on a synthetic `ps1` fake with neither a manifest record nor `AVB_TEST_SANDBOX_SOURCE_REVISION/_DIRTY`, so the fake was BLOCKED and the injected health failure never ran. |
| Fix (fixture only) | The sentinel now runs a sandbox **copy** of the runner in its temp tree (same pattern as test_hil_safety_gate / test_hil_binary_provenance): `Run-Tests-CI.ps1`, `Convert-Logs-To-JUnit.ps1`, `tools/lib/TestVerdict.ps1`, an inert `Lib-AvbLifecycle.ps1` stub, `src/avb_integration_fixed.c`, `include/intel_pci_ids.h`, a fixture manifest recording the fake (`status built`, `enabled`, real SHA256, `sourceRevision sentinel-rev`) and `AVB_TEST_SANDBOX_SOURCE_REVISION=sentinel-rev`, `_DIRTY=0`. The repository's real build manifest is never written; production provenance is unchanged. |
| RED | 4/7 on HEAD (and on 8a383d0): TC-FAKE-001 not executed, hook not fired, no INFRA marker. |
| GREEN | 7/7 locally; 7/7 with a hosted-CI-like environment (`GITHUB_ACTIONS=true`, mismatching `GITHUB_SHA`, `GITHUB_ENV` file → stays empty). Configuration-independent (the sentinel always uses its own Debug tree), so it covers all four hosted legs. |

### test_scripts_consolidate.exe (TC-SCRIPTS-001)

| | |
|---|---|
| Rule | `.ps1/.bat/.cmd` under `tools\`, excluding directories named `archive`, `lib`, `scripts` at any depth; budget `< 31`, raised four times, each raise naming the script (`d002a32`: `<31` for Evaluate-IdleTransition.ps1, count 30). `tools\lib\` holds dot-source libraries (BuildHelpers, DriverHelpers, ErrorHandling) and is excluded by design. |
| Additions since the baseline | `tools/test/TestVerdict.ps1` (d5ce8a7, 2026-10-19), `tools/hil-trusted/Invoke-HilPreflight.ps1` (45bb48c), `tools/hil-trusted/HilPreflight.Policy.ps1` (7cbc285) → 33 at run #496. The two hil-trusted scripts were removed in d4de77d → 31 at HEAD, still failing. |
| Fix | `TestVerdict.ps1` is a dot-source library with no side effects ("shared by Parse-TestResults, Convert-Logs-To-JUnit, Run-Tests-CI"), i.e. exactly what `tools\lib\` is for. Moved to `tools/lib/TestVerdict.ps1`; dot-sources in those three scripts → `Join-Path $PSScriptRoot '..\lib\TestVerdict.ps1'`; `Validate-PS1Syntax.ps1` list, `docs/testing/test-verdict-contract.md`, sandbox copies in test_hil_safety_gate, test_hil_binary_provenance, test_junit_verdict_normalization updated. Budget unchanged (`< 31`). |
| RED | existing Debug and Release binaries (built 2026-10-20 from the unchanged source): `FAIL: TC-SCRIPTS-001: 31 canonical scripts in tools\ (target <31)`, exit 1. |
| GREEN | both binaries: `PASS: TC-SCRIPTS-001: 30 canonical scripts`, 16/16, exit 0. |

## 2. hil.yml changes (exact)

- authorize (hosted): HIL re-run denied (`GITHUB_RUN_ATTEMPT` must be 1). Staging evidence is **attempt-specific**:
  jobs from `GET …/actions/runs/{id}/attempts/{attempt}/jobs` (paginated), and only a job named
  `Deploy to Staging (Phase 08 - Transition)` whose own `run_attempt` equals the authorized attempt counts (exactly
  one; completed + success; same head_sha and run_id). A success from (or carried over from) another attempt, a job
  without `run_attempt`, skipped, failed or running staging all deny.
- authorize: **not stale** — head_sha must equal the current `master` tip from `GET …/branches/master` (which also
  must report `protected: true`); a superseded master commit is not tested (its successor's CI triggers HIL).
- authorize: downloads `intel-avb-filter-driver-release` (deploy-staging's artifact) **from the authorized upstream
  run id** and requires exactly one `IntelAvbFilter.sys`; its SHA256 becomes output `staged_driver_sha256`;
  `authorized=true` is emitted only after this step.
- Physical-job preflight (both jobs, identical): `HIL_HALT` must be exactly `false` (missing or any other value
  denies); `HIL_BUGCHECK_ACK` required (missing or malformed denies; no event query without it); HIL re-run denied;
  staged driver hash required; installed driver SHA256 = `HIL_AUTHORIZED_DRIVER_SHA256` (unchanged).
- Run identity records `stagedDriverSha256` next to `installedDriverSha256`.
- hil-acceptance: rejects HIL re-runs and evidence of attempt ≠ 1; each bundle's `stagedDriverSha256` must equal the
  authorized staged hash; one installed driver for the whole run (a change between jobs fails); each suite needs
  exactly one traceability summary; any JUnit **or traceability** file outside the four identified bundles fails;
  the summary states whether the driver under test **is** the staged build of head_sha. Summary/publish globs
  restricted to the four bundles.
- Two mojibake em dashes (`â€”`, introduced by my iteration-5 PowerShell copy of the prerequisites block, committed in
  d4de77d) restored.

## 3. Upstream CI / staging authorization (attempt audit)

| Check | Source | Deny case covered (REAL-012) |
|---|---|---|
| repository, event push, branch master, head repository, conclusion success, completed, CI workflow path | event payload | failed CI, PR, fork PR from its master, feature branch, tag, foreign repository, other workflow named like CI |
| id, run_attempt, head_sha, event, branch, path, status, conclusion, repositories equal payload | `GET /actions/runs/{id}` | wrong SHA, stale attempt (CI re-run after the event) |
| staging job of **this attempt** succeeded | `GET /actions/runs/{id}/attempts/{n}/jobs` (paginated) | skipped / failed / running / absent / other SHA / carried over from attempt 1 / no `run_attempt`; granted: attempt-2 staging, paginated job lists, failed attempt-1 + successful attempt-2 staging |
| master protected; head_sha = master tip | `GET /branches/master` | unprotected, superseded |
| exactly one staged driver from that run | download-artifact `run-id` | none, two (granted: case-insensitive name) |
| HIL run attempt 1 | `GITHUB_RUN_ATTEMPT` | HIL re-run |

Unverified until live: whether the attempt-jobs endpoint lists a job carried over by "re-run failed jobs" with its
original `run_attempt` (assumed; if GitHub relabels it with the new attempt, a carried-over staging success could not
be told apart — in that case a re-run with a reused staging job must be ruled out by policy: do not use "re-run
failed jobs" on master CI).

## 4. Init entries — characterization (static, from source)

`IOCTL_AVB_INIT_DEVICE` (first call per handle) → `AvbBringUpHardware` → `intel_init()` whenever the adapter is
BAR-mapped — i.e. **every** Init call re-initializes the targeted adapter's PTP block:

| Adapter | Effect of one INIT_DEVICE |
|---|---|
| I226 (125B) | generic platform init writes **TIMINCA := 0x18000000** unconditionally and clears TSAUXC DisableSystime; `i226 init_ptp` writes **SYSTIM := 1 (PHC jumps to ~0 s)**, enables SYSTIM via TSAUXC, rewrites TSYNCRXCTL (all packets) and TSYNCTXCTL |
| I210 (1533) | idempotent `init_ptp`: no writes when configured; never re-seeds SYSTIM; repairs only an out-of-policy TIMINCA |
| I219 (15BB) | platform init writes **TIMINCA := 0x02F42400** unconditionally; SYSTIM handling of the I219 path not characterized |

Target adapter: `test_clock_config`, `test_clock_working`: every enumerated adapter (OPEN_ADAPTER + INIT each).
`test_minimal_ioctl`, `test_direct_clock`, `test_dev_lifecycle`, `test_hw_state_machine`: INIT on unbound handles
→ the driver's **global default context**, which `OPEN_ADAPTER` re-points (driver line 4168) — the target depends on
the previous test (order-dependent). `test_ioctl_routing` sends no INIT_DEVICE (macro only in a name table; its
Init class is a conservative over-approximation) but OPEN_ADAPTER re-points the global default.
Detection: the runner's post-test check compares TIMINCA/TSAUXC — TIMINCA 0x18000000 equals the I226 baseline, so
it normally sees no change; the **SYSTIM reset is not detected**, and an Init test would silently "repair" a TIMINCA
left wrong by an earlier test (masking it). Registrations retained; **unattended execution not approved** (Q).

## 5. Runner security: what protects the elevated runner

The removed job-started hook would have run for **every** job on the runner (runner-wide), but it was never
installed. Now:

| Protection | Scope |
|---|---|
| hil.yml from master only (workflow_run), hosted `authorize`, inline preflight, `environment: hil`, runs-on guards | **hil.yml jobs only.** Not a runner-wide boundary: another workflow (any branch a writer pushes, a PR after approval) can target `[self-hosted, Windows, i226]` and run as Administrator with none of these checks |
| test_hil_trust_boundary.ps1 (static) | workflows in the checked-out tree only, not other branches or PR heads |
| Runner group "selected workflows" (`hil.yml@refs/heads/master`) | runner-wide, GitHub-enforced — **requires an organization**; repository-level runners of a user-owned repository cannot be restricted |
| Fork-PR approval for outside collaborators | runner-wide for fork PRs (human approval of the PR's workflow changes) |
| Write access limited to trusted maintainers; ruleset restricting branch creation/updates | runner-wide for pushes |
| A runner-local job-started hook | runner-wide scheduling filter, but **not** a security boundary: the runner is elevated, so any job can rewrite it |

Residual risks: any writer can run arbitrary code as Administrator on the HIL workstation via a branch workflow;
an approved fork PR can do the same; `CodexSandboxUsers` read access to `actions-runner\.credentials*` (earlier
audit) is still open; the workstation is also the development machine.

## 6. HIL suite inventory

Generated from `Run-Tests-CI.ps1` (lists, gap tables, `$HilTestSafety`) and the test sources. "Test cases" =
TC/UT/IT/TEST ids printed by the executable; "Adapter" = static targeting pattern.

| Suite | Executable | Test cases | Adapter | Class | Automatic HIL | Reason |
|---|---|---|---|---|---|---|
| HardwareMandatory | test_ioctl_version | TEST-IOCTL-VERSION-001, UT-VERSION-001, UT-VERSION-002, UT-VERSION-003 | global default context (unbound handle) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareMandatory | test_ioctl_routing | (no TC-id strings) | first enumerated adapter via OPEN_ADAPTER (re-points the global default); sends NO INIT_DEVICE (macro only in a name table) | Init (conservative) | allowed | effectively ReadOnly + OPEN_ADAPTER; unattended approval pending (Q) |
| HardwareMandatory | test_ioctl_simple | (no TC-id strings) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareMandatory | test_minimal_ioctl | TEST-SSOT-003 | each enumerated adapter (OPEN_ADAPTER; also re-points the global default); INIT_DEVICE re-initializes the targeted adapter | Init | allowed | INIT_DEVICE only - characterization below; unattended approval pending (Q) |
| HardwareMandatory | test_ioctl_abi | TC-ABI-001 .. TEST-COVERAGE-001 (21 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareMandatory | test_tx_timestamp_provenance | (no TC-id strings) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareMandatory | test_tx_timestamp_retrieval | TC-CORR-TX-RX-001 | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareMandatory | test_ioctl_phc_query | IT-PHC-QUERY-001 .. VV-PHC-QUERY-003 (18 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareMandatory | test_ioctl_xstamp | TC-XSTAMP-001 .. TEST-IOCTL-XSTAMP-001 (6 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareMandatory | test_ioctl_phc_crosstimestamp_v2 | TC-XSTAMP-V2-HIL-001 .. TC-XSTAMP-V2-HIL-009 (9 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareMandatory | test_all_adapters | (no TC-id strings) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareMandatory | test_multidev_adapter_enum | TC-DEV-I225-001, TEST-MULTIDEV-001 | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareMandatory | test_multi_adapter_phc_sync | TC-MADAPT-001 .. TEST-MULTI-ADAPTER-PARALLEL-001 (7 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareMandatory | test_send_ptp_debug | (no TC-id strings) | global default context (unbound handle) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareMandatory | ptp_clock_control_test | TEST-PTP-001 | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: TC-5b ADJUST_FREQUENCY writes TIMINCA, plus SET_TIMESTAMP and INIT_DEVICE; contradicts the non-mutating Mandatory contract; split into read-only and mutating executables tracked in #340 |
| HardwareMandatory | test_rx_timestamp | TEST-RX-TS-001 .. UT-RX-TS-016 (17 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: SET_RX_TIMESTAMP, SET_QUEUE_TIMESTAMP; not exact-target-aware |
| HardwareMandatory | test_rx_timestamp_complete | TC-RX-TS-001 .. TEST-PTP-RX-TS-001 (16 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: SET_RX_TIMESTAMP, SET_QUEUE_TIMESTAMP; not exact-target-aware |
| HardwareUnit | test_cleanup_archive | TC-CLEANUP-001 .. TEST-CLEANUP-ARCHIVE-001 (8 ids) | no device access | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_ioctl_simple | (no TC-id strings) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_ioctl_routing | (no TC-id strings) | first enumerated adapter via OPEN_ADAPTER (re-points the global default); sends NO INIT_DEVICE (macro only in a name table) | Init (conservative) | allowed | effectively ReadOnly + OPEN_ADAPTER; unattended approval pending (Q) |
| HardwareUnit | test_ioctl_trace | TEST-SSOT-001 | global default context (unbound handle) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_minimal_ioctl | TEST-SSOT-003 | each enumerated adapter (OPEN_ADAPTER; also re-points the global default); INIT_DEVICE re-initializes the targeted adapter | Init | allowed | INIT_DEVICE only - characterization below; unattended approval pending (Q) |
| HardwareUnit | test_clock_config | (no TC-id strings) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default); INIT_DEVICE re-initializes the targeted adapter | Init | allowed | INIT_DEVICE only - characterization below; unattended approval pending (Q) |
| HardwareUnit | test_clock_working | (no TC-id strings) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default); INIT_DEVICE re-initializes the targeted adapter | Init | allowed | INIT_DEVICE only - characterization below; unattended approval pending (Q) |
| HardwareUnit | test_direct_clock | (no TC-id strings) | global default context (unbound handle); INIT_DEVICE re-initializes the targeted adapter | Init | allowed | INIT_DEVICE only - characterization below; unattended approval pending (Q) |
| HardwareUnit | test_hal_performance | TEST-PORTABILITY-HAL-003 | no device access | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_atdecc_event | TC-ATDECC-001 .. TEST-EVENT-003 (6 ids) | global default context (unbound handle) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_atdecc_aen_protocol | TC-AEN-001 .. TEST-EVENT-003 (7 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_ioctl_version | TEST-IOCTL-VERSION-001, UT-VERSION-001, UT-VERSION-002, UT-VERSION-003 | global default context (unbound handle) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_dev_lifecycle | TEST-DEV-LIFECYCLE-001 .. UT-DEV-OPEN-004 (22 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default); INIT_DEVICE re-initializes the targeted adapter | Init | allowed | INIT_DEVICE only - characterization below; unattended approval pending (Q) |
| HardwareUnit | test_power_management | TC-PWRMGMT-001 .. TEST-POWER-MGMT-001 (6 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_tx_timestamp_provenance | (no TC-id strings) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_tx_timestamp_retrieval | TC-CORR-TX-RX-001 | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_ioctl_phc_query | IT-PHC-QUERY-001 .. VV-PHC-QUERY-003 (18 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_ioctl_phc_epoch | TC-EPOCH-001 .. TEST-PTP-EPOCH-001 (6 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_ioctl_xstamp | TC-XSTAMP-001 .. TEST-IOCTL-XSTAMP-001 (6 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_ioctl_xstamp_v2 | TC-XSTAMP-V2-001 .. TC-XSTAMP-V2-HIL-004 (30 ids) | global default context (unbound handle) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_ioctl_abi | TC-ABI-001 .. TEST-COVERAGE-001 (21 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_ptp_crosstimestamp | UT-CORR-003 | global default context (unbound handle) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_zero_polling_overhead | TC-ZP-001 .. TEST-EVENT-006 (7 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_hw_state | TC-PCI-LAT-001, TC-PCI-LAT-002, TC-PCI-LAT-003 | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_statistics_counters | TC-STAT-001 .. TEST-STATISTICS-001 (11 ids) | global default context (unbound handle) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_build_sign | TC-SIGN-001 .. TEST-BUILD-SIGN-001 (8 ids) | global default context (unbound handle) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_compat_win11 | TC-WIN11-001 .. TEST-COMPAT-WIN11-001 (7 ids) | global default context (unbound handle) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_win7_stub | TC-COMPAT-001 .. TEST-COMPAT-WIN7-001 (6 ids) | global default context (unbound handle) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_avtp_tu_bit_events | TEST-EVENT-002 | no device access | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_ptp_event_latency | TC-PTP-LAT-001 .. TEST-EVENT-001 (7 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_event_latency_4ch | TC-LAT4-001 .. TEST-EVENT-005 (6 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_event_nf_zero_polling | TC-NF-ZP-001 .. TEST-EVENT-NF-002 (7 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_event_nf_latency | TC-NF-LAT-001 .. TEST-EVENT-NF-001 (7 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_send_ptp_debug | (no TC-id strings) | global default context (unbound handle) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_timestamp_latency | TC-PERF-PHC-001 .. TEST-PERF-TS-001 (11 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_perf_regression | TC-PERF-REG-001 .. TEST-PERF-REGRESSION-001 (6 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_ndis_fastpath_latency | TC-NDIS-LAT-001 .. TEST-NDIS-LATENCY-001 (6 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_multi_adapter_phc_sync | TC-MADAPT-001 .. TEST-MULTI-ADAPTER-PARALLEL-001 (7 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareUnit | test_ptp_freq | TEST-PTP-FREQ-001 .. UT-PTP-FREQ-015 (16 ids) | exact target per supported DID (AVB_HIL_TARGET_DID) | Mutating:Target | **blocked** | Mutating:Target: ADJUST_FREQUENCY and TIMINCA writes with restore; restore not verifiable unattended, see test_ptp_freq_complete |
| HardwareUnit | test_ptp_freq_complete | IT-FREQ-001 .. VV-FREQ-003 (17 ids) | exact target per supported DID (AVB_HIL_TARGET_DID) | Mutating:Target | **blocked** | Mutating:Target: TIMINCA restore failed on I226 on 2026-10-20, CLEANUP_FAILED 0x18000000 to 0x08000000, investigations/issue-328-unload-hang-20261020.md |
| HardwareUnit | test_qav_cbs | TEST-QAV-CBS-001 .. UT-CBS-014 (15 ids) | exact target per supported DID (AVB_HIL_TARGET_DID) | Mutating:Target | **blocked** | Mutating:Target: WRITE_REGISTER and SETUP_QAV on the target adapter |
| HardwareUnit | ptp_clock_control_test | TEST-PTP-001 | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: SET_TIMESTAMP, ADJUST_FREQUENCY, INIT_DEVICE; not exact-target-aware; #340 |
| HardwareUnit | test_mdio_phy | TEST-MDIO-PHY-001 .. UT-MDIO-015 (17 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: MDIO_WRITE; not exact-target-aware |
| HardwareUnit | test_ts_event_sub | TEST-PLAN-IOCTL-NEW-202 .. UT-TS-SUB-004 (21 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: SET_HW/RX/QUEUE_TIMESTAMP, SET_TARGET_TIME; not exact-target-aware |
| HardwareUnit | test_ptp_getset | TEST-PTP-GETSET-001 .. UT-PTP-GETSET-014 (15 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: SET_TIMESTAMP, SET_HW_TIMESTAMPING; not exact-target-aware |
| HardwareUnit | test_hw_ts_ctrl | TEST-HW-TS-CTRL-001 .. UT-HW-TS-013 (14 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: SET_HW_TIMESTAMPING; not exact-target-aware |
| HardwareUnit | test_rx_timestamp | TEST-RX-TS-001 .. UT-RX-TS-016 (17 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: SET_RX_TIMESTAMP, SET_QUEUE_TIMESTAMP; not exact-target-aware |
| HardwareUnit | test_rx_timestamp_complete | TC-RX-TS-001 .. TEST-PTP-RX-TS-001 (16 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: SET_RX_TIMESTAMP, SET_QUEUE_TIMESTAMP; not exact-target-aware |
| HardwareUnit | test_ioctl_target_time | TC-AUX-001 .. TC-TARGET-EVENT-001 (16 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: SET_TARGET_TIME, SET_HW_TIMESTAMPING; not exact-target-aware |
| HardwareUnit | test_ioctl_tas | TC-TAS-001 .. TC-TAS-010 (10 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: SETUP_TAS, DISARM_TAS; TSN configuration change; not exact-target-aware |
| HardwareUnit | test_ioctl_fp_ptm | TC-FP-001 .. TC-PTM-ERR-001 (15 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: SETUP_FP, SETUP_PTM; TSN configuration change; not exact-target-aware |
| HardwareUnit | test_ioctl_offset | TEST-IOCTL-OFFSET-001 | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: PHC_OFFSET_ADJUST; not exact-target-aware |
| HardwareUnit | test_ioctl_phc_monotonicity | TC-MONO-001 .. TEST-PHC-MONOTONIC-001 (5 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: PHC_OFFSET_ADJUST; not exact-target-aware |
| HardwareUnit | test_ioctl_access_control | TC-ACCTL-001 .. TEST-IOCTL-ACCTL-001 (6 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: PHC_OFFSET_ADJUST; not exact-target-aware |
| HardwareUnit | test_ioctl_buffer_fuzz | TC-FUZZ-001 .. TEST-SECURITY-BUFFER-001 (7 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: ADJUST_FREQUENCY, PHC_OFFSET_ADJUST; not exact-target-aware |
| HardwareUnit | test_event_log | TEST-EVENT-LOG-001 | global default context (unbound handle) | Mutating | **blocked** | Mutating: ADJUST_FREQUENCY; not exact-target-aware |
| HardwareUnit | test_eee_lpi | TC-EEE-001 .. TEST-EEE-001 (6 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: EEE_ENABLE/DISABLE; not exact-target-aware |
| HardwareUnit | test_pfc_pause | TC-PFC-001 .. TEST-PFC-001 (6 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: PFC_ENABLE; not exact-target-aware |
| HardwareUnit | test_srp_interface | TC-SRP-001 .. TEST-SRP-001 (6 ids) | global default context (unbound handle) | Mutating | **blocked** | Mutating: SRP_REGISTER/DEREGISTER_STREAM; not exact-target-aware |
| HardwareUnit | test_vlan_tag | TC-VLAN-001 .. TEST-VLAN-001 (6 ids) | global default context (unbound handle) | Mutating | **blocked** | Mutating: VLAN_ENABLE/DISABLE; not exact-target-aware |
| HardwareUnit | test_vlan_pcp_tc_mapping | TC-VLAN-001 .. TEST-VLAN-PCP-001 (7 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: SETUP_QAV; not exact-target-aware |
| HardwareUnit | test_security_validation | TEST-SECURITY-001 | global default context (unbound handle) | Mutating | **blocked** | Mutating: SET_TIMESTAMP; not exact-target-aware |
| HardwareUnit | test_ioctl_launch_time | IT-LAUNCH-001 .. VV-LAUNCH-002 (19 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: SET_LAUNCH_TIME; not exact-target-aware |
| HardwareUnit | test_gptp_phc_interface | TC-GPTP-001 .. TEST-GPTP-PHC-001 (6 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: ADJUST_FREQUENCY, PHC_OFFSET_ADJUST; not exact-target-aware |
| HardwareUnit | test_error_recovery | TC-ERR-001 .. TEST-ERROR-RECOVERY-001 (8 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: ADJUST_FREQUENCY, PHC_OFFSET_ADJUST; not exact-target-aware |
| HardwareIntegration | test_all_adapters | (no TC-id strings) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareIntegration | test_multidev_adapter_enum | TC-DEV-I225-001, TEST-MULTIDEV-001 | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareIntegration | test_device_register_access | (no TC-id strings) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareIntegration | test_ndis_send_path | TEST-NDIS-SEND-PATH-001 | global default context (unbound handle) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareIntegration | test_ndis_receive_path | TEST-NDIS-RECEIVE-PATH-001 | global default context (unbound handle) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareIntegration | test_hw_state_machine | (no TC-id strings) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default); INIT_DEVICE re-initializes the targeted adapter | Init | allowed | INIT_DEVICE only - characterization below; unattended approval pending (Q) |
| HardwareIntegration | test_lazy_initialization | (no TC-id strings) | global default context (unbound handle) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareIntegration | test_registry_diagnostics | TC-DEBUG-REG-001 .. VV-CORR-001 (9 ids) | global default context (unbound handle) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareIntegration | test_vv_corr_003_crossdomain | UT-CORR-004, VV-CORR-003 | global default context (unbound handle) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareIntegration | test_ptp_001 | TC-PTP-001, TEST-PTP-001 | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareIntegration | test_ptp_corr | IT-CORR-001 .. UT-CORR-003 (6 ids) | global default context (unbound handle) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareIntegration | test_hardware_detection | TC-HW-001 .. TEST-HW-DETECT-CAPS-001 (5 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | allowed | no state-changing IOCTL in the executed path |
| HardwareIntegration | avb_test_i219 | TEST-HW-I219-MDIO-001 | exact target per supported DID (AVB_HIL_TARGET_DID) | Mutating:Target | **blocked** | Mutating:Target: WRITE_REGISTER, SET_HW_TIMESTAMPING, INIT_DEVICE on the target adapter; I219 compatibility, #261 |
| HardwareIntegration | avb_test_portability | TC-PORT-001 .. TC-PORT-007 (7 ids) | exact target per supported DID (AVB_HIL_TARGET_DID) | Mutating:Target | **blocked** | Mutating:Target: SETUP_TAS, SETUP_FP, SETUP_PTM, WRITE_REGISTER; TSN configuration change; QA-SC-PORT-001, #114 |
| HardwareIntegration | avb_test_i217 | (no TC-id strings) | exact target per supported DID (AVB_HIL_TARGET_DID) | Mutating:Target | **blocked** | Mutating:Target: SETUP_TAS, SETUP_FP, WRITE_REGISTER; TSN configuration change; #114 |
| HardwareIntegration | avb_test_i225 | (no TC-id strings) | exact target per supported DID (AVB_HIL_TARGET_DID) | Mutating:Target | **blocked** | Mutating:Target: SETUP_TAS, SETUP_FP, SETUP_PTM, WRITE_REGISTER; TSN configuration change; #114 |
| HardwareIntegration | avb_test_i226_um | (no TC-id strings) | exact target per supported DID (AVB_HIL_TARGET_DID) | Mutating:Target | **blocked** | Mutating:Target: SETUP_TAS, SETUP_FP, SETUP_PTM, WRITE_REGISTER; TSN configuration change; #114 |
| HardwareIntegration | test_tsn_ioctl_handlers_um | (no TC-id strings) | global default context (unbound handle) | Mutating | **blocked** | Mutating: SETUP_TAS/FP/PTM, INIT_DEVICE; TSN configuration change; not exact-target-aware |
| HardwareIntegration | test_ptp_corr_extended | UT-CORR-001 .. VV-CORR-001 (5 ids) | global default context (unbound handle) | Mutating | **blocked** | Mutating: SET_HW_TIMESTAMPING; not exact-target-aware |
| HardwareIntegration | ptp_clock_control_production | (no TC-id strings) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: SET_TIMESTAMP, ADJUST_FREQUENCY; not exact-target-aware |
| HardwareIntegration | ptp_ioctl_latency_test | TC-CLOCKCFG-001 .. TEST-XSTAMP-PERF-001 (8 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: SET_TIMESTAMP, INIT_DEVICE; not exact-target-aware |
| HardwareIntegration | hw_timestamping_control | (no TC-id strings) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: SET_HW_TIMESTAMPING; not exact-target-aware |
| HardwareIntegration | rx_timestamping | (no TC-id strings) | global default context (unbound handle) | Mutating | **blocked** | Mutating: SET_RX_TIMESTAMP, SET_QUEUE_TIMESTAMP; not exact-target-aware |
| HardwareIntegration | tsauxc_toggle_test | (no TC-id strings) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | Mutating | **blocked** | Mutating: SET_HW_TIMESTAMPING; not exact-target-aware |
| HardwareIntegration | test_gptp_daemon_coexist | TC-COEXIST-001 .. TEST-GPTP-COMPAT-001 (6 ids) | global default context (unbound handle) | Mutating | **blocked** | Mutating: ADJUST_FREQUENCY, PHC_OFFSET_ADJUST; not exact-target-aware |
| HardwareIntegration | test_ptp_phc_stability | UT-CORR-005 .. UT-CORR-009 (5 ids) | each enumerated adapter (OPEN_ADAPTER; also re-points the global default) | ReadOnly | **blocked** | ReadOnly: TEMPORARY POLICY EXCLUSION 2026-10-20 - the default run is UT-CORR-007/008 only, but the binary contains the UT-CORR-009 driver reinstall path, ControlService STOP plus system Install-Driver-Elevated.ps1 -Action Reinstall, reachable only with --case UT-CORR-009 or --allow-lifecycle; coverage gap: UT-CORR-007/008 TX-PHC correlation, #149 #317 |
| HardwareIntegration | ssot_register_validation_test | (no TC-id strings) | no device access | Unknown | **blocked** | Unknown: no build entry or source mapping in Build-Tests.ps1 |
| HardwareMutation | test_ptp_phc_stability --case UT-CORR-005 / 006 | UT-CORR-005, UT-CORR-006 | exact target | Mutating:Target | **blocked** | never dispatched automatically |
| (none) | test_hot_plug, test_lifecycle_coverage | - | adapter | Unsafe | **blocked** | NIC administration; never under GitHub Actions |
| (none) | test_s3_sleep_wake, test_vv_corr_001_stability | - | host / PHC 24 h | - | **blocked** | sleep / 24-hour run; absent from all suites |

## 7. Coverage gaps (automatic HIL)

- Policy: `test_ptp_phc_stability` (UT-CORR-007/008 TX-PHC correlation, #149 #317) — temporary policy exclusion;
  classifier unchanged (ReadOnly).
- Previously executed, excluded: test_ptp_freq, test_ptp_freq_complete, test_qav_cbs, avb_test_i217/i219/i225/
  i226_um, avb_test_portability.
- Gate-BLOCKED (never executed since #343): 33 Mutating/Unknown tests (table above).
- Never automatic: HardwareMutation UT-CORR-005/006, NIC lifecycle, S3, 24 h.
- Init entries pending approval (section 4); SYSTIM reset undetected by the post-test check.
- Driver under test is the operator-installed one; acceptance reports whether it equals the staged build.
- HIL results are not part of CI's traceability-coverage (published by hil-acceptance); no executable acceptance
  suite beyond the hardware verdict.

## 8. Offline regression

| Test | Result |
|---|---|
| test_scripts_consolidate.exe Debug / Release | RED 31 (exit 1) → GREEN 30, 16/16 (exit 0) |
| test_health_sentinel.ps1 | RED 4/7 → GREEN 7/7 (local and CI-like env) |
| All 12 hosted-Unit ps1 tests ($UnitTests) | 12/12 suites green: timinca_ownership 14/14, known_device_table 3/3, requirement_outcomes 7/7, parse_results_aggregation 21/21, junit_verdict_normalization 13/13, no_interactive_waits 1/1, build_tests_syspaths 12/12, health_sentinel 7/7, hil_safety_gate 36/36, hil_binary_provenance 41/41, preflight_sentinel 5/5, evaluate_idle_transition 22/22 |
| test_hil_trust_boundary.ps1 | 40/40 (REAL-012: 26 authorize, 5 staged-driver, 19 acceptance cases; REAL-016: 21 preflight cases) |
| test_hil_suite_safety.ps1 | 18/18 (REAL-003 GREEN after the phc_stability policy exclusion) |
| test_hil_dispatch_paths.ps1 | 3/10 — unchanged intentional characterization of legacy launchers |
| Validate-PS1Syntax (10 changed files; default CI list) | OK |
| Workflow mutation check | 22/22 detected (M6 extra dispatch trigger by REAL-009; job guard still excludes dispatch) |

Not run: the C test binaries of the hosted Unit suite other than test_scripts_consolidate (unchanged sources; they
need Build-Tests, which would rewrite the local test-build-manifest of the HIL workstation).

Harness incident (disclosed): the first REAL-016 run executed the preflight with the stubs displaced — `New-Object`
autoloaded Microsoft.PowerShell.Utility, whose `Get-FileHash` replaced the stub — so it performed real read-only
queries on this workstation (service status, installed driver SHA256 `9A098B71…08E1`, possibly event log / PnP /
adapter status; nothing was changed). Fixed: modules are imported before the stubs and the prelude aborts with
exit 99 if any stub does not resolve to itself (verified in isolation first). A debug reproduction also showed the
IntelAvbFilter service **Stopped** (read-only observation).

## 9. Readiness

Hosted CI: both known Unit failures are fixed (believed to make CI → deploy-staging succeed; confirm on the next
master run). Automatic HIL: **NO-GO** until Q decides/sets:
1. Init entries: approve unattended execution as characterized, or exclude them (temporary policy exclusion).
2. Driver policy: report-only (current) or require installed = staged build.
3. GitHub: protect `master`; set `HIL_AUTHORIZED_DRIVER_SHA256`, `HIL_BUGCHECK_ACK`, `HIL_HALT=false`; inspect `hil`
   environment (no reviewers/wait timer, deployment branch `master`); fork-PR approval; limit writers.
4. Accept the runner-wide residual risk of section 5 (or move to an organization runner group).
5. #328 send-path issue remains open; IntelAvbFilter service observed **Stopped** on this workstation (read-only
   observation during this iteration, see section 8).

---

## Iteration 8 (2026-10-21) — P0 addendum: LocalSuites + pre-commit HIL-TDD (offline only)

Boundary kept: no build/install of the driver, no HIL, no runner/Windows/GitHub change, no commit/push by the agent
(Q committed the agent's edits locally while they landed: 18 commits after 6f666d8, none pushed).

### Root cause of FullSuite_20261021_095005.log (blank exit codes)
- Immediate: `LocalSuites.ps1` assigned an unqualified `$LASTEXITCODE = $null`. Run as documented
  (`& LocalSuites.ps1 *>&1 | Tee-Object`), that is a script-scope variable shadowing the automatic one for every child
  script; native calls write only `$global:LASTEXITCODE`, so callers read `$null` (printed blank).
  Reproduced only with `&` invocation, not with `powershell -File` (global scope).
- Systemic: each script treated a missing exit code as success (`-ne 0 -and $null -ne`, `exit $null` = 0,
  Install-Driver-Elevated mapped null to 0, Run-Tests-CI counted `$null` exit as PASSED).
- Fix: `tools/lib/NativeExit.ps1` (global-only reads, missing = failure) in the whole chain; static AST guard.

### Delivered (all offline RED -> GREEN)
| Area | Files | Tests |
|---|---|---|
| Exit codes | lib/NativeExit.ps1; vs_compile (unique temp files), Build-Driver, Build-Tests, Install-Driver(-Elevated), Run-Tests-CI, Run-Tests-Elevated, Test-DriverPreFlight | test_native_exit_codes 11/11 (HEAD: 1/11) |
| Local pre-commit provenance | lib/SourceDigest.ps1; Build-Tests manifest (provenanceMode, sourceDigest, builtUtc injected); Run-Tests-CI local mode | test_hil_binary_provenance 52/52 (042-052 new; baseline RED for 042-045, 047, 048, 050, 051; 046/052 invariance) |
| Results JSON | Run-Tests-CI `-ResultsJson` (avb-runner-results/1), TestVerdict `Get-AvbTestcaseVerdicts`; `-CaptureDbgView` now rejected (was silently ignored) | test_runner_results_json 16/16 (baseline: functions absent) |
| LocalSuites | rewritten; order unchanged; preflight `installed == built`; results JSON inventory; stop rules; per-configuration operator-confirmed reproduction | test_local_suites 16/16 (baseline: functions absent) |
| DbgView | Run-Tests-Elevated: `-Suite` routes through Run-Tests-CI gates; capture confirmed before test (else test not run); per-PID stop verified; MISSING/EMPTY/CAPTURED; sidecar `.capture.json`; bounded child timeout | test_local_suites 013-015 |
Regression: hilgate 38/38, sentinel 7/7, verdict 13/13, syntax gate 9/9.

### Defects found in the agent's own work this iteration (fixed, for the record)
- Commit 8e76ddd contains a dangling line-continuation backtick in Build-Tests.ps1 (manifest call swallowed the
  following `if` block; parser accepted it); fixed in c58b139. Not a usable bisect point.
- `@(f)` around a unary-comma return nests arrays (results `testcases`, test inventory) - fixed; tests tightened.

### Limitations (UNVERIFIED until physical validation)
- Nothing here ran against hardware, a real build, a real install or a real DebugView; all evidence is synthetic.
- Loaded driver image is not hashed: after Reinstall without reboot, "running image == installed file" is UNPROVEN.
- DbgView `/l` creating the log promptly and `Stop-Process` not losing buffered lines: UNVERIFIED.
- Results document is written only when Run-Tests-CI completes; early exits leave none (LocalSuites treats that as stop).

### NOT started (P0 "Restore fully automatic staged-driver HIL CI")
CI-safe installer mode; signed Debug+Release staging packages + manifest; hil.yml install/pre/post verification,
testable serialization, removal of HIL_AUTHORIZED_DRIVER_SHA256 / HIL_HALT / HIL_BUGCHECK_ACK, crash-evidence
handling, HIL_NOT_EXECUTED; master-protection policy; the 16 CI RED/GREEN cases. Needs Q decisions (see report).

---

## Iteration 9 (2026-10-21) - automatic staged-driver HIL CI (offline implementation; Q decisions 1-7)

Boundary kept by the agent: no driver operation, no physical HIL, no runner/Windows/GitHub change, no dispatch, no
commit/push. (Q committed and pushed the work to branch fix/hil-ci-suite-20261021; 5 files of the final fixes are
uncommitted.) Everything below is offline orchestration evidence; driver/hardware behaviour is UNVERIFIED.

### Facts established (read-only)
- master: classic protection disabled; repository ruleset `protect-default-branch` (id 25536988, active, created
  2026-10-21 09:42Z): no deletion, no force push, linear history, pull request required (0 approvals). Bypass list not
  visible unauthenticated. -> README bot direct pushes are rejected (its `git push || exit 0` hid that); local commits
  reach master only via PR.
- Signing: local builds are signed by the machine-specific `WDKTestCert User,134201267570487980` (275C2424...), trusted
  here; hosted staging created a NEW WDKTestCert and a NEW self-signed cert per job -> no stable signer existed.
- Former blocker: staging uploaded the whole build dir (two IntelAvbFilter.sys + obj/) -> `authorize` ("exactly one
  .sys") would have denied every run once the operator variables were set.
- Install-Driver.ps1 attended path: STOP_PENDING -> Read-Host -> Restart-Computer; downgrade Read-Host; DriverStore
  Remove-Item; Disable-NetAdapter on every filter-bound adapter (may include the management NIC); PendingFileRename
  reboot replacement reported as success; EventLog service restart; netcfg -u failure returns $true.
  PRE-EXISTING DEFECT (not changed, Q decision): `sc stop IntelAvbFilter` / `sc query ...` call the Set-Content alias,
  writing files `stop` (2026-10-21 11:48) and `query` in the repository root; the attended "Step 1: Stopping service"
  never stopped anything.

### Implementation
| Concern | Files |
|---|---|
| Stable signer | Build-Driver.ps1 `-SigningPfx` (re-sign .sys, regenerate + sign catalog, verify thumbprints); tools/ci/signing/README.md (one-time setup); pinned `tools/ci/signing/hil-test-signer.cer` (NOT yet created) |
| Package identity | tools/lib/DriverPackage.ps1 (manifest avb-driver-package/1; verification incl. pinned signer + local trust) |
| Staging | ci-standards-compliance.yml deploy-staging: Debug+Release matrix, secrets HIL_TEST_SIGNING_PFX_BASE64/_PASSWORD (fail loudly if absent), manifest + self-check, upload exactly the package; tag release ships the signer .cer |
| Unattended install | Install-Driver.ps1 `-NonInteractive -PackageDir -ExpectedSysSha256 -StepTimeoutSeconds -ResultJson` -> tools/lib/DriverDeploy.ps1 (same netcfg/ETW mechanism; bounded; no prompt/reboot/adapter disable/DriverStore delete/EventLog restart; timeouts leave netcfg running; exit 0/1/3) |
| Serialization | tools/lib/HilLock.ps1 (%ProgramData%\IntelAvbFilter-hil.lock; CI + LocalSuites); hil-release needs hil-debug |
| Crash evidence | tools/lib/HilMachineHealth.ps1 (HISTORICAL / PREVIOUS_RUN_CRASHED / CURRENT_RUN_INTERRUPTED; minidumps copied) |
| Runner orchestration | tools/ci/Invoke-HilDeployment.ps1 (lock -> crash evidence -> package -> install -> pre-flight; in-flight record next to the lock) |
| Workflow | hil.yml: authorize downloads both packages of the upstream run, checks manifests, hands them over; hil-debug / hil-release atomic (preflight -> checkout -> deploy -> build tests -> verify -> identity -> Mandatory/Unit/Integration -> lock release); acceptance per configuration; no `vars.*` |
| README bot | update-readme-status.yml: bot branch automation/readme-status + PR; never pushes master; failures are loud |

### Connectivity loss during deployment (#328 risk)
netcfg -u/-l restart the NDIS stack of every adapter bound to MS_IntelAvbFilter; the deployment records whether the
default-route (management) adapter is bound (`managementRisk`). If the runner loses its connection: the job may be
failed server-side while the local process continues or is lost; RUNNER_TEMP evidence does not survive the next job,
so the record is persisted first to `%ProgramData%\IntelAvbFilter-hil.lock.inflight.json` (verdict IN_FLIGHT before
the installer runs). The next HIL run takes over the stale lock (owner recorded), attaches the in-flight record and
the crash evidence, re-checks STOP_PENDING / pending replacement (NEEDS_OPERATOR, no reboot) and otherwise redeploys.
A netcfg that does not finish within the bound is left running (never killed) -> NEEDS_OPERATOR. Nothing claims a
recovery; nothing restarts the PC. UNVERIFIED on the real machine: whether netcfg briefly drops the management
connection, and how the GitHub runner reacts to that.

### Offline results (all GREEN; RED shown against the pre-iteration code)
syntax 16/16; native-exit 11/11; provenance 52/52; results-json 16/16; local-suites 16/16; ci-driver-deploy 27/27
(baseline: libraries absent; mutation probes M1-M3 killed); trust boundary 40/40 (pre-iteration workflows 32/40 -
REAL-007..013, 016 RED; FX + REAL-001..006/014/015 invariant; workflow mutants M1-M4 killed); suite safety 18/18;
identity 9/9; hilgate 38/38; sentinel 7/7; verdict 13/13.

### Unavoidable manual administration (once)
1. Create the HIL test-signing certificate; secrets HIL_TEST_SIGNING_PFX_BASE64 / _PASSWORD; commit the public
   `tools/ci/signing/hil-test-signer.cer` (PR).  2. Trust it on the HIL machine (LocalMachine Root + TrustedPublisher).
3. Optionally allow GitHub Actions to create pull requests (README bot), otherwise merge the bot branch manually.

## Iteration 10 (2026-10-27) - P0 complete CI/HIL execution: classification of ssot_register_validation_test

### Facts (verified)
- Source `tests/integration/tsn/ssot_register_validation_test.c` issues only CreateFile, IOCTL_AVB_GET_DEVICE_INFO,
  IOCTL_AVB_GET_HW_STATE and IOCTL_AVB_READ_REGISTER on the default handle (no ENUM/OPEN_ADAPTER); no write, no
  INIT_DEVICE, no NIC administration.  Registry: Unknown -> ReadOnly.
- The gate suite never checked this entry: its source mapping is keyed by Build-Tests Name, which was
  `ssot_register_validation` (Output `ssot_register_validation_test.exe`).  RED: ReadOnly alone -> TEST-HILGATE-008
  `ssot_register_validation_test: no build entry` (82/83, 231 s).  GREEN after renaming the Build-Tests Name to the
  registered entry name: 83/83 (199 s); 006 (no mutating IOCTL) and 009 (no NIC administration) PASS on the source.
  TEST-COMPLETE unchanged (001: 39/148, 003: 2 scripts - pre-existing).
- Existing hardware evidence, FullSuite legacy path 2026-10-27 (logs/ssot_register_validation_test.exe_*):
  Debug 09:28:07 and 12:34:43, Release 10:16:29 - each 19 passed / 0 failed; default handle = I219 (caps 0xC3);
  only IoctlCount changed on the three adapters.  The I210 and I226 are never read by Phase 2.

### Finding (driver security, NOT implemented - outside P0)
- The Release driver is compiled WITHOUT NDEBUG: `build/x64/Release/IntelAvbFilter/obj/IntelAvbFilter.tlog/
  CL.command.1.tlog` (10:06:50) defines _WIN64 _AMD64_ AMD64 _WIN32_WINNT WINVER WINNT NTDDI_VERSION NDIS_WDM NDIS630
  AVB_VERSION_BUILD AVB_VERSION_REVISION only.  Every `#ifndef NDEBUG` block (src/device.c:720,
  src/avb_integration_fixed.c:2190, include/avb_ioctl.h:87/260) is therefore compiled into Release, including
  IOCTL_AVB_WRITE_REGISTER - contrary to ADR-SEC-001 ("NDEBUG defined automatically").  Consistent with the Release
  10:16:29 run reading SYSTIM/TIMINCA/TSAUXC.  UNVERIFIED: that the driver loaded at 10:16:29 was the Release build.

### Remaining execution gaps of this entry (not started)
- Phase 2 covers the default adapter only, not every adapter advertising the PTP capability.
- No `[TAG] ID` testcase lines (one overall verdict per run).
- If ADR-SEC-001 is enforced, Release READ_REGISTER rejection would be reported as FAIL, not evidence-backed
  NOT_SUPPORTED.
## Iteration 11 (2026-10-27) - P0: single producer of test_tsn_ioctl_handlers.exe

### Facts (verified)
- Producers before the repair: (1) Build-Tests cl `test_tsn_ioctl_handlers`, Disabled, Source
  tests/unit/ioctl/test_tsn_ioctl_handlers.c = 9-line kernel-mode placeholder (precomp.h, no main); (2) Build-Tests
  nmake `tsn_ioctl_test` -> tests/integration/tsn/tsn_ioctl_test.mak, which compiles
  tests/integration/tsn/test_tsn_ioctl_handlers_um.c and links it as test_tsn_ioctl_handlers.exe (no declared artifact,
  no SHA); (3) out of chain: tools/build/Build-Driver.ps1 -BuildTests runs the same makefile without CFG (= Debug).
  test_tsn_ioctl_handlers_um (cl) compiles the same _um.c source.
- Flags: nmake adds /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DAVB_TEST_USERMODE /DWIN64 /D_WIN64 /I.;
  the source has no preprocessor conditionals and calls CreateFileW explicitly; include/ does not reference
  AVB_TEST_USERMODE or WIN32_LEAN_AND_MEAN.  Neither builder uses configuration-dependent flags.  Same program
  (source-level analysis; binaries not compared at runtime).
- Coverage (identical): INIT_DEVICE, then SETUP_TAS/FP/PTM once with a zeroed 1 KB buffer and once with a real
  configuration, default handle only, no capability check, no restoration, no `[TAG] ID` lines, unconditional exit 0.
- Canonical producer selected: cl `test_tsn_ioctl_handlers`, Source -> _um.c, enabled (manifest SHA, Source visible to
  the HILGATE source checks, Name = registered entry).  nmake entry `tsn_ioctl_test` removed from Build-Tests (the
  .mak stays: Build-Driver.ps1 and IntelAvbFilter.vcxproj reference it).  Registry: Unknown -> Mutating.
  #328 hazard hold UNCHANGED (Q's explicit TAS/FP restriction).
- RED: TEST-HILPROV-010 `statically Disabled yet dispatchable: test_tsn_ioctl_handlers (listed in HardwareUnitTests)`
  51/52.  GREEN: HILPROV 52/52, HILGATE 83/83 (008 now checks the real source; 081 hold PASS), COMPLETE-001 39 -> 38.
- Build (full Build-Tests, digest D6FC457D..., HEAD 8b33641 + working tree): Debug 136 s, Release 132 s, each
  147 entries = 141 built / 6 disabled / 0 failed; test_tsn_ioctl_handlers.exe Debug 5E9FC876D2C3, Release
  FACCA9E464DD (file = manifest).

### Remaining (not started)
- The program cannot fail (exit 0 always) and configures TAS/FP/PTM without restoration - must be repaired before the
  #328 hold could ever be lifted.  Two registered entries now execute the same program.
- Build-Driver.ps1 -BuildTests still overwrites five Debug nmake artifacts outside the manifest; the runner then
  blocks them visibly (SHA256 mismatch), never silently.

### Correction (2026-10-27, Iteration 12)
- The committed form (f10d6e0) did NOT remove the nmake entry `tsn_ioctl_test`: it is kept, links `tsn_ioctl_test.exe`,
  is registered in HardwareIntegration (Mutating, #328 hazard hold).  Manifests at f10d6e0: 148 entries = 142 built /
  6 disabled / 0 failed (Debug and Release).  Offline at f10d6e0: HILPROV 52/52, HILGATE 83/83, COMPLETE 001 38/148 and
  003 2 scripts (pre-existing).

## Iteration 12 (2026-10-27) - P0: machine safety record prerequisite and the first Init entry

### Facts (verified)
- LocalSuites_20261027_163938 (f10d6e0, clean-commit, Debug + Release): 72 BLOCKED(SAFETY) rows per configuration =
  53 `machine safety record UNAVAILABLE: safety record missing` + 17 #328 hazard hold + 2 Unsafe without authorization.
- `%ProgramData%\IntelAvbFilter-hil-safety.json` has never existed on DESKTOP-RSUO2LV: all 257 real-run runner notes
  say UNAVAILABLE; all 28 CLEAR notes are the sandbox fixture (`%TEMP%\sentinel_test_*\sandbox-hil-safety.json`).
  Missing in every LocalSuites run since 6583fec (2026-10-21 19:16).  Initialize-AvbHilSafetyRecord and
  Approve-AvbHilSafetyBaseline were never executed.  New-AvbHilBaselineEvidence.ps1 produced NOT_VERIFIED
  (2026-10-22 12:45, 12:46: DID double-hex) and BASELINE_VERIFIED (2026-10-22 21:42, driver 51DEDF..., now outdated).
- No producer of `avb-hil-recovery-verification/1` exists; the baseline producer emits no `phcTolerance`;
  `$HilMutatedState` is empty.  Hence (Run-Tests-CI.ps1 post-test check) every executed non-ReadOnly entry ends
  INCONCLUSIVE_RESTORATION, persisted as a GLOBAL safety event (HALTED) - code reading + HILGATE-042, not HIL-observed.
- First non-ReadOnly entry of the run: `test_minimal_ioctl.exe` (Init, HardwareUnit row 5).  Source
  tests/unit/ioctl/test_minimal_ioctl.c: INIT_DEVICE on an unbound handle (driver-global default adapter), ENUM,
  per adapter OPEN_ADAPTER(i) + GET_CLOCK_CONFIG; no baseline, no restoration, exit 0 whenever ENUM succeeds.
- INIT_DEVICE (src/avb_integration_fixed.c:1930 -> AvbBringUpHardware:464) calls intel_init() WITHOUT the PTP_READY
  idempotency guard that AvbEnsureDeviceReady:798 has.  Per device source (UNVERIFIED on hardware):
  I226 init_ptp: SYSTIM := 1 unconditionally, TSAUXC RMW, TSYNCRXCTL/TSYNCTXCTL overwritten, ETQF(3) written,
  TXSTMPL/H read-cleared, TIMINCA only if 0.  I219 init_ptp: TIMINCA := INTEL_TIMINCA_I219_INIT unconditionally,
  TSAUXC RMW, ETQF0/ETQS0 written, software SYSTIM offset re-seeded from the system clock.  I210 init_ptp: observe
  first; on a configured device no SYSTIM seed, TIMINCA repaired only outside the policy range.
- Existing ungated hardware output (FullSuite legacy path, test_minimal_ioctl.exe_20261027_162442.log): INIT_DEVICE
  TRUE; driver enumeration order 0=0x15BB 1=0x1533 2=0x125B (2026-10-22 baseline: 0=0x125B 1=0x1533 2=0x15BB, i.e.
  order changes between driver loads, so the default adapter INIT hits is not fixed); GET_CLOCK_CONFIG cfg.status
  0x00000000 on every adapter -> the test prints no clock value and still exits 0.
- No gated (runner) execution of test_minimal_ioctl exists: 2026-10-21 coverage gap, since 2026-10-22 safety-record
  BLOCKED.
- PHC observation window (hardware, hil-baseline-evidence-20261022_214259.json): the runner's Get-AvbHilClockState
  window is 5 back-to-back GET_CLOCK_CONFIG reads spanning 0.45-1.4 ms.  Pass-to-pass evaluator metrics without any
  mutation: offset uncertainty U 1.1-116 ms, rate uncertainty U 1.4e6-2.8e8 ppb (residual sigma 41-4753 ns, read
  latency 15-28 us).  With such windows no gPTP-justifiable tolerance can ever yield RESTORATION_VERIFIED:
  restoration-observability defect of the runner's PHC window (not of the evaluator).
- Undisturbed run counts: 6 Init + 29 Mutating + 18 Mutating:Target rows per configuration are safety-record BLOCKED;
  further 17 hazard hold, 2 Unsafe BLOCKED + 4 Unsafe gap rows, 1 ReadOnly 24-h gap (test_vv_corr_001_stability) and
  4 HardwareMandatory gap rows (entries also listed in HardwareUnit) = 81 not executed per configuration.

### Operator decisions (Q 2026-10-27)
- Order: repair the runner PHC observation window (cause 2) BEFORE the record is provisioned, so the fresh baseline
  evidence carries usable PHC characterization.
- #328 hazard hold: kept; its 17 rows per configuration are reported as operator-restricted residual.
- Unsafe: attended execution authorized for the suite-listed Unsafe entries test_lifecycle_coverage (HardwareUnit) and
  test_hot_plug (HardwareIntegration).
- Correction: test_s3_sleep_wake, test_vv_corr_001_stability and test_ptp_phc_lifecycle are in no Run-Tests-CI suite
  list (only $HardwareIntegrationCoverageGaps + registry) and were never run by the runner; they are not Phase 1
  blockers.  Questions about S3 and a 24-h run were asked in error; nothing is changed for these entries.
- Approval: Q approves the 2026-10-27 18:41 evidence document now (no phcTolerance in it).
- PHC (Q 2026-10-27): "no validation of phc quality for security relevant checks" - no PHC tolerance is defined;
  PHC time/rate are not a safety criterion of the restoration check.  Interpretation submitted to Q for confirmation
  before any runner change: the halt decision judges configuration/adapter state only; PHC windows stay recorded as
  evidence labelled "not judged", never as restored.  Supersedes, for PHC only, the 2026-10-21 rule that unobservable
  PHC state is INCONCLUSIVE_RESTORATION.

### Cause 2 - runner PHC observation window (implemented, hardware GREEN pending)
- Change: tools/test/Run-Tests-CI.ps1 Get-AvbHilClockState `-PhcReadIntervalMs` (default 250) sleeps between paired
  reads; 5 reads -> ~1 s window.  Used unchanged by the runner pre/post check, New-AvbHilBaselineEvidence.ps1 and
  Invoke-AvbTxTimestampProbe.ps1.  Sandbox observer unchanged.  Reads stay on the opened adapter (per-handle FsContext).
- RED: existing hardware evidence (windows 0.45-1.4 ms, see facts above).
- Offline regression (does not exercise the hardware read loop): HILGATE 83/83, evaluator 14/14, baseline evidence
  11/11, safety record 15/15, HILPROV 52/52.
- Hardware GREEN (Q, 2026-10-27 18:41, logs/hil-baseline/hil-baseline-evidence-20261027_184141.json, BASELINE_VERIFIED,
  no findings; installed driver F6499699... = Release build of f10d6e0; record UNBASELINED after Q's Initialize):
  all 9 windows span 1.00-1.05 s.  Undisturbed pass-to-pass uncertainty I226 offset 0.10-0.51 ms / rate 17-54k ppb,
  I210 offset 0.06-0.16 ms / rate 12-16k ppb (before: 10-116 ms / 1.4e6-2.2e7 ppb); I219 offset 32-35 us.  Every
  undisturbed change lies within or near its uncertainty.  Residual sigma rose to 1.4-22 us (spaced reads).

### Conclusion
- The missing record is an operator prerequisite (fail-closed by design), not a runner defect.
- The predicted halt after test_minimal_ioctl is not an observability false positive: per source the entry makes a
  real, unrestored PHC/timestamp-control mutation on whichever adapter is the driver default.  Better observability
  (PHC tolerance, TS_CONTROL/ETQF re-read) would turn it into CLEANUP_FAILED, not into verified restoration.

### Backlog (recorded, NOT started - WIP 1)
- B12-1 test_minimal_ioctl passes vacuously: cfg.status 0 ("Unknown") is never a failure; exit 0 always.
- B12-2 INIT_DEVICE lacks the PTP_READY idempotency guard of AvbEnsureDeviceReady (re-seeds a running I226 PHC):
  driver behaviour, needs an operator decision (driver change out of current P0 scope).
- B12-3 Driver enumeration order differs between driver loads (0x125B first on 2026-10-22, 0x15BB first on 2026-10-27).
- B12-4 No producer of `avb-hil-recovery-verification/1`; no `phcTolerance` producer: a HALTED record can only be
  rebaselined, never cleared by verified recovery.
- B12-5 I219 (0x15BB) PHC advanced 4640 ns in ~11 s across three undisturbed passes (2026-10-22 21:42 evidence):
  functional clock defect (the known slow-I219 observation), outside restoration scope.  Confirmed 2026-10-27 18:41
  with 1 s windows: rate 1-2e-6 s/s (about 2 us per 9 s).
- B12-6 I226 (0x125B) PHC runs 2.93 % fast against QPC (rate 1.0293 in both 2026-10-22 and 2026-10-27 captures):
  functional, outside restoration scope.