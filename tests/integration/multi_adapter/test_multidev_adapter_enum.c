/**
 * @file test_multidev_adapter_enum.c
 * @brief Test suite for multi-adapter enumeration and selection
 * 
 * Verifies: #15 (REQ-F-MULTIDEV-001: Multi-Adapter Management and Selection)
 * Test Type: Integration
 * Priority: P0 (Critical)
 * 
 * Acceptance Criteria (from #15):
 *   Given a system with N Intel Ethernet controllers
 *   When user calls IOCTL_AVB_ENUM_ADAPTERS with index=i where 0 <= i < N
 *   Then driver returns:
 *     - Total count of Intel adapters (count=N)
 *     - Vendor ID (0x8086)
 *     - Device ID (e.g., 0x15F2 for I225, 0x15B7 for I210)
 *     - Capability bitmask (PTP, QAV, TAS, FP support)
 * 
 * @see https://github.com/zarfld/IntelAvbFilter/issues/15
 */

#include <windows.h>
#include <stdio.h>
#include <assert.h>
#include "../../../include/avb_ioctl.h"

// Status codes (from NDIS but redefined for user-mode)
#define AVB_STATUS_SUCCESS 0x00000000

// Test framework macros
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("❌ FAILED: %s\n", message); \
            printf("   Line %d: %s\n", __LINE__, #condition); \
            return FALSE; \
        } \
        printf("✅ PASS: %s\n", message); \
    } while(0)

#define TEST_ASSERT_EQUAL(expected, actual, message) \
    do { \
        if ((expected) != (actual)) { \
            printf("❌ FAILED: %s\n", message); \
            printf("   Expected: 0x%08X, Got: 0x%08X\n", (UINT32)(expected), (UINT32)(actual)); \
            return FALSE; \
        } \
        printf("✅ PASS: %s\n", message); \
    } while(0)

// Device path to IntelAvbFilter control device
#define DEVICE_PATH "\\\\.\\IntelAvbFilter"

/**
 * Test Case: REQ-F-MULTIDEV-001.1 - Adapter Enumeration
 * 
 * Scenario: Enumerate all Intel adapters in system
 *   Given system has 1 or more Intel NICs
 *   When application calls IOCTL_AVB_ENUM_ADAPTERS with index=0
 *   Then driver returns count>=1, VID=0x8086, valid DID
 */
BOOLEAN Test_EnumerateAdapters_FirstAdapter()
{
    printf("\n=== Test: Enumerate First Adapter ===\n");
    
    HANDLE hDevice = CreateFile(
        DEVICE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    TEST_ASSERT(hDevice != INVALID_HANDLE_VALUE, 
                "Open device " DEVICE_PATH);
    
    AVB_ENUM_REQUEST req = {0};
    req.index = 0; // Request first adapter
    
    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_ENUM_ADAPTERS,
        &req,
        sizeof(req),
        &req,
        sizeof(req),
        &bytesReturned,
        NULL
    );
    
    TEST_ASSERT(result != FALSE, "IOCTL_AVB_ENUM_ADAPTERS succeeds");
    TEST_ASSERT(bytesReturned == sizeof(AVB_ENUM_REQUEST), 
                "Returns correct buffer size");
    
    printf("   Adapter Count: %u\n", req.count);
    printf("   Vendor ID: 0x%04X\n", req.vendor_id);
    printf("   Device ID: 0x%04X\n", req.device_id);
    printf("   Capabilities: 0x%08X\n", req.capabilities);
    
    TEST_ASSERT(req.count >= 1, "At least one Intel adapter found");
    TEST_ASSERT_EQUAL(0x8086, req.vendor_id, "Vendor ID is Intel (0x8086)");
    
    // Valid Intel I210/I225/I226/I350 device IDs
    BOOLEAN validDeviceId = 
        (req.device_id == 0x125B) || // I350
        (req.device_id == 0x1521) || // I350
        (req.device_id == 0x15B7) || // I210
        (req.device_id == 0x15B8) || // I210
        (req.device_id == 0x15F2) || // I225
        (req.device_id == 0x15F3) || // I225
        (req.device_id == 0x15F6) || // I226
        (req.device_id == 0x15F7) || // I226
        (req.device_id == 0x153A) || // I217/I219
        (req.device_id == 0x15B9);   // I219
    
    TEST_ASSERT(validDeviceId, "Device ID is valid Intel NIC");
    TEST_ASSERT_EQUAL(AVB_STATUS_SUCCESS, req.status, "Status is success");
    
    CloseHandle(hDevice);
    return TRUE;
}

