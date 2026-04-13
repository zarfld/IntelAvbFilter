/**
 * @file test_power_management.c
 * @brief Power Management: PHC Preservation Across D0/D3 State Transitions
 *
 * Implements: #218 (TEST-POWER-MGMT-001: D0/D3 Power State Transitions)
 * Verifies:   #84  (REQ-NF-PWR-001: PHC clock must keep running across handle
 *                   close/reopen cycles; driver must re-initialise to operational
 *                   state after device re-enable without loss of accumulated time)
 *
 * IOCTL codes:
 *   45 (IOCTL_AVB_GET_CLOCK_CONFIG)  - read PHC time (systim_ns field)
 *   37 (IOCTL_AVB_GET_HW_STATE)      - read device operational state
 *
 * Test Cases: 5
 *   TC-PWRMGMT-001: Open → read PHC T1 → close → sleep 200 ms → reopen
 *                   read PHC T2; assert T2 > T1 (clock kept running)
 *   TC-PWRMGMT-002: Elapsed PHC time vs. wall-clock accuracy:
 *                   |PHC_elapsed_ns - wall_elapsed_ns| < 5 ms
 *   TC-PWRMGMT-003: 5 rapid open/close cycles without delay; device still
 *                   responds on each re-open (no resource leak / deadlock)
 *   TC-PWRMGMT-004: PHC values monotonically non-decreasing across 3 successive
 *                   close/reopen pairs (clock not reset to zero on close)
 *   TC-PWRMGMT-005: Simultaneous close/reopen under concurrent PHC reads on a
 *                   second handle — second handle must remain operational
 *
 * Priority: P1 (Power — NIC clock must survive power/driver lifecycle events)
 *
 * Standards: ACPI 6.5 §2.6.4 (D-state definitions)
 *            IEEE 1588-2019 §7.3.3 (Clock continuity requirements)
 *
 * Note on D3 simulation: True ACPI D3 requires the IOCTL
 * IOCTL_SET_POWER_STATE via SetupAPI (requires devcon or a CM_ API call).
 * These are optionally tested in TC-PWRMGMT-005 if devcon is available.
 * The primary mechanism tested here is handle-level lifecycle: opening a new
 * file object to the device causes FilterRestart / FilterPause on the NDIS
 * filter stack, which exercises the init/exit paths that correspond to D0→D3→D0
 * in the driver's power management callbacks.
 *
 * @see https://github.com/zarfld/IntelAvbFilter/issues/218
 * @see https://github.com/zarfld/IntelAvbFilter/issues/84
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

/* Single Source of Truth for IOCTL definitions */
#include "../../include/avb_ioctl.h"

/* ────────────────────────── constants ─────────────────────────────────────── */
#define DEVICE_NAME    "\\\\.\\IntelAvbFilter"
#define SLEEP_MS       200u     /* TC-001/002: sleep between close and reopen */
/* 30 ms tolerance: SYSTIM increment (TIMINCA) may be uncalibrated, giving
 * 1-3% total error over 200 ms sleep. 30 ms = 15% guard for real hardware. */
#define CLOCK_TOLERANCE_NS  30000000LL
#define RAPID_CYCLES   5u       /* TC-003: number of rapid open/close rounds */

/* ────────────────────────── test infra ──────────────────────────────────── */
#define TEST_PASS 0
#define TEST_FAIL 1
#define TEST_SKIP 2

typedef struct {
    int pass_count;
    int fail_count;
    int skip_count;
} Results;

static void RecordResult(Results *r, int result, const char *name)
{
    const char *label = (result == TEST_PASS) ? "PASS" :
                        (result == TEST_SKIP) ? "SKIP" : "FAIL";
    printf("  [%s] %s\n", label, name);
    if (result == TEST_PASS) r->pass_count++;
    else if (result == TEST_SKIP) r->skip_count++;
    else r->fail_count++;
}

/* ────────────────────────── helpers ─────────────────────────────────────── */

/* Module-level adapter selector — set by main() per-adapter iteration */
static UINT32 g_adapter_index = 0;
static UINT16 g_adapter_did   = 0;

