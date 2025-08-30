/*++
 *
 * The file contains the routines to create a device and handle ioctls
 *
-- */

#include "precomp.h"

#pragma NDIS_INIT_FUNCTION(IntelAvbFilterRegisterDevice)

_IRQL_requires_max_(PASSIVE_LEVEL)
NDIS_STATUS
IntelAvbFilterRegisterDevice(
    VOID
    )
{
    NDIS_STATUS            Status = NDIS_STATUS_SUCCESS;
    UNICODE_STRING         DeviceName;
    UNICODE_STRING         DeviceLinkUnicodeString;
    PDRIVER_DISPATCH       DispatchTable[IRP_MJ_MAXIMUM_FUNCTION+1];
    NDIS_DEVICE_OBJECT_ATTRIBUTES   DeviceAttribute;
    PFILTER_DEVICE_EXTENSION        FilterDeviceExtension;
    PDRIVER_OBJECT                  DriverObject;
   
    DEBUGP(DL_TRACE, "==>IntelAvbFilterRegisterDevice\n");
   
    
    NdisZeroMemory(DispatchTable, (IRP_MJ_MAXIMUM_FUNCTION+1) * sizeof(PDRIVER_DISPATCH));
    
    DispatchTable[IRP_MJ_CREATE] = IntelAvbFilterDispatch;
    DispatchTable[IRP_MJ_CLEANUP] = IntelAvbFilterDispatch;
    DispatchTable[IRP_MJ_CLOSE] = IntelAvbFilterDispatch;
    DispatchTable[IRP_MJ_DEVICE_CONTROL] = IntelAvbFilterDeviceIoControl;
    
    
    NdisInitUnicodeString(&DeviceName, NTDEVICE_STRING);
    NdisInitUnicodeString(&DeviceLinkUnicodeString, LINKNAME_STRING);
    
    //
    // Create a device object and register our dispatch handlers
    //
    NdisZeroMemory(&DeviceAttribute, sizeof(NDIS_DEVICE_OBJECT_ATTRIBUTES));
    
    DeviceAttribute.Header.Type = NDIS_OBJECT_TYPE_DEVICE_OBJECT_ATTRIBUTES;
    DeviceAttribute.Header.Revision = NDIS_DEVICE_OBJECT_ATTRIBUTES_REVISION_1;
    DeviceAttribute.Header.Size = sizeof(NDIS_DEVICE_OBJECT_ATTRIBUTES);
    
    DeviceAttribute.DeviceName = &DeviceName;
    DeviceAttribute.SymbolicName = &DeviceLinkUnicodeString;
    DeviceAttribute.MajorFunctions = &DispatchTable[0];
    DeviceAttribute.ExtensionSize = sizeof(FILTER_DEVICE_EXTENSION);
    
    Status = NdisRegisterDeviceEx(
                FilterDriverHandle,
                &DeviceAttribute,
                &NdisDeviceObject,
                &NdisFilterDeviceHandle
                );
   
   
    if (Status == NDIS_STATUS_SUCCESS)
    {
        FilterDeviceExtension = (PFILTER_DEVICE_EXTENSION) NdisGetDeviceReservedExtension(NdisDeviceObject);
   
        FilterDeviceExtension->Signature = 'FTDR';
        FilterDeviceExtension->Handle = FilterDriverHandle;

        //
        // Workaround NDIS bug
        //
        DriverObject = (PDRIVER_OBJECT)FilterDriverObject;
    }
              
        
    DEBUGP(DL_TRACE, "<==IntelAvbFilterRegisterDevice: %x\n", Status);
        
    return (Status);
        
}

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
IntelAvbFilterDeregisterDevice(
    VOID
    )

{
    if (NdisFilterDeviceHandle != NULL)
    {
        NdisDeregisterDeviceEx(NdisFilterDeviceHandle);
    }

    NdisFilterDeviceHandle = NULL;

}

