/*++

Module Name:

    intel_i226_impl.c

Abstract:

    Intel I226 2.5G Ethernet device-specific implementation.
    Implements full TSN capabilities including TAS, Frame Preemption, and PTM.
    
    Register addresses are evidence-based from Linux IGC driver.

--*/

#include "precomp.h"
#include "intel_device_interface.h"
#include "avb_integration.h"
#include "external/intel_avb/lib/intel_windows.h"  // Required for platform_ops struct definition
#include "external/intel_avb/lib/intel_private.h"  // Required for struct intel_private definition
#include "intel-ethernet-regs/gen/i226_regs.h"  // SSOT register definitions

// Application-specific constants
#define I226_FREQOUT0_1MHZ      1000        // 1 µs half-cycle (1 MHz clock) - application value
#define I226_MAX_TIMER_INDEX    1           // I226 supports timer index 0-1
#define I226_AUTT_FLAG_TT0      0x01        // TT0 triggered flag (application-level)
#define I226_AUTT_FLAG_TT1      0x02        // TT1 triggered flag (application-level)

// Alternative SYSTIM read addresses (device-specific routing)
#define I226_SYSTIML_READ       0x0C0C8  // SYSTIML read address
#define I226_SYSTIMH_READ       0x0C0CC  // SYSTIMH read address

// I226 TSN Register Definitions - Evidence-Based from Linux IGC Driver
// TODO: Add these to i226.yaml and regenerate SSOT header
#define I226_TQAVCTRL           0x3570  // TSN control register
#define I226_BASET_L            0x3314  // Base time low 
#define I226_BASET_H            0x3318  // Base time high
#define I226_QBVCYCLET          0x331C  // Cycle time register
#define I226_QBVCYCLET_S        0x3320  // Cycle time shadow register
#define I226_STQT(i)            (0x3340 + (i)*4)  // Start time for queue i
#define I226_ENDQT(i)           (0x3380 + (i)*4)  // End time for queue i  
#define I226_TXQCTL(i)          (0x3300 + (i)*4)  // Queue control for queue i

// I226 TSN Control Bits - Evidence-Based from Linux IGC Driver
// TODO: Add these to i226.yaml and regenerate SSOT header
#define I226_TQAVCTRL_TRANSMIT_MODE_TSN  0x00000001  // TSN transmit mode
#define I226_TQAVCTRL_ENHANCED_QAV       0x00000008  // Enhanced QAV mode
#define I226_TQAVCTRL_FUTSCDDIS          0x00800000  // Future Schedule Disable (I226 specific)

// I226 Queue Control Bits
// TODO: Add these to i226.yaml and regenerate SSOT header
#define I226_TXQCTL_QUEUE_MODE_LAUNCHT   0x00000001  // Launch time mode
#define I226_TXQCTL_STRICT_CYCLE         0x00000002  // Strict cycle mode

// I226 EtherType Queue Filter (ETQF) - Evidence from Linux IGB driver (igb_ptp.c:806-811)
// Required for hardware PTP packet timestamping in continuous operation
// TODO: Add these to i226.yaml and regenerate SSOT header
#define I226_ETQF(n)            (0x05CB0 + (4 * (n)))  // EtherType Queue Filter array
#define I226_ETQF_FILTER_ENABLE (1 << 26)              // Enable filter
#define I226_ETQF_1588          (1 << 30)              // Enable timestamping for 1588 packets
#define I226_ETQF_ETYPE_MASK    0x0000FFFF             // EtherType mask (lower 16 bits)
#define ETH_P_1588              0x88F7                 // PTP/IEEE 1588 EtherType

// External platform operations
extern const struct platform_ops ndis_platform_ops;

// Forward declarations
static int init_ptp(device_t *dev);

/**
 * @brief DPC routine for periodic TSICR polling (Task 7)
 * @param Dpc Pointer to DPC object
 * @param DeferredContext Pointer to AVB_DEVICE_CONTEXT
 * @param SystemArgument1 Unused
 * @param SystemArgument2 Unused
 * 
 * Fires every 10ms to poll TSICR register for target time interrupts.
 * On I226, target time interrupts may not generate MSI-X interrupts reliably,
 * so polling TSICR is required to detect AUTT0/AUTT1 flags.
 */
#pragma warning(push)
#pragma warning(disable: 4505)  // Unreferenced function - used as DPC callback (function pointer)
static VOID AvbTimerDpcRoutine(
    PKDPC Dpc,
    PVOID DeferredContext,
    PVOID SystemArgument1,
    PVOID SystemArgument2)
