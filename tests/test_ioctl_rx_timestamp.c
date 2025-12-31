/**
 * @file test_ioctl_rx_timestamp.c
 * @brief PTP RX Timestamping IOCTL Test Suite
 *
 * Implements: #298 (TEST-RX-TS-001: PTP RX Timestamping Tests)
 * Verifies: #6 (REQ-F-PTP-004: PTP RX Timestamping via IOCTL)
 * 
 * @see https://github.com/zarfld/IntelAvbFilter/issues/298
 * @see https://github.com/zarfld/IntelAvbFilter/issues/6
 *
 * IOCTLs Tested:
 *   - 41 (IOCTL_AVB_GET_RX_TIMESTAMP): Retrieve RX timestamp for packet
 *   - 42 (IOCTL_AVB_ENABLE_RX_TIMESTAMP_FILTER): Configure RX timestamp filters
 *
 * Test Cases: 16
 * Priority: P0 (Critical)
 * Standards: IEEE 1012-2016 (Verification & Validation), IEEE 1588-2019 (PTP)
 *
 * Part of: #14 (Master IOCTL Requirements Tracking)
 */

#include <windows.h>
#include <stdio.h>
#include <stdbool.h>

// IOCTL Definitions
#define IOCTL_AVB_GET_RX_TIMESTAMP           CTL_CODE(FILE_DEVICE_NETWORK, 0x841, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AVB_ENABLE_RX_TIMESTAMP_FILTER CTL_CODE(FILE_DEVICE_NETWORK, 0x842, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Filter type flags
#define RX_TS_FILTER_NONE       0x00  // No filtering
#define RX_TS_FILTER_PTP_V2     0x01  // PTPv2 packets only
#define RX_TS_FILTER_UDP_PORT   0x02  // Specific UDP port
#define RX_TS_FILTER_MAC_ADDR   0x04  // Specific MAC address

#pragma pack(push, 1)
typedef struct _RX_TIMESTAMP_REQUEST {
    UINT64 packet_id;        // Packet identifier (sequence number or hash)
    UINT64 seconds;          // Timestamp seconds (output)
    UINT32 nanoseconds;      // Timestamp nanoseconds (output)
} RX_TIMESTAMP_REQUEST;

typedef struct _RX_TIMESTAMP_FILTER_REQUEST {
    UCHAR  filter_flags;     // Combination of RX_TS_FILTER_* flags
    USHORT udp_port;         // UDP port for filtering (if RX_TS_FILTER_UDP_PORT set)
    UCHAR  mac_addr[6];      // MAC address for filtering (if RX_TS_FILTER_MAC_ADDR set)
} RX_TIMESTAMP_FILTER_REQUEST;
#pragma pack(pop)

// Test state
static HANDLE g_hDevice = INVALID_HANDLE_VALUE;
static int g_passCount = 0;
static int g_failCount = 0;
static int g_skipCount = 0;

// Helper: Get RX timestamp for packet
static bool GetRxTimestamp(UINT64 packet_id, UINT64* seconds, UINT32* nanoseconds, const char* context) {
    RX_TIMESTAMP_REQUEST request = { 0 };
    request.packet_id = packet_id;
    
    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        g_hDevice,
        IOCTL_AVB_GET_RX_TIMESTAMP,
        &request,
        sizeof(request),
        &request,
        sizeof(request),
        &bytesReturned,
        NULL
    );
    
    if (result && bytesReturned == sizeof(request)) {
        if (seconds) *seconds = request.seconds;
        if (nanoseconds) *nanoseconds = request.nanoseconds;
        return true;
    }
    
    return false;
}

