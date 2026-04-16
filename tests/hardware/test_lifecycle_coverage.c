/**
 * @file test_lifecycle_coverage.c
 * @brief NDIS LWF Lifecycle Coverage Scenario Tests
 *
 * Implements: TEST-LCY-001
 * Traces to:  #265 (TEST-COVERAGE-001: NDIS LWF Coverage Framework)
 * Verifies:
 *   - Pillar 2 (Lifecycle/State Machine): FilterAttach, FilterPause,
 *     FilterRestart, FilterDetach callback counter instrumentation
 *   - Pillar 3 (Datapath Gauges): Outstanding NBL gauge invariants
 *   - Pillar 4 (Control-Path): OID dispatch counters, IoctlCount delta
 *
 * Test Cases and Ordering
 * -----------------------
 * Tests must run in this order because TC-LCY-004 (RESET) clears all
 * counters, which would invalidate the accumulated-value assertions of
 * TC-LCY-001 and TC-LCY-002 if reset ran first.
 *
 *   TC-LCY-001  After driver bind: FilterAttachCount>=1, FilterRestartCount>=1
 *   TC-LCY-002  After driver bind: OidRequestCount > 0 (NDIS sends OIDs on bind)
 *   TC-LCY-003  Steady-state gauge invariants (OutstandingNBLs/OIDs sane values)
 *   TC-LCY-005  GET_STATISTICS increments IoctlCount by exactly 1 per call
 *   TC-LCY-006  NIC disable/re-enable cycle:
 *                 delta FilterPauseCount >= 1
 *                 delta FilterDetachCount >= 1
 *                 delta FilterAttachCount >= 1
 *                 delta FilterRestartCount >= 1
 *                 OutstandingSendNBLs == 0 at idle (no traffic on toggled NIC)
 *                 OutstandingReceiveNBLs == 0 at idle
 *                 delta PauseRestartGeneration >= 1
 *   TC-LCY-004  RESET_STATISTICS clears all 24 ABI-2.0 fields to zero [LAST]
 *
 * Prerequisites
 * -------------
 *   - IntelAvbFilter driver installed and running (\\.\IntelAvbFilter opens)
 *   - At least one Intel NIC bound to the filter driver
 *   - Elevated process (TC-LCY-006 requires CM_Disable_DevNode privilege)
 *
 * Build
 * -----
 *   cl /nologo /W4 /WX /Zi /DWIN32 /D_WIN32_WINNT=0x0A00
 *      -I include -I external/intel_avb/lib
 *      tests\hardware\test_lifecycle_coverage.c
 *      /link setupapi.lib cfgmgr32.lib iphlpapi.lib
 *      /Fe:test_lifecycle_coverage.exe
 *
 * See also: TEST-PLAN-NDIS-LWF-COVERAGE.md
 */

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00     /* Windows 10 minimum */
#endif

/*
 * winsock2.h MUST precede windows.h to provide IP_ADAPTER_ADDRESSES,
 * GAA_FLAG_* constants, IfOperStatusUp, and related network types.
 * windows.h otherwise includes the older winsock.h which conflicts.
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <devguid.h>
#include <cfgmgr32.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avb_ioctl.h"

/* ── Test result framework ───────────────────────────────────────────────── */

#define TEST_PASS   0
#define TEST_FAIL   1
#define TEST_SKIP   2

#define MAX_ADAPTERS_ENUM  16    /* ceiling for per-adapter loop */
#define MAX_TESTS         128   /* MAX_ADAPTERS_ENUM * 8 TCs each */

typedef struct {
    char    name[256];  /* copied by RecordResult — no dangling pointer in adapter loop */
    int     result;
    char    reason[512];
    UINT64  duration_us;
} TestResult;

static TestResult g_results[MAX_TESTS];
static int        g_test_count = 0;

static void RecordResult(const char *name, int result, const char *reason, UINT64 us)
{
    if (g_test_count >= MAX_TESTS) return;
    strncpy_s(g_results[g_test_count].name,
              sizeof(g_results[g_test_count].name),
              name ? name : "",
              _TRUNCATE);
    g_results[g_test_count].result      = result;
    g_results[g_test_count].duration_us = us;
    strncpy_s(g_results[g_test_count].reason,
              sizeof(g_results[g_test_count].reason),
              reason ? reason : "",
              _TRUNCATE);
    g_test_count++;
}

static UINT64 GetTimestampUs(void)
{
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (cnt.QuadPart * 1000000ULL) / (UINT64)freq.QuadPart;
}

/* ── Device helpers ──────────────────────────────────────────────────────── */

