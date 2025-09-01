/*
 * Intel AVB Filter Driver - Diagnostic Test Tool
 * 
 * Purpose: Provides comprehensive hardware diagnostics and troubleshooting
 * capabilities for Intel AVB-enabled network adapters.
 * 
 * Features:
 * - Hardware state analysis
 * - Register access validation
 * - Capability verification
 * - Performance diagnostics
 * - Error analysis and reporting
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
    BOOL initialized;
    UINT32 adapter_count;
    UINT16 current_vid;
    UINT16 current_did;
    UINT32 current_caps;
} diagnostic_context_t;

static diagnostic_context_t g_ctx = {0};

/**
 * @brief Initialize diagnostic context
 */
BOOL diagnostic_init(void) 
{
    printf("Intel AVB Filter Driver - Comprehensive Diagnostic Tool\n");
    printf("=======================================================\n");
    printf("Purpose: Hardware diagnostics and troubleshooting\n\n");
    
    g_ctx.device = CreateFileW(DEVICE_NAME, 
                              GENERIC_READ | GENERIC_WRITE,
                              0, NULL, OPEN_EXISTING, 0, NULL);
    
    if (g_ctx.device == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        printf("? Failed to open device: %lu\n", error);
        printf("   Make sure Intel AVB Filter driver is installed and Intel hardware present\n\n");
        return FALSE;
    }
    
    printf("? Device opened successfully\n\n");
    g_ctx.initialized = TRUE;
    return TRUE;
}

/**
 * @brief Clean up diagnostic context
 */
void diagnostic_cleanup(void)
{
    if (g_ctx.device && g_ctx.device != INVALID_HANDLE_VALUE) {
        CloseHandle(g_ctx.device);
        g_ctx.device = INVALID_HANDLE_VALUE;
    }
    g_ctx.initialized = FALSE;
}

/**
 * @brief Enumerate and analyze all available adapters
 */
BOOL diagnostic_enumerate_adapters(void)
{
    AVB_ENUM_ADAPTERS_REQUEST req = {0};
    DWORD bytesReturned;
    
    printf("?? === ADAPTER ENUMERATION DIAGNOSTIC ===\n");
    
    BOOL result = DeviceIoControl(g_ctx.device, IOCTL_AVB_ENUM_ADAPTERS,
                                 &req, sizeof(req), &req, sizeof(req),
                                 &bytesReturned, NULL);
    
    if (!result) {
        printf("? Adapter enumeration failed: %lu\n", GetLastError());
        return FALSE;
    }
    
    g_ctx.adapter_count = req.count;
    g_ctx.current_vid = req.vendor_id;  
    g_ctx.current_did = req.device_id;
    g_ctx.current_caps = req.capabilities;
    
    printf("?? Total Intel AVB adapters found: %u\n", g_ctx.adapter_count);
    
    if (g_ctx.adapter_count == 0) {
        printf("??  No Intel AVB adapters detected\n");
        printf("    Possible causes:\n");
        printf("    - No Intel network adapters installed\n"); 
        printf("    - Driver not properly bound to adapters\n");
        printf("    - Unsupported Intel controller model\n");
        return FALSE;
    }
    
    printf("    Primary Adapter: VID=0x%04X DID=0x%04X\n", g_ctx.current_vid, g_ctx.current_did);
    printf("    Capabilities: 0x%08X\n", g_ctx.current_caps);
    
    // Analyze capabilities
    printf("    ?? Capability Analysis:\n");
    if (g_ctx.current_caps & INTEL_CAP_BASIC_1588) printf("      ? BASIC_1588 (IEEE 1588 support)\n");
    if (g_ctx.current_caps & INTEL_CAP_ENHANCED_TS) printf("      ? ENHANCED_TS (Enhanced timestamping)\n");
    if (g_ctx.current_caps & INTEL_CAP_TSN_TAS) printf("      ? TSN_TAS (Time-Aware Shaper)\n");
    if (g_ctx.current_caps & INTEL_CAP_TSN_FP) printf("      ? TSN_FP (Frame Preemption)\n");
    if (g_ctx.current_caps & INTEL_CAP_PCIe_PTM) printf("      ? PCIe_PTM (Precision Time Measurement)\n");
    if (g_ctx.current_caps & INTEL_CAP_2_5G) printf("      ? 2_5G (2.5 Gigabit support)\n");
    if (g_ctx.current_caps & INTEL_CAP_MMIO) printf("      ? MMIO (Memory-mapped I/O)\n");
    if (g_ctx.current_caps & INTEL_CAP_MDIO) printf("      ? MDIO (Management Data I/O)\n");
    
    return TRUE;
}