// Helper: Configure RX timestamp filter
static bool SetRxTimestampFilter(UCHAR flags, USHORT udp_port, const UCHAR* mac_addr, const char* context) {
    RX_TIMESTAMP_FILTER_REQUEST request = { 0 };
    request.filter_flags = flags;
    request.udp_port = udp_port;
    if (mac_addr) {
        memcpy(request.mac_addr, mac_addr, 6);
    }
    
    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        g_hDevice,
        IOCTL_AVB_ENABLE_RX_TIMESTAMP_FILTER,
        &request,
        sizeof(request),
        NULL,
        0,
        &bytesReturned,
        NULL
    );
    
    return (result != 0);
}

//=============================================================================
// Test Cases
//=============================================================================

// Test 1: Get RX Timestamp - Valid Packet ID
static void Test_GetRxTimestampValidPacket(void) {
    UINT64 seconds = 0;
    UINT32 nanoseconds = 0;
    UINT64 test_packet_id = 12345;
    
    if (GetRxTimestamp(test_packet_id, &seconds, &nanoseconds, "valid packet")) {
        // Success case - verify timestamp structure
        if (nanoseconds < 1000000000) {
            printf("  [PASS] UT-RX-TS-001: Get RX Timestamp (Valid Packet)\n");
            g_passCount++;
        } else {
            printf("  [FAIL] UT-RX-TS-001: Get RX Timestamp: Invalid nanoseconds (%u)\n", nanoseconds);
            g_failCount++;
        }
    } else {
        printf("  [FAIL] UT-RX-TS-001: Get RX Timestamp: IOCTL failed\n");
        g_failCount++;
    }
}

// Test 2: Get RX Timestamp - Zero Packet ID
static void Test_GetRxTimestampZeroPacketId(void) {
    UINT64 seconds = 0;
    UINT32 nanoseconds = 0;
    
    if (GetRxTimestamp(0, &seconds, &nanoseconds, "zero packet ID")) {
        printf("  [PASS] UT-RX-TS-002: Get RX Timestamp (Zero Packet ID)\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-RX-TS-002: Get RX Timestamp: Zero packet ID rejected\n");
        g_failCount++;
    }
}

// Test 3: Get RX Timestamp - Maximum Packet ID
static void Test_GetRxTimestampMaxPacketId(void) {
    UINT64 seconds = 0;
    UINT32 nanoseconds = 0;
    UINT64 max_packet_id = 0xFFFFFFFFFFFFFFFF;
    
    if (GetRxTimestamp(max_packet_id, &seconds, &nanoseconds, "max packet ID")) {
        printf("  [PASS] UT-RX-TS-003: Get RX Timestamp (Maximum Packet ID)\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-RX-TS-003: Get RX Timestamp: Max packet ID failed\n");
        g_failCount++;
    }
}

// Test 4: Get RX Timestamp - Non-existent Packet
static void Test_GetRxTimestampNonExistent(void) {
    UINT64 seconds = 0;
    UINT32 nanoseconds = 0;
    UINT64 fake_packet_id = 0xDEADBEEFDEADBEEF;
    
    // Should fail or return zero timestamp for non-existent packet
    bool result = GetRxTimestamp(fake_packet_id, &seconds, &nanoseconds, "non-existent packet");
    
    if (!result || (seconds == 0 && nanoseconds == 0)) {
        printf("  [PASS] UT-RX-TS-004: Get RX Timestamp (Non-existent Packet)\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-RX-TS-004: Non-existent packet returned timestamp (%llu.%u)\n", 
               seconds, nanoseconds);
        g_failCount++;
    }
}

// Test 5: Get RX Timestamp - NULL Pointer Handling
static void Test_GetRxTimestampNullPointer(void) {
    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        g_hDevice,
        IOCTL_AVB_GET_RX_TIMESTAMP,
        NULL,
        0,
        NULL,
        0,
        &bytesReturned,
        NULL
    );
    
    if (!result && GetLastError() == ERROR_INVALID_PARAMETER) {
        printf("  [PASS] UT-RX-TS-005: NULL Pointer Handling\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-RX-TS-005: NULL pointer not rejected\n");
        g_failCount++;
    }
}

