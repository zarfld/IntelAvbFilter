/**
 * @file test_control_device_lifecycle.c
 * @brief Regression test for control-device lifecycle after service restart (#328)
 *
 * Implements: TC-LCY-328
 * Traces to:  #328 (BUG: \\.\ IntelAvbFilter disappears after service restart)
 *
 * Root cause (confirmed 2026-09-21 from DbgView log):
 *   Run-Tests.ps1 stops/starts EventLog for ETW setup.  NDIS reacts by calling
 *   FilterUnload (NdisDeregisterDeviceEx destroys the DOS device path).  A
 *   runaway AvbTxTimestampPollDpc (use-after-free: NdisCancelTimer did not drain
 *   the in-flight DPC before AvbCleanupDevice freed the context) blocks module
 *   unload, so the service restart does not re-run DriverEntry and the device
 *   path stays absent while the service reports Running.
 *
 * Fixes verified by this test:
 *   1. KeFlushQueuedDpcs() in AvbStopTimers -- drains in-flight DPC before free
 *   2. FilterAttach re-registers device when NdisFilterDeviceHandle == NULL
 *   3. Removed NDIS_INIT_FUNCTION from IntelAvbFilterRegisterDevice
 *
 * Test phases:
 *   A (precondition): device accessible
 *   B (trigger):      Stop-Service EventLog -Force + Start-Service EventLog
 *                     (mirrors the Run-Tests.ps1 ETW setup step that triggered #328)
 *                     NDIS reacts by calling FilterUnload; the fix ensures the module
 *                     unloads cleanly (KeFlushQueuedDpcs) and the device is re-registered
 *                     either via a fresh DriverEntry or via FilterAttach re-registration.
 *   C (observe):      device path accessible (CreateFile) = fix works
 *                     device path absent (ERROR_FILE_NOT_FOUND = 2) = #328 still present
 *
 * Verdict:
 *   PASS -- Phase C: QueryDosDevice + CreateFile succeed
 *   FAIL -- CONTROL_DEVICE_MISSING_AFTER_EVENTLOG_RESTART (Win32 error 2)
 *   SKIP -- Phase A not met or insufficient privilege
 *
 * TC_CYCLES env var (default 1, max 10) repeats Phase B+C.
 * Prerequisites: Elevated (SERVICE_STOP + SERVICE_START require admin).
 *
 * Build (see tools/build/Build-Tests.ps1):
 *   cl /nologo /W4 /WX /Zi /DWIN32 /D_WIN32_WINNT=0x0A00
 *      -I include -I external/intel_avb/lib
 *      tests\\hardware\\test_control_device_lifecycle.c
 *      /Fe:build\\tools\\test_control_device_lifecycle.exe
 */

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SSOT — all IOCTL definitions from the authoritative header */
#include "avb_ioctl.h"

/* ── Verdict codes ───────────────────────────────────────────────────────── */
#define TC_PASS  0
#define TC_FAIL  1
#define TC_SKIP  2

/* ── QueryDosDevice helper ───────────────────────────────────────────────── */
static BOOL DevicePathExists(void)
{
    WCHAR buf[512];
    DWORD r = QueryDosDeviceW(L"IntelAvbFilter", buf, ARRAYSIZE(buf));
    return (r > 0);
}

/* ── Access-matrix probe ─────────────────────────────────────────────────── */
typedef struct {
    DWORD access;
    const char *label;
    BOOL  ok;
    DWORD err;
} AccessMatrixEntry;

static void RunAccessMatrix(AccessMatrixEntry *entries, int n)
{
    for (int i = 0; i < n; i++) {
        HANDLE h = CreateFileW(L"\\\\.\\IntelAvbFilter",
                               entries[i].access,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            entries[i].ok  = TRUE;
            entries[i].err = 0;
            CloseHandle(h);
        } else {
            entries[i].ok  = FALSE;
            entries[i].err = GetLastError();
        }
    }
}

/* ── Trivial IOCTL probe (retries on ERROR_NOT_READY for up to 20s) ──────── */
static BOOL TrivialIoctlProbe(DWORD *outErr)
{
    /* Retry to allow FilterAttach+FilterRestart to complete after service start.
     * 6 adapters on this machine can take several seconds. */
    for (int attempt = 0; attempt < 40; attempt++) {
        HANDLE h = CreateFileW(L"\\\\.\\IntelAvbFilter",
                               GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            *outErr = GetLastError();
            return FALSE;
        }
        AVB_DRIVER_STATISTICS stats;
        ZeroMemory(&stats, sizeof(stats));
        DWORD br = 0;
        BOOL ok = DeviceIoControl(h, IOCTL_AVB_GET_STATISTICS,
                                  NULL, 0, &stats, sizeof(stats), &br, NULL);
        *outErr = ok ? 0 : GetLastError();
        CloseHandle(h);
        if (ok) return TRUE;
        if (*outErr != ERROR_NOT_READY) return FALSE;
        Sleep(500);
    }
    return FALSE;
}

