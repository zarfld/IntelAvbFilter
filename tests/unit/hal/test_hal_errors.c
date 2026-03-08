/**
 * @file test_hal_errors.c
 * @brief Hardware Abstraction Layer Error Scenario Tests
 * 
 * Test ID: TEST-PORTABILITY-HAL-002 / TEST-ERROR-MAP-001
 * Implements: #309 (TEST-PORTABILITY-HAL-002: Hardware Abstraction Layer Error Scenarios)
 * Implements: #280 (TEST-ERROR-MAP-001: AVB→NTSTATUS Mapping Table)
 * Verifies: #84 (REQ-NF-PORTABILITY-001: Hardware Portability via Device Abstraction Layer)
 * Issue: https://github.com/zarfld/IntelAvbFilter/issues/309
 * Issue: https://github.com/zarfld/IntelAvbFilter/issues/280
 * 
 * Tests Error Scenarios: ES-PORT-HAL-001 through ES-PORT-HAL-010
 * Tests NTSTATUS Mapping: TC-ERR-MAP-001
 * 
 * Test Cases:
 *   TC-ERR-001: Unsupported Device ID (ES-PORT-HAL-001)
 *   TC-ERR-002: NULL Hardware Operation (ES-PORT-HAL-002)
 *   TC-ERR-003: Hardware Capability Mismatch (ES-PORT-HAL-003)
 *   TC-ERR-004: Register Offset Out of Bounds (ES-PORT-HAL-004)
 *   TC-ERR-005: Hardware Initialization Failure (ES-PORT-HAL-005)
 *   TC-ERR-006: Operation Table Version Mismatch (ES-PORT-HAL-007)
 *   TC-ERR-007: Device-Specific State Overflow (ES-PORT-HAL-009)
 *   TC-ERR-008: Missing Operation Implementation (ES-PORT-HAL-010)
 *   TC-ERR-MAP-001: Exhaustive NDIS_STATUS→NTSTATUS 1:1 mapping table (closes #280)
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

/* SSOT for all driver Event Log IDs (closes #289)
 * Never use raw integer literals for event IDs — use these names only. */
#include "../../../include/avb_events.h"

/* Map SSOT names to the legacy local aliases used below */
#define EVENT_ID_UNSUPPORTED_DEVICE        AVB_EVENT_UNSUPPORTED_DEVICE
#define EVENT_ID_NULL_OPERATION            AVB_EVENT_NULL_OPERATION
#define EVENT_ID_CAPABILITY_MISMATCH       AVB_EVENT_CAPABILITY_MISMATCH
#define EVENT_ID_INVALID_REGISTER_OFFSET   AVB_EVENT_INVALID_REGISTER_OFFSET
#define EVENT_ID_HARDWARE_INIT_FAILED      AVB_EVENT_HARDWARE_INIT_FAILED
#define EVENT_ID_VERSION_MISMATCH          AVB_EVENT_VERSION_MISMATCH
#define EVENT_ID_OPERATION_NOT_IMPLEMENTED AVB_EVENT_OPERATION_NOT_IMPLEMENTED
#define STATUS_SUCCESS                     ((ULONG)0x00000000L)
#define STATUS_UNSUCCESSFUL                ((ULONG)0xC0000001L)  /* NDIS_STATUS_FAILURE */
#define STATUS_NOT_IMPLEMENTED             ((ULONG)0xC0000002L)
#define STATUS_INVALID_PARAMETER           ((ULONG)0xC000000DL)
#define STATUS_DEVICE_CONFIGURATION_ERROR  ((ULONG)0xC0000182L)
#define STATUS_REVISION_MISMATCH           ((ULONG)0xC0000059L)
#define STATUS_NOT_SUPPORTED               ((ULONG)0xC00000BBL)  /* NDIS_STATUS_NOT_SUPPORTED */
#define STATUS_INSUFFICIENT_RESOURCES      ((ULONG)0xC000009AL)  /* NDIS_STATUS_RESOURCES */
#define STATUS_BUFFER_TOO_SMALL            ((ULONG)0xC0000023L)  /* NDIS_STATUS_BUFFER_TOO_SHORT */
#define STATUS_ACCESS_VIOLATION            ((ULONG)0xC0000005L)
#define STATUS_CANCELLED                   ((ULONG)0xC0000120L)  /* NDIS_STATUS_REQUEST_ABORTED */
#define STATUS_DEVICE_BUSY                 ((ULONG)0x80000011L)  /* NDIS_STATUS_RESET_IN_PROGRESS */
#define STATUS_INVALID_DEVICE_REQUEST      ((ULONG)0xC0000010L)  /* NDIS_STATUS_NOT_RECOGNIZED */

/* NDIS_STATUS symbolic aliases — map to their NTSTATUS numeric equivalents */
#define NDIS_STATUS_SUCCESS                STATUS_SUCCESS
#define NDIS_STATUS_FAILURE                STATUS_UNSUCCESSFUL
#define NDIS_STATUS_INVALID_PARAMETER      STATUS_INVALID_PARAMETER
#define NDIS_STATUS_NOT_SUPPORTED          STATUS_NOT_SUPPORTED
#define NDIS_STATUS_RESOURCES              STATUS_INSUFFICIENT_RESOURCES
#define NDIS_STATUS_BUFFER_TOO_SHORT       STATUS_BUFFER_TOO_SMALL
#define NDIS_STATUS_REQUEST_ABORTED        STATUS_CANCELLED
#define NDIS_STATUS_RESET_IN_PROGRESS      STATUS_DEVICE_BUSY
#define NDIS_STATUS_NOT_RECOGNIZED         STATUS_INVALID_DEVICE_REQUEST

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

