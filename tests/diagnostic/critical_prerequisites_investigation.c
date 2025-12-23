/*
 * Enhanced Hardware Investigation Tool - Phase 3: Critical Prerequisites
 * 
 * Purpose: Investigate the exact prerequisites that cause TAS activation failure
 * Evidence from Phase 2: Enable bit clears immediately regardless of base time
 * 
 * Investigation Areas:
 * - PTP clock verification (I226 SYSTIM advancement check)
 * - Missing register identification (cycle time, control registers)
 * - Hardware prerequisite sequence analysis
 * - Register field decoding and validation
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

// Fixed include path to match project structure
#include "..\\..\\external\\intel_avb\\include\\avb_ioctl.h"

#define DEVICE_NAME L"\\\\.\\IntelAvbFilter"

// Critical investigation context
typedef struct {
    HANDLE device;
    BOOL ptp_clock_running;
    UINT64 initial_systim;
    UINT64 current_systim;
    UINT32 systim_increment;
    
    // Register investigation
    UINT32 unknown_registers[64];  // Store unknown register values
    UINT32 register_count;
} critical_investigation_ctx_t;

static critical_investigation_ctx_t g_ctx = {0};

/**
 * @brief Enhanced register read with error details
 */
BOOL critical_read_register(UINT32 offset, UINT32* value, const char* reg_name)
{
    AVB_REGISTER_REQUEST req = {0};
    req.offset = offset;
    
    DWORD bytesReturned;
    BOOL result = DeviceIoControl(g_ctx.device, IOCTL_AVB_READ_REGISTER,
                                 &req, sizeof(req), &req, sizeof(req),
                                 &bytesReturned, NULL);
    
    if (!result || req.status != 0) {
        printf("    ? %s (0x%05X): FAILED (IOCTL: %d, Status: 0x%08X)\n", 
               reg_name, offset, result, req.status);
        return FALSE;
    }
    
    *value = req.value;
    printf("    ?? %s (0x%05X): 0x%08X\n", reg_name, offset, *value);
    return TRUE;
}

/**
 * @brief Enhanced register write with error details
 */
BOOL critical_write_register(UINT32 offset, UINT32 value, const char* reg_name)
{
    AVB_REGISTER_REQUEST req = {0};
    req.offset = offset;
    req.value = value;
    
    DWORD bytesReturned;
    BOOL result = DeviceIoControl(g_ctx.device, IOCTL_AVB_WRITE_REGISTER,
                                 &req, sizeof(req), &req, sizeof(req),
                                 &bytesReturned, NULL);
    
    if (!result || req.status != 0) {
        printf("    ? %s (0x%05X) = 0x%08X: FAILED (IOCTL: %d, Status: 0x%08X)\n", 
               reg_name, offset, value, result, req.status);
        return FALSE;
    }
    
    printf("    ? %s (0x%05X) = 0x%08X: SUCCESS\n", reg_name, offset, value);
    return TRUE;
}

/**
 * @brief Initialize critical investigation
 */