// Test 6: Enable Filter - No Filtering (Disable)
static void Test_EnableFilterNone(void) {
    if (SetRxTimestampFilter(RX_TS_FILTER_NONE, 0, NULL, "disable filtering")) {
        printf("  [PASS] UT-RX-TS-006: Disable RX Timestamp Filtering\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-RX-TS-006: Failed to disable filtering\n");
        g_failCount++;
    }
}

// Test 7: Enable Filter - PTPv2 Only
static void Test_EnableFilterPTPv2(void) {
    if (SetRxTimestampFilter(RX_TS_FILTER_PTP_V2, 0, NULL, "PTPv2 filter")) {
        printf("  [PASS] UT-RX-TS-007: Enable PTPv2 Filter\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-RX-TS-007: Failed to enable PTPv2 filter\n");
        g_failCount++;
    }
}

// Test 8: Enable Filter - UDP Port (PTP Event: 319)
static void Test_EnableFilterUdpPort(void) {
    const USHORT PTP_EVENT_PORT = 319;
    
    if (SetRxTimestampFilter(RX_TS_FILTER_UDP_PORT, PTP_EVENT_PORT, NULL, "UDP port filter")) {
        printf("  [PASS] UT-RX-TS-008: Enable UDP Port Filter (319)\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-RX-TS-008: Failed to enable UDP port filter\n");
        g_failCount++;
    }
}

// Test 9: Enable Filter - MAC Address
static void Test_EnableFilterMacAddress(void) {
    UCHAR ptp_multicast_mac[6] = { 0x01, 0x1B, 0x19, 0x00, 0x00, 0x00 };
    
    if (SetRxTimestampFilter(RX_TS_FILTER_MAC_ADDR, 0, ptp_multicast_mac, "MAC filter")) {
        printf("  [PASS] UT-RX-TS-009: Enable MAC Address Filter\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-RX-TS-009: Failed to enable MAC address filter\n");
        g_failCount++;
    }
}

// Test 10: Enable Filter - Combined Filters
static void Test_EnableFilterCombined(void) {
    UCHAR ptp_multicast_mac[6] = { 0x01, 0x1B, 0x19, 0x00, 0x00, 0x00 };
    UCHAR combined_flags = RX_TS_FILTER_PTP_V2 | RX_TS_FILTER_UDP_PORT | RX_TS_FILTER_MAC_ADDR;
    
    if (SetRxTimestampFilter(combined_flags, 319, ptp_multicast_mac, "combined filters")) {
        printf("  [PASS] UT-RX-TS-010: Enable Combined Filters\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-RX-TS-010: Failed to enable combined filters\n");
        g_failCount++;
    }
}

// Test 11: Enable Filter - Invalid Flags
static void Test_EnableFilterInvalidFlags(void) {
    UCHAR invalid_flags = 0xFF;
    
    if (!SetRxTimestampFilter(invalid_flags, 0, NULL, "invalid flags")) {
        printf("  [PASS] UT-RX-TS-011: Invalid Filter Flags Rejected\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-RX-TS-011: Invalid flags accepted\n");
        g_failCount++;
    }
}

// Test 12: Enable Filter - NULL Pointer Handling
static void Test_EnableFilterNullPointer(void) {
    DWORD bytesReturned = 0;
    BOOL result = DeviceIoControl(
        g_hDevice,
        IOCTL_AVB_ENABLE_RX_TIMESTAMP_FILTER,
        NULL,
        0,
        NULL,
        0,
        &bytesReturned,
        NULL
    );
    
    if (!result && GetLastError() == ERROR_INVALID_PARAMETER) {
        printf("  [PASS] UT-RX-TS-012: NULL Pointer Handling (Filter)\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-RX-TS-012: NULL pointer not rejected (filter)\n");
        g_failCount++;
    }
}