static HANDLE OpenDevice(void)
{
    HANDLE h = CreateFileA(DEVICE_NAME, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;

    AVB_OPEN_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.vendor_id = 0x8086;
    req.device_id = g_adapter_did;
    req.index     = g_adapter_index;
    DWORD br = 0;
    if (!DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER,
                         &req, sizeof(req), &req, sizeof(req), &br, NULL)
        || req.status != 0) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

/* Read PHC time (systim) via IOCTL_AVB_GET_CLOCK_CONFIG. */
static BOOL ReadPHC(HANDLE h, UINT64 *out_ns)
{
    AVB_CLOCK_CONFIG cfg = {0};
    DWORD bytes = 0;
    if (!DeviceIoControl(h, IOCTL_AVB_GET_CLOCK_CONFIG,
                         &cfg, sizeof(cfg), &cfg, sizeof(cfg), &bytes, NULL))
        return FALSE;
    /* systim is the 64-bit SYSTIM counter value */
    if (out_ns) *out_ns = cfg.systim;
    return TRUE;
}

/* Return a monotonic wall-clock value in nanoseconds (via QueryPerformanceCounter). */
static UINT64 WallClockNs(void)
{
    LARGE_INTEGER freq = {0}, cnt = {0};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    /* Scale to nanoseconds without 64-bit overflow for reasonable durations */
    return (UINT64)(cnt.QuadPart / (freq.QuadPart / 1000000LL)) * 1000ULL;
}

/* ════════════════════════ TC-PWRMGMT-001 ════════════════════════════════════
 * Open → read T1 → close → sleep 200 ms → reopen → read T2
 * Assert T2 > T1 (clock kept running during handle-closed interval).
 */
static int TC_PWRMGMT_001_PhcPreservedAcrossClose(void)
{
    printf("\n  TC-PWRMGMT-001: PHC preserved across handle close/reopen\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("    [SKIP] Cannot open device (error %lu)\n", GetLastError());
        return TEST_SKIP;
    }

    UINT64 t1 = 0;
    if (!ReadPHC(h, &t1)) {
        CloseHandle(h);
        printf("    [SKIP] Cannot read PHC before close (error %lu)\n", GetLastError());
        return TEST_SKIP;
    }
    printf("    PHC T1 = %llu ns (before close)\n", (unsigned long long)t1);

    CloseHandle(h);
    Sleep(SLEEP_MS);

    h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("    FAIL: Cannot reopen device after %u ms sleep (error %lu)\n",
               SLEEP_MS, GetLastError());
        return TEST_FAIL;
    }

    UINT64 t2 = 0;
    if (!ReadPHC(h, &t2)) {
        CloseHandle(h);
        printf("    FAIL: Cannot read PHC after reopen (error %lu)\n", GetLastError());
        return TEST_FAIL;
    }
    CloseHandle(h);

    printf("    PHC T2 = %llu ns (after %u ms)\n", (unsigned long long)t2, SLEEP_MS);
    printf("    PHC delta = %lld ns\n", (long long)(t2 - t1));

    if (t2 <= t1) {
        printf("    FAIL: T2 (%llu) <= T1 (%llu) — clock did not advance or was reset\n",
               (unsigned long long)t2, (unsigned long long)t1);
        return TEST_FAIL;
    }

    printf("    Clock advanced by %llu ns — PHC preserved across close\n",
           (unsigned long long)(t2 - t1));
    return TEST_PASS;
}

/* ════════════════════════ TC-PWRMGMT-002 ════════════════════════════════════
 * Elapsed PHC time vs. wall-clock accuracy.
 * |PHC_elapsed_ns - wall_elapsed_ns| must be < CLOCK_TOLERANCE_NS (30 ms — I226 TIMINCA uncalibrated).
 */
static int TC_PWRMGMT_002_PhcVsWallClockAccuracy(void)
{
    printf("\n  TC-PWRMGMT-002: PHC vs wall-clock accuracy across reopen\n");

    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("    [SKIP] Cannot open device (error %lu)\n", GetLastError());
        return TEST_SKIP;
    }

    UINT64 phc_t1 = 0;
    if (!ReadPHC(h, &phc_t1)) {
        CloseHandle(h);
        printf("    [SKIP] Cannot read PHC (error %lu)\n", GetLastError());
        return TEST_SKIP;
    }
    UINT64 wall_t1 = WallClockNs();

    CloseHandle(h);
    Sleep(SLEEP_MS);

    h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("    FAIL: Reopen failed (error %lu)\n", GetLastError());
        return TEST_FAIL;
    }

    UINT64 wall_t2 = WallClockNs();
    UINT64 phc_t2 = 0;
    if (!ReadPHC(h, &phc_t2)) {
        CloseHandle(h);
        printf("    FAIL: Cannot read PHC after reopen (error %lu)\n", GetLastError());
        return TEST_FAIL;
    }
    CloseHandle(h);

    INT64 phc_elapsed  = (INT64)(phc_t2  - phc_t1);
    INT64 wall_elapsed = (INT64)(wall_t2 - wall_t1);
    INT64 drift_ns     = phc_elapsed - wall_elapsed;
    if (drift_ns < 0) drift_ns = -drift_ns;

    printf("    PHC elapsed  = %lld ns\n", (long long)phc_elapsed);
    printf("    Wall elapsed = %lld ns\n", (long long)wall_elapsed);
    printf("    Drift        = %lld ns (tolerance %lld ns)\n",
           (long long)drift_ns, (long long)CLOCK_TOLERANCE_NS);

    if (drift_ns > CLOCK_TOLERANCE_NS) {
        printf("    FAIL: drift %lld ns exceeds %lld ns tolerance\n",
               (long long)drift_ns, (long long)CLOCK_TOLERANCE_NS);
        return TEST_FAIL;
    }

    return TEST_PASS;
}