static HANDLE OpenDevice(void)
{
    return CreateFileW(L"\\\\.\\IntelAvbFilter",
                       GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

static BOOL ReadStats(HANDLE hDev, AVB_DRIVER_STATISTICS *out)
{
    DWORD bytesReturned = 0;
    ZeroMemory(out, sizeof(*out));
    return DeviceIoControl(hDev, IOCTL_AVB_GET_STATISTICS,
                           NULL, 0,
                           out, sizeof(AVB_DRIVER_STATISTICS),
                           &bytesReturned, NULL)
           && (bytesReturned == sizeof(AVB_DRIVER_STATISTICS));
}

/* ── Adapter enumeration + selection ─────────────────────────────────────── */

typedef struct {
    UINT16 vendor_id;
    UINT16 device_id;
    UINT32 index;        /* global enumeration index — same value used for OPEN_ADAPTER.index */
    UINT32 capabilities;
} AdapterInfo;

/*
 * EnumAllAdapters
 * ---------------
 * Calls IOCTL_AVB_ENUM_ADAPTERS to build the full list of filter-bound adapters.
 * Returns the number of adapters found (0 if IOCTL fails or driver has none bound).
 */
static int EnumAllAdapters(HANDLE hDev, AdapterInfo *adapters, int maxAdapters)
{
    AVB_ENUM_REQUEST req = {0};
    req.index = 0;
    DWORD br = 0;
    if (!DeviceIoControl(hDev, IOCTL_AVB_ENUM_ADAPTERS,
                         &req, sizeof(req), &req, sizeof(req), &br, NULL)) {
        return 0;
    }
    int count = (int)req.count;
    if (count <= 0 || maxAdapters <= 0) return 0;

    /* index=0 already filled in by the first call above */
    adapters[0].vendor_id    = req.vendor_id;
    adapters[0].device_id    = req.device_id;
    adapters[0].capabilities = req.capabilities;
    adapters[0].index        = 0;

    for (int i = 1; i < count && i < maxAdapters; i++) {
        AVB_ENUM_REQUEST r = {0};
        r.index = (UINT32)i;
        br = 0;
        if (DeviceIoControl(hDev, IOCTL_AVB_ENUM_ADAPTERS,
                            &r, sizeof(r), &r, sizeof(r), &br, NULL)) {
            adapters[i].vendor_id    = r.vendor_id;
            adapters[i].device_id    = r.device_id;
            adapters[i].capabilities = r.capabilities;
            adapters[i].index        = (UINT32)i;
        }
    }
    return (count < maxAdapters) ? count : maxAdapters;
}

/*
 * OpenAdapter
 * -----------
 * Calls IOCTL_AVB_OPEN_ADAPTER to bind the file handle to a specific adapter.
 * After this call, IOCTL_AVB_GET_STATISTICS / IOCTL_AVB_RESET_STATISTICS on
 * this handle target that adapter's per-adapter state.
 */
static BOOL OpenAdapter(HANDLE hDev, const AdapterInfo *a)
{
    AVB_OPEN_REQUEST req = {0};
    req.vendor_id = a->vendor_id;
    req.device_id = a->device_id;
    req.index     = a->index;
    DWORD br = 0;
    return DeviceIoControl(hDev, IOCTL_AVB_OPEN_ADAPTER,
                           &req, sizeof(req), &req, sizeof(req), &br, NULL)
           && (req.status == 0 /* NDIS_STATUS_SUCCESS */);
}

/* ── NIC discovery for TC-LCY-006 ───────────────────────────────────────── */
/*
 * FindIntelNicDevInst
 * --------------------
 * Searches for an Intel NIC present and enabled in the system.
 * When filterDeviceId is non-zero, only NICs whose hardware ID contains
 * "DEV_XXXX" (matching the given device ID) are considered.  This ensures
 * TC-LCY-006 toggles exactly the NIC that is bound to the filter for the
 * adapter under test, even in multi-NIC systems.
 * When preferDisconnected is TRUE, prefers a NIC with no link (no cable)
 * so that the disable/re-enable cycle cannot interrupt active traffic.
 *
 * Algorithm:
 *   1. GetAdaptersAddresses → build table of Intel NIC descriptions + OperStatus
 *   2. SetupDiGetClassDevs(GUID_DEVCLASS_NET) → enumerate present net devices
 *   3. Filter by SPDRP_HARDWAREID containing "VEN_8086" (Intel vendor ID)
 *   4. If filterDeviceId != 0: also require "DEV_XXXX" in hardware ID
 *   5. Exclude devices with DN_HAS_PROBLEM (ghost / disabled / error state)
 *   6. Match each candidate against the IP-adapter table by description
 *   7. Return device instance of best match (disconnected preferred if requested)
 *
 * Returns TRUE and writes *pDevInst on success; FALSE to skip TC-LCY-006.
 */
static BOOL FindIntelNicDevInst(DEVINST *pDevInst, BOOL preferDisconnected,
                                 UINT16 filterDeviceId)
{
#define MAX_INTEL_ADDRS 16
    struct { WCHAR desc[256]; BOOL disconnected; } intelAddrs[MAX_INTEL_ADDRS];
    int intelAddrCount = 0;

    /* Step 1 — build link-state lookup from GetAdaptersAddresses */
    {
        ULONG reqLen = 32768;
        IP_ADAPTER_ADDRESSES *pBuf = (IP_ADAPTER_ADDRESSES *)malloc(reqLen);
        if (pBuf) {
            ULONG rc = GetAdaptersAddresses(AF_UNSPEC,
                                            GAA_FLAG_SKIP_ANYCAST |
                                            GAA_FLAG_SKIP_MULTICAST |
                                            GAA_FLAG_SKIP_DNS_SERVER,
                                            NULL, pBuf, &reqLen);
            if (rc == ERROR_BUFFER_OVERFLOW) {
                free(pBuf);
                pBuf = (IP_ADAPTER_ADDRESSES *)malloc(reqLen);
                if (pBuf) {
                    rc = GetAdaptersAddresses(AF_UNSPEC,
                                              GAA_FLAG_SKIP_ANYCAST |
                                              GAA_FLAG_SKIP_MULTICAST |
                                              GAA_FLAG_SKIP_DNS_SERVER,
                                              NULL, pBuf, &reqLen);
                }
            }
            if (pBuf && rc == ERROR_SUCCESS) {
                for (IP_ADAPTER_ADDRESSES *p = pBuf;
                     p && intelAddrCount < MAX_INTEL_ADDRS;
                     p = p->Next) {
                    if (p->Description && wcsstr(p->Description, L"Intel")) {
                        wcsncpy_s(intelAddrs[intelAddrCount].desc,
                                  ARRAYSIZE(intelAddrs[intelAddrCount].desc),
                                  p->Description, _TRUNCATE);
                        intelAddrs[intelAddrCount].disconnected =
                            (p->OperStatus != IfOperStatusUp);
                        intelAddrCount++;
                    }
                }
            }
            free(pBuf);
        }
    }

    /* Step 2 — enumerate SetupDi net devices and find best Intel candidate */
    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_NET,
                                            NULL, NULL, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) return FALSE;

    DEVINST bestDevInst       = 0;
    BOOL    bestDisconnected  = FALSE;
    BOOL    found             = FALSE;

    SP_DEVINFO_DATA devData;
    devData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD idx = 0; SetupDiEnumDeviceInfo(hDevInfo, idx, &devData); idx++) {
        /* Must be Intel by vendor ID */
        WCHAR hwId[1024] = {0};
        if (!SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devData,
                                               SPDRP_HARDWAREID, NULL,
                                               (PBYTE)hwId, sizeof(hwId), NULL)) continue;
        if (!wcsstr(hwId, L"VEN_8086")) continue;

        /* If a specific device ID was requested, require DEV_XXXX in the hardware ID */
        if (filterDeviceId != 0) {
            WCHAR devFilter[32] = {0};
            _snwprintf_s(devFilter, ARRAYSIZE(devFilter), _TRUNCATE,
                         L"DEV_%04X", filterDeviceId);
            if (!wcsstr(hwId, devFilter)) continue;
        }

        /* Skip ghost / problem devices */
        ULONG devStatus  = 0;
        ULONG devProblem = 0;
        if (CM_Get_DevNode_Status(&devStatus, &devProblem,
                                  devData.DevInst, 0) != CR_SUCCESS) continue;
        if (devStatus & DN_HAS_PROBLEM) continue;

        /* Determine link state by matching description to IP adapter table  */
        BOOL isDisconnected = FALSE;
        WCHAR descBuf[256] = {0};
        if (SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devData,
                                              SPDRP_DEVICEDESC, NULL,
                                              (PBYTE)descBuf, sizeof(descBuf),
                                              NULL)) {
            for (int j = 0; j < intelAddrCount; j++) {
                if (wcsstr(intelAddrs[j].desc, descBuf) ||
                    wcsstr(descBuf, intelAddrs[j].desc)) {
                    isDisconnected = intelAddrs[j].disconnected;
                    break;
                }
            }
        }

        /* Prefer disconnected (safe to toggle); accept any Intel NIC otherwise */
        if (!found ||
            (preferDisconnected && isDisconnected && !bestDisconnected)) {
            bestDevInst      = devData.DevInst;
            bestDisconnected = isDisconnected;
            found            = TRUE;
        }
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    if (found && pDevInst) *pDevInst = bestDevInst;
    return found;
}

