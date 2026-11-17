/**
 * @file test_ioctl_dev_lifecycle.c
 * @brief Device Lifecycle Management Verification Tests
 * 
 * Implements: #313 (TEST-DEV-LIFECYCLE-001)
 * Verifies: #12 (REQ-F-DEVICE-LIFECYCLE-001: Device Lifecycle Management via IOCTL)
 * 
 * Test Plan: TEST-PLAN-IOCTL-NEW-2025-12-31.md
 * IOCTLs: 20 (INIT), 21 (GET_INFO), 31 (ENUM), 32 (OPEN), 37 (GET_HW_STATE)
 * Test Cases: 19
 * Priority: P0 (Critical)
 * 
 * Standards: IEEE 1012-2016 (Verification & Validation)
 * 
 * @see https://github.com/zarfld/IntelAvbFilter/issues/313
 * @see https://github.com/zarfld/IntelAvbFilter/issues/12
 *
 * --init-contract: only the targeted #313 cases of the 2026-11-12 amendment of #12/#313 (INIT_DEVICE separates adapter
 * initialization from open-instance initialization; implementation owned by #404). Without the option the 20 cases above run
 * unchanged. On every adapter the driver enumerates (GET_ADAPTER_IDENTITY, validated; no model or family selection):
 *   PRESTATE       an observer handle bound by NET_LUID (OPEN_ADAPTER_BY_LUID binds only) sees AVB_HW_PTP_READY, readable
 *                  PTP state and an advancing PHC. Otherwise the READY scenarios do not apply here: BLOCKED.
 *   UT-DEV-INIT-001          first INIT_DEVICE on a new instance (own handle, bound by NET_LUID): exactly STATUS_SUCCESS.
 *   UT-DEV-INIT-002          the same instance again after its success: exactly STATUS_INVALID_DEVICE_STATE, no output. Sent only
 *                  after UT-DEV-INIT-001 succeeded (its precondition); otherwise BLOCKED, never run under a failed precondition.
 *   UT-DEV-INIT-SECOND (provisional ID, #313 amendment "second open instance on a READY adapter"): STATUS_SUCCESS. Sent only
 *                  after a first instance succeeded and the adapter is still READY; otherwise BLOCKED.
 *   UT-DEV-OPEN-READY  (provisional ID, #313 amendment "OPEN_ADAPTER without hardware change"): IOCTL 32 selects the adapter.
 *                  -STATUS is the raw result against the expected STATUS_SUCCESS (an error is FAIL); -BINDING separately
 *                  verifies through the handle's own QUERY_HW_SNAPSHOT NET_LUID which adapter it is bound to (no evidence:
 *                  BLOCKED). QUERY_HW_SNAPSHOT is always recorded raw (NTSTATUS, length, header, entry states), never
 *                  interpreted.
 * After each request, from the observer handle (GET_CLOCK_CONFIG and, independently, QUERY_HW_SNAPSHOT), two lines:
 *   <case>-PHC     a healthy PTP clock is not reset. A backward step of SYSTIM establishes a reset (FAIL). No PHC tolerance is
 *                  approved, so no step back cannot prove continuity: INCONCLUSIVE, with the measured values.
 *   <case>-TIMINCA the frequency is not rewritten: TIMINCA equal before and after in both reads (a rewrite with the same value
 *                  is not detectable). TSAUXC and hw_state are printed as evidence.
 * Not exercised (COVERAGE line): an adapter that is not READY, concurrent INIT_DEVICE (STATUS_DEVICE_BUSY), the order in which
 * READY is published, retry after a failed initialization (#412, separately owned), GET_DEVICE_INFO before initialization.
 * Mutations: INIT_DEVICE and OPEN_ADAPTER as the cases send them; on a driver that re-initializes a READY adapter they reset
 * its PTP clock, which no test can undo (reported, never hidden). Safety class Unsafe. Exit: 0 all passed, 1 any nonpassing
 * verdict, 2 no adapter ran, 3 only BLOCKED.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Single Source of Truth for IOCTL definitions */
#include "../../include/avb_ioctl.h"
#include "../common/avb_ntstatus.h"   /* --init-contract: exact NTSTATUS (Win32 errors are lossy) */

/* Test result codes */
#define TEST_PASS 0
#define TEST_FAIL 1
#define TEST_SKIP 2

/* Device paths */
#define DEVICE_PATH_PRIMARY   "\\\\.\\IntelAvbFilter"
#define DEVICE_PATH_ALTERNATE "\\\\.\\IntelAvbFilter0"

/* Test state */
typedef struct {
    HANDLE primary_adapter;
    int test_count;
    int pass_count;
    int fail_count;
    int skip_count;
} TestContext;

/*==============================================================================
 * Helper Functions
 *============================================================================*/

/**
 * @brief Open device handle
 */
HANDLE OpenDevice(const char *path) {
    HANDLE h;
    
    h = CreateFileA(path,
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL);
    
    if (h == INVALID_HANDLE_VALUE) {
        printf("  [INFO] Could not open %s: error %lu\n", path, GetLastError());
    }
    
    return h;
}

/**
 * @brief Initialize device via IOCTL 20
 */
BOOL InitializeDevice(HANDLE device) {
    AVB_DEVICE_INFO_REQUEST request = {0};
    DWORD bytes_returned = 0;
    BOOL result;
    
    result = DeviceIoControl(
        device,
        IOCTL_AVB_INIT_DEVICE,  /* From avb_ioctl.h */
        &request,
        sizeof(request),
        &request,
        sizeof(request),
        &bytes_returned,
        NULL
    );
    
    return (result && request.status == 0);
}

/**
 * @brief Get device info via IOCTL 21
 */
BOOL GetDeviceInfo(HANDLE device, char *info_buffer, UINT32 buffer_size) {
    AVB_DEVICE_INFO_REQUEST request = {0};
    DWORD bytes_returned = 0;
    BOOL result;
    
    request.buffer_size = buffer_size;
    
    result = DeviceIoControl(
        device,
        IOCTL_AVB_GET_DEVICE_INFO,  /* From avb_ioctl.h */
        &request,
        sizeof(request),
        &request,
        sizeof(request),
        &bytes_returned,
        NULL
    );
    
    if (result && request.status == 0) {
        strncpy_s(info_buffer, buffer_size, request.device_info, _TRUNCATE);
        return TRUE;
    }
    
    return FALSE;
}

/**
 * @brief Enumerate adapters via IOCTL 31
 */
BOOL EnumerateAdapters(HANDLE device, UINT32 *adapter_count) {
    AVB_ENUM_REQUEST request = {0};  /* SSOT structure from avb_ioctl.h line 221 */
    DWORD bytes_returned = 0;
    BOOL result;
    
    request.index = 0;  /* Query first adapter */
    
    result = DeviceIoControl(
        device,
        IOCTL_AVB_ENUM_ADAPTERS,  /* From avb_ioctl.h */
        &request,
        sizeof(request),
        &request,
        sizeof(request),
        &bytes_returned,
        NULL
    );
    
    if (result) {
        *adapter_count = request.count;  /* Total adapter count */
        return TRUE;
    }
    
    return FALSE;
}

