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
    timinca = INTEL_TIMINCA_I217_INIT; // Basic 8ns increment for I217
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
    if (dev == NULL) {
        return -1;
    }
    
    /* I217 SYSTIM registers are read-only per SSOT (access=ro in i217.yaml).           */
    /* Caller should not invoke set_systime on I217; return NOT_SUPPORTED.               */
    DEBUGP(DL_WARN, "I217 set_systime: SYSTIM is read-only, operation not supported (0x%llx ignored)\n", systime);
    DEBUGP(DL_TRACE, "<==i217_set_systime: STATUS_NOT_SUPPORTED\n");
    return -ENOTSUP;
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
 * @brief Acquire SWSM.SWESMBI semaphore before MDIC access (Section 10.2.5)
 * @param dev Device handle
 * @return 0 on success, -1 on timeout
 */
static int i217_acquire_swsm(device_t *dev)
{
    uint32_t swsm;
    int retries;

    for (retries = 0; retries < 1000; retries++) {
        /* Write 1, then re-read; if still 1 we own the semaphore */
        swsm = (uint32_t)I217_SWSM_SET(0,
                    I217_SWSM_SWESMBI_MASK, I217_SWSM_SWESMBI_SHIFT, 1);
        if (ndis_platform_ops.mmio_write(dev, I217_SWSM, swsm) != 0)
            return -1;
        if (ndis_platform_ops.mmio_read(dev, I217_SWSM, &swsm) != 0)
            return -1;
        if (I217_SWSM_GET(swsm, I217_SWSM_SWESMBI_MASK, I217_SWSM_SWESMBI_SHIFT))
            return 0;  /* acquired */
        KeStallExecutionProcessor(50); /* 50 µs back-off */
    }
    DEBUGP(DL_ERROR, "I217: SWSM semaphore acquire timeout\n");
    return -1;
}

/**
 * @brief Release SWSM.SWESMBI semaphore after MDIC access
 * @param dev Device handle
 */
static void i217_release_swsm(device_t *dev)
{
    uint32_t swsm = (uint32_t)I217_SWSM_SET(0,
                        I217_SWSM_SWESMBI_MASK, I217_SWSM_SWESMBI_SHIFT, 0);
    ndis_platform_ops.mmio_write(dev, I217_SWSM, swsm);
}

/**
 * @brief Core MDIC read (caller must hold SWSM semaphore)
 */
static int i217_mdic_read_locked(device_t *dev, uint16_t phy_addr, uint16_t reg_addr, uint16_t *value)
{
    uint32_t mdic_value;
    int result;
    int timeout;

    mdic_value = (uint32_t)I217_MDIC_SET(0, I217_MDIC_DATA_MASK, I217_MDIC_DATA_SHIFT, 0);
    mdic_value = (uint32_t)I217_MDIC_SET(mdic_value, I217_MDIC_REG_MASK, I217_MDIC_REG_SHIFT, reg_addr);
    mdic_value = (uint32_t)I217_MDIC_SET(mdic_value, I217_MDIC_PHY_MASK, I217_MDIC_PHY_SHIFT, phy_addr);
    mdic_value = (uint32_t)I217_MDIC_SET(mdic_value, I217_MDIC_OP_MASK, I217_MDIC_OP_SHIFT, 2); /* read */
    mdic_value = (uint32_t)I217_MDIC_SET(mdic_value, I217_MDIC_I_MASK, I217_MDIC_I_SHIFT, 1);

    result = ndis_platform_ops.mmio_write(dev, I217_MDIC, mdic_value);
    if (result != 0) return result;

    for (timeout = 0; timeout < 1000; timeout++) {
        result = ndis_platform_ops.mmio_read(dev, I217_MDIC, &mdic_value);
        if (result != 0) return result;
        if (I217_MDIC_GET(mdic_value, I217_MDIC_R_MASK, I217_MDIC_R_SHIFT)) {
            if (I217_MDIC_GET(mdic_value, I217_MDIC_E_MASK, I217_MDIC_E_SHIFT)) {
                DEBUGP(DL_ERROR, "I217 MDIC read error\n");
                return -1;
            }
            *value = (uint16_t)I217_MDIC_GET(mdic_value, I217_MDIC_DATA_MASK, I217_MDIC_DATA_SHIFT);
            return 0;
        }
        KeStallExecutionProcessor(10);
    }
    DEBUGP(DL_ERROR, "I217 MDIC read timeout\n");
    return -1;
}

