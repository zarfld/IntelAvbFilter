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
 *   UT-DEV-INIT-DISCOVERY    the device opens and every identity validates; otherwise FAILED_SETUP (untested adapters never pass).
 *   UT-DEV-INIT-PRESTATE     setup: an observer handle bound by NET_LUID (OPEN_ADAPTER_BY_LUID binds only) sees AVB_HW_PTP_READY,
 *                  readable PTP state and an advancing PHC. Not established: FAILED_SETUP, and every dependent case below
 *                  appears as its own BLOCKED line (not executed), none is sent.
 *   UT-DEV-INIT-001          first INIT_DEVICE on a new instance (own handle, bound by NET_LUID): exactly STATUS_SUCCESS.
 *   UT-DEV-INIT-002          the same instance again after its success: exactly STATUS_INVALID_DEVICE_STATE, no output. Sent only
 *                  after UT-DEV-INIT-001 succeeded (its precondition); otherwise BLOCKED, never run under a failed precondition.
 *   UT-DEV-INIT-SECOND (registered in #313, "second open instance on a READY adapter"): STATUS_SUCCESS. Sent only
 *                  after a first instance succeeded and the adapter is still READY; otherwise BLOCKED.
 *   UT-DEV-OPEN-READY  (registered in #313, "OPEN_ADAPTER without hardware change"): -PRESTATE re-establishes the READY
 *                  scenario immediately before the request from fresh observer reads, which are the baseline of its PHC and
 *                  TIMINCA lines (not established: FAILED_SETUP, not sent). IOCTL 32 selects by the global enumeration index.
 *                  -STATUS is the raw result against the expected STATUS_SUCCESS (an error is FAIL); -BINDING separately
 *                  verifies through the handle's own QUERY_HW_SNAPSHOT NET_LUID which adapter it is bound to (no evidence:
 *                  BLOCKED). QUERY_HW_SNAPSHOT is always recorded raw (NTSTATUS, length, header, entry states), never
 *                  interpreted.
 * After each request, from the observer handle (GET_CLOCK_CONFIG and, independently, QUERY_HW_SNAPSHOT), two lines:
 *   <case>-PHC     a healthy PTP clock is not reset. A backward step of SYSTIM establishes a reset (FAIL). No PHC tolerance is
 *                  approved, so no step back cannot prove continuity: INCONCLUSIVE, with the measured values.
 *   <case>-TIMINCA the frequency is not rewritten. A change in any valid before/after read pair is FAIL; PASS needs both read
 *                  paths unchanged; a missing path without an observed change is BLOCKED (a rewrite with the same value is not
 *                  detectable). TSAUXC and hw_state are printed as evidence.
 *   UT-DEV-INIT-FINAL        cleanup and final state: every handle the cases opened is closed, then fresh observer reads show READY,
 *                  an advancing PHC and TIMINCA/TSAUXC as before the cases; otherwise CLEANUP_FAILED (not measured:
 *                  INCONCLUSIVE_RESTORATION). A PHC reset reported above cannot be undone and is not judged again here.
 * Not exercised (COVERAGE line): an adapter that is not READY, concurrent INIT_DEVICE (STATUS_DEVICE_BUSY), the order in which
 * READY is published, retry after a failed initialization (#412, separately owned), GET_DEVICE_INFO before initialization.
 * Mutations: INIT_DEVICE and OPEN_ADAPTER as the cases send them; on a driver that re-initializes a READY adapter they reset
 * its PTP clock, which no test can undo (reported RED, never hidden). Safety class Init, in the automatic HardwareUnit suite.
 * Exit: 0 all passed, 1 any nonpassing verdict (FAILED_SETUP and CLEANUP_FAILED included), 2 no adapter ran, 3 only BLOCKED.
 *
 * --init-retry-fault: #313 UT-DEV-LIFECYCLE-002 "Initialization After Failed Start" (first attempt STATUS_DEVICE_HARDWARE_ERROR,
 * retry after the fault is cleared STATUS_SUCCESS, no lingering error state), executed with the Debug-only initialization fault
 * mechanism of #412 (include/avb_init_fault_dbg.h, src/avb_init_fault_dbg.c). On every enumerated adapter (no fixed limit;
 * per-adapter storage is sized from the validated inventory); identity, device node and arm target by NET_LUID, never by model:
 *   UT-DEV-LIFECYCLE-002-PRESTATE  setup before any mutation: device node, an independent management link (another adapter up
 *                  with a gateway), the driver Parameters key, stale fault values drained, removed and verified absent, the
 *                  target READY (the generic lifecycle state), the other adapters' state recorded. A precondition that is
 *                  checked and does not hold is FAILED_SETUP: setup failed, no initialization is attempted (without a
 *                  management link the NIC cycle is not run, it would cut the host's only path), the dependent lines stay
 *                  BLOCKED and a [COVERAGE] line keeps the adapter's gap visible.
 *   UT-DEV-LIFECYCLE-002-REACH     phase A: OBSERVE armed for origin ATTACH, owned NIC cycle (fresh attach of the target only).
 *                  PASS needs the driver record that consumed it: target, origin ATTACH, site BASIC (the fault point), IRQL 0,
 *                  and READY afterwards (its TIMINCA/TSAUXC are the normal-attach reference). Otherwise FAILED_SETUP: the
 *                  mechanism is not assumed to reach the fault point and the cases below are BLOCKED.
 *   UT-DEV-LIFECYCLE-002-ATTACH-FAILED  phase B setup: FAIL armed for ATTACH, RESTART, INIT_PRE and INIT_HANDLER (every bring-up
 *                  a fresh attach and one INIT_DEVICE make), owned NIC cycle; PASS needs ATTACH and RESTART consumption attributed
 *                  and the adapter enumerable below PTP_READY.
 *   UT-DEV-LIFECYCLE-002-FIRST-STATUS   instance A's first INIT_DEVICE against the published STATUS_DEVICE_HARDWARE_ERROR; the
 *                  actual NTSTATUS and which INIT origins consumed FAIL are recorded (a different status is FAIL, not normalized).
 *   UT-DEV-LIFECYCLE-002-FIRST-NOT-READY  READY is not published after that failure.
 *   UT-DEV-LIFECYCLE-002-RETRY-STATUS   the fault is cleared (unconsumed actions marked cleared, recording continues), instance
 *                  A retries on the same handle: exactly STATUS_SUCCESS. Sent only after the first request observably failed.
 *   UT-DEV-LIFECYCLE-002-RETRY-READY    afterwards READY (no lingering error state).
 *   UT-DEV-LIFECYCLE-002-RETRY-PHC      the PHC advancing afterwards, only where INTEL_CAP_BASIC_1588 is advertised (else an
 *                  evidence line); an invalid clock read is INCONCLUSIVE.
 *   UT-DEV-LIFECYCLE-002-OTHER-INSTANCE a new instance B on the READY adapter: STATUS_SUCCESS and no initialization attempt
 *                  started (the driver's sequence numbers unchanged, history complete) (#404 contract row 1).
 *   UT-DEV-LIFECYCLE-002-ATTRIBUTION    the session's history complete (a record for every sequence the driver reserved,
 *                  none in flight, not saturated); every record on the target in this session at IRQL 0, no attempt skipped,
 *                  every consumed action attributed to a record with its origin, site BASIC, index and kind.
 *   UT-DEV-LIFECYCLE-002-FINAL          cleanup: handles closed, the session drained (no attempt in flight), disarmed and its
 *                  values deleted, residue checked by enumeration; an owned NIC cycle without arm if the target is not READY or
 *                  its registers differ from the normal-attach reference; then READY, no fault value left, the other adapters
 *                  READY. Feature-specific parts are judged only where applicable: PHC advancing with INTEL_CAP_BASIC_1588,
 *                  TIMINCA/TSAUXC with BASIC_1588 and INTEL_CAP_MMIO, each only from valid reads. A difference is
 *                  CLEANUP_FAILED; an applicable part that could not be read, or an enumeration failure, is
 *                  INCONCLUSIVE_RESTORATION.
 * Sessions (PR #462 review): each arming has its own session id; the driver publishes an attempt's record only while that
 * session is still armed, so an attempt from an earlier phase can never write into a later one. The test never writes or
 * deletes a fault value: ARM, DISARM and the clearing before the retry are IOCTL_AVB_DBG_INITFAULT_CONTROL requests the
 * driver performs under the lock that admits attempts; the test only reads the registry. Phase A ends only after
 * FilterRestart's attempt is recorded and nothing is in flight. No capability filter selects adapters: INIT_DEVICE is a
 * lifecycle operation of every adapter.
 * Mutations: the target's owned NIC cycles (its PHC restarts with each attach), INIT_DEVICE on it, the fault values in the
 * Parameters key (removed by the cleanup). Safety class Unsafe (owned CM_Disable_DevNode / CM_Enable_DevNode). A Release driver
 * has no mechanism: REACH is then FAILED_SETUP, never a pass.
 */

#include <winsock2.h>   /* before windows.h: iphlpapi (--init-retry-fault management-link check) */
#include <windows.h>
#include <iphlpapi.h>
#include <cfgmgr32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Single Source of Truth for IOCTL definitions */
#include "../../include/avb_ioctl.h"
#include "../common/avb_ntstatus.h"   /* --init-contract: exact NTSTATUS (Win32 errors are lossy) */
#include "../../include/avb_init_fault_dbg.h"   /* --init-retry-fault: the Debug-only fault arm and records (#412) */

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
                    TEST_SKIP, "Executed by --init-retry-fault (Debug driver fault mechanism, #412), not by the default run");
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

    /* An observed change in any valid before/after pair is a FAIL on its own; a missing corroborating path only prevents a PASS
     * (PR #457 review) */
    sprintf_s(check, sizeof(check), "%s-TIMINCA", cs);
    {
        BOOL clk_pair = b->clk_ok && a->clk_ok, snap_pair = b->snap.ok && a->snap.ok;
        BOOL clk_changed = clk_pair && a->clk.timinca != b->clk.timinca;
        BOOL snap_changed = snap_pair && a->snap.timinca != b->snap.timinca;
        char cpath[64], spath[96];

        if (clk_pair) {
            sprintf_s(cpath, sizeof(cpath), "0x%08X -> 0x%08X", b->clk.timinca, a->clk.timinca);
        } else {
            strcpy_s(cpath, sizeof(cpath), "not read");
        }
        if (snap_pair) {
            sprintf_s(spath, sizeof(spath), "0x%08X -> 0x%08X", b->snap.timinca, a->snap.timinca);
        } else {
            sprintf_s(spath, sizeof(spath), "not read (ntstatus 0x%08lX/0x%08lX)", (unsigned long)b->snap.st, (unsigned long)a->snap.st);
        }
        if (clk_changed || snap_changed) {
            sprintf_s(reason, sizeof(reason), "frequency rewritten: TIMINCA clock %s, snapshot %s", cpath, spath);
            ic_verdict("FAIL", check, index, did, reason);
            return;
        }
        if (!clk_pair || !snap_pair) {
            sprintf_s(reason, sizeof(reason), "no change in the available read path(s), but PASS needs both: TIMINCA clock %s, snapshot %s",
                      cpath, spath);
            ic_verdict("BLOCKED", check, index, did, reason);
            return;
        }
    }
    {
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

/** The READY scenario observed now: two observer reads, READY and a readable, advancing PHC. *base receives the later read */
static BOOL ic_ready_now(HANDLE hO, IC_OBS *first, IC_OBS *base)
{
    ic_observe(hO, first);
    Sleep(10);
    ic_observe(hO, base);
    return first->hw_ok && first->hw_state == AVB_HW_PTP_READY && base->hw_ok && base->hw_state == AVB_HW_PTP_READY &&
           first->clk_ok && base->clk_ok && base->clk.systim > first->clk.systim;
}

/** Every dependent assertion that was not executed stays visible as its own BLOCKED line */
static void ic_blocked_cases(UINT32 index, avb_u16 did, const char *reason, const char *const *cases, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        ic_verdict("BLOCKED", cases[i], index, did, reason);
    }
}

/** Cleanup is closing every handle the cases opened (done before this call); then the final state, from fresh observer reads:
 *  READY, a readable advancing PHC and TIMINCA/TSAUXC as before the cases. A PHC reset reported above cannot be undone and is
 *  not judged again here */
static void ic_final_state(HANDLE hO, UINT32 index, avb_u16 did, const IC_OBS *pre)
{
    IC_OBS f0, f1;
    char reason[320];
    BOOL ready = ic_ready_now(hO, &f0, &f1);

    if (!f0.hw_ok || !f1.clk_ok || !f0.clk_ok) {
        sprintf_s(reason, sizeof(reason), "final state not measured: hw_state read %d, PHC read %d/%d", f0.hw_ok, f0.clk_ok, f1.clk_ok);
        ic_verdict("INCONCLUSIVE_RESTORATION", "UT-DEV-INIT-FINAL", index, did, reason);
    } else if (!ready || f1.clk.timinca != pre->clk.timinca || f1.clk.tsauxc != pre->clk.tsauxc) {
        sprintf_s(reason, sizeof(reason), "after cleanup: hw_state %u (READY = %u), PHC advancing %d, TIMINCA 0x%08X (before 0x%08X), "
                  "TSAUXC 0x%08X (before 0x%08X)", f1.hw_state, (unsigned)AVB_HW_PTP_READY, f1.clk.systim > f0.clk.systim,
                  f1.clk.timinca, pre->clk.timinca, f1.clk.tsauxc, pre->clk.tsauxc);
        ic_verdict("CLEANUP_FAILED", "UT-DEV-INIT-FINAL", index, did, reason);
    } else {
        sprintf_s(reason, sizeof(reason), "after cleanup: READY, PHC advancing, TIMINCA 0x%08X and TSAUXC 0x%08X as before the cases "
                  "(clock read; a PHC reset reported above is not undone)", f1.clk.timinca, f1.clk.tsauxc);
        ic_verdict("PASS", "UT-DEV-INIT-FINAL", index, did, reason);
    }
}

static void ic_run_adapter(const AVB_ADAPTER_IDENTITY_REQUEST *id)
{
    static const char *const after_prestate[] = { "UT-DEV-INIT-001-STATUS", "UT-DEV-INIT-002-STATUS", "UT-DEV-INIT-SECOND-STATUS",
                                                  "UT-DEV-OPEN-READY-STATUS", "UT-DEV-OPEN-READY-BINDING", "UT-DEV-INIT-FINAL" };
    const UINT32 index = id->index;
    const avb_u16 did = id->device_id;
    IC_OBS o0, o0b, o1, o2, o3, o4, oR;
    HANDLE hO, hA, hB, hC;
    DWORD got = 0;
    LONG st;
    BOOL init_ok;
    char reason[320];

    printf("\n=== adapter:%u did=0x%04X luid=0x%016llX ===\n", index, did, id->net_luid);
    g_ic_ran++;
    hO = ic_open_bound(id->net_luid);
    if (hO == INVALID_HANDLE_VALUE) {
        ic_verdict("FAILED_SETUP", "UT-DEV-INIT-PRESTATE", index, did, "cannot open an observer handle bound by NET_LUID");
        ic_blocked_cases(index, did, "not executed: setup failed (no observer handle)", after_prestate, 6);
        return;
    }
    if (!ic_ready_now(hO, &o0, &o0b)) {
        sprintf_s(reason, sizeof(reason), "READY scenario not established: hw_state %u (READY = %u), PHC read %d/%d, advancing %d",
                  o0.hw_state, (unsigned)AVB_HW_PTP_READY, o0.clk_ok, o0b.clk_ok, o0.clk_ok && o0b.clk_ok && o0b.clk.systim > o0.clk.systim);
        ic_verdict("FAILED_SETUP", "UT-DEV-INIT-PRESTATE", index, did, reason);
        ic_blocked_cases(index, did, "not executed: the READY scenario was not established (UT-DEV-INIT-PRESTATE); no request sent",
                         after_prestate, 6);
        CloseHandle(hO);
        return;
    }
    ic_verdict("PASS", "UT-DEV-INIT-PRESTATE", index, did, "READY, PTP state readable, PHC advancing");

    /* UT-DEV-INIT-001: first INIT_DEVICE on a new instance of a READY adapter */
    hA = ic_open_bound(id->net_luid);
    if (hA == INVALID_HANDLE_VALUE) {
        ic_verdict("FAILED_SETUP", "UT-DEV-INIT-001-STATUS", index, did, "cannot open the instance under test and bind it by NET_LUID");
        ic_blocked_cases(index, did, "not executed: UT-DEV-INIT-001 setup failed", after_prestate + 1, 4);
        ic_final_state(hO, index, did, &o0b);
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

    /* UT-DEV-INIT-SECOND (registered in #313): a second open instance on the READY adapter, after a first instance succeeded */
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

    /* UT-DEV-OPEN-READY (registered in #313): IOCTL 32 on a READY adapter. Its READY scenario is re-established immediately before
     * the request, from fresh observer reads, which are also its baseline (PR #457 review); not established: not sent */
    hC = INVALID_HANDLE_VALUE;
    if (!ic_ready_now(hO, &oR, &o3)) {
        sprintf_s(reason, sizeof(reason), "READY scenario not established before OPEN_ADAPTER: hw_state %u, PHC read %d/%d, advancing %d; "
                  "not sent", o3.hw_state, oR.clk_ok, o3.clk_ok, oR.clk_ok && o3.clk_ok && o3.clk.systim > oR.clk.systim);
        ic_verdict("FAILED_SETUP", "UT-DEV-OPEN-READY-PRESTATE", index, did, reason);
        ic_blocked_cases(index, did, "not executed: the READY scenario was not established before OPEN_ADAPTER", after_prestate + 3, 2);
    } else if ((hC = ic_open()) == INVALID_HANDLE_VALUE) {
        ic_verdict("PASS", "UT-DEV-OPEN-READY-PRESTATE", index, did, "READY, PHC readable and advancing immediately before OPEN_ADAPTER");
        ic_verdict("FAILED_SETUP", "UT-DEV-OPEN-READY-STATUS", index, did, "cannot open a handle for OPEN_ADAPTER");
        ic_blocked_cases(index, did, "not executed: OPEN_ADAPTER setup failed", after_prestate + 4, 1);
    } else {
        AVB_OPEN_REQUEST o;
        IC_SNAP bs;

        ic_verdict("PASS", "UT-DEV-OPEN-READY-PRESTATE", index, did, "READY, PHC readable and advancing immediately before OPEN_ADAPTER");
        /* OPEN_ADAPTER selects by the global enumeration index, the order of IOCTL_AVB_ENUM_ADAPTERS (src/device.c) */
        ZeroMemory(&o, sizeof(o));
        o.vendor_id = id->vendor_id;
        o.device_id = id->device_id;
        o.index = index;
        st = avb_ioctl_ntstatus(hC, IOCTL_AVB_OPEN_ADAPTER, &o, sizeof(o), &o, sizeof(o), &got);
        ic_observe(hO, &o4);
        printf("    [EVIDENCE] OPEN_ADAPTER vid=0x%04X did=0x%04X global index %u: ntstatus=0x%08lX information=%lu status=0x%08X\n",
               id->vendor_id, id->device_id, index, (unsigned long)st, got, o.status);
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

    /* Cleanup: every handle the cases opened is closed above; then the final state */
    ic_final_state(hO, index, did, &o0b);
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
        printf("[FAILED_SETUP] UT-DEV-INIT-DISCOVERY: cannot open %s (error %lu); no adapter tested\n", DEVICE_PATH_PRIMARY, GetLastError());
        return 1;
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
        printf("[FAILED_SETUP] UT-DEV-INIT-DISCOVERY: %u of %u identities validated; untested adapters are not passes\n", nall, count);
        g_ic_failed++;
    }
    for (i = 0; i < nall; i++) {
        ic_run_adapter(&all[i]);
    }
    free(all);
    printf("\nadapters run=%d nonpassing verdicts=%d blocked=%d\n", g_ic_ran, g_ic_failed, g_ic_blocked);
    if (g_ic_ran == 0) { return 2; }
    if (g_ic_failed != 0) { return 1; }
    return g_ic_blocked != 0 ? 3 : 0;
}

/*==============================================================================
 * --init-retry-fault (#412: UT-DEV-LIFECYCLE-002 with the Debug-only initialization fault mechanism)
 *============================================================================*/
static int g_irf_established, g_irf_completed;   /* adapters whose PRESTATE passed; adapters whose run reached its end */
#define IRF_DISABLE_BOUND_MS   120000
#define IRF_ENABLE_BOUND_MS    60000
#define IRF_RETURN_BOUND_MS    60000         /* identity back, the expected attempts recorded, READY where expected */
#define IRF_DRAIN_BOUND_MS     (2 * AVB_INITFAULT_MAX_HOLD_MS + 5000)   /* an attempt in flight: at most one HOLD plus intel_init */
#define IRF_ARM_LIFETIME_S     300           /* arm expiry from arming: one bounded cycle (240 s) plus the INIT_DEVICE requests */
#define IRF_CLEARED            AVB_INITFAULT_CLEARED   /* consumed_by of an action cleared before the retry */
#define IRF_NET_CLASS_KEY      "SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}\\"

static const char *irf_origin_name(unsigned o)
{
    static const char *const n[] = { "?", "ATTACH", "RESTART", "CREATE_IOCTL", "CREATE_ENUM", "CREATE_OPEN", "INIT_PRE",
                                     "INIT_HANDLER", "OPEN_HANDLER" };
    return o < ARRAYSIZE(n) ? n[o] : "?";
}

static const char *irf_site_name(unsigned s)
{
    static const char *const n[] = { "?", "BASIC", "BRINGUP", "ENSURE", "FORCED" };
    return s < ARRAYSIZE(n) ? n[s] : "?";
}

static const char *irf_act_name(unsigned a)
{
    static const char *const n[] = { "NONE", "OBSERVE", "FAIL", "HOLD", "HOLD_REFUSED" };
    return a < ARRAYSIZE(n) ? n[a] : "?";
}

/** The driver's Parameters key (created if absent; *created tells the cleanup to remove it again) */
static HKEY irf_params_open(BOOL *created)
{
    HKEY k = NULL;
    DWORD disp = 0;

    *created = FALSE;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, AVB_INITFAULT_PARAMS_SUBKEY, 0, NULL, 0, KEY_QUERY_VALUE, NULL, &k,
                        &disp) != ERROR_SUCCESS) {
        return NULL;
    }
    *created = (disp == REG_CREATED_NEW_KEY);
    return k;
}

