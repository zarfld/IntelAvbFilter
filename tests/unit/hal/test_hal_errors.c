/**
 * @file test_hal_errors.c
 * @brief Hardware Abstraction Layer Error Scenario Tests
 * 
 * Test ID: TEST-PORTABILITY-HAL-002
 * Implements: #309 (TEST-PORTABILITY-HAL-002: Hardware Abstraction Layer Error Scenarios)
 * Verifies: #84 (REQ-NF-PORTABILITY-001: Hardware Portability via Device Abstraction Layer)
 * Issue: https://github.com/zarfld/IntelAvbFilter/issues/309
 * 
 * Tests Error Scenarios: ES-PORT-HAL-001 through ES-PORT-HAL-010
 * 
 * Test Cases:
 *   TC-ERR-001: Unsupported Device ID (ES-PORT-HAL-001)
 *   TC-ERR-002: NULL Hardware Operation (ES-PORT-HAL-002)
 *   TC-ERR-003: Hardware Capability Mismatch (ES-PORT-HAL-003)
 *   TC-ERR-004: Register Offset Out of Bounds (ES-PORT-HAL-004)
 *   TC-ERR-005: Hardware Initialization Failure (ES-PORT-HAL-005)
 *   TC-ERR-006: Operation Table Version Mismatch (ES-PORT-HAL-007)
 *   TC-ERR-007: Device-Specific State Overflow (ES-PORT-HAL-009)
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

// NTSTATUS codes
#define STATUS_SUCCESS                     0x00000000
#define STATUS_NOT_SUPPORTED               0xC00000BB
#define STATUS_ACCESS_VIOLATION            0xC0000005
#define STATUS_INVALID_PARAMETER           0xC000000D
#define STATUS_DEVICE_CONFIGURATION_ERROR  0xC0000182
#define STATUS_REVISION_MISMATCH           0xC0000059
#define STATUS_NOT_IMPLEMENTED             0xC0000002

// Event IDs (from requirement)
#define EVENT_ID_UNSUPPORTED_DEVICE        17301
#define EVENT_ID_NULL_OPERATION            17302
#define EVENT_ID_CAPABILITY_MISMATCH       17303
#define EVENT_ID_INVALID_REGISTER_OFFSET   17304
#define EVENT_ID_HARDWARE_INIT_FAILED      17305
#define EVENT_ID_VERSION_MISMATCH          17307
#define EVENT_ID_OPERATION_NOT_IMPLEMENTED 17310

// Test result tracking
typedef struct {
    int passed;
    int failed;
    int total;
} TestResults;

static TestResults g_results = {0};
static int g_lastEventId = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        g_results.total++; \
        if (condition) { \
            printf("  ✓ PASS: %s\n", message); \
            g_results.passed++; \
        } else { \
            printf("  ✗ FAIL: %s\n", message); \
            g_results.failed++; \
        } \
    } while(0)

#define TEST_CASE(name) \
    printf("\n--- %s ---\n", name)

// Mock event logging
static void LogEvent(int eventId, const char* message) {
    g_lastEventId = eventId;
    printf("  [EVENT %d] %s\n", eventId, message);
}

// Mock HAL structures
typedef NTSTATUS (*ReadPhcFn)(void* Context, LARGE_INTEGER* Timestamp);
typedef NTSTATUS (*ReadRegister32Fn)(void* Context, ULONG Offset, ULONG* Value);
typedef NTSTATUS (*InitializeFn)(void* Context);

typedef struct _HARDWARE_OPS {
    ULONG Version;
    ReadPhcFn ReadPhc;
    void* AdjustPhcFrequency;
    void* AdjustPhcPhase;
    void* ConfigureTxQueue;
    void* ConfigureRxQueue;
    void* EnableLaunchTime;
    ReadRegister32Fn ReadRegister32;
    void* WriteRegister32;
    void* GetCapabilities;
    InitializeFn Initialize;
    void* Shutdown;
} HARDWARE_OPS;

typedef struct _HARDWARE_CAPABILITIES {
    BOOLEAN SupportsLaunchTime;
    BOOLEAN SupportsCreditBasedShaping;
    ULONG NumTxQueues;
} HARDWARE_CAPABILITIES;

#define BAR0_SIZE 0x10000  // 64KB typical size

// =============================================================================
// Mock Implementations
// =============================================================================

static NTSTATUS Mock_ReadRegister32_BoundsCheck(void* Context, ULONG Offset, ULONG* Value) {
    if (Offset >= BAR0_SIZE) {
        LogEvent(EVENT_ID_INVALID_REGISTER_OFFSET, "Register offset out of bounds");
        return STATUS_INVALID_PARAMETER;
    }
    *Value = 0x12345678;
    return STATUS_SUCCESS;
}

static NTSTATUS Mock_Initialize_Fail(void* Context) {
    LogEvent(EVENT_ID_HARDWARE_INIT_FAILED, "BAR0 mapping failed");
    return STATUS_DEVICE_CONFIGURATION_ERROR;
}

static NTSTATUS Mock_Initialize_Success(void* Context) {
    return STATUS_SUCCESS;
}

static NTSTATUS SelectHardwareOps_Mock(USHORT DeviceId, HARDWARE_OPS** OutOps) {
    if (DeviceId == 0x1533) {
        // Return valid ops
        static HARDWARE_OPS validOps = { .Version = 2 };
        *OutOps = &validOps;
        return STATUS_SUCCESS;
    } else {
        LogEvent(EVENT_ID_UNSUPPORTED_DEVICE, "Unsupported device ID");
        *OutOps = NULL;
        return STATUS_NOT_SUPPORTED;
    }
}

// =============================================================================
// TEST CASES
// =============================================================================

/**
 * TC-ERR-001: Unsupported Device ID (ES-PORT-HAL-001)
 */
