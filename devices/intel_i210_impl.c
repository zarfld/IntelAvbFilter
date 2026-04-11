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

// Forward declarations
static int init_ptp(device_t *dev);

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

    // Initialize PTP clock (required for GET_TIMESTAMP and TX timestamps to work)
    DEBUGP(DL_TRACE, "I210: Initializing PTP clock\n");
    int ptp_result = init_ptp(dev);
    if (ptp_result != 0) {
        DEBUGP(DL_TRACE, "I210: PTP initialization returned: %d\n", ptp_result);
        // Continue - basic functionality may still work
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
    ts_low = (uint32_t)(systime & INTEL_MASK_32BIT);
    ts_high = (uint32_t)((systime >> 32) & INTEL_MASK_32BIT);
    
    // I210-specific SYSTIM register access
    result = ndis_platform_ops.mmio_write(dev, INTEL_REG_SYSTIML, ts_low);   // SYSTIML
    if (result != 0) return result;
    
    result = ndis_platform_ops.mmio_write(dev, INTEL_REG_SYSTIMH, ts_high);  // SYSTIMH
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

    // I210 latch sequence: read SYSTIMR first (latches SYSTIML+SYSTIMH atomically per igb driver),
    // then read SYSTIML (nanoseconds 0-999,999,999), then SYSTIMH (seconds).
    // Reference: external/intel_igb/src/igb_ptp.c igb_ptp_read_i210()
    {
        uint32_t dummy = 0;
        (void)ndis_platform_ops.mmio_read(dev, I210_SYSTIMR, &dummy);  // trigger latch
    }

    result = ndis_platform_ops.mmio_read(dev, I210_SYSTIML, &ts_low);   // nanoseconds
    if (result != 0) return result;

    result = ndis_platform_ops.mmio_read(dev, I210_SYSTIMH, &ts_high);  // seconds
    if (result != 0) return result;

    // I210 SYSTIM: SYSTIMH = seconds, SYSTIML = nanoseconds (0-999,999,999)
    // Convert to full nanoseconds (same as I225/I226 split format)
    *systime = (uint64_t)ts_high * 1000000000ULL + (uint64_t)ts_low;

    DEBUGP(DL_TRACE, "<==i210_get_systime: sec=%u ns=%u -> 0x%llx\n", ts_high, ts_low, *systime);
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
    int result;

    DEBUGP(DL_TRACE, "==>i210_init_ptp\n");

    if (dev == NULL) {
        return -1;
    }

    // Step 1: Enable SYSTIM - write 0x0 to TSAUXC to clear all bits including bit 31 (DIS_ST).
    // Linux igb: E1000_WRITE_REG(hw, E1000_TSAUXC, 0x0) — write zero, no read-modify-write.
    // This must be done BEFORE seeding SYSTIM so the hardware accepts the new time.
    result = ndis_platform_ops.mmio_write(dev, INTEL_REG_TSAUXC, 0);
    if (result != 0) {
        DEBUGP(DL_ERROR, "I210: Failed to clear TSAUXC\n");
        return -1;
    }
    DEBUGP(DL_INFO, "I210: TSAUXC=0x0 (SYSTIM enabled)\n");

    // Step 2: Seed SYSTIM with a known non-zero value.
    // I210: SYSTIML=nanoseconds, SYSTIMH=seconds. Write SYSTIML first, SYSTIMH commits.
    // Linux igb: igb_ptp_write_i210() writes SYSTIML then SYSTIMH.
    if (ndis_platform_ops.mmio_write(dev, I210_SYSTIML, 1) != 0 ||
        ndis_platform_ops.mmio_write(dev, I210_SYSTIMH, 0) != 0) {
        DEBUGP(DL_TRACE, "I210: Failed to seed SYSTIM\n");
        return -1;
    }
    DEBUGP(DL_TRACE, "I210: SYSTIM seeded (sec=0, ns=1)\n");

    // Step 3: Enable TX timestamping
    {
        uint32_t tx_ctl = (uint32_t)I210_TSYNCTXCTL_SET(0, I210_TSYNCTXCTL_EN_MASK,
                                                          I210_TSYNCTXCTL_EN_SHIFT, 1);
        if (ndis_platform_ops.mmio_write(dev, I210_TSYNCTXCTL, tx_ctl) != 0) {
            DEBUGP(DL_ERROR, "I210: Failed to write TSYNCTXCTL\n");
        } else {
            DEBUGP(DL_TRACE, "I210: TX timestamping enabled (TSYNCTXCTL=0x%08X)\n", tx_ctl);
        }
    }

    // Step 4: Enable RX timestamping (all PTP packets)
    {
        uint32_t rx_ctl = (uint32_t)I210_TSYNCRXCTL_SET(0, I210_TSYNCRXCTL_EN_MASK,
                                                          I210_TSYNCRXCTL_EN_SHIFT, 1);
        rx_ctl = (uint32_t)I210_TSYNCRXCTL_SET(rx_ctl, I210_TSYNCRXCTL_TYPE_MASK,
                                                I210_TSYNCRXCTL_TYPE_SHIFT, I210_TSYNCRXCTL_TYPE_ALL);
        ndis_platform_ops.mmio_write(dev, I210_TSYNCRXCTL, rx_ctl);
        DEBUGP(DL_TRACE, "I210: RX timestamping enabled (TSYNCRXCTL=0x%08X)\n", rx_ctl);
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
    if (!dev || !ndis_platform_ops.mmio_write || !ndis_platform_ops.mmio_read) {
        return -1;
    }
    
    if (enable) {
        // Enable RX packet timestamping: EN bit (bit 4) + TYPE_ALL (all PTP packets)
        // SSOT constants from intel-ethernet-regs/gen/i210_regs.h
        uint32_t rx_ctl = (uint32_t)I210_TSYNCRXCTL_SET(0, I210_TSYNCRXCTL_EN_MASK, I210_TSYNCRXCTL_EN_SHIFT, 1);
        rx_ctl = (uint32_t)I210_TSYNCRXCTL_SET(rx_ctl, I210_TSYNCRXCTL_TYPE_MASK, I210_TSYNCRXCTL_TYPE_SHIFT, I210_TSYNCRXCTL_TYPE_ALL);
        if (ndis_platform_ops.mmio_write(dev, I210_TSYNCRXCTL, rx_ctl) != 0) {
            DEBUGP(DL_ERROR, "I210: Failed to write TSYNCRXCTL\n");
            return -1;
        }
        DEBUGP(DL_TRACE, "I210: RX timestamping enabled (TSYNCRXCTL=0x%08X)\n", rx_ctl);
        
        // Enable TX packet timestamping: EN bit (bit 4) only.
        // Note: I210 (IGB family) does NOT require the Foxville RSVD bit 5 that I225/I226 needs.
        uint32_t tx_ctl = (uint32_t)I210_TSYNCTXCTL_SET(0, I210_TSYNCTXCTL_EN_MASK, I210_TSYNCTXCTL_EN_SHIFT, 1);
        if (ndis_platform_ops.mmio_write(dev, I210_TSYNCTXCTL, tx_ctl) != 0) {
            DEBUGP(DL_ERROR, "I210: Failed to write TSYNCTXCTL\n");
            return -1;
        }
        DEBUGP(DL_TRACE, "I210: TX timestamping enabled (TSYNCTXCTL=0x%08X)\n", tx_ctl);
    } else {
        // Disable RX packet timestamping: clear EN and TYPE fields
        uint32_t regval = 0;
        if (ndis_platform_ops.mmio_read(dev, I210_TSYNCRXCTL, &regval) == 0) {
            regval = (uint32_t)I210_TSYNCRXCTL_SET(regval, I210_TSYNCRXCTL_EN_MASK, I210_TSYNCRXCTL_EN_SHIFT, 0);
            regval = (uint32_t)I210_TSYNCRXCTL_SET(regval, I210_TSYNCRXCTL_TYPE_MASK, I210_TSYNCRXCTL_TYPE_SHIFT, 0);
            ndis_platform_ops.mmio_write(dev, I210_TSYNCRXCTL, regval);
        }
        
        // Disable TX packet timestamping: clear EN field
        regval = 0;
        if (ndis_platform_ops.mmio_read(dev, I210_TSYNCTXCTL, &regval) == 0) {
            regval = (uint32_t)I210_TSYNCTXCTL_SET(regval, I210_TSYNCTXCTL_EN_MASK, I210_TSYNCTXCTL_EN_SHIFT, 0);
            ndis_platform_ops.mmio_write(dev, I210_TSYNCTXCTL, regval);
        }
        DEBUGP(DL_TRACE, "I210: Disabled packet timestamping\n");
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
    
    if (!(txstmph_val & INTEL_TSYNC_VALID)) {
        return 0;  // FIFO empty
    }
    
    result = ndis_platform_ops.mmio_read(dev, I210_TXSTMPL, &txstmpl_val);
    if (result != 0) return result;
    
    *timestamp_ns = ((uint64_t)(txstmph_val & INTEL_TSYNC_TS_MASK) << 32) | txstmpl_val;
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
    
    // Target time / auxiliary timestamp operations - NOT SUPPORTED (I210 has no TSICR register)
    .set_target_time = NULL,              // No TSICR/TRGTTIML registers on I210
    .get_target_time = NULL,              // No TSICR/TRGTTIML registers on I210
    .check_autt_flags = NULL,             // No TSAUXC AUTT flags on I210
    .clear_autt_flag = NULL,              // No TSAUXC AUTT flags on I210
    .get_aux_timestamp = NULL,            // No auxiliary timestamp FIFO on I210
    .clear_aux_timestamp_flag = NULL,     // No auxiliary timestamp FIFO on I210
    
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