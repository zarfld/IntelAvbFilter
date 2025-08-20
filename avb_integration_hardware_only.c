/*++

Module Name:

    avb_integration_hardware_only.c

Abstract:

    Intel AVB Filter Driver Integration - REAL HARDWARE ONLY
    NO FALLBACK, NO SIMULATION - Problems must be immediately visible

--*/

#include "precomp.h"
#include "avb_integration.h"

// ? REMOVED: All simulation and fallback code
// ? REAL HARDWARE ONLY: If hardware access fails, operations FAIL explicitly

// Platform operations structure - REAL HARDWARE ONLY
struct platform_ops {
    int (*init)(device_t *dev);
    void (*cleanup)(device_t *dev);
    int (*pci_read_config)(device_t *dev, ULONG offset, ULONG *value);
    int (*pci_write_config)(device_t *dev, ULONG offset, ULONG value);
    int (*mmio_read)(device_t *dev, ULONG offset, ULONG *value);
    int (*mmio_write)(device_t *dev, ULONG offset, ULONG value);
    int (*mdio_read)(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT *value);
    int (*mdio_write)(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT value);
    int (*read_timestamp)(device_t *dev, ULONGLONG *timestamp);
};

// Forward declarations - HARDWARE ONLY implementations
NTSTATUS AvbPlatformInit(device_t *dev);
VOID AvbPlatformCleanup(device_t *dev);

// Real hardware access implementations - NO FALLBACK
int AvbPciReadConfigHardwareOnly(device_t *dev, ULONG offset, ULONG *value);
int AvbPciWriteConfigHardwareOnly(device_t *dev, ULONG offset, ULONG value);
int AvbMmioReadHardwareOnly(device_t *dev, ULONG offset, ULONG *value);
int AvbMmioWriteHardwareOnly(device_t *dev, ULONG offset, ULONG value);
int AvbMdioReadHardwareOnly(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT *value);
int AvbMdioWriteHardwareOnly(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT value);
int AvbReadTimestampHardwareOnly(device_t *dev, ULONGLONG *timestamp);

// Intel library function declarations (real hardware implementations)
int intel_init(device_t *dev);
int intel_detach(device_t *dev);
int intel_get_device_info(device_t *dev, char *info_buffer, size_t buffer_size);
int intel_read_reg(device_t *dev, ULONG offset, ULONG *value);
int intel_write_reg(device_t *dev, ULONG offset, ULONG value);
int intel_gettime(device_t *dev, clockid_t clk_id, ULONGLONG *curtime, struct timespec *system_time);
int intel_set_systime(device_t *dev, ULONGLONG systime);
int intel_setup_time_aware_shaper(device_t *dev, struct tsn_tas_config *config);
int intel_setup_frame_preemption(device_t *dev, struct tsn_fp_config *config);
int intel_setup_ptm(device_t *dev, struct ptm_config *config);
int intel_mdio_read(device_t *dev, ULONG page, ULONG reg, USHORT *value);
int intel_mdio_write(device_t *dev, ULONG page, ULONG reg, USHORT value);

// Platform operations with wrapper functions - HARDWARE ONLY
static int PlatformInitWrapper(device_t *dev) {
    NTSTATUS status = AvbPlatformInit(dev);
    return NT_SUCCESS(status) ? 0 : -1;
}

static void PlatformCleanupWrapper(device_t *dev) {
    AvbPlatformCleanup(dev);
}

// Platform operations structure - NO FALLBACK, REAL HARDWARE ONLY
const struct platform_ops ndis_platform_ops = {
    PlatformInitWrapper,
    PlatformCleanupWrapper,
    AvbPciReadConfigHardwareOnly,      // ? NO FALLBACK: Real PCI config or FAIL
    AvbPciWriteConfigHardwareOnly,     // ? NO FALLBACK: Real PCI config or FAIL
    AvbMmioReadHardwareOnly,           // ? NO FALLBACK: Real MMIO or FAIL
    AvbMmioWriteHardwareOnly,          // ? NO FALLBACK: Real MMIO or FAIL
    AvbMdioReadHardwareOnly,           // ? NO FALLBACK: Real MDIO or FAIL
    AvbMdioWriteHardwareOnly,          // ? NO FALLBACK: Real MDIO or FAIL
    AvbReadTimestampHardwareOnly       // ? NO FALLBACK: Real timestamp or FAIL
};