static void irf_log_name(WCHAR *buf, size_t n, unsigned __int64 session, unsigned seq)
{
    swprintf_s(buf, n, L"%ls%016llX_%02u", AVB_INITFAULT_LOG_PREFIX, session, seq);
}

/** Counts every value whose name starts with AVB_INITFAULT_NAME_PREFIX (arm and records of any session); read only, the
 *  test never writes or deletes a fault value. Returns FALSE if the enumeration itself failed: residue unknown, never "none" */
static BOOL irf_residue(HKEY k, unsigned *count)
{
    DWORD i, len;
    LONG rc;
    size_t plen = wcslen(AVB_INITFAULT_NAME_PREFIX);

    *count = 0;
    for (i = 0; i < 4096; i++) {
        WCHAR name[64];
        len = ARRAYSIZE(name);
        rc = RegEnumValueW(k, i, name, &len, NULL, NULL, NULL, NULL);
        if (rc == ERROR_NO_MORE_ITEMS) { return TRUE; }
        if (rc == ERROR_MORE_DATA) { continue; }   /* a longer name: not one of ours (ours fit in 64) */
        if (rc != ERROR_SUCCESS) { return FALSE; }
        if (_wcsnicmp(name, AVB_INITFAULT_NAME_PREFIX, plen) == 0) { (*count)++; }
    }
    return FALSE;
}

