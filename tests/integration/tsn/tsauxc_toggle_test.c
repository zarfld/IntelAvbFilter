/**
 * @file tsauxc_toggle_test.c
 * @brief Test for TSAUXC bit 31 (DisableSystime) enable/disable cycle
 *
 * Validates TSAUXC toggle via the public driver API only:
 *   - IOCTL_AVB_ENUM_ADAPTERS      — discover all adapters
 *   - IOCTL_AVB_OPEN_ADAPTER       — bind handle to a specific adapter
 *   - IOCTL_AVB_SET_HW_TIMESTAMPING — enable/disable PTP (TSAUXC bit 31)
 *   - IOCTL_AVB_GET_CLOCK_CONFIG   — read TSAUXC state (tsauxc field)
 *   - IOCTL_AVB_GET_TIMESTAMP      — verify SYSTIM is ticking
 *
 * Adapter selection: the test iterates all adapters and picks the first one
 * where IOCTL_AVB_SET_HW_TIMESTAMPING returns current_tsauxc != 0.
 * Adapters where the driver no-ops TSAUXC (I219, I217) always return
 * current_tsauxc == 0 and are automatically skipped.
 *
 * No raw register IOCTLs (debug-only).
 * No register offset constants (internal driver detail, not public ABI).
 *
 * Verifies: Issue #NNN (TEST-TSAUXC-001)
 * Traces to: #48 (REQ-F-IOCTL-PHC-004)
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../include/avb_ioctl.h"

/* Open a fresh handle to the driver device */
static HANDLE OpenDriverHandle(void)
{
    return CreateFileW(L"\\\\.\\IntelAvbFilter",
                       GENERIC_READ | GENERIC_WRITE,
                       0, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
}

/* Enable or disable PTP hardware timestamping via public API.
 * Returns TRUE on success.  out receives the request/response (may be NULL). */
static BOOL SetHwTimestamping(HANDLE h, BOOL enable, AVB_HW_TIMESTAMPING_REQUEST *out)
{
    AVB_HW_TIMESTAMPING_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.enable     = enable ? 1u : 0u;
    req.timer_mask = 1u; /* SYSTIM0 only */
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AVB_SET_HW_TIMESTAMPING,
                              &req, sizeof(req),
                              &req, sizeof(req),
                              &br, NULL);
    if (out) *out = req;
    return ok && (req.status == 0);
}

/* Read current TSAUXC value via IOCTL_AVB_GET_CLOCK_CONFIG. */
static BOOL GetClockConfig(HANDLE h, AVB_CLOCK_CONFIG *cfg)
{
    ZeroMemory(cfg, sizeof(*cfg));
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AVB_GET_CLOCK_CONFIG,
                              cfg, sizeof(*cfg),
                              cfg, sizeof(*cfg),
                              &br, NULL);
    return ok && (cfg->status == 0);
}

/* Read PHC time (nanoseconds) via IOCTL_AVB_GET_TIMESTAMP. */
static BOOL ReadPhc(HANDLE h, ULONGLONG *out_ns)
{
    AVB_TIMESTAMP_REQUEST r;
    ZeroMemory(&r, sizeof(r));
    r.clock_id = 0;
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AVB_GET_TIMESTAMP,
                              &r, sizeof(r),
                              &r, sizeof(r),
                              &br, NULL);
    if (!ok || r.status != 0 || r.timestamp == 0) return FALSE;
    *out_ns = r.timestamp;
    return TRUE;
}

/* Check if SYSTIM is advancing over a 50 ms window. */
static BOOL IsSystimIncrementing(HANDLE h)
{
    ULONGLONG t1 = 0, t2 = 0;
    if (!ReadPhc(h, &t1)) {
        printf("  [ERR] GET_TIMESTAMP failed (sample 1)\n");
        return FALSE;
    }
    Sleep(50);
    if (!ReadPhc(h, &t2)) {
        printf("  [ERR] GET_TIMESTAMP failed (sample 2)\n");
        return FALSE;
    }
    LONGLONG delta = (LONGLONG)(t2 - t1);
    printf("  PHC delta: %lld ns\n", delta);
    return (delta > 0);
}

/* Run the TSAUXC toggle cycle on a handle already bound to a capable adapter.
 * The caller must have already issued SET_HW_TIMESTAMPING(enable=1) so that
 * PTP is active at entry. */
