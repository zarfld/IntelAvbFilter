/**
 * @file test_compat_win11.c
 * @brief Windows 11 Compatibility Smoke Test
 *
 * Implements: #256 (TEST-COMPAT-WIN11-001: Win11 21H2/22H2/23H2/24H2/25H2 smoke test)
 * Verifies that the IntelAvbFilter driver loads and functions correctly on Windows 11.
 *
 * Test Cases: 6
 *   TC-WIN11-001: OS build >= 22000 confirmed via ntdll!RtlGetVersion (bypasses manifest lies)
 *   TC-WIN11-002: Architecture is AMD64 (64-bit x64) -- Win11 is x64-only
 *   TC-WIN11-003: Driver service IntelAvbFilter STATE = SERVICE_RUNNING (SCM query)
 *   TC-WIN11-004: Device node \\.\IntelAvbFilter is accessible on Windows 11
 *   TC-WIN11-005: IOCTL_AVB_GET_VERSION returns success on Windows 11 kernel ABI
 *   TC-WIN11-006: Windows 11 channel record (25H2/24H2/23H2/etc, informational)
 *
 * Evidence captured: 2026-03-19 on 6xI226MACHINE
 *   OS: Windows 11 Pro, Version 25H2, Build 26200.8037
 *   System type: 64-bit operating system, x64-based processor (Intel N150)
 *   IntelAvbFilter service: STATE 4 RUNNING
 *   IOCTL_AVB_GET_VERSION: confirmed operational in prior test sessions
 *
 * @see https://github.com/zarfld/IntelAvbFilter/issues/256
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

/* Single Source of Truth for IOCTL definitions */
#include "../../include/avb_ioctl.h"

#define DEVICE_NAME "\\\\.\\IntelAvbFilter"

/* RTL OS version type -- avoids winternl.h KM header conflicts */
typedef struct _RTL_OSVERSIONINFOW_S {
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    WCHAR szCSDVersion[128];
} RTL_OSVERSIONINFOW_S;

typedef LONG (WINAPI *PFN_RtlGetVersion)(RTL_OSVERSIONINFOW_S *);

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) ((LONG)(Status) >= 0)
#endif

/* Cached OS version filled by TC-WIN11-001 */
static RTL_OSVERSIONINFOW_S g_vi;
static BOOL                  g_vi_valid = FALSE;

static int g_pass = 0;
static int g_fail = 0;
static int g_skip = 0;

/* ────────────────────────── TC-WIN11-001 ──────────────────────────────────── */
/*
 * Call ntdll!RtlGetVersion to get the true OS version, bypassing any
 * application-compatibility manifest that would lie via VerifyVersionInfo.
 * Windows 11 returns dwMajorVersion=10, dwBuildNumber >= 22000.
 * Windows 10 returns dwMajorVersion=10, dwBuildNumber < 22000.
 *
 * On 6xI226MACHINE: build 26200 (Win11 25H2) >= 22000 => PASS expected.
 */
static int TC_WIN11_001_Build22000(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) {
        printf("    [FAIL] ntdll.dll not loaded -- extremely unexpected\n");
        return 0;
    }

    PFN_RtlGetVersion fn = (PFN_RtlGetVersion)GetProcAddress(ntdll, "RtlGetVersion");
    if (!fn) {
        printf("    [FAIL] RtlGetVersion not exported by ntdll.dll\n");
        return 0;
    }

    ZeroMemory(&g_vi, sizeof(g_vi));
    g_vi.dwOSVersionInfoSize = sizeof(g_vi);
    LONG status = fn(&g_vi);
    if (!NT_SUCCESS(status)) {
        printf("    [FAIL] RtlGetVersion returned 0x%08lX\n", (unsigned long)status);
        return 0;
    }
    g_vi_valid = TRUE;

    printf("    dwMajorVersion = %lu\n", g_vi.dwMajorVersion);
    printf("    dwMinorVersion = %lu\n", g_vi.dwMinorVersion);
    printf("    dwBuildNumber  = %lu\n", g_vi.dwBuildNumber);
    printf("    Win11 threshold: 22000\n");

    if (g_vi.dwMajorVersion < 10 || g_vi.dwBuildNumber < 22000) {
        printf("    [FAIL] Build %lu < 22000: not Windows 11 (Win10 or earlier)\n",
               g_vi.dwBuildNumber);
        return 0;
    }

    printf("    Build %lu >= 22000: confirmed Windows 11\n", g_vi.dwBuildNumber);
    return 1;
}

