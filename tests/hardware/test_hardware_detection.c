/**
 * @file test_hardware_detection.c
 * @brief Hardware Capability Detection & Register Access Safety Tests
 *
 * Implements: #288 (TEST-HW-DETECT-CAPS-001: Hardware Capability Detection)
 * Implements: #287 (TEST-REG-ACCESS-LOCK-001: Safe Register Access via Spin Locks)
 * Verifies:   #44  (REQ-F-HW-DETECT-001: Hardware Capability Detection)
 * Verifies:   #45  (REQ-F-REG-ACCESS-001: Safe Register Access via Spin Locks)
 *
 * SSOT: include/avb_ioctl.h (Single Source of Truth for all IOCTL definitions)
 *
 * Test Cases:
 *   TC-HW-001: Adapter enumeration — vendor_id == 0x8086 for every enumerated adapter
 *   TC-HW-002: HW state query     — hw_state >= AVB_HW_UNBOUND, INTEL_CAP_BASIC_1588 set
 *   TC-HW-003: Capability consistency — I225/I226 must have TSN_TAS; I210 must NOT
 *   TC-HW-004: Concurrent register access safety — 4 threads, no errors, consistent results
 *
 * Build (manually):
 *   cl /nologo /W4 /WX /Zi /DWIN32 /D_WIN32_WINNT=0x0A00 ^
 *      -I include -I external/intel_avb/lib ^
 *      tests\hardware\test_hardware_detection.c ^
 *      /Fe:test_hardware_detection.exe
 *
 * @see https://github.com/zarfld/IntelAvbFilter/issues/288
 * @see https://github.com/zarfld/IntelAvbFilter/issues/287
 */

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Single Source of Truth for IOCTL definitions */
#include "avb_ioctl.h"

/* INTEL_CAP_* flags — re-declared for self-contained build.
 * Primary source: external/intel_avb/lib/intel.h
 * Pattern: identical to avb_test_portability.c (guarded by #ifndef). */
#ifndef INTEL_CAP_BASIC_1588
#define INTEL_CAP_BASIC_1588    (1 << 0)   /* Basic IEEE 1588 timestamping */
#define INTEL_CAP_ENHANCED_TS   (1 << 1)   /* Enhanced TS: TSYNCRXCTL/TSYNCTXCTL */
#define INTEL_CAP_TSN_TAS       (1 << 2)   /* Time-Aware Shaper (802.1Qbv) */
#define INTEL_CAP_TSN_FP        (1 << 3)   /* Frame Preemption (802.1Qbu) */
#define INTEL_CAP_PCIE_PTM      (1 << 4)   /* PCIe Precision Time Measurement */
#define INTEL_CAP_2_5G          (1 << 5)   /* 2.5 Gbps speed support */
#define INTEL_CAP_MDIO          (1 << 6)   /* MDIO PHY register access */
#define INTEL_CAP_MMIO          (1 << 7)   /* Memory-mapped I/O (BAR0 mapped) */
#define INTEL_CAP_EEE           (1 << 8)   /* Energy Efficient Ethernet */
#endif

/* --------------------------------------------------------------------------
 * Test result codes
 * -------------------------------------------------------------------------- */
#define TEST_PASS 0
#define TEST_FAIL 1
#define TEST_SKIP 2

/* --------------------------------------------------------------------------
 * Known Intel NIC device IDs
 * -------------------------------------------------------------------------- */
#define DID_I210_COPPER   0x1533u
#define DID_I210_FIBER    0x1536u
#define DID_I210_SGMII    0x1537u
#define DID_I210_SERDES   0x1538u
#define DID_I219_LM       0x156Fu
#define DID_I219_V        0x1570u
#define DID_I225_V        0x15F3u
#define DID_I226_LM       0x125Cu

/* --------------------------------------------------------------------------
 * Thread argument for TC-HW-004 concurrent access test
 * -------------------------------------------------------------------------- */
typedef struct {
    HANDLE  device;
    int     iterations;
    int     errors;
    DWORD   last_hw_state;
    avb_u32 last_capabilities;
} ThreadArg;

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

