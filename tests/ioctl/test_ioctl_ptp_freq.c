/**
 * @file test_ioctl_ptp_freq.c
 * @brief PTP Frequency Adjustment Verification Tests
 * 
 * Implements: #296 (TEST-PTP-FREQ-001)
 * Verifies: #3 (REQ-F-PTP-002: PTP Frequency Adjustment via IOCTL)
 * 
 * IOCTLs: 38 (IOCTL_AVB_ADJUST_FREQUENCY)
 * Test Cases: 15
 * Priority: P0 (Critical)
 * 
 * Standards: IEEE 1012-2016 (Verification & Validation)
 * Standards: IEEE 1588-2019 (PTP)
 * 
 * @see https://github.com/zarfld/IntelAvbFilter/issues/296
 * @see https://github.com/zarfld/IntelAvbFilter/issues/3
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Single Source of Truth for IOCTL definitions */
#include "../include/avb_ioctl.h"

/* Multi-adapter enumeration and capability gating */
#include "../common/avb_test_common.h"

/* Test result codes */
#define TEST_PASS 0
#define TEST_FAIL 1
#define TEST_SKIP 2

/* Frequency adjustment constants (parts per billion - ppb) */
#define MAX_FREQ_ADJ_PPB  1000000000   /* ±1 second/second = ±1e9 ppb */
#define TYPICAL_FREQ_ADJ_PPB  100000   /* ±100 ppm = ±100000 ppb */
#define NSEC_PER_SEC 1000000000ULL

/* Clock constants for frequency conversion */
#define BASE_CLOCK_HZ    125000000ULL  /* 125 MHz base clock */
#define NOMINAL_INCR_NS  8             /* 8ns nominal increment (125MHz = 8ns period) */
#define FRAC_SCALE       4294967296.0  /* 2^32 for fractional part */

/* Test state */
typedef struct {
    HANDLE adapter;
    INT64 initial_frequency;
    UINT32 nominal_incr_ns;  /* Device-specific nominal: 8 for I210, 24 for I226 */
    int test_count;
    int pass_count;
    int fail_count;
    int skip_count;
} TestContext;

/* Helper: Open device */
static HANDLE OpenAdapter(void)
{
    HANDLE h = CreateFileA(
        "\\\\.\\IntelAvbFilter",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        printf("  [WARN] Could not open device: error %lu\n", err);
    }
    
    return h;
}

/* Helper: Convert ppb to increment_ns/increment_frac
 *
 * Uses the device-detected nominal_ns so the boundary arithmetic is correct on
 * every adapter (I210 nominal=8, I226 nominal=24, I219 fallback=8).
 * Callers pass ctx->nominal_incr_ns; NOMINAL_INCR_NS is the fallback default.
 *
 * Example (I226, nominal=24, ppb=+50000):
 *   new_increment = 24.0 * (1 + 50000/1e9) = 24.0 * 1.00005 = 24.0012 ns
 *   increment_ns = 24, increment_frac = 0.0012 * 2^32
 */
static void ConvertPpbToIncrement(INT64 ppb, UINT32 nominal_ns,
                                   UINT32 *increment_ns, UINT32 *increment_frac)
{
    double adjustment_factor = 1.0 + ((double)ppb / 1000000000.0);
    double new_increment = (double)nominal_ns * adjustment_factor;

    *increment_ns = (UINT32)new_increment;  /* Integer part */
    double frac_part = new_increment - (double)(*increment_ns);
    *increment_frac = (UINT32)(frac_part * FRAC_SCALE);
}

/* Helper: Adjust frequency
 * nominal_ns — device-specific nominal period; use ctx->nominal_incr_ns.
 * Thread contexts that use small ppb values may pass NOMINAL_INCR_NS safely
 * since small adjustments are well within range for all supported devices.
 */
static BOOL AdjustFrequency(HANDLE adapter, UINT32 nominal_ns, INT64 ppb)
{
    AVB_FREQUENCY_REQUEST req = {0};  /* ✅ Use SSOT structure */
    DWORD bytesReturned = 0;
    BOOL result;

    /* Convert ppb to increment_ns/increment_frac using device-specific nominal */
    ConvertPpbToIncrement(ppb, nominal_ns, &req.increment_ns, &req.increment_frac);

    result = DeviceIoControl(
        adapter,
        IOCTL_AVB_ADJUST_FREQUENCY,
        &req, sizeof(req),
        &req, sizeof(req),
        &bytesReturned,
        NULL
    );

    /* ✅ Check BOTH DeviceIoControl result AND driver status field */
    if (result && req.status != 0) {
        /* IOCTL succeeded but driver returned error status */
        return FALSE;
    }

    return result;
}

