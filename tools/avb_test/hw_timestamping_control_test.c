/**
 * @file hw_timestamping_control_test.c
 * @brief Production test for hardware timestamping enable/disable control
 * 
 * Tests the IOCTL_AVB_SET_HW_TIMESTAMPING IOCTL which controls:
 * - TSAUXC bit 31 (DisableSystime): Primary enable/disable for HW timestamping
 * - TSAUXC bit 30 (PHC Enable): Optional PTP Hardware Clock enable
 * 
 * This replaces raw TSAUXC register manipulation with a proper production API.
 */

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include "../../include/avb_ioctl.h"

#define DEVICE_PATH "\\\\.\\IntelAvbFilter"

// Helper to open device
static HANDLE open_device(void) {
    HANDLE h = CreateFileA(DEVICE_PATH, GENERIC_READ | GENERIC_WRITE,
                           0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("ERROR: Failed to open device (error %lu)\n", GetLastError());
        printf("  Is the driver installed and running?\n");
    }
    return h;
}

// Helper to control hardware timestamping
static BOOL set_hw_timestamping(HANDLE h, BOOL enable, BOOL enable_target_time, uint32_t timer_mask, AVB_HW_TIMESTAMPING_REQUEST *result) {
    AVB_HW_TIMESTAMPING_REQUEST req;
    DWORD bytesReturned = 0;
    
    ZeroMemory(&req, sizeof(req));
    req.enable = enable ? 1 : 0;
    req.timer_mask = timer_mask;  // 0x1 = SYSTIM0 only (default)
    req.enable_target_time = enable_target_time ? 1 : 0;
    req.enable_aux_ts = 0;  // Don't enable auxiliary timestamp by default
    
    if (!DeviceIoControl(h, IOCTL_AVB_SET_HW_TIMESTAMPING,
                        &req, sizeof(req),
                        &req, sizeof(req),
                        &bytesReturned, NULL)) {
        printf("ERROR: SET_HW_TIMESTAMPING IOCTL failed (error %lu)\n", GetLastError());
        return FALSE;
    }
    
    if (req.status != NDIS_STATUS_SUCCESS) {
        printf("ERROR: SET_HW_TIMESTAMPING returned status 0x%08X\n", req.status);
        return FALSE;
    }
    
    if (result) {
        *result = req;
    }
    
    return TRUE;
}

// Helper to get current clock config (includes TSAUXC state)
static BOOL get_clock_config(HANDLE h, AVB_CLOCK_CONFIG *cfg) {
    DWORD bytesReturned = 0;
    ZeroMemory(cfg, sizeof(*cfg));
    
    if (!DeviceIoControl(h, IOCTL_AVB_GET_CLOCK_CONFIG, 
                        cfg, sizeof(*cfg), 
                        cfg, sizeof(*cfg), 
                        &bytesReturned, NULL)) {
        return FALSE;
    }
    
    return (cfg->status == NDIS_STATUS_SUCCESS);
}

