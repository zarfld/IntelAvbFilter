/**
 * @file avb_test_i217.c
 * @brief I217-specific user-mode validation test
 *
 * Enumerates all Intel adapters, locates an I217 (DID 0x153A/0x153B),
 * opens it via IOCTL_AVB_OPEN_ADAPTER, and runs I217-specific hardware checks.
 * Prints [SKIP] and exits 0 if no I217 is present — safe for CI.
 *
 * I217-specific notes (verified against Intel I217 datasheet via NotebookLM):
 *   - 1 GbE PCH-integrated MAC + PHY
 *   - IEEE 1588 / 802.1AS basic timestamping
 *   - TSYNCTXCTL (0xB614) / TSYNCRXCTL (0xB620) — enhanced TS present
 *   - MDC/MDIO management interface to PHY (INTEL_CAP_MDIO)
 *   - EEE IEEE 802.3az LPI supported (1000BASE-T, 100BASE-TX)
 *   - No TSN: no TAS (802.1Qbv), no FP (802.1Qbu), no PCIe PTM
 *   - No 2.5G — 10/100/1000 Mb/s only
 *
 * Expected capability mask:
 *   INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_MMIO |
 *   INTEL_CAP_MDIO | INTEL_CAP_EEE
 *
 * Verifies: #114 (QA-SC-PORT-001: Hardware Portability Across Intel Controllers)
 *
 * SSOT: ../../../include/avb_ioctl.h (via avb_test_common.h)
 */

#include "../../common/avb_test_common.h"

/* Expected capability mask per Intel I217 datasheet */
#define I217_EXPECTED_CAPS  (INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | \
                             INTEL_CAP_MMIO | INTEL_CAP_MDIO | INTEL_CAP_EEE)

/* I217 PCI Device IDs — SSOT: include/intel_pci_ids.h */
static int is_i217(avb_u16 did)
{
    return (did == INTEL_DEV_I217_LM ||   /* I217-LM */
            did == INTEL_DEV_I217_V);     /* I217-V  */
}