/**
 * @brief Test hardware state and initialization
 */
BOOL diagnostic_hardware_state(void)
{
    AVB_HW_STATE_QUERY query = {0};
    DWORD bytesReturned;
    
    printf("\n?? === HARDWARE STATE DIAGNOSTIC ===\n");
    
    BOOL result = DeviceIoControl(g_ctx.device, IOCTL_AVB_GET_HW_STATE,
                                 &query, sizeof(query), &query, sizeof(query),
                                 &bytesReturned, NULL);
    
    if (!result) {
        printf("? Hardware state query failed: %lu\n", GetLastError());
        return FALSE;
    }
    
    printf("?? Hardware State Analysis:\n");
    printf("    Current State: %u ", query.hw_state);
    
    switch (query.hw_state) {
        case 0: printf("(UNINITIALIZED)\n"); break;
        case 1: printf("(INITIALIZING)\n"); break;
        case 2: printf("(READY)\n"); break;
        case 3: printf("(ERROR)\n"); break;
        default: printf("(UNKNOWN)\n"); break;
    }
    
    printf("    Vendor ID: 0x%04X\n", query.vendor_id);
    printf("    Device ID: 0x%04X\n", query.device_id);
    printf("    Capabilities: 0x%08X\n", query.capabilities);
    
    // Provide recommendations based on state
    if (query.hw_state == 0) {
        printf("??  Hardware not initialized - trigger initialization\n");
    } else if (query.hw_state == 3) {
        printf("? Hardware in error state - check previous operations\n");
    } else if (query.hw_state == 2) {
        printf("? Hardware ready for operations\n");
    }
    
    return TRUE;
}

/**
 * @brief Test critical register access
 */
BOOL diagnostic_register_access(void)
{
    printf("\n?? === REGISTER ACCESS DIAGNOSTIC ===\n");
    
    // Test common registers based on device type
    UINT32 test_registers[] = {
        0x00000,  // CTRL
        0x00008,  // STATUS  
        0x0B600,  // SYSTIML (I210/I226)
        0x0B604,  // SYSTIMH (I210/I226)
        0x0B608,  // TIMINCA (I210/I226)
        0x0B640   // TSAUXC (I210/I226)
    };
    
    size_t num_registers = sizeof(test_registers) / sizeof(test_registers[0]);
    UINT32 successful_reads = 0;
    
    for (size_t i = 0; i < num_registers; i++) {
        AVB_REGISTER_REQUEST req = {0};
        req.offset = test_registers[i];
        
        DWORD bytesReturned;
        BOOL result = DeviceIoControl(g_ctx.device, IOCTL_AVB_READ_REGISTER,
                                     &req, sizeof(req), &req, sizeof(req),
                                     &bytesReturned, NULL);
        
        if (result && req.status == 0) {
            printf("    ? REG[0x%05X]: 0x%08X\n", req.offset, req.value);
            successful_reads++;
        } else {
            printf("    ? REG[0x%05X]: Read failed (status: 0x%08X)\n", req.offset, req.status);
        }
    }
    
    printf("?? Register Access Summary: %u/%u successful\n", 
           successful_reads, (UINT32)num_registers);
    
    if (successful_reads == 0) {
        printf("? No register access working - check hardware connectivity\n");
        return FALSE;
    } else if (successful_reads < num_registers) {
        printf("??  Partial register access - some features may be unavailable\n");
    } else {
        printf("? Full register access working\n");
    }
    
    return TRUE;
}

