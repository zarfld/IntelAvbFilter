/*++

Module Name:

    intel_82576_impl.c

Abstract:

    Intel 82576 Gigabit Network Connection device-specific implementation.
    CORRECTED REALITY: 82576 (2009) had experimental PTP but not reliable for production.
    Provides basic MMIO and MDIO access - no reliable PTP or TSN features.
    CLEAN DEVICE SEPARATION: No cross-device contamination.

--*/

#include "precomp.h"
#include "intel_device_interface.h"
#include "avb_integration.h"
#include "external/intel_avb/lib/intel_windows.h"  // Required for platform_ops

// External platform operations
extern const struct platform_ops ndis_platform_ops;

// 82576 Register Definitions - Basic registers only (no reliable PTP registers)
#define E1000_CTRL      0x00000  /* Device Control - RW */
#define E1000_STATUS    0x00008  /* Device Status - RO */
#define E1000_MDIC      0x00020  /* MDI Control - RW */

// 82576 MDIC register bit fields (same as 82575)
#define E1000_MDIC_DATA_MASK    0x0000FFFF
#define E1000_MDIC_REG_MASK     0x001F0000
#define E1000_MDIC_REG_SHIFT    16
#define E1000_MDIC_PHY_MASK     0x03E00000
#define E1000_MDIC_PHY_SHIFT    21
#define E1000_MDIC_OP_MASK      0x0C000000
#define E1000_MDIC_OP_SHIFT     26
#define E1000_MDIC_R_MASK       0x10000000  /* Ready bit */
#define E1000_MDIC_I_MASK       0x20000000  /* Interrupt enable */
#define E1000_MDIC_E_MASK       0x40000000  /* Error bit */

/**
 * @brief Initialize 82576 device
 * @param dev Device handle
 * @return 0 on success, <0 on error
 */
static int init(device_t *dev)
{
    DEBUGP(DL_TRACE, "==>82576_init (82576 basic connectivity only)\n");
    
    if (dev == NULL) {
        return -1;
    }
    
    // 82576-specific platform initialization
    if (ndis_platform_ops.init) {
        NTSTATUS result = ndis_platform_ops.init(dev);
        if (!NT_SUCCESS(result)) {
            DEBUGP(DL_ERROR, "82576 platform init failed: 0x%x\n", result);
            return -1;
        }
    }
    
    DEBUGP(DL_INFO, "? 82576 initialized successfully (basic MMIO/MDIO only)\n");
    DEBUGP(DL_TRACE, "<==82576_init: Success\n");
    return 0;
}

/**
 * @brief Cleanup 82576 device
 * @param dev Device handle
 * @return 0 on success, <0 on error
 */
static int cleanup(device_t *dev)
{
    DEBUGP(DL_TRACE, "==>82576_cleanup\n");
    
    if (dev == NULL) {
        return -1;
    }
    
    if (ndis_platform_ops.cleanup) {
        ndis_platform_ops.cleanup(dev);
    }
    
    DEBUGP(DL_TRACE, "<==82576_cleanup: Success\n");
    return 0;
}

/**
 * @brief Get 82576 device information - CORRECTED: Honest description  
 * @param dev Device handle
 * @param buffer Output buffer
 * @param size Buffer size
 * @return 0 on success, <0 on error
 */
static int get_info(device_t *dev, char *buffer, ULONG size)
{
    const char *info = "Intel 82576 Gigabit Network Connection - Basic MMIO/MDIO (No PTP)";
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
 * @brief 82576-specific MDIO read (enhanced 82575 compatible)
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
    
    DEBUGP(DL_TRACE, "==>82576_mdio_read: phy=0x%x, reg=0x%x\n", phy_addr, reg_addr);
    
    if (dev == NULL || value == NULL) {
        return -1;
    }
    
    // Build MDIC command using e1000 register bit definitions
    mdic_value = 0;
    mdic_value |= (uint32_t)(reg_addr & 0x1F) << E1000_MDIC_REG_SHIFT;
    mdic_value |= (uint32_t)(phy_addr & 0x1F) << E1000_MDIC_PHY_SHIFT;
    mdic_value |= (uint32_t)(2) << E1000_MDIC_OP_SHIFT;  // Read operation
    mdic_value |= E1000_MDIC_I_MASK;  // Interrupt on completion
    
    // Write MDIC command
    result = ndis_platform_ops.mmio_write(dev, E1000_MDIC, mdic_value);
    if (result != 0) {
        DEBUGP(DL_ERROR, "82576 MDIC write failed\n");
        return result;
    }
    
    // Wait for completion with 82576 enhanced timeout
    for (timeout = 0; timeout < 1500; timeout++) {
        result = ndis_platform_ops.mmio_read(dev, E1000_MDIC, &mdic_value);
        if (result != 0) {
            DEBUGP(DL_ERROR, "82576 MDIC read failed during polling\n");
            return result;
        }
        
        // Check for completion
        if (mdic_value & E1000_MDIC_R_MASK) {
            // Check for error
            if (mdic_value & E1000_MDIC_E_MASK) {
                DEBUGP(DL_ERROR, "82576 MDIO read error\n");
                return -1;
            }
            
            // Extract data
            *value = (uint16_t)(mdic_value & E1000_MDIC_DATA_MASK);
            DEBUGP(DL_TRACE, "<==82576_mdio_read: value=0x%x\n", *value);
            return 0;
        }
        
        // Enhanced timing for 82576
        KeStallExecutionProcessor(8); // 8 microseconds
    }
    
    DEBUGP(DL_ERROR, "82576 MDIO read timeout\n");
    return -1;
}