BOOL critical_investigation_init(void)
{
    printf("Critical Hardware Investigation Tool - Phase 3\n");
    printf("==============================================\n");
    printf("Purpose: Identify exact prerequisites for TAS activation failure\n");
    printf("Evidence: TAS enable bit clears immediately regardless of base time\n\n");
    
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
 * @brief Select I226 and verify context
 */
BOOL select_and_verify_i226(void)
{
    printf("?? === SELECTING AND VERIFYING I226 ===\n");
    
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
    
    // Extended verification
    UINT32 ctrl_value, status_value;
    if (critical_read_register(0x00000, &ctrl_value, "CTRL") &&
        critical_read_register(0x00008, &status_value, "STATUS")) {
        
        printf("?? I226 Context Verification:\n");
        printf("    CTRL: 0x%08X (Link state, speed, duplex)\n", ctrl_value);
        printf("    STATUS: 0x%08X (Link up: %s)\n", status_value, 
               (status_value & 0x00000002) ? "YES" : "NO");
        
        if (!(status_value & 0x00000002)) {
            printf("??  WARNING: I226 link is DOWN - this may affect TAS functionality\n");
        }
        
        return TRUE;
    }
    
    return FALSE;
}

/**
 * @brief CRITICAL: Investigate I226 PTP clock status
 */
void investigate_i226_ptp_clock(void)
{
    printf("\n?? === CRITICAL: I226 PTP CLOCK INVESTIGATION ===\n");
    printf("Purpose: Verify PTP clock is running (TAS prerequisite)\n\n");
    
    // Read initial SYSTIM
    UINT32 systiml_1, systimh_1, systiml_2, systimh_2;
    
    printf("?? Step 1: Reading I226 SYSTIM registers\n");
    if (!critical_read_register(0x0B600, &systiml_1, "SYSTIML_INITIAL") ||
        !critical_read_register(0x0B604, &systimh_1, "SYSTIMH_INITIAL")) {
        printf("? Cannot read I226 SYSTIM registers\n");
        return;
    }
    
    g_ctx.initial_systim = ((UINT64)systimh_1 << 32) | systiml_1;
    printf("    Initial SYSTIM: 0x%016llX\n", g_ctx.initial_systim);
    
    // Wait and read again
    printf("\n??  Step 2: Waiting 500ms to check clock advancement\n");
    Sleep(500);
    
    if (!critical_read_register(0x0B600, &systiml_2, "SYSTIML_AFTER_DELAY") ||
        !critical_read_register(0x0B604, &systimh_2, "SYSTIMH_AFTER_DELAY")) {
        printf("? Cannot read I226 SYSTIM after delay\n");
        return;
    }
    
    g_ctx.current_systim = ((UINT64)systimh_2 << 32) | systiml_2;
    printf("    SYSTIM after delay: 0x%016llX\n", g_ctx.current_systim);
    
    // Analyze clock advancement
    printf("\n?? Step 3: PTP Clock Analysis\n");
    if (g_ctx.current_systim > g_ctx.initial_systim) {
        UINT64 delta = g_ctx.current_systim - g_ctx.initial_systim;
        g_ctx.ptp_clock_running = TRUE;
        printf("    ? PTP CLOCK IS RUNNING\n");
        printf("    Clock advanced: %llu ns in 500ms\n", delta);
        printf("    Clock rate: %.2f MHz\n", delta / 500000.0);
        
        g_ctx.systim_increment = (UINT32)(delta / 500);  // ns per ms
    } else if (g_ctx.current_systim == g_ctx.initial_systim) {
        g_ctx.ptp_clock_running = FALSE;
        printf("    ? PTP CLOCK IS STUCK (not advancing)\n");
        printf("    This is likely the root cause of TAS activation failure\n");
    } else {
        g_ctx.ptp_clock_running = FALSE;
        printf("    ? PTP CLOCK WENT BACKWARDS (clock rollover or error)\n");
        printf("    This indicates a serious PTP synchronization issue\n");
    }
    
    // Additional PTP register analysis
    printf("\n?? Step 4: Additional PTP Register Analysis\n");
    UINT32 timinca, tsauxc, tsyncrxctl, tsynctxctl;
    
    critical_read_register(0x0B608, &timinca, "TIMINCA");
    critical_read_register(0x0B640, &tsauxc, "TSAUXC");
    critical_read_register(0x0B620, &tsyncrxctl, "TSYNCRXCTL");
    critical_read_register(0x0B614, &tsynctxctl, "TSYNCTXCTL");
    
    printf("    TIMINCA Analysis:\n");
    printf("      Increment value: %u ns per tick\n", (timinca >> 24) & 0xFF);
    printf("      Fine adjustment: 0x%04X\n", timinca & 0x00FFFFFF);
    
    printf("    TSAUXC Analysis:\n");
    printf("      PHC Enable: %s\n", (tsauxc & (1U << 30)) ? "ENABLED" : "DISABLED");
    printf("      DisableSystime: %s\n", (tsauxc & (1U << 31)) ? "CLOCK DISABLED" : "CLOCK ENABLED");
    
    printf("    Timestamp Capture:\n");
    printf("      RX timestamping: %s\n", (tsyncrxctl & 0x00000010) ? "ENABLED" : "DISABLED");
    printf("      TX timestamping: %s\n", (tsynctxctl & 0x00000010) ? "ENABLED" : "DISABLED");
}

/**
 * @brief Scan for unknown TAS-related registers
 */
void investigate_unknown_tas_registers(void)
{
    printf("\n?? === SCANNING FOR UNKNOWN TAS REGISTERS ===\n");
    printf("Purpose: Find missing registers that may be prerequisites for TAS\n\n");
    
    // Scan TAS register region (0x08600 - 0x086FF)
    printf("?? Step 1: Scanning TAS register block (0x08600 - 0x086FF)\n");
    
    UINT32 register_ranges[][3] = {
        {0x08600, 0x0863F, 4},   // TAS control and config area
        {0x08640, 0x0867F, 4},   // Extended TAS area (potential cycle time)  
        {0x08680, 0x086BF, 4},   // Additional TAS area
        {0x086C0, 0x086FF, 4}    // Final TAS area
    };
    
    UINT32 num_ranges = sizeof(register_ranges) / sizeof(register_ranges[0]);
    
    for (UINT32 r = 0; r < num_ranges; r++) {
        UINT32 start = register_ranges[r][0];
        UINT32 end = register_ranges[r][1];
        UINT32 step = register_ranges[r][2];
        
        printf("\n  Range 0x%05X - 0x%05X:\n", start, end);
        
        for (UINT32 offset = start; offset <= end; offset += step) {
            UINT32 value;
            char reg_name[32];
            sprintf_s(reg_name, sizeof(reg_name), "REG_0x%05X", offset);
            
            if (critical_read_register(offset, &value, reg_name)) {
                if (value != 0x00000000 && value != 0xFFFFFFFF) {
                    printf("      ?? NON-ZERO: 0x%05X = 0x%08X (potential active register)\n", 
                           offset, value);
                    
                    // Store for further analysis
                    if (g_ctx.register_count < 64) {
                        g_ctx.unknown_registers[g_ctx.register_count * 2] = offset;
                        g_ctx.unknown_registers[g_ctx.register_count * 2 + 1] = value;
                        g_ctx.register_count++;
                    }
                }
            }
        }
    }
    
    printf("\n?? Step 2: Potential cycle time register candidates\n");
    
    // Common locations for cycle time in TSN controllers
    UINT32 cycle_time_candidates[] = {
        0x08620,  // Common location after config registers
        0x08624,  // Next dword
        0x08640,  // Extended config area start
        0x08644,  // Next dword in extended area
        0x08660,  // Middle of extended area
        0x08680   // Later in TAS block
    };
    
    UINT32 num_candidates = sizeof(cycle_time_candidates) / sizeof(cycle_time_candidates[0]);
    
    for (UINT32 i = 0; i < num_candidates; i++) {
        UINT32 offset = cycle_time_candidates[i];
        UINT32 value;
        char reg_name[32];
        sprintf_s(reg_name, sizeof(reg_name), "CYCLE_CANDIDATE_0x%05X", offset);
        
        if (critical_read_register(offset, &value, reg_name)) {
            printf("    Candidate 0x%05X: 0x%08X\n", offset, value);
            
            // Test if this could be a cycle time register by writing a test value
            if (critical_write_register(offset, 0x000F4240, "TEST_1MS_CYCLE")) {  // 1ms in ns
                Sleep(50);
                UINT32 readback;
                if (critical_read_register(offset, &readback, "TEST_READBACK")) {
                    if (readback == 0x000F4240) {
                        printf("      ?? POTENTIAL CYCLE TIME REGISTER: 0x%05X (value stuck)\n", offset);
                    } else {
                        printf("      ??  Not cycle time register: 0x%05X (value changed to 0x%08X)\n", 
                               offset, readback);
                    }
                }
                
                // Restore original value
                critical_write_register(offset, value, "RESTORE_ORIGINAL");
            }
        }
    }
}

/**
 * @brief Test TAS activation with PTP clock correlation
 */
void test_tas_with_ptp_correlation(void)
{
    printf("\n?? === TESTING TAS WITH PTP CLOCK CORRELATION ===\n");
    printf("Purpose: Use actual PTP time for base time instead of system time\n\n");
    
    if (!g_ctx.ptp_clock_running) {
        printf("??  PTP clock not running - cannot perform PTP-correlated TAS test\n");
        return;
    }
    
    // Read current PTP time
    UINT32 systiml, systimh;
    if (!critical_read_register(0x0B600, &systiml, "CURRENT_SYSTIML") ||
        !critical_read_register(0x0B604, &systimh, "CURRENT_SYSTIMH")) {
        printf("? Cannot read current PTP time\n");
        return;
    }
    
    UINT64 current_ptp_time = ((UINT64)systimh << 32) | systiml;
    printf("?? Current PTP time: 0x%016llX ns\n", current_ptp_time);
    
    // Calculate base time as PTP time + 1 second
    UINT64 base_time_ptp = current_ptp_time + 1000000000ULL;  // +1 second
    
    printf("?? Testing TAS activation with PTP-correlated base time\n");
    printf("    Base time: 0x%016llX (+1 second from PTP clock)\n", base_time_ptp);
    
    // Clear TAS
    critical_write_register(0x08600, 0x00000000, "TAS_CTRL_CLEAR");
    
    // Program PTP-correlated base time
    UINT32 base_time_low = (UINT32)(base_time_ptp & 0xFFFFFFFF);
    UINT32 base_time_high = (UINT32)((base_time_ptp >> 32) & 0xFFFFFFFF);
    
    critical_write_register(0x08604, base_time_low, "TAS_CONFIG0_PTP");
    critical_write_register(0x08608, base_time_high, "TAS_CONFIG1_PTP");
    
    // Program gate list
    critical_write_register(0x08610, 0xFF000064, "TAS_GATE[0]_PTP");
    critical_write_register(0x08614, 0x01000064, "TAS_GATE[1]_PTP");
    
    // Try to enable
    critical_write_register(0x08600, 0x00000001, "TAS_CTRL_ENABLE_PTP");
    Sleep(100);
    
    UINT32 readback_value;
    if (critical_read_register(0x08600, &readback_value, "TAS_CTRL_READBACK_PTP")) {
        BOOL enable_stuck = (readback_value & 0x00000001) != 0;
        
        if (enable_stuck) {
            printf("    ? SUCCESS: TAS activated with PTP-correlated base time!\n");
            printf("    This confirms PTP clock synchronization was the key prerequisite\n");
        } else {
            printf("    ? FAILED: TAS still not activated even with PTP-correlated base time\n");
            printf("    Additional prerequisites still missing (likely cycle time register)\n");
        }
    }
}

/**
 * @brief Generate critical investigation report
 */
void generate_critical_report(void)
{
    printf("\n?? === CRITICAL INVESTIGATION REPORT ===\n");
    printf("Phase 3: Root cause analysis for TAS activation failure\n\n");
    
    printf("?? PTP Clock Analysis:\n");
    if (g_ctx.ptp_clock_running) {
        printf("    ? I226 PTP clock is running normally\n");
        printf("    Clock advancement rate: %u ns/ms\n", g_ctx.systim_increment);
        printf("    Initial SYSTIM: 0x%016llX\n", g_ctx.initial_systim);
        printf("    Current SYSTIM: 0x%016llX\n", g_ctx.current_systim);
        printf("    ?? PTP clock is NOT the root cause of TAS failure\n");
    } else {
        printf("    ? I226 PTP clock is NOT running\n");
        printf("    SYSTIM stuck at: 0x%016llX\n", g_ctx.initial_systim);
        printf("    ?? PTP clock failure is LIKELY the root cause of TAS failure\n");
        printf("    Recommendation: Fix I226 PTP initialization first\n");
    }
    
    printf("\n?? Register Discovery:\n");
    if (g_ctx.register_count > 0) {
        printf("    Found %u potentially active registers in TAS region\n", g_ctx.register_count);
        printf("    These may include the missing cycle time register\n");
        for (UINT32 i = 0; i < g_ctx.register_count && i < 10; i++) {
            UINT32 offset = g_ctx.unknown_registers[i * 2];
            UINT32 value = g_ctx.unknown_registers[i * 2 + 1];
            printf("      0x%05X: 0x%08X\n", offset, value);
        }
    } else {
        printf("    No active registers found in extended TAS region\n");
        printf("    Cycle time register may be in a different location\n");
    }
    
    printf("\n?? Root Cause Assessment:\n");
    if (!g_ctx.ptp_clock_running) {
        printf("    PRIMARY SUSPECT: I226 PTP clock initialization failure\n");
        printf("    Evidence: SYSTIM not advancing, TAS requires running PTP clock\n");
        printf("    Next steps: Apply I210 PTP fixes to I226 initialization\n");
    } else {
        printf("    PRIMARY SUSPECT: Missing cycle time register programming\n");
        printf("    Evidence: PTP clock running, but TAS still fails activation\n");
        printf("    Next steps: Locate cycle time register in I226 specification\n");
    }
    
    printf("\n?? Implementation Recommendations:\n");
    printf("    1. Priority 1: Fix I226 PTP clock initialization if stuck\n");
    printf("    2. Priority 2: Locate and program cycle time register\n");
    printf("    3. Priority 3: Use PTP time for base time calculations\n");
    printf("    4. Priority 4: Validate complete TAS activation sequence\n");
    
    printf("\n? Critical Investigation Complete - Root cause analysis ready!\n");
}

/**
 * @brief Main critical investigation program
 */
int main(void)
{
    if (!critical_investigation_init()) {
        return 1;
    }
    
    if (!select_and_verify_i226()) {
        printf("? Cannot select I226 - investigation not possible\n");
        CloseHandle(g_ctx.device);
        return 1;
    }
    
    // Run critical investigations
    investigate_i226_ptp_clock();
    investigate_unknown_tas_registers();
    test_tas_with_ptp_correlation();
    
    // Generate critical analysis report
    generate_critical_report();
    
    CloseHandle(g_ctx.device);
    return 0;
}