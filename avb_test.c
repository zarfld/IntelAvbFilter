/*++

Module Name:

    avb_test.c

Abstract:

    User-mode test application for Intel AVB Filter Driver
    Demonstrates how to use the AVB IOCTLs

--*/

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
/* POLICY COMPLIANCE: use shared ABI header; remove ad-hoc duplicates */
#include <winioctl.h>
#include "include/avb_ioctl.h"

/* Device path for the filter driver */
#define DEVICE_PATH L"\\\\.\\IntelAvbFilter"

/**
 * @brief Open handle to the AVB filter driver
 */
HANDLE OpenAvbDevice(void)
{
    HANDLE hDevice;
    
    hDevice = CreateFile(
        DEVICE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );
    
    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("Failed to open device. Error: %lu\n", GetLastError());
        printf("Make sure the Intel AVB Filter driver is loaded.\n");
    }
    
    return hDevice;
}

/**
 * @brief Test AVB device initialization
 */
BOOL TestAvbInit(HANDLE hDevice)
{
    DWORD bytesReturned;
    BOOL result;
    
    printf("Testing AVB device initialization...\n");
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_INIT_DEVICE,
        NULL,
        0,
        NULL,
        0,
        &bytesReturned,
        NULL
    );
    
    if (result) {
        printf("  SUCCESS: AVB device initialized\n");
    } else {
        printf("  FAILED: AVB device initialization failed. Error: %lu\n", GetLastError());
    }
    
    return result;
}

/**
 * @brief Test getting device information
 */
BOOL TestGetDeviceInfo(HANDLE hDevice)
{
    AVB_DEVICE_INFO_REQUEST request;
    DWORD bytesReturned;
    BOOL result;
    
    printf("Testing device info retrieval...\n");
    
    request.buffer_size = sizeof(request.device_info);
    request.status = 0;
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_GET_DEVICE_INFO,
        &request,
        sizeof(request),
        &request,
        sizeof(request),
        &bytesReturned,
        NULL
    );
    
    if (result) {
        printf("  SUCCESS: Device info retrieved\n");
        printf("  Status: 0x%lx\n", request.status);
        printf("  Info: %.100s\n", request.device_info);
    } else {
        printf("  FAILED: Device info retrieval failed. Error: %lu\n", GetLastError());
    }
    
    return result;
}

/**
 * @brief Test register read
 */
BOOL TestRegisterRead(HANDLE hDevice, UINT32 offset)
{
    AVB_REGISTER_REQUEST request;
    DWORD bytesReturned;
    BOOL result;
    
    printf("Testing register read at offset 0x%x...\n", offset);
    
    request.offset = offset;
    request.value = 0;
    request.status = 0;
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_READ_REGISTER,
        &request,
        sizeof(request),
        &request,
        sizeof(request),
        &bytesReturned,
        NULL
    );
    
    if (result) {
        printf("  SUCCESS: Register read completed\n");
        printf("  Status: 0x%lx\n", request.status);
        printf("  Value: 0x%x\n", request.value);
    } else {
        printf("  FAILED: Register read failed. Error: %lu\n", GetLastError());
    }
    
    return result;
}

/**
 * @brief Test register write
 */
BOOL TestRegisterWrite(HANDLE hDevice, UINT32 offset, UINT32 value)
{
    AVB_REGISTER_REQUEST request;
    DWORD bytesReturned;
    BOOL result;
    
    printf("Testing register write at offset 0x%x with value 0x%x...\n", offset, value);
    
    request.offset = offset;
    request.value = value;
    request.status = 0;
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_WRITE_REGISTER,
        &request,
        sizeof(request),
        &request,
        sizeof(request),
        &bytesReturned,
        NULL
    );
    
    if (result) {
        printf("  SUCCESS: Register write completed\n");
        printf("  Status: 0x%lx\n", request.status);
    } else {
        printf("  FAILED: Register write failed. Error: %lu\n", GetLastError());
    }
    
    return result;
}

/**
 * @brief Test timestamp retrieval
 */
BOOL TestGetTimestamp(HANDLE hDevice)
{
    AVB_TIMESTAMP_REQUEST request;
    DWORD bytesReturned;
    BOOL result;
    
    printf("Testing timestamp retrieval...\n");
    
    request.clock_id = 0; // default clock id
    request.timestamp = 0;
    request.status = 0;
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_GET_TIMESTAMP,
        &request,
        sizeof(request),
        &request,
        sizeof(request),
        &bytesReturned,
        NULL
    );
    
    if (result) {
        printf("  SUCCESS: Timestamp retrieved\n");
        printf("  Status: 0x%lx\n", request.status);
        printf("  Timestamp: %llu\n", request.timestamp);
    } else {
        printf("  FAILED: Timestamp retrieval failed. Error: %lu\n", GetLastError());
    }
    
    return result;
}

/**
 * @brief Main test function
 */
int main(int argc, char* argv[])
{
    HANDLE hDevice;
    int testsPassed = 0;
    int totalTests = 0;
    
    printf("Intel AVB Filter Driver Test Application\n");
    printf("========================================\n\n");
    
    // Open device
    hDevice = OpenAvbDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        return 1;
    }
    
    printf("Device opened successfully.\n\n");
    
    // Run tests
    totalTests++;
    if (TestAvbInit(hDevice)) {
        testsPassed++;
    }
    printf("\n");
    
    totalTests++;
    if (TestGetDeviceInfo(hDevice)) {
        testsPassed++;
    }
    printf("\n");
    
    // Test some common register reads (these are generic Intel NIC registers)
    totalTests++;
    if (TestRegisterRead(hDevice, 0x0000)) { // Device Control Register
        testsPassed++;
    }
    printf("\n");
    
    totalTests++;
    if (TestRegisterRead(hDevice, 0x0008)) { // Device Status Register
        testsPassed++;
    }
    printf("\n");
    
    totalTests++;
    if (TestGetTimestamp(hDevice)) {
        testsPassed++;
    }
    printf("\n");
    
    // Close device
    CloseHandle(hDevice);
    
    printf("Test Results:\n");
    printf("=============\n");
    printf("Tests Passed: %d/%d\n", testsPassed, totalTests);
    
    if (testsPassed == totalTests) {
        printf("All tests passed!\n");
        return 0;
    } else {
        printf("Some tests failed. This is expected if hardware access is not fully implemented.\n");
        return 1;
    }
}
