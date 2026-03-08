/**
 * @file test_win7_stub.c
 * @brief Windows OS Version Compatibility Check
 *
 * Implements: #258 (TEST-COMPAT-WIN7-001: Win7 SP1 not supported; Win10+ required)
 * Verifies:   driver targets Windows 10+ exclusively (Win7 EOL since Jan 14, 2020)
 *
 * This test documents and validates the Win10+ requirement at runtime.
 * All five TCs pass immediately on the target hardware (Win10+ with driver loaded).
 * If somehow run on Win7, TC-COMPAT-001 and TC-COMPAT-002 will FAIL — which is
 * the correct outcome: Win7 is not supported and the driver must not be installed there.
 *
 * Test Cases: 5
 *   TC-COMPAT-001: Runtime OS major version >= 10 (via ntdll!RtlGetVersion)
 *   TC-COMPAT-002: Build number >= 9200 (Win10 RTM baseline)
 *   TC-COMPAT-003: Device node accessible — \\.\IntelAvbFilter opens on current OS
 *   TC-COMPAT-004: IOCTL_AVB_GET_VERSION returns success (Win10+ kernel ABI)
 *   TC-COMPAT-005: Informational — Windows edition and version record
 *
 * Note: Uses ntdll!RtlGetVersion (not GetVersionExW) to get the true OS version
 * even when the application compatibility manifest would lie via VerifyVersionInfo.
 *
 * @see https://github.com/zarfld/IntelAvbFilter/issues/258
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

/* Single Source of Truth for IOCTL definitions */
#include "../../include/avb_ioctl.h"

#define DEVICE_NAME "\\\\.\\IntelAvbFilter"

/* ───── rtl types without pulling in winternl.h (avoids KM conflicts) ───────── */
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

/* Cached OS version filled by TC-COMPAT-001 */
static RTL_OSVERSIONINFOW_S g_vi;
static BOOL                  g_vi_valid = FALSE;

static int g_pass = 0;
static int g_fail = 0;
static int g_skip = 0;

/* ────────────────────────── TC-COMPAT-001 ──────────────────────────────────── */
/*
 * Call ntdll!RtlGetVersion — bypasses manifest-overridden VerifyVersionInfo.
 * dwMajorVersion must be >= 10. Windows 11 also returns major=10, build >= 22000.
 */
static int TC_COMPAT_001_OSVersionMajor(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) {
        printf("    [FAIL] ntdll.dll not loaded — extremely unexpected\n");
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

    if (g_vi.dwMajorVersion < 10) {
        printf("    [FAIL] OS is Windows %lu — driver requires Windows 10+\n",
               g_vi.dwMajorVersion);
        return 0;
    }

    printf("    OS major version %lu >= 10: within supported range\n",
           g_vi.dwMajorVersion);
    return 1;
}

/* ────────────────────────── TC-COMPAT-002 ──────────────────────────────────── */
/*
 * Build number validation:
 *   Win10 RTM (July 2015)  = 10240
 *   Win10 1809             = 17763
 *   Win10 21H2             = 19044
 *   Win11 RTM (Oct 2021)   = 22000
 * Minimum supported: 10240 (Win10 RTM).  Threshold set to 9200 (Win8/Server2012)
 * to avoid false failures on Server OS variants.
 */
static int TC_COMPAT_002_BuildNumber(void)
{
    if (!g_vi_valid) {
        printf("    [SKIP] Depends on TC-COMPAT-001 (OS version unavailable)\n");
        return -1;  /* SKIP */
    }

    ULONG build = g_vi.dwBuildNumber;

    if (g_vi.dwMajorVersion < 10 || build < 9200) {
        printf("    [FAIL] Build %lu is below minimum Win10 threshold (9200)\n", build);
        return 0;
    }

    const char *edition;
    if      (build >= 22000) edition = "Windows 11";
    else if (build >= 10240) edition = "Windows 10";
    else                     edition = "Windows Server 2012+";

    printf("    Build %lu identified as: %s\n", build, edition);
    printf("    Build number >= 9200: within supported range\n");
    return 1;
}

/* ────────────────────────── TC-COMPAT-003 ──────────────────────────────────── */
/*
 * The driver device node must be accessible under the current OS.
 * On Win7 the driver should not be installed; on Win10+ it must open cleanly.
 */