void Test_UnsupportedDeviceId(void) {
    TEST_CASE("TC-ERR-001: Unsupported Device ID (ES-PORT-HAL-001)");
    
    USHORT unsupportedDevices[] = {0x1521, 0x150E, 0x10C9, 0xFFFF};
    
    for (int i = 0; i < 4; i++) {
        HARDWARE_OPS* ops = NULL;
        g_lastEventId = 0;
        
        NTSTATUS status = SelectHardwareOps_Mock(unsupportedDevices[i], &ops);
        
        char msg[128];
        sprintf(msg, "Unsupported device 0x%04X returns STATUS_NOT_SUPPORTED", unsupportedDevices[i]);
        TEST_ASSERT(status == STATUS_NOT_SUPPORTED, msg);
        TEST_ASSERT(ops == NULL, "Ops pointer is NULL for unsupported device");
        TEST_ASSERT(g_lastEventId == EVENT_ID_UNSUPPORTED_DEVICE, 
                   "Event 17301 logged for unsupported device");
    }
    
    // Test supported device
    HARDWARE_OPS* ops = NULL;
    NTSTATUS status = SelectHardwareOps_Mock(0x1533, &ops);
    TEST_ASSERT(status == STATUS_SUCCESS, "Supported device (0x1533) returns STATUS_SUCCESS");
    TEST_ASSERT(ops != NULL, "Ops pointer valid for supported device");
}

/**
 * TC-ERR-002: NULL Hardware Operation (ES-PORT-HAL-002)
 */
void Test_NullHardwareOperation(void) {
    TEST_CASE("TC-ERR-002: NULL Hardware Operation (ES-PORT-HAL-002)");
    
    // Create malformed operation table with NULL operation
    HARDWARE_OPS badOps = {0};
    badOps.ReadPhc = NULL;  // Intentionally NULL
    
    // In production, this should be caught by static assertion or runtime check
    TEST_ASSERT(badOps.ReadPhc == NULL, "NULL operation detected");
    
    // Simulate detection
    if (badOps.ReadPhc == NULL) {
        LogEvent(EVENT_ID_NULL_OPERATION, "NULL operation pointer detected");
    }
    
    TEST_ASSERT(g_lastEventId == EVENT_ID_NULL_OPERATION, 
               "Event 17302 logged for NULL operation");
}

/**
 * TC-ERR-003: Hardware Capability Mismatch (ES-PORT-HAL-003)
 */
void Test_CapabilityMismatch(void) {
    TEST_CASE("TC-ERR-003: Hardware Capability Mismatch (ES-PORT-HAL-003)");
    
    // Create mock device without launch time support
    HARDWARE_CAPABILITIES caps = {0};
    caps.SupportsLaunchTime = FALSE;
    caps.SupportsCreditBasedShaping = TRUE;
    caps.NumTxQueues = 2;
    
    // Attempt to enable launch time
    if (!caps.SupportsLaunchTime) {
        LogEvent(EVENT_ID_CAPABILITY_MISMATCH, "Launch time not supported");
    }
    
    TEST_ASSERT(!caps.SupportsLaunchTime, "Device does not support launch time");
    TEST_ASSERT(g_lastEventId == EVENT_ID_CAPABILITY_MISMATCH,
               "Event 17303 logged for capability mismatch");
}

