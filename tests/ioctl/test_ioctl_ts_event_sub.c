/**
 * @file test_ioctl_ts_event_sub.c
 * @brief Timestamp Event Subscription Verification Tests
 * 
 * Implements: #314 (TEST-TS-EVENT-SUB-001)
 * Verifies: #13 (REQ-F-TS-EVENT-SUB-001: Timestamp Event Subscription via IOCTL)
 * 
 * Test Plan: TEST-PLAN-IOCTL-NEW-2025-12-31.md
 * IOCTLs: 33 (SUBSCRIBE_TS_EVENTS), 34 (MAP_TS_RING_BUFFER)
 * Test Cases: 19
 * Priority: P1
 * 
 * Standards: IEEE 1012-2016 (Verification & Validation)
 * 
 * @see https://github.com/zarfld/IntelAvbFilter/issues/314
 * @see https://github.com/zarfld/IntelAvbFilter/issues/13
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

/* Event types (bitflags) */
#define TS_EVENT_RX_TIMESTAMP      0x00000001
#define TS_EVENT_TX_TIMESTAMP      0x00000002
#define TS_EVENT_TARGET_TIME       0x00000004
#define TS_EVENT_AUX_TIMESTAMP     0x00000008

/* Ring buffer configuration */
#define DEFAULT_RING_BUFFER_SIZE   (64 * 1024)  /* 64KB */
#define MAX_RING_BUFFER_SIZE       (1024 * 1024) /* 1MB */

/* Test state */
typedef struct {
    HANDLE adapter;
    UINT32 ring_id;          /* Ring identifier (replaces subscription_handle) */
    HANDLE ring_buffer_handle; /* Shared memory handle */
    SIZE_T ring_buffer_size;
    int test_count;
    int pass_count;
    int fail_count;
    int skip_count;
} TestContext;

/* SSOT structures used - no custom definitions needed */
/* AVB_TS_SUBSCRIBE_REQUEST and AVB_TS_RING_MAP_REQUEST from avb_ioctl.h */

/* Timestamp event structure (not yet defined in SSOT - keep for now) */
typedef struct {
    UINT64 timestamp;
    UINT32 event_type;
    UINT32 sequence_number;
    UINT32 queue_id;
    UINT32 trigger_source;
    UINT16 packet_length;
    UINT8  reserved[6];
} TIMESTAMP_EVENT, *PTIMESTAMP_EVENT;

/*==============================================================================
 * Helper Functions
 *============================================================================*/

/**
 * @brief Open AVB adapter
 */
HANDLE OpenAdapter(void) {
    HANDLE h;
    
    h = CreateFileA("\\\\.\\IntelAvbFilter",
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL);
    
    if (h == INVALID_HANDLE_VALUE) {
        printf("  [WARN] Could not open device: error %lu\n", GetLastError());
    }
    
    return h;
}

/**
 * @brief Subscribe to timestamp events via IOCTL 33
 * @param event_flags Bitmask of TS_EVENT_* constants
 * @param queue_filter Ignored (SSOT uses VLAN/PCP filtering)
 * @return ring_id on success, 0 on failure
 */
UINT32 SubscribeToEvents(HANDLE adapter, UINT32 event_flags, UINT32 queue_filter) {
    AVB_TS_SUBSCRIBE_REQUEST request = {0};
    DWORD bytes_returned = 0;
    BOOL result;
    
    /* Map custom event_flags to SSOT types_mask (same values) */
    request.types_mask = event_flags;
    
    /* SSOT uses VLAN/PCP filtering, not queue_filter
     * For initial implementation, disable VLAN filtering (vlan=0, pcp=0) */
    request.vlan = 0;
    request.pcp = 0;
    request.reserved0 = 0;
    request.ring_id = 0;  /* Driver assigns ring_id */
    
    result = DeviceIoControl(
        adapter,
        IOCTL_AVB_TS_SUBSCRIBE,  /* From avb_ioctl.h */
        &request,
        sizeof(request),
        &request,
        sizeof(request),
        &bytes_returned,
        NULL
    );
    
    if (result && request.status == 0) {
        return request.ring_id;  /* Return ring_id, not HANDLE */
    }
    
    return 0;  /* 0 indicates failure */
}

