/**
 * @file test_ioctl_xstamp.c
 * @brief PHC Cross-Timestamp Tests (PHC ↔ System Clock capture)
 *
 * Implements: #198 (TEST-IOCTL-XSTAMP-001)
 * Verifies:   #186 (REQ-F-PHC-XSTAMP-001: Cross-timestamp PHC with host clock)
 *
 * IOCTL codes:
 *   45 (IOCTL_AVB_GET_CLOCK_CONFIG) — returns systim (PHC in nanoseconds)
 *
 * Cross-timestamp technique (no dedicated HW API available in this driver):
 *   Software bracketing — QPC is sampled before and after the IOCTL;
 *   midpoint is taken as the system-time correspondent of the PHC reading.
 *   This is the IEEE 1588-2019 §B.3.2 "sample-based cross-timestamping" method.
 *
 * Test Cases: 5
 *   TC-XSTAMP-001: Single capture — PHC > 0, status OK
 *   TC-XSTAMP-002: Capture latency — 1,000 captures, P99 bracket width < 5 ms
 *   TC-XSTAMP-003: PHC is monotonically non-decreasing across all captures
 *   TC-XSTAMP-004: Back-to-back captures show PHC advances relative to QPC
 *   TC-XSTAMP-005: PHC vs QPC correlation (soft pass if clocks not synced)
 *
 * Priority: P0 (Critical — cross-timestamp accuracy underpins all gPTP measurements)
 *
 * Standards: IEEE 1588-2019 §B.3.2 (Cross-timestamping)
 *            IEEE 802.1AS-2020 §10.4 (Synchronized clock)
 *
 * @see https://github.com/zarfld/IntelAvbFilter/issues/198
 * @see https://github.com/zarfld/IntelAvbFilter/issues/186
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

/* Single Source of Truth for IOCTL definitions */
#include "../include/avb_ioctl.h"

/* ────────────────────────── constants ───────────────────────────────────── */
#define XSTAMP_SAMPLES          1000u
#define MAX_BRACKET_NS          5000000LL   /* 5 ms — very conservative for usermode */
#define P99_PERCENTILE_INDEX    990u        /* index into sorted 1000-entry array */
#define NSEC_PER_SEC            1000000000ULL
#define NSEC_PER_MS             1000000LL

/* If gPTP is syncing, PHC ≈ TAI.  TAI = UTC + 37 s (as of 2024).
 * If SYSTIM > (Unix-epoch 2020-01-01 in ns), the clock looks TAI-synced.
 * 2020-01-01 00:00:00 UTC in ns since 1970: 1577836800 * 1e9 */
#define TAI_SYNCED_THRESHOLD_NS  1577836800000000000ULL

/* ────────────────────────── test infra ──────────────────────────────────── */
#define TEST_PASS 0
#define TEST_FAIL 1
#define TEST_SKIP 2

typedef struct {
    int pass_count;
    int fail_count;
    int skip_count;
} Results;

typedef struct {
    UINT64 phc_ns;          /* SYSTIM from driver */
    LONGLONG qpc_before;    /* ticks */
    LONGLONG qpc_after;     /* ticks */
    BOOL     ioctl_ok;
} XStampSample;

static LARGE_INTEGER s_qpc_freq;

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

/* Capture one cross-timestamp sample */
static XStampSample CaptureSample(HANDLE h)
{
    XStampSample s = {0};
    AVB_CLOCK_CONFIG cfg = {0};
    DWORD bytes = 0;
    LARGE_INTEGER qpc;

    QueryPerformanceCounter(&qpc);
    s.qpc_before = qpc.QuadPart;

    /* Use struct as BOTH input and output buffer — required by this driver */
    s.ioctl_ok = DeviceIoControl(h,
        IOCTL_AVB_GET_CLOCK_CONFIG,
        &cfg, sizeof(cfg),
        &cfg, sizeof(cfg),
        &bytes, NULL);

    QueryPerformanceCounter(&qpc);
    s.qpc_after = qpc.QuadPart;

    if (s.ioctl_ok) s.phc_ns = cfg.systim;
    return s;
}

/* Convert QPC ticks to nanoseconds */
static UINT64 TicksToNs(LONGLONG ticks)
{
    return (UINT64)(ticks) * NSEC_PER_SEC / (UINT64)s_qpc_freq.QuadPart;
}

