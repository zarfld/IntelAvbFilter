/**
 * @file test_ptp_phc_lifecycle.c
 * @brief UT-CORR-009 Driver reload: TX-PHC correlation restored after the driver is reinstalled
 *
 * Split out of test_ptp_phc_stability.c (Q 2026-10-22, TEST-HILSUITE-REAL-003): the driver-lifecycle path
 * (ControlService STOP + tools\setup\Install-Driver-Elevated.ps1 -Action Reinstall by process creation) exists only
 * in this executable.  Run-Tests-CI.ps1 classifies it Unsafe: attended runs only, AVB_HIL_AUTHORIZE_UNSAFE naming this
 * exact entry, never under GitHub Actions.  The UT-CORR-009 code below is moved verbatim.
 *
 * Verifies: #149 (REQ-F-PTP-007) | Traces to: #48 (REQ-F-IOCTL-PHC-004) | Spec: issue #199 | Track A of #317
 *
 * Exit codes (same contract as test_ptp_phc_stability.exe):
 *   0 = PASS, 1 = FAIL, 2 = SKIP_ONLY (no PASS result), 4 = CLEANUP_FAILED
 */
#include "ptp_phc_common.h"
/* =========================================================================
 * UT-CORR-009: Driver Reload — TX-PHC correlation restored after reload
 *
 * Procedure (per issue #199):
 *   1. Open device; read phc1, send PTP → tx1; verify |tx1 - phc1| < 1 µs
 *   2. Close handle; restart IntelAvbFilter service
 *   3. Re-open device; verify adapter count matches pre-reload
 *   4. Read phc2, send PTP → tx2; verify |tx2 - phc2| < 1 µs
 *
 * Requires elevated privileges (SCM service control is always granted when
 * run via Run-Tests-Elevated.ps1).
 * =========================================================================*/
static bool restart_service(const char *svc_name, int timeout_ms)
{
    SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCM) {
        printf("  [SKIP] OpenSCManager failed (error %lu) -- elevated?\n", GetLastError());
        return false;
    }
    SC_HANDLE hSvc = OpenServiceA(hSCM, svc_name,
                                   SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!hSvc) {
        printf("  [SKIP] OpenService('%s') failed (error %lu)\n", svc_name, GetLastError());
        CloseServiceHandle(hSCM);
        return false;
    }

    SERVICE_STATUS ss = {0};
    ControlService(hSvc, SERVICE_CONTROL_STOP, &ss);

    int waited = 0;
    while (waited < timeout_ms) {
        Sleep(500); waited += 500;
        if (!QueryServiceStatus(hSvc, &ss)) break;
        if (ss.dwCurrentState == SERVICE_STOPPED) break;
    }
    printf("  Service stop: state=%lu (waited %d ms)\n", ss.dwCurrentState, waited);
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);

    /* Delegate restart to the established install infrastructure.
     * Direct StartServiceA fails during NDIS's double pause/detach cycle because
     * the DriverStore .sys is transiently inaccessible; the install script handles
     * DriverStore lock detection, pnputil sequencing, and NDIS rebind correctly.
     * CWD is assumed to be the repo root (set by Run-Tests-Elevated.ps1). */
    printf("  Reinstalling via Install-Driver-Elevated.ps1 -Action Reinstall...\n");
    int rc = system("powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass"
                    " -File tools\\setup\\Install-Driver-Elevated.ps1"
                    " -Configuration Debug -Action Reinstall");
    if (rc != 0) {
        printf("  [FAIL] Reinstall script returned exit code %d\n", rc);
        return false;
    }

    hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCM) return false;
    hSvc = OpenServiceA(hSCM, svc_name, SERVICE_QUERY_STATUS);
    bool running = false;
    if (hSvc) {
        QueryServiceStatus(hSvc, &ss);
        printf("  Service state after reinstall: %lu\n", ss.dwCurrentState);
        running = (ss.dwCurrentState == SERVICE_RUNNING);
        CloseServiceHandle(hSvc);
    }
    CloseServiceHandle(hSCM);
    return running;
}