/**
 * @brief 82576-specific MDIO write (enhanced 82575 compatible)
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
    
    DEBUGP(DL_TRACE, "==>82576_mdio_write: phy=0x%x, reg=0x%x, value=0x%x\n", phy_addr, reg_addr, value);
    
    if (dev == NULL) {
        return -1;
    }
    
    // Build MDIC command using e1000 register bit definitions
    mdic_value = value & E1000_MDIC_DATA_MASK;
    mdic_value |= (uint32_t)(reg_addr & 0x1F) << E1000_MDIC_REG_SHIFT;
    mdic_value |= (uint32_t)(phy_addr & 0x1F) << E1000_MDIC_PHY_SHIFT;
    mdic_value |= (uint32_t)(1) << E1000_MDIC_OP_SHIFT;  // Write operation
    mdic_value |= E1000_MDIC_I_MASK;  // Interrupt on completion
    
    // Write MDIC command
    result = ndis_platform_ops.mmio_write(dev, E1000_MDIC, mdic_value);
    if (result != 0) {
        DEBUGP(DL_ERROR, "82576 MDIC write failed\n");
        return result;
    }
    
    // Wait for completion with 82576 enhanced timeout
    for (timeout = 0; timeout < 1500; timeout++) {
        result = ndis_platform_ops.mmio_read(dev, E1000_MDIC, &mdic_value);
        if (result != 0) {
            DEBUGP(DL_ERROR, "82576 MDIC read failed during polling\n");
            return result;
        }
        
        // Check for completion
        if (mdic_value & E1000_MDIC_R_MASK) {
            // Check for error
            if (mdic_value & E1000_MDIC_E_MASK) {
                DEBUGP(DL_ERROR, "82576 MDIO write error\n");
                return -1;
            }
            
            DEBUGP(DL_TRACE, "<==82576_mdio_write: Success\n");
            return 0;
        }
        
        // Enhanced timing for 82576
        KeStallExecutionProcessor(8); // 8 microseconds
    }
    
    DEBUGP(DL_ERROR, "82576 MDIO write timeout\n");
    return -1;
}

/**
 * @brief 82576 device operations structure - CORRECTED: No PTP support
 * 82576 (2009) still predates solid IEEE 1588 implementation in Intel hardware
 */
const intel_device_ops_t e82576_ops = {
    .device_name = "Intel 82576 Gigabit Network Connection - Basic MMIO/MDIO (No PTP)",
    .supported_capabilities = INTEL_CAP_MMIO | INTEL_CAP_MDIO, // REALISTIC: No reliable PTP
    
    // Basic operations - clean generic names
    .init = init,
    .cleanup = cleanup,
    .get_info = get_info,
    
    // PTP operations - NOT RELIABLY SUPPORTED on 82576
    .set_systime = NULL,          // Unreliable PTP hardware
    .get_systime = NULL,          // Unreliable PTP hardware
    .init_ptp = NULL,             // Unreliable PTP hardware
    
    // TSN operations - NOT SUPPORTED (TSN standard didn't exist in 2009)
    .setup_tas = NULL,
    .setup_frame_preemption = NULL,
    .setup_ptm = NULL,
    
    // MDIO operations - enhanced MDIO support
    .mdio_read = mdio_read,
    .mdio_write = mdio_write
};