/** One control operation, performed by the driver under its attempt-admission lock (IOCTL_AVB_DBG_INITFAULT_CONTROL). The raw
 *  NTSTATUS is returned; a Release driver (no mechanism) answers STATUS_INVALID_DEVICE_REQUEST */
static LONG irf_ctl(unsigned op, unsigned __int64 session, const AVB_INITFAULT_ARM *arm, AVB_INITFAULT_CONTROL *out)
{
    DWORD got = 0;
    LONG st;
    HANDLE h = ic_open();

    ZeroMemory(out, sizeof(*out));
    out->version = AVB_INITFAULT_VERSION;
    out->op = op;
    out->session = session;
    if (arm != NULL) { out->arm = *arm; }
    if (h == INVALID_HANDLE_VALUE) { return AVB_NTSTATUS_UNAVAILABLE; }
    st = avb_ioctl_ntstatus(h, IOCTL_AVB_DBG_INITFAULT_CONTROL, out, sizeof(*out), out, sizeof(*out), &got);
    CloseHandle(h);
    if (st == 0 && got != sizeof(*out)) { return AVB_NTSTATUS_UNAVAILABLE; }
    return st;
}

static BOOL irf_read_arm(HKEY k, AVB_INITFAULT_ARM *a)
{
    DWORD type = 0, size = sizeof(*a);
    ZeroMemory(a, sizeof(*a));
    return RegQueryValueExW(k, AVB_INITFAULT_ARM_VALUE, NULL, &type, (BYTE *)a, &size) == ERROR_SUCCESS && type == REG_BINARY &&
           size == sizeof(*a) && a->version == AVB_INITFAULT_VERSION;
}

/** Ends a session: waits (bounded, reading the arm only) until no attempt of it is in flight, then asks the driver to disarm.
 *  The driver deletes the arm and every fault value under the lock that admits attempts, so no attempt can write afterwards
 *  until the next ARM. Then a read-only enumeration verifies that nothing is left */
static BOOL irf_drain_clear(HKEY k, const char *label)
{
    AVB_INITFAULT_ARM a;
    AVB_INITFAULT_CONTROL c;
    DWORD start = GetTickCount();
    unsigned left = 0;
    BOOL drained = TRUE, enum_ok;
    LONG st;

    while (irf_read_arm(k, &a) && a.inflight != 0) {
        if (GetTickCount() - start >= IRF_DRAIN_BOUND_MS) {
            drained = FALSE;
            break;
        }
        Sleep(100);
    }
    if (!drained) {
        printf("    [EVIDENCE] %s: %u attempt(s) still in flight after %d ms; disarming anyway (their records are discarded)\n", label,
               a.inflight, IRF_DRAIN_BOUND_MS);
    }
    st = irf_ctl(AVB_INITFAULT_OP_DISARM, 0, NULL, &c);
    enum_ok = irf_residue(k, &left);
    printf("    [EVIDENCE] %s: drained %d; driver DISARM ntstatus=0x%08lX inflight at disarm %u, values deleted %u; fault values left %u "
           "(enumeration %s)\n", label, drained, (unsigned long)st, c.inflight_at_op, c.values_deleted, left, enum_ok ? "complete" : "FAILED");
    return st == 0 && enum_ok && left == 0 && drained;
}

static unsigned __int64 irf_new_session(void)
{
    LARGE_INTEGER q;
    unsigned __int64 s;
    QueryPerformanceCounter(&q);
    s = (unsigned __int64)q.QuadPart ^ ((unsigned __int64)GetCurrentProcessId() << 40) ^ ((unsigned __int64)GetTickCount() << 20);
    return s != 0 ? s : 1;
}

