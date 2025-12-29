/*++

Module Name:

    debug_device_interface.c

Abstract:

    Debug tool to check Intel AVB Filter Driver device interface status
    Helps diagnose why CreateFile fails

--*/

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <setupapi.h>

// Device interface debugging
void CheckDeviceInterface() {
    printf("=== Device Interface Debug Tool ===\n\n");
    
    // Try different device name patterns
    const char* device_names[] = {
        "\\\\.\\IntelAvbFilter",
        "\\\\?\\IntelAvbFilter", 
        "\\\\.\\Global\\IntelAvbFilter",
        "\\\\.\\IntelAvbFilter0",
        NULL
    };
    
    printf("Testing various device name patterns...\n");
    
    for (int i = 0; device_names[i] != NULL; i++) {
        printf("Trying: %s\n", device_names[i]);
        
        HANDLE hDevice = CreateFileA(
            device_names[i],
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        
        if (hDevice != INVALID_HANDLE_VALUE) {
            printf("  ? SUCCESS! Device opened successfully\n");
            CloseHandle(hDevice);
            return;
        } else {
            DWORD error = GetLastError();
            printf("  ? Failed with error: %lu", error);
            switch(error) {
                case 2: printf(" (The system cannot find the file specified)"); break;
                case 3: printf(" (The system cannot find the path specified)"); break;
                case 5: printf(" (Access is denied)"); break;
                case 21: printf(" (The device is not ready)"); break;
                default: printf(" (Unknown error)"); break;
            }
            printf("\n");
        }
    }
    
    printf("\n");
}

// Check Windows services
void CheckWindowsServices() {
    printf("=== Windows Services Check ===\n");
    
    SC_HANDLE scm = OpenSCManager(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    if (scm == NULL) {
        printf("? Cannot open Service Control Manager\n");
        return;
    }
    
    SC_HANDLE service = OpenService(scm, "IntelAvbFilter", SERVICE_QUERY_STATUS);
    if (service != NULL) {
        printf("? IntelAvbFilter service found\n");
        
        SERVICE_STATUS status;
        if (QueryServiceStatus(service, &status)) {
            printf("   State: ");
            switch(status.dwCurrentState) {
                case SERVICE_STOPPED: printf("STOPPED"); break;
                case SERVICE_START_PENDING: printf("START_PENDING"); break;
                case SERVICE_STOP_PENDING: printf("STOP_PENDING"); break;
                case SERVICE_RUNNING: printf("RUNNING ?"); break;
                case SERVICE_CONTINUE_PENDING: printf("CONTINUE_PENDING"); break;
                case SERVICE_PAUSE_PENDING: printf("PAUSE_PENDING"); break;
                case SERVICE_PAUSED: printf("PAUSED"); break;
                default: printf("Unknown (%lu)", status.dwCurrentState); break;
            }
            printf("\n");
        }
        
        CloseServiceHandle(service);
    } else {
        printf("? IntelAvbFilter service NOT found\n");
        printf("   This is normal for NDIS filter drivers\n");
    }
    
    CloseServiceHandle(scm);
    printf("\n");
}

// Check registry entries
void CheckRegistryEntries() {
    printf("=== Registry Entries Check ===\n");
    
    HKEY key;
    const char* reg_paths[] = {
        "SYSTEM\\CurrentControlSet\\Services\\IntelAvbFilter",
        "SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E974-E325-11CE-BFC1-08002BE10318}",
        "SYSTEM\\CurrentControlSet\\Control\\Network",
        NULL
    };
    
    for (int i = 0; reg_paths[i] != NULL; i++) {
        printf("Checking: HKLM\\%s\n", reg_paths[i]);
        
        if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, reg_paths[i], 0, KEY_READ, &key) == ERROR_SUCCESS) {
            printf("  ? Registry key exists\n");
            
            // Enumerate some values for debugging
            DWORD values = 0, max_value_len = 0;
            if (RegQueryInfoKey(key, NULL, NULL, NULL, NULL, NULL, NULL, 
                               &values, &max_value_len, NULL, NULL, NULL) == ERROR_SUCCESS) {
                printf("     Values: %lu\n", values);
            }
            
            RegCloseKey(key);
        } else {
            printf("  ? Registry key not found\n");
        }
    }
    
    printf("\n");
}

// Check for NDIS filter binding
void CheckNdisFilterBinding() {
    printf("=== NDIS Filter Binding Check ===\n");
    
    // Try to find network adapters that might have our filter bound
    printf("Checking for Intel network adapters...\n");
    
    HKEY key;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, 
                    "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e972-e325-11ce-bfc1-08002be10318}", 
                    0, KEY_READ, &key) == ERROR_SUCCESS) {
        
        printf("? Network adapter registry found\n");
        
        // Enumerate network adapters
        char subkey_name[256];
        DWORD index = 0;
        DWORD subkey_size = sizeof(subkey_name);
        
        while (RegEnumKeyEx(key, index, subkey_name, &subkey_size, 
                           NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
            
            // Check if this is a number (indicates a network adapter)
            if (isdigit(subkey_name[0])) {
                HKEY adapter_key;
                char full_path[512];
                sprintf_s(full_path, sizeof(full_path), 
                         "SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e972-e325-11ce-bfc1-08002be10318}\\%s", 
                         subkey_name);
                
                if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, full_path, 0, KEY_READ, &adapter_key) == ERROR_SUCCESS) {
                    char desc[256] = {0};
                    DWORD desc_size = sizeof(desc);
                    
                    if (RegQueryValueEx(adapter_key, "DriverDesc", NULL, NULL, 
                                       (LPBYTE)desc, &desc_size) == ERROR_SUCCESS) {
                        if (strstr(desc, "Intel") && strstr(desc, "I219")) {
                            printf("  ?? Found Intel I219: %s\n", desc);
                            
                            // Check if our filter is bound
                            char upper_filters[1024] = {0};
                            DWORD filter_size = sizeof(upper_filters);
                            
                            if (RegQueryValueEx(adapter_key, "UpperFilters", NULL, NULL, 
                                               (LPBYTE)upper_filters, &filter_size) == ERROR_SUCCESS) {
                                if (strstr(upper_filters, "IntelAvbFilter")) {
                                    printf("     ? IntelAvbFilter is bound!\n");
                                } else {
                                    printf("     ? IntelAvbFilter not in UpperFilters\n");
                                    printf("     Current UpperFilters: %s\n", upper_filters);
                                }
                            } else {
                                printf("     ? No UpperFilters found\n");
                            }
                        }
                    }
                    
                    RegCloseKey(adapter_key);
                }
            }
            
            index++;
            subkey_size = sizeof(subkey_name);
        }
        
        RegCloseKey(key);
    }
    
    printf("\n");
}

int main() {
    printf("Intel AVB Filter Driver - Device Interface Debug Tool\n");
    printf("====================================================\n\n");
    
    CheckDeviceInterface();
    CheckWindowsServices(); 
    CheckRegistryEntries();
    CheckNdisFilterBinding();
    
    printf("=== Summary ===\n");
    printf("If device interface fails but service/registry exists,\n");
    printf("the filter driver may need to be bound to network adapters\n");
    printf("using the Network Control Panel method.\n\n");
    
    printf("Next steps:\n");
    printf("1. Use Network Control Panel installation method\n");
    printf("2. Check if filter appears in network adapter properties\n");
    printf("3. Verify filter is bound to Intel I219 adapter\n");
    printf("4. Restart network adapter if needed\n\n");
    
    system("pause");
    return 0;
}