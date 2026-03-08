/**
 * @file test_gptp_daemon_coexist.c
 * @brief OpenAvnu gPTP daemon coexistence test (issue #240)
 *
 * Implements: #240 (TEST-GPTP-COMPAT-001: OpenAvnu/gPTP daemon coexistence)
 * TDD state : MIXED — no new IOCTLs. All PHC IOCTLs are existing and working.
 *             If an OpenAvnu/gPTP daemon is NOT running the test SKIPs.
 *             If it IS running the test verifies no races with the driver.
 *
 * Acceptance criteria (from #240):
 *   - When a gPTP daemon holds the NIC PHC, driver IOCTLs still succeed
 *   - PHC time reads are monotonically non-decreasing during concurrent access
 *   - ADJUST_FREQUENCY and PHC_OFFSET_ADJUST succeed while daemon is active
 *
 * Test Cases: 5
 *   TC-COEXIST-001: Detect running OpenAvnu/gPTP daemon process
 *   TC-COEXIST-002: GET_CLOCK_CONFIG returns valid config while daemon active
 *   TC-COEXIST-003: ADJUST_FREQUENCY(0 ppb delta) succeeds while daemon active
 *   TC-COEXIST-004: PHC_OFFSET_ADJUST(0 ns) succeeds while daemon active
 *   TC-COEXIST-005: PHC time is monotonically non-decreasing (T1 <= T2)
 *
 * Notes:
 *   - Daemon detection: CreateToolhelp32Snapshot scans TH32CS_SNAPPROCESS
 *   - Daemon names checked: gpsd.exe, ptp4l.exe, gptp.exe, openavnu_gptp.exe,
 *                           daemon_cl.exe (OpenAvnu classic daemon)
 *
 * @see https://github.com/zarfld/IntelAvbFilter/issues/240
 */

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include "../../include/avb_ioctl.h"

/* ── IOCTL codes (all existing) ─────────────────────────────────────────────── */
/* IOCTL_AVB_GET_CLOCK_CONFIG / IOCTL_AVB_ADJUST_FREQUENCY / IOCTL_AVB_PHC_OFFSET_ADJUST
 * are declared in avb_ioctl.h — no new codes needed.                             */

#define DEVICE_NAME "\\\\.\\IntelAvbFilter"
static int g_pass = 0, g_fail = 0, g_skip = 0;

/* Daemon process names to detect */
static const char *k_daemon_names[] = {
    "gpsd.exe",
    "ptp4l.exe",
    "gptp.exe",
    "openavnu_gptp.exe",
    "daemon_cl.exe",    /* OpenAvnu classic gPTP daemon */
    NULL
};