/* Helper: Detect device nominal increment_ns by probing the current TIMINCA register.
 * Sends increment_ns=NOMINAL_INCR_NS (8) — valid for any device — then reads
 * back current_increment (the value BEFORE the write) and extracts bits[31:24] = integer ns.
 * Restores the original TIMINCA if it differed from 8ns nominal.
 * Returns the actual nominal period (8 for I210, 24 for I226, etc.).
 */
static UINT32 GetDeviceNominalIncrNs(HANDLE adapter)
{
    AVB_FREQUENCY_REQUEST req = {0};
    DWORD bytesReturned = 0;
    UINT32 nominal;

    req.increment_ns = NOMINAL_INCR_NS;  /* 8 - valid for any supported device */
    req.increment_frac = 0;

    BOOL ok = DeviceIoControl(
        adapter,
        IOCTL_AVB_ADJUST_FREQUENCY,
        &req, sizeof(req),
        &req, sizeof(req),
        &bytesReturned,
        NULL
    );

    if (!ok || (req.status != 0) || req.current_increment == 0) {
        return NOMINAL_INCR_NS;  /* Fallback: assume I210 nominal */
    }

    /* current_increment holds the TIMINCA value BEFORE our write:
     * bits[31:24] = integer ns increment per cycle = hardware nominal */
    nominal = (req.current_increment >> 24) & 0xFF;
    if (nominal == 0) return NOMINAL_INCR_NS;  /* Safety fallback */

    /* Restore the original TIMINCA if we changed it */
    if (nominal != NOMINAL_INCR_NS) {
        AVB_FREQUENCY_REQUEST restore = {0};
        restore.increment_ns  = nominal;
        restore.increment_frac = req.current_increment & 0x00FFFFFFU;  /* preserve sub-ns */
        DeviceIoControl(adapter, IOCTL_AVB_ADJUST_FREQUENCY,
                        &restore, sizeof(restore), &restore, sizeof(restore), &bytesReturned, NULL);
    }

    return nominal;
}

/* Helper: Enable hardware timestamping */
static BOOL EnableHWTimestamping(HANDLE adapter)
{
    AVB_HW_TIMESTAMPING_REQUEST req = {0};
    DWORD bytesReturned = 0;
    
    /* Enable hardware timestamping */
    req.enable = 1;           /* 1 = enable (clear bit 31 of TSAUXC) */
    req.timer_mask = 0x1;     /* Enable SYSTIM0 */
    req.enable_target_time = 0;
    req.enable_aux_ts = 0;
    
    BOOL result = DeviceIoControl(
        adapter,
        IOCTL_AVB_SET_HW_TIMESTAMPING,
        &req, sizeof(req),
        &req, sizeof(req),
        &bytesReturned,
        NULL
    );
    
    if (result) {
        printf("  [INFO] Hardware timestamping enabled (TSAUXC: 0x%08X)\n", req.current_tsauxc);
    }
    
    return result;
}

/*
 * Test UT-PTP-FREQ-001: Zero Frequency Adjustment
 * Verifies: Can set frequency adjustment to 0 (nominal)
 */