/**
 * @brief Core MDIC write (caller must hold SWSM semaphore)
 */
static int i217_mdic_write_locked(device_t *dev, uint16_t phy_addr, uint16_t reg_addr, uint16_t value)
{
    uint32_t mdic_value;
    int result;
    int timeout;

    mdic_value = (uint32_t)I217_MDIC_SET(0, I217_MDIC_DATA_MASK, I217_MDIC_DATA_SHIFT, value);
    mdic_value = (uint32_t)I217_MDIC_SET(mdic_value, I217_MDIC_REG_MASK, I217_MDIC_REG_SHIFT, reg_addr);
    mdic_value = (uint32_t)I217_MDIC_SET(mdic_value, I217_MDIC_PHY_MASK, I217_MDIC_PHY_SHIFT, phy_addr);
    mdic_value = (uint32_t)I217_MDIC_SET(mdic_value, I217_MDIC_OP_MASK, I217_MDIC_OP_SHIFT, 1); /* write */
    mdic_value = (uint32_t)I217_MDIC_SET(mdic_value, I217_MDIC_I_MASK, I217_MDIC_I_SHIFT, 1);

    result = ndis_platform_ops.mmio_write(dev, I217_MDIC, mdic_value);
    if (result != 0) return result;

    for (timeout = 0; timeout < 1000; timeout++) {
        result = ndis_platform_ops.mmio_read(dev, I217_MDIC, &mdic_value);
        if (result != 0) return result;
        if (I217_MDIC_GET(mdic_value, I217_MDIC_R_MASK, I217_MDIC_R_SHIFT)) {
            if (I217_MDIC_GET(mdic_value, I217_MDIC_E_MASK, I217_MDIC_E_SHIFT)) {
                DEBUGP(DL_ERROR, "I217 MDIC write error\n");
                return -1;
            }
            return 0;
        }
        KeStallExecutionProcessor(10);
    }
    DEBUGP(DL_ERROR, "I217 MDIC write timeout\n");
    return -1;
}

/*
 * I217 MDIO paging (Section 11.1.2.2.5):
 *   PHY register 31 selects the page (page 800 = host, 801 = ME owned).
 *   To access extended registers: write page to reg 31, write address to
 *   reg I217_MDIO_ADDR_REG (0x11 = Address Set), read/write via
 *   I217_MDIO_DATA_REG (0x12 = Data Cycle).  Semaphore must be held
 *   throughout the full page-select + access sequence.
 */
#define I217_MDIO_PAGE_HOST 800
#define I217_MDIO_PAGE_REG  31
#define I217_MDIO_ADDR_REG  0x11
#define I217_MDIO_DATA_REG  0x12

/**
 * @brief Read from an I217 paged PHY register (acquires SWSM internally)
 * @param dev     Device handle
 * @param phy_addr PHY address
 * @param page    PHY page number (I217_MDIO_PAGE_HOST etc.)
 * @param reg_addr Extended register address
 * @param value   Output value
 * @return 0 on success, <0 on error
 */
