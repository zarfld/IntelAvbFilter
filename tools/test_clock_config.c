/**
 * Simple test to diagnose GET_CLOCK_CONFIG failure
 * Tests why IOCTL_AVB_GET_CLOCK_CONFIG returns all zeros
 */

#include <windows.h>
#include <stdio.h>
#include <stdint.h>

// Include IOCTL definitions
#define IOCTL_AVB_BASE 0x8000
#define IOCTL_AVB_GET_CLOCK_CONFIG CTL_CODE(IOCTL_AVB_BASE, 0x27, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AVB_OPEN_ADAPTER CTL_CODE(IOCTL_AVB_BASE, 0x20, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AVB_READ_REGISTER CTL_CODE(IOCTL_AVB_BASE, 0x16, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t status;
} AVB_OPEN_REQUEST;

typedef struct {
    uint64_t systim;
    uint32_t timinca;
    uint32_t tsauxc;
    uint32_t clock_rate_mhz;
    uint32_t status;
} AVB_CLOCK_CONFIG;

typedef struct {
    uint32_t offset;
    uint32_t value;
    uint32_t status;
} AVB_REGISTER_REQUEST;

int main(void)
{
    printf("========================================\n");
    printf("GET_CLOCK_CONFIG DIAGNOSTIC TEST\n");
    printf("========================================\n\n");

    // Open driver
    HANDLE hDriver = CreateFileA(
        "\\\\.\\IntelAvbFilter",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hDriver == INVALID_HANDLE_VALUE) {
        printf("ERROR: Failed to open driver (error %lu)\n", GetLastError());
        printf("Make sure driver is installed and running\n");
        return 1;
    }
    printf("✓ Driver opened\n");
    printf("  Using default context (adapter 0 - the one with full initialization)\n\n");

    // CRITICAL: DO NOT call OPEN_ADAPTER - it switches context away from adapter 0
    // Adapter 0 is the only one with full hardware initialization
    // Other adapters remain in BOUND state until explicitly initialized
    
    DWORD bytesReturned = 0;

    // Test 1: Read registers directly (this works)
    printf("TEST 1: Read PTP registers directly (IOCTL 22)\n");
    printf("-----------------------------------------------\n");
    
    AVB_REGISTER_REQUEST reg = {0};
    
    reg.offset = 0x0B600; // SYSTIML
    DeviceIoControl(hDriver, IOCTL_AVB_READ_REGISTER,
                   &reg, sizeof(reg), &reg, sizeof(reg),
                   &bytesReturned, NULL);
    printf("SYSTIML (0x0B600) = 0x%08X (status=0x%08X)\n", reg.value, reg.status);
    
    reg.offset = 0x0B604; // SYSTIMH
    DeviceIoControl(hDriver, IOCTL_AVB_READ_REGISTER,
                   &reg, sizeof(reg), &reg, sizeof(reg),
                   &bytesReturned, NULL);
    printf("SYSTIMH (0x0B604) = 0x%08X (status=0x%08X)\n", reg.value, reg.status);
    
    reg.offset = 0x0B608; // TIMINCA
    DeviceIoControl(hDriver, IOCTL_AVB_READ_REGISTER,
                   &reg, sizeof(reg), &reg, sizeof(reg),
                   &bytesReturned, NULL);
    printf("TIMINCA (0x0B608) = 0x%08X (status=0x%08X)\n", reg.value, reg.status);
    
    reg.offset = 0x0B640; // TSAUXC
    DeviceIoControl(hDriver, IOCTL_AVB_READ_REGISTER,
                   &reg, sizeof(reg), &reg, sizeof(reg),
                   &bytesReturned, NULL);
    printf("TSAUXC  (0x0B640) = 0x%08X (status=0x%08X)\n\n", reg.value, reg.status);

    // Test 2: Get clock config (this fails)
    printf("TEST 2: Get clock config (IOCTL 39)\n");
    printf("------------------------------------\n");
    
    AVB_CLOCK_CONFIG cfg = {0};
    BOOL success = DeviceIoControl(hDriver, IOCTL_AVB_GET_CLOCK_CONFIG,
                                   &cfg, sizeof(cfg),
                                   &cfg, sizeof(cfg),
                                   &bytesReturned, NULL);
    
    if (!success) {
        printf("ERROR: DeviceIoControl failed (error %lu)\n", GetLastError());
    } else {
        printf("SUCCESS: DeviceIoControl returned\n");
        printf("  Bytes returned: %lu (expected %lu)\n", bytesReturned, (DWORD)sizeof(cfg));
    }
    
    printf("\nClock Config Results:\n");
    printf("  SYSTIM:    0x%016llX\n", cfg.systim);
    printf("  TIMINCA:   0x%08X\n", cfg.timinca);
    printf("  TSAUXC:    0x%08X\n", cfg.tsauxc);
    printf("  Clock Rate: %u MHz\n", cfg.clock_rate_mhz);
    printf("  Status:    0x%08X\n\n", cfg.status);

    printf("ANALYSIS:\n");
    printf("---------\n");
    if (cfg.systim == 0 && cfg.timinca == 0 && cfg.tsauxc == 0) {
        printf("❌ GET_CLOCK_CONFIG is returning all zeros\n");
        printf("   This indicates intel_read_reg() is failing inside the driver\n");
        printf("   Even though direct register reads (IOCTL 22) work fine\n\n");
        printf("LIKELY CAUSE:\n");
        printf("  - Hardware context not properly set up for intel_read_reg()\n");
        printf("  - private_data not initialized for Intel library\n");
        printf("  - Platform operations not properly registered\n\n");
        printf("WORKAROUND:\n");
        printf("  Use IOCTL_AVB_READ_REGISTER (IOCTL 22) to read PTP registers\n");
    } else {
        printf("✓ GET_CLOCK_CONFIG is working!\n");
    }

    CloseHandle(hDriver);
    
    printf("\n========================================\n");
    printf("Press Enter to exit...\n");
    getchar();
    
    return 0;
}
