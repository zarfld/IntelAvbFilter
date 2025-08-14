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
            // Uppercase ASCII letters for case-insensitive compare
            if (c1 >= L'a' && c1 <= L'z') c1 = (WCHAR)(c1 - L'a' + L'A');
            if (c2 >= L'a' && c2 <= L'z') c2 = (WCHAR)(c2 - L'a' + L'A');
            if (c1 != c2) { match = FALSE; break; }
        }
        if (match) return TRUE;
    }
    return FALSE;
}

/**
 * @brief Check if NIC is one of our supported Intel controllers using friendly name
 */
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

    // Must contain "Intel"
    if (!WideContainsInsensitive(name, lenChars, L"INTEL")) {
        return FALSE;
    }

    if (OutVendorId) *OutVendorId = INTEL_VENDOR_ID;

    // Match specific controller families
    if (WideContainsInsensitive(name, lenChars, L"I210")) {
        if (OutDeviceId) *OutDeviceId = 0x1533; // representative I210 ID
        return TRUE;
    }
    if (WideContainsInsensitive(name, lenChars, L"I225")) {
        if (OutDeviceId) *OutDeviceId = 0x15F2;
        return TRUE;
    }
    if (WideContainsInsensitive(name, lenChars, L"I226")) {
        if (OutDeviceId) *OutDeviceId = 0x125B;
        return TRUE;
    }
    if (WideContainsInsensitive(name, lenChars, L"I219")) {
        if (OutDeviceId) *OutDeviceId = 0x15B7;
        return TRUE;
    }
    if (WideContainsInsensitive(name, lenChars, L"I217")) {
        if (OutDeviceId) *OutDeviceId = 0x153A;
        return TRUE;
    }

    // Also accept common marketing names
    if (WideContainsInsensitive(name, lenChars, L"ETHERNET CONNECTION I219") ||
        WideContainsInsensitive(name, lenChars, L"ETHERNET CONNECTION I217")) {
        if (OutDeviceId) *OutDeviceId = 0x15B7;
        return TRUE;
    }

    return FALSE;
}

/**
 * @brief Discover Intel controller hardware resources using NDIS patterns
 * Note: As an NDIS LWF we validate the controller but do not directly map BARs here.
 */
NTSTATUS
AvbDiscoverIntelControllerResources(
    _In_ PMS_FILTER FilterModule,
    _Out_ PPHYSICAL_ADDRESS Bar0Address,
    _Out_ PULONG Bar0Length
)
{
    USHORT ven = 0, dev = 0;

    DEBUGP(DL_TRACE, "==>AvbDiscoverIntelControllerResources\n");

    if (FilterModule == NULL || Bar0Address == NULL || Bar0Length == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    Bar0Address->QuadPart = 0;
    *Bar0Length = 0;

    if (!AvbIsSupportedIntelController(FilterModule, &ven, &dev)) {
        DEBUGP(DL_ERROR, "AvbDiscoverIntelControllerResources: Unsupported controller or non-Intel\n");
        return STATUS_DEVICE_NOT_READY;
    }

    DEBUGP(DL_INFO, "Intel controller detected (VendorId=0x%04x, DeviceId=0x%04x); BAR0 mapping not available in LWF context\n", ven, dev);

    DEBUGP(DL_TRACE, "<==AvbDiscoverIntelControllerResources (no BAR mapping)\n");
    return STATUS_NOT_SUPPORTED;
}

/**
 * @brief Enhanced initialization with Microsoft NDIS patterns for BAR0 discovery
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

    // Validate controller and (optionally) discover resources
    status = AvbDiscoverIntelControllerResources(FilterModule, &bar0_address, &bar0_length);
    if (NT_SUCCESS(status)) {
        status = AvbMapIntelControllerMemory(context, bar0_address, bar0_length);
        if (NT_SUCCESS(status)) {
            context->hw_access_enabled = TRUE;
            DEBUGP(DL_INFO, "Real hardware access enabled: BAR0=0x%llx, Length=0x%x\n",
                   bar0_address.QuadPart, bar0_length);
        } else {
            DEBUGP(DL_ERROR, "Failed to map Intel controller memory: 0x%x\n", status);
        }
    } else {
        // STATUS_NOT_SUPPORTED expected: run with simulation fallback
        DEBUGP(DL_WARN, "Hardware BAR mapping not available (status=0x%x); using simulation fallback if needed\n", status);
    }

    context->initialized = TRUE;
    *AvbContext = context;

    DEBUGP(DL_TRACE, "<==AvbInitializeDeviceWithBar0Discovery: Success (HW=%s)\n",
           context->hw_access_enabled ? "ENABLED" : "SIMULATED");
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

    DEBUGP(DL_TRACE, "==>AvbDiscoverIntelControllerResourcesAlternative\n");
    DEBUGP(DL_WARN, "Alternative resource discovery not implemented for LWF context\n");
    DEBUGP(DL_TRACE, "<==AvbDiscoverIntelControllerResourcesAlternative: Not implemented\n");

    return STATUS_NOT_IMPLEMENTED;
}