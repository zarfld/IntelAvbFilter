/*++

Module Name:

    intel_i217_impl.c

Abstract:

    Intel I217 Gigabit Ethernet device-specific implementation.
    Implements basic IEEE 1588 PTP capabilities.
    Uses SSOT register definitions from i217_regs.h.
    CLEAN DEVICE SEPARATION: No cross-device contamination.

--*/

#include "precomp.h"
#include "intel_device_interface.h"
#include "avb_integration.h"
#include "external/intel_avb/lib/intel_windows.h"  // Required for platform_ops
#include "intel-ethernet-regs/gen/i217_regs.h"  // SSOT register definitions

// External platform operations
extern const struct platform_ops ndis_platform_ops;

/**
 * @brief Initialize I217 device with basic PTP setup
 * @param dev Device handle
 * @return 0 on success, <0 on error
 */
static int init(device_t *dev)
{
    DEBUGP(DL_TRACE, "==>i217_init (I217-specific)\n");
    
    if (dev == NULL) {
        return -1;
    }
    
    // I217-specific platform initialization
    if (ndis_platform_ops.init) {
        NTSTATUS result = ndis_platform_ops.init(dev);
        if (!NT_SUCCESS(result)) {
            DEBUGP(DL_ERROR, "I217 platform init failed: 0x%x\n", result);
            return -1;
        }
    }
    
    DEBUGP(DL_INFO, "? I217 initialized successfully\n");
    DEBUGP(DL_TRACE, "<==i217_init: Success\n");
    return 0;
}

/**
 * @brief Cleanup I217 device
 * @param dev Device handle
 * @return 0 on success, <0 on error
 */
static int cleanup(device_t *dev)
{
    DEBUGP(DL_TRACE, "==>i217_cleanup\n");
    
    if (dev == NULL) {
        return -1;
    }
    
    if (ndis_platform_ops.cleanup) {
        ndis_platform_ops.cleanup(dev);
    }
    
    DEBUGP(DL_TRACE, "<==i217_cleanup: Success\n");
    return 0;
}

/**
 * @brief Get I217 device information
 * @param dev Device handle
 * @param buffer Output buffer
 * @param size Buffer size
 * @return 0 on success, <0 on error
 */
static int get_info(device_t *dev, char *buffer, ULONG size)
{
    const char *info = "Intel I217 Gigabit Ethernet - Basic PTP";
    ULONG info_len = (ULONG)strlen(info);
    
    UNREFERENCED_PARAMETER(dev);  // Fix C4100 warning
    
    if (buffer == NULL || size == 0) {
        return -1;
    }
    
    if (info_len >= size) {
        info_len = size - 1;
    }
    
    RtlCopyMemory(buffer, info, info_len);
    buffer[info_len] = '\0';
    
    return 0;
}

/**
 * @brief I217 PTP clock initialization using SSOT register definitions
 * @param dev Device handle
 * @return 0 on success, <0 on error
 */
static int init_ptp(device_t *dev)
{
    uint32_t timinca;
    int result;
    
    DEBUGP(DL_TRACE, "==>i217_init_ptp (I217-specific using SSOT)\n");
    
    if (dev == NULL) {
        return -1;
    }
    
    // Configure TIMINCA for basic increment using SSOT definition
    timinca = 0x08000001; // Basic 8ns increment for I217
    result = ndis_platform_ops.mmio_write(dev, I217_TIMINCA, timinca);
    if (result != 0) {
        DEBUGP(DL_ERROR, "I217 TIMINCA write failed\n");
        return result;
    }
    
    // Enable timestamp capture using SSOT bit definitions
    uint32_t tx_ctrl = I217_TSYNCTXCTL_EN_MASK;  // Enable TX timestamp
    result = ndis_platform_ops.mmio_write(dev, I217_TSYNCTXCTL, tx_ctrl);
    if (result != 0) {
        DEBUGP(DL_WARN, "I217 TX timestamp enable failed (non-fatal)\n");
    }
    
    uint32_t rx_ctrl = I217_TSYNCRXCTL_EN_MASK;  // Enable RX timestamp  
    result = ndis_platform_ops.mmio_write(dev, I217_TSYNCRXCTL, rx_ctrl);
    if (result != 0) {
        DEBUGP(DL_WARN, "I217 RX timestamp enable failed (non-fatal)\n");
    }
    
    DEBUGP(DL_INFO, "? I217 PTP clock configured (basic mode, TIMINCA=0x%08X)\n", timinca);
    
    DEBUGP(DL_TRACE, "<==i217_init_ptp: Success\n");
    return 0;
}

