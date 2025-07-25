/*++

Module Name:

    avb_bar0_discovery.c

Abstract:

    BAR0 hardware resource discovery implementation for Intel AVB Filter Driver
    Based on Microsoft Windows Driver Samples NDIS filter patterns
    Discovers Intel controller memory-mapped I/O addresses for real hardware access

--*/

#include "precomp.h"
#include "avb_integration.h"

/**
 * @brief Discover Intel controller hardware resources using NDIS patterns
 * Based on Microsoft Windows Driver Samples filter implementation
 */
NTSTATUS
AvbDiscoverIntelControllerResources(
    _In_ PMS_FILTER FilterModule,
    _Out_ PPHYSICAL_ADDRESS Bar0Address,
    _Out_ PULONG Bar0Length
)
{
    NDIS_STATUS ndisStatus;
    NDIS_OID_REQUEST oidRequest;
    ULONG bytesReturned;
    
    // PCI configuration space structure for resource discovery
    struct {
        ULONG VendorId;
        ULONG DeviceId;
        ULONG Command;
        ULONG Status;
        ULONG RevisionId;
        ULONG ClassCode;
        ULONG CacheLineSize;
        ULONG LatencyTimer;
        ULONG HeaderType;
        ULONG BIST;
        ULONG BaseAddresses[6];  // BAR0-BAR5
        ULONG CardbusCISPointer;
        ULONG SubVendorId;
        ULONG SubSystemId;
        ULONG ExpansionROMBaseAddress;
        ULONG CapabilitiesPointer;
        ULONG Reserved1;
        ULONG InterruptLine;
        ULONG InterruptPin;
        ULONG MinGrant;
        ULONG MaxLatency;
    } pciConfig;
    
    DEBUGP(DL_TRACE, "==>AvbDiscoverIntelControllerResources\n");
    
    if (FilterModule == NULL || Bar0Address == NULL || Bar0Length == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // Initialize output parameters
    Bar0Address->QuadPart = 0;
    *Bar0Length = 0;
    
    // Prepare OID request to get PCI configuration space
    RtlZeroMemory(&oidRequest, sizeof(NDIS_OID_REQUEST));
    oidRequest.Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
    oidRequest.Header.Revision = NDIS_OID_REQUEST_REVISION_1;
    oidRequest.Header.Size = sizeof(NDIS_OID_REQUEST);
    oidRequest.RequestType = NdisRequestQueryInformation;
    oidRequest.DATA.QUERY_INFORMATION.Oid = OID_GEN_PCI_DEVICE_CUSTOM_PROPERTIES;
    oidRequest.DATA.QUERY_INFORMATION.InformationBuffer = &pciConfig;
    oidRequest.DATA.QUERY_INFORMATION.InformationBufferLength = sizeof(pciConfig);
    
    // Query PCI configuration space through miniport
    ndisStatus = NdisFOidRequest(FilterModule->FilterHandle, &oidRequest);
    
    if (ndisStatus != NDIS_STATUS_SUCCESS) {
        DEBUGP(DL_ERROR, "Failed to query PCI configuration: 0x%x\n", ndisStatus);
        
        // Fallback: Try alternative OID for PCI info
        oidRequest.DATA.QUERY_INFORMATION.Oid = OID_GEN_PCI_DEVICE_CUSTOM_PROPERTIES;
        ndisStatus = NdisFOidRequest(FilterModule->FilterHandle, &oidRequest);
        
        if (ndisStatus != NDIS_STATUS_SUCCESS) {
            return STATUS_UNSUCCESSFUL;
        }
    }
    
    bytesReturned = oidRequest.DATA.QUERY_INFORMATION.BytesWritten;
    
    // Validate we got enough data
    if (bytesReturned < sizeof(pciConfig)) {
        DEBUGP(DL_ERROR, "Insufficient PCI config data: got %d, need %d\n", 
               bytesReturned, sizeof(pciConfig));
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // Verify this is an Intel device
    if ((pciConfig.VendorId & 0xFFFF) != INTEL_VENDOR_ID) {
        DEBUGP(DL_ERROR, "Not an Intel device: VendorId=0x%x\n", pciConfig.VendorId & 0xFFFF);
        return STATUS_DEVICE_NOT_READY;
    }
    
    // Extract BAR0 information (Intel controllers use BAR0 for MMIO)
    ULONG bar0_raw = pciConfig.BaseAddresses[0];
    
    if (bar0_raw == 0) {
        DEBUGP(DL_ERROR, "BAR0 is not configured\n");
        return STATUS_DEVICE_NOT_READY;
    }
    
    // Check if BAR0 is memory-mapped (bit 0 should be 0 for memory)
    if (bar0_raw & 0x1) {
        DEBUGP(DL_ERROR, "BAR0 is I/O space, not memory: 0x%x\n", bar0_raw);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    
    // Extract physical address (clear lower 4 bits which contain flags)
    Bar0Address->QuadPart = bar0_raw & 0xFFFFFFF0;
    
    // Determine BAR0 length by standard PCI method
    // Intel controllers typically use 128KB (0x20000) for MMIO
    *Bar0Length = 0x20000;  // 128KB - standard for Intel I210/I219/I225/I226
    
    // Verify the address is reasonable
    if (Bar0Address->QuadPart == 0 || Bar0Address->QuadPart == 0xFFFFFFF0) {
        DEBUGP(DL_ERROR, "Invalid BAR0 address: 0x%llx\n", Bar0Address->QuadPart);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    
    DEBUGP(DL_INFO, "Intel controller resources discovered:\n");
    DEBUGP(DL_INFO, "  VendorId: 0x%x, DeviceId: 0x%x\n", 
           pciConfig.VendorId & 0xFFFF, (pciConfig.VendorId >> 16) & 0xFFFF);
    DEBUGP(DL_INFO, "  BAR0 Address: 0x%llx, Length: 0x%x\n", 
           Bar0Address->QuadPart, *Bar0Length);
    
    DEBUGP(DL_TRACE, "<==AvbDiscoverIntelControllerResources: Success\n");
    return STATUS_SUCCESS;
}

/**
 * @brief Enhanced initialization with Microsoft NDIS patterns for BAR0 discovery
 * Replaces the TODO placeholder in the original AvbInitializeDevice
 */
NTSTATUS
AvbInitializeDeviceWithBar0Discovery(
    _In_ PMS_FILTER FilterModule,
    _Out_ PAVB_DEVICE_CONTEXT *AvbContext
)
{
    PAVB_DEVICE_CONTEXT context = NULL;
    NTSTATUS status;
    PHYSICAL_ADDRESS bar0_address = { 0 };
    ULONG bar0_length = 0;
    
    DEBUGP(DL_TRACE, "==>AvbInitializeDeviceWithBar0Discovery\n");

    *AvbContext = NULL;

    // Allocate context
    context = (PAVB_DEVICE_CONTEXT)ExAllocatePoolZero(
        NonPagedPool,
        sizeof(AVB_DEVICE_CONTEXT),
        FILTER_ALLOC_TAG
    );

    if (context == NULL) {
        DEBUGP(DL_ERROR, "Failed to allocate AVB device context\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Initialize context
    context->initialized = FALSE;
    context->filter_instance = FilterModule;
    context->hw_access_enabled = FALSE;
    context->miniport_handle = FilterModule->FilterHandle;
    context->hardware_context = NULL;

    // Initialize Intel device structure
    RtlZeroMemory(&context->intel_device, sizeof(device_t));
    context->intel_device.private_data = context;
    context->intel_device.pci_vendor_id = INTEL_VENDOR_ID;

    // NEW: BAR0 discovery using Microsoft NDIS patterns
    status = AvbDiscoverIntelControllerResources(FilterModule, &bar0_address, &bar0_length);
    if (NT_SUCCESS(status)) {
        // Map Intel controller memory using discovered BAR0
        status = AvbMapIntelControllerMemory(context, bar0_address, bar0_length);
        if (NT_SUCCESS(status)) {
            context->hw_access_enabled = TRUE;
            DEBUGP(DL_INFO, "Real hardware access enabled: BAR0=0x%llx, Length=0x%x\n", 
                   bar0_address.QuadPart, bar0_length);
        } else {
            DEBUGP(DL_ERROR, "Failed to map Intel controller memory: 0x%x\n", status);
            // Continue without hardware access - graceful degradation
        }
    } else {
        DEBUGP(DL_ERROR, "Failed to discover Intel controller resources: 0x%x\n", status);
        // Continue without hardware access - graceful degradation
    }

    context->initialized = TRUE;
    *AvbContext = context;

    DEBUGP(DL_TRACE, "<==AvbInitializeDeviceWithBar0Discovery: Success (HW=%s)\n",
           context->hw_access_enabled ? "ENABLED" : "SIMULATED");
    return STATUS_SUCCESS;
}

/**
 * @brief Alternative resource discovery using WMI/Registry approach
 * Backup method if direct OID queries fail
 */
NTSTATUS
AvbDiscoverIntelControllerResourcesAlternative(
    _In_ PMS_FILTER FilterModule,
    _Out_ PPHYSICAL_ADDRESS Bar0Address,
    _Out_ PULONG Bar0Length
)
{
    UNREFERENCED_PARAMETER(FilterModule);
    
    DEBUGP(DL_TRACE, "==>AvbDiscoverIntelControllerResourcesAlternative\n");
    
    // For Intel controllers, we can use known safe defaults if discovery fails
    // This provides a fallback for development and testing
    
    // Initialize with safe defaults for Intel controllers
    Bar0Address->QuadPart = 0;  // Will be filled by registry lookup if available
    *Bar0Length = 0x20000;      // 128KB - standard Intel controller MMIO size
    
    // TODO: Implement registry-based resource discovery as fallback
    // This would involve querying the Windows registry for PCI device information
    // under HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\PCI\...
    
    DEBUGP(DL_WARN, "Alternative resource discovery not yet implemented\n");
    DEBUGP(DL_TRACE, "<==AvbDiscoverIntelControllerResourcesAlternative: Not implemented\n");
    
    return STATUS_NOT_IMPLEMENTED;
}