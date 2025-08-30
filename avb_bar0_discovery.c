/*++

Module Name:

    avb_bar0_discovery.c

Abstract:

    BAR0 hardware resource discovery implementation for Intel AVB Filter Driver
    Provides a path for an NDIS LWF to acquire MMIO (BAR0) for Intel NICs by
    querying PnP properties and reading PCI config via HAL.

--*/

#include "precomp.h"
#include "avb_integration.h"

// Some WDK SDKs do not declare this prototype in headers included by this project.
// Declare it here; the symbol is exported by ntoskrnl.
extern PDEVICE_OBJECT IoGetDeviceAttachmentBaseRef(_In_ PDEVICE_OBJECT DeviceObject);

// Simple case-insensitive substring search for wide strings
static
BOOLEAN
WideContainsInsensitive(
    _In_reads_(lenChars) const WCHAR* buf,
    _In_ USHORT lenChars,
    _In_ const WCHAR* needle
)
{
    if (!buf || !needle || lenChars == 0) return FALSE;

    // Compute needle length
    USHORT nlen = 0;
    while (needle[nlen] != L'\0') nlen++;
    if (nlen == 0 || nlen > lenChars) return FALSE;

    for (USHORT i = 0; i + nlen <= lenChars; ++i) {
        BOOLEAN match = TRUE;
        for (USHORT j = 0; j < nlen; ++j) {
            WCHAR c1 = buf[i + j];
            WCHAR c2 = needle[j];
            if (c1 >= L'a' && c1 <= L'z') c1 = (WCHAR)(c1 - L'a' + L'A');
            if (c2 >= L'a' && c2 <= L'z') c2 = (WCHAR)(c2 - L'a' + L'A');
            if (c1 != c2) { match = FALSE; break; }
        }
        if (match) return TRUE;
    }
    return FALSE;
}

