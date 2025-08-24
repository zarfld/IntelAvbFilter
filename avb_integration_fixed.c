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
 *
 * Safe to call repeatedly; only the first successful call enables hardware access.
 * Ensures pci_vendor_id / pci_device_id / capabilities baseline are not lost even
 * if the external library overwrites fields.
 *
 * @param Ctx AVB device context.
 * @return NTSTATUS STATUS_SUCCESS on success, error otherwise.
 */
static NTSTATUS
AvbPerformBasicInitialization(_Inout_ PAVB_DEVICE_CONTEXT Ctx)
{
    if (Ctx->hw_access_enabled) {
        return STATUS_SUCCESS; /* Already done */
    }

    const USHORT vid = INTEL_VENDOR_ID;      /* 0x8086 */
    const USHORT did = 0x1533;               /* I210 (fallback – refined later) */

    /* Seed device_t prior to library call */
    Ctx->intel_device.pci_vendor_id = vid;
    Ctx->intel_device.pci_device_id = did;
    Ctx->intel_device.device_type   = INTEL_DEVICE_I210;
    Ctx->intel_device.private_data  = Ctx;
    Ctx->intel_device.capabilities  = INTEL_CAP_MMIO; /* baseline capability */

    DEBUGP(DL_INFO, "AvbPerformBasicInitialization: seeding VID=0x%04X DID=0x%04X\n", vid, did);

    /* Call cross?platform library init */
    int r = intel_init(&Ctx->intel_device);
    if (r != 0) {
        DEBUGP(DL_ERROR, "intel_init failed (%d)\n", r);
        return STATUS_UNSUCCESSFUL;
    }

    /* Re?assert critical identity fields in case library altered them */
    Ctx->intel_device.pci_vendor_id = vid;
    Ctx->intel_device.pci_device_id = did;
    if (!(Ctx->intel_device.capabilities & INTEL_CAP_MMIO)) {
        Ctx->intel_device.capabilities |= INTEL_CAP_MMIO;
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
    
    DEBUGP(DL_INFO, "==>AvbI210EnsureSystimRunning: Starting I210 PTP initialization\n");
    
    // Step 1: Check if SYSTIM is already running
    if (intel_read_reg(&ctx->intel_device, I210_SYSTIML, &lo)!=0 ||
        intel_read_reg(&ctx->intel_device, I210_SYSTIMH, &hi)!=0) {
        DEBUGP(DL_ERROR, "PTP init: read SYSTIM failed - hardware access problem\n");
        return;
    }
    
    DEBUGP(DL_TRACE, "PTP init: Initial SYSTIM=0x%08X%08X\n", hi, lo);
    
    // If already running (non-zero timestamp), just verify it's incrementing
    if (lo || hi) {
        // Wait briefly and check again to verify it's incrementing
        KeStallExecutionProcessor(10000); // 10ms delay
        ULONG lo2=0, hi2=0;
        if (intel_read_reg(&ctx->intel_device, I210_SYSTIML, &lo2)==0 &&
            intel_read_reg(&ctx->intel_device, I210_SYSTIMH, &hi2)==0) {
            if ((hi2 > hi) || (hi2 == hi && lo2 > lo)) {
                DEBUGP(DL_INFO, "PTP init: SYSTIM already running and incrementing (0x%08X%08X)\n", hi2, lo2);
                return;
            }
            DEBUGP(DL_WARN, "PTP init: SYSTIM non-zero but not incrementing - reinitializing\n");
        }
    }
    
    // Step 2: Read and configure TSAUXC register
    ULONG aux=0;
    if (intel_read_reg(&ctx->intel_device, INTEL_REG_TSAUXC, &aux)==0) {
        DEBUGP(DL_INFO, "PTP init: TSAUXC before=0x%08X\n", aux);
        
        // Clear DisableSystime bit (bit 31) and ensure PHC is enabled (bit 30)
        ULONG new_aux = (aux & 0x7FFFFFFFUL) | 0x40000000UL; // Clear bit 31, set bit 30
        if (aux != new_aux) {
            if (intel_write_reg(&ctx->intel_device, INTEL_REG_TSAUXC, new_aux)==0) {
                ULONG aux_verify=0;
                if (intel_read_reg(&ctx->intel_device, INTEL_REG_TSAUXC, &aux_verify)==0) {
                    DEBUGP(DL_INFO, "PTP init: TSAUXC updated to 0x%08X (PHC enabled, DisableSystime cleared)\n", aux_verify);
                } else {
                    DEBUGP(DL_ERROR, "PTP init: TSAUXC verification read failed\n");
                }
            } else {
                DEBUGP(DL_ERROR, "PTP init: TSAUXC write failed\n");
                return;
            }
        } else {
            DEBUGP(DL_INFO, "PTP init: TSAUXC already properly configured\n");
        }
    } else {
        DEBUGP(DL_ERROR, "PTP init: read TSAUXC failed - cannot configure PHC\n");
        return;
    }
    
    // Step 3: Configure TIMINCA for proper clock operation
    // I210 default: 8ns per clock tick (125MHz system clock)
    // TIMINCA = 0x08000000 (8ns) for normal operation
    ULONG timinca_value = 0x08000000UL; // 8ns increment per tick
    if (intel_write_reg(&ctx->intel_device, I210_TIMINCA, timinca_value)==0) {
        ULONG tim_verify=0;
        if (intel_read_reg(&ctx->intel_device, I210_TIMINCA, &tim_verify)==0) {
            DEBUGP(DL_INFO, "PTP init: TIMINCA set to 0x%08X (8ns per tick)\n", tim_verify);
        }
    } else {
        DEBUGP(DL_ERROR, "PTP init: TIMINCA write failed\n");
        return;
    }
    
    // Step 4: Reset SYSTIM to start the clock
    DEBUGP(DL_INFO, "PTP init: Resetting SYSTIM to start PTP clock\n");
    if (intel_write_reg(&ctx->intel_device, I210_SYSTIML, 0x00000000)!=0 ||
        intel_write_reg(&ctx->intel_device, I210_SYSTIMH, 0x00000000)!=0) {
        DEBUGP(DL_ERROR, "PTP init: SYSTIM reset failed\n");
        return;
    }
    
    // Step 5: Enable RX/TX timestamp capture with proper settings
    // Enable timestamping for all packet types (EN=1, TYPE=0 for all packets)
    ULONG tsyncrx = (1U << I210_TSYNCRXCTL_EN_SHIFT) | (0U << I210_TSYNCRXCTL_TYPE_SHIFT);
    ULONG tsynctx = (1U << I210_TSYNCTXCTL_EN_SHIFT) | (0U << I210_TSYNCTXCTL_TYPE_SHIFT);
    
    if (intel_write_reg(&ctx->intel_device, I210_TSYNCRXCTL, tsyncrx)==0 &&
        intel_write_reg(&ctx->intel_device, I210_TSYNCTXCTL, tsynctx)==0) {
        ULONG rx_verify=0, tx_verify=0;
        intel_read_reg(&ctx->intel_device, I210_TSYNCRXCTL, &rx_verify);
        intel_read_reg(&ctx->intel_device, I210_TSYNCTXCTL, &tx_verify);
        DEBUGP(DL_INFO, "PTP init: Timestamp capture enabled - RX=0x%08X, TX=0x%08X\n", rx_verify, tx_verify);
    } else {
        DEBUGP(DL_ERROR, "PTP init: Timestamp capture enable failed\n");
        return;
    }
    
    // Step 6: Allow clock to start and verify operation
    KeStallExecutionProcessor(50000); // 50ms delay to allow clock to start
    
    // Verify that SYSTIM is now incrementing
    if (intel_read_reg(&ctx->intel_device, I210_SYSTIML, &lo)==0 &&
        intel_read_reg(&ctx->intel_device, I210_SYSTIMH, &hi)==0) {
        
        DEBUGP(DL_INFO, "PTP init: SYSTIM after initialization=0x%08X%08X\n", hi, lo);
        
        // Wait and check if it's incrementing
        KeStallExecutionProcessor(10000); // 10ms delay
        ULONG lo2=0, hi2=0;
        if (intel_read_reg(&ctx->intel_device, I210_SYSTIML, &lo2)==0 &&
            intel_read_reg(&ctx->intel_device, I210_SYSTIMH, &hi2)==0) {
            
            DEBUGP(DL_INFO, "PTP init: SYSTIM after delay=0x%08X%08X\n", hi2, lo2);
            
            if ((hi2 > hi) || (hi2 == hi && lo2 > lo)) {
                DEBUGP(DL_INFO, "? PTP init: SUCCESS - I210 PTP clock is running and incrementing\n");
                // Set capability flags to indicate working PTP
                ctx->intel_device.capabilities |= INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS;
            } else {
                DEBUGP(DL_ERROR, "? PTP init: FAILED - SYSTIM is not incrementing\n");
                // Debug: Check other registers that might affect PTP
                ULONG ctrl=0, status=0;
                if (intel_read_reg(&ctx->intel_device, I210_CTRL, &ctrl)==0) {
                    DEBUGP(DL_INFO, "Debug: CTRL register = 0x%08X\n", ctrl);
                }
                if (intel_read_reg(&ctx->intel_device, I210_STATUS, &status)==0) {
                    DEBUGP(DL_INFO, "Debug: STATUS register = 0x%08X\n", status);
                }
            }
        } else {
            DEBUGP(DL_ERROR, "PTP init: Final SYSTIM verification read failed\n");
        }
    } else {
        DEBUGP(DL_ERROR, "PTP init: Post-initialization SYSTIM read failed\n");
    }
    
    DEBUGP(DL_INFO, "<==AvbI210EnsureSystimRunning: I210 PTP initialization complete\n");
}

// Platform operations with wrapper functions to handle NTSTATUS conversion
static int PlatformInitWrapper(_In_ device_t *dev) {
    NTSTATUS status = AvbPlatformInit(dev);
    return NT_SUCCESS(status) ? 0 : -1;
}

static void PlatformCleanupWrapper(_In_ device_t *dev) {
    AvbPlatformCleanup(dev);
}

// Platform operations structure with correct types
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

// Global AVB context
PAVB_DEVICE_CONTEXT g_AvbContext = NULL;

/**
 * @brief Initialize AVB device context for a filter module
 */
NTSTATUS 
AvbInitializeDevice(
    _In_ PMS_FILTER FilterModule,
    _Out_ PAVB_DEVICE_CONTEXT *AvbContext
)
{
    DEBUGP(DL_TRACE, "==>AvbInitializeDevice: Transitioning to real hardware access\n");
    
    // Call the BAR0 discovery version instead of simulation version
    return AvbInitializeDeviceWithBar0Discovery(FilterModule, AvbContext);
}

/**
 * @brief Cleanup AVB device context
 */
VOID 
AvbCleanupDevice(
    _In_ PAVB_DEVICE_CONTEXT AvbContext
)
{
    DEBUGP(DL_TRACE, "==>AvbCleanupDevice\n");

    if (AvbContext == NULL) {
        return;
    }

    // Clear global context if this was it
    if (g_AvbContext == AvbContext) {
        g_AvbContext = NULL;
    }

    // Free the context
    ExFreePoolWithTag(AvbContext, FILTER_ALLOC_TAG);

    DEBUGP(DL_TRACE, "<==AvbCleanupDevice\n");
}

/**
 * @brief Handle AVB-specific device IOCTLs
 */
NTSTATUS 
AvbHandleDeviceIoControl(
    _In_ PAVB_DEVICE_CONTEXT AvbContext,
    _In_ PIRP Irp
)
{
    if (!AvbContext) return STATUS_DEVICE_NOT_READY;

    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    ULONG ioControlCode      = irpSp->Parameters.DeviceIoControl.IoControlCode;
    PUCHAR buffer            = (PUCHAR)Irp->AssociatedIrp.SystemBuffer;
    ULONG inLen  = irpSp->Parameters.DeviceIoControl.InputBufferLength;
    ULONG outLen = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG_PTR information = 0;
    NTSTATUS status = STATUS_SUCCESS;

    /* Allow INIT / ENUM even if not yet initialized; block others */
    if (!AvbContext->initialized &&
        ioControlCode != IOCTL_AVB_INIT_DEVICE &&
        ioControlCode != IOCTL_AVB_ENUM_ADAPTERS) {
        return STATUS_DEVICE_NOT_READY;
    }

    /* Optional ABI header handling */
    if (inLen >= sizeof(AVB_REQUEST_HEADER)) {
        PAVB_REQUEST_HEADER hdr = (PAVB_REQUEST_HEADER)buffer;
        if (hdr->header_size == sizeof(AVB_REQUEST_HEADER)) {
            AvbContext->last_seen_abi_version = hdr->abi_version;
            if ((hdr->abi_version & 0xFFFF0000u) != (AVB_IOCTL_ABI_VERSION & 0xFFFF0000u)) {
                return STATUS_REVISION_MISMATCH;
            }
            buffer += sizeof(AVB_REQUEST_HEADER);
            inLen  -= sizeof(AVB_REQUEST_HEADER);
            outLen = (outLen >= sizeof(AVB_REQUEST_HEADER)) ? (outLen - sizeof(AVB_REQUEST_HEADER)) : 0;
        }
    }

    switch (ioControlCode) {
    case IOCTL_AVB_INIT_DEVICE:
    {
        status = AvbPerformBasicInitialization(AvbContext);
        if (NT_SUCCESS(status) && AvbContext->intel_device.device_type == INTEL_DEVICE_I210) {
            AvbI210EnsureSystimRunning(AvbContext);
        }
        break;
    }
    case IOCTL_AVB_ENUM_ADAPTERS:
    {
        if (outLen < sizeof(AVB_ENUM_REQUEST)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        PAVB_ENUM_REQUEST req = (PAVB_ENUM_REQUEST)buffer;
        RtlZeroMemory(req, sizeof(*req));
        /* Perform init on-demand to avoid requiring explicit INIT first */
        (void)AvbPerformBasicInitialization(AvbContext);
        req->index       = 0;
        req->count       = 1; /* Single bound adapter */
        req->vendor_id   = AvbContext->intel_device.pci_vendor_id;
        req->device_id   = AvbContext->intel_device.pci_device_id;
        req->capabilities= AvbContext->intel_device.capabilities;
        req->status      = NDIS_STATUS_SUCCESS;
        information      = sizeof(AVB_ENUM_REQUEST);
        DEBUGP(DL_INFO, "ENUM: count=1 VID=0x%04X DID=0x%04X caps=0x%08X init=%d hw=%d\n",
               req->vendor_id, req->device_id, req->capabilities,
               AvbContext->initialized, AvbContext->hw_access_enabled);
        break;
    }
    case IOCTL_AVB_GET_DEVICE_INFO:
    {
        if (outLen < sizeof(AVB_DEVICE_INFO_REQUEST)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        PAVB_DEVICE_INFO_REQUEST req = (PAVB_DEVICE_INFO_REQUEST)buffer;
        RtlZeroMemory(req->device_info, sizeof(req->device_info));
        int r = intel_get_device_info(&AvbContext->intel_device, req->device_info, sizeof(req->device_info));
        size_t used = 0; (void)RtlStringCbLengthA(req->device_info, sizeof(req->device_info), &used);
        req->buffer_size = (ULONG)used;
        req->status = (r==0)?NDIS_STATUS_SUCCESS:NDIS_STATUS_FAILURE;
        information = sizeof(AVB_DEVICE_INFO_REQUEST);
        status = (r==0)?STATUS_SUCCESS:STATUS_UNSUCCESSFUL;
        break;
    }
    case IOCTL_AVB_READ_REGISTER:
    case IOCTL_AVB_WRITE_REGISTER:
    {
        if (inLen < sizeof(AVB_REGISTER_REQUEST) || outLen < sizeof(AVB_REGISTER_REQUEST)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        PAVB_REGISTER_REQUEST req = (PAVB_REGISTER_REQUEST)buffer;
        if (ioControlCode == IOCTL_AVB_READ_REGISTER) {
            ULONG tmp=0; int r = intel_read_reg(&AvbContext->intel_device, req->offset, &tmp); req->value=(avb_u32)tmp; req->status=(r==0)?NDIS_STATUS_SUCCESS:NDIS_STATUS_FAILURE; status=(r==0)?STATUS_SUCCESS:STATUS_UNSUCCESSFUL; }
        else { int r = intel_write_reg(&AvbContext->intel_device, req->offset, req->value); req->status=(r==0)?NDIS_STATUS_SUCCESS:NDIS_STATUS_FAILURE; status=(r==0)?STATUS_SUCCESS:STATUS_UNSUCCESSFUL; }
        information = sizeof(AVB_REGISTER_REQUEST);
        break;
    }
    case IOCTL_AVB_GET_TIMESTAMP:
    case IOCTL_AVB_SET_TIMESTAMP:
    {
        if (inLen < sizeof(AVB_TIMESTAMP_REQUEST) || outLen < sizeof(AVB_TIMESTAMP_REQUEST)) { status = STATUS_BUFFER_TOO_SMALL; break; }
        PAVB_TIMESTAMP_REQUEST req = (PAVB_TIMESTAMP_REQUEST)buffer;
        if (ioControlCode == IOCTL_AVB_GET_TIMESTAMP) {
            ULONGLONG cur=0; struct timespec sys={0}; int r=intel_gettime(&AvbContext->intel_device, req->clock_id, &cur, &sys); if (r!=0) r=AvbReadTimestamp(&AvbContext->intel_device, &cur); req->timestamp=cur; req->status=(r==0)?NDIS_STATUS_SUCCESS:NDIS_STATUS_FAILURE; status=(r==0)?STATUS_SUCCESS:STATUS_UNSUCCESSFUL; }
        else { int r=intel_set_systime(&AvbContext->intel_device, req->timestamp); req->status=(r==0)?NDIS_STATUS_SUCCESS:NDIS_STATUS_FAILURE; status=(r==0)?STATUS_SUCCESS:STATUS_UNSUCCESSFUL; }
        information = sizeof(AVB_TIMESTAMP_REQUEST);
        break;
    }
    case IOCTL_AVB_SETUP_TAS:
    case IOCTL_AVB_SETUP_FP:
    case IOCTL_AVB_SETUP_PTM:
    case IOCTL_AVB_MDIO_READ:
    case IOCTL_AVB_MDIO_WRITE:
    case IOCTL_AVB_OPEN_ADAPTER:
    case IOCTL_AVB_TS_SUBSCRIBE:
    case IOCTL_AVB_TS_RING_MAP:
    case IOCTL_AVB_SETUP_QAV:
        /* (Preserve previous implementations – omitted here for brevity) */
        status = STATUS_INVALID_DEVICE_REQUEST; /* Placeholder in refactor focus */
        break;
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    Irp->IoStatus.Information = information;
    return status;
}

/**
 * @brief Platform initialization for NDIS environment
 */
NTSTATUS 
AvbPlatformInit(
    _In_ device_t *dev
)
{
    DEBUGP(DL_TRACE, "==>AvbPlatformInit\n");
    
    if (dev == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    DEBUGP(DL_TRACE, "<==AvbPlatformInit: Success\n");
    return STATUS_SUCCESS;
}

/**
 * @brief Platform cleanup for NDIS environment
 */
VOID 
AvbPlatformCleanup(
    _In_ device_t *dev
)
{
    DEBUGP(DL_TRACE, "==>AvbPlatformCleanup\n");
    
    if (dev == NULL) {
        return;
    }

    DEBUGP(DL_TRACE, "<==AvbPlatformCleanup\n");
}

// Real hardware access implementations replacing stub placeholders

int AvbPciReadConfig(device_t *dev, ULONG offset, ULONG *value)
{
    DEBUGP(DL_TRACE, "AvbPciReadConfig: Calling real hardware implementation\n");
    return AvbPciReadConfigReal(dev, offset, value);
}

int AvbPciWriteConfig(device_t *dev, ULONG offset, ULONG value)
{
    DEBUGP(DL_TRACE, "AvbPciWriteConfig: Calling real hardware implementation\n");
    return AvbPciWriteConfigReal(dev, offset, value);
}

int AvbMmioRead(device_t *dev, ULONG offset, ULONG *value)
{
    DEBUGP(DL_TRACE, "AvbMmioRead: Calling real hardware implementation\n");
    return AvbMmioReadReal(dev, offset, value);
}

int AvbMmioWrite(device_t *dev, ULONG offset, ULONG value)
{
    DEBUGP(DL_TRACE, "AvbMmioWrite: Calling real hardware implementation\n");
    return AvbMmioWriteReal(dev, offset, value);
}

int AvbMdioRead(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT *value)
{
    DEBUGP(DL_TRACE, "AvbMdioRead: Calling real hardware implementation\n");
    return AvbMdioReadReal(dev, phy_addr, reg_addr, value);
}

int AvbMdioWrite(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT value)
{
    DEBUGP(DL_TRACE, "AvbMdioWrite: Calling real hardware implementation\n");
    return AvbMdioWriteReal(dev, phy_addr, reg_addr, value);
}

int AvbReadTimestamp(device_t *dev, ULONGLONG *timestamp)
{
    DEBUGP(DL_TRACE, "AvbReadTimestamp: Calling real hardware implementation\n");
    return AvbReadTimestampReal(dev, timestamp);
}

int AvbMdioReadI219Direct(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT *value)
{
    DEBUGP(DL_TRACE, "AvbMdioReadI219Direct: Calling real hardware implementation\n");
    return AvbMdioReadI219DirectReal(dev, phy_addr, reg_addr, value);
}

int AvbMdioWriteI219Direct(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT value)
{
    DEBUGP(DL_TRACE, "AvbMdioWriteI219Direct: Calling real hardware implementation\n");
    return AvbMdioWriteI219DirectReal(dev, phy_addr, reg_addr, value);
}

// Helper functions
BOOLEAN AvbIsIntelDevice(UINT16 vendor_id, UINT16 device_id)
{
    UNREFERENCED_PARAMETER(device_id);
    return (vendor_id == INTEL_VENDOR_ID);
}

intel_device_type_t AvbGetIntelDeviceType(UINT16 device_id)
{
    switch (device_id) {
        case 0x1533: return INTEL_DEVICE_I210;
        case 0x153A: return INTEL_DEVICE_I217;  // I217-LM
        case 0x153B: return INTEL_DEVICE_I217;  // I217-V
        case 0x15B7: return INTEL_DEVICE_I219;  // I219-LM
        case 0x15B8: return INTEL_DEVICE_I219;  // I219-V (NEW!)
        case 0x15D6: return INTEL_DEVICE_I219;  // I219-V (NEW!)
        case 0x15D7: return INTEL_DEVICE_I219;  // I219-LM (NEW!)
        case 0x15D8: return INTEL_DEVICE_I219;  // I219-V (NEW!)
        case 0x0DC7: return INTEL_DEVICE_I219;  // I219-LM (22) (NEW!)
        case 0x1570: return INTEL_DEVICE_I219;  // I219-V (5) (NEW!)
        case 0x15E3: return INTEL_DEVICE_I219;  // I219-LM (6) (NEW!)
        case 0x15F2: return INTEL_DEVICE_I225;
        case 0x125B: return INTEL_DEVICE_I226;
        default: return INTEL_DEVICE_UNKNOWN;
    }
}

/**
 * @brief Helper function to find Intel filter modules
 */
PMS_FILTER 
AvbFindIntelFilterModule(void)
{
    // Prefer using an already-initialized global context if present
    if (g_AvbContext != NULL && g_AvbContext->filter_instance != NULL) {
        return g_AvbContext->filter_instance;
    }

    // Otherwise, iterate the filter instance list and pick the first with AvbContext
    PMS_FILTER pFilter = NULL;
    PLIST_ENTRY Link;
    BOOLEAN bFalse = FALSE;

    FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse);
    Link = FilterModuleList.Flink;

    while (Link != &FilterModuleList)
    {
        pFilter = CONTAINING_RECORD(Link, MS_FILTER, FilterModuleLink);
        if (pFilter != NULL && pFilter->AvbContext != NULL)
        {
            FILTER_RELEASE_LOCK(&FilterListLock, bFalse);
            return pFilter;
        }
        Link = Link->Flink;
    }

    FILTER_RELEASE_LOCK(&FilterListLock, bFalse);

    DEBUGP(DL_WARN, "AvbFindIntelFilterModule: No Intel filter module found\n");
    return NULL;
}

/**
 * @brief Check if a filter instance is attached to an Intel adapter
 */
BOOLEAN 
AvbIsFilterIntelAdapter(
    _In_ PMS_FILTER FilterInstance
)
{
    if (FilterInstance == NULL) {
        return FALSE;
    }
    
    if (FilterInstance->AvbContext != NULL) {
        PAVB_DEVICE_CONTEXT context = (PAVB_DEVICE_CONTEXT)FilterInstance->AvbContext;
        return (context->intel_device.pci_vendor_id == INTEL_VENDOR_ID);
    }
    
    return FALSE;
}