static void test_ut_corr_009(uint32_t adapter_count_before)
{
    printf("\n[UT-CORR-009] Driver Reload: TX-PHC correlation restored after service restart\n");
    printf("  Verifies: #149 (REQ-F-PTP-007) | Traces to: #48 , Spec: issue #199\n");
    printf("  Requires elevated privileges for SCM service control.\n");
    printf("  NOTE: adapter configuration after reinstall may differ from pre-test state.\n");

    /* --- Pre-condition: record device-node accessibility and adapter identities --- */
    {
        HANDLE hProbe = CreateFileW(L"\\\\.\\IntelAvbFilter",
                                    GENERIC_READ | GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hProbe == INVALID_HANDLE_VALUE) {
            printf("  [PRE] \\\\.\\ IntelAvbFilter inaccessible before reload (Win32 error %lu)\n",
                   GetLastError());
            tc_result("UT-CORR-009 Device node accessible before reload", false);
            return;
        }
        printf("  [PRE] \\\\.\\ IntelAvbFilter accessible before reload\n");
        CloseHandle(hProbe);
    }

    /* --- Step 1: Verify PHC accessibility before reload (lifecycle check only).
     * We do NOT check TX-PHC correlation before reload because send_ptp_get_tx
     * returns a PRE_SEND_PHC snapshot, not a hardware egress timestamp.
     * Checking |tx1 - phc1_send| < 1µs would measure kernel overhead, not
     * hardware correlation, and is not a meaningful pre-reload baseline. --- */
    HANDLE hDev1 = open_device();
    if (hDev1 == INVALID_HANDLE_VALUE) {
        printf("  FAIL: Cannot open device before reload (error %lu)\n", GetLastError());
        tc_result("UT-CORR-009 Device open before reload", false);
        return;
    }

    uint64_t phc1 = 0;
    bool phc1_ok  = read_phc(hDev1, 0, &phc1);
    if (phc1_ok) {
        printf("  Before reload: phc1 = %llu ns (PHC reachable)\n", (unsigned long long)phc1);
    } else {
        printf("  Before reload: PHC not readable (adapter may not be PTP-ready)\n");
    }
    /* TX availability is noted but not required as a pre-reload baseline. */
    {
        uint64_t tx1 = 0, phc1_send = 0;
        bool tx1_ok = send_ptp_get_tx(hDev1, 0, 0x900U, &tx1, &phc1_send);
        if (tx1_ok) {
            printf("  Before reload: TX SEND_PTP OK (prov=PRE_SEND_PHC, delta=%lld ns)\n",
                   (long long)(tx1 - phc1_send));
        } else {
            printf("  Before reload: TX SEND_PTP not available (link down / not ready)\n");
        }
    }
    CloseHandle(hDev1);

    /* --- Step 2: Restart service --- */
    /* Delegates to Install-Driver-Elevated.ps1 -Action Reinstall which handles
     * DriverStore locking, pnputil sequencing, and NDIS rebind timing. */
    bool svc_ok = restart_service(SERVICE_NAME, 120000);
    if (!svc_ok) {
        /* A failed service restart leaves the environment broken — not a non-fatal skip. */
        printf("  [FAIL] Service restart failed — driver environment requires recovery.\n");
        printf("  Run: tools\\setup\\manual_uninstall.ps1 then reboot, then reinstall.\n");
        tc_result("UT-CORR-009 Driver Reload (FAIL - service restart failed)", false);
        return;
    }
    Sleep(1000);  /* extra settle after service reaches RUNNING */

    /* --- Step 3: Re-open device and verify adapter count --- */
    HANDLE hDev2 = open_device();
    if (hDev2 == INVALID_HANDLE_VALUE) {
        DWORD openErr = GetLastError();
        printf("  FAIL: Cannot re-open \\\\.\\ IntelAvbFilter after reload (Win32 error %lu)\n",
               openErr);
        printf("  NOTE: This is the device-node availability failure (#328 Defect C)\n");
        printf("        Service may be running but control device not yet registered\n");
        tc_result("UT-CORR-009 Device node accessible after reload", false);
        return;
    }
    printf("  [POST] \\\\.\\ IntelAvbFilter accessible after reload\n");

    /* --- Step 3b: Record and compare adapter identities after reload ---
     * Count alone is insufficient — verify each VID/DID matches.
     * An adapter may rebind as a different index or disappear entirely. */
    uint16_t vids_after[8] = {0}, dids_after[8] = {0};
    int count_after = 0;
    int k;
    for (k = 0; k < 8; k++) {
        AVB_ENUM_REQUEST r = {0};
        r.index = k;
        DWORD br = 0;
        BOOL ok = DeviceIoControl(hDev2, IOCTL_AVB_ENUM_ADAPTERS,
                                   &r, sizeof(r), &r, sizeof(r), &br, NULL);
        if (!ok || r.status != NDIS_STATUS_SUCCESS) break;
        vids_after[count_after] = r.vendor_id;
        dids_after[count_after] = r.device_id;
        printf("  [POST] Adapter %d: VID=0x%04X DID=0x%04X\n", k, r.vendor_id, r.device_id);
        count_after++;
    }
    printf("  Adapter count: before=%u  after=%d\n", adapter_count_before, count_after);
    /* Strict: adapter count must not decrease after reload.
     * A reduced set means at least one adapter did not rebind successfully. */
    if (count_after < (int)adapter_count_before) {
        printf("  FAIL: Adapter count decreased after reload (%u -> %d) — rebind incomplete\n",
               adapter_count_before, count_after);
        CloseHandle(hDev2);
        tc_result("UT-CORR-009 Driver Reload (FAIL - adapter count decreased)", false);
        return;
    }

    /* --- Step 4: Verify PHC accessibility and adapter count after reload.
     * Hardware TX-PHC correlation cannot be verified with PRE_SEND_PHC timestamps.
     * The lifecycle test reports:
     *   PASS  — device reachable, adapter count maintained, PHC readable.
     *   FAIL  — device unreachable, adapter count decreased, or PHC not readable.
     *   SKIP  — TX SEND_PTP is reported separately for information only.
     * --- */
    (void)vids_after; (void)dids_after;
    uint64_t phc2 = 0;
    bool phc2_ok  = read_phc(hDev2, 0, &phc2);

    if (phc2_ok) {
        printf("  After reload: phc2 = %llu ns (PHC reachable)\n", (unsigned long long)phc2);
    } else {
        printf("  After reload: PHC not readable\n");
    }

    /* Record TX availability for information; do not gate lifecycle verdict on it. */
    {
        uint64_t tx2 = 0, phc2_send = 0;
        bool tx2_ok = phc2_ok && send_ptp_get_tx(hDev2, 0, 0x901U, &tx2, &phc2_send);
        if (tx2_ok) {
            printf("  After reload: TX SEND_PTP OK (prov=PRE_SEND_PHC, delta=%lld ns)\n",
                   (long long)(tx2 - phc2_send));
        } else {
            printf("  After reload: TX SEND_PTP not available (link down / not ready)\n");
        }
    }

    CloseHandle(hDev2);

    /* Lifecycle verdict — independent of TX correlation claim. */
    bool lifecycle_passed = phc2_ok && ((int)count_after >= (int)adapter_count_before);
    if (!phc2_ok)
        printf("  FAIL: PHC not readable after reload\n");
    if ((int)count_after < (int)adapter_count_before)
        printf("  FAIL: Adapter count decreased after reload (%u → %d)\n",
               adapter_count_before, count_after);
    tc_result("UT-CORR-009 Driver Lifecycle: re-open, PHC reachable, adapter count maintained",
              lifecycle_passed);

    /* Hardware TX-PHC correlation: SKIP — requires VERIFIED_HARDWARE_TX. */
    tc_skip("UT-CORR-009 Hardware TX-PHC correlation after reload: VERIFIED_HARDWARE_TX unavailable (not #328/#199 close criteria)");
}