// Whitelist based on friendly name (extra guard)
BOOLEAN
AvbIsSupportedIntelController(
    _In_ PMS_FILTER FilterModule,
    _Out_opt_ USHORT* OutVendorId,
    _Out_opt_ USHORT* OutDeviceId
)
{
    if (OutVendorId) *OutVendorId = 0;
    if (OutDeviceId) *OutDeviceId = 0;

    if (FilterModule == NULL || FilterModule->MiniportFriendlyName.Buffer == NULL) {
        DEBUGP(DL_ERROR, "AvbIsSupportedIntelController: NULL FilterModule or FriendlyName\n");
        return FALSE;
    }

    const WCHAR* name = FilterModule->MiniportFriendlyName.Buffer;
    USHORT lenChars = (USHORT)(FilterModule->MiniportFriendlyName.Length / sizeof(WCHAR));

    DEBUGP(DL_INFO, "AvbIsSupportedIntelController: Checking adapter: %wZ\n", &FilterModule->MiniportFriendlyName);
    
    if (!WideContainsInsensitive(name, lenChars, L"INTEL")) {
        DEBUGP(DL_INFO, "AvbIsSupportedIntelController: Not Intel device (no 'INTEL' string found)\n");
        return FALSE;
    }

    DEBUGP(DL_INFO, "AvbIsSupportedIntelController: Found Intel device, checking specific model...\n");

    if (WideContainsInsensitive(name, lenChars, L"I210")) { 
        if (OutVendorId) *OutVendorId = INTEL_VENDOR_ID; 
        if (OutDeviceId) *OutDeviceId = 0x1533; 
        DEBUGP(DL_INFO, "AvbIsSupportedIntelController: ? SUPPORTED - Intel I210 (VID:0x%04X, DID:0x%04X)\n", 
               INTEL_VENDOR_ID, 0x1533);
        return TRUE; 
    }
    if (WideContainsInsensitive(name, lenChars, L"I225")) { 
        if (OutVendorId) *OutVendorId = INTEL_VENDOR_ID; 
        if (OutDeviceId) *OutDeviceId = 0x15F2; 
        DEBUGP(DL_INFO, "AvbIsSupportedIntelController: ? SUPPORTED - Intel I225 (VID:0x%04X, DID:0x%04X)\n", 
               INTEL_VENDOR_ID, 0x15F2);
        return TRUE; 
    }
    if (WideContainsInsensitive(name, lenChars, L"I226")) { 
        if (OutVendorId) *OutVendorId = INTEL_VENDOR_ID; 
        if (OutDeviceId) *OutDeviceId = 0x125B; 
        DEBUGP(DL_INFO, "AvbIsSupportedIntelController: ? SUPPORTED - Intel I226 (VID:0x%04X, DID:0x%04X)\n", 
               INTEL_VENDOR_ID, 0x125B);
        return TRUE; 
    }
    if (WideContainsInsensitive(name, lenChars, L"I219")) { 
        if (OutVendorId) *OutVendorId = INTEL_VENDOR_ID; 
        if (OutDeviceId) *OutDeviceId = 0x15B7; 
        DEBUGP(DL_INFO, "AvbIsSupportedIntelController: ? SUPPORTED - Intel I219 (VID:0x%04X, DID:0x%04X)\n", 
               INTEL_VENDOR_ID, 0x15B7);
        return TRUE; 
    }
    if (WideContainsInsensitive(name, lenChars, L"I217")) { 
        if (OutVendorId) *OutVendorId = INTEL_VENDOR_ID; 
        if (OutDeviceId) *OutDeviceId = 0x153A; 
        DEBUGP(DL_INFO, "AvbIsSupportedIntelController: ? SUPPORTED - Intel I217 (VID:0x%04X, DID:0x%04X)\n", 
               INTEL_VENDOR_ID, 0x153A);
        return TRUE; 
    }

    // Also accept common marketing names
    if (WideContainsInsensitive(name, lenChars, L"ETHERNET CONNECTION I219") ||
        WideContainsInsensitive(name, lenChars, L"ETHERNET CONNECTION I217")) {
        if (OutVendorId) *OutVendorId = INTEL_VENDOR_ID; if (OutDeviceId) *OutDeviceId = 0x15B7; 
        DEBUGP(DL_INFO, "AvbIsSupportedIntelController: ? SUPPORTED - Intel I219/I217 (marketing name, VID:0x%04X, DID:0x%04X)\n", 
               INTEL_VENDOR_ID, 0x15B7);
        return TRUE;
    }

    DEBUGP(DL_WARN, "AvbIsSupportedIntelController: ? NOT SUPPORTED - Intel device but no AVB/TSN support: %wZ\n", 
           &FilterModule->MiniportFriendlyName);
    return FALSE;
}

#ifndef PCI_WHICHSPACE_CONFIG
#define PCI_WHICHSPACE_CONFIG 0
#endif

typedef union _AVB_PCI_SLOT_NUMBER {
    struct {
        ULONG   FunctionNumber : 3;
        ULONG   DeviceNumber   : 5;
        ULONG   Reserved       : 24;
    } bits;
    ULONG AsULONG;
} AVB_PCI_SLOT_NUMBER, *PAVB_PCI_SLOT_NUMBER;

// Resolve PDO from the miniport device name by getting bottom attachment
static
NTSTATUS
AvbGetPdoFromFilter(
    _In_ PMS_FILTER FilterModule,
    _Outptr_ PDEVICE_OBJECT* PdoOut
)
{
    NTSTATUS status;
    PFILE_OBJECT fileObj = NULL;
    PDEVICE_OBJECT devTop = NULL;

    *PdoOut = NULL;

    if (FilterModule->MiniportName.Buffer == NULL || FilterModule->MiniportName.Length == 0) {
        return STATUS_OBJECT_NAME_INVALID;
    }

    status = IoGetDeviceObjectPointer(&FilterModule->MiniportName,
                                      FILE_READ_DATA,
                                      &fileObj,
                                      &devTop);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_ERROR, "AvbGetPdoFromFilter: IoGetDeviceObjectPointer failed 0x%x for %wZ\n", status, &FilterModule->MiniportName);
        return status;
    }

    // Resolve PDO using IoGetDeviceAttachmentBaseRef (exported by ntoskrnl)
    PDEVICE_OBJECT pdo = IoGetDeviceAttachmentBaseRef(devTop);

    ObDereferenceObject(fileObj);

    if (!pdo) {
        DEBUGP(DL_ERROR, "AvbGetPdoFromFilter: could not resolve PDO\n");
        return STATUS_UNSUCCESSFUL;
    }

    *PdoOut = pdo; // caller must ObDereferenceObject when done
    return STATUS_SUCCESS;
}