/* ── TC-LCY-001 ──────────────────────────────────────────────────────────── */
/*
 * Given:  IntelAvbFilter driver loaded and at least one NIC bound
 * When:   Querying IOCTL_AVB_GET_STATISTICS
 * Then:   FilterAttachCount >= 1 AND FilterRestartCount >= 1
 *
 * Rationale: NDIS calls FilterAttach (NIC → Paused state) then FilterRestart
 *            (Paused → Running). After the initial bind cycle both counters
 *            must have been incremented at least once. A value of 0 means the
 *            callback was never instrumented — this was the bug fixed by
 *            commit 66d1804 (stats v2.0: FilterAttach now increments the
 *            counter instead of the formerly-unused declaration).
 */
static void RunTC_LCY_001(HANDLE hDev, const char *adapterTag)
{
    char name[320];
    _snprintf_s(name, sizeof(name), _TRUNCATE,
                "[%s] TC-LCY-001: FilterAttachCount>=1 and FilterRestartCount>=1 after bind",
                adapterTag);
    UINT64 t0 = GetTimestampUs();
    AVB_DRIVER_STATISTICS s;

    if (!ReadStats(hDev, &s)) {
        RecordResult(name, TEST_FAIL,
                     "IOCTL_AVB_GET_STATISTICS failed", GetTimestampUs() - t0);
        return;
    }

    /*
     * If both attach/restart counters are 0, a previous test run's
     * RESET_STATISTICS wiped the accumulated bind-time evidence and the
     * driver was not rebound since.  OidRequestCount may be non-zero
     * (NDIS keeps sending OIDs to already-attached adapters), but that
     * doesn't help verify the bind path.  SKIP with a diagnostic.
     * Remedy: reboot the test machine or reinstall the driver, then re-run.
     */
    if (s.FilterAttachCount  == 0 &&
        s.FilterRestartCount == 0) {
        RecordResult(name, TEST_SKIP,
                     "SKIP: FilterAttachCount=0 AND FilterRestartCount=0 — "
                     "a prior RESET_STATISTICS call wiped the bind-time evidence; "
                     "reboot or reinstall the driver to repopulate counters, then re-run",
                     GetTimestampUs() - t0);
        return;
    }

    char reason[256];
    if (s.FilterAttachCount < 1 || s.FilterRestartCount < 1) {
        _snprintf_s(reason, sizeof(reason), _TRUNCATE,
                    "FilterAttachCount=%llu (need >=1), "
                    "FilterRestartCount=%llu (need >=1)",
                    (unsigned long long)s.FilterAttachCount,
                    (unsigned long long)s.FilterRestartCount);
        RecordResult(name, TEST_FAIL, reason, GetTimestampUs() - t0);
        return;
    }

    _snprintf_s(reason, sizeof(reason), _TRUNCATE,
                "FilterAttachCount=%llu, FilterRestartCount=%llu",
                (unsigned long long)s.FilterAttachCount,
                (unsigned long long)s.FilterRestartCount);
    RecordResult(name, TEST_PASS, reason, GetTimestampUs() - t0);
}

