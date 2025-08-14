/*++

Module Name:

    avb_integration_fixed.c

Abstract:

    Implementation of AVB integration with Intel filter driver
    Provides hardware access bridge between NDIS filter and Intel AVB library

--*/

#include "precomp.h"
#include "avb_integration.h"

// Platform operations structure (matches what Intel library expects)
struct platform_ops {
    int (*init)(device_t *dev);
    void (*cleanup)(device_t *dev);
    int (*pci_read_config)(device_t *dev, ULONG offset, ULONG *value);
    int (*pci_write_config)(device_t *dev, ULONG offset, ULONG value);
    int (*mmio_read)(device_t *dev, ULONG offset, ULONG *value);
    int (*mmio_write)(device_t *dev, ULONG offset, ULONG value);
    int (*mdio_read)(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT *value);
    int (*mdio_write)(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT value);
    int (*read_timestamp)(device_t *dev, ULONGLONG *timestamp);
};

// Forward declarations with correct Windows kernel types
NTSTATUS AvbPlatformInit(device_t *dev);
VOID AvbPlatformCleanup(device_t *dev);

// Forward declarations for real hardware access implementations
int AvbPciReadConfigReal(device_t *dev, ULONG offset, ULONG *value);
int AvbPciWriteConfigReal(device_t *dev, ULONG offset, ULONG value);
int AvbMmioReadReal(device_t *dev, ULONG offset, ULONG *value);
int AvbMmioWriteReal(device_t *dev, ULONG offset, ULONG value);
int AvbMdioReadReal(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT *value);
int AvbMdioWriteReal(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT value);
int AvbReadTimestampReal(device_t *dev, ULONGLONG *timestamp);
int AvbMdioReadI219DirectReal(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT *value);
int AvbMdioWriteI219DirectReal(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT value);

// Intel library function declarations (implemented in intel_kernel_real.c)
int intel_init(device_t *dev);
int intel_detach(device_t *dev);
int intel_get_device_info(device_t *dev, char *info_buffer, size_t buffer_size);
int intel_read_reg(device_t *dev, ULONG offset, ULONG *value);
int intel_write_reg(device_t *dev, ULONG offset, ULONG value);
int intel_gettime(device_t *dev, clockid_t clk_id, ULONGLONG *curtime, struct timespec *system_time);
int intel_set_systime(device_t *dev, ULONGLONG systime);  
int intel_setup_time_aware_shaper(device_t *dev, struct tsn_tas_config *config);
int intel_setup_frame_preemption(device_t *dev, struct tsn_fp_config *config);
int intel_setup_ptm(device_t *dev, struct ptm_config *config);
int intel_mdio_read(device_t *dev, ULONG page, ULONG reg, USHORT *value);
int intel_mdio_write(device_t *dev, ULONG page, ULONG reg, USHORT value);

// Platform operations with wrapper functions to handle NTSTATUS conversion
static int PlatformInitWrapper(device_t *dev) {
    NTSTATUS status = AvbPlatformInit(dev);
    return NT_SUCCESS(status) ? 0 : -1;
}

