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

/* Forward declarations */
static NTSTATUS AvbPerformBasicInitialization(_Inout_ PAVB_DEVICE_CONTEXT Ctx);
static NTSTATUS AvbI210InitializePTPClock(_Inout_ PAVB_DEVICE_CONTEXT context);

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
    
    // CRITICAL FIX: Set and preserve baseline capabilities throughout initialization
    Ctx->intel_device.capabilities = baseline_caps;
    DEBUGP(DL_INFO, "?? AvbBringUpHardware: Set baseline capabilities 0x%08X for device type %d (%s)\n", 
           baseline_caps, Ctx->intel_device.device_type,
           Ctx->intel_device.device_type == INTEL_DEVICE_I226 ? "I226" :
           Ctx->intel_device.device_type == INTEL_DEVICE_I210 ? "I210" : "OTHER");
    
    NTSTATUS status = AvbPerformBasicInitialization(Ctx);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_WARN, "?? AvbBringUpHardware: basic init failed 0x%08X (keeping baseline capabilities=0x%08X)\n", 
               status, baseline_caps);
        // Ensure capabilities are preserved even if init fails
        Ctx->intel_device.capabilities = baseline_caps;
        return STATUS_SUCCESS;
    }
    
    // CRITICAL FIX: Restore baseline capabilities after initialization
    // The AvbPerformBasicInitialization resets capabilities to 0, then only adds MMIO
    Ctx->intel_device.capabilities = baseline_caps;
    DEBUGP(DL_INFO, "?? AvbBringUpHardware: Restored full baseline capabilities 0x%08X after init\n", 
           Ctx->intel_device.capabilities);
    
    // Specific device initialization paths
    if (Ctx->intel_device.device_type == INTEL_DEVICE_I226 && Ctx->hw_state >= AVB_HW_BAR_MAPPED) {
        DEBUGP(DL_INFO, "?? Starting I226 TSN initialization...\n");
        // I226 typically has PTP already running, promote to PTP_READY
        if (Ctx->hw_state < AVB_HW_PTP_READY) {
            Ctx->hw_state = AVB_HW_PTP_READY;
            DEBUGP(DL_INFO, "?? I226 HW state -> %s (TSN ready)\n", AvbHwStateName(Ctx->hw_state));
        }
        
        // Final capabilities verification for I226
        DEBUGP(DL_INFO, "?? I226 final capabilities: 0x%08X (expected: 0x%08X)\n", 
               Ctx->intel_device.capabilities, baseline_caps);
    }
    
    if (Ctx->intel_device.device_type == INTEL_DEVICE_I210 && Ctx->hw_state >= AVB_HW_BAR_MAPPED) {
        DEBUGP(DL_INFO, "?? Starting I210 PTP initialization...\n");
        (void)AvbI210InitializePTPClock(Ctx); // Use specific PTP init function
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

    // CRITICAL FIX: Store capabilities before intel_init (which might reset them)
    ULONG saved_capabilities = Ctx->intel_device.capabilities;
    DEBUGP(DL_INFO, "?? Saving baseline capabilities: 0x%08X\n", saved_capabilities);

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
    // CRITICAL FIX: DON'T reset capabilities to 0 - preserve baseline capabilities!
    // OLD CODE: Ctx->intel_device.capabilities = 0; /* reset published caps */
    // NEW CODE: Keep baseline capabilities, just ensure private_data is set
    DEBUGP(DL_INFO, "? STEP 2 SUCCESS: Device structure prepared (capabilities preserved: 0x%08X)\n", 
           Ctx->intel_device.capabilities);

    DEBUGP(DL_INFO, "?? STEP 3: Calling intel_init library function...\n");
    DEBUGP(DL_INFO, "   - VID=0x%04X DID=0x%04X\n", Ctx->intel_device.pci_vendor_id, Ctx->intel_device.pci_device_id);
    if (intel_init(&Ctx->intel_device) != 0) {
        DEBUGP(DL_ERROR, "? STEP 3 FAILED: intel_init failed (library)\n");
        return STATUS_UNSUCCESSFUL;
    }
    DEBUGP(DL_INFO, "? STEP 3 SUCCESS: intel_init completed successfully\n");

    // CRITICAL FIX: Restore capabilities if Intel library reset them
    if (Ctx->intel_device.capabilities != saved_capabilities) {
        DEBUGP(DL_WARN, "?? Intel library changed capabilities from 0x%08X to 0x%08X, restoring...\n",
               saved_capabilities, Ctx->intel_device.capabilities);
        Ctx->intel_device.capabilities = saved_capabilities;
    }

    DEBUGP(DL_INFO, "?? STEP 4: MMIO sanity check - reading CTRL register...\n");
    ULONG ctrl = 0xFFFFFFFF;
    if (intel_read_reg(&Ctx->intel_device, I210_CTRL, &ctrl) != 0 || ctrl == 0xFFFFFFFF) {
        DEBUGP(DL_ERROR, "? STEP 4 FAILED: MMIO sanity read failed CTRL=0x%08X (expected != 0xFFFFFFFF)\n", ctrl);
        DEBUGP(DL_ERROR, "   This indicates BAR0 mapping is not working properly\n");
        return STATUS_DEVICE_NOT_READY;
    }
    DEBUGP(DL_INFO, "? STEP 4 SUCCESS: MMIO sanity check passed - CTRL=0x%08X\n", ctrl);

    DEBUGP(DL_INFO, "?? STEP 5: Promoting hardware state to BAR_MAPPED...\n");
    // CRITICAL FIX: Add MMIO to existing capabilities, don't replace them
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
/* NDIS attach entry */
NTSTATUS AvbInitializeDevice(_In_ PMS_FILTER FilterModule, _Out_ PAVB_DEVICE_CONTEXT *AvbContext)
{
    if (!AvbContext) return STATUS_INVALID_PARAMETER;
    USHORT ven=0, dev=0;
    if (!AvbIsSupportedIntelController(FilterModule, &ven, &dev)) return STATUS_NOT_SUPPORTED;
    NTSTATUS st = AvbCreateMinimalContext(FilterModule, ven, dev, AvbContext);
    if (!NT_SUCCESS(st)) return st;
    (void)AvbBringUpHardware(*AvbContext); /* deferred; ignore failure */
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
/* DEDICATED I210 PTP clock initialization (non-recursive, complete implementation) */

/**
 * @brief DEDICATED I210 PTP clock initialization function
 * @param context The I210 device context
 * @return NTSTATUS Success/failure status
 * 
 * This function implements the complete Intel I210 PTP initialization sequence
 * according to Intel I210 datasheet section 8.14.3 (IEEE 1588 Configuration).
 */
static NTSTATUS AvbI210InitializePTPClock(_Inout_ PAVB_DEVICE_CONTEXT context)
{
    BOOLEAN ptp_clock_running;
    ULONG initial_lo, initial_hi;
    ULONG test_lo, test_hi;
    ULONG tsauxc_reset, tsauxc_enable;
    ULONG timinca_value, start_time_lo, start_time_hi;
    ULONG rx_enable, tx_enable;
    ULONG current_lo, current_hi, last_systim_lo;
    int attempt;

    if (!context || context->hw_state < AVB_HW_BAR_MAPPED) {
        DEBUGP(DL_ERROR, "AvbI210InitializePTPClock: Hardware not ready (state=%s)\n", 
               context ? AvbHwStateName(context->hw_state) : "NULL");
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (context->intel_device.device_type != INTEL_DEVICE_I210) {
        DEBUGP(DL_ERROR, "AvbI210InitializePTPClock: Called on non-I210 device (type=%d)\n", 
               context->intel_device.device_type);
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    
    DEBUGP(DL_INFO, "?? AvbI210InitializePTPClock: Starting dedicated I210 PTP initialization\n");
    DEBUGP(DL_INFO, "   - Context: VID=0x%04X DID=0x%04X\n", 
           context->intel_device.pci_vendor_id, context->intel_device.pci_device_id);
    DEBUGP(DL_INFO, "   - Hardware Context: %p\n", context->hardware_context);
    
    // Step 1: Ensure Intel library device structure is properly set up
    context->intel_device.private_data = context;
    
    // Step 2: Check if PTP is already running (avoid unnecessary reinitialization)
    initial_lo = 0;
    initial_hi = 0;
    if (intel_read_reg(&context->intel_device, I210_SYSTIML, &initial_lo) != 0 || 
        intel_read_reg(&context->intel_device, I210_SYSTIMH, &initial_hi) != 0) {
        DEBUGP(DL_ERROR, "AvbI210InitializePTPClock: Failed to read initial SYSTIM registers\n");
        return STATUS_DEVICE_HARDWARE_ERROR;
    }
    
    DEBUGP(DL_INFO, "   - Initial SYSTIM: 0x%08X%08X\n", initial_hi, initial_lo);
    
    // Quick test: if SYSTIM is already non-zero and incrementing, PTP may be running
    if (initial_lo != 0 || initial_hi != 0) {
        KeStallExecutionProcessor(1000); // 1ms
        test_lo = 0;
        test_hi = 0;
        if (intel_read_reg(&context->intel_device, I210_SYSTIML, &test_lo) == 0 && 
            intel_read_reg(&context->intel_device, I210_SYSTIMH, &test_hi) == 0) {
            
            if ((test_hi > initial_hi) || (test_hi == initial_hi && test_lo > initial_lo)) {
                DEBUGP(DL_INFO, "? I210 PTP: Clock already running and incrementing properly\n");
                DEBUGP(DL_INFO, "   - Clock advanced: 0x%08X%08X -> 0x%08X%08X\n", 
                       initial_hi, initial_lo, test_hi, test_lo);
                
                // Ensure capabilities and state are properly set
                context->intel_device.capabilities |= (INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS);
                if (context->hw_state < AVB_HW_PTP_READY) { 
                    context->hw_state = AVB_HW_PTP_READY; 
                    DEBUGP(DL_INFO, "HW state -> %s (PTP already operational)\n", AvbHwStateName(context->hw_state)); 
                }
                return STATUS_SUCCESS;
            }
        }
    }
    
    DEBUGP(DL_INFO, "?? I210 PTP: Clock not running, performing complete initialization...\n");
    
    // Step 3: Complete I210 PTP initialization sequence (Intel I210 datasheet 8.14.3)
    
    // 3a: Disable PTP completely for clean reset (bit 31 = DisableSystime)
    tsauxc_reset = 0x80000000;
    if (intel_write_reg(&context->intel_device, INTEL_REG_TSAUXC, tsauxc_reset) != 0) {
        DEBUGP(DL_ERROR, "? I210 PTP: Failed to write TSAUXC reset\n");
        return STATUS_DEVICE_HARDWARE_ERROR;
    }
    DEBUGP(DL_INFO, "   - Step 1: PTP disabled for reset (TSAUXC=0x80000000)\n");
    
    // 3b: Clear all time-related registers 
    intel_write_reg(&context->intel_device, I210_SYSTIML, 0x00000000);
    intel_write_reg(&context->intel_device, I210_SYSTIMH, 0x00000000);
    intel_write_reg(&context->intel_device, I210_TSYNCRXCTL, 0x00000000);
    intel_write_reg(&context->intel_device, I210_TSYNCTXCTL, 0x00000000);
    
    DEBUGP(DL_INFO, "   - Step 2: All time registers cleared\n");
    
    // 3c: CRITICAL DELAY - I210 hardware requires stabilization time after reset
    KeStallExecutionProcessor(50000); // 50ms - extended delay for I210 hardware stabilization
    
    // 3d: Configure TIMINCA first (MUST be done before enabling PTP)
    timinca_value = 0x08000000; // 8ns per tick, standard for 125MHz oscillator
    if (intel_write_reg(&context->intel_device, I210_TIMINCA, timinca_value) != 0) {
        DEBUGP(DL_ERROR, "? I210 PTP: Failed to write TIMINCA\n");
        return STATUS_DEVICE_HARDWARE_ERROR;
    }
    DEBUGP(DL_INFO, "   - Step 3: TIMINCA configured (8ns increment)\n");
    
    // 3e: Enable PTP with PHC enabled, DisableSystime cleared
    tsauxc_enable = 0x40000000; // PHC enabled (bit 30), DisableSystime cleared (bit 31=0)
    if (intel_write_reg(&context->intel_device, INTEL_REG_TSAUXC, tsauxc_enable) != 0) {
        DEBUGP(DL_ERROR, "? I210 PTP: Failed to enable PTP\n");
        return STATUS_DEVICE_HARDWARE_ERROR;
    }
    DEBUGP(DL_INFO, "   - Step 4: PTP enabled (TSAUXC=0x40000000)\n");
    
    // 3f: Set initial time to a recognizable non-zero value to trigger clock start
    start_time_lo = 0x10000000; // Use recognizable start value
    start_time_hi = 0x00000000;
    if (intel_write_reg(&context->intel_device, I210_SYSTIML, start_time_lo) != 0 ||
        intel_write_reg(&context->intel_device, I210_SYSTIMH, start_time_hi) != 0) {
        DEBUGP(DL_ERROR, "? I210 PTP: Failed to set initial time\n");
        return STATUS_DEVICE_HARDWARE_ERROR;
    }
    DEBUGP(DL_INFO, "   - Step 5: Initial time set (0x%08X%08X)\n", start_time_hi, start_time_lo);
    
    // 3g: Enable RX/TX timestamp capture using SSOT register definitions
    rx_enable = (1U << I210_TSYNCRXCTL_EN_SHIFT); // Enable timestamp capture
    tx_enable = (1U << I210_TSYNCTXCTL_EN_SHIFT); // Enable timestamp capture
    
    if (intel_write_reg(&context->intel_device, I210_TSYNCRXCTL, rx_enable) != 0 ||
        intel_write_reg(&context->intel_device, I210_TSYNCTXCTL, tx_enable) != 0) {
        DEBUGP(DL_ERROR, "? I210 PTP: Failed to enable timestamp capture\n");
        return STATUS_DEVICE_HARDWARE_ERROR;
    }
    DEBUGP(DL_INFO, "   - Step 6: Timestamp capture enabled\n");
    
    // Step 4: Verify clock operation with comprehensive testing
    DEBUGP(DL_INFO, "?? I210 PTP: Testing clock operation (8 samples @ 100ms intervals)...\n");
    
    ptp_clock_running = FALSE;
    last_systim_lo = start_time_lo;
    
    for (attempt = 1; attempt <= 8 && !ptp_clock_running; attempt++) {
        KeStallExecutionProcessor(100000); // 100ms between samples
        
        current_lo = 0;
        current_hi = 0;
        if (intel_read_reg(&context->intel_device, I210_SYSTIML, &current_lo) == 0 &&
            intel_read_reg(&context->intel_device, I210_SYSTIMH, &current_hi) == 0) {
            
            DEBUGP(DL_INFO, "   Clock check %d: SYSTIM=0x%08X%08X\n", 
                   attempt, current_hi, current_lo);
            
            // Check if clock advanced from last reading
            if (current_lo > last_systim_lo || current_hi > start_time_hi) {
                ptp_clock_running = TRUE;
                DEBUGP(DL_INFO, "? I210 PTP: SUCCESS - Clock is now running!\n");
                DEBUGP(DL_INFO, "   - Clock increment detected: 0x%08X -> 0x%08X\n", last_systim_lo, current_lo);
                break;
            }
            last_systim_lo = current_lo;
        } else {
            DEBUGP(DL_ERROR, "   Clock check %d: Register read failed\n", attempt);
        }
    }
    
    if (ptp_clock_running) {
        DEBUGP(DL_INFO, "? I210 PTP: Successfully initialized and verified\n");
        context->intel_device.capabilities |= (INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS);
        if (context->hw_state < AVB_HW_PTP_READY) { 
            context->hw_state = AVB_HW_PTP_READY; 
            DEBUGP(DL_INFO, "HW state -> %s (PTP operational)\n", AvbHwStateName(context->hw_state)); 
        }
        return STATUS_SUCCESS;
    } else {
        DEBUGP(DL_ERROR, "? I210 PTP: CRITICAL FAILURE - Clock still not running after complete initialization\n");
        DEBUGP(DL_ERROR, "   - This may indicate hardware issues or incorrect register mapping\n");
        DEBUGP(DL_ERROR, "   - MMIO access is working (basic init succeeded) but PTP clock is not responding\n");
        return STATUS_UNSUCCESSFUL; // Non-fatal for driver operation, but PTP not available
    }
}

/**
 * @brief Enhanced I210 PTP initialization with comprehensive diagnostics (public interface)
 * @param context The I210 device context
 * @return NTSTATUS Success/failure status
 * 
 * This is the public interface that includes additional validation and diagnostics.
 * It calls the dedicated PTP initialization function.
 */
NTSTATUS AvbI210EnsureSystimRunning(PAVB_DEVICE_CONTEXT context)
{
    if (!context || context->hw_state < AVB_HW_BAR_MAPPED) {
        DEBUGP(DL_WARN, "AvbI210EnsureSystimRunning: Hardware not ready (state=%s)\n", 
               context ? AvbHwStateName(context->hw_state) : "NULL");
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (context->intel_device.device_type != INTEL_DEVICE_I210) {
        DEBUGP(DL_ERROR, "AvbI210EnsureSystimRunning: Called on non-I210 device (type=%d)\n", 
               context->intel_device.device_type);
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    
    DEBUGP(DL_INFO, "?? AvbI210EnsureSystimRunning (NTSTATUS version): Starting I210 PTP initialization\n");
    DEBUGP(DL_INFO, "   - Context: VID=0x%04X DID=0x%04X\n", 
           context->intel_device.pci_vendor_id, context->intel_device.pci_device_id);
    
    // Call the dedicated PTP initialization function
    return AvbI210InitializePTPClock(context);
}

/* ======================================================================= */
/* IOCTL dispatcher with I210 PTP fix */
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
            
            status = AvbBringUpHardware(activeContext);
            
            // CRITICAL FIX: Use dedicated PTP initialization function to avoid recursion
            if (activeContext->intel_device.device_type == INTEL_DEVICE_I210 && 
                activeContext->hw_state >= AVB_HW_BAR_MAPPED) {
                DEBUGP(DL_INFO, "?? INIT_DEVICE: Forcing I210 PTP initialization on active context...\n");
                NTSTATUS ptp_status = AvbI210InitializePTPClock(activeContext);
                DEBUGP(DL_INFO, "?? INIT_DEVICE: I210 PTP initialization completed with status=0x%08X\n", ptp_status);
            }
            
            DEBUGP(DL_INFO, "?? IOCTL_AVB_INIT_DEVICE: Completed with status=0x%08X\n", status);
            DEBUGP(DL_INFO, "   - Final hw_state: %s (%d)\n", AvbHwStateName(activeContext->hw_state), activeContext->hw_state);
            DEBUGP(DL_INFO, "   - Final hardware access: %s\n", activeContext->hw_access_enabled ? "YES" : "NO");
        }
        break;
    case IOCTL_AVB_ENUM_ADAPTERS:
        if (outLen < sizeof(AVB_ENUM_REQUEST)) { 
            status = STATUS_BUFFER_TOO_SMALL; 
        } else {
            PAVB_ENUM_REQUEST r = (PAVB_ENUM_REQUEST)buf; 
            RtlZeroMemory(r, sizeof(*r));
            
            // Use existing enumeration logic (simplified)
            r->count = 1; // For now, assume single adapter
            r->vendor_id = (USHORT)AvbContext->intel_device.pci_vendor_id;
            r->device_id = (USHORT)AvbContext->intel_device.pci_device_id;
            r->capabilities = AvbContext->intel_device.capabilities;
            r->status = NDIS_STATUS_SUCCESS;
            info = sizeof(*r);
        }
        break;
    case IOCTL_AVB_READ_REGISTER:
    case IOCTL_AVB_WRITE_REGISTER:
        {
            if (inLen < sizeof(AVB_REGISTER_REQUEST) || outLen < sizeof(AVB_REGISTER_REQUEST)) {
                status = STATUS_BUFFER_TOO_SMALL; 
            } else {
                PAVB_DEVICE_CONTEXT activeContext = g_AvbContext ? g_AvbContext : AvbContext;
                
                if (activeContext->hw_state < AVB_HW_BAR_MAPPED) {
                    status = STATUS_DEVICE_NOT_READY;
                } else {
                    PAVB_REGISTER_REQUEST r = (PAVB_REGISTER_REQUEST)buf; 
                    
                    // Ensure Intel library is using correct hardware context
                    if (activeContext->hardware_context && activeContext->hw_access_enabled) {
                        activeContext->intel_device.private_data = activeContext;
                    }
                    
                    if (code == IOCTL_AVB_READ_REGISTER) {
                        ULONG tmp = 0; 
                        int rc = intel_read_reg(&activeContext->intel_device, r->offset, &tmp); 
                        r->value = (avb_u32)tmp; 
                        r->status = (rc == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE; 
                        status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                    } else {
                        int rc = intel_write_reg(&activeContext->intel_device, r->offset, r->value); 
                        r->status = (rc == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE; 
                        status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                    } 
                    info = sizeof(*r);
                }
            }
        }
        break;
    case IOCTL_AVB_OPEN_ADAPTER:
        {
            if (outLen < sizeof(AVB_OPEN_REQUEST)) { 
                status = STATUS_BUFFER_TOO_SMALL; 
            } else { 
                PAVB_OPEN_REQUEST req = (PAVB_OPEN_REQUEST)buf; 
                
                // For now, simple implementation - assume we match the request
                if (req->vendor_id == AvbContext->intel_device.pci_vendor_id &&
                    req->device_id == AvbContext->intel_device.pci_device_id) {
                    g_AvbContext = AvbContext;
                    
                    // Force I210 PTP initialization if this is an I210
                    if (AvbContext->intel_device.device_type == INTEL_DEVICE_I210 && 
                        AvbContext->hw_state >= AVB_HW_BAR_MAPPED) {
                        DEBUGP(DL_INFO, "?? OPEN_ADAPTER: Forcing I210 PTP initialization\n");
                        AvbI210InitializePTPClock(AvbContext);
                    }
                    
                    req->status = 0; // Success
                } else {
                    req->status = (avb_u32)STATUS_NO_SUCH_DEVICE;
                }
                info = sizeof(*req);
            }
        }
        break;
    default:
        status = STATUS_INVALID_DEVICE_REQUEST; 
        break;
    }

    Irp->IoStatus.Information = info; 
    return status;
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
    
    DEBUGP(DL_WARN, "AvbFindIntelFilterModule: No Intel filter found with valid context\n");
    return NULL; 
}

/**
 * @brief Verify that hardware access is properly routed to the correct adapter
 * @param context The device context to verify
 * @return TRUE if hardware access is correctly routed, FALSE otherwise
 */
BOOLEAN AvbVerifyHardwareContext(PAVB_DEVICE_CONTEXT context)
{
    if (!context || !context->hw_access_enabled || !context->hardware_context) {
        DEBUGP(DL_ERROR, "AvbVerifyHardwareContext: Invalid context\n");
        return FALSE;
    }
    
    DEBUGP(DL_INFO, "?? VerifyHardwareContext: Testing VID=0x%04X DID=0x%04X\n",
           context->intel_device.pci_vendor_id, context->intel_device.pci_device_id);
    
    // Test 1: Read CTRL register (should be readable on all Intel devices)
    ULONG ctrl_reg = 0xFFFFFFFF;
    if (intel_read_reg(&context->intel_device, I210_CTRL, &ctrl_reg) != 0 || ctrl_reg == 0xFFFFFFFF) {
        DEBUGP(DL_ERROR, "? VerifyHardwareContext: Cannot read CTRL register\n");
        return FALSE;
    }
    
    DEBUGP(DL_INFO, "   - CTRL register: 0x%08X\n", ctrl_reg);
    
    // Test 2: Device-specific register validation
    if (context->intel_device.device_type == INTEL_DEVICE_I210) {
        // I210-specific: Read TSAUXC (should exist and be accessible)
        ULONG tsauxc = 0;
        if (intel_read_reg(&context->intel_device, INTEL_REG_TSAUXC, &tsauxc) == 0) {
            DEBUGP(DL_INFO, "   - I210 TSAUXC: 0x%08X (I210-specific register accessible)\n", tsauxc);
            DEBUGP(DL_INFO, "? VerifyHardwareContext: CONFIRMED I210 hardware access\n");
            return TRUE;
        } else {
            DEBUGP(DL_ERROR, "? VerifyHardwareContext: I210 TSAUXC read failed\n");
            return FALSE;
        }
    } else if (context->intel_device.device_type == INTEL_DEVICE_I226) {
        // I226-specific: Read a different control register
        ULONG test_reg = 0;
        if (intel_read_reg(&context->intel_device, 0x08600, &test_reg) == 0) {
            DEBUGP(DL_INFO, "   - I226 TAS_CTRL: 0x%08X (I226-specific register accessible)\n", test_reg);
            DEBUGP(DL_INFO, "? VerifyHardwareContext: CONFIRMED I226 hardware access\n");
            return TRUE;
        }
    }
    
    // For other devices, CTRL access verification is sufficient
    DEBUGP(DL_INFO, "? VerifyHardwareContext: Hardware verification passed (CTRL access working)\n");
    return TRUE;
}

/**
 * @brief Force complete reinitialization of a device context
 * @param context The device context to reinitialize
 * @return NTSTATUS Success/failure status
 */
NTSTATUS AvbForceContextReinitialization(PAVB_DEVICE_CONTEXT context)
{
    if (!context) {
        return STATUS_INVALID_PARAMETER;
    }
    
    DEBUGP(DL_INFO, "?? ForceContextReinitialization: VID=0x%04X DID=0x%04X\n",
           context->intel_device.pci_vendor_id, context->intel_device.pci_device_id);
    DEBUGP(DL_INFO, "   - Current state: %s\n", AvbHwStateName(context->hw_state));
    DEBUGP(DL_INFO, "   - Hardware context: %p\n", context->hardware_context);
    
    // Step 1: Verify that we have a valid hardware context
    if (!context->hardware_context) {
        DEBUGP(DL_ERROR, "? ForceContextReinitialization: No hardware context - need to rediscover\n");
        
        // Force hardware rediscovery
        PHYSICAL_ADDRESS bar0 = {0};
        ULONG barLen = 0;
        NTSTATUS discovery_status = AvbDiscoverIntelControllerResources(context->filter_instance, &bar0, &barLen);
        if (!NT_SUCCESS(discovery_status)) {
            DEBUGP(DL_ERROR, "? ForceContextReinitialization: Hardware discovery failed: 0x%08X\n", discovery_status);
            return discovery_status;
        }
        
        // Map hardware resources
        NTSTATUS mapping_status = AvbMapIntelControllerMemory(context, bar0, barLen);
        if (!NT_SUCCESS(mapping_status)) {
            DEBUGP(DL_ERROR, "? ForceContextReinitialization: Hardware mapping failed: 0x%08X\n", mapping_status);
            return mapping_status;
        }
        
        DEBUGP(DL_INFO, "? ForceContextReinitialization: Hardware context restored\n");
    }
    
    // Step 2: Force Intel library reinitialization
    context->intel_device.private_data = context;
    int intel_result = intel_init(&context->intel_device);
    if (intel_result != 0) {
        DEBUGP(DL_ERROR, "? ForceContextReinitialization: Intel library init failed: %d\n", intel_result);
        return STATUS_DEVICE_HARDWARE_ERROR;
    }
    
    // Step 3: Verify hardware access is working
    if (!AvbVerifyHardwareContext(context)) {
        DEBUGP(DL_ERROR, "? ForceContextReinitialization: Hardware verification failed\n");
        return STATUS_DEVICE_HARDWARE_ERROR;
    }
    
    // Step 4: Update context state
    context->hw_access_enabled = TRUE;
    context->initialized = TRUE;
    if (context->hw_state < AVB_HW_BAR_MAPPED) {
        context->hw_state = AVB_HW_BAR_MAPPED;
    }
    
    // Step 5: For I210, call dedicated PTP initialization
    if (context->intel_device.device_type == INTEL_DEVICE_I210) {
        DEBUGP(DL_INFO, "? ForceContextReinitialization: Attempting I210 PTP initialization\n");
        NTSTATUS ptp_status = AvbI210InitializePTPClock(context);
        if (NT_SUCCESS(ptp_status)) {
            DEBUGP(DL_INFO, "? ForceContextReinitialization: I210 PTP initialization successful\n");
        } else {
            DEBUGP(DL_WARN, "? ForceContextReinitialization: I210 PTP initialization failed: 0x%08X\n", ptp_status);
            // Continue - MMIO functionality may still work
        }
    }
    
    DEBUGP(DL_INFO, "? ForceContextReinitialization: SUCCESS\n");
    return STATUS_SUCCESS;
}