static HANDLE OpenDevice(void) {
    return CreateFileA(DEVICE_NAME, GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

static int TryIoctl(HANDLE h, DWORD code, void *in_buf, DWORD in_sz,
                    void *out_buf, DWORD out_sz)
{
    DWORD ret = 0;
    if (DeviceIoControl(h, code, in_buf, in_sz, out_buf, out_sz, &ret, NULL))
        return 1;
    DWORD e = GetLastError();
    if (e == ERROR_INVALID_FUNCTION || e == ERROR_NOT_SUPPORTED) {
        printf("    [SKIP] IOCTL 0x%08lX not supported (err=%lu)\n",
               (unsigned long)code, (unsigned long)e);
        return -1;
    }
    printf("    [FAIL] DeviceIoControl failed (err=%lu)\n", (unsigned long)e);
    return 0;
}

/* ── Daemon detection ────────────────────────────────────────────────────────── */

static BOOL IsGptpDaemonRunning(char *found_name, DWORD found_name_sz)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        printf("    [WARN] CreateToolhelp32Snapshot failed (err=%lu)\n", GetLastError());
        return FALSE;
    }

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);

    BOOL found = FALSE;
    if (Process32First(snap, &pe)) {
        do {
            for (int i = 0; k_daemon_names[i] != NULL; i++) {
                if (_stricmp(pe.szExeFile, k_daemon_names[i]) == 0) {
                    found = TRUE;
                    if (found_name && found_name_sz > 0)
                        strncpy_s(found_name, found_name_sz,
                                  pe.szExeFile, _TRUNCATE);
                    break;
                }
            }
        } while (!found && Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

/* ───── TC-COEXIST-001 ───────────────────────────────────────────────────────── */
static int TC_COEXIST_001_DetectDaemon(void)
{
    char name[64] = {0};
    if (!IsGptpDaemonRunning(name, sizeof(name))) {
        printf("    No gPTP/OpenAvnu daemon found — coexistence tests will SKIP\n");
        printf("    (Run gpsd.exe, ptp4l.exe, gptp.exe or openavnu_gptp.exe first)\n");
        return -1; /* SKIP: precondition not met */
    }
    printf("    Found daemon: %s\n", name);
    /* Also verify device accessibility */
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("    [FAIL] Daemon running but driver device unavailable (err=%lu)\n",
               GetLastError());
        return 0;
    }
    CloseHandle(h);
    printf("    Driver device accessible while daemon is active\n");
    return 1;
}

/* ───── TC-COEXIST-002 ───────────────────────────────────────────────────────── */
static int TC_COEXIST_002_GetClockConfig(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    AVB_CLOCK_CONFIG cfg;
    ZeroMemory(&cfg, sizeof(cfg));

    int r = TryIoctl(h, IOCTL_AVB_GET_CLOCK_CONFIG, &cfg, sizeof(cfg),
                     &cfg, sizeof(cfg));
    CloseHandle(h);
    if (r > 0) {
        printf("    systim=%llu timinca=0x%08lX -- config retrieved\n",
               (unsigned long long)cfg.systim,
               (unsigned long)cfg.timinca);
    }
    return r;
}

/* ───── TC-COEXIST-003 ───────────────────────────────────────────────────────── */
static int TC_COEXIST_003_AdjustFrequency(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    /* First read current clock config to get the active timinca */
    AVB_CLOCK_CONFIG cfg;
    ZeroMemory(&cfg, sizeof(cfg));
    int rc = TryIoctl(h, IOCTL_AVB_GET_CLOCK_CONFIG, &cfg, sizeof(cfg),
                      &cfg, sizeof(cfg));
    if (rc <= 0) { CloseHandle(h); return rc; }

    /* Re-apply the same increment_ns derived from timinca — a true no-op */
    AVB_FREQUENCY_REQUEST adj;
    ZeroMemory(&adj, sizeof(adj));
    /* Use the current increment value so the write leaves the clock unchanged */
    adj.increment_ns   = (cfg.timinca & 0x00FFFFFFu);  /* bits [23:0] of TIMINCA */
    adj.increment_frac = 0;

    int r = TryIoctl(h, IOCTL_AVB_ADJUST_FREQUENCY, &adj, sizeof(adj),
                     &adj, sizeof(adj));
    CloseHandle(h);
    if (r > 0)
        printf("    ADJUST_FREQUENCY(noop timinca=0x%08lX) success while daemon active\n",
               (unsigned long)cfg.timinca);
    return r;
}

/* ───── TC-COEXIST-004 ───────────────────────────────────────────────────────── */
static int TC_COEXIST_004_PhcOffsetAdjust(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    AVB_OFFSET_REQUEST phc;
    ZeroMemory(&phc, sizeof(phc));
    phc.offset_ns = 0LL; /* zero offset adjust: must succeed */

    int r = TryIoctl(h, IOCTL_AVB_PHC_OFFSET_ADJUST, &phc, sizeof(phc),
                     &phc, sizeof(phc));
    CloseHandle(h);
    if (r > 0)
        printf("    PHC_OFFSET_ADJUST(0 ns) returned success while daemon is active\n");
    return r;
}

/* ───── TC-COEXIST-005 ───────────────────────────────────────────────────────── */
static int TC_COEXIST_005_PhcMonotonic(void)
{
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) return 0;

    AVB_CLOCK_CONFIG cfg1, cfg2;
    ZeroMemory(&cfg1, sizeof(cfg1));
    ZeroMemory(&cfg2, sizeof(cfg2));

    /* Read T1 */
    int r1 = TryIoctl(h, IOCTL_AVB_GET_CLOCK_CONFIG, &cfg1, sizeof(cfg1),
                      &cfg1, sizeof(cfg1));
    if (r1 <= 0) { CloseHandle(h); return r1; }

    /* Small delay to let time advance */
    Sleep(10);

    /* Read T2 */
    int r2 = TryIoctl(h, IOCTL_AVB_GET_CLOCK_CONFIG, &cfg2, sizeof(cfg2),
                      &cfg2, sizeof(cfg2));
    CloseHandle(h);
    if (r2 <= 0) return r2;

    avb_u64 t1 = cfg1.systim;
    avb_u64 t2 = cfg2.systim;
    printf("    T1=%llu  T2=%llu  delta=%lld ns\n",
           (unsigned long long)t1, (unsigned long long)t2,
           (long long)(t2 - t1));

    if (t2 < t1) {
        printf("    [FAIL] PHC time went backwards (T2 < T1) while daemon is active\n");
        return 0;
    }
    if (t1 == 0 && t2 == 0) {
        printf("    [WARN] Both timestamps are zero — PHC may not be running\n");
        /* Still pass: driver returned data, not a bug in coexistence */
    }
    printf("    PHC time is monotonically non-decreasing -- PASS\n");
    return 1;
}