__pragma(warning(suppress: 4505))  /* C4505: not referenced yet (no I217 hardware to test) */
static int i217_mdio_read_paged(device_t *dev, uint16_t phy_addr,
                                uint16_t page, uint16_t reg_addr,
                                uint16_t *value)
{
    int result;

    if (i217_acquire_swsm(dev) != 0)
        return -1;

    /* Select page via register 31 */
    result = i217_mdic_write_locked(dev, phy_addr, I217_MDIO_PAGE_REG, page);
    if (result != 0) goto out;

    /* Write target address using Address Set instruction (reg 0x11) */
    result = i217_mdic_write_locked(dev, phy_addr, I217_MDIO_ADDR_REG, reg_addr);
    if (result != 0) goto out;

    /* Read data via Data Cycle register (reg 0x12) */
    result = i217_mdic_read_locked(dev, phy_addr, I217_MDIO_DATA_REG, value);

out:
    i217_release_swsm(dev);
    DEBUGP(DL_TRACE, "i217_mdio_read_paged: page=%d reg=0x%x value=0x%x rc=%d\n",
           page, reg_addr, (result == 0) ? *value : 0, result);
    return result;
}

/**
 * @brief Write to an I217 paged PHY register (acquires SWSM internally)
 * @param dev     Device handle
 * @param phy_addr PHY address
 * @param page    PHY page number
 * @param reg_addr Extended register address
 * @param value   Value to write
 * @return 0 on success, <0 on error
 */
__pragma(warning(suppress: 4505))  /* C4505: not referenced yet (no I217 hardware to test) */
static int i217_mdio_write_paged(device_t *dev, uint16_t phy_addr,
                                 uint16_t page, uint16_t reg_addr,
                                 uint16_t value)
{
    int result;

    if (i217_acquire_swsm(dev) != 0)
        return -1;

    /* Select page */
    result = i217_mdic_write_locked(dev, phy_addr, I217_MDIO_PAGE_REG, page);
    if (result != 0) goto out;

    /* Write address */
    result = i217_mdic_write_locked(dev, phy_addr, I217_MDIO_ADDR_REG, reg_addr);
    if (result != 0) goto out;

    /* Write data */
    result = i217_mdic_write_locked(dev, phy_addr, I217_MDIO_DATA_REG, value);

out:
    i217_release_swsm(dev);
    DEBUGP(DL_TRACE, "i217_mdio_write_paged: page=%d reg=0x%x value=0x%x rc=%d\n",
           page, reg_addr, value, result);
    return result;
}

/**
 * @brief I217-specific MDIO read using SSOT register definitions
 *        Acquires SWSM.SWESMBI semaphore before touching MDIC (Section 10.2.5).
 * @param dev Device handle
 * @param phy_addr PHY address
 * @param reg_addr Register address
 * @param value Output value
 * @return 0 on success, <0 on error
 */
static int mdio_read(device_t *dev, uint16_t phy_addr, uint16_t reg_addr, uint16_t *value)
{
    int result;

    DEBUGP(DL_TRACE, "==>i217_mdio_read: phy=0x%x, reg=0x%x\n", phy_addr, reg_addr);

    if (dev == NULL || value == NULL)
        return -1;

    if (i217_acquire_swsm(dev) != 0)
        return -1;

    result = i217_mdic_read_locked(dev, phy_addr, reg_addr, value);

    i217_release_swsm(dev);

    DEBUGP(DL_TRACE, "<==i217_mdio_read: value=0x%x rc=%d\n", (result == 0) ? *value : 0, result);
    return result;
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
    int result;

    DEBUGP(DL_TRACE, "==>i217_mdio_write: phy=0x%x, reg=0x%x, value=0x%x\n", phy_addr, reg_addr, value);

    if (dev == NULL)
        return -1;

    if (i217_acquire_swsm(dev) != 0)
        return -1;

    result = i217_mdic_write_locked(dev, phy_addr, reg_addr, value);

    i217_release_swsm(dev);

    DEBUGP(DL_TRACE, "<==i217_mdio_write: rc=%d\n", result);
    return result;
}

/**
 * @brief Enable/disable packet timestamping (I217-specific)
 * @param dev Device handle
 * @param enable 1 = enable, 0 = disable
 * @return 0 on success, <0 on error
 * 
 * Configures TSYNCRXCTL and TSYNCTXCTL registers for I217 (IGB family).
 */