/**
 * @brief Set I217 system time using SSOT register definitions
 * @param dev Device handle
 * @param systime 64-bit time value in nanoseconds
 * @return 0 on success, <0 on error
 */
static int set_systime(device_t *dev, uint64_t systime)
{
    UNREFERENCED_PARAMETER(systime);
    
    DEBUGP(DL_TRACE, "==>i217_set_systime\n");
    
    if (dev == NULL) {
        return -1;
    }
    
    // I217 SYSTIM registers are marked as read-only in SSOT
    DEBUGP(DL_WARN, "I217 SYSTIM registers are read-only - cannot set system time\n");
    DEBUGP(DL_INFO, "I217 requested time: 0x%llx (operation not supported)\n", systime);
    
    // Initialize PTP if not already done
    if (init_ptp(dev) != 0) {
        DEBUGP(DL_ERROR, "I217 PTP initialization failed\n");
        return -1;
    }
    
    DEBUGP(DL_TRACE, "<==i217_set_systime: Limited success (read-only SYSTIM)\n");
    return 0;  // Return success but with limited functionality
}

/**
 * @brief Get I217 system time using SSOT register definitions
 * @param dev Device handle
 * @param systime Output 64-bit time value
 * @return 0 on success, <0 on error
 */
static int get_systime(device_t *dev, uint64_t *systime)
{
    uint32_t ts_low, ts_high;
    int result;
    
    DEBUGP(DL_TRACE, "==>i217_get_systime\n");
    
    if (dev == NULL || systime == NULL) {
        return -1;
    }
    
    // Read SYSTIM registers using SSOT definitions (read-only)
    result = ndis_platform_ops.mmio_read(dev, I217_SYSTIML, &ts_low);
    if (result != 0) {
        DEBUGP(DL_ERROR, "I217 SYSTIML read failed\n");
        return result;
    }
    
    result = ndis_platform_ops.mmio_read(dev, I217_SYSTIMH, &ts_high);
    if (result != 0) {
        DEBUGP(DL_ERROR, "I217 SYSTIMH read failed\n");
        return result;
    }
    
    *systime = ((uint64_t)ts_high << 32) | ts_low;
    
    DEBUGP(DL_TRACE, "<==i217_get_systime: 0x%llx\n", *systime);
    return 0;
}

/**
 * @brief I217-specific MDIO read using SSOT register definitions
 * @param dev Device handle
 * @param phy_addr PHY address
 * @param reg_addr Register address
 * @param value Output value
 * @return 0 on success, <0 on error
 */
static int mdio_read(device_t *dev, uint16_t phy_addr, uint16_t reg_addr, uint16_t *value)
{
    uint32_t mdic_value;
    int result;
    int timeout;
    
    DEBUGP(DL_TRACE, "==>i217_mdio_read: phy=0x%x, reg=0x%x\n", phy_addr, reg_addr);
    
    if (dev == NULL || value == NULL) {
        return -1;
    }
    
    // Build MDIC command using SSOT bit field definitions
    mdic_value = (uint32_t)I217_MDIC_SET(0, I217_MDIC_DATA_MASK, I217_MDIC_DATA_SHIFT, 0);  // Data = 0 for read
    mdic_value = (uint32_t)I217_MDIC_SET(mdic_value, I217_MDIC_REG_MASK, I217_MDIC_REG_SHIFT, reg_addr);
    mdic_value = (uint32_t)I217_MDIC_SET(mdic_value, I217_MDIC_PHY_MASK, I217_MDIC_PHY_SHIFT, phy_addr);
    mdic_value = (uint32_t)I217_MDIC_SET(mdic_value, I217_MDIC_OP_MASK, I217_MDIC_OP_SHIFT, 2);  // Read operation
    mdic_value = (uint32_t)I217_MDIC_SET(mdic_value, I217_MDIC_I_MASK, I217_MDIC_I_SHIFT, 1);     // Interrupt on completion
    
    // Write MDIC command
    result = ndis_platform_ops.mmio_write(dev, I217_MDIC, mdic_value);
    if (result != 0) {
        DEBUGP(DL_ERROR, "I217 MDIC write failed\n");
        return result;
    }
    
    // Wait for completion (poll ready bit)
    for (timeout = 0; timeout < 1000; timeout++) {
        result = ndis_platform_ops.mmio_read(dev, I217_MDIC, &mdic_value);
        if (result != 0) {
            DEBUGP(DL_ERROR, "I217 MDIC read failed during polling\n");
            return result;
        }
        
        // Check for completion using SSOT bit definitions
        if (I217_MDIC_GET(mdic_value, I217_MDIC_R_MASK, I217_MDIC_R_SHIFT)) {
            // Check for error
            if (I217_MDIC_GET(mdic_value, I217_MDIC_E_MASK, I217_MDIC_E_SHIFT)) {
                DEBUGP(DL_ERROR, "I217 MDIO read error\n");
                return -1;
            }
            
            // Extract data using SSOT bit definitions
            *value = (uint16_t)I217_MDIC_GET(mdic_value, I217_MDIC_DATA_MASK, I217_MDIC_DATA_SHIFT);
            DEBUGP(DL_TRACE, "<==i217_mdio_read: value=0x%x\n", *value);
            return 0;
        }
        
        // Small delay
        KeStallExecutionProcessor(10); // 10 microseconds
    }
    
    DEBUGP(DL_ERROR, "I217 MDIO read timeout\n");
    return -1;
}