// Read a DWORD from PCI config using HAL
static
NTSTATUS
AvbReadPciConfigDword(
    _In_ ULONG BusNumber,
    _In_ AVB_PCI_SLOT_NUMBER Slot,
    _In_ ULONG Offset,
    _Out_ ULONG* Value
)
{
    ULONG read = HalGetBusDataByOffset(PCIConfiguration,
                                       BusNumber,
                                       Slot.AsULONG,
                                       Value,
                                       Offset,
                                       sizeof(ULONG));
    return (read == sizeof(ULONG)) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

// Helper: known BAR lengths per Intel device, based on Intel datasheets (I210/I219/I225/I226)
static
ULONG
AvbGetIntelBarLengthByDeviceId(USHORT deviceId)
{
    switch (deviceId) {
        // I210 family (Datasheet 333016): 128KB CSR space
        case 0x1533: // I210-AT
        case 0x1536: // I210-IS
        case 0x1537: // I210-IT
        case 0x1538: // I210-CS/CL
        case 0x157B: // Flash-less variant
            return 0x20000; // 128KB

        // I217/I219 family (PCH integrated MAC/PHY) — e1000e defines 128KB BAR
        case 0x153A: // I217-LM
        case 0x153B: // I217-V
        case 0x15B7: // I219-LM
        case 0x15B8: // I219-V
        case 0x15D6:
        case 0x15D7:
        case 0x15D8:
        case 0x0DC7:
        case 0x1570:
        case 0x15E3:
            return 0x20000; // 128KB

        // I225/I226 (Spec update/product briefs): 128KB CSR space
        case 0x15F2: // I225
        case 0x15F3:
        case 0x0D9F:
        case 0x125B: // I226
        case 0x125C:
        case 0x125D:
            return 0x20000; // 128KB

        default:
            return 0x20000; // conservative default
    }
}

/**
 * Discover Intel controller hardware resources (BAR0) for MMIO mapping in LWF.
 * Strategy:
 *  1) Open the miniport device by name (AttachParameters->BaseMiniportName).
 *  2) Query bus number and device/function from PnP properties on that device.
 *  3) Read PCI config Vendor/Device IDs and BAR0 using HalGetBusDataByOffset.
 *  4) Return BAR0 physical address and a conservative MMIO size (128KB or per device ID).
 */
NTSTATUS
AvbDiscoverIntelControllerResources(
    _In_ PMS_FILTER FilterModule,
    _Out_ PPHYSICAL_ADDRESS Bar0Address,
    _Out_ PULONG Bar0Length
)
{
    NTSTATUS status;
    PDEVICE_OBJECT pdo = NULL;
    ULONG busNumber = 0;
    ULONG address = 0;
    ULONG len = 0;
    AVB_PCI_SLOT_NUMBER slot = { 0 };
    ULONG id = 0, bar0lo = 0, bar0hi = 0;

    DEBUGP(DL_TRACE, "==>AvbDiscoverIntelControllerResources (MMIO in LWF)\n");

    if (!FilterModule || !Bar0Address || !Bar0Length) return STATUS_INVALID_PARAMETER;

    Bar0Address->QuadPart = 0;
    *Bar0Length = 0;

    status = AvbGetPdoFromFilter(FilterModule, &pdo);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_ERROR, "Failed to resolve PDO: 0x%x\n", status);
        return status;
    }

    status = IoGetDeviceProperty(pdo,
                                 DevicePropertyBusNumber,
                                 sizeof(busNumber),
                                 &busNumber,
                                 &len);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_ERROR, "IoGetDeviceProperty(DevicePropertyBusNumber) failed: 0x%x\n", status);
        ObDereferenceObject(pdo);
        return status;
    }

    len = 0;
    status = IoGetDeviceProperty(pdo,
                                 DevicePropertyAddress,
                                 sizeof(address),
                                 &address,
                                 &len);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_ERROR, "IoGetDeviceProperty(DevicePropertyAddress) failed: 0x%x\n", status);
        ObDereferenceObject(pdo);
        return status;
    }

    ObDereferenceObject(pdo);

    slot.bits.DeviceNumber = (address >> 16) & 0xFFFF;
    slot.bits.FunctionNumber = (address & 0xFFFF) & 0x7;

    // Read Vendor/Device ID (DWORD @ 0x00 per PCI spec)
    status = AvbReadPciConfigDword(busNumber, slot, 0x00, &id);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_ERROR, "Failed to read PCI ID dword: 0x%x\n", status);
        return status;
    }

    USHORT ven = (USHORT)(id & 0xFFFF);
    USHORT dev = (USHORT)((id >> 16) & 0xFFFF);

    if (ven != INTEL_VENDOR_ID) {
        DEBUGP(DL_ERROR, "Not an Intel device: VEN=0x%04x, DEV=0x%04x\n", ven, dev);
        return STATUS_DEVICE_NOT_READY;
    }

    // Read BAR0 low DWORD (@ 0x10). If 64-bit memory BAR, also read BAR1 (@ 0x14)
    status = AvbReadPciConfigDword(busNumber, slot, 0x10, &bar0lo);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_ERROR, "Failed to read BAR0: 0x%x\n", status);
        return status;
    }

    if (bar0lo & 0x1) {
        DEBUGP(DL_ERROR, "BAR0 indicates I/O space, not MMIO: 0x%08x\n", bar0lo);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    ULONGLONG phys = (ULONGLONG)(bar0lo & ~0xFUL);

    // Bits [2:1] == 0b10 indicates 64-bit memory BAR per PCI spec
    if ((bar0lo & 0x6) == 0x4) {
        status = AvbReadPciConfigDword(busNumber, slot, 0x14, &bar0hi);
        if (!NT_SUCCESS(status)) {
            DEBUGP(DL_ERROR, "Failed to read BAR1 (high) for 64-bit BAR: 0x%x\n", status);
            return status;
        }
        phys |= ((ULONGLONG)bar0hi) << 32;
    }

    Bar0Address->QuadPart = phys;
    *Bar0Length = AvbGetIntelBarLengthByDeviceId(dev);

    DEBUGP(DL_INFO, "Intel controller detected: VEN=0x%04x, DEV=0x%04x\n", ven, dev);
    DEBUGP(DL_INFO, "BAR0=0x%llx, Length=0x%x (MMIO enabled, %s BAR)\n",
           Bar0Address->QuadPart, *Bar0Length,
           ((bar0lo & 0x6) == 0x4) ? "64-bit" : "32-bit");

    DEBUGP(DL_TRACE, "<==AvbDiscoverIntelControllerResources: SUCCESS\n");
    return STATUS_SUCCESS;
}

