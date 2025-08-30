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
    if (!Ctx) return STATUS_INVALID_PARAMETER;
    if (Ctx->hw_access_enabled) return STATUS_SUCCESS;

    /* Step 1: Discover & map BAR0 if not yet mapped */
    if (Ctx->hardware_context == NULL) {
        PHYSICAL_ADDRESS bar0 = {0};
        ULONG barLen = 0;
        NTSTATUS ds = AvbDiscoverIntelControllerResources(Ctx->filter_instance, &bar0, &barLen);
        if (!NT_SUCCESS(ds)) {
            DEBUGP(DL_ERROR, "BAR0 discovery failed 0x%08X (cannot map MMIO yet) VID=0x%04X DID=0x%04X\n", ds, Ctx->intel_device.pci_vendor_id, Ctx->intel_device.pci_device_id);
            return ds; /* propagate */
        }
        DEBUGP(DL_INFO, "BAR0 discovered: PA=0x%llx Len=0x%x\n", bar0.QuadPart, barLen);
        NTSTATUS ms = AvbMapIntelControllerMemory(Ctx, bar0, barLen);
        if (!NT_SUCCESS(ms)) {
            DEBUGP(DL_ERROR, "BAR0 map failed 0x%08X (MmMapIoSpace)\n", ms);
            return ms;
        }
        DEBUGP(DL_INFO, "MMIO mapped (opaque ctx=%p)\n", Ctx->hardware_context);
    }

    Ctx->intel_device.private_data = Ctx;
    Ctx->intel_device.capabilities = 0; /* reset published caps */

    DEBUGP(DL_INFO, "intel_init: VID=0x%04X DID=0x%04X\n", Ctx->intel_device.pci_vendor_id, Ctx->intel_device.pci_device_id);
    if (intel_init(&Ctx->intel_device) != 0) {
        DEBUGP(DL_ERROR, "intel_init failed (library)\n");
        return STATUS_UNSUCCESSFUL;
    }

    ULONG ctrl = 0xFFFFFFFF;
    if (intel_read_reg(&Ctx->intel_device, I210_CTRL, &ctrl) != 0 || ctrl == 0xFFFFFFFF) {
        DEBUGP(DL_ERROR, "MMIO sanity read failed CTRL=0x%08X (expected != 0xFFFFFFFF)\n", ctrl);
        return STATUS_DEVICE_NOT_READY;
    }

    Ctx->intel_device.capabilities |= INTEL_CAP_MMIO;
    if (Ctx->hw_state < AVB_HW_BAR_MAPPED) {
        Ctx->hw_state = AVB_HW_BAR_MAPPED;
        DEBUGP(DL_INFO, "HW state -> %s (CTRL=0x%08X)\n", AvbHwStateName(Ctx->hw_state), ctrl);
    }
    Ctx->initialized = TRUE;
    Ctx->hw_access_enabled = TRUE;
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
        status = AvbBringUpHardware(AvbContext);
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
        if (outLen < sizeof(AVB_DEVICE_INFO_REQUEST)) status = STATUS_BUFFER_TOO_SMALL; else if (AvbContext->hw_state < AVB_HW_BAR_MAPPED) status = STATUS_DEVICE_NOT_READY; else {
            PAVB_DEVICE_INFO_REQUEST r = (PAVB_DEVICE_INFO_REQUEST)buf; RtlZeroMemory(r->device_info, sizeof(r->device_info)); int rc=intel_get_device_info(&AvbContext->intel_device, r->device_info, sizeof(r->device_info)); size_t used=0; (void)RtlStringCbLengthA(r->device_info, sizeof(r->device_info), &used); r->buffer_size=(ULONG)used; r->status=(rc==0)?NDIS_STATUS_SUCCESS:NDIS_STATUS_FAILURE; info=sizeof(*r); status=(rc==0)?STATUS_SUCCESS:STATUS_UNSUCCESSFUL; }
        break;
    case IOCTL_AVB_READ_REGISTER:
    case IOCTL_AVB_WRITE_REGISTER:
        if (inLen < sizeof(AVB_REGISTER_REQUEST) || outLen < sizeof(AVB_REGISTER_REQUEST)) status = STATUS_BUFFER_TOO_SMALL; else if (AvbContext->hw_state < AVB_HW_BAR_MAPPED) status = STATUS_DEVICE_NOT_READY; else { PAVB_REGISTER_REQUEST r=(PAVB_REGISTER_REQUEST)buf; if (code==IOCTL_AVB_READ_REGISTER){ULONG tmp=0; int rc=intel_read_reg(&AvbContext->intel_device, r->offset, &tmp); r->value=(avb_u32)tmp; r->status=(rc==0)?NDIS_STATUS_SUCCESS:NDIS_STATUS_FAILURE; status=(rc==0)?STATUS_SUCCESS:STATUS_UNSUCCESSFUL;} else {int rc=intel_write_reg(&AvbContext->intel_device, r->offset, r->value); r->status=(rc==0)?NDIS_STATUS_SUCCESS:NDIS_STATUS_FAILURE; status=(rc==0)?STATUS_SUCCESS:STATUS_UNSUCCESSFUL;} info=sizeof(*r);} break;
    case IOCTL_AVB_GET_TIMESTAMP:
    case IOCTL_AVB_SET_TIMESTAMP:
        if (inLen < sizeof(AVB_TIMESTAMP_REQUEST) || outLen < sizeof(AVB_TIMESTAMP_REQUEST)) status = STATUS_BUFFER_TOO_SMALL; else if (AvbContext->hw_state < AVB_HW_PTP_READY) status = STATUS_DEVICE_NOT_READY; else { PAVB_TIMESTAMP_REQUEST r=(PAVB_TIMESTAMP_REQUEST)buf; if (code==IOCTL_AVB_GET_TIMESTAMP){ULONGLONG t=0; struct timespec sys={0}; int rc=intel_gettime(&AvbContext->intel_device,r->clock_id,&t,&sys); if (rc!=0) rc=AvbReadTimestamp(&AvbContext->intel_device,&t); r->timestamp=t; r->status=(rc==0)?NDIS_STATUS_SUCCESS:NDIS_STATUS_FAILURE; status=(rc==0)?STATUS_SUCCESS:STATUS_UNSUCCESSFUL;} else {int rc=intel_set_systime(&AvbContext->intel_device,r->timestamp); r->status=(rc==0)?NDIS_STATUS_SUCCESS:NDIS_STATUS_FAILURE; status=(rc==0)?STATUS_SUCCESS:STATUS_UNSUCCESSFUL;} info=sizeof(*r);} break;
    case IOCTL_AVB_GET_HW_STATE:
        if (outLen < sizeof(AVB_HW_STATE_QUERY)) status = STATUS_BUFFER_TOO_SMALL; else { PAVB_HW_STATE_QUERY q=(PAVB_HW_STATE_QUERY)buf; RtlZeroMemory(q,sizeof(*q)); q->hw_state=AvbContext->hw_state; q->vendor_id=AvbContext->intel_device.pci_vendor_id; q->device_id=AvbContext->intel_device.pci_device_id; q->capabilities=AvbContext->intel_device.capabilities; info=sizeof(*q);} break;
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

PMS_FILTER AvbFindIntelFilterModule(VOID){ if (g_AvbContext && g_AvbContext->filter_instance) return g_AvbContext->filter_instance; PMS_FILTER f=NULL; PLIST_ENTRY l; BOOLEAN bFalse=FALSE; FILTER_ACQUIRE_LOCK(&FilterListLock,bFalse); l=FilterModuleList.Flink; while(l!=&FilterModuleList){ f=CONTAINING_RECORD(l,MS_FILTER,FilterModuleLink); if (f && f->AvbContext){ FILTER_RELEASE_LOCK(&FilterListLock,bFalse); return f;} l=l->Flink; } FILTER_RELEASE_LOCK(&FilterListLock,bFalse); return NULL; }