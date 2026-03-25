---
specType: architecture
standard: 42010
phase: 03-architecture
version: 1.0.0
author: Architecture Team
date: "2026-03-25"
status: accepted
traceability:
  requirements:
    - REQ-NF-BUILD-CI-001
    - TEST-COVERAGE-001
---

# ADR-STATS-001: Extend AVB_DRIVER_STATISTICS for NDIS LWF Coverage Instrumentation

## Metadata
```yaml
adrId: ADR-STATS-001
status: accepted
relatedRequirements:
  - "#265"   # TEST-COVERAGE-001
  - "#98"    # REQ-NF-BUILD-CI-001
relatedComponents:
  - include/avb_ioctl.h (AVB_DRIVER_STATISTICS struct)
  - src/filter.c (callback counter increments)
  - IOCTL_AVB_GET_STATISTICS (0x9C40A020)
supersedes: []
supersededBy: null
author: zarfld
date: 2026-03-25
reviewers: []
```

## Context

For an NDIS lightweight filter (LWF), "X% line coverage" via user-mode instrumentation tools
(OpenCppCoverage, gcov) is meaningless: the `.sys` file runs in kernel mode; `DeviceIoControl`
is an instrumentation-invisible boundary. No user-mode tool can measure what runs inside the
filter.

The correct coverage model (see `07-verification-validation/test-plans/TEST-PLAN-NDIS-LWF-COVERAGE.md`)
requires **scenario coverage evidence**: after each test scenario, the test harness reads driver
counters and asserts that expected callbacks ran, that invariants (zero outstanding NBLs/OIDs)
hold at state boundaries, and that error paths were exercised.

The existing `AVB_DRIVER_STATISTICS` struct (returned by `IOCTL_AVB_GET_STATISTICS`) already
carries packet/byte counts and `FilterAttachCount`. It is missing lifecycle counters (Pause,
Restart, Detach), outstanding-resource gauges (per-callback outstanding NBL/OID counts), and
control-path counters (OID dispatch/complete, FilterStatus, FilterNetPnPEvent).

The struct is exposed over IOCTL as a fixed-size binary blob. Any size change is an **ABI
break** — all user-mode consumers (test tools, AVBServices, `avb_test_*.exe`) must be
recompiled against the new header. This is acceptable as long as the change is versioned and
communicated.

## Decision

We will extend `_AVB_DRIVER_STATISTICS` in `include/avb_ioctl.h` to add eleven new `avb_u64`
fields covering lifecycle, datapath, and control-path coverage counters:

```c
avb_u64 FilterPauseCount;          /* FilterPause invocations               */
avb_u64 FilterRestartCount;        /* FilterRestart invocations             */
avb_u64 FilterDetachCount;         /* FilterDetach invocations              */
avb_u64 OutstandingSendNBLs;       /* Send NBLs currently in flight         */
avb_u64 OutstandingReceiveNBLs;    /* Receive NBLs currently in flight      */
avb_u64 OidRequestCount;           /* FilterOidRequest invocations          */
avb_u64 OidCompleteCount;          /* FilterOidRequestComplete invocations  */
avb_u64 OutstandingOids;           /* OID requests currently pending        */
avb_u64 FilterStatusCount;         /* FilterStatus indications passed       */
avb_u64 FilterNetPnPCount;         /* FilterNetPnPEvent invocations         */
avb_u64 PauseRestartGeneration;    /* Monotonically increasing per cycle    */
```

Struct grows from **13 × 8 = 104 bytes → 24 × 8 = 192 bytes**.

The existing comment `"13 × 8 = 104 bytes"` and TC-ABI-017 size assertion in
`tests/unit/abi/test_ioctl_abi.c` must be updated; a new **TC-ABI-018** must assert
`sizeof(AVB_DRIVER_STATISTICS) == 192`.

Counter semantics:
- **Entry counters** (`FilterPauseCount`, `FilterRestartCount`, `FilterDetachCount`,
  `OidRequestCount`, `OidCompleteCount`, `FilterStatusCount`, `FilterNetPnPCount`):
  incremented with `InterlockedIncrement64` at the top of each callback.
- **Outstanding gauges** (`OutstandingSendNBLs`, `OutstandingReceiveNBLs`, `OutstandingOids`):
  incremented at callback entry, decremented with `InterlockedDecrement64` at
  `NdisFSendNetBufferListsComplete` / `NdisFReturnNetBufferLists` / `FilterOidRequestComplete`.
  These must reach **zero at `FilterPause` completion** — the test harness asserts this.
- **`PauseRestartGeneration`**: incremented at `FilterPause` entry; allows tests to detect
  spurious or missing pause/restart cycles.

## Status

Accepted. Implementation pending (see roadmap in `TEST-PLAN-NDIS-LWF-COVERAGE.md` Step 1–2).