/**
 * Test Case: REQ-F-MULTIDEV-001.1 - Enumerate all adapters
 * 
 * Scenario: Iterate through all adapters
 *   Given system has N Intel NICs
 *   When application enumerates index 0 to N-1
 *   Then driver returns same count=N for all queries
 */
BOOLEAN Test_EnumerateAdapters_AllAdapters()
{
    printf("\n=== Test: Enumerate All Adapters ===\n");
    
    HANDLE hDevice = CreateFile(
        DEVICE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    TEST_ASSERT(hDevice != INVALID_HANDLE_VALUE, "Open device");
    
    // First query to get total count
    AVB_ENUM_REQUEST req = {0};
    req.index = 0;
    
    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_ENUM_ADAPTERS,
        &req,
        sizeof(req),
        &req,
        sizeof(req),
        &bytesReturned,
        NULL
    );
    
    TEST_ASSERT(result != FALSE, "First enumeration succeeds");
    
    UINT32 totalAdapters = req.count;
    printf("   Total adapters: %u\n", totalAdapters);
    
    // Enumerate all adapters
    for (UINT32 i = 0; i < totalAdapters; i++)
    {
        req.index = i;
        req.count = 0;
        req.vendor_id = 0;
        req.device_id = 0;
        req.capabilities = 0;
        
        result = DeviceIoControl(
            hDevice,
            IOCTL_AVB_ENUM_ADAPTERS,
            &req,
            sizeof(req),
            &req,
            sizeof(req),
            &bytesReturned,
            NULL
        );
        
        TEST_ASSERT(result != FALSE, "Enumeration succeeds for each index");
        TEST_ASSERT_EQUAL(totalAdapters, req.count, 
                         "Count remains consistent across queries");
        TEST_ASSERT_EQUAL(0x8086, req.vendor_id, "Vendor ID is Intel");
        
        printf("   Adapter[%u]: VID=0x%04X DID=0x%04X Caps=0x%08X\n",
               i, req.vendor_id, req.device_id, req.capabilities);
    }
    
    CloseHandle(hDevice);
    return TRUE;
}

/**
 * Test Case: REQ-F-MULTIDEV-001.1 - Out of bounds index
 * 
 * Scenario: Request adapter beyond available count
 *   Given system has N adapters
 *   When application calls with index >= N
 *   Then driver returns error
 */
BOOLEAN Test_EnumerateAdapters_OutOfBounds()
{
    printf("\n=== Test: Out of Bounds Index ===\n");
    
    HANDLE hDevice = CreateFile(
        DEVICE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    TEST_ASSERT(hDevice != INVALID_HANDLE_VALUE, "Open device");
    
    // Get total count first
    AVB_ENUM_REQUEST req = {0};
    req.index = 0;
    
    DWORD bytesReturned = 0;
    DeviceIoControl(
        hDevice,
        IOCTL_AVB_ENUM_ADAPTERS,
        &req,
        sizeof(req),
        &req,
        sizeof(req),
        &bytesReturned,
        NULL
    );
    
    UINT32 totalAdapters = req.count;
    
    // Try index beyond count
    req.index = totalAdapters + 10;
    req.count = 0;
    req.vendor_id = 0;
    req.device_id = 0;
    
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_ENUM_ADAPTERS,
        &req,
        sizeof(req),
        &req,
        sizeof(req),
        &bytesReturned,
        NULL
    );
    
    TEST_ASSERT(result == FALSE, "Out of bounds request fails");
    TEST_ASSERT(GetLastError() == ERROR_NO_MORE_ITEMS || 
                GetLastError() == ERROR_INVALID_PARAMETER,
                "Returns appropriate error code");
    
    CloseHandle(hDevice);
    return TRUE;
}

/**
 * Test Case: REQ-F-MULTIDEV-001.2 - Adapter Selection
 * 
 * Scenario: Open specific adapter by VID/DID
 *   Given system has enumerated adapters
 *   When application calls IOCTL_AVB_OPEN_ADAPTER with specific VID/DID
 *   Then driver locates and initializes that adapter
 */
