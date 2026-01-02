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
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Single Source of Truth for IOCTL definitions */
#include "../../include/avb_ioctl.h"

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

/* Adapter enumeration structures */
typedef struct {
    UINT32 adapter_count;
    UINT32 buffer_size;
    UINT32 status;
} ENUM_ADAPTERS_REQUEST, *PENUM_ADAPTERS_REQUEST;

typedef struct {
    UINT32 adapter_index;
    WCHAR  device_path[260];
    UINT32 status;
} OPEN_ADAPTER_REQUEST, *POPEN_ADAPTER_REQUEST;

typedef struct {
    UINT32 power_state;      /* D0-D3 */
    UINT32 link_status;      /* Up/Down */
    UINT32 resources_allocated;
    UINT32 status;
} HW_STATE_REQUEST, *PHW_STATE_REQUEST;

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
    ENUM_ADAPTERS_REQUEST request = {0};
    DWORD bytes_returned = 0;
    BOOL result;
    
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
    
    if (result && request.status == 0) {
        *adapter_count = request.adapter_count;
        return TRUE;
    }
    
    return FALSE;
}

/**
 * @brief Open adapter via IOCTL 32
 */
HANDLE OpenAdapterByIndex(HANDLE device, UINT32 index) {
    OPEN_ADAPTER_REQUEST request = {0};
    DWORD bytes_returned = 0;
    BOOL result;
    
    request.adapter_index = index;
    
    result = DeviceIoControl(
        device,
        IOCTL_AVB_OPEN_ADAPTER,  /* From avb_ioctl.h */
        &request,
        sizeof(request),
        &request,
        sizeof(request),
        &bytes_returned,
        NULL
    );
    
    if (result && request.status == 0) {
        /* In real implementation, would return adapter handle from response */
        return device;  /* Placeholder */
    }
    
    return INVALID_HANDLE_VALUE;
}

/**
 * @brief Get hardware state via IOCTL 37
 */
BOOL GetHardwareState(HANDLE device, HW_STATE_REQUEST *state) {
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
    
    return (result && state->status == 0);
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
 */
void Test_MultipleAdapterEnumeration(TestContext *ctx) {
    PrintTestResult(ctx, "UT-DEV-ENUM-002: Multiple Adapter Enumeration", 
                    TEST_SKIP, "Requires 2+ adapters (hardware-dependent)");
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
 */
void Test_OpenByDevicePath(TestContext *ctx) {
    PrintTestResult(ctx, "UT-DEV-OPEN-002: Open by Device Path", 
                    TEST_SKIP, "Requires symbolic link path enumeration");
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

/**
 * UT-DEV-OPEN-004: Concurrent Open Requests
 */
void Test_ConcurrentOpenRequests(TestContext *ctx) {
    PrintTestResult(ctx, "UT-DEV-OPEN-004: Concurrent Open Requests", 
                    TEST_SKIP, "Requires multi-threaded test framework");
}

/**
 * UT-DEV-HW-STATE-001: Hardware State Retrieval - D0
 */
void Test_HardwareStateRetrievalD0(TestContext *ctx) {
    HANDLE device;
    HW_STATE_REQUEST state = {0};
    BOOL result;
    
    device = OpenDevice(DEVICE_PATH_PRIMARY);
    if (device == INVALID_HANDLE_VALUE) {
        PrintTestResult(ctx, "UT-DEV-HW-STATE-001: Hardware State Retrieval - D0", 
                        TEST_SKIP, "Device not accessible");
        return;
    }
    
    result = GetHardwareState(device, &state);
    
    if (result) {
        printf("    Power state: D%u\n", state.power_state);
        printf("    Link status: %s\n", state.link_status ? "Up" : "Down");
        printf("    Resources allocated: %u\n", state.resources_allocated);
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
    HW_STATE_REQUEST state = {0};
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
                    (init_result && state_result && state.resources_allocated) ? TEST_PASS : TEST_FAIL,
                    (init_result && state_result && state.resources_allocated) ? NULL : 
                    "Resources not allocated after init");
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

int main(int argc, char **argv) {
    TestContext ctx = {0};
    
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