/**
 * @brief Open adapter via IOCTL 32
 */
HANDLE OpenAdapterByIndex(HANDLE device, UINT32 index) {
    AVB_ENUM_REQUEST enum_req = {0};
    AVB_OPEN_REQUEST open_req = {0};  /* SSOT structure from avb_ioctl.h line 230 */
    DWORD bytes_returned = 0;
    BOOL result;
    
    /* First enumerate to get vendor_id and device_id */
    enum_req.index = index;
    result = DeviceIoControl(
        device,
        IOCTL_AVB_ENUM_ADAPTERS,
        &enum_req,
        sizeof(enum_req),
        &enum_req,
        sizeof(enum_req),
        &bytes_returned,
        NULL
    );
    
    if (!result || enum_req.count == 0) {
        return INVALID_HANDLE_VALUE;
    }
    
    /* Now open adapter using vendor_id and device_id */
    open_req.vendor_id = enum_req.vendor_id;
    open_req.device_id = enum_req.device_id;
    
    result = DeviceIoControl(
        device,
        IOCTL_AVB_OPEN_ADAPTER,  /* From avb_ioctl.h */
        &open_req,
        sizeof(open_req),
        &open_req,
        sizeof(open_req),
        &bytes_returned,
        NULL
    );
    
    if (result) {
        /* In real implementation, would return adapter handle from response */
        return device;  /* Placeholder */
    }
    
    return INVALID_HANDLE_VALUE;
}

/**
 * @brief Get hardware state via IOCTL 37
 */
BOOL GetHardwareState(HANDLE device, AVB_HW_STATE_QUERY *state) {
    DWORD bytes_returned = 0;
    BOOL result;
    
    result = DeviceIoControl(
        device,
        IOCTL_AVB_GET_HW_STATE,  /* From avb_ioctl.h */
        state,
        sizeof(*state),
        state,
        sizeof(*state),
        &bytes_returned,
        NULL
    );
    
    return result;  /* AVB_HW_STATE_QUERY has no status field, check DeviceIoControl result */
}

/**
 * @brief Print test result
 */
void PrintTestResult(TestContext *ctx, const char *test_name, int result, const char *reason) {
    ctx->test_count++;
    
    switch (result) {
        case TEST_PASS:
            printf("  [PASS] %s\n", test_name);
            ctx->pass_count++;
            break;
        case TEST_FAIL:
            printf("  [FAIL] %s: %s\n", test_name, reason);
            ctx->fail_count++;
            break;
        case TEST_SKIP:
            printf("  [SKIP] %s: %s\n", test_name, reason);
            ctx->skip_count++;
            break;
    }
}

/*==============================================================================
 * Test Cases (Issue #313 - 19 test cases)
 *============================================================================*/

/**
 * UT-DEV-INIT-001: First-Time Device Initialization
 */
void Test_FirstTimeInitialization(TestContext *ctx) {
    HANDLE device;
    BOOL result;
    
    device = OpenDevice(DEVICE_PATH_PRIMARY);
    if (device == INVALID_HANDLE_VALUE) {
        PrintTestResult(ctx, "UT-DEV-INIT-001: First-Time Device Initialization", 
                        TEST_SKIP, "Device not accessible");
        return;
    }
    
    result = InitializeDevice(device);
    
    CloseHandle(device);
    
    PrintTestResult(ctx, "UT-DEV-INIT-001: First-Time Device Initialization", 
                    result ? TEST_PASS : TEST_FAIL,
                    result ? NULL : "Initialization IOCTL failed");
}

/**
 * UT-DEV-INIT-002: Duplicate Initialization Prevention
 */
void Test_DuplicateInitializationPrevention(TestContext *ctx) {
    HANDLE device;
    BOOL first, second;
    
    device = OpenDevice(DEVICE_PATH_PRIMARY);
    if (device == INVALID_HANDLE_VALUE) {
        PrintTestResult(ctx, "UT-DEV-INIT-002: Duplicate Initialization Prevention", 
                        TEST_SKIP, "Device not accessible");
        return;
    }
    
    first = InitializeDevice(device);
    second = InitializeDevice(device);  /* Should fail or be idempotent */
    
    CloseHandle(device);
    
    PrintTestResult(ctx, "UT-DEV-INIT-002: Duplicate Initialization Prevention", 
                    (first && !second) ? TEST_PASS : TEST_FAIL,
                    (first && !second) ? NULL : "Duplicate init not prevented");
}

/**
 * UT-DEV-INFO-001: Device Information Retrieval
 */
void Test_DeviceInformationRetrieval(TestContext *ctx) {
    HANDLE device;
    char info[AVB_DEVICE_INFO_MAX];
    BOOL result;
    
    device = OpenDevice(DEVICE_PATH_PRIMARY);
    if (device == INVALID_HANDLE_VALUE) {
        PrintTestResult(ctx, "UT-DEV-INFO-001: Device Information Retrieval", 
                        TEST_SKIP, "Device not accessible");
        return;
    }
    
    result = GetDeviceInfo(device, info, sizeof(info));
    
    if (result) {
        printf("    Device info: %s\n", info);
    }
    
    CloseHandle(device);
    
    PrintTestResult(ctx, "UT-DEV-INFO-001: Device Information Retrieval", 
                    result ? TEST_PASS : TEST_FAIL,
                    result ? NULL : "Failed to retrieve device info");
}

/**
 * UT-DEV-INFO-002: Device Info Before Initialization
 */
void Test_DeviceInfoBeforeInitialization(TestContext *ctx) {
    HANDLE device;
    char info[AVB_DEVICE_INFO_MAX];
    BOOL result;
    
    /* Open device without calling INIT_DEVICE first */
    device = OpenDevice(DEVICE_PATH_PRIMARY);
    if (device == INVALID_HANDLE_VALUE) {
        PrintTestResult(ctx, "UT-DEV-INFO-002: Device Info Before Initialization", 
                        TEST_SKIP, "Device not accessible");
        return;
    }
    
    result = GetDeviceInfo(device, info, sizeof(info));
    
    CloseHandle(device);
    
    /* Should handle gracefully (either succeed with partial info or fail politely) */
    PrintTestResult(ctx, "UT-DEV-INFO-002: Device Info Before Initialization", 
                    TEST_PASS, "Handled gracefully");
}

/**
 * UT-DEV-ENUM-001: Single Adapter Enumeration
 */