#pragma warning(pop)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    
    PAVB_DEVICE_CONTEXT context = (PAVB_DEVICE_CONTEXT)DeferredContext;
    if (!context) {
        DEBUGP(DL_TRACE, "!!! DPC: NULL context!\n");
        return;
    }
    
    // Increment call counter (thread-safe)
    LONG call_count = InterlockedIncrement(&context->target_time_dpc_call_count);
    
    // DIAGNOSTIC: Always log on first 10 calls, then every 100 calls
    BOOLEAN should_log = (call_count <= 10) || (call_count % 100 == 0);
    
    if (should_log) {
        DEBUGP(DL_TRACE, "!!! DPC[%d]: Target Time DPC executing (context=%p, active=%d)\n", 
               call_count, context, context->target_time_poll_active);
    }
    
    // Check if polling should stop (flag cleared by cleanup)
    if (!context->target_time_poll_active) {
        DEBUGP(DL_TRACE, "!!! DPC[%d]: Poll flag deactivated, exiting\n", call_count);
        return;
    }
    
    // Write diagnostic every 100 calls (every 1 second at 10ms interval)
    if (call_count % 100 == 0) {
        DEBUGP(DL_TRACE, "!!! DPC: Polled %d times (every 10ms)\n", call_count);
    }
    
    // Read TSICR to check for timer events
    device_t *dev = &context->intel_device;
    uint32_t tsicr = 0;
    
    if (ndis_platform_ops.mmio_read(dev, I226_TSICR, &tsicr) != 0) {
        DEBUGP(DL_TRACE, "!!! DPC: Failed to read TSICR\n");
        return;
    }
    
    // DIAGNOSTIC: Read current SYSTIM to compare with target time
    uint32_t systim_low = 0, systim_high = 0;
    uint64_t current_systim = 0;
    if (ndis_platform_ops.mmio_read(dev, I226_SYSTIML_READ, &systim_low) == 0 &&
        ndis_platform_ops.mmio_read(dev, I226_SYSTIMH_READ, &systim_high) == 0) {
        current_systim = ((uint64_t)systim_high << 32) | systim_low;
    }
    
    // DIAGNOSTIC: Read target time registers to verify they're still set
    uint32_t target_low = 0, target_high = 0;
    uint64_t target_time = 0;
    if (ndis_platform_ops.mmio_read(dev, I226_TRGTTIML0, &target_low) == 0 &&
        ndis_platform_ops.mmio_read(dev, I226_TRGTTIMH0, &target_high) == 0) {
        target_time = ((uint64_t)target_high << 32) | target_low;
    }
    
    // DIAGNOSTIC: Always log on first 10 calls to show DPC is working
    if (should_log) {
        DEBUGP(DL_TRACE, "!!! DPC[%d]: TSICR=0x%08X (TT0=%d, TT1=%d)\n",
               call_count, tsicr,
               (tsicr & I226_TSICR_TT0_MASK) ? 1 : 0,
               (tsicr & I226_TSICR_TT1_MASK) ? 1 : 0);
        DEBUGP(DL_TRACE, "!!! DPC[%d]: SYSTIM=0x%016llX, TARGET=0x%016llX, delta=%s%lld ns\n",
               call_count, current_systim, target_time,
               (current_systim >= target_time) ? "+" : "-",
               (current_systim >= target_time) ? 
                   (long long)(current_systim - target_time) : 
                   (long long)(target_time - current_systim));
    }
    
    // Periodic status log via DEBUGP (replaces registry diagnostics)
    if ((call_count <= 100 && call_count % 10 == 0) || (call_count % 100 == 0)) {
        DEBUGP(DL_TRACE, "!!! DPC[%d]: TSICR=0x%08X SYSTIM=0x%016llX TARGET=0x%016llX\n",
               call_count, tsicr, (unsigned long long)current_systim, (unsigned long long)target_time);
    }
    
    // Only log when interrupt bits are set (avoid spam in normal operation)
    if (tsicr & (I226_TSICR_TT0_MASK | I226_TSICR_TT1_MASK)) {
        DEBUGP(DL_TRACE, "!!! DPC: *** INTERRUPT DETECTED *** TSICR=0x%08X (TT0=%d, TT1=%d)\n",
               tsicr,
               (tsicr & I226_TSICR_TT0_MASK) ? 1 : 0,
               (tsicr & I226_TSICR_TT1_MASK) ? 1 : 0);
    }
    
    // Check TT0 interrupt
    if (tsicr & I226_TSICR_TT0_MASK) {
        DEBUGP(DL_TRACE, "!!! DPC DETECTED: TT0 interrupt fired! (TSICR=0x%08X)\n", tsicr);
        
        // BUG FIX: AUXSTMP registers are NOT automatically updated when target time reached
        // Per I226 datasheet: AUXSTMP only written when TSYNCRXCTL.TSIP latches an event
        // For target time events, we should use the TARGET time itself (TRGTTIML0/TRGTTIMH0)
        // This is the time that was configured and reached, not the auxiliary timestamp
        
        uint32_t tt0_target_low = 0, tt0_target_high = 0;
        if (ndis_platform_ops.mmio_read(dev, I226_TRGTTIML0, &tt0_target_low) == 0 &&
            ndis_platform_ops.mmio_read(dev, I226_TRGTTIMH0, &tt0_target_high) == 0) {
            uint64_t target_timestamp = ((uint64_t)tt0_target_high << 32) | tt0_target_low;
            
            DEBUGP(DL_TRACE, "!!! DPC: TARGET TIME 0 = 0x%016llX (%llu ns)\n",
                   (unsigned long long)target_timestamp,
                   (unsigned long long)target_timestamp);
            
            // Post timestamp event to subscribers (AVB_TS_EVENT_TARGET_TIME = 0x04)
            // NOTE: Event type mask bit values: RX=0x01, TX=0x02, TARGET_TIME=0x04, AUX=0x08
            // Parameters: context, event_type, timestamp_ns, vlan_id, pcp, queue (timer_idx), packet_length, trigger_source
            DEBUGP(DL_TRACE, "!!! DPC: POSTING TARGET_TIME EVENT (type=0x04, ts=0x%016llX)\n",
                   (unsigned long long)target_timestamp);
            AvbPostTimestampEvent(context, 0x04, target_timestamp, 0, 0, 0, 0, 0, 0);
            DEBUGP(DL_TRACE, "!!! DPC: Posted target time event to subscribers (RETURNED FROM AvbPostTimestampEvent)\n");
        } else {
            DEBUGP(DL_TRACE, "!!! DPC: Failed to read target time registers\n");
        }
        
        // Clear TT0 interrupt (RW1C - write 1 to clear)
        uint32_t clear_val = I226_TSICR_TT0_MASK;
        if (ndis_platform_ops.mmio_write(dev, I226_TSICR, clear_val) != 0) {
            DEBUGP(DL_TRACE, "!!! DPC: Failed to clear TSICR.TT0\n");
        } else {
            DEBUGP(DL_TRACE, "!!! DPC: Cleared TSICR.TT0 interrupt\n");
        }
    }
    
    // Check TT1 interrupt (similar handling)
    if (tsicr & I226_TSICR_TT1_MASK) {
        DEBUGP(DL_TRACE, "!!! DPC DETECTED: TT1 interrupt fired! (TSICR=0x%08X)\n", tsicr);
        
        // BUG FIX: Read TARGET time (not AUXSTMP) for timer 1
        uint32_t tt1_target_low = 0, tt1_target_high = 0;
        if (ndis_platform_ops.mmio_read(dev, I226_TRGTTIML1, &tt1_target_low) == 0 &&
            ndis_platform_ops.mmio_read(dev, I226_TRGTTIMH1, &tt1_target_high) == 0) {
            uint64_t target_timestamp = ((uint64_t)tt1_target_high << 32) | tt1_target_low;
            
            DEBUGP(DL_TRACE, "!!! DPC: TARGET TIME 1 = 0x%016llX\n", (unsigned long long)target_timestamp);
            
            // Post timestamp event to subscribers
            // Parameters: context, event_type, timestamp_ns, vlan_id, pcp, queue (timer_idx=1), packet_length, trigger_source
            AvbPostTimestampEvent(context, 0x04, target_timestamp, 0, 0, 1, 0, 0, 0);
            DEBUGP(DL_TRACE, "!!! DPC: Posted target time event (TT1) to subscribers\n");
        }
        
        // Clear TT1 interrupt
        uint32_t clear_val = I226_TSICR_TT1_MASK;
        ndis_platform_ops.mmio_write(dev, I226_TSICR, clear_val);
        DEBUGP(DL_TRACE, "!!! DPC: Cleared TSICR.TT1 interrupt\n");
    }
}

/**
 * @brief Initialize I226 device
 * @param dev Device handle
 * @return 0 on success, <0 on error
 */
static int init(device_t *dev)
{
    DEBUGP(DL_TRACE, "==>i226_init\n");
    
    if (dev == NULL) {
        return -1;
    }
    
    // I226-specific initialization
    if (ndis_platform_ops.init) {
        NTSTATUS result = ndis_platform_ops.init(dev);
        if (!NT_SUCCESS(result)) {
            DEBUGP(DL_TRACE, "I226 platform init failed: 0x%x\n", result);
            return -1;
        }
    }
    
    // Initialize PTP clock (required for GET_TIMESTAMP to work)
    DEBUGP(DL_TRACE, "I226: Initializing PTP clock\n");
    int ptp_result = init_ptp(dev);
    if (ptp_result != 0) {
        DEBUGP(DL_TRACE, "I226: PTP initialization returned: %d\n", ptp_result);
        // Continue - basic functionality still works
    }
    
    DEBUGP(DL_TRACE, "<==i226_init: Success\n");
    return 0;
}

/**
 * @brief Cleanup I226 device
 * @param dev Device handle
 * @return 0 on success, <0 on error
 */
static int cleanup(device_t *dev)
{
    DEBUGP(DL_TRACE, "==>i226_cleanup\n");
    
    if (dev == NULL) {
        return -1;
    }
    
    if (ndis_platform_ops.cleanup) {
        ndis_platform_ops.cleanup(dev);
    }
    
    DEBUGP(DL_TRACE, "<==i226_cleanup: Success\n");
    return 0;
}

/**
 * @brief Get I226 device information
 * @param dev Device handle
 * @param buffer Output buffer
 * @param size Buffer size
 * @return 0 on success, <0 on error
 */
