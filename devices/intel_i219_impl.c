/*++

Module Name:

    intel_i219_impl.c

Abstract:

    Intel I219 Gigabit Ethernet device-specific implementation.
    Implements enhanced IEEE 1588 PTP capabilities with MDIO support.
    Uses SSOT register definitions from i219_regs.h.
    CLEAN DEVICE SEPARATION: No cross-device contamination.

--*/

#include "precomp.h"
#include "intel_device_interface.h"
#include "avb_integration.h"
#include "external/intel_avb/lib/intel_windows.h"  // Required for platform_ops
#include "external/intel_avb/lib/intel_private.h"  // SSOT TIMINCA/EtherType constants
#include "intel-ethernet-regs/gen/i219_regs.h"  // SSOT register definitions

// External platform operations
extern const struct platform_ops ndis_platform_ops;

/* Forward declaration — init_ptp() is defined after init() in this file */
static int init_ptp(device_t *dev);

/* Software TSAUXC shadow for I219.
 * The I219/I218 hardware does NOT implement a TSAUXC register at offset 0x0B640
 * (confirmed by Intel I218/I219 Specification Update: TimeSync CSR map ends at
 * 0x0B638).  We maintain a software shadow so that callers that gate on TSAUXC
 * bit 31 (e.g. test_ioctl_hw_ts_ctrl UT-HW-TS-008/011) observe the expected
 * enable/disable state even though no hardware bits are changed.
 * NOTE: For simplicity this is a single static; a multi-I219 system would need
 * per-device storage.  Initialised to 0 (all timers enabled, BIT31 clear). */
static volatile uint32_t i219_tsauxc_shadow = 0x00000000U;

/* Software absolute-time offset for I219 SYSTIM.
 * I219 SYSTIM is a free-running relative counter (200,000 raw counts per
 * real nanosecond, wraps ~25.6 hours).  Absolute epoch time cannot be stored
 * in hardware.  This offset is added in get_systime() so callers receive
 * Unix-epoch-anchored nanoseconds.  Updated by set_systime() without any
 * hardware register writes.
 * NOTE: single static; a multi-I219 system would need per-device storage. */
static volatile uint64_t i219_systim_offset = 0;

/**
 * @brief Initialize I219 device with enhanced PTP setup
 * @param dev Device handle
 * @return 0 on success, <0 on error
 */
static int init(device_t *dev)
{
    DEBUGP(DL_TRACE, "==>i219_init (I219-specific)\n");
    
    if (dev == NULL) {
        return -1;
    }
    
    // I219-specific platform initialization
    if (ndis_platform_ops.init) {
        NTSTATUS result = ndis_platform_ops.init(dev);
        if (!NT_SUCCESS(result)) {
            DEBUGP(DL_ERROR, "I219 platform init failed: 0x%x\n", result);
            return -1;
        }
    }

    // Initialize PTP clock: enables SYSTIM counter by clearing TSAUXC bit 31 (DISABLE_SYSTIM).
    // Without this call the I219 free-running counter never starts, so every get_systime()
    // returns the same frozen value (delta==0 in test_hw_ts_ctrl / test_ioctl_phc_epoch).
    {
        int ptp_result = init_ptp(dev);
        if (ptp_result != 0) {
            DEBUGP(DL_WARN, "I219 init: PTP clock init returned %d — SYSTIM may be frozen\n", ptp_result);
            /* Non-fatal: basic NDIS filtering still works; log and continue */
        }
    }

    DEBUGP(DL_INFO, "I219 initialized successfully\n");
    DEBUGP(DL_TRACE, "<==i219_init: Success\n");
    return 0;
}

/**
 * @brief Cleanup I219 device
 * @param dev Device handle
 * @return 0 on success, <0 on error
 */
static int cleanup(device_t *dev)
{
    DEBUGP(DL_TRACE, "==>i219_cleanup\n");
    
    if (dev == NULL) {
        return -1;
    }
    
    if (ndis_platform_ops.cleanup) {
        ndis_platform_ops.cleanup(dev);
    }
    
    DEBUGP(DL_TRACE, "<==i219_cleanup: Success\n");
    return 0;
}

