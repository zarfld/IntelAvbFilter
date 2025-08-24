/*++

Module Name:

    avb_integration_fixed.c

Abstract:

    Implementation of AVB integration with Intel filter driver
    Provides hardware access bridge between NDIS filter and Intel AVB library

--*/

#include "precomp.h"
#include "avb_integration.h"
#include "external/intel_avb/lib/intel_windows.h"
#include <ntstrsafe.h>
#include "intel-ethernet-regs/gen/i210_regs.h"  /* Single Source Of Truth register definitions for i210 family */
#include "external/intel_avb/lib/intel_private.h" /* For INTEL_REG_TSAUXC */

/* ------------------------------------------------------------------------- */
/**
 * @brief Perform minimal device structure initialization and call intel_init once.
 *        Advances hw_state to AVB_HW_BAR_MAPPED after successful MMIO sanity check.
 */
static NTSTATUS
AvbPerformBasicInitialization(_Inout_ PAVB_DEVICE_CONTEXT Ctx)
{
    if (Ctx->hw_access_enabled) {
        return STATUS_SUCCESS; /* Already done */
    }

    const USHORT vid = INTEL_VENDOR_ID;      /* 0x8086 */
    const USHORT did = 0x1533;               /* I210 (fallback – refined later) */

    Ctx->intel_device.pci_vendor_id = vid;
    Ctx->intel_device.pci_device_id = did;
    Ctx->intel_device.device_type   = INTEL_DEVICE_I210;
    Ctx->intel_device.private_data  = Ctx;
    Ctx->intel_device.capabilities  = 0; /* publish later after validation */

    DEBUGP(DL_INFO, "AvbPerformBasicInitialization: seeding VID=0x%04X DID=0x%04X\n", vid, did);

    int r = intel_init(&Ctx->intel_device);
    if (r != 0) {
        DEBUGP(DL_ERROR, "intel_init failed (%d)\n", r);
        return STATUS_UNSUCCESSFUL;
    }

    /* Re-assert in case library overwrote */
    Ctx->intel_device.pci_vendor_id = vid;
    Ctx->intel_device.pci_device_id = did;

    /* Basic MMIO sanity: read CTRL register (should not be 0xFFFFFFFF) */
    ULONG ctrl = 0xFFFFFFFF;
    if (intel_read_reg(&Ctx->intel_device, I210_CTRL, &ctrl) == 0 && ctrl != 0xFFFFFFFF) {
        Ctx->intel_device.capabilities |= INTEL_CAP_MMIO; /* MMIO verified */
        if (Ctx->hw_state < AVB_HW_BAR_MAPPED) {
            Ctx->hw_state = AVB_HW_BAR_MAPPED;
            DEBUGP(DL_INFO, "AVB HW state -> %s (CTRL=0x%08X)\n", AvbHwStateName(Ctx->hw_state), ctrl);
        }
    } else {
        DEBUGP(DL_ERROR, "MMIO sanity read failed (CTRL=0x%08X)\n", ctrl);
        return STATUS_DEVICE_NOT_READY;
    }

    Ctx->hw_access_enabled = TRUE;
    Ctx->initialized       = TRUE;

    DEBUGP(DL_INFO, "intel_init OK – caps=0x%08X\n", Ctx->intel_device.capabilities);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------------- */
/* Helper: ensure i210 PTP system time is running (complete initialization sequence) */
static VOID AvbI210EnsureSystimRunning(_In_ PAVB_DEVICE_CONTEXT ctx)
{
    ULONG lo=0, hi=0;
    if (ctx->hw_state < AVB_HW_BAR_MAPPED) {
        DEBUGP(DL_WARN, "PTP init requested before BAR_MAPPED state\n");
        return;
    }
    DEBUGP(DL_INFO, "==>AvbI210EnsureSystimRunning: Starting I210 PTP initialization\n");

    if (intel_read_reg(&ctx->intel_device, I210_SYSTIML, &lo)!=0 ||
        intel_read_reg(&ctx->intel_device, I210_SYSTIMH, &hi)!=0) {
        DEBUGP(DL_ERROR, "PTP init: read SYSTIM failed - hardware access problem\n");
        return;
    }
    if (lo || hi) {
        KeStallExecutionProcessor(10000);
        ULONG lo2=0, hi2=0;
        if (intel_read_reg(&ctx->intel_device, I210_SYSTIML, &lo2)==0 &&
            intel_read_reg(&ctx->intel_device, I210_SYSTIMH, &hi2)==0 &&
            ((hi2>hi) || (hi2==hi && lo2>lo))) {
            /* Already running */
            ctx->intel_device.capabilities |= (INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS);
            if (ctx->hw_state < AVB_HW_PTP_READY) {
                ctx->hw_state = AVB_HW_PTP_READY;
                DEBUGP(DL_INFO, "AVB HW state -> %s (pre-existing clock)\n", AvbHwStateName(ctx->hw_state));
            }
            return;
        }
        DEBUGP(DL_WARN, "PTP init: SYSTIM non-zero but not incrementing - reinitializing\n");
    }

    ULONG aux=0;
    if (intel_read_reg(&ctx->intel_device, INTEL_REG_TSAUXC, &aux)!=0) {
        DEBUGP(DL_ERROR, "PTP init: read TSAUXC failed\n");
        return;
    }
    ULONG new_aux = (aux & 0x7FFFFFFFUL) | 0x40000000UL; /* clear bit31, set bit30 */
    if (new_aux != aux && intel_write_reg(&ctx->intel_device, INTEL_REG_TSAUXC, new_aux)!=0) {
        DEBUGP(DL_ERROR, "PTP init: TSAUXC write failed\n"); return; }

    if (intel_write_reg(&ctx->intel_device, I210_TIMINCA, 0x08000000UL)!=0) { DEBUGP(DL_ERROR, "PTP init: TIMINCA write failed\n"); return; }
    if (intel_write_reg(&ctx->intel_device, I210_SYSTIML, 0)!=0 || intel_write_reg(&ctx->intel_device, I210_SYSTIMH, 0)!=0) { DEBUGP(DL_ERROR, "PTP init: reset SYSTIM failed\n"); return; }

    ULONG tsyncrx = (1U << I210_TSYNCRXCTL_EN_SHIFT);
    ULONG tsynctx = (1U << I210_TSYNCTXCTL_EN_SHIFT);
    if (intel_write_reg(&ctx->intel_device, I210_TSYNCRXCTL, tsyncrx)!=0 || intel_write_reg(&ctx->intel_device, I210_TSYNCTXCTL, tsynctx)!=0) { DEBUGP(DL_ERROR, "PTP init: enable timestamp capture failed\n"); return; }

    KeStallExecutionProcessor(50000);
    if (intel_read_reg(&ctx->intel_device, I210_SYSTIML, &lo)==0 &&
        intel_read_reg(&ctx->intel_device, I210_SYSTIMH, &hi)==0) {
        KeStallExecutionProcessor(10000);
        ULONG lo2=0, hi2=0;
        if (intel_read_reg(&ctx->intel_device, I210_SYSTIML, &lo2)==0 &&
            intel_read_reg(&ctx->intel_device, I210_SYSTIMH, &hi2)==0 &&
            ((hi2>hi) || (hi2==hi && lo2>lo))) {
            ctx->intel_device.capabilities |= (INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS);
            if (ctx->hw_state < AVB_HW_PTP_READY) {
                ctx->hw_state = AVB_HW_PTP_READY;
                DEBUGP(DL_INFO, "AVB HW state -> %s (clock started)\n", AvbHwStateName(ctx->hw_state));
            }
            DEBUGP(DL_INFO, "? PTP init: SUCCESS – SYSTIM incrementing\n");
        } else {
            DEBUGP(DL_ERROR, "? PTP init: FAILED – SYSTIM not incrementing\n");
        }
    }
    DEBUGP(DL_INFO, "<==AvbI210EnsureSystimRunning\n");
}

/* Platform operations with wrapper functions */
static int PlatformInitWrapper(_In_ device_t *dev) { NTSTATUS status = AvbPlatformInit(dev); return NT_SUCCESS(status) ? 0 : -1; }
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

PAVB_DEVICE_CONTEXT g_AvbContext = NULL;

NTSTATUS AvbInitializeDevice(_In_ PMS_FILTER FilterModule, _Out_ PAVB_DEVICE_CONTEXT *AvbContext)
{
    DEBUGP(DL_TRACE, "==>AvbInitializeDevice: Transitioning to real hardware access\n");
    return AvbInitializeDeviceWithBar0Discovery(FilterModule, AvbContext);
}

VOID AvbCleanupDevice(_In_ PAVB_DEVICE_CONTEXT AvbContext)
{
    DEBUGP(DL_TRACE, "==>AvbCleanupDevice\n");
    if (!AvbContext) return;
    if (g_AvbContext == AvbContext) g_AvbContext = NULL;
    ExFreePoolWithTag(AvbContext, FILTER_ALLOC_TAG);
    DEBUGP(DL_TRACE, "<==AvbCleanupDevice\n");
}

NTSTATUS AvbHandleDeviceIoControl(_In_ PAVB_DEVICE_CONTEXT AvbContext, _In_ PIRP Irp)
{
    if (!AvbContext) return STATUS_DEVICE_NOT_READY;
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    ULONG ioControlCode      = irpSp->Parameters.DeviceIoControl.IoControlCode;
    PUCHAR buffer            = (PUCHAR)Irp->AssociatedIrp.SystemBuffer;
    ULONG inLen  = irpSp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG_PTR information = 0; NTSTATUS status = STATUS_SUCCESS;

    if (!AvbContext->initialized && ioControlCode != IOCTL_AVB_INIT_DEVICE && ioControlCode != IOCTL_AVB_ENUM_ADAPTERS && ioControlCode != IOCTL_AVB_GET_HW_STATE) {
        return STATUS_DEVICE_NOT_READY;
    }

    if (inLen >= sizeof(AVB_REQUEST_HEADER)) {
        PAVB_REQUEST_HEADER hdr = (PAVB_REQUEST_HEADER)buffer;
        if (hdr->header_size == sizeof(AVB_REQUEST_HEADER)) {
            AvbContext->last_seen_abi_version = hdr->abi_version;
            if ((hdr->abi_version & 0xFFFF0000u) != (AVB_IOCTL_ABI_VERSION & 0xFFFF0000u)) return STATUS_REVISION_MISMATCH;
            buffer += sizeof(AVB_REQUEST_HEADER);
            inLen  -= sizeof(AVB_REQUEST_HEADER);
            outLen = (outLen >= sizeof(AVB_REQUEST_HEADER)) ? (outLen - sizeof(AVB_REQUEST_HEADER)) : 0;
        }
    }

    switch (ioControlCode) {
    case IOCTL_AVB_INIT_DEVICE:
        status = AvbPerformBasicInitialization(AvbContext);
        if (NT_SUCCESS(status) && AvbContext->intel_device.device_type == INTEL_DEVICE_I210 && AvbContext->hw_state >= AVB_HW_BAR_MAPPED) {
            AvbI210EnsureSystimRunning(AvbContext);
        }
        break;
    case IOCTL_AVB_ENUM_ADAPTERS:
        if (outLen < sizeof(AVB_ENUM_REQUEST)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        {
            PAVB_ENUM_REQUEST req = (PAVB_ENUM_REQUEST)buffer; RtlZeroMemory(req, sizeof(*req));
            if (AvbContext->hw_state >= AVB_HW_BOUND) {
                (void)AvbPerformBasicInitialization(AvbContext); /* may advance to BAR_MAPPED */
                req->count       = 1;
                req->vendor_id   = AvbContext->intel_device.pci_vendor_id;
                req->device_id   = AvbContext->intel_device.pci_device_id;
                req->capabilities= AvbContext->intel_device.capabilities;
            } else {
                req->count = 0; /* not bound yet */
            }
            req->index = 0; req->status = NDIS_STATUS_SUCCESS; information = sizeof(AVB_ENUM_REQUEST);
            DEBUGP(DL_INFO, "ENUM: count=%u state=%s caps=0x%08X\n", req->count, AvbHwStateName(AvbContext->hw_state), req->capabilities);
        }
        break;
    case IOCTL_AVB_GET_DEVICE_INFO:
        if (outLen < sizeof(AVB_DEVICE_INFO_REQUEST)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        if (AvbContext->hw_state < AVB_HW_BAR_MAPPED) { status = STATUS_DEVICE_NOT_READY; break; }
        {
            PAVB_DEVICE_INFO_REQUEST req = (PAVB_DEVICE_INFO_REQUEST)buffer; RtlZeroMemory(req->device_info, sizeof(req->device_info));
            int r = intel_get_device_info(&AvbContext->intel_device, req->device_info, sizeof(req->device_info)); size_t used=0; (void)RtlStringCbLengthA(req->device_info, sizeof(req->device_info), &used); req->buffer_size=(ULONG)used; req->status=(r==0)?NDIS_STATUS_SUCCESS:NDIS_STATUS_FAILURE; information=sizeof(AVB_DEVICE_INFO_REQUEST); status=(r==0)?STATUS_SUCCESS:STATUS_UNSUCCESSFUL;
        }
        break;
    case IOCTL_AVB_READ_REGISTER:
    case IOCTL_AVB_WRITE_REGISTER:
        if (AvbContext->hw_state < AVB_HW_BAR_MAPPED) { status = STATUS_DEVICE_NOT_READY; break; }
        if (inLen < sizeof(AVB_REGISTER_REQUEST) || outLen < sizeof(AVB_REGISTER_REQUEST)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        else {
            PAVB_REGISTER_REQUEST req = (PAVB_REGISTER_REQUEST)buffer; if (ioControlCode==IOCTL_AVB_READ_REGISTER){ULONG tmp=0; int r=intel_read_reg(&AvbContext->intel_device, req->offset, &tmp); req->value=(avb_u32)tmp; req->status=(r==0)?NDIS_STATUS_SUCCESS:NDIS_STATUS_FAILURE; status=(r==0)?STATUS_SUCCESS:STATUS_UNSUCCESSFUL;} else {int r=intel_write_reg(&AvbContext->intel_device, req->offset, req->value); req->status=(r==0)?NDIS_STATUS_SUCCESS:NDIS_STATUS_FAILURE; status=(r==0)?STATUS_SUCCESS:STATUS_UNSUCCESSFUL;} information=sizeof(AVB_REGISTER_REQUEST);} break;
    case IOCTL_AVB_GET_TIMESTAMP:
    case IOCTL_AVB_SET_TIMESTAMP:
        if (AvbContext->hw_state < AVB_HW_PTP_READY) { status = STATUS_DEVICE_NOT_READY; break; }
        if (inLen < sizeof(AVB_TIMESTAMP_REQUEST) || outLen < sizeof(AVB_TIMESTAMP_REQUEST)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        else { PAVB_TIMESTAMP_REQUEST req=(PAVB_TIMESTAMP_REQUEST)buffer; if (ioControlCode==IOCTL_AVB_GET_TIMESTAMP){ULONGLONG cur=0; struct timespec sys={0}; int r=intel_gettime(&AvbContext->intel_device, req->clock_id, &cur, &sys); if (r!=0) r=AvbReadTimestamp(&AvbContext->intel_device, &cur); req->timestamp=cur; req->status=(r==0)?NDIS_STATUS_SUCCESS:NDIS_STATUS_FAILURE; status=(r==0)?STATUS_SUCCESS:STATUS_UNSUCCESSFUL;} else {int r=intel_set_systime(&AvbContext->intel_device, req->timestamp); req->status=(r==0)?NDIS_STATUS_SUCCESS:NDIS_STATUS_FAILURE; status=(r==0)?STATUS_SUCCESS:STATUS_UNSUCCESSFUL;} information=sizeof(AVB_TIMESTAMP_REQUEST);} break;
    case IOCTL_AVB_GET_HW_STATE:
        if (outLen < sizeof(AVB_HW_STATE_QUERY)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        else { PAVB_HW_STATE_QUERY q=(PAVB_HW_STATE_QUERY)buffer; RtlZeroMemory(q, sizeof(*q)); q->hw_state=AvbContext->hw_state; q->vendor_id=AvbContext->intel_device.pci_vendor_id; q->device_id=AvbContext->intel_device.pci_device_id; q->capabilities=AvbContext->intel_device.capabilities; information=sizeof(AVB_HW_STATE_QUERY); }
        break;
    default:
        status = STATUS_INVALID_DEVICE_REQUEST; break;
    }

    Irp->IoStatus.Information = information; return status;
}

NTSTATUS AvbPlatformInit(_In_ device_t *dev)
{ DEBUGP(DL_TRACE, "==>AvbPlatformInit\n"); if (!dev) return STATUS_INVALID_PARAMETER; DEBUGP(DL_TRACE, "<==AvbPlatformInit: Success\n"); return STATUS_SUCCESS; }
VOID AvbPlatformCleanup(_In_ device_t *dev){ DEBUGP(DL_TRACE, "==>AvbPlatformCleanup\n"); UNREFERENCED_PARAMETER(dev); DEBUGP(DL_TRACE, "<==AvbPlatformCleanup\n"); }

int AvbPciReadConfig(device_t *dev, ULONG offset, ULONG *value){ DEBUGP(DL_TRACE, "AvbPciReadConfig: real impl\n"); return AvbPciReadConfigReal(dev, offset, value);} 
int AvbPciWriteConfig(device_t *dev, ULONG offset, ULONG value){ DEBUGP(DL_TRACE, "AvbPciWriteConfig: real impl\n"); return AvbPciWriteConfigReal(dev, offset, value);} 
int AvbMmioRead(device_t *dev, ULONG offset, ULONG *value){ DEBUGP(DL_TRACE, "AvbMmioRead: real impl\n"); return AvbMmioReadReal(dev, offset, value);} 
int AvbMmioWrite(device_t *dev, ULONG offset, ULONG value){ DEBUGP(DL_TRACE, "AvbMmioWrite: real impl\n"); return AvbMmioWriteReal(dev, offset, value);} 
int AvbMdioRead(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT *value){ DEBUGP(DL_TRACE, "AvbMdioRead: real impl\n"); return AvbMdioReadReal(dev, phy_addr, reg_addr, value);} 
int AvbMdioWrite(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT value){ DEBUGP(DL_TRACE, "AvbMdioWrite: real impl\n"); return AvbMdioWriteReal(dev, phy_addr, reg_addr, value);} 
int AvbReadTimestamp(device_t *dev, ULONGLONG *timestamp){ DEBUGP(DL_TRACE, "AvbReadTimestamp: real impl\n"); return AvbReadTimestampReal(dev, timestamp);} 
int AvbMdioReadI219Direct(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT *value){ DEBUGP(DL_TRACE, "AvbMdioReadI219Direct: real impl\n"); return AvbMdioReadI219DirectReal(dev, phy_addr, reg_addr, value);} 
int AvbMdioWriteI219Direct(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT value){ DEBUGP(DL_TRACE, "AvbMdioWriteI219Direct: real impl\n"); return AvbMdioWriteI219DirectReal(dev, phy_addr, reg_addr, value);} 

BOOLEAN AvbIsIntelDevice(UINT16 vendor_id, UINT16 device_id){ UNREFERENCED_PARAMETER(device_id); return (vendor_id==INTEL_VENDOR_ID);} 
intel_device_type_t AvbGetIntelDeviceType(UINT16 device_id){ switch (device_id){case 0x1533: return INTEL_DEVICE_I210; case 0x153A: case 0x153B: return INTEL_DEVICE_I217; case 0x15B7: case 0x15B8: case 0x15D6: case 0x15D7: case 0x15D8: case 0x0DC7: case 0x1570: case 0x15E3: return INTEL_DEVICE_I219; case 0x15F2: return INTEL_DEVICE_I225; case 0x125B: return INTEL_DEVICE_I226; default: return INTEL_DEVICE_UNKNOWN; }}

PMS_FILTER AvbFindIntelFilterModule(void)
{
    if (g_AvbContext && g_AvbContext->filter_instance) return g_AvbContext->filter_instance;
    PMS_FILTER pFilter=NULL; PLIST_ENTRY Link; BOOLEAN bFalse=FALSE;
    FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse);
    Link = FilterModuleList.Flink;
    while (Link != &FilterModuleList) {
        pFilter = CONTAINING_RECORD(Link, MS_FILTER, FilterModuleLink);
        if (pFilter && pFilter->AvbContext) { FILTER_RELEASE_LOCK(&FilterListLock, bFalse); return pFilter; }
        Link = Link->Flink;
    }
    FILTER_RELEASE_LOCK(&FilterListLock, bFalse);
    DEBUGP(DL_WARN, "AvbFindIntelFilterModule: No Intel filter module found\n");
    return NULL;
}

BOOLEAN AvbIsFilterIntelAdapter(_In_ PMS_FILTER FilterInstance){ if (!FilterInstance) return FALSE; if (FilterInstance->AvbContext){ PAVB_DEVICE_CONTEXT c=(PAVB_DEVICE_CONTEXT)FilterInstance->AvbContext; return (c->intel_device.pci_vendor_id==INTEL_VENDOR_ID);} return FALSE; }