// Global AVB context
PAVB_DEVICE_CONTEXT g_AvbContext = NULL;

/**
 * @brief Initialize AVB device context for a filter module
 * HARDWARE ONLY - No simulation fallback
 */
NTSTATUS 
AvbInitializeDevice(
    _In_ PMS_FILTER FilterModule,
    _Out_ PAVB_DEVICE_CONTEXT *AvbContext
)
{
    DEBUGP(DL_TRACE, "==>AvbInitializeDevice: HARDWARE ONLY MODE - No fallback allowed\n");
    
    // Call the BAR0 discovery version - MUST SUCCEED or FAIL
    return AvbInitializeDeviceWithBar0DiscoveryHardwareOnly(FilterModule, AvbContext);
}

/**
 * @brief BAR0 discovery and hardware mapping - HARDWARE ONLY
 * MUST succeed or entire initialization FAILS
 */
NTSTATUS 
AvbInitializeDeviceWithBar0DiscoveryHardwareOnly(
    _In_ PMS_FILTER FilterModule,
    _Out_ PAVB_DEVICE_CONTEXT *AvbContext
)
{
    PAVB_DEVICE_CONTEXT context = NULL;
    NTSTATUS status;
    
    DEBUGP(DL_TRACE, "==>AvbInitializeDeviceWithBar0DiscoveryHardwareOnly\n");
    
    if (FilterModule == NULL || AvbContext == NULL) {
        DEBUGP(DL_ERROR, "Invalid parameters to AvbInitializeDeviceWithBar0DiscoveryHardwareOnly\n");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Allocate AVB device context
    context = (PAVB_DEVICE_CONTEXT)ExAllocatePool2(
        POOL_FLAG_NON_PAGED | POOL_FLAG_UNINITIALIZED,
        sizeof(AVB_DEVICE_CONTEXT),
        FILTER_ALLOC_TAG
    );
    
    if (context == NULL) {
        DEBUGP(DL_ERROR, "Failed to allocate AVB device context\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // Initialize context
    RtlZeroMemory(context, sizeof(AVB_DEVICE_CONTEXT));
    context->filter_instance = FilterModule;
    context->initialized = FALSE;
    context->hw_access_enabled = FALSE;
    
    // ? HARDWARE DISCOVERY: MUST succeed or FAIL
    status = AvbDiscoverIntelControllerResourcesHardwareOnly(context);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_ERROR, "? HARDWARE DISCOVERY FAILED: 0x%x - Cannot continue without real hardware access\n", status);
        ExFreePoolWithTag(context, FILTER_ALLOC_TAG);
        return status;  // ? NO FALLBACK: Fail immediately
    }
    
    // ? HARDWARE MAPPING: MUST succeed or FAIL
    status = AvbMapIntelControllerMemoryHardwareOnly(context);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_ERROR, "? HARDWARE MAPPING FAILED: 0x%x - Cannot continue without MMIO access\n", status);
        ExFreePoolWithTag(context, FILTER_ALLOC_TAG);
        return status;  // ? NO FALLBACK: Fail immediately
    }
    
    // ? INTEL LIBRARY INITIALIZATION: MUST succeed
    int result = intel_init(&context->intel_device);
    if (result != 0) {
        DEBUGP(DL_ERROR, "? INTEL LIBRARY INIT FAILED: %d - Real hardware access required\n", result);
        AvbUnmapIntelControllerMemoryHardwareOnly(context);
        ExFreePoolWithTag(context, FILTER_ALLOC_TAG);
        return STATUS_DEVICE_NOT_READY;  // ? NO FALLBACK: Fail immediately
    }
    
    // ? SUCCESS: Real hardware access confirmed
    context->initialized = TRUE;
    context->hw_access_enabled = TRUE;  // Only set if REAL hardware works
    g_AvbContext = context;
    *AvbContext = context;
    
    DEBUGP(DL_INFO, "? HARDWARE ONLY initialization complete - Real hardware access confirmed\n");
    DEBUGP(DL_TRACE, "<==AvbInitializeDeviceWithBar0DiscoveryHardwareOnly: SUCCESS\n");
    
    return STATUS_SUCCESS;
}