/** Open the driver device node. Returns INVALID_HANDLE_VALUE on failure. */
static HANDLE OpenDevice(void)
{
    HANDLE h = CreateFileA(
        "\\\\.\\IntelAvbFilter",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    return h;
}

/** Enumerate adapters (index 0..count-1). Returns number found, -1 on IOCTL error. */
static int EnumAdapters(HANDLE h, AVB_ENUM_REQUEST *out_adapters, int max_count)
{
    int found = 0;
    for (int i = 0; i < max_count; i++) {
        AVB_ENUM_REQUEST req;
        DWORD br = 0;
        ZeroMemory(&req, sizeof(req));
        req.index = (avb_u32)i;
        if (!DeviceIoControl(h, IOCTL_AVB_ENUM_ADAPTERS,
                             &req, sizeof(req), &req, sizeof(req), &br, NULL)) {
            break; /* no more adapters */
        }
        if (req.status != 0) {
            break;
        }
        out_adapters[i] = req;
        found++;
    }
    return found;
}

/* --------------------------------------------------------------------------
 * TC-HW-001: Adapter Enumeration
 *   - Opens device; SKIPs if driver not loaded.
 *   - Calls IOCTL_AVB_ENUM_ADAPTERS for index 0..N.
 *   - Verifies every returned adapter has vendor_id == 0x8086 (Intel).
 *   - SKIPs (not FAILs) if no adapters are present.
 * -------------------------------------------------------------------------- */
static int TC_HW_001_AdapterEnumeration(HANDLE h)
{
    AVB_ENUM_REQUEST adapters[16];
    int count = EnumAdapters(h, adapters, 16);

    if (count == 0) {
        printf("  [SKIP] TC-HW-001: No Intel AVB adapters found — hardware not present\n");
        return TEST_SKIP;
    }

    for (int i = 0; i < count; i++) {
        if (adapters[i].vendor_id != 0x8086u) {
            printf("  [FAIL] TC-HW-001: Adapter[%d] vendor_id=0x%04X, expected 0x8086\n",
                   i, adapters[i].vendor_id);
            return TEST_FAIL;
        }
    }

    printf("  [PASS] TC-HW-001: Enumerated %d adapter(s), all vendor_id=0x8086\n", count);
    return TEST_PASS;
}

/* --------------------------------------------------------------------------
 * TC-HW-002: HW State Query
 *   - For each enumerated adapter, opens adapter via IOCTL_AVB_OPEN_ADAPTER.
 *   - Issues IOCTL_AVB_GET_HW_STATE.
 *   - Verifies hw_state >= AVB_HW_UNBOUND (not AVB_HW_TEARDOWN = -1).
 *   - Verifies capabilities has INTEL_CAP_BASIC_1588 set.
 * -------------------------------------------------------------------------- */
static int TC_HW_002_HwStateQuery(HANDLE h)
{
    AVB_ENUM_REQUEST adapters[16];
    int count = EnumAdapters(h, adapters, 16);

    if (count == 0) {
        printf("  [SKIP] TC-HW-002: No adapters — hardware not present\n");
        return TEST_SKIP;
    }

    for (int i = 0; i < count; i++) {
        /* Open per-adapter context */
        AVB_OPEN_REQUEST open_req;
        DWORD br = 0;
        ZeroMemory(&open_req, sizeof(open_req));
        open_req.vendor_id = adapters[i].vendor_id;
        open_req.device_id = adapters[i].device_id;
        open_req.index     = (avb_u32)i;

        if (!DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER,
                             &open_req, sizeof(open_req),
                             &open_req, sizeof(open_req), &br, NULL)
            || open_req.status != 0) {
            printf("  [FAIL] TC-HW-002: OPEN_ADAPTER[%d] failed (status=0x%08X, err=%lu)\n",
                   i, open_req.status, GetLastError());
            return TEST_FAIL;
        }

        /* Query HW state */
        AVB_HW_STATE_QUERY query;
        ZeroMemory(&query, sizeof(query));
        br = 0;
        if (!DeviceIoControl(h, IOCTL_AVB_GET_HW_STATE,
                             NULL, 0,
                             &query, sizeof(query), &br, NULL)) {
            printf("  [FAIL] TC-HW-002: GET_HW_STATE[%d] IOCTL failed (err=%lu)\n",
                   i, GetLastError());
            return TEST_FAIL;
        }

        /* hw_state must be >= AVB_HW_UNBOUND (i.e. not AVB_HW_TEARDOWN = -1) */
        if ((int)query.hw_state < (int)AVB_HW_UNBOUND) {
            printf("  [FAIL] TC-HW-002: Adapter[%d] hw_state=%d, expected >= AVB_HW_UNBOUND(%d)\n",
                   i, (int)query.hw_state, (int)AVB_HW_UNBOUND);
            return TEST_FAIL;
        }

        /* Every Intel AVB adapter must advertise BASIC_1588 */
        if (!(query.capabilities & INTEL_CAP_BASIC_1588)) {
            printf("  [FAIL] TC-HW-002: Adapter[%d] DID=0x%04X missing INTEL_CAP_BASIC_1588 (caps=0x%08X)\n",
                   i, adapters[i].device_id, query.capabilities);
            return TEST_FAIL;
        }

        printf("  [PASS] TC-HW-002: Adapter[%d] DID=0x%04X hw_state=%d caps=0x%08X\n",
               i, adapters[i].device_id, (int)query.hw_state, query.capabilities);
    }

    return TEST_PASS;
}