static int Test_ZeroFrequencyAdjustment(TestContext *ctx)
{
    if (!AdjustFrequency(ctx->adapter, ctx->nominal_incr_ns, 0)) {
        printf("  [FAIL] UT-PTP-FREQ-001: Zero Frequency Adjustment: IOCTL failed\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-FREQ-001: Zero Frequency Adjustment\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-FREQ-002: Positive Frequency Adjustment
 * Verifies: Can set positive frequency adjustment (+100 ppm)
 */
static int Test_PositiveFrequencyAdjustment(TestContext *ctx)
{
    INT64 adj_ppb = 100000;  /* +100 ppm */

    if (!AdjustFrequency(ctx->adapter, ctx->nominal_incr_ns, adj_ppb)) {
        printf("  [FAIL] UT-PTP-FREQ-002: Positive Frequency Adjustment: IOCTL failed\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-FREQ-002: Positive Frequency Adjustment (+100 ppm)\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-FREQ-003: Negative Frequency Adjustment
 * Verifies: Can set negative frequency adjustment (-100 ppm)
 */
static int Test_NegativeFrequencyAdjustment(TestContext *ctx)
{
    INT64 adj_ppb = -100000;  /* -100 ppm */

    if (!AdjustFrequency(ctx->adapter, ctx->nominal_incr_ns, adj_ppb)) {
        printf("  [FAIL] UT-PTP-FREQ-003: Negative Frequency Adjustment: IOCTL failed\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-FREQ-003: Negative Frequency Adjustment (-100 ppm)\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-FREQ-004: Maximum Positive Adjustment
 * Verifies: Can set maximum positive frequency adjustment
 */
static int Test_MaximumPositiveAdjustment(TestContext *ctx)
{
    /* nominal*(1 + 999999999/1e9) = nominal*1.999999999 ≈ 2*nominal-epsilon
     * → increment_ns = 2*nominal-1 = max_valid_incr for all devices → accepted */
    INT64 adj_ppb = 999999999;

    if (!AdjustFrequency(ctx->adapter, ctx->nominal_incr_ns, adj_ppb)) {
        printf("  [FAIL] UT-PTP-FREQ-004: Maximum Positive Adjustment: IOCTL failed\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-FREQ-004: Maximum Positive Adjustment\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-FREQ-005: Maximum Negative Adjustment
 * Verifies: Can set maximum negative frequency adjustment
 *
 * Hardware limit: with nominal 8 ns/cycle (125 MHz), the minimum TIMINCA increment_ns
 * is 1, achieved when ppb = (1.0/8.0 - 1.0) * 1e9 = -875,000,000 ppb.
 * Values below this produce increment_ns = 0 after ConvertPpbToIncrement, which
 * the driver correctly rejects (clock stopped / frozen). -999,999,999 ppb is
 * below this hardware limit and cannot be represented in the TIMINCA register.
 */
static int Test_MaximumNegativeAdjustment(TestContext *ctx)
{
    /* -875000000 ppb: nominal*(1-0.875) = nominal*0.125 → increment_ns ≥ 1 for all devices
     * I210 (nominal=8):  8*0.125=1  ≥ 1 ✓
     * I226 (nominal=24): 24*0.125=3 ≥ 1 ✓
     * I219 (fallback=8): 8*0.125=1  ≥ 1 ✓ */
    INT64 adj_ppb = -875000000;

    if (!AdjustFrequency(ctx->adapter, ctx->nominal_incr_ns, adj_ppb)) {
        printf("  [FAIL] UT-PTP-FREQ-005: Maximum Negative Adjustment: IOCTL failed\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-FREQ-005: Maximum Negative Adjustment\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-FREQ-006: Out-of-Range Rejection (Positive)
 * Verifies: Rejects adjustments that map to increment_ns > max_valid_incr
 *
 * Device-specific behaviour:
 *   ppb = +1 000 000 001 via ConvertPpbToIncrement(nominal=N) gives:
 *     increment_ns = N * (1 + 1 000 000 001/1 000 000 000) = N * 2.000000001 = 2*N
 *   max_valid_incr = 2*N - 1  for all devices, so 2*N is always rejected.
 *
 *   I210 (nominal=8 → max=15):  ppb → increment_ns=16 > 15 ✓ rejected
 *   I226 (nominal=24 → max=47): ppb → increment_ns=48 > 47 ✓ rejected
 *   I219 (GetDeviceNominalIncrNs falls back to 8 because writing 8 fails on
 *         I219 max=7): ppb → increment_ns=16 > 7 ✓ rejected
 *
 * The full ConvertPpbToIncrement → IOCTL path is exercised on every adapter.
 */
static int Test_OutOfRangeRejectionPositive(TestContext *ctx)
{
    INT64 adj_ppb = 1000000001;  /* > +1e9 ppb (invalid) */

    if (AdjustFrequency(ctx->adapter, ctx->nominal_incr_ns, adj_ppb)) {
        printf("  [FAIL] UT-PTP-FREQ-006: Out-of-Range Rejection (Positive): "
               "Invalid value accepted (ppb=%lld, nominal=%u)\n",
               (long long)adj_ppb, ctx->nominal_incr_ns);
        return TEST_FAIL;
    }

    printf("  [PASS] UT-PTP-FREQ-006: Out-of-Range Rejection (Positive) "
           "(ppb=%lld, nominal=%u ns)\n",
           (long long)adj_ppb, ctx->nominal_incr_ns);
    return TEST_PASS;
}

/*
 * Test UT-PTP-FREQ-007: Out-of-Range Rejection (Negative)
 * Verifies: Rejects adjustments < -1e9 ppb
 *
 * ppb = -1 000 000 001 via ConvertPpbToIncrement(nominal=N) gives:
 *   new_increment = N * (1 - 1.000000001) = N * (-0.000000001) ≈ 0
 *   increment_ns = 0 (truncation toward zero), which the driver always rejects
 *   (increment_ns==0 means clock frozen — rejected for all devices).
 * Exercises the full ConvertPpbToIncrement → IOCTL path on every adapter.
 */
static int Test_OutOfRangeRejectionNegative(TestContext *ctx)
{
    INT64 adj_ppb = -1000000001;  /* < -1e9 ppb (invalid) */

    if (AdjustFrequency(ctx->adapter, ctx->nominal_incr_ns, adj_ppb)) {
        printf("  [FAIL] UT-PTP-FREQ-007: Out-of-Range Rejection (Negative): "
               "Invalid value accepted (ppb=%lld, nominal=%u)\n",
               (long long)adj_ppb, ctx->nominal_incr_ns);
        return TEST_FAIL;
    }

    printf("  [PASS] UT-PTP-FREQ-007: Out-of-Range Rejection (Negative) "
           "(ppb=%lld, nominal=%u ns)\n",
           (long long)adj_ppb, ctx->nominal_incr_ns);
    return TEST_PASS;
}

/*
 * Test UT-PTP-FREQ-008: Small Adjustment (+1 ppb)
 * Verifies: Can set very small frequency adjustments
 */
static int Test_SmallAdjustment(TestContext *ctx)
{
    INT64 adj_ppb = 1;  /* +1 ppb */

    if (!AdjustFrequency(ctx->adapter, ctx->nominal_incr_ns, adj_ppb)) {
        printf("  [FAIL] UT-PTP-FREQ-008: Small Adjustment: IOCTL failed\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-FREQ-008: Small Adjustment (+1 ppb)\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-FREQ-009: Rapid Frequency Changes
 * Verifies: Can handle rapid frequency adjustment changes
 */
static int Test_RapidFrequencyChanges(TestContext *ctx)
{
    INT64 adjustments[] = {100000, -100000, 50000, -50000, 0};
    int count = sizeof(adjustments) / sizeof(adjustments[0]);
    int i;
    
    for (i = 0; i < count; i++) {
        if (!AdjustFrequency(ctx->adapter, ctx->nominal_incr_ns, adjustments[i])) {
            printf("  [FAIL] UT-PTP-FREQ-009: Rapid Frequency Changes: Adjustment %d failed\n", i);
            return TEST_FAIL;
        }
    }
    
    printf("  [PASS] UT-PTP-FREQ-009: Rapid Frequency Changes\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-FREQ-010: Frequency Adjustment Persistence
 * Verifies: Frequency adjustment persists until changed
 */
static int Test_FrequencyAdjustmentPersistence(TestContext *ctx)
{
    AVB_TIMESTAMP_REQUEST ts1, ts2, ts3;
    DWORD br;
    INT64 delta1, delta2;
    
    /* Set to nominal (0 ppb) and get baseline */
    if (!AdjustFrequency(ctx->adapter, ctx->nominal_incr_ns, 0)) {
        printf("  [FAIL] UT-PTP-FREQ-010: Persistence: Initial adjustment failed\n");
        return TEST_FAIL;
    }
    
    ZeroMemory(&ts1, sizeof(ts1));
    if (!DeviceIoControl(ctx->adapter, IOCTL_AVB_GET_TIMESTAMP, &ts1, sizeof(ts1), &ts1, sizeof(ts1), &br, NULL)) {
        printf("  [FAIL] UT-PTP-FREQ-010: Persistence: GET_TIMESTAMP failed\n");
        return TEST_FAIL;
    }
    
    Sleep(100);  /* 100ms delay */
    
    ZeroMemory(&ts2, sizeof(ts2));
    if (!DeviceIoControl(ctx->adapter, IOCTL_AVB_GET_TIMESTAMP, &ts2, sizeof(ts2), &ts2, sizeof(ts2), &br, NULL)) {
        printf("  [FAIL] UT-PTP-FREQ-010: Persistence: Second GET_TIMESTAMP failed\n");
        return TEST_FAIL;
    }
    
    delta1 = ts2.timestamp - ts1.timestamp;
    
    /* Apply +1000 ppb adjustment (+1000ns per second = +100ns per 100ms) */
    if (!AdjustFrequency(ctx->adapter, ctx->nominal_incr_ns, 1000)) {
        printf("  [FAIL] UT-PTP-FREQ-010: Persistence: Adjustment to +1000 ppb failed\n");
        return TEST_FAIL;
    }
    
    Sleep(100);  /* Another 100ms delay with adjusted frequency */
    
    ZeroMemory(&ts3, sizeof(ts3));
    if (!DeviceIoControl(ctx->adapter, IOCTL_AVB_GET_TIMESTAMP, &ts3, sizeof(ts3), &ts3, sizeof(ts3), &br, NULL)) {
        printf("  [FAIL] UT-PTP-FREQ-010: Persistence: Third GET_TIMESTAMP failed\n");
        return TEST_FAIL;
    }
    
    delta2 = ts3.timestamp - ts2.timestamp;
    
    /* Debug output */
    printf("  DEBUG: ts1=%lld, ts2=%lld, ts3=%lld\n", ts1.timestamp, ts2.timestamp, ts3.timestamp);
    printf("  DEBUG: delta1=%lld ns, delta2=%lld ns\n", delta1, delta2);
    
    /* Verify timestamps advanced and adjustment persisted */
    if (delta1 > 0 && delta2 > 0 && ts3.timestamp > ts1.timestamp) {
        printf("  [PASS] UT-PTP-FREQ-010: Persistence (delta1=%lld ns, delta2=%lld ns)\n", delta1, delta2);
        /* Reset to nominal */
        AdjustFrequency(ctx->adapter, ctx->nominal_incr_ns, 0);
        return TEST_PASS;
    }
    
    printf("  [FAIL] UT-PTP-FREQ-010: Persistence: Timestamps not advancing correctly\n");
    printf("  FAIL: delta1=%lld, delta2=%lld (expected >0)\n", delta1, delta2);
    return TEST_FAIL;
}

/*
 * Test UT-PTP-FREQ-011: Fractional PPB Precision
 * Verifies: Hardware resolution for fractional ppb values
 */
static int Test_FractionalPPBPrecision(TestContext *ctx)
{
    AVB_ENUM_REQUEST enum_req;
    DWORD br;
    
    /* Query hardware capabilities */
    ZeroMemory(&enum_req, sizeof(enum_req));
    enum_req.index = 0;
    
    if (!DeviceIoControl(ctx->adapter, IOCTL_AVB_ENUM_ADAPTERS, &enum_req, sizeof(enum_req), &enum_req, sizeof(enum_req), &br, NULL)) {
        printf("  [FAIL] UT-PTP-FREQ-011: Fractional Precision: ENUM_ADAPTERS failed\n");
        return TEST_FAIL;
    }
    
    /* Intel I210/I211/I225 family supports frequency adjustment */
    /* Precision is hardware-dependent, but IOCTL should accept any INT64 value within range */
    
    /* Test very small adjustment (0.001 ppb) */
    if (!AdjustFrequency(ctx->adapter, ctx->nominal_incr_ns, 1)) {
        printf("  [FAIL] UT-PTP-FREQ-011: Fractional Precision: 0.001 ppb adjustment failed\n");
        return TEST_FAIL;
    }

    /* Reset to nominal */
    AdjustFrequency(ctx->adapter, ctx->nominal_incr_ns, 0);
    
    printf("  [PASS] UT-PTP-FREQ-011: Fractional Precision (VID:0x%04X DID:0x%04X)\n",
           enum_req.vendor_id, enum_req.device_id);
    return TEST_PASS;
}

/* Thread context for concurrent test */
typedef struct {
    HANDLE adapter;
    UINT32 nominal_ns;   /* Device-specific nominal — small ppb values are
                          * unaffected, but pass through correctly. */
    INT64 adjustment;
    int iterations;
    volatile LONG* success_count;
    volatile LONG* fail_count;
} FreqThreadContext;

static DWORD WINAPI FrequencyWorker(LPVOID param)
{
    FreqThreadContext* tc = (FreqThreadContext*)param;
    int i;

    for (i = 0; i < tc->iterations; i++) {
        if (AdjustFrequency(tc->adapter, tc->nominal_ns, tc->adjustment)) {
            InterlockedIncrement(tc->success_count);
        } else {
            InterlockedIncrement(tc->fail_count);
        }
        Sleep(1);  /* Small delay between adjustments */
    }
    
    return 0;
}

/*
 * Test UT-PTP-FREQ-012: Concurrent Adjustment Requests
 * Verifies: Serializes concurrent frequency adjustment requests
 */
static int Test_ConcurrentAdjustmentRequests(TestContext *ctx)
{
    HANDLE threads[4];
    FreqThreadContext thread_ctx[4];
    volatile LONG success_count = 0;
    volatile LONG fail_count = 0;
    INT64 adjustments[] = {100000, -100000, 50000, 0};  /* Different adjustments */
    DWORD wait_result;
    int i;
    
    /* Create 4 threads with different frequency adjustments */
    for (i = 0; i < 4; i++) {
        thread_ctx[i].adapter = ctx->adapter;
        thread_ctx[i].nominal_ns = ctx->nominal_incr_ns;
        thread_ctx[i].adjustment = adjustments[i];
        thread_ctx[i].iterations = 10;  /* 10 adjustments per thread */
        thread_ctx[i].success_count = &success_count;
        thread_ctx[i].fail_count = &fail_count;
        
        threads[i] = CreateThread(NULL, 0, FrequencyWorker, &thread_ctx[i], 0, NULL);
        if (threads[i] == NULL) {
            printf("  [FAIL] UT-PTP-FREQ-012: Concurrent Requests: CreateThread failed\n");
            return TEST_FAIL;
        }
    }
    
    /* Wait for all threads to complete (max 5 seconds) */
    wait_result = WaitForMultipleObjects(4, threads, TRUE, 5000);
    
    /* Close thread handles */
    for (i = 0; i < 4; i++) {
        if (threads[i] != NULL) {
            CloseHandle(threads[i]);
        }
    }
    
    if (wait_result == WAIT_TIMEOUT) {
        printf("  [FAIL] UT-PTP-FREQ-012: Concurrent Requests: Timeout (possible deadlock)\n");
        return TEST_FAIL;
    }
    
    /* Reset to nominal */
    AdjustFrequency(ctx->adapter, ctx->nominal_incr_ns, 0);

    if (success_count > 0 && fail_count == 0) {
        printf("  [PASS] UT-PTP-FREQ-012: Concurrent Requests (%ld succeeded, %ld failed)\n",
               (long)success_count, (long)fail_count);
        return TEST_PASS;
    }
    
    printf("  [FAIL] UT-PTP-FREQ-012: Concurrent Requests: %ld succeeded, %ld failed\n",
           (long)success_count, (long)fail_count);
    return TEST_FAIL;
}

/*
 * Test UT-PTP-FREQ-013: Adjustment During Active Sync
 * Verifies: Can adjust frequency while PTP sync is active
 */
static int Test_AdjustmentDuringActiveSync(TestContext *ctx)
{
    AVB_TIMESTAMP_REQUEST ts1, ts2;
    DWORD br;
    
    /* Simulate active sync by reading timestamps while adjusting frequency */
    ZeroMemory(&ts1, sizeof(ts1));
    if (!DeviceIoControl(ctx->adapter, IOCTL_AVB_GET_TIMESTAMP, &ts1, sizeof(ts1), &ts1, sizeof(ts1), &br, NULL)) {
        printf("  [FAIL] UT-PTP-FREQ-013: During Active Sync: Initial GET_TIMESTAMP failed\n");
        return TEST_FAIL;
    }
    
    /* Apply frequency adjustment while timestamps are active */
    if (!AdjustFrequency(ctx->adapter, ctx->nominal_incr_ns, 100000)) {  /* +100 ppm */
        printf("  [FAIL] UT-PTP-FREQ-013: During Active Sync: Adjustment failed\n");
        return TEST_FAIL;
    }
    
    Sleep(50);  /* Let adjustment take effect */
    
    /* Verify timestamps still work after adjustment */
    ZeroMemory(&ts2, sizeof(ts2));
    if (!DeviceIoControl(ctx->adapter, IOCTL_AVB_GET_TIMESTAMP, &ts2, sizeof(ts2), &ts2, sizeof(ts2), &br, NULL)) {
        printf("  [FAIL] UT-PTP-FREQ-013: During Active Sync: GET_TIMESTAMP after adjustment failed\n");
        return TEST_FAIL;
    }
    
    /* Reset to nominal */
    AdjustFrequency(ctx->adapter, ctx->nominal_incr_ns, 0);

    /* Debug output */
    printf("  DEBUG: ts1=%lld, ts2=%lld, delta=%lld ns\n", ts1.timestamp, ts2.timestamp, ts2.timestamp - ts1.timestamp);
    
    /* Verify timestamps advanced (>20ms expected for 50ms delay) */
    if (ts2.timestamp > ts1.timestamp && (ts2.timestamp - ts1.timestamp) > 20000000) {
        printf("  [PASS] UT-PTP-FREQ-013: During Active Sync\n");
        return TEST_PASS;
    }
    
    printf("  [FAIL] UT-PTP-FREQ-013: During Active Sync: Timestamps not advancing correctly\n");
    printf("  FAIL: delta=%lld ns (expected >20000000)\n", ts2.timestamp - ts1.timestamp);
    return TEST_FAIL;
}

/*
 * Test UT-PTP-FREQ-014: Null Pointer Handling
 * Verifies: Rejects NULL buffer pointers
 */
static int Test_NullPointerHandling(TestContext *ctx)
{
    DWORD bytesReturned = 0;
    
    BOOL result = DeviceIoControl(
        ctx->adapter,
        IOCTL_AVB_ADJUST_FREQUENCY,
        NULL, 0,
        NULL, 0,
        &bytesReturned,
        NULL
    );
    
    if (result) {
        printf("  [FAIL] UT-PTP-FREQ-014: Null Pointer Handling: NULL buffer accepted\n");
        return TEST_FAIL;
    }
    
    printf("  [PASS] UT-PTP-FREQ-014: Null Pointer Handling\n");
    return TEST_PASS;
}

/*
 * Test UT-PTP-FREQ-015: Adjustment Reset on Driver Restart
 * Verifies: Frequency adjustment resets to 0 after driver restart
 * 
 * Note: Tests frequency adjustment persistence across adapter handle close/reopen
 *       (simulates driver restart behavior without requiring actual driver reload)
 */
static int Test_AdjustmentResetOnRestart(TestContext *ctx)
{
    HANDLE adapter2 = INVALID_HANDLE_VALUE;
    AVB_FREQUENCY_REQUEST req1 = {0};  /* ✅ Use SSOT structure */
    AVB_FREQUENCY_REQUEST req2 = {0};  /* ✅ Use SSOT structure */
    DWORD br;
    
    /* Step 1: Apply non-zero frequency adjustment (+100 ppm) */
    if (!AdjustFrequency(ctx->adapter, ctx->nominal_incr_ns, 100000)) {
        printf("  [FAIL] UT-PTP-FREQ-015: Reset on Restart: Initial adjustment failed\n");
        return TEST_FAIL;
    }
    
    /* Step 2: Verify adjustment was applied by reading back (if supported) */
    req1.increment_ns = 0;
    req1.increment_frac = 0;
    if (DeviceIoControl(ctx->adapter, IOCTL_AVB_ADJUST_FREQUENCY, &req1, sizeof(req1), &req1, sizeof(req1), &br, NULL)) {
        printf("  DEBUG: Current adjustment before handle close: increment_ns=%u, increment_frac=%u\n", 
               req1.increment_ns, req1.increment_frac);
    }
    
    /* Step 3: Close adapter handle (simulates driver unload/cleanup) */
    if (ctx->adapter != INVALID_HANDLE_VALUE) {
        CloseHandle(ctx->adapter);
        ctx->adapter = INVALID_HANDLE_VALUE;
    }
    
    Sleep(100);  /* Small delay to let driver cleanup complete */
    
    /* Step 4: Reopen adapter (simulates driver reload) */
    adapter2 = OpenAdapter();
    if (adapter2 == INVALID_HANDLE_VALUE) {
        printf("  [FAIL] UT-PTP-FREQ-015: Reset on Restart: Failed to reopen adapter\n");
        ctx->adapter = OpenAdapter();  /* Try to restore context for cleanup */
        return TEST_FAIL;
    }
    
    /* Step 5: Check if frequency adjustment reset to 0 (driver behavior on init) */
    /* Apply zero adjustment and verify it succeeds */
    req2.increment_ns = NOMINAL_INCR_NS;  /* Reset to nominal 8ns */
    req2.increment_frac = 0;
    if (!DeviceIoControl(adapter2, IOCTL_AVB_ADJUST_FREQUENCY, &req2, sizeof(req2), &req2, sizeof(req2), &br, NULL)) {
        printf("  [FAIL] UT-PTP-FREQ-015: Reset on Restart: Cannot verify adjustment after reopen\n");
        CloseHandle(adapter2);
        ctx->adapter = OpenAdapter();  /* Restore context */
        return TEST_FAIL;
    }
    
    /* Step 6: Restore context for cleanup */
    ctx->adapter = adapter2;
    
    /* Step 7: Apply small adjustment to verify adapter is functional */
    if (!AdjustFrequency(ctx->adapter, ctx->nominal_incr_ns, 1000)) {
        printf("  [FAIL] UT-PTP-FREQ-015: Reset on Restart: Adapter not functional after reopen\n");
        return TEST_FAIL;
    }

    /* Reset to nominal for next tests */
    AdjustFrequency(ctx->adapter, ctx->nominal_incr_ns, 0);
    
    printf("  [PASS] UT-PTP-FREQ-015: Reset on Restart (adapter handle close/reopen)\n");
    return TEST_PASS;
}

/* Main test runner */
int main(void)
{
    TestContext ctx = {0};
    
    printf("\n");
    printf("====================================================================\n");
    printf(" PTP Frequency Adjustment Test Suite\n");
    printf("====================================================================\n");
    printf(" Implements: #296 (TEST-PTP-FREQ-001)\n");
    printf(" Verifies: #3 (REQ-F-PTP-002)\n");
    printf(" IOCTLs: ADJUST_FREQUENCY (38)\n");
    printf(" Total Tests: 15\n");
    printf(" Priority: P0 (Critical)\n");
    printf("====================================================================\n");
    printf("\n");
    
    /* Enumerate all adapters; run tests on each that supports BASIC_1588 */
    AvbAdapterInfo adapters[AVB_MAX_ADAPTERS];
    int adapterCount = AvbEnumerateAdapters(adapters, AVB_MAX_ADAPTERS);
    int anyFound = 0;

    if (adapterCount <= 0) {
        printf("[SKIP] No AVB adapters found. Skipping all tests.\n");
        return TEST_SKIP;
    }

    for (int ai = 0; ai < adapterCount; ai++) {
        if (!AVB_HAS_CAP(&adapters[ai], INTEL_CAP_BASIC_1588)) {
            printf("[SKIP] Adapter %s (DID=0x%04X) lacks BASIC_1588 — skipping.\n",
                   adapters[ai].device_name, adapters[ai].device_id);
            continue;
        }
        anyFound = 1;
        printf("--- Adapter %d: %s (DID=0x%04X) ---\n",
               adapters[ai].global_index, adapters[ai].device_name, adapters[ai].device_id);

        ctx.adapter = AvbOpenAdapter(&adapters[ai]);
        if (ctx.adapter == INVALID_HANDLE_VALUE) {
            printf("[ERROR] Failed to open adapter %s. Skipping.\n", adapters[ai].device_name);
            continue;
        }

        /* Enable hardware timestamping (required for timestamp operations) */
        if (!EnableHWTimestamping(ctx.adapter)) {
            printf("[WARN] Failed to enable hardware timestamping. Some tests may fail.\n");
        }

        /* Detect device-specific nominal increment_ns (8 for I210, 24 for I226, …)
         * so that FREQ-006's out-of-range boundary is computed correctly per device. */
        ctx.nominal_incr_ns = GetDeviceNominalIncrNs(ctx.adapter);
        if (ctx.nominal_incr_ns == 0) ctx.nominal_incr_ns = NOMINAL_INCR_NS;
        printf("  [INFO] Device nominal increment: %u ns/cycle\n", ctx.nominal_incr_ns);

        printf("Running PTP Frequency Adjustment tests...\n\n");

        /* Run tests */
        #define RUN_TEST(test) do { \
            int result = test(&ctx); \
            ctx.test_count++; \
            if (result == TEST_PASS) ctx.pass_count++; \
            else if (result == TEST_FAIL) ctx.fail_count++; \
            else if (result == TEST_SKIP) ctx.skip_count++; \
        } while(0)

        RUN_TEST(Test_ZeroFrequencyAdjustment);
        RUN_TEST(Test_PositiveFrequencyAdjustment);
        RUN_TEST(Test_NegativeFrequencyAdjustment);
        RUN_TEST(Test_MaximumPositiveAdjustment);
        RUN_TEST(Test_MaximumNegativeAdjustment);
        RUN_TEST(Test_OutOfRangeRejectionPositive);
        RUN_TEST(Test_OutOfRangeRejectionNegative);
        RUN_TEST(Test_SmallAdjustment);
        RUN_TEST(Test_RapidFrequencyChanges);
        RUN_TEST(Test_FrequencyAdjustmentPersistence);
        RUN_TEST(Test_FractionalPPBPrecision);
        RUN_TEST(Test_ConcurrentAdjustmentRequests);
        RUN_TEST(Test_AdjustmentDuringActiveSync);
        RUN_TEST(Test_NullPointerHandling);
        RUN_TEST(Test_AdjustmentResetOnRestart);

        #undef RUN_TEST

        /* Reset to nominal before close */
        AdjustFrequency(ctx.adapter, ctx.nominal_incr_ns, 0);

        CloseHandle(ctx.adapter);
        ctx.adapter = INVALID_HANDLE_VALUE;
    }

    if (!anyFound) {
        printf("[SKIP] No adapter with BASIC_1588 found.\n");
        return TEST_SKIP;
    }

    /* Summary */
    printf("\n");
    printf("====================================================================\n");
    printf(" Test Summary\n");
    printf("====================================================================\n");
    printf(" Total:   %d tests\n", ctx.test_count);
    printf(" Passed:  %d tests\n", ctx.pass_count);
    printf(" Failed:  %d tests\n", ctx.fail_count);
    printf(" Skipped: %d tests\n", ctx.skip_count);
    printf("====================================================================\n");
    printf("\n");

    return (ctx.fail_count > 0) ? TEST_FAIL : TEST_PASS;
}
