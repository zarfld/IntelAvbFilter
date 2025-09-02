/*++

Module Name:

    intel_i350_impl.c

Abstract:

    Intel I350 Gigabit Network Connection device-specific implementation.
    Implements IEEE 1588 PTP capabilities with hardware timestamping.
    Based on Intel IGB driver specifications and e1000_regs.h definitions.
    CLEAN DEVICE SEPARATION: No cross-device contamination.

--*/

#include "precomp.h"
#include "intel_device_interface.h"
#include "avb_integration.h"
#include "external/intel_avb/lib/intel_windows.h"  // Required for platform_ops

// External platform operations
extern const struct platform_ops ndis_platform_ops;

// I350 Register Definitions - Based on e1000_regs.h
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

// I350 Control Register Definitions
#define E1000_CTRL      0x00000  /* Device Control - RW */
#define E1000_STATUS    0x00008  /* Device Status - RO */
#define E1000_MDIC      0x00020  /* MDI Control - RW */

// I350 PTP Control Bits
#define E1000_TSYNCRXCTL_ENABLED    0x00000010  /* enable Rx timestamping */
#define E1000_TSYNCTXCTL_ENABLED    0x00000010  /* enable Tx timestamping */
#define E1000_TSYNC_RECEIVE_EVENT   0x00000040  /* packet is sync or delay_req */
#define E1000_TSYNC_TXCTL_MAX_DELAY 0x0000000F  /* max delay for timestamp */

// I350-specific MDIC register bit fields (from e1000_regs.h context)
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
 * @brief Initialize I350 device
 * @param dev Device handle
 * @return 0 on success, <0 on error
 */
static int init(device_t *dev)
{
    DEBUGP(DL_TRACE, "==>i350_init (I350-specific)\n");
    
    if (dev == NULL) {
        return -1;
    }
    
    // I350-specific platform initialization
    if (ndis_platform_ops.init) {
        NTSTATUS result = ndis_platform_ops.init(dev);
        if (!NT_SUCCESS(result)) {
            DEBUGP(DL_ERROR, "I350 platform init failed: 0x%x\n", result);
            return -1;
        }
    }
    
    DEBUGP(DL_INFO, "? I350 initialized successfully\n");
    DEBUGP(DL_TRACE, "<==i350_init: Success\n");
    return 0;
}

/**
 * @brief Cleanup I350 device
 * @param dev Device handle
 * @return 0 on success, <0 on error
 */
static int cleanup(device_t *dev)
{
    DEBUGP(DL_TRACE, "==>i350_cleanup\n");
    
    if (dev == NULL) {
        return -1;
    }
    
    if (ndis_platform_ops.cleanup) {
        ndis_platform_ops.cleanup(dev);
    }
    
    DEBUGP(DL_TRACE, "<==i350_cleanup: Success\n");
    return 0;
}

/**
 * @brief Get I350 device information - CORRECTED: Honest description
 * @param dev Device handle
 * @param buffer Output buffer
 * @param size Buffer size
 * @return 0 on success, <0 on error
 */
