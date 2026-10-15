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
 *   Phase A (before): QueryDosDevice, 4-flag CreateFile matrix, GET_VERSION IOCTL,
 *                     per-adapter lifecycle counter snapshot (via ENUM_ADAPTERS +
 *                     OPEN_ADAPTER + GET_STATISTICS for each bound adapter).
 *   Phase B (trigger): The USER manually initiates the transition.
 *                      This test waits; it does NOT lock, idle, or power-manage.
 *   Phase C (after):   Same checks as Phase A.  Per-adapter identity matched by
 *                      (vendor_id, device_id, enum_index); counters compared.
 *   Verdict:           See idle_verdict.h truth table.
 *
 * Constraints (MUST observe):
 *   - Does NOT restart IntelAvbFilter, EventLog, or any other service.
 *   - Does NOT change NIC power settings.
 *   - Does NOT reinstall or reload the driver.
 *   - Read-only: IOCTL_AVB_GET_VERSION, IOCTL_AVB_ENUM_ADAPTERS,
 *                IOCTL_AVB_OPEN_ADAPTER, IOCTL_AVB_GET_STATISTICS only.
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
 * Verdict codes (from idle_verdict.h):
 *   0 = TC_PASS         NDIS lifecycle confirmed; device accessible before and after
 *   1 = TC_FAIL         Device inaccessible after transition (#328 reproduced)
 *   2 = TC_SKIP         Precondition not met (baseline unavailable)
 *   3 = TC_INCONCLUSIVE Transition not observed, adapter ambiguity, or timeout
 */

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "avb_ioctl.h"
#include "idle_verdict.h"  /* TC_PASS/FAIL/SKIP/TC_INCONCLUSIVE, AdapterSnapshot,
                              IdleVerdictInput, ComputeIdleVerdict() */

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

/* ── IOCTL health check (GET_VERSION) ────────────────────────────────────── */
static BOOL ProbeVersionIoctl(DWORD *outErr)
{
    HANDLE h = CreateFileW(L"\\\\.\\IntelAvbFilter",
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        if (outErr) *outErr = GetLastError();
        return FALSE;
    }
    struct _IOCTL_VERSION vi = {0};
    DWORD br = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AVB_GET_VERSION,
                              NULL, 0, &vi, sizeof(vi), &br, NULL);
    if (outErr) *outErr = ok ? 0 : GetLastError();
    CloseHandle(h);
    return ok;
}

/* ── Per-adapter lifecycle counter snapshot ──────────────────────────────── */
/*
 * Enumerates all filter-bound adapters, opens a fresh device handle per
 * adapter, binds it via IOCTL_AVB_OPEN_ADAPTER, and reads per-adapter
 * statistics into snaps[].
 *
 * Returns the number of adapters enumerated (0..max_snaps).
 * snaps[] entries with stats_valid==0 had a failed OPEN or GET_STATISTICS.
 */
static int SnapshotAdapters(AdapterSnapshot *snaps, int max_snaps)
{
    int i;
    AVB_ENUM_REQUEST req;
    DWORD br;
    int count;

    memset(snaps, 0, (size_t)max_snaps * sizeof(snaps[0]));

    HANDLE hEnum = CreateFileW(L"\\\\.\\IntelAvbFilter",
                               GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hEnum == INVALID_HANDLE_VALUE) return 0;

    /* First ENUM_ADAPTERS call: index=0, returns total count */
    memset(&req, 0, sizeof(req));
    br = 0;
    if (!DeviceIoControl(hEnum, IOCTL_AVB_ENUM_ADAPTERS,
                         &req, sizeof(req), &req, sizeof(req), &br, NULL)) {
        CloseHandle(hEnum);
        return 0;
    }

    count = (int)req.count;
    if (count <= 0) { CloseHandle(hEnum); return 0; }
    if (count > max_snaps) count = max_snaps;

    /* Collect (VID, DID, capabilities) for each adapter index */
    typedef struct { UINT16 vid; UINT16 did; UINT32 idx; } AdInfo;
    AdInfo info[IDLE_MAX_ADAPTERS];
    memset(info, 0, sizeof(info));

    /* Index 0 was already returned above */
    info[0].vid = req.vendor_id;
    info[0].did = req.device_id;
    info[0].idx = 0;

    for (i = 1; i < count; i++) {
        AVB_ENUM_REQUEST r;
        memset(&r, 0, sizeof(r));
        r.index = (UINT32)i;
        br = 0;
        if (DeviceIoControl(hEnum, IOCTL_AVB_ENUM_ADAPTERS,
                            &r, sizeof(r), &r, sizeof(r), &br, NULL)) {
            info[i].vid = r.vendor_id;
            info[i].did = r.device_id;
            info[i].idx = (UINT32)i;
        }
    }
    CloseHandle(hEnum);

    /* Open a fresh handle per adapter, bind, and read per-adapter stats */
    for (i = 0; i < count; i++) {
        AdapterSnapshot *s = &snaps[i];
        s->vendor_id  = info[i].vid;
        s->device_id  = info[i].did;
        s->enum_index = info[i].idx;

        HANDLE h = CreateFileW(L"\\\\.\\IntelAvbFilter",
                               GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) continue;

        AVB_OPEN_REQUEST oreq;
        memset(&oreq, 0, sizeof(oreq));
        oreq.vendor_id = info[i].vid;
        oreq.device_id = info[i].did;
        oreq.index     = info[i].idx;
        br = 0;
        if (!DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER,
                             &oreq, sizeof(oreq), &oreq, sizeof(oreq), &br, NULL)
            || oreq.status != 0) {
            CloseHandle(h);
            continue;
        }

        AVB_DRIVER_STATISTICS stats;
        memset(&stats, 0, sizeof(stats));
        br = 0;
        if (DeviceIoControl(h, IOCTL_AVB_GET_STATISTICS,
                            NULL, 0, &stats, sizeof(stats), &br, NULL)
            && br == sizeof(AVB_DRIVER_STATISTICS)) {
            s->FilterAttachCount      = stats.FilterAttachCount;
            s->FilterPauseCount       = stats.FilterPauseCount;
            s->FilterRestartCount     = stats.FilterRestartCount;
            s->FilterDetachCount      = stats.FilterDetachCount;
            s->PauseRestartGeneration = stats.PauseRestartGeneration;
            s->OidRequestCount        = stats.OidRequestCount;
            s->stats_valid            = 1;
        }
        CloseHandle(h);
    }

    return count;
}

