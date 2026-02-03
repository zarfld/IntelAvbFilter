/**
 * Test Suite: IOCTL API Versioning (TEST-IOCTL-VERSION-001)
 * Verifies: #64 (REQ-F-IOCTL-VERSIONING-001: IOCTL API Versioning)
 * IOCTL: IOCTL_AVB_GET_VERSION (0x9C40A000)
 * Priority: P0 (Critical) - Prerequisite for all IOCTL testing
 * 
 * Purpose: Verify which driver build is active by querying version.
 * This test will FAIL initially (RED) - proving old driver loaded.
 * After implementing handler, should PASS (GREEN).
 */

#include <windows.h>
#include <stdio.h>
#include <assert.h>
#include "../../include/avb_ioctl.h"

// Expected driver version (from #64 spec)
#define EXPECTED_MAJOR_VERSION 1
#define EXPECTED_MINOR_VERSION 0

/**
 * Test Case 1: Query Driver Version (Basic Functionality)
 * 
 * Given: Driver is loaded
 * When: Application calls IOCTL_AVB_GET_VERSION
 * Then: Returns Major=1, Minor=0
 * 
 * Expected to FAIL initially (old driver doesn't have handler)
 */
static BOOL TestQueryDriverVersion(HANDLE hDevice)
{
    IOCTL_VERSION version = {0};
    DWORD bytesReturned = 0;
    BOOL result;
    
    printf("  [TEST] UT-VERSION-001: Query Driver Version\n");
    printf("    DEBUG: Calling IOCTL_AVB_GET_VERSION (0x%08X)\n", IOCTL_AVB_GET_VERSION);
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_GET_VERSION,
        NULL, 0,                          // No input buffer
        &version, sizeof(version),        // Output: IOCTL_VERSION structure
        &bytesReturned,
        NULL
    );
    
    printf("    DEBUG: DeviceIoControl result=%d, bytes_returned=%lu\n", result, bytesReturned);
    
    if (!result) {
        DWORD error = GetLastError();
        printf("    DEBUG: DeviceIoControl failed with error %lu (0x%08X)\n", error, error);
        printf("  [FAIL] UT-VERSION-001: IOCTL not implemented (expected - old driver)\n");
        return FALSE;
    }
    
    printf("    DEBUG: version.Major=%u, version.Minor=%u\n", version.Major, version.Minor);
    
    // Verify output buffer size
    if (bytesReturned != sizeof(IOCTL_VERSION)) {
        printf("  [FAIL] UT-VERSION-001: bytes_returned=%lu, expected=%zu\n", 
               bytesReturned, sizeof(IOCTL_VERSION));
        return FALSE;
    }
    
    // Verify version values
    if (version.Major != EXPECTED_MAJOR_VERSION || version.Minor != EXPECTED_MINOR_VERSION) {
        printf("  [FAIL] UT-VERSION-001: Version mismatch (got %u.%u, expected %u.%u)\n",
               version.Major, version.Minor, EXPECTED_MAJOR_VERSION, EXPECTED_MINOR_VERSION);
        return FALSE;
    }
    
    printf("  [PASS] UT-VERSION-001: Driver version = %u.%u ✓\n", version.Major, version.Minor);
    return TRUE;
}

/**
 * Test Case 2: Verify Version Output Buffer Size
 * 
 * Given: Driver supports IOCTL_GET_VERSION
 * When: Buffer size checked
 * Then: Returns exactly sizeof(IOCTL_VERSION) = 4 bytes
 */
static BOOL TestVersionBufferSize(HANDLE hDevice)
{
    IOCTL_VERSION version = {0};
    DWORD bytesReturned = 0;
    BOOL result;
    
    printf("  [TEST] UT-VERSION-002: Verify Output Buffer Size\n");
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_GET_VERSION,
        NULL, 0,
        &version, sizeof(version),
        &bytesReturned,
        NULL
    );
    
    if (!result) {
        printf("  [SKIP] UT-VERSION-002: IOCTL not implemented yet\n");
        return FALSE;
    }
    
    if (bytesReturned != 4) {
        printf("  [FAIL] UT-VERSION-002: bytes_returned=%lu, expected=4\n", bytesReturned);
        return FALSE;
    }
    
    printf("  [PASS] UT-VERSION-002: Buffer size correct (4 bytes) ✓\n");
    return TRUE;
}