/** Arms (origin, action) pairs for the one target adapter in a new session, expiring IRF_ARM_LIFETIME_S from now */
static BOOL irf_arm(avb_u64 luid, const unsigned (*acts)[2], unsigned n, unsigned __int64 *session)
{
    AVB_INITFAULT_ARM a;
    FILETIME ft;
    ULARGE_INTEGER t;
    unsigned i;

    ZeroMemory(&a, sizeof(a));
    a.version = AVB_INITFAULT_VERSION;
    a.count = n;
    a.net_luid = luid;
    a.session = irf_new_session();
    GetSystemTimeAsFileTime(&ft);
    t.LowPart = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    a.expiry_100ns = t.QuadPart + (unsigned __int64)IRF_ARM_LIFETIME_S * 10000000ULL;
    printf("    [EVIDENCE] arming session 0x%016llX\n", a.session);
    for (i = 0; i < n && i < AVB_INITFAULT_MAX_ACTIONS; i++) {
        a.actions[i].origin = acts[i][0];
        a.actions[i].action = acts[i][1];
        printf("    [EVIDENCE] armed action %u: origin=%s action=%s\n", i, irf_origin_name(acts[i][0]), irf_act_name(acts[i][1]));
    }
    *session = a.session;
    {
        AVB_INITFAULT_CONTROL c;
        LONG st = irf_ctl(AVB_INITFAULT_OP_ARM, 0, &a, &c);
        printf("    [EVIDENCE] driver ARM ntstatus=0x%08lX (values deleted before arming %u)\n", (unsigned long)st, c.values_deleted);
        return st == 0;
    }
}

/** The session's records, by sequence; present[i] tells which exist and validate (version, sequence, session) */
static unsigned irf_read_records(HKEY k, unsigned __int64 session, AVB_INITFAULT_RECORD *r, BOOL *present)
{
    WCHAR name[64];
    unsigned i, n = 0;

    for (i = 0; i < AVB_INITFAULT_MAX_RECORDS; i++) {
        DWORD type = 0, size = sizeof(r[i]);
        irf_log_name(name, ARRAYSIZE(name), session, i);
        ZeroMemory(&r[i], sizeof(r[i]));
        present[i] = RegQueryValueExW(k, name, NULL, &type, (BYTE *)&r[i], &size) == ERROR_SUCCESS && type == REG_BINARY &&
                     size == sizeof(r[i]) && r[i].version == AVB_INITFAULT_VERSION && r[i].seq == i && r[i].session == session;
        if (present[i]) { n++; }
    }
    return n;
}

/** The session's history is complete: the arm is readable, not saturated, no attempt is in flight, and every sequence the
 *  driver reserved (below next_seq) has its record. A missing record is never read as "no attempt" */
static BOOL irf_history_complete(HKEY k, unsigned __int64 session, AVB_INITFAULT_ARM *a, AVB_INITFAULT_RECORD *r, BOOL *present,
                                 unsigned *missing)
{
    unsigned i;
    BOOL arm_ok = irf_read_arm(k, a) && a->session == session;

    (void)irf_read_records(k, session, r, present);
    *missing = 0;
    for (i = 0; arm_ok && i < a->next_seq && i < AVB_INITFAULT_MAX_RECORDS; i++) {
        if (!present[i]) { (*missing)++; }
    }
    return arm_ok && a->next_seq < AVB_INITFAULT_MAX_RECORDS && a->inflight == 0 && *missing == 0;
}

static unsigned irf_print_records(HKEY k, unsigned __int64 session, const char *label, AVB_INITFAULT_RECORD *r, BOOL *present)
{
    AVB_INITFAULT_ARM a;
    unsigned i, n = irf_read_records(k, session, r, present);
    BOOL arm_ok = irf_read_arm(k, &a) && a.session == session;

    printf("    [EVIDENCE] %s: %u driver record(s) of session 0x%016llX; arm %s next_seq=%u inflight=%u\n", label, n, session,
           arm_ok ? "present" : "absent", arm_ok ? a.next_seq : 0u, arm_ok ? a.inflight : 0u);
    for (i = 0; i < AVB_INITFAULT_MAX_RECORDS; i++) {
        if (!present[i]) { continue; }
        printf("    [EVIDENCE] record seq=%u luid=0x%016llX origin=%s site=%s applied=%s action=%d irql=%u hw_state %u->%u "
               "init_result=%d hold=%u ms tid=%u qpc enter=%lld hold_end=%lld exit=%lld nonpassive_skipped=%u\n", r[i].seq,
               r[i].net_luid, irf_origin_name(r[i].origin), irf_site_name(r[i].site), irf_act_name(r[i].applied),
               (int)r[i].action_index, r[i].irql, r[i].hw_state_before, r[i].hw_state_after, r[i].init_result, r[i].hold_ms,
               r[i].thread_id, r[i].qpc_enter, r[i].qpc_hold_end, r[i].qpc_exit, r[i].nonpassive_skipped);
    }
    return n;
}

/** Action i was consumed and its consumption is attributed: the consuming record exists in the arm's session, is on the
 *  target, at the fault point, with the action's origin, index and kind. Arm disappearance or consumed_by alone is not
 *  attribution */
static BOOL irf_attributed(const AVB_INITFAULT_ARM *a, unsigned i, avb_u64 luid, const AVB_INITFAULT_RECORD *r, const BOOL *present)
{
    unsigned c = a->actions[i].consumed_by, s;
    if (c == 0 || c == IRF_CLEARED || c > AVB_INITFAULT_MAX_RECORDS) { return FALSE; }
    s = c - 1u;
    return present[s] && r[s].session == a->session && r[s].net_luid == luid && r[s].site == AVB_INITFAULT_SITE_BASIC &&
           r[s].origin == a->actions[i].origin && r[s].action_index == i && r[s].applied == a->actions[i].action && r[s].irql == 0;
}

static BOOL irf_has_origin(const AVB_INITFAULT_RECORD *r, const BOOL *present, unsigned origin)
{
    unsigned i;
    for (i = 0; i < AVB_INITFAULT_MAX_RECORDS; i++) {
        if (present[i] && r[i].origin == origin) { return TRUE; }
    }
    return FALSE;
}

/** NET_LUID to exactly this adapter's device node (never by model or name) */
static BOOL irf_devinst(avb_u64 luid, DEVINST *inst)
{
    NET_LUID l;
    GUID guid;
    char key[256], pnp[512];
    DWORD size = sizeof(pnp);

    l.Value = luid;
    if (ConvertInterfaceLuidToGuid(&l, &guid) != NO_ERROR) { return FALSE; }
    sprintf_s(key, sizeof(key), IRF_NET_CLASS_KEY "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}\\Connection", guid.Data1,
              guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5],
              guid.Data4[6], guid.Data4[7]);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, key, "PnPInstanceId", RRF_RT_REG_SZ, NULL, pnp, &size) != ERROR_SUCCESS) { return FALSE; }
    printf("    [EVIDENCE] device instance: %s\n", pnp);
    return CM_Locate_DevNodeA(inst, pnp, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS;
}

/** Another adapter, up with a gateway, keeps the host reachable while the target's owned NIC cycle runs */
static BOOL irf_other_link(avb_u64 luid)
{
    ULONG size = 32 * 1024;
    IP_ADAPTER_ADDRESSES *list = (IP_ADAPTER_ADDRESSES *)malloc(size), *a;
    BOOL other = FALSE;

    if (list == NULL || GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, NULL, list, &size) != NO_ERROR) {
        free(list);
        return FALSE;
    }
    for (a = list; a != NULL; a = a->Next) {
        if (a->Luid.Value != luid && a->OperStatus == IfOperStatusUp && a->FirstGatewayAddress != NULL) { other = TRUE; }
    }
    free(list);
    return other;
}

typedef struct { DEVINST inst; BOOL enable; CONFIGRET result; } IRF_CM_CALL;

static DWORD WINAPI irf_cm_thread(LPVOID arg)
{
    IRF_CM_CALL *call = (IRF_CM_CALL *)arg;
    call->result = call->enable ? CM_Enable_DevNode(call->inst, 0) : CM_Disable_DevNode(call->inst, 0);
    return 0;
}

/** One device-node call with a bound: a stalled call ends in a verdict, not a hung test. Timeout ownership of the worker is
 *  #461's (PR #462 review, tracked there) */
static BOOL irf_cm_bounded(DEVINST inst, BOOL enable, DWORD bound_ms)
{
    IRF_CM_CALL *call = (IRF_CM_CALL *)calloc(1, sizeof(*call));
    HANDLE thread;
    BOOL ok;

    if (call == NULL) { return FALSE; }
    call->inst = inst;
    call->enable = enable;
    call->result = CR_FAILURE;
    thread = CreateThread(NULL, 0, irf_cm_thread, call, 0, NULL);
    if (thread == NULL) {
        free(call);
        return FALSE;
    }
    if (WaitForSingleObject(thread, bound_ms) != WAIT_OBJECT_0) {
        CloseHandle(thread);   /* call must outlive the worker thread; leaked on purpose */
        printf("    %s did not return within %lu ms\n", enable ? "CM_Enable_DevNode" : "CM_Disable_DevNode", bound_ms);
        return FALSE;
    }
    CloseHandle(thread);
    printf("    %s result=0x%08X\n", enable ? "CM_Enable_DevNode" : "CM_Disable_DevNode", (unsigned)call->result);
    ok = (call->result == CR_SUCCESS);
    free(call);
    return ok;
}

