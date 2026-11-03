# HIL suite migration matrix: HardwareMandatory / HardwareMutation -> HardwareUnit / HardwareIntegration (2026-11-03)

Operator decision (Q, 2026-11-03): only `HardwareUnit` and `HardwareIntegration` are authorized hardware suites.
`HardwareMandatory` and `HardwareMutation` are not approved suites, bundles, stages or CI jobs. This matrix is the
pre-modification evidence; nothing has been changed for the migration yet.

Sources (read at working tree of 2026-11-03, base `ffa5a98`): `tools/test/Run-Tests-CI.ps1` registry tables
(`$HardwareMandatoryTests` l.409, `$HardwareMutationTests` l.442, `$HardwareMutationAttendedOnly` l.451,
`$HardwareMandatoryCoverageGaps` l.630, `$HardwareUnitTests`, `$HardwareIntegrationTests`, `$HilTestSafety` l.694-879),
`tools/build/Build-Tests.ps1` (sources), test sources (TC ids), and the per-test results documents of
`logs/FullSuite_20261103_063836.log` (270 `logs/RunTests_<Config>_*/results-*.json`; LOCAL_DIAGNOSTIC evidence, not CI).
Membership compared with the runner's entry key (`x.exe args` -> `x args`).

## FACTS

### F1 Suite-scoped "mandatory" behaviour today (`$IsMandatorySuite = ($Suite -eq 'HardwareMandatory')`, l.857)

| Rule | HardwareMandatory | HardwareUnit / HardwareIntegration | Run-Tests-CI.ps1 |
|---|---|---|---|
| M1 driver lost mid-suite | remaining tests `FAILED(INFRA)` | `SKIPPED(INFRA)` | l.2631-2640 |
| M2 binary missing | `FAILED(BUILD)` | `SKIPPED` | l.2663-2672 |
| M3 exit 2 (SKIP_ONLY) | `FAILED(SKIP_ONLY)` | other path (not a pass) | l.2820-2826 |
| M4 sentinel failure text | "mandatory suite aborting" | "remaining tests will be skipped" | l.2875-2879 |

Verification class: every suite is already mandatory verification (`Get-AvbSuiteVerificationClass`, l.859-861:
SKIP_NOT_IMPLEMENTED = INCOMPLETE, INCONCLUSIVE = nonpassing) - independent of suite name.

`HardwareMutation` behaviour: attended-only entries are a declared coverage gap under GitHub Actions and appended to
the run outside it (l.2569-2573). `$HardwareMutationTests` is empty.

### F2 Matrix