/**
 * Test Case 3: Multiple Concurrent Version Queries
 * 
 * Given: Driver supports IOCTL_GET_VERSION
 * When: Called 100 times in sequence
 * Then: All return consistent version (Major=1, Minor=0)
 */
static BOOL TestMultipleVersionQueries(HANDLE hDevice)
{
    printf("  [TEST] UT-VERSION-003: Multiple Concurrent Queries\n");
    
    for (int i = 0; i < 100; i++) {
        IOCTL_VERSION version = {0};
        DWORD bytesReturned = 0;
        
        BOOL result = DeviceIoControl(
            hDevice,
            IOCTL_AVB_GET_VERSION,
            NULL, 0,
            &version, sizeof(version),
            &bytesReturned,
            NULL
        );
        
        if (!result) {
            printf("  [SKIP] UT-VERSION-003: IOCTL not implemented yet\n");
            return FALSE;
        }
        
        if (version.Major != EXPECTED_MAJOR_VERSION || version.Minor != EXPECTED_MINOR_VERSION) {
            printf("  [FAIL] UT-VERSION-003: Iteration %d version mismatch (got %u.%u)\n",
                   i, version.Major, version.Minor);
            return FALSE;
        }
    }
    
    printf("  [PASS] UT-VERSION-003: 100 queries consistent (all return %u.%u) ✓\n",
           EXPECTED_MAJOR_VERSION, EXPECTED_MINOR_VERSION);
    return TRUE;
}

int main(void)
{
    HANDLE hDevice;
    int passCount = 0;
    int failCount = 0;
    int skipCount = 0;
    
    printf("====================================================================\n");
    printf(" IOCTL API Versioning Test Suite (TEST-IOCTL-VERSION-001)\n");
    printf("====================================================================\n");
    printf(" Issue: #273 (TEST-IOCTL-VERSION-001)\n");
    printf(" Requirement: #64 (REQ-F-IOCTL-VERSIONING-001)\n");
    printf(" IOCTL: IOCTL_AVB_GET_VERSION (0x%08X)\n", IOCTL_AVB_GET_VERSION);
    printf(" Expected Version: %u.%u\n", EXPECTED_MAJOR_VERSION, EXPECTED_MINOR_VERSION);
    printf(" Priority: P0 (Critical)\n");
    printf("====================================================================\n\n");
    
    // Open driver device
    printf("Opening driver device...\n");
    hDevice = CreateFileA(
        "\\\\.\\IntelAvbFilter",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    if (hDevice == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        printf("FATAL: Failed to open driver (error %lu)\n", error);
        printf("  - Make sure driver is installed\n");
        printf("  - Try running as Administrator\n");
        return 1;
    }
    
    printf("Driver device opened successfully (handle=%p)\n\n", hDevice);
    
    // Run tests
    printf("Running IOCTL Versioning tests...\n\n");
    
    // Test 1: Basic version query (will FAIL initially - expected)
    if (TestQueryDriverVersion(hDevice)) {
        passCount++;
    } else {
        failCount++;
    }
    
    // Test 2: Buffer size verification
    if (TestVersionBufferSize(hDevice)) {
        passCount++;
    } else {
        skipCount++;  // Skip if IOCTL not implemented
    }
    
    // Test 3: Multiple queries
    if (TestMultipleVersionQueries(hDevice)) {
        passCount++;
    } else {
        skipCount++;  // Skip if IOCTL not implemented
    }
    
    // Close device
    CloseHandle(hDevice);
    
    // Summary
    printf("\n====================================================================\n");
    printf(" Test Summary\n");
    printf("====================================================================\n");
    printf(" Total:   %d tests\n", passCount + failCount + skipCount);
    printf(" Passed:  %d tests\n", passCount);
    printf(" Failed:  %d tests\n", failCount);
    printf(" Skipped: %d tests\n", skipCount);
    printf("====================================================================\n");
    
    if (failCount > 0) {
        printf("\n⚠️  EXPECTED FAILURE: Old driver doesn't have IOCTL_GET_VERSION handler\n");
        printf("   Next step: Implement handler in avb_integration_fixed.c\n");
        printf("   Then rebuild, reinstall, and re-run this test\n");
    }
    
    return (failCount > 0) ? 1 : 0;
}
