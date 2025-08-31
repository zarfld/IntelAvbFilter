/*++

Module Name:

    avb_integration_fixed.c

Abstract:

    Intel AVB integration for NDIS filter – unified implementation.
    Provides minimal-context creation (BOUND) immediately on attach so
    enumeration succeeds even if later hardware bring-up fails. Deferred
    initialization promotes BAR_MAPPED and PTP_READY states and accrues
    capabilities.

--*/

#include "precomp.h"
#include "avb_integration.h"
#include "external/intel_avb/lib/intel_windows.h"
#include "external/intel_avb/lib/intel_private.h" /* INTEL_REG_TSAUXC */
#include "intel-ethernet-regs/gen/i210_regs.h"
#include <ntstrsafe.h>

/* Global singleton (assumes one Intel adapter binding) */
PAVB_DEVICE_CONTEXT g_AvbContext = NULL;

/* Forward statics */
static NTSTATUS AvbPerformBasicInitialization(_Inout_ PAVB_DEVICE_CONTEXT Ctx);
static VOID     AvbI210EnsureSystimRunning(_In_ PAVB_DEVICE_CONTEXT ctx);

/* Platform ops wrapper (Intel library selects this) */
static int PlatformInitWrapper(_In_ device_t *dev) { return NT_SUCCESS(AvbPlatformInit(dev)) ? 0 : -1; }
static void PlatformCleanupWrapper(_In_ device_t *dev) { AvbPlatformCleanup(dev); }

const struct platform_ops ndis_platform_ops = {
    PlatformInitWrapper,
    PlatformCleanupWrapper,
    AvbPciReadConfig,
    AvbPciWriteConfig,
    AvbMmioRead,
    AvbMmioWrite,
    AvbMdioRead,
    AvbMdioWrite,
    AvbReadTimestamp
};

/* ======================================================================= */
/**
 * @brief Allocate minimal context and mark BOUND so user-mode can enumerate.
 */
NTSTATUS AvbCreateMinimalContext(
    _In_ PMS_FILTER FilterModule,
    _In_ USHORT VendorId,
    _In_ USHORT DeviceId,
    _Outptr_ PAVB_DEVICE_CONTEXT *OutCtx)
{
    if (!FilterModule || !OutCtx) return STATUS_INVALID_PARAMETER;
    PAVB_DEVICE_CONTEXT ctx = (PAVB_DEVICE_CONTEXT)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(AVB_DEVICE_CONTEXT), FILTER_ALLOC_TAG);
    if (!ctx) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(ctx, sizeof(*ctx));
    ctx->filter_instance = FilterModule;
    ctx->intel_device.pci_vendor_id = VendorId;
    ctx->intel_device.pci_device_id = DeviceId;
    ctx->intel_device.device_type   = AvbGetIntelDeviceType(DeviceId);
    ctx->hw_state = AVB_HW_BOUND;
    g_AvbContext = ctx;
    *OutCtx = ctx;
    DEBUGP(DL_INFO, "AVB minimal context created VID=0x%04X DID=0x%04X state=%s\n", VendorId, DeviceId, AvbHwStateName(ctx->hw_state));
    return STATUS_SUCCESS;
}

/**
 * @brief Attempt full HW bring-up (intel_init + MMIO sanity + optional PTP for i210).
 *        Failure is non-fatal; enumeration remains with baseline capabilities.
 */
NTSTATUS AvbBringUpHardware(_Inout_ PAVB_DEVICE_CONTEXT Ctx)
{
    if (!Ctx) return STATUS_INVALID_PARAMETER;
    
    // Always set baseline capabilities based on device type first
    ULONG baseline_caps = 0;
    switch (Ctx->intel_device.device_type) {
        case INTEL_DEVICE_I210:
            baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_MMIO;
            break;
        case INTEL_DEVICE_I217:
            baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_MMIO;
            break;
        case INTEL_DEVICE_I219:
            baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_MMIO | INTEL_CAP_MDIO;
            break;
        case INTEL_DEVICE_I225:
            baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_TSN_TAS | 
                           INTEL_CAP_TSN_FP | INTEL_CAP_PCIe_PTM | INTEL_CAP_2_5G | INTEL_CAP_MMIO;
            break;
        case INTEL_DEVICE_I226:
            baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_TSN_TAS | 
                           INTEL_CAP_TSN_FP | INTEL_CAP_PCIe_PTM | INTEL_CAP_2_5G | INTEL_CAP_MMIO | INTEL_CAP_EEE;
            break;
        default:
            baseline_caps = INTEL_CAP_MMIO;
            break;
    }
    
    // Set baseline capabilities even if hardware init fails
    Ctx->intel_device.capabilities = baseline_caps;
    DEBUGP(DL_INFO, "?? AvbBringUpHardware: Set baseline capabilities 0x%08X for device type %d\n", 
           baseline_caps, Ctx->intel_device.device_type);
    
    NTSTATUS status = AvbPerformBasicInitialization(Ctx);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_WARN, "?? AvbBringUpHardware: basic init failed 0x%08X (keeping baseline capabilities=0x%08X)\n", 
               status, baseline_caps);
        // Don't return error - allow enumeration with baseline capabilities
        return STATUS_SUCCESS;
    }
    
    if (Ctx->intel_device.device_type == INTEL_DEVICE_I210 && Ctx->hw_state >= AVB_HW_BAR_MAPPED) {
        DEBUGP(DL_INFO, "?? Starting I210 PTP initialization...\n");
        AvbI210EnsureSystimRunning(Ctx);
    }
    return STATUS_SUCCESS;
}

/* ======================================================================= */
/**
 * @brief Perform intel_init then verify MMIO by reading CTRL. Promote to BAR_MAPPED.
 */
static NTSTATUS AvbPerformBasicInitialization(_Inout_ PAVB_DEVICE_CONTEXT Ctx)
{
    DEBUGP(DL_INFO, "?? AvbPerformBasicInitialization: Starting for VID=0x%04X DID=0x%04X\n", 
           Ctx->intel_device.pci_vendor_id, Ctx->intel_device.pci_device_id);
    
    if (!Ctx) return STATUS_INVALID_PARAMETER;
    
    if (Ctx->hw_access_enabled) {
        DEBUGP(DL_INFO, "? AvbPerformBasicInitialization: Already initialized, returning success\n");
        return STATUS_SUCCESS;
    }

    /* Step 1: Discover & map BAR0 if not yet mapped */
    if (Ctx->hardware_context == NULL) {
        DEBUGP(DL_INFO, "?? STEP 1: Starting BAR0 discovery and mapping...\n");
        PHYSICAL_ADDRESS bar0 = {0};
        ULONG barLen = 0;
        NTSTATUS ds = AvbDiscoverIntelControllerResources(Ctx->filter_instance, &bar0, &barLen);
        if (!NT_SUCCESS(ds)) {
            DEBUGP(DL_ERROR, "? STEP 1 FAILED: BAR0 discovery failed 0x%08X (cannot map MMIO yet) VID=0x%04X DID=0x%04X\n", 
                   ds, Ctx->intel_device.pci_vendor_id, Ctx->intel_device.pci_device_id);
            return ds; /* propagate */
        }
        DEBUGP(DL_INFO, "? STEP 1a SUCCESS: BAR0 discovered: PA=0x%llx Len=0x%x\n", bar0.QuadPart, barLen);
        
        NTSTATUS ms = AvbMapIntelControllerMemory(Ctx, bar0, barLen);
        if (!NT_SUCCESS(ms)) {
            DEBUGP(DL_ERROR, "? STEP 1b FAILED: BAR0 map failed 0x%08X (MmMapIoSpace)\n", ms);
            return ms;
        }
        DEBUGP(DL_INFO, "? STEP 1b SUCCESS: MMIO mapped (opaque ctx=%p)\n", Ctx->hardware_context);
    } else {
        DEBUGP(DL_INFO, "? STEP 1 SKIPPED: Hardware context already exists (%p)\n", Ctx->hardware_context);
    }

    DEBUGP(DL_INFO, "?? STEP 2: Setting up Intel device structure...\n");
    Ctx->intel_device.private_data = Ctx;
    Ctx->intel_device.capabilities = 0; /* reset published caps */
    DEBUGP(DL_INFO, "? STEP 2 SUCCESS: Device structure prepared\n");

    DEBUGP(DL_INFO, "?? STEP 3: Calling intel_init library function...\n");
    DEBUGP(DL_INFO, "   - VID=0x%04X DID=0x%04X\n", Ctx->intel_device.pci_vendor_id, Ctx->intel_device.pci_device_id);
    if (intel_init(&Ctx->intel_device) != 0) {
        DEBUGP(DL_ERROR, "? STEP 3 FAILED: intel_init failed (library)\n");
        return STATUS_UNSUCCESSFUL;
    }
    DEBUGP(DL_INFO, "? STEP 3 SUCCESS: intel_init completed successfully\n");

    DEBUGP(DL_INFO, "?? STEP 4: MMIO sanity check - reading CTRL register...\n");
    ULONG ctrl = 0xFFFFFFFF;
    if (intel_read_reg(&Ctx->intel_device, I210_CTRL, &ctrl) != 0 || ctrl == 0xFFFFFFFF) {
        DEBUGP(DL_ERROR, "? STEP 4 FAILED: MMIO sanity read failed CTRL=0x%08X (expected != 0xFFFFFFFF)\n", ctrl);
        DEBUGP(DL_ERROR, "   This indicates BAR0 mapping is not working properly\n");
        return STATUS_DEVICE_NOT_READY;
    }
    DEBUGP(DL_INFO, "? STEP 4 SUCCESS: MMIO sanity check passed - CTRL=0x%08X\n", ctrl);

    DEBUGP(DL_INFO, "?? STEP 5: Promoting hardware state to BAR_MAPPED...\n");
    Ctx->intel_device.capabilities |= INTEL_CAP_MMIO;
    if (Ctx->hw_state < AVB_HW_BAR_MAPPED) {
        Ctx->hw_state = AVB_HW_BAR_MAPPED;
        DEBUGP(DL_INFO, "? STEP 5 SUCCESS: HW state -> %s (CTRL=0x%08X)\n", AvbHwStateName(Ctx->hw_state), ctrl);
    }
    Ctx->initialized = TRUE;
    Ctx->hw_access_enabled = TRUE;
    
    DEBUGP(DL_INFO, "?? AvbPerformBasicInitialization: COMPLETE SUCCESS\n");
    DEBUGP(DL_INFO, "   - Final hw_state: %s\n", AvbHwStateName(Ctx->hw_state));
    DEBUGP(DL_INFO, "   - Final capabilities: 0x%08X\n", Ctx->intel_device.capabilities);
    DEBUGP(DL_INFO, "   - Hardware access enabled: %s\n", Ctx->hw_access_enabled ? "YES" : "NO");
    
    return STATUS_SUCCESS;
}