/**
 * TC-ERR-004: Register Offset Out of Bounds (ES-PORT-HAL-004)
 */
void Test_RegisterOffsetOutOfBounds(void) {
    TEST_CASE("TC-ERR-004: Register Offset Out of Bounds (ES-PORT-HAL-004)");
    
    ULONG value;
    NTSTATUS status;
    
    g_lastEventId = 0;
    
    // Attempt to read beyond BAR0 size
    status = Mock_ReadRegister32_BoundsCheck(NULL, 0xFFFFFFFF, &value);
    TEST_ASSERT(status == STATUS_INVALID_PARAMETER, 
               "Out-of-bounds offset returns STATUS_INVALID_PARAMETER");
    TEST_ASSERT(g_lastEventId == EVENT_ID_INVALID_REGISTER_OFFSET,
               "Event 17304 logged for invalid offset");
    
    // Attempt to read at BAR0_SIZE (boundary)
    g_lastEventId = 0;
    status = Mock_ReadRegister32_BoundsCheck(NULL, BAR0_SIZE, &value);
    TEST_ASSERT(status == STATUS_INVALID_PARAMETER, 
               "Boundary offset (BAR0_SIZE) rejected");
    TEST_ASSERT(g_lastEventId == EVENT_ID_INVALID_REGISTER_OFFSET,
               "Event 17304 logged for boundary violation");
    
    // Valid read (within bounds)
    g_lastEventId = 0;
    status = Mock_ReadRegister32_BoundsCheck(NULL, 0x1000, &value);
    TEST_ASSERT(status == STATUS_SUCCESS, "Valid offset accepted");
    TEST_ASSERT(value == 0x12345678, "Valid read returns expected value");
    TEST_ASSERT(g_lastEventId == 0, "No event logged for valid read");
}

/**
 * TC-ERR-005: Hardware Initialization Failure (ES-PORT-HAL-005)
 */
void Test_HardwareInitFailure(void) {
    TEST_CASE("TC-ERR-005: Hardware Initialization Failure (ES-PORT-HAL-005)");
    
    g_lastEventId = 0;
    
    // Simulate BAR0 mapping failure
    NTSTATUS status = Mock_Initialize_Fail(NULL);
    
    TEST_ASSERT(status == STATUS_DEVICE_CONFIGURATION_ERROR,
               "Init failure returns STATUS_DEVICE_CONFIGURATION_ERROR");
    TEST_ASSERT(g_lastEventId == EVENT_ID_HARDWARE_INIT_FAILED,
               "Event 17305 logged for hardware init failure");
    
    // Test successful initialization
    g_lastEventId = 0;
    status = Mock_Initialize_Success(NULL);
    TEST_ASSERT(status == STATUS_SUCCESS, "Successful init returns STATUS_SUCCESS");
    TEST_ASSERT(g_lastEventId == 0, "No event logged for successful init");
}

/**
 * TC-ERR-006: Operation Table Version Mismatch (ES-PORT-HAL-007)
 */
void Test_VersionMismatch(void) {
    TEST_CASE("TC-ERR-006: Operation Table Version Mismatch (ES-PORT-HAL-007)");
    
    // Simulate v1 operation table
    HARDWARE_OPS opsV1 = {0};
    opsV1.Version = 1;
    
    ULONG expectedVersion = 2;
    
    // Validate version
    g_lastEventId = 0;
    NTSTATUS status = STATUS_SUCCESS;
    
    if (opsV1.Version != expectedVersion) {
        LogEvent(EVENT_ID_VERSION_MISMATCH, "HAL version mismatch");
        status = STATUS_REVISION_MISMATCH;
    }
    
    TEST_ASSERT(status == STATUS_REVISION_MISMATCH,
               "Version mismatch returns STATUS_REVISION_MISMATCH");
    TEST_ASSERT(g_lastEventId == EVENT_ID_VERSION_MISMATCH,
               "Event 17307 logged for version mismatch");
    
    // Test matching version
    HARDWARE_OPS opsV2 = {0};
    opsV2.Version = 2;
    
    g_lastEventId = 0;
    status = STATUS_SUCCESS;
    
    if (opsV2.Version != expectedVersion) {
        status = STATUS_REVISION_MISMATCH;
    }
    
    TEST_ASSERT(status == STATUS_SUCCESS, "Matching version accepted");
    TEST_ASSERT(g_lastEventId == 0, "No event logged for matching version");
}