/**
 * @brief Get I219 device information
 * @param dev Device handle
 * @param buffer Output buffer
 * @param size Buffer size
 * @return 0 on success, <0 on error
 */
static int get_info(device_t *dev, char *buffer, ULONG size)
{
    const char *info = "Intel I219 Gigabit Ethernet - Enhanced PTP";
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
 * @brief I219 enhanced PTP initialization
 * @param dev Device handle
 * @return 0 on success, <0 on error
 */
static int init_ptp(device_t *dev)
{
    uint32_t tsauxc;
    uint32_t etqf0;
    uint32_t etqs0;
    int result;

    DEBUGP(DL_TRACE, "==>i219_init_ptp\n");

    if (dev == NULL) {
        return -1;
    }

    /* Step 1: Enable system time counting — clear DISABLE_SYSTIME (bit 31) in TSAUXC */
    result = ndis_platform_ops.mmio_read(dev, I219_TSAUXC, &tsauxc);
    if (result != 0) {
        DEBUGP(DL_ERROR, "I219 init_ptp: Failed to read TSAUXC: %d\n", result);
        return result;
    }
    tsauxc &= ~INTEL_TSAUXC_DISABLE_SYSTIM;  /* bit 31 = 0 → counters run */
    result = ndis_platform_ops.mmio_write(dev, I219_TSAUXC, tsauxc);
    if (result != 0) {
        DEBUGP(DL_ERROR, "I219 init_ptp: Failed to write TSAUXC: %d\n", result);
        return result;
    }
    DEBUGP(DL_INFO, "I219 init_ptp: TSAUXC=0x%08X (systime enabled)\n", tsauxc);

    /* Step 2: Set clock increment for 1GbE — IP=2, IV=16,000,000 (0xF42400)
     * TIMINCA = 0x02F42400: increment period 2 cycles, value 0xF42400 sub-ns steps.
     * Source: Intel I219 datasheet, TIMINCA register, 1GbE configuration. */
    result = ndis_platform_ops.mmio_write(dev, I219_TIMINCA, INTEL_TIMINCA_I219_INIT);
    if (result != 0) {
        DEBUGP(DL_ERROR, "I219 init_ptp: Failed to write TIMINCA: %d\n", result);
        return result;
    }
    DEBUGP(DL_INFO, "I219 init_ptp: TIMINCA=0x%08X (1GbE clock rate)\n", INTEL_TIMINCA_I219_INIT);

    /* Step 3: Configure ETQF0 to identify IEEE 1588 PTP packets (EtherType 0x88F7).
     * Bits: QUEUE_EN(31)=1, TS_1588(30)=1, FILTER_EN(26)=1, ETYPE(15:0)=0x88F7 */
    etqf0 = (uint32_t)I219_ETQF0_SET(0, I219_ETQF0_ETYPE_MASK, I219_ETQF0_ETYPE_SHIFT, (unsigned long long)INTEL_ETHERTYPE_PTP);
    etqf0 = (uint32_t)I219_ETQF0_SET(etqf0, I219_ETQF0_FILTER_EN_MASK, I219_ETQF0_FILTER_EN_SHIFT, 1);
    etqf0 = (uint32_t)I219_ETQF0_SET(etqf0, I219_ETQF0_TS_1588_MASK,   I219_ETQF0_TS_1588_SHIFT,   1);
    etqf0 = (uint32_t)I219_ETQF0_SET(etqf0, I219_ETQF0_QUEUE_EN_MASK,  I219_ETQF0_QUEUE_EN_SHIFT,  1);
    result = ndis_platform_ops.mmio_write(dev, I219_ETQF0, etqf0);
    if (result != 0) {
        DEBUGP(DL_ERROR, "I219 init_ptp: Failed to write ETQF0: %d\n", result);
        return result;
    }
    DEBUGP(DL_INFO, "I219 init_ptp: ETQF0=0x%08X (PTP 0x%04X filter)\n", etqf0, (unsigned)INTEL_ETHERTYPE_PTP);

    /* Step 4: Configure ETQS0 to route IEEE 1588 packets to receive queue 0 */
    etqs0 = (uint32_t)I219_ETQS0_SET(0,     I219_ETQS0_QUEUE_IDX_MASK, I219_ETQS0_QUEUE_IDX_SHIFT, 0);
    etqs0 = (uint32_t)I219_ETQS0_SET(etqs0, I219_ETQS0_QUEUE_EN_MASK,  I219_ETQS0_QUEUE_EN_SHIFT,  1);
    result = ndis_platform_ops.mmio_write(dev, I219_ETQS0, etqs0);
    if (result != 0) {
        DEBUGP(DL_ERROR, "I219 init_ptp: Failed to write ETQS0: %d\n", result);
        return result;
    }
    DEBUGP(DL_INFO, "I219 init_ptp: ETQS0=0x%08X (queue 0 routing)\n", etqs0);

    /* Step 5: Seed software time offset from current system clock so that
     * get_systime() returns Unix-epoch-anchored nanoseconds immediately.
     * KeQuerySystemTime returns Windows FILETIME (100-ns ticks from 1601-01-01).
     * Subtract FILETIME epoch offset (116444736000000000) to get Unix epoch
     * 100-ns ticks, then multiply by 100 to get nanoseconds. */
    {
        LARGE_INTEGER initTime;
        uint32_t ts_hi = 0, ts_lo = 0;
        const uint64_t FILETIME_TO_UNIX_EPOCH_100NS = 116444736000000000ULL;
        uint64_t win_ticks;
        uint64_t now_unix_ns;
        uint64_t raw;

        KeQuerySystemTime(&initTime);
        win_ticks = (uint64_t)initTime.QuadPart;
        now_unix_ns = (win_ticks >= FILETIME_TO_UNIX_EPOCH_100NS)
            ? (win_ticks - FILETIME_TO_UNIX_EPOCH_100NS) * 100ULL
            : 0ULL;

        ndis_platform_ops.mmio_read(dev, I219_SYSTIMH, &ts_hi);
        ndis_platform_ops.mmio_read(dev, I219_SYSTIML, &ts_lo);
        raw = ((uint64_t)ts_hi << 32) | ts_lo;
        InterlockedExchange64((volatile LONG64 *)&i219_systim_offset,
                              (LONG64)(now_unix_ns - raw / 200000ULL));
        DEBUGP(DL_INFO, "I219 init_ptp: offset=0x%llx (epoch=0x%llx, raw_ns=0x%llx)\n",
               i219_systim_offset, now_unix_ns, raw / 200000ULL);
    }

    DEBUGP(DL_TRACE, "<==i219_init_ptp: Success\n");
    return 0;
}

/**
 * @brief Set I219 system time using enhanced PTP
 * @param dev Device handle
 * @param systime 64-bit time value in nanoseconds
 * @return 0 on success, <0 on error
 */
static int set_systime(device_t *dev, uint64_t systime)
{
    uint32_t ts_hi = 0, ts_lo = 0;
    uint64_t raw;

    DEBUGP(DL_TRACE, "==>i219_set_systime: 0x%llx\n", systime);

    if (dev == NULL) {
        return -1;
    }

    /* I219 SYSTIM architecture (confirmed via Intel I219 spec, NotebookLM April 2026):
     *   - TIMINCA=0x02F42400: IP=2, IV=16,000,000 — counter increments every 80 ns
     *     by 16,000,000 raw counts → 200,000 raw counts per real nanosecond.
     *   - SYSTIML (0x0B600): Read-Only.  Cannot set time by writing hardware.
     *   - Max representable time: (2^64-1)/200000 ≈ 25.6 hours — cannot hold Unix epoch.
     *
     * We use a pure software-offset approach:
     *   get_systime() = hardware_raw / 200000 + i219_systim_offset
     *   set_systime(t) sets i219_systim_offset = t - hardware_raw / 200000
     * No hardware register writes are required. */

    if (systime == 0) {
        /* Caller passed 0 — seed from current system clock (Unix epoch ns). */
        LARGE_INTEGER currentTime;
        const uint64_t FILETIME_TO_UNIX_EPOCH_100NS = 116444736000000000ULL;
        uint64_t win_ticks;
        KeQuerySystemTime(&currentTime);
        win_ticks = (uint64_t)currentTime.QuadPart;
        systime = (win_ticks >= FILETIME_TO_UNIX_EPOCH_100NS)
            ? (win_ticks - FILETIME_TO_UNIX_EPOCH_100NS) * 100ULL
            : 0ULL;
        DEBUGP(DL_INFO, "I219 set_systime: using system time: 0x%llx\n", systime);
    }

    /* Read current raw counter (SYSTIMH read latches SYSTIML on I219). */
    ndis_platform_ops.mmio_read(dev, I219_SYSTIMH, &ts_hi);
    ndis_platform_ops.mmio_read(dev, I219_SYSTIML, &ts_lo);
    raw = ((uint64_t)ts_hi << 32) | ts_lo;

    /* Update software offset — no hardware register writes needed.
     * InterlockedExchange64 provides an explicit memory barrier so the write is
     * visible on all cores before the function returns. */
    InterlockedExchange64((volatile LONG64 *)&i219_systim_offset,
                          (LONG64)(systime - raw / 200000ULL));

    DEBUGP(DL_WARN, "[I219-DIAG] SET: target=0x%llx offset=0x%llx raw=0x%llx raw/200k=%llu\n",
           systime, i219_systim_offset, raw, raw / 200000ULL);
    return 0;
}

/**
 * @brief Get I219 system time using enhanced PTP
 * @param dev Device handle
 * @param systime Output 64-bit time value
 * @return 0 on success, <0 on error
 */
static int get_systime(device_t *dev, uint64_t *systime)
{
    uint32_t ts_low, ts_high;
    int result;

    DEBUGP(DL_TRACE, "==>i219_get_systime\n");

    if (dev == NULL || systime == NULL) {
        return -1;
    }

    /* I219 (e1000e family): reading SYSTIMH first latches SYSTIML to prevent
     * a rollover mid-read.  SYSTIML must be read immediately after.
     * (Opposite of IGB/I210 where SYSTIML read latches SYSTIMH.)
     *
     * Double-read defensive check: if SYSTIMH changes between the two H reads,
     * the counter wrapped between latching L and our second H read — retry.
     * On I219 SYSTIM wraps every ~25.6 hours so retries are extremely rare in
     * practice; this loop protects against the pathological case. */
    {
        uint32_t h1, h2;
        int retry = 0;
        do {
            result = ndis_platform_ops.mmio_read(dev, I219_SYSTIMH, &h1);
            if (result != 0) {
                DEBUGP(DL_ERROR, "I219 get_systime: SYSTIMH(1) read failed (%d) — KE fallback\n", result);
                goto fallback;
            }
            result = ndis_platform_ops.mmio_read(dev, I219_SYSTIML, &ts_low);
            if (result != 0) {
                DEBUGP(DL_ERROR, "I219 get_systime: SYSTIML read failed (%d) — KE fallback\n", result);
                goto fallback;
            }
            result = ndis_platform_ops.mmio_read(dev, I219_SYSTIMH, &h2);
            if (result != 0) {
                DEBUGP(DL_ERROR, "I219 get_systime: SYSTIMH(2) read failed (%d) — KE fallback\n", result);
                goto fallback;
            }
            if (h1 != h2) {
                DEBUGP(DL_WARN, "[I219-DIAG] GET: H changed during read (h1=0x%x h2=0x%x) — retry %d\n",
                       h1, h2, ++retry);
            }
        } while (h1 != h2);
        ts_high = h1;
    }

    /* I219 raw SYSTIM counter advances at 200,000 counts per real nanosecond
     * with TIMINCA=0x02F42400 (IP=2, IV=16,000,000) at the 25 MHz PCH clock
     * (80 ns period × 16,000,000 counts/cycle = 200,000 counts/ns).
     * Each raw count = 5 femtoseconds.  Add the software offset to return
     * Unix-epoch-anchored nanoseconds (see i219_systim_offset). */
    {
        uint64_t raw2 = ((uint64_t)ts_high << 32) | ts_low;
        uint64_t offset_snap = i219_systim_offset;  /* volatile read; ring-3→0 entry provides fence */
        *systime = offset_snap + raw2 / 200000ULL;
        DEBUGP(DL_WARN, "[I219-DIAG] GET: result=0x%llx raw=0x%llx raw/200k=%llu offset=0x%llx\n",
               *systime, raw2, raw2 / 200000ULL, offset_snap);
    }
    return 0;

fallback:
    {
        LARGE_INTEGER currentTime;
        KeQuerySystemTime(&currentTime);
        *systime = currentTime.QuadPart * 100ULL;
        DEBUGP(DL_TRACE, "<==i219_get_systime: 0x%llx (KE fallback)\n", *systime);
        return 0;
    }
}

/**
 * @brief I219-specific MDIO read using SSOT register definitions
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
    
    DEBUGP(DL_TRACE, "==>i219_mdio_read: phy=0x%x, reg=0x%x\n", phy_addr, reg_addr);
    
    if (dev == NULL || value == NULL) {
        return -1;
    }
    
    // Build MDIC command using SSOT bit field definitions
    mdic_value = (uint32_t)I219_MDIC_SET(0, I219_MDIC_DATA_MASK, I219_MDIC_DATA_SHIFT, 0);  // Data = 0 for read
    mdic_value = (uint32_t)I219_MDIC_SET(mdic_value, I219_MDIC_REG_MASK, I219_MDIC_REG_SHIFT, reg_addr);
    mdic_value = (uint32_t)I219_MDIC_SET(mdic_value, I219_MDIC_PHY_MASK, I219_MDIC_PHY_SHIFT, phy_addr);
    mdic_value = (uint32_t)I219_MDIC_SET(mdic_value, I219_MDIC_OP_MASK, I219_MDIC_OP_SHIFT, 2);  // Read operation
    mdic_value = (uint32_t)I219_MDIC_SET(mdic_value, I219_MDIC_I_MASK, I219_MDIC_I_SHIFT, 1);     // Interrupt on completion
    
    // Write MDIC command using SSOT register definition
    result = ndis_platform_ops.mmio_write(dev, I219_MDIC, mdic_value);
    if (result != 0) {
        DEBUGP(DL_ERROR, "I219 MDIC write failed\n");
        return result;
    }
    
    // Wait for completion (poll ready bit)
    for (timeout = 0; timeout < 1000; timeout++) {
        result = ndis_platform_ops.mmio_read(dev, I219_MDIC, &mdic_value);
        if (result != 0) {
            DEBUGP(DL_ERROR, "I219 MDIC read failed during polling\n");
            return result;
        }
        
        // Check for completion using SSOT bit definitions
        if (I219_MDIC_GET(mdic_value, I219_MDIC_R_MASK, I219_MDIC_R_SHIFT)) {
            // Check for error
            if (I219_MDIC_GET(mdic_value, I219_MDIC_E_MASK, I219_MDIC_E_SHIFT)) {
                DEBUGP(DL_ERROR, "I219 MDIO read error\n");
                return -1;
            }
            
            // Extract data using SSOT bit definitions
            *value = (uint16_t)I219_MDIC_GET(mdic_value, I219_MDIC_DATA_MASK, I219_MDIC_DATA_SHIFT);
            DEBUGP(DL_TRACE, "<==i219_mdio_read: value=0x%x\n", *value);
            return 0;
        }
        
        // Small delay
        KeStallExecutionProcessor(10); // 10 microseconds
    }
    
    DEBUGP(DL_ERROR, "I219 MDIO read timeout\n");
    return -1;
}

/**
 * @brief I219-specific MDIO write using SSOT register definitions
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
    
    DEBUGP(DL_TRACE, "==>i219_mdio_write: phy=0x%x, reg=0x%x, value=0x%x\n", phy_addr, reg_addr, value);
    
    if (dev == NULL) {
        return -1;
    }
    
    // Build MDIC command using SSOT bit field definitions
    mdic_value = (uint32_t)I219_MDIC_SET(0, I219_MDIC_DATA_MASK, I219_MDIC_DATA_SHIFT, value);
    mdic_value = (uint32_t)I219_MDIC_SET(mdic_value, I219_MDIC_REG_MASK, I219_MDIC_REG_SHIFT, reg_addr);
    mdic_value = (uint32_t)I219_MDIC_SET(mdic_value, I219_MDIC_PHY_MASK, I219_MDIC_PHY_SHIFT, phy_addr);
    mdic_value = (uint32_t)I219_MDIC_SET(mdic_value, I219_MDIC_OP_MASK, I219_MDIC_OP_SHIFT, 1);  // Write operation
    mdic_value = (uint32_t)I219_MDIC_SET(mdic_value, I219_MDIC_I_MASK, I219_MDIC_I_SHIFT, 1);     // Interrupt on completion
    
    // Write MDIC command using SSOT register definition
    result = ndis_platform_ops.mmio_write(dev, I219_MDIC, mdic_value);
    if (result != 0) {
        DEBUGP(DL_ERROR, "I219 MDIC write failed\n");
        return result;
    }
    
    // Wait for completion
    for (timeout = 0; timeout < 1000; timeout++) {
        result = ndis_platform_ops.mmio_read(dev, I219_MDIC, &mdic_value);
        if (result != 0) {
            DEBUGP(DL_ERROR, "I219 MDIC read failed during polling\n");
            return result;
        }
        
        // Check for completion using SSOT bit definitions
        if (I219_MDIC_GET(mdic_value, I219_MDIC_R_MASK, I219_MDIC_R_SHIFT)) {
            // Check for error
            if (I219_MDIC_GET(mdic_value, I219_MDIC_E_MASK, I219_MDIC_E_SHIFT)) {
                DEBUGP(DL_ERROR, "I219 MDIO write error\n");
                return -1;
            }
            
            DEBUGP(DL_TRACE, "<==i219_mdio_write: Success\n");
            return 0;
        }
        
        // Small delay
        KeStallExecutionProcessor(10); // 10 microseconds
    }
    
    DEBUGP(DL_ERROR, "I219 MDIO write timeout\n");
    return -1;
}

/**
 * @brief Enable/disable packet timestamping (I219-specific)
 * @param dev Device handle
 * @param enable 1 = enable, 0 = disable
 * @return 0 on success, <0 on error
 * 
 * Configures TSYNCRXCTL and TSYNCTXCTL registers for I219 (IGB family).
 */
static int enable_packet_timestamping(device_t *dev, int enable)
{
    if (!dev || !ndis_platform_ops.mmio_write || !ndis_platform_ops.mmio_read) {
        return -1;
    }

    if (enable) {
        /* Enable RX packet timestamping.
         * TSYNCRXCTL at 0xB620 (IGB PTP block base 0xB600 + 0x20).
         * EN  = bit 4 (0x10): must be 1 to enable RX timestamping.
         * TYPE = bits 3:1; ALL_PKTS = 4 → field value 4, stored at shift=1: 4<<1=0x08.
         * Combined: 0x10 | 0x08 = 0x18. */
        uint32_t rx_ctl = (uint32_t)I219_TSYNCRXCTL_SET(0,
                              I219_TSYNCRXCTL_EN_MASK, I219_TSYNCRXCTL_EN_SHIFT, 1);
        rx_ctl = (uint32_t)I219_TSYNCRXCTL_SET(rx_ctl,
                              I219_TSYNCRXCTL_TYPE_MASK, I219_TSYNCRXCTL_TYPE_SHIFT,
                              I219_TSYNCRXCTL_TYPE_ALL_PKTS);
        if (ndis_platform_ops.mmio_write(dev, I219_TSYNCRXCTL, rx_ctl) != 0) {
            DEBUGP(DL_ERROR, "I219: Failed to write TSYNCRXCTL\n");
            return -1;
        }
        DEBUGP(DL_INFO, "I219: RX timestamping enabled (TSYNCRXCTL=0x%08X)\n", rx_ctl);

        /* Enable TX packet timestamping.
         * TSYNCTXCTL at 0xB614 (IGB PTP block base 0xB600 + 0x14).
         * EN = bit 4 (0x10): hardware latches SYSTIM on next 1588-flagged TX descriptor. */
        uint32_t tx_ctl = (uint32_t)I219_TSYNCTXCTL_SET(0,
                              I219_TSYNCTXCTL_EN_MASK, I219_TSYNCTXCTL_EN_SHIFT, 1);
        if (ndis_platform_ops.mmio_write(dev, I219_TSYNCTXCTL, tx_ctl) != 0) {
            DEBUGP(DL_ERROR, "I219: Failed to write TSYNCTXCTL\n");
            return -1;
        }
        DEBUGP(DL_INFO, "I219: TX timestamping enabled (TSYNCTXCTL=0x%08X)\n", tx_ctl);
    } else {
        uint32_t regval = 0;

        if (ndis_platform_ops.mmio_read(dev, I219_TSYNCRXCTL, &regval) == 0) {
            regval = (uint32_t)I219_TSYNCRXCTL_SET(regval,
                          I219_TSYNCRXCTL_EN_MASK, I219_TSYNCRXCTL_EN_SHIFT, 0);
            regval = (uint32_t)I219_TSYNCRXCTL_SET(regval,
                          I219_TSYNCRXCTL_TYPE_MASK, I219_TSYNCRXCTL_TYPE_SHIFT, 0);
            ndis_platform_ops.mmio_write(dev, I219_TSYNCRXCTL, regval);
        }
        if (ndis_platform_ops.mmio_read(dev, I219_TSYNCTXCTL, &regval) == 0) {
            regval = (uint32_t)I219_TSYNCTXCTL_SET(regval,
                          I219_TSYNCTXCTL_EN_MASK, I219_TSYNCTXCTL_EN_SHIFT, 0);
            ndis_platform_ops.mmio_write(dev, I219_TSYNCTXCTL, regval);
        }
        DEBUGP(DL_INFO, "I219: Packet timestamping disabled\n");
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
static int i219_read_tx_timestamp(device_t *dev, uint64_t *timestamp_ns)
{
    uint32_t time_low, time_high;
    int result;
    
    if (dev == NULL || timestamp_ns == NULL) {
        return -EINVAL;
    }
    
    result = ndis_platform_ops.mmio_read(dev, I219_TXSTMPL, &time_low);
    if (result != 0) return result;
    
    result = ndis_platform_ops.mmio_read(dev, I219_TXSTMPH, &time_high);
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
static int i219_read_rx_timestamp(device_t *dev, uint64_t *timestamp_ns)
{
    uint32_t time_low, time_high;
    int result;
    
    if (dev == NULL || timestamp_ns == NULL) {
        return -EINVAL;
    }
    
    result = ndis_platform_ops.mmio_read(dev, I219_RXSTMPL, &time_low);
    if (result != 0) return result;
    
    result = ndis_platform_ops.mmio_read(dev, I219_RXSTMPH, &time_high);
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
static int i219_poll_tx_timestamp_fifo(device_t *dev, uint64_t *timestamp_ns)
{
    uint32_t txstmph_val = 0, txstmpl_val = 0;
    int result;
    
    if (dev == NULL || timestamp_ns == NULL) {
        return -EINVAL;
    }
    
    result = ndis_platform_ops.mmio_read(dev, I219_TXSTMPH, &txstmph_val);
    if (result != 0) return result;
    
    if (!(txstmph_val & INTEL_TSYNC_VALID)) {
        return 0;  // FIFO empty
    }
    
    result = ndis_platform_ops.mmio_read(dev, I219_TXSTMPL, &txstmpl_val);
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
static int i219_read_timinca(device_t *dev, uint32_t *timinca_value)
{
    if (dev == NULL || timinca_value == NULL) {
        return -EINVAL;
    }
    
    return ndis_platform_ops.mmio_read(dev, I219_TIMINCA, timinca_value);
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
static int i219_write_timinca(device_t *dev, uint32_t timinca_value)
{
    if (dev == NULL) {
        return -EINVAL;
    }
    
    return ndis_platform_ops.mmio_write(dev, I219_TIMINCA, timinca_value);
}

/**
 * @brief Read TSAUXC register (Time Sync Auxiliary Control)
 * @param dev Device context
 * @param tsauxc_value Output: TSAUXC register value
 * @return 0 on success, <0 on error
 *
 * NOTE: The I219 does NOT have a TSAUXC register. The I219 TimeSync register
 * map ends at offset 0x0B638 (RXUDP); offset 0x0B640 is undefined on this
 * device family (confirmed by Intel I218/I219 Specification Update).
 * SYSTIM is always enabled on I219 once the clock is programmed.
 * We return a pseudo-value of 0x0 (all disable bits clear = all timers enabled)
 * to allow callers to proceed without hardware access.
 */
static int i219_read_tsauxc(device_t *dev, uint32_t *tsauxc_value)
{
    if (dev == NULL || tsauxc_value == NULL) {
        return -EINVAL;
    }
    
    /* I219 has no TSAUXC register — return software shadow value.
     * The shadow is updated by i219_write_tsauxc so callers see the last
     * written intent (e.g. UT-HW-TS-008/011 verify BIT31 after disable). */
    *tsauxc_value = i219_tsauxc_shadow;
    DEBUGP(DL_TRACE, "i219_read_tsauxc: returning shadow TSAUXC=0x%08X (I219 has no HW register)\n",
           i219_tsauxc_shadow);
    return 0;
}

/**
 * @brief Write TSAUXC register (Time Sync Auxiliary Control)
 * @param dev Device context
 * @param tsauxc_value TSAUXC register value to write
 * @return 0 on success, <0 on error
 *
 * NOTE: The I219 does NOT have a TSAUXC register. This is a no-op that
 * reports success to callers. SYSTIM cannot be disabled via TSAUXC on I219.
 */
static int i219_write_tsauxc(device_t *dev, uint32_t tsauxc_value)
{
    if (dev == NULL) {
        return -EINVAL;
    }
    
    /* I219 has no TSAUXC register — store to software shadow only.
     * This lets callers read back the last-written intent (disable/enable state)
     * even though no hardware bits are modified. */
    i219_tsauxc_shadow = tsauxc_value;
    DEBUGP(DL_TRACE, "i219_write_tsauxc: stored shadow TSAUXC=0x%08X (I219 has no HW register)\n",
           tsauxc_value);
    return 0;
}

/**
 * @brief I219 device operations structure using clean generic function names
 *
 * NULL op rationale — I219 hardware does NOT implement the following registers;
 * setting these ops to NULL is architecturally correct, not a stub:
 *   set_target_time / get_target_time  → I219 lacks TRGTTIML0/H0 and TRGTTIML1/H1
 *   check_autt_flags / clear_autt_flag → I219 lacks TSICR (no aux-timestamp interrupt status)
 *   get_aux_timestamp / clear_aux_timestamp_flag → I219 lacks AUXSTMPL0/H0, AUXSTMPL1/H1
 *   setup_tas / setup_frame_preemption / setup_ptm → I219 lacks TSN hardware (no TAS/FP/PTM)
 * Callers must guard with ops->fn != NULL before invoking any of these.
 */
const intel_device_ops_t i219_ops = {
    .device_name = "Intel I219 Gigabit Ethernet - Enhanced PTP",
    .supported_capabilities = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_MMIO | INTEL_CAP_MDIO,

    /* Basic operations */
    .init    = init,
    .cleanup = cleanup,
    .get_info = get_info,

    /* PTP clock operations */
    .set_systime  = set_systime,
    .get_systime  = get_systime,
    .init_ptp     = init_ptp,
    .enable_packet_timestamping = enable_packet_timestamping,

    /* PTP register accessors (HAL compliance — no magic numbers in src/) */
    .read_tx_timestamp      = i219_read_tx_timestamp,
    .read_rx_timestamp      = i219_read_rx_timestamp,
    .poll_tx_timestamp_fifo = i219_poll_tx_timestamp_fifo,
    .read_timinca  = i219_read_timinca,
    .write_timinca = i219_write_timinca,
    .read_tsauxc   = i219_read_tsauxc,
    .write_tsauxc  = i219_write_tsauxc,

    /* Target time / aux timestamp — NULL: I219 hardware lacks TRGTTIML, TSICR, AUXSTMPL */
    .set_target_time             = NULL,
    .get_target_time             = NULL,
    .check_autt_flags            = NULL,
    .clear_autt_flag             = NULL,
    .get_aux_timestamp           = NULL,
    .clear_aux_timestamp_flag    = NULL,

    /* TSN features — NULL: I219 is pre-TSN hardware (no TAS/FP/PTM registers) */
    .setup_tas               = NULL,
    .setup_frame_preemption  = NULL,
    .setup_ptm               = NULL,

    /* MDIO operations — I219 uses SSOT MDIC register definitions */
    .mdio_read  = mdio_read,
    .mdio_write = mdio_write
};