/**
 * @brief Discover Intel controller resources - HARDWARE ONLY
 * NO simulation, NO fallback - MUST find real hardware
 */
NTSTATUS 
AvbDiscoverIntelControllerResourcesHardwareOnly(
    _In_ PAVB_DEVICE_CONTEXT AvbContext
)
{
    NTSTATUS status;
    UINT16 vendor_id, device_id;
    
    DEBUGP(DL_TRACE, "==>AvbDiscoverIntelControllerResourcesHardwareOnly\n");
    
    if (AvbContext == NULL || AvbContext->filter_instance == NULL) {
        DEBUGP(DL_ERROR, "Invalid context for hardware resource discovery\n");
        return STATUS_INVALID_PARAMETER;
    }
    
    // ? REAL PCI CONFIG ACCESS: MUST succeed
    status = AvbQueryPciConfigurationHardwareOnly(
        AvbContext->filter_instance,
        0x00,  // Vendor ID offset
        &vendor_id
    );
    
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_ERROR, "? PCI Vendor ID read FAILED: 0x%x - Real hardware access required\n", status);
        return status;  // ? NO FALLBACK
    }
    
    status = AvbQueryPciConfigurationHardwareOnly(
        AvbContext->filter_instance,
        0x02,  // Device ID offset
        &device_id
    );
    
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_ERROR, "? PCI Device ID read FAILED: 0x%x - Real hardware access required\n", status);
        return status;  // ? NO FALLBACK
    }
    
    // ? INTEL DEVICE VALIDATION: MUST be Intel controller
    if (vendor_id != INTEL_VENDOR_ID) {
        DEBUGP(DL_ERROR, "? NOT AN INTEL CONTROLLER: VID=0x%04X (expected 0x8086)\n", vendor_id);
        return STATUS_NOT_SUPPORTED;  // ? NO FALLBACK
    }
    
    // Store real hardware information
    AvbContext->intel_device.pci_vendor_id = vendor_id;
    AvbContext->intel_device.pci_device_id = device_id;
    AvbContext->intel_device.device_type = AvbGetIntelDeviceType(device_id);
    
    if (AvbContext->intel_device.device_type == INTEL_DEVICE_UNKNOWN) {
        DEBUGP(DL_ERROR, "? UNSUPPORTED INTEL DEVICE: DID=0x%04X\n", device_id);
        return STATUS_NOT_SUPPORTED;  // ? NO FALLBACK
    }
    
    // ? BAR0 DISCOVERY: MUST find real hardware resources
    status = AvbDiscoverBar0ResourcesHardwareOnly(AvbContext);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_ERROR, "? BAR0 DISCOVERY FAILED: 0x%x - Cannot continue without MMIO resources\n", status);
        return status;  // ? NO FALLBACK
    }
    
    DEBUGP(DL_INFO, "? REAL HARDWARE DISCOVERED: Intel %s (VID=0x%04X, DID=0x%04X)\n", 
           AvbGetDeviceTypeName(AvbContext->intel_device.device_type),
           vendor_id, device_id);
    
    DEBUGP(DL_TRACE, "<==AvbDiscoverIntelControllerResourcesHardwareOnly: SUCCESS\n");
    return STATUS_SUCCESS;
}

// ? REMOVED: All AvbMmioReadReal with fallback
// ? REPLACED: Hardware-only implementations that FAIL if hardware not available

/**
 * @brief Read MMIO register - HARDWARE ONLY
 * NO fallback - FAILS immediately if hardware not mapped
 */