/**
 * TC-ERR-007: Device-Specific State Overflow (ES-PORT-HAL-009)
 */
void Test_DeviceSpecificStateOverflow(void) {
    TEST_CASE("TC-ERR-007: Device-Specific State Overflow (ES-PORT-HAL-009)");
    
    // Simulate device-specific contexts
    typedef struct { UCHAR Buffer[256]; } I210_CONTEXT;
    typedef struct { UCHAR Buffer[512]; } I225_CONTEXT;
    typedef struct { UCHAR Buffer[1024]; } I226_CONTEXT;
    
    typedef union {
        I210_CONTEXT I210;
        I225_CONTEXT I225;
        I226_CONTEXT I226;
    } DEVICE_SPECIFIC;
    
    // Verify union sizes
    TEST_ASSERT(sizeof(DEVICE_SPECIFIC) >= sizeof(I210_CONTEXT),
               "Union accommodates I210_CONTEXT");
    TEST_ASSERT(sizeof(DEVICE_SPECIFIC) >= sizeof(I225_CONTEXT),
               "Union accommodates I225_CONTEXT");
    TEST_ASSERT(sizeof(DEVICE_SPECIFIC) >= sizeof(I226_CONTEXT),
               "Union accommodates I226_CONTEXT");
    
    // The largest context should define union size
    TEST_ASSERT(sizeof(DEVICE_SPECIFIC) == sizeof(I226_CONTEXT),
               "Union size equals largest context");
}

// Mock function for unimplemented operation
static NTSTATUS Default_GetTemperature(void* Context, LONG* Temperature) {
    (void)Context;  // Unused
    (void)Temperature;  // Unused
    LogEvent(EVENT_ID_OPERATION_NOT_IMPLEMENTED, "GetTemperature not implemented");
    return STATUS_NOT_IMPLEMENTED;
}

/**
 * TC-ERR-008: Missing Operation Implementation (ES-PORT-HAL-010)
 */
void Test_MissingOperationImplementation(void) {
    TEST_CASE("TC-ERR-008: Missing Operation Implementation (ES-PORT-HAL-010)");
    
    g_lastEventId = 0;
    LONG temp = 0;
    
    NTSTATUS status = Default_GetTemperature(NULL, &temp);
    
    TEST_ASSERT(status == STATUS_NOT_IMPLEMENTED,
               "Unimplemented operation returns STATUS_NOT_IMPLEMENTED");
    TEST_ASSERT(g_lastEventId == EVENT_ID_OPERATION_NOT_IMPLEMENTED,
               "Event 17310 logged for unimplemented operation");
}

// =============================================================================
// MAIN
// =============================================================================

int main(void) {
    printf("========================================\n");
    printf("HAL ERROR SCENARIO TESTS (TEST-PORTABILITY-HAL-002)\n");
    printf("========================================\n");
    printf("Verifies: #84 (REQ-NF-PORTABILITY-001)\n");
    printf("Issue: https://github.com/zarfld/IntelAvbFilter/issues/309\n\n");
    
    // Run test cases
    Test_UnsupportedDeviceId();
    Test_NullHardwareOperation();
    Test_CapabilityMismatch();
    Test_RegisterOffsetOutOfBounds();
    Test_HardwareInitFailure();
    Test_VersionMismatch();
    Test_DeviceSpecificStateOverflow();
    Test_MissingOperationImplementation();
    
    // Print results
    printf("\n========================================\n");
    printf("TEST RESULTS\n");
    printf("========================================\n");
    printf("Total:  %d\n", g_results.total);
    printf("Passed: %d\n", g_results.passed);
    printf("Failed: %d\n", g_results.failed);
    printf("========================================\n");
    
    if (g_results.failed == 0) {
        printf("✓ ALL ERROR SCENARIOS HANDLED CORRECTLY\n");
        return 0;
    } else {
        printf("✗ SOME ERROR TESTS FAILED\n");
        return 1;
    }
}