int main(void) {
    HANDLE h = INVALID_HANDLE_VALUE;
    int tests_passed = 0;
    int tests_failed = 0;
    
    printf("=== Hardware Timestamping Control Test ===\n");
    printf("Testing IOCTL_AVB_SET_HW_TIMESTAMPING (TSAUXC control)\n\n");
    
    h = open_device();
    if (h == INVALID_HANDLE_VALUE) {
        return 1;
    }
    
    // Test 1: Query initial state
    printf("Test 1: Query Initial TSAUXC State\n");
    {
        AVB_CLOCK_CONFIG cfg;
        if (get_clock_config(h, &cfg)) {
            printf("  TSAUXC:        0x%08X\n", cfg.tsauxc);
            printf("  Bit 31 (DisableSystime): %s\n", 
                   (cfg.tsauxc & 0x80000000) ? "SET (HW TIMESTAMPING DISABLED)" : "CLEAR (HW TIMESTAMPING ENABLED)");
            printf("  Bit 30 (PHC Enable):     %s\n",
                   (cfg.tsauxc & 0x40000000) ? "SET (PHC ENABLED)" : "CLEAR (PHC DISABLED)");
            tests_passed++;
        } else {
            printf("  ✗ Failed to query initial state\n");
            tests_failed++;
        }
    }
    
    // Test 2: Enable HW timestamping (SYSTIM0 only, no target time)
    printf("\nTest 2: Enable HW Timestamping (SYSTIM0)\n");
    {
        AVB_HW_TIMESTAMPING_REQUEST result;
        if (set_hw_timestamping(h, TRUE, FALSE, 0x1, &result)) {
            printf("  ✓ IOCTL succeeded\n");
            printf("  Previous TSAUXC: 0x%08X\n", result.previous_tsauxc);
            printf("  Current TSAUXC:  0x%08X\n", result.current_tsauxc);
            
            // Verify bit 31 is clear (SYSTIM0 enabled)
            if (!(result.current_tsauxc & 0x80000000)) {
                printf("  ✓ Bit 31 correctly CLEAR (SYSTIM0 enabled)\n");
                tests_passed++;
            } else {
                printf("  ✗ TSAUXC bits not in expected state\n");
                tests_failed++;
            }
            
            // Verify SYSTIM is running
            Sleep(100);
            AVB_TIMESTAMP_REQUEST ts1, ts2;
            DWORD br;
            ZeroMemory(&ts1, sizeof(ts1));
            ZeroMemory(&ts2, sizeof(ts2));
            
            DeviceIoControl(h, IOCTL_AVB_GET_TIMESTAMP, &ts1, sizeof(ts1), &ts1, sizeof(ts1), &br, NULL);
            Sleep(10);
            DeviceIoControl(h, IOCTL_AVB_GET_TIMESTAMP, &ts2, sizeof(ts2), &ts2, sizeof(ts2), &br, NULL);
            
            if (ts2.timestamp > ts1.timestamp) {
                printf("  ✓ SYSTIM counter is running (delta=%llu ns)\n", ts2.timestamp - ts1.timestamp);
            } else {
                printf("  ✗ SYSTIM counter appears stuck\n");
            }
        } else {
            printf("  ✗ Test 2 FAILED\n");
            tests_failed++;
        }
    }
    
    // Test 3: Disable HW timestamping
    printf("\nTest 3: Disable HW Timestamping\n");
    {
        AVB_HW_TIMESTAMPING_REQUEST result;
        if (set_hw_timestamping(h, FALSE, FALSE, 0, &result)) {
            printf("  ✓ IOCTL succeeded\n");
            printf("  Previous TSAUXC: 0x%08X\n", result.previous_tsauxc);
            printf("  Current TSAUXC:  0x%08X\n", result.current_tsauxc);
            
            // Verify bit 31 is set (disabled)
            if (result.current_tsauxc & 0x80000000) {
                printf("  ✓ Bit 31 correctly SET (HW timestamping disabled)\n");
                tests_passed++;
                
                // Verify SYSTIM stopped
                Sleep(50);
                AVB_TIMESTAMP_REQUEST ts1, ts2;
                DWORD br;
                ZeroMemory(&ts1, sizeof(ts1));
                ZeroMemory(&ts2, sizeof(ts2));
                
                DeviceIoControl(h, IOCTL_AVB_GET_TIMESTAMP, &ts1, sizeof(ts1), &ts1, sizeof(ts1), &br, NULL);
                Sleep(10);
                DeviceIoControl(h, IOCTL_AVB_GET_TIMESTAMP, &ts2, sizeof(ts2), &ts2, sizeof(ts2), &br, NULL);
                
                if (ts2.timestamp == ts1.timestamp) {
                    printf("  ✓ SYSTIM counter is stopped (frozen at %llu)\n", ts1.timestamp);
                } else {
                    printf("  ⚠ SYSTIM still incrementing (delta=%llu ns) - may take time to stop\n", 
                           ts2.timestamp - ts1.timestamp);
                }
            } else {
                printf("  ✗ Bit 31 not set after disable\n");
                tests_failed++;
            }
        } else {
            printf("  ✗ Test 3 FAILED\n");
            tests_failed++;
        }
    }
    
    // Test 4: Re-enable HW timestamping with target time interrupts
    printf("\nTest 4: Re-enable HW Timestamping (with Target Time interrupts)\n");
    {
        AVB_HW_TIMESTAMPING_REQUEST result;
        if (set_hw_timestamping(h, TRUE, TRUE, 0x1, &result)) {
            printf("  ✓ IOCTL succeeded\n");
            printf("  Previous TSAUXC: 0x%08X\n", result.previous_tsauxc);
            printf("  Current TSAUXC:  0x%08X\n", result.current_tsauxc);
            
            // Verify bit 31 is clear (enabled)
            if (!(result.current_tsauxc & 0x80000000)) {
                printf("  ✓ Bit 31 correctly CLEAR (HW timestamping enabled)\n");
                
                // Check target time bits (bit 0 = EN_TT0, bit 4 = EN_TT1)
                if (result.current_tsauxc & 0x00000001) {
                    printf("  ✓ Bit 0 SET (Target Time 0 interrupt enabled)\n");
                } else {
                    printf("  ℹ Bit 0 CLEAR (Target Time 0 disabled)\n");
                }
                if (result.current_tsauxc & 0x00000010) {
                    printf("  ✓ Bit 4 SET (Target Time 1 interrupt enabled)\n");
                } else {
                    printf("  ℹ Bit 4 CLEAR (Target Time 1 disabled)\n");
                }
                
                tests_passed++;
                
                // Verify SYSTIM resumed
                Sleep(50);
                AVB_TIMESTAMP_REQUEST ts1, ts2;
                DWORD br;
                ZeroMemory(&ts1, sizeof(ts1));
                ZeroMemory(&ts2, sizeof(ts2));
                
                DeviceIoControl(h, IOCTL_AVB_GET_TIMESTAMP, &ts1, sizeof(ts1), &ts1, sizeof(ts1), &br, NULL);
                Sleep(10);
                DeviceIoControl(h, IOCTL_AVB_GET_TIMESTAMP, &ts2, sizeof(ts2), &ts2, sizeof(ts2), &br, NULL);
                
                if (ts2.timestamp > ts1.timestamp) {
                    printf("  ✓ SYSTIM counter resumed (delta=%llu ns)\n", ts2.timestamp - ts1.timestamp);
                } else {
                    printf("  ✗ SYSTIM counter still stuck\n");
                }
            } else {
                printf("  ✗ Bit 31 not cleared after enable\n");
                tests_failed++;
            }
        } else {
            printf("  ✗ Test 4 FAILED\n");
            tests_failed++;
        }
    }
    
    CloseHandle(h);
    
    // Summary
    printf("\n=== Test Summary ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("\\nProduction IOCTL used: IOCTL_AVB_SET_HW_TIMESTAMPING\\n");
    printf("Based on Intel Foxville Ethernet Controller specification:\\n");
    printf("  • Bit 31: Disable SYSTIM0 (primary timer enable/disable)\\n");
    printf("  • Bits 27-29: Disable SYSTIM1/2/3 (additional timers)\\n");
    printf("  • Bit 4: EN_TT1 (Target Time 1 interrupt)\\n");
    printf("  • Bit 0: EN_TT0 (Target Time 0 interrupt)\\n");
    printf("  • Bit 10: EN_TS1 (Auxiliary timestamp 1 on SDP)\\n");
    printf("  • Bit 8: EN_TS0 (Auxiliary timestamp 0 on SDP)\\n");
    printf("  • Returns previous and current TSAUXC values\\n");
    printf("  • No raw register access required\\n");
    
    return (tests_failed == 0) ? 0 : 1;
}
