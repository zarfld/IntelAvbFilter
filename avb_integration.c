/*++

Module Name:

    avb_integration.c

Abstract:

    Intel AVB Filter Driver Integration
    Real hardware access - Problems are immediately visible

--*/

#include "precomp.h"
#include "avb_integration.h"

// Platform operations structure
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

// Forward declarations - Clean function names
NTSTATUS AvbPlatformInit(device_t *dev);
VOID AvbPlatformCleanup(device_t *dev);

// Real hardware access implementations
int AvbPciReadConfig(device_t *dev, ULONG offset, ULONG *value);
int AvbPciWriteConfig(device_t *dev, ULONG offset, ULONG value);
int AvbMmioRead(device_t *dev, ULONG offset, ULONG *value);
int AvbMmioWrite(device_t *dev, ULONG offset, ULONG value);
int AvbMdioRead(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT *value);
int AvbMdioWrite(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT value);
int AvbReadTimestamp(device_t *dev, ULONGLONG *timestamp);

// Intel library function declarations
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

// Platform operations with wrapper functions
static int PlatformInitWrapper(device_t *dev) {
    NTSTATUS status = AvbPlatformInit(dev);
    return NT_SUCCESS(status) ? 0 : -1;
}

static void PlatformCleanupWrapper(device_t *dev) {
    AvbPlatformCleanup(dev);
}

// Platform operations structure - Clean interface
const struct platform_ops ndis_platform_ops = {
    PlatformInitWrapper,
    PlatformCleanupWrapper,
    AvbPciReadConfig,      // Clean function names
    AvbPciWriteConfig,     // Clean function names
    AvbMmioRead,           // Clean function names
    AvbMmioWrite,          // Clean function names
    AvbMdioRead,           // Clean function names
    AvbMdioWrite,          // Clean function names
    AvbReadTimestamp       // Clean function names
};

// Global AVB context
PAVB_DEVICE_CONTEXT g_AvbContext = NULL;

/**
 * @brief Initialize AVB device context for a filter module
 */
NTSTATUS 
AvbInitializeDevice(
    _In_ PMS_FILTER FilterModule,
    _Out_ PAVB_DEVICE_CONTEXT *AvbContext
)
{
    DEBUGP(DL_TRACE, "==>AvbInitializeDevice: Real hardware access mode\n");
    
    // Call the BAR0 discovery version from avb_bar0_discovery.c
    return AvbInitializeDeviceWithBar0Discovery(FilterModule, AvbContext);
}

/**
 * @brief BAR0 discovery and hardware mapping
 */
NTSTATUS 
AvbInitializeDeviceWithBar0Discovery(
    _In_ PMS_FILTER FilterModule,
    _Out_ PAVB_DEVICE_CONTEXT *AvbContext
)
{
    DEBUGP(DL_TRACE, "==>AvbInitializeDeviceWithBar0Discovery\n");
    
    // Call the actual implementation from avb_bar0_discovery.c
    return AvbInitializeDeviceWithBar0Discovery(FilterModule, AvbContext);
}

/**
 * @brief Legacy wrapper - redirects to actual BAR0 discovery implementation
 */