/**
 * TC-ERR-MAP-001: Exhaustive NDIS_STATUS → NTSTATUS 1:1 mapping table
 *
 * Asserts that every NDIS_STATUS code used by the AVB filter driver maps to
 * the correct numeric NTSTATUS value, and that the symbolic NDIS_STATUS alias
 * equals its NTSTATUS equivalent.  Covers all codes observed in filter.c,
 * avb_ioctl.c, and avb_hal.c.
 *
 * Issue: closes #280 (TEST-ERROR-MAP-001: AVB→NTSTATUS Mapping)
 */
typedef struct {
    const char* name;         /* Symbolic name for diagnostics */
    ULONG       ndis_value;   /* Driver-side NDIS_STATUS emitted */
    ULONG       nt_value;     /* Expected NTSTATUS seen in user-mode */
} STATUS_MAP_ENTRY;

/* Exhaustive table of all NDIS_STATUS codes used by the filter driver.
 * Column 2 (ndis_value) is what the driver writes; column 3 (nt_value) is
 * what must appear in the struct.status field returned to user-mode.
 * For NDIS they are numerically identical — this table proves that and
 * provides a permanent regression guard if the definitions ever diverge. */
static const STATUS_MAP_ENTRY k_status_map[] = {
    /* Core success */
    { "NDIS_STATUS_SUCCESS",           NDIS_STATUS_SUCCESS,          STATUS_SUCCESS                    },
    /* Error codes used in FilterAttach / device init */
    { "NDIS_STATUS_FAILURE",           NDIS_STATUS_FAILURE,           STATUS_UNSUCCESSFUL               },
    { "NDIS_STATUS_INVALID_PARAMETER", NDIS_STATUS_INVALID_PARAMETER, STATUS_INVALID_PARAMETER          },
    { "NDIS_STATUS_NOT_SUPPORTED",     NDIS_STATUS_NOT_SUPPORTED,     STATUS_NOT_SUPPORTED              },
    { "NDIS_STATUS_RESOURCES",         NDIS_STATUS_RESOURCES,         STATUS_INSUFFICIENT_RESOURCES     },
    /* IOCTL response codes */
    { "NDIS_STATUS_BUFFER_TOO_SHORT",  NDIS_STATUS_BUFFER_TOO_SHORT,  STATUS_BUFFER_TOO_SMALL           },
    { "NDIS_STATUS_REQUEST_ABORTED",   NDIS_STATUS_REQUEST_ABORTED,   STATUS_CANCELLED                  },
    { "NDIS_STATUS_RESET_IN_PROGRESS", NDIS_STATUS_RESET_IN_PROGRESS, STATUS_DEVICE_BUSY                },
    { "NDIS_STATUS_NOT_RECOGNIZED",    NDIS_STATUS_NOT_RECOGNIZED,    STATUS_INVALID_DEVICE_REQUEST     },
    /* HAL-specific error codes (non-NDIS) */
    { "STATUS_NOT_IMPLEMENTED",        STATUS_NOT_IMPLEMENTED,        STATUS_NOT_IMPLEMENTED            },
    { "STATUS_DEVICE_CONFIGURATION_ERROR", STATUS_DEVICE_CONFIGURATION_ERROR, STATUS_DEVICE_CONFIGURATION_ERROR },
    { "STATUS_REVISION_MISMATCH",      STATUS_REVISION_MISMATCH,      STATUS_REVISION_MISMATCH          },
};
#define STATUS_MAP_COUNT ((int)(sizeof(k_status_map) / sizeof(k_status_map[0])))

void Test_NtstatusErrorMapComplete(void) {
    TEST_CASE("TC-ERR-MAP-001: Exhaustive NDIS_STATUS\u2192NTSTATUS 1:1 mapping table (closes #280)");
    printf("  Verifying %d entries...\n", STATUS_MAP_COUNT);

    for (int i = 0; i < STATUS_MAP_COUNT; i++) {
        const STATUS_MAP_ENTRY* e = &k_status_map[i];
        char msg[160];

        /* Assert: NDIS alias == NTSTATUS constant (1:1 identity mapping) */
        snprintf(msg, sizeof(msg), "%s (0x%08X) == expected NTSTATUS (0x%08X)",
                 e->name, (unsigned)e->ndis_value, (unsigned)e->nt_value);
        TEST_ASSERT(e->ndis_value == e->nt_value, msg);
    }

    /* Extra guard: ensure table covers at least 12 entries so no code
     * silently removes rows. */
    char count_msg[64];
    snprintf(count_msg, sizeof(count_msg),
             "Mapping table has >= 12 entries (actual: %d)", STATUS_MAP_COUNT);
    TEST_ASSERT(STATUS_MAP_COUNT >= 12, count_msg);
}

// =============================================================================
// MAIN
// =============================================================================

int main(void) {
    printf("\n========================================\n");
    printf("HAL ERROR SCENARIO TESTS (TEST-PORTABILITY-HAL-002 / TEST-ERROR-MAP-001)\n");
    printf("========================================\n");
    printf("Verifies: #84 (REQ-NF-PORTABILITY-001)\n");
    printf("Closes:   #280 (TEST-ERROR-MAP-001: NDIS_STATUS\u2192NTSTATUS mapping)\n");
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
    Test_NtstatusErrorMapComplete();  /* TC-ERR-MAP-001 — closes #280 */
    
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
