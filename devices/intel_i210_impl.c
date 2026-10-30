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
#include "include/avb_ioctl.h"     // AVB_HWREG_* ids for hil_register_offset (explicit; host builds do not get it via avb_integration.h)
#include "external/intel_avb/lib/intel_windows.h"  // Required for platform_ops struct definition
#include "external/intel_avb/lib/intel_private.h"  // Required for struct intel_private definition

// External platform operations
extern const struct platform_ops ndis_platform_ops;

// Forward declarations
static int init_ptp(device_t *dev);

/* ---------------------------------------------------------------------------------------------------
 * TIMINCA (clock increment) encoding.  Private to the I210 device implementation: the generic layer only
 * sees the logical request (increment_ns + increment_frac / 2^32 ns) through the two frequency ops.
 *
 * Intel I210 Datasheet 333016 v3.7, sections 7.8.3.2 and 8.15.11 (TIMINCA, 0xB608):
 *     SYSTIM advances every 8 ns clock cycle (125 MHz) by  INC_TIME = 8 ns +/- Incvalue * 2^-32 ns
 *     Incvalue = bits 30:0, ISGN = bit 31 (1b = subtract), reset value 0 => exactly 8 ns: the NOMINAL value.
 * The register layout (I210_TIMINCA, I210_TIMINCA_INCVALUE_*, I210_TIMINCA_ISGN_*) comes from the register
 * SSOT (intel-ethernet-regs, #21 / ADR #124); nothing about the layout is redefined here.  Only the
 * 8 ns cycle time and the policy range below have no SSOT definition.
 *
 * Physical representability (|L - 8 ns| <= (2^31 - 1) * 2^-32 ns, about +/-6.25 percent, i.e. increment_ns
 * 7 or 8) is a separate check from the public API range policy.  The earlier (ns << 24) | frac24 model
 * programmed 0x08000000 as "8 ns", which by the datasheet is 8.03125 ns per cycle (+3906.25 ppm).
 * ------------------------------------------------------------------------------------------------- */
#define I210_SYSTIM_CYCLE_NS        8u        /* datasheet 7.8.3.2: one 125 MHz cycle */
#define I210_SYSTIM_FRAC_BITS       32u       /* Incvalue resolution: 2^-32 ns */

/* PROVISIONAL public API range for ADJUST_FREQUENCY on I210: +/-100,000 ppb from #149 (which lists #3 as
 * superseded; #3 states +/-999,999 ppb, the conflict is recorded in #326).  An API policy, NOT the hardware
 * limit - do not describe it as one. */
#define I210_FREQ_API_MAX_ABS_PPB   100000u

/* Raw TIMINCA that gives exactly 8 ns per cycle (the reset value). */
static uint32_t i210_timinca_nominal(void)
{
    return 0u;
}

/* Logical increment -> raw TIMINCA.  0 on success; -EINVAL (and *raw untouched) when not representable. */
static int i210_timinca_encode(uint32_t increment_ns, uint32_t increment_frac, uint32_t *raw)
{
    const uint64_t nominal = (uint64_t)I210_SYSTIM_CYCLE_NS << I210_SYSTIM_FRAC_BITS;
    const uint64_t total   = ((uint64_t)increment_ns << I210_SYSTIM_FRAC_BITS) | (uint64_t)increment_frac;
    uint64_t magnitude;
    uint32_t sign = 0u;

    if (total >= nominal) {
        magnitude = total - nominal;
    } else {
        magnitude = nominal - total;
        sign = (uint32_t)I210_TIMINCA_ISGN_MASK;
    }
    if (magnitude > (uint64_t)I210_TIMINCA_INCVALUE_MASK) {
        return -EINVAL;
    }
    *raw = sign | (uint32_t)((magnitude << I210_TIMINCA_INCVALUE_SHIFT) & I210_TIMINCA_INCVALUE_MASK);
    return 0;
}

/* Largest |Incvalue| (2^-32 ns units) that a +/-ppb adjustment may use: round(ppb * 8 ns * 2^32 / 1e9). */
static uint32_t i210_timinca_max_units_for_ppb(uint32_t ppb)
{
    const uint64_t period_units = (uint64_t)I210_SYSTIM_CYCLE_NS << I210_SYSTIM_FRAC_BITS;
    return (uint32_t)(((uint64_t)ppb * period_units + 500000000ULL) / 1000000000ULL);
}

