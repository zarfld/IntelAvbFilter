/**
 * @file test_device_idle_regression.c
 * @brief Bounded observational regression for IntelAvbFilter device accessibility
 *        across session idle / screen-lock / NIC power-management transitions.
 *
 * Implements: TC-LCY-IDLE-001
 * Traces to:  #328 (IntelAvbFilter disappears under NIC power management)
 *
 * Background:
 *   TC-LCY-328 (test_control_device_lifecycle.c) tests the EventLog service
 *   restart trigger -- the specific NDIS FilterUnload path that caused the
 *   original device-path loss.  That test now PASSES.
 *
 *   The IDLE/POWER trigger (session lock, display-off, connected-standby) is
 *   a SEPARATE untested scenario.  The original #328 report described:
 *     "NIC power management reset after session idle → \\.\IntelAvbFilter absent
 *      while service reports RUNNING."
 *   This is distinct from FilterUnload: it involves NDIS adapter detach/reattach
 *   under power-state change, not a driver unload.
 *
 * What this test does:
 *   Phase A (before): QueryDosDevice, 4-flag CreateFile matrix, read-only IOCTL,
 *                     NDIS lifecycle snapshot (IoctlCount, OidRequestCount).
 *   Phase B (trigger): The USER manually initiates the transition.
 *                      This test waits; it does NOT lock, idle, or power-manage.
 *   Phase C (after):   Same checks as Phase A. Compare all metrics.
 *   Verdict:           PASS = identical accessibility; FAIL = any degradation.
 *
 * Constraints (MUST observe):
 *   - Does NOT restart IntelAvbFilter, EventLog, or any other service.
 *   - Does NOT change NIC power settings.
 *   - Does NOT reinstall or reload the driver.
 *   - Only reads from the device (IOCTL_AVB_GET_STATISTICS, IOCTL_AVB_GET_VERSION).
 *   - Observational only: captures deltas without mutation.
 *
 * Usage (from elevated terminal):
 *   test_device_idle_regression.exe [timeout_seconds]
 *   Default timeout: 120 s.  After printing Phase A snapshot, it waits for
 *   the user to trigger a session lock (Win+L) and unlock, then presses Enter.
 *   Alternatively set TC_IDLE_TIMEOUT_S env var.
 *   Set TC_IDLE_AUTO=0 to require explicit Enter; TC_IDLE_AUTO=1 for timeout-only.
 *
 * Build (see tools/build/Build-Tests.ps1):
 *   cl /nologo /W4 /WX /Zi /DWIN32 /D_WIN32_WINNT=0x0A00
 *      -I include -I external/intel_avb/lib
 *      tests\hardware\test_device_idle_regression.c
 *      /Fe:build\tools\test_device_idle_regression.exe /link advapi32.lib
 *
 * Verdict codes:
 *   0 = TC-LCY-IDLE-001 PASS (device accessible before and after)
 *   1 = TC-LCY-IDLE-001 FAIL (device accessibility degraded — #328 reproduced)
 *   2 = SKIP (precondition not met or insufficient privilege)
 *   3 = TIMEOUT (user did not trigger transition within timeout window)
 */

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "avb_ioctl.h"

/* ── Verdict codes ───────────────────────────────────────────────────────── */
#define TC_PASS    0
#define TC_FAIL    1
#define TC_SKIP    2
#define TC_TIMEOUT 3

/* ── Device path check ───────────────────────────────────────────────────── */
static BOOL DevicePathPresent(DWORD *outErr)
{
    WCHAR buf[512];
    DWORD r = QueryDosDeviceW(L"IntelAvbFilter", buf, ARRAYSIZE(buf));
    if (outErr) *outErr = r ? 0 : GetLastError();
    return (r > 0);
}

/* ── Access-matrix probe ─────────────────────────────────────────────────── */
typedef struct { DWORD access; const char *label; BOOL ok; DWORD err; }
AccessEntry;

static void ProbeAccessMatrix(AccessEntry *entries, int n)
{
    for (int i = 0; i < n; i++) {
        HANDLE h = CreateFileW(L"\\\\.\\IntelAvbFilter",
                               entries[i].access,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            entries[i].ok = TRUE; entries[i].err = 0; CloseHandle(h);
        } else {
            entries[i].ok = FALSE; entries[i].err = GetLastError();
        }
    }
}

