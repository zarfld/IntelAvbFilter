/**
 * @file avb_test_i210_um.c
 * @brief I210-specific user-mode validation test
 *
 * Enumerates all Intel adapters, locates the I210 (DID 0x1533 family),
 * opens it via IOCTL_AVB_OPEN_ADAPTER, and runs basic hardware checks.
 * Prints [SKIP] and exits 0 if no I210 is present — safe for CI.
 *
 * SSOT: ../../../include/avb_ioctl.h (via avb_test_common.h)
 */

#include "../../common/avb_test_common.h"

/* I210 PCI Device IDs — SSOT from intel_pci_ids.h (via avb_test_common.h) */

static int is_i210(avb_u16 device_id)
{
    return device_id == INTEL_DEV_I210_AT    ||
           device_id == INTEL_DEV_I210_AT2   ||
           device_id == INTEL_DEV_I210_FIBER ||
           device_id == INTEL_DEV_I210_IS    ||
           device_id == INTEL_DEV_I210_IT    ||
           device_id == INTEL_DEV_I210_CS;
}

int main(void)
{
    printf("=== Intel I210 User-Mode Test ===\n\n");

    /* Enumerate all adapters */
    AvbAdapterInfo adapters[AVB_MAX_ADAPTERS];
    ZeroMemory(adapters, sizeof(adapters));
    int count = AvbEnumerateAdapters(adapters, AVB_MAX_ADAPTERS);

    if (count == 0) {
        printf("[SKIP] No Intel adapters found via IntelAvbFilter driver.\n");
        printf("       Ensure driver is installed: sc query IntelAvbFilter\n");
        return 0;
    }

    /* Find an I210 */
    const AvbAdapterInfo *i210 = NULL;
    for (int i = 0; i < count; i++) {
        if (is_i210(adapters[i].device_id)) {
            i210 = &adapters[i];
            break;
        }
    }

    if (!i210) {
        printf("[SKIP] No I210 adapter found among %d detected adapter(s).\n", count);

        /* Print what IS present so the user can diagnose */
        for (int i = 0; i < count; i++) {
            char caps[128];
            printf("       Adapter %d: %s (DID=0x%04X) caps=%s\n",
                   adapters[i].global_index,
                   adapters[i].device_name,
                   adapters[i].device_id,
                   AvbCapabilityString(adapters[i].capabilities, caps, sizeof(caps)));
        }
        return 0;
    }

    {
        char caps[128];
        printf("[INFO] Found I210: DID=0x%04X global_index=%u caps=%s\n",
               i210->device_id, i210->global_index,
               AvbCapabilityString(i210->capabilities, caps, sizeof(caps)));
    }

    /* Open a handle bound to the I210 */
    HANDLE h = AvbOpenAdapter(i210);
    if (h == INVALID_HANDLE_VALUE) {
        printf("[FAIL] AvbOpenAdapter failed for I210 (error %lu)\n", GetLastError());
        return 1;
    }

    AvbTestStats stats;
    ZeroMemory(&stats, sizeof(stats));

    /* ── Test 1: CTRL register ── */
    {
        avb_u32 ctrl = 0;
        int rc = AvbReadReg(h, 0x00000, &ctrl);
        if (rc == 0) {
            printf("  [PASS] CTRL register: 0x%08X\n", (unsigned)ctrl);
            stats.passed++; stats.total++;
        } else {
            printf("  [FAIL] CTRL register read failed (error %d)\n", rc);
            stats.failed++; stats.total++;
        }
    }

    /* ── Test 2: STATUS register ── */
    {
        avb_u32 status = 0;
        int rc = AvbReadReg(h, 0x00008, &status);
        if (rc == 0) {
            printf("  [PASS] STATUS register: 0x%08X\n", (unsigned)status);
            stats.passed++; stats.total++;
        } else {
            printf("  [FAIL] STATUS register read failed (error %d)\n", rc);
            stats.failed++; stats.total++;
        }
    }

    /* ── Test 3: Hardware timestamp ── */
    {
        AVB_TIMESTAMP_REQUEST ts;
        ZeroMemory(&ts, sizeof(ts));
        DWORD br = 0;
        if (DeviceIoControl(h, IOCTL_AVB_GET_TIMESTAMP,
                            &ts, sizeof(ts), &ts, sizeof(ts), &br, NULL)) {
            printf("  [PASS] Timestamp: 0x%016llX\n", (unsigned long long)ts.timestamp);
            stats.passed++; stats.total++;
        } else {
            printf("  [FAIL] IOCTL_AVB_GET_TIMESTAMP failed (error %lu)\n", GetLastError());
            stats.failed++; stats.total++;
        }
    }

    /* ── Test 4: Device info ── */
    {
        AVB_DEVICE_INFO_REQUEST di;
        ZeroMemory(&di, sizeof(di));
        di.buffer_size = sizeof(di.device_info);
        DWORD br = 0;
        if (DeviceIoControl(h, IOCTL_AVB_GET_DEVICE_INFO,
                            &di, sizeof(di), &di, sizeof(di), &br, NULL)) {
            di.device_info[sizeof(di.device_info) - 1] = '\0';
            printf("  [PASS] Device info: %s (status=0x%08X)\n",
                   di.device_info, di.status);
            stats.passed++; stats.total++;
        } else {
            printf("  [FAIL] IOCTL_AVB_GET_DEVICE_INFO failed (error %lu)\n", GetLastError());
            stats.failed++; stats.total++;
        }
    }

    /* ── Test 5: TIMINCA register initialized ── */
    /*
     * Verifies that init_ptp() correctly writes INTEL_TIMINCA_I210_INIT to
     * the I210_TIMINCA register (0x0B608).  The I210 runs at 125 MHz, so each
     * clock cycle is 8 ns → TIMINCA must be 0x08000000 (integer 8 in the
     * increment field, fractional = 0).  A value of 0 means the SYSTIM clock
     * never advances.
     *
     * Implements: issue for I210 TIMINCA init fix
     */
    printf("\n--- Test 5: TIMINCA register ---\n");
    {
        avb_u32 timinca = 0;
        int rc = AvbReadReg(h, 0x0B608 /* I210_TIMINCA */, &timinca);
        if (rc != 0) {
            printf("  [FAIL] TIMINCA read failed (error %d)\n", rc);
            stats.failed++; stats.total++;
        } else {
            printf("  [INFO] TIMINCA=0x%08X (expected 0x08000000)\n", (unsigned)timinca);
            if (timinca == 0x08000000U) {
                printf("  [PASS] TIMINCA correctly initialized (8 ns/cycle for 125 MHz)\n");
                stats.passed++; stats.total++;
            } else if (timinca == 0) {
                printf("  [FAIL] TIMINCA=0: SYSTIM clock frozen — init_ptp() did not set TIMINCA\n");
                stats.failed++; stats.total++;
            } else {
                printf("  [FAIL] TIMINCA=0x%08X: unexpected value (expected 0x08000000)\n",
                       (unsigned)timinca);
                stats.failed++; stats.total++;
            }
        }
    }

    /* ── Test 6: PTP clock monotonicity ── */
    /*
     * Reads SYSTIM twice via IOCTL_AVB_GET_TIMESTAMP with a 50 ms sleep.
     * If TIMINCA == 0 the clock is frozen and T2 == T1 (FAIL).
     * If TIMINCA is set correctly T2 > T1 by ~50,000,000 ns (PASS).
     *
     * This is the observable symptom of the TIMINCA bug.
     */
    printf("\n--- Test 6: PTP clock monotonicity (50 ms sleep) ---\n");
    {
        AVB_TIMESTAMP_REQUEST ts1req, ts2req;
        DWORD br = 0;
        ZeroMemory(&ts1req, sizeof(ts1req));
        ZeroMemory(&ts2req, sizeof(ts2req));

        BOOL ok1 = DeviceIoControl(h, IOCTL_AVB_GET_TIMESTAMP,
                                   &ts1req, sizeof(ts1req), &ts1req, sizeof(ts1req),
                                   &br, NULL);
        if (!ok1) {
            printf("  [FAIL] IOCTL_AVB_GET_TIMESTAMP (T1) failed (error %lu)\n", GetLastError());
            stats.failed++; stats.total++;
        } else {
            avb_u64 t1 = ts1req.timestamp;
            Sleep(50);  /* 50 ms — SYSTIM must advance by ~50,000,000 ns */

            BOOL ok2 = DeviceIoControl(h, IOCTL_AVB_GET_TIMESTAMP,
                                       &ts2req, sizeof(ts2req), &ts2req, sizeof(ts2req),
                                       &br, NULL);
            if (!ok2) {
                printf("  [FAIL] IOCTL_AVB_GET_TIMESTAMP (T2) failed (error %lu)\n", GetLastError());
                stats.failed++; stats.total++;
            } else {
                avb_u64 t2 = ts2req.timestamp;
                avb_u64 delta = (t2 > t1) ? (t2 - t1) : 0;
                printf("  [INFO] T1=0x%016llX T2=0x%016llX delta=%llu ns\n",
                       (unsigned long long)t1, (unsigned long long)t2,
                       (unsigned long long)delta);
                if (t2 > t1) {
                    printf("  [PASS] PTP clock monotonic (delta=%llu ns)\n",
                           (unsigned long long)delta);
                    stats.passed++; stats.total++;
                } else {
                    printf("  [FAIL] PTP clock NOT advancing (T2<=T1) — TIMINCA likely not set\n");
                    stats.failed++; stats.total++;
                }
            }
        }
    }

    CloseHandle(h);
    return AvbPrintSummary(&stats);
}