/* ======================================================================= */
/**
 * @brief Configure / verify I210 PHC and promote to PTP_READY when incrementing.
 */
static VOID AvbI210EnsureSystimRunning(_In_ PAVB_DEVICE_CONTEXT ctx)
{
    if (ctx->hw_state < AVB_HW_BAR_MAPPED) {
        DEBUGP(DL_WARN, "AvbI210EnsureSystimRunning: Hardware not ready (state=%s)\n", AvbHwStateName(ctx->hw_state));
        return;
    }
    
    DEBUGP(DL_INFO, "?? AvbI210EnsureSystimRunning: Starting I210 PTP initialization\n");
    
    // Step 1: Check current SYSTIM state
    ULONG lo=0, hi=0;
    if (intel_read_reg(&ctx->intel_device, I210_SYSTIML, &lo) != 0 || 
        intel_read_reg(&ctx->intel_device, I210_SYSTIMH, &hi) != 0) {
        DEBUGP(DL_ERROR, "AvbI210EnsureSystimRunning: Failed to read SYSTIM registers\n");
        return;
    }
    
    DEBUGP(DL_INFO, "PTP init: Initial SYSTIM = 0x%08X%08X\n", hi, lo);
    
    // Step 2: Check if clock is already running by testing increment
    if (lo || hi) {
        DEBUGP(DL_INFO, "PTP init: Clock has non-zero value, testing increment...\n");
        KeStallExecutionProcessor(10000); // 10ms delay
        
        ULONG lo2=0, hi2=0;
        if (intel_read_reg(&ctx->intel_device, I210_SYSTIML, &lo2) == 0 && 
            intel_read_reg(&ctx->intel_device, I210_SYSTIMH, &hi2) == 0 && 
            ((hi2 > hi) || (hi2 == hi && lo2 > lo))) {
            
            DEBUGP(DL_INFO, "? PTP init: Clock already running (0x%08X%08X -> 0x%08X%08X)\n", 
                   hi, lo, hi2, lo2);
            ctx->intel_device.capabilities |= (INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS);
            if (ctx->hw_state < AVB_HW_PTP_READY) { 
                ctx->hw_state = AVB_HW_PTP_READY; 
                DEBUGP(DL_INFO, "HW state -> %s (PTP already running)\n", AvbHwStateName(ctx->hw_state)); 
            }
            return;
        }
    }
    
    DEBUGP(DL_INFO, "?? PTP init: Clock not running, performing full initialization...\n");
    
    // Step 3: CRITICAL FIX - Complete hardware reset sequence per Intel I210 datasheet
    DEBUGP(DL_INFO, "?? PTP init: Step 3A - Disabling PTP completely for clean reset...\n");
    
    // First disable PTP completely to ensure clean state
    ULONG disable_aux = 0x80000000UL; // Set DisableSystime (bit 31), clear PHC (bit 30)
    if (intel_write_reg(&ctx->intel_device, INTEL_REG_TSAUXC, disable_aux) != 0) {
        DEBUGP(DL_ERROR, "PTP init: Failed to disable PTP for reset\n");
        return;
    }
    DEBUGP(DL_INFO, "PTP init: PTP disabled for clean reset (TSAUXC=0x%08X)\n", disable_aux);
    
    // Allow hardware to settle in disabled state
    KeStallExecutionProcessor(100000); // 100ms for hardware to fully disable
    
    // Step 3B: Clear SYSTIM registers to known state
    DEBUGP(DL_INFO, "?? PTP init: Step 3B - Clearing SYSTIM registers...\n");
    if (intel_write_reg(&ctx->intel_device, I210_SYSTIML, 0x00000000) != 0 ||
        intel_write_reg(&ctx->intel_device, I210_SYSTIMH, 0x00000000) != 0) {
        DEBUGP(DL_ERROR, "PTP init: Failed to clear SYSTIM registers\n");
        return;
    }
    DEBUGP(DL_INFO, "PTP init: SYSTIM cleared to 0x0000000000000000\n");
    
    // Step 4: Configure TIMINCA for proper clock frequency BEFORE enabling
    DEBUGP(DL_INFO, "?? PTP init: Step 4 - Configuring TIMINCA for 125MHz operation...\n");
    // I210 uses 8ns per tick for 125MHz system clock (from Intel datasheet section 8.12.6)
    if (intel_write_reg(&ctx->intel_device, I210_TIMINCA, 0x08000000UL) != 0) {
        DEBUGP(DL_ERROR, "PTP init: Failed to write TIMINCA register\n");
        return;
    }
    DEBUGP(DL_INFO, "PTP init: TIMINCA set to 0x08000000 (8ns per tick, 125MHz clock)\n");
    
    // Step 5: Configure timestamp capture BEFORE enabling PTP
    DEBUGP(DL_INFO, "?? PTP init: Step 5 - Configuring timestamp capture...\n");
    // Enable timestamp capture for all packets (RX and TX)
    ULONG rxctl = (1U << I210_TSYNCRXCTL_EN_SHIFT) | (I210_TSYNCRXCTL_TYPE_ALL << I210_TSYNCRXCTL_TYPE_SHIFT);
    ULONG txctl = (1U << I210_TSYNCTXCTL_EN_SHIFT) | (I210_TSYNCTXCTL_TYPE_ALL << I210_TSYNCTXCTL_TYPE_SHIFT);
    
    if (intel_write_reg(&ctx->intel_device, I210_TSYNCRXCTL, rxctl) != 0 ||
        intel_write_reg(&ctx->intel_device, I210_TSYNCTXCTL, txctl) != 0) {
        DEBUGP(DL_ERROR, "PTP init: Failed to configure timestamp capture\n");
        return;
    }
    DEBUGP(DL_INFO, "PTP init: Timestamp capture configured (RX=0x%08X, TX=0x%08X)\n", rxctl, txctl);
    
    // Step 6: CRITICAL - Enable PTP with proper TSAUXC configuration
    DEBUGP(DL_INFO, "?? PTP init: Step 6 - Enabling PTP hardware...\n");
    // Enable PHC (bit 30) and ensure DisableSystime is cleared (bit 31)
    ULONG new_aux = 0x40000000UL; // PHC enabled, DisableSystime cleared, no trigger bits
    if (intel_write_reg(&ctx->intel_device, INTEL_REG_TSAUXC, new_aux) != 0) {
        DEBUGP(DL_ERROR, "PTP init: Failed to enable PTP (TSAUXC write)\n");
        return;
    }
    DEBUGP(DL_INFO, "PTP init: PTP enabled (TSAUXC=0x%08X: PHC enabled, DisableSystime cleared)\n", new_aux);
    
    // Step 7: CRITICAL - Set initial non-zero time to trigger clock start
    DEBUGP(DL_INFO, "?? PTP init: Step 7 - Setting initial time to start clock...\n");
    // Use a significant non-zero value that makes increment detection easier
    ULONG initial_time_lo = 0x10000000UL; // 268 million nanoseconds
    ULONG initial_time_hi = 0x00000000UL;
    
    if (intel_write_reg(&ctx->intel_device, I210_SYSTIML, initial_time_lo) != 0 ||
        intel_write_reg(&ctx->intel_device, I210_SYSTIMH, initial_time_hi) != 0) {
        DEBUGP(DL_ERROR, "PTP init: Failed to set initial SYSTIM time\n");
        return;
    }
    DEBUGP(DL_INFO, "PTP init: Initial time set to 0x%08X%08X\n", initial_time_hi, initial_time_lo);
    
    // Step 8: CRITICAL - Extended hardware stabilization time
    DEBUGP(DL_INFO, "PTP init: Waiting for clock stabilization (testing multiple samples)...\n");
    // Intel I210 needs significant time to start the PTP clock after configuration
    KeStallExecutionProcessor(200000); // 200ms initial stabilization time
    
    // Step 9: Enhanced clock verification with multiple attempts
    BOOLEAN clockRunning = FALSE;
    ULONG prev_lo = initial_time_lo;
    ULONG prev_hi = initial_time_hi;
    
    for (int attempt = 0; attempt < 8 && !clockRunning; attempt++) {
        // Longer delay between attempts for I210 hardware
        KeStallExecutionProcessor(100000); // 100ms delay per attempt
        
        ULONG lo_check=0, hi_check=0;
        if (intel_read_reg(&ctx->intel_device, I210_SYSTIML, &lo_check) == 0 && 
            intel_read_reg(&ctx->intel_device, I210_SYSTIMH, &hi_check) == 0) {
            
            DEBUGP(DL_INFO, "PTP init: Clock check %d - SYSTIM=0x%08X%08X", 
                   attempt + 1, hi_check, lo_check);
            
            // Check if time has advanced from previous reading  
            if (hi_check > prev_hi || (hi_check == prev_hi && lo_check > prev_lo)) {
                ULONG delta = (hi_check == prev_hi) ? (lo_check - prev_lo) : 0xFFFFFFFF;
                clockRunning = TRUE;
                DEBUGP(DL_INFO, " ? INCREMENTING (delta=%lu ns)\n", delta);
                DEBUGP(DL_INFO, "? PTP init: SUCCESS - Clock running (attempt %d, delta=%lu ns)\n", 
                       attempt + 1, delta);
                break;
            } else {
                DEBUGP(DL_INFO, " (not advancing yet)\n");
                
                // Special recovery attempt for stubborn I210 hardware
                if (attempt == 3 && lo_check == prev_lo && hi_check == prev_hi) {
                    DEBUGP(DL_INFO, "PTP init: Attempting advanced recovery sequence (attempt %d)...\n", attempt + 1);
                    
                    // Try alternative TSAUXC configuration with different timing
                    ULONG recovery_aux = 0x40000001UL; // PHC enabled + start trigger
                    if (intel_write_reg(&ctx->intel_device, INTEL_REG_TSAUXC, recovery_aux) == 0) {
                        KeStallExecutionProcessor(50000); // 50ms
                        
                        // Reset to normal configuration
                        if (intel_write_reg(&ctx->intel_device, INTEL_REG_TSAUXC, 0x40000000UL) == 0) {
                            DEBUGP(DL_INFO, "PTP init: Recovery trigger sequence completed\n");
                            KeStallExecutionProcessor(50000); // Additional settling time
                        }
                    }
                }
                
                prev_lo = lo_check;
                prev_hi = hi_check;
            }
        } else {
            DEBUGP(DL_ERROR, "PTP init: Failed to read SYSTIM during verification (attempt %d)\n", attempt + 1);
        }
    }
    
    if (clockRunning) {
        // Success: add PTP capabilities and promote state
        ctx->intel_device.capabilities |= (INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS);
        if (ctx->hw_state < AVB_HW_PTP_READY) { 
            ctx->hw_state = AVB_HW_PTP_READY; 
            DEBUGP(DL_INFO, "? PTP init: SUCCESS - HW state promoted to %s (PTP clock running)\n", 
                   AvbHwStateName(ctx->hw_state)); 
        }
        
        DEBUGP(DL_INFO, "? PTP init: I210 PTP initialization completed successfully\n");
        DEBUGP(DL_INFO, "   - Clock is incrementing normally\n");
        DEBUGP(DL_INFO, "   - Hardware timestamps available\n");
        DEBUGP(DL_INFO, "   - IEEE 1588 features operational\n");
    } else {
        DEBUGP(DL_ERROR, "? PTP init: FAILED - Clock still not running after extended initialization\n");
        DEBUGP(DL_ERROR, "   This indicates either:\n");
        DEBUGP(DL_ERROR, "   1. Hardware access issues (BAR0 mapping problems)\n");
        DEBUGP(DL_ERROR, "   2. Clock configuration problems (TSAUXC/TIMINCA)\n");
        DEBUGP(DL_ERROR, "   3. Hardware-specific issues requiring datasheet validation\n");
        
        // Read final register state for diagnosis
        ULONG final_aux=0, final_timinca=0, final_lo=0, final_hi=0;
        intel_read_reg(&ctx->intel_device, INTEL_REG_TSAUXC, &final_aux);
        intel_read_reg(&ctx->intel_device, I210_TIMINCA, &final_timinca);
        intel_read_reg(&ctx->intel_device, I210_SYSTIML, &final_lo);
        intel_read_reg(&ctx->intel_device, I210_SYSTIMH, &final_hi);
        DEBUGP(DL_ERROR, "   Final registers: TSAUXC=0x%08X, TIMINCA=0x%08X\n", final_aux, final_timinca);
        DEBUGP(DL_ERROR, "   Final SYSTIM: 0x%08X%08X\n", final_hi, final_lo);
        
        // CRITICAL DEBUG: Check timestamp control registers
        ULONG final_rxctl=0, final_txctl=0;
        if (intel_read_reg(&ctx->intel_device, I210_TSYNCRXCTL, &final_rxctl) == 0 &&
            intel_read_reg(&ctx->intel_device, I210_TSYNCTXCTL, &final_txctl) == 0) {
            DEBUGP(DL_ERROR, "   Final timestamp controls: TSYNCRXCTL=0x%08X, TSYNCTXCTL=0x%08X\n", 
                   final_rxctl, final_txctl);
            
            // Decode the bit fields for diagnosis
            ULONG rx_en = (final_rxctl & I210_TSYNCRXCTL_EN_MASK) >> I210_TSYNCRXCTL_EN_SHIFT;
            ULONG rx_type = (final_rxctl & I210_TSYNCRXCTL_TYPE_MASK) >> I210_TSYNCRXCTL_TYPE_SHIFT;
            ULONG tx_en = (final_txctl & I210_TSYNCTXCTL_EN_MASK) >> I210_TSYNCTXCTL_EN_SHIFT;
            ULONG tx_type = (final_txctl & I210_TSYNCTXCTL_TYPE_MASK) >> I210_TSYNCTXCTL_TYPE_SHIFT;
            
            DEBUGP(DL_ERROR, "   Decoded: RX_EN=%lu, RX_TYPE=%lu, TX_EN=%lu, TX_TYPE=%lu\n",
                   rx_en, rx_type, tx_en, tx_type);
            
            // Expected values should be: EN=1, TYPE=0 (ALL packets)
            if (rx_en != 1 || tx_en != 1) {
                DEBUGP(DL_ERROR, "   ISSUE: Timestamp capture not properly enabled\n");
                DEBUGP(DL_ERROR, "   Expected: RX_EN=1, TX_EN=1\n");
                DEBUGP(DL_ERROR, "   This may prevent the PTP clock from running\n");
            }
        }
        
        // CRITICAL FIX: For I210, also check if hardware requires additional clock domain reset
        DEBUGP(DL_ERROR, "?? PTP init: Attempting final I210-specific recovery sequence...\n");
        
        // Try writing to SYSTIML with trigger bit in TSAUXC (Intel I210 specific pattern)
        ULONG trigger_aux = 0x40000002UL; // PHC enabled + SYSTIM trigger (bit 1)
        if (intel_write_reg(&ctx->intel_device, INTEL_REG_TSAUXC, trigger_aux) == 0) {
            KeStallExecutionProcessor(10000); // 10ms
            
            // Write initial time with trigger active
            if (intel_write_reg(&ctx->intel_device, I210_SYSTIML, 0x10000000UL) == 0) {
                KeStallExecutionProcessor(10000); // 10ms
                
                // Clear trigger and return to normal mode
                if (intel_write_reg(&ctx->intel_device, INTEL_REG_TSAUXC, 0x40000000UL) == 0) {
                    DEBUGP(DL_INFO, "PTP init: I210 trigger sequence completed, testing again...\n");
                    
                    // Give hardware time to process trigger
                    KeStallExecutionProcessor(100000); // 100ms
                    
                    // Final verification attempt
                    ULONG final_test_lo=0, final_test_hi=0;
                    if (intel_read_reg(&ctx->intel_device, I210_SYSTIML, &final_test_lo) == 0 &&
                        intel_read_reg(&ctx->intel_device, I210_SYSTIMH, &final_test_hi) == 0) {
                        
                        DEBUGP(DL_INFO, "PTP init: Post-trigger SYSTIM = 0x%08X%08X\n", final_test_hi, final_test_lo);
                        
                        if (final_test_lo != 0 || final_test_hi != 0) {
                            // Check for increment one more time
                            KeStallExecutionProcessor(50000); // 50ms
                            ULONG verify_lo=0, verify_hi=0;
                            if (intel_read_reg(&ctx->intel_device, I210_SYSTIML, &verify_lo) == 0 &&
                                intel_read_reg(&ctx->intel_device, I210_SYSTIMH, &verify_hi) == 0 &&
                                (verify_hi > final_test_hi || (verify_hi == final_test_hi && verify_lo > final_test_lo))) {
                                
                                DEBUGP(DL_INFO, "? PTP init: RECOVERY SUCCESS - Clock started via trigger sequence!\n");
                                DEBUGP(DL_INFO, "   Recovery increment: 0x%08X%08X -> 0x%08X%08X\n",
                                       final_test_hi, final_test_lo, verify_hi, verify_lo);
                                
                                // Mark as successful
                                ctx->intel_device.capabilities |= (INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS);
                                if (ctx->hw_state < AVB_HW_PTP_READY) { 
                                    ctx->hw_state = AVB_HW_PTP_READY; 
                                    DEBUGP(DL_INFO, "? PTP init: RECOVERY - HW state promoted to %s\n", 
                                           AvbHwStateName(ctx->hw_state)); 
                                }
                                return; // Success path
                            }
                        }
                    }
                }
            }
        }
        
        // Still set basic capabilities for enumeration even if PTP fails
        ctx->intel_device.capabilities |= INTEL_CAP_MMIO;
        DEBUGP(DL_INFO, "? PTP init: MMIO capabilities still available despite PTP failure\n");
    }
}

