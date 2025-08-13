/*++

Module Name:

    avb_test_i210.c

Abstract:

    Simple test application for Intel AVB Filter Driver I210 validation
    Tests device detection and basic hardware access via IOCTLs

--*/

#include <windows.h>
#include <stdio.h>
#include <stdint.h>

// IOCTL definitions (must match driver definitions)
#ifndef CTL_CODE
#include <winioctl.h>
#endif

#define IOCTL_AVB_INIT_DEVICE         CTL_CODE(FILE_DEVICE_NETWORK, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AVB_READ_REGISTER       CTL_CODE(FILE_DEVICE_NETWORK, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AVB_GET_DEVICE_INFO     CTL_CODE(FILE_DEVICE_NETWORK, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Request/response structures (must match driver side)
typedef struct _AVB_REGISTER_REQUEST {
    ULONG offset;
    ULONG value;
    ULONG status;
} AVB_REGISTER_REQUEST, *PAVB_REGISTER_REQUEST;

typedef struct _AVB_DEVICE_INFO {
    ULONG device_type;
    ULONG vendor_id;
    ULONG device_id;
    BOOLEAN hw_access_enabled;
    char device_name[64];
} AVB_DEVICE_INFO, *PAVB_DEVICE_INFO;

static BOOL ReadReg(HANDLE hDevice, ULONG offset, ULONG *outValue)
{
    AVB_REGISTER_REQUEST req;
    DWORD bytesReturned = 0;

    req.offset = offset;
    req.value = 0;
    req.status = 0;

    BOOL ok = DeviceIoControl(
        hDevice,
        IOCTL_AVB_READ_REGISTER,
        &req,
        sizeof(req),
        &req,
        sizeof(req),
        &bytesReturned,
        NULL);

    if (!ok) {
        fprintf(stderr, "  ReadReg(0x%08lx) failed, GLE=%lu\n", offset, GetLastError());
        return FALSE;
    }

    if (outValue) *outValue = req.value;
    return TRUE;
}

static void DumpBasicI210Regs(HANDLE hDevice)
{
    ULONG val = 0;
    // Common MAC control/status
    if (ReadReg(hDevice, 0x00000, &val)) printf("  CTRL(0x00000)   = 0x%08lx\n", val);
    if (ReadReg(hDevice, 0x00008, &val)) printf("  STATUS(0x00008) = 0x%08lx\n", val);

    // Time Sync control (I210)
    if (ReadReg(hDevice, 0x0B620, &val)) printf("  TSYNCRXCTL      = 0x%08lx\n", val);
    if (ReadReg(hDevice, 0x0B614, &val)) printf("  TSYNCTXCTL      = 0x%08lx\n", val);

    // Timestamp sample registers (read-only)
    if (ReadReg(hDevice, 0x0B624, &val)) printf("  RXSTMPL         = 0x%08lx\n", val);
    if (ReadReg(hDevice, 0x0B628, &val)) printf("  RXSTMPH         = 0x%08lx\n", val);
    if (ReadReg(hDevice, 0x0B618, &val)) printf("  TXSTMPL         = 0x%08lx\n", val);
    if (ReadReg(hDevice, 0x0B61C, &val)) printf("  TXSTMPH         = 0x%08lx\n", val);
}

int main(void)
{
    HANDLE hDevice = INVALID_HANDLE_VALUE;
    DWORD bytesReturned = 0;
    BOOL result;

    printf("=== Intel AVB Filter Driver I210 Test ===\n\n");

    // Open the device interface created by the filter driver
    hDevice = CreateFileW(L"\\\\.\\IntelAvbFilter",
                         GENERIC_READ | GENERIC_WRITE,
                         0,
                         NULL,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL,
                         NULL);

    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("ERROR: Cannot open \\.\\IntelAvbFilter (GLE=%lu)\n", GetLastError());
        printf("Ensure the driver is installed, started, and the device interface exists.\n");
        return 1;
    }

    printf("? Opened \\.\\IntelAvbFilter successfully\n");

    // Initialize device (no payload expected)
    result = DeviceIoControl(hDevice,
                             IOCTL_AVB_INIT_DEVICE,
                             NULL, 0,
                             NULL, 0,
                             &bytesReturned,
                             NULL);
    printf(result ? "? Device initialization: SUCCESS\n" : "? Device initialization: FAILED (GLE=%lu)\n", GetLastError());

    // Query device info
    AVB_DEVICE_INFO info = {0};
    result = DeviceIoControl(hDevice,
                             IOCTL_AVB_GET_DEVICE_INFO,
                             NULL, 0,
                             &info, sizeof(info),
                             &bytesReturned,
                             NULL);
    if (!result) {
        printf("? Get device info: FAILED (GLE=%lu)\n", GetLastError());
        CloseHandle(hDevice);
        return 2;
    }

    printf("? Device info: SUCCESS\n");
    printf("  Name: %s\n", info.device_name);
    printf("  VID: 0x%04lx  DID: 0x%04lx\n", info.vendor_id & 0xFFFF, info.device_id & 0xFFFF);
    printf("  HW access: %s\n", info.hw_access_enabled ? "ENABLED" : "DISABLED (simulation)");

    // Check that we are on an I210 family device
    switch (info.device_id & 0xFFFF) {
        case 0x1533: // I210
        case 0x1536: // I210
        case 0x1537: // I210
        case 0x1538: // I210
        case 0x157B: // I210
            printf("  Detected I210 family device.\n");
            break;
        default:
            printf("  WARNING: Not an I210 device ID. Proceeding anyway.\n");
            break;
    }

    // Dump a few key registers (works in real hardware mode)
    printf("\n--- Basic I210 register snapshot ---\n");
    DumpBasicI210Regs(hDevice);

    CloseHandle(hDevice);
    return 0;
}