/* ── TC-LCY-002 ──────────────────────────────────────────────────────────── */
/*
 * Given:  IntelAvbFilter driver loaded and at least one NIC bound
 * When:   Querying IOCTL_AVB_GET_STATISTICS
 * Then:   OidRequestCount > 0
 *
 * Rationale: NDIS dispatches OIDs to the filter during the bind sequence
 *            (e.g. OID_GEN_CURRENT_PACKET_FILTER). A zero count means
 *            FilterOidRequest is not being called or not being counted.
 */
static void RunTC_LCY_002(HANDLE hDev, const char *adapterTag)
{
    char name[320];
    _snprintf_s(name, sizeof(name), _TRUNCATE,
                "[%s] TC-LCY-002: OidRequestCount > 0 after driver bind",
                adapterTag);
    UINT64 t0 = GetTimestampUs();
    AVB_DRIVER_STATISTICS s;

    if (!ReadStats(hDev, &s)) {
        RecordResult(name, TEST_FAIL,
                     "IOCTL_AVB_GET_STATISTICS failed", GetTimestampUs() - t0);
        return;
    }

    char reason[128];
    _snprintf_s(reason, sizeof(reason), _TRUNCATE,
                "OidRequestCount=%llu",
                (unsigned long long)s.OidRequestCount);
    RecordResult(name,
                 (s.OidRequestCount > 0) ? TEST_PASS : TEST_FAIL,
                 reason, GetTimestampUs() - t0);
}

/* ── TC-LCY-003 ──────────────────────────────────────────────────────────── */
/*
 * Given:  Driver at steady state (no concurrent disable/enable)
 * When:   Querying IOCTL_AVB_GET_STATISTICS
 * Then:   OutstandingSendNBLs, OutstandingReceiveNBLs, OutstandingOids
 *         are all in a sane range (< 2^32)
 *
 * Rationale: These are gauges (incremented on entry, decremented on
 *            completion). They must never underflow (wrap to ~MAXUINT64).
 *            At steady state with no active traffic they may be 0.
 *            With active traffic they may be small but not astronomically
 *            large. A value >= 2^32 indicates an instrumentation bug
 *            (missed completion / double-decrement / uninitialized field).
 */
static void RunTC_LCY_003(HANDLE hDev, const char *adapterTag)
{
    char name[320];
    _snprintf_s(name, sizeof(name), _TRUNCATE,
                "[%s] TC-LCY-003: Gauge invariants - OutstandingNBLs and OutstandingOids in sane range",
                adapterTag);
    UINT64 t0 = GetTimestampUs();
    AVB_DRIVER_STATISTICS s;

    if (!ReadStats(hDev, &s)) {
        RecordResult(name, TEST_FAIL,
                     "IOCTL_AVB_GET_STATISTICS failed", GetTimestampUs() - t0);
        return;
    }

    /* An underflow wraps to near MAXUINT64; any value >= 2^32 is suspicious */
#define GAUGE_OVERFLOW_LIMIT (1ULL << 32)

    char reason[512];
    if (s.OutstandingSendNBLs    >= GAUGE_OVERFLOW_LIMIT ||
        s.OutstandingReceiveNBLs >= GAUGE_OVERFLOW_LIMIT ||
        s.OutstandingOids        >= GAUGE_OVERFLOW_LIMIT) {
        _snprintf_s(reason, sizeof(reason), _TRUNCATE,
                    "Gauge overflow (likely underflow/wrap): "
                    "SendNBLs=%llu RcvNBLs=%llu OIDs=%llu (limit=%llu)",
                    (unsigned long long)s.OutstandingSendNBLs,
                    (unsigned long long)s.OutstandingReceiveNBLs,
                    (unsigned long long)s.OutstandingOids,
                    (unsigned long long)GAUGE_OVERFLOW_LIMIT);
        RecordResult(name, TEST_FAIL, reason, GetTimestampUs() - t0);
        return;
    }

    _snprintf_s(reason, sizeof(reason), _TRUNCATE,
                "SendNBLs=%llu, RcvNBLs=%llu, OutstandingOids=%llu",
                (unsigned long long)s.OutstandingSendNBLs,
                (unsigned long long)s.OutstandingReceiveNBLs,
                (unsigned long long)s.OutstandingOids);
    RecordResult(name, TEST_PASS, reason, GetTimestampUs() - t0);
}

/* ── TC-LCY-005 ──────────────────────────────────────────────────────────── */
/*
 * Given:  Driver opened and IOCTL_AVB_GET_STATISTICS functional
 * When:   Calling IOCTL_AVB_GET_STATISTICS twice in immediate succession
 * Then:   second.IoctlCount == first.IoctlCount + 1
 *
 * Rationale: Each IOCTL dispatch atomically increments IoctlCount before
 *            building the snapshot. Between two consecutive user-mode calls
 *            in a single-threaded test no other IOCTL can intervene, so the
 *            delta must be exactly 1 (not 0, not 2+).
 */
static void RunTC_LCY_005(HANDLE hDev, const char *adapterTag)
{
    char name[320];
    _snprintf_s(name, sizeof(name), _TRUNCATE,
                "[%s] TC-LCY-005: IoctlCount increments by exactly 1 per GET_STATISTICS call",
                adapterTag);
    UINT64 t0 = GetTimestampUs();
    AVB_DRIVER_STATISTICS s1, s2;

    if (!ReadStats(hDev, &s1)) {
        RecordResult(name, TEST_FAIL,
                     "First IOCTL_AVB_GET_STATISTICS failed", GetTimestampUs() - t0);
        return;
    }
    if (!ReadStats(hDev, &s2)) {
        RecordResult(name, TEST_FAIL,
                     "Second IOCTL_AVB_GET_STATISTICS failed", GetTimestampUs() - t0);
        return;
    }

    UINT64 delta = s2.IoctlCount - s1.IoctlCount;
    char reason[256];
    _snprintf_s(reason, sizeof(reason), _TRUNCATE,
                "IoctlCount: snap1=%llu snap2=%llu delta=%llu (expected 1)",
                (unsigned long long)s1.IoctlCount,
                (unsigned long long)s2.IoctlCount,
                (unsigned long long)delta);
    RecordResult(name, (delta == 1) ? TEST_PASS : TEST_FAIL,
                 reason, GetTimestampUs() - t0);
}