/* --------------------------------------------------------------------------
 * TC-HW-003: Capability Consistency per Device Family
 *   - I225 (0x15F3) and I226 (0x125C) MUST have INTEL_CAP_TSN_TAS.
 *   - I210 (0x1533/0x1536/0x1537/0x1538) must NOT have INTEL_CAP_TSN_TAS.
 *   - Adapters not in either family are SKIPped for this assertion.
 *   - SKIPs the test entirely if no adapters from known families are present.
 * -------------------------------------------------------------------------- */
static int TC_HW_003_CapabilityConsistency(HANDLE h)
{
    AVB_ENUM_REQUEST adapters[16];
    int count = EnumAdapters(h, adapters, 16);

    if (count == 0) {
        printf("  [SKIP] TC-HW-003: No adapters — hardware not present\n");
        return TEST_SKIP;
    }

    int checked = 0;

    for (int i = 0; i < count; i++) {
        /* Open per-adapter context */
        AVB_OPEN_REQUEST open_req;
        DWORD br = 0;
        ZeroMemory(&open_req, sizeof(open_req));
        open_req.vendor_id = adapters[i].vendor_id;
        open_req.device_id = adapters[i].device_id;
        open_req.index     = (avb_u32)i;

        if (!DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER,
                             &open_req, sizeof(open_req),
                             &open_req, sizeof(open_req), &br, NULL)
            || open_req.status != 0) {
            continue; /* best-effort; failure caught by TC-HW-002 */
        }

        AVB_HW_STATE_QUERY query;
        ZeroMemory(&query, sizeof(query));
        br = 0;
        if (!DeviceIoControl(h, IOCTL_AVB_GET_HW_STATE,
                             NULL, 0,
                             &query, sizeof(query), &br, NULL)) {
            continue;
        }

        avb_u16 did = adapters[i].device_id;
        avb_u32 caps = query.capabilities;

        /* I225/I226: TSN-capable — must have TAS */
        if (did == DID_I225_V || did == DID_I226_LM) {
            checked++;
            if (!(caps & INTEL_CAP_TSN_TAS)) {
                printf("  [FAIL] TC-HW-003: Adapter[%d] DID=0x%04X (TSN family) missing INTEL_CAP_TSN_TAS (caps=0x%08X)\n",
                       i, did, caps);
                return TEST_FAIL;
            }
            printf("  [PASS] TC-HW-003: Adapter[%d] DID=0x%04X (TSN) has INTEL_CAP_TSN_TAS\n", i, did);
        }

        /* I210: legacy — must NOT have TAS */
        if (did == DID_I210_COPPER || did == DID_I210_FIBER ||
            did == DID_I210_SGMII  || did == DID_I210_SERDES) {
            checked++;
            if (caps & INTEL_CAP_TSN_TAS) {
                printf("  [FAIL] TC-HW-003: Adapter[%d] DID=0x%04X (I210) unexpectedly has INTEL_CAP_TSN_TAS (caps=0x%08X)\n",
                       i, did, caps);
                return TEST_FAIL;
            }
            printf("  [PASS] TC-HW-003: Adapter[%d] DID=0x%04X (I210) correctly lacks INTEL_CAP_TSN_TAS\n", i, did);
        }
    }

    if (checked == 0) {
        printf("  [SKIP] TC-HW-003: No I210/I225/I226 adapters present — capability check skipped\n");
        return TEST_SKIP;
    }

    return TEST_PASS;
}

/* --------------------------------------------------------------------------
 * TC-HW-004: Concurrent Register Access Safety
 *   - Spawns 4 threads each issuing IOCTL_AVB_GET_HW_STATE 50 times.
 *   - Verifies no IOCTL errors occur under concurrent load.
 *   - Verifies all threads report the same hw_state value (consistency).
 * -------------------------------------------------------------------------- */
static DWORD WINAPI ConcurrentAccessThread(LPVOID param)
{
    ThreadArg *arg = (ThreadArg *)param;

    for (int i = 0; i < arg->iterations; i++) {
        AVB_HW_STATE_QUERY query;
        DWORD br = 0;
        ZeroMemory(&query, sizeof(query));

        if (!DeviceIoControl(arg->device, IOCTL_AVB_GET_HW_STATE,
                             NULL, 0,
                             &query, sizeof(query), &br, NULL)) {
            arg->errors++;
        } else {
            arg->last_hw_state     = (DWORD)query.hw_state;
            arg->last_capabilities = query.capabilities;
        }
    }
    return 0;
}

