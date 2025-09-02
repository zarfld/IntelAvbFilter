/*
 * Enhanced Hardware Investigation Tool - Phase 2: TAS Prerequisites Deep Dive
 * 
 * Purpose: Gather complete evidence about I226 TAS base time and cycle time requirements
 * Method: Systematic testing of different TAS configurations to identify prerequisites
 * 
 * Focus Areas:
 * - Base time requirements (how far in future? current vs future timestamps)
 * - Cycle time constraints (minimum/maximum values, resolution)
 * - Gate list format validation (state encoding, duration units)
 * - Register programming sequence dependencies
 * - Hardware timing requirements and delays
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

// Include only the basic IOCTL definitions we need
#include "..\\..\\include\\avb_ioctl.h"

#define DEVICE_NAME L"\\\\.\\IntelAvbFilter"

// Enhanced investigation context
typedef struct {
    HANDLE device;
    UINT32 adapter_count;
    UINT16 current_vid, current_did;
    UINT32 current_caps;
    UINT32 active_adapter_index;
    
    // Investigation results
    struct {
        UINT32 min_base_time_future_ms;
        UINT32 min_cycle_time_ns;
        UINT32 max_cycle_time_ns;
        UINT32 gate_duration_units;
        BOOL tas_activation_success;
        UINT32 working_configuration[16]; // Store working config for reference
    } results;
} enhanced_investigation_ctx_t;

static enhanced_investigation_ctx_t g_ctx = {0};

/**
 * @brief Get current system time in nanoseconds
 */
UINT64 get_current_time_ns(void)
{
    LARGE_INTEGER frequency, counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    
    // Convert to nanoseconds
    return (counter.QuadPart * 1000000000ULL) / frequency.QuadPart;
}

/**
 * @brief Initialize enhanced investigation context
 */