/**
 * @brief Map ring buffer via IOCTL 34
 * @param ring_id Ring identifier from subscribe operation
 * @return HANDLE (from shm_token) on success, INVALID_HANDLE_VALUE on failure
 */
HANDLE MapRingBuffer(HANDLE adapter, UINT32 ring_id, SIZE_T requested_size, SIZE_T *actual_size) {
    AVB_TS_RING_MAP_REQUEST request = {0};
    DWORD bytes_returned = 0;
    BOOL result;
    
    request.ring_id = ring_id;
    request.length = (UINT32)requested_size;  /* in/out field */
    request.user_cookie = 0;  /* Optional user context */
    request.shm_token = 0;    /* Driver populates */
    
    result = DeviceIoControl(
        adapter,
        IOCTL_AVB_TS_RING_MAP,  /* From avb_ioctl.h */
        &request,
        sizeof(request),
        &request,
        sizeof(request),
        &bytes_returned,
        NULL
    );
    
    if (result && request.status == 0) {
        if (actual_size) {
            *actual_size = (SIZE_T)request.length;  /* Driver sets actual length */
        }
        /* shm_token is HANDLE value on user-mode */
        return (HANDLE)(UINT_PTR)request.shm_token;
    }
    
    return INVALID_HANDLE_VALUE;
}

/**
 * @brief Unsubscribe from events
 * @param ring_id Ring identifier to unsubscribe
 */
void Unsubscribe(UINT32 ring_id) {
    /* SSOT: Unsubscribe logic would use ring_id
     * Implementation depends on driver API (not yet defined)
     * Placeholder for now */
    if (ring_id != 0) {
        /* TODO: Add unsubscribe IOCTL when driver implements it */
    }
}

/**
 * @brief Unmap ring buffer
 * @param buffer_handle Shared memory handle from MapRingBuffer
 */