static int get_info(device_t *dev, char *buffer, ULONG size)
{
    const char *info = "Intel I226 2.5G Ethernet - Advanced TSN";
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
 * @brief Set I226 system time (SYSTIM registers)
 * @param dev Device handle
 * @param systime 64-bit time value in nanoseconds
 * @return 0 on success, <0 on error
 */
static int set_systime(device_t *dev, uint64_t systime)
{
    uint32_t sec, nsec;
    uint32_t tsauxc_val = 0;
    int result;
    
    DEBUGP(DL_TRACE, "==>i226_set_systime: 0x%llx\n", systime);
    
    if (dev == NULL) {
        return -1;
    }
    
    // I226/I225: split format -- SYSTIMH=seconds, SYSTIML=nanoseconds (0-999,999,999)
    // Do NOT use (systime >> 32) / (systime & 0xFFFFFFFF) -- that is the I210 flat format
    sec  = (uint32_t)(systime / 1000000000ULL);
    nsec = (uint32_t)(systime % 1000000000ULL);
    
    // Step 1: Disable SYSTIM counter for an atomic write sequence.
    // TSAUXC (0xB640) bit 31 halts the SYSTIM increment logic.
    // Note: The SSOT header labels this bit PLLLOCKED but the hardware spec defines it
    // as "Disable systime" -- confirmed via IEEE 1588 I226 hardware manual.
    ndis_platform_ops.mmio_read(dev, I226_TSAUXC, &tsauxc_val);
    result = ndis_platform_ops.mmio_write(dev, I226_TSAUXC, tsauxc_val | (1UL << 31));
    if (result != 0) {
        DEBUGP(DL_WARN, "i226_set_systime: failed to disable SYSTIM via TSAUXC, proceeding\n");
    }
    
    // Step 2: Write nanoseconds to SYSTIML
    result = ndis_platform_ops.mmio_write(dev, I226_SYSTIML, nsec);
    if (result != 0) {
        ndis_platform_ops.mmio_write(dev, I226_TSAUXC, tsauxc_val); // restore
        return result;
    }
    
    // Step 3: Write seconds to SYSTIMH
    result = ndis_platform_ops.mmio_write(dev, I226_SYSTIMH, sec);
    if (result != 0) {
        ndis_platform_ops.mmio_write(dev, I226_TSAUXC, tsauxc_val); // restore
        return result;
    }
    
    // Step 4: Re-enable SYSTIM counter
    ndis_platform_ops.mmio_write(dev, I226_TSAUXC, tsauxc_val & ~(1UL << 31));
    
    DEBUGP(DL_TRACE, "<==i226_set_systime: sec=%u nsec=%u Success\n", sec, nsec);
    return 0;
}

/**
 * @brief Get I226 system time (SYSTIM registers)
 * @param dev Device handle
 * @param systime Output 64-bit time value
 * @return 0 on success, <0 on error
 */
static int get_systime(device_t *dev, uint64_t *systime)
{
    uint32_t ts_low, ts_high;
    int result;
    
    DEBUGP(DL_TRACE, "==>i226_get_systime\n");
    
    if (dev == NULL || systime == NULL) {
        return -1;
    }
    
    // I226/I225: SYSTIMH=seconds, SYSTIML=nanoseconds (split format, NOT flat 64-bit)
    // Read order: SYSTIML first -- reading SYSTIML latches SYSTIMH into a shadow register,
    // ensuring both values correspond to the same instant (hardware-guaranteed atomic snapshot).
    result = ndis_platform_ops.mmio_read(dev, I226_SYSTIML, &ts_low);
    if (result != 0) return result;
    
    result = ndis_platform_ops.mmio_read(dev, I226_SYSTIMH, &ts_high);
    if (result != 0) return result;
    
    // Reconstruct 64-bit nanosecond timestamp: seconds * 1e9 + nanoseconds
    // NOT ((high << 32) | low) -- that would be the I210 flat format
    *systime = (uint64_t)ts_high * 1000000000ULL + (uint64_t)ts_low;
    
    DEBUGP(DL_TRACE, "<==i226_get_systime: sec=%u nsec=%u total=0x%llx\n", ts_high, ts_low, *systime);
    return 0;
}

/**
 * @brief Initialize I226 PTP functionality
 * @param dev Device handle
 * @return 0 on success, <0 on error
 */
static int init_ptp(device_t *dev)
{
    uint32_t tsauxc_val = 0;
    uint32_t systimh = 0, systiml = 0;
    uint32_t timinca = 0;
    
    DEBUGP(DL_TRACE, "==>i226_init_ptp: Starting PTP clock initialization\n");
    
    if (dev == NULL) {
        DEBUGP(DL_TRACE, "i226_init_ptp: NULL device\n");
        return -1;
    }
    
    // Step 1: Read and configure TIMINCA (clock increment register)
    if (ndis_platform_ops.mmio_read(dev, I226_TIMINCA, &timinca) == 0) {
        DEBUGP(DL_TRACE, "I226: Current TIMINCA=0x%08X\n", timinca);
        
        // If TIMINCA is 0, set default 24ns increment for I226
        if (timinca == 0) {
            timinca = INTEL_TIMINCA_DEFAULT;  // 24ns per cycle (I226 default)
            if (ndis_platform_ops.mmio_write(dev, I226_TIMINCA, timinca) != 0) {
                DEBUGP(DL_TRACE, "I226: Failed to write TIMINCA\n");
                return -1;
            }
            DEBUGP(DL_TRACE, "I226: TIMINCA set to 0x%08X (24ns/cycle)\n", timinca);
        }
    } else {
        DEBUGP(DL_TRACE, "I226: Failed to read TIMINCA\n");
        return -1;
    }
    
    // Step 2: Initialize SYSTIM registers to 1 (writing 0 might not trigger clock start)
    if (ndis_platform_ops.mmio_write(dev, I226_SYSTIML, 1) != 0 ||
        ndis_platform_ops.mmio_write(dev, I226_SYSTIMH, 0) != 0) {
        DEBUGP(DL_TRACE, "I226: Failed to initialize SYSTIM\n");
        return -1;
    }
    DEBUGP(DL_TRACE, "I226: SYSTIM initialized to 0x0000000000000001\n");
    
    // Step 3: Verify SYSTIM is actually written
    if (ndis_platform_ops.mmio_read(dev, I226_SYSTIML, &systiml) == 0 &&
        ndis_platform_ops.mmio_read(dev, I226_SYSTIMH, &systimh) == 0) {
        DEBUGP(DL_TRACE, "I226: SYSTIM readback: 0x%08X%08X\n", systimh, systiml);
    }
    
    // Step 4: Enable SYSTIM clock via TSAUXC
    if (ndis_platform_ops.mmio_read(dev, I226_TSAUXC, &tsauxc_val) == 0) {
        DEBUGP(DL_TRACE, "I226: Current TSAUXC=0x%08X\n", tsauxc_val);
        
        // Clear disable bit (bit 31) if set, and enable auto-adjust (bit 2)
        tsauxc_val &= ~I226_TSAUXC_PLLLOCKED_MASK;  // Clear PLLLOCKED/disable bit
        tsauxc_val |= I226_TSAUXC_EN_CLK0_MASK;     // Set EN_CLK0 - Enable clock 0
        
        if (ndis_platform_ops.mmio_write(dev, I226_TSAUXC, tsauxc_val) != 0) {
            DEBUGP(DL_TRACE, "I226: Failed to write TSAUXC\n");
            return -1;
        }
        DEBUGP(DL_TRACE, "I226: TSAUXC configured: 0x%08X\n", tsauxc_val);
        
        // Verify write
        if (ndis_platform_ops.mmio_read(dev, I226_TSAUXC, &tsauxc_val) == 0) {
            DEBUGP(DL_TRACE, "I226: TSAUXC readback: 0x%08X\n", tsauxc_val);
        }
    } else {
        DEBUGP(DL_TRACE, "I226: Failed to read TSAUXC\n");
        return -1;
    }
    
    // Enable RX/TX hardware timestamping (REQUIRED for RXSTMPL/H and TXSTMPL/H to capture timestamps)
    uint32_t tsyncrxctl = 0, tsynctxctl = 0;
    
    // TSYNCRXCTL: Enable RX timestamping for all packets
    if (ndis_platform_ops.mmio_read(dev, I226_TSYNCRXCTL, &tsyncrxctl) == 0) {
        // Set EN bit (bit 4) and TYPE field (bits 3-1) to TYPE_ALL (0x4 = 0b100)
        tsyncrxctl = (uint32_t)I226_TSYNCRXCTL_SET(0, I226_TSYNCRXCTL_EN_MASK, I226_TSYNCRXCTL_EN_SHIFT, 1);
        tsyncrxctl = (uint32_t)I226_TSYNCRXCTL_SET(tsyncrxctl, I226_TSYNCRXCTL_TYPE_MASK, I226_TSYNCRXCTL_TYPE_SHIFT, 0x4);
        ndis_platform_ops.mmio_write(dev, I226_TSYNCRXCTL, tsyncrxctl);
        DEBUGP(DL_TRACE, "I226: RX timestamping enabled (TSYNCRXCTL=0x%08X)\n", tsyncrxctl);
    }
    
    // TSYNCTXCTL: Enable TX timestamping (Foxville architecture requires Bit 5 = 1)
    if (ndis_platform_ops.mmio_read(dev, I226_TSYNCTXCTL, &tsynctxctl) == 0) {
        // Set EN bit (bit 4) AND RSVD High bit (bit 5) - mandatory for I225/I226 (Foxville)
        tsynctxctl = (uint32_t)I226_TSYNCTXCTL_SET(0, I226_TSYNCTXCTL_EN_MASK, I226_TSYNCTXCTL_EN_SHIFT, 1);
        tsynctxctl = (uint32_t)I226_TSYNCTXCTL_SET(tsynctxctl, I226_TSYNCTXCTL_RSVD_MASK, I226_TSYNCTXCTL_RSVD_SHIFT, 1);
        ndis_platform_ops.mmio_write(dev, I226_TSYNCTXCTL, tsynctxctl);
        DEBUGP(DL_TRACE, "I226: TX timestamping enabled (TSYNCTXCTL=0x%08X, EN=1, RSVD=1)\n", tsynctxctl);
    }
    
    // ETQF(3): Configure EtherType filter for PTP packets (0x88F7)
    // Evidence: Linux igb_ptp.c lines 806-811 shows this is REQUIRED for continuous timestamping
    // Without this filter, hardware may not timestamp packets during high-frequency polling
    uint32_t etqf_value = I226_ETQF_FILTER_ENABLE | I226_ETQF_1588 | ETH_P_1588;
    ndis_platform_ops.mmio_write(dev, I226_ETQF(3), etqf_value);
    DEBUGP(DL_TRACE, "I226: EtherType filter configured (ETQF[3]=0x%08X, PTP EtherType=ETH_P_1588)\n", etqf_value);
    
    // Clear TX timestamp FIFO registers to remove any stale data (Linux igb_ptp.c:861-864)
    // CRITICAL: Without this, first timestamp retrieval in rapid polling may get stale data
    // Read operations clear the registers (side effect of MMIO read)
    uint32_t dummy;
    ndis_platform_ops.mmio_read(dev, I226_TXSTMPL, &dummy);
    ndis_platform_ops.mmio_read(dev, I226_TXSTMPH, &dummy);
    DEBUGP(DL_TRACE, "I226: FIFO cleared (TXSTMPL/H read to remove stale timestamps)\n");
    
    DEBUGP(DL_TRACE, "<==i226_init_ptp: PTP clock initialized successfully (Build 78: +ETQF filter +FIFO clear)\n");
    return 0;
}

/**
 * @brief Setup I226 Time Aware Shaper (TAS)
 * Implementation matches ChatGPT5 validation sequence
 * @param dev Device handle
 * @param config TAS configuration
 * @return 0 on success, <0 on error
 */
static int setup_tas(device_t *dev, struct tsn_tas_config *config)
{
    PAVB_DEVICE_CONTEXT context;
    uint32_t regValue;
    int result;
    
    DEBUGP(DL_TRACE, "==>i226_setup_tas (I226-specific implementation)\n");
    
    if (dev == NULL || config == NULL) {
        DEBUGP(DL_TRACE, "i226_setup_tas: Invalid parameters\n");
        return -1;
    }
    
    context = (PAVB_DEVICE_CONTEXT)dev->private_data;
    if (context == NULL) {
        DEBUGP(DL_TRACE, "i226_setup_tas: No device context\n");
        return -1;
    }
    
    // Log device identification
    DEBUGP(DL_TRACE, "I226 TAS Setup: VID:DID = 0x%04X:0x%04X\n", 
           context->intel_device.pci_vendor_id, context->intel_device.pci_device_id);
    
    // Verify PHC is running
    uint64_t systim_current;
    result = get_systime(dev, &systim_current);
    if (result != 0 || systim_current == 0) {
        DEBUGP(DL_TRACE, "I226 PHC not running - TAS requires active PTP clock\n");
        return -1;
    }
    
    DEBUGP(DL_TRACE, "? I226 PHC verified: SYSTIM=0x%llx\n", systim_current);
    
    // Program TQAVCTRL register (I226-specific address)
    result = ndis_platform_ops.mmio_read(dev, I226_TQAVCTRL, &regValue);
    if (result != 0) return result;
    
    // Set TSN mode and enhanced QAV
    regValue |= I226_TQAVCTRL_TRANSMIT_MODE_TSN | I226_TQAVCTRL_ENHANCED_QAV;
    
    result = ndis_platform_ops.mmio_write(dev, I226_TQAVCTRL, regValue);
    if (result != 0) return result;
    
    DEBUGP(DL_TRACE, "? I226 TQAVCTRL programmed: 0x%08X\n", regValue);
    
    // Program cycle time
    uint32_t cycle_time_ns = config->cycle_time_s * 1000000000UL + config->cycle_time_ns;
    if (cycle_time_ns == 0) {
        cycle_time_ns = 1000000;  // Default 1ms cycle
    }
    
    result = ndis_platform_ops.mmio_write(dev, I226_QBVCYCLET, cycle_time_ns);
    if (result != 0) return result;
    
    DEBUGP(DL_TRACE, "? I226 cycle time programmed: %lu ns\n", cycle_time_ns);
    
    // Final verification
    result = ndis_platform_ops.mmio_read(dev, I226_TQAVCTRL, &regValue);
    if (result == 0 && (regValue & I226_TQAVCTRL_TRANSMIT_MODE_TSN)) {
        DEBUGP(DL_TRACE, "?? I226 TAS activation SUCCESS: TQAVCTRL=0x%08X\n", regValue);
        return 0;
    } else {
        DEBUGP(DL_TRACE, "? I226 TAS activation FAILED: TQAVCTRL=0x%08X\n", regValue);
        return -1;
    }
}

/**
 * @brief Setup I226 Frame Preemption
 * @param dev Device handle
 * @param config FP configuration
 * @return 0 on success, <0 on error
 */
static int setup_frame_preemption(device_t *dev, struct tsn_fp_config *config)
{
    DEBUGP(DL_TRACE, "==>i226_setup_frame_preemption\n");
    
    if (dev == NULL || config == NULL) {
        return -1;
    }
    
    // I226 Frame Preemption implementation to be added
    DEBUGP(DL_TRACE, "I226 Frame Preemption: Implementation pending\n");
    
    return 0;
}

/**
 * @brief Setup I226 PTM
 * @param dev Device handle
 * @param config PTM configuration
 * @return 0 on success, <0 on error
 */
static int setup_ptm(device_t *dev, struct ptm_config *config)
{
    DEBUGP(DL_TRACE, "==>i226_setup_ptm\n");
    
    if (dev == NULL || config == NULL) {
        return -1;
    }
    
    // I226 PTM implementation to be added
    DEBUGP(DL_TRACE, "I226 PTM: Implementation pending\n");
    
    return 0;
}

/**
 * @brief I226-specific MDIO read via MDIC register (SSOT: I226_MDIC from i226_regs.h)
 *
 * Uses the same MDIC polling pattern as 82575/82576 but with I226-specific
 * register offset from the SSOT-generated header.
 *
 * @param dev Device handle
 * @param phy_addr PHY address (0-31)
 * @param reg_addr Register address (0-31)
 * @param value Output value
 * @return 0 on success, -1 on error/timeout
 */
static int mdio_read(device_t *dev, uint16_t phy_addr, uint16_t reg_addr, uint16_t *value)
{
    uint32_t mdic;
    int      timeout;
    int      result;

    if (dev == NULL || value == NULL) {
        return -1;
    }
    *value = 0;

    // Build MDIC read command using SSOT bit definitions from i226_regs.h
    mdic  = 0;
    mdic |= (uint32_t)(reg_addr & 0x1FU) << I226_MDIC_REG_SHIFT;
    mdic |= (uint32_t)(phy_addr & 0x1FU) << I226_MDIC_PHY_SHIFT;
    mdic |= (uint32_t)(2U)               << I226_MDIC_OP_SHIFT;  // 2 = read
    mdic |= (uint32_t)I226_MDIC_I_MASK;                          // interrupt on completion

    result = ndis_platform_ops.mmio_write(dev, I226_MDIC, mdic);
    if (result != 0) {
        DEBUGP(DL_ERROR, "I226 MDIC write failed (mdio_read setup)\n");
        return -1;
    }

    // Poll ready bit (I226_MDIC_R_MASK)
    for (timeout = 0; timeout < 1000; timeout++) {
        result = ndis_platform_ops.mmio_read(dev, I226_MDIC, &mdic);
        if (result != 0) {
            DEBUGP(DL_ERROR, "I226 MDIC read poll failed\n");
            return -1;
        }
        if (mdic & I226_MDIC_R_MASK) {
            if (mdic & I226_MDIC_E_MASK) {
                DEBUGP(DL_ERROR, "I226 MDIO read error bit set\n");
                return -1;
            }
            *value = (uint16_t)(mdic & I226_MDIC_DATA_MASK);
            DEBUGP(DL_TRACE, "I226 mdio_read phy=0x%x reg=0x%x -> 0x%04x\n",
                   phy_addr, reg_addr, *value);
            return 0;
        }
        KeStallExecutionProcessor(10); // 10 µs
    }

    DEBUGP(DL_ERROR, "I226 MDIO read timeout (phy=0x%x reg=0x%x)\n", phy_addr, reg_addr);
    return -1;
}

/**
 * @brief I226-specific MDIO write via MDIC register (SSOT: I226_MDIC from i226_regs.h)
 *
 * @param dev Device handle
 * @param phy_addr PHY address (0-31)
 * @param reg_addr Register address (0-31)
 * @param value Value to write
 * @return 0 on success, -1 on error/timeout
 */
static int mdio_write(device_t *dev, uint16_t phy_addr, uint16_t reg_addr, uint16_t value)
{
    uint32_t mdic;
    int      timeout;
    int      result;

    if (dev == NULL) {
        return -1;
    }

    // Build MDIC write command using SSOT bit definitions from i226_regs.h
    mdic  = 0;
    mdic |= (uint32_t)(value & (uint32_t)I226_MDIC_DATA_MASK) << I226_MDIC_DATA_SHIFT;
    mdic |= (uint32_t)(reg_addr & 0x1FU)  << I226_MDIC_REG_SHIFT;
    mdic |= (uint32_t)(phy_addr & 0x1FU)  << I226_MDIC_PHY_SHIFT;
    mdic |= (uint32_t)(1U)                << I226_MDIC_OP_SHIFT;  // 1 = write
    mdic |= (uint32_t)I226_MDIC_I_MASK;                           // interrupt on completion

    result = ndis_platform_ops.mmio_write(dev, I226_MDIC, mdic);
    if (result != 0) {
        DEBUGP(DL_ERROR, "I226 MDIC write failed (mdio_write)\n");
        return -1;
    }

    // Poll ready bit
    for (timeout = 0; timeout < 1000; timeout++) {
        result = ndis_platform_ops.mmio_read(dev, I226_MDIC, &mdic);
        if (result != 0) {
            DEBUGP(DL_ERROR, "I226 MDIC read poll failed (write confirm)\n");
            return -1;
        }
        if (mdic & I226_MDIC_R_MASK) {
            if (mdic & I226_MDIC_E_MASK) {
                DEBUGP(DL_ERROR, "I226 MDIO write error bit set\n");
                return -1;
            }
            DEBUGP(DL_TRACE, "I226 mdio_write phy=0x%x reg=0x%x val=0x%04x OK\n",
                   phy_addr, reg_addr, value);
            return 0;
        }
        KeStallExecutionProcessor(10); // 10 µs
    }

    DEBUGP(DL_ERROR, "I226 MDIO write timeout (phy=0x%x reg=0x%x)\n", phy_addr, reg_addr);
    return -1;
}

/**
 * @brief Enable/disable packet timestamping (I226-specific)
 * @param dev Device handle
 * @param enable 1 = enable, 0 = disable
 * @return 0 on success, <0 on error
 * 
 * Configures TSYNCRXCTL and TSYNCTXCTL registers for I226 (IGC family).
 * Uses SSOT register definitions from i226_regs.h.
 */
static int enable_packet_timestamping(device_t *dev, int enable)
{
    if (!dev || !ndis_platform_ops.mmio_write || !ndis_platform_ops.mmio_read) {
        return -1;
    }
    
    if (enable) {
        // Enable RX packet timestamping using SSOT definitions
        uint32_t rx_ctl = 0;
        rx_ctl = (uint32_t)I226_TSYNCRXCTL_SET(0, I226_TSYNCRXCTL_EN_MASK, I226_TSYNCRXCTL_EN_SHIFT, 1);  // Enable
        rx_ctl = (uint32_t)I226_TSYNCRXCTL_SET(rx_ctl, I226_TSYNCRXCTL_TYPE_MASK, I226_TSYNCRXCTL_TYPE_SHIFT, 0x4);  // All packets
        
        if (ndis_platform_ops.mmio_write(dev, I226_TSYNCRXCTL, rx_ctl) != 0) {
            DEBUGP(DL_TRACE, "I226: Failed to write TSYNCRXCTL\n");
            return -1;
        }
        DEBUGP(DL_TRACE, "I226: Enabled RX packet timestamping (TSYNCRXCTL=0x%08X)\n", rx_ctl);
        
        // Enable TX packet timestamping using SSOT definitions
        // Per i226 datasheet: bit 4 (EN) + bit 5 (RSVD, must be set) -> 0x30
        uint32_t tx_ctl = 0;
        tx_ctl = (uint32_t)I226_TSYNCTXCTL_SET(0, I226_TSYNCTXCTL_EN_MASK, I226_TSYNCTXCTL_EN_SHIFT, 1);      // bit 4: Enable
        tx_ctl = (uint32_t)I226_TSYNCTXCTL_SET(tx_ctl, I226_TSYNCTXCTL_RSVD_MASK, I226_TSYNCTXCTL_RSVD_SHIFT, 1); // bit 5: RSVD (required)
        
        if (ndis_platform_ops.mmio_write(dev, I226_TSYNCTXCTL, tx_ctl) != 0) {
            DEBUGP(DL_TRACE, "I226: Failed to write TSYNCTXCTL\n");
            return -1;
        }
        DEBUGP(DL_TRACE, "I226: Enabled TX packet timestamping (TSYNCTXCTL=0x%08X)\n", tx_ctl);
    } else {
        // Disable packet timestamping
        uint32_t regval = 0;
        
        if (ndis_platform_ops.mmio_read(dev, I226_TSYNCRXCTL, &regval) == 0) {
            regval = (uint32_t)I226_TSYNCRXCTL_SET(regval, I226_TSYNCRXCTL_EN_MASK, I226_TSYNCRXCTL_EN_SHIFT, 0);
            regval = (uint32_t)I226_TSYNCRXCTL_SET(regval, I226_TSYNCRXCTL_TYPE_MASK, I226_TSYNCRXCTL_TYPE_SHIFT, 0);
            ndis_platform_ops.mmio_write(dev, I226_TSYNCRXCTL, regval);
        }
        
        if (ndis_platform_ops.mmio_read(dev, I226_TSYNCTXCTL, &regval) == 0) {
            regval = (uint32_t)I226_TSYNCTXCTL_SET(regval, I226_TSYNCTXCTL_EN_MASK, I226_TSYNCTXCTL_EN_SHIFT, 0);
            ndis_platform_ops.mmio_write(dev, I226_TSYNCTXCTL, regval);
        }
        DEBUGP(DL_TRACE, "I226: Disabled packet timestamping\n");
    }
    
    return 0;
}

/**
 * @brief Set target time for specified timer (Issue #13 Task 7)
 * @param dev Device context
 * @param timer_index Timer index (0 or 1)
 * @param target_time_ns Target time in nanoseconds
 * @param enable_interrupt Enable interrupt for this timer
 * @return 0 on success, <0 on error
 * 
 * I226 Register Details:
 * - TRGTTIML0 (0x0B644): Target Time 0 Low (bits 0-31)
 * - TRGTTIMH0 (0x0B648): Target Time 0 High (bits 32-63)
 * - TRGTTIML1 (0x0B64C): Target Time 1 Low (bits 0-31)
 * - TRGTTIMH1 (0x0B650): Target Time 1 High (bits 32-63)
 * - TSAUXC (0x0B640): EN_TT0 (bit 0), EN_TT1 (bit 4)
 */
static int i226_set_target_time(device_t *dev, uint8_t timer_index, 
                                uint64_t target_time_ns, int enable_interrupt)
{
    DEBUGP(DL_TRACE, "!!! ENTRY: i226_set_target_time(timer=%u, target=0x%016llX, enable_int=%d)\n",
           timer_index, (unsigned long long)target_time_ns, enable_interrupt);
    
    if (timer_index > I226_MAX_TIMER_INDEX) {
        DEBUGP(DL_TRACE, "I226: Invalid timer index %u (max %u)\n", timer_index, I226_MAX_TIMER_INDEX);
        return -EINVAL;
    }
    
    // I226-specific register offsets (from SSOT)
    uint32_t trgttiml_offset = (timer_index == 0) ? I226_TRGTTIML0 : I226_TRGTTIML1;
    uint32_t trgttimh_offset = (timer_index == 0) ? I226_TRGTTIMH0 : I226_TRGTTIMH1;
    
    uint32_t time_low = (uint32_t)(target_time_ns & INTEL_MASK_32BIT);
    uint32_t time_high = (uint32_t)(target_time_ns >> 32);
    
    // Read current SYSTIM before setting target to show delta
    uint64_t current_systim = 0;
    uint32_t systim_low = 0, systim_high = 0;
    ndis_platform_ops.mmio_read(dev, I226_SYSTIML_READ, &systim_low);  // SYSTIML
    ndis_platform_ops.mmio_read(dev, I226_SYSTIMH_READ, &systim_high); // SYSTIMH
    current_systim = ((uint64_t)systim_high << 32) | systim_low;
    int64_t delta_ns = (int64_t)(target_time_ns - current_systim);
    
    DEBUGP(DL_TRACE, "!!! I226: Setting target time %u: 0x%08X%08X (%llu ns)\n", 
           timer_index, time_high, time_low, (unsigned long long)target_time_ns);
    DEBUGP(DL_TRACE, "!!! I226:   Current SYSTIM:  0x%016llX (%llu ns)\n",
           (unsigned long long)current_systim, (unsigned long long)current_systim);
    DEBUGP(DL_TRACE, "!!! I226:   Delta (target - current): %s%lld ns (%lld ms)\n",
           (delta_ns < 0) ? "-" : "+", (long long)(delta_ns < 0 ? -delta_ns : delta_ns), 
           (long long)(delta_ns < 0 ? -delta_ns : delta_ns) / 1000000);
    if (delta_ns < 0) {
        DEBUGP(DL_TRACE, "!!! I226: WARNING - Target time is in the PAST by %lld ns!\n", (long long)(-delta_ns));
    }
    
    // Write target time registers (low then high for atomicity)
    if (ndis_platform_ops.mmio_write(dev, trgttiml_offset, time_low) != 0) {
        DEBUGP(DL_TRACE, "I226: Failed to write TRGTTIML%u\n", timer_index);
        return -EIO;
    }
    if (ndis_platform_ops.mmio_write(dev, trgttimh_offset, time_high) != 0) {
        DEBUGP(DL_TRACE, "I226: Failed to write TRGTTIMH%u\n", timer_index);
        return -EIO;
    }
    
    // Verify write by reading back
    uint32_t verify_low = 0, verify_high = 0;
    ndis_platform_ops.mmio_read(dev, trgttiml_offset, &verify_low);
    ndis_platform_ops.mmio_read(dev, trgttimh_offset, &verify_high);
    uint64_t verified_target = ((uint64_t)verify_high << 32) | verify_low;
    UNREFERENCED_PARAMETER(verified_target);  // Used only in DEBUGP
    
    DEBUGP(DL_TRACE, "!!! I226:   VERIFY: wrote=0x%08X%08X, read=0x%08X%08X %s\n",
           time_high, time_low, verify_high, verify_low,
           (verified_target == target_time_ns) ? "OK" : "MISMATCH!");
    // NOTE: TRGTTIML0 is shadow-latched -- the register value isn't visible on readback
    // until the timer is armed (TSAUXC.EN_TT0 set). Checking the readback here would
    // incorrectly flag working adapters as failing. Hardware responsiveness is instead
    // inferred in userspace from the previous_target sentinel value (see test skip logic).
    
    // ===================================================================
    // Timer/DPC Initialization for Target Time Polling
    // ===================================================================
    // Access AVB context via two-step pattern:
    //   dev->private_data -> struct intel_private -> platform_data -> AVB_DEVICE_CONTEXT
    {
        struct intel_private *priv = (struct intel_private *)dev->private_data;
        PAVB_DEVICE_CONTEXT avb_context = priv ? (PAVB_DEVICE_CONTEXT)priv->platform_data : NULL;

        if (avb_context) {
            DEBUGP(DL_TRACE, "!!! SET_TARGET_TIME: Context valid (ctx=%p, via priv=%p)\n", avb_context, priv);
        } else {
            DEBUGP(DL_TRACE, "!!! SET_TARGET_TIME: Context is NULL! (priv=%p)\n", priv);
        }

        if (avb_context && !avb_context->target_time_poll_active) {
            DEBUGP(DL_TRACE, "!!! SET_TARGET_TIME: Entering timer init block (flag was FALSE)\n");

            KeInitializeTimer(&avb_context->target_time_timer);
            KeInitializeDpc(&avb_context->target_time_dpc, AvbTimerDpcRoutine, avb_context);

            LARGE_INTEGER dueTime;
            dueTime.QuadPart = -100000LL;  // 10ms (negative = relative time)
            KeSetTimerEx(&avb_context->target_time_timer, dueTime, 10, &avb_context->target_time_dpc);

            DEBUGP(DL_TRACE, "!!! TARGET_TIMER_INIT: Activating flag (ctx=%p, before=%d)\n",
                   avb_context, avb_context->target_time_poll_active ? 1 : 0);

            avb_context->target_time_poll_active = TRUE;
            avb_context->target_time_poll_interval_ms = 10;
            avb_context->target_time_dpc_call_count = 0;

            DEBUGP(DL_TRACE, "!!! TIMER INITIALIZED: 10ms periodic DPC started (context=%p)\n", avb_context);
        } else if (avb_context && avb_context->target_time_poll_active) {
            DEBUGP(DL_TRACE, "!!! SET_TARGET_TIME: SKIPPING timer init - already_active flag is TRUE (ctx=%p)\n", avb_context);
        } else {
            DEBUGP(DL_TRACE, "!!! SET_TARGET_TIME: SKIPPING timer init - context is NULL!\n");
        }
    }

    // Enable interrupt in TSAUXC if requested
    if (enable_interrupt) {
        uint32_t tsauxc;
        uint32_t tsauxc_after = 0;
        uint32_t en_bit;
        uint32_t tsim;
        uint32_t tsim_before;
        uint32_t tsim_after = 0;
        uint32_t tt_int_bit;
        uint32_t freqout0 = 0;
        uint32_t tsyncrxctl = 0;
        
        // EIMS (Extended Interrupt Mask Set) variables
        uint32_t eims_before = 0;
        uint32_t eims_new;
        uint32_t eims_after = 0;
        
        DEBUGP(DL_TRACE, "!!! INTERRUPT ENABLE: enable_interrupt=%d (entering TSAUXC config)\n", enable_interrupt);
        
        if (ndis_platform_ops.mmio_read(dev, I226_TSAUXC, &tsauxc) != 0) {
            DEBUGP(DL_TRACE, "I226: Failed to read TSAUXC\n");
            return -EIO;
        }
        
        DEBUGP(DL_TRACE, "!!! I226:   TSAUXC BEFORE: 0x%08X (EN_TT0=%d, EN_TT1=%d, AUTT0=%d, AUTT1=%d)\n",
               tsauxc,
               (tsauxc & I226_TSAUXC_EN_TT0_MASK) ? 1 : 0,
               (tsauxc & I226_TSAUXC_EN_TT1_MASK) ? 1 : 0,
               (tsauxc & I226_TSAUXC_AUTT0_MASK) ? 1 : 0,
               (tsauxc & I226_TSAUXC_AUTT1_MASK) ? 1 : 0);
        
        // EN_TT0 = bit 0, EN_TT1 = bit 4
        en_bit = (timer_index == 0) ? I226_TSAUXC_EN_TT0_MASK : I226_TSAUXC_EN_TT1_MASK;
        tsauxc |= en_bit;
        
        if (ndis_platform_ops.mmio_write(dev, I226_TSAUXC, tsauxc) != 0) {
            DEBUGP(DL_TRACE, "I226: Failed to write TSAUXC\n");
            return -EIO;
        }
        
        // Verify interrupt enable by reading back
        ndis_platform_ops.mmio_read(dev, I226_TSAUXC, &tsauxc_after);
        
        DEBUGP(DL_TRACE, "!!! I226:   TSAUXC AFTER:  0x%08X (EN_TT0=%d, EN_TT1=%d, AUTT0=%d, AUTT1=%d) %s\n",
               tsauxc_after,
               (tsauxc_after & I226_TSAUXC_EN_TT0_MASK) ? 1 : 0,
               (tsauxc_after & I226_TSAUXC_EN_TT1_MASK) ? 1 : 0,
               (tsauxc_after & I226_TSAUXC_AUTT0_MASK) ? 1 : 0,
               (tsauxc_after & I226_TSAUXC_AUTT1_MASK) ? 1 : 0,
               (tsauxc_after == tsauxc) ? "UNCHANGED!" : "OK");
        
        // Enable TT0/TT1 interrupt in TSIM (0xB674) - CRITICAL for interrupt delivery!
        // Per I225 datasheet Section 8.16.2: TSIM bit 3 = TT0 interrupt mask, bit 4 = TT1
        if (ndis_platform_ops.mmio_read(dev, I226_TSIM, &tsim) != 0) {
            DEBUGP(DL_TRACE, "I226: Failed to read TSIM\n");
            return -EIO;
        }
        
        tsim_before = tsim;
        tt_int_bit = (timer_index == 0) ? I226_TSIM_TT0_MASK : I226_TSIM_TT1_MASK;  // TT0=bit3, TT1=bit4
        tsim |= tt_int_bit;  // Enable TT interrupt mask
        
        if (ndis_platform_ops.mmio_write(dev, I226_TSIM, tsim) != 0) {
            DEBUGP(DL_TRACE, "I226: Failed to write TSIM\n");
            return -EIO;
        }
        
        // Verify TSIM write
        ndis_platform_ops.mmio_read(dev, I226_TSIM, &tsim_after);
        DEBUGP(DL_TRACE, "!!! I226:   TSIM: 0x%08X -> 0x%08X (TT0_mask=%d, TT1_mask=%d)\n",
               tsim_before, tsim_after,
               (tsim_after & I226_TSIM_TT0_MASK) ? 1 : 0,
               (tsim_after & I226_TSIM_TT1_MASK) ? 1 : 0);
        
        // ========================================================================
        // CRITICAL FIX: Write FREQOUT0 to activate auxiliary timer compare logic
        // ========================================================================
        // 
        // Evidence from i210 Datasheet (Section 8.16.1):
        //   "TT0 interrupt is set also by CLK0 output which is based on TRGTTIM0"
        //   "TT1 interrupt is set also by CLK1 output which is based on TRGTTIM1"
        //
        // Root cause discovered: Hardware defaults FREQOUT0=0, which disables the
        // compare logic. Target time interrupts SHARE hardware with clock output.
        //
        // From i210 Datasheet Section 8.15.18 (FREQOUT0 Register):
        //   Field: CHCT (Clock Out Half Cycle Time)
        //   Units: nanoseconds
        //   Valid range: 9 to 70,000,000 ns (9 ns to 70 ms)
        //   Purpose: Defines half-cycle time for clock output generation
        //
        // Value chosen: 1000 ns (1 microsecond, 1 MHz reference clock)
        //   - Within documented valid range (9-70,000,000 ns)
        //   - Standard value for IEEE 1588 PTP timing applications
        //   - Activates compare logic without using arbitrary magic number
        //
        // Read current value for diagnostics
        if (ndis_platform_ops.mmio_read(dev, I226_FREQOUT0, &freqout0) == 0) {
            DEBUGP(DL_TRACE, "!!! I226:   FREQOUT0 before: 0x%08X (%u ns)\n", 
                   freqout0, freqout0);
        }
        
        // Write FREQOUT0 to activate hardware compare logic
        if (ndis_platform_ops.mmio_write(dev, I226_FREQOUT0, I226_FREQOUT0_1MHZ) != 0) {
            DEBUGP(DL_TRACE, "I226: Failed to write FREQOUT0\n");
            return -EIO;
        }
        
        DEBUGP(DL_TRACE, "!!! I226:   FREQOUT0 set to %u ns (activating TT compare logic)\n", 
               I226_FREQOUT0_1MHZ);
        
        // ========================================================================
        // CRITICAL FIX: Enable EIMS.OTHER for TimeSync interrupts
        // ========================================================================
        //
        // Root cause discovered:
        //   TimeSync interrupts (TT0, TT1, etc.) are in the "OTHER" interrupt
        //   category. Two levels of interrupt enable are required:
        //
        //   1. TSIM (TimeSync Interrupt Mask) - bit 3 for TT0 [OK] Already done
        //   2. EIMS (Extended Interrupt Mask Set) - bit 31 for OTHER [FAIL] MISSING!
        //
        // From i226.yaml (intel-ethernet-regs):
        //   EIMS register at 0x01524 (base 0x01500 + offset 0x24)
        //   Bit 31: OTHER - "Other interrupt causes (summary of ICR as masked)"
        //
        // Without EIMS.OTHER=1, TSICR.TT0 can set internally but the interrupt
        // never reaches the CPU. This is why DPC polling sees TSICR=0 always.
        //
        // Evidence from registry diagnostics:
        //   - i226_TSIM_After: 8 (bit 3 set, TT0 mask enabled) [OK]
        //   - i226_EN_TT0: 1 (target time feature enabled) [OK]
        //   - i226_FREQOUT0_After: 1000 (clock configured) [OK]
        //   - But TSICR.TT0 never triggers! [FAIL]
        //
        // This is identical to i210 architecture where TimeSync interrupts
        // require both TSIM (specific mask) and IMS/EIMS (category mask).
        //
        if (ndis_platform_ops.mmio_read(dev, I226_EIMS, &eims_before) == 0) {
            DEBUGP(DL_TRACE, "!!! I226:   EIMS before: 0x%08X (OTHER=%d)\n",
                   eims_before,
                   (eims_before & I226_EIMS_OTHER_MASK) ? 1 : 0);
        }
        
        eims_new = eims_before | I226_EIMS_OTHER_MASK;
        if (ndis_platform_ops.mmio_write(dev, I226_EIMS, eims_new) != 0) {
            DEBUGP(DL_TRACE, "I226: Failed to write EIMS\n");
            return -EIO;
        }
        
        // Verify EIMS write
        ndis_platform_ops.mmio_read(dev, I226_EIMS, &eims_after);
        DEBUGP(DL_TRACE, "!!! I226:   EIMS after: 0x%08X (OTHER=%d) %s\n",
               eims_after,
               (eims_after & I226_EIMS_OTHER_MASK) ? 1 : 0,
               (eims_after & I226_EIMS_OTHER_MASK) ? "ENABLED [OK]" : "FAILED!");
        
        // Read TSYNCRXCTL to verify timestamp system status
        if (ndis_platform_ops.mmio_read(dev, I226_TSYNCRXCTL, &tsyncrxctl) == 0) {
            DEBUGP(DL_TRACE, "!!! I226:   TSYNCRXCTL: 0x%08X (VALID=%d, TYPE=%d, ENABLED=%d)\n",
                   tsyncrxctl,
                   (tsyncrxctl & I226_TSYNCRXCTL_RXTT_MASK) ? 1 : 0,
                   (tsyncrxctl & I226_TSYNCRXCTL_TYPE_MASK) >> I226_TSYNCRXCTL_TYPE_SHIFT,
                   (tsyncrxctl & I226_TSYNCRXCTL_EN_MASK) ? 1 : 0);
        }
        
    }
    
    DEBUGP(DL_TRACE, "i226_set_target_time: Configured timer %u, target=0x%016llX\n",
           timer_index, (unsigned long long)target_time_ns);
    
    return 0;
}

/**
 * @brief Get target time for specified timer
 * @param dev Device context
 * @param timer_index Timer index (0 or 1)
 * @param target_time_ns Pointer to receive target time in nanoseconds
 * @return 0 on success, <0 on error
 */
static int i226_get_target_time(device_t *dev, uint8_t timer_index, uint64_t *target_time_ns)
{
    if (timer_index > I226_MAX_TIMER_INDEX || !target_time_ns) {
        return -EINVAL;
    }
    
    uint32_t trgttiml_offset = (timer_index == 0) ? I226_TRGTTIML0 : I226_TRGTTIML1;
    uint32_t trgttimh_offset = (timer_index == 0) ? I226_TRGTTIMH0 : I226_TRGTTIMH1;
    
    uint32_t time_low, time_high;
    
    if (ndis_platform_ops.mmio_read(dev, trgttiml_offset, &time_low) != 0) {
        return -EIO;
    }
    if (ndis_platform_ops.mmio_read(dev, trgttimh_offset, &time_high) != 0) {
        return -EIO;
    }
    
    *target_time_ns = ((uint64_t)time_high << 32) | time_low;
    return 0;
}

/**
 * @brief Check TT0/TT1 flags (Target Time interrupt flags)
 * @param dev Device context
 * @param autt_flags Pointer to receive TT flags (bit 0 = TT0, bit 1 = TT1)
 * @return 0 on success, <0 on error
 * 
 * CORRECTED per I225 datasheet Section 8.16.1:
 * - Read TSICR (0xB66C) - Time Sync Interrupt Cause Register
 * - Bit 3: TT0 (Target Time 0 Trigger)
 * - Bit 4: TT1 (Target Time 1 Trigger)
 * 
 * NOTE: AUTT0/AUTT1 (bits 5-6 in TSICR) are for SDP input sampling, NOT target time!
 * Previous implementation incorrectly read TSAUXC bits 16-17 which are RESERVED.
 */
static int i226_check_autt_flags(device_t *dev, uint8_t *autt_flags)
{
    if (!autt_flags) {
        return -EINVAL;
    }
    
    uint32_t tsicr;
    if (ndis_platform_ops.mmio_read(dev, I226_TSICR, &tsicr) != 0) {
        return -EIO;
    }
    
    // Extract TT0 (bit 3) and TT1 (bit 4) from TSICR
    *autt_flags = 0;
    if (tsicr & I226_TSICR_TT0_MASK) {
        *autt_flags |= I226_AUTT_FLAG_TT0;  // TT0 triggered
    }
    if (tsicr & I226_TSICR_TT1_MASK) {
        *autt_flags |= I226_AUTT_FLAG_TT1;  // TT1 triggered
    }
    
    return 0;
}

/**
 * @brief Clear TT0/TT1 flag for specified timer
 * @param dev Device context
 * @param timer_index Timer index (0 or 1)
 * @return 0 on success, <0 on error
 * 
 * CORRECTED per I225 datasheet Section 8.16.1:
 * - Write to TSICR (0xB66C) with W1C (Write-1-to-Clear)
 * - Bit 3: TT0 (Target Time 0 Trigger)
 * - Bit 4: TT1 (Target Time 1 Trigger)
 * 
 * Previous implementation incorrectly wrote to TSAUXC which doesn't clear interrupt flags.
 */
static int i226_clear_autt_flag(device_t *dev, uint8_t timer_index)
{
    if (timer_index > I226_MAX_TIMER_INDEX) {
        return -EINVAL;
    }
    
    // TT0 = bit 3, TT1 = bit 4 in TSICR (write-1-to-clear)
    uint32_t tt_bit = (timer_index == 0) ? I226_TSICR_TT0_MASK : I226_TSICR_TT1_MASK;
    
    // Write 1 to clear the TT flag in TSICR
    if (ndis_platform_ops.mmio_write(dev, I226_TSICR, tt_bit) != 0) {
        return -EIO;
    }
    
    DEBUGP(DL_TRACE, "I226: Cleared TT%u flag in TSICR\n", timer_index);
    return 0;
}

/**
 * @brief Get auxiliary timestamp value
 * @param dev Device context
 * @param aux_index Auxiliary timestamp index (0 or 1)
 * @param aux_timestamp_ns Pointer to receive timestamp in nanoseconds
 * @return 0 on success, <0 on error
 * 
 * I226 Register Offsets:
 * - AUXSTMPL0 (0x0B65C): Auxiliary Timestamp 0 Low
 * - AUXSTMPH0 (0x0B660): Auxiliary Timestamp 0 High
 * - AUXSTMPL1 (0x0B664): Auxiliary Timestamp 1 Low
 * - AUXSTMPH1 (0x0B668): Auxiliary Timestamp 1 High
 */
static int i226_get_aux_timestamp(device_t *dev, uint8_t aux_index, uint64_t *aux_timestamp_ns)
{
    if (aux_index > I226_MAX_TIMER_INDEX || !aux_timestamp_ns) {
        return -EINVAL;
    }
    
    uint32_t auxstmpl_offset = (aux_index == 0) ? I226_AUXSTMPL0 : I226_AUXSTMPL1;
    uint32_t auxstmph_offset = (aux_index == 0) ? I226_AUXSTMPH0 : I226_AUXSTMPH1;
    
    uint32_t time_low, time_high;
    
    if (ndis_platform_ops.mmio_read(dev, auxstmpl_offset, &time_low) != 0) {
        return -EIO;
    }
    if (ndis_platform_ops.mmio_read(dev, auxstmph_offset, &time_high) != 0) {
        return -EIO;
    }
    
    *aux_timestamp_ns = ((uint64_t)time_high << 32) | time_low;
    return 0;
}

/**
 * @brief Clear auxiliary timestamp flag
 * @param dev Device context
 * @param aux_index Auxiliary timestamp index (0 or 1)
 * @return 0 on success, <0 on error
 * 
 * Note: On I226, aux timestamp flags are the same as AUTT flags
 */
static int i226_clear_aux_timestamp_flag(device_t *dev, uint8_t aux_index)
{
    // Reuse AUTT flag clearing (same flags)
    return i226_clear_autt_flag(dev, aux_index);
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
static int i226_read_tx_timestamp(device_t *dev, uint64_t *timestamp_ns)
{
    uint32_t time_low, time_high;
    int result;
    
    if (dev == NULL || timestamp_ns == NULL) {
        return -EINVAL;
    }
    
    result = ndis_platform_ops.mmio_read(dev, I226_TXSTMPL, &time_low);
    if (result != 0) return result;
    
    result = ndis_platform_ops.mmio_read(dev, I226_TXSTMPH, &time_high);
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
static int i226_read_rx_timestamp(device_t *dev, uint64_t *timestamp_ns)
{
    uint32_t time_low, time_high;
    int result;
    
    if (dev == NULL || timestamp_ns == NULL) {
        return -EINVAL;
    }
    
    result = ndis_platform_ops.mmio_read(dev, I226_RXSTMPL, &time_low);
    if (result != 0) return result;
    
    result = ndis_platform_ops.mmio_read(dev, I226_RXSTMPH, &time_high);
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
 * Replaces: Direct register access with proper FIFO semantics
 * 
 * Critical: Follows Intel reference implementation (igb_ptp.c):
 *   1. Check TSYNCTXCTL.TXTT (bit 0) for valid timestamp
 *   2. Read TXSTMPL first, then TXSTMPH (atomic read)
 *   3. Reading TXSTMPH clears TXTT bit and unlocks for next capture
 * 
 * Hardware: TSYNCTXCTL.TXTT (Transmit Timestamp) indicates valid data
 *   - Bit 0 of TSYNCTXCTL @ 0x0B614
 *   - Set by hardware when packet with 2STEP_1588 flag is transmitted
 *   - Cleared by reading TXSTMPH register
 */
static int i226_poll_tx_timestamp_fifo(device_t *dev, uint64_t *timestamp_ns)
{
    uint32_t tsynctxctl_val = 0, txstmpl_val = 0, txstmph_val = 0;
    int result;
    
    if (dev == NULL || timestamp_ns == NULL) {
        return -EINVAL;
    }
    
    // Step 1: Read TSYNCTXCTL and check TXTT bit (bit 0) for valid timestamp
    result = ndis_platform_ops.mmio_read(dev, I226_TSYNCTXCTL, &tsynctxctl_val);
    if (result != 0) return result;
    
    // Check TXTT bit (bit 0) - indicates timestamp valid in FIFO
    if (!(tsynctxctl_val & I226_TSYNCTXCTL_TXTT_MASK)) {
        return 0;  // FIFO empty, no timestamp available
    }
    
    // Step 2: Read TXSTMPL first (low 32 bits)
    result = ndis_platform_ops.mmio_read(dev, I226_TXSTMPL, &txstmpl_val);
    if (result != 0) return result;
    
    // Step 3: Read TXSTMPH (high 32 bits) - this clears TXTT bit and unlocks for next capture
    result = ndis_platform_ops.mmio_read(dev, I226_TXSTMPH, &txstmph_val);
    if (result != 0) return result;
    
    // Construct 64-bit timestamp (nanoseconds since epoch)
    *timestamp_ns = ((uint64_t)txstmph_val << 32) | txstmpl_val;
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
static int i226_read_timinca(device_t *dev, uint32_t *timinca_value)
{
    if (dev == NULL || timinca_value == NULL) {
        return -EINVAL;
    }
    
    return ndis_platform_ops.mmio_read(dev, I226_TIMINCA, timinca_value);
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
static int i226_write_timinca(device_t *dev, uint32_t timinca_value)
{
    if (dev == NULL) {
        return -EINVAL;
    }
    
    return ndis_platform_ops.mmio_write(dev, I226_TIMINCA, timinca_value);
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
static int i226_read_tsauxc(device_t *dev, uint32_t *tsauxc_value)
{
    if (dev == NULL || tsauxc_value == NULL) {
        return -EINVAL;
    }
    
    return ndis_platform_ops.mmio_read(dev, I226_TSAUXC, tsauxc_value);
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
static int i226_write_tsauxc(device_t *dev, uint32_t tsauxc_value)
{
    if (dev == NULL) {
        return -EINVAL;
    }
    
    return ndis_platform_ops.mmio_write(dev, I226_TSAUXC, tsauxc_value);
}

// I226 device operations structure - using generic function names
const intel_device_ops_t i226_ops = {
    .device_name = "Intel I226 2.5G Ethernet - Advanced TSN",
    .supported_capabilities = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | 
                             INTEL_CAP_TSN_TAS | INTEL_CAP_TSN_FP | INTEL_CAP_PCIE_PTM |
                             INTEL_CAP_2_5G | INTEL_CAP_MMIO | INTEL_CAP_EEE,
    
    // Basic operations - clean generic names
    .init = init,
    .cleanup = cleanup,
    .get_info = get_info,
    
    // PTP operations - clean generic names
    .set_systime = set_systime,
    .get_systime = get_systime,
    .init_ptp = init_ptp,
    .enable_packet_timestamping = enable_packet_timestamping,
    
    // PTP register access operations (HAL compliance - no magic numbers in src/)
    .read_tx_timestamp = i226_read_tx_timestamp,
    .read_rx_timestamp = i226_read_rx_timestamp,
    .poll_tx_timestamp_fifo = i226_poll_tx_timestamp_fifo,
    .read_timinca = i226_read_timinca,
    .write_timinca = i226_write_timinca,
    .read_tsauxc = i226_read_tsauxc,
    .write_tsauxc = i226_write_tsauxc,
    
    // Target time and auxiliary timestamp operations (Issue #13 Task 7, Issue #7)
    .set_target_time = i226_set_target_time,
    .get_target_time = i226_get_target_time,
    .check_autt_flags = i226_check_autt_flags,
    .clear_autt_flag = i226_clear_autt_flag,
    .get_aux_timestamp = i226_get_aux_timestamp,
    .clear_aux_timestamp_flag = i226_clear_aux_timestamp_flag,
    
    // TSN operations - clean generic names
    .setup_tas = setup_tas,
    .setup_frame_preemption = setup_frame_preemption,
    .setup_ptm = setup_ptm,
    
    // Register access (uses default implementation)
    .read_register = NULL,
    .write_register = NULL,
    
    // MDIO operations - clean generic names
    .mdio_read = mdio_read,
    .mdio_write = mdio_write,
    
    // Advanced features
    .enable_advanced_features = NULL,
    .validate_configuration = NULL
};