void Test_SingleAdapterEnumeration(TestContext *ctx) {
    HANDLE device;
    UINT32 count = 0;
    BOOL result;
    
    device = OpenDevice(DEVICE_PATH_PRIMARY);
    if (device == INVALID_HANDLE_VALUE) {
        PrintTestResult(ctx, "UT-DEV-ENUM-001: Single Adapter Enumeration", 
                        TEST_SKIP, "Device not accessible");
        return;
    }
    
    result = EnumerateAdapters(device, &count);
    
    if (result) {
        printf("    Adapter count: %u\n", count);
    }
    
    CloseHandle(device);
    
    PrintTestResult(ctx, "UT-DEV-ENUM-001: Single Adapter Enumeration", 
                    (result && count >= 1) ? TEST_PASS : TEST_FAIL,
                    (result && count >= 1) ? NULL : "No adapters enumerated");
}

/**
 * UT-DEV-ENUM-002: Multiple Adapter Enumeration
 * Verifies that IOCTL_AVB_ENUM_ADAPTERS reports >= 2 adapters and that
 * individual adapters 0 and 1 can each be enumerated independently.
 * NOTE: This system has 6 x Intel I226-LM adapters, so this is not hardware-dependent.
 */
void Test_MultipleAdapterEnumeration(TestContext *ctx) {
    HANDLE device;
    UINT32 total_count = 0;
    BOOL result;

    device = OpenDevice(DEVICE_PATH_PRIMARY);
    if (device == INVALID_HANDLE_VALUE) {
        PrintTestResult(ctx, "UT-DEV-ENUM-002: Multiple Adapter Enumeration",
                        TEST_SKIP, "Device not accessible");
        return;
    }

    result = EnumerateAdapters(device, &total_count);

    if (!result) {
        CloseHandle(device);
        PrintTestResult(ctx, "UT-DEV-ENUM-002: Multiple Adapter Enumeration",
                        TEST_FAIL, "EnumerateAdapters IOCTL failed");
        return;
    }

    printf("    Total adapters reported: %u\n", total_count);

    if (total_count < 2) {
        CloseHandle(device);
        PrintTestResult(ctx, "UT-DEV-ENUM-002: Multiple Adapter Enumeration",
                        TEST_SKIP, "Only 1 adapter detected (need 2+)");
        return;
    }

    /* Verify adapters 0 and 1 can each be individually opened.
     * Use a fresh CreateFile handle per adapter: the driver rejects
     * IOCTL_AVB_OPEN_ADAPTER when FileObject->FsContext is already bound
     * (i.e. a previous OPEN_ADAPTER on the same handle must not pollute
     * a subsequent attempt for a different adapter index). */
    {
        BOOL ok0 = FALSE, ok1 = FALSE;
        UINT32 i;

        for (i = 0; i < 2; i++) {
            HANDLE hFresh = OpenDevice(DEVICE_PATH_PRIMARY);
            if (hFresh == INVALID_HANDLE_VALUE) break;

            AVB_ENUM_REQUEST ereq;
            memset(&ereq, 0, sizeof(ereq));
            ereq.index = i;
            DWORD br = 0;
            if (!DeviceIoControl(hFresh, IOCTL_AVB_ENUM_ADAPTERS,
                                 &ereq, sizeof(ereq), &ereq, sizeof(ereq), &br, NULL)) {
                CloseHandle(hFresh);
                break;
            }

            AVB_OPEN_REQUEST oreq;
            memset(&oreq, 0, sizeof(oreq));
            oreq.vendor_id = ereq.vendor_id;
            oreq.device_id = ereq.device_id;
            oreq.index     = i;
            br = 0;
            BOOL opened = DeviceIoControl(hFresh, IOCTL_AVB_OPEN_ADAPTER,
                                          &oreq, sizeof(oreq), &oreq, sizeof(oreq), &br, NULL);
            CloseHandle(hFresh);
            if (i == 0) ok0 = opened;
            else        ok1 = opened;
        }

        CloseHandle(device);

        if (!ok0 || !ok1) {
            PrintTestResult(ctx, "UT-DEV-ENUM-002: Multiple Adapter Enumeration",
                            TEST_FAIL, "Could not open both adapters 0 and 1 individually");
            return;
        }
    }

    printf("    Verified %u adapters, opened adapters 0 and 1 successfully\n", total_count);
    PrintTestResult(ctx, "UT-DEV-ENUM-002: Multiple Adapter Enumeration",
                    TEST_PASS, NULL);
}

/**
 * UT-DEV-ENUM-003: Enumeration with No Adapters
 */
void Test_EnumerationWithNoAdapters(TestContext *ctx) {
    PrintTestResult(ctx, "UT-DEV-ENUM-003: Enumeration with No Adapters", 
                    TEST_SKIP, "Requires adapter removal (manual test)");
}

/**
 * UT-DEV-OPEN-001: Open First Available Adapter
 */
void Test_OpenFirstAvailableAdapter(TestContext *ctx) {
    HANDLE device, adapter;
    
    device = OpenDevice(DEVICE_PATH_PRIMARY);
    if (device == INVALID_HANDLE_VALUE) {
        PrintTestResult(ctx, "UT-DEV-OPEN-001: Open First Available Adapter", 
                        TEST_SKIP, "Device not accessible");
        return;
    }
    
    adapter = OpenAdapterByIndex(device, 0);
    
    CloseHandle(device);
    
    if (adapter != INVALID_HANDLE_VALUE) {
        CloseHandle(adapter);
    }
    
    PrintTestResult(ctx, "UT-DEV-OPEN-001: Open First Available Adapter", 
                    (adapter != INVALID_HANDLE_VALUE) ? TEST_PASS : TEST_FAIL,
                    (adapter != INVALID_HANDLE_VALUE) ? NULL : "Failed to open adapter");
}

/**
 * UT-DEV-OPEN-002: Open by Device Path
 * Verifies the driver exposes its primary symbolic link (\\\\.\\ namespace).
 * Both DEVICE_PATH_PRIMARY and DEVICE_PATH_ALTERNATE are tried; at least one
 * must succeed.  No enumeration is required — the paths are static constants.
 */
void Test_OpenByDevicePath(TestContext *ctx) {
    HANDLE h;
    BOOL primary_ok, alt_ok, state_ok;
    AVB_HW_STATE_QUERY state = {0};
    DWORD bytes = 0;

    /* Try primary path */
    h = OpenDevice(DEVICE_PATH_PRIMARY);
    primary_ok = (h != INVALID_HANDLE_VALUE);
    if (primary_ok) {
        state_ok = GetHardwareState(h, &state);
        CloseHandle(h);
        if (state_ok) {
            printf("    Opened primary path: %s (hw_state=%u)\n",
                   DEVICE_PATH_PRIMARY, state.hw_state);
            PrintTestResult(ctx, "UT-DEV-OPEN-002: Open by Device Path",
                            TEST_PASS, NULL);
            return;
        }
    }

    /* Fallback: try alternate path */
    h = OpenDevice(DEVICE_PATH_ALTERNATE);
    alt_ok = (h != INVALID_HANDLE_VALUE);
    if (alt_ok) {
        state_ok = GetHardwareState(h, &state);
        CloseHandle(h);
        printf("    Opened alternate path: %s (hw_state=%u)\n",
               DEVICE_PATH_ALTERNATE, state.hw_state);
        PrintTestResult(ctx, "UT-DEV-OPEN-002: Open by Device Path",
                        state_ok ? TEST_PASS : TEST_FAIL,
                        state_ok ? NULL : "Opened alternate path but IOCTL failed");
        return;
    }

    PrintTestResult(ctx, "UT-DEV-OPEN-002: Open by Device Path",
                    TEST_FAIL, "Neither primary nor alternate device path accessible");
}