/**
 * @brief Test timestamp functionality
 */
BOOL diagnostic_timestamp_test(void)
{
    printf("\n? === TIMESTAMP DIAGNOSTIC ===\n");
    
    AVB_TIMESTAMP_REQUEST req = {0};
    req.clock_id = 0;  // Default clock
    
    DWORD bytesReturned;
    BOOL result = DeviceIoControl(g_ctx.device, IOCTL_AVB_GET_TIMESTAMP,
                                 &req, sizeof(req), &req, sizeof(req),
                                 &bytesReturned, NULL);
    
    if (!result) {
        DWORD error = GetLastError();
        printf("? Timestamp request failed: %lu\n", error);
        return FALSE;
    }
    
    if (req.status != 0) {
        printf("? Timestamp operation failed: 0x%08X\n", req.status);
        return FALSE;
    }
    
    printf("?? Timestamp Test Results:\n");
    printf("    Initial timestamp: 0x%016llX\n", req.timestamp);
    
    // Test timestamp advancement
    Sleep(100);  // Wait 100ms
    
    AVB_TIMESTAMP_REQUEST req2 = {0};
    req2.clock_id = 0;
    
    result = DeviceIoControl(g_ctx.device, IOCTL_AVB_GET_TIMESTAMP,
                            &req2, sizeof(req2), &req2, sizeof(req2),
                            &bytesReturned, NULL);
    
    if (result && req2.status == 0) {
        printf("    Second timestamp:  0x%016llX\n", req2.timestamp);
        
        if (req2.timestamp > req.timestamp) {
            UINT64 diff = req2.timestamp - req.timestamp;
            printf("    ? Timestamp advancing (delta: %llu)\n", diff);
        } else if (req2.timestamp == req.timestamp) {
            printf("    ??  Timestamp not advancing - clock may be stuck\n");
        } else {
            printf("    ? Timestamp going backwards - clock error\n");
        }
    }
    
    return TRUE;
}

/**
 * @brief Test device information retrieval
 */
BOOL diagnostic_device_info(void)
{
    printf("\n?? === DEVICE INFORMATION DIAGNOSTIC ===\n");
    
    AVB_DEVICE_INFO_REQUEST req = {0};
    
    DWORD bytesReturned;
    BOOL result = DeviceIoControl(g_ctx.device, IOCTL_AVB_GET_DEVICE_INFO,
                                 &req, sizeof(req), &req, sizeof(req),
                                 &bytesReturned, NULL);
    
    if (!result) {
        printf("? Device info request failed: %lu\n", GetLastError());
        return FALSE;
    }
    
    if (req.status != 0) {
        printf("? Device info operation failed: 0x%08X\n", req.status);
        return FALSE;
    }
    
    printf("?? Device Information:\n");
    printf("    Description: %.63s\n", req.info_string);
    printf("    Buffer used: %u bytes\n", req.buffer_size);
    printf("    ? Device info retrieval working\n");
    
    return TRUE;
}

/**
 * @brief Generate comprehensive diagnostic report
 */