_Use_decl_annotations_
NTSTATUS
IntelAvbFilterDispatch(
    PDEVICE_OBJECT       DeviceObject,
    PIRP                 Irp
    )
{
    PIO_STACK_LOCATION       IrpStack;
    NTSTATUS                 Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(DeviceObject);

    IrpStack = IoGetCurrentIrpStackLocation(Irp);
    
    switch (IrpStack->MajorFunction)
    {
        case IRP_MJ_CREATE:
            break;

        case IRP_MJ_CLEANUP:
            break;

        case IRP_MJ_CLOSE:
            break;

        default:
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

_Use_decl_annotations_                
NTSTATUS
IntelAvbFilterDeviceIoControl(
    PDEVICE_OBJECT        DeviceObject,
    PIRP                  Irp
    )
{
    PIO_STACK_LOCATION          IrpSp;
    NTSTATUS                    Status = STATUS_SUCCESS;
    PFILTER_DEVICE_EXTENSION    FilterDeviceExtension;
    PUCHAR                      InputBuffer;
    PUCHAR                      OutputBuffer;
    ULONG                       InputBufferLength, OutputBufferLength;
    PLIST_ENTRY                 Link;
    PUCHAR                      pInfo;
    ULONG                       InfoLength = 0;
    PMS_FILTER                  pFilter = NULL;
    BOOLEAN                     bFalse = FALSE;


    UNREFERENCED_PARAMETER(DeviceObject);


    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    if (IrpSp->FileObject == NULL)
    {
        return(STATUS_UNSUCCESSFUL);
    }


    FilterDeviceExtension = (PFILTER_DEVICE_EXTENSION)NdisGetDeviceReservedExtension(DeviceObject);

    ASSERT(FilterDeviceExtension->Signature == 'FTDR');
    
    Irp->IoStatus.Information = 0;

    switch (IrpSp->Parameters.DeviceIoControl.IoControlCode)
    {

        case IOCTL_FILTER_RESTART_ALL:
            break;

        case IOCTL_FILTER_RESTART_ONE_INSTANCE:
            InputBuffer = OutputBuffer = (PUCHAR)Irp->AssociatedIrp.SystemBuffer;
            InputBufferLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;

            pFilter = filterFindFilterModule (InputBuffer, InputBufferLength);

            if (pFilter == NULL)
            {
                
                break;
            }

            NdisFRestartFilter(pFilter->FilterHandle);

            break;

        case IOCTL_FILTER_ENUERATE_ALL_INSTANCES:
            
            InputBuffer = OutputBuffer = (PUCHAR)Irp->AssociatedIrp.SystemBuffer;
            InputBufferLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
            OutputBufferLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
            
            
            pInfo = OutputBuffer;
            
            FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse);
            
            Link = FilterModuleList.Flink;
            
            while (Link != &FilterModuleList)
            {
                pFilter = CONTAINING_RECORD(Link, MS_FILTER, FilterModuleLink);

                
                InfoLength += (pFilter->FilterModuleName.Length + sizeof(USHORT));
                        
                if (InfoLength <= OutputBufferLength)
                {
                    *(PUSHORT)pInfo = pFilter->FilterModuleName.Length;
                    NdisMoveMemory(pInfo + sizeof(USHORT), 
                                   (PUCHAR)(pFilter->FilterModuleName.Buffer),
                                   pFilter->FilterModuleName.Length);
                            
                    pInfo += (pFilter->FilterModuleName.Length + sizeof(USHORT));
                }
                
                Link = Link->Flink;
            }
               
            FILTER_RELEASE_LOCK(&FilterListLock, bFalse);
            if (InfoLength <= OutputBufferLength)
            {
       
                Status = NDIS_STATUS_SUCCESS;
            }
            //
            // Buffer is small
            //
            else
            {
                Status = STATUS_BUFFER_TOO_SMALL;
            }
            break;

        // AVB IOCTLs - Handle through AVB integration layer
        case IOCTL_AVB_INIT_DEVICE:
        case IOCTL_AVB_GET_DEVICE_INFO:
        case IOCTL_AVB_READ_REGISTER:
        case IOCTL_AVB_WRITE_REGISTER:
        case IOCTL_AVB_GET_TIMESTAMP:
        case IOCTL_AVB_SET_TIMESTAMP:
        case IOCTL_AVB_SETUP_TAS:
        case IOCTL_AVB_SETUP_FP:
        case IOCTL_AVB_SETUP_PTM:
        case IOCTL_AVB_MDIO_READ:
        case IOCTL_AVB_MDIO_WRITE:
        case IOCTL_AVB_GET_HW_STATE:
        {
            DEBUGP(DL_TRACE, "IntelAvbFilterDeviceIoControl: AVB IOCTL=0x%x\n", 
                   IrpSp->Parameters.DeviceIoControl.IoControlCode);
            
            // Try to find an Intel filter instance with initialized AVB context
            pFilter = AvbFindIntelFilterModule();
            DEBUGP(DL_INFO, "?? IOCTL Handler: Found filter=%p\n", pFilter);
            
            if (pFilter) {
                DEBUGP(DL_INFO, "   - Filter Name: %wZ\n", &pFilter->MiniportFriendlyName);
                if (pFilter->AvbContext) {
                    PAVB_DEVICE_CONTEXT ctx = (PAVB_DEVICE_CONTEXT)pFilter->AvbContext;
                    DEBUGP(DL_INFO, "   - Context: VID=0x%04X DID=0x%04X state=%s\n",
                           ctx->intel_device.pci_vendor_id, ctx->intel_device.pci_device_id,
                           AvbHwStateName(ctx->hw_state));
                }
            }

            // If not found, iterate all filter instances and lazily initialize until one succeeds
            if (pFilter == NULL)
            {
                DEBUGP(DL_INFO, "?? No initialized Intel filter found, attempting lazy initialization\n");
                
                FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse);
                Link = FilterModuleList.Flink;
                
                while (Link != &FilterModuleList)
                {
                    PMS_FILTER cand = CONTAINING_RECORD(Link, MS_FILTER, FilterModuleLink);
                    Link = Link->Flink; // Move to next before releasing lock
                    
                    FILTER_RELEASE_LOCK(&FilterListLock, bFalse);
                    
                    DEBUGP(DL_INFO, "?? Checking candidate filter: %wZ\n", &cand->MiniportFriendlyName);
                    
                    // Check if this filter might be an Intel adapter
                    if (cand->AvbContext == NULL)
                    {
                        USHORT ven = 0, dev = 0;
                        if (AvbIsSupportedIntelController(cand, &ven, &dev))
                        {
                            DEBUGP(DL_INFO, "? Found uninitialized Intel adapter %wZ (VID=0x%04X DID=0x%04X), initializing AVB context\n",
                                   &cand->MiniportFriendlyName, ven, dev);
                            
                            NTSTATUS initSt = AvbInitializeDevice(cand, (PAVB_DEVICE_CONTEXT*)&cand->AvbContext);
                            if (NT_SUCCESS(initSt) && cand->AvbContext != NULL)
                            {
                                DEBUGP(DL_INFO, "? Successfully initialized AVB context for %wZ\n", 
                                       &cand->MiniportFriendlyName);
                                pFilter = cand;
                                break;
                            }
                            else
                            {
                                DEBUGP(DL_WARN, "? Failed to initialize AVB context for %wZ: 0x%x\n", 
                                       &cand->MiniportFriendlyName, initSt);
                            }
                        } else {
                            DEBUGP(DL_INFO, "   - Not supported Intel controller\n");
                        }
                    } else {
                        DEBUGP(DL_INFO, "   - Already has AVB context\n");
                    }
                    
                    FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse);
                }
                
                FILTER_RELEASE_LOCK(&FilterListLock, bFalse);
            }

            if (pFilter != NULL && pFilter->AvbContext != NULL) {
                DEBUGP(DL_TRACE, "? Using filter %p for IOCTL\n", pFilter);
                Status = AvbHandleDeviceIoControl((PAVB_DEVICE_CONTEXT)pFilter->AvbContext, Irp);
                InfoLength = (ULONG)Irp->IoStatus.Information;
                DEBUGP(DL_TRACE, "?? IOCTL processed: Status=0x%x, InfoLength=%lu\n", 
                       Status, InfoLength);
            } else {
                DEBUGP(DL_ERROR, "? No Intel filter found or AVB context not initialized\n");
                Status = STATUS_DEVICE_NOT_READY;
            }
            break;
        }

        case IOCTL_AVB_ENUM_ADAPTERS:
        {
            DEBUGP(DL_TRACE, "IntelAvbFilterDeviceIoControl: ENUM_ADAPTERS (multi-adapter mode)\n");
            
            // ENUM_ADAPTERS needs special handling for multi-adapter support
            // Count ALL Intel adapters, not just find one
            
            InputBuffer = OutputBuffer = (PUCHAR)Irp->AssociatedIrp.SystemBuffer;
            InputBufferLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
            OutputBufferLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
            
            if (OutputBufferLength < sizeof(AVB_ENUM_REQUEST)) {
                DEBUGP(DL_ERROR, "ENUM_ADAPTERS: Buffer too small (%lu < %lu)\n", 
                       OutputBufferLength, (ULONG)sizeof(AVB_ENUM_REQUEST));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            
            PAVB_ENUM_REQUEST req = (PAVB_ENUM_REQUEST)OutputBuffer;
            ULONG requestedIndex = req->index; // Input: which adapter index to query
            ULONG adapterCount = 0;
            BOOLEAN foundRequestedAdapter = FALSE;
            
            // Initialize response
            req->count = 0;
            req->vendor_id = 0;
            req->device_id = 0;
            req->capabilities = 0;
            req->status = NDIS_STATUS_SUCCESS;
            
            DEBUGP(DL_INFO, "ENUM_ADAPTERS: Scanning for adapters, requested index=%lu\n", requestedIndex);
            
            // Scan ALL filter instances to count Intel adapters
            FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse);
            Link = FilterModuleList.Flink;
            
            while (Link != &FilterModuleList)
            {
                PMS_FILTER cand = CONTAINING_RECORD(Link, MS_FILTER, FilterModuleLink);
                Link = Link->Flink; // Move to next
                
                FILTER_RELEASE_LOCK(&FilterListLock, bFalse);
                
                // Check if this is a supported Intel adapter
                USHORT ven = 0, dev = 0;
                if (AvbIsSupportedIntelController(cand, &ven, &dev))
                {
                    DEBUGP(DL_INFO, "ENUM_ADAPTERS: Found Intel adapter #%lu: %wZ (VID=0x%04X, DID=0x%04X)\n",
                           adapterCount, &cand->MiniportFriendlyName, ven, dev);
                    
                    // If this is the requested adapter index, populate the response
                    if (adapterCount == requestedIndex)
                    {
                        req->vendor_id = ven;
                        req->device_id = dev;
                        
                        // Initialize AVB context if needed
                        if (cand->AvbContext == NULL)
                        {
                            DEBUGP(DL_INFO, "ENUM_ADAPTERS: Initializing AVB context for requested adapter #%lu\n", adapterCount);
                            NTSTATUS initSt = AvbInitializeDevice(cand, (PAVB_DEVICE_CONTEXT*)&cand->AvbContext);
                            if (NT_SUCCESS(initSt) && cand->AvbContext != NULL)
                            {
                                DEBUGP(DL_INFO, "ENUM_ADAPTERS: Successfully initialized AVB context for requested adapter #%lu\n", 
                                       adapterCount);
                            }
                            else
                            {
                                DEBUGP(DL_WARN, "ENUM_ADAPTERS: Failed to initialize AVB context for requested adapter #%lu: 0x%x\n", 
                                       adapterCount, initSt);
                                // Still count it but with limited capabilities
                                req->capabilities = 0;
                            }
                        }
                        
                        // Get capabilities from Intel library
                        if (cand->AvbContext != NULL)
                        {
                            PAVB_DEVICE_CONTEXT ctx = (PAVB_DEVICE_CONTEXT)cand->AvbContext;
                            
                            // Call Intel library initialization to get capabilities
                            int result = intel_init(&ctx->intel_device);
                            ctx->hw_access_enabled = (result == 0);
                            
                            if (result == 0)
                            {
                                req->capabilities = intel_get_capabilities(&ctx->intel_device);
                                DEBUGP(DL_INFO, "ENUM_ADAPTERS: Adapter #%lu capabilities=0x%08X\n", 
                                       adapterCount, req->capabilities);
                            }
                            else
                            {
                                DEBUGP(DL_WARN, "ENUM_ADAPTERS: Intel init failed for adapter #%lu\n", adapterCount);
                                req->capabilities = 0;
                            }
                        }
                        
                        foundRequestedAdapter = TRUE;
                    }
                    
                    adapterCount++;
                }
                
                FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse);
            }
            
            FILTER_RELEASE_LOCK(&FilterListLock, bFalse);
            
            // Set total count
            req->count = adapterCount;
            
            if (adapterCount == 0)
            {
                DEBUGP(DL_WARN, "ENUM_ADAPTERS: No Intel adapters found with active bindings\n");
                Status = STATUS_NO_SUCH_DEVICE;
            }
            else if (requestedIndex >= adapterCount)
            {
                DEBUGP(DL_WARN, "ENUM_ADAPTERS: Requested index %lu >= adapter count %lu\n", 
                       requestedIndex, adapterCount);
                Status = STATUS_INVALID_PARAMETER;
            }
            else
            {
                DEBUGP(DL_INFO, "ENUM_ADAPTERS: Success - Total adapters=%lu, returned adapter #%lu (VID=0x%04X, DID=0x%04X, caps=0x%08X)\n",
                       adapterCount, requestedIndex, req->vendor_id, req->device_id, req->capabilities);
                Status = NDIS_STATUS_SUCCESS;
                InfoLength = sizeof(AVB_ENUM_REQUEST);
            }
            
            break;
        }
             
        default:
            break;
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = InfoLength;

    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
            

}


_IRQL_requires_max_(DISPATCH_LEVEL)
PMS_FILTER
filterFindFilterModule(
    _In_reads_bytes_(BufferLength)
         PUCHAR                   Buffer,
    _In_ ULONG                    BufferLength
    )
{

   PMS_FILTER              pFilter;
   PLIST_ENTRY             Link;
   BOOLEAN                  bFalse = FALSE;
   
   FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse);
               
   Link = FilterModuleList.Flink;
               
   while (Link != &FilterModuleList)
   {
       pFilter = CONTAINING_RECORD(Link, MS_FILTER, FilterModuleLink);

       if (BufferLength >= pFilter->FilterModuleName.Length)
       {
           if (NdisEqualMemory(Buffer, pFilter->FilterModuleName.Buffer, pFilter->FilterModuleName.Length))
           {
               FILTER_RELEASE_LOCK(&FilterListLock, bFalse);
               return pFilter;
           }
       }
           
       Link = Link->Flink;
   }
   
   FILTER_RELEASE_LOCK(&FilterListLock, bFalse);
   return NULL;
}













































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