Safety class from `$HilTestSafety`. Observed = 06:38 run, Debug / Release (records' suite as dispatched by Run-Tests.ps1).

| # | Entry (args) | Origin | Already in | TC ids (source / observed) | Proposed destination | Safety | Mandatory rules | Observed D / R |
|---|---|---|---|---|---|---|---|---|
| 1 | `test_ioctl_version` | M list | HU | UT-VERSION-001..003 | HU (existing entry) | ReadOnly | M1-M3 per entry | PASSED / PASSED |
| 2 | `test_ioctl_routing` | M list | HU | none in source | HU (existing) | ReadOnly | M1-M3 | PASSED / PASSED |
| 3 | `test_ioctl_simple` | M list | HU | none in source | HU (existing) | ReadOnly | M1-M3 | PASSED / PASSED |
| 4 | `test_ioctl_abi` | M list | HU (**listed twice**) | TC-ABI-001..023 | HU (existing, once) | ReadOnly | M1-M3 | PASSED / PASSED |
| 5 | `test_tx_timestamp_provenance` | M list | HU (**listed twice**) | TEST-PROV-H01..H07, L01.. (observed) | HU (existing, once) | ReadOnly | M1-M3 (M3 written for its Section H) | PASSED / PASSED |
| 6 | `test_tx_timestamp_retrieval` | M list | HU | TC-CORR-005, TC-CORR-TX-RX-001, TC-TS-CORR-001 | HU (existing) | ReadOnly | M1-M3 | FAILED / FAILED |
| 7 | `test_ioctl_phc_query` | M list | HU | UT-/IT-/VV-PHC-QUERY-* | HU (existing) | ReadOnly | M1-M3 | PASSED / PASSED |
| 8 | `test_ioctl_xstamp` | M list | HU | TC-XSTAMP-001..005 | HU (existing) | ReadOnly | M1-M3 | PASSED / PASSED |
| 9 | `test_ioctl_phc_crosstimestamp_v2` | M list | HU | TC-XSTAMP-V2-HIL-001..009 | HU (existing) | ReadOnly | M1-M3 | PASSED / PASSED |
| 10 | `test_all_adapters` | M list | HI | none in source | HI (existing) | ReadOnly | M1-M3 | PASSED / PASSED |
| 11 | `test_multidev_adapter_enum` | M list | HI | TC-DEV-I225-001 | HI (existing) | ReadOnly | M1-M3 | PASSED / PASSED |
| 12 | `test_multi_adapter_phc_sync` | M list | HU | TC-MADAPT-001..005 | HU (existing) | ReadOnly | M1-M3 | PASSED / PASSED |
| 13 | `test_send_ptp_debug` | M list | HU | none in source | HU (existing) | ReadOnly | M1-M3 | PASSED / PASSED |
| 14 | `test_minimal_ioctl` | M gap (Init exclusion) | HU | none in source | HU - see decision D3 | Init | - | PASSED / PASSED (ran in HU) |
| 15 | `ptp_clock_control_test` | M gap (Mutating) | HU | none in source | HU - see D3 | Mutating | - | BLOCKED(SAFETY) / same |
| 16 | `test_rx_timestamp` | M gap (Mutating) | HU | UT-RX-TS-001..016 | HU - see D3 | Mutating | - | BLOCKED(SAFETY) / same |
| 17 | `test_rx_timestamp_complete` | M gap (Mutating) | HU | TC-RX-TS-001..015 | HU - see D3 | Mutating | - | BLOCKED(SAFETY) / same |
| 18 | `test_ptp_phc_stability --case UT-CORR-005` | X attended-only | - (bare entry in HI) | UT-CORR-005 | HI attended-only (same CI-gap / local-append behaviour) | Mutating:Target | - | not run (attended only) |
| 19 | `test_ptp_phc_stability --case UT-CORR-006` | X attended-only | - (bare entry in HI) | UT-CORR-006 | HI attended-only | Mutating:Target | - | not run (attended only) |

Bare `test_ptp_phc_stability` (HI, ReadOnly) observed UT-CORR-007, UT-CORR-008: FAILED / FAILED (functional, parked).

### F3 No loss

- Rows 1-17 are already dispatched by HU or HI with identical arguments; deleting the Mandatory list/gap table removes
  no execution. Rows 18-19 move with unchanged entry text, reason and attended-only semantics.
- `$HardwareMutationTests` is empty: nothing to move.

### F4 Pre-existing defects found (not caused by the migration)

- Duplicate dispatch in `$HardwareUnitTests`: `test_tx_timestamp_provenance` (l.489, l.563), `test_ioctl_abi` (l.508,
  l.515), `test_ptp_crosstimestamp` (l.513, l.520) - identical entries, each runs twice per HU run.
- `test_minimal_ioctl`: excluded from Mandatory as "Init: TEMPORARY POLICY EXCLUSION 2026-10-21" (PHC re-init, I226
  SYSTIM := 1, post-test check misses the discontinuity), yet listed in HU and executed there unattended.
- Rows 15-17 are listed in HU although Mutating and not target-aware: every HU run reports them BLOCKED(SAFETY)
  (nonpassing) instead of a declared coverage gap.

## Decisions (Q, 2026-11-03) and implementation

- D1 No mandatory subset: M1-M3 apply to every entry of both hardware suites (`$IsMandatorySuite` = HardwareUnit or
  HardwareIntegration). TEST-RESULTSJSON-015/028, TEST-HILGATE-036, TEST-SENTINEL-005.
- D2 Identical entries deduplicated (also `test_ptp_001` x2 in HardwareIntegration, found by the full inventory);
  TEST-LOCALSUITES-029 rejects duplicate dispatch and checks all 19 rows above.
- D3 Rows 14-17 stay plain HardwareUnit entries; the Mandatory gap table is removed (no exclusion). Rows 15-17 remain
  BLOCKED(SAFETY) by the unchanged safety gate until their targeting and restoration are repaired (later repair phase).
- Rows 18-19 moved to `$HardwareIntegrationAttendedOnly` (CI: failing NOT EXECUTED blocker; local: executed).
- FullSuite.ps1: the operator's per-program `*.exe` loop is authoritative and restored verbatim, blacklist kept for
  `test_s3_sleep_wake` / `test_vv_corr_001_stability` until their infrastructure exists.