static int TC_HW_004_ConcurrentAccess(HANDLE h)
{
    /* Must have at least one adapter to run this test */
    AVB_ENUM_REQUEST adapters[16];
    int count = EnumAdapters(h, adapters, 16);
    if (count == 0) {
        printf("  [SKIP] TC-HW-004: No adapters — hardware not present\n");
        return TEST_SKIP;
    }

    /* Open adapter context before spawning threads */
    AVB_OPEN_REQUEST open_req;
    DWORD br = 0;
    ZeroMemory(&open_req, sizeof(open_req));
    open_req.vendor_id = adapters[0].vendor_id;
    open_req.device_id = adapters[0].device_id;
    open_req.index     = 0;
    if (!DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER,
                         &open_req, sizeof(open_req),
                         &open_req, sizeof(open_req), &br, NULL)
        || open_req.status != 0) {
        printf("  [FAIL] TC-HW-004: OPEN_ADAPTER failed (status=0x%08X)\n", open_req.status);
        return TEST_FAIL;
    }

#define NUM_THREADS  4
#define ITERATIONS  50

    ThreadArg args[NUM_THREADS];
    HANDLE    threads[NUM_THREADS];

    for (int t = 0; t < NUM_THREADS; t++) {
        args[t].device          = h;
        args[t].iterations      = ITERATIONS;
        args[t].errors          = 0;
        args[t].last_hw_state   = (DWORD)-1;
        args[t].last_capabilities = 0;
        threads[t] = CreateThread(NULL, 0, ConcurrentAccessThread, &args[t], 0, NULL);
        if (threads[t] == NULL) {
            printf("  [FAIL] TC-HW-004: CreateThread(%d) failed (err=%lu)\n", t, GetLastError());
            return TEST_FAIL;
        }
    }

    WaitForMultipleObjects(NUM_THREADS, threads, TRUE, 10000 /* 10s timeout */);

    int total_errors = 0;
    for (int t = 0; t < NUM_THREADS; t++) {
        total_errors += args[t].errors;
        CloseHandle(threads[t]);
    }

    if (total_errors > 0) {
        printf("  [FAIL] TC-HW-004: %d IOCTL error(s) under concurrent load (%d threads × %d iterations)\n",
               total_errors, NUM_THREADS, ITERATIONS);
        return TEST_FAIL;
    }

    /* Verify all threads observed the same hw_state (spin lock ensures consistency) */
    DWORD ref_state = args[0].last_hw_state;
    for (int t = 1; t < NUM_THREADS; t++) {
        if (args[t].last_hw_state != ref_state) {
            printf("  [FAIL] TC-HW-004: Inconsistent hw_state across threads "
                   "(thread 0=%lu, thread %d=%lu)\n",
                   ref_state, t, args[t].last_hw_state);
            return TEST_FAIL;
        }
    }

    printf("  [PASS] TC-HW-004: %d threads × %d iterations, 0 errors, hw_state=0x%08lX consistent\n",
           NUM_THREADS, ITERATIONS, ref_state);

#undef NUM_THREADS
#undef ITERATIONS

    return TEST_PASS;
}

/* --------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------- */
int main(void)
{
    printf("=== test_hardware_detection ===\n");
    printf("Verifies: #44 (REQ-F-HW-DETECT-001), #45 (REQ-F-REG-ACCESS-001)\n");
    printf("Implements: #288 (TEST-HW-DETECT-CAPS-001), #287 (TEST-REG-ACCESS-LOCK-001)\n\n");

    /* Open device — SKIP all tests if driver not loaded */
    HANDLE h = OpenDevice();
    if (h == INVALID_HANDLE_VALUE) {
        printf("SKIP: Driver not loaded (CreateFile error=%lu)\n", GetLastError());
        printf("=== RESULT: 0 passed, 0 failed, 4 skipped ===\n");
        return 0;
    }

    int pass = 0, fail = 0, skip = 0;

    struct { const char *name; int (*fn)(HANDLE); } tests[] = {
        { "TC-HW-001 Adapter Enumeration",         TC_HW_001_AdapterEnumeration  },
        { "TC-HW-002 HW State Query",              TC_HW_002_HwStateQuery        },
        { "TC-HW-003 Capability Consistency",      TC_HW_003_CapabilityConsistency },
        { "TC-HW-004 Concurrent Access Safety",    TC_HW_004_ConcurrentAccess    },
    };

    for (int i = 0; i < (int)(sizeof(tests) / sizeof(tests[0])); i++) {
        printf("[%s]\n", tests[i].name);
        int r = tests[i].fn(h);
        if      (r == TEST_PASS) pass++;
        else if (r == TEST_FAIL) fail++;
        else                     skip++;
        printf("\n");
    }

    CloseHandle(h);

    printf("=== RESULT: %d passed, %d failed, %d skipped ===\n", pass, fail, skip);
    return (fail > 0) ? 1 : 0;
}