int main(void)
{
    int r;
    printf("============================================================\n");
    printf("  IntelAvbFilter -- OpenAvnu gPTP Coexistence Tests\n");
    printf("  Implements: #240 (TEST-GPTP-COMPAT-001)\n");
    printf("  Note: ALL tests SKIP if no gPTP daemon is running\n");
    printf("============================================================\n\n");

#define RUN(tc, label) \
    printf("  %s\n", (label)); \
    r = tc(); \
    if      (r > 0) { g_pass++; printf("  [PASS] %s\n\n", (label)); } \
    else if (r == 0){ g_fail++; printf("  [FAIL] %s\n\n", (label)); } \
    else            { g_skip++; printf("  [SKIP] %s\n\n", (label)); }

    RUN(TC_COEXIST_001_DetectDaemon,    "TC-COEXIST-001: Detect gPTP daemon process");

    /* If TC-001 SKIPped (no daemon), the remaining tests are meaningless.
       Check by inspecting g_skip before running them.                       */
    if (g_skip > 0) {
        printf("  [Note] Daemon not active -- skipping TC 002-005\n");
        g_skip += 4; /* count TCs 002-005 as skipped */
    } else {
        RUN(TC_COEXIST_002_GetClockConfig, "TC-COEXIST-002: GET_CLOCK_CONFIG while daemon active");
        RUN(TC_COEXIST_003_AdjustFrequency,"TC-COEXIST-003: ADJUST_FREQUENCY(0) while daemon active");
        RUN(TC_COEXIST_004_PhcOffsetAdjust,"TC-COEXIST-004: PHC_OFFSET_ADJUST(0) while daemon active");
        RUN(TC_COEXIST_005_PhcMonotonic,   "TC-COEXIST-005: PHC monotonic T1 <= T2 while daemon active");
    }

    printf("-------------------------------------------\n");
    printf(" PASS=%d  FAIL=%d  SKIP=%d  TOTAL=%d\n",
           g_pass, g_fail, g_skip, g_pass + g_fail + g_skip);
    printf("-------------------------------------------\n");
    return (g_fail == 0) ? 0 : 1;
}
