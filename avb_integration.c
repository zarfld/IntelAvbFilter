/*++

Module Name:

    avb_integration.c

Abstract:

    Implementation of AVB integration with Intel filter driver
    Provides hardware access bridge between NDIS filter and Intel AVB library

--*/

#include "precomp.h"
#include "avb_integration.h"

// Forward declarations for platform operations
static const struct platform_ops ndis_platform_ops = {
    .init = AvbPlatformInit,
    .cleanup = AvbPlatformCleanup,
    .pci_read_config = AvbPciReadConfig,
    .pci_write_config = AvbPciWriteConfig,
    .mmio_read = AvbMmioRead,
    .mmio_write = AvbMmioWrite,
    .mdio_read = AvbMdioRead,
    .mdio_write = AvbMdioWrite,
    .read_timestamp = AvbReadTimestamp
};

// Global AVB context (could be moved to filter instance context later)
static PAVB_DEVICE_CONTEXT g_AvbContext = NULL;

/**
 * @brief Initialize AVB device context for a filter module
 */
NTSTATUS 
AvbInitializeDevice(
    _In_ PMS_FILTER FilterModule,
    _Out_ PAVB_DEVICE_CONTEXT *AvbContext
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PAVB_DEVICE_CONTEXT context = NULL;
    NDIS_MINIPORT_ADAPTER_ATTRIBUTES adapterAttributes;
    NDIS_STATUS ndisStatus;

    DEBUGP(DL_TRACE, "==>AvbInitializeDevice\n");

    *AvbContext = NULL;

    // Allocate context
    context = (PAVB_DEVICE_CONTEXT)ExAllocatePoolZero(
        NonPagedPool,
        sizeof(AVB_DEVICE_CONTEXT),
        FILTER_ALLOC_TAG
    );

    if (context == NULL) {
        DEBUGP(DL_ERROR, "Failed to allocate AVB device context\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Initialize context
    context->initialized = FALSE;
    context->filter_instance = FilterModule;
    context->hw_access_enabled = FALSE;
    context->miniport_handle = FilterModule->FilterHandle;

    // Get adapter attributes to identify the device
    ndisStatus = NdisFOidRequest(
        FilterModule->FilterHandle,
        &adapterAttributes // This needs proper OID request structure
    );

    if (ndisStatus != NDIS_STATUS_SUCCESS) {
        DEBUGP(DL_ERROR, "Failed to get adapter attributes: 0x%x\n", ndisStatus);
        // Continue anyway, we'll try to determine device type later
    }

    // Initialize Intel device structure
    RtlZeroMemory(&context->intel_device, sizeof(device_t));
    context->intel_device.private_data = context;
    context->intel_device.pci_vendor_id = INTEL_VENDOR_ID;
    // Device ID will be determined during hardware access

    // Set up platform operations for Windows NDIS environment
    // This will be used by the Intel AVB library
    // (Note: The actual assignment might need to be done differently
    //  depending on how the Intel library expects this)

    context->initialized = TRUE;
    *AvbContext = context;
    g_AvbContext = context; // Store globally for platform operations

    DEBUGP(DL_TRACE, "<==AvbInitializeDevice: Success\n");
    return STATUS_SUCCESS;
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

    if (AvbContext->initialized) {
        // Call Intel library cleanup if it was initialized
        intel_detach(&AvbContext->intel_device);
    }

    if (g_AvbContext == AvbContext) {
        g_AvbContext = NULL;
    }

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
    PUCHAR inputBuffer, outputBuffer;
    ULONG inputBufferLength, outputBufferLength;
    ULONG_PTR information = 0;

    DEBUGP(DL_TRACE, "==>AvbHandleDeviceIoControl: IOCTL=0x%x\n", ioControlCode);

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
    
    inputBuffer = outputBuffer = (PUCHAR)Irp->AssociatedIrp.SystemBuffer;
    inputBufferLength = irpSp->Parameters.DeviceIoControl.InputBufferLength;
    outputBufferLength = irpSp->Parameters.DeviceIoControl.OutputBufferLength;

    switch (ioControlCode) {
        
        case IOCTL_AVB_INIT_DEVICE:
        {
            if (!AvbContext->hw_access_enabled) {
                // Initialize Intel AVB library
                int result = intel_init(&AvbContext->intel_device);
                if (result == 0) {
                    AvbContext->hw_access_enabled = TRUE;
                    status = STATUS_SUCCESS;
                } else {
                    status = STATUS_UNSUCCESSFUL;
                }
            }
            break;
        }

        case IOCTL_AVB_GET_DEVICE_INFO:
        {
            PAVB_DEVICE_INFO_REQUEST request = (PAVB_DEVICE_INFO_REQUEST)inputBuffer;
            if (inputBufferLength >= sizeof(AVB_DEVICE_INFO_REQUEST)) {
                int result = intel_get_device_info(
                    &AvbContext->intel_device,
                    request->device_info,
                    request->buffer_size
                );
                request->status = (result == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
                information = sizeof(AVB_DEVICE_INFO_REQUEST);
            } else {
                status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        case IOCTL_AVB_READ_REGISTER:
        {
            PAVB_REGISTER_REQUEST request = (PAVB_REGISTER_REQUEST)inputBuffer;
            if (inputBufferLength >= sizeof(AVB_REGISTER_REQUEST) && 
                outputBufferLength >= sizeof(AVB_REGISTER_REQUEST)) {
                
                DEBUGP(DL_TRACE, "AvbHandleDeviceIoControl: READ_REGISTER offset=0x%x\n", request->offset);
                
                int result = intel_read_reg(
                    &AvbContext->intel_device,
                    request->offset,
                    &request->value
                );
                
                request->status = (result == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
                information = sizeof(AVB_REGISTER_REQUEST);
                
                if (result == 0) {
                    DEBUGP(DL_TRACE, "AvbHandleDeviceIoControl: READ_REGISTER successful, value=0x%x\n", request->value);
                } else {
                    DEBUGP(DL_ERROR, "AvbHandleDeviceIoControl: READ_REGISTER failed, offset=0x%x, result=%d\n", 
                           request->offset, result);
                }
            } else {
                DEBUGP(DL_ERROR, "AvbHandleDeviceIoControl: READ_REGISTER buffer too small, in=%lu, out=%lu, required=%lu\n",
                       inputBufferLength, outputBufferLength, sizeof(AVB_REGISTER_REQUEST));
                status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        case IOCTL_AVB_WRITE_REGISTER:
        {
            PAVB_REGISTER_REQUEST request = (PAVB_REGISTER_REQUEST)inputBuffer;
            if (inputBufferLength >= sizeof(AVB_REGISTER_REQUEST) &&
                outputBufferLength >= sizeof(AVB_REGISTER_REQUEST)) {
                
                DEBUGP(DL_TRACE, "AvbHandleDeviceIoControl: WRITE_REGISTER offset=0x%x, value=0x%x\n", 
                       request->offset, request->value);
                
                int result = intel_write_reg(
                    &AvbContext->intel_device,
                    request->offset,
                    request->value
                );
                
                request->status = (result == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
                information = sizeof(AVB_REGISTER_REQUEST);
                
                if (result == 0) {
                    DEBUGP(DL_TRACE, "AvbHandleDeviceIoControl: WRITE_REGISTER successful\n");
                } else {
                    DEBUGP(DL_ERROR, "AvbHandleDeviceIoControl: WRITE_REGISTER failed, offset=0x%x, value=0x%x, result=%d\n", 
                           request->offset, request->value, result);
                }
            } else {
                DEBUGP(DL_ERROR, "AvbHandleDeviceIoControl: WRITE_REGISTER buffer too small, in=%lu, out=%lu, required=%lu\n",
                       inputBufferLength, outputBufferLength, sizeof(AVB_REGISTER_REQUEST));
                status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        case IOCTL_AVB_GET_TIMESTAMP:
        {
            PAVB_TIMESTAMP_REQUEST request = (PAVB_TIMESTAMP_REQUEST)inputBuffer;
            if (inputBufferLength >= sizeof(AVB_TIMESTAMP_REQUEST)) {
                struct timespec system_time;
                int result = intel_gettime(
                    &AvbContext->intel_device,
                    request->clock_id,
                    &request->timestamp,
                    &system_time
                );
                request->status = (result == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
                information = sizeof(AVB_TIMESTAMP_REQUEST);
            } else {
                status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        case IOCTL_AVB_SET_TIMESTAMP:
        {
            PAVB_TIMESTAMP_REQUEST request = (PAVB_TIMESTAMP_REQUEST)inputBuffer;
            if (inputBufferLength >= sizeof(AVB_TIMESTAMP_REQUEST)) {
                int result = intel_set_systime(
                    &AvbContext->intel_device,
                    request->timestamp
                );
                request->status = (result == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
                information = sizeof(AVB_TIMESTAMP_REQUEST);
            } else {
                status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        case IOCTL_AVB_SETUP_TAS:
        {
            PAVB_TAS_REQUEST request = (PAVB_TAS_REQUEST)inputBuffer;
            if (inputBufferLength >= sizeof(AVB_TAS_REQUEST)) {
                int result = intel_setup_time_aware_shaper(
                    &AvbContext->intel_device,
                    &request->config
                );
                request->status = (result == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
                information = sizeof(AVB_TAS_REQUEST);
            } else {
                status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        case IOCTL_AVB_SETUP_FP:
        {
            PAVB_FP_REQUEST request = (PAVB_FP_REQUEST)inputBuffer;
            if (inputBufferLength >= sizeof(AVB_FP_REQUEST)) {
                int result = intel_setup_frame_preemption(
                    &AvbContext->intel_device,
                    &request->config
                );
                request->status = (result == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
                information = sizeof(AVB_FP_REQUEST);
            } else {
                status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        case IOCTL_AVB_SETUP_PTM:
        {
            PAVB_PTM_REQUEST request = (PAVB_PTM_REQUEST)inputBuffer;
            if (inputBufferLength >= sizeof(AVB_PTM_REQUEST)) {
                int result = intel_setup_ptm(
                    &AvbContext->intel_device,
                    &request->config
                );
                request->status = (result == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
                information = sizeof(AVB_PTM_REQUEST);
            } else {
                status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        case IOCTL_AVB_MDIO_READ:
        {
            PAVB_MDIO_REQUEST request = (PAVB_MDIO_REQUEST)inputBuffer;
            if (inputBufferLength >= sizeof(AVB_MDIO_REQUEST)) {
                int result = intel_mdio_read(
                    &AvbContext->intel_device,
                    request->page,
                    request->reg,
                    &request->value
                );
                request->status = (result == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
                information = sizeof(AVB_MDIO_REQUEST);
            } else {
                status = STATUS_BUFFER_TOO_SMALL;
            }
            break;
        }

        case IOCTL_AVB_MDIO_WRITE:
        {
            PAVB_MDIO_REQUEST request = (PAVB_MDIO_REQUEST)inputBuffer;
            if (inputBufferLength >= sizeof(AVB_MDIO_REQUEST)) {
                int result = intel_mdio_write(
                    &AvbContext->intel_device,
                    request->page,
                    request->reg,
                    request->value
                );
                request->status = (result == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
                information = sizeof(AVB_MDIO_REQUEST);
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
    PAVB_DEVICE_CONTEXT context = (PAVB_DEVICE_CONTEXT)dev->private_data;
    
    DEBUGP(DL_TRACE, "==>AvbPlatformInit\n");
    
    if (context == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    // Initialize any Windows-specific resources here
    // This could include mapping hardware registers, etc.
    
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
    PAVB_DEVICE_CONTEXT context = (PAVB_DEVICE_CONTEXT)dev->private_data;
    
    DEBUGP(DL_TRACE, "==>AvbPlatformCleanup\n");
    
    if (context == NULL) {
        return;
    }

    // Cleanup any Windows-specific resources here
    
    DEBUGP(DL_TRACE, "<==AvbPlatformCleanup\n");
}

/**
 * @brief Read PCI configuration space
 * This implementation uses NDIS to query PCI configuration
 */
int 
AvbPciReadConfig(
    _In_ device_t *dev, 
    _In_ DWORD offset, 
    _Out_ DWORD *value
)
{
    PAVB_DEVICE_CONTEXT context = (PAVB_DEVICE_CONTEXT)dev->private_data;
    NDIS_STATUS status;
    FILTER_REQUEST filterRequest;
    ULONG pciConfig[64]; // Buffer for PCI config space (256 bytes)
    
    DEBUGP(DL_TRACE, "AvbPciReadConfig: offset=0x%x\n", offset);
    
    if (context == NULL || value == NULL || context->filter_instance == NULL) {
        return -1;
    }

    // Validate offset alignment and bounds
    if (offset % 4 != 0 || offset >= 256) {
        DEBUGP(DL_ERROR, "Invalid PCI config offset: 0x%x\n", offset);
        return -1;
    }

    // Initialize the filter request
    NdisZeroMemory(&filterRequest, sizeof(FILTER_REQUEST));
    NdisInitializeEvent(&filterRequest.ReqEvent);
    
    // Set up OID request to read PCI configuration
    filterRequest.Request.Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
    filterRequest.Request.Header.Revision = NDIS_OID_REQUEST_REVISION_1;
    filterRequest.Request.Header.Size = sizeof(NDIS_OID_REQUEST);
    filterRequest.Request.RequestType = NdisRequestQueryInformation;
    filterRequest.Request.RequestId = 0;
    filterRequest.Request.Oid = OID_GEN_PCI_DEVICE_CUSTOM_PROPERTIES;
    filterRequest.Request.DATA.QUERY_INFORMATION.InformationBuffer = pciConfig;
    filterRequest.Request.DATA.QUERY_INFORMATION.InformationBufferLength = sizeof(pciConfig);
    filterRequest.Request.DATA.QUERY_INFORMATION.BytesWritten = 0;
    filterRequest.Request.DATA.QUERY_INFORMATION.BytesNeeded = 0;
    
    // Send the request
    status = NdisFOidRequest(context->filter_instance->FilterHandle, &filterRequest.Request);
    
    if (status == NDIS_STATUS_PENDING) {
        NdisWaitEvent(&filterRequest.ReqEvent, 0);
        status = filterRequest.Status;
    }
    
    if (status == NDIS_STATUS_SUCCESS) {
        // Extract the requested DWORD from the configuration space
        *value = pciConfig[offset / 4];
        DEBUGP(DL_TRACE, "PCI config read successful: offset=0x%x, value=0x%x\n", offset, *value);
        return 0;
    } else {
        DEBUGP(DL_WARN, "PCI config read failed: offset=0x%x, status=0x%x\n", offset, status);
        *value = 0;
        return -1;
    }
}

/**
 * @brief Write PCI configuration space
 * This implementation uses NDIS to set PCI configuration
 */
int 
AvbPciWriteConfig(
    _In_ device_t *dev, 
    _In_ DWORD offset, 
    _In_ DWORD value
)
{
    PAVB_DEVICE_CONTEXT context = (PAVB_DEVICE_CONTEXT)dev->private_data;
    NDIS_STATUS status;
    FILTER_REQUEST filterRequest;
    struct {
        ULONG Offset;
        ULONG Value;
    } pciWriteData;
    
    DEBUGP(DL_TRACE, "AvbPciWriteConfig: offset=0x%x, value=0x%x\n", offset, value);
    
    if (context == NULL || context->filter_instance == NULL) {
        return -1;
    }

    // Validate offset alignment and bounds
    if (offset % 4 != 0 || offset >= 256) {
        DEBUGP(DL_ERROR, "Invalid PCI config offset: 0x%x\n", offset);
        return -1;
    }

    // Initialize the filter request
    NdisZeroMemory(&filterRequest, sizeof(FILTER_REQUEST));
    NdisInitializeEvent(&filterRequest.ReqEvent);
    
    // Prepare write data
    pciWriteData.Offset = offset;
    pciWriteData.Value = value;
    
    // Set up OID request to write PCI configuration
    filterRequest.Request.Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
    filterRequest.Request.Header.Revision = NDIS_OID_REQUEST_REVISION_1;
    filterRequest.Request.Header.Size = sizeof(NDIS_OID_REQUEST);
    filterRequest.Request.RequestType = NdisRequestSetInformation;
    filterRequest.Request.RequestId = 0;
    filterRequest.Request.Oid = OID_GEN_PCI_DEVICE_CUSTOM_PROPERTIES;
    filterRequest.Request.DATA.SET_INFORMATION.InformationBuffer = &pciWriteData;
    filterRequest.Request.DATA.SET_INFORMATION.InformationBufferLength = sizeof(pciWriteData);
    filterRequest.Request.DATA.SET_INFORMATION.BytesRead = 0;
    filterRequest.Request.DATA.SET_INFORMATION.BytesNeeded = 0;
    
    // Send the request
    status = NdisFOidRequest(context->filter_instance->FilterHandle, &filterRequest.Request);
    
    if (status == NDIS_STATUS_PENDING) {
        NdisWaitEvent(&filterRequest.ReqEvent, 0);
        status = filterRequest.Status;
    }
    
    if (status == NDIS_STATUS_SUCCESS) {
        DEBUGP(DL_TRACE, "PCI config write successful: offset=0x%x, value=0x%x\n", offset, value);
        return 0;
    } else {
        DEBUGP(DL_WARN, "PCI config write failed: offset=0x%x, value=0x%x, status=0x%x\n", offset, value, status);
        return -1;
    }
}

/**
 * @brief Read MMIO register
 * This implementation maps the hardware BAR and performs MMIO read
 */
int 
AvbMmioRead(
    _In_ device_t *dev, 
    _In_ uint32_t offset, 
    _Out_ uint32_t *value
)
{
    PAVB_DEVICE_CONTEXT context = (PAVB_DEVICE_CONTEXT)dev->private_data;
    NDIS_STATUS status;
    FILTER_REQUEST filterRequest;
    struct {
        ULONG Offset;
        ULONG Value;
    } mmioReadData;
    
    DEBUGP(DL_TRACE, "AvbMmioRead: offset=0x%x\n", offset);
    
    if (context == NULL || value == NULL || context->filter_instance == NULL) {
        return -1;
    }

    // Validate offset alignment
    if (offset % 4 != 0) {
        DEBUGP(DL_ERROR, "Invalid MMIO offset alignment: 0x%x\n", offset);
        return -1;
    }

    // Initialize the filter request
    NdisZeroMemory(&filterRequest, sizeof(FILTER_REQUEST));
    NdisInitializeEvent(&filterRequest.ReqEvent);
    
    // Prepare read data
    mmioReadData.Offset = offset;
    mmioReadData.Value = 0;
    
    // Set up OID request to read MMIO register
    filterRequest.Request.Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
    filterRequest.Request.Header.Revision = NDIS_OID_REQUEST_REVISION_1;
    filterRequest.Request.Header.Size = sizeof(NDIS_OID_REQUEST);
    filterRequest.Request.RequestType = NdisRequestQueryInformation;
    filterRequest.Request.RequestId = 0;
    filterRequest.Request.Oid = OID_GEN_HARDWARE_STATUS; // Using generic hardware status OID
    filterRequest.Request.DATA.QUERY_INFORMATION.InformationBuffer = &mmioReadData;
    filterRequest.Request.DATA.QUERY_INFORMATION.InformationBufferLength = sizeof(mmioReadData);
    filterRequest.Request.DATA.QUERY_INFORMATION.BytesWritten = 0;
    filterRequest.Request.DATA.QUERY_INFORMATION.BytesNeeded = 0;
    
    // Send the request
    status = NdisFOidRequest(context->filter_instance->FilterHandle, &filterRequest.Request);
    
    if (status == NDIS_STATUS_PENDING) {
        NdisWaitEvent(&filterRequest.ReqEvent, 0);
        status = filterRequest.Status;
    }
    
    if (status == NDIS_STATUS_SUCCESS) {
        *value = mmioReadData.Value;
        DEBUGP(DL_TRACE, "MMIO read successful: offset=0x%x, value=0x%x\n", offset, *value);
        return 0;
    } else {
        DEBUGP(DL_WARN, "MMIO read failed: offset=0x%x, status=0x%x\n", offset, status);
        
        // Fallback: Try to use a custom IOCTL to the miniport
        // This would require cooperation from the miniport driver
        *value = 0;
        return -1;
    }
}

/**
 * @brief Write MMIO register
 * This implementation maps the hardware BAR and performs MMIO write
 */
int 
AvbMmioWrite(
    _In_ device_t *dev, 
    _In_ uint32_t offset, 
    _In_ uint32_t value
)
{
    PAVB_DEVICE_CONTEXT context = (PAVB_DEVICE_CONTEXT)dev->private_data;
    NDIS_STATUS status;
    FILTER_REQUEST filterRequest;
    struct {
        ULONG Offset;
        ULONG Value;
    } mmioWriteData;
    
    DEBUGP(DL_TRACE, "AvbMmioWrite: offset=0x%x, value=0x%x\n", offset, value);
    
    if (context == NULL || context->filter_instance == NULL) {
        return -1;
    }

    // Validate offset alignment
    if (offset % 4 != 0) {
        DEBUGP(DL_ERROR, "Invalid MMIO offset alignment: 0x%x\n", offset);
        return -1;
    }

    // Initialize the filter request
    NdisZeroMemory(&filterRequest, sizeof(FILTER_REQUEST));
    NdisInitializeEvent(&filterRequest.ReqEvent);
    
    // Prepare write data
    mmioWriteData.Offset = offset;
    mmioWriteData.Value = value;
    
    // Set up OID request to write MMIO register
    filterRequest.Request.Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
    filterRequest.Request.Header.Revision = NDIS_OID_REQUEST_REVISION_1;
    filterRequest.Request.Header.Size = sizeof(NDIS_OID_REQUEST);
    filterRequest.Request.RequestType = NdisRequestSetInformation;
    filterRequest.Request.RequestId = 0;
    filterRequest.Request.Oid = OID_GEN_HARDWARE_STATUS; // Using generic hardware status OID
    filterRequest.Request.DATA.SET_INFORMATION.InformationBuffer = &mmioWriteData;
    filterRequest.Request.DATA.SET_INFORMATION.InformationBufferLength = sizeof(mmioWriteData);
    filterRequest.Request.DATA.SET_INFORMATION.BytesRead = 0;
    filterRequest.Request.DATA.SET_INFORMATION.BytesNeeded = 0;
    
    // Send the request
    status = NdisFOidRequest(context->filter_instance->FilterHandle, &filterRequest.Request);
    
    if (status == NDIS_STATUS_PENDING) {
        NdisWaitEvent(&filterRequest.ReqEvent, 0);
        status = filterRequest.Status;
    }
    
    if (status == NDIS_STATUS_SUCCESS) {
        DEBUGP(DL_TRACE, "MMIO write successful: offset=0x%x, value=0x%x\n", offset, value);
        return 0;
    } else {
        DEBUGP(DL_WARN, "MMIO write failed: offset=0x%x, value=0x%x, status=0x%x\n", offset, value, status);
        return -1;
    }
}

/**
 * @brief Read MDIO register
 * This implementation uses NDIS OID requests to access PHY registers
 */
int 
AvbMdioRead(
    _In_ device_t *dev, 
    _In_ uint16_t phy_addr, 
    _In_ uint16_t reg_addr, 
    _Out_ uint16_t *value
)
{
    PAVB_DEVICE_CONTEXT context = (PAVB_DEVICE_CONTEXT)dev->private_data;
    NDIS_STATUS status;
    FILTER_REQUEST filterRequest;
    NDIS_REQUEST_PHY_READ phyRead;
    
    DEBUGP(DL_TRACE, "AvbMdioRead: phy=0x%x, reg=0x%x\n", phy_addr, reg_addr);
    
    if (context == NULL || value == NULL || context->filter_instance == NULL) {
        return -1;
    }

    // Initialize the filter request
    NdisZeroMemory(&filterRequest, sizeof(FILTER_REQUEST));
    NdisInitializeEvent(&filterRequest.ReqEvent);
    
    // Prepare PHY read structure
    NdisZeroMemory(&phyRead, sizeof(phyRead));
    phyRead.PhyAddress = phy_addr;
    phyRead.RegisterAddress = reg_addr;
    phyRead.DeviceAddress = 0; // For clause 22 MDIO
    
    // Set up OID request to read PHY register
    filterRequest.Request.Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
    filterRequest.Request.Header.Revision = NDIS_OID_REQUEST_REVISION_1;
    filterRequest.Request.Header.Size = sizeof(NDIS_OID_REQUEST);
    filterRequest.Request.RequestType = NdisRequestQueryInformation;
    filterRequest.Request.RequestId = 0;
    filterRequest.Request.Oid = OID_GEN_PHY_READ;
    filterRequest.Request.DATA.QUERY_INFORMATION.InformationBuffer = &phyRead;
    filterRequest.Request.DATA.QUERY_INFORMATION.InformationBufferLength = sizeof(phyRead);
    filterRequest.Request.DATA.QUERY_INFORMATION.BytesWritten = 0;
    filterRequest.Request.DATA.QUERY_INFORMATION.BytesNeeded = 0;
    
    // Send the request
    status = NdisFOidRequest(context->filter_instance->FilterHandle, &filterRequest.Request);
    
    if (status == NDIS_STATUS_PENDING) {
        NdisWaitEvent(&filterRequest.ReqEvent, 0);
        status = filterRequest.Status;
    }
    
    if (status == NDIS_STATUS_SUCCESS) {
        *value = phyRead.Value;
        DEBUGP(DL_TRACE, "MDIO read successful: phy=0x%x, reg=0x%x, value=0x%x\n", phy_addr, reg_addr, *value);
        return 0;
    } else {
        DEBUGP(DL_WARN, "MDIO read failed: phy=0x%x, reg=0x%x, status=0x%x\n", phy_addr, reg_addr, status);
        
        // Fallback: Try direct register access for I219
        if (context->intel_device.device_type == INTEL_DEVICE_I219) {
            return AvbMdioReadI219Direct(dev, phy_addr, reg_addr, value);
        }
        
        *value = 0;
        return -1;
    }
}

/**
 * @brief Write MDIO register
 * This implementation uses NDIS OID requests to access PHY registers
 */
int 
AvbMdioWrite(
    _In_ device_t *dev, 
    _In_ uint16_t phy_addr, 
    _In_ uint16_t reg_addr, 
    _In_ uint16_t value
)
{
    PAVB_DEVICE_CONTEXT context = (PAVB_DEVICE_CONTEXT)dev->private_data;
    NDIS_STATUS status;
    FILTER_REQUEST filterRequest;
    NDIS_REQUEST_PHY_WRITE phyWrite;
    
    DEBUGP(DL_TRACE, "AvbMdioWrite: phy=0x%x, reg=0x%x, value=0x%x\n", phy_addr, reg_addr, value);
    
    if (context == NULL || context->filter_instance == NULL) {
        return -1;
    }

    // Initialize the filter request
    NdisZeroMemory(&filterRequest, sizeof(FILTER_REQUEST));
    NdisInitializeEvent(&filterRequest.ReqEvent);
    
    // Prepare PHY write structure
    NdisZeroMemory(&phyWrite, sizeof(phyWrite));
    phyWrite.PhyAddress = phy_addr;
    phyWrite.RegisterAddress = reg_addr;
    phyWrite.DeviceAddress = 0; // For clause 22 MDIO
    phyWrite.Value = value;
    
    // Set up OID request to write PHY register
    filterRequest.Request.Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
    filterRequest.Request.Header.Revision = NDIS_OID_REQUEST_REVISION_1;
    filterRequest.Request.Header.Size = sizeof(NDIS_OID_REQUEST);
    filterRequest.Request.RequestType = NdisRequestSetInformation;
    filterRequest.Request.RequestId = 0;
    filterRequest.Request.Oid = OID_GEN_PHY_WRITE;
    filterRequest.Request.DATA.SET_INFORMATION.InformationBuffer = &phyWrite;
    filterRequest.Request.DATA.SET_INFORMATION.InformationBufferLength = sizeof(phyWrite);
    filterRequest.Request.DATA.SET_INFORMATION.BytesRead = 0;
    filterRequest.Request.DATA.SET_INFORMATION.BytesNeeded = 0;
    
    // Send the request
    status = NdisFOidRequest(context->filter_instance->FilterHandle, &filterRequest.Request);
    
    if (status == NDIS_STATUS_PENDING) {
        NdisWaitEvent(&filterRequest.ReqEvent, 0);
        status = filterRequest.Status;
    }
    
    if (status == NDIS_STATUS_SUCCESS) {
        DEBUGP(DL_TRACE, "MDIO write successful: phy=0x%x, reg=0x%x, value=0x%x\n", phy_addr, reg_addr, value);
        return 0;
    } else {
        DEBUGP(DL_WARN, "MDIO write failed: phy=0x%x, reg=0x%x, value=0x%x, status=0x%x\n", phy_addr, reg_addr, value, status);
        
        // Fallback: Try direct register access for I219
        if (context->intel_device.device_type == INTEL_DEVICE_I219) {
            return AvbMdioWriteI219Direct(dev, phy_addr, reg_addr, value);
        }
        
        return -1;
    }
}

/**
 * @brief Direct I219 MDIO read using MMIO registers
 */
int 
AvbMdioReadI219Direct(
    _In_ device_t *dev, 
    _In_ uint16_t phy_addr, 
    _In_ uint16_t reg_addr, 
    _Out_ uint16_t *value
)
{
    uint32_t mdio_ctrl, mdio_data;
    int timeout = 1000;
    int result;
    
    // Set up MDIO control register for read operation
    mdio_ctrl = (1 << 31) |                    // Start bit
                (0 << 30) |                    // Read operation
                (phy_addr << 21) |             // PHY address
                (reg_addr << 16);              // Register address
    
    // Write to MDIO control register
    result = AvbMmioWrite(dev, I219_REG_MDIO_CTRL, mdio_ctrl);
    if (result != 0) {
        return result;
    }
    
    // Wait for operation to complete
    do {
        result = AvbMmioRead(dev, I219_REG_MDIO_CTRL, &mdio_ctrl);
        if (result != 0) {
            return result;
        }
        timeout--;
    } while ((mdio_ctrl & (1 << 31)) && timeout > 0);
    
    if (timeout == 0) {
        DEBUGP(DL_ERROR, "I219 MDIO read timeout\n");
        return -1;
    }
    
    // Read the data
    result = AvbMmioRead(dev, I219_REG_MDIO_DATA, &mdio_data);
    if (result != 0) {
        return result;
    }
    
    *value = (uint16_t)(mdio_data & 0xFFFF);
    return 0;
}

/**
 * @brief Direct I219 MDIO write using MMIO registers
 */
int 
AvbMdioWriteI219Direct(
    _In_ device_t *dev, 
    _In_ uint16_t phy_addr, 
    _In_ uint16_t reg_addr, 
    _In_ uint16_t value
)
{
    uint32_t mdio_ctrl;
    int timeout = 1000;
    int result;
    
    // Write the data first
    result = AvbMmioWrite(dev, I219_REG_MDIO_DATA, (uint32_t)value);
    if (result != 0) {
        return result;
    }
    
    // Set up MDIO control register for write operation
    mdio_ctrl = (1 << 31) |                    // Start bit
                (1 << 30) |                    // Write operation
                (phy_addr << 21) |             // PHY address
                (reg_addr << 16);              // Register address
    
    // Write to MDIO control register
    result = AvbMmioWrite(dev, I219_REG_MDIO_CTRL, mdio_ctrl);
    if (result != 0) {
        return result;
    }
    
    // Wait for operation to complete
    do {
        result = AvbMmioRead(dev, I219_REG_MDIO_CTRL, &mdio_ctrl);
        if (result != 0) {
            return result;
        }
        timeout--;
    } while ((mdio_ctrl & (1 << 31)) && timeout > 0);
    
    if (timeout == 0) {
        DEBUGP(DL_ERROR, "I219 MDIO write timeout\n");
        return -1;
    }
    
    return 0;
}

/**
 * @brief Read hardware timestamp
 * This implementation reads IEEE 1588 timestamp from hardware registers
 */
int 
AvbReadTimestamp(
    _In_ device_t *dev, 
    _Out_ uint64_t *timestamp
)
{
    PAVB_DEVICE_CONTEXT context = (PAVB_DEVICE_CONTEXT)dev->private_data;
    uint32_t ts_low, ts_high;
    int result;
    
    DEBUGP(DL_TRACE, "AvbReadTimestamp\n");
    
    if (context == NULL || timestamp == NULL) {
        return -1;
    }

    // Read timestamp based on device type
    switch (context->intel_device.device_type) {
        case INTEL_DEVICE_I210:
            // I210 IEEE 1588 timestamp registers
            result = AvbMmioRead(dev, 0x0B600, &ts_low);  // SYSTIML
            if (result != 0) return result;
            result = AvbMmioRead(dev, 0x0B604, &ts_high); // SYSTIMH
            if (result != 0) return result;
            break;
            
        case INTEL_DEVICE_I219:
            // I219 IEEE 1588 timestamp registers
            result = AvbMmioRead(dev, I219_REG_1588_TS_LOW, &ts_low);
            if (result != 0) return result;
            result = AvbMmioRead(dev, I219_REG_1588_TS_HIGH, &ts_high);
            if (result != 0) return result;
            break;
            
        case INTEL_DEVICE_I225:
        case INTEL_DEVICE_I226:
            // I225/I226 IEEE 1588 timestamp registers
            result = AvbMmioRead(dev, 0x0B600, &ts_low);  // SYSTIML
            if (result != 0) return result;
            result = AvbMmioRead(dev, 0x0B604, &ts_high); // SYSTIMH
            if (result != 0) return result;
            break;
            
        default:
            DEBUGP(DL_ERROR, "Unsupported device type for timestamp read: %d\n", 
                   context->intel_device.device_type);
            return -1;
    }
    
    // Combine low and high parts
    *timestamp = ((uint64_t)ts_high << 32) | ts_low;
    
    DEBUGP(DL_TRACE, "Timestamp read: 0x%llx\n", *timestamp);
    return 0;
}

/**
 * @brief Check if a filter instance is attached to an Intel adapter
 */
BOOLEAN 
AvbIsFilterIntelAdapter(
    _In_ PMS_FILTER FilterInstance
)
{
    NDIS_STATUS status;
    FILTER_REQUEST filterRequest;
    ULONG vendorId = 0;
    ULONG deviceId = 0;
    
    if (FilterInstance == NULL) {
        return FALSE;
    }
    
    // Initialize the filter request
    NdisZeroMemory(&filterRequest, sizeof(FILTER_REQUEST));
    NdisInitializeEvent(&filterRequest.ReqEvent);
    
    // Query vendor ID
    filterRequest.Request.Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
    filterRequest.Request.Header.Revision = NDIS_OID_REQUEST_REVISION_1;
    filterRequest.Request.Header.Size = sizeof(NDIS_OID_REQUEST);
    filterRequest.Request.RequestType = NdisRequestQueryInformation;
    filterRequest.Request.RequestId = 0;
    filterRequest.Request.Oid = OID_GEN_VENDOR_ID;
    filterRequest.Request.DATA.QUERY_INFORMATION.InformationBuffer = &vendorId;
    filterRequest.Request.DATA.QUERY_INFORMATION.InformationBufferLength = sizeof(vendorId);
    filterRequest.Request.DATA.QUERY_INFORMATION.BytesWritten = 0;
    filterRequest.Request.DATA.QUERY_INFORMATION.BytesNeeded = 0;
    
    // Send the request
    status = NdisFOidRequest(FilterInstance->FilterHandle, &filterRequest.Request);
    
    if (status == NDIS_STATUS_PENDING) {
        NdisWaitEvent(&filterRequest.ReqEvent, 0);
        status = filterRequest.Status;
    }
    
    if (status != NDIS_STATUS_SUCCESS) {
        DEBUGP(DL_WARN, "Failed to query vendor ID: 0x%x\n", status);
        return FALSE;
    }
    
    // Check if it's Intel
    if (vendorId != INTEL_VENDOR_ID) {
        return FALSE;
    }
    
    // Query device ID to determine device type
    NdisResetEvent(&filterRequest.ReqEvent);
    filterRequest.Request.Oid = OID_GEN_DEVICE_TYPE;
    filterRequest.Request.DATA.QUERY_INFORMATION.InformationBuffer = &deviceId;
    filterRequest.Request.DATA.QUERY_INFORMATION.InformationBufferLength = sizeof(deviceId);
    filterRequest.Request.DATA.QUERY_INFORMATION.BytesWritten = 0;
    filterRequest.Request.DATA.QUERY_INFORMATION.BytesNeeded = 0;
    
    status = NdisFOidRequest(FilterInstance->FilterHandle, &filterRequest.Request);
    
    if (status == NDIS_STATUS_PENDING) {
        NdisWaitEvent(&filterRequest.ReqEvent, 0);
        status = filterRequest.Status;
    }
    
    if (status == NDIS_STATUS_SUCCESS && FilterInstance->AvbContext != NULL) {
        PAVB_DEVICE_CONTEXT context = (PAVB_DEVICE_CONTEXT)FilterInstance->AvbContext;
        // Update device information
        context->intel_device.pci_device_id = (UINT16)deviceId;
        context->intel_device.device_type = AvbGetIntelDeviceType((UINT16)deviceId);
        
        DEBUGP(DL_INFO, "Found Intel device: VendorID=0x%x, DeviceID=0x%x, Type=%d\n", 
               vendorId, deviceId, context->intel_device.device_type);
    }
    
    return TRUE;
}

/**
 * @brief Helper function to find Intel filter modules
 */
PMS_FILTER 
AvbFindIntelFilterModule(void)
{
    PMS_FILTER pFilter = NULL;
    PLIST_ENTRY link;
    BOOLEAN bFalse = FALSE;
    
    FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse);
    
    link = FilterModuleList.Flink;
    
    while (link != &FilterModuleList) {
        pFilter = CONTAINING_RECORD(link, MS_FILTER, FilterModuleLink);
        
        // Check if this is an Intel adapter by querying adapter attributes
        if (AvbIsFilterIntelAdapter(pFilter)) {
            FILTER_RELEASE_LOCK(&FilterListLock, bFalse);
            return pFilter;
        }
        
        link = link->Flink;
    }
    
    FILTER_RELEASE_LOCK(&FilterListLock, bFalse);
    return pFilter;
}

/**
 * @brief Check if device is Intel
 */
BOOLEAN 
AvbIsIntelDevice(
    _In_ UINT16 vendor_id, 
    _In_ UINT16 device_id
)
{
    UNREFERENCED_PARAMETER(device_id);
    return (vendor_id == INTEL_VENDOR_ID);
}

/**
 * @brief Get Intel device type from device ID
 */
intel_device_type_t 
AvbGetIntelDeviceType(
    _In_ UINT16 device_id
)
{
    switch (device_id) {
        case 0x1533: // I210
        case 0x1534:
        case 0x1536:
        case 0x1537:
        case 0x1538:
        case 0x157B:
        case 0x157C:
            return INTEL_DEVICE_I210;
            
        case 0x15A0: // I219
        case 0x15A1:
        case 0x15A2:
        case 0x15A3:
        case 0x15B7:
        case 0x15B8:
        case 0x15B9:
        case 0x15BB:
        case 0x15BC:
        case 0x15BD:
        case 0x15BE:
            return INTEL_DEVICE_I219;
            
        case 0x15F2: // I225
        case 0x15F3:
        case 0x15F4:
        case 0x15F5:
        case 0x15F6:
        case 0x15F7:
        case 0x15F8:
        case 0x15F9:
        case 0x15FA:
        case 0x15FB:
        case 0x15FC:
            return INTEL_DEVICE_I225;
            
        case 0x125B: // I226
        case 0x125C:
        case 0x125D:
            return INTEL_DEVICE_I226;
            
        default:
            return INTEL_DEVICE_UNKNOWN;
    }
}
