# Test verdict contract

Single contract for what a testcase verdict means, how it maps to a driver status,
and how it normalises to JUnit and to the CI gate. Implemented by
`tools/lib/TestVerdict.ps1` (shared by `Parse-TestResults.ps1`,
`Convert-Logs-To-JUnit.ps1`, `Run-Tests-CI.ps1`) and, on the C side, by
`tests/common/avb_verdict.h`.

Principle: report what actually happened. A defect must never be relabelled as
missing hardware, and a missing implementation must never read as green.

| Condition | Driver NTSTATUS | Verdict | JUnit | Mandatory gate |
|---|---|---|---|---|
| Hardware genuinely lacks the capability | `STATUS_NOT_SUPPORTED` | `SKIP_UNSUPPORTED` | skipped, labelled | green |
| Hardware supports it, driver operation not implemented | `STATUS_NOT_IMPLEMENTED` (`0xC0000002`) | `SKIP_NOT_IMPLEMENTED` | skipped, labelled | **incomplete, nonpassing** |
| Implemented but wrong, or success with invalid payload / provenance | actual error status, or success | `FAIL` | failure | red |
| Unsafe or unmet prerequisite (incl. missing safe restore path) | e.g. `STATUS_DEVICE_NOT_READY` | `BLOCKED` | failure | red |
| Insufficient evidence (e.g. capability not determinable) | any | `INCONCLUSIVE` | failure (mandatory) / skipped, labelled (optional) | red / not verified |
| Verified | success + valid | `PASS` | passed | green |

`SKIP` (no suffix) remains a runtime "not applicable" (link down, requires
restart). It is not a capability claim and never replaces the verdicts above.
`CLEANUP_FAILED` and `INFRA_FAILURE` are failures.

## Rules

* `STATUS_NOT_SUPPORTED` is evidence of absent hardware capability only when the
  adapter's capability for **that operation** is known absent. A generic nonzero
  capability mask is not evidence of support for a specific operation. If the
  per-operation capability is not defined by an authoritative contract the result
  is `INCONCLUSIVE`, not `SKIP_UNSUPPORTED`.
* `NOT_SUPPORTED` returned by an adapter that advertises the capability is a
  contract violation: `FAIL`.
* Mandatory verification = every suite except `HardwareMutation` (the explicit
  experimental lane). No separate lane or gate exists for the new verdicts.
* `SKIP_NOT_IMPLEMENTED` stays a JUnit `<skipped>` carrying an explicit
  implementation-gap message; the requirement stays visibly incomplete
  (`NOT_IMPL` column in the traceability matrix, non-zero suite exit).

## Evidence preserved in every non-pass line

Test output lines use `[VERDICT] TC-ID/adapter:N: ntstatus=0x... did=0x... op=... reason`.
The converters keep the full line, the exact verdict (`type=` on failures, label in
skip messages), the adapter identity and the original NTSTATUS. The configuration
(Debug/Release) and suite are recorded as `<property>` elements.

## METHOD_BUFFERED caveat

For `METHOD_BUFFERED` IOCTLs the I/O manager copies the output buffer back only for
non-error completions. When the driver completes with an error status, a status
field inside the output structure is **not** delivered, and `GetLastError()` is
lossy (`STATUS_NOT_IMPLEMENTED` and `STATUS_INVALID_DEVICE_REQUEST` both map to
Win32 1). A verdict that depends on the exact error status must capture the raw
NTSTATUS (see `tests/common/avb_verdict.h`).

## Traceability matrix columns

`FAIL` counts genuine failures only. `SKIP` counts everything that is neither a pass nor
a failure, and therefore includes `SKIP_NOT_IMPLEMENTED` and `INCONCLUSIVE`. `NOT_IMPL`
and `INCONCL` are exact-verdict subsets of `SKIP`, appended after the original three
columns, so existing consumers that read the first three counts keep working and cannot
mistake an incomplete or inconclusive requirement for a pure pass.
`scripts/requirement_test_outcomes.py` classifies each requirement: `failing`,
`incomplete` (NOT_IMPL or INCONCL present), `verified`, or `skip_only`.