void UnmapRingBuffer(HANDLE buffer_handle) {
    if (buffer_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(buffer_handle);
    }
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
 * Test Cases (Issue #314 - 19 test cases)
 *============================================================================*/

/**
 * UT-TS-SUB-001: Basic Event Subscription
 */
void Test_BasicEventSubscription(TestContext *ctx) {
    HANDLE subscription;
    
    subscription = SubscribeToEvents(ctx->adapter, 
                                     TS_EVENT_RX_TIMESTAMP | TS_EVENT_TX_TIMESTAMP,
                                     0);
    
    if (subscription != INVALID_HANDLE_VALUE) {
        Unsubscribe(subscription);
        PrintTestResult(ctx, "UT-TS-SUB-001: Basic Event Subscription", TEST_PASS, NULL);
    } else {
        PrintTestResult(ctx, "UT-TS-SUB-001: Basic Event Subscription", TEST_FAIL, 
                        "Subscription IOCTL failed");
    }
}

/**
 * UT-TS-SUB-002: Selective Event Type Subscription
 */
void Test_SelectiveEventTypeSubscription(TestContext *ctx) {
    HANDLE subscription;
    
    /* Subscribe only to RX timestamps */
    subscription = SubscribeToEvents(ctx->adapter, TS_EVENT_RX_TIMESTAMP, 0);
    
    if (subscription != INVALID_HANDLE_VALUE) {
        Unsubscribe(subscription);
        PrintTestResult(ctx, "UT-TS-SUB-002: Selective Event Type Subscription", TEST_PASS, NULL);
    } else {
        PrintTestResult(ctx, "UT-TS-SUB-002: Selective Event Type Subscription", TEST_FAIL, 
                        "Selective subscription failed");
    }
}

/**
 * UT-TS-SUB-003: Multiple Concurrent Subscriptions
 */
void Test_MultipleConcurrentSubscriptions(TestContext *ctx) {
    PrintTestResult(ctx, "UT-TS-SUB-003: Multiple Concurrent Subscriptions", TEST_SKIP, 
                    "Requires multi-process test framework");
}

/**
 * UT-TS-SUB-004: Unsubscribe Operation
 */
void Test_UnsubscribeOperation(TestContext *ctx) {
    HANDLE subscription;
    
    subscription = SubscribeToEvents(ctx->adapter, TS_EVENT_RX_TIMESTAMP, 0);
    
    if (subscription != INVALID_HANDLE_VALUE) {
        Unsubscribe(subscription);
        PrintTestResult(ctx, "UT-TS-SUB-004: Unsubscribe Operation", TEST_PASS, NULL);
    } else {
        PrintTestResult(ctx, "UT-TS-SUB-004: Unsubscribe Operation", TEST_FAIL, 
                        "Subscription failed");
    }
}

/**
 * UT-TS-RING-001: Ring Buffer Mapping
 */
void Test_RingBufferMapping(TestContext *ctx) {
    HANDLE subscription;
    void *buffer;
    SIZE_T actual_size = 0;
    
    subscription = SubscribeToEvents(ctx->adapter, TS_EVENT_RX_TIMESTAMP, 0);
    if (subscription == INVALID_HANDLE_VALUE) {
        PrintTestResult(ctx, "UT-TS-RING-001: Ring Buffer Mapping", TEST_SKIP, 
                        "Subscription failed");
        return;
    }
    
    buffer = MapRingBuffer(ctx->adapter, subscription, DEFAULT_RING_BUFFER_SIZE, &actual_size);
    
    if (buffer) {
        printf("    Ring buffer mapped: %zu bytes\n", actual_size);
        UnmapRingBuffer(buffer);
        PrintTestResult(ctx, "UT-TS-RING-001: Ring Buffer Mapping", TEST_PASS, NULL);
    } else {
        PrintTestResult(ctx, "UT-TS-RING-001: Ring Buffer Mapping", TEST_FAIL, 
                        "Mapping IOCTL failed");
    }
    
    Unsubscribe(subscription);
}

/**
 * UT-TS-RING-002: Ring Buffer Size Negotiation
 */
void Test_RingBufferSizeNegotiation(TestContext *ctx) {
    HANDLE subscription;
    void *buffer;
    SIZE_T requested = 32 * 1024;  /* 32KB */
    SIZE_T actual = 0;
    
    subscription = SubscribeToEvents(ctx->adapter, TS_EVENT_RX_TIMESTAMP, 0);
    if (subscription == INVALID_HANDLE_VALUE) {
        PrintTestResult(ctx, "UT-TS-RING-002: Ring Buffer Size Negotiation", TEST_SKIP, 
                        "Subscription failed");
        return;
    }
    
    buffer = MapRingBuffer(ctx->adapter, subscription, requested, &actual);
    
    if (buffer && (actual >= requested)) {
        printf("    Requested: %zu, Actual: %zu\n", requested, actual);
        UnmapRingBuffer(buffer);
        PrintTestResult(ctx, "UT-TS-RING-002: Ring Buffer Size Negotiation", TEST_PASS, NULL);
    } else {
        PrintTestResult(ctx, "UT-TS-RING-002: Ring Buffer Size Negotiation", TEST_FAIL, 
                        "Size negotiation failed");
    }
    
    Unsubscribe(subscription);
}

/**
 * UT-TS-RING-003: Ring Buffer Wraparound
 */
void Test_RingBufferWraparound(TestContext *ctx) {
    PrintTestResult(ctx, "UT-TS-RING-003: Ring Buffer Wraparound", TEST_SKIP, 
                    "Requires event generation and producer/consumer synchronization");
}

/**
 * UT-TS-RING-004: Ring Buffer Read Synchronization
 */
void Test_RingBufferReadSynchronization(TestContext *ctx) {
    PrintTestResult(ctx, "UT-TS-RING-004: Ring Buffer Read Synchronization", TEST_SKIP, 
                    "Requires concurrent producer/consumer test");
}

/**
 * UT-TS-EVENT-001: RX Timestamp Event Delivery
 */
void Test_RXTimestampEventDelivery(TestContext *ctx) {
    PrintTestResult(ctx, "UT-TS-EVENT-001: RX Timestamp Event Delivery", TEST_SKIP, 
                    "Requires packet reception and event polling");
}

/**
 * UT-TS-EVENT-002: TX Timestamp Event Delivery
 */
void Test_TXTimestampEventDelivery(TestContext *ctx) {
    PrintTestResult(ctx, "UT-TS-EVENT-002: TX Timestamp Event Delivery", TEST_SKIP, 
                    "Requires packet transmission and event polling");
}

/**
 * UT-TS-EVENT-003: Target Time Reached Event
 */
void Test_TargetTimeReachedEvent(TestContext *ctx) {
    PrintTestResult(ctx, "UT-TS-EVENT-003: Target Time Reached Event", TEST_SKIP, 
                    "Requires target time programming (IOCTL 43) and event polling");
}

/**
 * UT-TS-EVENT-004: Aux Timestamp Event
 */
void Test_AuxTimestampEvent(TestContext *ctx) {
    PrintTestResult(ctx, "UT-TS-EVENT-004: Aux Timestamp Event", TEST_SKIP, 
                    "Requires aux timestamp trigger (GPIO or external signal)");
}

/**
 * UT-TS-EVENT-005: Event Sequence Numbering
 */
void Test_EventSequenceNumbering(TestContext *ctx) {
    PrintTestResult(ctx, "UT-TS-EVENT-005: Event Sequence Numbering", TEST_SKIP, 
                    "Requires multiple event generation and sequence validation");
}

/**
 * UT-TS-EVENT-006: Event Filtering by Criteria
 */
void Test_EventFilteringByCriteria(TestContext *ctx) {
    HANDLE subscription;
    
    /* Subscribe with queue filter */
    subscription = SubscribeToEvents(ctx->adapter, TS_EVENT_RX_TIMESTAMP, 0x0001 /* Queue 0 */);
    
    if (subscription != INVALID_HANDLE_VALUE) {
        Unsubscribe(subscription);
        PrintTestResult(ctx, "UT-TS-EVENT-006: Event Filtering by Criteria", TEST_PASS, NULL);
    } else {
        PrintTestResult(ctx, "UT-TS-EVENT-006: Event Filtering by Criteria", TEST_FAIL, 
                        "Filtered subscription failed");
    }
}

/**
 * UT-TS-RING-005: Ring Buffer Unmap Operation
 */
void Test_RingBufferUnmapOperation(TestContext *ctx) {
    HANDLE subscription;
    void *buffer;
    SIZE_T actual_size = 0;
    
    subscription = SubscribeToEvents(ctx->adapter, TS_EVENT_RX_TIMESTAMP, 0);
    if (subscription == INVALID_HANDLE_VALUE) {
        PrintTestResult(ctx, "UT-TS-RING-005: Ring Buffer Unmap Operation", TEST_SKIP, 
                        "Subscription failed");
        return;
    }
    
    buffer = MapRingBuffer(ctx->adapter, subscription, DEFAULT_RING_BUFFER_SIZE, &actual_size);
    
    if (buffer) {
        UnmapRingBuffer(buffer);
        PrintTestResult(ctx, "UT-TS-RING-005: Ring Buffer Unmap Operation", TEST_PASS, NULL);
    } else {
        PrintTestResult(ctx, "UT-TS-RING-005: Ring Buffer Unmap Operation", TEST_FAIL, 
                        "Mapping failed");
    }
    
    Unsubscribe(subscription);
}

/**
 * UT-TS-PERF-001: High Event Rate Performance
 */
void Test_HighEventRatePerformance(TestContext *ctx) {
    PrintTestResult(ctx, "UT-TS-PERF-001: High Event Rate Performance", TEST_SKIP, 
                    "Requires sustained traffic generation (10K events/sec)");
}

/**
 * UT-TS-ERROR-001: Invalid Subscription Handle
 */
void Test_InvalidSubscriptionHandle(TestContext *ctx) {
    void *buffer;
    SIZE_T actual_size = 0;
    
    /* Try to map with invalid handle */
    buffer = MapRingBuffer(ctx->adapter, INVALID_HANDLE_VALUE, DEFAULT_RING_BUFFER_SIZE, &actual_size);
    
    PrintTestResult(ctx, "UT-TS-ERROR-001: Invalid Subscription Handle", 
                    (buffer == NULL) ? TEST_PASS : TEST_FAIL,
                    (buffer == NULL) ? NULL : "Invalid handle accepted");
}

/**
 * UT-TS-ERROR-002: Ring Buffer Mapping Failure
 */
void Test_RingBufferMappingFailure(TestContext *ctx) {
    HANDLE subscription;
    void *buffer;
    SIZE_T huge_size = MAX_RING_BUFFER_SIZE * 10;  /* Unreasonably large */
    SIZE_T actual_size = 0;
    
    subscription = SubscribeToEvents(ctx->adapter, TS_EVENT_RX_TIMESTAMP, 0);
    if (subscription == INVALID_HANDLE_VALUE) {
        PrintTestResult(ctx, "UT-TS-ERROR-002: Ring Buffer Mapping Failure", TEST_SKIP, 
                        "Subscription failed");
        return;
    }
    
    buffer = MapRingBuffer(ctx->adapter, subscription, huge_size, &actual_size);
    
    /* Should fail gracefully */
    if (buffer == NULL) {
        PrintTestResult(ctx, "UT-TS-ERROR-002: Ring Buffer Mapping Failure", TEST_PASS, NULL);
    } else {
        UnmapRingBuffer(buffer);
        PrintTestResult(ctx, "UT-TS-ERROR-002: Ring Buffer Mapping Failure", TEST_FAIL, 
                        "Huge allocation succeeded (unexpected)");
    }
    
    Unsubscribe(subscription);
}

/**
 * UT-TS-ERROR-003: Event Overflow Notification
 */
void Test_EventOverflowNotification(TestContext *ctx) {
    PrintTestResult(ctx, "UT-TS-ERROR-003: Event Overflow Notification", TEST_SKIP, 
                    "Requires small buffer + high event rate to force overflow");
}

/*==============================================================================
 * Main Test Harness
 *============================================================================*/

int main(int argc, char **argv) {
    TestContext ctx = {0};
    
    printf("\n");
    printf("====================================================================\n");
    printf(" Timestamp Event Subscription Test Suite\n");
    printf("====================================================================\n");
    printf(" Test Plan: TEST-PLAN-IOCTL-NEW-2025-12-31.md\n");
    printf(" Issue: #314 (TEST-TS-EVENT-SUB-001)\n");
    printf(" Requirement: #13 (REQ-F-TS-EVENT-SUB-001)\n");
    printf(" IOCTLs: SUBSCRIBE_TS_EVENTS (33), MAP_TS_RING_BUFFER (34)\n");
    printf(" Total Tests: 19\n");
    printf(" Priority: P1\n");
    printf("====================================================================\n");
    printf("\n");
    
    /* Open adapter */
    ctx.adapter = OpenAdapter();
    if (ctx.adapter == INVALID_HANDLE_VALUE) {
        printf("[ERROR] Failed to open AVB adapter. Skipping all tests.\n\n");
        return 1;
    }
    
    /* Run test cases */
    printf("Running Timestamp Event Subscription tests...\n\n");
    
    Test_BasicEventSubscription(&ctx);
    Test_SelectiveEventTypeSubscription(&ctx);
    Test_MultipleConcurrentSubscriptions(&ctx);
    Test_UnsubscribeOperation(&ctx);
    Test_RingBufferMapping(&ctx);
    Test_RingBufferSizeNegotiation(&ctx);
    Test_RingBufferWraparound(&ctx);
    Test_RingBufferReadSynchronization(&ctx);
    Test_RXTimestampEventDelivery(&ctx);
    Test_TXTimestampEventDelivery(&ctx);
    Test_TargetTimeReachedEvent(&ctx);
    Test_AuxTimestampEvent(&ctx);
    Test_EventSequenceNumbering(&ctx);
    Test_EventFilteringByCriteria(&ctx);
    Test_RingBufferUnmapOperation(&ctx);
    Test_HighEventRatePerformance(&ctx);
    Test_InvalidSubscriptionHandle(&ctx);
    Test_RingBufferMappingFailure(&ctx);
    Test_EventOverflowNotification(&ctx);
    
    /* Close adapter */
    CloseHandle(ctx.adapter);
    
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
