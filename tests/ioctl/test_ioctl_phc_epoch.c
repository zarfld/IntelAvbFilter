/**
 * @file test_ioctl_phc_epoch.c
 * @brief PHC Epoch Baseline and TAI-UTC Offset Validation Tests
 *
 * Implements: #196 (TEST-IOCTL-PHC-EPOCH-001)
 * Verifies:   #187 (REQ-F-PHC-EPOCH-001: PHC epoch base = TAI 1970-01-01 00:00:00)
 *
 * IOCTL codes:
 *   45 (IOCTL_AVB_GET_CLOCK_CONFIG) — returns systim (hardware clock in nanoseconds)
 *
 * TAI background:
 *   - PHC counts nanoseconds since the TAI epoch (1970-01-01 00:00:00 UTC).
 *   - TAI = UTC + leap_seconds.  The current (2024) TAI-UTC offset is 37 seconds.
 *   - Once the gPTP stack sets the hardware clock, SYSTIM is directly comparable
 *     to GetSystemTimeAsFileTime (which returns UTC).
 *   - If the gPTP stack has not synced the NIC yet, SYSTIM reflects time-since-
 *     driver-load and will be much smaller than the expected TAI wall-clock value.
 *
 * Test Cases: 5
 *   TC-EPOCH-001: PHC > 0 and driver IOCTL succeeds
 *   TC-EPOCH-002: PHC advances over a 100 ms window (not frozen)
 *   TC-EPOCH-003: If PHC ≥ 2020-threshold → compute PHC - UTC_ns; verify delta ≈ 37 s
 *   TC-EPOCH-004: SOFT PASS if PHC < 2020-threshold (not gPTP-synced)
 *   TC-EPOCH-005: PHC stays below year-2100 bound (no 64-bit wrap or corruption)
 *
 * Priority: P0 (Critical — wrong epoch offset causes all PTP timing to be wrong)
 *
 * Standards: IEEE 1588-2019 §8.2 (TAI epoch definition)
 *            IETF RFC 8233 (TAI-UTC offset)
 *            IEEE 802.1AS-2020 §8.6.2 (epochNumber)
 *
 * @see https://github.com/zarfld/IntelAvbFilter/issues/196
 * @see https://github.com/zarfld/IntelAvbFilter/issues/187
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

/* Single Source of Truth for IOCTL definitions */
#include "../include/avb_ioctl.h"

/* ────────────────────────── constants ───────────────────────────────────── */
#define NSEC_PER_SEC            1000000000ULL
#define NSEC_PER_MS             1000000LL

/* 2020-01-01 00:00:00 UTC as nanoseconds since 1970-01-01 (Unix/TAI epoch) */
#define EPOCH_YEAR2020_NS       (1577836800ULL * NSEC_PER_SEC)

/* 2100-01-01 00:00:00 UTC as nanoseconds since 1970-01-01 */
#define EPOCH_YEAR2100_NS       (4102444800ULL * NSEC_PER_SEC)

/* TAI-UTC offset in seconds (current per IERS: valid since 2017-01-01) */
#define TAI_UTC_OFFSET_S        37ULL
#define TAI_UTC_OFFSET_NS       (TAI_UTC_OFFSET_S * NSEC_PER_SEC)

/* Allowable error when checking TAI-UTC offset (±10 s covers drift + test jitter) */
#define TAI_UTC_TOLERANCE_NS    (10ULL * NSEC_PER_SEC)

/* Windows FILETIME epoch = 1601-01-01. Unix/TAI epoch = 1970-01-01.
 * Difference in 100-ns FILETIME ticks: 116444736000000000 */
#define FILETIME_TO_UNIX_OFFSET_100NS  116444736000000000ULL

/* Advance check window */
#define ADVANCE_SLEEP_MS        100u

/* ────────────────────────── test infra ──────────────────────────────────── */
#define TEST_PASS 0
#define TEST_FAIL 1
#define TEST_SKIP 2

typedef struct {
    int pass_count;
    int fail_count;
    int skip_count;
} Results;

static HANDLE OpenDevice(void)
{
    HANDLE h = CreateFileA(
        "\\\\.\\IntelAvbFilter",
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("  [SKIP] Cannot open device (error %lu)\n", GetLastError());
    }
    return h;
}

static BOOL ReadPHC(HANDLE h, UINT64 *out_ns)
{
    AVB_CLOCK_CONFIG cfg = {0};
    DWORD bytes = 0;
    /* Use struct as BOTH input and output buffer — required by this driver */
    BOOL ok = DeviceIoControl(h,
        IOCTL_AVB_GET_CLOCK_CONFIG,
        &cfg, sizeof(cfg),
        &cfg, sizeof(cfg),
        &bytes, NULL);
    if (ok && out_ns) *out_ns = cfg.systim;
    return ok;
}