// Test 13: Rapid Filter Switching
static void Test_RapidFilterSwitching(void) {
    bool success = true;
    
    for (int i = 0; i < 100; i++) {
        UCHAR flags = (i % 4);  // Cycle through 0x00, 0x01, 0x02, 0x03
        if (!SetRxTimestampFilter(flags, 0, NULL, "rapid switching")) {
            success = false;
            break;
        }
    }
    
    if (success) {
        printf("  [PASS] UT-RX-TS-013: Rapid Filter Switching\n");
        g_passCount++;
    } else {
        printf("  [FAIL] UT-RX-TS-013: Rapid filter switching failed\n");
        g_failCount++;
    }
}

// Test 14: Timestamp Queue Overflow Handling (SKIP - requires packet injection)
static void Test_TimestampQueueOverflow(void) {
    printf("  [SKIP] UT-RX-TS-014: Timestamp Queue Overflow: Requires packet injection framework\n");
    g_skipCount++;
}

// Test 15: Filter Persistence After Disable/Re-enable (SKIP - requires state verification)
static void Test_FilterPersistence(void) {
    printf("  [SKIP] UT-RX-TS-015: Filter Persistence: Requires state verification mechanism\n");
    g_skipCount++;
}

// Test 16: Concurrent Timestamp Retrieval (SKIP - requires multi-threading)
static void Test_ConcurrentTimestampRetrieval(void) {
    printf("  [SKIP] UT-RX-TS-016: Concurrent Timestamp Retrieval: Requires multi-threaded framework\n");
    g_skipCount++;
}

//=============================================================================
// Main Test Runner
//=============================================================================

int main(void) {
    printf("\n");
    printf("====================================================================\n");
    printf(" PTP RX Timestamping Test Suite\n");
    printf("====================================================================\n");
    printf(" Implements: #298 (TEST-RX-TS-001)\n");
    printf(" Verifies: #6 (REQ-F-PTP-004)\n");
    printf(" IOCTLs: GET_RX_TIMESTAMP (41), ENABLE_RX_TIMESTAMP_FILTER (42)\n");
    printf(" Total Tests: 16\n");
    printf(" Priority: P0 (Critical)\n");
    printf("====================================================================\n\n");
    
    // Open device
    g_hDevice = CreateFileW(
        L"\\\\.\\IntelAvbFilter",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );
    
    if (g_hDevice == INVALID_HANDLE_VALUE) {
        printf("ERROR: Failed to open device (error %lu)\n", GetLastError());
        printf("Make sure the driver is installed and running.\n\n");
        return 1;
    }
    
    printf("Running PTP RX Timestamping tests...\n\n");
    
    // Run all tests
    Test_GetRxTimestampValidPacket();
    Test_GetRxTimestampZeroPacketId();
    Test_GetRxTimestampMaxPacketId();
    Test_GetRxTimestampNonExistent();
    Test_GetRxTimestampNullPointer();
    Test_EnableFilterNone();
    Test_EnableFilterPTPv2();
    Test_EnableFilterUdpPort();
    Test_EnableFilterMacAddress();
    Test_EnableFilterCombined();
    Test_EnableFilterInvalidFlags();
    Test_EnableFilterNullPointer();
    Test_RapidFilterSwitching();
    Test_TimestampQueueOverflow();
    Test_FilterPersistence();
    Test_ConcurrentTimestampRetrieval();
    
    // Cleanup
    CloseHandle(g_hDevice);
    
    // Summary
    printf("\n");
    printf("====================================================================\n");
    printf(" Test Summary\n");
    printf("====================================================================\n");
    printf(" Total:   %d tests\n", g_passCount + g_failCount + g_skipCount);
    printf(" Passed:  %d tests\n", g_passCount);
    printf(" Failed:  %d tests\n", g_failCount);
    printf(" Skipped: %d tests\n", g_skipCount);
    printf("====================================================================\n\n");
    
    return (g_failCount == 0) ? 0 : 1;
}