int main(void)
{
    printf("=== Intel I217 User-Mode Test ===\n\n");
    printf("Expected capabilities: BASIC_1588 | ENHANCED_TS | MMIO | MDIO | EEE\n");
    printf("(per Intel I217 datasheet, NotebookLM-verified)\n\n");

    /* Enumerate all adapters */
    AvbAdapterInfo adapters[AVB_MAX_ADAPTERS];
    ZeroMemory(adapters, sizeof(adapters));
    int count = AvbEnumerateAdapters(adapters, AVB_MAX_ADAPTERS);

    if (count == 0) {
        printf("[SKIP] No Intel adapters found via IntelAvbFilter driver.\n");
        printf("       Ensure driver is installed: sc query IntelAvbFilter\n");
        return 0;
    }

    /* Find an I217 */
    const AvbAdapterInfo *i217 = NULL;
    for (int i = 0; i < count; i++) {
        if (is_i217(adapters[i].device_id)) {
            i217 = &adapters[i];
            break;
        }
    }

    if (!i217) {
        printf("[SKIP] No I217 adapter found among %d detected adapter(s).\n", count);
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
        printf("[INFO] Found I217: DID=0x%04X global_index=%u caps=%s\n",
               i217->device_id, i217->global_index,
               AvbCapabilityString(i217->capabilities, caps, sizeof(caps)));
    }

    /* Open a handle bound to the I217 */
    HANDLE h = AvbOpenAdapter(i217);
    if (h == INVALID_HANDLE_VALUE) {
        printf("[FAIL] AvbOpenAdapter failed for I217 (error %lu)\n", GetLastError());
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
     * I217 has TSYNCTXCTL/TSYNCRXCTL (ENHANCED_TS per Intel datasheet).
     * enable_target_time=0, enable_aux_ts=0 — I217 has no TRGTTIM/AUXSTMP.
     */
    printf("\n--- Test 3: PTP Hardware Timestamping Enable ---\n");
    {
        AVB_HW_TIMESTAMPING_REQUEST hwts;
        ZeroMemory(&hwts, sizeof(hwts));
        hwts.enable             = 1;
        hwts.timer_mask         = 1;   /* SYSTIM0 */
        hwts.enable_target_time = 0;   /* I217 has no TRGTTIM */
        hwts.enable_aux_ts      = 0;   /* I217 has no AUXSTMP */

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
     * Verifies: #114 (QA-SC-PORT-001)
     *
     * Per Intel I217 datasheet (NotebookLM-verified):
     *   MUST have: BASIC_1588, ENHANCED_TS, MMIO, MDIO, EEE
     *   MUST NOT have: TSN_TAS, TSN_FP, PCIE_PTM, 2_5G
     */
    printf("\n--- Test 5: Capabilities Verification (per Intel I217 datasheet) ---\n");
    {
        char capbuf[128];
        avb_u32 caps = i217->capabilities;
        printf("  [INFO] Capabilities = 0x%08X (%s)\n",
               (unsigned)caps, AvbCapabilityString(caps, capbuf, sizeof(capbuf)));
        printf("  [INFO] Expected     = 0x%08X\n", (unsigned)I217_EXPECTED_CAPS);

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
                            "Enhanced timestamps not reported — I217 datasheet confirms 0xB614/0xB620 present");
        }

        if (caps & INTEL_CAP_MDIO) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_MDIO present (MDC/MDIO to PHY)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_MDIO",
                            "MDIO not reported — I217 datasheet confirms MDC/MDIO management interface");
        }

        if (caps & INTEL_CAP_EEE) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_EEE present (IEEE 802.3az LPI)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_EEE",
                            "EEE not reported — I217 datasheet confirms 802.3az EEE support");
        }

        /* Must-not-have capabilities */
        if (!(caps & INTEL_CAP_TSN_TAS)) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_TSN_TAS correctly absent (I217 has no 802.1Qbv)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_TSN_TAS",
                            "TAS reported on I217 — only I225/I226 support 802.1Qbv");
        }

        if (!(caps & INTEL_CAP_TSN_FP)) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_TSN_FP correctly absent (I217 has no 802.1Qbu)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_TSN_FP",
                            "Frame Preemption reported on I217 — only I225/I226 support 802.1Qbu");
        }

        if (!(caps & INTEL_CAP_PCIE_PTM)) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_PCIE_PTM correctly absent (I217 has no PCIe PTM)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_PCIE_PTM",
                            "PCIe PTM reported on I217 — only Foxville (I225/I226) supports PTM");
        }

        if (!(caps & INTEL_CAP_2_5G)) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_2_5G correctly absent (I217 is 1 GbE)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_2_5G",
                            "2.5G reported on I217 — hardware is 10/100/1000 Mb/s only");
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

    /* ── Test 7: I217 Variant Matrix ── */
    printf("\n--- Test 7: I217 Variant Matrix ---\n");
    {
        static const struct { avb_u16 did; const char *name; } kI217Variants[] = {
            { INTEL_DEV_I217_LM, "I217-LM" },
            { INTEL_DEV_I217_V,  "I217-V"  },
        };
        static const int kI217VariantCount =
            (int)(sizeof(kI217Variants) / sizeof(kI217Variants[0]));

        printf("  [INFO] Full I217 variant table (%d entries):\n", kI217VariantCount);
        const char *matchName = NULL;
        for (int vi = 0; vi < kI217VariantCount; vi++) {
            int current = (kI217Variants[vi].did == i217->device_id) ? 1 : 0;
            printf("  [INFO]   DID=0x%04X  %-20s %s\n",
                   kI217Variants[vi].did,
                   kI217Variants[vi].name,
                   current ? "<-- THIS DEVICE" : "");
            if (current) { matchName = kI217Variants[vi].name; }
        }

        if (matchName) {
            printf("  [PASS] DID=0x%04X recognized as %s\n",
                   (unsigned)i217->device_id, matchName);
            stats.passed++; stats.total++;
        } else {
            printf("  [FAIL] DID=0x%04X not in recognized I217 variant table\n",
                   (unsigned)i217->device_id);
            printf("         Update kI217Variants[] in avb_test_i217.c\n");
            stats.failed++; stats.total++;
        }
    }

    /* ── Test 8: MDIO PHY Register Read ── */
    /*
     * I217 has MDC/MDIO management interface per datasheet.
     * Read PHY Control Register (Clause 22, page 0, reg 0).
     * 0xFFFF = MDIO bus non-responsive.
     */
    printf("\n--- Test 8: MDIO PHY Register Read ---\n");
    {
        AVB_MDIO_REQUEST mdioReq;

        /* Read PHY Control Register (Clause 22, page 0, reg 0) */
        ZeroMemory(&mdioReq, sizeof(mdioReq));
        mdioReq.page = 0;
        mdioReq.reg  = 0;  /* PHY Control Register */
        result = DeviceIoControl(h, IOCTL_AVB_MDIO_READ,
                                 &mdioReq, sizeof(mdioReq),
                                 &mdioReq, sizeof(mdioReq),
                                 &bytesReturned, NULL);
        if (!result) {
            AVB_REPORT_FAIL(&stats, "IOCTL_AVB_MDIO_READ (PHY reg 0)", "DeviceIoControl failed");
        } else if (mdioReq.status != 0) {
            printf("  [FAIL] MDIO_READ status=0x%08X — driver rejected request\n",
                   mdioReq.status);
            stats.failed++; stats.total++;
        } else if (mdioReq.value == 0xFFFFU) {
            AVB_REPORT_FAIL(&stats, "MDIO PHY reg 0",
                            "Value=0xFFFF — MDIO bus non-responsive or PHY absent");
        } else {
            printf("  [PASS] PHY Control Reg (reg 0) = 0x%04X "
                   "(AN_Enable=%u Duplex=%u)\n",
                   (unsigned)mdioReq.value,
                   (mdioReq.value >> 12) & 1u,
                   (mdioReq.value >>  8) & 1u);
            stats.passed++; stats.total++;
        }

        /* Read PHY Status Register (Clause 22, page 0, reg 1) */
        ZeroMemory(&mdioReq, sizeof(mdioReq));
        mdioReq.page = 0;
        mdioReq.reg  = 1;  /* PHY Status Register */
        result = DeviceIoControl(h, IOCTL_AVB_MDIO_READ,
                                 &mdioReq, sizeof(mdioReq),
                                 &mdioReq, sizeof(mdioReq),
                                 &bytesReturned, NULL);
        if (!result || mdioReq.status != 0) {
            printf("  [WARN] MDIO_READ PHY reg 1 failed — skipping\n");
            stats.skipped++; stats.total++;
        } else if (mdioReq.value == 0xFFFFU) {
            AVB_REPORT_FAIL(&stats, "MDIO PHY reg 1", "Value=0xFFFF — PHY Status unreadable");
        } else {
            int caps_ok = (mdioReq.value >> 11) != 0;
            printf("  [INFO] PHY Status Reg (reg 1) = 0x%04X "
                   "(Link=%u AN_complete=%u caps[15:11]=0x%X)\n",
                   (unsigned)mdioReq.value,
                   (mdioReq.value >> 2) & 1,
                   (mdioReq.value >> 5) & 1,
                   (mdioReq.value >> 11) & 0x1FU);
            if (caps_ok) {
                AVB_REPORT_PASS(&stats, "PHY Status reg 1 capability bits non-zero");
            } else {
                AVB_REPORT_FAIL(&stats, "PHY Status reg 1",
                                "Capability bits [15:11] all zero — unexpected for I217");
            }
        }
    }

    /* ── Test 9: TSN Not Supported Verification ── */
    /*
     * I217 has no TSN hardware. SETUP_TAS and SETUP_FP must return
     * ERROR_NOT_SUPPORTED (capability gate in driver).
     */
    printf("\n--- Test 9: TSN IOCTLs Return NOT_SUPPORTED (I217 has no TSN) ---\n");
    {
        /* SETUP_TAS must be gated by INTEL_CAP_TSN_TAS */
        AVB_TAS_REQUEST tasReq;
        ZeroMemory(&tasReq, sizeof(tasReq));
        tasReq.config.cycle_time_ns = 1000000U;  /* 1 ms — valid but irrelevant */
        result = DeviceIoControl(h, IOCTL_AVB_SETUP_TAS,
                                 &tasReq, sizeof(tasReq), &tasReq, sizeof(tasReq),
                                 &bytesReturned, NULL);
        DWORD err = GetLastError();
        if (!result && err == ERROR_NOT_SUPPORTED) {
            AVB_REPORT_PASS(&stats, "SETUP_TAS returns ERROR_NOT_SUPPORTED (no TSN_TAS cap)");
        } else if (!result) {
            printf("  [FAIL] SETUP_TAS failed with unexpected error %lu (expected 50=NOT_SUPPORTED)\n",
                   err);
            stats.failed++; stats.total++;
        } else {
            AVB_REPORT_FAIL(&stats, "SETUP_TAS",
                            "Succeeded on I217 — capability gate missing in driver");
        }

        /* SETUP_FP must be gated by INTEL_CAP_TSN_FP */
        AVB_FP_REQUEST fpReq;
        ZeroMemory(&fpReq, sizeof(fpReq));
        fpReq.config.min_fragment_size = 60U;
        result = DeviceIoControl(h, IOCTL_AVB_SETUP_FP,
                                 &fpReq, sizeof(fpReq), &fpReq, sizeof(fpReq),
                                 &bytesReturned, NULL);
        err = GetLastError();
        if (!result && err == ERROR_NOT_SUPPORTED) {
            AVB_REPORT_PASS(&stats, "SETUP_FP returns ERROR_NOT_SUPPORTED (no TSN_FP cap)");
        } else if (!result) {
            printf("  [FAIL] SETUP_FP failed with unexpected error %lu (expected 50=NOT_SUPPORTED)\n",
                   err);
            stats.failed++; stats.total++;
        } else {
            AVB_REPORT_FAIL(&stats, "SETUP_FP",
                            "Succeeded on I217 — capability gate missing in driver");
        }
    }

    CloseHandle(h);
    return AvbPrintSummary(&stats);
}
