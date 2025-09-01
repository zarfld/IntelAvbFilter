/*
 * Intel AVB Filter Driver - Hardware State Test Tool
 * 
 * Purpose: Specialized test tool for hardware state management and transitions.
 * Tests the complete hardware initialization lifecycle and state management.
 * 
 * Features:
 * - Hardware state enumeration
 * - State transition testing
 * - Initialization sequence validation
 * - Recovery testing
 * - State consistency verification
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// Include the IOCTL definitions
#include "..\\..\\include\\avb_ioctl.h"

#define DEVICE_NAME L"\\\\.\\IntelAvbFilter"

typedef struct {
    HANDLE device;
    UINT32 adapter_count;
    UINT32 current_state;
    UINT16 vid, did;
    UINT32 caps;
} hw_state_context_t;

static hw_state_context_t g_ctx = {0};

/**
 * @brief Convert hardware state to readable string
 */
const char* hw_state_name(UINT32 state)
{
    switch (state) {
        case 0: return "UNINITIALIZED";
        case 1: return "INITIALIZING"; 
        case 2: return "READY";
        case 3: return "ERROR";
        case 4: return "BOUND";
        case 5: return "PTP_READY";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Initialize test context
 */
BOOL hw_state_init(void)
{
    printf("Intel AVB Filter Driver - Hardware State Test Tool\n");
    printf("==================================================\n");
    printf("Purpose: Test hardware state management and transitions\n\n");
    
    g_ctx.device = CreateFileW(DEVICE_NAME,
                              GENERIC_READ | GENERIC_WRITE, 
                              0, NULL, OPEN_EXISTING, 0, NULL);
    
    if (g_ctx.device == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        printf("? Failed to open device: %lu\n", error);
        return FALSE;
    }
    
    printf("? Device opened successfully\n\n");
    return TRUE;
}

/**
 * @brief Clean up test context
 */
void hw_state_cleanup(void)
{
    if (g_ctx.device && g_ctx.device != INVALID_HANDLE_VALUE) {
        CloseHandle(g_ctx.device);
        g_ctx.device = INVALID_HANDLE_VALUE;
    }
}

/**
 * @brief Get current hardware state
 */
BOOL hw_state_get_current(UINT32* state, UINT16* vid, UINT16* did, UINT32* caps)
{
    AVB_HW_STATE_QUERY query = {0};
    DWORD bytesReturned;
    
    BOOL result = DeviceIoControl(g_ctx.device, IOCTL_AVB_GET_HW_STATE,
                                 &query, sizeof(query), &query, sizeof(query),
                                 &bytesReturned, NULL);
    
    if (!result) {
        return FALSE;
    }
    
    if (state) *state = query.hw_state;
    if (vid) *vid = query.vendor_id;
    if (did) *did = query.device_id;
    if (caps) *caps = query.capabilities;
    
    return TRUE;
}

/**
 * @brief Test hardware state query functionality
 */
BOOL test_state_query(void)
{
    printf("?? === HARDWARE STATE QUERY TEST ===\n");
    
    UINT32 state;
    UINT16 vid, did;
    UINT32 caps;
    
    if (!hw_state_get_current(&state, &vid, &did, &caps)) {
        printf("? Hardware state query failed: %lu\n", GetLastError());
        return FALSE;
    }
    
    g_ctx.current_state = state;
    g_ctx.vid = vid;
    g_ctx.did = did;
    g_ctx.caps = caps;
    
    printf("?? Current Hardware State:\n");
    printf("    State: %u (%s)\n", state, hw_state_name(state));
    printf("    Device: 0x%04X:0x%04X\n", vid, did);
    printf("    Capabilities: 0x%08X\n", caps);
    
    return TRUE;
}

/**
 * @brief Test device initialization and state transitions
 */
BOOL test_initialization_sequence(void)
{
    printf("\n?? === INITIALIZATION SEQUENCE TEST ===\n");
    
    // Get initial state
    UINT32 initial_state;
    if (!hw_state_get_current(&initial_state, NULL, NULL, NULL)) {
        printf("? Failed to get initial state\n");
        return FALSE;
    }
    
    printf("?? Initial state: %u (%s)\n", initial_state, hw_state_name(initial_state));
    
    // Trigger device initialization
    AVB_INIT_REQUEST init_req = {0};
    DWORD bytesReturned;
    
    printf("?? Triggering device initialization...\n");
    BOOL result = DeviceIoControl(g_ctx.device, IOCTL_AVB_INIT_DEVICE,
                                 &init_req, sizeof(init_req), &init_req, sizeof(init_req),
                                 &bytesReturned, NULL);
    
    if (!result) {
        printf("? Device initialization failed: %lu\n", GetLastError());
        return FALSE;
    }
    
    printf("? Initialization IOCTL succeeded (status: 0x%08X)\n", init_req.status);
    
    // Check state after initialization
    UINT32 post_init_state;
    if (hw_state_get_current(&post_init_state, NULL, NULL, NULL)) {
        printf("?? Post-init state: %u (%s)\n", post_init_state, hw_state_name(post_init_state));
        
        if (post_init_state != initial_state) {
            printf("? State transition detected: %s ? %s\n", 
                   hw_state_name(initial_state), hw_state_name(post_init_state));
        } else {
            printf("??  No state transition (may already be initialized)\n");
        }
    }
    
    return TRUE;
}

/**
 * @brief Test state consistency across multiple queries
 */
BOOL test_state_consistency(void)
{
    printf("\n?? === STATE CONSISTENCY TEST ===\n");
    
    UINT32 states[5];
    UINT16 vids[5], dids[5]; 
    UINT32 caps[5];
    
    printf("?? Reading hardware state 5 times...\n");
    
    for (int i = 0; i < 5; i++) {
        if (!hw_state_get_current(&states[i], &vids[i], &dids[i], &caps[i])) {
            printf("? State query %d failed\n", i + 1);
            return FALSE;
        }
        
        printf("    [%d] State=%u VID=0x%04X DID=0x%04X Caps=0x%08X\n",
               i + 1, states[i], vids[i], dids[i], caps[i]);
        
        Sleep(50);  // Small delay between queries
    }
    
    // Check consistency
    BOOL consistent = TRUE;
    for (int i = 1; i < 5; i++) {
        if (states[i] != states[0] || vids[i] != vids[0] || 
            dids[i] != dids[0] || caps[i] != caps[0]) {
            consistent = FALSE;
            break;
        }
    }
    
    if (consistent) {
        printf("? Hardware state is consistent across queries\n");
    } else {
        printf("??  Hardware state inconsistency detected\n");
        printf("    This may indicate:\n");
        printf("    - Hardware state transitions during test\n");
        printf("    - Driver state management issues\n");
        printf("    - Multi-adapter context switching\n");
    }
    
    return consistent;
}

/**
 * @brief Test adapter enumeration impact on state
 */
BOOL test_enumeration_impact(void)
{
    printf("\n?? === ENUMERATION IMPACT TEST ===\n");
    
    // Get state before enumeration
    UINT32 state_before;
    if (!hw_state_get_current(&state_before, NULL, NULL, NULL)) {
        printf("? Failed to get state before enumeration\n");
        return FALSE;
    }
    
    printf("?? State before enumeration: %u (%s)\n", state_before, hw_state_name(state_before));
    
    // Perform adapter enumeration
    AVB_ENUM_ADAPTERS_REQUEST enum_req = {0};
    DWORD bytesReturned;
    
    printf("?? Performing adapter enumeration...\n");
    BOOL result = DeviceIoControl(g_ctx.device, IOCTL_AVB_ENUM_ADAPTERS,
                                 &enum_req, sizeof(enum_req), &enum_req, sizeof(enum_req),
                                 &bytesReturned, NULL);
    
    if (!result) {
        printf("? Adapter enumeration failed: %lu\n", GetLastError());
        return FALSE;
    }
    
    g_ctx.adapter_count = enum_req.count;
    printf("?? Found %u adapters\n", enum_req.count);
    
    // Get state after enumeration  
    UINT32 state_after;
    if (hw_state_get_current(&state_after, NULL, NULL, NULL)) {
        printf("?? State after enumeration: %u (%s)\n", state_after, hw_state_name(state_after));
        
        if (state_after != state_before) {
            printf("? Enumeration triggered state change: %s ? %s\n",
                   hw_state_name(state_before), hw_state_name(state_after));
        } else {
            printf("? State remains stable after enumeration\n");
        }
    }
    
    return TRUE;
}

/**
 * @brief Test multi-adapter state management
 */
BOOL test_multi_adapter_states(void)
{
    printf("\n?? === MULTI-ADAPTER STATE TEST ===\n");
    
    if (g_ctx.adapter_count <= 1) {
        printf("??  Only %u adapter(s) detected - skipping multi-adapter test\n", g_ctx.adapter_count);
        return TRUE;
    }
    
    printf("?? Testing state management with %u adapters\n", g_ctx.adapter_count);
    
    // Test adapter selection
    for (UINT32 i = 0; i < g_ctx.adapter_count && i < 3; i++) {  // Test up to 3 adapters
        AVB_OPEN_REQUEST open_req = {0};
        open_req.adapter_index = i;
        
        DWORD bytesReturned;
        BOOL result = DeviceIoControl(g_ctx.device, IOCTL_AVB_OPEN_ADAPTER,
                                     &open_req, sizeof(open_req), &open_req, sizeof(open_req),
                                     &bytesReturned, NULL);
        
        if (result) {
            printf("    ?? Adapter %u: Opened successfully\n", i);
            
            // Check state for this adapter
            UINT32 state;
            UINT16 vid, did;
            UINT32 caps;
            
            if (hw_state_get_current(&state, &vid, &did, &caps)) {
                printf("        State: %u (%s)\n", state, hw_state_name(state));
                printf("        Device: 0x%04X:0x%04X\n", vid, did);
                printf("        Capabilities: 0x%08X\n", caps);
            }
        } else {
            printf("    ? Adapter %u: Failed to open (%lu)\n", i, GetLastError());
        }
        
        Sleep(100);  // Small delay between adapter switches
    }
    
    return TRUE;
}

/**
 * @brief Test error state handling
 */
BOOL test_error_handling(void)
{
    printf("\n??  === ERROR STATE HANDLING TEST ===\n");
    
    // Try to trigger potential error conditions
    printf("?? Testing invalid register access...\n");
    
    AVB_REGISTER_REQUEST reg_req = {0};
    reg_req.offset = 0xFFFFFFFF;  // Invalid offset
    
    DWORD bytesReturned;
    BOOL result = DeviceIoControl(g_ctx.device, IOCTL_AVB_READ_REGISTER,
                                 &reg_req, sizeof(reg_req), &reg_req, sizeof(reg_req),
                                 &bytesReturned, NULL);
    
    if (result && reg_req.status != 0) {
        printf("? Invalid register access properly rejected (status: 0x%08X)\n", reg_req.status);
        
        // Check if this affects hardware state
        UINT32 current_state;
        if (hw_state_get_current(&current_state, NULL, NULL, NULL)) {
            printf("?? Hardware state after error: %u (%s)\n", current_state, hw_state_name(current_state));
            
            if (current_state == 3) {  // ERROR state
                printf("??  Hardware entered ERROR state after invalid operation\n");
                printf("    This is expected behavior for error handling\n");
                
                // Try to recover by reinitializing
                printf("?? Attempting recovery via reinitialization...\n");
                AVB_INIT_REQUEST init_req = {0};
                DeviceIoControl(g_ctx.device, IOCTL_AVB_INIT_DEVICE,
                               &init_req, sizeof(init_req), &init_req, sizeof(init_req),
                               &bytesReturned, NULL);
                
                if (hw_state_get_current(&current_state, NULL, NULL, NULL)) {
                    printf("?? State after recovery: %u (%s)\n", current_state, hw_state_name(current_state));
                }
            }
        }
    } else {
        printf("??  Invalid register access was not rejected\n");
    }
    
    return TRUE;
}

/**
 * @brief Generate hardware state report
 */
void generate_state_report(void)
{
    printf("\n?? === HARDWARE STATE TEST REPORT ===\n");
    
    printf("???  Final System State:\n");
    UINT32 final_state;
    UINT16 vid, did;
    UINT32 caps;
    
    if (hw_state_get_current(&final_state, &vid, &did, &caps)) {
        printf("    Current State: %u (%s)\n", final_state, hw_state_name(final_state));
        printf("    Device: 0x%04X:0x%04X\n", vid, did);
        printf("    Capabilities: 0x%08X\n", caps);
        printf("    Adapter Count: %u\n", g_ctx.adapter_count);
    }
    
    printf("\n?? State Management Analysis:\n");
    
    switch (final_state) {
        case 0:  // UNINITIALIZED
            printf("    ??  Hardware is uninitialized\n");
            printf("        - Run device initialization\n");
            printf("        - Check hardware connectivity\n");
            break;
            
        case 1:  // INITIALIZING
            printf("    ?? Hardware is initializing\n");
            printf("        - Wait for initialization to complete\n");
            printf("        - Monitor state transitions\n");
            break;
            
        case 2:  // READY
            printf("    ? Hardware is ready for operations\n");
            printf("        - All features should be available\n");
            printf("        - Begin application testing\n");
            break;
            
        case 3:  // ERROR
            printf("    ? Hardware is in error state\n");
            printf("        - Check previous operations for errors\n");
            printf("        - Attempt reinitialization\n");
            printf("        - Verify hardware connectivity\n");
            break;
            
        case 4:  // BOUND
            printf("    ?? Hardware is bound but not fully initialized\n");
            printf("        - Trigger full initialization sequence\n");
            printf("        - Check adapter enumeration\n");
            break;
            
        case 5:  // PTP_READY
            printf("    ? Hardware has PTP subsystem ready\n");
            printf("        - IEEE 1588 operations available\n");
            printf("        - Timestamp functions operational\n");
            break;
            
        default:
            printf("    ? Unknown hardware state\n");
            printf("        - Check driver version compatibility\n");
            printf("        - Verify hardware support\n");
            break;
    }
    
    printf("\n?? Hardware State Test Complete!\n");
}

/**
 * @brief Main hardware state test program
 */
int main(void)
{
    if (!hw_state_init()) {
        return 1;
    }
    
    BOOL success = TRUE;
    
    // Run all hardware state tests
    if (!test_state_query()) success = FALSE;
    if (!test_initialization_sequence()) success = FALSE;
    if (!test_state_consistency()) success = FALSE;
    if (!test_enumeration_impact()) success = FALSE;
    if (!test_multi_adapter_states()) success = FALSE;
    if (!test_error_handling()) success = FALSE;
    
    // Generate final report
    generate_state_report();
    
    hw_state_cleanup();
    
    printf("\n");
    if (success) {
        printf("? All hardware state tests completed successfully\n");
        return 0;
    } else {
        printf("??  Some hardware state tests failed\n");
        return 1;
    }
}