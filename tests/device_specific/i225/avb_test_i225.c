/**
 * @file avb_test_i225.c
 * @brief I225-specific user-mode validation test
 *
 * Enumerates all Intel adapters, locates an I225 (Foxville 2.5GbE),
 * opens it via IOCTL_AVB_OPEN_ADAPTER, and runs I225-specific hardware checks.
 * Prints [SKIP] and exits 0 if no I225 is present — safe for CI.
 *
 * I225-specific notes (verified against Intel I225 datasheet via NotebookLM):
 *   - 2.5 GbE (Foxville): 2500/1000/100/10 Mb/s
 *   - IEEE 1588 / 802.1AS + 802.1AS-Rev (dual clock masters, 4 HW timers)
 *   - TSYNCTXCTL (0xB614) / TSYNCRXCTL (0xB620) — ENHANCED_TS present
 *   - Time-Aware Shaper 802.1Qbv (INTEL_CAP_TSN_TAS)
 *   - Frame Preemption 802.1Qbu / 802.3br (INTEL_CAP_TSN_FP)
 *   - PCIe PTM — Precision Time Measurement (INTEL_CAP_PCIE_PTM)
 *   - MDC/MDIO management interface to internal PHY (INTEL_CAP_MDIO)
 *   - NO EEE (IEEE 802.3az EEE is exclusive to the I226 successor)
 *
 * Expected capability mask:
 *   INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_TSN_TAS |
 *   INTEL_CAP_TSN_FP | INTEL_CAP_PCIE_PTM | INTEL_CAP_2_5G |
 *   INTEL_CAP_MMIO | INTEL_CAP_MDIO
 *   (INTEL_CAP_EEE must NOT be set — EEE is I226 only)
 *
 * Verifies: #114 (QA-SC-PORT-001: Hardware Portability Across Intel Controllers)
 *
 * SSOT: ../../../include/avb_ioctl.h (via avb_test_common.h)
 */

#include "../../common/avb_test_common.h"

/* Expected capability mask per Intel I225 datasheet */
#define I225_EXPECTED_CAPS  (INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | \
                             INTEL_CAP_TSN_TAS | INTEL_CAP_TSN_FP |         \
                             INTEL_CAP_PCIE_PTM | INTEL_CAP_2_5G |          \
                             INTEL_CAP_MMIO | INTEL_CAP_MDIO)

/* I225 PCI Device IDs — SSOT: include/intel_pci_ids.h */
static int is_i225(avb_u16 did)
{
    return (did == INTEL_DEV_I225_V      ||
            did == INTEL_DEV_I225_IT     ||
            did == INTEL_DEV_I225_LM     ||
            did == INTEL_DEV_I225_K      ||
            did == INTEL_DEV_I225_K2     ||
            did == INTEL_DEV_I225_LMVP   ||
            did == INTEL_DEV_I225_VB     ||
            did == INTEL_DEV_I225_IT2    ||
            did == INTEL_DEV_I225_LM2    ||
            did == INTEL_DEV_I225_LM3    ||
            did == INTEL_DEV_I225_V2     ||
            did == INTEL_DEV_I225_VARIANT);
}