void diagnostic_generate_report(void)
{
    printf("\n?? === COMPREHENSIVE DIAGNOSTIC REPORT ===\n");
    
    printf("???  System Configuration:\n");
    printf("    Total Adapters: %u\n", g_ctx.adapter_count);
    printf("    Primary Device: 0x%04X:0x%04X\n", g_ctx.current_vid, g_ctx.current_did);
    printf("    Capabilities: 0x%08X\n", g_ctx.current_caps);
    
    // Device type analysis
    printf("\n?? Device Type Analysis:\n");
    if (g_ctx.current_did == 0x1533) {
        printf("    ?? Intel I210 detected\n");
        printf("        - IEEE 1588 PTP support\n");
        printf("        - Enhanced timestamping\n");  
        printf("        - MMIO register access\n");
    } else if (g_ctx.current_did == 0x125B) {
        printf("    ?? Intel I226-LM detected\n");
        printf("        - Full TSN support (TAS + FP)\n");
        printf("        - 2.5 Gigabit capability\n");
        printf("        - PCIe PTM support\n");
        printf("        - Energy Efficient Ethernet\n");
    } else if (g_ctx.current_did == 0x15F2) {
        printf("    ?? Intel I225-LM detected\n");
        printf("        - TSN support (TAS + FP)\n");
        printf("        - 2.5 Gigabit capability\n");
    } else {
        printf("    ?? Intel device 0x%04X\n", g_ctx.current_did);
        printf("        - Check Intel specifications for capabilities\n");
    }
    
    // Feature availability
    printf("\n? Feature Availability:\n");
    printf("    IEEE 1588 PTP: %s\n", (g_ctx.current_caps & INTEL_CAP_BASIC_1588) ? "? Available" : "? Not supported");
    printf("    Enhanced Timestamping: %s\n", (g_ctx.current_caps & INTEL_CAP_ENHANCED_TS) ? "? Available" : "? Not supported");
    printf("    Time-Aware Shaper: %s\n", (g_ctx.current_caps & INTEL_CAP_TSN_TAS) ? "? Available" : "? Not supported");
    printf("    Frame Preemption: %s\n", (g_ctx.current_caps & INTEL_CAP_TSN_FP) ? "? Available" : "? Not supported");
    printf("    PCIe PTM: %s\n", (g_ctx.current_caps & INTEL_CAP_PCIe_PTM) ? "? Available" : "? Not supported");
    printf("    2.5 Gigabit: %s\n", (g_ctx.current_caps & INTEL_CAP_2_5G) ? "? Available" : "? Not supported");
    
    // Recommendations
    printf("\n?? Recommendations:\n");
    if (g_ctx.adapter_count > 1) {
        printf("    ?? Multi-adapter system detected\n");
        printf("        - Use avb_multi_adapter_test.exe for detailed multi-adapter testing\n");
        printf("        - Consider service isolation using different adapters\n");
    }
    
    if (g_ctx.current_caps & INTEL_CAP_TSN_TAS) {
        printf("    ???  TSN capabilities available\n");
        printf("        - Use avb_i226_test.exe for TSN feature testing\n");
        printf("        - Test TAS and Frame Preemption activation\n");
    }
    
    if (!(g_ctx.current_caps & INTEL_CAP_BASIC_1588)) {
        printf("    ??  No IEEE 1588 support detected\n");
        printf("        - AVB/TSN functionality will be limited\n");
        printf("        - Check if this is the correct adapter\n");
    }
    
    printf("\n?? Diagnostic Complete!\n");
}

/**
 * @brief Main diagnostic program
 */
int main(void)
{
    if (!diagnostic_init()) {
        return 1;
    }
    
    BOOL success = TRUE;
    
    // Run comprehensive diagnostics
    if (!diagnostic_enumerate_adapters()) success = FALSE;
    if (!diagnostic_hardware_state()) success = FALSE; 
    if (!diagnostic_register_access()) success = FALSE;
    if (!diagnostic_timestamp_test()) success = FALSE;
    if (!diagnostic_device_info()) success = FALSE;
    
    // Generate final report
    diagnostic_generate_report();
    
    diagnostic_cleanup();
    
    printf("\n");
    if (success) {
        printf("? All diagnostics completed successfully\n");
        printf("?? Hardware appears to be functioning correctly\n");
        return 0;
    } else {
        printf("??  Some diagnostics failed\n");  
        printf("?? Review the output above for troubleshooting guidance\n");
        return 1;
    }
}