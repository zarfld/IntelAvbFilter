/*++

Module Name:

    intel_i210_impl.c

Abstract:

    Intel I210 Gigabit Ethernet device-specific implementation.
    Implements basic PTP/IEEE 1588 functionality. No TSN capabilities.

--*/

#include "precomp.h"
#include "intel_device_interface.h"
#include "avb_integration.h"
#include "external/intel_avb/lib/intel_windows.h"  // Required for platform_ops struct definition

// External platform operations
extern const struct platform_ops ndis_platform_ops;

/**
 * @brief Initialize I210 device
 * @param dev Device handle
 * @return 0 on success, <0 on error
 */
static int init(device_t *dev)
{
    DEBUGP(DL_TRACE, "==>i210_init\n");
    
    if (dev == NULL) {
        return -1;
    }
    
    // I210-specific initialization
    if (ndis_platform_ops.init) {
        NTSTATUS result = ndis_platform_ops.init(dev);
        if (!NT_SUCCESS(result)) {
            DEBUGP(DL_ERROR, "I210 platform init failed: 0x%x\n", result);
            return -1;
        }
    }
    
    DEBUGP(DL_TRACE, "<==i210_init: Success\n");
    return 0;
}

/**
 * @brief Cleanup I210 device
 * @param dev Device handle
 * @return 0 on success, <0 on error
 */
static int cleanup(device_t *dev)
{
    DEBUGP(DL_TRACE, "==>i210_cleanup\n");
    
    if (dev == NULL) {
        return -1;
    }
    
    if (ndis_platform_ops.cleanup) {
        ndis_platform_ops.cleanup(dev);
    }
    
    DEBUGP(DL_TRACE, "<==i210_cleanup: Success\n");
    return 0;
}

/**
 * @brief Get I210 device information
 * @param dev Device handle
 * @param buffer Output buffer
 * @param size Buffer size
 * @return 0 on success, <0 on error
 */
