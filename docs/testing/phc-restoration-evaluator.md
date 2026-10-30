# PHC/QPC restoration evaluator — design

Status: **offline design + pure implementation** (`tools/lib/PhcRestoration.ps1`, tests
`tests/unit/ci/test_phc_restoration_evaluator.ps1`). Characterized with **synthetic inputs only** — no hardware
evidence exists yet, no tolerance is defined, and nothing here verifies any adapter. Governing issue: #343
(post-test adapter/PHC state integrity).

## Purpose

Decide, from independent before/after measurements, whether a state-changing test restored the PHC. It separates
two questions that are easy to conflate:

| Question | Inputs | Rule |
|---|---|---|
| Configuration restored? | TIMINCA, TSAUXC, timestamp-control state read back | exact equality; any difference is `CLEANUP_FAILED` (no tolerance involved) |
| Running clock restored? | paired PHC/QPC reads in a window before and after | fitted relationship compared against a **hardware-derived** tolerance |

Writing back the numeric PHC value does not restore a continuously running clock. UT-CORR-005 (source-inspected)
restores `phc_before + last 100 ms + 50 ms`, ignoring the time the test actually took; the evaluator measures the
resulting offset instead of trusting the write.

## Measurement model

* QPC is monotonic, not wall-clock time. Each PHC read is bracketed by QPC reads; the sample time is the bracket
  midpoint, the half-bracket width is the read-latency bound.
* Per window, least squares `PHC = a + b·t` (t in QPC seconds) → rate `b`, residual σ (sampling noise), slope
  standard error.
* The **before** fit is extrapolated to the mean time of the **after** window:
  `offset = mean(PHC_after) − (a_b + b_b·t*)`, `rate change = (b_a / b_b − 1)·10⁹ ppb`.
* Uncertainty U (coverage factor 2): fit standard errors (prediction error of the extrapolated before fit, mean
  error of the after window) plus the mean half-bracket latency added **linearly as a bound**. This is
  conservative: if hardware shows the IOCTL latency bias is stable between windows, it cancels and the bound can be
  replaced by its measured variability — that needs characterization data, not an assumption.
* Numerics: PHC values (~1.7·10¹⁸ ns) exceed double precision (2⁵³), so all values are centred with 64-bit
  integer differences first (TEST-PHCEVAL-012).

## Verdicts

| Condition | Verdict |
|---|---|
| capability absent **with evidence** | `SKIP_UNSUPPORTED` (absent without evidence → `INCONCLUSIVE_RESTORATION`) |
| before window invalid (missing provenance, < 3 reads, QPC not increasing, PHC backwards) | `FAILED_SETUP` |
| after window invalid, missing, or from another adapter identity | `INCONCLUSIVE_RESTORATION` |
| configuration differs, or PHC backwards inside the after window | `CLEANUP_FAILED` |
| no tolerance, or tolerance not `hardware-characterization`, other DID, no evidence reference | `INCONCLUSIVE_RESTORATION` |
| \|offset\| − U > tol or \|rate\| − U > tol | `CLEANUP_FAILED` |
| \|offset\| + U ≤ tol and \|rate\| + U ≤ tol | `RESTORATION_VERIFIED` |
| otherwise (uncertainty straddles tolerance) | `INCONCLUSIVE_RESTORATION` |

Every result keeps the adapter (DID + identity), testcase and metrics. Measurements whose provenance source is not
`hardware` are labelled "not hardware evidence" (`HardwareEvidence = $false`), and a tolerance whose evidence is
synthetic is rejected for hardware measurements.

## Preserved findings (investigation inputs, not root causes)

| Finding | Status |
|---|---|
| I226 TIMINCA `0x18000000 → 0x08000000` after `test_ptp_freq_complete` (2026-10-20) | recorded; TEST-PHCEVAL-002 keeps it a `CLEANUP_FAILED` configuration case; root cause unknown (a driver clamp was ruled out by source: I226 limit 47 ns) |
| UT-CORR-005 restore ignores elapsed time (+50 ms margin) | source-inspected; expected `CLEANUP_FAILED` once measured |
| UT-CORR-006 restore not read back on I219/I226 | source-inspected |
| I219 PHC advancing abnormally slowly | observed 2026-10-21 11:46 run; the evaluator judges restoration, not clock function — a slow clock is never accepted because it stayed slow |
| TX/RX provenance, PTP event sequence, ATDECC timeout | unresolved, outside this evaluator |

## Open measurement requirements (before any tolerance exists)

Per adapter (I210, I219, I226), from the read-only characterization:

1. read-bracket latency distribution (QPC ticks per GET_CLOCK_CONFIG/GET_TIMESTAMP) and whether its bias is stable;
2. residual σ of PHC vs QPC over windows of the planned length;
3. natural rate (ppb vs QPC) and its drift across windows minutes apart, without any mutation;
4. repeatability of the extrapolated offset between two undisturbed windows — this is what bounds an achievable
   offset tolerance; the tolerance then needs a hardware-requirement justification (gPTP / test needs), not only the
   measured noise.

Not implemented: the sampling tool that produces these windows from real hardware, and the integration of the
evaluator into the runner's post-test check (today it still records `INCONCLUSIVE_RESTORATION` for every
non-ReadOnly test).