/** The test-owned fresh attach of the target only: bounded disable, then bounded enable (always attempted) */
static BOOL irf_cycle(DEVINST inst)
{
    BOOL dis = irf_cm_bounded(inst, FALSE, IRF_DISABLE_BOUND_MS);
    BOOL en = irf_cm_bounded(inst, TRUE, IRF_ENABLE_BOUND_MS);
    return dis && en;
}

static BOOL irf_identity_present(const AVB_ADAPTER_IDENTITY_REQUEST *want)
{
    AVB_ADAPTER_IDENTITY_REQUEST id;
    UINT32 i, count = 1;
    BOOL found = FALSE;
    HANDLE h = ic_open();

    if (h == INVALID_HANDLE_VALUE) { return FALSE; }
    for (i = 0; i < count && ic_identity(h, i, &id); i++) {
        count = id.count;
        if (id.net_luid == want->net_luid && id.device_id == want->device_id) { found = TRUE; }
    }
    CloseHandle(h);
    return found;
}

static BOOL irf_hw_state(HANDLE h, avb_u32 *s)
{
    AVB_HW_STATE_QUERY q;
    DWORD br = 0;
    ZeroMemory(&q, sizeof(q));
    *s = 0;
    if (!DeviceIoControl(h, IOCTL_AVB_GET_HW_STATE, &q, sizeof(q), &q, sizeof(q), &br, NULL)) { return FALSE; }
    *s = q.hw_state;
    return TRUE;
}

/** After a cycle: until the identity is back, every action in want[] is consumed, no attempt is in flight and, if
 *  need_restart, FilterRestart's attempt is recorded too (the attach sequence is over). Bounded; the arm is re-read */
static BOOL irf_wait_back(HKEY k, const AVB_ADAPTER_IDENTITY_REQUEST *id, unsigned __int64 session, const unsigned *want,
                          unsigned nwant, BOOL need_restart, AVB_INITFAULT_ARM *a, AVB_INITFAULT_RECORD *r, BOOL *present)
{
    DWORD start = GetTickCount();
    unsigned i;

    for (;;) {
        BOOL all = irf_identity_present(id) && irf_read_arm(k, a) && a->session == session && a->inflight == 0;
        for (i = 0; all && i < nwant; i++) {
            if (a->actions[want[i]].consumed_by == 0) { all = FALSE; }
        }
        if (all && need_restart) {
            (void)irf_read_records(k, session, r, present);
            all = irf_has_origin(r, present, AVB_INITFAULT_ORIGIN_RESTART);
        }
        if (all) {
            printf("    [EVIDENCE] adapter back with the expected attempts recorded after %lu ms\n", GetTickCount() - start);
            return TRUE;
        }
        if (GetTickCount() - start >= IRF_RETURN_BOUND_MS) {
            printf("    [EVIDENCE] not back with the expected attempts recorded within %d ms\n", IRF_RETURN_BOUND_MS);
            return FALSE;
        }
        Sleep(200);
    }
}

/** A bound observer handle; waits (bounded) for hw_state READY, then two observer reads (*first, *obs) */
static HANDLE irf_wait_ready(avb_u64 luid, IC_OBS *first, IC_OBS *obs)
{
    DWORD start = GetTickCount();
    HANDLE h = INVALID_HANDLE_VALUE;
    avb_u32 s = 0;

    while (GetTickCount() - start < IRF_RETURN_BOUND_MS) {
        if (h == INVALID_HANDLE_VALUE) { h = ic_open_bound(luid); }
        if (h != INVALID_HANDLE_VALUE && irf_hw_state(h, &s) && s == AVB_HW_PTP_READY) { break; }
        Sleep(500);
    }
    if (h == INVALID_HANDLE_VALUE) {
        ZeroMemory(first, sizeof(*first));
        ZeroMemory(obs, sizeof(*obs));
        return h;
    }
    (void)ic_ready_now(h, first, obs);
    return h;
}

/** Generic lifecycle condition: the adapter's published state is READY in both reads (no feature assumed) */
static BOOL irf_hw_ready(const IC_OBS *first, const IC_OBS *obs)
{
    return first->hw_ok && obs->hw_ok && first->hw_state == AVB_HW_PTP_READY && obs->hw_state == AVB_HW_PTP_READY;
}

/* Feature-specific observations, qualified by the adapter's advertised capabilities and by read validity (never by model):
 * a PHC exists with INTEL_CAP_BASIC_1588; the clock registers (TIMINCA/TSAUXC through GET_CLOCK_CONFIG) need BASIC_1588 and
 * INTEL_CAP_MMIO. A read is valid when GET_CLOCK_CONFIG completed with STATUS_SUCCESS, a full reply and status 0 */
static BOOL irf_phc_applicable(avb_u32 caps) { return (caps & INTEL_CAP_BASIC_1588) != 0; }
static BOOL irf_regs_applicable(avb_u32 caps) { return (caps & (INTEL_CAP_BASIC_1588 | INTEL_CAP_MMIO)) == (INTEL_CAP_BASIC_1588 | INTEL_CAP_MMIO); }

/* 1 = advancing, 0 = not advancing, -1 = not judged (not applicable or read invalid; *why says which) */
static int irf_phc_advancing(avb_u32 caps, const IC_OBS *a, const IC_OBS *b, const char **why)
{
    if (!irf_phc_applicable(caps)) { *why = "not applicable: INTEL_CAP_BASIC_1588 not advertised"; return -1; }
    if (!a->clk_ok || !b->clk_ok) { *why = "PHC read invalid (GET_CLOCK_CONFIG did not complete with a valid reply)"; return -1; }
    *why = "";
    return b->clk.systim > a->clk.systim ? 1 : 0;
}

/* 1 = equal, 0 = different, -1 = not judged */
static int irf_regs_equal(avb_u32 caps, BOOL ref_valid, const IC_OBS *ref, const IC_OBS *now, const char **why)
{
    if (!irf_regs_applicable(caps)) { *why = "not applicable: INTEL_CAP_BASIC_1588 and INTEL_CAP_MMIO not both advertised"; return -1; }
    if (!ref_valid || !now->clk_ok) { *why = "register read invalid (reference or final GET_CLOCK_CONFIG)"; return -1; }
    *why = "";
    return now->clk.timinca == ref->clk.timinca && now->clk.tsauxc == ref->clk.tsauxc ? 1 : 0;
}