static int get_info(device_t *dev, char *buffer, ULONG size)
{
    const char *info = "Intel I210 Gigabit Ethernet - IEEE 1588 PTP";
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
 * @brief Set I210 system time (SYSTIM registers)
 * I210 may require specific initialization sequence
 * @param dev Device handle
 * @param systime 64-bit time value in nanoseconds
 * @return 0 on success, <0 on error
 */
static int set_systime(device_t *dev, uint64_t systime)
{
    uint32_t ts_low, ts_high;
    int result;
    
    DEBUGP(DL_TRACE, "==>i210_set_systime: 0x%llx\n", systime);
    
    if (dev == NULL) {
        return -1;
    }
    
    // Use current system time if zero
    if (systime == 0) {
        LARGE_INTEGER currentTime;
        KeQuerySystemTime(&currentTime);
        systime = currentTime.QuadPart * 100; // Convert to nanoseconds
        DEBUGP(DL_INFO, "I210 using system time: 0x%llx\n", systime);
    }
    
    // Split timestamp
    ts_low = (uint32_t)(systime & 0xFFFFFFFF);
    ts_high = (uint32_t)((systime >> 32) & 0xFFFFFFFF);
    
    // I210-specific SYSTIM register access
    result = ndis_platform_ops.mmio_write(dev, 0x0B600, ts_low);   // SYSTIML
    if (result != 0) return result;
    
    result = ndis_platform_ops.mmio_write(dev, 0x0B604, ts_high);  // SYSTIMH
    if (result != 0) return result;
    
    DEBUGP(DL_TRACE, "<==i210_set_systime: Success\n");
    return 0;
}

/**
 * @brief Get I210 system time (SYSTIM registers)
 * @param dev Device handle
 * @param systime Output 64-bit time value
 * @return 0 on success, <0 on error
 */
static int get_systime(device_t *dev, uint64_t *systime)
{
    uint32_t ts_low, ts_high;
    int result;
    
    DEBUGP(DL_TRACE, "==>i210_get_systime\n");
    
    if (dev == NULL || systime == NULL) {
        return -1;
    }
    
    // Read SYSTIM registers
    result = ndis_platform_ops.mmio_read(dev, 0x0B600, &ts_low);   // SYSTIML
    if (result != 0) return result;
    
    result = ndis_platform_ops.mmio_read(dev, 0x0B604, &ts_high);  // SYSTIMH
    if (result != 0) return result;
    
    *systime = ((uint64_t)ts_high << 32) | ts_low;
    
    DEBUGP(DL_TRACE, "<==i210_get_systime: 0x%llx\n", *systime);
    return 0;
}

/**
 * @brief Initialize I210 PTP functionality
 * I210 may require specific PHC initialization
 * @param dev Device handle
 * @return 0 on success, <0 on error
 */
static int init_ptp(device_t *dev)
{
    uint32_t tsauxc;
    int result;
    
    DEBUGP(DL_TRACE, "==>i210_init_ptp\n");
    
    if (dev == NULL) {
        return -1;
    }
    
    // I210 PTP initialization - enable PHC
    result = ndis_platform_ops.mmio_read(dev, 0x0B640, &tsauxc);  // TSAUXC
    if (result == 0) {
        // Clear DisableSystime bit (enable PHC)
        tsauxc &= ~0x80000000;
        result = ndis_platform_ops.mmio_write(dev, 0x0B640, tsauxc);
        if (result == 0) {
            DEBUGP(DL_INFO, "I210 PHC enabled via TSAUXC: 0x%08X\n", tsauxc);
        }
    }
    
    DEBUGP(DL_TRACE, "<==i210_init_ptp: Result=%d\n", result);
    return result;
}

/**
 * @brief Enable/disable packet timestamping (I210-specific)
 * @param dev Device handle
 * @param enable 1 = enable, 0 = disable
 * @return 0 on success, <0 on error
 * 
 * Configures TSYNCRXCTL and TSYNCTXCTL registers for I210.
 * Reference: external/intel_igb/src/igb_ptp.c lines 703-807
 */
static int enable_packet_timestamping(device_t *dev, int enable)
{
    // I210 PTP registers (IGB family)
    const uint32_t TSYNCRXCTL = 0x0B344;
    const uint32_t TSYNCTXCTL = 0x0B348;
    const uint32_t RXTT_ENABLE = 0x80000000;  // Bit 31
    const uint32_t TXTT_ENABLE = 0x80000000;  // Bit 31
    const uint32_t TYPE_ALL = 0x0E000000;     // Bits 27-25
    
    if (!dev || !ndis_platform_ops.mmio_write || !ndis_platform_ops.mmio_read) {
        return -1;
    }
    
    if (enable) {
        // Enable RX packet timestamping
        uint32_t rx_ctl = RXTT_ENABLE | TYPE_ALL;
        if (ndis_platform_ops.mmio_write(dev, TSYNCRXCTL, rx_ctl) != 0) {
            DEBUGP(DL_ERROR, "I210: Failed to write TSYNCRXCTL\n");
            return -1;
        }
        DEBUGP(DL_INFO, "I210: Enabled RX packet timestamping (TSYNCRXCTL=0x%08X)\n", rx_ctl);
        
        // Enable TX packet timestamping
        uint32_t tx_ctl = TXTT_ENABLE;
        if (ndis_platform_ops.mmio_write(dev, TSYNCTXCTL, tx_ctl) != 0) {
            DEBUGP(DL_ERROR, "I210: Failed to write TSYNCTXCTL\n");
            return -1;
        }
        DEBUGP(DL_INFO, "I210: Enabled TX packet timestamping (TSYNCTXCTL=0x%08X)\n", tx_ctl);
    } else {
        // Disable packet timestamping
        uint32_t regval = 0;
        const uint32_t TYPE_MASK = 0x0E000000;
        
        if (ndis_platform_ops.mmio_read(dev, TSYNCRXCTL, &regval) == 0) {
            regval &= ~(RXTT_ENABLE | TYPE_MASK);
            ndis_platform_ops.mmio_write(dev, TSYNCRXCTL, regval);
        }
        
        if (ndis_platform_ops.mmio_read(dev, TSYNCTXCTL, &regval) == 0) {
            regval &= ~TXTT_ENABLE;
            ndis_platform_ops.mmio_write(dev, TSYNCTXCTL, regval);
        }
        DEBUGP(DL_INFO, "I210: Disabled packet timestamping\n");
    }
    
    return 0;
}

/**
 * @brief Read TX timestamp registers (TXSTMPL/H)
 * @param dev Device context
 * @param timestamp_ns Output: 64-bit timestamp in nanoseconds
 * @return 0 on success, <0 on error
 * 
 * Implements: HAL compliance - eliminates magic numbers in src/
 * Replaces: Direct register access at 0x0B618/0x0B61C
 */
static int i210_read_tx_timestamp(device_t *dev, uint64_t *timestamp_ns)
{
    uint32_t time_low, time_high;
    int result;
    
    if (dev == NULL || timestamp_ns == NULL) {
        return -EINVAL;
    }
    
    result = ndis_platform_ops.mmio_read(dev, I210_TXSTMPL, &time_low);
    if (result != 0) return result;
    
    result = ndis_platform_ops.mmio_read(dev, I210_TXSTMPH, &time_high);
    if (result != 0) return result;
    
    *timestamp_ns = ((uint64_t)time_high << 32) | time_low;
    return 0;
}

/**
 * @brief Read RX timestamp registers (RXSTMPL/H)
 * @param dev Device context
 * @param timestamp_ns Output: 64-bit timestamp in nanoseconds
 * @return 0 on success, <0 on error
 * 
 * Implements: HAL compliance - eliminates magic numbers in src/
 * Replaces: Direct register access at 0x0B624/0x0B628
 */
static int i210_read_rx_timestamp(device_t *dev, uint64_t *timestamp_ns)
{
    uint32_t time_low, time_high;
    int result;
    
    if (dev == NULL || timestamp_ns == NULL) {
        return -EINVAL;
    }
    
    result = ndis_platform_ops.mmio_read(dev, I210_RXSTMPL, &time_low);
    if (result != 0) return result;
    
    result = ndis_platform_ops.mmio_read(dev, I210_RXSTMPH, &time_high);
    if (result != 0) return result;
    
    *timestamp_ns = ((uint64_t)time_high << 32) | time_low;
    return 0;
}

/**
 * @brief Poll TX timestamp FIFO for next entry
 * @param dev Device context
 * @param timestamp_ns Output: 64-bit timestamp in nanoseconds (if return==1)
 * @return 1 if valid timestamp retrieved, 0 if FIFO empty, <0 on error
 * 
 * Implements: HAL compliance - encapsulates device-specific FIFO polling
 * Note: Reads TXSTMPH first (check valid bit), then TXSTMPL (advances FIFO)
 */
static int i210_poll_tx_timestamp_fifo(device_t *dev, uint64_t *timestamp_ns)
{
    uint32_t txstmph_val = 0, txstmpl_val = 0;
    int result;
    
    if (dev == NULL || timestamp_ns == NULL) {
        return -EINVAL;
    }
    
    result = ndis_platform_ops.mmio_read(dev, I210_TXSTMPH, &txstmph_val);
    if (result != 0) return result;
    
    if (!(txstmph_val & 0x80000000)) {
        return 0;  // FIFO empty
    }
    
    result = ndis_platform_ops.mmio_read(dev, I210_TXSTMPL, &txstmpl_val);
    if (result != 0) return result;
    
    *timestamp_ns = ((uint64_t)(txstmph_val & 0x7FFFFFFF) << 32) | txstmpl_val;
    return 1;  // Valid timestamp retrieved
}

/**
 * @brief Read TIMINCA register (clock increment configuration)
 * @param dev Device context
 * @param timinca_value Output: TIMINCA register value
 * @return 0 on success, <0 on error
 * 
 * Implements: HAL compliance - eliminates magic numbers in src/
 * Replaces: Direct register access at 0x0B608
 */
static int i210_read_timinca(device_t *dev, uint32_t *timinca_value)
{
    if (dev == NULL || timinca_value == NULL) {
        return -EINVAL;
    }
    
    return ndis_platform_ops.mmio_read(dev, I210_TIMINCA, timinca_value);
}

/**
 * @brief Write TIMINCA register (clock increment configuration)
 * @param dev Device context
 * @param timinca_value TIMINCA register value to write
 * @return 0 on success, <0 on error
 * 
 * Implements: HAL compliance - eliminates magic numbers in src/
 * Replaces: Direct register access at 0x0B608
 */
static int i210_write_timinca(device_t *dev, uint32_t timinca_value)
{
    if (dev == NULL) {
        return -EINVAL;
    }
    
    return ndis_platform_ops.mmio_write(dev, I210_TIMINCA, timinca_value);
}

/**
 * @brief Read TSAUXC register (Time Sync Auxiliary Control)
 * @param dev Device context
 * @param tsauxc_value Output: TSAUXC register value
 * @return 0 on success, <0 on error
 * 
 * Implements: HAL compliance - eliminates magic numbers in src/
 * Replaces: Direct register access at 0x0B640
 */
static int i210_read_tsauxc(device_t *dev, uint32_t *tsauxc_value)
{
    if (dev == NULL || tsauxc_value == NULL) {
        return -EINVAL;
    }
    
    return ndis_platform_ops.mmio_read(dev, I210_TSAUXC, tsauxc_value);
}

/**
 * @brief Write TSAUXC register (Time Sync Auxiliary Control)
 * @param dev Device context
 * @param tsauxc_value TSAUXC register value to write
 * @return 0 on success, <0 on error
 * 
 * Implements: HAL compliance - eliminates magic numbers in src/
 * Replaces: Direct register access at 0x0B640 and hardcoded bit masks
 */
static int i210_write_tsauxc(device_t *dev, uint32_t tsauxc_value)
{
    if (dev == NULL) {
        return -EINVAL;
    }
    
    return ndis_platform_ops.mmio_write(dev, I210_TSAUXC, tsauxc_value);
}

/**
 * @brief I210 device operations structure - CORRECTED: No TSN support
 * I210 (2013) has excellent IEEE 1588 PTP but NO TSN features (TSN standard finalized 2015-2016)
 */
const intel_device_ops_t i210_ops = {
    .device_name = "Intel I210 Gigabit Ethernet - IEEE 1588 PTP (No TSN)",
    .supported_capabilities = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_MMIO, // REALISTIC: No TSN
    
    // Basic operations
    .init = init,
    .cleanup = cleanup,
    .get_info = get_info,
    
    // PTP operations - I210 has excellent IEEE 1588 support
    .set_systime = set_systime,
    .get_systime = get_systime,
    .init_ptp = init_ptp,
    .enable_packet_timestamping = enable_packet_timestamping,
    
    // PTP register access operations (HAL compliance - no magic numbers in src/)
    .read_tx_timestamp = i210_read_tx_timestamp,
    .read_rx_timestamp = i210_read_rx_timestamp,
    .poll_tx_timestamp_fifo = i210_poll_tx_timestamp_fifo,
    .read_timinca = i210_read_timinca,
    .write_timinca = i210_write_timinca,
    .read_tsauxc = i210_read_tsauxc,
    .write_tsauxc = i210_write_tsauxc,
    
    // TSN operations - NOT SUPPORTED (I210 predates TSN hardware implementation)
    .setup_tas = NULL,                    // No TSN hardware
    .setup_frame_preemption = NULL,       // No TSN hardware
    .setup_ptm = NULL,                    // No PCIe PTM hardware
    
    // Register access - I210 supports both generic and device-specific
    .read_register = NULL,  // Use generic platform implementation
    .write_register = NULL, // Use generic platform implementation
    
    // MDIO operations - Not typically used on I210 (integrated PHY)
    .mdio_read = NULL,      // Integrated PHY, use generic if needed
    .mdio_write = NULL,     // Integrated PHY, use generic if needed
    
    // Advanced features - None on I210
    .enable_advanced_features = NULL,
    .validate_configuration = NULL
};