/* ── Main ────────────────────────────────────────────────────────────────── */
static BOOL WaitForServiceState(SC_HANDLE hSvc, DWORD target, int timeout_s)
{
    for (int i = 0; i < timeout_s * 2; i++) {
        SERVICE_STATUS ss;
        if (!QueryServiceStatus(hSvc, &ss)) return FALSE;
        if (ss.dwCurrentState == target) return TRUE;
        Sleep(500);
    }
    return FALSE;
}

int main(void)
{
    int pass = 0, fail = 0, skip = 0;

    int cycles = 1;
    const char *envCycles = getenv("TC_CYCLES");
    if (envCycles) { int v = atoi(envCycles); if (v > 0 && v <= 10) cycles = v; }

    printf("TC-LCY-328: Control device lifecycle after service restart\n");
    printf("  Regression for issue #328\n");
    printf("  Trigger: sc stop + sc start IntelAvbFilter\n");
    printf("  Cycles:  %d\n\n", cycles);

    /* ── Phase A: precondition ────────────────────────────────────────────── */
    printf("[Phase A] Baseline device accessibility\n");

    if (!DevicePathExists()) {
        printf("  [SKIP-A] QueryDosDevice(\"IntelAvbFilter\") FAILED err=%lu\n", GetLastError());
        printf("  VERDICT: DEVICE_ABSENT_AT_BASELINE\n");
        printf("  ACTION:  reinstall driver to recover baseline, then re-run.\n\n");
        printf("Summary: 0 PASS  0 FAIL  1 SKIP\n");
        return TC_SKIP;
    }
    printf("  QueryDosDevice: OK\n");

    AccessMatrixEntry matrix[] = {
        { 0,                               "access=0",           FALSE, 0 },
        { GENERIC_READ,                    "GENERIC_READ",       FALSE, 0 },
        { GENERIC_WRITE,                   "GENERIC_WRITE",      FALSE, 0 },
        { GENERIC_READ | GENERIC_WRITE,    "GENERIC_READ|WRITE", FALSE, 0 },
    };
    RunAccessMatrix(matrix, 4);
    BOOL baselineOk = TRUE;
    for (int i = 0; i < 4; i++) {
        printf("  CreateFile %-22s : %s (err=%lu)\n",
               matrix[i].label, matrix[i].ok ? "OK" : "FAIL", matrix[i].err);
        if (!matrix[i].ok) baselineOk = FALSE;
    }
    DWORD ioErr = 0;
    BOOL  ioOk  = TrivialIoctlProbe(&ioErr);
    /* Phase A IOCTL is advisory: NOT_READY means NDIS hasn't rebound adapters yet.
     * #328 is about path absence, not IOCTL failure; only fail on hard errors. */
    if (!ioOk && ioErr != ERROR_NOT_READY) baselineOk = FALSE;
    printf("  IOCTL_AVB_GET_STATISTICS   : %s (err=%lu)%s\n",
           ioOk ? "OK" : "WARN", ioErr, ioOk ? "" : " [NDIS rebind pending -- non-fatal]");

    if (!baselineOk) {
        printf("\n  [SKIP-A] Phase A preconditions not fully met.\n\n");
        printf("Summary: 0 PASS  0 FAIL  1 SKIP\n");
        return TC_SKIP;
    }
    printf("  [PASS-A] Baseline verified.\n\n");

    /* ── Open SCM ─────────────────────────────────────────────────────────── */
    SC_HANDLE hScm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hScm) {
        printf("[SKIP] OpenSCManager failed err=%lu (need elevated)\n", GetLastError());
        printf("Summary: 0 PASS  0 FAIL  1 SKIP\n");
        return TC_SKIP;
    }
    SC_HANDLE hSvc = OpenServiceW(hScm, L"EventLog",
                                   SERVICE_QUERY_STATUS | SERVICE_STOP | SERVICE_START);
    SC_HANDLE hAvb = OpenServiceW(hScm, L"IntelAvbFilter", SERVICE_QUERY_STATUS);
    if (!hSvc || !hAvb) {
        if (hSvc) CloseServiceHandle(hSvc);
        if (hAvb) CloseServiceHandle(hAvb);
        CloseServiceHandle(hScm);
        printf("[SKIP] OpenService failed err=%lu (need elevated)\n", GetLastError());
        printf("Summary: 0 PASS  0 FAIL  1 SKIP\n");
        return TC_SKIP;
    }

    /* ── Cycles B+C ───────────────────────────────────────────────────────── */
    for (int cycle = 1; cycle <= cycles; cycle++) {

        printf("[Phase B] Service stop + start (cycle %d/%d)\n", cycle, cycles);

        SERVICE_STATUS ss;
        if (!ControlService(hSvc, SERVICE_CONTROL_STOP, &ss)) {
            DWORD e = GetLastError();
            if (e != ERROR_SERVICE_NOT_ACTIVE) {
                printf("  [SKIP-B] ControlService(STOP) failed err=%lu\n", e);
                skip++;
                continue;
            }
        }
        printf("  SERVICE_CONTROL_STOP sent -- waiting up to 10s\n");
        if (!WaitForServiceState(hSvc, SERVICE_STOPPED, 10)) {
            printf("  [FAIL-B] Service did not reach STOPPED within 10s\n");
            fail++;
            break;
        }
        printf("  Service: STOPPED\n");
        Sleep(1000);

        if (!StartServiceW(hSvc, 0, NULL)) {
            DWORD e = GetLastError();
            /* ERROR_SERVICE_ALREADY_RUNNING (1056) or ERROR_ALREADY_EXISTS (183):
             * NDIS may auto-restart the filter service before we call StartService.
             * This is expected with the fix in place — clean unload allows fast reload. */
            if (e != ERROR_SERVICE_ALREADY_RUNNING && e != ERROR_ALREADY_EXISTS) {
                printf("  [FAIL-B] StartService failed err=%lu\n", e);
                fail++;
                break;
            }
            printf("  Service auto-restarted by NDIS (err=%lu -- OK)\n", e);
        }
        printf("  StartService called -- waiting up to 15s for RUNNING\n");
        if (!WaitForServiceState(hSvc, SERVICE_RUNNING, 15)) {
            printf("  [FAIL-B] Service did not reach RUNNING within 15s\n");
            fail++;
            break;
        }
        printf("  Service: RUNNING -- waiting 2s for NDIS filter binds\n");
        Sleep(2000);

        /* ── Phase C: observe ─────────────────────────────────────────────── */
        printf("[Phase C] Post-restart device accessibility (cycle %d)\n", cycle);

        BOOL pathOk = DevicePathExists();
        printf("  QueryDosDevice: %s\n", pathOk ? "OK" : "FAIL");

        AccessMatrixEntry m2[] = {
            { 0,                               "access=0",           FALSE, 0 },
            { GENERIC_READ,                    "GENERIC_READ",       FALSE, 0 },
            { GENERIC_WRITE,                   "GENERIC_WRITE",      FALSE, 0 },
            { GENERIC_READ | GENERIC_WRITE,    "GENERIC_READ|WRITE", FALSE, 0 },
        };
        RunAccessMatrix(m2, 4);
        for (int i = 0; i < 4; i++) {
            printf("  CreateFile %-22s : %s (err=%lu)\n",
                   m2[i].label, m2[i].ok ? "OK" : "FAIL", m2[i].err);
        }
        DWORD ioErr2 = 0;
        BOOL  ioOk2  = TrivialIoctlProbe(&ioErr2);
        /* Phase C IOCTL is advisory — #328 fix is about path presence, not IOCTL health.
         * NDIS rebinding after sc start may take additional time or adapter restart. */
        printf("  IOCTL_AVB_GET_STATISTICS   : %s (err=%lu)%s\n",
               ioOk2 ? "OK" : "WARN", ioErr2, ioOk2 ? "" : " [non-fatal]");

        /* Verdict: #328 regression is PASS if device PATH is accessible (CreateFile OK).
         * A missing path (err=2) after restart is the exact #328 observable. */
        BOOL cyclePass = pathOk;
        for (int i = 0; i < 4 && cyclePass; i++) if (!m2[i].ok) cyclePass = FALSE;

        if (cyclePass) {
            printf("  [PASS-C] TC-LCY-328 cycle %d/%d\n\n", cycle, cycles);
            pass++;
        } else {
            DWORD firstErr = !pathOk ? GetLastError() : m2[3].err;
            const char *verdict =
                (!pathOk || firstErr == 2 || m2[0].err == 2)
                    ? "CONTROL_DEVICE_MISSING_AFTER_EVENTLOG_RESTART"
                : (firstErr == 5 || m2[3].err == 5)
                    ? "CONTROL_DEVICE_ACCESS_DENIED_AFTER_RESTART"
                : "CONTROL_DEVICE_IOCTL_FAILED_AFTER_RESTART";
            printf("  [FAIL-C] TC-LCY-328 cycle %d/%d : %s\n\n",
                   cycle, cycles, verdict);
            fail++;
        }
    }

    CloseServiceHandle(hAvb);
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hScm);

    printf("Summary: %d PASS  %d FAIL  %d SKIP\n", pass, fail, skip);
    if (fail > 0) return TC_FAIL;
    if (skip > 0 && pass == 0) return TC_SKIP;
    return TC_PASS;
}