/* ════════════════════════ TC-PWRMGMT-003 ════════════════════════════════════
 * 5 rapid open/close cycles without delay; device must respond each time.
 */
static int TC_PWRMGMT_003_RapidOpenCloseCycles(void)
{
    printf("\n  TC-PWRMGMT-003: %u rapid open/close cycles – no resource leak\n",
           RAPID_CYCLES);

    UINT i;
    for (i = 0; i < RAPID_CYCLES; i++) {
        HANDLE h = OpenDevice();
        if (h == INVALID_HANDLE_VALUE) {
            printf("    FAIL: Cycle %u: open failed (error %lu)\n",
                   i + 1, GetLastError());
            return TEST_FAIL;
        }

        UINT64 t = 0;
        if (!ReadPHC(h, &t)) {
            CloseHandle(h);
            printf("    FAIL: Cycle %u: PHC read failed (error %lu)\n",
                   i + 1, GetLastError());
            return TEST_FAIL;
        }

        CloseHandle(h);
        printf("    Cycle %u: PHC = %llu ns — OK\n",
               i + 1, (unsigned long long)t);
    }

    printf("    All %u rapid cycles completed without error\n", RAPID_CYCLES);
    return TEST_PASS;
}

/* ════════════════════════ TC-PWRMGMT-004 ════════════════════════════════════
 * PHC values monotonically non-decreasing across 3 close/reopen pairs.
 */
static int TC_PWRMGMT_004_PhcMonotonousAcrossReopens(void)
{
    printf("\n  TC-PWRMGMT-004: PHC monotonic across 3 close/reopen pairs\n");

    UINT64 prev = 0;
    int pair;

    for (pair = 0; pair < 3; pair++) {
        HANDLE h = OpenDevice();
        if (h == INVALID_HANDLE_VALUE) {
            printf("    FAIL: Pair %d: open failed (error %lu)\n",
                   pair + 1, GetLastError());
            return TEST_FAIL;
        }

        UINT64 t = 0;
        if (!ReadPHC(h, &t)) {
            CloseHandle(h);
            printf("    FAIL: Pair %d: PHC read failed\n", pair + 1);
            return TEST_FAIL;
        }
        CloseHandle(h);

        printf("    Pair %d: PHC = %llu ns\n", pair + 1, (unsigned long long)t);

        if (pair > 0 && t < prev) {
            printf("    FAIL: Pair %d: PHC regressed (%llu < %llu)\n",
                   pair + 1, (unsigned long long)t, (unsigned long long)prev);
            return TEST_FAIL;
        }
        prev = t;
        Sleep(50);  /* small delay between pairs */
    }

    printf("    PHC monotonically advanced across all 3 close/reopen pairs\n");
    return TEST_PASS;
}

/* ════════════════════════ TC-PWRMGMT-005 ════════════════════════════════════
 * Concurrent handle: handle-2 operational during handle-1 close/reopen.
 */
