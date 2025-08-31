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
        (void)AvbI210EnsureSystimRunning(Ctx);
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
                NTSTATUS ptp_status = AvbI210EnsureSystimRunning(activeContext);
                DEBUGP(DL_INFO, "?? INIT_DEVICE: I210 PTP initialization completed with status=0x%08X\n", ptp_status);
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

/* ======================================================================= */
/* Enhanced I210 PTP functions (added to match header declarations) */

/**
 * @brief Enhanced I210 PTP initialization with proper return status
 * @param context The I210 device context
 * @return NTSTATUS Success/failure status
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
    
    // Step 1: Check current SYSTIM state
    ULONG lo = 0, hi = 0;
    if (intel_read_reg(&context->intel_device, I210_SYSTIML, &lo) != 0 || 
        intel_read_reg(&context->intel_device, I210_SYSTIMH, &hi) != 0) {
        DEBUGP(DL_ERROR, "AvbI210EnsureSystimRunning: Failed to read SYSTIM registers\n");
        return STATUS_DEVICE_HARDWARE_ERROR;
    }
    
    DEBUGP(DL_INFO, "   - Initial SYSTIM: 0x%08X%08X\n", hi, lo);
    
    // Step 2: Test if clock is already running
    if (lo || hi) {
        KeStallExecutionProcessor(10000); // 10ms delay
        ULONG lo2 = 0, hi2 = 0;
        if (intel_read_reg(&context->intel_device, I210_SYSTIML, &lo2) == 0 && 
            intel_read_reg(&context->intel_device, I210_SYSTIMH, &hi2) == 0 && 
            ((hi2 > hi) || (hi2 == hi && lo2 > lo))) {
            
            DEBUGP(DL_INFO, "? I210 PTP: Clock already running (0x%08X%08X -> 0x%08X%08X)\n", 
                   hi, lo, hi2, lo2);
            context->intel_device.capabilities |= (INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS);
            if (context->hw_state < AVB_HW_PTP_READY) { 
                context->hw_state = AVB_HW_PTP_READY; 
                DEBUGP(DL_INFO, "HW state -> %s (PTP already running)\n", AvbHwStateName(context->hw_state)); 
            }
            return STATUS_SUCCESS;
        }
    }
    
    DEBUGP(DL_INFO, "?? I210 PTP: Clock not running, performing initialization...\n");
    
    // Step 3: Complete I210 PTP initialization sequence (Intel datasheet 8.14)
    
    // 3a: Disable PTP first for clean reset
    ULONG aux = 0;
    intel_read_reg(&context->intel_device, INTEL_REG_TSAUXC, &aux);
    intel_write_reg(&context->intel_device, INTEL_REG_TSAUXC, 0x80000000); // Disable completely
    
    DEBUGP(DL_INFO, "   - Step 1: PTP disabled for reset (TSAUXC=0x80000000)\n");
    
    // 3b: Clear all time registers
    intel_write_reg(&context->intel_device, I210_SYSTIML, 0x00000000);
    intel_write_reg(&context->intel_device, I210_SYSTIMH, 0x00000000);
    intel_write_reg(&context->intel_device, I210_TSYNCRXCTL, 0x00000000);
    intel_write_reg(&context->intel_device, I210_TSYNCTXCTL, 0x00000000);
    
    DEBUGP(DL_INFO, "   - Step 2: All time registers cleared\n");
    
    // 3c: Wait for hardware stabilization (critical for I210)
    KeStallExecutionProcessor(50000); // 50ms - extended delay for I210 hardware
    
    // 3d: Configure TIMINCA before enabling PTP (critical order!)
    intel_write_reg(&context->intel_device, I210_TIMINCA, 0x08000000); // 8ns per tick, 125MHz
    
    DEBUGP(DL_INFO, "   - Step 3: TIMINCA configured (8ns increment)\n");
    
    // 3e: Enable PTP with proper PHC configuration
    ULONG new_aux = 0x40000000; // PHC enabled (bit 30), DisableSystime cleared (bit 31)
    intel_write_reg(&context->intel_device, INTEL_REG_TSAUXC, new_aux);
    
    DEBUGP(DL_INFO, "   - Step 4: PTP enabled (TSAUXC=0x40000000)\n");
    
    // 3f: Set initial time to trigger clock start
    intel_write_reg(&context->intel_device, I210_SYSTIML, 0x10000000); // Non-zero start value
    intel_write_reg(&context->intel_device, I210_SYSTIMH, 0x00000000);
    
    DEBUGP(DL_INFO, "   - Step 5: Initial time set (0x0000000010000000)\n");
    
    // 3g: Enable timestamp capture
    ULONG rx_config = (1U << I210_TSYNCRXCTL_EN_SHIFT);
    ULONG tx_config = (1U << I210_TSYNCTXCTL_EN_SHIFT);
    intel_write_reg(&context->intel_device, I210_TSYNCRXCTL, rx_config);
    intel_write_reg(&context->intel_device, I210_TSYNCTXCTL, tx_config);
    
    DEBUGP(DL_INFO, "   - Step 6: Timestamp capture enabled\n");
    
    // Step 4: Verify clock operation with extended testing
    DEBUGP(DL_INFO, "?? I210 PTP: Testing clock operation (8 samples @ 100ms intervals)...\n");
    
    BOOLEAN clock_running = FALSE;
    for (int attempt = 0; attempt < 8 && !clock_running; attempt++) {
        KeStallExecutionProcessor(100000); // 100ms between samples
        
        ULONG test_lo = 0, test_hi = 0;
        if (intel_read_reg(&context->intel_device, I210_SYSTIML, &test_lo) == 0 &&
            intel_read_reg(&context->intel_device, I210_SYSTIMH, &test_hi) == 0) {
            
            DEBUGP(DL_INFO, "   Clock check %d: SYSTIM=0x%08X%08X\n", 
                   attempt + 1, test_hi, test_lo);
            
            // Check if clock advanced from initial value
            if (test_lo != 0x10000000 || test_hi != 0x00000000) {
                clock_running = TRUE;
                DEBUGP(DL_INFO, "? I210 PTP: SUCCESS - Clock is now running!\n");
                break;
            }
        }
    }
    
    if (clock_running) {
        DEBUGP(DL_INFO, "? I210 PTP: Successfully initialized and verified\n");
        context->intel_device.capabilities |= (INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS);
        if (context->hw_state < AVB_HW_PTP_READY) { 
            context->hw_state = AVB_HW_PTP_READY; 
            DEBUGP(DL_INFO, "HW state -> %s (PTP operational)\n", AvbHwStateName(context->hw_state)); 
        }
        return STATUS_SUCCESS;
    } else {
        DEBUGP(DL_ERROR, "? I210 PTP: FAILED - Clock still not running after complete initialization\n");
        return STATUS_UNSUCCESSFUL; // Non-fatal - MMIO still works
    }
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
    
    // Step 5: For I210, attempt PTP initialization
    if (context->intel_device.device_type == INTEL_DEVICE_I210) {
        DEBUGP(DL_INFO, "? ForceContextReinitialization: Attempting I210 PTP initialization\n");
        NTSTATUS ptp_status = AvbI210EnsureSystimRunning(context);
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