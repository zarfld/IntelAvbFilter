/**
 * @file avb_test_i219.c
 * @brief I219-specific user-mode validation test
 *
 * Enumerates all Intel adapters, locates an I219 (DID 0x15B7 family),
 * opens it via IOCTL_AVB_OPEN_ADAPTER, and runs I219-specific hardware checks.
 * Prints [SKIP] and exits 0 if no I219 is present — safe for CI.
 *
 * I219-specific notes:
 *   - MMIO BAR0 PTP block base: 0xB600
 *   - SYSTIML=0xB600, SYSTIMH=0xB604, TSYNCTXCTL=0xB614, TSYNCRXCTL=0xB620
 *   - No TRGTTIM / AUXSTMP registers (2-step only; INTEL_CAP_ENHANCED_TS)
 *   - MDIO via page 800/801 protocol (INTEL_CAP_MDIO)
 *
 * SSOT: ../../../include/avb_ioctl.h (via avb_test_common.h)
 */

#include "../../common/avb_test_common.h"

/* I219 PCI Device ID range — uses SSOT intel_pci_ids.h where constants exist */
static int is_i219(avb_u16 did)
{
    return (did == INTEL_DEV_I219_LM   || did == INTEL_DEV_I219_V   ||   /* Gen 1 */
            did == INTEL_DEV_I219_LM4  || did == INTEL_DEV_I219_V4  ||   /* Gen 4 */
            did == INTEL_DEV_I219_LM5  || did == INTEL_DEV_I219_V5  ||   /* Gen 5 */
            did == 0x15DFU || did == 0x15E0U ||                           /* Gen 13 — no SSOT constant yet */
            did == 0x15E1U || did == 0x15E2U ||                           /* Gen 9 */
            did == INTEL_DEV_I219_LM6  ||                                 /* Gen 10 */
            did == 0x0D4FU || did == 0x0D4EU ||                           /* Gen 14 */
            did == 0x0D4CU || did == 0x0D4DU ||                           /* Gen 15 */
            did == 0x0D4AU ||                                             /* Gen 16 */
            did == 0x550FU || did == 0x5510U ||                           /* Gen 18 */
            did == 0x5511U || did == 0x5512U ||                           /* Gen 19 */
            did == 0x5513U || did == 0x5514U);                            /* Gen 20 */
}