int main(void)
{
    printf("=== Intel I225 User-Mode Test ===\n\n");
    printf("Expected capabilities: BASIC_1588 | ENHANCED_TS | TSN_TAS | TSN_FP |"
           " PCIE_PTM | 2_5G | MMIO | MDIO  (NO EEE — that is I226 only)\n");
    printf("(per Intel I225 datasheet, NotebookLM-verified)\n\n");

    /* Enumerate all adapters */
    AvbAdapterInfo adapters[AVB_MAX_ADAPTERS];
    ZeroMemory(adapters, sizeof(adapters));
    int count = AvbEnumerateAdapters(adapters, AVB_MAX_ADAPTERS);

    if (count == 0) {
        printf("[SKIP] No Intel adapters found via IntelAvbFilter driver.\n");
        printf("       Ensure driver is installed: sc query IntelAvbFilter\n");
        return 0;
    }

    /* Find an I225 */
    const AvbAdapterInfo *i225 = NULL;
    for (int i = 0; i < count; i++) {
        if (is_i225(adapters[i].device_id)) {
            i225 = &adapters[i];
            break;
        }
    }

    if (!i225) {
        printf("[SKIP] No I225 adapter found among %d detected adapter(s).\n", count);
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
        printf("[INFO] Found I225: DID=0x%04X global_index=%u caps=%s\n",
               i225->device_id, i225->global_index,
               AvbCapabilityString(i225->capabilities, caps, sizeof(caps)));
    }

    /* Open a handle bound to the I225 */
    HANDLE h = AvbOpenAdapter(i225);
    if (h == INVALID_HANDLE_VALUE) {
        printf("[FAIL] AvbOpenAdapter failed for I225 (error %lu)\n", GetLastError());
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
    printf("\n--- Test 3: PTP Hardware Timestamping Enable ---\n");
    {
        AVB_HW_TIMESTAMPING_REQUEST hwts;
        ZeroMemory(&hwts, sizeof(hwts));
        hwts.enable             = 1;
        hwts.timer_mask         = 1;
        hwts.enable_target_time = 1;   /* I225 supports TRGTTIM */
        hwts.enable_aux_ts      = 1;   /* I225 supports AUXSTMP */

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
     * Per Intel I225 datasheet (NotebookLM-verified):
     *   MUST have: BASIC_1588, ENHANCED_TS, TSN_TAS, TSN_FP, PCIE_PTM, 2_5G, MMIO, MDIO
     *   MUST NOT have: EEE (exclusive to I226 successor)
     */
    printf("\n--- Test 5: Capabilities Verification (per Intel I225 datasheet) ---\n");
    {
        char capbuf[128];
        avb_u32 caps = i225->capabilities;
        printf("  [INFO] Capabilities = 0x%08X (%s)\n",
               (unsigned)caps, AvbCapabilityString(caps, capbuf, sizeof(capbuf)));
        printf("  [INFO] Expected     = 0x%08X\n", (unsigned)I225_EXPECTED_CAPS);

        /* Must-have capabilities */
        if (caps & INTEL_CAP_BASIC_1588) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_BASIC_1588 present");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_BASIC_1588", "IEEE 1588 PTP not reported");
        }

        if (caps & INTEL_CAP_ENHANCED_TS) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_ENHANCED_TS present (TSYNCTXCTL/TSYNCRXCTL)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_ENHANCED_TS", "Enhanced timestamps not reported");
        }

        if (caps & INTEL_CAP_TSN_TAS) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_TSN_TAS present (802.1Qbv Time-Aware Shaper)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_TSN_TAS",
                            "TAS not reported — I225 datasheet confirms 802.1Qbv support");
        }

        if (caps & INTEL_CAP_TSN_FP) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_TSN_FP present (802.1Qbu Frame Preemption)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_TSN_FP",
                            "Frame Preemption not reported — I225 datasheet confirms 802.1Qbu/802.3br");
        }

        if (caps & INTEL_CAP_PCIE_PTM) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_PCIE_PTM present (PCIe Precision Time Measurement)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_PCIE_PTM",
                            "PCIe PTM not reported — I225 datasheet confirms PTM support");
        }

        if (caps & INTEL_CAP_2_5G) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_2_5G present (2500BASE-T)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_2_5G",
                            "2.5G not reported — I225 supports 2500/1000/100/10 Mb/s");
        }

        if (caps & INTEL_CAP_MMIO) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_MMIO present (BAR0 register access)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_MMIO", "MMIO not reported — all Intel NICs have BAR0");
        }

        if (caps & INTEL_CAP_MDIO) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_MDIO present (MDC/MDIO to internal PHY)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_MDIO",
                            "MDIO not reported — I225 datasheet confirms MDC/MDIO interface");
        }

        /* Must-NOT-have: EEE is exclusive to I226 */
        if (!(caps & INTEL_CAP_EEE)) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_EEE correctly absent (EEE is I226 only, not I225)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_EEE",
                            "EEE reported on I225 — Intel datasheet: EEE exclusive to I226 successor");
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
            Sleep(10);
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
                    AVB_REPORT_FAIL(&stats, "PTP clock monotonic", "T2 <= T1 — clock not advancing");
                }
            }
        }
    }

    /* ── Test 7: I225 Variant Matrix ── */
    printf("\n--- Test 7: I225 Variant Matrix ---\n");
    {
        static const struct { avb_u16 did; const char *name; } kI225Variants[] = {
            { INTEL_DEV_I225_V,       "I225-V   2.5G"    },
            { INTEL_DEV_I225_IT,      "I225-IT  2.5G"    },
            { INTEL_DEV_I225_LM,      "I225-LM  2.5G"    },
            { INTEL_DEV_I225_K,       "I225-K   2.5G"    },
            { INTEL_DEV_I225_K2,      "I225-K2  2.5G"    },
            { INTEL_DEV_I225_LMVP,    "I225-LMvP 2.5G"   },
            { INTEL_DEV_I225_VB,      "I225-VB  2.5G"    },
            { INTEL_DEV_I225_IT2,     "I225-IT2 2.5G"    },
            { INTEL_DEV_I225_LM2,     "I225-LM2 2.5G"    },
            { INTEL_DEV_I225_LM3,     "I225-LM3 2.5G"    },
            { INTEL_DEV_I225_V2,      "I225-V2  2.5G"    },
            { INTEL_DEV_I225_VARIANT, "I225 variant"     },
        };
        static const int kI225VariantCount =
            (int)(sizeof(kI225Variants) / sizeof(kI225Variants[0]));

        printf("  [INFO] Full I225 variant table (%d entries):\n", kI225VariantCount);
        const char *matchName = NULL;
        for (int vi = 0; vi < kI225VariantCount; vi++) {
            int current = (kI225Variants[vi].did == i225->device_id) ? 1 : 0;
            printf("  [INFO]   DID=0x%04X  %-20s %s\n",
                   kI225Variants[vi].did,
                   kI225Variants[vi].name,
                   current ? "<-- THIS DEVICE" : "");
            if (current) { matchName = kI225Variants[vi].name; }
        }

        if (matchName) {
            printf("  [PASS] DID=0x%04X recognized as %s\n",
                   (unsigned)i225->device_id, matchName);
            stats.passed++; stats.total++;
        } else {
            printf("  [FAIL] DID=0x%04X not in recognized I225 variant table\n",
                   (unsigned)i225->device_id);
            printf("         Update kI225Variants[] in avb_test_i225.c\n");
            stats.failed++; stats.total++;
        }
    }

    /* ── Test 8: TSN TAS Accepted (SETUP_TAS must not return NOT_SUPPORTED) ── */
    /*
     * I225 has INTEL_CAP_TSN_TAS. SETUP_TAS should be accepted by the driver
     * (capability gate should pass). A safe minimal config: all gates open,
     * 1 ms cycle. Hardware programming may still fail (e.g. timing not yet
     * configured) — we only verify the gate is NOT blocking.
     */
    printf("\n--- Test 8: TSN TAS Gate Open (SETUP_TAS accepted) ---\n");
    {
        AVB_TAS_REQUEST tasReq;
        ZeroMemory(&tasReq, sizeof(tasReq));
        tasReq.config.cycle_time_ns = 1000000U;   /* 1 ms */
        memset(tasReq.config.gate_states, 0xFF, sizeof(tasReq.config.gate_states));
        result = DeviceIoControl(h, IOCTL_AVB_SETUP_TAS,
                                 &tasReq, sizeof(tasReq), &tasReq, sizeof(tasReq),
                                 &bytesReturned, NULL);
        DWORD err = GetLastError();
        if (!result && err == ERROR_NOT_SUPPORTED) {
            AVB_REPORT_FAIL(&stats, "SETUP_TAS",
                            "Returned ERROR_NOT_SUPPORTED on I225 — capability gate incorrectly blocking");
        } else if (result || err != ERROR_NOT_SUPPORTED) {
            /* Any non-NOT_SUPPORTED result means capability gate passed */
            printf("  [PASS] SETUP_TAS capability gate open (result=%d err=%lu status=0x%08X)\n",
                   result, err, tasReq.status);
            stats.passed++; stats.total++;
        }
    }

    /* ── Test 9: TSN FP Accepted (SETUP_FP must not return NOT_SUPPORTED) ── */
    printf("\n--- Test 9: TSN FP Gate Open (SETUP_FP accepted) ---\n");
    {
        AVB_FP_REQUEST fpReq;
        ZeroMemory(&fpReq, sizeof(fpReq));
        fpReq.config.min_fragment_size = 60U;
        fpReq.config.preemptable_queues = 0x0FU;  /* queues 0-3 preemptable */
        result = DeviceIoControl(h, IOCTL_AVB_SETUP_FP,
                                 &fpReq, sizeof(fpReq), &fpReq, sizeof(fpReq),
                                 &bytesReturned, NULL);
        DWORD err = GetLastError();
        if (!result && err == ERROR_NOT_SUPPORTED) {
            AVB_REPORT_FAIL(&stats, "SETUP_FP",
                            "Returned ERROR_NOT_SUPPORTED on I225 — capability gate incorrectly blocking");
        } else {
            printf("  [PASS] SETUP_FP capability gate open (result=%d err=%lu status=0x%08X)\n",
                   result, err, fpReq.status);
            stats.passed++; stats.total++;
        }
    }

    /* ── Test 10: PCIe PTM Accepted (SETUP_PTM must not return NOT_SUPPORTED) ── */
    printf("\n--- Test 10: PCIe PTM Gate Open (SETUP_PTM accepted) ---\n");
    {
        AVB_PTM_REQUEST ptmReq;
        ZeroMemory(&ptmReq, sizeof(ptmReq));
        ptmReq.config.enabled = 1;
        result = DeviceIoControl(h, IOCTL_AVB_SETUP_PTM,
                                 &ptmReq, sizeof(ptmReq), &ptmReq, sizeof(ptmReq),
                                 &bytesReturned, NULL);
        DWORD err = GetLastError();
        if (!result && err == ERROR_NOT_SUPPORTED) {
            AVB_REPORT_FAIL(&stats, "SETUP_PTM",
                            "Returned ERROR_NOT_SUPPORTED on I225 — capability gate incorrectly blocking");
        } else {
            printf("  [PASS] SETUP_PTM capability gate open (result=%d err=%lu status=0x%08X)\n",
                   result, err, ptmReq.status);
            stats.passed++; stats.total++;
        }
    }

    /* ── Test 11: MDIO PHY Register Read ── */
    printf("\n--- Test 11: MDIO PHY Register Read ---\n");
    {
        AVB_MDIO_REQUEST mdioReq;

        ZeroMemory(&mdioReq, sizeof(mdioReq));
        mdioReq.page = 0;
        mdioReq.reg  = 0;  /* PHY Control Register */
        result = DeviceIoControl(h, IOCTL_AVB_MDIO_READ,
                                 &mdioReq, sizeof(mdioReq),
                                 &mdioReq, sizeof(mdioReq),
                                 &bytesReturned, NULL);
        if (!result) {
            AVB_REPORT_FAIL(&stats, "IOCTL_AVB_MDIO_READ", "DeviceIoControl failed");
        } else if (mdioReq.status != 0) {
            printf("  [FAIL] MDIO_READ status=0x%08X — driver rejected request\n",
                   mdioReq.status);
            stats.failed++; stats.total++;
        } else if (mdioReq.value == 0xFFFFU) {
            AVB_REPORT_FAIL(&stats, "MDIO PHY reg 0",
                            "Value=0xFFFF — MDIO bus non-responsive");
        } else {
            printf("  [PASS] PHY Control Reg (reg 0) = 0x%04X\n", (unsigned)mdioReq.value);
            stats.passed++; stats.total++;
        }
    }

    CloseHandle(h);
    return AvbPrintSummary(&stats);
}