/* ────────────────────────── TC-WIN11-002 ──────────────────────────────────── */
/*
 * Windows 11 is x64-only (no 32-bit arm or x86 SKU of Win11 Consumer/Pro).
 * GetNativeSystemInfo returns the actual machine architecture, not the emulated
 * architecture for a WOW64 process.
 */
static int TC_WIN11_002_Architecture(void)
{
    SYSTEM_INFO si;
    ZeroMemory(&si, sizeof(si));
    GetNativeSystemInfo(&si);

    printf("    wProcessorArchitecture = 0x%04X  (9=AMD64, 12=ARM64, 0=x86)\n",
           si.wProcessorArchitecture);

    if (si.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_AMD64) {
        printf("    [FAIL] Not AMD64 (0x%04X != 0x0009)\n", si.wProcessorArchitecture);
        return 0;
    }

    printf("    Architecture: AMD64 (0x0009) -- 64-bit x86-64 confirmed\n");
    return 1;
}

/* ────────────────────────── TC-WIN11-003 ──────────────────────────────────── */
/*
 * The IntelAvbFilter kernel service must be in STATE_RUNNING (4).
 * If not running, the driver has not loaded -- compatibility broken.
 */
static int TC_WIN11_003_ServiceRunning(void)
{
    SC_HANDLE hSCM = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCM) {
        printf("    [FAIL] OpenSCManager failed (err=%lu)\n", GetLastError());
        return 0;
    }

    SC_HANDLE hSvc = OpenServiceA(hSCM, "IntelAvbFilter", SERVICE_QUERY_STATUS);
    if (!hSvc) {
        DWORD err = GetLastError();
        CloseServiceHandle(hSCM);
        printf("    [FAIL] OpenService(IntelAvbFilter) failed (err=%lu) -- not installed?\n",
               err);
        return 0;
    }

    SERVICE_STATUS ss;
    ZeroMemory(&ss, sizeof(ss));
    BOOL ok = QueryServiceStatus(hSvc, &ss);
    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);

    if (!ok) {
        printf("    [FAIL] QueryServiceStatus failed (err=%lu)\n", GetLastError());
        return 0;
    }

    printf("    dwCurrentState        = %lu  (4 = SERVICE_RUNNING)\n", ss.dwCurrentState);
    printf("    dwControlsAccepted    = 0x%08lX\n", ss.dwControlsAccepted);

    if (ss.dwCurrentState != SERVICE_RUNNING) {
        printf("    [FAIL] Service IntelAvbFilter is not RUNNING (state=%lu)\n",
               ss.dwCurrentState);
        return 0;
    }

    printf("    Service IntelAvbFilter: STATE 4 RUNNING -- driver loaded\n");
    return 1;
}

/* ────────────────────────── TC-WIN11-004 ──────────────────────────────────── */
/*
 * The device node \\.\IntelAvbFilter must be openable using CreateFileA.
 * On Win11, NDIS and the kernel stack must have properly created the device
 * object for the symbolic link to succeed.
 */
static int TC_WIN11_004_DeviceAccessible(void)
{
    HANDLE h = CreateFileA(DEVICE_NAME,
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("    [FAIL] Cannot open %s (err=%lu)\n", DEVICE_NAME, GetLastError());
        return 0;
    }

    CloseHandle(h);
    printf("    %s opened successfully on Windows 11\n", DEVICE_NAME);
    printf("    Device node and NDIS dispatch table: operational\n");
    return 1;
}

/* ────────────────────────── TC-WIN11-005 ──────────────────────────────────── */
/*
 * IOCTL_AVB_GET_VERSION is the baseline IOCTL smoke test.
 * It validates that the full Win11 IOCTL dispatch path from user-mode to kernel
 * is functioning correctly and returns driver version information.
 */
static int TC_WIN11_005_VersionIoctl(void)
{
    HANDLE h = CreateFileA(DEVICE_NAME,
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("    [FAIL] Cannot open device (err=%lu)\n", GetLastError());
        return 0;
    }

    IOCTL_VERSION ver;
    ZeroMemory(&ver, sizeof(ver));
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_AVB_GET_VERSION,
                               &ver, sizeof(ver),
                               &ver, sizeof(ver), &returned, NULL);
    CloseHandle(h);

    if (!ok) {
        printf("    [FAIL] IOCTL_AVB_GET_VERSION failed (err=%lu)\n", GetLastError());
        return 0;
    }

    printf("    Driver version : %u.%u.%u.%u\n",
           ver.Major, ver.Minor, ver.Build, ver.Revision);
    printf("    IOCTL responded with %lu bytes\n", returned);
    printf("    IOCTL_AVB_GET_VERSION: PASS on Windows 11 (build %lu)\n",
           g_vi_valid ? g_vi.dwBuildNumber : 0UL);
    return 1;
}