/* ======================================================================= */
/* NDIS attach entry */
NTSTATUS AvbInitializeDevice(_In_ PMS_FILTER FilterModule, _Out_ PAVB_DEVICE_CONTEXT *AvbContext)
{
    if (!AvbContext) return STATUS_INVALID_PARAMETER;
    USHORT ven=0, dev=0;
    if (!AvbIsSupportedIntelController(FilterModule, &ven, &dev)) return STATUS_NOT_SUPPORTED;
    NTSTATUS st = AvbCreateMinimalContext(FilterModule, ven, dev, AvbContext);
    if (!NT_SUCCESS(st)) return st;
    (void)AvbBringUpHardware(*AvbContext); /* deferred, ignore failure */
    return STATUS_SUCCESS;
}

VOID AvbCleanupDevice(_In_ PAVB_DEVICE_CONTEXT AvbContext)
{
    if (!AvbContext) return;
    if (AvbContext->hardware_context) {
        AvbUnmapIntelControllerMemory(AvbContext);
    }
    if (g_AvbContext == AvbContext) g_AvbContext = NULL;
    ExFreePoolWithTag(AvbContext, FILTER_ALLOC_TAG);
}

/* ======================================================================= */
/* IOCTL dispatcher */
NTSTATUS AvbHandleDeviceIoControl(_In_ PAVB_DEVICE_CONTEXT AvbContext, _In_ PIRP Irp)
{
    if (!AvbContext) return STATUS_DEVICE_NOT_READY;
    PIO_STACK_LOCATION sp = IoGetCurrentIrpStackLocation(Irp);
    ULONG code   = sp->Parameters.DeviceIoControl.IoControlCode;
    PUCHAR buf   = (PUCHAR)Irp->AssociatedIrp.SystemBuffer;
    ULONG inLen  = sp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = sp->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG_PTR info = 0; NTSTATUS status = STATUS_SUCCESS;

    if (!AvbContext->initialized && code == IOCTL_AVB_INIT_DEVICE) (void)AvbBringUpHardware(AvbContext);
    if (!AvbContext->initialized && code != IOCTL_AVB_ENUM_ADAPTERS && code != IOCTL_AVB_INIT_DEVICE && code != IOCTL_AVB_GET_HW_STATE)
        return STATUS_DEVICE_NOT_READY;

    switch (code) {
    case IOCTL_AVB_INIT_DEVICE:
        {
            DEBUGP(DL_INFO, "?? IOCTL_AVB_INIT_DEVICE: Starting hardware bring-up\n");
            
            // Use the active global context (set by IOCTL_AVB_OPEN_ADAPTER) 
            PAVB_DEVICE_CONTEXT activeContext = g_AvbContext ? g_AvbContext : AvbContext;
            
            DEBUGP(DL_INFO, "   - Using context: VID=0x%04X DID=0x%04X\n",
                   activeContext->intel_device.pci_vendor_id, activeContext->intel_device.pci_device_id);
            DEBUGP(DL_INFO, "   - Current hw_state: %s (%d)\n", AvbHwStateName(activeContext->hw_state), activeContext->hw_state);
            DEBUGP(DL_INFO, "   - Hardware access enabled: %s\n", activeContext->hw_access_enabled ? "YES" : "NO");
            DEBUGP(DL_INFO, "   - Initialized flag: %s\n", activeContext->initialized ? "YES" : "NO");
            DEBUGP(DL_INFO, "   - Hardware context: %p\n", activeContext->hardware_context);
            DEBUGP(DL_INFO, "   - Device type: %d (%s)\n", activeContext->intel_device.device_type,
                   activeContext->intel_device.device_type == INTEL_DEVICE_I210 ? "I210" :
                   activeContext->intel_device.device_type == INTEL_DEVICE_I226 ? "I226" : "OTHER");
            
            // Force immediate BAR0 discovery if hardware context is missing
            if (activeContext->hardware_context == NULL && activeContext->hw_state == AVB_HW_BOUND) {
                DEBUGP(DL_WARN, "*** FORCED BAR0 DISCOVERY *** No hardware context, forcing immediate discovery...\n");
                
                PHYSICAL_ADDRESS bar0 = {0};
                ULONG barLen = 0;
                NTSTATUS ds = AvbDiscoverIntelControllerResources(activeContext->filter_instance, &bar0, &barLen);
                if (NT_SUCCESS(ds)) {
                    DEBUGP(DL_WARN, "*** BAR0 DISCOVERY SUCCESS *** PA=0x%llx, Len=0x%x\n", bar0.QuadPart, barLen);
                    NTSTATUS ms = AvbMapIntelControllerMemory(activeContext, bar0, barLen);
                    if (NT_SUCCESS(ms)) {
                        DEBUGP(DL_WARN, "*** BAR0 MAPPING SUCCESS *** Hardware context now available\n");
                        
                        // Complete initialization sequence
                        activeContext->intel_device.private_data = activeContext;
                        if (intel_init(&activeContext->intel_device) == 0) {
                            DEBUGP(DL_WARN, "*** INTEL_INIT SUCCESS ***\n");
                            
                            // Test MMIO sanity
                            ULONG ctrl = 0xFFFFFFFF;
                            if (intel_read_reg(&activeContext->intel_device, I210_CTRL, &ctrl) == 0 && ctrl != 0xFFFFFFFF) {
                                DEBUGP(DL_WARN, "*** MMIO SANITY SUCCESS *** CTRL=0x%08X\n", ctrl);
                                activeContext->hw_state = AVB_HW_BAR_MAPPED;
                                activeContext->hw_access_enabled = TRUE;
                                activeContext->initialized = TRUE;
                            } else {
                                DEBUGP(DL_ERROR, "*** MMIO SANITY FAILED *** CTRL=0x%08X\n", ctrl);
                            }
                        } else {
                            DEBUGP(DL_ERROR, "*** INTEL_INIT FAILED ***\n");
                        }
                    } else {
                        DEBUGP(DL_ERROR, "*** BAR0 MAPPING FAILED *** Status=0x%08X\n", ms);
                    }
                } else {
                    DEBUGP(DL_ERROR, "*** BAR0 DISCOVERY FAILED *** Status=0x%08X\n", ds);
                }
            }
            
            status = AvbBringUpHardware(activeContext);
            
            // CRITICAL: Force I210 PTP initialization if this is an I210
            if (activeContext->intel_device.device_type == INTEL_DEVICE_I210 && 
                activeContext->hw_state >= AVB_HW_BAR_MAPPED) {
                DEBUGP(DL_INFO, "?? INIT_DEVICE: Forcing I210 PTP initialization on active context...\n");
                AvbI210EnsureSystimRunning(activeContext);
                DEBUGP(DL_INFO, "?? INIT_DEVICE: I210 PTP initialization completed\n");
            }
            
            DEBUGP(DL_INFO, "?? IOCTL_AVB_INIT_DEVICE: Completed with status=0x%08X\n", status);
            DEBUGP(DL_INFO, "   - Final hw_state: %s (%d)\n", AvbHwStateName(activeContext->hw_state), activeContext->hw_state);
            DEBUGP(DL_INFO, "   - Final hardware access: %s\n", activeContext->hw_access_enabled ? "YES" : "NO");
        }
        break;
    case IOCTL_AVB_ENUM_ADAPTERS:
        if (outLen < sizeof(AVB_ENUM_REQUEST)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        else {
            PAVB_ENUM_REQUEST r = (PAVB_ENUM_REQUEST)buf; 
            RtlZeroMemory(r, sizeof(*r));
            
            DEBUGP(DL_INFO, "?? IOCTL_AVB_ENUM_ADAPTERS: Starting enumeration\n");
            
            // Count all Intel adapters in the system
            ULONG adapterCount = 0;
            BOOLEAN bFalse = FALSE;
            
            FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse);
            PLIST_ENTRY Link = FilterModuleList.Flink;
            
            // First pass: count Intel adapters
            while (Link != &FilterModuleList) {
                PMS_FILTER f = CONTAINING_RECORD(Link, MS_FILTER, FilterModuleLink);
                Link = Link->Flink;
                
                if (f && f->AvbContext) {
                    PAVB_DEVICE_CONTEXT ctx = (PAVB_DEVICE_CONTEXT)f->AvbContext;
                    if (ctx->intel_device.pci_vendor_id == INTEL_VENDOR_ID && 
                        ctx->intel_device.pci_device_id != 0) {
                        adapterCount++;
                    }
                }
            }
            
            DEBUGP(DL_INFO, "?? ENUM_ADAPTERS: Found %lu Intel adapters\n", adapterCount);
            
            // If requesting specific index, find and return that adapter
            if (r->index < adapterCount) {
                ULONG currentIndex = 0;
                Link = FilterModuleList.Flink;
                
                while (Link != &FilterModuleList) {
                    PMS_FILTER f = CONTAINING_RECORD(Link, MS_FILTER, FilterModuleLink);
                    Link = Link->Flink;
                    
                    if (f && f->AvbContext) {
                        PAVB_DEVICE_CONTEXT ctx = (PAVB_DEVICE_CONTEXT)f->AvbContext;
                        if (ctx->intel_device.pci_vendor_id == INTEL_VENDOR_ID && 
                            ctx->intel_device.pci_device_id != 0) {
                        
                            if (currentIndex == r->index) {
                                // Return this specific adapter's info
                                r->count = adapterCount;
                                r->vendor_id = (USHORT)ctx->intel_device.pci_vendor_id;
                                r->device_id = (USHORT)ctx->intel_device.pci_device_id;
                                r->capabilities = ctx->intel_device.capabilities;
                                r->status = NDIS_STATUS_SUCCESS;
                                
                                DEBUGP(DL_INFO, "? ENUM_ADAPTERS[%lu]: VID=0x%04X, DID=0x%04X, Caps=0x%08X\n",
                                       r->index, r->vendor_id, r->device_id, r->capabilities);
                                break;
                            }
                            currentIndex++;
                        }
                    }
                }
            } else {
                // Return count and first adapter info (for compatibility)
                r->count = adapterCount;
                if (adapterCount > 0) {
                    // Use current context info or first found adapter
                    r->vendor_id = (USHORT)AvbContext->intel_device.pci_vendor_id;
                    r->device_id = (USHORT)AvbContext->intel_device.pci_device_id;
                    r->capabilities = AvbContext->intel_device.capabilities;
                } else {
                    r->vendor_id = 0;
                    r->device_id = 0;
                    r->capabilities = 0;
                }
                r->status = NDIS_STATUS_SUCCESS;
                
                DEBUGP(DL_INFO, "? ENUM_ADAPTERS(summary): count=%lu, VID=0x%04X, DID=0x%04X, Caps=0x%08X\n",
                       r->count, r->vendor_id, r->device_id, r->capabilities);
            }
            
            FILTER_RELEASE_LOCK(&FilterListLock, bFalse);
            info = sizeof(*r);
        }
        break;
    case IOCTL_AVB_GET_DEVICE_INFO:
        {
            DEBUGP(DL_INFO, "?? IOCTL_AVB_GET_DEVICE_INFO: Starting device info request\n");
            
            if (outLen < sizeof(AVB_DEVICE_INFO_REQUEST)) { 
                DEBUGP(DL_ERROR, "? DEVICE_INFO FAILED: Buffer too small\n");
                status = STATUS_BUFFER_TOO_SMALL; 
            } else {
                // CRITICAL FIX: Use the specific adapter context that was selected via IOCTL_AVB_OPEN_ADAPTER
                PAVB_DEVICE_CONTEXT activeContext = g_AvbContext ? g_AvbContext : AvbContext;
                
                DEBUGP(DL_INFO, "   - Using context: VID=0x%04X DID=0x%04X\n",
                       activeContext->intel_device.pci_vendor_id, activeContext->intel_device.pci_device_id);
                DEBUGP(DL_INFO, "   - Hardware state: %s\n", AvbHwStateName(activeContext->hw_state));
                DEBUGP(DL_INFO, "   - Device type: %d\n", activeContext->intel_device.device_type);
                DEBUGP(DL_INFO, "   - Filter instance: %p\n", activeContext->filter_instance);
                
                if (activeContext->hw_state < AVB_HW_BAR_MAPPED) { 
                    DEBUGP(DL_ERROR, "? DEVICE_INFO FAILED: Hardware not ready - hw_state=%s\n", 
                           AvbHwStateName(activeContext->hw_state));
                    status = STATUS_DEVICE_NOT_READY; 
                } else {
                    DEBUGP(DL_INFO, "? DEVICE_INFO: Hardware state validation passed\n");
                    
                    PAVB_DEVICE_INFO_REQUEST r = (PAVB_DEVICE_INFO_REQUEST)buf; 
                    RtlZeroMemory(r->device_info, sizeof(r->device_info)); 
                    
                    // CRITICAL FIX: Ensure the intel device structure points to the correct adapter
                    if (activeContext->hardware_context && activeContext->hw_access_enabled) {
                        activeContext->intel_device.private_data = activeContext;
                        DEBUGP(DL_INFO, "?? DEVICE_INFO: Intel device structure updated for VID=0x%04X DID=0x%04X\n",
                               activeContext->intel_device.pci_vendor_id, activeContext->intel_device.pci_device_id);
                    }
                    
                    DEBUGP(DL_INFO, "?? DEVICE_INFO: Calling intel_get_device_info...\n");
                    int rc = intel_get_device_info(&activeContext->intel_device, r->device_info, sizeof(r->device_info)); 
                    DEBUGP(DL_INFO, "?? DEVICE_INFO: intel_get_device_info returned %d\n", rc);
                    
                    if (rc == 0) {
                        DEBUGP(DL_INFO, "? DEVICE_INFO: Device info string: %s\n", r->device_info);
                    } else {
                        DEBUGP(DL_ERROR, "? DEVICE_INFO: intel_get_device_info failed with code %d\n", rc);
                        // Fallback to manual device info generation
                        const char* deviceName = "";
                        switch (activeContext->intel_device.device_type) {
                            case INTEL_DEVICE_I210: deviceName = "Intel I210 Gigabit Ethernet - Full TSN Support"; break;
                            case INTEL_DEVICE_I226: deviceName = "Intel I226 2.5G Ethernet - Advanced TSN"; break;
                            case INTEL_DEVICE_I225: deviceName = "Intel I225 2.5G Ethernet - Enhanced TSN"; break;
                            case INTEL_DEVICE_I217: deviceName = "Intel I217 Gigabit Ethernet - Basic PTP"; break;
                            case INTEL_DEVICE_I219: deviceName = "Intel I219 Gigabit Ethernet - Enhanced PTP"; break;
                            default: deviceName = "Unknown Intel Ethernet Device"; break;
                        }
                        RtlStringCbCopyA(r->device_info, sizeof(r->device_info), deviceName);
                        rc = 0; // Override failure
                        DEBUGP(DL_INFO, "? DEVICE_INFO: Using fallback device info: %s\n", r->device_info);
                    }
                    
                    size_t used = 0; 
                    (void)RtlStringCbLengthA(r->device_info, sizeof(r->device_info), &used); 
                    r->buffer_size = (ULONG)used; 
                    r->status = (rc == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE; 
                    info = sizeof(*r); 
                    status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL; 
                    
                    DEBUGP(DL_INFO, "?? DEVICE_INFO COMPLETE: status=0x%08X, buffer_size=%lu\n", status, r->buffer_size);
                }
            }
        }
        break;
    case IOCTL_AVB_READ_REGISTER:
    case IOCTL_AVB_WRITE_REGISTER:
        {
            if (inLen < sizeof(AVB_REGISTER_REQUEST) || outLen < sizeof(AVB_REGISTER_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL; 
            } else {
                // CRITICAL FIX: Use the specific adapter context that was selected via IOCTL_AVB_OPEN_ADAPTER
                PAVB_DEVICE_CONTEXT activeContext = g_AvbContext ? g_AvbContext : AvbContext;
                
                if (activeContext->hw_state < AVB_HW_BAR_MAPPED) {
                    DEBUGP(DL_ERROR, "Register access failed: Hardware not ready (state=%s)\n", 
                           AvbHwStateName(activeContext->hw_state));
                    status = STATUS_DEVICE_NOT_READY;
                } else {
                    PAVB_REGISTER_REQUEST r = (PAVB_REGISTER_REQUEST)buf; 
                    
                    // CRITICAL FIX: Ensure the Intel library is using the correct hardware context
                    if (activeContext->hardware_context && activeContext->hw_access_enabled) {
                        activeContext->intel_device.private_data = activeContext;
                    }
                    
                    DEBUGP(DL_TRACE, "Register %s: offset=0x%05X, context VID=0x%04X DID=0x%04X (type=%d)\n",
                           (code == IOCTL_AVB_READ_REGISTER) ? "READ" : "WRITE",
                           r->offset, activeContext->intel_device.pci_vendor_id,
                           activeContext->intel_device.pci_device_id, activeContext->intel_device.device_type);
                    
                    if (code == IOCTL_AVB_READ_REGISTER) {
                        ULONG tmp = 0; 
                        int rc = intel_read_reg(&activeContext->intel_device, r->offset, &tmp); 
                        r->value = (avb_u32)tmp; 
                        r->status = (rc == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE; 
                        status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                        
                        if (rc == 0) {
                            DEBUGP(DL_TRACE, "Register READ success: offset=0x%05X, value=0x%08X (VID=0x%04X DID=0x%04X)\n",
                                   r->offset, r->value, activeContext->intel_device.pci_vendor_id, 
                                   activeContext->intel_device.pci_device_id);
                        } else {
                            DEBUGP(DL_ERROR, "Register READ failed: offset=0x%05X (VID=0x%04X DID=0x%04X)\n",
                                   r->offset, activeContext->intel_device.pci_vendor_id, 
                                   activeContext->intel_device.pci_device_id);
                        }
                    } else {
                        int rc = intel_write_reg(&activeContext->intel_device, r->offset, r->value); 
                        r->status = (rc == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE; 
                        status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                        
                        if (rc == 0) {
                            DEBUGP(DL_TRACE, "Register WRITE success: offset=0x%05X, value=0x%08X (VID=0x%04X DID=0x%04X)\n",
                                   r->offset, r->value, activeContext->intel_device.pci_vendor_id, 
                                   activeContext->intel_device.pci_device_id);
                        } else {
                            DEBUGP(DL_ERROR, "Register WRITE failed: offset=0x%05X, value=0x%08X (VID=0x%04X DID=0x%04X)\n",
                                   r->offset, r->value, activeContext->intel_device.pci_vendor_id, 
                                   activeContext->intel_device.pci_device_id);
                        }
                    } 
                    info = sizeof(*r);
                }
            }
        }
        break;
    case IOCTL_AVB_GET_TIMESTAMP:
    case IOCTL_AVB_SET_TIMESTAMP:
        {
            if (inLen < sizeof(AVB_TIMESTAMP_REQUEST) || outLen < sizeof(AVB_TIMESTAMP_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL; 
            } else {
                // Use the active global context (set by IOCTL_AVB_OPEN_ADAPTER)
                PAVB_DEVICE_CONTEXT activeContext = g_AvbContext ? g_AvbContext : AvbContext;
                
                if (activeContext->hw_state < AVB_HW_PTP_READY) {
                    DEBUGP(DL_WARN, "Timestamp access: Hardware state %s, attempting PTP initialization\n",
                           AvbHwStateName(activeContext->hw_state));
                    
                    // If this is I210 and we have BAR access, try to initialize PTP
                    if (activeContext->intel_device.device_type == INTEL_DEVICE_I210 && 
                        activeContext->hw_state >= AVB_HW_BAR_MAPPED) {
                        DEBUGP(DL_INFO, "Attempting I210 PTP initialization for timestamp access\n");
                        AvbI210EnsureSystimRunning(activeContext);
                    }
                    
                    // Check state again after potential initialization
                    if (activeContext->hw_state < AVB_HW_PTP_READY) {
                        status = STATUS_DEVICE_NOT_READY;
                        break;
                    }
                }
                
                PAVB_TIMESTAMP_REQUEST r = (PAVB_TIMESTAMP_REQUEST)buf; 
                
                DEBUGP(DL_TRACE, "Timestamp %s: context VID=0x%04X DID=0x%04X\n",
                       (code == IOCTL_AVB_GET_TIMESTAMP) ? "GET" : "SET",
                       activeContext->intel_device.pci_vendor_id, activeContext->intel_device.pci_device_id);
                
                if (code == IOCTL_AVB_GET_TIMESTAMP) {
                    ULONGLONG t = 0; 
                    struct timespec sys = {0}; 
                    int rc = intel_gettime(&activeContext->intel_device, r->clock_id, &t, &sys); 
                    if (rc != 0) {
                        rc = AvbReadTimestamp(&activeContext->intel_device, &t); 
                    }
                    r->timestamp = t; 
                    r->status = (rc == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE; 
                    status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                } else {
                    int rc = intel_set_systime(&activeContext->intel_device, r->timestamp); 
                    r->status = (rc == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE; 
                    status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                } 
                info = sizeof(*r);
            }
        }
        break;
    case IOCTL_AVB_GET_HW_STATE:
        DEBUGP(DL_INFO, "?? IOCTL_AVB_GET_HW_STATE: Hardware state query\n");
        DEBUGP(DL_INFO, "   - Context: %p\n", AvbContext);
        DEBUGP(DL_INFO, "   - Global context: %p\n", g_AvbContext);
        DEBUGP(DL_INFO, "   - Filter instance: %p\n", AvbContext->filter_instance);
        DEBUGP(DL_INFO, "   - Device type: %d\n", AvbContext->intel_device.device_type);
        
        if (outLen < sizeof(AVB_HW_STATE_QUERY)) { 
            status = STATUS_BUFFER_TOO_SMALL; 
        } else { 
            PAVB_HW_STATE_QUERY q = (PAVB_HW_STATE_QUERY)buf; 
            RtlZeroMemory(q, sizeof(*q)); 
            q->hw_state = AvbContext->hw_state; 
            q->vendor_id = AvbContext->intel_device.pci_vendor_id; 
            q->device_id = AvbContext->intel_device.pci_device_id; 
            q->capabilities = AvbContext->intel_device.capabilities; 
            info = sizeof(*q);
            
            DEBUGP(DL_INFO, "? HW_STATE: state=%s, VID=0x%04X, DID=0x%04X, caps=0x%08X\n",
                   AvbHwStateName(q->hw_state), q->vendor_id, q->device_id, q->capabilities);
            
            // Force BAR0 discovery attempt if hardware is still in BOUND state
            if (AvbContext->hw_state == AVB_HW_BOUND && AvbContext->hardware_context == NULL) {
                DEBUGP(DL_INFO, "?? FORCING BAR0 DISCOVERY: Hardware stuck in BOUND state, attempting manual discovery...\n");
                
                // Ensure device type is properly set
                if (AvbContext->intel_device.device_type == INTEL_DEVICE_UNKNOWN && 
                    AvbContext->intel_device.pci_device_id != 0) {
                    AvbContext->intel_device.device_type = AvbGetIntelDeviceType(AvbContext->intel_device.pci_device_id);
                    DEBUGP(DL_INFO, "?? Updated device type to %d for DID=0x%04X\n", 
                           AvbContext->intel_device.device_type, AvbContext->intel_device.pci_device_id);
                }
                
                PHYSICAL_ADDRESS bar0 = {0};
                ULONG barLen = 0;
                NTSTATUS ds = AvbDiscoverIntelControllerResources(AvbContext->filter_instance, &bar0, &barLen);
                if (NT_SUCCESS(ds)) {
                    DEBUGP(DL_INFO, "? MANUAL BAR0 DISCOVERY SUCCESS: PA=0x%llx, Len=0x%x\n", bar0.QuadPart, barLen);
                    NTSTATUS ms = AvbMapIntelControllerMemory(AvbContext, bar0, barLen);
                    if (NT_SUCCESS(ms)) {
                        DEBUGP(DL_INFO, "? MANUAL BAR0 MAPPING SUCCESS: Hardware context now available\n");
                        
                        // Complete the initialization sequence
                        AvbContext->intel_device.private_data = AvbContext;
                        if (intel_init(&AvbContext->intel_device) == 0) {
                            DEBUGP(DL_INFO, "? MANUAL intel_init SUCCESS\n");
                            
                            // Test MMIO sanity
                            ULONG ctrl = 0xFFFFFFFF;
                            if (intel_read_reg(&AvbContext->intel_device, I210_CTRL, &ctrl) == 0 && ctrl != 0xFFFFFFFF) {
                                DEBUGP(DL_INFO, "? MANUAL MMIO SANITY SUCCESS: CTRL=0x%08X\n", ctrl);
                                AvbContext->hw_state = AVB_HW_BAR_MAPPED;
                                AvbContext->hw_access_enabled = TRUE;
                                AvbContext->initialized = TRUE;
                                q->hw_state = AvbContext->hw_state; // Update return value
                                
                                // Try I210 PTP initialization if applicable
                                if (AvbContext->intel_device.device_type == INTEL_DEVICE_I210) {
                                    DEBUGP(DL_INFO, "?? MANUAL I210 PTP INIT: Starting...\n");
                                    AvbI210EnsureSystimRunning(AvbContext);
                                }
                            } else {
                                DEBUGP(DL_ERROR, "? MANUAL MMIO SANITY FAILED: CTRL=0x%08X\n", ctrl);
                            }
                        } else {
                            DEBUGP(DL_ERROR, "? MANUAL intel_init FAILED\n");
                        }
                    } else {
                        DEBUGP(DL_ERROR, "? MANUAL BAR0 MAPPING FAILED: 0x%08X\n", ms);
                    }
                } else {
                    DEBUGP(DL_ERROR, "? MANUAL BAR0 DISCOVERY FAILED: 0x%08X\n", ds);
                }
            }
        } 
        break;
    case IOCTL_AVB_OPEN_ADAPTER:
        DEBUGP(DL_INFO, "?? IOCTL_AVB_OPEN_ADAPTER: Multi-adapter context switching\n");
        
        if (outLen < sizeof(AVB_OPEN_REQUEST)) { 
            DEBUGP(DL_ERROR, "? OPEN_ADAPTER: Buffer too small (%lu < %lu)\n", outLen, sizeof(AVB_OPEN_REQUEST));
            status = STATUS_BUFFER_TOO_SMALL; 
        } else { 
            PAVB_OPEN_REQUEST req = (PAVB_OPEN_REQUEST)buf; 
            DEBUGP(DL_INFO, "?? OPEN_ADAPTER: Looking for VID=0x%04X DID=0x%04X\n", 
                   req->vendor_id, req->device_id);
            
            // Search through ALL filter modules to find the requested adapter
            BOOLEAN bFalse = FALSE;
            PMS_FILTER targetFilter = NULL;
            PAVB_DEVICE_CONTEXT targetContext = NULL;
            
            FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse);
            PLIST_ENTRY Link = FilterModuleList.Flink;
            
            while (Link != &FilterModuleList && targetFilter == NULL) {
                PMS_FILTER cand = CONTAINING_RECORD(Link, MS_FILTER, FilterModuleLink);
                Link = Link->Flink;
                
                if (cand && cand->AvbContext) {
                    PAVB_DEVICE_CONTEXT ctx = (PAVB_DEVICE_CONTEXT)cand->AvbContext;
                    DEBUGP(DL_INFO, "?? OPEN_ADAPTER: Checking filter %wZ - VID=0x%04X DID=0x%04X\n",
                           &cand->MiniportFriendlyName, ctx->intel_device.pci_vendor_id, 
                           ctx->intel_device.pci_device_id);
                    
                    if (ctx->intel_device.pci_vendor_id == req->vendor_id && 
                        ctx->intel_device.pci_device_id == req->device_id) {
                        targetFilter = cand;
                        targetContext = ctx;
                        DEBUGP(DL_INFO, "? Found target adapter: %wZ (VID=0x%04X, DID=0x%04X)\n", 
                               &cand->MiniportFriendlyName, ctx->intel_device.pci_vendor_id, 
                               ctx->intel_device.pci_device_id);
                        break;
                    }
                }
            }
            
            FILTER_RELEASE_LOCK(&FilterListLock, bFalse);
            
            if (targetFilter == NULL) {
                DEBUGP(DL_ERROR, "? OPEN_ADAPTER: No adapter found for VID=0x%04X DID=0x%04X\n", 
                       req->vendor_id, req->device_id);
                DEBUGP(DL_ERROR, "   Available adapters:\n");
                
                // List all available adapters for debugging
                FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse);
                Link = FilterModuleList.Flink;
                while (Link != &FilterModuleList) {
                    PMS_FILTER f = CONTAINING_RECORD(Link, MS_FILTER, FilterModuleLink);
                    Link = Link->Flink;
                    if (f && f->AvbContext) {
                        PAVB_DEVICE_CONTEXT ctx = (PAVB_DEVICE_CONTEXT)f->AvbContext;
                        DEBUGP(DL_ERROR, "     - %wZ: VID=0x%04X DID=0x%04X\n",
                               &f->MiniportFriendlyName, ctx->intel_device.pci_vendor_id, 
                               ctx->intel_device.pci_device_id);
                    }
                }
                FILTER_RELEASE_LOCK(&FilterListLock, bFalse);
                
                req->status = (avb_u32)STATUS_NO_SUCH_DEVICE;
                info = sizeof(*req);
                status = STATUS_SUCCESS; // IRP handled, but device not found
            } else {
                // CRITICAL: Switch global context to the requested adapter
                DEBUGP(DL_INFO, "?? OPEN_ADAPTER: Switching global context\n");
                DEBUGP(DL_INFO, "   - From: VID=0x%04X DID=0x%04X (filter=%p)\n",
                       g_AvbContext ? g_AvbContext->intel_device.pci_vendor_id : 0,
                       g_AvbContext ? g_AvbContext->intel_device.pci_device_id : 0,
                       g_AvbContext ? g_AvbContext->filter_instance : NULL);
                DEBUGP(DL_INFO, "   - To:   VID=0x%04X DID=0x%04X (filter=%p)\n",
                       targetContext->intel_device.pci_vendor_id,
                       targetContext->intel_device.pci_device_id,
                       targetFilter);
                
                // Make the target context the active global context
                g_AvbContext = targetContext;
                
                // CRITICAL FIX: Ensure the Intel library device structure points to the correct hardware context
                // This was the missing piece - the Intel library wasn't updated when context switched
                if (targetContext->hardware_context && targetContext->hw_access_enabled) {
                    DEBUGP(DL_INFO, "?? OPEN_ADAPTER: Updating Intel library device structure\n");
                    DEBUGP(DL_INFO, "   - Intel device private_data: %p -> %p\n", 
                           targetContext->intel_device.private_data, targetContext);
                    DEBUGP(DL_INFO, "   - Hardware context: %p\n", targetContext->hardware_context);
                    
                    // Ensure Intel library device points to this specific hardware context
                    targetContext->intel_device.private_data = targetContext;
                }
                
                // Ensure the target context is fully initialized
                if (!targetContext->initialized || targetContext->hw_state < AVB_HW_BAR_MAPPED) {
                    DEBUGP(DL_INFO, "?? OPEN_ADAPTER: Target adapter needs initialization\n");
                    DEBUGP(DL_INFO, "   - Current state: %s\n", AvbHwStateName(targetContext->hw_state));
                    DEBUGP(DL_INFO, "   - Initialized: %s\n", targetContext->initialized ? "YES" : "NO");
                    
                    NTSTATUS initStatus = AvbBringUpHardware(targetContext);
                    if (!NT_SUCCESS(initStatus)) {
                        DEBUGP(DL_ERROR, "? OPEN_ADAPTER: Target adapter initialization failed: 0x%08X\n", initStatus);
                        // Continue anyway - some functionality may still work
                    }
                }
                
                // CRITICAL FIX: Force I210 PTP initialization specifically for I210 context switching
                if (targetContext->intel_device.device_type == INTEL_DEVICE_I210 && 
                    targetContext->hw_state >= AVB_HW_BAR_MAPPED) {
                    DEBUGP(DL_INFO, "?? OPEN_ADAPTER: Forcing I210 PTP initialization for selected adapter\n");
                    DEBUGP(DL_INFO, "   - Device Type: %d (INTEL_DEVICE_I210)\n", targetContext->intel_device.device_type);
                    DEBUGP(DL_INFO, "   - Hardware State: %s\n", AvbHwStateName(targetContext->hw_state));
                    DEBUGP(DL_INFO, "   - Hardware Context: %p\n", targetContext->hardware_context);
                    
                    // Force complete I210 PTP setup
                    AvbI210EnsureSystimRunning(targetContext);
                    
                    DEBUGP(DL_INFO, "?? OPEN_ADAPTER: I210 PTP initialization completed\n");
                    DEBUGP(DL_INFO, "   - Final Hardware State: %s\n", AvbHwStateName(targetContext->hw_state));
                    DEBUGP(DL_INFO, "   - Final Capabilities: 0x%08X\n", targetContext->intel_device.capabilities);
                }
                
                req->status = 0; // Success
                info = sizeof(*req);
                status = STATUS_SUCCESS;
                
                DEBUGP(DL_INFO, "? OPEN_ADAPTER: Context switch completed successfully\n");
                DEBUGP(DL_INFO, "   - Active context: VID=0x%04X DID=0x%04X\n",
                       g_AvbContext->intel_device.pci_vendor_id,
                       g_AvbContext->intel_device.pci_device_id);
                DEBUGP(DL_INFO, "   - Hardware state: %s\n", AvbHwStateName(g_AvbContext->hw_state));
                DEBUGP(DL_INFO, "   - Capabilities: 0x%08X\n", g_AvbContext->intel_device.capabilities);
            }
        }
        break;
    default:
        status = STATUS_INVALID_DEVICE_REQUEST; break;
    }

    Irp->IoStatus.Information = info; return status;
}

/* ======================================================================= */
/* Platform wrappers (real HW access provided in other translation units) */
NTSTATUS AvbPlatformInit(_In_ device_t *dev){ UNREFERENCED_PARAMETER(dev); return STATUS_SUCCESS; }
VOID     AvbPlatformCleanup(_In_ device_t *dev){ UNREFERENCED_PARAMETER(dev); }
int AvbPciReadConfig(device_t *dev, ULONG o, ULONG *v){ return AvbPciReadConfigReal(dev,o,v);} 
int AvbPciWriteConfig(device_t *dev, ULONG o, ULONG v){ return AvbPciWriteConfigReal(dev,o,v);} 
int AvbMmioRead(device_t *dev, ULONG o, ULONG *v){ return AvbMmioReadReal(dev,o,v);} 
int AvbMmioWrite(device_t *dev, ULONG o, ULONG v){ return AvbMmioWriteReal(dev,o,v);} 
int AvbMdioRead(device_t *dev, USHORT p, USHORT r, USHORT *val){ return AvbMdioReadReal(dev,p,r,val);} 
int AvbMdioWrite(device_t *dev, USHORT p, USHORT r, USHORT val){ return AvbMdioWriteReal(dev,p,r,val);} 
int AvbReadTimestamp(device_t *dev, ULONGLONG *ts){ return AvbReadTimestampReal(dev,ts);} 
int AvbMdioReadI219Direct(device_t *dev, USHORT p, USHORT r, USHORT *val){ return AvbMdioReadI219DirectReal(dev,p,r,val);} 
int AvbMdioWriteI219Direct(device_t *dev, USHORT p, USHORT r, USHORT val){ return AvbMdioWriteI219DirectReal(dev,p,r,val);} 

/* Helpers */
BOOLEAN AvbIsIntelDevice(UINT16 vid, UINT16 did){ UNREFERENCED_PARAMETER(did); return vid==INTEL_VENDOR_ID; }
intel_device_type_t AvbGetIntelDeviceType(UINT16 did){ switch(did){case 0x1533:return INTEL_DEVICE_I210; case 0x153A:case 0x153B:return INTEL_DEVICE_I217; case 0x15B7:case 0x15B8:case 0x15D6:case 0x15D7:case 0x15D8:case 0x0DC7:case 0x1570:case 0x15E3:return INTEL_DEVICE_I219; case 0x15F2:return INTEL_DEVICE_I225; case 0x125B:return INTEL_DEVICE_I226; default:return INTEL_DEVICE_UNKNOWN; }}

ULONG intel_get_capabilities(device_t *dev)
{
    if (dev == NULL) {
        return 0;
    }
    
    return dev->capabilities;
}

PMS_FILTER AvbFindIntelFilterModule(VOID)
{ 
    if (g_AvbContext && g_AvbContext->filter_instance) {
        // Verify this is actually a supported Intel device
        if (g_AvbContext->intel_device.pci_vendor_id == INTEL_VENDOR_ID &&
            g_AvbContext->intel_device.pci_device_id != 0) {
            DEBUGP(DL_INFO, "AvbFindIntelFilterModule: Using global context VID=0x%04X DID=0x%04X\n",
                   g_AvbContext->intel_device.pci_vendor_id, g_AvbContext->intel_device.pci_device_id);
            return g_AvbContext->filter_instance;
        }
    }
    
    PMS_FILTER bestFilter = NULL;
    PAVB_DEVICE_CONTEXT bestContext = NULL;
    PLIST_ENTRY l; 
    BOOLEAN bFalse = FALSE; 
    
    DEBUGP(DL_INFO, "AvbFindIntelFilterModule: Searching filter list for best Intel adapter...\n");
    
    FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse); 
    l = FilterModuleList.Flink; 
    
    while (l != &FilterModuleList) { 
        PMS_FILTER f = CONTAINING_RECORD(l, MS_FILTER, FilterModuleLink); 
        l = l->Flink; // Move to next before potentially releasing lock
        
        if (f && f->AvbContext) { 
            PAVB_DEVICE_CONTEXT ctx = (PAVB_DEVICE_CONTEXT)f->AvbContext;
            
            DEBUGP(DL_INFO, "AvbFindIntelFilterModule: Checking filter %wZ - VID=0x%04X DID=0x%04X state=%s\n",
                   &f->MiniportFriendlyName, ctx->intel_device.pci_vendor_id, 
                   ctx->intel_device.pci_device_id, AvbHwStateName(ctx->hw_state));
            
            // Look for properly initialized Intel contexts
            if (ctx->intel_device.pci_vendor_id == INTEL_VENDOR_ID && 
                ctx->intel_device.pci_device_id != 0) {
                
                // Prefer contexts with better hardware state
                if (bestContext == NULL || ctx->hw_state > bestContext->hw_state) {
                    bestFilter = f;
                    bestContext = ctx;
                    DEBUGP(DL_INFO, "AvbFindIntelFilterModule: New best candidate: %wZ (state=%s)\n",
                           &f->MiniportFriendlyName, AvbHwStateName(ctx->hw_state));
                }
            }
        } 
    } 
    
    FILTER_RELEASE_LOCK(&FilterListLock, bFalse); 
    
    if (bestFilter && bestContext) {
        DEBUGP(DL_INFO, "AvbFindIntelFilterModule: Selected best Intel filter: %wZ (VID=0x%04X DID=0x%04X state=%s)\n",
               &bestFilter->MiniportFriendlyName, bestContext->intel_device.pci_vendor_id,
               bestContext->intel_device.pci_device_id, AvbHwStateName(bestContext->hw_state));
        return bestFilter;
    }
    
    DEBUGP(DL_WARN, "AvbFindIntelFilterModule: No Intel filter found with valid context\n");
    return NULL; 
}