/* ── Phase print: access matrix + version IOCTL + per-adapter snapshot ───── */
/*
 * Returns TRUE if all required access checks and GET_VERSION succeed.
 * Fills *out_count and snaps[0..IDLE_MAX_ADAPTERS-1] with per-adapter data.
 */
static BOOL PrintPhase(const char *label, AccessEntry *m, int n,
                       AdapterSnapshot *snaps, int *out_count)
{
    int i;
    DWORD qdErr = 0;
    BOOL  qd    = DevicePathPresent(&qdErr);
    BOOL  allOk = qd;

    printf("  [%s] QueryDosDevice(\"IntelAvbFilter\"): %s (err=%lu)\n",
           label, qd ? "OK" : "FAIL", qdErr);

    ProbeAccessMatrix(m, n);
    for (i = 0; i < n; i++) {
        printf("  [%s] CreateFile %-22s : %s (err=%lu)\n",
               label, m[i].label, m[i].ok ? "OK" : "FAIL", m[i].err);
        if (!m[i].ok) allOk = FALSE;
    }

    /* GET_VERSION: read-only health check */
    {
        DWORD verErr = 0;
        BOOL verOk = ProbeVersionIoctl(&verErr);
        printf("  [%s] GET_VERSION                         : %s (err=%lu)\n",
               label, verOk ? "OK" : "FAIL", verErr);
        if (!verOk) allOk = FALSE;
    }

    /* Per-adapter lifecycle snapshot */
    *out_count = SnapshotAdapters(snaps, IDLE_MAX_ADAPTERS);
    printf("  [%s] Adapters enumerated: %d\n", label, *out_count);
    for (i = 0; i < *out_count; i++) {
        const AdapterSnapshot *s = &snaps[i];
        if (!s->stats_valid) {
            printf("  [%s]   [%d] VID=%04X DID=%04X idx=%u -- STATS UNAVAILABLE\n",
                   label, i, (unsigned)s->vendor_id, (unsigned)s->device_id,
                   (unsigned)s->enum_index);
            continue;
        }
        printf("  [%s]   [%d] VID=%04X DID=%04X idx=%u  "
               "Attach=%llu Pause=%llu Restart=%llu Detach=%llu Gen=%llu OID=%llu\n",
               label, i,
               (unsigned)s->vendor_id, (unsigned)s->device_id,
               (unsigned)s->enum_index,
               (unsigned long long)s->FilterAttachCount,
               (unsigned long long)s->FilterPauseCount,
               (unsigned long long)s->FilterRestartCount,
               (unsigned long long)s->FilterDetachCount,
               (unsigned long long)s->PauseRestartGeneration,
               (unsigned long long)s->OidRequestCount);
    }

    return allOk;
}

