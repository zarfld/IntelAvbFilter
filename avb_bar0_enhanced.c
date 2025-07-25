/*++

Module Name:

    avb_bar0_enhanced.c

Abstract:

    Enhanced BAR0 hardware resource discovery for Intel AVB Filter Driver
    Based on Intel official BAR configuration documentation and PCIe specifications
    References:
    - Intel Base Address Register (BAR) Settings documentation
    - Intel PCIe BAR0 implementation guidelines

--*/

#include "precomp.h"
#include "avb_integration.h"

/**
 * @brief Enhanced BAR0 discovery with Intel-specific validation
 * Based on Intel PCIe BAR0 documentation for programmable devices
 */
NTSTATUS
AvbDiscoverIntelControllerResourcesEnhanced(
    _In_ PMS_FILTER FilterModule,
    _Out_ PPHYSICAL_ADDRESS Bar0Address,
    _Out_ PULONG Bar0Length
)
{
    NDIS_STATUS ndisStatus;
    NDIS_OID_REQUEST oidRequest;
    
    // Intel PCIe configuration space structure (per Intel BAR documentation)
    struct {
        ULONG VendorId;          // 0x00: Must be 0x8086 for Intel
        ULONG DeviceId;          // 0x04: Device-specific ID
        ULONG Command;           // 0x08: PCI Command register
        ULONG Status;            // 0x0C: PCI Status register
        ULONG RevisionId;        // 0x10: Class code + Revision
        ULONG ClassCode;         // 0x14: Upper 24 bits of class code
        ULONG CacheLineSize;     // 0x18: Cache line size
        ULONG LatencyTimer;      // 0x1C: Latency timer
        ULONG HeaderType;        // 0x20: Header type
        ULONG BIST;              // 0x24: Built-in self test
        ULONG BaseAddresses[6];  // 0x28-0x3C: BAR0-BAR5 (Intel uses BAR0 for MMIO)
        ULONG CardbusCISPointer; // 0x40: Cardbus CIS pointer
        ULONG SubVendorId;       // 0x44: Subsystem vendor ID
        ULONG SubSystemId;       // 0x48: Subsystem ID
        ULONG ExpansionROMBaseAddress; // 0x4C: Expansion ROM BAR
        ULONG CapabilitiesPointer;     // 0x50: Capabilities pointer
        ULONG Reserved1;         // 0x54: Reserved
        ULONG InterruptLine;     // 0x58: Interrupt line
        ULONG InterruptPin;      // 0x5C: Interrupt pin
        ULONG MinGrant;          // 0x60: Min grant
        ULONG MaxLatency;        // 0x64: Max latency
    } pciConfig;
    
    DEBUGP(DL_TRACE, "==>AvbDiscoverIntelControllerResourcesEnhanced\n");
    
    if (FilterModule == NULL || Bar0Address == NULL || Bar0Length == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // Initialize output parameters
    Bar0Address->QuadPart = 0;
    *Bar0Length = 0;
    
    // Query PCI configuration space through NDIS
    RtlZeroMemory(&oidRequest, sizeof(NDIS_OID_REQUEST));
    oidRequest.Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
    oidRequest.Header.Revision = NDIS_OID_REQUEST_REVISION_1;
    oidRequest.Header.Size = sizeof(NDIS_OID_REQUEST);
    oidRequest.RequestType = NdisRequestQueryInformation;
    oidRequest.DATA.QUERY_INFORMATION.Oid = OID_GEN_PCI_DEVICE_CUSTOM_PROPERTIES;
    oidRequest.DATA.QUERY_INFORMATION.InformationBuffer = &pciConfig;
    oidRequest.DATA.QUERY_INFORMATION.InformationBufferLength = sizeof(pciConfig);
    
    ndisStatus = NdisFOidRequest(FilterModule->FilterHandle, &oidRequest);
    if (ndisStatus != NDIS_STATUS_SUCCESS) {
        DEBUGP(DL_ERROR, "Failed to query PCI configuration: 0x%x\n", ndisStatus);
        return STATUS_UNSUCCESSFUL;
    }
    
    // Validate Intel vendor ID (per Intel BAR documentation)
    if ((pciConfig.VendorId & 0xFFFF) != INTEL_VENDOR_ID) {
        DEBUGP(DL_ERROR, "Not an Intel device: VendorId=0x%x\n", pciConfig.VendorId & 0xFFFF);
        return STATUS_DEVICE_NOT_READY;
    }
    
    USHORT deviceId = (pciConfig.VendorId >> 16) & 0xFFFF;
    DEBUGP(DL_INFO, "Intel device detected: DeviceId=0x%x\n", deviceId);
    
    // Enhanced BAR0 validation per Intel PCIe BAR0 documentation
    ULONG bar0_raw = pciConfig.BaseAddresses[0];
    
    if (bar0_raw == 0) {
        DEBUGP(DL_ERROR, "BAR0 is not configured by BIOS\n");
        return STATUS_DEVICE_NOT_READY;
    }
    
    // Validate BAR0 type (per Intel BAR documentation)
    if (bar0_raw & 0x1) {
        DEBUGP(DL_ERROR, "BAR0 is I/O space, Intel controllers require memory space\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    
    // Check memory type (Intel controllers use 32-bit non-prefetchable memory)
    ULONG memoryType = (bar0_raw >> 1) & 0x3;
    if (memoryType == 0x2) {
        DEBUGP(DL_WARN, "BAR0 indicates 64-bit memory, but Intel Ethernet controllers use 32-bit\n");
    }
    
    // Extract physical address (per PCIe specification - clear lower 4 bits)
    Bar0Address->QuadPart = bar0_raw & 0xFFFFFFF0;
    
    // Intel controller-specific BAR0 length determination
    switch (deviceId) {
        case 0x1533: // I210 copper
        case 0x1536: // I210 fiber
        case 0x1537: // I210 backplane
        case 0x1538: // I210 SGMII
        case 0x157B: // I210 flash-less
            *Bar0Length = 0x20000;  // 128KB for I210 family
            break;
            
        case 0x15B7: // I219-LM
        case 0x15B8: // I219-V
        case 0x15D6: // I219-V (2)
        case 0x15D7: // I219-LM (2)
        case 0x15D8: // I219-LM (3)
            *Bar0Length = 0x20000;  // 128KB for I219 family
            break;
            
        case 0x15F2: // I225-LM
        case 0x15F3: // I225-V
        case 0x0D9F: // I225 variant
            *Bar0Length = 0x20000;  // 128KB for I225 family
            break;
            
        case 0x125B: // I226-LM
        case 0x125C: // I226-V
        case 0x125D: // I226-IT
            *Bar0Length = 0x20000;  // 128KB for I226 family
            break;
            
        default:
            DEBUGP(DL_WARN, "Unknown Intel device ID 0x%x, using default BAR0 size\n", deviceId);
            *Bar0Length = 0x20000;  // Default 128KB
            break;
    }
    
    // Final validation per Intel documentation
    if (Bar0Address->QuadPart == 0 || Bar0Address->QuadPart == 0xFFFFFFF0) {
        DEBUGP(DL_ERROR, "Invalid BAR0 address: 0x%llx\n", Bar0Address->QuadPart);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    
    // Verify alignment (Intel controllers require 4KB alignment minimum)
    if (Bar0Address->QuadPart & 0xFFF) {
        DEBUGP(DL_WARN, "BAR0 address not 4KB aligned: 0x%llx\n", Bar0Address->QuadPart);
    }
    
    DEBUGP(DL_INFO, "Intel BAR0 discovery successful:\n");
    DEBUGP(DL_INFO, "  Device: Intel 0x%x, Address: 0x%llx, Length: 0x%x\n", 
           deviceId, Bar0Address->QuadPart, *Bar0Length);
    
    DEBUGP(DL_TRACE, "<==AvbDiscoverIntelControllerResourcesEnhanced: Success\n");
    return STATUS_SUCCESS;
}