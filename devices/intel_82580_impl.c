/*++

Module Name:

    intel_82580_impl.c

Abstract:

    Intel 82580 Gigabit Network Connection device-specific implementation.
    Implements enhanced IEEE 1588 PTP capabilities with improved timestamping.
    Based on Intel IGB driver specifications and e1000_regs.h definitions.
    CLEAN DEVICE SEPARATION: No cross-device contamination.

--*/

#include "precomp.h"
#include "intel_device_interface.h"
#include "avb_integration.h"
#include "external/intel_avb/lib/intel_windows.h"  // Required for platform_ops

// External platform operations
extern const struct platform_ops ndis_platform_ops;

// 82580 Register Definitions - Based on e1000_regs.h
#define E1000_SYSTIML   0x0B600  /* System time register Low - RO */
#define E1000_SYSTIMH   0x0B604  /* System time register High - RO */
#define E1000_TIMINCA   0x0B608  /* Increment attributes register - RW */
#define E1000_TSAUXC    0x0B640  /* Timesync Auxiliary Control register */
#define E1000_TSYNCRXCTL 0x0B620 /* Rx Time Sync Control register - RW */
#define E1000_TSYNCTXCTL 0x0B614 /* Tx Time Sync Control register - RW */
#define E1000_RXSTMPL   0x0B624  /* Rx timestamp Low - RO */
#define E1000_RXSTMPH   0x0B628  /* Rx timestamp High - RO */
#define E1000_TXSTMPL   0x0B618  /* Tx timestamp value Low - RO */
#define E1000_TXSTMPH   0x0B61C  /* Tx timestamp value High - RO */

// 82580 Control Register Definitions
#define E1000_CTRL      0x00000  /* Device Control - RW */
#define E1000_STATUS    0x00008  /* Device Status - RO */
#define E1000_MDIC      0x00020  /* MDI Control - RW */

// 82580 Enhanced PTP Control Bits (improved over 82575)
#define E1000_TSYNCRXCTL_ENABLED    0x00000010  /* enable Rx timestamping */
#define E1000_TSYNCTXCTL_ENABLED    0x00000010  /* enable Tx timestamping */
#define E1000_TSYNC_RECEIVE_EVENT   0x00000040  /* packet is sync or delay_req */
#define E1000_TSYNC_TXCTL_MAX_DELAY 0x0000000F  /* max delay for timestamp */
#define E1000_TSYNCTXCTL_START_SYNC 0x80000000  /* initiate Tx timestamp */
#define E1000_TSYNC_INTERRUPT_MASK  0x00000001  /* timestamp interrupt mask */

// 82580-specific MDIC register bit fields
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

// 82580 timestamp shift constant from IGB driver
#define IGB_82580_TSYNC_SHIFT 24

/**
 * @brief Initialize 82580 device
 * @param dev Device handle
 * @return 0 on success, <0 on error
 */
static int init(device_t *dev)
{
    DEBUGP(DL_TRACE, "==>82580_init (82580-specific)\n");
    
    if (dev == NULL) {
        return -1;
    }
    
    // 82580-specific platform initialization
    if (ndis_platform_ops.init) {
        NTSTATUS result = ndis_platform_ops.init(dev);
        if (!NT_SUCCESS(result)) {
            DEBUGP(DL_ERROR, "82580 platform init failed: 0x%x\n", result);
            return -1;
        }
    }
    
    DEBUGP(DL_INFO, "? 82580 initialized successfully\n");
    DEBUGP(DL_TRACE, "<==82580_init: Success\n");
    return 0;
}

/**
 * @brief Cleanup 82580 device
 * @param dev Device handle
 * @return 0 on success, <0 on error
 */
static int cleanup(device_t *dev)
{
    DEBUGP(DL_TRACE, "==>82580_cleanup\n");
    
    if (dev == NULL) {
        return -1;
    }
    
    if (ndis_platform_ops.cleanup) {
        ndis_platform_ops.cleanup(dev);
    }
    
    DEBUGP(DL_TRACE, "<==82580_cleanup: Success\n");
    return 0;
}

/**
 * @brief Get 82580 device information
 * @param dev Device handle
 * @param buffer Output buffer
 * @param size Buffer size
 * @return 0 on success, <0 on error
 */
