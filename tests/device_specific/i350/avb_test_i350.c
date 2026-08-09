/**
 * @file avb_test_i350.c
 * @brief I350-specific user-mode validation test
 *
 * Enumerates all Intel adapters, locates an I350 (DID 0x1521/0x1522/0x1523/0x1524/0x1546),
 * opens it via IOCTL_AVB_OPEN_ADAPTER, and runs I350-specific hardware checks.
 * Prints [SKIP] and exits 0 if no I350 is present — safe for CI.
 *
 * I350-specific notes (Intel IGB driver / datasheet):
 *   - Gigabit server NIC (1/2/4-port variants)
 *   - IEEE 1588 / 802.1AS hardware timestamping
 *   - TSYNCTXCTL (0xB614) / TSYNCRXCTL (0xB620) — enhanced TS present
 *   - MDC/MDIO management interface to PHY (INTEL_CAP_MDIO)
 *   - No TSN: no TAS (802.1Qbv), no FP (802.1Qbu), no PCIe PTM
 *   - No 2.5G — 10/100/1000 Mb/s only
 *
 * Expected capability mask:
 *   INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_MMIO | INTEL_CAP_MDIO
 *
 * SSOT: ../../../include/avb_ioctl.h (via avb_test_common.h)
 */

#include "../../common/avb_test_common.h"

/* Expected capability mask per Intel I350 datasheet / IGB driver */
#define I350_EXPECTED_CAPS  (INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | \
                             INTEL_CAP_MMIO | INTEL_CAP_MDIO)

/* I350 PCI Device IDs — SSOT: include/intel_pci_ids.h */
static int is_i350(avb_u16 did)
{
    return (did == INTEL_DEV_I350_T4  ||   /* I350-T4  4-port Copper */
            did == INTEL_DEV_I350_F2  ||   /* I350-F2  2-port Fiber  */
            did == INTEL_DEV_I350_F4  ||   /* I350-F4  4-port Fiber  */
            did == INTEL_DEV_I350_T2  ||   /* I350-T2  2-port Copper */
            did == INTEL_DEV_I350_DA4);    /* I350-DA4 4-port SFP+   */
}