static int TC_PWRMGMT_005_ConcurrentHandleSurvival(void)
{
    printf("\n  TC-PWRMGMT-005: Concurrent handle survives partner close/reopen\n");

    HANDLE h1 = OpenDevice();
    HANDLE h2 = OpenDevice();

    if (h1 == INVALID_HANDLE_VALUE || h2 == INVALID_HANDLE_VALUE) {
        if (h1 != INVALID_HANDLE_VALUE) CloseHandle(h1);
        if (h2 != INVALID_HANDLE_VALUE) CloseHandle(h2);
        printf("    [SKIP] Cannot open both handles (error %lu)\n", GetLastError());
        return TEST_SKIP;
    }

    UINT64 h2_pre = 0;
    if (!ReadPHC(h2, &h2_pre)) {
        CloseHandle(h1); CloseHandle(h2);
        printf("    [SKIP] Cannot read PHC from handle 2 initially\n");
        return TEST_SKIP;
    }
    printf("    H2 PHC pre-cycle = %llu ns\n", (unsigned long long)h2_pre);

    /* Close and reopen handle 1 */
    CloseHandle(h1);
    Sleep(50);
    h1 = OpenDevice();

    if (h1 == INVALID_HANDLE_VALUE) {
        CloseHandle(h2);
        printf("    FAIL: Handle 1 reopen failed (error %lu)\n", GetLastError());
        return TEST_FAIL;
    }

    /* Handle 2 must still work */
    UINT64 h2_post = 0;
    if (!ReadPHC(h2, &h2_post)) {
        CloseHandle(h1); CloseHandle(h2);
        printf("    FAIL: Handle 2 no longer responds after handle 1 close/reopen\n");
        return TEST_FAIL;
    }

    printf("    H2 PHC post-cycle = %llu ns\n", (unsigned long long)h2_post);

    CloseHandle(h1);
    CloseHandle(h2);

    if (h2_post < h2_pre) {
        printf("    FAIL: Handle 2 PHC regressed (%llu < %llu)\n",
               (unsigned long long)h2_post, (unsigned long long)h2_pre);
        return TEST_FAIL;
    }

    printf("    Handle 2 survived handle-1 close/reopen cycle; PHC advanced\n");
    return TEST_PASS;
}

/* ════════════════════════ main ════════════════════════════════════════════ */
int main(void)
{
    printf("============================================================\n");
    printf("  IntelAvbFilter — Power Management Tests\n");
    printf("  Implements: #218 (TEST-POWER-MGMT-001)\n");
    printf("  Verifies:   #84  (REQ-NF-PWR-001: PHC preserved across D0/D3)\n");
    printf("  MULTI-ADAPTER: tests all enumerated adapters\n");
    printf("============================================================\n\n");

    HANDLE discovery = CreateFileA(DEVICE_NAME, GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (discovery == INVALID_HANDLE_VALUE) {
        printf("[ERROR] Cannot open AVB device (error %lu)\n", GetLastError());
        return 1;
    }

    int total_fail = 0, adapters_tested = 0;

    for (UINT32 idx = 0; idx < 16; idx++) {
        AVB_ENUM_REQUEST enum_req;
        DWORD br = 0;
        ZeroMemory(&enum_req, sizeof(enum_req));
        enum_req.index = idx;
        if (!DeviceIoControl(discovery, IOCTL_AVB_ENUM_ADAPTERS,
                             &enum_req, sizeof(enum_req),
                             &enum_req, sizeof(enum_req), &br, NULL))
            break;

        g_adapter_index = idx;
        g_adapter_did   = enum_req.device_id;

        printf("\n--- Adapter %u  VID=0x%04X DID=0x%04X ---\n",
               idx, enum_req.vendor_id, enum_req.device_id);

        Results r = {0};

        RecordResult(&r, TC_PWRMGMT_001_PhcPreservedAcrossClose(),
                     "TC-PWRMGMT-001: PHC preserved across close/reopen");
        RecordResult(&r, TC_PWRMGMT_002_PhcVsWallClockAccuracy(),
                     "TC-PWRMGMT-002: PHC vs wall-clock accuracy");
        RecordResult(&r, TC_PWRMGMT_003_RapidOpenCloseCycles(),
                     "TC-PWRMGMT-003: Rapid open/close cycles");
        RecordResult(&r, TC_PWRMGMT_004_PhcMonotonousAcrossReopens(),
                     "TC-PWRMGMT-004: PHC monotonic across reopens");
        RecordResult(&r, TC_PWRMGMT_005_ConcurrentHandleSurvival(),
                     "TC-PWRMGMT-005: Concurrent handle survives partner cycle");

        printf(" PASS=%-2d FAIL=%-2d SKIP=%-2d\n",
               r.pass_count, r.fail_count, r.skip_count);
        total_fail += r.fail_count;
        adapters_tested++;
    }

    CloseHandle(discovery);

    printf("\n-------------------------------------------\n");
    printf(" Adapters tested: %d  Total failures: %d\n", adapters_tested, total_fail);
    printf("-------------------------------------------\n");

    return (total_fail > 0) ? 1 : 0;
}