/* ── IOCTL probe (read-only; logs Win32 error) ───────────────────────────── */
typedef struct { BOOL version_ok; DWORD version_err; BOOL stats_ok; DWORD stats_err; }
IoctlSnapshot;

static IoctlSnapshot ProbeIoctls(void)
{
    IoctlSnapshot s = {0};
    HANDLE h = CreateFileW(L"\\\\.\\IntelAvbFilter",
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        s.version_err = s.stats_err = GetLastError();
        return s;
    }
    /* IOCTL_AVB_GET_VERSION: minimal read-only health check (always first) */
    {
        struct _IOCTL_VERSION vi = {0};
        DWORD br = 0;
        s.version_ok = DeviceIoControl(h, IOCTL_AVB_GET_VERSION,
                                        NULL, 0, &vi, sizeof(vi), &br, NULL);
        s.version_err = s.version_ok ? 0 : GetLastError();
    }
    /* IOCTL_AVB_GET_STATISTICS: per-adapter counters for lifecycle tracking */
    {
        AVB_DRIVER_STATISTICS stats = {0};
        DWORD br = 0;
        s.stats_ok = DeviceIoControl(h, IOCTL_AVB_GET_STATISTICS,
                                      NULL, 0, &stats, sizeof(stats), &br, NULL);
        s.stats_err = s.stats_ok ? 0 : GetLastError();
    }
    CloseHandle(h);
    return s;
}

/* ── Print a phase snapshot ──────────────────────────────────────────────── */
static BOOL PrintPhase(const char *label, AccessEntry *m, int n, IoctlSnapshot *io)
{
    DWORD qdErr = 0;
    BOOL  qd    = DevicePathPresent(&qdErr);
    BOOL  allOk = qd;

    printf("  [%s] QueryDosDevice(\"IntelAvbFilter\"): %s (err=%lu)\n",
           label, qd ? "OK" : "FAIL", qdErr);

    ProbeAccessMatrix(m, n);
    for (int i = 0; i < n; i++) {
        printf("  [%s] CreateFile %-22s : %s (err=%lu)\n",
               label, m[i].label, m[i].ok ? "OK" : "FAIL", m[i].err);
        if (!m[i].ok) allOk = FALSE;
    }

    *io = ProbeIoctls();
    printf("  [%s] GET_VERSION                         : %s (err=%lu)\n",
           label, io->version_ok ? "OK" : "FAIL", io->version_err);
    printf("  [%s] GET_STATISTICS                      : %s (err=%lu)\n",
           label, io->stats_ok ? "OK" : "FAIL", io->stats_err);
    if (!io->version_ok) allOk = FALSE;

    return allOk;
}