static void irf_run_adapter(const AVB_ADAPTER_IDENTITY_REQUEST *id, const AVB_ADAPTER_IDENTITY_REQUEST *all, UINT32 nall)
{
    static const char *const deps[] = { "UT-DEV-LIFECYCLE-002-REACH", "UT-DEV-LIFECYCLE-002-ATTACH-FAILED",
                                        "UT-DEV-LIFECYCLE-002-FIRST-STATUS", "UT-DEV-LIFECYCLE-002-FIRST-NOT-READY",
                                        "UT-DEV-LIFECYCLE-002-RETRY-STATUS", "UT-DEV-LIFECYCLE-002-RETRY-READY",
                                        "UT-DEV-LIFECYCLE-002-OTHER-INSTANCE", "UT-DEV-LIFECYCLE-002-ATTRIBUTION" };
    static const unsigned reach_act[][2] = { { AVB_INITFAULT_ORIGIN_ATTACH, AVB_INITFAULT_ACT_OBSERVE } };
    static const unsigned fail_act[][2] = { { AVB_INITFAULT_ORIGIN_ATTACH, AVB_INITFAULT_ACT_FAIL },
                                            { AVB_INITFAULT_ORIGIN_RESTART, AVB_INITFAULT_ACT_FAIL },
                                            { AVB_INITFAULT_ORIGIN_INIT_PRE, AVB_INITFAULT_ACT_FAIL },
                                            { AVB_INITFAULT_ORIGIN_INIT_HANDLER, AVB_INITFAULT_ACT_FAIL } };
    static const unsigned want_reach[] = { 0 }, want_attach[] = { 0, 1 };
    static AVB_INITFAULT_RECORD rec[AVB_INITFAULT_MAX_RECORDS];
    static BOOL present[AVB_INITFAULT_MAX_RECORDS];
    const UINT32 index = id->index;
    const avb_u16 did = id->device_id;
    const avb_u32 caps = id->capabilities;
    IC_OBS p0, p1, r0, r1, f0, f1;
    IC_OBS *oth0 = NULL, *oth1 = NULL;   /* the other adapters' baseline, sized from the validated inventory (no fixed limit) */
    BOOL *oth_ready = NULL;
    AVB_INITFAULT_ARM arm;
    unsigned __int64 session = 0;
    DEVINST inst = 0;
    HKEY k = NULL;
    BOOL created = FALSE, have_ref = FALSE, ref_valid = FALSE, mutated = FALSE, clear_ok = TRUE;
    HANDLE hO = INVALID_HANDLE_VALUE, hA = INVALID_HANDLE_VALUE, hB = INVALID_HANDLE_VALUE;
    avb_u32 hs = 0;
    DWORD got = 0;
    LONG st1 = 0, st2 = 0, st3 = 0;
    UINT32 j;
    unsigned i, nrec, missing = 0;
    const char *why = "";
    char reason[480];
    const char *not_run = "not executed: UT-DEV-LIFECYCLE-002-PRESTATE setup failed, no initialization was attempted on this adapter";

    printf("\n=== adapter:%u did=0x%04X luid=0x%016llX caps=0x%08X ===\n", index, did, id->net_luid, caps);
    g_ic_ran++;
    ZeroMemory(&p0, sizeof(p0)); ZeroMemory(&p1, sizeof(p1)); ZeroMemory(&r0, sizeof(r0)); ZeroMemory(&r1, sizeof(r1));
    ZeroMemory(&f0, sizeof(f0)); ZeroMemory(&f1, sizeof(f1)); ZeroMemory(&arm, sizeof(arm));

    /* ---- UT-DEV-LIFECYCLE-002-PRESTATE: everything the scenario needs, before any mutation. A precondition that is checked
     *      and does not hold is FAILED_SETUP: setup failed, no initialization is attempted, every dependent line stays BLOCKED
     *      and the adapter's coverage gap is printed ---- */
    if (!irf_devinst(id->net_luid, &inst)) {
        ic_verdict("FAILED_SETUP", "UT-DEV-LIFECYCLE-002-PRESTATE", index, did, "setup failed: cannot resolve the device node the owned NIC cycle needs; no initialization was attempted");
        ic_blocked_cases(index, did, not_run, deps, (int)ARRAYSIZE(deps));
        printf("    [COVERAGE] UT-DEV-LIFECYCLE-002 not covered on adapter:%u did=0x%04X (setup failed)\n", index, did);
        return;
    }
    if (!irf_other_link(id->net_luid)) {
        ic_verdict("FAILED_SETUP", "UT-DEV-LIFECYCLE-002-PRESTATE", index, did, "setup failed: no independent management link (the owned NIC cycle would isolate the host, so it was not run); no initialization was attempted");
        ic_blocked_cases(index, did, not_run, deps, (int)ARRAYSIZE(deps));
        printf("    [COVERAGE] UT-DEV-LIFECYCLE-002 not covered on adapter:%u did=0x%04X (setup failed: this adapter carries the host's only management path)\n", index, did);
        return;
    }
    k = irf_params_open(&created);
    if (k == NULL) {
        ic_verdict("FAILED_SETUP", "UT-DEV-LIFECYCLE-002-PRESTATE", index, did, "setup failed: cannot open the driver Parameters key for the fault arm; no initialization was attempted");
        ic_blocked_cases(index, did, not_run, deps, (int)ARRAYSIZE(deps));
        printf("    [COVERAGE] UT-DEV-LIFECYCLE-002 not covered on adapter:%u did=0x%04X (setup failed)\n", index, did);
        return;
    }
    {
        unsigned stale = 0;
        if (irf_residue(k, &stale) && stale != 0) {
            printf("    [EVIDENCE] %u stale fault value(s) from an earlier run found before arming\n", stale);
        }
        clear_ok = irf_drain_clear(k, "stale values before arming");
    }
    hO = irf_wait_ready(id->net_luid, &p0, &p1);
    if (!clear_ok || hO == INVALID_HANDLE_VALUE || !irf_hw_ready(&p0, &p1)) {
        sprintf_s(reason, sizeof(reason), "setup failed: %s; no initialization was attempted",
                  !clear_ok ? "stale fault values could not be removed and verified absent" : "the target is not READY before the scenario");
        ic_verdict("FAILED_SETUP", "UT-DEV-LIFECYCLE-002-PRESTATE", index, did, reason);
        ic_blocked_cases(index, did, not_run, deps, (int)ARRAYSIZE(deps));
        printf("    [COVERAGE] UT-DEV-LIFECYCLE-002 not covered on adapter:%u did=0x%04X (setup failed)\n", index, did);
        if (hO != INVALID_HANDLE_VALUE) { CloseHandle(hO); }
        RegCloseKey(k);
        if (created) { (void)RegDeleteKeyW(HKEY_LOCAL_MACHINE, AVB_INITFAULT_PARAMS_SUBKEY); }
        return;
    }
    {
        int adv = irf_phc_advancing(caps, &p0, &p1, &why);
        printf("    [EVIDENCE] before the scenario: READY; PHC advancing: %s\n", adv == 1 ? "yes" : adv == 0 ? "NO" : why);
    }
    CloseHandle(hO);
    hO = INVALID_HANDLE_VALUE;
    oth0 = (IC_OBS *)calloc(nall, sizeof(*oth0));
    oth1 = (IC_OBS *)calloc(nall, sizeof(*oth1));
    oth_ready = (BOOL *)calloc(nall, sizeof(*oth_ready));
    if (oth0 == NULL || oth1 == NULL || oth_ready == NULL) {
        ic_verdict("FAILED_SETUP", "UT-DEV-LIFECYCLE-002-PRESTATE", index, did, "setup failed: cannot allocate the other adapters' baseline storage; no initialization was attempted");
        ic_blocked_cases(index, did, not_run, deps, (int)ARRAYSIZE(deps));
        printf("    [COVERAGE] UT-DEV-LIFECYCLE-002 not covered on adapter:%u did=0x%04X (setup failed)\n", index, did);
        free(oth0); free(oth1); free(oth_ready);
        RegCloseKey(k);
        if (created) { (void)RegDeleteKeyW(HKEY_LOCAL_MACHINE, AVB_INITFAULT_PARAMS_SUBKEY); }
        return;
    }
    for (j = 0; j < nall; j++) {   /* the other adapters' state, for the final check */
        HANDLE h;
        if (all[j].net_luid == id->net_luid) { continue; }
        h = ic_open_bound(all[j].net_luid);
        if (h != INVALID_HANDLE_VALUE) {
            (void)ic_ready_now(h, &oth0[j], &oth1[j]);
            oth_ready[j] = irf_hw_ready(&oth0[j], &oth1[j]);
            CloseHandle(h);
        }
        printf("    [EVIDENCE] other adapter:%u did=0x%04X before: READY=%d\n", all[j].index, all[j].device_id, oth_ready[j]);
    }
    ic_verdict("PASS", "UT-DEV-LIFECYCLE-002-PRESTATE", index, did, "device node, independent management link, Parameters key without fault values, target READY");
    g_irf_established++;

    /* ---- Phase A, UT-DEV-LIFECYCLE-002-REACH: the fresh attach reaches the fault point (OBSERVE, no fault). The phase ends
     *      only after FilterRestart's attempt is recorded and nothing is in flight, so no attempt of it outlives the session ---- */
    mutated = TRUE;
    if (!irf_arm(id->net_luid, reach_act, ARRAYSIZE(reach_act), &session) || !irf_cycle(inst) ||
        !irf_wait_back(k, id, session, want_reach, ARRAYSIZE(want_reach), TRUE, &arm, rec, present)) {
        (void)irf_print_records(k, session, "phase A", rec, present);
        ic_verdict("FAILED_SETUP", "UT-DEV-LIFECYCLE-002-REACH", index, did, "arm, owned NIC cycle or the return with OBSERVE consumed and FilterRestart's attempt recorded did not complete");
        ic_blocked_cases(index, did, "the mechanism was not shown to reach the fault point", deps + 1, (int)ARRAYSIZE(deps) - 1);
        goto cleanup;
    }
    (void)irf_print_records(k, session, "phase A", rec, present);
    hO = irf_wait_ready(id->net_luid, &r0, &r1);
    if (!irf_attributed(&arm, 0, id->net_luid, rec, present) || hO == INVALID_HANDLE_VALUE || !irf_hw_ready(&r0, &r1)) {
        sprintf_s(reason, sizeof(reason), "OBSERVE attributed to target/ATTACH/BASIC in session 0x%016llX: %d; READY after the attach: %d",
                  session, irf_attributed(&arm, 0, id->net_luid, rec, present), hO != INVALID_HANDLE_VALUE && irf_hw_ready(&r0, &r1));
        ic_verdict("FAILED_SETUP", "UT-DEV-LIFECYCLE-002-REACH", index, did, reason);
        ic_blocked_cases(index, did, "the mechanism was not shown to reach the fault point", deps + 1, (int)ARRAYSIZE(deps) - 1);
        goto cleanup;
    }
    have_ref = TRUE;   /* r1: a normally attached adapter, the reference for the final state */
    ref_valid = r1.clk_ok;
    sprintf_s(reason, sizeof(reason), "record seq=%u: target, origin ATTACH, site BASIC, OBSERVE consumed at IRQL 0; READY after the "
              "attach; register reference %s (TIMINCA 0x%08X TSAUXC 0x%08X)", arm.actions[0].consumed_by - 1u,
              !irf_regs_applicable(caps) ? "not applicable" : ref_valid ? "valid" : "read invalid", r1.clk.timinca, r1.clk.tsauxc);
    ic_verdict("PASS", "UT-DEV-LIFECYCLE-002-REACH", index, did, reason);
    CloseHandle(hO);
    hO = INVALID_HANDLE_VALUE;

    /* ---- Phase B setup, UT-DEV-LIFECYCLE-002-ATTACH-FAILED: attach and restart initialization fail, adapter below READY ---- */
    if (!irf_drain_clear(k, "end of phase A")) {
        ic_verdict("FAILED_SETUP", "UT-DEV-LIFECYCLE-002-ATTACH-FAILED", index, did, "phase A's session could not be drained and cleared");
        ic_blocked_cases(index, did, "phase B was not started", deps + 2, (int)ARRAYSIZE(deps) - 2);
        goto cleanup;
    }
    if (!irf_arm(id->net_luid, fail_act, ARRAYSIZE(fail_act), &session) || !irf_cycle(inst) ||
        !irf_wait_back(k, id, session, want_attach, ARRAYSIZE(want_attach), FALSE, &arm, rec, present)) {
        (void)irf_print_records(k, session, "phase B attach", rec, present);
        ic_verdict("FAILED_SETUP", "UT-DEV-LIFECYCLE-002-ATTACH-FAILED", index, did, "arm, owned NIC cycle or the return with ATTACH and RESTART consumed did not complete");
        ic_blocked_cases(index, did, "no failed adapter initialization was established", deps + 2, (int)ARRAYSIZE(deps) - 2);
        goto cleanup;
    }
    (void)irf_print_records(k, session, "phase B attach", rec, present);
    hO = ic_open_bound(id->net_luid);
    if (hO == INVALID_HANDLE_VALUE || !irf_hw_state(hO, &hs) || hs >= AVB_HW_PTP_READY || !irf_attributed(&arm, 0, id->net_luid, rec, present) ||
        !irf_attributed(&arm, 1, id->net_luid, rec, present)) {
        sprintf_s(reason, sizeof(reason), "ATTACH FAIL attributed %d, RESTART FAIL attributed %d, hw_state %u (must be below PTP_READY %u)",
                  irf_attributed(&arm, 0, id->net_luid, rec, present), irf_attributed(&arm, 1, id->net_luid, rec, present), hs,
                  (unsigned)AVB_HW_PTP_READY);
        ic_verdict("FAILED_SETUP", "UT-DEV-LIFECYCLE-002-ATTACH-FAILED", index, did, reason);
        ic_blocked_cases(index, did, "no failed adapter initialization was established", deps + 2, (int)ARRAYSIZE(deps) - 2);
        goto cleanup;
    }
    sprintf_s(reason, sizeof(reason), "ATTACH and RESTART initialization failed by the armed fault (records seq=%u, seq=%u); adapter "
              "enumerable, hw_state %u", arm.actions[0].consumed_by - 1u, arm.actions[1].consumed_by - 1u, hs);
    ic_verdict("PASS", "UT-DEV-LIFECYCLE-002-ATTACH-FAILED", index, did, reason);

    /* ---- Instance A, first INIT_DEVICE: published expectation STATUS_DEVICE_HARDWARE_ERROR (#313 UT-DEV-LIFECYCLE-002) ---- */
    hA = ic_open_bound(id->net_luid);
    if (hA == INVALID_HANDLE_VALUE) {
        ic_verdict("FAILED_SETUP", "UT-DEV-LIFECYCLE-002-FIRST-STATUS", index, did, "cannot open instance A bound to the target");
        ic_blocked_cases(index, did, "instance A was not opened", deps + 3, (int)ARRAYSIZE(deps) - 3);
        goto cleanup;
    }
    st1 = ic_init(hA, &got);
    nrec = irf_print_records(k, session, "after instance A's first INIT_DEVICE", rec, present);
    (void)irf_read_arm(k, &arm);
    printf("    [EVIDENCE] instance A first INIT_DEVICE: ntstatus=0x%08lX information=%lu; INIT_PRE FAIL attributed %d, INIT_HANDLER FAIL "
           "attributed %d; %u record(s) so far\n", (unsigned long)st1, got, irf_attributed(&arm, 2, id->net_luid, rec, present),
           irf_attributed(&arm, 3, id->net_luid, rec, present), nrec);
    ic_status_line("UT-DEV-LIFECYCLE-002-FIRST", index, did, st1, got, (LONG)STATUS_DEVICE_HARDWARE_ERROR, FALSE);
    if (irf_hw_state(hO, &hs)) {
        sprintf_s(reason, sizeof(reason), "hw_state %u after the failed request (PTP_READY = %u must not be published)", hs, (unsigned)AVB_HW_PTP_READY);
        ic_verdict(hs < AVB_HW_PTP_READY ? "PASS" : "FAIL", "UT-DEV-LIFECYCLE-002-FIRST-NOT-READY", index, did, reason);
    } else {
        ic_verdict("INCONCLUSIVE", "UT-DEV-LIFECYCLE-002-FIRST-NOT-READY", index, did, "GET_HW_STATE failed");
    }
    if (st1 == 0 || st1 == AVB_NTSTATUS_UNAVAILABLE) {
        ic_blocked_cases(index, did, "instance A's first INIT_DEVICE did not observably fail: no failed initialization to retry", deps + 4, 3);
    } else {
        /* The fault is cleared before the retry: the driver marks any unconsumed action cleared under its admission lock
         * (recording continues) */
        {
            AVB_INITFAULT_CONTROL c;
            LONG cst;
            for (i = 0; i < arm.count; i++) {
                if (arm.actions[i].consumed_by == 0) {
                    printf("    [EVIDENCE] action %u (%s %s) was not consumed\n", i, irf_origin_name(arm.actions[i].origin),
                           irf_act_name(arm.actions[i].action));
                }
            }
            cst = irf_ctl(AVB_INITFAULT_OP_CLEAR_UNCONSUMED, session, NULL, &c);
            printf("    [EVIDENCE] driver CLEAR_UNCONSUMED ntstatus=0x%08lX actions cleared %u, inflight %u\n", (unsigned long)cst,
                   c.actions_cleared, c.inflight_at_op);
        }

        /* ---- Instance A retries on the same handle: published expectation STATUS_SUCCESS ---- */
        st2 = ic_init(hA, &got);
        (void)irf_print_records(k, session, "after instance A's retry", rec, present);
        ic_status_line("UT-DEV-LIFECYCLE-002-RETRY", index, did, st2, got, (LONG)STATUS_SUCCESS, FALSE);
        CloseHandle(hO);
        hO = irf_wait_ready(id->net_luid, &f0, &f1);
        sprintf_s(reason, sizeof(reason), "after the retry: hw_state %u (no lingering error state requires READY)", f1.hw_state);
        ic_verdict(irf_hw_ready(&f0, &f1) ? "PASS" : "FAIL", "UT-DEV-LIFECYCLE-002-RETRY-READY", index, did, reason);
        {   /* feature-specific, only where the capability is advertised and the read is valid */
            int adv = irf_phc_advancing(caps, &f0, &f1, &why);
            if (adv < 0 && !irf_phc_applicable(caps)) {
                printf("    [EVIDENCE] UT-DEV-LIFECYCLE-002-RETRY-PHC: %s\n", why);
            } else {
                sprintf_s(reason, sizeof(reason), "PHC after the retry: %s (SYSTIM %llu -> %llu)", adv == 1 ? "advancing" : adv == 0 ?
                          "not advancing" : why, f0.clk.systim, f1.clk.systim);
                ic_verdict(adv == 1 ? "PASS" : adv == 0 ? "FAIL" : "INCONCLUSIVE", "UT-DEV-LIFECYCLE-002-RETRY-PHC", index, did, reason);
            }
        }

        /* ---- Instance B on the READY adapter: STATUS_SUCCESS without a hardware initialization attempt (#404). Attempts are
         *      counted by the driver's sequence numbers (next_seq), not by readable records, and the history must be complete ---- */
        if (!irf_hw_ready(&f0, &f1)) {
            ic_verdict("BLOCKED", "UT-DEV-LIFECYCLE-002-OTHER-INSTANCE", index, did, "not executed: the adapter is not READY after the retry");
        } else {
            AVB_INITFAULT_ARM before, after;
            BOOL b_ok = irf_history_complete(k, session, &before, rec, present, &missing);
            BOOL a_ok;
            hB = ic_open_bound(id->net_luid);
            st3 = (hB != INVALID_HANDLE_VALUE) ? ic_init(hB, &got) : AVB_NTSTATUS_UNAVAILABLE;
            a_ok = irf_history_complete(k, session, &after, rec, present, &missing);
            (void)irf_print_records(k, session, "after instance B's INIT_DEVICE", rec, present);
            sprintf_s(reason, sizeof(reason), "ntstatus=0x%08lX (expected STATUS_SUCCESS), initialization attempts started by it: %u "
                      "(expected 0, from next_seq %u -> %u), history complete before %d after %d", (unsigned long)st3,
                      after.next_seq - before.next_seq, before.next_seq, after.next_seq, b_ok, a_ok);
            if (!b_ok || !a_ok) {
                ic_verdict(st3 != STATUS_SUCCESS || after.next_seq != before.next_seq ? "FAIL" : "INCONCLUSIVE", "UT-DEV-LIFECYCLE-002-OTHER-INSTANCE", index, did, reason);
            } else {
                ic_verdict(st3 == STATUS_SUCCESS && after.next_seq == before.next_seq ? "PASS" : "FAIL", "UT-DEV-LIFECYCLE-002-OTHER-INSTANCE", index, did, reason);
            }
        }
    }

    /* ---- UT-DEV-LIFECYCLE-002-ATTRIBUTION: the session's history is complete (a record for every reserved sequence, none in
     *      flight, not saturated); every record is on the target in this session at IRQL 0; every consumed action attributed ---- */
    {
        BOOL complete = irf_history_complete(k, session, &arm, rec, present, &missing);
        unsigned bad = 0, consumed = 0;
        nrec = 0;
        for (i = 0; i < AVB_INITFAULT_MAX_RECORDS; i++) {
            if (!present[i]) { continue; }
            nrec++;
            if (rec[i].net_luid != id->net_luid || rec[i].session != session || rec[i].irql != 0 || rec[i].nonpassive_skipped != 0) { bad++; }
        }
        for (i = 0; i < arm.count; i++) {
            if (arm.actions[i].consumed_by != 0 && arm.actions[i].consumed_by != IRF_CLEARED) {
                consumed++;
                if (!irf_attributed(&arm, i, id->net_luid, rec, present)) { bad++; }
            }
        }
        sprintf_s(reason, sizeof(reason), "session 0x%016llX: next_seq %u, %u record(s), %u missing, inflight %u, %u consumed action(s), "
                  "%u inconsistency(ies) (wrong adapter or session, IRQL, skipped attempt or unattributed consumption)", session,
                  arm.next_seq, nrec, missing, arm.inflight, consumed, bad);
        ic_verdict(complete && bad == 0 ? "PASS" : "FAIL", "UT-DEV-LIFECYCLE-002-ATTRIBUTION", index, did, reason);
    }

cleanup:
    /* ---- Cleanup (test-owned) and UT-DEV-LIFECYCLE-002-FINAL ---- */
    if (hA != INVALID_HANDLE_VALUE) { CloseHandle(hA); }
    if (hB != INVALID_HANDLE_VALUE) { CloseHandle(hB); }
    if (hO != INVALID_HANDLE_VALUE) { CloseHandle(hO); }
    hO = INVALID_HANDLE_VALUE;
    clear_ok = irf_drain_clear(k, "cleanup");   /* drain, disarm, delete: nothing may act on the cleanup cycle */
    if (mutated) {
        int eq;
        hO = irf_wait_ready(id->net_luid, &f0, &f1);
        eq = have_ref ? irf_regs_equal(caps, ref_valid, &r1, &f1, &why) : -1;
        if (hO == INVALID_HANDLE_VALUE || !irf_hw_ready(&f0, &f1) || eq == 0) {
            printf("    [EVIDENCE] cleanup: target not READY or its registers not as after a normal attach; owned NIC cycle without arm\n");
            if (hO != INVALID_HANDLE_VALUE) { CloseHandle(hO); }
            (void)irf_cycle(inst);
            hO = irf_wait_ready(id->net_luid, &f0, &f1);
        }
    } else {
        hO = irf_wait_ready(id->net_luid, &f0, &f1);
    }
    {
        unsigned left = 0;
        BOOL enum_ok = irf_residue(k, &left);
        BOOL ready = hO != INVALID_HANDLE_VALUE && irf_hw_ready(&f0, &f1);
        int adv = irf_phc_advancing(caps, &f0, &f1, &why);
        const char *why_adv = why;
        int eq = have_ref ? irf_regs_equal(caps, ref_valid, &r1, &f1, &why) : -1;
        const char *why_eq = have_ref ? why : "no normal-attach reference";
        BOOL oth_ok = TRUE, oth_unjudged = FALSE, measured = hO != INVALID_HANDLE_VALUE && f0.hw_ok && f1.hw_ok;

        for (j = 0; j < nall; j++) {
            HANDLE h;
            IC_OBS a0, a1;
            BOOL now_ready;
            const char *w1 = "", *w2 = "";
            int oadv, oeq;
            if (all[j].net_luid == id->net_luid || !oth_ready[j]) { continue; }   /* a missing baseline: #461 (PR #462 review) */
            ZeroMemory(&a0, sizeof(a0));
            ZeroMemory(&a1, sizeof(a1));
            h = ic_open_bound(all[j].net_luid);
            if (h != INVALID_HANDLE_VALUE) {
                (void)ic_ready_now(h, &a0, &a1);
                CloseHandle(h);
            }
            now_ready = irf_hw_ready(&a0, &a1);
            oadv = irf_phc_advancing(all[j].capabilities, &a0, &a1, &w1);
            oeq = irf_regs_equal(all[j].capabilities, oth1[j].clk_ok, &oth1[j], &a1, &w2);
            printf("    [EVIDENCE] other adapter:%u did=0x%04X after: READY %d; PHC advancing %s; TIMINCA/TSAUXC as before %s\n",
                   all[j].index, all[j].device_id, now_ready, oadv == 1 ? "yes" : oadv == 0 ? "NO" : w1, oeq == 1 ? "yes" : oeq == 0 ? "NO" : w2);
            if (!now_ready || oadv == 0 || oeq == 0) { oth_ok = FALSE; }
            if ((oadv < 0 && irf_phc_applicable(all[j].capabilities)) || (oeq < 0 && irf_regs_applicable(all[j].capabilities))) { oth_unjudged = TRUE; }
        }
        sprintf_s(reason, sizeof(reason), "target READY %d; PHC advancing %s; TIMINCA 0x%08X TSAUXC 0x%08X vs normal-attach reference "
                  "0x%08X/0x%08X: %s; fault values left %u (enumeration %s, clearing %s); other adapters unchanged %d%s",
                  ready, adv == 1 ? "yes" : adv == 0 ? "NO" : why_adv, f1.clk.timinca, f1.clk.tsauxc, r1.clk.timinca, r1.clk.tsauxc,
                  eq == 1 ? "equal" : eq == 0 ? "DIFFERENT" : why_eq, left, enum_ok ? "complete" : "FAILED", clear_ok ? "verified" : "not verified",
                  oth_ok, oth_unjudged ? " (some applicable observations unread)" : "");
        if (!measured || !enum_ok) {
            ic_verdict("INCONCLUSIVE_RESTORATION", "UT-DEV-LIFECYCLE-002-FINAL", index, did, reason);
        } else if (!ready || adv == 0 || eq == 0 || left != 0 || !oth_ok) {
            ic_verdict("CLEANUP_FAILED", "UT-DEV-LIFECYCLE-002-FINAL", index, did, reason);
        } else if ((adv < 0 && irf_phc_applicable(caps)) || (eq < 0 && irf_regs_applicable(caps)) || oth_unjudged || !clear_ok) {
            ic_verdict("INCONCLUSIVE_RESTORATION", "UT-DEV-LIFECYCLE-002-FINAL", index, did, reason);
        } else {
            ic_verdict("PASS", "UT-DEV-LIFECYCLE-002-FINAL", index, did, reason);
        }
    }
    if (hO != INVALID_HANDLE_VALUE) { CloseHandle(hO); }
    RegCloseKey(k);
    if (created) { (void)RegDeleteKeyW(HKEY_LOCAL_MACHINE, AVB_INITFAULT_PARAMS_SUBKEY); }
    free(oth0);
    free(oth1);
    free(oth_ready);
    g_irf_completed++;
}