/* ── Main ────────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    int timeout_s = 120;
    if (argc > 1) timeout_s = atoi(argv[1]);
    {
        const char *envT = getenv("TC_IDLE_TIMEOUT_S");
        if (envT) { int v = atoi(envT); if (v > 0) timeout_s = v; }
    }
    int auto_mode = 1; /* default: timeout-only (no Enter required) */
    {
        const char *envA = getenv("TC_IDLE_AUTO");
        if (envA && atoi(envA) == 0) auto_mode = 0;
    }

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

    IdleVerdictInput vin;
    memset(&vin, 0, sizeof(vin));

    vin.phase_a_ok = PrintPhase("A", m_a, 4,
                                vin.snap_a, &vin.adapter_count_a) ? 1 : 0;

    if (!vin.phase_a_ok) {
        printf("\n  [SKIP] Phase A precondition not met -- device not accessible at baseline.\n");
        printf("  ACTION: check service status and driver installation.\n\n");
        printf("Summary: TC-LCY-IDLE-001 SKIP (exit 2)\n");
        return TC_SKIP;
    }
    printf("  [PASS-A] Baseline verified. %d adapter(s) snapshotted.\n\n",
           vin.adapter_count_a);

    /* ── Phase B: wait for user-initiated transition ───────────────────────── */
    printf("[Phase B] Waiting for idle/power transition\n");
    printf("  ACTION: Lock the screen (Win+L), wait for display to turn off,\n");
    printf("          or let the system idle for a few minutes, then unlock.\n");
    printf("  When done (or to proceed without transition): press Enter\n");
    printf("  Automatic timeout in %d seconds.\n\n", timeout_s);

    {
        HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
        DWORD  start  = GetTickCount();
        BOOL   entered = FALSE;

        while (!entered) {
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
                Sleep(100);
                continue;
            }
            if (elapsed >= (DWORD)(timeout_s * 1000)) {
                printf("\n  [TIMEOUT] %d s elapsed -- proceeding to Phase C automatically.\n\n",
                       timeout_s);
                break;
            }
            if (elapsed % 10000 < 200) {
                printf("  ... %d s remaining (press Enter to continue now)\r",
                       timeout_s - (int)(elapsed / 1000));
                fflush(stdout);
            }
            Sleep(200);
        }
    }

    /* ── Phase C: post-transition snapshot ─────────────────────────────────── */
    printf("[Phase C] Post-transition snapshot\n");
    AccessEntry m_c[] = {
        { 0,                            "access=0",           FALSE, 0 },
        { GENERIC_READ,                 "GENERIC_READ",       FALSE, 0 },
        { GENERIC_WRITE,                "GENERIC_WRITE",      FALSE, 0 },
        { GENERIC_READ | GENERIC_WRITE, "GENERIC_READ|WRITE", FALSE, 0 },
    };

    vin.phase_c_ok = PrintPhase("C", m_c, 4,
                                vin.snap_c, &vin.adapter_count_c) ? 1 : 0;

    /* Capture Win32 error for FAIL classification */
    if (!vin.phase_c_ok) {
        DWORD qdErr = 0;
        DevicePathPresent(&qdErr);
        /* Prefer the most specific error: path-absent → err=2; access-denied → err=5 */
        vin.phase_c_win32_err = qdErr ? qdErr
                              : (m_c[3].err ? m_c[3].err : m_c[2].err);
    }
    printf("\n");

    /* ── Verdict ───────────────────────────────────────────────────────────── */
    char reason[512];
    memset(reason, 0, sizeof(reason));
    int verdict = ComputeIdleVerdict(&vin, reason, sizeof(reason));

    printf("[Verdict]\n");
    printf("  Phase A accessible : %s\n", vin.phase_a_ok ? "YES" : "NO");
    printf("  Phase C accessible : %s\n", vin.phase_c_ok ? "YES" : "NO");
    printf("  Adapters (A/C)     : %d / %d\n",
           vin.adapter_count_a, vin.adapter_count_c);
    printf("  Outcome            : %s\n", reason);
    printf("\n");

    switch (verdict) {
    case TC_PASS:
        printf("[PASS] TC-LCY-IDLE-001: VERIFIED_TRANSITION (exit 0)\n");
        printf("  NDIS lifecycle event confirmed; device accessible before and after.\n");
        printf("  #328 NOT REPRODUCED in this run.\n");
        break;
    case TC_FAIL:
        printf("[FAIL] TC-LCY-IDLE-001: DEVICE_FAILURE_OBSERVED (exit 1)\n");
        printf("  Device inaccessible after transition -- #328 REPRODUCED.\n");
        printf("  Win32 error: %lu. DO NOT reinstall driver without explicit authorization.\n",
               (unsigned long)vin.phase_c_win32_err);
        break;
    case TC_SKIP:
        printf("[SKIP] TC-LCY-IDLE-001: BASELINE_UNAVAILABLE (exit 2)\n");
        printf("  ACTION: verify service status and driver installation.\n");
        break;
    default: /* TC_INCONCLUSIVE */
        printf("[INCONCLUSIVE] TC-LCY-IDLE-001: %s (exit 3)\n", reason);
        printf("  No NDIS lifecycle activity was observed between Phase A and Phase C.\n");
        printf("  To obtain a verifiable result:\n");
        printf("    1. Trigger an actual idle/power-management transition (Win+L, then wait\n");
        printf("       for display-off, then unlock) before the timeout expires.\n");
        printf("    2. For forced NIC power cycling: disable and re-enable the adapter in\n");
        printf("       Device Manager from a SEPARATE session (not this console window).\n");
        printf("    3. Increase timeout: set TC_IDLE_TIMEOUT_S=300 and retry.\n");
        break;
    }

    return verdict;
}
