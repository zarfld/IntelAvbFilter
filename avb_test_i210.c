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

// Match driver's _NDIS_CONTROL_CODE definitions (see avb_integration.h)
#ifndef _NDIS_CONTROL_CODE
#define _NDIS_CONTROL_CODE(request, method) CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, request, method, FILE_ANY_ACCESS)
#endif

#define IOCTL_AVB_INIT_DEVICE         _NDIS_CONTROL_CODE(20, METHOD_BUFFERED)
#define IOCTL_AVB_GET_DEVICE_INFO     _NDIS_CONTROL_CODE(21, METHOD_BUFFERED)
#define IOCTL_AVB_READ_REGISTER       _NDIS_CONTROL_CODE(22, METHOD_BUFFERED)
#define IOCTL_AVB_WRITE_REGISTER      _NDIS_CONTROL_CODE(23, METHOD_BUFFERED)
#define IOCTL_AVB_GET_TIMESTAMP       _NDIS_CONTROL_CODE(24, METHOD_BUFFERED)
#define IOCTL_AVB_SET_TIMESTAMP       _NDIS_CONTROL_CODE(25, METHOD_BUFFERED)
#define IOCTL_AVB_SETUP_TAS           _NDIS_CONTROL_CODE(26, METHOD_BUFFERED)
#define IOCTL_AVB_SETUP_FP            _NDIS_CONTROL_CODE(27, METHOD_BUFFERED)
#define IOCTL_AVB_SETUP_PTM           _NDIS_CONTROL_CODE(28, METHOD_BUFFERED)
#define IOCTL_AVB_MDIO_READ           _NDIS_CONTROL_CODE(29, METHOD_BUFFERED)
#define IOCTL_AVB_MDIO_WRITE          _NDIS_CONTROL_CODE(30, METHOD_BUFFERED)

// Request/response structures (must match driver side)
typedef struct _AVB_REGISTER_REQUEST {
    ULONG offset;
    ULONG value;
    ULONG status; // maps to NDIS_STATUS in driver
} AVB_REGISTER_REQUEST, *PAVB_REGISTER_REQUEST;

// Matches AVB_DEVICE_INFO_REQUEST in avb_integration.h
#define MAX_AVB_DEVICE_INFO_SIZE 1024
typedef struct _AVB_DEVICE_INFO_REQUEST {
    char device_info[MAX_AVB_DEVICE_INFO_SIZE];
    ULONG buffer_size;
    ULONG status; // maps to NDIS_STATUS in driver
} AVB_DEVICE_INFO_REQUEST, *PAVB_DEVICE_INFO_REQUEST;

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

    // Query device info (string buffer from driver)
    AVB_DEVICE_INFO_REQUEST infoReq;
    ZeroMemory(&infoReq, sizeof(infoReq));
    infoReq.buffer_size = MAX_AVB_DEVICE_INFO_SIZE;

    result = DeviceIoControl(hDevice,
                             IOCTL_AVB_GET_DEVICE_INFO,
                             &infoReq, sizeof(infoReq),
                             &infoReq, sizeof(infoReq),
                             &bytesReturned,
                             NULL);
    if (!result) {
        printf("? Get device info: FAILED (GLE=%lu)\n", GetLastError());
        CloseHandle(hDevice);
        return 2;
    }

    printf("? Device info: SUCCESS\n");
    if (infoReq.device_info[0] != '\0') {
        printf("%s\n", infoReq.device_info);
    } else {
        printf("  (Driver returned empty device info)\n");
    }

    // Dump a few key registers (works in real hardware mode)
    printf("\n--- Basic I210 register snapshot ---\n");
    DumpBasicI210Regs(hDevice);

    CloseHandle(hDevice);
    return 0;
}