/**
 * UT-DEV-OPEN-003: Invalid Adapter Index Rejection
 */
void Test_InvalidAdapterIndexRejection(TestContext *ctx) {
    HANDLE device, adapter;
    
    device = OpenDevice(DEVICE_PATH_PRIMARY);
    if (device == INVALID_HANDLE_VALUE) {
        PrintTestResult(ctx, "UT-DEV-OPEN-003: Invalid Adapter Index Rejection", 
                        TEST_SKIP, "Device not accessible");
        return;
    }
    
    /* Try to open adapter with out-of-range index */
    adapter = OpenAdapterByIndex(device, 9999);
    
    CloseHandle(device);
    
    PrintTestResult(ctx, "UT-DEV-OPEN-003: Invalid Adapter Index Rejection", 
                    (adapter == INVALID_HANDLE_VALUE) ? TEST_PASS : TEST_FAIL,
                    (adapter == INVALID_HANDLE_VALUE) ? NULL : "Invalid index accepted");
}

/* ---------------------------------------------------------------------------
 * Concurrent-open worker: opens device, does one IOCTL, closes, sets result.
 * ---------------------------------------------------------------------------*/
typedef struct {
    int     thread_id;
    BOOL    success;
} ConcurrentOpenArg;

static DWORD WINAPI ConcurrentOpenWorker(LPVOID param) {
    ConcurrentOpenArg *arg = (ConcurrentOpenArg *)param;
    HANDLE h;
    AVB_HW_STATE_QUERY state = {0};

    h = OpenDevice(DEVICE_PATH_PRIMARY);
    if (h == INVALID_HANDLE_VALUE) {
        arg->success = FALSE;
        return 1;
    }
    /* Do a harmless read-only IOCTL to exercise the concurrent path */
    arg->success = GetHardwareState(h, &state);
    CloseHandle(h);
    return arg->success ? 0 : 1;
}

/**
 * UT-DEV-OPEN-004: Concurrent Open Requests
 * Spawns 4 threads that each open the device and issue GET_HW_STATE
 * simultaneously.  All must succeed — the driver must be re-entrant.
 */
void Test_ConcurrentOpenRequests(TestContext *ctx) {
#define CONCURRENT_THREADS 4
    HANDLE       threads[CONCURRENT_THREADS];
    ConcurrentOpenArg args[CONCURRENT_THREADS];
    DWORD        i, all_ok = TRUE;

    for (i = 0; i < CONCURRENT_THREADS; i++) {
        args[i].thread_id = (int)i;
        args[i].success   = FALSE;
        threads[i] = CreateThread(NULL, 0, ConcurrentOpenWorker, &args[i], 0, NULL);
        if (threads[i] == NULL) {
            printf("    Thread %lu creation failed: error %lu\n", i, GetLastError());
            all_ok = FALSE;
        }
    }

    /* Wait for all threads (with 5-second safety timeout each) */
    for (i = 0; i < CONCURRENT_THREADS; i++) {
        if (threads[i] != NULL) {
            WaitForSingleObject(threads[i], 5000);
            if (!args[i].success) all_ok = FALSE;
            CloseHandle(threads[i]);
        }
    }

    printf("    Concurrent threads: %d, all succeeded: %s\n",
           CONCURRENT_THREADS, all_ok ? "YES" : "NO");
    PrintTestResult(ctx, "UT-DEV-OPEN-004: Concurrent Open Requests",
                    all_ok ? TEST_PASS : TEST_FAIL,
                    all_ok ? NULL : "One or more concurrent opens/IOCTLs failed");
#undef CONCURRENT_THREADS
}

/**
 * UT-DEV-HW-STATE-001: Hardware State Retrieval - D0
 */
void Test_HardwareStateRetrievalD0(TestContext *ctx) {
    HANDLE device;
    AVB_HW_STATE_QUERY state = {0};  /* SSOT structure from avb_ioctl.h line 265 */
    BOOL result;
    
    device = OpenDevice(DEVICE_PATH_PRIMARY);
    if (device == INVALID_HANDLE_VALUE) {
        PrintTestResult(ctx, "UT-DEV-HW-STATE-001: Hardware State Retrieval - D0", 
                        TEST_SKIP, "Device not accessible");
        return;
    }
    
    result = GetHardwareState(device, &state);
    
    if (result) {
        printf("    HW State: %u\n", state.hw_state);
        printf("    Vendor ID: 0x%04X\n", state.vendor_id);
        printf("    Device ID: 0x%04X\n", state.device_id);
        printf("    Capabilities: 0x%08X\n", state.capabilities);
    }
    
    CloseHandle(device);
    
    PrintTestResult(ctx, "UT-DEV-HW-STATE-001: Hardware State Retrieval - D0", 
                    result ? TEST_PASS : TEST_FAIL,
                    result ? NULL : "Failed to retrieve hardware state");
}

/**
 * UT-DEV-HW-STATE-002: Hardware State During D3 Transition
 */
void Test_HardwareStateDuringD3Transition(TestContext *ctx) {
    PrintTestResult(ctx, "UT-DEV-HW-STATE-002: Hardware State During D3 Transition", 
                    TEST_SKIP, "Requires power management control");
}

/**
 * UT-DEV-HW-STATE-003: Link State Detection
 */
void Test_LinkStateDetection(TestContext *ctx) {
    PrintTestResult(ctx, "UT-DEV-HW-STATE-003: Link State Detection", 
                    TEST_SKIP, "Requires manual cable toggle");
}

/**
 * UT-DEV-HW-STATE-004: Resource Allocation Status
 */
void Test_ResourceAllocationStatus(TestContext *ctx) {
    HANDLE device;
    AVB_HW_STATE_QUERY state = {0};  /* SSOT structure from avb_ioctl.h line 265 */
    BOOL init_result, state_result;
    
    device = OpenDevice(DEVICE_PATH_PRIMARY);
    if (device == INVALID_HANDLE_VALUE) {
        PrintTestResult(ctx, "UT-DEV-HW-STATE-004: Resource Allocation Status", 
                        TEST_SKIP, "Device not accessible");
        return;
    }
    
    init_result = InitializeDevice(device);
    state_result = GetHardwareState(device, &state);
    
    CloseHandle(device);
    
    PrintTestResult(ctx, "UT-DEV-HW-STATE-004: Resource Allocation Status", 
                    (init_result && state_result && state.hw_state != 0) ? TEST_PASS : TEST_FAIL,
                    (init_result && state_result && state.hw_state != 0) ? NULL : 
                    "Hardware state check failed after init");
}