/* Non-zero when |Incvalue| of `raw` lies within the provisional +/-I210_FREQ_API_MAX_ABS_PPB policy range. */
static int i210_timinca_within_api_range(uint32_t raw)
{
    const uint32_t magnitude = (uint32_t)(((uint64_t)raw & I210_TIMINCA_INCVALUE_MASK) >> I210_TIMINCA_INCVALUE_SHIFT);
    return magnitude <= i210_timinca_max_units_for_ppb(I210_FREQ_API_MAX_ABS_PPB);
}

/**
 * @brief Initialize I210 device
 * @param dev Device handle
 * @return 0 on success, <0 on error (the PTP initialization failed; the device is NOT initialized)
 *
 * init_ptp() is the single owner of the I210 TSAUXC / TIMINCA / SYSTIM / timestamp-control programming.
 * The generic platform init (AvbPlatformInit via ndis_platform_ops.init) is intentionally NOT called here
 * (issue #343): for the I210 it wrote a second, contradictory TIMINCA (INTEL_TIMINCA_DEFAULT, a 25 MHz
 * clock model) before init_ptp() programmed the datasheet-correct value.  Its responsibilities are retained
 * as follows:
 *   - enable SYSTIM (TSAUXC bit 31)  -> init_ptp(): TSAUXC is written to 0 when SYSTIM is disabled
 *   - program TIMINCA                -> init_ptp(): nominal value, see i210_timinca_nominal()
 *   - SYSTIM-advancing check         -> dropped: it was informational only (always returned success) and
 *                                       ran before init_ptp() re-seeded SYSTIM.  PHC health verification is
 *                                       separate lifecycle work and is NOT claimed by this function.
 * Failures of mandatory steps are returned to the caller; they are no longer swallowed.
 */
