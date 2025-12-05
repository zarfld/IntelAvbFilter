/**
 * Check link status for all Intel I226 adapters
 */

#include <windows.h>
#include <stdio.h>
#include <stdint.h>

// IOCTL definitions
#define IOCTL_AVB_BASE 0x8000
#define IOCTL_AVB_ENUM_ADAPTERS CTL_CODE(IOCTL_AVB_BASE, 0x1F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AVB_OPEN_ADAPTER CTL_CODE(IOCTL_AVB_BASE, 0x20, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AVB_READ_REGISTER CTL_CODE(IOCTL_AVB_BASE, 0x16, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t capabilities;
    uint32_t status;
} AVB_ADAPTER_INFO;

typedef struct {
    uint32_t count;
    AVB_ADAPTER_INFO adapters[16];
    uint32_t status;
} AVB_ENUM_REQUEST;

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t status;
} AVB_OPEN_REQUEST;

typedef struct {
    uint32_t offset;
    uint32_t value;
    uint32_t status;
} AVB_REGISTER_REQUEST;

int main(void)
{
    printf("========================================\n");
    printf("LINK STATUS CHECK FOR ALL ADAPTERS\n");
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
        return 1;
    }

    // Enumerate all adapters
    AVB_ENUM_REQUEST enumReq = {0};
    DWORD bytesReturned = 0;
    
    if (!DeviceIoControl(hDriver, IOCTL_AVB_ENUM_ADAPTERS,
                         &enumReq, sizeof(enumReq),
                         &enumReq, sizeof(enumReq),
                         &bytesReturned, NULL)) {
        printf("ERROR: ENUM_ADAPTERS failed (error %lu)\n", GetLastError());
        CloseHandle(hDriver);
        return 1;
    }

    printf("Found %u adapter(s)\n\n", enumReq.count);

    // Check STATUS register (0x00008) for each adapter
    // Bit 1 (LU - Link Up) indicates link status
    const uint32_t STATUS_REG = 0x00008;
    const uint32_t STATUS_LU = 0x00000002;  // Link Up bit

    for (uint32_t i = 0; i < enumReq.count && i < 16; i++) {
        printf("[Adapter %u] VID=0x%04X DID=0x%04X Caps=0x%08X\n",
               i, enumReq.adapters[i].vendor_id, enumReq.adapters[i].device_id,
               enumReq.adapters[i].capabilities);

        // Open this specific adapter
        AVB_OPEN_REQUEST openReq = {0};
        openReq.vendor_id = enumReq.adapters[i].vendor_id;
        openReq.device_id = enumReq.adapters[i].device_id;
        
        // For multi-adapter systems, we need to distinguish between adapters
        // Since all have same VID/DID, we can't use OPEN_ADAPTER reliably
        // Instead, just check adapter 0 (default) and note the limitation
        
        if (i == 0) {
            // Only adapter 0 is accessible without OPEN_ADAPTER
            AVB_REGISTER_REQUEST reg = {0};
            reg.offset = STATUS_REG;
            
            if (DeviceIoControl(hDriver, IOCTL_AVB_READ_REGISTER,
                               &reg, sizeof(reg),
                               &reg, sizeof(reg),
                               &bytesReturned, NULL)) {
                uint32_t status = reg.value;
                int link_up = (status & STATUS_LU) ? 1 : 0;
                
                printf("  STATUS Register: 0x%08X\n", status);
                printf("  Link Status: %s\n", link_up ? "UP ✓" : "DOWN ✗");
                printf("  Full Duplex: %s\n", (status & 0x00000001) ? "YES" : "NO");
                printf("  Speed: %s\n", 
                       (status & 0x00000080) ? "1000 Mbps" :
                       (status & 0x00000040) ? "100 Mbps" : "10 Mbps");
            } else {
                printf("  ERROR: Could not read STATUS register\n");
            }
        } else {
            printf("  Note: Can only check adapter 0 without OPEN_ADAPTER\n");
            printf("        (OPEN_ADAPTER switches context, breaking default adapter)\n");
        }
        
        printf("\n");
    }

    CloseHandle(hDriver);
    
    printf("========================================\n");
    printf("RECOMMENDATION:\n");
    printf("Use adapter 0 (first adapter) if it has link up\n");
    printf("Or modify driver to initialize all adapters\n");
    printf("========================================\n");
    
    return 0;
}
