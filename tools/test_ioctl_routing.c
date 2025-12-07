/**
 * IOCTL Routing Diagnostic Test
 * Tests which IOCTLs reach the driver and which are blocked by Windows
 *
 * Approach:
 * 1. Test each IOCTL individually WITHOUT OPEN_ADAPTER
 * 2. Test each IOCTL WITH OPEN_ADAPTER first
 * 3. Compare registry diagnostic results
 */

#include <windows.h>
#include <stdio.h>
#include "../include/avb_ioctl.h"

static const char* GetIoctlName(DWORD ioctl) {
    switch (ioctl) {
        case IOCTL_AVB_INIT_DEVICE: return "INIT_DEVICE";
        case IOCTL_AVB_ENUM_ADAPTERS: return "ENUM_ADAPTERS";
        case IOCTL_AVB_OPEN_ADAPTER: return "OPEN_ADAPTER";
        case IOCTL_AVB_READ_REGISTER: return "READ_REGISTER";
        case IOCTL_AVB_GET_CLOCK_CONFIG: return "GET_CLOCK_CONFIG";
        default: return "UNKNOWN";
    }
}

static void ClearRegistryDiagnostic(void) {
    HKEY hKey;
    RegDeleteKeyValueA(HKEY_LOCAL_MACHINE, "Software\\IntelAvb", "LastIOCTL");
}

static BOOL CheckRegistryDiagnostic(DWORD expectedIoctl, BOOL* pFound, DWORD* pValue) {
    HKEY hKey;
    DWORD value = 0;
    DWORD size = sizeof(value);
    DWORD type;
    
    LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\IntelAvb", 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) {
        *pFound = FALSE;
        return FALSE;
    }
    
    result = RegQueryValueExA(hKey, "LastIOCTL", NULL, &type, (BYTE*)&value, &size);
    RegCloseKey(hKey);
    
    if (result != ERROR_SUCCESS) {
        *pFound = FALSE;
        return FALSE;
    }
    
    *pFound = TRUE;
    *pValue = value;
    return (value == expectedIoctl);
}

static void TestIoctlWithoutOpen(HANDLE h, DWORD ioctl, const char* name) {
    printf("\n--- Test %s (0x%08X) WITHOUT OPEN_ADAPTER ---\n", name, ioctl);
    
    ClearRegistryDiagnostic();
    
    BYTE buffer[256];
    memset(buffer, 0xCC, sizeof(buffer));
    DWORD bytesReturned = 0;
    
    BOOL result = DeviceIoControl(h, ioctl, buffer, sizeof(buffer), buffer, sizeof(buffer), &bytesReturned, NULL);
    DWORD lastError = GetLastError();
    
    printf("  DeviceIoControl: result=%d, error=%lu, bytes=%lu\n", result, lastError, bytesReturned);
    
    Sleep(100); // Give driver time to write registry
    
    BOOL found;
    DWORD regValue;
    BOOL matches = CheckRegistryDiagnostic(ioctl, &found, &regValue);
    
    if (!found) {
        printf("  Registry diagnostic: KEY NOT FOUND - IOCTL NEVER REACHED DRIVER\n");
    } else if (matches) {
        printf("  Registry diagnostic: FOUND 0x%08X - IOCTL REACHED DRIVER\n", regValue);
    } else {
        printf("  Registry diagnostic: FOUND 0x%08X (expected 0x%08X) - WRONG IOCTL\n", regValue, ioctl);
    }
}