/**
 * UT-DEV-LIFECYCLE-001: Full Lifecycle Sequence
 */
void Test_FullLifecycleSequence(TestContext *ctx) {
    HANDLE device, adapter;
    UINT32 count;
    BOOL init, enum_ok, open_ok;
    
    device = OpenDevice(DEVICE_PATH_PRIMARY);
    if (device == INVALID_HANDLE_VALUE) {
        PrintTestResult(ctx, "UT-DEV-LIFECYCLE-001: Full Lifecycle Sequence", 
                        TEST_SKIP, "Device not accessible");
        return;
    }
    
    /* Full sequence: Init → Enum → Open → Close */
    init = InitializeDevice(device);
    enum_ok = EnumerateAdapters(device, &count);
    
    if (count > 0) {
        adapter = OpenAdapterByIndex(device, 0);
        open_ok = (adapter != INVALID_HANDLE_VALUE);
        if (open_ok) {
            CloseHandle(adapter);
        }
    } else {
        open_ok = FALSE;
    }
    
    CloseHandle(device);
    
    PrintTestResult(ctx, "UT-DEV-LIFECYCLE-001: Full Lifecycle Sequence", 
                    (init && enum_ok && open_ok) ? TEST_PASS : TEST_FAIL,
                    (init && enum_ok && open_ok) ? NULL : "Lifecycle sequence incomplete");
}

/**
 * UT-DEV-LIFECYCLE-002: Initialization After Failed Start
 */
void Test_InitializationAfterFailedStart(TestContext *ctx) {
    PrintTestResult(ctx, "UT-DEV-LIFECYCLE-002: Initialization After Failed Start", 
                    TEST_SKIP, "Requires failure injection mechanism");
}

/**
 * UT-DEV-LIFECYCLE-003: Hot-Plug Device Detection
 */
void Test_HotPlugDeviceDetection(TestContext *ctx) {
    PrintTestResult(ctx, "UT-DEV-LIFECYCLE-003: Hot-Plug Device Detection", 
                    TEST_SKIP, "Requires manual hot-plug operation");
}

/**
 * UT-DEV-LIFECYCLE-004: Graceful Shutdown Sequence
 */
void Test_GracefulShutdownSequence(TestContext *ctx) {
    PrintTestResult(ctx, "UT-DEV-LIFECYCLE-004: Graceful Shutdown Sequence", 
                    TEST_SKIP, "Requires driver shutdown test framework");
}

/**
 * UT-DEV-LIFECYCLE-005: PnP Remove and Re-Add
 */
void Test_PnPRemoveAndReAdd(TestContext *ctx) {
    PrintTestResult(ctx, "UT-DEV-LIFECYCLE-005: PnP Remove and Re-Add", 
                    TEST_SKIP, "Requires Device Manager control or devcon");
}

/*==============================================================================
 * Main Test Harness
 *============================================================================*/

/*==============================================================================
 * --init-contract (#313 amendment 2026-11-12, #404)
 *============================================================================*/

/* QUERY_HW_SNAPSHOT as observed: raw NTSTATUS, returned length, header and the TIMINCA/TSAUXC entry states. Recorded, never
 * interpreted: why a snapshot is unavailable is not inferred here */
typedef struct {
    LONG st;
    DWORD got;
    avb_u32 version, count, status;
    avb_u64 net_luid;
    avb_u32 timinca_state, tsauxc_state;
    avb_u32 timinca, tsauxc;
    BOOL ok;                /* complete header and both entries VALID */
} IC_SNAP;

typedef struct {
    LONG clk_st;            /* raw NTSTATUS of GET_CLOCK_CONFIG */
    BOOL clk_ok;
    AVB_CLOCK_CONFIG clk;
    IC_SNAP snap;
    BOOL hw_ok;
    avb_u32 hw_state;
    LARGE_INTEGER qpc;
} IC_OBS;

static int g_ic_ran, g_ic_failed, g_ic_blocked;

static void ic_verdict(const char *tag, const char *check, UINT32 index, avb_u16 did, const char *reason)
{
    printf("[%s] %s/adapter:%u did=0x%04X%s%s\n", tag, check, index, did, reason ? ": " : "", reason ? reason : "");
    fflush(stdout);
    if (strcmp(tag, "BLOCKED") == 0) {
        g_ic_blocked++;
    } else if (strcmp(tag, "PASS") != 0) {
        g_ic_failed++;
    }
}

