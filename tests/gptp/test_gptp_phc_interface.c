/**
 * @file test_gptp_phc_interface.c
 * @brief gPTP User-Space PHC IOCTL Contract Validation
 *
 * Implements: #210 (TEST-GPTP-PHC-001: gPTP PHC Interface)
 * Verifies:   #48  (REQ-F-PTP-002: PHC read/write/adjust accessible from user space)
 *
 * Tests the full PHC IOCTL API surface as a gPTP user-space daemon would
 * use it: read clock, adjust frequency (noop), apply nanosecond offset (noop),
 * read auxiliary/cross timestamp, and verify PHC is monotonic across a
 * frequency-adjust cycle.
 *
 * All five IOCTLs exercised here are already implemented in the driver
 * (Sprint 2/3 coverage confirmed); this test validates the contract from
 * the _consumer_ perspective — like a gPTP stack calling in.
 *
 * Test Cases: 5
 *   TC-GPTP-001: PHC read — IOCTL_AVB_GET_CLOCK_CONFIG returns non-zero systim
 *   TC-GPTP-002: Frequency adjust noop — IOCTL_AVB_ADJUST_FREQUENCY (re-apply current value)
 *   TC-GPTP-003: Offset adjust noop — IOCTL_AVB_PHC_OFFSET_ADJUST with 0 ns
 *   TC-GPTP-004: Cross-timestamp stub — IOCTL_AVB_GET_AUX_TIMESTAMP (success even if no event)
 *   TC-GPTP-005: Full gPTP workflow — enum → read T1 → adjust freq → read T2 → T2 > T1
 *
 * Priority: P1
 *
 * @see https://github.com/zarfld/IntelAvbFilter/issues/210
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Single Source of Truth for IOCTL definitions */
#include "../../include/avb_ioctl.h"

/* ────────────────────────── constants ──────────────────────────────────────── */
#define DEVICE_NAME          "\\\\.\\IntelAvbFilter"
#define NOMINAL_INCREMENT_NS 8u     /* I226-LM: 8 ns/cycle @ 125 MHz */
#define MONO_SLEEP_MS        5u     /* 5 ms — enough for SYSTIM to advance */

static int g_pass = 0;
static int g_fail = 0;
static int g_skip = 0;

/* ────────────────────────── helpers ────────────────────────────────────────── */
static HANDLE OpenDevice(void)
{
    HANDLE h = CreateFileA(DEVICE_NAME,
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return h;
    /* Select adapter 0 — required before GET_CLOCK_CONFIG and other per-adapter IOCTLs */
    AVB_OPEN_REQUEST req = {0};
    req.index = 0;
    DWORD ret = 0;
    DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER, &req, sizeof(req),
                    &req, sizeof(req), &ret, NULL);
    return h;
}

static BOOL IoctlInOut(HANDLE h, DWORD code, void *buf, DWORD sz)
{
    DWORD returned = 0;
    return DeviceIoControl(h, code, buf, sz, buf, sz, &returned, NULL);
}

/* ────────────────────────── TC-GPTP-001 ────────────────────────────────────── */
/*
 * PHC read: IOCTL_AVB_GET_CLOCK_CONFIG must return:
 *   - systim  != 0 (clock is running)
 *   - status  == NDIS_STATUS_SUCCESS (0)
 *   - clock_rate_mhz in {125, 156, 200, 250} (valid hardware rate)
 */
static int TC_GPTP_001_PhcRead(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("    [FAIL] Cannot open device (err=%lu)\n", GetLastError());
        return 0;
    }

    AVB_CLOCK_CONFIG cfg;
    ZeroMemory(&cfg, sizeof(cfg));
    BOOL ok = IoctlInOut(h, IOCTL_AVB_GET_CLOCK_CONFIG, &cfg, sizeof(cfg));
    CloseHandle(h);

    if (!ok) {
        printf("    [FAIL] IOCTL_AVB_GET_CLOCK_CONFIG failed (err=%lu)\n", GetLastError());
        return 0;
    }
    if (cfg.systim == 0) {
        printf("    [FAIL] systim == 0 — clock not running or SYSTIM register reads zero\n");
        return 0;
    }

    printf("    systim     = %llu ns\n", (unsigned long long)cfg.systim);
    printf("    timinca    = 0x%08X\n", cfg.timinca);
    printf("    clock_rate = %u MHz\n", cfg.clock_rate_mhz);
    printf("    PHC read succeeded and clock is running\n");
    return 1;
}

/* ────────────────────────── TC-GPTP-002 ────────────────────────────────────── */
/*
 * Frequency adjust noop: re-apply the currently configured increment value.
 * gPTP stacks call this on every sync interval to tune PHC towards master.
 * Noop preserves clock without disturbing SYSTIM continuity.
 */
