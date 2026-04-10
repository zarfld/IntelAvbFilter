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

/* I219 PCI Device ID range — add new SKUs as they ship */
static int is_i219(avb_u16 did)
{
    return (did == 0x15B7 || did == 0x15B8 ||           /* Gen 1 */
            did == 0x15BB || did == 0x15BC ||           /* Gen 4 */
            did == 0x15BD || did == 0x15BE ||           /* Gen 5 */
            did == 0x15DF || did == 0x15E0 ||           /* Gen 13 */
            did == 0x15E1 || did == 0x15E2 ||           /* Gen 9 */
            did == 0x15E3 ||                            /* Gen 10 */
            did == 0x0D4F || did == 0x0D4E ||           /* Gen 14 */
            did == 0x0D4C || did == 0x0D4D ||           /* Gen 15 */
            did == 0x0D4A ||                            /* Gen 16 */
            did == 0x550F || did == 0x5510 ||           /* Gen 18 */
            did == 0x5511 || did == 0x5512 ||           /* Gen 19 */
            did == 0x5513 || did == 0x5514);            /* Gen 20 */
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

    /* ── Test 3: CTRL and STATUS registers ── */
    printf("\n--- Test 3: Register Access ---\n");
    {
        avb_u32 v = 0;
        int rc = AvbReadReg(h, 0x00000, &v);
        if (rc == 0) { printf("  [PASS] CTRL (0x00000) = 0x%08X\n", (unsigned)v); stats.passed++; stats.total++; }
        else          { printf("  [FAIL] CTRL read failed (error %d)\n", rc);        stats.failed++; stats.total++; }
    }
    {
        avb_u32 v = 0;
        int rc = AvbReadReg(h, 0x00008, &v);
        if (rc == 0) { printf("  [PASS] STATUS (0x00008) = 0x%08X%s\n", (unsigned)v,
                               (v & 0x2u) ? " [link up]" : " [link down]"); stats.passed++; stats.total++; }
        else          { printf("  [FAIL] STATUS read failed (error %d)\n", rc);                           stats.failed++; stats.total++; }
    }

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

        /* Verify TSYNCTXCTL EN bit at I219-specific offset 0xB614 */
        avb_u32 txctl = 0;
        if (AvbReadReg(h, 0xB614, &txctl) == 0) {
            printf("  [INFO] TSYNCTXCTL (0xB614) = 0x%08X\n", (unsigned)txctl);
            if (txctl & 0x10u) { AVB_REPORT_PASS(&stats, "TSYNCTXCTL EN bit (bit4) set"); }
            else               { AVB_REPORT_FAIL(&stats, "TSYNCTXCTL EN bit", "EN bit not set at 0xB614"); }
        } else {
            AVB_REPORT_FAIL(&stats, "TSYNCTXCTL read", "AvbReadReg(0xB614) failed");
        }

        /* Verify TSYNCRXCTL EN bit at I219-specific offset 0xB620 */
        avb_u32 rxctl = 0;
        if (AvbReadReg(h, 0xB620, &rxctl) == 0) {
            DWORD en   = (rxctl >> 4) & 0x1u;
            DWORD type = (rxctl >> 1) & 0x7u;
            printf("  [INFO] TSYNCRXCTL (0xB620) = 0x%08X (EN=%lu TYPE=%lu)\n",
                   (unsigned)rxctl, (unsigned long)en, (unsigned long)type);
            if (en)     { AVB_REPORT_PASS(&stats, "TSYNCRXCTL EN bit (bit4) set"); }
            else        { AVB_REPORT_FAIL(&stats, "TSYNCRXCTL EN bit", "EN bit not set at 0xB620"); }
            if (type == 4u) { AVB_REPORT_PASS(&stats, "TSYNCRXCTL TYPE=ALL_PKTS(4)"); }
            else            { printf("  [FAIL] TSYNCRXCTL TYPE=%lu, expected 4 (ALL_PKTS)\n", (unsigned long)type);
                              stats.failed++; stats.total++; }
        } else {
            AVB_REPORT_FAIL(&stats, "TSYNCRXCTL read", "AvbReadReg(0xB620) failed");
        }
    }

    /* ── Test 5: Hardware PTP clock (SYSTIML/SYSTIMH) ── */
    printf("\n--- Test 5: Hardware PTP Clock ---\n");
    {
        avb_u32 stml = 0, stmh = 0;
        int rcl = AvbReadReg(h, 0xB600, &stml);  /* I219_SYSTIML — latch read */
        int rch = AvbReadReg(h, 0xB604, &stmh);  /* I219_SYSTIMH */

        if (rcl == 0 && rch == 0) {
            avb_u64 hwTime = ((avb_u64)stmh << 32) | stml;
            printf("  [INFO] SYSTIML (0xB600)=0x%08X SYSTIMH (0xB604)=0x%08X\n",
                   (unsigned)stml, (unsigned)stmh);
            printf("  [INFO] Hardware PTP time = 0x%016llX (%llu ns)\n",
                   (unsigned long long)hwTime, (unsigned long long)hwTime);
            if (hwTime != 0) { AVB_REPORT_PASS(&stats, "Hardware PTP clock running (non-zero)"); }
            else              { AVB_REPORT_FAIL(&stats, "Hardware PTP clock", "Time is zero — TIMINCA not initialized"); }
        } else {
            AVB_REPORT_FAIL(&stats, "SYSTIML/SYSTIMH read", "AvbReadReg failed");
        }
    }

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

    CloseHandle(h);
    return AvbPrintSummary(&stats);
}