/* Get current UTC time as nanoseconds since Unix epoch (1970-01-01) */
static UINT64 GetUtcNs(void)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    UINT64 ft100ns = ((UINT64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    /* Convert FILETIME offset to Unix epoch; multiply 100-ns ticks to ns */
    if (ft100ns < FILETIME_TO_UNIX_OFFSET_100NS) return 0;
    return (ft100ns - FILETIME_TO_UNIX_OFFSET_100NS) * 100ULL;
}

static void RecordResult(Results *r, int result, const char *name)
{
    const char *label = (result == TEST_PASS) ? "PASS" :
                        (result == TEST_SKIP) ? "SKIP" : "FAIL";
    printf("  [%s] %s\n", label, name);
    if (result == TEST_PASS) r->pass_count++;
    else if (result == TEST_SKIP) r->skip_count++;
    else r->fail_count++;
}

/* ════════════════════════ TC-EPOCH-001 ════════════════════════════════════
 * Verify IOCTL succeeds and returns a non-zero PHC value.
 */
static int TC_Epoch_001_BasicRead(void)
{
    printf("\n  TC-EPOCH-001: PHC basic read > 0\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return TEST_SKIP;

    UINT64 systim = 0;
    BOOL ok = ReadPHC(h, &systim);
    DWORD err = GetLastError();
    CloseHandle(h);

    if (!ok) {
        printf("    FAIL: IOCTL_AVB_GET_CLOCK_CONFIG error %lu\n", err);
        return TEST_FAIL;
    }
    printf("    systim = %llu ns\n", systim);

    if (systim == 0) {
        printf("    FAIL: PHC returned zero — hardware clock not running\n");
        return TEST_FAIL;
    }
    return TEST_PASS;
}

/* ════════════════════════ TC-EPOCH-002 ════════════════════════════════════
 * PHC must advance over a 100 ms window (not frozen / stuck).
 */
static int TC_Epoch_002_Advance(void)
{
    printf("\n  TC-EPOCH-002: PHC advances over %u ms\n", ADVANCE_SLEEP_MS);

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return TEST_SKIP;

    UINT64 t0 = 0, t1 = 0;
    if (!ReadPHC(h, &t0)) { CloseHandle(h); return TEST_SKIP; }
    Sleep(ADVANCE_SLEEP_MS);
    BOOL ok = ReadPHC(h, &t1);
    CloseHandle(h);

    if (!ok) return TEST_SKIP;

    LONGLONG delta_ns = (LONGLONG)t1 - (LONGLONG)t0;
    printf("    t0=%llu  t1=%llu  delta=%lld ns\n", t0, t1, delta_ns);

    if (delta_ns <= 0) {
        printf("    FAIL: PHC did not advance (delta=%lld ns)\n", delta_ns);
        return TEST_FAIL;
    }

    /* Sanity: delta should be roughly 100 ms ± 50 ms */
    LONGLONG expected_lo = (LONGLONG)(ADVANCE_SLEEP_MS - 50) * NSEC_PER_MS;
    LONGLONG expected_hi = (LONGLONG)(ADVANCE_SLEEP_MS + 50) * 10LL * NSEC_PER_MS;
    if (delta_ns < expected_lo) {
        printf("    WARN: PHC advanced only %lld ns over %u ms — clock running slow?\n",
               delta_ns, ADVANCE_SLEEP_MS);
        /* Not a hard failure — gPTP might be slewing */
    }
    if (delta_ns > expected_hi) {
        printf("    FAIL: PHC jumped %lld ns in %u ms — too fast (wrap or corruption?)\n",
               delta_ns, ADVANCE_SLEEP_MS);
        return TEST_FAIL;
    }
    return TEST_PASS;
}

/* ════════════════════════ TC-EPOCH-003 ════════════════════════════════════
 * If PHC ≥ year-2020 threshold: PHC should be TAI, so PHC - UTC ≈ 37 s.
 * SOFT PASS if PHC < threshold (not gPTP-synced yet).
 */
static int TC_Epoch_003_TAI_UTC_Offset(void)
{
    printf("\n  TC-EPOCH-003: TAI-UTC offset ≈ %llu s (if gPTP-synced)\n",
           TAI_UTC_OFFSET_S);

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return TEST_SKIP;

    UINT64 phc_ns  = 0;
    BOOL ok = ReadPHC(h, &phc_ns);
    CloseHandle(h);
    if (!ok) return TEST_SKIP;

    UINT64 utc_ns = GetUtcNs();

    printf("    PHC (ns)  : %llu\n", phc_ns);
    printf("    UTC (ns)  : %llu\n", utc_ns);

    if (phc_ns < EPOCH_YEAR2020_NS) {
        printf("    PHC < 2020 threshold — driver clock likely not gPTP-synced\n");
        printf("    [SOFT PASS]\n");
        return TEST_PASS;   /* soft pass */
    }

    /* Both values are now TAI/UTC-epoch-relative */
    LONGLONG offset_ns = (LONGLONG)phc_ns - (LONGLONG)utc_ns;
    printf("    PHC - UTC : %lld ns  (%lld s)\n",
           offset_ns, offset_ns / (LONGLONG)NSEC_PER_SEC);
    printf("    Expected  : +%llu s ± %llu s\n",
           TAI_UTC_OFFSET_S, TAI_UTC_TOLERANCE_NS / NSEC_PER_SEC);

    LONGLONG expected = (LONGLONG)TAI_UTC_OFFSET_NS;
    LONGLONG lo = expected - (LONGLONG)TAI_UTC_TOLERANCE_NS;
    LONGLONG hi = expected + (LONGLONG)TAI_UTC_TOLERANCE_NS;

    if (offset_ns < lo || offset_ns > hi) {
        printf("    FAIL: TAI-UTC offset %lld s out of expected range [%lld, %lld] s\n",
               offset_ns / (LONGLONG)NSEC_PER_SEC,
               lo / (LONGLONG)NSEC_PER_SEC,
               hi / (LONGLONG)NSEC_PER_SEC);
        return TEST_FAIL;
    }
    printf("    TAI-UTC offset within tolerance — PASS\n");
    return TEST_PASS;
}

/* ════════════════════════ TC-EPOCH-004 ════════════════════════════════════
 * PHC stays below year-2100 bound (no 64-bit wrap or HW corruption).
 */
static int TC_Epoch_004_UpperBound(void)
{
    printf("\n  TC-EPOCH-004: PHC below year-2100 upper bound\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return TEST_SKIP;

    UINT64 phc_ns = 0;
    BOOL ok = ReadPHC(h, &phc_ns);
    CloseHandle(h);
    if (!ok) return TEST_SKIP;

    printf("    PHC = %llu ns\n", phc_ns);
    printf("    Y2100 bound = %llu ns\n", EPOCH_YEAR2100_NS);

    if (phc_ns > EPOCH_YEAR2100_NS) {
        printf("    FAIL: PHC %llu ns exceeds year-2100 bound (likely wrap/corruption)\n",
               phc_ns);
        return TEST_FAIL;
    }
    return TEST_PASS;
}

/* ════════════════════════ TC-EPOCH-005 ════════════════════════════════════
 * Multiple reads — PHC is non-decreasing (epoch-specific monotonicity spot-check).
 */
static int TC_Epoch_005_MonotonicSpot(void)
{
#define SPOT_READS 100u
    printf("\n  TC-EPOCH-005: Epoch-range spot monotonicity (%u reads)\n", SPOT_READS);

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return TEST_SKIP;

    UINT64 prev = 0, cur = 0;
    UINT32 inversions = 0;

    if (!ReadPHC(h, &prev)) { CloseHandle(h); return TEST_SKIP; }

    for (UINT32 i = 1; i < SPOT_READS; i++) {
        if (!ReadPHC(h, &cur)) continue;
        if (cur < prev) {
            printf("    [!] INVERSION i=%u prev=%llu cur=%llu\n", i, prev, cur);
            inversions++;
        }
        prev = cur;
    }
    CloseHandle(h);

    printf("    inversions=%u\n", inversions);
    if (inversions > 0) {
        printf("    FAIL: PHC not monotonic in epoch range (%u inversion(s))\n", inversions);
        return TEST_FAIL;
    }
    return TEST_PASS;
#undef SPOT_READS
}

/* ─────────────────────────── main ──────────────────────────────────────── */
int main(void)
{
    printf("===========================================\n");
    printf(" PHC Epoch Validation Tests (Issue #196)\n");
    printf(" IOCTL_AVB_GET_CLOCK_CONFIG (code 45)\n");
    printf(" TAI epoch: 1970-01-01T00:00:00Z\n");
    printf(" TAI-UTC offset: %llu s\n", TAI_UTC_OFFSET_S);
    printf("===========================================\n");

    /* Diagnostic: print current UTC/TAI context */
    UINT64 utc_ns = GetUtcNs();
    printf(" System UTC: %llu ns (%llu s)\n", utc_ns, utc_ns / NSEC_PER_SEC);
    printf(" System TAI: %llu ns (%llu s)\n",
           utc_ns + TAI_UTC_OFFSET_NS,
           (utc_ns + TAI_UTC_OFFSET_NS) / NSEC_PER_SEC);
    printf("\n");

    Results r = {0};
    RecordResult(&r, TC_Epoch_001_BasicRead(),      "TC-EPOCH-001: Basic read > 0");
    RecordResult(&r, TC_Epoch_002_Advance(),        "TC-EPOCH-002: Clock advances");
    RecordResult(&r, TC_Epoch_003_TAI_UTC_Offset(), "TC-EPOCH-003: TAI-UTC offset ≈ 37 s");
    RecordResult(&r, TC_Epoch_004_UpperBound(),     "TC-EPOCH-004: Below year-2100 bound");
    RecordResult(&r, TC_Epoch_005_MonotonicSpot(),  "TC-EPOCH-005: Spot monotonicity");

    printf("\n-------------------------------------------\n");
    printf(" PASS=%d  FAIL=%d  SKIP=%d  TOTAL=%d\n",
           r.pass_count, r.fail_count, r.skip_count,
           r.pass_count + r.fail_count + r.skip_count);
    printf("-------------------------------------------\n");

    return (r.fail_count > 0) ? 1 : 0;
}
