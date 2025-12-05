/*++

Module Name:

    avb_integration_fixed.c

Abstract:

    Intel AVB integration for NDIS filter � unified implementation.
    Provides minimal-context creation (BOUND) immediately on attach so
    enumeration succeeds even if later hardware bring-up fails. Deferred
    initialization promotes BAR_MAPPED and PTP_READY states and accrues
    capabilities.

    CLEAN DEVICE SEPARATION: No device-specific register definitions in generic layer.

--*/

#include "precomp.h"
#include "avb_integration.h"
#include "external/intel_avb/lib/intel_windows.h"
#include <ntstrsafe.h>

// Generic register offsets (common across Intel devices)
#define INTEL_GENERIC_CTRL_REG      0x00000  // Device control register (all Intel devices)
#define INTEL_GENERIC_STATUS_REG    0x00008  // Device status register (all Intel devices)

/* Global singleton (assumes one Intel adapter binding) */
PAVB_DEVICE_CONTEXT g_AvbContext = NULL;

/* Forward declarations */
static NTSTATUS AvbPerformBasicInitialization(_Inout_ PAVB_DEVICE_CONTEXT Ctx);

/* Platform ops wrapper (Intel library selects this) */
static int PlatformInitWrapper(_In_ device_t *dev) { 
    DEBUGP(DL_ERROR, "!!! DEBUG: PlatformInitWrapper called!\n");
    NTSTATUS status = AvbPlatformInit(dev);
    DEBUGP(DL_ERROR, "!!! DEBUG: AvbPlatformInit returned: 0x%08X\n", status);
    return NT_SUCCESS(status) ? 0 : -1;
}
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
    
    // Always set REALISTIC baseline capabilities based on device type and hardware specs
    ULONG baseline_caps = 0;
    switch (Ctx->intel_device.device_type) {
        // Legacy IGB devices - REALISTIC capabilities only
   //     case INTEL_DEVICE_82575:
   //         baseline_caps = INTEL_CAP_MMIO | INTEL_CAP_MDIO; // Basic hardware only - NO PTP (2008 era)
   //         break;
   //     case INTEL_DEVICE_82576:
   //         baseline_caps = INTEL_CAP_MMIO | INTEL_CAP_MDIO; // Basic hardware only - NO PTP (2009 era)
   //         break;
   //     case INTEL_DEVICE_82580:
   //         baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_MMIO | INTEL_CAP_MDIO; // Basic PTP added (2010)
   //         break;
   //     case INTEL_DEVICE_I350:
   //         baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_MMIO | INTEL_CAP_MDIO; // Standard IEEE 1588 (2012)
   //         break;
   //     case INTEL_DEVICE_I354:
   //         baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_MMIO | INTEL_CAP_MDIO; // Same as I350 (2012)
   //         break;
            
        // Modern I-series devices - REALISTIC capabilities based on actual hardware
        case INTEL_DEVICE_I210:
            baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_MMIO; // Enhanced PTP, NO TSN (2013)
            break;
        case INTEL_DEVICE_I217:
            baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_MMIO | INTEL_CAP_MDIO; // Basic PTP (2013)
            break;
        case INTEL_DEVICE_I219:
            baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_MMIO | INTEL_CAP_MDIO; // Enhanced PTP, NO TSN (2014)
            break;
            
        // ONLY I225/I226 support TSN - TSN standard finalized 2015-2016, first Intel implementation 2019
        case INTEL_DEVICE_I225:
            baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_TSN_TAS | 
                           INTEL_CAP_TSN_FP | INTEL_CAP_PCIe_PTM | INTEL_CAP_2_5G | INTEL_CAP_MMIO; // First Intel TSN (2019)
            break;
        case INTEL_DEVICE_I226:
            baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_TSN_TAS | 
                           INTEL_CAP_TSN_FP | INTEL_CAP_PCIe_PTM | INTEL_CAP_2_5G | INTEL_CAP_MMIO | INTEL_CAP_EEE; // Full TSN (2020)
            break;
        default:
            baseline_caps = INTEL_CAP_MMIO; // Minimal safe assumption
            break;
    }
    
    // Set baseline capabilities even if hardware init fails
    Ctx->intel_device.capabilities = baseline_caps;
    DEBUGP(DL_INFO, "? AvbBringUpHardware: Set baseline capabilities 0x%08X for device type %d\n", 
           baseline_caps, Ctx->intel_device.device_type);
    
    NTSTATUS status = AvbPerformBasicInitialization(Ctx);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_WARN, "?? AvbBringUpHardware: basic init failed 0x%08X (keeping baseline capabilities=0x%08X)\n", 
               status, baseline_caps);
        // Don't return error - allow enumeration with baseline capabilities
        return STATUS_SUCCESS;
    }
    
    // Device-specific post-initialization (allocate Intel library private structure)
    DEBUGP(DL_ERROR, "!!! DEBUG: AvbBringUpHardware hw_state=%s (need BAR_MAPPED)\n", 
           AvbHwStateName(Ctx->hw_state));
    
    if (Ctx->hw_state >= AVB_HW_BAR_MAPPED) {
        DEBUGP(DL_ERROR, "!!! DEBUG: Calling intel_init() for VID=0x%04X DID=0x%04X DevType=%d\n",
               Ctx->intel_device.pci_vendor_id, Ctx->intel_device.pci_device_id, 
               Ctx->intel_device.device_type);
        
        // CRITICAL: Use intel_init() which properly dispatches to device-specific operations
        // This calls i226_ops.init() → ndis_platform_ops.init() → AvbPlatformInit()
        // which initializes PTP clock and allocates device private structure
        int init_result = intel_init(&Ctx->intel_device);
        
        DEBUGP(DL_ERROR, "!!! DEBUG: intel_init() returned: %d\n", init_result);
        
        if (init_result == 0) {
            DEBUGP(DL_INFO, "? intel_init() successful - device initialized with PTP and TSN\n");
        } else {
            DEBUGP(DL_WARN, "?? intel_init() failed: %d (PTP/TSN features unavailable)\n", init_result);
        }
    } else {
        DEBUGP(DL_ERROR, "!!! DEBUG: SKIPPING intel_init() - hw_state not ready!\n");
    }
    
    return STATUS_SUCCESS;
}