static int init(device_t *dev)
{
    int rc;

    DEBUGP(DL_TRACE, "==>i210_init\n");

    if (dev == NULL) {
        return -1;
    }

    rc = init_ptp(dev);
    if (rc != 0) {
        DEBUGP(DL_ERROR, "I210: PTP initialization failed: %d\n", rc);
        return rc;
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
    
    /* I210 (IGB family) SYSTIM format: SYSTIMH = seconds, SYSTIML = nanoseconds (0..999999999).
     * This matches get_systime() which reconstructs via: ts_high * 1e9 + ts_low.
     * Writing raw 32-bit splits of the 64-bit ns value would produce a massive
     * readback mismatch (−1.3×10¹⁸ ns) because the register fields have different units. */
    ts_high = (uint32_t)(systime / 1000000000ULL);  /* seconds */
    ts_low  = (uint32_t)(systime % 1000000000ULL);  /* nanoseconds (0..999,999,999) */

    // I210-specific SYSTIM register access
    result = ndis_platform_ops.mmio_write(dev, INTEL_REG_SYSTIML, ts_low);   // SYSTIML = ns
    if (result != 0) return result;

    result = ndis_platform_ops.mmio_write(dev, INTEL_REG_SYSTIMH, ts_high);  // SYSTIMH = sec
    if (result != 0) return result;

    DEBUGP(DL_TRACE, "<==i210_set_systime: sec=%u ns=%u\n", ts_high, ts_low);
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
 * @brief Initialize I210 PTP functionality (idempotent; single owner of the I210 PTP programming)
 * @param dev Device handle
 * @return 0 on success, <0 on error (NOT initialized; the caller must not treat the device as ready)
 *
 * The I210 configuration is first READ BACK, and only what is missing is written:
 *   - full initialization  when SYSTIM is disabled (TSAUXC bit 31) or BOTH timestamp controls are off, i.e. a
 *     fresh or power-cycled device: enable SYSTIM, program the nominal TIMINCA, seed SYSTIM, enable TX/RX.
 *   - minimal repair       when the configuration is in place but TIMINCA is outside the provisional API
 *     policy range (e.g. the legacy 0x08000000, +3906 ppm) or one timestamp control is off: fix only that.
 *     SYSTIM is never re-seeded here.
 *   - no writes at all     when everything is already configured.  A repeated init (bring-up, restart,
 *     OPEN_ADAPTER, explicit INIT_DEVICE) therefore does not reset a running clock, does not clear target-time
 *     enables in TSAUXC, and keeps an in-policy frequency adjustment.
 * TIMINCA == 0 is the I210 NOMINAL value (datasheet 8.15.11), never evidence of "uninitialized".
 * If the configuration cannot be read, nothing is written and the call fails.  Enabling a timestamp control
 * is verified by readback.  This is configuration evidence only: it does NOT prove the clock advances at
 * the right rate (PHC health is separate lifecycle work).
 */
static int init_ptp(device_t *dev)
{
    const uint32_t nominal = i210_timinca_nominal();
    const uint32_t want_tx = (uint32_t)I210_TSYNCTXCTL_SET(0, I210_TSYNCTXCTL_EN_MASK,
                                                           I210_TSYNCTXCTL_EN_SHIFT, 1);
    const uint32_t want_rx = (uint32_t)I210_TSYNCRXCTL_SET(
                                 I210_TSYNCRXCTL_SET(0, I210_TSYNCRXCTL_EN_MASK, I210_TSYNCRXCTL_EN_SHIFT, 1),
                                 I210_TSYNCRXCTL_TYPE_MASK, I210_TSYNCRXCTL_TYPE_SHIFT, I210_TSYNCRXCTL_TYPE_ALL);
    uint32_t tsauxc = 0, timinca = 0, tx_ctl = 0, rx_ctl = 0;
    int systim_off, tx_on, rx_on, full;

    DEBUGP(DL_TRACE, "==>i210_init_ptp\n");

    if (dev == NULL) {
        return -1;
    }

    /* Observe first. Nothing is written unless every configuration register could be read. */
    if (ndis_platform_ops.mmio_read(dev, I210_TSAUXC, &tsauxc) != 0 ||
        ndis_platform_ops.mmio_read(dev, I210_TIMINCA, &timinca) != 0 ||
        ndis_platform_ops.mmio_read(dev, I210_TSYNCTXCTL, &tx_ctl) != 0 ||
        ndis_platform_ops.mmio_read(dev, I210_TSYNCRXCTL, &rx_ctl) != 0) {
        DEBUGP(DL_ERROR, "I210: cannot read the PTP configuration; not initializing\n");
        return -EIO;
    }
    DEBUGP(DL_TRACE, "I210: TSAUXC=0x%08X TIMINCA=0x%08X TSYNCTXCTL=0x%08X TSYNCRXCTL=0x%08X\n",
           tsauxc, timinca, tx_ctl, rx_ctl);

    systim_off = (tsauxc & INTEL_TSAUXC_DISABLE_SYSTIM) != 0;
    tx_on = (tx_ctl & want_tx) == want_tx;
    rx_on = (rx_ctl & (uint32_t)(I210_TSYNCRXCTL_EN_MASK | I210_TSYNCRXCTL_TYPE_MASK)) == want_rx;
    full  = systim_off || (!tx_on && !rx_on);

    if (full) {
        /* Step 1: Enable SYSTIM - write 0x0 to TSAUXC to clear all bits including bit 31 (DIS_ST).
         * Linux igb: E1000_WRITE_REG(hw, E1000_TSAUXC, 0x0) - write zero, no read-modify-write.
         * This must be done BEFORE seeding SYSTIM so the hardware accepts the new time. */
        if (ndis_platform_ops.mmio_write(dev, I210_TSAUXC, 0) != 0) {
            DEBUGP(DL_ERROR, "I210: Failed to clear TSAUXC\n");
            return -EIO;
        }
        DEBUGP(DL_INFO, "I210: TSAUXC=0x0 (SYSTIM enabled)\n");
    }

    /* Step 2: TIMINCA to the nominal 8 ns per 125 MHz cycle.
     * Datasheet 333016 v3.7 section 8.15.11: INC_TIME = 8 ns +/- Incvalue * 2^-32 ns, reset value 0,
     * so the NOMINAL encoding is Incvalue = 0, ISGN = 0 (see i210_timinca_nominal above).
     * The previous 0x08000000 ("8 << 24") is 8 ns + 2^27 * 2^-32 ns = 8.03125 ns, i.e. +3906.25 ppm.
     * This is the ONLY place the I210 init chain programs TIMINCA (TEST-TIMINCA-ARCH-006).
     * Full init: always the nominal value.  Otherwise only a value outside the policy range is repaired, so an
     * in-policy ADJUST_FREQUENCY result survives a repeated init. */
    if ((full && timinca != nominal) || (!full && !i210_timinca_within_api_range(timinca))) {
        if (ndis_platform_ops.mmio_write(dev, I210_TIMINCA, nominal) != 0) {
            DEBUGP(DL_ERROR, "I210: Failed to write TIMINCA\n");
            return -EIO;
        }
        DEBUGP(DL_INFO, "I210: TIMINCA set to nominal 0x%08X (8 ns/cycle)\n", nominal);
    }

    /* Step 3: Seed SYSTIM with a known non-zero value - full initialization only, never on a configured device.
     * I210: SYSTIML=nanoseconds, SYSTIMH=seconds. Write SYSTIML first, SYSTIMH commits.
     * Linux igb: igb_ptp_write_i210() writes SYSTIML then SYSTIMH. */
    if (full) {
        if (ndis_platform_ops.mmio_write(dev, I210_SYSTIML, 1) != 0 ||
            ndis_platform_ops.mmio_write(dev, I210_SYSTIMH, 0) != 0) {
            DEBUGP(DL_ERROR, "I210: Failed to seed SYSTIM\n");
            return -EIO;
        }
        DEBUGP(DL_TRACE, "I210: SYSTIM seeded (sec=0, ns=1)\n");
    }

    /* Step 4: Enable TX timestamping, verified by readback. */
    if (!tx_on) {
        uint32_t check = 0;
        if (ndis_platform_ops.mmio_write(dev, I210_TSYNCTXCTL, want_tx) != 0 ||
            ndis_platform_ops.mmio_read(dev, I210_TSYNCTXCTL, &check) != 0 || (check & want_tx) != want_tx) {
            DEBUGP(DL_ERROR, "I210: TX timestamping could not be enabled\n");
            return -EIO;
        }
        DEBUGP(DL_TRACE, "I210: TX timestamping enabled (TSYNCTXCTL=0x%08X)\n", want_tx);
    }

    /* Step 5: Enable RX timestamping (all PTP packets), verified by readback. */
    if (!rx_on) {
        uint32_t check = 0;
        if (ndis_platform_ops.mmio_write(dev, I210_TSYNCRXCTL, want_rx) != 0 ||
            ndis_platform_ops.mmio_read(dev, I210_TSYNCRXCTL, &check) != 0 ||
            (check & (uint32_t)(I210_TSYNCRXCTL_EN_MASK | I210_TSYNCRXCTL_TYPE_MASK)) != want_rx) {
            DEBUGP(DL_ERROR, "I210: RX timestamping could not be enabled\n");
            return -EIO;
        }
        DEBUGP(DL_TRACE, "I210: RX timestamping enabled (TSYNCRXCTL=0x%08X)\n", want_rx);
    }

    DEBUGP(DL_TRACE, "<==i210_init_ptp: success (%s)\n", full ? "full initialization" : "configuration verified/repaired");
    return 0;
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
 * @brief Validate a logical frequency request (side-effect free: no MMIO, no clock mutation, no init)
 * @return 0 if acceptable; -EINVAL if not representable in TIMINCA or outside the provisional API range
 *
 * Implements: #3 / #149 (frequency adjustment range), #326 (range conflict); HAL: intel_device_ops_t
 */
static int i210_validate_frequency_request(device_t *dev, uint32_t increment_ns, uint32_t increment_frac)
{
    uint32_t raw = 0;

    UNREFERENCED_PARAMETER(dev);   /* pure: the request alone decides */

    if (i210_timinca_encode(increment_ns, increment_frac, &raw) != 0) {
        return -EINVAL;
    }
    if (!i210_timinca_within_api_range(raw)) {
        return -EINVAL;
    }
    return 0;
}

/**
 * @brief Apply a logical frequency request: validate, read previous raw TIMINCA, encode, write
 * @param previous_increment Out: raw TIMINCA value read before the change (AVB_FREQUENCY_REQUEST.current_increment)
 * @return 0 on success; -EINVAL for an unacceptable request (no register touched); -EIO on register failure
 */
static int i210_adjust_frequency(device_t *dev, uint32_t increment_ns, uint32_t increment_frac,
                                 uint32_t *previous_increment)
{
    uint32_t raw = 0;
    uint32_t previous = 0;

    if (dev == NULL || previous_increment == NULL) {
        return -EINVAL;
    }
    if (i210_validate_frequency_request(dev, increment_ns, increment_frac) != 0 ||
        i210_timinca_encode(increment_ns, increment_frac, &raw) != 0) {
        return -EINVAL;
    }
    if (i210_read_timinca(dev, &previous) != 0) {
        return -EIO;
    }
    *previous_increment = previous;
    if (i210_write_timinca(dev, raw) != 0) {
        return -EIO;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * I210 target-time support
 *
 * The I210 shares the same PTP auxiliary-control register layout as the I226:
 *   I210_TSAUXC  (0x0B640) — Time Sync Auxiliary Control
 *   I210_TRGTTIML0/H0 (0x0B644/0x0B648) — Target Time 0 Low/High
 *   I210_TRGTTIML1/H1 (0x0B64C/0x0B650) — Target Time 1 Low/High
 *   I210_FREQOUT0 (0x0B654) — must be non-zero to activate target-time compare
 *   I210_TSIM   (0x0B674) — Time Sync Interrupt Mask
 * The SSOT header (i210_regs.h) defines register addresses but not bit fields;
 * the bit positions below are taken from the Intel I210 Ethernet Controller
 * Datasheet v3.7, Sections 8.16.8 (TSAUXC) and 8.16.2 (TSIM).
 * ---------------------------------------------------------------------------
 */
/* I210 TSAUXC bit masks (I210 DS §8.16.8) */
#define I210_TSAUXC_EN_TT0_MASK   0x00000001U  /* bit 0: Target Time 0 enable */
#define I210_TSAUXC_EN_TT1_MASK   0x00000002U  /* bit 1: Target Time 1 enable */
/* I210 TSIM bit masks (I210 DS §8.16.2 — same positions as I226) */
#define I210_TSIM_TT0_MASK        0x00000008U  /* bit 3: TT0 interrupt enable */
#define I210_TSIM_TT1_MASK        0x00000010U  /* bit 4: TT1 interrupt enable */
/* FREQOUT0 value: 1 µs half-cycle time activates target-time compare logic */
#define I210_FREQOUT0_1MHZ        1000U        /* 1000 ns (within valid range 9–70 000 000 ns) */

/**
 * @brief Set target time for I210 adapter
 *
 * Writes the TRGTTIML0/H0 (or TRGTTIML1/H1) registers and arms the target-time
 * interrupt so that AvbTimerDpcRoutine can detect the event via TSICR polling.
 * Implementation mirrors i226_set_target_time, using I210-specific register names.
 *
 * @param dev            Device context
 * @param timer_index    0 or 1 (selects TRGTTIM0 or TRGTTIM1)
 * @param target_time_ns Target absolute time in nanoseconds
 * @param enable_interrupt 1 = arm interrupt; 0 = write registers only
 * @return 0 on success, -EINVAL / -EIO on error
 */
static int i210_set_target_time(device_t *dev, uint8_t timer_index,
                                uint64_t target_time_ns, int enable_interrupt)
{
    DEBUGP(DL_TRACE, "!!! ENTRY: i210_set_target_time(timer=%u, target=0x%016llX, enable_int=%d)\n",
           timer_index, (unsigned long long)target_time_ns, enable_interrupt);

    if (timer_index > 1) {
        DEBUGP(DL_TRACE, "I210: Invalid timer index %u (max 1)\n", timer_index);
        return -EINVAL;
    }

    uint32_t trgttiml_offset = (timer_index == 0) ? I210_TRGTTIML0 : I210_TRGTTIML1;
    uint32_t trgttimh_offset = (timer_index == 0) ? I210_TRGTTIMH0 : I210_TRGTTIMH1;

    /* TRGTTIML0/H0 use the same seconds/nanoseconds split as SYSTIML/SYSTIMH
     * (I210 DS §8.14.21-22): TRGTTIML0 = nanoseconds (0..999,999,999, bits 29:0),
     *                          TRGTTIMH0 = seconds (bits 31:0).
     * A raw bitwise split of a 64-bit ns value would overflow bits 29:0 and
     * produce a value >999,999,999, violating the register constraint. */
    uint32_t time_low  = (uint32_t)(target_time_ns % 1000000000ULL);  /* nanoseconds (0..999,999,999) */
    uint32_t time_high = (uint32_t)(target_time_ns / 1000000000ULL);  /* seconds */

    /* Write target time registers (low first for atomicity) */
    if (ndis_platform_ops.mmio_write(dev, trgttiml_offset, time_low) != 0) {
        DEBUGP(DL_TRACE, "I210: Failed to write TRGTTIML%u\n", timer_index);
        return -EIO;
    }
    if (ndis_platform_ops.mmio_write(dev, trgttimh_offset, time_high) != 0) {
        DEBUGP(DL_TRACE, "I210: Failed to write TRGTTIMH%u\n", timer_index);
        return -EIO;
    }

    DEBUGP(DL_TRACE, "!!! I210: Target time %u written: 0x%08X%08X (%llu ns)\n",
           timer_index, time_high, time_low, (unsigned long long)target_time_ns);

    /* NOTE: DPC polling (like i226) is omitted here.  The I210 target-time interrupt
     * is driven by TSICR TT0/TT1 bits (same register address as I226: 0x0B66C) and
     * the existing ISR / TSICR-clear path in avb_integration_fixed.c handles it.
     * A dedicated periodic poll DPC can be added later if diagnostic logging is needed. */

    if (enable_interrupt) {
        /* Enable EN_TT0/TT1 in TSAUXC */
        uint32_t tsauxc = 0;
        if (ndis_platform_ops.mmio_read(dev, I210_TSAUXC, &tsauxc) != 0) {
            DEBUGP(DL_TRACE, "I210: Failed to read TSAUXC\n");
            return -EIO;
        }
        uint32_t en_bit = (timer_index == 0) ? I210_TSAUXC_EN_TT0_MASK : I210_TSAUXC_EN_TT1_MASK;
        tsauxc |= en_bit;
        if (ndis_platform_ops.mmio_write(dev, I210_TSAUXC, tsauxc) != 0) {
            DEBUGP(DL_TRACE, "I210: Failed to write TSAUXC\n");
            return -EIO;
        }
        DEBUGP(DL_TRACE, "!!! I210: TSAUXC EN_TT%u set (0x%08X)\n", timer_index, tsauxc);

        /* Enable TT interrupt in TSIM */
        uint32_t tsim = 0;
        if (ndis_platform_ops.mmio_read(dev, I210_TSIM, &tsim) != 0) {
            DEBUGP(DL_TRACE, "I210: Failed to read TSIM\n");
            return -EIO;
        }
        uint32_t tt_bit = (timer_index == 0) ? I210_TSIM_TT0_MASK : I210_TSIM_TT1_MASK;
        tsim |= tt_bit;
        if (ndis_platform_ops.mmio_write(dev, I210_TSIM, tsim) != 0) {
            DEBUGP(DL_TRACE, "I210: Failed to write TSIM\n");
            return -EIO;
        }
        DEBUGP(DL_TRACE, "!!! I210: TSIM TT%u enabled (0x%08X)\n", timer_index, tsim);

        /* Write FREQOUT0 to activate target-time compare logic
         * (See i226_set_target_time for detailed rationale; same silicon behaviour) */
        ndis_platform_ops.mmio_write(dev, I210_FREQOUT0, I210_FREQOUT0_1MHZ);
        DEBUGP(DL_TRACE, "!!! I210: FREQOUT0 set to %u ns\n", I210_FREQOUT0_1MHZ);
    }

    DEBUGP(DL_TRACE, "i210_set_target_time: done (timer=%u target=0x%016llX)\n",
           timer_index, (unsigned long long)target_time_ns);
    return 0;
}

/* QUERY_HW_SNAPSHOT offset map: AVB_HWREG_* id -> I210 MMIO offset, only registers intel-ethernet-regs/gen/i210_regs.h
 * defines.  Any other id (e.g. SRRCTL) is NOT_IN_SSOT - never a guessed address. */
static int i210_hil_register_offset(uint32_t reg_id, uint32_t *offset)
{
    if (offset == NULL) { return -1; }
    switch (reg_id) {
    case AVB_HWREG_TIMINCA:    *offset = I210_TIMINCA; return 0;
    case AVB_HWREG_TSAUXC:     *offset = I210_TSAUXC; return 0;
    case AVB_HWREG_TSYNCTXCTL: *offset = I210_TSYNCTXCTL; return 0;
    case AVB_HWREG_TSYNCRXCTL: *offset = I210_TSYNCRXCTL; return 0;
    case AVB_HWREG_RXPBSIZE:   *offset = I210_RXPBSIZE; return 0;
    case AVB_HWREG_TRGTTIML0:  *offset = I210_TRGTTIML0; return 0;
    case AVB_HWREG_TRGTTIMH0:  *offset = I210_TRGTTIMH0; return 0;
    case AVB_HWREG_TRGTTIML1:  *offset = I210_TRGTTIML1; return 0;
    case AVB_HWREG_TRGTTIMH1:  *offset = I210_TRGTTIMH1; return 0;
    case AVB_HWREG_TQAVCTRL:   *offset = I210_TQAVCTRL; return 0;
    case AVB_HWREG_RAL0:       *offset = I210_RAL0; return 0;
    case AVB_HWREG_RAH0:       *offset = I210_RAH0; return 0;
    default: return -1;
    }
}

/**
 * @brief I210 device operations structure - CORRECTED: No TSN support
 * I210 (2013) has excellent IEEE 1588 PTP but NO TSN features (TSN standard finalized 2015-2016)
 */
const intel_device_ops_t i210_ops = {
    /* Capabilities per Intel I210 datasheet:
     * ENHANCED_TS: TSYNCTXCTL (0xB614) / TSYNCRXCTL (0xB620) present
     * MDIO and EEE not implemented in this driver.
     * EEE: IEEE 802.3az EEE supported via EEER register (0x0E30), LPI mode, 1000BASE-T and 100BASE-TX.
     * MDIO: Not implemented in driver (integrated PHY, NULL ops). */
    .device_name = "Intel I210 Gigabit Ethernet - IEEE 1588 PTP",
    .supported_capabilities = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS |
                              INTEL_CAP_MMIO | INTEL_CAP_EEE, // EEE supported per I210 datasheet (EEER 0x0E30)
    
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

    // PHC frequency adjustment: device-owned TIMINCA encoding (IOCTL_AVB_ADJUST_FREQUENCY)
    .validate_frequency_request = i210_validate_frequency_request,
    .adjust_frequency = i210_adjust_frequency,

    // Target time / auxiliary timestamp operations
    // I210 has TRGTTIML0/H0 and TRGTTIML1/H1 at 0x0B644–0x0B650 (I210 DS §8.14.21-24)
    .set_target_time = i210_set_target_time,  // Implemented — I210 has target-time registers
    .get_target_time = NULL,              // Not yet implemented
    .check_autt_flags = NULL,             // No TSAUXC AUTT flags on I210
    .clear_autt_flag = NULL,              // No TSAUXC AUTT flags on I210
    .get_aux_timestamp = NULL,            // No auxiliary timestamp FIFO on I210
    .clear_aux_timestamp_flag = NULL,     // No auxiliary timestamp FIFO on I210
    
    // TSN operations - NOT SUPPORTED (I210 predates TSN hardware implementation)
    .setup_tas = NULL,                    // No TSN hardware
    .disable_tas = NULL,         /* No TAS hardware on this device */
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
    .validate_configuration = NULL,
    .hil_register_offset = i210_hil_register_offset
};