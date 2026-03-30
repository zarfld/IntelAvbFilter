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