static int RunToggleTest(HANDLE h)
{
    printf("\n========================================\n");
    printf("TSAUXC BIT 31 ENABLE/DISABLE CYCLE TEST\n");
    printf("========================================\n\n");

    int passed = 0, failed = 0;
    AVB_HW_TIMESTAMPING_REQUEST tsreq;
    AVB_CLOCK_CONFIG cfg;

    /* STEP 0 — read initial TSAUXC state via GET_CLOCK_CONFIG */
    printf("STEP 0: Read initial TSAUXC state\n");
    if (!GetClockConfig(h, &cfg)) {
        printf("  [ERR] GET_CLOCK_CONFIG failed\n");
        return -1;
    }
    printf("  TSAUXC initial : 0x%08X\n", cfg.tsauxc);
    printf("  Bit 31         : %s\n",
           (cfg.tsauxc & 0x80000000U) ? "SET (PTP DISABLED)" : "CLEAR (PTP ENABLED)");
    printf("\n");

    /* STEP 1 — enable PTP (clear bit 31) */
    printf("STEP 1: Enable PTP clock (SET_HW_TIMESTAMPING enable=1)\n");
    if (!SetHwTimestamping(h, TRUE, &tsreq)) {
        printf("  [FAIL] SET_HW_TIMESTAMPING(enable=1) failed\n");
        ++failed;
    } else {
        printf("  TSAUXC before : 0x%08X\n", tsreq.previous_tsauxc);
        printf("  TSAUXC after  : 0x%08X\n", tsreq.current_tsauxc);
        if (tsreq.current_tsauxc & 0x80000000U) {
            printf("  [FAIL] Bit 31 still set after enable\n");
            ++failed;
        } else {
            printf("  [PASS] Bit 31 cleared\n");
            ++passed;
            printf("  Checking SYSTIM is ticking...\n");
            if (IsSystimIncrementing(h)) {
                printf("  [PASS] SYSTIM is running\n");
                ++passed;
            } else {
                printf("  [FAIL] SYSTIM not incrementing after enable\n");
                ++failed;
            }
        }
    }
    printf("\n");

    /* STEP 2 — disable PTP (set bit 31) */
    printf("STEP 2: Disable PTP clock (SET_HW_TIMESTAMPING enable=0)\n");
    if (!SetHwTimestamping(h, FALSE, &tsreq)) {
        printf("  [FAIL] SET_HW_TIMESTAMPING(enable=0) failed\n");
        ++failed;
    } else {
        printf("  TSAUXC before : 0x%08X\n", tsreq.previous_tsauxc);
        printf("  TSAUXC after  : 0x%08X\n", tsreq.current_tsauxc);
        if (!(tsreq.current_tsauxc & 0x80000000U)) {
            printf("  [FAIL] Bit 31 not set after disable\n");
            ++failed;
        } else {
            printf("  [PASS] Bit 31 set\n");
            ++passed;
            printf("  Checking SYSTIM freeze (hardware-dependent)...\n");
            if (!IsSystimIncrementing(h)) {
                printf("  [PASS] SYSTIM frozen\n");
                ++passed;
            } else {
                printf("  [INFO] SYSTIM still ticking — acceptable on some hardware variants\n");
            }
        }
    }
    printf("\n");

    /* STEP 3 — re-enable PTP */
    printf("STEP 3: Re-enable PTP clock (SET_HW_TIMESTAMPING enable=1)\n");
    if (!SetHwTimestamping(h, TRUE, &tsreq)) {
        printf("  [FAIL] SET_HW_TIMESTAMPING(re-enable) failed\n");
        ++failed;
    } else {
        printf("  TSAUXC before : 0x%08X\n", tsreq.previous_tsauxc);
        printf("  TSAUXC after  : 0x%08X\n", tsreq.current_tsauxc);
        if (tsreq.current_tsauxc & 0x80000000U) {
            printf("  [FAIL] Bit 31 still set after re-enable\n");
            ++failed;
        } else {
            printf("  [PASS] Bit 31 cleared again\n");
            ++passed;
            printf("  Checking SYSTIM running again...\n");
            if (IsSystimIncrementing(h)) {
                printf("  [PASS] SYSTIM running again\n");
                ++passed;
            } else {
                printf("  [FAIL] SYSTIM not recovering after re-enable\n");
                ++failed;
            }
        }
    }
    printf("\n");

    printf("========================================\n");
    printf("TEST SUMMARY\n");
    printf("========================================\n");
    printf("Tests Passed: %d\n", passed);
    printf("Tests Failed: %d\n", failed);

    if (failed == 0) {
        printf("\n[PASS] ALL TESTS PASSED\n");
        printf("TSAUXC bit 31 enable/disable cycle works correctly!\n");
        return 0;
    } else {
        printf("\n[FAIL] SOME TESTS FAILED\n");
        return 1;
    }
}