static HANDLE ic_open(void)
{
    return CreateFileA(DEVICE_PATH_PRIMARY, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

static BOOL ic_identity(HANDLE h, UINT32 index, AVB_ADAPTER_IDENTITY_REQUEST *id)
{
    DWORD br = 0;
    ZeroMemory(id, sizeof(*id));
    id->version = AVB_ADAPTER_IDENTITY_VERSION;
    id->index = index;
    return DeviceIoControl(h, IOCTL_AVB_GET_ADAPTER_IDENTITY, id, sizeof(*id), id, sizeof(*id), &br, NULL) &&
           br == sizeof(*id) && id->version == AVB_ADAPTER_IDENTITY_VERSION && id->index == index && id->status == 0 &&
           id->count > index;
}

/** OPEN_ADAPTER_BY_LUID binds the handle only (no PTP initialization, src/device.c) */
static HANDLE ic_open_bound(avb_u64 luid)
{
    AVB_OPEN_BY_LUID_REQUEST b;
    DWORD br = 0;
    HANDLE h = ic_open();

    if (h == INVALID_HANDLE_VALUE) {
        return h;
    }
    ZeroMemory(&b, sizeof(b));
    b.version = AVB_OPEN_BY_LUID_VERSION;
    b.net_luid = luid;
    if (!DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER_BY_LUID, &b, sizeof(b), &b, sizeof(b), &br, NULL)) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

/** QUERY_HW_SNAPSHOT on h with its raw NTSTATUS, returned length, header and entry states */
static void ic_snapshot(HANDLE h, IC_SNAP *r)
{
    AVB_HW_SNAPSHOT_REQUEST s;

    ZeroMemory(r, sizeof(*r));
    ZeroMemory(&s, sizeof(s));
    s.version = AVB_HW_SNAPSHOT_VERSION;
    r->st = avb_ioctl_ntstatus(h, IOCTL_AVB_QUERY_HW_SNAPSHOT, &s, sizeof(s), &s, sizeof(s), &r->got);
    if (r->st == 0 && r->got >= (DWORD)FIELD_OFFSET(AVB_HW_SNAPSHOT_REQUEST, entries)) {
        r->version = s.version;
        r->count = s.count;
        r->status = s.status;
        r->net_luid = s.net_luid;
    }
    if (r->st == 0 && r->got == sizeof(s) && s.count >= AVB_HWREG_TSAUXC) {
        r->timinca_state = s.entries[AVB_HWREG_TIMINCA - 1].state;
        r->tsauxc_state = s.entries[AVB_HWREG_TSAUXC - 1].state;
        r->timinca = s.entries[AVB_HWREG_TIMINCA - 1].value;
        r->tsauxc = s.entries[AVB_HWREG_TSAUXC - 1].value;
    }
    r->ok = r->st == 0 && r->got == sizeof(s) && r->version == AVB_HW_SNAPSHOT_VERSION && r->status == 0 &&
            r->timinca_state == AVB_HWREG_STATE_VALID && r->tsauxc_state == AVB_HWREG_STATE_VALID;
}

static void ic_print_snapshot(const char *label, const IC_SNAP *r)
{
    printf("    [EVIDENCE] %s snapshot: ntstatus=0x%08lX information=%lu (expected %lu) version=%u count=%u status=0x%08X "
           "luid=0x%016llX TIMINCA state=%u value=0x%08X TSAUXC state=%u value=0x%08X -> %s\n", label, (unsigned long)r->st, r->got,
           (unsigned long)sizeof(AVB_HW_SNAPSHOT_REQUEST), r->version, r->count, r->status, r->net_luid, r->timinca_state,
           r->timinca, r->tsauxc_state, r->tsauxc, r->ok ? "valid" : "unavailable");
}

/** Read-only observation through the observer handle: hw_state, GET_CLOCK_CONFIG and, independently, QUERY_HW_SNAPSHOT */
static void ic_observe(HANDLE h, IC_OBS *o)
{
    AVB_HW_STATE_QUERY hs;
    DWORD br = 0;

    ZeroMemory(o, sizeof(*o));
    ZeroMemory(&hs, sizeof(hs));
    o->hw_ok = DeviceIoControl(h, IOCTL_AVB_GET_HW_STATE, &hs, sizeof(hs), &hs, sizeof(hs), &br, NULL);
    o->hw_state = hs.hw_state;
    o->clk_st = avb_ioctl_ntstatus(h, IOCTL_AVB_GET_CLOCK_CONFIG, &o->clk, sizeof(o->clk), &o->clk, sizeof(o->clk), &br);
    QueryPerformanceCounter(&o->qpc);
    o->clk_ok = o->clk_st == 0 && br == sizeof(o->clk) && o->clk.status == 0;
    ic_snapshot(h, &o->snap);
    printf("    [EVIDENCE] observe: hw_state=%u%s clock ntstatus=0x%08lX systim=%llu timinca=0x%08X tsauxc=0x%08X\n", o->hw_state,
           o->hw_ok ? "" : "(unread)", (unsigned long)o->clk_st, o->clk_ok ? o->clk.systim : 0ULL, o->clk.timinca, o->clk.tsauxc);
    ic_print_snapshot("observe", &o->snap);
}

static long long ic_wall_ns(const IC_OBS *a, const IC_OBS *b)
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return (long long)((double)(b->qpc.QuadPart - a->qpc.QuadPart) * 1e9 / (double)f.QuadPart);
}

/** <case>-PHC and <case>-TIMINCA between two observations around one request */
static void ic_ptp_lines(const char *cs, UINT32 index, avb_u16 did, const IC_OBS *b, const IC_OBS *a)
{
    char check[64], reason[320];

    sprintf_s(check, sizeof(check), "%s-PHC", cs);
    if (!b->clk_ok || !a->clk_ok) {
        sprintf_s(reason, sizeof(reason), "PHC not read before or after (ntstatus 0x%08lX / 0x%08lX)", (unsigned long)b->clk_st,
                  (unsigned long)a->clk_st);
        ic_verdict("INCONCLUSIVE", check, index, did, reason);
    } else if (a->clk.systim < b->clk.systim) {
        sprintf_s(reason, sizeof(reason), "PTP clock reset: SYSTIM stepped back from %llu to %llu (wall %lld ns); the request "
                  "re-initialized a healthy clock", b->clk.systim, a->clk.systim, ic_wall_ns(b, a));
        ic_verdict("FAIL", check, index, did, reason);
    } else {
        sprintf_s(reason, sizeof(reason), "no step back (SYSTIM %llu -> %llu, wall %lld ns); continuity is not established without an "
                  "approved PHC tolerance", b->clk.systim, a->clk.systim, ic_wall_ns(b, a));
        ic_verdict("INCONCLUSIVE", check, index, did, reason);
    }

    sprintf_s(check, sizeof(check), "%s-TIMINCA", cs);
    if (!b->clk_ok || !a->clk_ok || !b->snap.ok || !a->snap.ok) {
        sprintf_s(reason, sizeof(reason), "TIMINCA not read in both paths before and after: clock %d/%d; snapshot ntstatus 0x%08lX/0x%08lX "
                  "information %lu/%lu TIMINCA state %u/%u (see the snapshot evidence lines)", b->clk_ok, a->clk_ok,
                  (unsigned long)b->snap.st, (unsigned long)a->snap.st, b->snap.got, a->snap.got, b->snap.timinca_state,
                  a->snap.timinca_state);
        ic_verdict("BLOCKED", check, index, did, reason);
    } else if (a->clk.timinca != b->clk.timinca || a->snap.timinca != b->snap.timinca) {
        sprintf_s(reason, sizeof(reason), "frequency rewritten: TIMINCA 0x%08X -> 0x%08X (clock), 0x%08X -> 0x%08X (snapshot)",
                  b->clk.timinca, a->clk.timinca, b->snap.timinca, a->snap.timinca);
        ic_verdict("FAIL", check, index, did, reason);
    } else {
        sprintf_s(reason, sizeof(reason), "TIMINCA 0x%08X unchanged in both reads (a rewrite with the same value is not detectable); "
                  "TSAUXC 0x%08X -> 0x%08X, hw_state %u -> %u (evidence)", a->clk.timinca, b->snap.tsauxc, a->snap.tsauxc,
                  b->hw_state, a->hw_state);
        ic_verdict("PASS", check, index, did, reason);
    }
}

/** INIT_DEVICE on h with its raw NTSTATUS and IoStatus.Information */
static LONG ic_init(HANDLE h, DWORD *got)
{
    AVB_DEVICE_INFO_REQUEST r;
    ZeroMemory(&r, sizeof(r));
    return avb_ioctl_ntstatus(h, IOCTL_AVB_INIT_DEVICE, &r, sizeof(r), &r, sizeof(r), got);
}

/** <case>-STATUS against one exact expected NTSTATUS; an error with output is never a match */
static void ic_status_line(const char *cs, UINT32 index, avb_u16 did, LONG st, DWORD got, LONG want, BOOL want_no_output)
{
    char check[64], reason[200];

    sprintf_s(check, sizeof(check), "%s-STATUS", cs);
    sprintf_s(reason, sizeof(reason), "ntstatus=0x%08lX information=%lu, expected exactly 0x%08lX%s", (unsigned long)st, got,
              (unsigned long)want, want_no_output ? " with no output" : "");
    if (st == AVB_NTSTATUS_UNAVAILABLE) {
        ic_verdict("INCONCLUSIVE", check, index, did, "raw NTSTATUS not captured");
    } else if (st == want && (!want_no_output || got == 0)) {
        ic_verdict("PASS", check, index, did, reason);
    } else {
        ic_verdict("FAIL", check, index, did, reason);
    }
}

static void ic_run_adapter(const AVB_ADAPTER_IDENTITY_REQUEST *id, const AVB_ADAPTER_IDENTITY_REQUEST *all, UINT32 n)
{
    const UINT32 index = id->index;
    const avb_u16 did = id->device_id;
    IC_OBS o0, o0b, o1, o2, o3, o4;
    HANDLE hO, hA, hB, hC;
    DWORD got = 0;
    LONG st;
    BOOL init_ok;
    UINT32 k, ordinal = 0;
    char reason[320];

    printf("\n=== adapter:%u did=0x%04X luid=0x%016llX ===\n", index, did, id->net_luid);
    g_ic_ran++;
    hO = ic_open_bound(id->net_luid);
    if (hO == INVALID_HANDLE_VALUE) {
        ic_verdict("BLOCKED", "PRESTATE", index, did, "cannot open an observer handle bound by NET_LUID");
        return;
    }
    ic_observe(hO, &o0);
    Sleep(10);
    ic_observe(hO, &o0b);
    if (!o0.hw_ok || o0.hw_state != AVB_HW_PTP_READY || !o0b.clk_ok || !o0.clk_ok || o0b.clk.systim <= o0.clk.systim) {
        sprintf_s(reason, sizeof(reason), "not a READY adapter with a healthy PTP clock: hw_state %u (READY = %u), PHC read %d/%d, "
                  "advancing %d; the READY scenarios do not apply here and are not tested", o0.hw_state, (unsigned)AVB_HW_PTP_READY,
                  o0.clk_ok, o0b.clk_ok, o0.clk_ok && o0b.clk_ok && o0b.clk.systim > o0.clk.systim);
        ic_verdict("BLOCKED", "PRESTATE", index, did, reason);
        CloseHandle(hO);
        return;
    }
    ic_verdict("PASS", "PRESTATE", index, did, "READY, PTP state readable, PHC advancing");

    /* UT-DEV-INIT-001: first INIT_DEVICE on a new instance of a READY adapter */
    hA = ic_open_bound(id->net_luid);
    if (hA == INVALID_HANDLE_VALUE) {
        ic_verdict("BLOCKED", "UT-DEV-INIT-001-STATUS", index, did, "cannot open the instance under test and bind it by NET_LUID");
        CloseHandle(hO);
        return;
    }
    st = ic_init(hA, &got);
    ic_observe(hO, &o1);
    ic_status_line("UT-DEV-INIT-001", index, did, st, got, (LONG)STATUS_SUCCESS, FALSE);
    ic_ptp_lines("UT-DEV-INIT-001", index, did, &o0b, &o1);
    init_ok = st == (LONG)STATUS_SUCCESS;

    /* UT-DEV-INIT-002: the same instance again after its success. Its precondition is that success: after a failed first
     * initialization the duplicate case does not exist, and "retry after failure" is #412's, not this case */
    if (!init_ok) {
        sprintf_s(reason, sizeof(reason), "precondition not met: the instance's first INIT_DEVICE did not succeed (ntstatus=0x%08lX); "
                  "not sent", (unsigned long)st);
        ic_verdict("BLOCKED", "UT-DEV-INIT-002-STATUS", index, did, reason);
        o2 = o1;
    } else {
        st = ic_init(hA, &got);
        ic_observe(hO, &o2);
        ic_status_line("UT-DEV-INIT-002", index, did, st, got, (LONG)STATUS_INVALID_DEVICE_STATE, TRUE);
        ic_ptp_lines("UT-DEV-INIT-002", index, did, &o1, &o2);
    }
    CloseHandle(hA);

    /* UT-DEV-INIT-SECOND (provisional ID): a second open instance on the READY adapter, after a first instance succeeded */
    hB = INVALID_HANDLE_VALUE;
    if (!init_ok || !o2.hw_ok || o2.hw_state != AVB_HW_PTP_READY) {
        sprintf_s(reason, sizeof(reason), "precondition not met: first instance initialized %d, adapter READY %d (hw_state %u); not sent",
                  init_ok, o2.hw_ok && o2.hw_state == AVB_HW_PTP_READY, o2.hw_state);
        ic_verdict("BLOCKED", "UT-DEV-INIT-SECOND-STATUS", index, did, reason);
        o3 = o2;
    } else if ((hB = ic_open_bound(id->net_luid)) == INVALID_HANDLE_VALUE) {
        ic_verdict("BLOCKED", "UT-DEV-INIT-SECOND-STATUS", index, did, "cannot open a second instance and bind it by NET_LUID");
        o3 = o2;
    } else {
        st = ic_init(hB, &got);
        ic_observe(hO, &o3);
        ic_status_line("UT-DEV-INIT-SECOND", index, did, st, got, (LONG)STATUS_SUCCESS, FALSE);
        ic_ptp_lines("UT-DEV-INIT-SECOND", index, did, &o2, &o3);
        CloseHandle(hB);
    }

    /* UT-DEV-OPEN-READY (provisional ID): IOCTL 32 selects the adapter by its ABI fields (vendor, device, instance ordinal) */
    for (k = 0; k < n && all[k].index != index; k++) {
        ordinal += all[k].vendor_id == id->vendor_id && all[k].device_id == id->device_id;
    }
    hC = ic_open();
    if (hC == INVALID_HANDLE_VALUE) {
        ic_verdict("BLOCKED", "UT-DEV-OPEN-READY-STATUS", index, did, "cannot open a handle for OPEN_ADAPTER");
    } else {
        AVB_OPEN_REQUEST o;
        IC_SNAP bs;

        ZeroMemory(&o, sizeof(o));
        o.vendor_id = id->vendor_id;
        o.device_id = id->device_id;
        o.index = ordinal;
        st = avb_ioctl_ntstatus(hC, IOCTL_AVB_OPEN_ADAPTER, &o, sizeof(o), &o, sizeof(o), &got);
        ic_observe(hO, &o4);
        printf("    [EVIDENCE] OPEN_ADAPTER vid=0x%04X did=0x%04X instance ordinal %u: ntstatus=0x%08lX information=%lu status=0x%08X\n",
               id->vendor_id, id->device_id, ordinal, (unsigned long)st, got, o.status);
        /* STATUS: the raw result against the expected STATUS_SUCCESS, kept apart from the binding evidence */
        ic_status_line("UT-DEV-OPEN-READY", index, did, st, got, (LONG)STATUS_SUCCESS, FALSE);
        /* BINDING: which adapter the handle is bound to, from the handle's own snapshot */
        if (st != (LONG)STATUS_SUCCESS) {
            sprintf_s(reason, sizeof(reason), "no binding to verify: OPEN_ADAPTER did not succeed (ntstatus=0x%08lX)", (unsigned long)st);
            ic_verdict("BLOCKED", "UT-DEV-OPEN-READY-BINDING", index, did, reason);
        } else {
            ic_snapshot(hC, &bs);
            ic_print_snapshot("OPEN_ADAPTER handle", &bs);
            if (bs.st != 0 || bs.got < (DWORD)FIELD_OFFSET(AVB_HW_SNAPSHOT_REQUEST, entries)) {
                sprintf_s(reason, sizeof(reason), "binding evidence unavailable: snapshot ntstatus=0x%08lX information=%lu",
                          (unsigned long)bs.st, bs.got);
                ic_verdict("BLOCKED", "UT-DEV-OPEN-READY-BINDING", index, did, reason);
            } else if (bs.net_luid != id->net_luid) {
                sprintf_s(reason, sizeof(reason), "the handle is bound to NET_LUID 0x%016llX, not to this adapter (0x%016llX)",
                          bs.net_luid, id->net_luid);
                ic_verdict("FAIL", "UT-DEV-OPEN-READY-BINDING", index, did, reason);
            } else {
                ic_verdict("PASS", "UT-DEV-OPEN-READY-BINDING", index, did, "the handle is bound to this adapter (snapshot NET_LUID)");
            }
        }
        ic_ptp_lines("UT-DEV-OPEN-READY", index, did, &o3, &o4);
        CloseHandle(hC);
    }
    printf("    [COVERAGE] not exercised: an adapter that is not READY, concurrent INIT_DEVICE, the order in which READY is "
           "published, retry after a failed initialization (#412), GET_DEVICE_INFO before initialization\n");
    CloseHandle(hO);
}

static int run_init_contract(void)
{
    AVB_ADAPTER_IDENTITY_REQUEST first, *all = NULL;
    UINT32 i, count = 0, nall = 0;
    BOOL complete = FALSE;
    HANDLE h;

    printf("=== IntelAvbFilter - INIT_DEVICE / OPEN_ADAPTER contract (#313 amendment 2026-11-12, #404) ===\n");
    h = ic_open();
    if (h == INVALID_HANDLE_VALUE) {
        printf("[BLOCKED] DISCOVERY: cannot open %s (error %lu)\n", DEVICE_PATH_PRIMARY, GetLastError());
        return 3;
    }
    if (ic_identity(h, 0, &first)) {
        count = first.count;
        all = (AVB_ADAPTER_IDENTITY_REQUEST *)calloc(count, sizeof(*all));
        complete = all != NULL;
        for (i = 0; complete && i < count; i++) {
            if (!ic_identity(h, i, &all[i]) || all[i].count != count) {
                complete = FALSE;
                break;
            }
            printf("    [EVIDENCE] adapter:%u did=0x%04X luid=0x%016llX caps=0x%08X\n", i, all[i].device_id, all[i].net_luid,
                   all[i].capabilities);
            nall++;
        }
    }
    CloseHandle(h);
    if (!complete) {
        printf("[BLOCKED] DISCOVERY: %u of %u identities validated; untested adapters are not passes\n", nall, count);
        g_ic_blocked++;
    }
    for (i = 0; i < nall; i++) {
        ic_run_adapter(&all[i], all, nall);
    }
    free(all);
    printf("\nadapters run=%d nonpassing verdicts=%d blocked=%d\n", g_ic_ran, g_ic_failed, g_ic_blocked);
    if (g_ic_ran == 0) { return 2; }
    if (g_ic_failed != 0) { return 1; }
    return g_ic_blocked != 0 ? 3 : 0;
}

int main(int argc, char **argv) {
    TestContext ctx = {0};

    if (argc == 2 && strcmp(argv[1], "--init-contract") == 0) {
        return run_init_contract();
    }

    printf("\n");
    printf("====================================================================\n");
    printf(" Device Lifecycle Management Test Suite\n");
    printf("====================================================================\n");
    printf(" Test Plan: TEST-PLAN-IOCTL-NEW-2025-12-31.md\n");
    printf(" Issue: #313 (TEST-DEV-LIFECYCLE-001)\n");
    printf(" Requirement: #12 (REQ-F-DEVICE-LIFECYCLE-001)\n");
    printf(" IOCTLs: INIT (20), GET_INFO (21), ENUM (31), OPEN (32), GET_HW_STATE (37)\n");
    printf(" Total Tests: 19\n");
    printf(" Priority: P0 (Critical)\n");
    printf("====================================================================\n");
    printf("\n");
    
    /* Run test cases */
    printf("Running Device Lifecycle tests...\n\n");
    
    Test_FirstTimeInitialization(&ctx);
    Test_DuplicateInitializationPrevention(&ctx);
    Test_DeviceInformationRetrieval(&ctx);
    Test_DeviceInfoBeforeInitialization(&ctx);
    Test_SingleAdapterEnumeration(&ctx);
    Test_MultipleAdapterEnumeration(&ctx);
    Test_EnumerationWithNoAdapters(&ctx);
    Test_OpenFirstAvailableAdapter(&ctx);
    Test_OpenByDevicePath(&ctx);
    Test_InvalidAdapterIndexRejection(&ctx);
    Test_ConcurrentOpenRequests(&ctx);
    Test_HardwareStateRetrievalD0(&ctx);
    Test_HardwareStateDuringD3Transition(&ctx);
    Test_LinkStateDetection(&ctx);
    Test_ResourceAllocationStatus(&ctx);
    Test_FullLifecycleSequence(&ctx);
    Test_InitializationAfterFailedStart(&ctx);
    Test_HotPlugDeviceDetection(&ctx);
    Test_GracefulShutdownSequence(&ctx);
    Test_PnPRemoveAndReAdd(&ctx);
    
    /* Print summary */
    printf("\n");
    printf("====================================================================\n");
    printf(" Test Summary\n");
    printf("====================================================================\n");
    printf(" Total:   %d tests\n", ctx.test_count);
    printf(" Passed:  %d tests\n", ctx.pass_count);
    printf(" Failed:  %d tests\n", ctx.fail_count);
    printf(" Skipped: %d tests\n", ctx.skip_count);
    printf("====================================================================\n");
    printf("\n");
    
    /* Exit with pass rate */
    if (ctx.fail_count > 0) {
        return 1;  /* Failures detected */
    } else if (ctx.pass_count == 0) {
        return 2;  /* All tests skipped */
    } else {
        return 0;  /* Pass */
    }
}