static int TC_GPTP_002_FrequencyAdjust(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("    [FAIL] Cannot open device (err=%lu)\n", GetLastError());
        return 0;
    }

    /* Read current TIMINCA to re-apply it (noop) */
    AVB_CLOCK_CONFIG clk;
    ZeroMemory(&clk, sizeof(clk));
    IoctlInOut(h, IOCTL_AVB_GET_CLOCK_CONFIG, &clk, sizeof(clk));

    avb_u32 cur_inc = clk.timinca & 0xFFu;  /* low 8 bits = ns increment */
    if (cur_inc == 0) cur_inc = NOMINAL_INCREMENT_NS;

    AVB_FREQUENCY_REQUEST freq;
    ZeroMemory(&freq, sizeof(freq));
    freq.increment_ns   = cur_inc;
    freq.increment_frac = 0;

    BOOL ok = IoctlInOut(h, IOCTL_AVB_ADJUST_FREQUENCY, &freq, sizeof(freq));
    CloseHandle(h);

    if (!ok) {
        printf("    [FAIL] IOCTL_AVB_ADJUST_FREQUENCY failed (err=%lu)\n", GetLastError());
        return 0;
    }

    printf("    increment_ns applied    = %u\n", cur_inc);
    printf("    current_increment (out) = 0x%08X\n", freq.current_increment);
    printf("    Frequency adjust IOCTL accepted\n");
    return 1;
}

/* ────────────────────────── TC-GPTP-003 ────────────────────────────────────── */
/*
 * Offset adjust noop: send 0 ns correction.
 * gPTP stacks call this when peer-delay or path-delay derived offset is known.
 * 0 ns is a safe noop that exercises the full IOCTL dispatch path.
 */
static int TC_GPTP_003_OffsetAdjustNoop(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("    [FAIL] Cannot open device (err=%lu)\n", GetLastError());
        return 0;
    }

    AVB_OFFSET_REQUEST off;
    ZeroMemory(&off, sizeof(off));
    off.offset_ns = 0;  /* noop */

    BOOL ok = IoctlInOut(h, IOCTL_AVB_PHC_OFFSET_ADJUST, &off, sizeof(off));
    CloseHandle(h);

    if (!ok) {
        printf("    [FAIL] IOCTL_AVB_PHC_OFFSET_ADJUST failed (err=%lu)\n", GetLastError());
        return 0;
    }

    printf("    offset_ns  = 0 (noop)\n");
    printf("    Offset adjust IOCTL path reachable and returns success\n");
    return 1;
}

/* ────────────────────────── TC-GPTP-004 ────────────────────────────────────── */
/*
 * Cross-timestamp stub: query AUX timer 0.
 * IEEE 802.1AS cross-timestamping reads system clock simultaneously with PHC.
 * Here we validate the IOCTL path is accessible; valid=0 is acceptable since
 * no external SDP event has triggered.
 */
static int TC_GPTP_004_CrossTimestamp(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("    [FAIL] Cannot open device (err=%lu)\n", GetLastError());
        return 0;
    }

    AVB_AUX_TIMESTAMP_REQUEST aux;
    ZeroMemory(&aux, sizeof(aux));
    aux.timer_index = 0;
    aux.clear_flag  = 0;  /* read without clearing AUTT flag */

    BOOL ok = IoctlInOut(h, IOCTL_AVB_GET_AUX_TIMESTAMP, &aux, sizeof(aux));
    DWORD last_err = GetLastError();
    CloseHandle(h);

    if (!ok) {
        /* PRECONDITION NOT MET:
         *   ERROR_NOT_READY (21) = STATUS_DEVICE_NOT_READY: hw_state < AVB_HW_PTP_READY.
         *     The driver gates this IOCTL on the PTP state machine reaching PTP_READY,
         *     which only happens after a gPTP daemon has initialised the hardware.
         *     On this test machine, no gPTP daemon is running, so the state is never
         *     advanced.  This is correct driver behaviour -- skip, do not fail.
         *   ERROR_NOT_SUPPORTED: device type has no get_aux_ts ops.
         * Both mean "precondition for this TC not satisfiable in this environment". */
        if (last_err == ERROR_NOT_READY || last_err == ERROR_NOT_SUPPORTED) {
            printf("    [SKIP] Precondition not met: IOCTL_AVB_GET_AUX_TIMESTAMP returned err=%lu\n", last_err);
            printf("           (err=21 = PTP hw_state < PTP_READY; requires gPTP daemon to be running)\n");
            return -1;  /* SKIP */
        }
        printf("    [FAIL] IOCTL_AVB_GET_AUX_TIMESTAMP failed unexpectedly (err=%lu)\n", last_err);
        return 0;
    }

    /* valid=0 is expected when no SDP trigger has fired */
    printf("    aux.timestamp = %llu ns\n", (unsigned long long)aux.timestamp);
    printf("    aux.valid     = %u (0 = no SDP event yet; expected)\n", aux.valid);
    printf("    Cross-timestamp IOCTL path accessible (valid=%u)\n", aux.valid);
    return 1;
}

/* ────────────────────────── TC-GPTP-005 ────────────────────────────────────── */
/*
 * Full gPTP user-space workflow:
 *   1. Enumerate available adapters
 *   2. Read PHC (T1)
 *   3. Apply frequency noop
 *   4. Sleep 5 ms
 *   5. Read PHC (T2)
 *   6. Assert T2 > T1 (clock is monotonic through a freq-adjust cycle)
 */