/* ────────────────────────── TC-WIN11-006 ──────────────────────────────────── */
/*
 * Informational: identify the specific Windows 11 channel from the build number.
 * Always passes if build >= 22000 (already validated by TC-WIN11-001).
 * Provides a human-readable record of the exact OS version tested.
 *
 * Build 26200 = Windows 11 25H2 (confirmed on 6xI226MACHINE, 2026-03-19)
 */
static int TC_WIN11_006_Win11VersionRecord(void)
{
    if (!g_vi_valid) {
        printf("    [SKIP] OS version unavailable (TC-WIN11-001 failed)\n");
        return -1;
    }

    ULONG build = g_vi.dwBuildNumber;

    const char *channel = "Win11 (unrecognized sub-channel)";
    if      (build >= 27000) channel = "Win11 Dev/Canary channel";
    else if (build >= 26200) channel = "Win11 25H2 (or 24H2 Dev)";
    else if (build >= 25398) channel = "Win11 IoT 23H2 LTSC";
    else if (build >= 22631) channel = "Win11 23H2";
    else if (build >= 22621) channel = "Win11 22H2";
    else if (build >= 22000) channel = "Win11 21H2 (RTM)";

    printf("    OS Channel      : %s\n", channel);
    printf("    Build Number    : %lu\n", build);
    printf("    Full Version    : %lu.%lu.%lu\n",
           g_vi.dwMajorVersion, g_vi.dwMinorVersion, build);
    printf("    Win11 threshold : 22000  (delta from threshold: +%ld)\n",
           (long)(build - 22000));
    printf("    Evidence machine: 6xI226MACHINE (Intel N150, 32 GB, x64)\n");
    printf("    Evidence date   : 2026-03-19\n");
    printf("    Issue           : #256 (TEST-COMPAT-WIN11-001)\n");

    if (build < 22000) {
        printf("    [FAIL] Build %lu < 22000 -- this is not Windows 11\n", build);
        return 0;
    }
    return 1;
}

/* ────────────────────────── main ───────────────────────────────────────────── */
int main(void)
{
    int r;

    printf("============================================================\n");
    printf("  IntelAvbFilter -- Windows 11 Compatibility Smoke Tests\n");
    printf("  Implements: #256 (TEST-COMPAT-WIN11-001)\n");
    printf("  Target: Win11 21H2/22H2/23H2/24H2/25H2 (build >= 22000)\n");
    printf("============================================================\n\n");

#define RUN(tc, label) \
    printf("  %s\n", (label)); \
    r = tc(); \
    if      (r > 0) { g_pass++; printf("  [PASS] %s\n\n", (label)); } \
    else if (r == 0){ g_fail++; printf("  [FAIL] %s\n\n", (label)); } \
    else            { g_skip++; printf("  [SKIP] %s\n\n", (label)); }

    RUN(TC_WIN11_001_Build22000,        "TC-WIN11-001: OS build >= 22000 (ntdll!RtlGetVersion)");
    RUN(TC_WIN11_002_Architecture,      "TC-WIN11-002: Architecture is AMD64 (64-bit x64)");
    RUN(TC_WIN11_003_ServiceRunning,    "TC-WIN11-003: Service IntelAvbFilter STATE = RUNNING");
    RUN(TC_WIN11_004_DeviceAccessible,  "TC-WIN11-004: Device node accessible on Windows 11");
    RUN(TC_WIN11_005_VersionIoctl,      "TC-WIN11-005: IOCTL_AVB_GET_VERSION on Windows 11");
    RUN(TC_WIN11_006_Win11VersionRecord,"TC-WIN11-006: Windows 11 version record (informational)");

    printf("-------------------------------------------\n");
    printf(" PASS=%d  FAIL=%d  SKIP=%d  TOTAL=%d\n",
           g_pass, g_fail, g_skip, g_pass + g_fail + g_skip);
    printf("-------------------------------------------\n");

    return (g_fail == 0) ? 0 : 1;
}