NTSTATUS 
AvbDiscoverIntelControllerResources(
    _In_ PAVB_DEVICE_CONTEXT AvbContext
)
{
    PHYSICAL_ADDRESS bar0Address = { 0 };
    ULONG bar0Length = 0;
    
    DEBUGP(DL_TRACE, "==>AvbDiscoverIntelControllerResources (legacy wrapper)\n");
    
    if (AvbContext == NULL || AvbContext->filter_instance == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // Call the real BAR0 discovery function
    return AvbDiscoverIntelControllerResources(AvbContext->filter_instance, &bar0Address, &bar0Length);
}

/**
 * @brief Read MMIO register
 */
int AvbMmioRead(device_t *dev, ULONG offset, ULONG *value)
{
    PAVB_DEVICE_CONTEXT context;
    
    if (dev == NULL || value == NULL) {
        DEBUGP(DL_ERROR, "AvbMmioRead: Invalid parameters\n");
        return -EINVAL;
    }
    
    context = (PAVB_DEVICE_CONTEXT)dev->private_data;
    if (context == NULL) {
        DEBUGP(DL_ERROR, "AvbMmioRead: No device context\n");
        return -ENODEV;
    }
    
    // Check hardware mapping
    if (context->hardware_context == NULL || 
        context->hardware_context->mmio_base == NULL) {
        DEBUGP(DL_ERROR, "AvbMmioRead: Hardware not mapped - offset=0x%x\n", offset);
        DEBUGP(DL_ERROR, "  BAR0 discovery or memory mapping failed\n");
        return -ENODEV;
    }
    
    // Read from hardware
    *value = READ_REGISTER_ULONG(
        (PULONG)((PUCHAR)context->hardware_context->mmio_base + offset)
    );
    
    DEBUGP(DL_TRACE, "AvbMmioRead: offset=0x%x, value=0x%08x\n", offset, *value);
    
    return 0;
}

/**
 * @brief Write MMIO register
 */
int AvbMmioWrite(device_t *dev, ULONG offset, ULONG value)
{
    PAVB_DEVICE_CONTEXT context;
    
    if (dev == NULL) {
        DEBUGP(DL_ERROR, "AvbMmioWrite: Invalid parameters\n");
        return -EINVAL;
    }
    
    context = (PAVB_DEVICE_CONTEXT)dev->private_data;
    if (context == NULL) {
        DEBUGP(DL_ERROR, "AvbMmioWrite: No device context\n");
        return -ENODEV;
    }
    
    // Check hardware mapping
    if (context->hardware_context == NULL || 
        context->hardware_context->mmio_base == NULL) {
        DEBUGP(DL_ERROR, "AvbMmioWrite: Hardware not mapped - offset=0x%x\n", offset);
        return -ENODEV;
    }
    
    // Write to hardware
    WRITE_REGISTER_ULONG(
        (PULONG)((PUCHAR)context->hardware_context->mmio_base + offset),
        value
    );
    
    DEBUGP(DL_TRACE, "AvbMmioWrite: offset=0x%x, value=0x%08x\n", offset, value);
    
    return 0;
}

/**
 * @brief Read timestamp from hardware
 */
int AvbReadTimestamp(device_t *dev, ULONGLONG *timestamp)
{
    ULONG timestamp_low, timestamp_high;
    int result;
    
    if (dev == NULL || timestamp == NULL) {
        DEBUGP(DL_ERROR, "AvbReadTimestamp: Invalid parameters\n");
        return -EINVAL;
    }
    
    // Read timestamp high register first (latches the value)
    result = AvbMmioRead(dev, INTEL_REG_SYSTIMH, &timestamp_high);
    if (result != 0) {
        DEBUGP(DL_ERROR, "AvbReadTimestamp: Failed to read timestamp high register\n");
        return result;
    }
    
    // Read timestamp low register
    result = AvbMmioRead(dev, INTEL_REG_SYSTIML, &timestamp_low);
    if (result != 0) {
        DEBUGP(DL_ERROR, "AvbReadTimestamp: Failed to read timestamp low register\n");
        return result;
    }
    
    // Combine to 64-bit timestamp
    *timestamp = ((ULONGLONG)timestamp_high << 32) | timestamp_low;
    
    DEBUGP(DL_TRACE, "AvbReadTimestamp: timestamp=0x%016llX\n", *timestamp);
    
    return 0;
}

/**
 * @brief Cleanup AVB device context
 */
VOID 
AvbCleanupDevice(
    _In_ PAVB_DEVICE_CONTEXT AvbContext
)
{
    DEBUGP(DL_TRACE, "==>AvbCleanupDevice\n");

    if (AvbContext == NULL) {
        return;
    }

    // Cleanup hardware mappings
    if (AvbContext->hardware_context != NULL) {
        AvbUnmapIntelControllerMemory(AvbContext);
    }

    // Cleanup Intel library
    intel_detach(&AvbContext->intel_device);

    // Clear global context if this was it
    if (g_AvbContext == AvbContext) {
        g_AvbContext = NULL;
    }

    // Free the context
    ExFreePoolWithTag(AvbContext, FILTER_ALLOC_TAG);

    DEBUGP(DL_TRACE, "<==AvbCleanupDevice\n");
}

/**
 * @brief Device type identification
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
            DEBUGP(DL_ERROR, "Unsupported device ID: 0x%04X\n", device_id);
            return INTEL_DEVICE_UNKNOWN;
    }
}

// Clean, professional implementation - no confusing naming conventions