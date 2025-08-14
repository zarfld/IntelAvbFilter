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
        return FALSE;
    }

    const WCHAR* name = FilterModule->MiniportFriendlyName.Buffer;
    USHORT lenChars = (USHORT)(FilterModule->MiniportFriendlyName.Length / sizeof(WCHAR));

    if (!WideContainsInsensitive(name, lenChars, L"INTEL")) return FALSE;

    if (WideContainsInsensitive(name, lenChars, L"I210")) { if (OutVendorId) *OutVendorId = INTEL_VENDOR_ID; if (OutDeviceId) *OutDeviceId = 0x1533; return TRUE; }
    if (WideContainsInsensitive(name, lenChars, L"I225")) { if (OutVendorId) *OutVendorId = INTEL_VENDOR_ID; if (OutDeviceId) *OutDeviceId = 0x15F2; return TRUE; }
    if (WideContainsInsensitive(name, lenChars, L"I226")) { if (OutVendorId) *OutVendorId = INTEL_VENDOR_ID; if (OutDeviceId) *OutDeviceId = 0x125B; return TRUE; }
    if (WideContainsInsensitive(name, lenChars, L"I219")) { if (OutVendorId) *OutVendorId = INTEL_VENDOR_ID; if (OutDeviceId) *OutDeviceId = 0x15B7; return TRUE; }
    if (WideContainsInsensitive(name, lenChars, L"I217")) { if (OutVendorId) *OutVendorId = INTEL_VENDOR_ID; if (OutDeviceId) *OutDeviceId = 0x153A; return TRUE; }

    // Also accept common marketing names
    if (WideContainsInsensitive(name, lenChars, L"ETHERNET CONNECTION I219") ||
        WideContainsInsensitive(name, lenChars, L"ETHERNET CONNECTION I217")) {
        if (OutVendorId) *OutVendorId = INTEL_VENDOR_ID; if (OutDeviceId) *OutDeviceId = 0x15B7; return TRUE;
    }

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

/**
 * Discover Intel controller hardware resources (BAR0) for MMIO mapping in LWF.
 * Strategy:
 *  1) Open the miniport device by name (AttachParameters->BaseMiniportName).
 *  2) Query bus number and device/function from PnP properties on that device.
 *  3) Read PCI config Vendor/Device IDs and BAR0 using HalGetBusDataByOffset.
 *  4) Return BAR0 physical address and a conservative MMIO size (128KB).
 */