int AvbMmioReadHardwareOnly(device_t *dev, ULONG offset, ULONG *value)
{
    PAVB_DEVICE_CONTEXT context;
    
    if (dev == NULL || value == NULL) {
        DEBUGP(DL_ERROR, "? AvbMmioReadHardwareOnly: Invalid parameters\n");
        return -EINVAL;
    }
    
    context = (PAVB_DEVICE_CONTEXT)dev->private_data;
    if (context == NULL) {
        DEBUGP(DL_ERROR, "? AvbMmioReadHardwareOnly: No device context\n");
        return -ENODEV;
    }
    
    // ? NO FALLBACK: Hardware mapping MUST exist
    if (context->hardware_context == NULL || 
        context->hardware_context->mmio_base == NULL) {
        DEBUGP(DL_ERROR, "? AvbMmioReadHardwareOnly: Hardware not mapped - offset=0x%x\n", offset);
        DEBUGP(DL_ERROR, "    This indicates BAR0 discovery or memory mapping failed\n");
        DEBUGP(DL_ERROR, "    Fix hardware access implementation before continuing\n");
        return -ENODEV;  // ? EXPLICIT FAILURE
    }
    
    // ? REAL HARDWARE ACCESS ONLY
    *value = READ_REGISTER_ULONG(
        (PULONG)((PUCHAR)context->hardware_context->mmio_base + offset)
    );
    
    DEBUGP(DL_TRACE, "? AvbMmioReadHardwareOnly: offset=0x%x, value=0x%08x (REAL HARDWARE)\n", 
           offset, *value);
    
    return 0;  // ? SUCCESS - Real hardware access
}

/**
 * @brief Write MMIO register - HARDWARE ONLY
 * NO fallback - FAILS immediately if hardware not mapped
 */
int AvbMmioWriteHardwareOnly(device_t *dev, ULONG offset, ULONG value)
{
    PAVB_DEVICE_CONTEXT context;
    
    if (dev == NULL) {
        DEBUGP(DL_ERROR, "? AvbMmioWriteHardwareOnly: Invalid parameters\n");
        return -EINVAL;
    }
    
    context = (PAVB_DEVICE_CONTEXT)dev->private_data;
    if (context == NULL) {
        DEBUGP(DL_ERROR, "? AvbMmioWriteHardwareOnly: No device context\n");
        return -ENODEV;
    }
    
    // ? NO FALLBACK: Hardware mapping MUST exist
    if (context->hardware_context == NULL || 
        context->hardware_context->mmio_base == NULL) {
        DEBUGP(DL_ERROR, "? AvbMmioWriteHardwareOnly: Hardware not mapped - offset=0x%x, value=0x%x\n", 
               offset, value);
        DEBUGP(DL_ERROR, "    This indicates BAR0 discovery or memory mapping failed\n");
        DEBUGP(DL_ERROR, "    Fix hardware access implementation before continuing\n");
        return -ENODEV;  // ? EXPLICIT FAILURE
    }
    
    // ? REAL HARDWARE ACCESS ONLY
    WRITE_REGISTER_ULONG(
        (PULONG)((PUCHAR)context->hardware_context->mmio_base + offset),
        value
    );
    
    DEBUGP(DL_TRACE, "? AvbMmioWriteHardwareOnly: offset=0x%x, value=0x%08x (REAL HARDWARE)\n", 
           offset, value);
    
    return 0;  // ? SUCCESS - Real hardware access
}

/**
 * @brief Read timestamp from hardware - HARDWARE ONLY
 * NO fallback - FAILS immediately if hardware not available
 */