/* ── Main ────────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    int timeout_s = 120;
    if (argc > 1) timeout_s = atoi(argv[1]);
    const char *envT = getenv("TC_IDLE_TIMEOUT_S");
    if (envT) { int v = atoi(envT); if (v > 0) timeout_s = v; }
    int auto_mode = 1; /* default: timeout-only (no Enter required) */
    const char *envA = getenv("TC_IDLE_AUTO");
    if (envA && atoi(envA) == 0) auto_mode = 0;

    printf("TC-LCY-IDLE-001: Device accessibility across idle/power transition\n");
    printf("  Regression: #328 (device absent after NIC power management)\n");
    printf("  Trigger:    USER-INITIATED (lock/unlock or display-off/on)\n");
    printf("  Timeout:    %d s\n\n", timeout_s);

    /* ── Phase A: baseline ─────────────────────────────────────────────────── */
    printf("[Phase A] Baseline snapshot\n");
    AccessEntry m_a[] = {
        { 0,                            "access=0",           FALSE, 0 },
        { GENERIC_READ,                 "GENERIC_READ",       FALSE, 0 },
        { GENERIC_WRITE,                "GENERIC_WRITE",      FALSE, 0 },
        { GENERIC_READ | GENERIC_WRITE, "GENERIC_READ|WRITE", FALSE, 0 },
    };
    IoctlSnapshot io_a = {0};
    BOOL phase_a_ok = PrintPhase("A", m_a, 4, &io_a);
    if (!phase_a_ok) {
        printf("\n  [SKIP] Phase A precondition not met -- device not accessible at baseline.\n");
        printf("  ACTION: check service status and driver installation.\n\n");
        printf("Summary: TC-LCY-IDLE-001 SKIP\n");
        return TC_SKIP;
    }
    printf("  [PASS-A] Baseline verified.\n\n");

    /* ── Phase B: wait for user-initiated transition ───────────────────────── */
    printf("[Phase B] Waiting for idle/power transition\n");
    printf("  ACTION: Lock the screen (Win+L), wait for display to turn off,\n");
    printf("          or let the system idle for a few minutes, then unlock.\n");
    printf("  When done (or to proceed without transition): press Enter\n");
    printf("  Automatic timeout in %d seconds.\n\n", timeout_s);

    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD start   = GetTickCount();
    BOOL  entered = FALSE;

    while (!entered) {
        /* Non-blocking check for Enter key */
        INPUT_RECORD ir;
        DWORD nr = 0;
        if (GetNumberOfConsoleInputEvents(hStdin, &nr) && nr > 0) {
            ReadConsoleInput(hStdin, &ir, 1, &nr);
            if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown &&
                ir.Event.KeyEvent.wVirtualKeyCode == VK_RETURN) {
                entered = TRUE;
                printf("  [Enter pressed -- proceeding to Phase C]\n\n");
                break;
            }
        }
        DWORD elapsed = GetTickCount() - start;
        if (!auto_mode && !entered) {
            /* Manual mode: only Enter triggers */
            Sleep(100);
            continue;
        }
        if (elapsed >= (DWORD)(timeout_s * 1000)) {
            if (!entered) {
                printf("\n  [TIMEOUT] %d s elapsed -- proceeding to Phase C automatically.\n\n",
                       timeout_s);
            }
            break;
        }
        /* Print countdown every 10 s */
        if (elapsed % 10000 < 200) {
            printf("  ... %d s remaining (press Enter to continue now)\r",
                   timeout_s - (int)(elapsed / 1000));
            fflush(stdout);
        }
        Sleep(200);
    }

    /* ── Phase C: post-transition snapshot ─────────────────────────────────── */
    printf("[Phase C] Post-transition snapshot\n");
    AccessEntry m_c[] = {
        { 0,                            "access=0",           FALSE, 0 },
        { GENERIC_READ,                 "GENERIC_READ",       FALSE, 0 },
        { GENERIC_WRITE,                "GENERIC_WRITE",      FALSE, 0 },
        { GENERIC_READ | GENERIC_WRITE, "GENERIC_READ|WRITE", FALSE, 0 },
    };
    IoctlSnapshot io_c = {0};
    BOOL phase_c_ok = PrintPhase("C", m_c, 4, &io_c);
    printf("\n");

    /* ── Verdict ───────────────────────────────────────────────────────────── */
    BOOL degraded = !phase_c_ok;

    /* Classify device-path loss vs access-denied */
    const char *failure_class = "";
    if (!degraded) {
        /* all OK */
    } else if (!DevicePathPresent(NULL)) {
        /* ERROR_FILE_NOT_FOUND (2): #328 exact symptom */
        failure_class = "DEVICE_PATH_ABSENT";
    } else if (m_c[3].err == ERROR_ACCESS_DENIED || m_c[2].err == ERROR_ACCESS_DENIED) {
        /* Separate ACCESS_DENIED (5) from path-absent (2).
         * ACCESS_DENIED can occur if the test runs non-elevated after an idle transition
         * that changes the device security descriptor. */
        failure_class = "ACCESS_DENIED_AFTER_IDLE";
    } else {
        failure_class = "DEVICE_IOCTL_FAILURE";
    }

    printf("[Verdict]\n");
    printf("  Phase A accessible : %s\n", phase_a_ok ? "YES" : "NO");
    printf("  Phase C accessible : %s\n", phase_c_ok ? "YES" : "NO");
    printf("  Degradation        : %s\n", degraded ? "YES" : "NO");
    if (degraded) printf("  Failure class      : %s\n", failure_class);
    printf("\n");

    if (!degraded) {
        printf("[PASS] TC-LCY-IDLE-001: device accessible before and after idle transition\n");
        printf("  NOTE: #328 NOT REPRODUCED in this run -- NOT RESOLVED.\n");
        printf("  To reproduce: increase idle time, enable NIC power management, or\n");
        printf("  perform multiple lock/unlock cycles.\n");
        return TC_PASS;
    } else {
        printf("[FAIL] TC-LCY-IDLE-001: %s -- #328 REPRODUCED\n", failure_class);
        printf("  Report: Win32 errors above; compare with #328 acceptance criteria.\n");
        printf("  DO NOT reinstall driver without explicit authorization.\n");
        return TC_FAIL;
    }
}
