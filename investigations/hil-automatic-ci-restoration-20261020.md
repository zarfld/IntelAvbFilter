# HIL orchestration: CI → deploy-staging → hil.yml (2026-10-20, iteration 6: review corrections)

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