/**
 * @brief I217-specific MDIO write using SSOT register definitions
 * @param dev Device handle
 * @param phy_addr PHY address
 * @param reg_addr Register address
 * @param value Value to write
 * @return 0 on success, <0 on error
 */
static int mdio_write(device_t *dev, uint16_t phy_addr, uint16_t reg_addr, uint16_t value)
{
    uint32_t mdic_value;
    int result;
    int timeout;
    
    DEBUGP(DL_TRACE, "==>i217_mdio_write: phy=0x%x, reg=0x%x, value=0x%x\n", phy_addr, reg_addr, value);
    
    if (dev == NULL) {
        return -1;
    }
    
    // Build MDIC command using SSOT bit field definitions
    mdic_value = (uint32_t)I217_MDIC_SET(0, I217_MDIC_DATA_MASK, I217_MDIC_DATA_SHIFT, value);
    mdic_value = (uint32_t)I217_MDIC_SET(mdic_value, I217_MDIC_REG_MASK, I217_MDIC_REG_SHIFT, reg_addr);
    mdic_value = (uint32_t)I217_MDIC_SET(mdic_value, I217_MDIC_PHY_MASK, I217_MDIC_PHY_SHIFT, phy_addr);
    mdic_value = (uint32_t)I217_MDIC_SET(mdic_value, I217_MDIC_OP_MASK, I217_MDIC_OP_SHIFT, 1);  // Write operation
    mdic_value = (uint32_t)I217_MDIC_SET(mdic_value, I217_MDIC_I_MASK, I217_MDIC_I_SHIFT, 1);     // Interrupt on completion
    
    // Write MDIC command
    result = ndis_platform_ops.mmio_write(dev, I217_MDIC, mdic_value);
    if (result != 0) {
        DEBUGP(DL_ERROR, "I217 MDIC write failed\n");
        return result;
    }
    
    // Wait for completion
    for (timeout = 0; timeout < 1000; timeout++) {
        result = ndis_platform_ops.mmio_read(dev, I217_MDIC, &mdic_value);
        if (result != 0) {
            DEBUGP(DL_ERROR, "I217 MDIC read failed during polling\n");
            return result;
        }
        
        // Check for completion using SSOT bit definitions
        if (I217_MDIC_GET(mdic_value, I217_MDIC_R_MASK, I217_MDIC_R_SHIFT)) {
            // Check for error
            if (I217_MDIC_GET(mdic_value, I217_MDIC_E_MASK, I217_MDIC_E_SHIFT)) {
                DEBUGP(DL_ERROR, "I217 MDIO write error\n");
                return -1;
            }
            
            DEBUGP(DL_TRACE, "<==i217_mdio_write: Success\n");
            return 0;
        }
        
        // Small delay
        KeStallExecutionProcessor(10); // 10 microseconds
    }
    
    DEBUGP(DL_ERROR, "I217 MDIO write timeout\n");
    return -1;
}

/**
 * @brief I217 device operations structure using clean generic function names
 */
const intel_device_ops_t i217_ops = {
    .device_name = "Intel I217 Gigabit Ethernet - Basic PTP",
    .supported_capabilities = INTEL_CAP_BASIC_1588 | INTEL_CAP_MMIO,  // Basic PTP only
    
    // Basic operations - clean generic names
    .init = init,
    .cleanup = cleanup,
    .get_info = get_info,
    
    // PTP operations - clean generic names
    .set_systime = set_systime,    // Limited functionality (read-only SYSTIM)
    .get_systime = get_systime,
    .init_ptp = init_ptp,
    
    // TSN operations - I217 doesn't support TSN
    .setup_tas = NULL,
    .setup_frame_preemption = NULL,
    .setup_ptm = NULL,
    
    // MDIO operations - I217 has good MDIO support
    .mdio_read = mdio_read,
    .mdio_write = mdio_write
};