NTSTATUS
AvbDiscoverIntelControllerResources(
    _In_ PMS_FILTER FilterModule,
    _Out_ PPHYSICAL_ADDRESS Bar0Address,
    _Out_ PULONG Bar0Length
)
{
    NTSTATUS status;
    PFILE_OBJECT fileObj = NULL;
    PDEVICE_OBJECT devObj = NULL;
    ULONG busNumber = 0;
    ULONG address = 0;
    ULONG len = 0;
    AVB_PCI_SLOT_NUMBER slot = { 0 };
    ULONG id = 0, bar0 = 0;

    DEBUGP(DL_TRACE, "==>AvbDiscoverIntelControllerResources (MMIO in LWF)\n");

    if (!FilterModule || !Bar0Address || !Bar0Length) return STATUS_INVALID_PARAMETER;

    Bar0Address->QuadPart = 0;
    *Bar0Length = 0;

    if (FilterModule->MiniportName.Buffer == NULL || FilterModule->MiniportName.Length == 0) {
        DEBUGP(DL_ERROR, "MiniportName is empty; cannot resolve device object\n");
        return STATUS_OBJECT_NAME_INVALID;
    }

    // Get device object for the miniport name
    status = IoGetDeviceObjectPointer(&FilterModule->MiniportName,
                                      FILE_READ_DATA,
                                      &fileObj,
                                      &devObj);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_ERROR, "IoGetDeviceObjectPointer failed 0x%x for %wZ\n", status, &FilterModule->MiniportName);
        return status;
    }

    // Query PnP properties directly from this device (PnP forwards to PDO)
    status = IoGetDeviceProperty(devObj,
                                 DevicePropertyBusNumber,
                                 sizeof(busNumber),
                                 &busNumber,
                                 &len);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_ERROR, "IoGetDeviceProperty(DevicePropertyBusNumber) failed: 0x%x\n", status);
        ObDereferenceObject(fileObj);
        return status;
    }

    len = 0;
    status = IoGetDeviceProperty(devObj,
                                 DevicePropertyAddress,
                                 sizeof(address),
                                 &address,
                                 &len);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_ERROR, "IoGetDeviceProperty(DevicePropertyAddress) failed: 0x%x\n", status);
        ObDereferenceObject(fileObj);
        return status;
    }

    ObDereferenceObject(fileObj);

    // Extract device/function from address property
    slot.bits.DeviceNumber = (address >> 16) & 0xFFFF;
    slot.bits.FunctionNumber = (address & 0xFFFF) & 0x7;

    // Read VendorID/DeviceID (DWORD at offset 0)
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

    // Read BAR0 (offset 0x10)
    status = AvbReadPciConfigDword(busNumber, slot, 0x10, &bar0);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_ERROR, "Failed to read BAR0: 0x%x\n", status);
        return status;
    }

    if (bar0 & 0x1) {
        DEBUGP(DL_ERROR, "BAR0 indicates I/O space, not MMIO: 0x%08x\n", bar0);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Bar0Address->QuadPart = (ULONGLONG)(bar0 & ~0xFUL);

    // Use a conservative size for Intel controllers (128KB)
    *Bar0Length = 0x20000;

    DEBUGP(DL_INFO, "Intel controller detected: VEN=0x%04x, DEV=0x%04x\n", ven, dev);
    DEBUGP(DL_INFO, "BAR0=0x%llx, Length=0x%x (MMIO enabled)\n", Bar0Address->QuadPart, *Bar0Length);

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

    PAVB_DEVICE_CONTEXT ctx = (PAVB_DEVICE_CONTEXT)ExAllocatePoolZero(
        NonPagedPool,
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

    // Discover BAR0
    status = AvbDiscoverIntelControllerResources(FilterModule, &bar0, &barLen);
    if (NT_SUCCESS(status)) {
        NTSTATUS m = AvbMapIntelControllerMemory(ctx, bar0, barLen);
        if (NT_SUCCESS(m)) {
            ctx->hw_access_enabled = TRUE;
            DEBUGP(DL_INFO, "MMIO mapped: BAR0=0x%llx, Len=0x%x\n", bar0.QuadPart, barLen);
        } else {
            DEBUGP(DL_ERROR, "MmMapIoSpace failed: 0x%x\n", m);
        }
    } else {
        DEBUGP(DL_ERROR, "BAR0 discovery failed: 0x%x\n", status);
        // Continue; we may still classify device
    }

    // Read PCI IDs to classify device
    do {
        PFILE_OBJECT fileObj = NULL;
        PDEVICE_OBJECT devObj = NULL;
        ULONG busNumber = 0, address = 0, len = 0;
        AVB_PCI_SLOT_NUMBER slot = { 0 };
        ULONG id = 0;
        NTSTATUS st;

        if (FilterModule->MiniportName.Buffer == NULL || FilterModule->MiniportName.Length == 0) {
            break;
        }
        st = IoGetDeviceObjectPointer(&FilterModule->MiniportName, FILE_READ_DATA, &fileObj, &devObj);
        if (!NT_SUCCESS(st)) break;
        st = IoGetDeviceProperty(devObj, DevicePropertyBusNumber, sizeof(busNumber), &busNumber, &len);
        if (NT_SUCCESS(st)) {
            len = 0;
            st = IoGetDeviceProperty(devObj, DevicePropertyAddress, sizeof(address), &address, &len);
        }
        ObDereferenceObject(fileObj);
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

    ctx->initialized = TRUE;
    *AvbContext = ctx;

    DEBUGP(DL_TRACE, "<==AvbInitializeDeviceWithBar0Discovery (HW=%s)\n", ctx->hw_access_enabled ? "ENABLED" : "DISABLED");
    return STATUS_SUCCESS;
}