BOOLEAN Test_OpenAdapter_ByVidDid()
{
    printf("\n=== Test: Open Adapter by VID/DID ===\n");
    
    HANDLE hDevice = CreateFile(
        DEVICE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    TEST_ASSERT(hDevice != INVALID_HANDLE_VALUE, "Open device");
    
    // Enumerate to get first adapter's VID/DID
    AVB_ENUM_REQUEST enumReq = {0};
    enumReq.index = 0;
    
    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_ENUM_ADAPTERS,
        &enumReq,
        sizeof(enumReq),
        &enumReq,
        sizeof(enumReq),
        &bytesReturned,
        NULL
    );
    
    TEST_ASSERT(result != FALSE, "Enumeration succeeds");
    
    printf("   Opening adapter VID=0x%04X DID=0x%04X\n",
           enumReq.vendor_id, enumReq.device_id);
    
    // Open the adapter
    AVB_OPEN_REQUEST openReq = {0};
    openReq.vendor_id = enumReq.vendor_id;
    openReq.device_id = enumReq.device_id;
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_OPEN_ADAPTER,
        &openReq,
        sizeof(openReq),
        &openReq,
        sizeof(openReq),
        &bytesReturned,
        NULL
    );
    
    TEST_ASSERT(result != FALSE, "IOCTL_AVB_OPEN_ADAPTER succeeds");
    TEST_ASSERT_EQUAL(AVB_STATUS_SUCCESS, openReq.status, 
                     "Adapter opened successfully");
    
    CloseHandle(hDevice);
    return TRUE;
}

/**
 * Test Case: REQ-F-MULTIDEV-001.2 - Invalid VID/DID
 * 
 * Scenario: Try to open non-existent adapter
 *   When application calls IOCTL_AVB_OPEN_ADAPTER with invalid VID/DID
 *   Then driver returns error
 */
BOOLEAN Test_OpenAdapter_InvalidVidDid()
{
    printf("\n=== Test: Open Adapter with Invalid VID/DID ===\n");
    
    HANDLE hDevice = CreateFile(
        DEVICE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    TEST_ASSERT(hDevice != INVALID_HANDLE_VALUE, "Open device");
    
    // Try to open with invalid VID/DID
    AVB_OPEN_REQUEST openReq = {0};
    openReq.vendor_id = 0x9999; // Invalid vendor
    openReq.device_id = 0x9999; // Invalid device
    
    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_OPEN_ADAPTER,
        &openReq,
        sizeof(openReq),
        &openReq,
        sizeof(openReq),
        &bytesReturned,
        NULL
    );
    
    TEST_ASSERT(result == FALSE, "Invalid VID/DID request fails");
    TEST_ASSERT(GetLastError() == ERROR_NO_SUCH_DEVICE ||
                GetLastError() == ERROR_FILE_NOT_FOUND,
                "Returns NO_SUCH_DEVICE error");
    
    CloseHandle(hDevice);
    return TRUE;
}

/**
 * Main test runner
 */
int main(int argc, char* argv[])
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  TEST-MULTIDEV-001: Multi-Adapter Management and Selection   ║\n");
    printf("║  Verifies: Issue #15 (REQ-F-MULTIDEV-001)                    ║\n");
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    
    int passCount = 0;
    int failCount = 0;
    
    // Run all test cases
    if (Test_EnumerateAdapters_FirstAdapter()) passCount++; else failCount++;
    if (Test_EnumerateAdapters_AllAdapters()) passCount++; else failCount++;
    if (Test_EnumerateAdapters_OutOfBounds()) passCount++; else failCount++;
    if (Test_OpenAdapter_ByVidDid()) passCount++; else failCount++;
    if (Test_OpenAdapter_InvalidVidDid()) passCount++; else failCount++;
    
    // Print summary
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║  Test Summary                                                 ║\n");
    printf("╠═══════════════════════════════════════════════════════════════╣\n");
    printf("║  Total Tests: %2d                                              ║\n", passCount + failCount);
    printf("║  Passed:      %2d  ✅                                          ║\n", passCount);
    printf("║  Failed:      %2d  ❌                                          ║\n", failCount);
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    
    return (failCount == 0) ? 0 : 1;
}