NTSTATUS
AvbDiscoverIntelControllerResourcesAlternative(
    _In_ PMS_FILTER FilterModule,
    _Out_ PPHYSICAL_ADDRESS Bar0Address,
    _Out_ PULONG Bar0Length
)
{
    UNREFERENCED_PARAMETER(FilterModule);
    UNREFERENCED_PARAMETER(Bar0Address);
    UNREFERENCED_PARAMETER(Bar0Length);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
AvbInitializeDeviceWithBar0Discovery(
    _In_ PMS_FILTER FilterModule,
    _Out_ PAVB_DEVICE_CONTEXT *AvbContext
)
{
    NTSTATUS status;
    PHYSICAL_ADDRESS bar0 = { 0 };
    ULONG barLen = 0;

    DEBUGP(DL_TRACE, "==>AvbInitializeDeviceWithBar0Discovery\n");

    if (!AvbContext || !FilterModule) return STATUS_INVALID_PARAMETER;
    *AvbContext = NULL;

    PAVB_DEVICE_CONTEXT ctx = (PAVB_DEVICE_CONTEXT)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(AVB_DEVICE_CONTEXT),
        FILTER_ALLOC_TAG);
    if (!ctx) {
        DEBUGP(DL_ERROR, "AVB ctx allocation failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(&ctx->intel_device, sizeof(ctx->intel_device));
    ctx->initialized = FALSE;
    ctx->filter_instance = FilterModule;
    ctx->hw_access_enabled = FALSE;
    ctx->miniport_handle = FilterModule->FilterHandle;
    ctx->hardware_context = NULL;
    ctx->intel_device.private_data = ctx;
    ctx->intel_device.pci_vendor_id = 0;
    ctx->intel_device.pci_device_id = 0;
    ctx->intel_device.device_type = INTEL_DEVICE_UNKNOWN;

    // Discover BAR0 and classify device
    status = AvbDiscoverIntelControllerResources(FilterModule, &bar0, &barLen);
    if (NT_SUCCESS(status)) {
        // Also read PCI IDs to populate intel_device fields using PDO
        do {
            PDEVICE_OBJECT pdo2 = NULL;
            ULONG busNumber = 0, address = 0, len = 0;
            AVB_PCI_SLOT_NUMBER slot = { 0 };
            ULONG id = 0;
            NTSTATUS st;

            st = AvbGetPdoFromFilter(FilterModule, &pdo2);
            if (!NT_SUCCESS(st)) break;
            st = IoGetDeviceProperty(pdo2, DevicePropertyBusNumber, sizeof(busNumber), &busNumber, &len);
            if (NT_SUCCESS(st)) {
                len = 0;
                st = IoGetDeviceProperty(pdo2, DevicePropertyAddress, sizeof(address), &address, &len);
            }
            ObDereferenceObject(pdo2);
            if (!NT_SUCCESS(st)) break;

            slot.bits.DeviceNumber = (address >> 16) & 0xFFFF;
            slot.bits.FunctionNumber = (address & 0xFFFF) & 0x7;
            st = AvbReadPciConfigDword(busNumber, slot, 0x00, &id);
            if (!NT_SUCCESS(st)) break;
            ctx->intel_device.pci_vendor_id = (USHORT)(id & 0xFFFF);
            ctx->intel_device.pci_device_id = (USHORT)((id >> 16) & 0xFFFF);
            ctx->intel_device.device_type = AvbGetIntelDeviceType(ctx->intel_device.pci_device_id);
            DEBUGP(DL_INFO, "PCI IDs: VEN=0x%04x DEV=0x%04x Type=%d\n", ctx->intel_device.pci_vendor_id, ctx->intel_device.pci_device_id, ctx->intel_device.device_type);
        } while (0);

        NTSTATUS m = AvbMapIntelControllerMemory(ctx, bar0, barLen);
        if (NT_SUCCESS(m)) {
            ctx->hw_access_enabled = TRUE;
            DEBUGP(DL_INFO, "MMIO mapped: BAR0=0x%llx, Len=0x%x\n", bar0.QuadPart, barLen);
        } else {
            DEBUGP(DL_ERROR, "MmMapIoSpace failed: 0x%x\n", m);
        }
    } else {
        DEBUGP(DL_ERROR, "BAR0 discovery failed: 0x%x\n", status);
        // Continue; device fields might still be filled below if needed
    }

    ctx->initialized = TRUE;
    *AvbContext = ctx;

    DEBUGP(DL_TRACE, "<==AvbInitializeDeviceWithBar0Discovery (HW=%s)\n", ctx->hw_access_enabled ? "ENABLED" : "DISABLED");
    return STATUS_SUCCESS;
}

/**
 * @brief Map Intel controller MMIO register space (BAR0) into system virtual address space
 * @param AvbContext Device context
 * @param PhysicalAddress Physical BAR0 base address
 * @param Length Length (bytes) to map
 * @return NTSTATUS STATUS_SUCCESS if mapped successfully
 */
NTSTATUS
AvbMapIntelControllerMemory(
    _In_ PAVB_DEVICE_CONTEXT AvbContext,
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ ULONG Length
)
{
    PINTEL_HARDWARE_CONTEXT hwContext;
    
    DEBUGP(DL_TRACE, "==>AvbMapIntelControllerMemory: PA=0x%llx, Length=0x%x\n", 
           PhysicalAddress.QuadPart, Length);
    
    if (AvbContext == NULL) {
        DEBUGP(DL_ERROR, "AvbMapIntelControllerMemory: Invalid context\n");
        return STATUS_INVALID_PARAMETER;
    }
    
    if (PhysicalAddress.QuadPart == 0 || Length == 0) {
        DEBUGP(DL_ERROR, "AvbMapIntelControllerMemory: Invalid physical address or length\n");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Allocate hardware context
    hwContext = (PINTEL_HARDWARE_CONTEXT)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(INTEL_HARDWARE_CONTEXT),
        FILTER_ALLOC_TAG
    );
    
    if (hwContext == NULL) {
        DEBUGP(DL_ERROR, "AvbMapIntelControllerMemory: Failed to allocate hardware context\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // Initialize hardware context
    RtlZeroMemory(hwContext, sizeof(INTEL_HARDWARE_CONTEXT));

    // Store physical address and length
    hwContext->physical_address = PhysicalAddress;
    hwContext->mmio_length = Length;
    hwContext->mapped = FALSE;
    
    // Map the MMIO region using Windows kernel memory manager
    hwContext->mmio_base = (PUCHAR)MmMapIoSpace(
        PhysicalAddress,
        Length,
        MmNonCached
    );
    
    if (hwContext->mmio_base == NULL) {
        DEBUGP(DL_ERROR, "AvbMapIntelControllerMemory: MmMapIoSpace failed\n");
        ExFreePoolWithTag(hwContext, FILTER_ALLOC_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    hwContext->mapped = TRUE;
    AvbContext->hardware_context = hwContext;
    
    DEBUGP(DL_INFO, "AvbMapIntelControllerMemory: Success - PA=0x%llx mapped to VA=0x%p\n", 
           PhysicalAddress.QuadPart, hwContext->mmio_base);
    DEBUGP(DL_TRACE, "<==AvbMapIntelControllerMemory: SUCCESS\n");
    
    return STATUS_SUCCESS;
}

/**
 * @brief Unmap previously mapped Intel controller MMIO space
 * @param AvbContext Device context
 */
VOID
AvbUnmapIntelControllerMemory(
    _In_ PAVB_DEVICE_CONTEXT AvbContext
)
{
    PINTEL_HARDWARE_CONTEXT hwContext;
    
    DEBUGP(DL_TRACE, "==>AvbUnmapIntelControllerMemory\n");
    
    if (AvbContext == NULL) {
        return;
    }
    
    hwContext = AvbContext->hardware_context;
    if (hwContext == NULL) {
        return;
    }
    
    // Unmap MMIO if it was mapped
    if (hwContext->mapped && hwContext->mmio_base != NULL) {
        MmUnmapIoSpace(hwContext->mmio_base, hwContext->mmio_length);
        DEBUGP(DL_INFO, "AvbUnmapIntelControllerMemory: Unmapped MMIO at VA=0x%p\n", 
               hwContext->mmio_base);
    }
    
    // Free hardware context
    ExFreePoolWithTag(hwContext, FILTER_ALLOC_TAG);
    AvbContext->hardware_context = NULL;
    
    DEBUGP(DL_TRACE, "<==AvbUnmapIntelControllerMemory\n");
}