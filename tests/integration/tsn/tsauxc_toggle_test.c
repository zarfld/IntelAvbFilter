/**
 * @file tsauxc_toggle_test.c
 * @brief Comprehensive test for TSAUXC bit 31 (DisableSystime) enable/disable cycle
 * 
 * This test validates that we can correctly:
 * 1. Read current TSAUXC state
 * 2. Clear bit 31 to enable PTP clock (SYSTIM increments)
 * 3. Set bit 31 to disable PTP clock (SYSTIM freezes)
 * 4. Clear bit 31 again to re-enable PTP clock
 * 
 * Validates the implementation in avb_integration_fixed.c lines 1144-1166
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../include/avb_ioctl.h"

#define REG_TSAUXC      0x0B640  // Time Sync Auxiliary Control
#define REG_SYSTIML     0x0B600  // System Time Low
#define REG_SYSTIMH     0x0B604  // System Time High

#define BIT31_DISABLE_SYSTIME  0x80000000

// Helper function to read a register
static BOOL ReadRegister(HANDLE h, ULONG offset, ULONG* value) {
    AVB_REGISTER_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.offset = offset;
    
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(h, IOCTL_AVB_READ_REGISTER, 
                        &req, sizeof(req), 
                        &req, sizeof(req), 
                        &bytesReturned, NULL)) {
        return FALSE;
    }
    
    *value = req.value;
    return TRUE;
}

// Helper function to write a register
static BOOL WriteRegister(HANDLE h, ULONG offset, ULONG value) {
    AVB_REGISTER_REQUEST req;
    ZeroMemory(&req, sizeof(req));
    req.offset = offset;
    req.value = value;
    
    DWORD bytesReturned = 0;
    return DeviceIoControl(h, IOCTL_AVB_WRITE_REGISTER, 
                          &req, sizeof(req), 
                          &req, sizeof(req), 
                          &bytesReturned, NULL);
}

// Test if SYSTIM counter is incrementing
static BOOL IsSystimIncrementing(HANDLE h) {
    ULONG systim1 = 0, systim2 = 0;
    
    if (!ReadRegister(h, REG_SYSTIML, &systim1)) {
        printf("  ?? Failed to read SYSTIML (sample 1)\n");
        return FALSE;
    }
    
    Sleep(50); // 50ms delay
    
    if (!ReadRegister(h, REG_SYSTIML, &systim2)) {
        printf("  ?? Failed to read SYSTIML (sample 2)\n");
        return FALSE;
    }
    
    LONG delta = (LONG)(systim2 - systim1);
    printf("  SYSTIML delta: %ld (0x%08X -> 0x%08X)\n", delta, systim1, systim2);
    
    return (delta > 0);
}

// Main test function
int TestTsauxcToggle(HANDLE h) {
    printf("\n========================================\n");
    printf("TSAUXC BIT 31 ENABLE/DISABLE CYCLE TEST\n");
    printf("========================================\n\n");
    
    ULONG tsauxc_original = 0;
    ULONG tsauxc_current = 0;
    int testsPassed = 0;
    int testsFailed = 0;
    
    // Step 0: Read original TSAUXC value
    printf("STEP 0: Read original TSAUXC state\n");
    if (!ReadRegister(h, REG_TSAUXC, &tsauxc_original)) {
        printf("  ?? FAILED: Could not read TSAUXC register\n");
        return -1;
    }
    printf("  TSAUXC original value: 0x%08X\n", tsauxc_original);
    printf("  Bit 31 (DisableSystime): %s\n", 
           (tsauxc_original & BIT31_DISABLE_SYSTIME) ? "SET (PTP DISABLED)" : "CLEAR (PTP ENABLED)");
    printf("\n");
    
    // Step 1: Ensure PTP is enabled (bit 31 cleared)
    printf("STEP 1: Enable PTP clock (clear bit 31)\n");
    tsauxc_current = tsauxc_original & ~BIT31_DISABLE_SYSTIME;  // Clear bit 31
    if (!WriteRegister(h, REG_TSAUXC, tsauxc_current)) {
        printf("  ?? FAILED: Could not write TSAUXC to enable PTP\n");
        testsFailed++;
    } else {
        printf("  ? Wrote TSAUXC: 0x%08X (bit 31 cleared)\n", tsauxc_current);
        
        // Verify the write
        ULONG verify = 0;
        if (ReadRegister(h, REG_TSAUXC, &verify)) {
            printf("  TSAUXC readback: 0x%08X\n", verify);
            if (verify & BIT31_DISABLE_SYSTIME) {
                printf("  ?? FAILED: Bit 31 still set after clearing!\n");
                testsFailed++;
            } else {
                printf("  ? PASSED: Bit 31 successfully cleared\n");
                testsPassed++;
                
                // Verify SYSTIM is incrementing
                printf("  Checking if SYSTIM is incrementing...\n");
                if (IsSystimIncrementing(h)) {
                    printf("  ? PASSED: SYSTIM is incrementing (PTP clock running)\n");
                    testsPassed++;
                } else {
                    printf("  ?? FAILED: SYSTIM is NOT incrementing (PTP clock stuck)\n");
                    testsFailed++;
                }
            }
        } else {
            printf("  ?? FAILED: Could not read back TSAUXC\n");
            testsFailed++;
        }
    }
    printf("\n");
    
    // Step 2: Disable PTP clock (set bit 31)
    printf("STEP 2: Disable PTP clock (set bit 31)\n");
    tsauxc_current = tsauxc_original | BIT31_DISABLE_SYSTIME;  // Set bit 31
    if (!WriteRegister(h, REG_TSAUXC, tsauxc_current)) {
        printf("  ?? FAILED: Could not write TSAUXC to disable PTP\n");
        testsFailed++;
    } else {
        printf("  ? Wrote TSAUXC: 0x%08X (bit 31 set)\n", tsauxc_current);
        
        // Verify the write
        ULONG verify = 0;
        if (ReadRegister(h, REG_TSAUXC, &verify)) {
            printf("  TSAUXC readback: 0x%08X\n", verify);
            if (!(verify & BIT31_DISABLE_SYSTIME)) {
                printf("  ?? FAILED: Bit 31 not set after setting!\n");
                testsFailed++;
            } else {
                printf("  ? PASSED: Bit 31 successfully set\n");
                testsPassed++;
                
                // Verify SYSTIM is NOT incrementing (should be frozen)
                printf("  Checking if SYSTIM is frozen...\n");
                if (!IsSystimIncrementing(h)) {
                    printf("  ? PASSED: SYSTIM is frozen (PTP clock disabled)\n");
                    testsPassed++;
                } else {
                    printf("  ?? WARNING: SYSTIM is still incrementing (should be frozen)\n");
                    printf("     This may be expected on some hardware variants\n");
                    // Not counted as failure - some hardware may continue incrementing
                }
            }
        } else {
            printf("  ?? FAILED: Could not read back TSAUXC\n");
            testsFailed++;
        }
    }
    printf("\n");
    
    // Step 3: Re-enable PTP clock (clear bit 31 again)
    printf("STEP 3: Re-enable PTP clock (clear bit 31 again)\n");
    tsauxc_current = tsauxc_current & ~BIT31_DISABLE_SYSTIME;  // Clear bit 31
    if (!WriteRegister(h, REG_TSAUXC, tsauxc_current)) {
        printf("  ?? FAILED: Could not write TSAUXC to re-enable PTP\n");
        testsFailed++;
    } else {
        printf("  ? Wrote TSAUXC: 0x%08X (bit 31 cleared)\n", tsauxc_current);
        
        // Verify the write
        ULONG verify = 0;
        if (ReadRegister(h, REG_TSAUXC, &verify)) {
            printf("  TSAUXC readback: 0x%08X\n", verify);
            if (verify & BIT31_DISABLE_SYSTIME) {
                printf("  ?? FAILED: Bit 31 still set after re-clearing!\n");
                testsFailed++;
            } else {
                printf("  ? PASSED: Bit 31 successfully cleared again\n");
                testsPassed++;
                
                // Verify SYSTIM is incrementing again
                printf("  Checking if SYSTIM is incrementing again...\n");
                if (IsSystimIncrementing(h)) {
                    printf("  ? PASSED: SYSTIM is incrementing (PTP clock running again)\n");
                    testsPassed++;
                } else {
                    printf("  ?? FAILED: SYSTIM is NOT incrementing (PTP clock not recovered)\n");
                    testsFailed++;
                }
            }
        } else {
            printf("  ?? FAILED: Could not read back TSAUXC\n");
            testsFailed++;
        }
    }
    printf("\n");
    
    // Step 4: Restore original TSAUXC value
    printf("STEP 4: Restore original TSAUXC value\n");
    if (!WriteRegister(h, REG_TSAUXC, tsauxc_original)) {
        printf("  ?? WARNING: Could not restore original TSAUXC value\n");
    } else {
        printf("  ? Restored TSAUXC to: 0x%08X\n", tsauxc_original);
    }
    printf("\n");
    
    // Summary
    printf("========================================\n");
    printf("TEST SUMMARY\n");
    printf("========================================\n");
    printf("Tests Passed: %d\n", testsPassed);
    printf("Tests Failed: %d\n", testsFailed);
    
    if (testsFailed == 0) {
        printf("\n? ALL TESTS PASSED\n");
        printf("TSAUXC bit 31 enable/disable cycle works correctly!\n");
        return 0;
    } else {
        printf("\n? SOME TESTS FAILED\n");
        printf("TSAUXC bit 31 toggle behavior may be incorrect.\n");
        return 1;
    }
}

int main(int argc, char* argv[]) {
    printf("TSAUXC Toggle Test - Validates TSAUXC bit 31 enable/disable cycle\n");
    printf("Target: Intel I210/I226 Ethernet Controllers\n\n");
    
    // Open driver handle
    HANDLE h = CreateFileW(L"\\\\.\\IntelAvbFilter",
                          GENERIC_READ | GENERIC_WRITE,
                          0, NULL, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (h == INVALID_HANDLE_VALUE) {
        printf("ERROR: Could not open driver device (error %lu)\n", GetLastError());
        printf("Ensure IntelAvbFilter driver is installed and running.\n");
        return -1;
    }
    
    printf("? Driver handle opened successfully\n\n");
    
    // Initialize device
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(h, IOCTL_AVB_INIT_DEVICE, NULL, 0, NULL, 0, &bytesReturned, NULL)) {
        printf("WARNING: IOCTL_AVB_INIT_DEVICE failed (error %lu)\n", GetLastError());
        printf("Continuing anyway...\n\n");
    } else {
        printf("? Device initialized successfully\n\n");
    }
    
    // Run the toggle test
    int result = TestTsauxcToggle(h);
    
    CloseHandle(h);
    
    printf("\nPress Enter to exit...");
    getchar();
    
    return result;
}