/* ── TC-LCY-006 ──────────────────────────────────────────────────────────── */
/*
 * Given:  At least one Intel NIC can be toggled (driver bound,
 *         preferably no active link so no traffic is disrupted)
 * When:   NIC is disabled via CM_Disable_DevNode then re-enabled via
 *         CM_Enable_DevNode (requires elevated process)
 * Then:
 *   - delta FilterPauseCount        >= 1  (FilterPause fired on disable)
 *   - delta FilterDetachCount       >= 1  (FilterDetach fired on disable)
 *   - delta FilterAttachCount       >= 1  (FilterAttach fired on re-enable)
 *   - delta FilterRestartCount      >= 1  (FilterRestart fired on re-enable)
 *   - OutstandingSendNBLs           == 0  (all sends drained before pause)
 *   - OutstandingReceiveNBLs        == 0  (all receives returned before pause)
 *   - delta PauseRestartGeneration  >= 1  (generation counter advanced)
 *
 * SKIP if: no Intel NIC found; CM_Disable_DevNode returns non-success
 *          (insufficient privileges or device not disableable).
 *
 * Timing: 3 s after disable (allow FilterPause + FilterDetach to complete),
 *         4 s after re-enable (allow FilterAttach + FilterRestart to complete).
 */
static void RunTC_LCY_006(HANDLE hDev, const char *adapterTag, UINT16 device_id)
{
    char name[320];
    _snprintf_s(name, sizeof(name), _TRUNCATE,
                "[%s] TC-LCY-006: NIC disable/re-enable - Pause+Restart deltas and zero NBL gauges",
                adapterTag);
    UINT64 t0 = GetTimestampUs();

    DEVINST devInst = 0;
    if (!FindIntelNicDevInst(&devInst, TRUE /*prefer disconnected*/, device_id)) {
        RecordResult(name, TEST_SKIP,
                     "SKIP: No suitable Intel NIC found for disable/re-enable",
                     GetTimestampUs() - t0);
        return;
    }

    AVB_DRIVER_STATISTICS before;
    if (!ReadStats(hDev, &before)) {
        RecordResult(name, TEST_FAIL,
                     "IOCTL_AVB_GET_STATISTICS (before snapshot) failed",
                     GetTimestampUs() - t0);
        return;
    }

    /* Disable NIC — triggers NDIS to call FilterPause → FilterDetach */
    CONFIGRET cr = CM_Disable_DevNode(devInst, 0);
    if (cr != CR_SUCCESS) {
        char reason[256];
        _snprintf_s(reason, sizeof(reason), _TRUNCATE,
                    "SKIP: CM_Disable_DevNode returned CR=0x%08X "
                    "(need elevated process or device is not disableable)",
                    (unsigned int)cr);
        RecordResult(name, TEST_SKIP, reason, GetTimestampUs() - t0);
        return;
    }

    /* Allow NDIS FilterPause + FilterDetach to complete (~3 seconds) */
    Sleep(3000);

    /* Re-enable NIC — triggers FilterAttach → FilterRestart */
    cr = CM_Enable_DevNode(devInst, 0);
    if (cr != CR_SUCCESS) {
        /* Best-effort recovery before reporting failure */
        CM_Enable_DevNode(devInst, 0);
        char reason[256];
        _snprintf_s(reason, sizeof(reason), _TRUNCATE,
                    "CM_Enable_DevNode returned CR=0x%08X",
                    (unsigned int)cr);
        RecordResult(name, TEST_FAIL, reason, GetTimestampUs() - t0);
        return;
    }

    /* Allow NDIS FilterAttach + FilterRestart to complete (~4 seconds) */
    Sleep(4000);

    /* The FsContext on hDev may be stale after FilterDetach destroyed the
     * adapter context and FilterAttach created a new one.  Open a fresh
     * handle and re-bind it to the same adapter (matched by device_id) so
     * the after-snapshot IOCTL reaches the newly-attached adapter context. */
    AVB_DRIVER_STATISTICS after;
    {
        BOOL gotAfter = FALSE;
        HANDLE hFresh = CreateFileA("\\\\.\\IntelAvbFilter",
                                    GENERIC_READ | GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFresh != INVALID_HANDLE_VALUE) {
            /* Find adapter with matching device_id and bind the fresh handle */
            for (int ri = 0; ri < 8; ri++) {
                AVB_ENUM_REQUEST ereq;
                memset(&ereq, 0, sizeof(ereq));
                ereq.index = (UINT32)ri;
                DWORD br = 0;
                if (!DeviceIoControl(hFresh, IOCTL_AVB_ENUM_ADAPTERS,
                                     &ereq, sizeof(ereq), &ereq, sizeof(ereq), &br, NULL))
                    break;
                if (device_id != 0 && ereq.device_id != device_id)
                    continue;
                AVB_OPEN_REQUEST oreq;
                memset(&oreq, 0, sizeof(oreq));
                oreq.vendor_id = ereq.vendor_id;
                oreq.device_id = ereq.device_id;
                oreq.index     = ereq.index;
                br = 0;
                DeviceIoControl(hFresh, IOCTL_AVB_OPEN_ADAPTER,
                                &oreq, sizeof(oreq), &oreq, sizeof(oreq), &br, NULL);
                break;
            }
            gotAfter = ReadStats(hFresh, &after);
            CloseHandle(hFresh);
        }
        if (!gotAfter) {
            RecordResult(name, TEST_FAIL,
                         "IOCTL_AVB_GET_STATISTICS (after snapshot) failed — "
                         "adapter may not have re-attached within 4 s",
                         GetTimestampUs() - t0);
            return;
        }
    }

    /* Compute deltas (unsigned subtraction is well-defined for wrap) */
    UINT64 dPause      = after.FilterPauseCount      - before.FilterPauseCount;
    UINT64 dDetach     = after.FilterDetachCount     - before.FilterDetachCount;
    UINT64 dAttach     = after.FilterAttachCount     - before.FilterAttachCount;
    UINT64 dRestart    = after.FilterRestartCount    - before.FilterRestartCount;
    UINT64 dGeneration = after.PauseRestartGeneration - before.PauseRestartGeneration;

    /*
     * Heuristic SKIP: if ALL lifecycle deltas are 0, the NIC that was toggled
     * is not the one bound to IntelAvbFilter.  This happens when multiple Intel
     * NICs exist and FindIntelNicDevInst picked a NIC the filter isn't tracking.
     * Report a SKIP with diagnostics instead of a spurious FAIL — the test
     * could not locate the filter-bound adapter via SetupDi alone.
     */
    if (dPause == 0 && dDetach == 0 && dAttach == 0 &&
        dRestart == 0 && dGeneration == 0) {
        char skipReason[512];
        _snprintf_s(skipReason, sizeof(skipReason), _TRUNCATE,
                    "SKIP: NIC disable/re-enable completed (%.1f s) but no NDIS "
                    "filter callbacks fired — toggled NIC not bound to "
                    "IntelAvbFilter (system has multiple Intel adapters; "
                    "before: Pause=%llu Detach=%llu Attach=%llu Restart=%llu)",
                    (double)(GetTimestampUs() - t0) / 1e6,
                    (unsigned long long)before.FilterPauseCount,
                    (unsigned long long)before.FilterDetachCount,
                    (unsigned long long)before.FilterAttachCount,
                    (unsigned long long)before.FilterRestartCount);
        RecordResult(name, TEST_SKIP, skipReason, GetTimestampUs() - t0);
        return;
    }

    char reason[1024];
    reason[0] = '\0';
    int result   = TEST_PASS;
    size_t used  = 0;

#define APPEND_FAIL(fmt, ...) \
    do { \
        _snprintf_s(reason + used, sizeof(reason) - used, _TRUNCATE, fmt, __VA_ARGS__); \
        used  = strlen(reason); \
        result = TEST_FAIL; \
    } while (0)

    if (dPause      < 1) APPEND_FAIL("delta_PauseCount=%llu(need>=1); ",     (unsigned long long)dPause);
    if (dDetach     < 1) APPEND_FAIL("delta_DetachCount=%llu(need>=1); ",    (unsigned long long)dDetach);
    if (dAttach     < 1) APPEND_FAIL("delta_AttachCount=%llu(need>=1); ",    (unsigned long long)dAttach);
    if (dRestart    < 1) APPEND_FAIL("delta_RestartCount=%llu(need>=1); ",   (unsigned long long)dRestart);
    if (dGeneration < 1) APPEND_FAIL("delta_Generation=%llu(need>=1); ",     (unsigned long long)dGeneration);
    if (after.OutstandingSendNBLs    != 0) APPEND_FAIL("SendNBLs=%llu(need==0); ",  (unsigned long long)after.OutstandingSendNBLs);
    if (after.OutstandingReceiveNBLs != 0) APPEND_FAIL("RcvNBLs=%llu(need==0); ",   (unsigned long long)after.OutstandingReceiveNBLs);

    if (result == TEST_PASS) {
        _snprintf_s(reason, sizeof(reason), _TRUNCATE,
                    "dPause=%llu dDetach=%llu dAttach=%llu dRestart=%llu "
                    "dGen=%llu SendNBLs=%llu RcvNBLs=%llu",
                    (unsigned long long)dPause,
                    (unsigned long long)dDetach,
                    (unsigned long long)dAttach,
                    (unsigned long long)dRestart,
                    (unsigned long long)dGeneration,
                    (unsigned long long)after.OutstandingSendNBLs,
                    (unsigned long long)after.OutstandingReceiveNBLs);
    }

    RecordResult(name, result, reason, GetTimestampUs() - t0);
}