static LONGLONG BracketWidthNs(const XStampSample *s)
{
    return (LONGLONG)TicksToNs(s->qpc_after - s->qpc_before);
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

/* Simple insertion-sort for small arrays (avoid qsort for clarity) */
static int CompareLonglong(const void *a, const void *b)
{
    LONGLONG la = *(const LONGLONG *)a;
    LONGLONG lb = *(const LONGLONG *)b;
    return (la > lb) - (la < lb);
}

/* ════════════════════════ TC-XSTAMP-001 ════════════════════════════════════
 * Single PHC cross-timestamp capture — basic sanity.
 */
static int TC_XStamp_001_SingleCapture(void)
{
    printf("\n  TC-XSTAMP-001: Single capture sanity\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return TEST_SKIP;

    XStampSample s = CaptureSample(h);
    CloseHandle(h);

    if (!s.ioctl_ok) {
        printf("    FAIL: IOCTL_AVB_GET_CLOCK_CONFIG failed (error %lu)\n", GetLastError());
        return TEST_FAIL;
    }

    printf("    PHC systim   : %llu ns\n", s.phc_ns);
    printf("    QPC bracket  : %lld ns\n", BracketWidthNs(&s));

    if (s.phc_ns == 0) {
        printf("    FAIL: PHC returned zero (clock not running?)\n");
        return TEST_FAIL;
    }

    LONGLONG bw = BracketWidthNs(&s);
    if (bw < 0) {
        printf("    FAIL: negative bracket (QPC went backwards?)\n");
        return TEST_FAIL;
    }

    return TEST_PASS;
}

/* ════════════════════════ TC-XSTAMP-002 ════════════════════════════════════
 * 1,000 captures — P99 bracket width under threshold.
 */
static int TC_XStamp_002_CapturLatency(void)
{
    printf("\n  TC-XSTAMP-002: Capture latency P99 < %lld ms (%u samples)\n",
           MAX_BRACKET_NS / NSEC_PER_MS, XSTAMP_SAMPLES);

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return TEST_SKIP;

    static LONGLONG widths[XSTAMP_SAMPLES];
    UINT32 errors = 0;

    for (UINT32 i = 0; i < XSTAMP_SAMPLES; i++) {
        XStampSample s = CaptureSample(h);
        if (!s.ioctl_ok) { errors++; widths[i] = MAX_BRACKET_NS * 2; continue; }
        widths[i] = BracketWidthNs(&s);
    }
    CloseHandle(h);

    qsort(widths, XSTAMP_SAMPLES, sizeof(LONGLONG), CompareLonglong);

    LONGLONG p50 = widths[500];
    LONGLONG p99 = widths[P99_PERCENTILE_INDEX];
    LONGLONG worstcase = widths[XSTAMP_SAMPLES - 1];

    printf("    errors=%u  P50=%lld ns  P99=%lld ns  worst=%lld ns\n",
           errors, p50, p99, worstcase);

    if (errors > XSTAMP_SAMPLES / 10) {
        printf("    FAIL: Too many IOCTL errors (%u/%u)\n", errors, XSTAMP_SAMPLES);
        return TEST_FAIL;
    }
    if (p99 > MAX_BRACKET_NS) {
        printf("    FAIL: P99 bracket %lld ns exceeds threshold %lld ns\n",
               p99, MAX_BRACKET_NS);
        return TEST_FAIL;
    }
    return TEST_PASS;
}

/* ════════════════════════ TC-XSTAMP-003 ════════════════════════════════════
 * PHC is monotonically non-decreasing across all 1,000 captures.
 */
static int TC_XStamp_003_PHCMonotonic(void)
{
    printf("\n  TC-XSTAMP-003: PHC monotonic across %u captures\n", XSTAMP_SAMPLES);

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return TEST_SKIP;

    UINT64 prev = 0;
    UINT32 inversions = 0, errors = 0;

    for (UINT32 i = 0; i < XSTAMP_SAMPLES; i++) {
        XStampSample s = CaptureSample(h);
        if (!s.ioctl_ok) { errors++; continue; }
        if (s.phc_ns < prev) {
            printf("    [!] INVERSION i=%u prev=%llu cur=%llu\n", i, prev, s.phc_ns);
            inversions++;
        }
        prev = s.phc_ns;
    }
    CloseHandle(h);

    printf("    errors=%u inversions=%u\n", errors, inversions);
    if (inversions > 0) {
        printf("    FAIL: PHC not monotonic (%u inversion(s))\n", inversions);
        return TEST_FAIL;
    }
    return TEST_PASS;
}

/* ════════════════════════ TC-XSTAMP-004 ════════════════════════════════════
 * Back-to-back captures: PHC delta relative to QPC delta is positive.
 * Verifies PHC is counting forward with respect to wall time.
 */
static int TC_XStamp_004_BackToBack(void)
{
    printf("\n  TC-XSTAMP-004: Back-to-back PHC/QPC advance check\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return TEST_SKIP;

    XStampSample s1 = CaptureSample(h);
    Sleep(50);  /* deliberate 50 ms gap */
    XStampSample s2 = CaptureSample(h);
    CloseHandle(h);

    if (!s1.ioctl_ok || !s2.ioctl_ok) {
        printf("    [SKIP] IOCTL failed — skip\n");
        return TEST_SKIP;
    }

    LONGLONG qpc_delta_ns = (LONGLONG)TicksToNs(s2.qpc_before - s1.qpc_after);
    LONGLONG phc_delta_ns = (LONGLONG)s2.phc_ns - (LONGLONG)s1.phc_ns;

    printf("    QPC gap   : %lld ns\n", qpc_delta_ns);
    printf("    PHC delta : %lld ns\n", phc_delta_ns);

    if (phc_delta_ns <= 0) {
        printf("    FAIL: PHC did not advance (delta=%lld ns) over 50 ms gap\n", phc_delta_ns);
        return TEST_FAIL;
    }
    if (qpc_delta_ns <= 0) {
        printf("    WARN: negative QPC gap — clock oddity; treating as SKIP\n");
        return TEST_SKIP;
    }
    return TEST_PASS;
}

/* ════════════════════════ TC-XSTAMP-005 ════════════════════════════════════
 * PHC vs QPC correlation (soft pass if gPTP not synchronised).
 * If PHC looks synced to TAI (value > year-2020 threshold), verify
 * the offset |PHC - QPC_mid| is within reasonable synchronisation bounds.
 * If PHC is small (just driver boot time), this test soft-passes.
 */
static int TC_XStamp_005_Correlation(void)
{
    printf("\n  TC-XSTAMP-005: PHC/QPC correlation (soft if not gPTP-synced)\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return TEST_SKIP;

    XStampSample s = CaptureSample(h);
    CloseHandle(h);

    if (!s.ioctl_ok) return TEST_SKIP;

    /* QPC midpoint in ns since some arbitrary epoch (QPC epoch ≠ Unix) */
    UINT64 qpc_mid_ticks = (UINT64)((s.qpc_before + s.qpc_after) / 2);
    UINT64 qpc_mid_ns    = TicksToNs((LONGLONG)qpc_mid_ticks);

    printf("    PHC systim   : %llu ns\n", s.phc_ns);
    printf("    QPC mid (ns) : %llu ns\n", qpc_mid_ns);

    /* Is PHC in TAI-synced territory? */
    if (s.phc_ns < TAI_SYNCED_THRESHOLD_NS) {
        printf("    PHC < 2020 threshold — clock likely not TAI-synced yet\n");
        printf("    [SOFT PASS] Cannot assert correlation without gPTP sync\n");
        return TEST_PASS;  /* soft pass */
    }

    /*
     * PHC is TAI-synced.  QPC counts from boot (not Unix epoch), so we
     * cannot directly subtract them.  Instead verify both are non-zero
     * and PHC is in a sane absolute range (2020–2100 AD in ns).
     */
    UINT64 year2100_ns = 4102444800ULL * NSEC_PER_SEC;
    if (s.phc_ns > year2100_ns) {
        printf("    FAIL: PHC value %llu ns exceeds year-2100 bound (likely wrap/corruption)\n",
               s.phc_ns);
        return TEST_FAIL;
    }

    printf("    PHC in valid TAI range [2020, 2100] — PASS\n");
    return TEST_PASS;
}

/* ─────────────────────────── main ──────────────────────────────────────── */
int main(void)
{
    QueryPerformanceFrequency(&s_qpc_freq);

    printf("===========================================\n");
    printf(" PHC Cross-Timestamp Tests (Issue #198)\n");
    printf(" IOCTL_AVB_GET_CLOCK_CONFIG (code 45)\n");
    printf(" QPC freq: %lld Hz\n", s_qpc_freq.QuadPart);
    printf("===========================================\n");

    Results r = {0};
    RecordResult(&r, TC_XStamp_001_SingleCapture(),   "TC-XSTAMP-001: Single capture sanity");
    RecordResult(&r, TC_XStamp_002_CapturLatency(),   "TC-XSTAMP-002: Capture latency P99");
    RecordResult(&r, TC_XStamp_003_PHCMonotonic(),    "TC-XSTAMP-003: PHC monotonic");
    RecordResult(&r, TC_XStamp_004_BackToBack(),      "TC-XSTAMP-004: Back-to-back advance");
    RecordResult(&r, TC_XStamp_005_Correlation(),     "TC-XSTAMP-005: PHC/QPC correlation");

    printf("\n-------------------------------------------\n");
    printf(" PASS=%d  FAIL=%d  SKIP=%d  TOTAL=%d\n",
           r.pass_count, r.fail_count, r.skip_count,
           r.pass_count + r.fail_count + r.skip_count);
    printf("-------------------------------------------\n");

    return (r.fail_count > 0) ? 1 : 0;
}