int AvbReadTimestampHardwareOnly(device_t *dev, ULONGLONG *timestamp)
{
    ULONG timestamp_low = 0, timestamp_high = 0;
    int result;
    PAVB_DEVICE_CONTEXT context;
    
    if (dev == NULL || timestamp == NULL) {
        DEBUGP(DL_ERROR, "? AvbReadTimestampHardwareOnly: Invalid parameters\n");
        return -EINVAL;
    }
    context = (PAVB_DEVICE_CONTEXT)dev->private_data;
    if (!context) {
        return -ENODEV;
    }

    // ? NO FALLBACK: Must read from real hardware registers
    switch (context->intel_device.device_type) {
    case INTEL_DEVICE_I210:
    case INTEL_DEVICE_I217:
    case INTEL_DEVICE_I225:
        case INTEL_DEVICE_I226:
            // Use I210 PTP SYSTIM registers from SSOT
            result = AvbMmioReadHardwareOnly(dev, I210_SYSTIMH, &timestamp_high);
            if (result != 0) {
                DEBUGP(DL_ERROR, "? AvbReadTimestampHardwareOnly: Failed to read SYSTIMH\n");
                return result;
            }
            result = AvbMmioReadHardwareOnly(dev, I210_SYSTIML, &timestamp_low);
            if (result != 0) {
                DEBUGP(DL_ERROR, "? AvbReadTimestampHardwareOnly: Failed to read SYSTIML\n");
                return result;
            }
            break;

        case INTEL_DEVICE_I219:
            // Not yet verified in SSOT/spec for I219; avoid guessing offsets.
            DEBUGP(DL_ERROR, "? AvbReadTimestampHardwareOnly: I219 timestamp registers not verified in SSOT/spec yet\n");
            return -ENOTSUP;

        default:
            DEBUGP(DL_ERROR, "? AvbReadTimestampHardwareOnly: Unsupported device type %d\n", context->intel_device.device_type);
            return -ENOTSUP;
    }
    
    // Combine to 64-bit timestamp
    *timestamp = ((ULONGLONG)timestamp_high << 32) | timestamp_low;
    
    DEBUGP(DL_TRACE, "? AvbReadTimestampHardwareOnly: timestamp=0x%016llX (REAL HARDWARE)\n", 
           *timestamp);
    
    return 0;  // ? SUCCESS - Real hardware timestamp
}

// ? REMOVED: All simulation code, fallback mechanisms, and "smart" workarounds
// ? RESULT: Clear, explicit failures when hardware access doesn't work
// ? RESULT: Immediate visibility of what needs to be fixed

/**
 * @brief Cleanup AVB device context - Hardware only cleanup
 */
VOID 
AvbCleanupDevice(
    _In_ PAVB_DEVICE_CONTEXT AvbContext
)
{
    DEBUGP(DL_TRACE, "==>AvbCleanupDevice (Hardware Only)\n");

    if (AvbContext == NULL) {
        return;
    }

    // Cleanup real hardware mappings
    if (AvbContext->hardware_context != NULL) {
        AvbUnmapIntelControllerMemoryHardwareOnly(AvbContext);
    }

    // Cleanup Intel library
    intel_detach(&AvbContext->intel_device);

    // Clear global context if this was it
    if (g_AvbContext == AvbContext) {
        g_AvbContext = NULL;
    }

    // Free the context
    ExFreePoolWithTag(AvbContext, FILTER_ALLOC_TAG);

    DEBUGP(DL_TRACE, "<==AvbCleanupDevice (Hardware Only)\n");
}

/**
 * @brief Device type identification - Intel devices only
 */
intel_device_type_t AvbGetIntelDeviceType(UINT16 device_id)
{
    switch (device_id) {
        case 0x1533: return INTEL_DEVICE_I210;
        case 0x153A: return INTEL_DEVICE_I217;  // I217-LM
        case 0x153B: return INTEL_DEVICE_I217;  // I217-V
        case 0x15B7: return INTEL_DEVICE_I219;  // I219-LM
        case 0x15B8: return INTEL_DEVICE_I219;  // I219-V
        case 0x15D6: return INTEL_DEVICE_I219;  // I219-V
        case 0x15D7: return INTEL_DEVICE_I219;  // I219-LM
        case 0x15D8: return INTEL_DEVICE_I219;  // I219-V
        case 0x0DC7: return INTEL_DEVICE_I219;  // I219-LM (22)
        case 0x1570: return INTEL_DEVICE_I219;  // I219-V (5)
        case 0x15E3: return INTEL_DEVICE_I219;  // I219-LM (6)
        case 0x15F2: return INTEL_DEVICE_I225;
        case 0x125B: return INTEL_DEVICE_I226;
        default: 
            DEBUGP(DL_ERROR, "? UNSUPPORTED DEVICE ID: 0x%04X\n", device_id);
            return INTEL_DEVICE_UNKNOWN;
    }
}

// ? PHILOSOPHY IMPLEMENTED: NO HIDDEN PROBLEMS
// ? RESULT: Every failure is explicit and traceable
// ? RESULT: No confusion about whether real hardware is working
// ? RESULT: Clear path to fix actual hardware access issues