static void PlatformCleanupWrapper(device_t *dev) {
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
    PIO_STACK_LOCATION irpSp;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG ioControlCode;
    PUCHAR buffer;
    ULONG inputBufferLength, outputBufferLength;
    ULONG_PTR information = 0;

    if (AvbContext == NULL) {
        DEBUGP(DL_ERROR, "AvbHandleDeviceIoControl: AvbContext is NULL\n");
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (!AvbContext->initialized) {
        DEBUGP(DL_ERROR, "AvbHandleDeviceIoControl: AvbContext not initialized\n");
        return STATUS_DEVICE_NOT_READY;
    }

    irpSp = IoGetCurrentIrpStackLocation(Irp);
    ioControlCode = irpSp->Parameters.DeviceIoControl.IoControlCode;
    
    DEBUGP(DL_TRACE, "==>AvbHandleDeviceIoControl: IOCTL=0x%x\n", ioControlCode);
    
    buffer = (PUCHAR)Irp->AssociatedIrp.SystemBuffer;
    inputBufferLength = irpSp->Parameters.DeviceIoControl.InputBufferLength;
    outputBufferLength = irpSp->Parameters.DeviceIoControl.OutputBufferLength;

    switch (ioControlCode) {
        case IOCTL_AVB_INIT_DEVICE:
        {
            if (!AvbContext->hw_access_enabled) {
                int result = intel_init(&AvbContext->intel_device);
                AvbContext->hw_access_enabled = (result == 0) ? TRUE : FALSE;
                status = (result == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                DEBUGP(DL_TRACE, "AVB device initialized, result=%d\n", result);
            }
            break;
        }

        case IOCTL_AVB_GET_DEVICE_INFO:
        {
            if (outputBufferLength >= sizeof(AVB_DEVICE_INFO_REQUEST)) {
                PAVB_DEVICE_INFO_REQUEST req = (PAVB_DEVICE_INFO_REQUEST)buffer;
                RtlZeroMemory(req->device_info, sizeof(req->device_info));
                int r = intel_get_device_info(&AvbContext->intel_device, req->device_info, sizeof(req->device_info));
                req->buffer_size = (ULONG)strnlen(req->device_info, sizeof(req->device_info));
                req->status = (r == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
                information = sizeof(AVB_DEVICE_INFO_REQUEST);
                status = (r == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                DEBUGP(DL_TRACE, "GET_DEVICE_INFO: result=%d, size=%lu\n", r, req->buffer_size);
            } else {
                status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        case IOCTL_AVB_READ_REGISTER:
        {
            if (inputBufferLength >= sizeof(AVB_REGISTER_REQUEST) && outputBufferLength >= sizeof(AVB_REGISTER_REQUEST)) {
                PAVB_REGISTER_REQUEST req = (PAVB_REGISTER_REQUEST)buffer; // METHOD_BUFFERED echo
                DEBUGP(DL_TRACE, "READ_REGISTER offset=0x%x\n", req->offset);
                int result = intel_read_reg(&AvbContext->intel_device, req->offset, &req->value);
                req->status = (result == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
                information = sizeof(AVB_REGISTER_REQUEST);
                status = (result == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                if (result == 0) {
                    DEBUGP(DL_TRACE, "READ_REGISTER OK, value=0x%x\n", req->value);
                } else {
                    DEBUGP(DL_ERROR, "READ_REGISTER failed, off=0x%x, res=%d\n", req->offset, result);
                }
            } else {
                status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        case IOCTL_AVB_WRITE_REGISTER:
        {
            if (inputBufferLength >= sizeof(AVB_REGISTER_REQUEST) && outputBufferLength >= sizeof(AVB_REGISTER_REQUEST)) {
                PAVB_REGISTER_REQUEST req = (PAVB_REGISTER_REQUEST)buffer; // METHOD_BUFFERED echo
                DEBUGP(DL_TRACE, "WRITE_REGISTER offset=0x%x, value=0x%x\n", req->offset, req->value);
                int result = intel_write_reg(&AvbContext->intel_device, req->offset, req->value);
                req->status = (result == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
                information = sizeof(AVB_REGISTER_REQUEST);
                status = (result == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
            } else {
                status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        case IOCTL_AVB_GET_TIMESTAMP:
        {
            if (inputBufferLength >= sizeof(AVB_TIMESTAMP_REQUEST) && outputBufferLength >= sizeof(AVB_TIMESTAMP_REQUEST)) {
                PAVB_TIMESTAMP_REQUEST req = (PAVB_TIMESTAMP_REQUEST)buffer; // METHOD_BUFFERED echo
                ULONGLONG curtime = 0;
                struct timespec sys = {0};
                int result = intel_gettime(&AvbContext->intel_device, req->clock_id, &curtime, &sys);
                if (result != 0) {
                    result = AvbReadTimestamp(&AvbContext->intel_device, &curtime);
                }
                req->timestamp = curtime;
                req->status = (result == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
                information = sizeof(AVB_TIMESTAMP_REQUEST);
                status = (result == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                DEBUGP(DL_TRACE, "GET_TIMESTAMP: result=%d, ts=0x%llx\n", result, curtime);
            } else {
                status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        case IOCTL_AVB_SET_TIMESTAMP:
        {
            if (inputBufferLength >= sizeof(AVB_TIMESTAMP_REQUEST) && outputBufferLength >= sizeof(AVB_TIMESTAMP_REQUEST)) {
                PAVB_TIMESTAMP_REQUEST req = (PAVB_TIMESTAMP_REQUEST)buffer; // METHOD_BUFFERED echo
                int result = intel_set_systime(&AvbContext->intel_device, req->timestamp);
                req->status = (result == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
                information = sizeof(AVB_TIMESTAMP_REQUEST);
                status = (result == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                DEBUGP(DL_TRACE, "SET_TIMESTAMP: result=%d\n", result);
            } else {
                status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        case IOCTL_AVB_SETUP_TAS:
        {
            if (inputBufferLength >= sizeof(AVB_TAS_REQUEST) && outputBufferLength >= sizeof(AVB_TAS_REQUEST)) {
                PAVB_TAS_REQUEST req = (PAVB_TAS_REQUEST)buffer; // METHOD_BUFFERED echo
                int result = intel_setup_time_aware_shaper(&AvbContext->intel_device, &req->config);
                req->status = (result == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
                information = sizeof(AVB_TAS_REQUEST);
                status = (result == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                DEBUGP(DL_TRACE, "SETUP_TAS: result=%d\n", result);
            } else {
                status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        case IOCTL_AVB_SETUP_FP:
        {
            if (inputBufferLength >= sizeof(AVB_FP_REQUEST) && outputBufferLength >= sizeof(AVB_FP_REQUEST)) {
                PAVB_FP_REQUEST req = (PAVB_FP_REQUEST)buffer; // METHOD_BUFFERED echo
                int result = intel_setup_frame_preemption(&AvbContext->intel_device, &req->config);
                req->status = (result == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
                information = sizeof(AVB_FP_REQUEST);
                status = (result == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                DEBUGP(DL_TRACE, "SETUP_FP: result=%d\n", result);
            } else {
                status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        case IOCTL_AVB_SETUP_PTM:
        {
            if (inputBufferLength >= sizeof(AVB_PTM_REQUEST) && outputBufferLength >= sizeof(AVB_PTM_REQUEST)) {
                PAVB_PTM_REQUEST req = (PAVB_PTM_REQUEST)buffer; // METHOD_BUFFERED echo
                int result = intel_setup_ptm(&AvbContext->intel_device, &req->config);
                req->status = (result == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
                information = sizeof(AVB_PTM_REQUEST);
                status = (result == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                DEBUGP(DL_TRACE, "SETUP_PTM: result=%d\n", result);
            } else {
                status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        case IOCTL_AVB_MDIO_READ:
        {
            if (inputBufferLength >= sizeof(AVB_MDIO_REQUEST) && outputBufferLength >= sizeof(AVB_MDIO_REQUEST)) {
                PAVB_MDIO_REQUEST req = (PAVB_MDIO_REQUEST)buffer; // METHOD_BUFFERED echo
                USHORT val = 0;
                int result = intel_mdio_read(&AvbContext->intel_device, req->page, req->reg, &val);
                req->value = val;
                req->status = (result == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
                information = sizeof(AVB_MDIO_REQUEST);
                status = (result == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                DEBUGP(DL_TRACE, "MDIO_READ: page=%lu reg=%lu val=0x%04x result=%d\n", req->page, req->reg, req->value, result);
            } else {
                status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        case IOCTL_AVB_MDIO_WRITE:
        {
            if (inputBufferLength >= sizeof(AVB_MDIO_REQUEST) && outputBufferLength >= sizeof(AVB_MDIO_REQUEST)) {
                PAVB_MDIO_REQUEST req = (PAVB_MDIO_REQUEST)buffer; // METHOD_BUFFERED echo
                int result = intel_mdio_write(&AvbContext->intel_device, req->page, req->reg, req->value);
                req->status = (result == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
                information = sizeof(AVB_MDIO_REQUEST);
                status = (result == 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
                DEBUGP(DL_TRACE, "MDIO_WRITE: page=%lu reg=%lu val=0x%04x result=%d\n", req->page, req->reg, req->value, result);
            } else {
                status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        default:
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    Irp->IoStatus.Information = information;
    DEBUGP(DL_TRACE, "<==AvbHandleDeviceIoControl: 0x%x\n", status);
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