static void TestIoctlWithOpen(HANDLE h, DWORD ioctl, const char* name) {
    printf("\n--- Test %s (0x%08X) WITH OPEN_ADAPTER ---\n", name, ioctl);
    
    // First do ENUM_ADAPTERS to get a device
    AVB_ENUM_REQUEST enumReq;
    memset(&enumReq, 0, sizeof(enumReq));
    enumReq.index = 0;
    DWORD bytes = 0;
    
    if (!DeviceIoControl(h, IOCTL_AVB_ENUM_ADAPTERS, &enumReq, sizeof(enumReq), &enumReq, sizeof(enumReq), &bytes, NULL)) {
        printf("  ENUM_ADAPTERS failed: error=%lu\n", GetLastError());
        return;
    }
    
    if (enumReq.count == 0) {
        printf("  No adapters found\n");
        return;
    }
    
    // Now OPEN_ADAPTER
    AVB_OPEN_REQUEST openReq;
    openReq.vendor_id = enumReq.vendor_id;
    openReq.device_id = enumReq.device_id;
    
    if (!DeviceIoControl(h, IOCTL_AVB_OPEN_ADAPTER, &openReq, sizeof(openReq), &openReq, sizeof(openReq), &bytes, NULL)) {
        printf("  OPEN_ADAPTER failed: error=%lu\n", GetLastError());
        return;
    }
    
    printf("  Opened adapter VID=0x%04X DID=0x%04X\n", enumReq.vendor_id, enumReq.device_id);
    
    // Clear registry and test target IOCTL
    ClearRegistryDiagnostic();
    
    BYTE buffer[256];
    memset(buffer, 0xCC, sizeof(buffer));
    DWORD bytesReturned = 0;
    
    BOOL result = DeviceIoControl(h, ioctl, buffer, sizeof(buffer), buffer, sizeof(buffer), &bytesReturned, NULL);
    DWORD lastError = GetLastError();
    
    printf("  DeviceIoControl: result=%d, error=%lu, bytes=%lu\n", result, lastError, bytesReturned);
    
    Sleep(100); // Give driver time to write registry
    
    BOOL found;
    DWORD regValue;
    BOOL matches = CheckRegistryDiagnostic(ioctl, &found, &regValue);
    
    if (!found) {
        printf("  Registry diagnostic: KEY NOT FOUND - IOCTL NEVER REACHED DRIVER\n");
    } else if (matches) {
        printf("  Registry diagnostic: FOUND 0x%08X - IOCTL REACHED DRIVER\n", regValue);
    } else {
        printf("  Registry diagnostic: FOUND 0x%08X (expected 0x%08X) - WRONG IOCTL\n", regValue, ioctl);
    }
}

int main(void) {
    printf("IOCTL Routing Diagnostic Test\n");
    printf("==============================\n");
    printf("\n");
    printf("GET_CLOCK_CONFIG IOCTL code: 0x%08X\n", IOCTL_AVB_GET_CLOCK_CONFIG);
    printf("READ_REGISTER IOCTL code: 0x%08X\n", IOCTL_AVB_READ_REGISTER);
    printf("\n");
    
    HANDLE h = CreateFileA("\\\\.\\IntelAvbFilter", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("Failed to open device: error=%lu\n", GetLastError());
        return 1;
    }
    
    printf("Device opened successfully\n");
    
    // PHASE 1: Test IOCTLs without OPEN_ADAPTER
    printf("\n");
    printf("========================================\n");
    printf("PHASE 1: Testing IOCTLs WITHOUT OPEN\n");
    printf("========================================\n");
    
    TestIoctlWithoutOpen(h, IOCTL_AVB_ENUM_ADAPTERS, "ENUM_ADAPTERS");
    TestIoctlWithoutOpen(h, IOCTL_AVB_READ_REGISTER, "READ_REGISTER");
    TestIoctlWithoutOpen(h, IOCTL_AVB_GET_CLOCK_CONFIG, "GET_CLOCK_CONFIG");
    
    // PHASE 2: Test IOCTLs with OPEN_ADAPTER
    printf("\n");
    printf("========================================\n");
    printf("PHASE 2: Testing IOCTLs WITH OPEN\n");
    printf("========================================\n");
    
    TestIoctlWithOpen(h, IOCTL_AVB_READ_REGISTER, "READ_REGISTER");
    TestIoctlWithOpen(h, IOCTL_AVB_GET_CLOCK_CONFIG, "GET_CLOCK_CONFIG");
    
    CloseHandle(h);
    
    printf("\n");
    printf("========================================\n");
    printf("DIAGNOSTIC COMPLETE\n");
    printf("========================================\n");
    printf("\n");
    printf("Analysis:\n");
    printf("- If registry key NOT FOUND: IOCTL never reached IntelAvbFilterDeviceIoControl\n");
    printf("- If registry key FOUND: IOCTL reached driver successfully\n");
    printf("- Compare WITHOUT OPEN vs WITH OPEN to see if OPEN_ADAPTER breaks routing\n");
    
    return 0;
}