static int enable_packet_timestamping(device_t *dev, int enable)
{
    if (!dev || !ndis_platform_ops.mmio_write || !ndis_platform_ops.mmio_read) {
        return -1;
    }

    if (enable) {
        /* Enable RX timestamping: EN=1, TYPE=L4 UDP PTP event messages (2-step capable) */
        uint32_t rx_ctl = (uint32_t)I217_TSYNCRXCTL_SET(0,
                              I217_TSYNCRXCTL_EN_MASK, I217_TSYNCRXCTL_EN_SHIFT, 1);
        rx_ctl = (uint32_t)I217_TSYNCRXCTL_SET(rx_ctl,
                              I217_TSYNCRXCTL_TYPE_MASK, I217_TSYNCRXCTL_TYPE_SHIFT,
                              I217_TSYNCRXCTL_TYPE_L4UDP_PTP_EVT);
        if (ndis_platform_ops.mmio_write(dev, I217_TSYNCRXCTL, rx_ctl) != 0) {
            DEBUGP(DL_ERROR, "I217: Failed to write TSYNCRXCTL\n");
            return -1;
        }
        DEBUGP(DL_INFO, "I217: Enabled RX packet timestamping (TSYNCRXCTL=0x%08X)\n", rx_ctl);

        /* Enable TX timestamping: EN=1 */
        uint32_t tx_ctl = (uint32_t)I217_TSYNCTXCTL_SET(0,
                              I217_TSYNCTXCTL_EN_MASK, I217_TSYNCTXCTL_EN_SHIFT, 1);
        if (ndis_platform_ops.mmio_write(dev, I217_TSYNCTXCTL, tx_ctl) != 0) {
            DEBUGP(DL_ERROR, "I217: Failed to write TSYNCTXCTL\n");
            return -1;
        }
        DEBUGP(DL_INFO, "I217: Enabled TX packet timestamping (TSYNCTXCTL=0x%08X)\n", tx_ctl);
    } else {
        /* Disable: clear EN bit in each control register (read-modify-write) */
        uint32_t regval;

        if (ndis_platform_ops.mmio_read(dev, I217_TSYNCRXCTL, &regval) == 0) {
            regval = (uint32_t)I217_TSYNCRXCTL_SET(regval,
                                  I217_TSYNCRXCTL_EN_MASK, I217_TSYNCRXCTL_EN_SHIFT, 0);
            ndis_platform_ops.mmio_write(dev, I217_TSYNCRXCTL, regval);
        }
        if (ndis_platform_ops.mmio_read(dev, I217_TSYNCTXCTL, &regval) == 0) {
            regval = (uint32_t)I217_TSYNCTXCTL_SET(regval,
                                  I217_TSYNCTXCTL_EN_MASK, I217_TSYNCTXCTL_EN_SHIFT, 0);
            ndis_platform_ops.mmio_write(dev, I217_TSYNCTXCTL, regval);
        }
        DEBUGP(DL_INFO, "I217: Disabled packet timestamping\n");
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
static int i217_read_tx_timestamp(device_t *dev, uint64_t *timestamp_ns)
{
    uint32_t time_low, time_high;
    int result;
    
    if (dev == NULL || timestamp_ns == NULL) {
        return -EINVAL;
    }
    
    result = ndis_platform_ops.mmio_read(dev, I217_TXSTMPL, &time_low);
    if (result != 0) return result;
    
    result = ndis_platform_ops.mmio_read(dev, I217_TXSTMPH, &time_high);
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
static int i217_read_rx_timestamp(device_t *dev, uint64_t *timestamp_ns)
{
    uint32_t time_low, time_high;
    int result;
    
    if (dev == NULL || timestamp_ns == NULL) {
        return -EINVAL;
    }
    
    result = ndis_platform_ops.mmio_read(dev, I217_RXSTMPL, &time_low);
    if (result != 0) return result;
    
    result = ndis_platform_ops.mmio_read(dev, I217_RXSTMPH, &time_high);
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
static int i217_poll_tx_timestamp_fifo(device_t *dev, uint64_t *timestamp_ns)
{
    uint32_t txstmph_val = 0, txstmpl_val = 0;
    int result;
    
    if (dev == NULL || timestamp_ns == NULL) {
        return -EINVAL;
    }
    
    result = ndis_platform_ops.mmio_read(dev, I217_TXSTMPH, &txstmph_val);
    if (result != 0) return result;
    
    if (!(txstmph_val & INTEL_TSYNC_VALID)) {
        return 0;  // FIFO empty
    }
    
    result = ndis_platform_ops.mmio_read(dev, I217_TXSTMPL, &txstmpl_val);
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
static int i217_read_timinca(device_t *dev, uint32_t *timinca_value)
{
    if (dev == NULL || timinca_value == NULL) {
        return -EINVAL;
    }
    
    return ndis_platform_ops.mmio_read(dev, I217_TIMINCA, timinca_value);
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
static int i217_write_timinca(device_t *dev, uint32_t timinca_value)
{
    if (dev == NULL) {
        return -EINVAL;
    }
    
    return ndis_platform_ops.mmio_write(dev, I217_TIMINCA, timinca_value);
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
static int i217_read_tsauxc(device_t *dev, uint32_t *tsauxc_value)
{
    if (dev == NULL || tsauxc_value == NULL) {
        return -EINVAL;
    }
    
    return ndis_platform_ops.mmio_read(dev, I217_TSAUXC, tsauxc_value);
}

/**
 * @brief Write TSAUXC register — no-op for I217
 * @param dev Device context
 * @param tsauxc_value TSAUXC value (ignored)
 * @return 0 (success, operation silently ignored)
 *
 * I217 has no hardware PPS outputs or auxiliary timestamp capture;
 * TSAUXC writes are silently discarded to avoid side-effects.
 * Callers that check enabled features via read_tsauxc will see the
 * unchanged (hardware-reset) value and skip unsupported operations.
 */
static int i217_write_tsauxc(device_t *dev, uint32_t tsauxc_value)
{
    UNREFERENCED_PARAMETER(tsauxc_value);
    if (dev == NULL)
        return -EINVAL;
    DEBUGP(DL_INFO, "I217 write_tsauxc: no-op (no HW PPS/aux on I217)\n");
    return 0;
}

/**
 * @brief I217 device operations structure
 *
 * Capability notes:
 *  - 2-step PTP only: SYSTIM is read-only; set_systime returns -ENOTSUP.
 *  - No hardware PPS / auxiliary capture: write_tsauxc is a no-op.
 *  - No TSICR / TRGTTIM / AUXSTMP: unsupported ops are NULL.
 *  - MDIO paging (page 800/801) supported via mdio_read/write_paged.
 *  - SWSM.SWESMBI semaphore acquired internally by all MDIO operations.
 */
const intel_device_ops_t i217_ops = {
    .device_name = "Intel I217 Gigabit Ethernet - Basic PTP (2-step only)",
    .supported_capabilities = INTEL_CAP_BASIC_1588 | INTEL_CAP_MMIO,

    .init    = init,
    .cleanup = cleanup,
    .get_info = get_info,

    /* set_systime returns -ENOTSUP (SYSTIM is read-only on I217) */
    .set_systime = set_systime,
    .get_systime = get_systime,
    .init_ptp = init_ptp,
    .enable_packet_timestamping = enable_packet_timestamping,

    .read_tx_timestamp        = i217_read_tx_timestamp,
    .read_rx_timestamp        = i217_read_rx_timestamp,
    .poll_tx_timestamp_fifo   = i217_poll_tx_timestamp_fifo,
    .read_timinca             = i217_read_timinca,
    .write_timinca            = i217_write_timinca,
    .read_tsauxc              = i217_read_tsauxc,
    /* write_tsauxc is a no-op: I217 has no HW PPS / aux outputs */
    .write_tsauxc             = i217_write_tsauxc,

    /* TSN not supported on I217 */
    .setup_tas              = NULL,
    .setup_frame_preemption = NULL,
    .setup_ptm              = NULL,

    .mdio_read  = mdio_read,
    .mdio_write = mdio_write
};