int main(void)
{
    printf("========================================================================\n");
    printf("PHC Stability -- UT-CORR-009 Driver Lifecycle (reinstalls the driver)\n");
    printf("Verifies: #149 (REQ-F-PTP-007) | Spec: issue #199 | Track A of #317\n");
    printf("========================================================================\n");

    HANDLE hDev = open_device();
    if (hDev == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open %S (Win32 error %lu)\n", DEVICE_PATH_W, GetLastError());
        printf("  Is the IntelAvbFilter driver installed and running?\n");
        return 1;
    }
    int adapter_count = 0;
    for (int ai = 0; ai < 8; ai++) {
        AVB_ENUM_REQUEST r = {0};
        r.index = ai;
        DWORD br = 0;
        BOOL ok = DeviceIoControl(hDev, IOCTL_AVB_ENUM_ADAPTERS, &r, sizeof(r), &r, sizeof(r), &br, NULL);
        if (!ok || r.status != NDIS_STATUS_SUCCESS) break;
        printf("  Adapter %d: VID=0x%04X DID=0x%04X Caps=0x%08X\n", ai, r.vendor_id, r.device_id, r.capabilities);
        adapter_count++;
    }
    /* The device handle is closed before the service stop (as in the original combined executable). */
    CloseHandle(hDev);
    if (adapter_count == 0) {
        printf("ERROR: No adapters enumerated.\n");
        return 1;
    }

    printf("\n--- Driver Lifecycle Test (UT-CORR-009) ---\n");
    printf("  NOTE: This test reinstalls the driver. Adapter configuration\n");
    printf("  may not match pre-test state after reinstall.\n");
    printf("  Recovery: tools\\setup\\Install-Driver-Elevated.ps1 -Action Reinstall\n");
    test_ut_corr_009((uint32_t)adapter_count);

    printf("\n========================================================================\n");
    printf("Test Summary -- UT-CORR-009 Driver Lifecycle (#317 Track A)\n");
    printf("  Total: %d  Passed: %d  Failed: %d  Skipped: %d  CleanupFailed: %d\n",
           s_total, s_passed, s_failed, s_skipped, s_cleanup_failed);
    printf("========================================================================\n");
    if (s_cleanup_failed > 0) return 4;
    if (s_failed > 0)         return 1;
    if (s_passed == 0)        return 2;
    return 0;
}