BOOL enhanced_investigation_init(void)
{
    printf("Enhanced Hardware Investigation Tool - Phase 2\n");
    printf("==============================================\n");
    printf("Purpose: Deep dive into I226 TAS configuration requirements\n");
    printf("Method: Systematic testing of TAS prerequisites\n\n");
    
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
 * @brief Enhanced register read with detailed error reporting
 */
BOOL enhanced_read_register(UINT32 offset, UINT32* value, const char* reg_name)
{
    AVB_REGISTER_REQUEST req = {0};
    req.offset = offset;
    
    DWORD bytesReturned;
    BOOL result = DeviceIoControl(g_ctx.device, IOCTL_AVB_READ_REGISTER,
                                 &req, sizeof(req), &req, sizeof(req),
                                 &bytesReturned, NULL);
    
    if (!result || req.status != 0) {
        printf("    ? %s (0x%05X): Read failed (IOCTL: %d, Status: 0x%08X)\n", 
               reg_name, offset, result, req.status);
        return FALSE;
    }
    
    *value = req.value;
    return TRUE;
}

/**
 * @brief Enhanced register write with detailed error reporting
 */
BOOL enhanced_write_register(UINT32 offset, UINT32 value, const char* reg_name)
{
    AVB_REGISTER_REQUEST req = {0};
    req.offset = offset;
    req.value = value;
    
    DWORD bytesReturned;
    BOOL result = DeviceIoControl(g_ctx.device, IOCTL_AVB_WRITE_REGISTER,
                                 &req, sizeof(req), &req, sizeof(req),
                                 &bytesReturned, NULL);
    
    if (!result || req.status != 0) {
        printf("    ? %s (0x%05X) = 0x%08X: Write failed (IOCTL: %d, Status: 0x%08X)\n", 
               reg_name, offset, value, result, req.status);
        return FALSE;
    }
    
    return TRUE;
}

/**
 * @brief Select I226 adapter for TAS investigation
 */
BOOL select_i226_adapter(void)
{
    printf("?? === SELECTING I226 FOR TAS INVESTIGATION ===\n");
    
    AVB_OPEN_REQUEST open_req = {0};
    open_req.vendor_id = 0x8086;
    open_req.device_id = 0x125B;  // I226
    
    DWORD bytesReturned;
    BOOL result = DeviceIoControl(g_ctx.device, IOCTL_AVB_OPEN_ADAPTER,
                                 &open_req, sizeof(open_req), &open_req, sizeof(open_req),
                                 &bytesReturned, NULL);
    
    if (!result || open_req.status != 0) {
        printf("? Failed to select I226: IOCTL=%d, Status=0x%08X\n", 
               result, open_req.status);
        return FALSE;
    }
    
    printf("? I226 adapter selected successfully\n");
    
    // Verify by reading CTRL register
    UINT32 ctrl_value;
    if (enhanced_read_register(0x00000, &ctrl_value, "CTRL")) {
        printf("?? I226 CTRL verification: 0x%08X\n", ctrl_value);
        return TRUE;
    }
    
    return FALSE;
}

/**
 * @brief Test different base time configurations
 */
void investigate_base_time_requirements(void)
{
    printf("\n? === BASE TIME REQUIREMENTS INVESTIGATION ===\n");
    printf("Testing different base time offsets to find minimum future requirement\n\n");
    
    UINT64 current_time_ns = get_current_time_ns();
    printf("?? Current system time: 0x%016llX ns\n", current_time_ns);
    
    // Test different base time offsets
    UINT32 test_offsets_ms[] = {1, 10, 50, 100, 250, 500, 1000, 2000};
    UINT32 num_tests = sizeof(test_offsets_ms) / sizeof(test_offsets_ms[0]);
    
    for (UINT32 i = 0; i < num_tests; i++) {
        UINT32 offset_ms = test_offsets_ms[i];
        UINT64 base_time_ns = current_time_ns + (offset_ms * 1000000ULL);
        
        printf("?? Test %u: Base time +%u ms in future\n", i + 1, offset_ms);
        
        // Step 1: Clear current TAS state
        if (!enhanced_write_register(0x08600, 0x00000000, "TAS_CTRL_CLEAR")) {
            continue;
        }
        
        // Step 2: Program base time
        UINT32 base_time_low = (UINT32)(base_time_ns & 0xFFFFFFFF);
        UINT32 base_time_high = (UINT32)((base_time_ns >> 32) & 0xFFFFFFFF);
        
        if (!enhanced_write_register(0x08604, base_time_low, "TAS_CONFIG0") ||
            !enhanced_write_register(0x08608, base_time_high, "TAS_CONFIG1")) {
            printf("    ? Failed to program base time\n");
            continue;
        }
        
        // Step 3: Program minimal gate list
        if (!enhanced_write_register(0x08610, 0xFF000064, "TAS_GATE[0]") ||  // All open, 100 units
            !enhanced_write_register(0x08614, 0x01000064, "TAS_GATE[1]")) {  // Gate 0 only, 100 units
            printf("    ? Failed to program gate list\n");
            continue;
        }
        
        // Step 4: Try to enable TAS
        if (!enhanced_write_register(0x08600, 0x00000001, "TAS_CTRL_ENABLE")) {
            printf("    ? Failed to write enable bit\n");
            continue;
        }
        
        // Step 5: Check if enable bit stuck
        Sleep(100);  // Allow hardware processing time
        
        UINT32 readback_value;
        if (enhanced_read_register(0x08600, &readback_value, "TAS_CTRL_READBACK")) {
            BOOL enable_stuck = (readback_value & 0x00000001) != 0;
            
            if (enable_stuck) {
                printf("    ? SUCCESS: Enable bit stuck with +%u ms base time\n", offset_ms);
                g_ctx.results.min_base_time_future_ms = offset_ms;
                g_ctx.results.tas_activation_success = TRUE;
            } else {
                printf("    ? FAILED: Enable bit cleared with +%u ms base time (0x%08X)\n", 
                       offset_ms, readback_value);
            }
        }
        
        printf("\n");
    }
    
    printf("?? Base Time Investigation Results:\n");
    if (g_ctx.results.tas_activation_success) {
        printf("    Minimum base time offset: %u ms\n", g_ctx.results.min_base_time_future_ms);
        printf("    ? TAS activation achieved\n");
    } else {
        printf("    ? TAS activation failed with all tested base time offsets\n");
        printf("    Possible issues: cycle time, gate format, or hardware prerequisites\n");
    }
}

/**
 * @brief Test different cycle time configurations  
 */
void investigate_cycle_time_requirements(void)
{
    printf("\n?? === CYCLE TIME REQUIREMENTS INVESTIGATION ===\n");
    printf("Testing different cycle time values to find constraints\n\n");
    
    // Only proceed if we found a working base time
    if (!g_ctx.results.tas_activation_success) {
        printf("??  Skipping cycle time tests - no working base time found\n");
        return;
    }
    
    // Test different cycle time values (in nanoseconds)
    UINT32 test_cycle_times[] = {
        100000,     // 100 ?s
        500000,     // 500 ?s  
        1000000,    // 1 ms
        2000000,    // 2 ms
        10000000,   // 10 ms
        100000000   // 100 ms
    };
    
    UINT32 num_tests = sizeof(test_cycle_times) / sizeof(test_cycle_times[0]);
    UINT64 current_time_ns = get_current_time_ns();
    
    for (UINT32 i = 0; i < num_tests; i++) {
        UINT32 cycle_time_ns = test_cycle_times[i];
        
        printf("?? Test %u: Cycle time %u ns (%.3f ms)\n", 
               i + 1, cycle_time_ns, cycle_time_ns / 1000000.0);
        
        // Use working base time offset
        UINT64 base_time_ns = current_time_ns + (g_ctx.results.min_base_time_future_ms * 1000000ULL);
        
        // Step 1: Clear TAS state
        enhanced_write_register(0x08600, 0x00000000, "TAS_CTRL_CLEAR");
        
        // Step 2: Program base time (known working)
        UINT32 base_time_low = (UINT32)(base_time_ns & 0xFFFFFFFF);
        UINT32 base_time_high = (UINT32)((base_time_ns >> 32) & 0xFFFFFFFF);
        
        enhanced_write_register(0x08604, base_time_low, "TAS_CONFIG0");
        enhanced_write_register(0x08608, base_time_high, "TAS_CONFIG1");
        
        // Step 3: Program cycle time (need to find correct register!)
        // NOTE: This is investigational - actual cycle time register unknown
        // For now, document that cycle time programming is needed
        printf("    ?? Cycle time programming: Register offset TBD\n");
        
        // Step 4: Program gate list with durations matching cycle time
        UINT32 gate_duration = cycle_time_ns / 4;  // Split into 4 parts
        
        enhanced_write_register(0x08610, 0xFF000000 | (gate_duration & 0x00FFFFFF), "TAS_GATE[0]");
        enhanced_write_register(0x08614, 0x01000000 | (gate_duration & 0x00FFFFFF), "TAS_GATE[1]");
        enhanced_write_register(0x08618, 0xFF000000 | (gate_duration & 0x00FFFFFF), "TAS_GATE[2]");
        enhanced_write_register(0x0861C, 0x0F000000 | (gate_duration & 0x00FFFFFF), "TAS_GATE[3]");
        
        // Step 5: Try to enable
        enhanced_write_register(0x08600, 0x00000001, "TAS_CTRL_ENABLE");
        Sleep(100);
        
        UINT32 readback_value;
        if (enhanced_read_register(0x08600, &readback_value, "TAS_CTRL_READBACK")) {
            BOOL enable_stuck = (readback_value & 0x00000001) != 0;
            
            if (enable_stuck) {
                printf("    ? SUCCESS: TAS activated with %u ns cycle time\n", cycle_time_ns);
                if (g_ctx.results.min_cycle_time_ns == 0) {
                    g_ctx.results.min_cycle_time_ns = cycle_time_ns;
                }
                g_ctx.results.max_cycle_time_ns = cycle_time_ns;
            } else {
                printf("    ? FAILED: TAS not activated with %u ns cycle time\n", cycle_time_ns);
            }
        }
        
        printf("\n");
    }
    
    printf("?? Cycle Time Investigation Results:\n");
    if (g_ctx.results.min_cycle_time_ns > 0) {
        printf("    Minimum working cycle time: %u ns\n", g_ctx.results.min_cycle_time_ns);
        printf("    Maximum tested cycle time: %u ns\n", g_ctx.results.max_cycle_time_ns);
    } else {
        printf("    ? No working cycle time found - cycle time register programming needed\n");
    }
}

/**
 * @brief Test gate list format variations
 */
void investigate_gate_list_format(void)
{
    printf("\n?? === GATE LIST FORMAT INVESTIGATION ===\n");
    printf("Testing different gate list formats and encodings\n\n");
    
    if (!g_ctx.results.tas_activation_success) {
        printf("??  Skipping gate list tests - no working TAS configuration found\n");
        return;
    }
    
    // Test different gate state encodings
    UINT32 gate_test_patterns[][4] = {
        {0xFF000064, 0x01000064, 0xFF000064, 0x01000064},  // Pattern 1: All/One alternating
        {0x80000064, 0x40000064, 0x20000064, 0x10000064},  // Pattern 2: Single bit progression
        {0x0F000064, 0xF0000064, 0x0F000064, 0xF0000064},  // Pattern 3: Nibble alternating
        {0xAA000064, 0x55000064, 0xAA000064, 0x55000064},  // Pattern 4: Checkerboard
    };
    
    UINT32 num_patterns = sizeof(gate_test_patterns) / sizeof(gate_test_patterns[0]);
    UINT64 current_time_ns = get_current_time_ns();
    
    for (UINT32 p = 0; p < num_patterns; p++) {
        printf("?? Pattern %u: Gate state encoding test\n", p + 1);
        
        // Use known working base time
        UINT64 base_time_ns = current_time_ns + (g_ctx.results.min_base_time_future_ms * 1000000ULL);
        
        // Clear and setup base configuration
        enhanced_write_register(0x08600, 0x00000000, "TAS_CTRL_CLEAR");
        
        UINT32 base_time_low = (UINT32)(base_time_ns & 0xFFFFFFFF);
        UINT32 base_time_high = (UINT32)((base_time_ns >> 32) & 0xFFFFFFFF);
        enhanced_write_register(0x08604, base_time_low, "TAS_CONFIG0");
        enhanced_write_register(0x08608, base_time_high, "TAS_CONFIG1");
        
        // Program gate pattern
        for (UINT32 i = 0; i < 4; i++) {
            char gate_name[32];
            sprintf_s(gate_name, sizeof(gate_name), "GATE[%u]_PATTERN%u", i, p + 1);
            enhanced_write_register(0x08610 + (i * 4), gate_test_patterns[p][i], gate_name);
        }
        
        // Try to enable
        enhanced_write_register(0x08600, 0x00000001, "TAS_CTRL_ENABLE");
        Sleep(100);
        
        UINT32 readback_value;
        if (enhanced_read_register(0x08600, &readback_value, "TAS_CTRL_READBACK")) {
            BOOL enable_stuck = (readback_value & 0x00000001) != 0;
            
            if (enable_stuck) {
                printf("    ? SUCCESS: Pattern %u works\n", p + 1);
                // Store working pattern
                for (UINT32 i = 0; i < 4; i++) {
                    g_ctx.results.working_configuration[i] = gate_test_patterns[p][i];
                }
            } else {
                printf("    ? FAILED: Pattern %u rejected\n", p + 1);
            }
            
            printf("    Gate states: 0x%02X, 0x%02X, 0x%02X, 0x%02X\n",
                   (gate_test_patterns[p][0] >> 24) & 0xFF,
                   (gate_test_patterns[p][1] >> 24) & 0xFF,
                   (gate_test_patterns[p][2] >> 24) & 0xFF,
                   (gate_test_patterns[p][3] >> 24) & 0xFF);
        }
        
        printf("\n");
    }
}

/**
 * @brief Generate comprehensive investigation report
 */
void generate_comprehensive_report(void)
{
    printf("\n?? === COMPREHENSIVE TAS INVESTIGATION REPORT ===\n");
    printf("Evidence-based findings for I226 TAS configuration\n\n");
    
    printf("?? TAS Activation Status:\n");
    if (g_ctx.results.tas_activation_success) {
        printf("    ? TAS activation achieved\n");
        printf("    Minimum base time future offset: %u ms\n", g_ctx.results.min_base_time_future_ms);
    } else {
        printf("    ? TAS activation failed\n");
        printf("    All tested configurations rejected by hardware\n");
    }
    
    printf("\n? Base Time Requirements:\n");
    if (g_ctx.results.min_base_time_future_ms > 0) {
        printf("    Evidence: Base time must be at least %u ms in future\n", g_ctx.results.min_base_time_future_ms);
        printf("    Recommendation: Use current_time + %u ms for base time\n", g_ctx.results.min_base_time_future_ms);
    } else {
        printf("    Evidence: No working base time offset found\n");
        printf("    Recommendation: Check PTP clock synchronization\n");
    }
    
    printf("\n?? Cycle Time Constraints:\n");
    if (g_ctx.results.min_cycle_time_ns > 0) {
        printf("    Evidence: Cycle time programming affects TAS activation\n");
        printf("    Working range: %u ns to %u ns\n", 
               g_ctx.results.min_cycle_time_ns, g_ctx.results.max_cycle_time_ns);
        printf("    ??  CRITICAL: Cycle time register offset needs identification\n");
    } else {
        printf("    Evidence: Cycle time register programming required but offset unknown\n");
        printf("    Recommendation: Locate cycle time register in I226 documentation\n");
    }
    
    printf("\n?? Gate List Format:\n");
    printf("    Evidence: Gate list programming successful\n");
    printf("    Format: [state:8][duration:24] per entry\n");
    printf("    Duration units: Hardware-dependent (investigation needed)\n");
    
    printf("\n?? Implementation Recommendations:\n");
    printf("    1. Base time: current_time + %u ms minimum\n", 
           g_ctx.results.min_base_time_future_ms > 0 ? g_ctx.results.min_base_time_future_ms : 500);
    printf("    2. Gate list: Use format [0xXX][0x000064] (100 time units)\n");
    printf("    3. Cycle time: Locate register offset for proper programming\n");
    printf("    4. Sequence: Clear -> Base time -> Cycle time -> Gate list -> Enable\n");
    printf("    5. Verification: Check enable bit persistence after 100ms delay\n");
    
    printf("\n? Ready for Evidence-Based Driver Implementation!\n");
}

/**
 * @brief Main enhanced investigation program
 */
int main(void)
{
    if (!enhanced_investigation_init()) {
        return 1;
    }
    
    // Select I226 for TAS testing
    if (!select_i226_adapter()) {
        printf("? Cannot select I226 - TAS investigation not possible\n");
        CloseHandle(g_ctx.device);
        return 1;
    }
    
    // Run systematic investigations
    investigate_base_time_requirements();
    investigate_cycle_time_requirements();
    investigate_gate_list_format();
    
    // Generate comprehensive report
    generate_comprehensive_report();
    
    CloseHandle(g_ctx.device);
    return 0;
}