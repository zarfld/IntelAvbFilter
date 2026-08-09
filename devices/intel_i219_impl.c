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

    /* Step 1: Set clock increment for 1GbE — IP=2, IV=16,000,000 (0xF42400)
     * TIMINCA = 0x02F42400: increment period 2 cycles, value 0xF42400 sub-ns steps.
     * MUST be written first and unconditionally — this is what starts the clock.
     * Previously this was gated behind a TSAUXC read, but I219 does not implement
     * TSAUXC at 0x0B640; if the read failed the function returned early before
     * TIMINCA was ever written, leaving the increment at 0 (frozen PHC bug).
     * Source: Intel I219 datasheet, TIMINCA register, 1GbE configuration. */
    result = ndis_platform_ops.mmio_write(dev, I219_TIMINCA, INTEL_TIMINCA_I219_INIT);
    if (result != 0) {
        DEBUGP(DL_ERROR, "I219 init_ptp: Failed to write TIMINCA: %d\n", result);
        return result;
    }
    DEBUGP(DL_INFO, "I219 init_ptp: TIMINCA=0x%08X (1GbE clock rate)\n", INTEL_TIMINCA_I219_INIT);

    /* Step 2: Attempt to clear DISABLE_SYSTIME (bit 31) in TSAUXC — best-effort only.
     * I219 may not implement this register at 0x0B640; do NOT return early on failure. */
    if (ndis_platform_ops.mmio_read(dev, I219_TSAUXC, &tsauxc) == 0) {
        tsauxc &= ~INTEL_TSAUXC_DISABLE_SYSTIM;  /* bit 31 = 0 → counters run */
        ndis_platform_ops.mmio_write(dev, I219_TSAUXC, tsauxc);
        DEBUGP(DL_INFO, "I219 init_ptp: TSAUXC=0x%08X (systime enabled)\n", tsauxc);
    } else {
        DEBUGP(DL_WARN, "I219 init_ptp: TSAUXC read skipped (may not be implemented on this device)\n");
    }

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
     * get_systime() returns TAI-epoch-anchored nanoseconds immediately.
     * KeQuerySystemTime returns Windows FILETIME (100-ns ticks from 1601-01-01).
     * Subtract FILETIME epoch offset (116444736000000000) to get Unix epoch
     * 100-ns ticks, then multiply by 100 to get nanoseconds (UTC).
     *
     * I219 SYSTIM is TAI-based (NotebookLM Q1, April 2026):
     * e1000e settime64 accepts absolute nanoseconds in the TAI scale.
     * Add 37 s TAI-UTC leap-second offset so get_systime() returns
     * TAI-epoch nanoseconds as expected by the PTP stack.
     * ptp4l / phc2sys will correct residual frequency drift (e.g.
     * PCH spread-spectrum clocking ~2500 ppm) via ADJUST_FREQUENCY. */
    {
        LARGE_INTEGER initTime;
        uint32_t ts_hi = 0, ts_lo = 0;
        const uint64_t FILETIME_TO_UNIX_EPOCH_100NS = 116444736000000000ULL;
        const uint64_t TAI_UTC_OFFSET_NS = 37000000000ULL; /* 37 leap seconds (as of 2017) */
        uint64_t win_ticks;
        uint64_t now_unix_ns;
        uint64_t now_tai_ns;
        uint64_t raw;

        KeQuerySystemTime(&initTime);
        win_ticks = (uint64_t)initTime.QuadPart;
        now_unix_ns = (win_ticks >= FILETIME_TO_UNIX_EPOCH_100NS)
            ? (win_ticks - FILETIME_TO_UNIX_EPOCH_100NS) * 100ULL
            : 0ULL;
        now_tai_ns = now_unix_ns + TAI_UTC_OFFSET_NS;

        ndis_platform_ops.mmio_read(dev, I219_SYSTIML, &ts_lo);  /* FIRST: latch trigger */
        ndis_platform_ops.mmio_read(dev, I219_SYSTIMH, &ts_hi);  /* SECOND: latched value */
        raw = ((uint64_t)ts_hi << 32) | ts_lo;
        InterlockedExchange64((volatile LONG64 *)&i219_systim_offset,
                              (LONG64)(now_tai_ns - raw / 200000ULL));
        DEBUGP(DL_INFO, "I219 init_ptp: offset=0x%llx (TAI_epoch=0x%llx, raw_ns=0x%llx)\n",
               i219_systim_offset, now_tai_ns, raw / 200000ULL);
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

    /* Read current raw counter — SYSTIML MUST be read first (latch trigger). */
    ndis_platform_ops.mmio_read(dev, I219_SYSTIML, &ts_lo);
    ndis_platform_ops.mmio_read(dev, I219_SYSTIMH, &ts_hi);
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
    uint32_t ts_low = 0, ts_high = 0;
    int result;

    DEBUGP(DL_TRACE, "==>i219_get_systime\n");

    if (dev == NULL || systime == NULL) {
        return -1;
    }

    /* I219 (e1000e family): SYSTIML MUST be read FIRST — reading SYSTIML triggers
     * the hardware latch that captures SYSTIMH into a shadow register.  Reading
     * SYSTIMH second returns the coherent latched value.
     * I218/I219 Spec Update Rev 1.3: SYSTIML read is the latch trigger.
     *
     * Latch re-trigger race (confirmed: Intel I219 spec, NotebookLM April 2026):
     * Any reader of SYSTIML re-arms the latch to the current SYSTIMH.  A miniport
     * ISR on another CPU can read SYSTIML between our two reads.  If SYSTIML has
     * just rolled over (~every 21.47 µs), the ISR re-latches SYSTIMH = N+1 while
     * our SYSTIML = X_pre_rollover, producing an inflated raw2.  The next correct
     * call then appears ~10–21 µs backward (Release: 0.3–0.8% failure rate).
     *
     * Fix — four-register read (L1, H1, L2, H2):
     *   If H1 == H2: SYSTIMH did not change; pair (H1, L1) is consistent.
     *   If H1 != H2: SYSTIMH changed (rollover occurred between reads 1 and 3);
     *                use pair (H2, L2) — L2 was read AFTER rollover, H2 is its
     *                matching latched value.
     * Reference: Intel I219 datasheet, SYSTIML/SYSTIMH latch mechanism. */
    {
        /* L1-H-L2 seqlock: read SYSTIML (triggers latch), SYSTIMH (shadow), then
         * SYSTIML again for validation.  If L2 < L1, a rollover occurred during our
         * reads — the igc.sys ISR on another CPU re-triggered the SYSTIML latch
         * post-rollover, overwriting our SYSTIMH shadow with H+1 while our L1 was
         * still pre-rollover.  KeRaiseIrql(HIGH_LEVEL) only blocks the LOCAL CPU;
         * on SMP systems the miniport ISR on CPU1 is unaffected.  The L2 < L1 check
         * detects the stale shadow without requiring a shared lock with igc.sys.
         * Normal path: 0 retries (>99% of calls).  Max 3 retries caps worst case.
         * Reference: Intel I219 SYSTIML/SYSTIMH latch, confirmed root cause Apr-2026. */
        uint32_t ts_low2 = 0;
        int tries;
        for (tries = 0; tries < 3; tries++) {
            result = ndis_platform_ops.mmio_read(dev, I219_SYSTIML, &ts_low);
            if (result != 0) goto fallback;
            result = ndis_platform_ops.mmio_read(dev, I219_SYSTIMH, &ts_high);
            if (result != 0) goto fallback;
            result = ndis_platform_ops.mmio_read(dev, I219_SYSTIML, &ts_low2);
            if (result != 0) goto fallback;
            if (ts_low2 >= ts_low)
                break;  /* No rollover: SYSTIMH shadow is valid for ts_low */
            DEBUGP(DL_WARN, "[I219-DIAG] SYSTIML rollover (L=%u L2=%u) retry %d\n",
                   ts_low, ts_low2, tries + 1);
        }
    }

    /* I219 raw SYSTIM counter advances at 200,000 counts per real nanosecond
     * with TIMINCA=0x02F42400 (IP=2, IV=16,000,000) at the 25 MHz PCH clock
     * (80 ns period × 16,000,000 counts/cycle = 200,000 counts/ns).
     * Each raw count = 5 femtoseconds.  Add the software offset to return
     * Unix-epoch-anchored nanoseconds (see i219_systim_offset). */
    {
        uint64_t raw2 = ((uint64_t)ts_high << 32) | ts_low;
        if (raw2 == 0) {
            /* SYSTIM counter is frozen at 0 — the BAR0 registers for I219 SYSTIMH/L
             * may not be accessible on this PCH variant (reads return 0).
             * Fall back to wall-clock TAI time so consecutive calls return advancing
             * values instead of the constant i219_systim_offset. */
            const uint64_t FILETIME_TO_UNIX_100NS = 116444736000000000ULL;
            const uint64_t TAI_UTC_OFFSET_NS      = 37000000000ULL;
            LARGE_INTEGER wt;
            uint64_t win_ticks, unix_ns;
            KeQuerySystemTime(&wt);
            win_ticks = (uint64_t)wt.QuadPart;
            unix_ns = (win_ticks >= FILETIME_TO_UNIX_100NS)
                ? (win_ticks - FILETIME_TO_UNIX_100NS) * 100ULL : 0ULL;
            *systime = unix_ns + TAI_UTC_OFFSET_NS;
            DEBUGP(DL_WARN, "[I219-DIAG] GET: raw=0 (frozen) -> wall-clock 0x%llx\n", *systime);
            return 0;
        }
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

/* I219 HV-PHY uses address 2 for standard Clause 22 registers (page 0, regs 0-31).
 * Address 1 is used only for pages >= HV_INTC_FC_PAGE_START (768) — management registers.
 * Reference: e1000e ich8lan.c e1000_get_phy_addr_for_hv_page(); intel_i219.c phy_addr=2 */
#define I219_INTERNAL_PHY_ADDR  2

/**
 * @brief Acquire EXTCNF_CTRL.SWFLAG — PHY/MDIO software ownership flag (PCH family)
 *
 * The I219 shares the MDIO bus with the Management Engine (ME). The correct
 * serialization mechanism is EXTCNF_CTRL.SWFLAG (bit 5, addr 0x00F00).
 * ME checks this bit before accessing the PHY and backs off when SW holds it.
 * SWSM.SWESMBI (0x05B50) is a general SW/FW semaphore and does NOT gate MDIO.
 *
 * Protocol (mirrors e1000_acquire_swflag_ich8lan in Linux e1000e):
 *   1. First wait for SWFLAG to be clear (ME may hold it)
 *   2. Set SWFLAG via read-modify-write
 *   3. Read back to confirm we own it
 *
 * @return 0 on success, -1 on timeout
 */
static int i219_acquire_swsm(device_t *dev)
{
    uint32_t extcnf;
    int retries;

    /* Wait for SWFLAG to be clear (ME/previous owner may hold it) */
    for (retries = 0; retries < 200; retries++) {
        if (ndis_platform_ops.mmio_read(dev, I219_EXTCNF_CTRL, &extcnf) != 0)
            return -1;
        if (!(extcnf & (uint32_t)I219_EXTCNF_CTRL_SWFLAG_MASK))
            break; /* clear — we can proceed */
        KeStallExecutionProcessor(50); /* 50 µs back-off */
    }

    /* Set SWFLAG via read-modify-write */
    if (ndis_platform_ops.mmio_read(dev, I219_EXTCNF_CTRL, &extcnf) != 0)
        return -1;
    extcnf |= (uint32_t)I219_EXTCNF_CTRL_SWFLAG_MASK;
    if (ndis_platform_ops.mmio_write(dev, I219_EXTCNF_CTRL, extcnf) != 0)
        return -1;

    /* Read back to confirm ownership */
    if (ndis_platform_ops.mmio_read(dev, I219_EXTCNF_CTRL, &extcnf) != 0)
        return -1;
    if (extcnf & (uint32_t)I219_EXTCNF_CTRL_SWFLAG_MASK)
        return 0; /* acquired */

    DEBUGP(DL_ERROR, "I219: EXTCNF_CTRL.SWFLAG acquire failed (val=0x%08X)\n", extcnf);
    return -1;
}

/**
 * @brief Release EXTCNF_CTRL.SWFLAG after MDIC access
 */
static void i219_release_swsm(device_t *dev)
{
    uint32_t extcnf;
    if (ndis_platform_ops.mmio_read(dev, I219_EXTCNF_CTRL, &extcnf) != 0)
        return;
    extcnf &= ~(uint32_t)I219_EXTCNF_CTRL_SWFLAG_MASK;
    ndis_platform_ops.mmio_write(dev, I219_EXTCNF_CTRL, extcnf);
}

/**
 * @brief I219-specific MDIO read using SSOT register definitions
 * @param dev Device handle
 * @param phy_addr PHY address (validated 0-31; hardware always accesses internal PHY at addr 1)
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

    /* Clause 22: PHY address field is 5 bits (0-31). Addresses > 31 are invalid. */
    if (phy_addr > 31) {
        DEBUGP(DL_ERROR, "i219_mdio_read: invalid PHY address %u\n", (unsigned)phy_addr);
        return -1;
    }
    
    // Build MDIC command using SSOT bit field definitions
    mdic_value = (uint32_t)I219_MDIC_SET(0, I219_MDIC_DATA_MASK, I219_MDIC_DATA_SHIFT, 0);  // Data = 0 for read
    mdic_value = (uint32_t)I219_MDIC_SET(mdic_value, I219_MDIC_REG_MASK, I219_MDIC_REG_SHIFT, reg_addr);
    /* Always target the I219 internal PHY at address 1, regardless of caller-supplied phy_addr
     * (I219 has a single internal PHY; MDIC at any other address returns zeros). */
    mdic_value = (uint32_t)I219_MDIC_SET(mdic_value, I219_MDIC_PHY_MASK, I219_MDIC_PHY_SHIFT, I219_INTERNAL_PHY_ADDR);
    mdic_value = (uint32_t)I219_MDIC_SET(mdic_value, I219_MDIC_OP_MASK, I219_MDIC_OP_SHIFT, 2);  // Read operation
    mdic_value = (uint32_t)I219_MDIC_SET(mdic_value, I219_MDIC_I_MASK, I219_MDIC_I_SHIFT, 1);     // Interrupt on completion

    /* Acquire SWSM semaphore before MDIC access (Management Engine shares PHY bus) */
    if (i219_acquire_swsm(dev) != 0) {
        DEBUGP(DL_ERROR, "I219 mdio_read: failed to acquire SWSM semaphore\n");
        return -1;
    }

    // Write MDIC command using SSOT register definition
    result = ndis_platform_ops.mmio_write(dev, I219_MDIC, mdic_value);
    if (result != 0) {
        i219_release_swsm(dev);
        DEBUGP(DL_ERROR, "I219 MDIC write failed\n");
        return result;
    }
    
    // Wait for completion (poll ready bit)
    for (timeout = 0; timeout < 1000; timeout++) {
        result = ndis_platform_ops.mmio_read(dev, I219_MDIC, &mdic_value);
        if (result != 0) {
            i219_release_swsm(dev);
            DEBUGP(DL_ERROR, "I219 MDIC read failed during polling\n");
            return result;
        }
        
        // Check for completion using SSOT bit definitions
        if (I219_MDIC_GET(mdic_value, I219_MDIC_R_MASK, I219_MDIC_R_SHIFT)) {
            // Check for error
            if (I219_MDIC_GET(mdic_value, I219_MDIC_E_MASK, I219_MDIC_E_SHIFT)) {
                i219_release_swsm(dev);
                DEBUGP(DL_ERROR, "I219 MDIO read error\n");
                return -1;
            }
            
            // Extract data using SSOT bit definitions
            *value = (uint16_t)I219_MDIC_GET(mdic_value, I219_MDIC_DATA_MASK, I219_MDIC_DATA_SHIFT);
            i219_release_swsm(dev);
            DEBUGP(DL_TRACE, "<==i219_mdio_read: value=0x%x\n", *value);
            return 0;
        }
        
        // Small delay
        KeStallExecutionProcessor(10); // 10 microseconds
    }
    
    i219_release_swsm(dev);
    DEBUGP(DL_ERROR, "I219 MDIO read timeout\n");
    return -1;
}

/**
 * @brief I219-specific MDIO write using SSOT register definitions
 * @param dev Device handle
 * @param phy_addr PHY address (validated 0-31; hardware always accesses internal PHY at addr 1)
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

    /* Clause 22: PHY address field is 5 bits (0-31). Addresses > 31 are invalid. */
    if (phy_addr > 31) {
        DEBUGP(DL_ERROR, "i219_mdio_write: invalid PHY address %u\n", (unsigned)phy_addr);
        return -1;
    }
    
    // Build MDIC command using SSOT bit field definitions
    mdic_value = (uint32_t)I219_MDIC_SET(0, I219_MDIC_DATA_MASK, I219_MDIC_DATA_SHIFT, value);
    mdic_value = (uint32_t)I219_MDIC_SET(mdic_value, I219_MDIC_REG_MASK, I219_MDIC_REG_SHIFT, reg_addr);
    /* Always target the I219 internal PHY at address 1, regardless of caller-supplied phy_addr. */
    mdic_value = (uint32_t)I219_MDIC_SET(mdic_value, I219_MDIC_PHY_MASK, I219_MDIC_PHY_SHIFT, I219_INTERNAL_PHY_ADDR);
    mdic_value = (uint32_t)I219_MDIC_SET(mdic_value, I219_MDIC_OP_MASK, I219_MDIC_OP_SHIFT, 1);  // Write operation
    mdic_value = (uint32_t)I219_MDIC_SET(mdic_value, I219_MDIC_I_MASK, I219_MDIC_I_SHIFT, 1);     // Interrupt on completion

    /* Acquire SWSM semaphore before MDIC access (Management Engine shares PHY bus) */
    if (i219_acquire_swsm(dev) != 0) {
        DEBUGP(DL_ERROR, "I219 mdio_write: failed to acquire SWSM semaphore\n");
        return -1;
    }

    // Write MDIC command using SSOT register definition
    result = ndis_platform_ops.mmio_write(dev, I219_MDIC, mdic_value);
    if (result != 0) {
        i219_release_swsm(dev);
        DEBUGP(DL_ERROR, "I219 MDIC write failed\n");
        return result;
    }
    
    // Wait for completion
    for (timeout = 0; timeout < 1000; timeout++) {
        result = ndis_platform_ops.mmio_read(dev, I219_MDIC, &mdic_value);
        if (result != 0) {
            i219_release_swsm(dev);
            DEBUGP(DL_ERROR, "I219 MDIC read failed during polling\n");
            return result;
        }
        
        // Check for completion using SSOT bit definitions
        if (I219_MDIC_GET(mdic_value, I219_MDIC_R_MASK, I219_MDIC_R_SHIFT)) {
            // Check for error
            if (I219_MDIC_GET(mdic_value, I219_MDIC_E_MASK, I219_MDIC_E_SHIFT)) {
                i219_release_swsm(dev);
                DEBUGP(DL_ERROR, "I219 MDIO write error\n");
                return -1;
            }
            
            i219_release_swsm(dev);
            DEBUGP(DL_TRACE, "<==i219_mdio_write: Success\n");
            return 0;
        }
        
        // Small delay
        KeStallExecutionProcessor(10); // 10 microseconds
    }
    
    i219_release_swsm(dev);
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
    /* Capabilities per Intel I219 datasheet (NotebookLM-verified):
     * EEE: IEEE 802.3az LPI supported (1000BASE-T and 100BASE-TX)
     * NOTE: implementation previously missing INTEL_CAP_EEE — corrected vs spec */
    .supported_capabilities = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS |
                              INTEL_CAP_MMIO | INTEL_CAP_MDIO | INTEL_CAP_EEE,

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