int main(void)
{
    printf("=== Intel I350 User-Mode Test ===\n\n");
    printf("Expected capabilities: BASIC_1588 | ENHANCED_TS | MMIO | MDIO\n");
    printf("(per Intel I350 datasheet / IGB driver)\n\n");

    /* Enumerate all adapters */
    AvbAdapterInfo adapters[AVB_MAX_ADAPTERS];
    ZeroMemory(adapters, sizeof(adapters));
    int count = AvbEnumerateAdapters(adapters, AVB_MAX_ADAPTERS);

    if (count == 0) {
        printf("[SKIP] No Intel adapters found via IntelAvbFilter driver.\n");
        printf("       Ensure driver is installed: sc query IntelAvbFilter\n");
        return 0;
    }

    /* Find an I350 */
    const AvbAdapterInfo *i350 = NULL;
    for (int i = 0; i < count; i++) {
        if (is_i350(adapters[i].device_id)) {
            i350 = &adapters[i];
            break;
        }
    }

    if (!i350) {
        printf("[SKIP] No I350 adapter found among %d detected adapter(s).\n", count);
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
        printf("[INFO] Found I350: DID=0x%04X global_index=%u caps=%s\n",
               i350->device_id, i350->global_index,
               AvbCapabilityString(i350->capabilities, caps, sizeof(caps)));
    }

    /* Open a handle bound to the I350 */
    HANDLE h = AvbOpenAdapter(i350);
    if (h == INVALID_HANDLE_VALUE) {
        printf("[FAIL] AvbOpenAdapter failed for I350 (error %lu)\n", GetLastError());
        return 1;
    }

    AvbTestStats stats;
    ZeroMemory(&stats, sizeof(stats));

    DWORD bytesReturned = 0;
    BOOL  result;

    /* ── Test 1: Device initialization ── */
    printf("\n--- Test 1: Device Initialization ---\n");
    result = DeviceIoControl(h, IOCTL_AVB_INIT_DEVICE,
                             NULL, 0, NULL, 0, &bytesReturned, NULL);
    if (result) {
        AVB_REPORT_PASS(&stats, "IOCTL_AVB_INIT_DEVICE");
    } else {
        printf("  [WARN] IOCTL_AVB_INIT_DEVICE failed (error %lu) — continuing\n",
               GetLastError());
        stats.skipped++; stats.total++;
    }

    /* ── Test 2: Device info ── */
    printf("\n--- Test 2: Device Information ---\n");
    {
        AVB_DEVICE_INFO_REQUEST dir;
        ZeroMemory(&dir, sizeof(dir));
        dir.buffer_size = sizeof(dir.device_info);
        result = DeviceIoControl(h, IOCTL_AVB_GET_DEVICE_INFO,
                                 &dir, sizeof(dir), &dir, sizeof(dir),
                                 &bytesReturned, NULL);
        if (result) {
            dir.device_info[sizeof(dir.device_info) - 1] = '\0';
            printf("  [PASS] Device info: %s (status=0x%08X)\n",
                   dir.device_info, dir.status);
            stats.passed++; stats.total++;
        } else {
            AVB_REPORT_FAIL(&stats, "IOCTL_AVB_GET_DEVICE_INFO", "DeviceIoControl failed");
        }
    }

    /* ── Test 3: PTP HW Timestamping Enable ── */
    /*
     * I350 has TSYNCTXCTL/TSYNCRXCTL (ENHANCED_TS per IGB driver).
     * enable_target_time=0, enable_aux_ts=0 — I350 has no TRGTTIM/AUXSTMP.
     */
    printf("\n--- Test 3: PTP Hardware Timestamping Enable ---\n");
    {
        AVB_HW_TIMESTAMPING_REQUEST hwts;
        ZeroMemory(&hwts, sizeof(hwts));
        hwts.enable             = 1;
        hwts.timer_mask         = 1;   /* SYSTIM0 */
        hwts.enable_target_time = 0;   /* I350 has no TRGTTIM */
        hwts.enable_aux_ts      = 0;   /* I350 has no AUXSTMP */

        result = DeviceIoControl(h, IOCTL_AVB_SET_HW_TIMESTAMPING,
                                 &hwts, sizeof(hwts), &hwts, sizeof(hwts),
                                 &bytesReturned, NULL);

        if (result && hwts.status == 0) {
            printf("  [PASS] HW timestamping enabled (TSAUXC: 0x%08X -> 0x%08X)\n",
                   hwts.previous_tsauxc, hwts.current_tsauxc);
            stats.passed++; stats.total++;
        } else {
            printf("  [FAIL] SET_HW_TIMESTAMPING failed (ok=%d error=%lu status=0x%08X)\n",
                   result, GetLastError(), hwts.status);
            stats.failed++; stats.total++;
        }
    }

    /* ── Test 4: GET_TIMESTAMP ── */
    printf("\n--- Test 4: IOCTL_AVB_GET_TIMESTAMP ---\n");
    {
        AVB_TIMESTAMP_REQUEST ts;
        ZeroMemory(&ts, sizeof(ts));
        result = DeviceIoControl(h, IOCTL_AVB_GET_TIMESTAMP,
                                 &ts, sizeof(ts), &ts, sizeof(ts),
                                 &bytesReturned, NULL);
        if (result) {
            printf("  [INFO] Timestamp = 0x%016llX\n", (unsigned long long)ts.timestamp);
            if (ts.timestamp != 0) {
                AVB_REPORT_PASS(&stats, "GET_TIMESTAMP non-zero");
            } else {
                AVB_REPORT_FAIL(&stats, "GET_TIMESTAMP", "Returned zero — hardware not initialized");
            }
        } else {
            AVB_REPORT_FAIL(&stats, "IOCTL_AVB_GET_TIMESTAMP", "DeviceIoControl failed");
        }
    }

    /* ── Test 5: Capabilities Verification ── */
    /*
     * Per Intel I350 datasheet / IGB driver:
     *   MUST have: BASIC_1588, ENHANCED_TS, MMIO, MDIO
     *   MUST NOT have: TSN_TAS, TSN_FP, PCIE_PTM, 2_5G
     */
    printf("\n--- Test 5: Capabilities Verification (per Intel I350 datasheet) ---\n");
    {
        char capbuf[128];
        avb_u32 caps = i350->capabilities;
        printf("  [INFO] Capabilities = 0x%08X (%s)\n",
               (unsigned)caps, AvbCapabilityString(caps, capbuf, sizeof(capbuf)));
        printf("  [INFO] Expected     = 0x%08X\n", (unsigned)I350_EXPECTED_CAPS);

        /* Must-have capabilities */
        if (caps & INTEL_CAP_BASIC_1588) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_BASIC_1588 present");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_BASIC_1588", "IEEE 1588 PTP not reported — driver bug");
        }

        if (caps & INTEL_CAP_ENHANCED_TS) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_ENHANCED_TS present (TSYNCTXCTL/TSYNCRXCTL)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_ENHANCED_TS",
                            "Enhanced timestamps not reported — I350 IGB driver confirms 0xB614/0xB620");
        }

        if (caps & INTEL_CAP_MMIO) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_MMIO present (BAR0 accessible)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_MMIO",
                            "MMIO not reported — I350 is a discrete NIC with full BAR0");
        }

        if (caps & INTEL_CAP_MDIO) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_MDIO present (MDC/MDIO to PHY)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_MDIO",
                            "MDIO not reported — I350 datasheet confirms MDIC-based PHY access");
        }

        /* Must-not-have capabilities */
        if (!(caps & INTEL_CAP_TSN_TAS)) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_TSN_TAS correctly absent (I350 has no 802.1Qbv)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_TSN_TAS",
                            "TAS reported on I350 — only I225/I226 support 802.1Qbv");
        }

        if (!(caps & INTEL_CAP_TSN_FP)) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_TSN_FP correctly absent (I350 has no 802.1Qbu)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_TSN_FP",
                            "Frame Preemption reported on I350 — only I225/I226 support 802.1Qbu");
        }

        if (!(caps & INTEL_CAP_PCIE_PTM)) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_PCIE_PTM correctly absent (I350 has no PCIe PTM)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_PCIE_PTM",
                            "PCIe PTM reported on I350 — only Foxville (I225/I226) supports PTM");
        }

        if (!(caps & INTEL_CAP_2_5G)) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_2_5G correctly absent (I350 is 1 GbE)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_2_5G",
                            "2.5G reported on I350 — hardware is 10/100/1000 Mb/s only");
        }
    }

    /* ── Test 6: PTP Clock Monotonicity ── */
    printf("\n--- Test 6: PTP Clock Monotonicity ---\n");
    {
        AVB_TIMESTAMP_REQUEST ts1req, ts2req;
        ZeroMemory(&ts1req, sizeof(ts1req));
        ZeroMemory(&ts2req, sizeof(ts2req));

        BOOL ok1 = DeviceIoControl(h, IOCTL_AVB_GET_TIMESTAMP,
                                   &ts1req, sizeof(ts1req), &ts1req, sizeof(ts1req),
                                   &bytesReturned, NULL);
        if (!ok1) {
            AVB_REPORT_FAIL(&stats, "Monotonicity first read", "IOCTL_AVB_GET_TIMESTAMP failed");
        } else {
            avb_u64 ts1 = ts1req.timestamp;
            Sleep(10);  /* 10 ms — clock must advance */
            BOOL ok2 = DeviceIoControl(h, IOCTL_AVB_GET_TIMESTAMP,
                                       &ts2req, sizeof(ts2req), &ts2req, sizeof(ts2req),
                                       &bytesReturned, NULL);
            if (!ok2) {
                AVB_REPORT_FAIL(&stats, "Monotonicity second read", "IOCTL_AVB_GET_TIMESTAMP failed");
            } else {
                avb_u64 ts2 = ts2req.timestamp;
                avb_u64 delta = (ts2 > ts1) ? (ts2 - ts1) : 0;
                printf("  [INFO] T1=0x%016llX T2=0x%016llX delta=%llu ns\n",
                       (unsigned long long)ts1, (unsigned long long)ts2,
                       (unsigned long long)delta);
                if (ts2 > ts1) {
                    AVB_REPORT_PASS(&stats, "PTP clock monotonic (T2 > T1)");
                } else {
                    AVB_REPORT_FAIL(&stats, "PTP clock monotonic",
                                    "T2 <= T1 — clock not advancing");
                }
            }
        }
    }

    /* ── Test 7: I350 Variant Matrix ── */
    printf("\n--- Test 7: I350 Variant Matrix ---\n");
    {
        static const struct { avb_u16 did; const char *name; } kI350Variants[] = {
            { INTEL_DEV_I350_T4,  "I350-T4"  },
            { INTEL_DEV_I350_F2,  "I350-F2"  },
            { INTEL_DEV_I350_F4,  "I350-F4"  },
            { INTEL_DEV_I350_T2,  "I350-T2"  },
            { INTEL_DEV_I350_DA4, "I350-DA4" },
        };
        static const int kI350VariantCount =
            (int)(sizeof(kI350Variants) / sizeof(kI350Variants[0]));

        printf("  [INFO] Full I350 variant table (%d entries):\n", kI350VariantCount);
        const char *matchName = NULL;
        for (int v = 0; v < kI350VariantCount; v++) {
            int match = (kI350Variants[v].did == i350->device_id);
            printf("    DID=0x%04X  %-10s  %s\n",
                   kI350Variants[v].did,
                   kI350Variants[v].name,
                   match ? "<-- detected" : "");
            if (match) matchName = kI350Variants[v].name;
        }
        if (matchName) {
            printf("  [PASS] Detected I350 variant: %s (DID=0x%04X)\n",
                   matchName, i350->device_id);
            stats.passed++; stats.total++;
        } else {
            printf("  [WARN] DID=0x%04X not in variant table — add to intel_pci_ids.h\n",
                   i350->device_id);
            stats.skipped++; stats.total++;
        }
    }

    CloseHandle(h);

    /* ── Summary ── */
    printf("\n========================================\n");
    printf("I350 Test Summary: %d passed, %d failed, %d skipped (total %d)\n",
           stats.passed, stats.failed, stats.skipped, stats.total);
    printf("========================================\n");

    return (stats.failed > 0) ? 1 : 0;
}