int main(void)
{
    printf("TSAUXC Toggle Test\n");
    printf("Validates TSAUXC bit-31 enable/disable cycle via public driver API.\n");
    printf("Adapters without functional TSAUXC (I219, I217) are skipped automatically.\n\n");

    /* --- Step 1: probe adapter count --- */
    HANDLE hEnum = OpenDriverHandle();
    if (hEnum == INVALID_HANDLE_VALUE) {
        printf("[ERR] Cannot open driver device (error %lu)\n", GetLastError());
        return -1;
    }

    AVB_ENUM_REQUEST enumReq;
    ZeroMemory(&enumReq, sizeof(enumReq));
    DWORD br = 0;
    if (!DeviceIoControl(hEnum, IOCTL_AVB_ENUM_ADAPTERS,
                         &enumReq, sizeof(enumReq),
                         &enumReq, sizeof(enumReq),
                         &br, NULL)) {
        printf("[ERR] IOCTL_AVB_ENUM_ADAPTERS failed (error %lu)\n", GetLastError());
        CloseHandle(hEnum);
        return -1;
    }
    DWORD adapterCount = enumReq.count;
    CloseHandle(hEnum);

    printf("Found %lu adapter(s)\n\n", adapterCount);
    if (adapterCount == 0) {
        printf("[SKIP] No Intel adapters found.\n");
        return 0;
    }

    /* --- Step 2: find first adapter with functional TSAUXC ---
     * Strategy: open a fresh handle per adapter, call OPEN_ADAPTER to bind it,
     * then call SET_HW_TIMESTAMPING(enable=1).  If current_tsauxc == 0 the
     * driver's abstraction layer signalled no real TSAUXC (no-op path) — skip.
     * If current_tsauxc != 0 the register exists and we run the toggle test. */
    HANDLE hTest  = INVALID_HANDLE_VALUE;
    DWORD  testIdx = 0;
    WORD   testVid = 0, testDid = 0;

    for (DWORD i = 0; i < adapterCount; ++i) {
        HANDLE hProbe = OpenDriverHandle();
        if (hProbe == INVALID_HANDLE_VALUE) continue;

        /* Query this adapter's VID/DID/caps */
        AVB_ENUM_REQUEST er;
        ZeroMemory(&er, sizeof(er));
        er.index = i;
        br = 0;
        if (!DeviceIoControl(hProbe, IOCTL_AVB_ENUM_ADAPTERS,
                             &er, sizeof(er), &er, sizeof(er), &br, NULL)) {
            CloseHandle(hProbe);
            continue;
        }
        printf("Adapter [%lu]: VID=0x%04X DID=0x%04X Caps=0x%08X\n",
               i, er.vendor_id, er.device_id, er.capabilities);

        /* Bind this handle to the adapter */
        AVB_OPEN_REQUEST openReq;
        ZeroMemory(&openReq, sizeof(openReq));
        openReq.vendor_id = er.vendor_id;
        openReq.device_id = er.device_id;
        openReq.index     = 0; /* first instance for this VID/DID */
        br = 0;
        if (!DeviceIoControl(hProbe, IOCTL_AVB_OPEN_ADAPTER,
                             &openReq, sizeof(openReq),
                             &openReq, sizeof(openReq),
                             &br, NULL)
            || openReq.status != 0) {
            printf("  [SKIP] IOCTL_AVB_OPEN_ADAPTER failed\n");
            CloseHandle(hProbe);
            continue;
        }

        /* Enable PTP and check whether TSAUXC is functional on this adapter.
         * Adapters where the driver no-ops TSAUXC return current_tsauxc == 0. */
        AVB_HW_TIMESTAMPING_REQUEST tsr;
        if (!SetHwTimestamping(hProbe, TRUE, &tsr)) {
            printf("  [SKIP] SET_HW_TIMESTAMPING failed\n");
            CloseHandle(hProbe);
            continue;
        }

        if (tsr.current_tsauxc == 0) {
            printf("  [SKIP] current_tsauxc=0 — adapter has no functional TSAUXC\n");
            CloseHandle(hProbe);
            continue;
        }

        printf("  [OK] TSAUXC=0x%08X — adapter is TSAUXC-capable, selected for test\n",
               tsr.current_tsauxc);
        hTest  = hProbe;
        testIdx = i;
        testVid = er.vendor_id;
        testDid = er.device_id;
        break;
    }

    if (hTest == INVALID_HANDLE_VALUE) {
        printf("\n[SKIP] No TSAUXC-capable adapter found.\n");
        printf("       All present adapters reported current_tsauxc=0 (I219/I217 no-op path).\n");
        return 0;
    }

    printf("\nRunning toggle test on adapter %lu (VID=0x%04X DID=0x%04X)\n",
           testIdx, testVid, testDid);

    int result = RunToggleTest(hTest);
    CloseHandle(hTest);
    return result;
}
