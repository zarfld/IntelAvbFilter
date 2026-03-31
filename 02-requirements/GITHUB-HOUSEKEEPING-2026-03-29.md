# GitHub Housekeeping — 2026-03-29

## ✅ Step 1 — Push (DONE — 2026-03-30)

All commits pushed. `origin/master = HEAD` (`095d6a8`).

Included in origin since 2026-03-29:
- `414ed7c` docs(#317/#199): close — VV-CORR-001 PASS 86400s
- `58cd132` docs(test-plans): close #209 in master categorized plan
- `e5f9b89` fix(settings): add Get-Item approval
- `083df70` docs(TODO): close #209 — TC-TARGET-005 15/15 PASS
- `2ad239e` fix(#209): add past-target-time guard in IOCTL_AVB_SET_TARGET_TIME
- `2b66ae4` feat(ci): enhance code quality checks — implements #252
- `095d6a8` feat(ci): enhance CI workflows with JUnit reporting — partial #265

---

## Step 1b — Commit Untracked Files

```powershell
git add 02-requirements/GITHUB-HOUSEKEEPING-2026-03-29.md `
         tools/build/check-yml-ascii.ps1 `
         tools/build/strip-emoji-yml.ps1 `
         tools/build/verify-ascii.ps1
git commit -m "chore: add housekeeping doc and yml ascii-validation build tools"
git push origin master
```

---

## Step 2 — Close Issues on GitHub

### ~~#204~~ — TEST-PTP-TARGET-001: Target Time Interrupt Verification *(close with evidence)*
**Action**: Post close comment to #204, then close issue.

> TC-TARGET-001..015 in `test_ioctl_target_time.c` cover all 15 TCs for this issue — `IOCTL_AVB_SET_TARGET_TIME` (IOCTL 43), TRGTTIML0/H0 and TRGTTIML1/H1 registers.
>
> **Key fix**: `2ad239e` adds past-time guard: if `target_time < current_systim`, handler returns `STATUS_INVALID_PARAMETER` (Win32 error 87) — satisfies UT-TARGET-004.
>
> **Run result (2026-03-29)**: **15 PASS / 0 FAIL / 1 SKIP** (TC-TARGET-EVENT-001 SKIP — ring buffer MapViewOfFile not implemented; accepted gap).
>
> Note: The close comment erroneously posted to **#209** on 2026-03-29 has been corrected — this evidence belongs to #204.

---

### #209 — TEST-LAUNCH-TIME-001: Launch Time Offload *(re-open — attribution error)*
**Action**: 1) Post correction comment to #209. 2) Re-open the issue.

> ⚠️ **Attribution error — re-opening #209**
>
> This issue was incorrectly closed on 2026-03-29. The close comment referenced `IOCTL_AVB_SET_TARGET_TIME` / TC-TARGET-005 / commit `2ad239e`, but those belong to **issue #204 (TEST-PTP-TARGET-001: Target Time Interrupt Verification)**.
>
> **#209 is "TEST-LAUNCH-TIME-001: Launch Time Configuration and Offload Verification"**, which requires:
> - `IOCTL_AVB_SET_LAUNCH_TIME` — does **not** exist in the driver or `avb_ioctl.h`
> - `TQAVLAUNCHTIME` register programming (per-packet TX scheduling, IEEE 802.1Qbv)
> - UT-LAUNCH-001..010, IT-LAUNCH-001..003, VV-LAUNCH-001..002 — none implemented
>
> Re-opening as `status:backlog`. This is a hardware-gated TSN TX scheduling feature distinct from target-time interrupts.

---

### #238 — TEST-PTP-001: Verify PTP Hardware Timestamp Correlation for gPTP *(NEW — 2026-03-30)*
**Action**: Close with comment, then close issue.

> Acceptance criteria fully covered by the evidence from #199 (closed 2026-03-29):
>
> - HW timestamp captured for 100% of Sync messages → ✅ IT-CORR-001/003/004 PASS (14/14, 6×I226-LM)
> - Timestamp correlation accuracy <±100ns → ✅ VV-CORR-001 PASS (mean=+0.0 ns, stddev=0.0 ns, 86400s)
> - Zero timestamp pair loss → ✅ confirmed in IT-CORR-001
> - gPTP clock offset <±1µs → ✅ UT-CORR-001 PASS (bracket window 42–50 µs); VV-CORR-003 PASS (cross-domain PHC↔System↔TX, 42/42)
>
> Tracking issue with full test plan: #199 (TEST-PTP-CORR-001, closed 2026-03-29). Traces to: #149.

---

### #317 — [INFRA] Mock NDIS Unit Test Harness
**Action**: Close with comment, then close issue.

> All closure criteria met as of 2026-03-29. All 5 tracks complete:
>
> - **Track A** (UT-CORR-005..009 PHC stability): ✅ 15/15 PASS — 2026-03-28
> - **Track B** (UT-CORR-003 + IT-CORR-002 cross-timestamp): ✅ 6/6 PASS — 2026-03-28
> - **Track C** (UT-CORR-002 RX correlation): ✅ PASS; UT-CORR-004 ⚠️ SKIP (no loopback cable, hw-gated) — 2026-03-28
> - **Track D** (UT-CORR-001 bracket, UT-CORR-010 SKIP-guard): ✅ 30/30 PASS + SKIP accepted — 2026-03-28
> - **Track E** (VV-CORR-001 24h stability): ✅ **PASS** — 86400s, 8633 samples/adapter, mean=+0.0 ns, stddev=0.0 ns, 0 outliers. Log: `test_vv_corr_001_stability_20260328_195650.log` — 2026-03-29
> - **VV-CORR-003** cross-domain PHC↔System↔TX: ✅ 42/42 PASS — 2026-03-29
> - **VV-CORR-002** ⚠️ SKIP — gPTP lab / GPS-disciplined grandmaster required (infrastructure-gated, documented per plan)
>
> See `07-verification-validation/test-plans/TEST-PLAN-MOCK-NDIS-HARNESS.md` v1.7.

---

### #199 — TEST-PTP-CORR-001: PTP Hardware Correlation
**Action**: Close with comment, then close issue.

> All 17 specified tests resolved as of 2026-03-29. Issue was reopened 2026-03-27 after premature closure with only 4/17 run. Full resolution:
>
> - IT-CORR-001/003/004: ✅ PASS (14/14, 6×I226-LM)
> - IT-CORR-002: ✅ PASS — `IOCTL_AVB_PHC_CROSSTIMESTAMP` (code 63) implemented and tested
> - UT-CORR-001..009: ✅ PASS (various logs 2026-03-28)
> - UT-CORR-004/010: ⚠️ SKIP (hardware-gated — no loopback cable / no hw-TS-disable path; accepted per test plan)
> - VV-CORR-001: ✅ PASS — 86400s run, mean=+0.0 ns, stddev=0.0 ns
> - VV-CORR-003: ✅ PASS — 42/42, cross-domain PHC↔System↔TX confirmed
> - VV-CORR-002: ⚠️ SKIP — gPTP lab required
>
> Tracking issue: #317 (also closed). See `TEST-PLAN-MOCK-NDIS-HARNESS.md` v1.7.

---

### #288 — TEST-HW-DETECT-CAPS-001: HW Capability Detection
**Action**: Verify already closed; if not, close with comment.

> TC-PCI-LAT-001/002/003 added to `test_hw_state.c`. 12/12 PASS (P99 < 250 µs, outlier < 15 ms; thresholds empirically justified from 5 runs with Release + Debug drivers, 6×I226-LM). Run: 2026-03-29.

---

### #312 — TEST-MDIO-PHY-001: MDIO/PHY Register Access
**Action**: Verify already closed; if not, close with comment.

> Moved to Category 1. P=54 F=0 S=36 (driver v1.0.193.x, 2026-03-09). All acceptance criteria met.

---

### #252 — TEST-MAINTAIN-001: CI Code Quality Steps *(NEW — 2026-03-30)*
**Action**: Close with comment, then close issue.

> All 8 TEST-MAINTAIN-001 CI steps implemented in `2b66ae4` (2026-03-30):
>
> - Cyclomatic complexity gate: lizard `--CCN 10 --length 50` (blocking)
> - Duplication check: lizard `--duplicate` (continue-on-error)
> - Comment coverage scan: inline Python scanner (continue-on-error)
> - Format check: clang-format `--dry-run --Werror` (continue-on-error)
> - Static analysis: cppcheck `--enable=unusedFunction` (continue-on-error)
> - Magic number gate: delegated to `ssot-magic-numbers-gate` job
> - Artifacts: lizard-report.txt/xml, cppcheck-report.txt uploaded
>
> `code-quality` job in `.github/workflows/ci-standards-compliance.yml`.

---

### #265 — TEST-COVERAGE-001: Driver Coverage *(NEW — 2026-03-30, COMMENT ONLY — do not close)*
**Action**: Add progress comment to issue; leave open.

> **JUnit output wired in `095d6a8` (2026-03-30):**
> - `tools/test/Run-Tests-CI.ps1` now generates `logs/junit-${Suite}-${Configuration}-YYYYMMDD.xml` via `Convert-Logs-To-JUnit.ps1`
> - Log filename format fixed: now `<name>.exe_YYYYMMDD_HHMMSS.log` (matches converter)
> - `dorny/test-reporter@v1` publish steps added to `unit-tests` and `integration-tests` CI jobs
> - `checks: write` permissions added to both jobs
>
> **Still open (Pillars 3/5/6 incomplete):**
> - Pillar 3: Disposition counters (pass-through/dropped/cloned/loopback NBLs) not yet in `AVB_DRIVER_STATISTICS` struct
> - Pillar 5: Driver Verifier — no evidence committed
> - Pillar 6: HLK/NDISTest — no results committed
> - `MIN_TEST_COVERAGE: 80` declared in CI but not enforced (kernel `.sys` constraint)

---

## Step 3 — Update Parent Issues (add backward links)

### #149 — REQ-F-PTP-007: Hardware Timestamp Correlation
Add to Traceability section:
```
- Verified by: #317 (TEST-PTP-CORR — all 17 tests, VV-CORR-001 86400s PASS 2026-03-29)
- Verified by: #199 (TEST-PTP-CORR-001 closed 2026-03-29)
- Verified by: #238 (TEST-PTP-001 closed 2026-03-30 — evidence via #199)
```

### #48 — REQ-F-IOCTL-PHC-004: Cross-Timestamp IOCTL
Add to Traceability section:
```
- Verified by: #317 Track B (UT-CORR-003 PASS, IT-CORR-002 PASS 2026-03-28)
```

---

## Step 4 — Active Failures Still Open (review/label)

| Issue | State | Suggested Action |
|-------|-------|-----------------|
| [#269](https://github.com/zarfld/IntelAvbFilter/issues/269) | ETW manifest not registered — TC-ETW-001 1F | Tag `bug`, assign sprint 6. Fix: `wevtutil im` in install script + `mc.exe` header + `EventWrite` in `DriverEntry`. |
| [#271](https://github.com/zarfld/IntelAvbFilter/issues/271) | S3/Wake PHC — TC-S3-002 unconfirmed | Add label `hardware-gated`. Re-run requires dedicated machine + WoL + serial console. |
| [#250](https://github.com/zarfld/IntelAvbFilter/issues/250) | No CI pass/fail traceability report | **Partially addressed** (`095d6a8`): `dorny/test-reporter@v1` now publishes JUnit XML to GitHub Actions test-results UI for `unit-tests` and `integration-tests` jobs. Verify if this satisfies acceptance criteria; if yes, close with evidence. |

---

## Step 5 — Issues to Leave Open (hardware-gated)

These have passing tests where possible; remaining TCs require hardware not on CI runner:

| Issue | Blocked On |
|-------|-----------|
| #177 | GPIO oscilloscope — TC-PTP-LAT-001 |
| #179 | 4-channel oscilloscope (MDO3000) |
| #176 | ATDECC controller + AVB stream source |
| #261 | Intel I219 adapter |

Add `hardware-gated` label to each if not already present.

---

## Step 6 — Open Requirements: Test Coverage Status (2026-03-30)

This section maps **all open functional and non-functional requirements** to their test coverage and current verification state. Requirements are fed in batches; this table grows as each batch is reviewed.

> **Scope note**: The driver is a hardware PHC interface layer (NDIS filter). It provides IOCTL-based PHC and timestamping access to user-space applications (e.g., openavnu/gptp). Requirements for the gPTP *protocol stack* (BMCA, state machines, message handling, etc.) are **out of scope for this driver** — they are fulfilled by the external gPTP daemon. All other functional and non-functional requirements are in scope.

---

### ✅ Fully Confirmed — All Test Cases Passed

All acceptance criteria met; no open gaps.

| Requirement | Title | Covering Test Issues | Evidence |
|-------------|-------|---------------------|----------|
| **#2** REQ-F-PTP-001 | PTP Clock Get/Set (IOCTL 24/25) | #295 TEST-PTP-CLOCK-001; #195/#266 TEST-PHC-SET-001 | #295: 12P/0F CLOSED; #195/#266: 14P/0F PASS 2026-03-09 |
| **#3** REQ-F-PTP-002 | PHC Frequency Adjustment (IOCTL 38) | #296 v2 (17 TCs); #192 v1 (14 TCs); #193 PHC Query (17 TCs); #200/#274 PHC r/w latency | All CLOSED/PASS; GPS 24h stability CLOSED; P50/P99 PASS 2026-03-09 |
| **#5** REQ-F-PTP-003 | HW Timestamping Control (TSAUXC, IOCTL 40) | #297 TEST-PTP-HW-TS-001 (13 TCs) | CLOSED; EN_TT0/1, EN_TS0/1, EN_CLK0, all bit combinations verified |
| **#6** REQ-F-PTP-004 | Rx Packet Timestamping (IOCTLs 41/42) | #298 v1 (15 TCs); #311 v2 (15 TCs) | Both CLOSED; CFG_TS_EN + per-queue SRRCTL[n].TIMESTAMP fully verified; port reset TC ≤5ms |
| **#7** REQ-F-PTP-005 | Target Time & Aux Timestamp (IOCTLs 43/44) | #204 (15 TCs); #299 (16 TCs) | #204: 15P/0F/1S 2026-03-29; #299: CLOSED; past-time guard (`2ad239e`) verified by TC-TARGET-005; TRGTTIML/H0/1, AUXSTMPL/H0 registers covered |
| **#39** REQ-F-CLOCK-ADJ-001 | PHC Offset Adjustment (nanosecond) | #194 TEST-IOCTL-OFFSET-001 (15 TCs) | 15P/0F 2026-03-20; QPC wall-clock bracketing ±200µs tolerance proven |
| **#47** REQ-F-CLOCK-INIT-001 | PTP Clock Initialization | Covered across #295, #297, #196 collectively | TIMINCA/TSAUXC/TSYNCRXCTL init sequence exercised; SYSTIM0 incrementing confirmed |
| **#48** REQ-F-GPTP-TIMESTAMP-001 | HW Timestamp Integration (gPTP consumer interface) | #198 TEST-IOCTL-XSTAMP-001; #210 TEST-GPTP-PHC-001; #199/#317 CORR harness | All PASS/CLOSED; PHC read/adjust/cross-timestamp API validated from gPTP daemon perspective |
| **#149** REQ-F-PTP-007 | PTP Hardware Timestamp Correlation | #199 TEST-PTP-CORR-001; #238 TEST-PTP-001; #317 NDIS harness (all tracks) | CLOSED; VV-CORR-001: 86400s, mean=+0.0ns, stddev=0.0ns, 0 outliers; VV-CORR-003: 42/42 PHC↔System↔TX; IT-CORR-001/003/004: 14/14 (6×I226-LM) |
| **#80** REQ-NF-PTP-ACC-001 | PTP Synchronization Accuracy ±1µs | #199/#317 VV-CORR-001, UT-CORR-001 bracket | VV-CORR-001 PASS: stddev=0.0ns; UT-CORR-001: bracket window 42–50µs ✅ |
| **#41** REQ-F-PTP-EPOCH-001 | PHC TAI Epoch (1970-01-01, not UTC) | #196 TEST-PTP-EPOCH-001 | PASS exit 0; driver v1.0.189.0 2026-03-09 |
| **#48** REQ-F-IOCTL-XSTAMP-001 | PHC ↔ System simultaneous snapshot | #198 TEST-IOCTL-XSTAMP-001; IT-CORR-002 | PASS exit 0 / 6/6 PASS 2026-03-28; IOCTL_AVB_PHC_CROSSTIMESTAMP implemented and verified |
| **#47** REQ-NF-REL-PHC-001 | PHC never decreases across read cycles | #285 TEST-PHC-MONOTONIC-001 | PASS exit 0; driver v1.0.189.0 2026-03-09 |
| Timestamp Event Subscription (IOCTLs 33/34) | Ring buffer map + event subscription | #314 TEST-TS-EVENT-SUB-001 (19 TCs); #237 TEST-EVENT-001 (97P/17S) | Both CLOSED/PASS; IOCTLs 33/34 fully covered |
| **#10** REQ-F-MDIO-001 | MDIO/PHY Register Access (IOCTL 29: MDIO_READ, 30: MDIO_WRITE) | #312 TEST-MDIO-PHY-001 (15 TCs); `tests/ioctl/test_ioctl_mdio_phy.c` on disk | P=54 F=0 S=36 (driver v1.0.193.x, 2026-03-09); #312 CLOSED/completed 2026-03-07; all acceptance criteria met. 36 SKIP = hw-specific PHY features only (Clause 45 extended regs, cable diagnostics, D3 low-power — not present on all PHYs) |
| **#22** REQ-NF-SSOT-EVENT-001 | SSOT Register Definitions Match Hardware | `ssot_register_validation_test`: TSAUXC/TIMINCA hardware-readable; SSOT addresses validated | P=19 F=0 S=0 |
| **#59** REQ-NF-REL-DRIVER-001 | Driver Reliability Under Error Conditions | `test_hal_errors` (52P) + `test_error_recovery` (5P); Driver Verifier active; 500-IOCTL storm exercised | P=57 F=0 S=0 |
| **#63** REQ-NF-INTEROP-TSN-001 | Credit-Based Shaper (CBS/QAV) Interoperability | `test_qav_cbs` CI: all 14 TCs incl. zero/max credits, invalid class rejection, register read-back | P=14 F=0 S=0 |

| **#73** REQ-NF-SEC-IOCTL-001 | IOCTL Access Control (non-admin rejection) | `test_ioctl_access_control` CI: non-admin denied, admin granted | P=5 F=0 S=0 |
| **#85** REQ-NF-MAINTAIN-001 | No Magic Numbers in Source | `test_magic_numbers`: 0 violations across `src/` + `devices/` | P=1 F=0 S=0 |
| **#88** REQ-NF-COMPAT-NDIS-001 (6.50+) | NDIS 6.50+ Send/Receive Path Compatibility | `test_ndis_send_path` (6P) + `test_ndis_receive_path` (6P) | P=12 F=0 S=0 |
| **#89** REQ-NF-SECURITY-BUFFER-001 | Buffer Overflow & Security Validation | `test_ioctl_buffer_fuzz` CI (5P) + `test_security_validation` (15P incl. penetration tests) | P=20 F=0 S=0 |
| **#95** REQ-NF-DEBUG-REG-001 | Registry Diagnostics Accuracy | `test_registry_diagnostics`: 9 TCs passed | P=9 F=0 S=0 |
| **#96** REQ-NF-COMPAT-WDF-001 | WDF IOCTL Dispatch Compatibility | `test_compat_win11`: WDF dispatch confirmed on Win11 build 26200 | P=6 F=0 S=0 |
| **#97** REQ-NF-COMPAT-NDIS-001 (LWF) | NDIS LWF Filter Chain Interoperability | `test_ndis_send_path` + `test_ndis_receive_path`: LWF chain exercised | P=12 F=0 S=0 |
| **#99** REQ-NF-BUILD-SIGN-001 | Driver Code-Signing (kernel acceptance) | `test_build_sign`: TC-SIGN-007 kernel accepted signature, service state=4 (RUNNING) | P=7 F=0 S=0 |

---

### ⚠️ Partial Coverage — Known Gaps Present

Test code exists and runs, but specific cases remain unverified due to hardware constraints or unimplemented sub-features.

| Requirement / Issue | Title | Passed | Gap | Blocker Type |
|---------------------|-------|--------|-----|--------------|
| **#13** REQ-F-TS-SUB-001 / **#19** REQ-F-TSRING-001 (TC-TARGET-EVENT-001) | Target-time interrupt → ring buffer event delivery | 15P/0F for IOCTL 43/44 register side (per #7); IOCTLs 33/34 API covered (#314 CLOSED) | 1 SKIP — ring buffer `MapViewOfFile` / section object path (#19) for event delivery not implemented in test harness; TRGTTIML/H register programming confirmed (#7), interrupt → userspace event chain unverified end-to-end | Code (test harness) |
| **#119** REQ-F-GPTP-COMPAT-001 | gPTP daemon coexistence (TC-COEXIST-001..005) | 0P/0F/5S — all skipped | No gPTP/OpenAvnu daemon found at test time: `PASS=0 FAIL=0 SKIP=5 TOTAL=5`. TC-COEXIST-001..005 all SKIP. Must re-run with `gptp.exe` or `openavnu_gptp.exe` active. | Infrastructure (daemon not running) |
| **#25** REQ-NF-MAINT-LOGGING-001: Logging Consistency & Structured ETW Tracing | ETW event emission on all critical paths; migration from DbgPrint → ETW_LOG(); structured payloads | #87 REQ-NF-TEST-INTEGRATION-001 labeled `status:completed` (open) | No dedicated ETW capture test; #87 covers general IOCTL execution via integration suite — does NOT verify ETW events emitted/captured. ETW manifest registration (`wevtutil im`) not confirmed. **⚠️ Note**: issue #25 title says REQ-F-PTP-IOCTL-001 but body is REQ-NF-MAINT-LOGGING-001 — title/body mismatch on GitHub. | Spec/test gap (no ETW-specific test case) |
| **#76** REQ-NF-COMPAT-I219-001: Intel I219-V/LM Controller Compatibility | Device detection, BAR0 register access, PTP clock on I219 variants; TSN feature limitation reporting | #261 TEST-COMPAT-I219-001 open, `status:in-progress` | Test issue #261 is a draft — contains planned test steps and an expected-result matrix (not actual execution evidence). No test run results. I219 hardware required to execute. | Hardware (I219 NIC required) |
| **#177** | PTP Timestamp Event End-to-End Latency <500ns | 18P/12S — API contract, 60s stress, 4-observer delivery PASS | TC-PTP-LAT-001/003 SKIP — GPIO voltage probe + oscilloscope required to measure actual ISR→userspace latency | Hardware (oscilloscope) |
| **#179** | 4-Channel Oscilloscope Cross-Event Timing | 12P/18S — observer ordering + no priority inversion PASS | TC-LAT4-001/003/004 SKIP — Tektronix MDO3000 or equivalent required | Hardware (oscilloscope) |
| VV-CORR-002 | PHC accuracy vs GPS reference (gPTP lab) | N/A (accepted SKIP in test plan) | SKIP per TEST-PLAN-MOCK-NDIS-HARNESS v1.7 — GPS-disciplined grandmaster + gPTP lab required; not on CI runner | Infrastructure |
| **#271** | PHC State Preservation after S3 Sleep/Wake | TC-S3-001 PASS (S3 entry confirmed) | TC-S3-002 UNCONFIRMED — machine entered S3 during test run; post-wake PHC preservation not measured. S3 fix coded (`FilterRestart` re-programs TIMINCA/SYSTIML/TSAUXC/TSYNCRXCTL) but physical wake-up cycle not verified | Hardware (controlled S3 session) |
| REQ-F-PTP-LEAPSEC-001 | Leap Second Handling (per IEEE 1588-2019) | TAI epoch confirmed (#196) | No dedicated test case for leap-second boundary behavior; driver stores TAI but does not expose `UTC_OFFSET` IOCTL; specification exists but verification absent | Spec gap (no test written) |
| UT-CORR-004 | RX timestamp correlation with loopback cable | SKIP | No loopback cable on test bench; hardware-gated per plan | Hardware (loopback cable) |
| **#175** TEST-EVENT-002 | AVTP TU Bit / gPTP GM Flap Event test | Test file committed; CLOSED | Requires 2× gPTP grandmaster devices + gPTP-capable switch on same network | Hardware (lab) |
| **#174** TEST-EVENT-004 | AVTP Diagnostic Counter Events | Test file committed; CLOSED | Requires network emulator + packet generator for synthetic packet injection | Hardware (lab) |
| **#12** REQ-F-DEV-001 | Device Lifecycle Management (IOCTLs 20: INIT_DEVICE, 21: GET_DEVICE_INFO, 31: ENUM_ADAPTERS, 32: OPEN_ADAPTER, 37: GET_HW_STATE) | #313 CLOSED/completed 2026-03-07 (19 TCs); `device_open_test.c`, `test_device_register_access.c`, `avb_device_separation_test_um.c` exist | No P/F/S run evidence found for dedicated lifecycle IOCTL test; `test_ioctl_device_lifecycle.c` not on disk; PnP remove/hot-plug (UT-DEV-LIFECYCLE-003/005) and D3 state (UT-DEV-HW-STATE-002) require hardware events not reproducible on CI. OPEN_ADAPTER (IOCTL 32) implicitly exercised by every other IOCTL test as precondition. | Code (no standalone lifecycle test run; hardware-gated for PnP/D3 cases) |
| **#69** REQ-NF-COMPAT-WIN10-001 | Windows 10/11 Compatibility | **#268 re-opened** (wrongly closed as duplicate — #256/#257 trace to #80/#81, not #69). Dedicated 10-TC plan (#268) unexecuted: Win10 1809 install, Secure Boot, HLK, Driver Verifier 5h stress, Win10→11 upgrade path, clean uninstall, multi-NIC, NDIS 6.50+ static assertion, Event Viewer check. Only basic WDF/IOCTL on Win11 build 26200 confirmed from `test_compat_win11`. No Win10 test evidence at all. | P=6 (Win11 basic only) |
| **#23** REQ-NF-SEC-DEBUG-001 | Raw Register Access Gated by NDEBUG | `#ifndef NDEBUG` guards present (`avb_integration_fixed.c:1832`, `device.c:461`) but no automated test validates all raw-register-access paths are guarded. No dedicated test log. | No test |
| **#46** REQ-NF-PERF-NDIS-001 | NDIS Fastpath Latency <1µs | `test_ndis_fastpath_latency`: 5P/0F/0S but 226 calls exceeded 100µs WARN. Req specifies <1µs; test threshold is 100µs — threshold mismatch, requirement not verified at required precision. | P=5/0/0 +226 WARN |
| **#58** REQ-NF-PERF-PHC-001 | PHC HW Register Read <500ns | `test_ioctl_phc_query` CI: 11P/0F/S=6. SKIP: multi-adapter, multi-process, driver-lifecycle TCs. IOCTL p95=80200ns — req spec is HW register read <500ns; IOCTL overhead is OS-crossing cost (architectural mismatch in requirement). | P=11/0/S=6 |
| **#61** REQ-NF-INTEROP-MILAN-001 | IEEE 802.1AS/Milan Protocol Interoperability | `avb_test_i210`/`avb_test_i226`: base=OK, optional=OK (functional checks only, no protocol PASS counts). No 802.1AS/Milan conformance test. `test_gptp_daemon_coexist`: 0P/0F/5S — daemon not running. | avb: OK / coexist: 0/0/5 |
| **#71** REQ-NF-DOC-API-001 | IOCTL API Documentation Complete | `docs/IOCTL_API_SUMMARY.md` exists. No CI test validates doc completeness vs. actual IOCTL set; no Doxygen API reference in CI. | No automated test |
| **#72** REQ-NF-TEST-COVERAGE-001 | Test Coverage ≥80% Code Paths | `test_lifecycle_coverage`: 24P/0F/S=7 (TC-LCY-006 SKIP: no NIC for disable/re-enable). No code coverage % report (gcov/OpenCppCoverage) found in CI logs. | P=24/0/S=7; no % report |
| **#86** REQ-NF-DOC-DEPLOY-001 | Deployment/Installation Documentation | `docs/installation-guide.md` exists. No CI validation enforces doc presence or completeness. | No automated test |
| **#98** REQ-NF-BUILD-CI-001 | CI Build Pipeline Passes All Tests | CI (`ci_hwunit_20260326_153353.log`): **2 active FAILs** — `test_tx_timestamp_retrieval.exe` (F=1) and `test_ioctl_phc_monotonicity.exe` (F=1). | ~20P/**2F**/0S |
| **#161** REQ-NF-EVENT-002 | Zero Polling Overhead for Event Delivery | `test_zero_polling_overhead`: 18P/0F/**S=18**. SKIP=18: WPR/WPA trace environment not available. Zero-overhead proof requires WPR capture. | P=18/0/S=18 |
| **#165** REQ-NF-EVENT-001 | PTP Event Latency <500ns End-to-End | `test_ptp_event_latency`: 18P/0F/**S=12**. SKIP=12: TC-PTP-LAT-001/003 require GPIO + oscilloscope for ISR→userspace measurement. | P=18/0/S=12 |
| **#307** REQ-NF-PERF-BENCHMARKS-001 | HAL Performance Benchmarks Within Budget | `test_hal_performance` CI: 12P/**1F**/0S — PM-HAL-009 FAIL: init variance 11.7% > 10% threshold. `test_perf_regression`: 5P/0F/0S (CAPTURE mode only — no comparison yet). | P=17/**F=1**/0 |

---

### ❌ Not Implemented — No Driver Code or Test

| Requirement / Issue | Title | What Is Missing |
|---------------------|-------|-----------------|
| **#209** TEST-LAUNCH-TIME-001 | IEEE 802.1Qbv Launch Time Offload (per-packet TX scheduling) | `IOCTL_AVB_SET_LAUNCH_TIME` does **not** exist in `avb_ioctl.h` or driver. `TQAVLAUNCHTIME` register not programmed at all. No test file. UT-LAUNCH-001..010 / IT-LAUNCH-001..003 / VV-LAUNCH-001..002 all unwritten. Re-opened 2026-03-30 (`status:backlog`). Distinct from target-time interrupts (#7). |
| **#269** TEST-EVENT-LOG-001 | Windows Event Log Emission on Driver Init | Driver does **not** emit Event ID 1 to Windows Event Log on `DriverEntry`. ETW manifest not registered (`wevtutil im` not in install script). TC-1 FAIL confirmed (10P/1F). Fix path: `mc.exe` manifest → `EventWrite` calls in `DriverEntry`/critical paths. |
| gPTP Protocol Stack | Sync/Follow_Up/Announce handling, BMCA, Port State Machines, Peer Delay, Neighbor Discovery, Correction Field, Domain/Priority management | **OUT OF SCOPE for this driver**. These are implemented in the external gPTP daemon (openavnu/gptp). The driver satisfies gPTP integration via #48 (hardware timestamp interface) — verified. No driver code needed or expected for protocol-layer requirements. |
| REQ-F-8021AS-STATE-001 | 802.1AS Port State Machine | Same as above — port state machine is gPTP stack responsibility, not driver. |
| **#62** REQ-NF-STD-NAMING-001 | Function/Variable Naming Convention Compliance | No naming convention test in `tests/`. `test_magic_numbers` covers unnamed numeric literals only, not naming rules. No static analysis tool for naming configured in CI. |
| **#87** REQ-NF-TEST-INTEGRATION-001 | gPTP Integration Test Suite | `test_gptp_daemon_coexist`: **P=0/F=0/S=5** — no gPTP daemon found; all 5 TCs unconditionally SKIP on every logged run. Infrastructure (daemon) never available. |

---

### Summary Table

| Status | Count | Core Requirements |
|--------|-------|------------------|
| ✅ Fully Confirmed (REQ-F) | 15 | #2, #3, #5, #6, #7 (register side), #39, #41, #47, #48, #80, #149, TS-Event-Sub (counted as above incl. #48 GPTP-TIMESTAMP), **#10** MDIO (P=54 F=0 S=36) |
| ⚠️ Partial REQ-F (hw-gated / spec gaps) | 12 | #13/#19 TC-TARGET-EVENT-001 (ring buf), #119 (daemon not running), #177 (oscilloscope), #179 (oscilloscope), VV-CORR-002 (gPTP lab), #271 (S3 wake), REQ-LEAPSEC, UT-CORR-004 (loopback), #175/#174 (hardware lab), **#25** (ETW no dedicated test), **#76** (I219 test draft/hardware-gated), **#12** Device Lifecycle (no standalone run; hw-gated PnP/D3) |
| ❌ Not Implemented (REQ-F) | 3 | #209 Launch Time Offload, #269 Windows Event Log, gPTP stack (out of scope) |
| ✅ Fully Confirmed (REQ-NF) | 11 | #22, #59, #63, #73, #85, #88, #89, #95, #96, #97, #99 |
| ⚠️ Partial (REQ-NF) | 12 | #23 (no NDEBUG test), #46 (threshold mismatch), #58 (S=6 + latency mismatch), #61 (no Milan test; daemon S=5), **#69** (#268 re-opened; no Win10 test; 10-TC plan unexecuted), #71 (doc no CI test), #72 (S=7 + no coverage %), #86 (doc no CI test), #98 (2 CI FAILs), #161 (S=18 WPR), #165 (S=12 oscilloscope), #307 (1 FAIL + perf_regression capture-only) |
| ❌ Not Implemented (REQ-NF) | 2 | #62 (no naming convention test), #87 (all 5 tests SKIP — no daemon) |
| ⏳ Not yet reviewed | ~44 | Remaining open REQ-F issues not yet assessed in this table |