static int get_info(device_t *dev, char *buffer, ULONG size)
{
    const char *info = "Intel 82580 Gigabit Network Connection - Enhanced IEEE 1588";
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
 * @brief Initialize 82580 PTP functionality with enhanced features
 * @param dev Device handle
 * @return 0 on success, <0 on error
 */
static int init_ptp(device_t *dev)
{
    uint32_t tsauxc, tsyncrxctl, tsynctxctl;
    int result;
    
    DEBUGP(DL_TRACE, "==>82580_init_ptp (82580-specific enhanced PTP)\n");
    
    if (dev == NULL) {
        return -1;
    }
    
    // 82580 enhanced PTP initialization
    // Configure TIMINCA for enhanced increment (82580 specific)
    uint32_t timinca = 0x80000006;  // Enhanced 6ns increment for 82580
    result = ndis_platform_ops.mmio_write(dev, E1000_TIMINCA, timinca);
    if (result != 0) {
        DEBUGP(DL_ERROR, "82580 TIMINCA write failed\n");
        return result;
    }
    
    // Enable enhanced PTP auxiliary functions via TSAUXC
    result = ndis_platform_ops.mmio_read(dev, E1000_TSAUXC, &tsauxc);
    if (result == 0) {
        tsauxc |= 0x00000003;  // Enable multiple auxiliary functions for 82580
        result = ndis_platform_ops.mmio_write(dev, E1000_TSAUXC, tsauxc);
        if (result == 0) {
            DEBUGP(DL_INFO, "82580 enhanced TSAUXC configured: 0x%08X\n", tsauxc);
        }
    }
    
    // Enable enhanced Rx timestamping with event detection
    result = ndis_platform_ops.mmio_read(dev, E1000_TSYNCRXCTL, &tsyncrxctl);
    if (result == 0) {
        tsyncrxctl |= E1000_TSYNCRXCTL_ENABLED | E1000_TSYNC_RECEIVE_EVENT;
        result = ndis_platform_ops.mmio_write(dev, E1000_TSYNCRXCTL, tsyncrxctl);
        if (result == 0) {
            DEBUGP(DL_INFO, "82580 enhanced Rx timestamping enabled: 0x%08X\n", tsyncrxctl);
        }
    }
    
    // Enable enhanced Tx timestamping with improved precision
    result = ndis_platform_ops.mmio_read(dev, E1000_TSYNCTXCTL, &tsynctxctl);
    if (result == 0) {
        tsynctxctl |= E1000_TSYNCTXCTL_ENABLED | E1000_TSYNC_TXCTL_MAX_DELAY;
        result = ndis_platform_ops.mmio_write(dev, E1000_TSYNCTXCTL, tsynctxctl);
        if (result == 0) {
            DEBUGP(DL_INFO, "82580 enhanced Tx timestamping enabled: 0x%08X\n", tsynctxctl);
        }
    }
    
    DEBUGP(DL_TRACE, "<==82580_init_ptp: Success\n");
    return 0;
}

/**
 * @brief Set 82580 system time using SYSTIM registers
 * @param dev Device handle
 * @param systime 64-bit time value in nanoseconds
 * @return 0 on success, <0 on error
 */
static int set_systime(device_t *dev, uint64_t systime)
{
    uint32_t ts_low, ts_high;
    int result;
    
    DEBUGP(DL_TRACE, "==>82580_set_systime: 0x%llx\n", systime);
    
    if (dev == NULL) {
        return -1;
    }
    
    // Use current system time if zero
    if (systime == 0) {
        LARGE_INTEGER currentTime;
        KeQuerySystemTime(&currentTime);
        systime = currentTime.QuadPart * 100; // Convert to nanoseconds
        DEBUGP(DL_INFO, "82580 using system time: 0x%llx\n", systime);
    }
    
    // Initialize enhanced PTP if not already done
    init_ptp(dev);
    
    // 82580 requires timestamp shift adjustment
    systime = systime >> IGB_82580_TSYNC_SHIFT;
    
    // Split timestamp
    ts_low = (uint32_t)(systime & 0xFFFFFFFF);
    ts_high = (uint32_t)((systime >> 32) & 0xFFFFFFFF);
    
    // Write SYSTIM registers
    result = ndis_platform_ops.mmio_write(dev, E1000_SYSTIML, ts_low);
    if (result != 0) return result;
    
    result = ndis_platform_ops.mmio_write(dev, E1000_SYSTIMH, ts_high);
    if (result != 0) return result;
    
    DEBUGP(DL_TRACE, "<==82580_set_systime: Success\n");
    return 0;
}

/**
 * @brief Get 82580 system time using SYSTIM registers with shift adjustment
 * @param dev Device handle
 * @param systime Output 64-bit time value
 * @return 0 on success, <0 on error
 */
static int get_systime(device_t *dev, uint64_t *systime)
{
    uint32_t ts_low, ts_high;
    int result;
    
    DEBUGP(DL_TRACE, "==>82580_get_systime\n");
    
    if (dev == NULL || systime == NULL) {
        return -1;
    }
    
    // Read SYSTIM registers
    result = ndis_platform_ops.mmio_read(dev, E1000_SYSTIML, &ts_low);
    if (result != 0) return result;
    
    result = ndis_platform_ops.mmio_read(dev, E1000_SYSTIMH, &ts_high);
    if (result != 0) return result;
    
    *systime = ((uint64_t)ts_high << 32) | ts_low;
    
    // 82580 requires timestamp shift adjustment
    *systime = *systime << IGB_82580_TSYNC_SHIFT;
    
    DEBUGP(DL_TRACE, "<==82580_get_systime: 0x%llx\n", *systime);
    return 0;
}

/**
 * @brief 82580-specific MDIO read with enhanced timing
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
    
    DEBUGP(DL_TRACE, "==>82580_mdio_read: phy=0x%x, reg=0x%x\n", phy_addr, reg_addr);
    
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
        DEBUGP(DL_ERROR, "82580 MDIC write failed\n");
        return result;
    }
    
    // Wait for completion with enhanced timeout for 82580
    for (timeout = 0; timeout < 2000; timeout++) {
        result = ndis_platform_ops.mmio_read(dev, E1000_MDIC, &mdic_value);
        if (result != 0) {
            DEBUGP(DL_ERROR, "82580 MDIC read failed during polling\n");
            return result;
        }
        
        // Check for completion
        if (mdic_value & E1000_MDIC_R_MASK) {
            // Check for error
            if (mdic_value & E1000_MDIC_E_MASK) {
                DEBUGP(DL_ERROR, "82580 MDIO read error\n");
                return -1;
            }
            
            // Extract data
            *value = (uint16_t)(mdic_value & E1000_MDIC_DATA_MASK);
            DEBUGP(DL_TRACE, "<==82580_mdio_read: value=0x%x\n", *value);
            return 0;
        }
        
        // Enhanced timing for 82580
        KeStallExecutionProcessor(5); // 5 microseconds for better performance
    }
    
    DEBUGP(DL_ERROR, "82580 MDIO read timeout\n");
    return -1;
}

/**
 * @brief 82580-specific MDIO write with enhanced timing
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
    
    DEBUGP(DL_TRACE, "==>82580_mdio_write: phy=0x%x, reg=0x%x, value=0x%x\n", phy_addr, reg_addr, value);
    
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
        DEBUGP(DL_ERROR, "82580 MDIC write failed\n");
        return result;
    }
    
    // Wait for completion with enhanced timeout
    for (timeout = 0; timeout < 2000; timeout++) {
        result = ndis_platform_ops.mmio_read(dev, E1000_MDIC, &mdic_value);
        if (result != 0) {
            DEBUGP(DL_ERROR, "82580 MDIC read failed during polling\n");
            return result;
        }
        
        // Check for completion
        if (mdic_value & E1000_MDIC_R_MASK) {
            // Check for error
            if (mdic_value & E1000_MDIC_E_MASK) {
                DEBUGP(DL_ERROR, "82580 MDIO write error\n");
                return -1;
            }
            
            DEBUGP(DL_TRACE, "<==82580_mdio_write: Success\n");
            return 0;
        }
        
        // Enhanced timing for 82580
        KeStallExecutionProcessor(5); // 5 microseconds for better performance
    }
    
    DEBUGP(DL_ERROR, "82580 MDIO write timeout\n");
    return -1;
}

/**
 * @brief 82580 device operations structure using clean generic function names
 */
const intel_device_ops_t e82580_ops = {
    .device_name = "Intel 82580 Gigabit Network Connection - Enhanced IEEE 1588",
    .supported_capabilities = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_MMIO | INTEL_CAP_MDIO,
    
    // Basic operations - clean generic names
    .init = init,
    .cleanup = cleanup,
    .get_info = get_info,
    
    // PTP operations - enhanced PTP support with better precision
    .set_systime = set_systime,
    .get_systime = get_systime,
    .init_ptp = init_ptp,
    
    // TSN operations - 82580 doesn't support advanced TSN
    .setup_tas = NULL,
    .setup_frame_preemption = NULL,
    .setup_ptm = NULL,
    
    // MDIO operations - 82580 has enhanced MDIO support
    .mdio_read = mdio_read,
    .mdio_write = mdio_write
};