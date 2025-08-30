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
    if (ctx->hw_state < AVB_HW_BAR_MAPPED) return;
    ULONG lo=0, hi=0;
    if (intel_read_reg(&ctx->intel_device, I210_SYSTIML, &lo)!=0 || intel_read_reg(&ctx->intel_device, I210_SYSTIMH, &hi)!=0)
        return;
    if (lo || hi) {
        KeStallExecutionProcessor(10000);
        ULONG lo2=0, hi2=0;
        if (intel_read_reg(&ctx->intel_device, I210_SYSTIML, &lo2)==0 && intel_read_reg(&ctx->intel_device, I210_SYSTIMH, &hi2)==0 && ((hi2>hi) || (hi2==hi && lo2>lo))) {
            ctx->intel_device.capabilities |= (INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS);
            if (ctx->hw_state < AVB_HW_PTP_READY) { ctx->hw_state = AVB_HW_PTP_READY; DEBUGP(DL_INFO, "HW state -> %s (PTP already running)\n", AvbHwStateName(ctx->hw_state)); }
            return;
        }
    }
    ULONG aux=0; (void)intel_read_reg(&ctx->intel_device, INTEL_REG_TSAUXC, &aux);
    (void)intel_write_reg(&ctx->intel_device, INTEL_REG_TSAUXC, (aux & 0x7FFFFFFFUL) | 0x40000000UL);
    (void)intel_write_reg(&ctx->intel_device, I210_TIMINCA, 0x08000000UL);
    (void)intel_write_reg(&ctx->intel_device, I210_SYSTIML, 0);
    (void)intel_write_reg(&ctx->intel_device, I210_SYSTIMH, 0);
    (void)intel_write_reg(&ctx->intel_device, I210_TSYNCRXCTL, (1U << I210_TSYNCRXCTL_EN_SHIFT));
    (void)intel_write_reg(&ctx->intel_device, I210_TSYNCTXCTL, (1U << I210_TSYNCTXCTL_EN_SHIFT));
    KeStallExecutionProcessor(60000);
    if (intel_read_reg(&ctx->intel_device, I210_SYSTIMH, &hi)==0 && intel_read_reg(&ctx->intel_device, I210_SYSTIML, &lo)==0 && (hi || lo)) {
        ctx->intel_device.capabilities |= (INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS);
        if (ctx->hw_state < AVB_HW_PTP_READY) { ctx->hw_state = AVB_HW_PTP_READY; DEBUGP(DL_INFO, "HW state -> %s (PTP started)\n", AvbHwStateName(ctx->hw_state)); }
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
        DEBUGP(DL_INFO, "?? IOCTL_AVB_INIT_DEVICE: Starting hardware bring-up\n");
        DEBUGP(DL_INFO, "   - Current hw_state: %s (%d)\n", AvbHwStateName(AvbContext->hw_state), AvbContext->hw_state);
        DEBUGP(DL_INFO, "   - Hardware access enabled: %s\n", AvbContext->hw_access_enabled ? "YES" : "NO");
        DEBUGP(DL_INFO, "   - Initialized flag: %s\n", AvbContext->initialized ? "YES" : "NO");
        
        status = AvbBringUpHardware(AvbContext);
        
        DEBUGP(DL_INFO, "?? IOCTL_AVB_INIT_DEVICE: Completed with status=0x%08X\n", status);
        DEBUGP(DL_INFO, "   - Final hw_state: %s (%d)\n", AvbHwStateName(AvbContext->hw_state), AvbContext->hw_state);
        DEBUGP(DL_INFO, "   - Final hardware access: %s\n", AvbContext->hw_access_enabled ? "YES" : "NO");
        break;
    case IOCTL_AVB_ENUM_ADAPTERS:
        if (outLen < sizeof(AVB_ENUM_REQUEST)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        else {
            PAVB_ENUM_REQUEST r = (PAVB_ENUM_REQUEST)buf; 
            RtlZeroMemory(r, sizeof(*r));
            
            // Use capabilities already set by AvbBringUpHardware
            r->count=1; 
            r->index=0; 
            r->vendor_id=(USHORT)AvbContext->intel_device.pci_vendor_id; 
            r->device_id=(USHORT)AvbContext->intel_device.pci_device_id; 
            r->capabilities=AvbContext->intel_device.capabilities; 
            r->status=NDIS_STATUS_SUCCESS; 
            info=sizeof(*r);
            
            DEBUGP(DL_INFO, "? ENUM_ADAPTERS: VID=0x%04X, DID=0x%04X, Caps=0x%08X\n",
                   r->vendor_id, r->device_id, r->capabilities);
        }
        break;
    case IOCTL_AVB_GET_DEVICE_INFO:
        DEBUGP(DL_INFO, "?? IOCTL_AVB_GET_DEVICE_INFO: Starting device info request\n");
        DEBUGP(DL_INFO, "   - Buffer size check: inLen=%lu, outLen=%lu, required=%lu\n", 
               inLen, outLen, sizeof(AVB_DEVICE_INFO_REQUEST));
        DEBUGP(DL_INFO, "   - Hardware state: %s (%d)\n", AvbHwStateName(AvbContext->hw_state), AvbContext->hw_state);
        DEBUGP(DL_INFO, "   - Hardware access enabled: %s\n", AvbContext->hw_access_enabled ? "YES" : "NO");
        DEBUGP(DL_INFO, "   - Hardware context: %p\n", AvbContext->hardware_context);
        
        if (outLen < sizeof(AVB_DEVICE_INFO_REQUEST)) { 
            DEBUGP(DL_ERROR, "? DEVICE_INFO FAILED: Buffer too small\n");
            status = STATUS_BUFFER_TOO_SMALL; 
        } else if (AvbContext->hw_state < AVB_HW_BAR_MAPPED) { 
            DEBUGP(DL_ERROR, "? DEVICE_INFO FAILED: Hardware not ready - hw_state=%s, need=%s\n", 
                   AvbHwStateName(AvbContext->hw_state), AvbHwStateName(AVB_HW_BAR_MAPPED));
            DEBUGP(DL_ERROR, "   Possible causes:\n");
            DEBUGP(DL_ERROR, "   - BAR0 discovery failed during initialization\n");
            DEBUGP(DL_ERROR, "   - MmMapIoSpace failed\n");
            DEBUGP(DL_ERROR, "   - intel_init failed\n");
            DEBUGP(DL_ERROR, "   - MMIO sanity check failed\n");
            status = STATUS_DEVICE_NOT_READY; 
        } else {
            DEBUGP(DL_INFO, "? DEVICE_INFO: Hardware state validation passed\n");
            
            PAVB_DEVICE_INFO_REQUEST r = (PAVB_DEVICE_INFO_REQUEST)buf; 
            RtlZeroMemory(r->device_info, sizeof(r->device_info)); 
            
            DEBUGP(DL_INFO, "?? DEVICE_INFO: Calling intel_get_device_info...\n");
            int rc = intel_get_device_info(&AvbContext->intel_device, r->device_info, sizeof(r->device_info)); 
            DEBUGP(DL_INFO, "?? DEVICE_INFO: intel_get_device_info returned %d\n", rc);
            
            if (rc == 0) {
                DEBUGP(DL_INFO, "? DEVICE_INFO: Device info string: %s\n", r->device_info);
            } else {
                DEBUGP(DL_ERROR, "? DEVICE_INFO: intel_get_device_info failed with code %d\n", rc);
            }
            
            size_t used = 0; 
            (void)RtlStringCbLengthA(r->device_info, sizeof(r->device_info), &used); 
            r->buffer_size = (ULONG)used; 
            r->status = (rc == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE; 
            info = sizeof(*r); 
            status = (rc == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL; 
            
            DEBUGP(DL_INFO, "?? DEVICE_INFO COMPLETE: status=0x%08X, buffer_size=%lu\n", status, r->buffer_size);
        }
        break;
    case IOCTL_AVB_READ_REGISTER:
    case IOCTL_AVB_WRITE_REGISTER:
        if (inLen < sizeof(AVB_REGISTER_REQUEST) || outLen < sizeof(AVB_REGISTER_REQUEST)) status = STATUS_BUFFER_TOO_SMALL; else if (AvbContext->hw_state < AVB_HW_BAR_MAPPED) status = STATUS_DEVICE_NOT_READY; else { PAVB_REGISTER_REQUEST r=(PAVB_REGISTER_REQUEST)buf; if (code==IOCTL_AVB_READ_REGISTER){ULONG tmp=0; int rc=intel_read_reg(&AvbContext->intel_device, r->offset, &tmp); r->value=(avb_u32)tmp; r->status=(rc==0)?NDIS_STATUS_SUCCESS:NDIS_STATUS_FAILURE; status=(rc==0)?STATUS_SUCCESS:STATUS_UNSUCCESSFUL;} else {int rc=intel_write_reg(&AvbContext->intel_device, r->offset, r->value); r->status=(rc==0)?NDIS_STATUS_SUCCESS:NDIS_STATUS_FAILURE; status=(rc==0)?STATUS_SUCCESS:STATUS_UNSUCCESSFUL;} info=sizeof(*r);} break;
    case IOCTL_AVB_GET_TIMESTAMP:
    case IOCTL_AVB_SET_TIMESTAMP:
        if (inLen < sizeof(AVB_TIMESTAMP_REQUEST) || outLen < sizeof(AVB_TIMESTAMP_REQUEST)) status = STATUS_BUFFER_TOO_SMALL; else if (AvbContext->hw_state < AVB_HW_PTP_READY) status = STATUS_DEVICE_NOT_READY; else { PAVB_TIMESTAMP_REQUEST r=(PAVB_TIMESTAMP_REQUEST)buf; if (code==IOCTL_AVB_GET_TIMESTAMP){ULONGLONG t=0; struct timespec sys={0}; int rc=intel_gettime(&AvbContext->intel_device,r->clock_id,&t,&sys); if (rc!=0) rc=AvbReadTimestamp(&AvbContext->intel_device,&t); r->timestamp=t; r->status=(rc==0)?NDIS_STATUS_SUCCESS:NDIS_STATUS_FAILURE; status=(rc==0)?STATUS_SUCCESS:STATUS_UNSUCCESSFUL;} else {int rc=intel_set_systime(&AvbContext->intel_device,r->timestamp); r->status=(rc==0)?NDIS_STATUS_SUCCESS:NDIS_STATUS_FAILURE; status=(rc==0)?STATUS_SUCCESS:STATUS_UNSUCCESSFUL;} info=sizeof(*r);} break;
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