static int get_info(device_t *dev, char *buffer, ULONG size)
{
    const char *info = "Intel I350 Gigabit Network Connection - IEEE 1588 PTP (No TSN)";
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
 * @brief Initialize I350 PTP functionality based on Intel IGB driver specifications
 * @param dev Device handle
 * @return 0 on success, <0 on error
 */
static int init_ptp(device_t *dev)
{
    uint32_t tsauxc, tsyncrxctl, tsynctxctl;
    int result;
    
    DEBUGP(DL_TRACE, "==>i350_init_ptp (I350-specific IGB PTP)\n");
    
    if (dev == NULL) {
        return -1;
    }
    
    // I350 PTP initialization based on IGB driver patterns
    // Configure TIMINCA for 8ns increment (I350 typical value)
    uint32_t timinca = 0x80000008;  // 8ns increment for I350
    result = ndis_platform_ops.mmio_write(dev, E1000_TIMINCA, timinca);
    if (result != 0) {
        DEBUGP(DL_ERROR, "I350 TIMINCA write failed\n");
        return result;
    }
    
    // Enable PTP auxiliary functions via TSAUXC
    result = ndis_platform_ops.mmio_read(dev, E1000_TSAUXC, &tsauxc);
    if (result == 0) {
        tsauxc |= 0x00000001;  // Enable auxiliary snapshot
        result = ndis_platform_ops.mmio_write(dev, E1000_TSAUXC, tsauxc);
        if (result == 0) {
            DEBUGP(DL_INFO, "I350 TSAUXC configured: 0x%08X\n", tsauxc);
        }
    }
    
    // Enable Rx timestamping
    result = ndis_platform_ops.mmio_read(dev, E1000_TSYNCRXCTL, &tsyncrxctl);
    if (result == 0) {
        tsyncrxctl |= E1000_TSYNCRXCTL_ENABLED;
        result = ndis_platform_ops.mmio_write(dev, E1000_TSYNCRXCTL, tsyncrxctl);
        if (result == 0) {
            DEBUGP(DL_INFO, "I350 Rx timestamping enabled: 0x%08X\n", tsyncrxctl);
        }
    }
    
    // Enable Tx timestamping  
    result = ndis_platform_ops.mmio_read(dev, E1000_TSYNCTXCTL, &tsynctxctl);
    if (result == 0) {
        tsynctxctl |= E1000_TSYNCTXCTL_ENABLED;
        result = ndis_platform_ops.mmio_write(dev, E1000_TSYNCTXCTL, tsynctxctl);
        if (result == 0) {
            DEBUGP(DL_INFO, "I350 Tx timestamping enabled: 0x%08X\n", tsynctxctl);
        }
    }
    
    DEBUGP(DL_TRACE, "<==i350_init_ptp: Success\n");
    return 0;
}

/**
 * @brief Set I350 system time using SYSTIM registers
 * @param dev Device handle
 * @param systime 64-bit time value in nanoseconds
 * @return 0 on success, <0 on error
 */
static int set_systime(device_t *dev, uint64_t systime)
{
    uint32_t ts_low, ts_high;
    int result;
    
    DEBUGP(DL_TRACE, "==>i350_set_systime: 0x%llx\n", systime);
    
    if (dev == NULL) {
        return -1;
    }
    
    // Use current system time if zero
    if (systime == 0) {
        LARGE_INTEGER currentTime;
        KeQuerySystemTime(&currentTime);
        systime = currentTime.QuadPart * 100; // Convert to nanoseconds
        DEBUGP(DL_INFO, "I350 using system time: 0x%llx\n", systime);
    }
    
    // Initialize PTP if not already done
    init_ptp(dev);
    
    // Split timestamp
    ts_low = (uint32_t)(systime & 0xFFFFFFFF);
    ts_high = (uint32_t)((systime >> 32) & 0xFFFFFFFF);
    
    // Write SYSTIM registers (I350 uses standard e1000 addresses)
    result = ndis_platform_ops.mmio_write(dev, E1000_SYSTIML, ts_low);
    if (result != 0) return result;
    
    result = ndis_platform_ops.mmio_write(dev, E1000_SYSTIMH, ts_high);
    if (result != 0) return result;
    
    DEBUGP(DL_TRACE, "<==i350_set_systime: Success\n");
    return 0;
}

/**
 * @brief Get I350 system time using SYSTIM registers
 * @param dev Device handle
 * @param systime Output 64-bit time value
 * @return 0 on success, <0 on error
 */
static int get_systime(device_t *dev, uint64_t *systime)
{
    uint32_t ts_low, ts_high;
    int result;
    
    DEBUGP(DL_TRACE, "==>i350_get_systime\n");
    
    if (dev == NULL || systime == NULL) {
        return -1;
    }
    
    // Read SYSTIM registers
    result = ndis_platform_ops.mmio_read(dev, E1000_SYSTIML, &ts_low);
    if (result != 0) return result;
    
    result = ndis_platform_ops.mmio_read(dev, E1000_SYSTIMH, &ts_high);
    if (result != 0) return result;
    
    *systime = ((uint64_t)ts_high << 32) | ts_low;
    
    DEBUGP(DL_TRACE, "<==i350_get_systime: 0x%llx\n", *systime);
    return 0;
}

/**
 * @brief I350-specific MDIO read using e1000_regs.h bit field definitions
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
    
    DEBUGP(DL_TRACE, "==>i350_mdio_read: phy=0x%x, reg=0x%x\n", phy_addr, reg_addr);
    
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
        DEBUGP(DL_ERROR, "I350 MDIC write failed\n");
        return result;
    }
    
    // Wait for completion (poll ready bit)
    for (timeout = 0; timeout < 1000; timeout++) {
        result = ndis_platform_ops.mmio_read(dev, E1000_MDIC, &mdic_value);
        if (result != 0) {
            DEBUGP(DL_ERROR, "I350 MDIC read failed during polling\n");
            return result;
        }
        
        // Check for completion
        if (mdic_value & E1000_MDIC_R_MASK) {
            // Check for error
            if (mdic_value & E1000_MDIC_E_MASK) {
                DEBUGP(DL_ERROR, "I350 MDIO read error\n");
                return -1;
            }
            
            // Extract data
            *value = (uint16_t)(mdic_value & E1000_MDIC_DATA_MASK);
            DEBUGP(DL_TRACE, "<==i350_mdio_read: value=0x%x\n", *value);
            return 0;
        }
        
        // Small delay
        KeStallExecutionProcessor(10); // 10 microseconds
    }
    
    DEBUGP(DL_ERROR, "I350 MDIO read timeout\n");
    return -1;
}

/**
 * @brief I350-specific MDIO write using e1000_regs.h bit field definitions
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
    
    DEBUGP(DL_TRACE, "==>i350_mdio_write: phy=0x%x, reg=0x%x, value=0x%x\n", phy_addr, reg_addr, value);
    
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
        DEBUGP(DL_ERROR, "I350 MDIC write failed\n");
        return result;
    }
    
    // Wait for completion
    for (timeout = 0; timeout < 1000; timeout++) {
        result = ndis_platform_ops.mmio_read(dev, E1000_MDIC, &mdic_value);
        if (result != 0) {
            DEBUGP(DL_ERROR, "I350 MDIC read failed during polling\n");
            return result;
        }
        
        // Check for completion
        if (mdic_value & E1000_MDIC_R_MASK) {
            // Check for error
            if (mdic_value & E1000_MDIC_E_MASK) {
                DEBUGP(DL_ERROR, "I350 MDIO write error\n");
                return -1;
            }
            
            DEBUGP(DL_TRACE, "<==i350_mdio_write: Success\n");
            return 0;
        }
        
        // Small delay
        KeStallExecutionProcessor(10); // 10 microseconds
    }
    
    DEBUGP(DL_ERROR, "I350 MDIO write timeout\n");
    return -1;
}

/**
 * @brief I350 device operations structure - CORRECTED: No TSN support  
 * I350 (2012) has standard IEEE 1588 PTP but NO TSN features (TSN standard didn't exist yet)
 */
const intel_device_ops_t i350_ops = {
    .device_name = "Intel I350 Gigabit Network Connection - IEEE 1588 PTP (No TSN)",
    .supported_capabilities = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_MMIO | INTEL_CAP_MDIO, // REALISTIC: No TSN
    
    // Basic operations - clean generic names
    .init = init,
    .cleanup = cleanup,
    .get_info = get_info,
    
    // PTP operations - I350 has good IEEE 1588 support
    .set_systime = set_systime,
    .get_systime = get_systime,
    .init_ptp = init_ptp,
    
    // TSN operations - NOT SUPPORTED (I350 predates TSN standard 2015-2016)
    .setup_tas = NULL,                    // No TSN hardware
    .setup_frame_preemption = NULL,       // No TSN hardware  
    .setup_ptm = NULL,                    // No PCIe PTM hardware
    
    // MDIO operations - I350 has excellent MDIO support
    .mdio_read = mdio_read,
    .mdio_write = mdio_write
};