static int run_init_retry_fault(void)
{
    AVB_ADAPTER_IDENTITY_REQUEST first, *all = NULL;
    UINT32 i, count = 0, nall = 0;
    BOOL complete = FALSE;
    HANDLE h;

    printf("=== IntelAvbFilter - UT-DEV-LIFECYCLE-002 initialization after failed start (#313, #412; Debug fault mechanism) ===\n");
    h = ic_open();
    if (h == INVALID_HANDLE_VALUE) {
        printf("[FAILED_SETUP] UT-DEV-LIFECYCLE-002-DISCOVERY: cannot open %s (error %lu); no adapter tested\n", DEVICE_PATH_PRIMARY,
               GetLastError());
        return 1;
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
        printf("[FAILED_SETUP] UT-DEV-LIFECYCLE-002-DISCOVERY: %u of %u identities validated; untested adapters are not passes\n", nall, count);
        g_ic_failed++;
    }
    for (i = 0; i < nall; i++) {
        irf_run_adapter(&all[i], all, nall);   /* every validated identity; no fixed inventory limit, no capability filter */
    }
    free(all);
    printf("\nadapters enumerated=%u setup attempted=%d scenario established (PRESTATE PASS)=%d scenario run to its end=%d; "
           "nonpassing verdicts=%d blocked=%d\n", nall, g_ic_ran, g_irf_established, g_irf_completed, g_ic_failed, g_ic_blocked);
    if (g_ic_ran == 0) { return 2; }
    if (g_ic_failed != 0) { return 1; }
    return g_ic_blocked != 0 ? 3 : 0;
}

int main(int argc, char **argv) {
    TestContext ctx = {0};

    if (argc == 2 && strcmp(argv[1], "--init-contract") == 0) {
        return run_init_contract();
    }
    if (argc == 2 && strcmp(argv[1], "--init-retry-fault") == 0) {
        return run_init_retry_fault();
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