## Rationale

### Why instrument the driver rather than external coverage tools?

For a kernel LWF, scenario coverage evidence is far more informative than statement coverage.
A passing `FilterPauseCount` delta proves the NDIS state machine ran the callback in a real
sequence; no static instrumentation tool can provide that.

### Why all-`avb_u64`, `#pragma pack(push, 8)` alignment?

The struct is already `#pragma pack(push, 8)`. Consistent 8-byte fields guarantee no implicit
padding, predictable offsets, and atomic 64-bit reads on x64 (natural alignment). Using
`volatile LONGLONG` internally (in `AVB_ADAPTER`) and copying to `avb_u64` on IOCTL read
avoids torn reads across the kernel/user boundary.

### Why a single struct extension rather than a new IOCTL?

Adding a new IOCTL would split the query surface. A single `IOCTL_AVB_GET_STATISTICS` call
gives an atomic snapshot of all counters at one point in time, preventing TOCTOU races
between per-counter reads. The ABI break cost is accepted because all consumers are
in-tree.

### Why not use ETW traces for coverage?

ETW traces are high-frequency and require a separate trace session to consume. They are
appropriate for debugging and performance profiling, not for deterministic test assertions.
A counter read is synchronous, zero-overhead when not being tested, and directly assertable.

## Considered Alternatives

| Alternative | Summary | Pros | Cons | Reason Not Chosen |
|---|---|---|---|---|
| ETW TraceLogging per callback | Emit ETW event at each callback entry; collect in test | Rich data, timestamped | Requires active trace session; not directly assertable; overhead | Not suitable for assertion-based testing |
| Per-callback separate IOCTLs | New IOCTL per counter set | Smaller individual payloads | Splits atomic snapshot; multiplies IOCTL surface; more ABI to maintain | Single atomic snapshot preferred |
| WPP software tracing with counters | WPP macros with counter sidecars | Already in driver | WPP counters not user-queryable via IOCTL without extra glue | Too much plumbing for this purpose |
| New separate IOCTL_AVB_GET_LIFECYCLE_STATS | Keep packet stats and lifecycle stats separate | No ABI break on existing stats struct | Two IOCTLs to read for complete picture; TOCTOU between reads | Atomicity of single read wins |

## Consequences

### Positive
- Tests can prove that each NDIS callback was exercised in every scenario (lifecycle coverage)
- Tests can assert zero outstanding NBLs/OIDs at `FilterPause` completion (correctness invariant)
- `PauseRestartGeneration` makes pause/restart cycle count auditable
- Single IOCTL gives an atomic snapshot of all coverage evidence
- `test_ioctl_abi.c` TC-ABI-018 guards the struct layout forever (regression protection)

### Negative / Liabilities
- **ABI break**: all consumers must be rebuilt against updated `avb_ioctl.h`. Mitigation:
  `AVB_ABI_VERSION` already encodes major/minor; bump `ABI_VERSION_MAJOR` on this change so
  old binaries get `NDIS_STATUS_INVALID_DATA` instead of silently misreading fields.
- Struct comment and ABI test must be updated in the same commit (enforced by TC-ABI-018
  being a CI gate — if the test is not updated the CI build fails).
- 11 extra `InterlockedIncrement64`/`InterlockedDecrement64` calls in hot datapath
  (`OutstandingSendNBLs`, `OutstandingReceiveNBLs`). On x64, `lock xadd` is ~5–10 ns;
  acceptable for an LWF that already does register MMIO on the datapath.

## Implementation Checklist

- [ ] Bump `ABI_VERSION_MAJOR` in `include/avb_ioctl.h`
- [ ] Add 11 fields to `_AVB_DRIVER_STATISTICS`; update comment to `"24 × 8 = 192 bytes"`
- [ ] Add corresponding `volatile LONGLONG` fields to `AVB_ADAPTER` in `src/avb_integration.h`
- [ ] Add `STATS_INC` / `STATS_DEC` macros in `src/filter.c` for readability
- [ ] Increment entry counters at top of each callback in `src/filter.c`
- [ ] Increment/decrement Outstanding gauges at send-complete / receive-return / OID-complete
- [ ] Copy new fields in `AvbHandleGetStatistics` IOCTL handler
- [ ] Update TC-ABI-017 comment; add TC-ABI-018: `sizeof(AVB_DRIVER_STATISTICS) == 192`
- [ ] Verify `test_ioctl_abi.exe` passes (CI gate)
- [ ] Add lifecycle assertions to `tests/hardware/test_lifecycle_coverage.c` (new test)

## Traceability

- Traces to: #265 (TEST-COVERAGE-001)
- Traces to: #98 (REQ-NF-BUILD-CI-001)
- Implements plan: `07-verification-validation/test-plans/TEST-PLAN-NDIS-LWF-COVERAGE.md` Step 1–2