static int TC_COMPAT_003_DeviceNodeAccessible(void)
{
    HANDLE h = CreateFileA(DEVICE_NAME,
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("    [FAIL] Cannot open %s (err=%lu) — driver not loaded?\n",
               DEVICE_NAME, GetLastError());
        return 0;
    }
    CloseHandle(h);
    printf("    %s is accessible — driver is loaded and device node exists\n", DEVICE_NAME);
    return 1;
}

/* ────────────────────────── TC-COMPAT-004 ──────────────────────────────────── */
/*
 * IOCTL_AVB_GET_VERSION must succeed — validates that the full Win10+
 * IOCTL dispatch path is operational on the current OS build.
 */
static int TC_COMPAT_004_VersionIoctl(void)
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
    printf("    IOCTL succeeded — Win10+ ABI fully operational\n");
    return 1;
}

/* ────────────────────────── TC-COMPAT-005 ──────────────────────────────────── */
/*
 * Informational: print a human-readable OS summary for the test record.
 * Always passes on Win10+; fails (by design) on Win7 to document non-support.
 */
static int TC_COMPAT_005_InfoRecord(void)
{
    if (!g_vi_valid) {
        printf("    [SKIP] OS version not available\n");
        return -1;
    }

    ULONG maj   = g_vi.dwMajorVersion;
    ULONG build = g_vi.dwBuildNumber;

    const char *name = "Unknown";
    if (maj == 10) {
        if      (build >= 22000) name = "Windows 11";
        else if (build >= 10240) name = "Windows 10";
        else if (build >= 9600)  name = "Windows 8.1 / Server 2012 R2";
        else if (build >= 9200)  name = "Windows 8 / Server 2012";
    } else if (maj == 6) {
        if      (g_vi.dwMinorVersion == 3) name = "Windows 8.1 (not supported)";
        else if (g_vi.dwMinorVersion == 2) name = "Windows 8 (not supported)";
        else if (g_vi.dwMinorVersion == 1) name = "Windows 7 (NOT SUPPORTED)";
        else                               name = "Windows Vista (NOT SUPPORTED)";
    }

    printf("    OS              : %s\n", name);
    printf("    Version         : %lu.%lu.%lu\n", maj, g_vi.dwMinorVersion, build);
    printf("    Supported       : %s\n", (maj >= 10) ? "YES (Win10+)" : "NO (below Win10)");

    if (maj < 10) {
        printf("    CONCLUSION: This OS is not supported. Driver must not be installed.\n");
        return 0;
    }
    printf("    CONCLUSION: OS is within the supported Win10+ range.\n");
    return 1;
}

/* ────────────────────────── main ───────────────────────────────────────────── */
int main(void)
{
    int r;

    printf("============================================================\n");
    printf("  IntelAvbFilter -- Windows OS Compatibility Tests\n");
    printf("  Implements: #258 (TEST-COMPAT-WIN7-001)\n");
    printf("  Win7 EOL Jan 2020 -- driver requires Windows 10+\n");
    printf("============================================================\n\n");

#define RUN(tc, label) \
    printf("  %s\n", (label)); \
    r = tc(); \
    if      (r > 0) { g_pass++; printf("  [PASS] %s\n\n", (label)); } \
    else if (r == 0){ g_fail++; printf("  [FAIL] %s\n\n", (label)); } \
    else            { g_skip++; printf("  [SKIP] %s\n\n", (label)); }

    RUN(TC_COMPAT_001_OSVersionMajor,      "TC-COMPAT-001: Runtime OS major >= 10 (ntdll!RtlGetVersion)");
    RUN(TC_COMPAT_002_BuildNumber,         "TC-COMPAT-002: Build number >= 9200 (Win10 minimum)");
    RUN(TC_COMPAT_003_DeviceNodeAccessible,"TC-COMPAT-003: Device node accessible on current OS");
    RUN(TC_COMPAT_004_VersionIoctl,        "TC-COMPAT-004: IOCTL_AVB_GET_VERSION on Win10+ ABI");
    RUN(TC_COMPAT_005_InfoRecord,          "TC-COMPAT-005: Windows version record (informational)");

    printf("-------------------------------------------\n");
    printf(" PASS=%d  FAIL=%d  SKIP=%d  TOTAL=%d\n",
           g_pass, g_fail, g_skip, g_pass + g_fail + g_skip);
    printf("-------------------------------------------\n");

    return (g_fail == 0) ? 0 : 1;
}