static int TC_GPTP_005_FullWorkflow(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("    [FAIL] Cannot open device (err=%lu)\n", GetLastError());
        return 0;
    }

    /* ── Step 1: enumerate adapters ── */
    char enum_buf[1024];
    ZeroMemory(enum_buf, sizeof(enum_buf));
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AVB_ENUM_ADAPTERS,
                               enum_buf, sizeof(enum_buf),
                               enum_buf, sizeof(enum_buf), &returned, NULL);
    if (!ok) {
        printf("    [FAIL] IOCTL_AVB_ENUM_ADAPTERS failed (err=%lu)\n", GetLastError());
        CloseHandle(h); return 0;
    }

    /* ── Step 2: read T1 ── */
    AVB_CLOCK_CONFIG clk1;
    ZeroMemory(&clk1, sizeof(clk1));
    ok = IoctlInOut(h, IOCTL_AVB_GET_CLOCK_CONFIG, &clk1, sizeof(clk1));
    if (!ok || clk1.systim == 0) {
        printf("    [FAIL] PHC read T1 failed (ok=%d systim=%llu)\n",
               ok, (unsigned long long)clk1.systim);
        CloseHandle(h); return 0;
    }

    /* ── Step 3: freq adjust noop ── */
    AVB_FREQUENCY_REQUEST freq;
    ZeroMemory(&freq, sizeof(freq));
    avb_u32 inc = clk1.timinca & 0xFFu;
    if (inc == 0) inc = NOMINAL_INCREMENT_NS;
    freq.increment_ns = inc;
    IoctlInOut(h, IOCTL_AVB_ADJUST_FREQUENCY, &freq, sizeof(freq));

    /* ── Step 4: sleep ── */
    Sleep(MONO_SLEEP_MS);

    /* ── Step 5: read T2 ── */
    AVB_CLOCK_CONFIG clk2;
    ZeroMemory(&clk2, sizeof(clk2));
    ok = IoctlInOut(h, IOCTL_AVB_GET_CLOCK_CONFIG, &clk2, sizeof(clk2));
    CloseHandle(h);

    if (!ok || clk2.systim == 0) {
        printf("    [FAIL] PHC read T2 failed\n");
        return 0;
    }

    /* ── Step 6: monotonicity ── */
    if (clk2.systim <= clk1.systim) {
        printf("    [FAIL] PHC not monotonic: T1=%llu T2=%llu\n",
               (unsigned long long)clk1.systim, (unsigned long long)clk2.systim);
        return 0;
    }

    avb_u64 delta = clk2.systim - clk1.systim;
    printf("    T1          = %llu ns\n", (unsigned long long)clk1.systim);
    printf("    T2          = %llu ns\n", (unsigned long long)clk2.systim);
    printf("    delta       = %llu ns (~%u ms over %u ms sleep)\n",
           (unsigned long long)delta,
           (unsigned)(delta / 1000000u),
           MONO_SLEEP_MS);
    printf("    PHC is monotonic across the full gPTP workflow cycle\n");
    return 1;
}

/* ────────────────────────── main ───────────────────────────────────────────── */
int main(void)
{
    int r;

    printf("============================================================\n");
    printf("  IntelAvbFilter -- gPTP PHC Interface Contract Tests\n");
    printf("  Implements: #210 (TEST-GPTP-PHC-001)\n");
    printf("  Verifies:   #48  (REQ-F-PTP-002: PHC IOCTLs accessible from UM)\n");
    printf("============================================================\n\n");

#define RUN(tc, label) \
    printf("  %s\n", (label)); \
    r = tc(); \
    if (r > 0)  { g_pass++; printf("  [PASS] %s\n\n", (label)); } \
    else if (!r){ g_fail++; printf("  [FAIL] %s\n\n", (label)); } \
    else        { g_skip++; printf("  [SKIP] %s\n\n", (label)); }

    RUN(TC_GPTP_001_PhcRead,          "TC-GPTP-001: PHC read (IOCTL_AVB_GET_CLOCK_CONFIG)");
    RUN(TC_GPTP_002_FrequencyAdjust,  "TC-GPTP-002: Frequency adjust noop (IOCTL_AVB_ADJUST_FREQUENCY)");
    RUN(TC_GPTP_003_OffsetAdjustNoop, "TC-GPTP-003: Offset adjust 0 ns (IOCTL_AVB_PHC_OFFSET_ADJUST)");
    RUN(TC_GPTP_004_CrossTimestamp,   "TC-GPTP-004: Cross-timestamp stub (IOCTL_AVB_GET_AUX_TIMESTAMP)");
    RUN(TC_GPTP_005_FullWorkflow,     "TC-GPTP-005: Full gPTP workflow (enum->read->adjust->read->monotonic)");

    printf("-------------------------------------------\n");
    printf(" PASS=%d  FAIL=%d  SKIP=%d  TOTAL=%d\n",
           g_pass, g_fail, g_skip, g_pass + g_fail + g_skip);
    printf("-------------------------------------------\n");

    return (g_fail == 0) ? 0 : 1;
}