int main(void)
{
    printf("=== Intel I219 User-Mode Test ===\n\n");

    /* Enumerate all adapters */
    AvbAdapterInfo adapters[AVB_MAX_ADAPTERS];
    ZeroMemory(adapters, sizeof(adapters));
    int count = AvbEnumerateAdapters(adapters, AVB_MAX_ADAPTERS);

    if (count == 0) {
        printf("[SKIP] No Intel adapters found via IntelAvbFilter driver.\n");
        printf("       Ensure driver is installed: sc query IntelAvbFilter\n");
        return 0;
    }

    /* Find an I219 */
    const AvbAdapterInfo *i219 = NULL;
    for (int i = 0; i < count; i++) {
        if (is_i219(adapters[i].device_id)) {
            i219 = &adapters[i];
            break;
        }
    }

    if (!i219) {
        printf("[SKIP] No I219 adapter found among %d detected adapter(s).\n", count);
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
        printf("[INFO] Found I219: DID=0x%04X global_index=%u caps=%s\n",
               i219->device_id, i219->global_index,
               AvbCapabilityString(i219->capabilities, caps, sizeof(caps)));
    }

    /* Open a handle bound to the I219 */
    HANDLE h = AvbOpenAdapter(i219);
    if (h == INVALID_HANDLE_VALUE) {
        printf("[FAIL] AvbOpenAdapter failed for I219 (error %lu)\n", GetLastError());
        return 1;
    }

    AvbTestStats stats;
    ZeroMemory(&stats, sizeof(stats));

    DWORD bytesReturned = 0;
    BOOL result;

    /* ── Test 1: Device initialization ── */
    printf("\n--- Test 1: Device Initialization ---\n");
    result = DeviceIoControl(h, IOCTL_AVB_INIT_DEVICE,
                             NULL, 0, NULL, 0, &bytesReturned, NULL);
    if (result) {
        AVB_REPORT_PASS(&stats, "IOCTL_AVB_INIT_DEVICE");
    } else {
        printf("  [WARN] IOCTL_AVB_INIT_DEVICE failed (error %lu) — continuing\n", GetLastError());
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

    /* Test 3: Register Access -- covered by test_hw_state.exe (device-agnostic, runs on all adapters) */

    /* ── Test 4: Enable PTP HW timestamping ── */
    printf("\n--- Test 4: PTP Hardware Timestamping Enable ---\n");
    {
        AVB_HW_TIMESTAMPING_REQUEST hwts;
        ZeroMemory(&hwts, sizeof(hwts));
        hwts.enable             = 1;
        hwts.timer_mask         = 1;   /* SYSTIM0 */
        hwts.enable_target_time = 0;   /* I219 has no TRGTTIM */
        hwts.enable_aux_ts      = 0;   /* I219 has no AUXSTMP */

        result = DeviceIoControl(h, IOCTL_AVB_SET_HW_TIMESTAMPING,
                                 &hwts, sizeof(hwts), &hwts, sizeof(hwts),
                                 &bytesReturned, NULL);

        if (result && hwts.status == 0) {
            printf("  [PASS] HW timestamping enabled "
                   "(TSAUXC: 0x%08X -> 0x%08X)\n",
                   hwts.previous_tsauxc, hwts.current_tsauxc);
            stats.passed++; stats.total++;
        } else {
            printf("  [FAIL] SET_HW_TIMESTAMPING failed "
                   "(ok=%d error=%lu status=0x%08X)\n",
                   result, GetLastError(), hwts.status);
            stats.failed++; stats.total++;
        }

        /* Raw TSYNCTXCTL/TSYNCRXCTL register checks covered by test_hw_ts_ctrl.exe */
    }

    /* Test 5: PTP Clock value -- covered by test_ptp_getset.exe (device-agnostic, runs on all adapters) */

    /* ── Test 6: GET_TIMESTAMP IOCTL ── */
    printf("\n--- Test 6: IOCTL_AVB_GET_TIMESTAMP ---\n");
    {
        AVB_TIMESTAMP_REQUEST ts;
        ZeroMemory(&ts, sizeof(ts));
        result = DeviceIoControl(h, IOCTL_AVB_GET_TIMESTAMP,
                                 &ts, sizeof(ts), &ts, sizeof(ts),
                                 &bytesReturned, NULL);
        if (result) {
            printf("  [INFO] Timestamp = 0x%016llX\n", (unsigned long long)ts.timestamp);
            if (ts.timestamp != 0) { AVB_REPORT_PASS(&stats, "GET_TIMESTAMP non-zero"); }
            else                   { AVB_REPORT_FAIL(&stats, "GET_TIMESTAMP", "Returned zero — hardware not initialized"); }
        } else {
            AVB_REPORT_FAIL(&stats, "IOCTL_AVB_GET_TIMESTAMP", "DeviceIoControl failed");
        }
    }

    /* ── Test 7: Capabilities Verification ── */
    /*
     * Addresses: #261 (TEST-COMPAT-I219-001), #114 (QA-SC-PORT-001)
     *
     * I219 must report INTEL_CAP_BASIC_1588 (PTP) and INTEL_CAP_MDIO.
     * It must NOT report TSN features (TAS/FP/2.5G/EEE) — I219 is 1 GbE
     * with no Time-Aware Shaper or Frame Preemption hardware.
     * Expected capabilities: INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS |
     *                        INTEL_CAP_MMIO | INTEL_CAP_MDIO
     */
    printf("\n--- Test 7: Capabilities Verification ---\n");
    {
        char capbuf[128];
        avb_u32 caps = i219->capabilities;
        printf("  [INFO] Capabilities = 0x%08X (%s)\n",
               (unsigned)caps, AvbCapabilityString(caps, capbuf, sizeof(capbuf)));

        /* Must have basic IEEE 1588 PTP support */
        if (caps & INTEL_CAP_BASIC_1588) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_BASIC_1588 present");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_BASIC_1588", "PTP capability not reported — driver bug");
        }

        /* Must have MDIO support (PCH-based PHY access) */
        if (caps & INTEL_CAP_MDIO) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_MDIO present");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_MDIO", "MDIO capability not reported");
        }

        /* Must NOT have TSN Time-Aware Shaper — I219 has no 802.1Qbv hardware */
        if (!(caps & INTEL_CAP_TSN_TAS)) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_TSN_TAS correctly absent (I219 has no TAS)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_TSN_TAS", "TAS reported on I219 — should not be supported");
        }

        /* Must NOT have Frame Preemption — I219 has no 802.1Qbu hardware */
        if (!(caps & INTEL_CAP_TSN_FP)) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_TSN_FP correctly absent (I219 has no FP)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_TSN_FP", "Frame Preemption reported on I219 — should not be supported");
        }

        /* Must NOT report 2.5 GbE — I219 is 1 GbE only */
        if (!(caps & INTEL_CAP_2_5G)) {
            AVB_REPORT_PASS(&stats, "INTEL_CAP_2_5G correctly absent (I219 is 1 GbE)");
        } else {
            AVB_REPORT_FAIL(&stats, "INTEL_CAP_2_5G", "2.5G reported on I219 — hardware is 1 GbE only");
        }
    }

    /* ── Test 8: PTP Clock Monotonicity (via IOCTL_AVB_GET_TIMESTAMP) ── */
    /*
     * Addresses: #261 (TEST-COMPAT-I219-001 — Test 4: PTP Clock Accuracy)
     *
     * Read timestamp via IOCTL_AVB_GET_TIMESTAMP twice with a 10 ms gap.
     * The second reading must be strictly greater than the first.
     * Uses the same IOCTL path as Test 6 (proven to work on I219).
     * Raw per-register access is covered by test_ioctl_phc_monotonicity.exe
     * which runs on all supported adapters.
     */
    printf("\n--- Test 8: PTP Clock Monotonicity ---\n");
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

            Sleep(10);  /* 10 ms — clock must advance by at least ~10,000,000 ns */

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
                                    "T2 <= T1 — clock not advancing or TIMINCA not set");
                }
            }
        }
    }

    /* ── Test 9: I219 Variant Matrix ── */
    /*
     * Addresses: #261 (TEST-COMPAT-I219-001 — Test 7: I219 Variant Matrix)
     *
     * Verify the detected device ID is a recognized I219 variant and print
     * the full variant table in [INFO] lines for the test log.
     * Constants sourced from include/intel_pci_ids.h (SSOT).
     */
    printf("\n--- Test 9: I219 Variant Matrix ---\n");
    {
        static const struct { avb_u16 did; const char *name; } kI219Variants[] = {
            { INTEL_DEV_I219_LM_A0,   "I219-LM (A0)"         },
            { INTEL_DEV_I219_V_A0,    "I219-V  (A0)"         },
            { INTEL_DEV_I219_LM_A1,   "I219-LM (A1)"         },
            { INTEL_DEV_I219_V_A1,    "I219-V  (A1)"         },
            { INTEL_DEV_I219_LM,      "I219-LM"              },
            { INTEL_DEV_I219_V,       "I219-V"               },
            { INTEL_DEV_I219_LM3,     "I219-LM3"             },
            { INTEL_DEV_I219_LM4,     "I219-LM4"             },
            { INTEL_DEV_I219_V4,      "I219-V4"              },
            { INTEL_DEV_I219_LM5,     "I219-LM5"             },
            { INTEL_DEV_I219_V5,      "I219-V5"              },
            { INTEL_DEV_I219_D0,      "I219-V  (Skylake)"    },
            { INTEL_DEV_I219_D1,      "I219-LM (Skylake)"    },
            { INTEL_DEV_I219_D2,      "I219-LM (Kaby Lake)"  },
            { INTEL_DEV_I219_LM_DC7,  "I219-LM (DC7)"        },
            { INTEL_DEV_I219_V6,      "I219-V  (Broadwell)"  },
            { INTEL_DEV_I219_LM6,     "I219-LM (Broadwell)"  },
            { 0x15DFU, "I219-LM (Gen 13 Cannon Lake)"        },
            { 0x15E0U, "I219-V  (Gen 13 Cannon Lake)"        },
            { 0x15E1U, "I219-V  (Gen 9)"                     },
            { 0x15E2U, "I219-LM (Gen 9)"                     },
            { 0x0D4EU, "I219-LM (Gen 14 Tiger Lake)"         },
            { 0x0D4FU, "I219-V  (Gen 14 Tiger Lake)"         },
            { 0x0D4CU, "I219-LM (Gen 15 Alder Lake)"         },
            { 0x0D4DU, "I219-V  (Gen 15 Alder Lake)"         },
            { 0x0D4AU, "I219-LM (Gen 16 Raptor Lake)"        },
            { 0x550FU, "I219-LM (Gen 18 Meteor Lake)"        },
            { 0x5510U, "I219-V  (Gen 18 Meteor Lake)"        },
            { 0x5511U, "I219-LM (Gen 19 Arrow Lake)"         },
            { 0x5512U, "I219-V  (Gen 19 Arrow Lake)"         },
            { 0x5513U, "I219-LM (Gen 20 Panther Lake)"       },
            { 0x5514U, "I219-V  (Gen 20 Panther Lake)"       },
        };
        static const int kI219VariantCount =
            (int)(sizeof(kI219Variants) / sizeof(kI219Variants[0]));

        printf("  [INFO] Full I219 variant table (%d entries):\n", kI219VariantCount);
        const char *matchName = NULL;
        for (int vi = 0; vi < kI219VariantCount; vi++) {
            int current = (kI219Variants[vi].did == i219->device_id) ? 1 : 0;
            printf("  [INFO]   DID=0x%04X  %-34s %s\n",
                   kI219Variants[vi].did,
                   kI219Variants[vi].name,
                   current ? "<-- THIS DEVICE" : "");
            if (current) { matchName = kI219Variants[vi].name; }
        }

        if (matchName) {
            printf("  [PASS] DID=0x%04X recognized as %s\n",
                   (unsigned)i219->device_id, matchName);
            stats.passed++; stats.total++;
        } else {
            printf("  [FAIL] DID=0x%04X not in recognized I219 variant table\n",
                   (unsigned)i219->device_id);
            printf("         Update kI219Variants[] in avb_test_i219.c\n");
            stats.failed++; stats.total++;
        }
    }

    CloseHandle(h);
    return AvbPrintSummary(&stats);
}