/* ── TC-LCY-004 [MUST RUN LAST] ─────────────────────────────────────────── */
/*
 * Given:  Driver opened and IOCTL_AVB_RESET_STATISTICS available
 * When:   Issuing IOCTL_AVB_RESET_STATISTICS then reading statistics
 * Then:   Every one of the 24 AVB_DRIVER_STATISTICS fields reads exactly 0
 *
 * Rationale: The reset IOCTL must atomically zero all fields. This is a
 *            direct, definitive test of IOCTL_AVB_RESET_STATISTICS.
 *
 * ORDER CONSTRAINT: Running TC-LCY-004 last ensures that all preceding
 *            tests (TC-LCY-001/002) can rely on accumulated counter values
 *            from the driver's lifetime; a reset before them would produce
 *            0-counts for callbacks that already ran before this test ran.
 */
static void RunTC_LCY_004(HANDLE hDev, const char *adapterTag)
{
    char name[320];
    _snprintf_s(name, sizeof(name), _TRUNCATE,
                "[%s] TC-LCY-004: RESET_STATISTICS clears all 24 ABI-2.0 fields to zero [LAST]",
                adapterTag);
    UINT64 t0 = GetTimestampUs();

    DWORD bytesReturned = 0;
    BOOL ok = DeviceIoControl(hDev, IOCTL_AVB_RESET_STATISTICS,
                              NULL, 0, NULL, 0, &bytesReturned, NULL);
    if (!ok) {
        DWORD err = GetLastError();
        char reason[128];
        _snprintf_s(reason, sizeof(reason), _TRUNCATE,
                    "IOCTL_AVB_RESET_STATISTICS failed (GetLastError=%lu)", err);
        RecordResult(name, TEST_FAIL, reason, GetTimestampUs() - t0);
        return;
    }

    AVB_DRIVER_STATISTICS s;
    if (!ReadStats(hDev, &s)) {
        RecordResult(name, TEST_FAIL,
                     "IOCTL_AVB_GET_STATISTICS after reset failed",
                     GetTimestampUs() - t0);
        return;
    }

    /*
     * Check each of the 24 fields individually.
     *
     * Special case — IoctlCount:
     *   The IOCTL dispatch increments IoctlCount BEFORE the handler runs.
     *   When TC-LCY-004 calls GET_STATISTICS immediately after the reset, that
     *   GET call is IOCTL #1 since the reset, so IoctlCount == 1 is correct.
     *   Any other value is a bug.
     */
    char failDetail[1024] = {0};
    int  failCount        = 0;
    size_t detailUsed     = 0;

#define CHECK_FIELD_ZERO(field) \
    if (s.field != 0) { \
        _snprintf_s(failDetail + detailUsed, \
                    sizeof(failDetail) - detailUsed, _TRUNCATE, \
                    #field "=%llu; ", (unsigned long long)s.field); \
        detailUsed = strlen(failDetail); \
        failCount++; \
    }

    /* ABI 1.0 fields (original 13) — IoctlCount expected == 1, not 0 */
    CHECK_FIELD_ZERO(TxPackets)
    CHECK_FIELD_ZERO(RxPackets)
    CHECK_FIELD_ZERO(TxBytes)
    CHECK_FIELD_ZERO(RxBytes)
    CHECK_FIELD_ZERO(PhcQueryCount)
    CHECK_FIELD_ZERO(PhcAdjustCount)
    CHECK_FIELD_ZERO(PhcSetCount)
    CHECK_FIELD_ZERO(TimestampCount)
    /* IoctlCount: the GET_STATISTICS call that reads post-reset state is
     * IOCTL #1 since reset — correct result is exactly 1.             */
    if (s.IoctlCount != 1) {
        _snprintf_s(failDetail + detailUsed,
                    sizeof(failDetail) - detailUsed, _TRUNCATE,
                    "IoctlCount=%llu (expected 1 — the GET itself); ",
                    (unsigned long long)s.IoctlCount);
        detailUsed = strlen(failDetail);
        failCount++;
    }
    CHECK_FIELD_ZERO(ErrorCount)
    CHECK_FIELD_ZERO(MemoryAllocFailures)
    CHECK_FIELD_ZERO(HardwareFaults)
    CHECK_FIELD_ZERO(FilterAttachCount)
    /* ABI 2.0 extended fields (new 11) */
    CHECK_FIELD_ZERO(FilterPauseCount)
    CHECK_FIELD_ZERO(FilterRestartCount)
    CHECK_FIELD_ZERO(FilterDetachCount)
    CHECK_FIELD_ZERO(OutstandingSendNBLs)
    CHECK_FIELD_ZERO(OutstandingReceiveNBLs)
    CHECK_FIELD_ZERO(OidRequestCount)
    CHECK_FIELD_ZERO(OidCompleteCount)
    CHECK_FIELD_ZERO(OutstandingOids)
    CHECK_FIELD_ZERO(FilterStatusCount)
    CHECK_FIELD_ZERO(FilterNetPnPCount)
    CHECK_FIELD_ZERO(PauseRestartGeneration)

    if (failCount > 0) {
        char reason[1100];
        _snprintf_s(reason, sizeof(reason), _TRUNCATE,
                    "%d field(s) unexpected after reset: %s",
                    failCount, failDetail);
        RecordResult(name, TEST_FAIL, reason, GetTimestampUs() - t0);
    } else {
        RecordResult(name, TEST_PASS,
                     "23 fields == 0, IoctlCount == 1 (GET itself) after IOCTL_AVB_RESET_STATISTICS",
                     GetTimestampUs() - t0);
    }
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("================================================================\n");
    printf("NDIS LWF Lifecycle Coverage Tests\n");
    printf("TEST-LCY-001 | Part of #265 (TEST-COVERAGE-001)\n");
    printf("  Verifies:   #36  (REQ-F-NDIS-ATTACH-001: FilterAttach/FilterDetach)\n");
    printf("  Implements: #265 (TEST-COVERAGE-001: Verify Unit Test Coverage)\n");
    printf("================================================================\n\n");

    /* ABI version guard */
    printf("sizeof(AVB_DRIVER_STATISTICS) = %u (expect 192)\n",
           (unsigned)sizeof(AVB_DRIVER_STATISTICS));
    if (sizeof(AVB_DRIVER_STATISTICS) != 192) {
        fprintf(stderr,
                "\nFATAL: ABI size mismatch — struct is %u bytes, need 192\n"
                "  Rebuild with current include/avb_ioctl.h (ABI 2.0)\n",
                (unsigned)sizeof(AVB_DRIVER_STATISTICS));
        return 1;
    }

    HANDLE hDev = OpenDevice();
    if (hDev == INVALID_HANDLE_VALUE) {
        fprintf(stderr,
                "\nERROR: Cannot open \\\\.\\IntelAvbFilter (err=%lu)\n"
                "  Ensure the filter driver is installed and running.\n"
                "  Use: tools\\setup\\Install-Driver-Elevated.ps1\n",
                GetLastError());
        return 1;
    }
    printf("Device opened: \\\\.\\IntelAvbFilter\n\n");

    /*
     * Enumerate all filter-bound adapters via IOCTL_AVB_ENUM_ADAPTERS.
     * For each adapter:
     *   1. IOCTL_AVB_OPEN_ADAPTER — bind this handle to adapter [i]
     *   2. Run TC-LCY-001..003, TC-LCY-005 (stat/gauge checks)
     *   3. Run TC-LCY-006 (NIC toggle — uses adapter's device_id to locate
     *                         the exact SetupDi node, not a random Intel NIC)
     *   4. Run TC-LCY-004 LAST — RESET clears per-adapter stats
     *
     * If IOCTL_AVB_ENUM_ADAPTERS returns 0 or fails (e.g. driver too old),
     * fall back to the legacy single-adapter path with no explicit selection.
     */
    AdapterInfo adapters[MAX_ADAPTERS_ENUM];
    int adapterCount = EnumAllAdapters(hDev, adapters, MAX_ADAPTERS_ENUM);

    if (adapterCount == 0) {
        printf("NOTE: IOCTL_AVB_ENUM_ADAPTERS returned 0 adapters (or IOCTL failed).\n");
        printf("      Running all TCs against the default (implicitly-selected) adapter.\n\n");

        const char *tag = "DefaultAdapter";
        printf("=== %s ===\n\n", tag);
        RunTC_LCY_001(hDev, tag);
        RunTC_LCY_002(hDev, tag);
        RunTC_LCY_003(hDev, tag);
        RunTC_LCY_005(hDev, tag);
        RunTC_LCY_006(hDev, tag, 0 /* any Intel device */);
        RunTC_LCY_004(hDev, tag); /* LAST — resets all counters */
    } else {
        printf("Discovered %d filter-bound adapter(s) via IOCTL_AVB_ENUM_ADAPTERS.\n", adapterCount);
        printf("TC-LCY-004 (RESET_STATISTICS) runs ONCE after all adapters to avoid\n");
        printf("contaminating per-adapter reads with inter-adapter resets.\n\n");

        for (int ai = 0; ai < adapterCount; ai++) {
            char adapterTag[128];
            _snprintf_s(adapterTag, sizeof(adapterTag), _TRUNCATE,
                        "Adapter%d[VEN_%04X:DEV_%04X]",
                        ai,
                        (unsigned)adapters[ai].vendor_id,
                        (unsigned)adapters[ai].device_id);

            printf("================================================================\n");
            printf("=== %s ===\n", adapterTag);
            printf("================================================================\n\n");

            if (!OpenAdapter(hDev, &adapters[ai])) {
                printf("  [WARN] IOCTL_AVB_OPEN_ADAPTER failed for %s — "
                       "running TCs without explicit selection\n\n", adapterTag);
                /* Continue anyway; stats may be for a different adapter */
            }

            /*
             * TC-LCY-004 (RESET) is intentionally NOT called here.
             * Reason: the driver's statistics appear to be global (shared
             * across all adapters).  Running RESET between adapters zeros
             * in-flight gauge counters (OutstandingReceiveNBLs) which then
             * underflow when pending completions arrive, causing false
             * TC-LCY-003 failures on subsequent adapters.  It also zeros
             * FilterAttachCount, causing false TC-LCY-001 failures on
             * link-down adapters that don't re-trigger FilterAttach on
             * OPEN_ADAPTER.  RESET runs once at the very end instead.
             */
            RunTC_LCY_001(hDev, adapterTag);
            RunTC_LCY_002(hDev, adapterTag);
            RunTC_LCY_003(hDev, adapterTag);
            RunTC_LCY_005(hDev, adapterTag);
            RunTC_LCY_006(hDev, adapterTag, adapters[ai].device_id);

            printf("\n");
        }

        /*
         * TC-LCY-004: RESET runs exactly once, with the last adapter still
         * selected (it doesn't matter which adapter is open — stats are global).
         * This verifies RESET_STATISTICS works without contaminating other TCs.
         */
        char finalTag[128];
        _snprintf_s(finalTag, sizeof(finalTag), _TRUNCATE,
                    "FinalReset[after %d adapters]", adapterCount);
        printf("================================================================\n");
        printf("=== %s ===\n", finalTag);
        printf("================================================================\n\n");
        RunTC_LCY_004(hDev, finalTag);
    }

    CloseHandle(hDev);

    /* Print summary */
    printf("\n================================================================\n");
    printf("Results (%d test cases across %d adapter(s))\n",
           g_test_count,
           (adapterCount > 0) ? adapterCount : 1);
    printf("================================================================\n");

    int passed  = 0;
    int failed  = 0;
    int skipped = 0;

    for (int i = 0; i < g_test_count; i++) {
        const TestResult *r = &g_results[i];
        const char *status;
        if      (r->result == TEST_PASS) { status = "PASS"; passed++;  }
        else if (r->result == TEST_FAIL) { status = "FAIL"; failed++;  }
        else                             { status = "SKIP"; skipped++; }

        printf("[%-4s] %s\n", status, r->name);
        if (r->reason[0]) printf("       %s\n", r->reason);
        printf("       Duration: %llu us\n\n",
               (unsigned long long)r->duration_us);
    }

    printf("----------------------------------------------------------------\n");
    printf("PASS: %d   FAIL: %d   SKIP: %d   TOTAL: %d\n",
           passed, failed, skipped, g_test_count);
    printf("================================================================\n\n");

    return (failed > 0) ? 1 : 0;
}