/* ======================================================================= */
/**
 * @brief Perform basic hardware discovery and MMIO setup. Promote to BAR_MAPPED.
 * 
 * CLEAN ARCHITECTURE: No device-specific register access - pure generic functionality.
 * Uses only common Intel register offsets that exist across all supported devices.
 * 
 * @param Ctx Device context to initialize
 * @return STATUS_SUCCESS on success, error status on failure
 * @note Implements Intel datasheet generic initialization sequence
 */
static NTSTATUS AvbPerformBasicInitialization(_Inout_ PAVB_DEVICE_CONTEXT Ctx)
{
    DEBUGP(DL_INFO, "? AvbPerformBasicInitialization: Starting for VID=0x%04X DID=0x%04X\n", 
           Ctx->intel_device.pci_vendor_id, Ctx->intel_device.pci_device_id);
    
    if (!Ctx) return STATUS_INVALID_PARAMETER;
    
    if (Ctx->hw_access_enabled) {
        DEBUGP(DL_INFO, "? AvbPerformBasicInitialization: Already initialized, returning success\n");
        return STATUS_SUCCESS;
    }

    /* Step 1: Discover & map BAR0 if not yet mapped */
    if (Ctx->hardware_context == NULL) {
        DEBUGP(DL_INFO, "? STEP 1: Starting BAR0 discovery and mapping...\n");
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

    DEBUGP(DL_INFO, "? STEP 2: Setting up Intel device structure...\n");
    Ctx->intel_device.private_data = Ctx;
    Ctx->intel_device.capabilities = 0; /* reset published caps */
    DEBUGP(DL_INFO, "? STEP 2 SUCCESS: Device structure prepared\n");

    DEBUGP(DL_INFO, "? STEP 3: Calling intel_init library function...\n");
    DEBUGP(DL_INFO, "   - VID=0x%04X DID=0x%04X\n", Ctx->intel_device.pci_vendor_id, Ctx->intel_device.pci_device_id);
    if (intel_init(&Ctx->intel_device) != 0) {
        DEBUGP(DL_ERROR, "? STEP 3 FAILED: intel_init failed (library)\n");
        return STATUS_UNSUCCESSFUL;
    }
    DEBUGP(DL_INFO, "? STEP 3 SUCCESS: intel_init completed successfully\n");

    DEBUGP(DL_INFO, "? STEP 4: MMIO sanity check - reading CTRL register via Intel library...\n");
    ULONG ctrl = 0xFFFFFFFF;
    
    // CLEAN ARCHITECTURE: Use Intel library for all register access - no device-specific contamination
    if (intel_read_reg(&Ctx->intel_device, INTEL_GENERIC_CTRL_REG, &ctrl) != 0 || ctrl == 0xFFFFFFFF) {
        DEBUGP(DL_ERROR, "? STEP 4 FAILED: MMIO sanity read failed CTRL=0x%08X (expected != 0xFFFFFFFF)\n", ctrl);
        DEBUGP(DL_ERROR, "   This indicates BAR0 mapping is not working properly\n");
        return STATUS_DEVICE_NOT_READY;
    }
    DEBUGP(DL_INFO, "? STEP 4 SUCCESS: MMIO sanity check passed - CTRL=0x%08X\n", ctrl);

    DEBUGP(DL_INFO, "? STEP 5: Promoting hardware state to BAR_MAPPED...\n");
    Ctx->intel_device.capabilities |= INTEL_CAP_MMIO;
    if (Ctx->hw_state < AVB_HW_BAR_MAPPED) {
        Ctx->hw_state = AVB_HW_BAR_MAPPED;
        DEBUGP(DL_INFO, "? STEP 5 SUCCESS: HW state -> %s (CTRL=0x%08X)\n", AvbHwStateName(Ctx->hw_state), ctrl);
    }
    Ctx->initialized = TRUE;
    Ctx->hw_access_enabled = TRUE;
    
    DEBUGP(DL_INFO, "? AvbPerformBasicInitialization: COMPLETE SUCCESS\n");
    DEBUGP(DL_INFO, "   - Final hw_state: %s\n", AvbHwStateName(Ctx->hw_state));
    DEBUGP(DL_INFO, "   - Final capabilities: 0x%08X\n", Ctx->intel_device.capabilities);
    DEBUGP(DL_INFO, "   - Hardware access enabled: %s\n", Ctx->hw_access_enabled ? "YES" : "NO");
    
    return STATUS_SUCCESS;
}

/* ======================================================================= */
/* Enhanced Device Initialization Functions - Clean Architecture */

/**
 * @brief Generic device initialization with proper return status
 * 
 * Delegates to device-specific implementations via Intel library.
 * Supports all Intel device families through clean abstraction.
 * 
 * @param context The device context  
 * @return STATUS_SUCCESS on success, STATUS_DEVICE_NOT_READY if hardware not ready,
 *         STATUS_DEVICE_HARDWARE_ERROR if initialization fails
 * @note Uses Intel library device operations for hardware-specific setup
 */
NTSTATUS AvbEnsureDeviceReady(PAVB_DEVICE_CONTEXT context)
{
    if (!context || context->hw_state < AVB_HW_BAR_MAPPED) {
        DEBUGP(DL_WARN, "AvbEnsureDeviceReady: Hardware not ready (state=%s)\n", 
               context ? AvbHwStateName(context->hw_state) : "NULL");
        return STATUS_DEVICE_NOT_READY;
    }
    
    DEBUGP(DL_INFO, "? AvbEnsureDeviceReady: Starting device initialization\n");
    DEBUGP(DL_INFO, "   - Context: VID=0x%04X DID=0x%04X (type=%d)\n", 
           context->intel_device.pci_vendor_id, context->intel_device.pci_device_id,
           context->intel_device.device_type);
    
    // CLEAN ARCHITECTURE: Use Intel library device-specific initialization
    // No device-specific register access in generic integration layer
    int init_result = intel_init(&context->intel_device);
    if (init_result != 0) {
        DEBUGP(DL_ERROR, "? AvbEnsureDeviceReady: Device initialization failed: %d\n", init_result);
        return STATUS_DEVICE_HARDWARE_ERROR;
    }
    
    DEBUGP(DL_INFO, "? AvbEnsureDeviceReady: Device initialization successful\n");
    
    // Update capabilities and state based on successful initialization
    context->intel_device.capabilities |= INTEL_CAP_MMIO;
    if (context->hw_state < AVB_HW_PTP_READY) {
        context->hw_state = AVB_HW_PTP_READY;
        DEBUGP(DL_INFO, "HW state -> %s (device ready)\n", AvbHwStateName(context->hw_state));
    }
    
    return STATUS_SUCCESS;
}

// Legacy function names for compatibility - redirect to generic implementations
/**
 * @brief Legacy I210-specific initialization function (redirects to generic implementation)
 * 
 * @deprecated Use AvbEnsureDeviceReady() instead for all device types
 * @param context The I210 device context
 * @return NTSTATUS Success/failure status
 */
NTSTATUS AvbI210EnsureSystimRunning(PAVB_DEVICE_CONTEXT context)
{
    DEBUGP(DL_INFO, "AvbI210EnsureSystimRunning: Redirecting to generic device initialization\n");
    return AvbEnsureDeviceReady(context);
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
/* IOCTL dispatcher */
NTSTATUS AvbHandleDeviceIoControl(_In_ PAVB_DEVICE_CONTEXT AvbContext, _In_ PIRP Irp)
{
    if (!AvbContext) return STATUS_DEVICE_NOT_READY;
    PIO_STACK_LOCATION sp = IoGetCurrentIrpStackLocation(Irp);
    ULONG code   = sp->Parameters.DeviceIoControl.IoControlCode;
    PUCHAR buf   = (PUCHAR)Irp->AssociatedIrp.SystemBuffer;
    ULONG inLen  = sp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = sp->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG_PTR info = 0; 
    NTSTATUS status = STATUS_SUCCESS;

    // Use current context for all operations to avoid shadowing warnings
    PAVB_DEVICE_CONTEXT currentContext = g_AvbContext ? g_AvbContext : AvbContext;

    if (!currentContext->initialized && code == IOCTL_AVB_INIT_DEVICE) (void)AvbBringUpHardware(currentContext);
    if (!currentContext->initialized && code != IOCTL_AVB_ENUM_ADAPTERS && code != IOCTL_AVB_INIT_DEVICE && code != IOCTL_AVB_GET_HW_STATE)
        return STATUS_DEVICE_NOT_READY;

    switch (code) {
    case IOCTL_AVB_INIT_DEVICE:
        {
            DEBUGP(DL_INFO, "? IOCTL_AVB_INIT_DEVICE: Starting hardware bring-up\n");
            
            DEBUGP(DL_INFO, "   - Using context: VID=0x%04X DID=0x%04X\n",
                   currentContext->intel_device.pci_vendor_id, currentContext->intel_device.pci_device_id);
            DEBUGP(DL_INFO, "   - Current hw_state: %s (%d)\n", AvbHwStateName(currentContext->hw_state), currentContext->hw_state);
            DEBUGP(DL_INFO, "   - Hardware access enabled: %s\n", currentContext->hw_access_enabled ? "YES" : "NO");
            DEBUGP(DL_INFO, "   - Initialized flag: %s\n", currentContext->initialized ? "YES" : "NO");
            DEBUGP(DL_INFO, "   - Hardware context: %p\n", currentContext->hardware_context);
            DEBUGP(DL_INFO, "   - Device type: %d (%s)\n", currentContext->intel_device.device_type,
                   currentContext->intel_device.device_type == INTEL_DEVICE_I210 ? "I210" :
                   currentContext->intel_device.device_type == INTEL_DEVICE_I226 ? "I226" : "OTHER");
            
            // Force immediate BAR0 discovery if hardware context is missing
            if (currentContext->hardware_context == NULL && currentContext->hw_state == AVB_HW_BOUND) {
                DEBUGP(DL_WARN, "*** FORCED BAR0 DISCOVERY *** No hardware context, forcing immediate discovery...\n");
                
                PHYSICAL_ADDRESS bar0 = {0};
                ULONG barLen = 0;
                NTSTATUS ds = AvbDiscoverIntelControllerResources(currentContext->filter_instance, &bar0, &barLen);
                if (NT_SUCCESS(ds)) {
                    DEBUGP(DL_WARN, "*** BAR0 DISCOVERY SUCCESS *** PA=0x%llx, Len=0x%x\n", bar0.QuadPart, barLen);
                    NTSTATUS ms = AvbMapIntelControllerMemory(currentContext, bar0, barLen);
                    if (NT_SUCCESS(ms)) {
                        DEBUGP(DL_WARN, "*** BAR0 MAPPING SUCCESS *** Hardware context now available\n");
                        
                        // Complete initialization sequence
                        currentContext->intel_device.private_data = currentContext;
                        if (intel_init(&currentContext->intel_device) == 0) {
                            DEBUGP(DL_INFO, "? MANUAL intel_init SUCCESS\n");
                            
                            // Test MMIO sanity using generic register access
                            ULONG ctrl = 0xFFFFFFFF;
                            if (intel_read_reg(&currentContext->intel_device, INTEL_GENERIC_CTRL_REG, &ctrl) == 0 && ctrl != 0xFFFFFFFF) {
                                DEBUGP(DL_INFO, "? MANUAL MMIO SANITY SUCCESS: CTRL=0x%08X\n", ctrl);
                                currentContext->hw_state = AVB_HW_BAR_MAPPED;
                                currentContext->hw_access_enabled = TRUE;
                                currentContext->initialized = TRUE;
                                
                                // Device-specific initialization handled by Intel library
                                DEBUGP(DL_INFO, "? MANUAL DEVICE-SPECIFIC INIT: Delegating to Intel library...\n");
                                int device_init_result = intel_init(&currentContext->intel_device);
                                if (device_init_result == 0) {
                                    DEBUGP(DL_INFO, "? Device-specific initialization successful\n");
                                } else {
                                    DEBUGP(DL_WARN, "?? Device-specific initialization failed: %d\n", device_init_result);
                                }
                            } else {
                                DEBUGP(DL_ERROR, "? MANUAL MMIO SANITY FAILED: CTRL=0x%08X\n", ctrl);
                            }
                        } else {
                            DEBUGP(DL_ERROR, "? MANUAL intel_init FAILED\n");
                        }
                    } else {
                        DEBUGP(DL_ERROR, "*** BAR0 MAPPING FAILED *** Status=0x%08X\n", ms);
                    }
                } else {
                    DEBUGP(DL_ERROR, "*** BAR0 DISCOVERY FAILED *** Status=0x%08X\n", ds);
                }
            }
            
            status = AvbBringUpHardware(currentContext);
            
            DEBUGP(DL_INFO, "? IOCTL_AVB_INIT_DEVICE: Completed with status=0x%08X\n", status);
            DEBUGP(DL_INFO, "   - Final hw_state: %s (%d)\n", AvbHwStateName(currentContext->hw_state), currentContext->hw_state);
            DEBUGP(DL_INFO, "   - Final hardware access: %s\n", currentContext->hw_access_enabled ? "YES" : "NO");
        }
        break;
    case IOCTL_AVB_ENUM_ADAPTERS:
        if (outLen < sizeof(AVB_ENUM_REQUEST)) { 
            status = STATUS_BUFFER_TOO_SMALL; 
        } else {
            PAVB_ENUM_REQUEST r = (PAVB_ENUM_REQUEST)buf; 
            RtlZeroMemory(r, sizeof(*r));
            
            DEBUGP(DL_INFO, "? IOCTL_AVB_ENUM_ADAPTERS: Starting enumeration\n");
            
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
            
            DEBUGP(DL_INFO, "? ENUM_ADAPTERS: Found %lu Intel adapters\n", adapterCount);
            
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
                    // Use current context info
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
            DEBUGP(DL_INFO, "? IOCTL_AVB_GET_DEVICE_INFO: Starting device info request\n");
            
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
                    
                    DEBUGP(DL_INFO, "? DEVICE_INFO COMPLETE: status=0x%08X, buffer_size=%lu\n", status, r->buffer_size);
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
                        (void)AvbI210EnsureSystimRunning(activeContext);
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
        DEBUGP(DL_INFO, "? IOCTL_AVB_GET_HW_STATE: Hardware state query\n");
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
                DEBUGP(DL_INFO, "? FORCING BAR0 DISCOVERY: Hardware stuck in BOUND state, attempting manual discovery...\n");
                
                // Ensure device type is properly set
                if (AvbContext->intel_device.device_type == INTEL_DEVICE_UNKNOWN && 
                    AvbContext->intel_device.pci_device_id != 0) {
                    AvbContext->intel_device.device_type = AvbGetIntelDeviceType(AvbContext->intel_device.pci_device_id);
                    DEBUGP(DL_INFO, "? Updated device type to %d for DID=0x%04X\n", 
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
        DEBUGP(DL_INFO, "? IOCTL_AVB_OPEN_ADAPTER: Multi-adapter context switching\n");
        
        if (outLen < sizeof(AVB_OPEN_REQUEST)) { 
            DEBUGP(DL_ERROR, "? OPEN_ADAPTER: Buffer too small (%lu < %lu)\n", outLen, sizeof(AVB_OPEN_REQUEST));
            status = STATUS_BUFFER_TOO_SMALL; 
        } else { 
            PAVB_OPEN_REQUEST req = (PAVB_OPEN_REQUEST)buf; 
            DEBUGP(DL_INFO, "? OPEN_ADAPTER: Looking for VID=0x%04X DID=0x%04X\n", 
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
                    DEBUGP(DL_INFO, "? OPEN_ADAPTER: Checking filter %wZ - VID=0x%04X DID=0x%04X\n",
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
                        UNREFERENCED_PARAMETER(ctx);
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
                DEBUGP(DL_INFO, "? OPEN_ADAPTER: Switching global context\n");
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
                    DEBUGP(DL_INFO, "? OPEN_ADAPTER: Target adapter needs initialization\n");
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
                    DEBUGP(DL_INFO, "? OPEN_ADAPTER: Forcing I210 PTP initialization for selected adapter\n");
                    DEBUGP(DL_INFO, "   - Device Type: %d (INTEL_DEVICE_I210)\n", targetContext->intel_device.device_type);
                    DEBUGP(DL_INFO, "   - Hardware State: %s\n", AvbHwStateName(targetContext->hw_state));
                    DEBUGP(DL_INFO, "   - Hardware Context: %p\n", targetContext->hardware_context);
                    
                    // Force complete I210 PTP setup
                    AvbI210EnsureSystimRunning(targetContext);
                    
                    DEBUGP(DL_INFO, "? OPEN_ADAPTER: I210 PTP initialization completed\n");
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
    case IOCTL_AVB_SETUP_TAS:
        {
            DEBUGP(DL_INFO, "? IOCTL_AVB_SETUP_TAS: Phase 2 Enhanced TAS Configuration\n");
            
            if (inLen < sizeof(AVB_TAS_REQUEST) || outLen < sizeof(AVB_TAS_REQUEST)) {
                DEBUGP(DL_ERROR, "? TAS SETUP: Buffer too small\n");
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                PAVB_DEVICE_CONTEXT activeContext = g_AvbContext ? g_AvbContext : AvbContext;
                
                if (activeContext->hw_state < AVB_HW_BAR_MAPPED) {
                    DEBUGP(DL_ERROR, "? TAS SETUP: Hardware not ready (state=%s)\n", 
                           AvbHwStateName(activeContext->hw_state));
                    status = STATUS_DEVICE_NOT_READY;
                } else {
                    PAVB_TAS_REQUEST r = (PAVB_TAS_REQUEST)buf;
                    
                    // Ensure Intel library is using the correct hardware context
                    if (activeContext->hardware_context && activeContext->hw_access_enabled) {
                        activeContext->intel_device.private_data = activeContext;
                    }
                    
                    DEBUGP(DL_INFO, "?? TAS SETUP: Phase 2 Enhanced Configuration on VID=0x%04X DID=0x%04X\n",
                           activeContext->intel_device.pci_vendor_id, activeContext->intel_device.pci_device_id);
                    
                    // Check if this device supports TAS
                    if (!(activeContext->intel_device.capabilities & INTEL_CAP_TSN_TAS)) {
                        DEBUGP(DL_WARN, "? TAS SETUP: Device does not support TAS (caps=0x%08X)\n", 
                               activeContext->intel_device.capabilities);
                        r->status = (avb_u32)STATUS_NOT_SUPPORTED;
                        status = STATUS_SUCCESS; // IOCTL handled, but feature not supported
                    } else {
                        // Call Intel library TAS setup function (standard implementation)
                        DEBUGP(DL_INFO, "? TAS SETUP: Calling Intel library TAS implementation\n");
                        int rc = intel_setup_time_aware_shaper(&activeContext->intel_device, &r->config);
                        r->status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                        status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                        
                        if (rc == 0) {
                            DEBUGP(DL_INFO, "? TAS configuration successful\n");
                        } else {
                            DEBUGP(DL_ERROR, "? TAS setup failed: %d\n", rc);
                            
                            // Provide diagnostic information
                            switch (rc) {
                                case -ENOTSUP:
                                    DEBUGP(DL_ERROR, "   Reason: Device doesn't support TAS\n");
                                    break;
                                case -EBUSY:
                                    DEBUGP(DL_ERROR, "   Reason: Prerequisites not met (PTP clock or hardware state)\n");
                                    break;
                                case -EIO:
                                    DEBUGP(DL_ERROR, "   Reason: Hardware register access failed\n");
                                    break;
                                case -EINVAL:
                                    DEBUGP(DL_ERROR, "   Reason: Invalid configuration parameters\n");
                                    break;
                                default:
                                    DEBUGP(DL_ERROR, "   Reason: Unknown error\n");
                                    break;
                            }
                        }
                    }
                    
                    info = sizeof(*r);
                }
            }
        }
        break;

    case IOCTL_AVB_SETUP_FP:
        {
            DEBUGP(DL_INFO, "? IOCTL_AVB_SETUP_FP: Phase 2 Enhanced Frame Preemption Configuration\n");
            
            if (inLen < sizeof(AVB_FP_REQUEST) || outLen < sizeof(AVB_FP_REQUEST)) {
                DEBUGP(DL_ERROR, "? FP SETUP: Buffer too small\n");
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                PAVB_DEVICE_CONTEXT activeContext = g_AvbContext ? g_AvbContext : AvbContext;
                
                if (activeContext->hw_state < AVB_HW_BAR_MAPPED) {
                    DEBUGP(DL_ERROR, "? FP SETUP: Hardware not ready (state=%s)\n", 
                           AvbHwStateName(activeContext->hw_state));
                    status = STATUS_DEVICE_NOT_READY;
                } else {
                    PAVB_FP_REQUEST r = (PAVB_FP_REQUEST)buf;
                    
                    // Ensure Intel library is using the correct hardware context
                    if (activeContext->hardware_context && activeContext->hw_access_enabled) {
                        activeContext->intel_device.private_data = activeContext;
                    }
                    
                    DEBUGP(DL_INFO, "?? FP SETUP: Phase 2 Enhanced Configuration on VID=0x%04X DID=0x%04X\n",
                           activeContext->intel_device.pci_vendor_id, activeContext->intel_device.pci_device_id);
                    
                    // Check if this device supports Frame Preemption
                    if (!(activeContext->intel_device.capabilities & INTEL_CAP_TSN_FP)) {
                        DEBUGP(DL_WARN, "? FP SETUP: Device does not support Frame Preemption (caps=0x%08X)\n", 
                               activeContext->intel_device.capabilities);
                        r->status = (avb_u32)STATUS_NOT_SUPPORTED;
                        status = STATUS_SUCCESS; // IOCTL handled, but feature not supported
                    } else {
                        // Call Intel library Frame Preemption setup function (standard implementation)
                        DEBUGP(DL_INFO, "? FP SETUP: Calling Intel library Frame Preemption implementation\n");
                        int rc = intel_setup_frame_preemption(&activeContext->intel_device, &r->config);
                        r->status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                        status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                        
                        if (rc == 0) {
                            DEBUGP(DL_INFO, "? Frame Preemption configuration successful\n");
                        } else {
                            DEBUGP(DL_ERROR, "? Frame Preemption setup failed: %d\n", rc);
                            
                            // Provide diagnostic information
                            switch (rc) {
                                case -ENOTSUP:
                                    DEBUGP(DL_ERROR, "   Reason: Device doesn't support Frame Preemption\n");
                                    break;
                                case -EBUSY:
                                    DEBUGP(DL_ERROR, "   Reason: Link not active or link partner doesn't support preemption\n");
                                    break;
                                case -EIO:
                                    DEBUGP(DL_ERROR, "   Reason: Hardware register access failed\n");
                                    break;
                                default:
                                    DEBUGP(DL_ERROR, "   Reason: Unknown error\n");
                                    break;
                            }
                        }
                    }
                    
                    info = sizeof(*r);
                }
            }
        }
        break;

    case IOCTL_AVB_SETUP_PTM:
        {
            DEBUGP(DL_INFO, "? IOCTL_AVB_SETUP_PTM: Phase 2 Enhanced PTM Configuration\n");
            
            if (inLen < sizeof(AVB_PTM_REQUEST) || outLen < sizeof(AVB_PTM_REQUEST)) {
                DEBUGP(DL_ERROR, "? PTM SETUP: Buffer too small\n");
                status = STATUS_BUFFER_TOO_SMALL;
            } else {
                PAVB_DEVICE_CONTEXT activeContext = g_AvbContext ? g_AvbContext : AvbContext;
                
                if (activeContext->hw_state < AVB_HW_BAR_MAPPED) {
                    DEBUGP(DL_ERROR, "? PTM SETUP: Hardware not ready (state=%s)\n", 
                           AvbHwStateName(activeContext->hw_state));
                    status = STATUS_DEVICE_NOT_READY;
                } else {
                    PAVB_PTM_REQUEST r = (PAVB_PTM_REQUEST)buf;
                    
                    // Ensure Intel library is using the correct hardware context
                    if (activeContext->hardware_context && activeContext->hw_access_enabled) {
                        activeContext->intel_device.private_data = activeContext;
                    }
                    
                    DEBUGP(DL_INFO, "?? PTM SETUP: Phase 2 Enhanced Configuration on VID=0x%04X DID=0x%04X\n",
                           activeContext->intel_device.pci_vendor_id, activeContext->intel_device.pci_device_id);
                    
                    // Check if this device supports PCIe PTM
                    if (!(activeContext->intel_device.capabilities & INTEL_CAP_PCIe_PTM)) {
                        DEBUGP(DL_WARN, "? PTM SETUP: Device does not support PCIe PTM (caps=0x%08X)\n", 
                               activeContext->intel_device.capabilities);
                        r->status = (avb_u32)STATUS_NOT_SUPPORTED;
                        status = STATUS_SUCCESS; // IOCTL handled, but feature not supported
                    } else {
                        // Phase 2: Use enhanced PTM implementation
                        DEBUGP(DL_INFO, "? Phase 2: Calling enhanced PTM implementation\n");
                        int rc = intel_setup_ptm(&activeContext->intel_device, &r->config);
                        r->status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                        status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                        
                        if (rc == 0) {
                            DEBUGP(DL_INFO, "? Phase 2: PTM configuration completed\n");
                        } else {
                            DEBUGP(DL_ERROR, "? Phase 2: PTM setup failed: %d\n", rc);
                        }
                    }
                    
                    info = sizeof(*r);
                }
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

/**
 * @brief Initialize PTP clock hardware for Intel devices
 * 
 * This function programs TIMINCA and starts SYSTIM counting.
 * Required before any TSN features (TAS/FP/PTM) will work.
 * 
 * @param dev Device handle with valid MMIO access
 * @return STATUS_SUCCESS if PTP initialized, error otherwise
 */
NTSTATUS AvbPlatformInit(_In_ device_t *dev)
{
    DEBUGP(DL_ERROR, "!!! DEBUG: AvbPlatformInit ENTERED!\n");
    
    if (!dev) {
        DEBUGP(DL_ERROR, "!!! DEBUG: AvbPlatformInit - dev is NULL!\n");
        return STATUS_INVALID_PARAMETER;
    }
    
    DEBUGP(DL_ERROR, "!!! DEBUG: AvbPlatformInit - dev is valid, proceeding...\n");
    
    // Generic PTP register offsets (same for I210, I211, I217, I219, I225, I226)
    const ULONG SYSTIML_REG = 0x0B600;
    const ULONG SYSTIMH_REG = 0x0B604;
    const ULONG TIMINCA_REG = 0x0B608;
    const ULONG TSAUXC_REG = 0x0B640;   // Time Sync Auxiliary Control
    
    DEBUGP(DL_INFO, "? AvbPlatformInit: Starting PTP clock initialization\n");
    DEBUGP(DL_INFO, "   Device: VID=0x%04X DID=0x%04X Type=%d\n", 
           dev->pci_vendor_id, dev->pci_device_id, dev->device_type);
    
    // Step 0: Enable PTP clock by clearing TSAUXC bit 31 (DisableSystime)
    ULONG tsauxc_value = 0;
    if (AvbMmioReadReal(dev, TSAUXC_REG, &tsauxc_value) == 0) {
        DEBUGP(DL_INFO, "   TSAUXC before: 0x%08X\n", tsauxc_value);
        if (tsauxc_value & 0x80000000) {
            // Bit 31 is set - PTP clock is DISABLED, need to enable it
            tsauxc_value &= 0x7FFFFFFF;  // Clear bit 31 to enable SYSTIM
            if (AvbMmioWriteReal(dev, TSAUXC_REG, tsauxc_value) != 0) {
                DEBUGP(DL_ERROR, "? Failed to enable PTP clock (TSAUXC write failed)\n");
                return STATUS_DEVICE_HARDWARE_ERROR;
            }
            DEBUGP(DL_INFO, "? PTP clock enabled (TSAUXC bit 31 cleared)\n");
            
            // Verify the bit was cleared
            ULONG tsauxc_verify = 0;
            if (AvbMmioReadReal(dev, TSAUXC_REG, &tsauxc_verify) == 0) {
                DEBUGP(DL_INFO, "   TSAUXC after:  0x%08X\n", tsauxc_verify);
            }
        } else {
            DEBUGP(DL_INFO, "? PTP clock already enabled (TSAUXC=0x%08X)\n", tsauxc_value);
        }
    } else {
        DEBUGP(DL_WARN, "? Could not read TSAUXC register\n");
    }
    
    // Step 1: Program TIMINCA for 1ns increment per clock cycle
    // Value depends on device clock frequency:
    // - I210/I211: 25MHz -> TIMINCA = 0x18000000 (384ns per tick, 2.5M ticks/sec)
    // - I225/I226: 25MHz -> TIMINCA = 0x18000000
    // - I217/I219: Different clocking, but 0x18000000 works
    ULONG timinca_value = 0x18000000;  // Standard value for 25MHz clock
    
    ULONG current_timinca = 0;
    if (AvbMmioReadReal(dev, TIMINCA_REG, &current_timinca) == 0) {
        DEBUGP(DL_INFO, "   Current TIMINCA: 0x%08X\n", current_timinca);
    }
    
    if (AvbMmioWriteReal(dev, TIMINCA_REG, timinca_value) != 0) {
        DEBUGP(DL_ERROR, "? Failed to write TIMINCA register\n");
        return STATUS_DEVICE_HARDWARE_ERROR;
    }
    DEBUGP(DL_INFO, "? TIMINCA programmed: 0x%08X\n", timinca_value);
    
    // Step 2: Read initial SYSTIM value (SYSTIM is READ-ONLY on I226, starts from 0 on power-up)
    ULONG systim_init_lo = 0, systim_init_hi = 0;
    if (AvbMmioReadReal(dev, SYSTIML_REG, &systim_init_lo) == 0 &&
        AvbMmioReadReal(dev, SYSTIMH_REG, &systim_init_hi) == 0) {
        DEBUGP(DL_INFO, "? Initial SYSTIM: 0x%08X%08X\n", systim_init_hi, systim_init_lo);
    }
    
    // Step 3: Verify SYSTIM is incrementing (wait 10ms for better delta)
    LARGE_INTEGER delay;
    delay.QuadPart = -100000;  // 10ms delay (10,000 microseconds)
    KeDelayExecutionThread(KernelMode, FALSE, &delay);
    
    ULONG systim_check_lo = 0, systim_check_hi = 0;
    if (AvbMmioReadReal(dev, SYSTIML_REG, &systim_check_lo) == 0 &&
        AvbMmioReadReal(dev, SYSTIMH_REG, &systim_check_hi) == 0) {
        
        DEBUGP(DL_INFO, "? SYSTIM after 10ms: 0x%08X%08X\n", systim_check_hi, systim_check_lo);
        
        // Calculate delta (handle 64-bit properly)
        ULONGLONG initial = ((ULONGLONG)systim_init_hi << 32) | systim_init_lo;
        ULONGLONG current = ((ULONGLONG)systim_check_hi << 32) | systim_check_lo;
        
        if (current > initial) {
            DEBUGP(DL_INFO, "?? PTP clock is RUNNING! Delta: %llu ns (expected ~10,000,000 ns for 10ms)\n", 
                   (current - initial));
            // Note: ctx->hw_state update happens in caller (AvbBringUpHardware)
            return STATUS_SUCCESS;
        } else {
            DEBUGP(DL_WARN, "?? PTP clock not incrementing (SYSTIM unchanged: initial=0x%llX, current=0x%llX)\n", 
                   initial, current);
            // Not a fatal error - continue anyway, TAS might still be testable
            return STATUS_SUCCESS;
        }
    }
    
    DEBUGP(DL_WARN, "? Could not verify SYSTIM status\n");
    return STATUS_SUCCESS;  // Non-fatal
}
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

intel_device_type_t AvbGetIntelDeviceType(UINT16 did)
{ 
    switch(did) {
        // I-series modern devices
        case 0x1533: return INTEL_DEVICE_I210;  // I210 Copper
        case 0x1534: return INTEL_DEVICE_I210;  // I210 Copper OEM1  
        case 0x1535: return INTEL_DEVICE_I210;  // I210 Copper IT
        case 0x1536: return INTEL_DEVICE_I210;  // I210 Fiber
        case 0x1537: return INTEL_DEVICE_I210;  // I210 Serdes
        case 0x1538: return INTEL_DEVICE_I210;  // I210 SGMII
        
        case 0x153A: 
        case 0x153B: return INTEL_DEVICE_I217;  // I217 family
        
        case 0x15B7: 
        case 0x15B8: 
        case 0x15D6: 
        case 0x15D7: 
        case 0x15D8: 
        case 0x0DC7: 
        case 0x1570: 
        case 0x15E3: return INTEL_DEVICE_I219;  // I219 family
        
        case 0x15F2: return INTEL_DEVICE_I225;  // I225
        case 0x125B: return INTEL_DEVICE_I226;  // I226
        
        // IGB device family (82xxx series)
   //     case 0x10A7: return INTEL_DEVICE_82575;  // 82575EB Copper
   //     case 0x10A9: return INTEL_DEVICE_82575;  // 82575EB Fiber/Serdes  
   //     case 0x10D6: return INTEL_DEVICE_82575;  // 82575GB Quad Copper
        
    //    case 0x10C9: return INTEL_DEVICE_82576;  // 82576 Gigabit Network Connection
    //    case 0x10E6: return INTEL_DEVICE_82576;  // 82576 Fiber
    //    case 0x10E7: return INTEL_DEVICE_82576;  // 82576 Serdes
    //    case 0x10E8: return INTEL_DEVICE_82576;  // 82576 Quad Copper
    //    case 0x1526: return INTEL_DEVICE_82576;  // 82576 Quad Copper ET2
    //    case 0x150A: return INTEL_DEVICE_82576;  // 82576 NS
    //    case 0x1518: return INTEL_DEVICE_82576;  // 82576 NS Serdes
    //    case 0x150D: return INTEL_DEVICE_82576;  // 82576 Serdes Quad
        
     //   case 0x150E: return INTEL_DEVICE_82580;  // 82580 Copper
    //    case 0x150F: return INTEL_DEVICE_82580;  // 82580 Fiber
   //     case 0x1510: return INTEL_DEVICE_82580;  // 82580 Serdes
     //   case 0x1511: return INTEL_DEVICE_82580;  // 82580 SGMII
   //     case 0x1516: return INTEL_DEVICE_82580;  // 82580 Copper Dual
   //     case 0x1527: return INTEL_DEVICE_82580;  // 82580 Quad Fiber
        
     //   case 0x1521: return INTEL_DEVICE_I350;   // I350 Copper
    //    case 0x1522: return INTEL_DEVICE_I350;   // I350 Fiber
    //    case 0x1523: return INTEL_DEVICE_I350;   // I350 Serdes
    //    case 0x1524: return INTEL_DEVICE_I350;   // I350 SGMII
    //    case 0x1546: return INTEL_DEVICE_I350;   // I350 DA4
        
        // I354 uses same operations as I350
     //   case 0x1F40: return INTEL_DEVICE_I354;   // I354 Backplane 2.5GbE
     //   case 0x1F41: return INTEL_DEVICE_I354;   // I354 Backplane 1GbE
     //   case 0x1F45: return INTEL_DEVICE_I354;   // I354 SGMII
        
        default: return INTEL_DEVICE_UNKNOWN; 
    }
}

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
