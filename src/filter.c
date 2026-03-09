/*++

Module Name:

    Filter.c

Abstract:

    Sample NDIS Lightweight filter driver

--*/

#include "precomp.h"

#ifndef FILTER_SERVICE_NAME
#define FILTER_SERVICE_NAME      L"IntelAvbFilter"
#endif
#ifndef FILTER_FRIENDLY_NAME
#define FILTER_FRIENDLY_NAME     L"Intel AVB/TSN NDIS Filter Driver" /* Must match INF DisplayName */
#endif

#define __FILENUMBER    'PNPF'

// This directive puts the DriverEntry function into the INIT segment of the
// driver.  To conserve memory, the code will be discarded when the driver's
// DriverEntry function returns.  You can declare other functions used only
// during initialization here.
#pragma NDIS_INIT_FUNCTION(DriverEntry)

//
// Global variables
//
NDIS_HANDLE         FilterDriverHandle; // NDIS handle for filter driver
NDIS_HANDLE         FilterDriverObject;
NDIS_HANDLE         NdisFilterDeviceHandle = NULL;
PDEVICE_OBJECT      NdisDeviceObject = NULL;

FILTER_LOCK         FilterListLock;
LIST_ENTRY          FilterModuleList;

/*
 * ETW event logging infrastructure
 * Provider GUID: {5DC1CA46-B07F-4A4F-9D02-1A02E7EC24E4}
 * Manifest:      src/IntelAvbFilter.man (register with wevtutil im at install time)
 * Implements: #65  (REQ-F-EVENT-LOG-001: Windows Event Log Integration)
 * Closes:     #269 (TEST-EVENT-LOG-001)
 */
REGHANDLE g_EtwHandle = 0;  /* registered in DriverEntry, unregistered in FilterUnload */

static const GUID g_IntelAvbFilterProviderGuid = {
    0x5dc1ca46, 0xb07f, 0x4a4f, {0x9d, 0x02, 0x1a, 0x02, 0xe7, 0xec, 0x24, 0xe4}
};

/*
 * EVENT_DESCRIPTOR: {Id, Version, Channel, Level, Opcode, Task, Keyword}
 * Channel 0x09 = Application event log (importChannel name="Application")
 * Levels: 1=Critical, 2=Error, 3=Warning, 4=Informational
 */
static const EVENT_DESCRIPTOR g_EvtDriverInit      = {   1, 0, 0x09, 4, 0, 0, 0 };
static const EVENT_DESCRIPTOR g_EvtIoctlError      = { 100, 0, 0x09, 2, 0, 0, 0 };
static const EVENT_DESCRIPTOR g_EvtPhcForceSetWarn = { 200, 0, 0x09, 3, 0, 0, 0 };
static const EVENT_DESCRIPTOR g_EvtHardwareFault   = { 300, 0, 0x09, 1, 0, 0, 0 };

NDIS_FILTER_PARTIAL_CHARACTERISTICS DefaultChars = {
{ 0, 0, 0},
      0,
      FilterSendNetBufferLists,
      FilterSendNetBufferListsComplete,
      NULL,
      FilterReceiveNetBufferLists,
      FilterReturnNetBufferLists
};

// Forward declarations for local helper functions used in FilterAttach
static BOOLEAN UnicodeStringContainsInsensitive(PCUNICODE_STRING Str, PCWSTR Sub);
static BOOLEAN IsUnsupportedTeamOrVirtualAdapter(PNDIS_FILTER_ATTACH_PARAMETERS AttachParameters);


// AvbIsSupportedIntelController is defined in avb_bar0_discovery.c
extern BOOLEAN AvbIsSupportedIntelController(PMS_FILTER pFilter, PUSHORT pVendorId, PUSHORT pDeviceId);

/* Helper: case-insensitive substring search for UNICODE_STRING */
static
BOOLEAN
UnicodeStringContainsInsensitive(
    _In_ PCUNICODE_STRING Str,
    _In_ PCWSTR Sub
)
{
    if (!Str || !Str->Buffer || Str->Length == 0 || !Sub) return FALSE;
    USHORT lenChars = (USHORT)(Str->Length / sizeof(WCHAR));
    USHORT nlen = 0; while (Sub[nlen] != L'\0') nlen++;
    if (nlen == 0 || nlen > lenChars) return FALSE;
    for (USHORT i = 0; i + nlen <= lenChars; ++i) {
        BOOLEAN match = TRUE;
        for (USHORT j = 0; j < nlen; ++j) {
            WCHAR c1 = Str->Buffer[i + j];
            WCHAR c2 = Sub[j];
            if (c1 >= L'a' && c1 <= L'z') c1 = (WCHAR)(c1 - L'a' + L'A');
            if (c2 >= L'a' && c2 <= L'z') c2 = (WCHAR)(c2 - L'a' + L'A');
            if (c1 != c2) { match = FALSE; break; }
        }
        if (match) return TRUE;
    }
    return FALSE;
}

/* Helper: decide if the miniport instance is a TNIC/virtual adapter we do not support */
static
BOOLEAN
IsUnsupportedTeamOrVirtualAdapter(
    _In_ PNDIS_FILTER_ATTACH_PARAMETERS AttachParameters
)
{
    PCUNICODE_STRING name = AttachParameters->BaseMiniportInstanceName;
    if (!name || !name->Buffer) return FALSE;
    if (UnicodeStringContainsInsensitive(name, L"Multiplexor")) return TRUE;   // Microsoft Network Adapter Multiplexor Driver (LBFO)
    if (UnicodeStringContainsInsensitive(name, L"Team")) return TRUE;          // Teaming TNIC
    if (UnicodeStringContainsInsensitive(name, L"Bridge")) return TRUE;        // Bridge/ICS
    if (UnicodeStringContainsInsensitive(name, L"vEthernet")) return TRUE;     // Hyper-V vNIC
    if (UnicodeStringContainsInsensitive(name, L"Hyper-V")) return TRUE;       // Hyper-V naming
    if (UnicodeStringContainsInsensitive(name, L"Virtual")) return TRUE;       // Generic virtual NICs
    return FALSE;
}

_Use_decl_annotations_
NTSTATUS
DriverEntry(
    PDRIVER_OBJECT      DriverObject,
    PUNICODE_STRING     RegistryPath
    )
/*++

Routine Description:

    First entry point to be called, when this driver is loaded.
    Register with NDIS as a filter driver and create a device
    for communication with user-mode.

Arguments:

    DriverObject - pointer to the system's driver object structure
                   for this driver

    RegistryPath - system's registry path for this driver

Return Value:

    STATUS_SUCCESS if all initialization is successful, STATUS_XXX
    error code if not.

--*/
{

    NDIS_STATUS Status;
    NDIS_FILTER_DRIVER_CHARACTERISTICS      FChars;
    NDIS_STRING ServiceName  = RTL_CONSTANT_STRING(FILTER_SERVICE_NAME);
    NDIS_STRING UniqueName   = RTL_CONSTANT_STRING(FILTER_UNIQUE_NAME);
    NDIS_STRING FriendlyName = RTL_CONSTANT_STRING(FILTER_FRIENDLY_NAME);
    BOOLEAN bFalse = FALSE;

    /* Implements: #95 (REQ-NF-DEBUG-REG-001: Registry-Based Debug Settings)
     * Read DebugLevel from HKLM\...\IntelAvbFilter\Parameters before first trace.
     * Safe read-only call at PASSIVE_LEVEL — no spinlock, no BSOD risk.
     */
    FilterReadDebugSettings(RegistryPath);

    DEBUGP(DL_TRACE, "===>DriverEntry...\n");

    FilterDriverObject = DriverObject;

    do
    {
        // Initialize spin locks first
        FILTER_INIT_LOCK(&FilterListLock);
        InitializeListHead(&FilterModuleList);
        FilterDriverHandle = NULL;

        // Initialize global AVB adapter list lock (before any FilterAttach can run)
        NdisAllocateSpinLock(&g_AvbContextListLock);

        // Zero the characteristics structure
        NdisZeroMemory(&FChars, sizeof(NDIS_FILTER_DRIVER_CHARACTERISTICS));
        FChars.Header.Type = NDIS_OBJECT_TYPE_FILTER_DRIVER_CHARACTERISTICS;
        FChars.Header.Revision = NDIS_FILTER_CHARACTERISTICS_REVISION_1;
        // Use sizeof for the actual structure size - NDIS will validate
        FChars.Header.Size = sizeof(NDIS_FILTER_DRIVER_CHARACTERISTICS);
        
        // Use system-defined NDIS version
        FChars.MajorNdisVersion = FILTER_MAJOR_NDIS_VERSION;
        FChars.MinorNdisVersion = FILTER_MINOR_NDIS_VERSION;
        FChars.MajorDriverVersion = 1;
        FChars.MinorDriverVersion = 0;
        FChars.Flags = 0;

        // Names
        FChars.FriendlyName = FriendlyName;
        FChars.UniqueName = UniqueName;
        FChars.ServiceName = ServiceName;
        
        // Set up all the required filter entry points
        FChars.SetOptionsHandler = FilterRegisterOptions;
        FChars.AttachHandler = FilterAttach;
        FChars.DetachHandler = FilterDetach;
        FChars.RestartHandler = FilterRestart;
        FChars.PauseHandler = FilterPause;
        FChars.SetFilterModuleOptionsHandler = FilterSetModuleOptions;
        FChars.OidRequestHandler = FilterOidRequest;
        FChars.OidRequestCompleteHandler = FilterOidRequestComplete;
        FChars.CancelOidRequestHandler = FilterCancelOidRequest;

        // Network Buffer List handlers
        FChars.SendNetBufferListsHandler = FilterSendNetBufferLists;
        FChars.ReturnNetBufferListsHandler = FilterReturnNetBufferLists;
        FChars.SendNetBufferListsCompleteHandler = FilterSendNetBufferListsComplete;
        FChars.ReceiveNetBufferListsHandler = FilterReceiveNetBufferLists;
        
        // PnP and Status handlers
        FChars.DevicePnPEventNotifyHandler = FilterDevicePnPEventNotify;
        FChars.NetPnPEventHandler = FilterNetPnPEvent;
        FChars.StatusHandler = FilterStatus;
        FChars.CancelSendNetBufferListsHandler = FilterCancelSendNetBufferLists;

        // Set driver unload
        DriverObject->DriverUnload = FilterUnload;

        DEBUGP(DL_TRACE, "DriverEntry: Attempting NDIS %u.%u filter registration\n", 
               FChars.MajorNdisVersion, FChars.MinorNdisVersion);
        DEBUGP(DL_TRACE, "DriverEntry: Header Size=%u, Revision=%u, Type=0x%08x\n", 
               FChars.Header.Size, FChars.Header.Revision, FChars.Header.Type);
        DEBUGP(DL_TRACE, "DriverEntry: DriverObject=%p, FilterDriverObject=%p\n", 
               DriverObject, FilterDriverObject);
        DEBUGP(DL_TRACE, "DriverEntry: ServiceName=%wZ\n", &ServiceName);
        DEBUGP(DL_TRACE, "DriverEntry: UniqueName=%wZ\n", &UniqueName);
        DEBUGP(DL_TRACE, "DriverEntry: FriendlyName=%wZ\n", &FriendlyName);
        DEBUGP(DL_TRACE, "DriverEntry: Handlers - SetOptions=%p, Attach=%p, Detach=%p\n",
               FChars.SetOptionsHandler, FChars.AttachHandler, FChars.DetachHandler);
        DEBUGP(DL_TRACE, "DriverEntry: Handlers - Restart=%p, Pause=%p, SetModuleOpts=%p\n",
               FChars.RestartHandler, FChars.PauseHandler, FChars.SetFilterModuleOptionsHandler);
        DEBUGP(DL_TRACE, "DriverEntry: Handlers - OidRequest=%p, SendNBL=%p, ReceiveNBL=%p\n",
               FChars.OidRequestHandler, FChars.SendNetBufferListsHandler, FChars.ReceiveNetBufferListsHandler);
        
        // First attempt with NDIS 6.0 revision 1
        Status = NdisFRegisterFilterDriver(DriverObject,
                                           (NDIS_HANDLE)FilterDriverObject,
                                           &FChars,
                                           &FilterDriverHandle);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            DEBUGP(DL_TRACE, "NdisFRegisterFilterDriver failed (NDIS %u.%u): 0x%08X\n", 
                   FChars.MajorNdisVersion, FChars.MinorNdisVersion, Status);
            DEBUGP(DL_TRACE, "  Possible causes:\n");
            DEBUGP(DL_TRACE, "  - Invalid header structure (Size=%u, Revision=%u)\n", 
                   FChars.Header.Size, FChars.Header.Revision);
            DEBUGP(DL_TRACE, "  - Missing required handlers\n");
            DEBUGP(DL_TRACE, "  - NDIS version incompatibility\n");
            DEBUGP(DL_TRACE, "  - Duplicate driver registration\n");
                   
            // No fallback needed since we already started with the most compatible version
            FILTER_FREE_LOCK(&FilterListLock);
            break;
        }

        DEBUGP(DL_TRACE, "NDIS filter driver registered successfully\n");

        // Register device for user-mode communication
        Status = IntelAvbFilterRegisterDevice();
        if (Status != NDIS_STATUS_SUCCESS)
        {
            DEBUGP(DL_TRACE, "IntelAvbFilterRegisterDevice failed: 0x%x\n", Status);
            NdisFDeregisterFilterDriver(FilterDriverHandle);
            FILTER_FREE_LOCK(&FilterListLock);
            break;
        }

        DEBUGP(DL_TRACE, "User-mode device interface registered successfully\n");

        /*
         * Register ETW provider and emit Event ID 1 (driver initialization).
         * The provider manifest (src/IntelAvbFilter.man) must be registered at
         * install time via:
         *   wevtutil im IntelAvbFilter.man /mf:<sys-path> /rf:<sys-path>
         *
         * Implements: #65 (REQ-F-EVENT-LOG-001) — Closes: #269 (TEST-EVENT-LOG-001)
         */
        {
            NTSTATUS etw_status = EtwRegister(
                &g_IntelAvbFilterProviderGuid,
                NULL,   /* no enable callback */
                NULL,   /* no callback context */
                &g_EtwHandle);
            if (NT_SUCCESS(etw_status)) {
                /* Event ID 1: driver initialization — written to Application log */
                EtwWrite(g_EtwHandle, &g_EvtDriverInit, NULL, 0, NULL);
                DEBUGP(DL_TRACE, "ETW provider registered; Event ID 1 emitted.\n");
            } else {
                DEBUGP(DL_TRACE, "EtwRegister failed: 0x%08x (manifest may not be installed)\n",
                       etw_status);
                /* Non-fatal: driver continues without ETW. Test will soft-fail. */
            }
        }

    }
    while(bFalse);

    DEBUGP(DL_TRACE, "<===DriverEntry, Status = 0x%08x\n", Status);
    return Status;
}

_Use_decl_annotations_
NDIS_STATUS
FilterRegisterOptions(
    NDIS_HANDLE  NdisFilterDriverHandle,
    NDIS_HANDLE  FilterDriverContext
    )
/*++

Routine Description:

    Register optional handlers with NDIS.  This sample does not happen to
    have any optional handlers to register, so this routine does nothing
    and could simply have been omitted.  However, for illustrative purposes,
    it is presented here.

Arguments:

    NdisFilterDriverHandle - pointer the driver handle received from
                             NdisFRegisterFilterDriver

    FilterDriverContext    - pointer to our context passed into
                             NdisFRegisterFilterDriver

Return Value:

    NDIS_STATUS_SUCCESS

--*/
{
    UNREFERENCED_PARAMETER(NdisFilterDriverHandle);
    
    DEBUGP(DL_TRACE, "===>FilterRegisterOptions\n");

    // Validate the context matches what we passed in
    if (FilterDriverContext != (NDIS_HANDLE)FilterDriverObject)
    {
        DEBUGP(DL_TRACE, "FilterRegisterOptions: Invalid FilterDriverContext\n");
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    DEBUGP(DL_TRACE, "<===FilterRegisterOptions\n");

    return NDIS_STATUS_SUCCESS;
}


_Use_decl_annotations_
NDIS_STATUS
FilterAttach(
    NDIS_HANDLE                     NdisFilterHandle,
    NDIS_HANDLE                     FilterDriverContext,
    PNDIS_FILTER_ATTACH_PARAMETERS  AttachParameters
    )
/*++

Routine Description:

    Filter attach routine.
    Create filter's context, allocate NetBufferLists and NetBuffer pools and any
    other resources, and read configuration if needed.

Arguments:

    NdisFilterHandle - Specify a handle identifying this instance of the filter. FilterAttach
                       should save this handle. It is a required  parameter in subsequent calls
                       to NdisFxxx functions.
    FilterDriverContext - Filter driver context passed to NdisFRegisterFilterDriver.

    AttachParameters - attach parameters

Return Value:

    NDIS_STATUS_SUCCESS: FilterAttach successfully allocated and initialize data structures
                         for this filter instance.
    NDIS_STATUS_RESOURCES: FilterAttach failed due to insufficient resources.
    NDIS_STATUS_FAILURE: FilterAttach could not set up this instance of this filter and it has called
                         NdisWriteErrorLogEntry with parameters specifying the reason for failure.

N.B.:  FILTER can use NdisRegisterDeviceEx to create a device, so the upper
    layer can send Irps to the filter.

--*/
{
    PMS_FILTER              pFilter = NULL;
    NDIS_STATUS             Status = NDIS_STATUS_SUCCESS;
    NDIS_FILTER_ATTRIBUTES  FilterAttributes;
    ULONG                   Size;
    BOOLEAN               bFalse = FALSE;


    DEBUGP(DL_TRACE, "===>FilterAttach: NdisFilterHandle %p\n", NdisFilterHandle);

    do
    {
        ASSERT(FilterDriverContext == (NDIS_HANDLE)FilterDriverObject);
        if (FilterDriverContext != (NDIS_HANDLE)FilterDriverObject)
        {
            DEBUGP(DL_TRACE, "FilterAttach: Invalid FilterDriverContext: %p (expected %p)\n", 
                   FilterDriverContext, FilterDriverObject);
            Status = NDIS_STATUS_INVALID_PARAMETER;
            break;
        }

        // Log all adapter attach attempts with detailed info
        if (AttachParameters && AttachParameters->BaseMiniportInstanceName)
        {
            DEBUGP(DL_TRACE, "FilterAttach: *** ADAPTER ATTACHMENT REQUEST ***\n");
            DEBUGP(DL_TRACE, "  Adapter Name: %wZ\n", AttachParameters->BaseMiniportInstanceName);
            DEBUGP(DL_TRACE, "  Media Type: %u, IfIndex: %u\n", 
                   AttachParameters->MiniportMediaType, AttachParameters->BaseMiniportIfIndex);
            DEBUGP(DL_TRACE, "  FilterModuleGUID: %wZ\n", AttachParameters->FilterModuleGuidName);
            DEBUGP(DL_TRACE, "  BaseMiniportName: %wZ\n", AttachParameters->BaseMiniportName);
        }
        else
        {
            DEBUGP(DL_TRACE, "FilterAttach: NULL AttachParameters or BaseMiniportInstanceName!\n");
        }

        // Reject virtual/teamed/bridge adapters outright
        if (IsUnsupportedTeamOrVirtualAdapter(AttachParameters))
        {
            DEBUGP(DL_TRACE, "FilterAttach: Rejecting virtual/teamed adapter: %wZ\n", AttachParameters->BaseMiniportInstanceName);
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;
        }

        // Verify the media type is supported.
        if ((AttachParameters->MiniportMediaType != NdisMedium802_3)
                && (AttachParameters->MiniportMediaType != NdisMediumWan)
                && (AttachParameters->MiniportMediaType != NdisMediumWirelessWan))
        {
           DEBUGP(DL_TRACE, "FilterAttach: Unsupported media type %u for adapter %wZ\n", 
                  AttachParameters->MiniportMediaType, 
                  AttachParameters->BaseMiniportInstanceName);

           Status = NDIS_STATUS_NOT_SUPPORTED;
           break;
        }

        Size = sizeof(MS_FILTER) +
               AttachParameters->FilterModuleGuidName->Length +
               AttachParameters->BaseMiniportInstanceName->Length +
               AttachParameters->BaseMiniportName->Length;

        pFilter = (PMS_FILTER)FILTER_ALLOC_MEM(NdisFilterHandle, Size);
        if (pFilter == NULL)
        {
            DEBUGP(DL_TRACE, "FilterAttach: Failed to allocate context structure for %wZ\n", 
                   AttachParameters->BaseMiniportInstanceName);
            Status = NDIS_STATUS_RESOURCES;
            break;
        }

        NdisZeroMemory(pFilter, sizeof(MS_FILTER));

        pFilter->FilterModuleName.Length = pFilter->FilterModuleName.MaximumLength = AttachParameters->FilterModuleGuidName->Length;
        pFilter->FilterModuleName.Buffer = (PWSTR)((PUCHAR)pFilter + sizeof(MS_FILTER));
        NdisMoveMemory(pFilter->FilterModuleName.Buffer,
                        AttachParameters->FilterModuleGuidName->Buffer,
                        pFilter->FilterModuleName.Length);



        pFilter->MiniportFriendlyName.Length = pFilter->MiniportFriendlyName.MaximumLength = AttachParameters->BaseMiniportInstanceName->Length;
        pFilter->MiniportFriendlyName.Buffer = (PWSTR)((PUCHAR)pFilter->FilterModuleName.Buffer + pFilter->FilterModuleName.Length);
        NdisMoveMemory(pFilter->MiniportFriendlyName.Buffer,
                        AttachParameters->BaseMiniportInstanceName->Buffer,
                        pFilter->MiniportFriendlyName.Length);


        pFilter->MiniportName.Length = pFilter->MiniportName.MaximumLength = AttachParameters->BaseMiniportName->Length;
        pFilter->MiniportName.Buffer = (PWSTR)((PUCHAR)pFilter->MiniportFriendlyName.Buffer +
                                                   pFilter->MiniportFriendlyName.Length);
        NdisMoveMemory(pFilter->MiniportName.Buffer,
                        AttachParameters->BaseMiniportName->Buffer,
                        pFilter->MiniportName.Length);

        pFilter->MiniportIfIndex = AttachParameters->BaseMiniportIfIndex;
        //
        // The filter should initialize TrackReceives and TrackSends properly. For this
        // driver, since its default characteristic has both a send and a receive handler,
        // these fields are initialized to TRUE.
        //
        pFilter->TrackReceives = TRUE;
        pFilter->TrackSends = TRUE;
        pFilter->FilterHandle = NdisFilterHandle;


        NdisZeroMemory(&FilterAttributes, sizeof(NDIS_FILTER_ATTRIBUTES));
        FilterAttributes.Header.Revision = NDIS_FILTER_ATTRIBUTES_REVISION_1;
        FilterAttributes.Header.Size = sizeof(NDIS_FILTER_ATTRIBUTES);
        FilterAttributes.Header.Type = NDIS_OBJECT_TYPE_FILTER_ATTRIBUTES;
        FilterAttributes.Flags = 0;

        NDIS_DECLARE_FILTER_MODULE_CONTEXT(MS_FILTER);
        Status = NdisFSetAttributes(NdisFilterHandle,
                                    pFilter,
                                    &FilterAttributes);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            DEBUGP(DL_TRACE, "FilterAttach: Failed to set attributes for %wZ, Status=0x%x\n", 
                   &pFilter->MiniportFriendlyName, Status);
            break;
        }

        pFilter->State = FilterPaused;

        // Initialize AVB context for Intel devices
        pFilter->AvbContext = NULL;

        // Check if this is a supported Intel controller
        {
            USHORT ven = 0, dev = 0;
            DEBUGP(DL_TRACE, "FilterAttach: *** CHECKING ADAPTER SUPPORT *** %wZ\n", 
                   &pFilter->MiniportFriendlyName);
                   
            if (!AvbIsSupportedIntelController(pFilter, &ven, &dev))
            {
                DEBUGP(DL_TRACE, "FilterAttach: REJECTING adapter: %wZ (VID:0x%04x DID:0x%04x) - Status=NDIS_STATUS_NOT_SUPPORTED\n", 
                       &pFilter->MiniportFriendlyName, ven, dev);
                Status = NDIS_STATUS_NOT_SUPPORTED;
                break;
            }

            DEBUGP(DL_TRACE, "FilterAttach: *** FOUND SUPPORTED INTEL CONTROLLER *** %wZ (VID:0x%04x DID:0x%04x)\n", 
                   &pFilter->MiniportFriendlyName, ven, dev);

            // Supported Intel controller: initialize AVB context
            DEBUGP(DL_TRACE, "FilterAttach: *** STARTING AVB INITIALIZATION *** for %wZ\n", 
                   &pFilter->MiniportFriendlyName);
                   
            Status = AvbInitializeDevice(pFilter, (PAVB_DEVICE_CONTEXT*)&pFilter->AvbContext);
            if (Status != STATUS_SUCCESS)
            {
                DEBUGP(DL_TRACE, "FilterAttach: *** AVB INIT FAILED *** Intel NIC 0x%04x:0x%04x Status=0x%x for %wZ\n", 
                       ven, dev, Status, &pFilter->MiniportFriendlyName);
                // Don't fail attach if AVB init fails - continue with filter functionality
                pFilter->AvbContext = NULL;
                Status = NDIS_STATUS_SUCCESS;
            }
            else
            {
                PAVB_DEVICE_CONTEXT avbCtx = (PAVB_DEVICE_CONTEXT)pFilter->AvbContext;
                if (avbCtx) {
                    // Don't override the state - let the initialization set it correctly
                    DEBUGP(DL_TRACE, "*** AVB CONTEXT INITIALIZED SUCCESSFULLY *** %wZ HW_STATE=%s IfIndex=%u Context=%p\n", 
                           &pFilter->MiniportFriendlyName, AvbHwStateName(avbCtx->hw_state), pFilter->MiniportIfIndex, avbCtx);
                } else {
                    DEBUGP(DL_TRACE, "*** AVB CONTEXT IS NULL *** after successful init for %wZ\n", 
                           &pFilter->MiniportFriendlyName);
                }
                DEBUGP(DL_TRACE, "FilterAttach: AVB context initialized successfully for Intel NIC 0x%04x:0x%04x (%wZ)\n", 
                       ven, dev, &pFilter->MiniportFriendlyName);
            }
        }

        FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse);
        InsertHeadList(&FilterModuleList, &pFilter->FilterModuleLink);
        FILTER_RELEASE_LOCK(&FilterListLock, bFalse);

        DEBUGP(DL_TRACE, "FilterAttach: Successfully attached to %wZ with AVB context %p\n", 
               &pFilter->MiniportFriendlyName, pFilter->AvbContext);

    }
    while ( bFalse);

    if (Status != NDIS_STATUS_SUCCESS)
    {
        if (pFilter != NULL)
        {
            if (pFilter->AvbContext != NULL) {
                AvbCleanupDevice((PAVB_DEVICE_CONTEXT)pFilter->AvbContext);
                pFilter->AvbContext = NULL;
            }
            FILTER_FREE_MEM(pFilter);
        }
        DEBUGP(DL_TRACE, "FilterAttach: Failed to attach to adapter, Status=0x%x\n", Status);
    }

    DEBUGP(DL_TRACE, "<===FilterAttach: Status 0x%x\n", Status);
    return Status;
}

_Use_decl_annotations_
NDIS_STATUS
FilterPause(
    NDIS_HANDLE                     FilterModuleContext,
    PNDIS_FILTER_PAUSE_PARAMETERS   PauseParameters
    )
/*++

Routine Description:

    Filter pause routine.
    Complete all the outstanding sends and queued sends,
    wait for all the outstanding recvs to be returned
    and return all the queued receives.

Arguments:

    FilterModuleContext - pointer to the filter context stucture
    PauseParameters     - additional information about the pause

Return Value:

    NDIS_STATUS_SUCCESS if filter pauses successfully, NDIS_STATUS_PENDING
    if not.  No other return value is allowed (pause must succeed, eventually).

N.B.: When the filter is in Pausing state, it can still process OID requests,
    complete sending, and returning packets to NDIS, and also indicate status.
    After this function completes, the filter must not attempt to send or
    receive packets, but it may still process OID requests and status
    indications.

--*/
{
    PMS_FILTER          pFilter = (PMS_FILTER)(FilterModuleContext);
    NDIS_STATUS         Status;
    BOOLEAN               bFalse = FALSE;

    UNREFERENCED_PARAMETER(PauseParameters);

    // BUGFIX: GitHub Issue #315 - Enhanced pointer validation to prevent crash
    if (pFilter == NULL || (ULONG_PTR)pFilter < FILTER_PTR_MIN_VALID || ((ULONG_PTR)pFilter & FILTER_PTR_KERNEL_MASK) != FILTER_PTR_KERNEL_MASK)
    {
        DEBUGP(DL_TRACE, "FilterPause: INVALID FilterModuleContext=%p!\n", pFilter);
        // Return success anyway - NDIS will handle cleanup
        return NDIS_STATUS_SUCCESS;
    }

    DEBUGP(DL_TRACE, "===>IntelAvbFilter FilterPause: FilterInstance %p\n", FilterModuleContext);

    //
    // Set the flag that the filter is going to pause
    //
    FILTER_ASSERT(pFilter->State == FilterRunning);

    FILTER_ACQUIRE_LOCK(&pFilter->Lock, bFalse);
    pFilter->State = FilterPausing;
    FILTER_RELEASE_LOCK(&pFilter->Lock, bFalse);

    //
    // Do whatever work is required to bring the filter into the Paused state.
    //
    // If you have diverted and queued any send or receive NBLs, return them
    // now.
    //
    // If you send or receive original NBLs, stop doing that and wait for your
    // NBLs to return to you now.
    //

    Status = NDIS_STATUS_SUCCESS;

    pFilter->State = FilterPaused;

    DEBUGP(DL_TRACE, "<===FilterPause:  Status %x\n", Status);
    return Status;
}

//
// FilterQueryTimestampCapability
//
// Helper function to query OID_TIMESTAMP_CAPABILITY from miniport.
// Returns TRUE if miniport supports PTP v2 L2 TX timestamps, FALSE otherwise.
// This verifies the miniport implements NDIS 6.82+ hardware timestamping.
//
// Implementation based on German technical guide provided by user.
// Uses SDK-defined NDIS_TIMESTAMP_CAPABILITIES structure (ntddndis.h).
//
static BOOLEAN
FilterQueryTimestampCapability(
    _In_ PMS_FILTER pFilter
    )
{
    UNREFERENCED_PARAMETER(pFilter);
    
    DEBUGP(DL_TRACE, "FilterQueryTimestampCapability: Querying OID_TIMESTAMP_CAPABILITY\n");
    
    // Note: Full OID query implementation would use NdisAllocateCloneOidRequest
    // and NdisFOidRequest pattern. For now, we assume the miniport supports it
    // since the Intel drivers typically do.
    //
    // TODO: Implement full synchronous OID query if needed for production.
    
    DEBUGP(DL_TRACE, "FilterQueryTimestampCapability: OID query not yet implemented, assuming capable\n");
    return TRUE;  // Optimistic: assume Intel miniport supports HW timestamping
}

//
// FilterConfigureTimestamping
//
// Helper function to enable hardware timestamping via OID_TIMESTAMP_CONFIG.
// This is equivalent to setting TSYNCTXCTL.EN=1 at the hardware level.
//
// Implementation based on German technical guide provided by user.
// Uses SDK-defined NDIS_TIMESTAMP_CAPABILITY structure (ntddndis.h).
//
static NDIS_STATUS
FilterConfigureTimestamping(
    _In_ PMS_FILTER pFilter,
    _In_ BOOLEAN Enable
    )
{
    UNREFERENCED_PARAMETER(pFilter);
    
    if (Enable)
    {
        DEBUGP(DL_TRACE, "FilterConfigureTimestamping: Enabling HW timestamping via OID_TIMESTAMP_CONFIG\n");
    }
    else
    {
        DEBUGP(DL_TRACE, "FilterConfigureTimestamping: Disabling HW timestamping via OID_TIMESTAMP_CONFIG\n");
    }
    
    // Note: Full OID set implementation would use NdisAllocateCloneOidRequest
    // and NdisFOidRequest pattern with NDIS_TIMESTAMP_CAPABILITY structure.
    //
    // TODO: Implement full OID set if miniport requires explicit enable.
    //       Many Intel minorts enable HW timestamping by default for PTP packets.
    
    DEBUGP(DL_TRACE, "FilterConfigureTimestamping: OID set not yet implemented, returning success\n");
    return NDIS_STATUS_SUCCESS;
}

_Use_decl_annotations_
NDIS_STATUS
FilterRestart(
    NDIS_HANDLE                     FilterModuleContext,
    PNDIS_FILTER_RESTART_PARAMETERS RestartParameters
    )
{
    NDIS_STATUS     Status;
    PMS_FILTER      pFilter = (PMS_FILTER)FilterModuleContext;
    NDIS_HANDLE     ConfigurationHandle = NULL;
    PNDIS_RESTART_GENERAL_ATTRIBUTES NdisGeneralAttributes;
    PNDIS_RESTART_ATTRIBUTES         NdisRestartAttributes;
    NDIS_CONFIGURATION_OBJECT        ConfigObject;

    // BUGFIX: GitHub Issue #315 - Enhanced pointer validation to prevent crash
    if (pFilter == NULL || (ULONG_PTR)pFilter < FILTER_PTR_MIN_VALID || ((ULONG_PTR)pFilter & FILTER_PTR_KERNEL_MASK) != FILTER_PTR_KERNEL_MASK)
    {
        DEBUGP(DL_TRACE, "FilterRestart: INVALID FilterModuleContext=%p!\n", pFilter);
        // Return failure - cannot restart without valid filter context
        return NDIS_STATUS_FAILURE;
    }

    DEBUGP(DL_TRACE, "===>FilterRestart:   FilterModuleContext %p\n", FilterModuleContext);

    FILTER_ASSERT(pFilter->State == FilterPaused);

    ConfigObject.Header.Type = NDIS_OBJECT_TYPE_CONFIGURATION_OBJECT;
    ConfigObject.Header.Revision = NDIS_CONFIGURATION_OBJECT_REVISION_1;
    ConfigObject.Header.Size = sizeof(NDIS_CONFIGURATION_OBJECT);
    ConfigObject.NdisHandle = FilterDriverHandle;
    ConfigObject.Flags = 0;

    Status = NdisOpenConfigurationEx(&ConfigObject, &ConfigurationHandle);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        //
        // Filter driver can choose to fail the restart if it cannot open the configuration
        //

#if 0
        //
        // The code is here just to demonstrate how to call NDIS to write an
        // event to the eventlog.
        //
        PWCHAR              ErrorString = L"IntelAvbFilter";

        DEBUGP(DL_TRACE, "FilterRestart: Cannot open configuration.\n");
        NdisWriteEventLogEntry(FilterDriverObject,
                                EVENT_NDIS_DRIVER_FAILURE,
                                0,
                                1,
                                &ErrorString,
                                sizeof(Status),
                                &Status);
#endif

    }

    //
    // This sample doesn't actually do anything with the configuration handle;
    // it is opened here for illustrative purposes.  If you do not need to
    // read configuration, you may omit the code manipulating the
    // ConfigurationHandle.
    //

    if (Status == NDIS_STATUS_SUCCESS)
    {
        NdisCloseConfiguration(ConfigurationHandle);
    }

    NdisRestartAttributes = RestartParameters->RestartAttributes;

    //
    // If NdisRestartAttributes is not NULL, then the filter can modify generic
    // attributes and add new media specific info attributes at the end.
    // Otherwise, if NdisRestartAttributes is NULL, the filter should not try to
    // modify/add attributes.
    //
    if (NdisRestartAttributes != NULL)
    {
        PNDIS_RESTART_ATTRIBUTES   NextAttributes;

        ASSERT(NdisRestartAttributes->Oid == OID_GEN_MINIPORT_RESTART_ATTRIBUTES);

        NdisGeneralAttributes = (PNDIS_RESTART_GENERAL_ATTRIBUTES)NdisRestartAttributes->Data;

        //
        // Check to see if we need to change any attributes. For example, the
        // driver can change the current MAC address here. Or the driver can add
        // media specific info attributes.
        //
        NdisGeneralAttributes->LookaheadSize = 128;

        //
        // Check each attribute to see whether the filter needs to modify it.
        //
        NextAttributes = NdisRestartAttributes->Next;

        while (NextAttributes != NULL)
        {
            //
            // If somehow the filter needs to change a attributes which requires more space then
            // the current attributes:
            // 1. Remove the attribute from the Attributes list:
            //    TempAttributes = NextAttributes;
            //    NextAttributes = NextAttributes->Next;
            // 2. Free the memory for the current attributes: NdisFreeMemory(TempAttributes, 0 , 0);
            // 3. Dynamically allocate the memory for the new attributes by calling
            //    NdisAllocateMemoryWithTagPriority:
            //    NewAttributes = NdisAllocateMemoryWithTagPriority(Handle, size, Priority);
            // 4. Fill in the new attribute
            // 5. NewAttributes->Next = NextAttributes;
            // 6. NextAttributes = NewAttributes; // Just to make the next statement work.
            //
            NextAttributes = NextAttributes->Next;
        }

        //
        // Add a new attributes at the end
        // 1. Dynamically allocate the memory for the new attributes by calling
        //    NdisAllocateMemoryWithTagPriority.
        // 2. Fill in the new attribute
        // 3. NextAttributes->Next = NewAttributes;
        // 4. NewAttributes->Next = NULL;



    }


    //
    // Initialize hardware timestamping (Steps 5a-5b)
    //
    pFilter->HwTimestampEnabled = FALSE;  // Default to disabled
    
    if (FilterQueryTimestampCapability(pFilter))
    {
        // Miniport supports hardware timestamping, enable it
        Status = FilterConfigureTimestamping(pFilter, TRUE);
        if (Status == NDIS_STATUS_SUCCESS)
        {
            pFilter->HwTimestampEnabled = TRUE;
            DEBUGP(DL_TRACE, "FilterRestart: Hardware timestamping enabled successfully\n");
        }
        else
        {
            DEBUGP(DL_TRACE, "FilterRestart: Failed to enable hardware timestamping, Status=0x%x\n", Status);
            // Non-fatal: continue without timestamping
        }
    }
    else
    {
        DEBUGP(DL_TRACE, "FilterRestart: Miniport does not support hardware timestamping\n");
    }


    //
    // If everything is OK, set the filter in running state.
    //
    pFilter->State = FilterRunning; // when successful

    // Todo 9 (safe): source_mac populated lazily in IOCTL_AVB_TEST_SEND_PTP handler
    // (OID request from FilterRestart context is unsafe — NDIS completes it at
    // DISPATCH_LEVEL which corrupts the LFH delay-free list → BugCheck 0x13A_17)

    Status = NDIS_STATUS_SUCCESS;


    //
    // Ensure the state is Paused if restart failed.
    //

    if (Status != NDIS_STATUS_SUCCESS)
    {
        pFilter->State = FilterPaused;
    }


    DEBUGP(DL_TRACE, "<===FilterRestart:  FilterModuleContext %p, Status %x\n", FilterModuleContext, Status);
    return Status;
}


_Use_decl_annotations_
VOID
FilterDetach(
    NDIS_HANDLE     FilterModuleContext
    )
/*++

Routine Description:

    Filter detach routine.
    This is a required function that will deallocate all the resources allocated during
    FilterAttach. NDIS calls FilterAttach to remove a filter instance from a filter stack.

Arguments:

    FilterModuleContext - pointer to the filter context area.

Return Value:
    None.

NOTE: Called at PASSIVE_LEVEL and the filter is in paused state

--*/
{
    PMS_FILTER                  pFilter = (PMS_FILTER)FilterModuleContext;
    BOOLEAN                      bFalse = FALSE;

    // BUGFIX: GitHub Issue #315 - Enhanced pointer validation to prevent crash
    if (pFilter == NULL || (ULONG_PTR)pFilter < FILTER_PTR_MIN_VALID || ((ULONG_PTR)pFilter & FILTER_PTR_KERNEL_MASK) != FILTER_PTR_KERNEL_MASK)
    {
        DEBUGP(DL_TRACE, "FilterDetach: INVALID FilterModuleContext=%p! Cannot detach.\n", pFilter);
        // Cannot fail detach, but can't proceed either - just return
        return;
    }

    DEBUGP(DL_TRACE, "===>FilterDetach:    FilterInstance %p\n", FilterModuleContext);


    //
    // Filter must be in paused state
    //
    FILTER_ASSERT(pFilter->State == FilterPaused);


    //
    // Detach must not fail, so do not put any code here that can possibly fail.
    //

    //
    // Free filter instance name if allocated.
    //
    if (pFilter->FilterName.Buffer != NULL)
    {
        FILTER_FREE_MEM(pFilter->FilterName.Buffer);
    }


    FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse);
    RemoveEntryList(&pFilter->FilterModuleLink);
    FILTER_RELEASE_LOCK(&FilterListLock, bFalse);

    // Cleanup AVB context
    if (pFilter->AvbContext != NULL) {
        AvbCleanupDevice((PAVB_DEVICE_CONTEXT)pFilter->AvbContext);
        pFilter->AvbContext = NULL;
    }

    //
    // Free the memory allocated
    FILTER_FREE_MEM(pFilter);

    DEBUGP(DL_TRACE, "<===FilterDetach Successfully\n");
    return;
}

_Use_decl_annotations_
VOID
FilterUnload(
    PDRIVER_OBJECT      DriverObject
    )
/*++

Routine Description:

    Filter driver's unload routine.
    Deregister the driver from NDIS.

Arguments:

    DriverObject - pointer to the system's driver object structure
                   for this driver

Return Value:

    NONE

--*/
{
#if DBG
    BOOLEAN               bFalse = FALSE;
#endif

    UNREFERENCED_PARAMETER(DriverObject);

    DEBUGP(DL_TRACE, "===>FilterUnload\n");

    /* Unregister ETW provider (paired with EtwRegister in DriverEntry) */
    if (g_EtwHandle != 0) {
        EtwUnregister(g_EtwHandle);
        g_EtwHandle = 0;
    }

    //
    // Should free the filter context list
    //
    IntelAvbFilterDeregisterDevice();
    NdisFDeregisterFilterDriver(FilterDriverHandle);

#if DBG
    FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse);
    ASSERT(IsListEmpty(&FilterModuleList));

    FILTER_RELEASE_LOCK(&FilterListLock, bFalse);

#endif

    FILTER_FREE_LOCK(&FilterListLock);

    // Release global AVB adapter list lock (paired with NdisAllocateSpinLock in DriverEntry)
    NdisFreeSpinLock(&g_AvbContextListLock);

    DEBUGP(DL_TRACE, "<===FilterUnload\n");

    return;

}

_Use_decl_annotations_
NDIS_STATUS
FilterOidRequest(
    NDIS_HANDLE         FilterModuleContext,
    PNDIS_OID_REQUEST   Request
    )
/*++

Routine Description:

    Request handler
    Handle requests from upper layers

Arguments:

    FilterModuleContext   - our filter
    Request               - the request passed down


Return Value:

     NDIS_STATUS_SUCCESS
     NDIS_STATUS_PENDING
     NDIS_STATUS_XXX

NOTE: Called at <= DISPATCH_LEVEL  (unlike a miniport's MiniportOidRequest)

--*/
{
    PMS_FILTER              pFilter = (PMS_FILTER)FilterModuleContext;
    NDIS_STATUS             Status;
    PNDIS_OID_REQUEST       ClonedRequest=NULL;
    BOOLEAN                 bSubmitted = FALSE;
    PFILTER_REQUEST_CONTEXT Context;
    BOOLEAN                 bFalse = FALSE;

    // BUGFIX: GitHub Issue #315 - Enhanced pointer validation to prevent crash
    if (pFilter == NULL || (ULONG_PTR)pFilter < FILTER_PTR_MIN_VALID || ((ULONG_PTR)pFilter & FILTER_PTR_KERNEL_MASK) != FILTER_PTR_KERNEL_MASK)
    {
        DEBUGP(DL_TRACE, "FilterOidRequest: INVALID FilterModuleContext=%p!\n", pFilter);
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    DEBUGP(DL_TRACE, "===>FilterOidRequest: Request %p.\n", Request);

    //
    // Most of the time, a filter will clone the OID request and pass down
    // the clone.  When the clone completes, the filter completes the original
    // OID request.
    //
    // If your filter needs to modify a specific request, it can modify the
    // request before or after sending down the cloned request.  Or, it can
    // complete the original request on its own without sending down any
    // clone at all.
    //
    // If your filter driver does not need to modify any OID requests, then
    // you may simply omit this routine entirely; NDIS will pass OID requests
    // down on your behalf.  This is more efficient than implementing a
    // routine that does nothing but clone all requests, as in the sample here.
    //

    do
    {
        Status = NdisAllocateCloneOidRequest(pFilter->FilterHandle,
                                            Request,
                                            FILTER_TAG,
                                            &ClonedRequest);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            DEBUGP(DL_TRACE, "FilerOidRequest: Cannot Clone Request\n");
            break;
        }

        Context = (PFILTER_REQUEST_CONTEXT)(&ClonedRequest->SourceReserved[0]);
        *Context = Request;

        bSubmitted = TRUE;

        //
        // Use same request ID
        //
        ClonedRequest->RequestId = Request->RequestId;

        pFilter->PendingOidRequest = ClonedRequest;


        Status = NdisFOidRequest(pFilter->FilterHandle, ClonedRequest);

        if (Status != NDIS_STATUS_PENDING)
        {


            FilterOidRequestComplete(pFilter, ClonedRequest, Status);
            Status = NDIS_STATUS_PENDING;
        }



    }while (bFalse);

    if (bSubmitted == FALSE)
    {
        switch(Request->RequestType)
        {
            case NdisRequestMethod:
                Request->DATA.METHOD_INFORMATION.BytesRead = 0;
                Request->DATA.METHOD_INFORMATION.BytesNeeded = 0;
                Request->DATA.METHOD_INFORMATION.BytesWritten = 0;
                break;

            case NdisRequestSetInformation:
                Request->DATA.SET_INFORMATION.BytesRead = 0;
                Request->DATA.SET_INFORMATION.BytesNeeded = 0;
                break;

            case NdisRequestQueryInformation:
            case NdisRequestQueryStatistics:
            default:
                Request->DATA.QUERY_INFORMATION.BytesWritten = 0;
                Request->DATA.QUERY_INFORMATION.BytesNeeded = 0;
                break;
        }

    }
    DEBUGP(DL_TRACE, "<===FilterOidRequest: Status %8x.\n", Status);

    return Status;

}

_Use_decl_annotations_
VOID
FilterCancelOidRequest(
    NDIS_HANDLE             FilterModuleContext,
    PVOID                   RequestId
    )
/*++

Routine Description:

    Cancels an OID request

    If your filter driver does not intercept and hold onto any OID requests,
    then you do not need to implement this routine.  You may simply omit it.
    Furthermore, if the filter only holds onto OID requests so it can pass
    down a clone (the most common case) the filter does not need to implement
    this routine; NDIS will then automatically request that the lower-level
    filter/miniport cancel your cloned OID.

    Most filters do not need to implement this routine.

Arguments:

    FilterModuleContext   - our filter
    RequestId             - identifies the request(s) to cancel

--*/
{
    PMS_FILTER                          pFilter = (PMS_FILTER)FilterModuleContext;
    PNDIS_OID_REQUEST                   Request = NULL;
    PFILTER_REQUEST_CONTEXT             Context;
    PNDIS_OID_REQUEST                   OriginalRequest = NULL;
    BOOLEAN                             bFalse = FALSE;

    // BUGFIX: GitHub Issue #315 - Enhanced pointer validation to prevent crash
    if (pFilter == NULL || (ULONG_PTR)pFilter < FILTER_PTR_MIN_VALID || ((ULONG_PTR)pFilter & FILTER_PTR_KERNEL_MASK) != FILTER_PTR_KERNEL_MASK)
    {
        DEBUGP(DL_TRACE, "FilterCancelOidRequest: INVALID FilterModuleContext=%p!\n", pFilter);
        return;
    }

    FILTER_ACQUIRE_LOCK(&pFilter->Lock, bFalse);

    Request = pFilter->PendingOidRequest;

    if (Request != NULL)
    {
        Context = (PFILTER_REQUEST_CONTEXT)(&Request->SourceReserved[0]);

        OriginalRequest = (*Context);
    }

    if ((OriginalRequest != NULL) && (OriginalRequest->RequestId == RequestId))
    {
        FILTER_RELEASE_LOCK(&pFilter->Lock, bFalse);

        NdisFCancelOidRequest(pFilter->FilterHandle, RequestId);
    }
    else
    {
        FILTER_RELEASE_LOCK(&pFilter->Lock, bFalse);
    }


}

_Use_decl_annotations_
VOID
FilterOidRequestComplete(
    NDIS_HANDLE         FilterModuleContext,
    PNDIS_OID_REQUEST   Request,
    NDIS_STATUS         Status
    )
/*++

Routine Description:

    Notification that an OID request has been completed

    If this filter sends a request down to a lower layer, and the request is
    pended, the FilterOidRequestComplete routine is invoked when the request
    is complete.  Most requests we've sent are simply clones of requests
    received from a higher layer; all we need to do is complete the original
    higher request.

    However, if this filter driver sends original requests down, it must not
    attempt to complete a pending request to the higher layer.

Arguments:

    FilterModuleContext   - our filter context area
    NdisRequest           - the completed request
    Status                - completion status

--*/
{
    PMS_FILTER                          pFilter = (PMS_FILTER)FilterModuleContext;
    PNDIS_OID_REQUEST                   OriginalRequest;
    PFILTER_REQUEST_CONTEXT             Context;
    BOOLEAN                             bFalse = FALSE;

    // BUGFIX: GitHub Issue #315 - Enhanced pointer validation to prevent crash
    if (pFilter == NULL || (ULONG_PTR)pFilter < FILTER_PTR_MIN_VALID || ((ULONG_PTR)pFilter & FILTER_PTR_KERNEL_MASK) != FILTER_PTR_KERNEL_MASK)
    {
        DEBUGP(DL_TRACE, "FilterOidRequestComplete: INVALID FilterModuleContext=%p!\n", pFilter);
        return;
    }

    DEBUGP(DL_TRACE, "===>FilterOidRequestComplete, Request %p.\n", Request);

    Context = (PFILTER_REQUEST_CONTEXT)(&Request->SourceReserved[0]);
    OriginalRequest = (*Context);

    //
    // This is an internal request
    //
    if (OriginalRequest == NULL)
    {
        filterInternalRequestComplete(pFilter, Request, Status);
        return;
    }



    FILTER_ACQUIRE_LOCK(&pFilter->Lock, bFalse);

    ASSERT(pFilter->PendingOidRequest == Request);
    pFilter->PendingOidRequest = NULL;

    FILTER_RELEASE_LOCK(&pFilter->Lock, bFalse);


    //
    // Copy the information from the returned request to the original request
    //
    switch(Request->RequestType)
    {
        case NdisRequestMethod:
            OriginalRequest->DATA.METHOD_INFORMATION.OutputBufferLength =  Request->DATA.METHOD_INFORMATION.OutputBufferLength;
            OriginalRequest->DATA.METHOD_INFORMATION.BytesRead = Request->DATA.METHOD_INFORMATION.BytesRead;
            OriginalRequest->DATA.METHOD_INFORMATION.BytesNeeded = Request->DATA.METHOD_INFORMATION.BytesNeeded;
            OriginalRequest->DATA.METHOD_INFORMATION.BytesWritten = Request->DATA.METHOD_INFORMATION.BytesWritten;
            break;

        case NdisRequestSetInformation:
            OriginalRequest->DATA.SET_INFORMATION.BytesRead = Request->DATA.SET_INFORMATION.BytesRead;
            OriginalRequest->DATA.SET_INFORMATION.BytesNeeded = Request->DATA.SET_INFORMATION.BytesNeeded;
            break;

        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
        default:
            OriginalRequest->DATA.QUERY_INFORMATION.BytesWritten = Request->DATA.QUERY_INFORMATION.BytesWritten;
            OriginalRequest->DATA.QUERY_INFORMATION.BytesNeeded = Request->DATA.QUERY_INFORMATION.BytesNeeded;
            break;
    }


    (*Context) = NULL;

    NdisFreeCloneOidRequest(pFilter->FilterHandle, Request);

    NdisFOidRequestComplete(pFilter->FilterHandle, OriginalRequest, Status);

    DEBUGP(DL_TRACE, "<===FilterOidRequestComplete.\n");
}


_Use_decl_annotations_
VOID
FilterStatus(
    NDIS_HANDLE             FilterModuleContext,
    PNDIS_STATUS_INDICATION StatusIndication
    )
/*++

Routine Description:

    Status indication handler

Arguments:

    FilterModuleContext     - our filter context
    StatusIndication        - the status being indicated

NOTE: called at <= DISPATCH_LEVEL

  FILTER driver may call NdisFIndicateStatus to generate a status indication to
  all higher layer modules.

--*/
{
    PMS_FILTER              pFilter = (PMS_FILTER)FilterModuleContext;
#if DBG
    BOOLEAN                  bFalse = FALSE;
#endif

    // BUGFIX: GitHub Issue #315 - Enhanced pointer validation to prevent crash
    if (pFilter == NULL || (ULONG_PTR)pFilter < FILTER_PTR_MIN_VALID || ((ULONG_PTR)pFilter & FILTER_PTR_KERNEL_MASK) != FILTER_PTR_KERNEL_MASK)
    {
        DEBUGP(DL_TRACE, "FilterStatus: INVALID FilterModuleContext=%p!\n", pFilter);
        return;
    }

    DEBUGP(DL_TRACE, "===>FilterStatus, IndicateStatus = %8x.\n", StatusIndication->StatusCode);


    //
    // The filter may do processing on the status indication here, including
    // intercepting and dropping it entirely.  However, the sample does nothing
    // with status indications except pass them up to the higher layer.  It is
    // more efficient to omit the FilterStatus handler entirely if it does
    // nothing, but it is included in this sample for illustrative purposes.
    //

#if DBG
    FILTER_ACQUIRE_LOCK(&pFilter->Lock, bFalse);
    ASSERT(pFilter->bIndicating == FALSE);
    pFilter->bIndicating = TRUE;
    FILTER_RELEASE_LOCK(&pFilter->Lock, bFalse);
#endif // DBG

    NdisFIndicateStatus(pFilter->FilterHandle, StatusIndication);

#if DBG
    FILTER_ACQUIRE_LOCK(&pFilter->Lock, bFalse);
    ASSERT(pFilter->bIndicating == TRUE);
    pFilter->bIndicating = FALSE;
    FILTER_RELEASE_LOCK(&pFilter->Lock, bFalse);
#endif // DBG

    DEBUGP(DL_TRACE, "<===FilterStatus.\n");

}

_Use_decl_annotations_
VOID
FilterDevicePnPEventNotify(
    NDIS_HANDLE             FilterModuleContext,
    PNET_DEVICE_PNP_EVENT   NetDevicePnPEvent
    )
/*++

Routine Description:

    Device PNP event handler

Arguments:

    FilterModuleContext         - our filter context
    NetDevicePnPEvent           - a Device PnP event

NOTE: called at PASSIVE_LEVEL

--*/
{
    PMS_FILTER             pFilter = (PMS_FILTER)FilterModuleContext;
    NDIS_DEVICE_PNP_EVENT  DevicePnPEvent = NetDevicePnPEvent->DevicePnPEvent;
#if DBG
    BOOLEAN                bFalse = FALSE;
#endif

    // BUGFIX: GitHub Issue #315 - Enhanced pointer validation to prevent crash
    if (pFilter == NULL || (ULONG_PTR)pFilter < FILTER_PTR_MIN_VALID || ((ULONG_PTR)pFilter & FILTER_PTR_KERNEL_MASK) != FILTER_PTR_KERNEL_MASK)
    {
        DEBUGP(DL_TRACE, "FilterDevicePnPEventNotify: INVALID FilterModuleContext=%p!\n", pFilter);
        return;
    }

    DEBUGP(DL_TRACE, "===>FilterDevicePnPEventNotify: NetPnPEvent = %p.\n", NetDevicePnPEvent);

    //
    // The filter may do processing on the event here, including intercepting
    // and dropping it entirely.  However, the sample does nothing with Device
    // PNP events, except pass them down to the next lower* layer.  It is more
    // efficient to omit the FilterDevicePnPEventNotify handler entirely if it
    // does nothing, but it is included in this sample for illustrative purposes.
    //
    // * Trivia: Device PNP events percolate DOWN the stack, instead of upwards
    // like status indications and Net PnP events.  So the next layer is the
    // LOWER layer.
    //

    switch (DevicePnPEvent)
    {

        case NdisDevicePnPEventQueryRemoved:
        case NdisDevicePnPEventRemoved:
        case NdisDevicePnPEventSurpriseRemoved:
        case NdisDevicePnPEventQueryStopped:
        case NdisDevicePnPEventStopped:
        case NdisDevicePnPEventPowerProfileChanged:
        case NdisDevicePnPEventFilterListChanged:

            break;

        default:
            DEBUGP(DL_TRACE, "FilterDevicePnPEventNotify: Invalid event.\n");
            FILTER_ASSERT(bFalse);

            break;
    }

    NdisFDevicePnPEventNotify(pFilter->FilterHandle, NetDevicePnPEvent);

    DEBUGP(DL_TRACE, "<===FilterDevicePnPEventNotify\n");

}

_Use_decl_annotations_
NDIS_STATUS
FilterNetPnPEvent(
    NDIS_HANDLE              FilterModuleContext,
    PNET_PNP_EVENT_NOTIFICATION NetPnPEventNotification
    )
/*++

Routine Description:

    Net PNP event handler

Arguments:

    FilterModuleContext         - our filter context
    NetPnPEventNotification     - a Net PnP event

NOTE: called at PASSIVE_LEVEL

--*/
{
    PMS_FILTER                pFilter = (PMS_FILTER)FilterModuleContext;
    NDIS_STATUS               Status = NDIS_STATUS_SUCCESS;

    // BUGFIX: GitHub Issue #315 - Enhanced pointer validation to prevent crash
    if (pFilter == NULL || (ULONG_PTR)pFilter < FILTER_PTR_MIN_VALID || ((ULONG_PTR)pFilter & FILTER_PTR_KERNEL_MASK) != FILTER_PTR_KERNEL_MASK)
    {
        DEBUGP(DL_TRACE, "FilterNetPnPEvent: INVALID FilterModuleContext=%p!\n", pFilter);
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    //
    // The filter may do processing on the event here, including intercepting
    // and dropping it entirely.  However, the sample does nothing with Net PNP
    // events, except pass them up to the next higher layer.  It is more
    // efficient to omit the FilterNetPnPEvent handler entirely if it does
    // nothing, but it is included in this sample for illustrative purposes.
    //

    Status = NdisFNetPnPEvent(pFilter->FilterHandle, NetPnPEventNotification);

    return Status;
}

_Use_decl_annotations_
VOID
FilterSendNetBufferListsComplete(
    NDIS_HANDLE         FilterModuleContext,
    PNET_BUFFER_LIST    NetBufferLists,
    ULONG               SendCompleteFlags
    )
/*++

Routine Description:

    Send complete handler

    This routine is invoked whenever the lower layer is finished processing
    sent NET_BUFFER_LISTs.  If the filter does not need to be involved in the
    send path, you should remove this routine and the FilterSendNetBufferLists
    routine.  NDIS will pass along send packets on behalf of your filter more
    efficiently than the filter can.

Arguments:

    FilterModuleContext     - our filter context
    NetBufferLists          - a chain of NBLs that are being returned to you
    SendCompleteFlags       - flags (see documentation)

Return Value:

     NONE

--*/
{
    PMS_FILTER         pFilter = (PMS_FILTER)FilterModuleContext;
    ULONG              NumOfSendCompletes = 0;
    BOOLEAN            DispatchLevel;
    PNET_BUFFER_LIST   CurrNbl;

    // BUGFIX: GitHub Issue #315 - Enhanced pointer validation to prevent crash
    if (pFilter == NULL || (ULONG_PTR)pFilter < FILTER_PTR_MIN_VALID || ((ULONG_PTR)pFilter & FILTER_PTR_KERNEL_MASK) != FILTER_PTR_KERNEL_MASK)
    {
        DEBUGP(DL_TRACE, "FilterSendNetBufferListsComplete: INVALID FilterModuleContext=%p! Passing NBLs up anyway.\n", pFilter);
        NdisFSendNetBufferListsComplete(FilterDriverHandle, NetBufferLists, SendCompleteFlags);
        return;
    }

    //
    // If your filter injected any send packets into the datapath to be sent,
    // you must identify their NBLs here and remove them from the chain.  Do not
    // attempt to send-complete your NBLs up to the higher layer.
    //

    //
    // If your filter has modified any NBLs (or NBs, MDLs, etc) in your
    // FilterSendNetBufferLists handler, you must undo the modifications here.
    // In general, NBLs must be returned in the same condition in which you had
    // you received them.  (Exceptions: the NBLs can be re-ordered on the linked
    // list, and the scratch fields are don't-care).
    //

    if (pFilter->TrackSends)
    {
        CurrNbl = NetBufferLists;
        while (CurrNbl)
        {
            NumOfSendCompletes++;
            CurrNbl = NET_BUFFER_LIST_NEXT_NBL(CurrNbl);

        }
        DispatchLevel = NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendCompleteFlags);
        FILTER_ACQUIRE_LOCK(&pFilter->Lock, DispatchLevel);
        pFilter->OutstandingSends -= NumOfSendCompletes;
        FILTER_LOG_SEND_REF(2, pFilter, PrevNbl, pFilter->OutstandingSends);
        FILTER_RELEASE_LOCK(&pFilter->Lock, DispatchLevel);
    }

    // Send complete the NBLs.  If you removed any NBLs from the chain, make
    // sure the chain isn't empty (i.e., NetBufferLists!=NULL).

    // Step 8b: Handle test packet injection completions (kernel-originated NBLs)
    // Remove test NBLs from chain and free resources (do NOT send complete up to protocol)
    PAVB_DEVICE_CONTEXT avbCtx = (PAVB_DEVICE_CONTEXT)pFilter->AvbContext;
    PNET_BUFFER_LIST PrevNbl = NULL;
    CurrNbl = NetBufferLists;
    while (CurrNbl) {
        PNET_BUFFER_LIST NextNbl = NET_BUFFER_LIST_NEXT_NBL(CurrNbl);
        BOOLEAN is_test_packet = FALSE;

        // Primary identification: check if the NET_BUFFER came from our injection pool.
        // This is more reliable than inspecting MDL data (CurrentMdl may be advanced by igc.sys).
        // NdisGetPoolFromNetBuffer returns the pool handle used to allocate the NB, which is
        // unique per filter instance. Available in NDIS 6.20+.
        PNET_BUFFER nb = NET_BUFFER_LIST_FIRST_NB(CurrNbl);
        NDIS_HANDLE nbActualPool = nb ? NdisGetPoolFromNetBuffer(nb) : NULL;
        // Diagnostic: print for EVERY NBL to confirm (a) the loop runs, (b) the pool handles
        if (avbCtx && avbCtx->nb_pool_handle && nb) {
            if (nbActualPool == avbCtx->nb_pool_handle) {
                is_test_packet = TRUE;
            }
        }

        // Secondary/fallback: destination MAC 01:1B:19:00:00:00 (PTP L2 multicast).
        // Handles rare case where avbCtx or pool handle is unavailable at completion time.
        if (!is_test_packet && nb) {
            PMDL mdl = NET_BUFFER_CURRENT_MDL(nb);
            if (mdl && mdl->MappedSystemVa) {
                UCHAR *data = (UCHAR *)mdl->MappedSystemVa + NET_BUFFER_CURRENT_MDL_OFFSET(nb);
                if (data[0] == 0x01 && data[1] == 0x1B && data[2] == 0x19 &&
                    data[3] == 0x00 && data[4] == 0x00 && data[5] == 0x00) {
                    is_test_packet = TRUE;
                }
            }
        }

        if (is_test_packet) {
            // Remove from chain
            if (PrevNbl) {
                NET_BUFFER_LIST_NEXT_NBL(PrevNbl) = NextNbl;
            } else {
                NetBufferLists = NextNbl;  // Update head of chain
            }

            // avbCtx already fetched above

            // Harvest NDIS TaggedTransmitHw timestamp BEFORE freeing the NBL.
            // igc.sys (NDIS 6.82+) writes a ULONG64 to NetBufferListInfo[AVB_TX_TIMESTAMP_SLOT]
            // (= NetBufferListInfoReserved3, slot 26 on AMD64/Win11) on send-complete when
            // NDIS_NBL_FLAGS_CAPTURE_TIMESTAMP_ON_TRANSMIT is set.
            // NOTE: We no longer set that flag (it defers send-complete ~600µs while providing
            // no benefit since igc.sys never populates slot 26 for filter-injected NBLs).
            // The upgrade path is kept for future HW that does implement TaggedTransmitHw.
            // CRITICAL: Must be read BEFORE NdisFreeNetBufferList() — reading after free is UAF.
            // InterlockedExchange64 is safe at DISPATCH_LEVEL (single XCHG instruction on x64).
            if (avbCtx) {
                // Try NDIS 6.82 TaggedTransmitHw path first (slot 26 = NetBufferListInfoReserved3).
                // igc.sys populates this only if it implements TaggedTransmitHw.
                ULONG64 ts = *((ULONG64*)(&CurrNbl->NetBufferListInfo[AVB_TX_TIMESTAMP_SLOT]));
                DEBUGP(DL_TRACE, "TestPkt TxComplete: NBL=%p NblFlags=0x%08lX slot26=0x%016I64X\n",
                       CurrNbl, (ULONG)CurrNbl->NblFlags, ts);

                if (ts != 0) {
                    // TaggedTransmitHw: igc.sys provided a hardware-latched timestamp.
                    // This is more accurate than the pre-send SYSTIM; upgrade the stored value.
                    (void)InterlockedExchange64(&avbCtx->last_ndis_tx_timestamp, (LONGLONG)ts);
                    DEBUGP(DL_TRACE, "TX timestamp upgraded to TaggedTransmitHw: 0x%016I64X\n", ts);
                } else {
                    // slot 26 = 0: igc.sys does not support TaggedTransmitHw.
                    // The pre-send SYSTIM (captured in IOCTL_AVB_TEST_SEND_PTP) is already in
                    // last_ndis_tx_timestamp — do NOT overwrite with 0.
                    DEBUGP(DL_TRACE, "TX timestamp: slot26=0, keeping pre-send SYSTIM\n");
                }
            }

            // Return NBL to ring (fast path) or free from pool (fallback slow path).
            if (avbCtx) {
                BOOLEAN returned_to_ring = FALSE;
                for (int _ri = 0; _ri < AVB_TEST_NBL_RING_SIZE; _ri++) {
                    if (avbCtx->test_nbl_ring[_ri].nbl == CurrNbl) {
                        // Ring-allocated: clear OOB timestamp slot, mark free for reuse.
                        CurrNbl->NetBufferListInfo[AVB_TX_TIMESTAMP_SLOT] = NULL;
                        InterlockedExchange(&avbCtx->test_nbl_ring[_ri].in_use, 0);
                        returned_to_ring = TRUE;
                        break;
                    }
                }
                if (!returned_to_ring) {
                    // Pool-allocated (ring-full fallback): free normally.
                    PNET_BUFFER test_nb = NET_BUFFER_LIST_FIRST_NB(CurrNbl);
                    if (test_nb && avbCtx->nb_pool_handle) {
                        NdisFreeNetBuffer(test_nb);
                    }
                    if (avbCtx->nbl_pool_handle) {
                        NdisFreeNetBufferList(CurrNbl);
                    }
                }
            }

            
            // Decrement pending counter (volatile LONG* required for InterlockedDecrement)
            if (avbCtx) {
#if DBG
                LONG pending = InterlockedDecrement((volatile LONG*)&avbCtx->test_packets_pending);
                DEBUGP(DL_TRACE, "Test packet send completed (pending=%d)\n", pending);
#else
                InterlockedDecrement((volatile LONG*)&avbCtx->test_packets_pending);
#endif
            }
            
            // Note: Do NOT advance PrevNbl (we removed CurrNbl from chain)
        } else {
            // Normal packet - keep in chain
            PrevNbl = CurrNbl;
        }
        
        CurrNbl = NextNbl;
    }

    // Send complete remaining NBLs (test NBLs were removed)
    // Safety check: only call if chain not empty
    if (NetBufferLists != NULL) {
        NdisFSendNetBufferListsComplete(pFilter->FilterHandle, NetBufferLists, SendCompleteFlags);
    }

}


_Use_decl_annotations_
VOID
FilterSendNetBufferLists(
    NDIS_HANDLE         FilterModuleContext,
    PNET_BUFFER_LIST    NetBufferLists,
    NDIS_PORT_NUMBER    PortNumber,
    ULONG               SendFlags
    )
/*++

Routine Description:

    Send Net Buffer List handler
    This function is an optional function for filter drivers. If provided, NDIS
    will call this function to transmit a linked list of NetBuffers, described by a
    NetBufferList, over the network. If this handler is NULL, NDIS will skip calling
    this filter when sending a NetBufferList and will call the next lower
    driver in the stack.  A filter that doesn't provide a FilerSendNetBufferList
    handler can not originate a send on its own.

Arguments:

    FilterModuleContext     - our filter context area
    NetBufferLists          - a List of NetBufferLists to send
    PortNumber              - Port Number to which this send is targeted
    SendFlags               - specifies if the call is at DISPATCH_LEVEL

--*/
{
    PMS_FILTER          pFilter = (PMS_FILTER)FilterModuleContext;
    PNET_BUFFER_LIST    CurrNbl;
    BOOLEAN             DispatchLevel;
    BOOLEAN             bFalse = FALSE;

    // BUGFIX: GitHub Issue #315 - NULL/invalid pointer check to prevent DRIVER_IRQL_NOT_LESS_OR_EQUAL (0xD1)
    // Race condition: NDIS may call this after FilterDetach if packets are in flight
    // Check for NULL OR invalid kernel pointers (e.g., 0x4 seen in crash dumps)
    // Valid x64 kernel pointers have upper bits set (>= 0xFFFF8000'00000000)
    if (pFilter == NULL || (ULONG_PTR)pFilter < FILTER_PTR_MIN_VALID || ((ULONG_PTR)pFilter & FILTER_PTR_KERNEL_MASK) != FILTER_PTR_KERNEL_MASK)
    {
        DEBUGP(DL_TRACE, "FilterSendNetBufferLists: INVALID FilterModuleContext=%p! Completing NBLs with error.\n", pFilter);
        CurrNbl = NetBufferLists;
        while (CurrNbl)
        {
            PNET_BUFFER_LIST NextNbl = NET_BUFFER_LIST_NEXT_NBL(CurrNbl);
            NET_BUFFER_LIST_STATUS(CurrNbl) = NDIS_STATUS_PAUSED;
            CurrNbl = NextNbl;
        }
        NdisFSendNetBufferListsComplete(NULL, NetBufferLists, 
            NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags) ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
        return;
    }

    do
    {

       DispatchLevel = NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags);
#if DBG
        //
        // we should never get packets to send if we are not in running state
        //

        FILTER_ACQUIRE_LOCK(&pFilter->Lock, DispatchLevel);
        //
        // If the filter is not in running state, fail the send
        //
        if (pFilter->State != FilterRunning)
        {
            FILTER_RELEASE_LOCK(&pFilter->Lock, DispatchLevel);

            CurrNbl = NetBufferLists;
            while (CurrNbl)
            {
                NET_BUFFER_LIST_STATUS(CurrNbl) = NDIS_STATUS_PAUSED;
                CurrNbl = NET_BUFFER_LIST_NEXT_NBL(CurrNbl);
            }
            NdisFSendNetBufferListsComplete(pFilter->FilterHandle,
                        NetBufferLists,
                        DispatchLevel ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
            break;

        }
        FILTER_RELEASE_LOCK(&pFilter->Lock, DispatchLevel);
#endif
        if (pFilter->TrackSends)
        {
            FILTER_ACQUIRE_LOCK(&pFilter->Lock, DispatchLevel);
            CurrNbl = NetBufferLists;
            while (CurrNbl)
            {
                pFilter->OutstandingSends++;
                FILTER_LOG_SEND_REF(1, pFilter, CurrNbl, pFilter->OutstandingSends);

                CurrNbl = NET_BUFFER_LIST_NEXT_NBL(CurrNbl);
            }
            FILTER_RELEASE_LOCK(&pFilter->Lock, DispatchLevel);
        }

        //
        // If necessary, queue the NetBufferLists in a local structure for later
        // processing.  However, do not queue them for "too long", or else the
        // system's performance may be degraded.  If you need to hold onto an
        // NBL for an unbounded amount of time, then allocate memory, perform a
        // deep copy, and complete the original NBL.
        //

        //
        // STEP 5c: Attach HW timestamp metadata for PTP packets
        // This tells the Intel miniport to set the 2STEP_1588 bit in the TX descriptor,
        // which triggers hardware TX timestamp capture into TXSTMPL/TXSTMPH registers.
        //
        // Implementation based on German technical guide provided by user.
        //
        if (pFilter->HwTimestampEnabled)
        {
            CurrNbl = NetBufferLists;
            while (CurrNbl)
            {
                PNET_BUFFER currNb = NET_BUFFER_LIST_FIRST_NB(CurrNbl);
                
                if (currNb != NULL)
                {
                    // Access Ethernet header to check EtherType at offset 12-13
                    // PTP packets use EtherType 0x88F7 (IEEE 1588 over Ethernet Layer 2)
                    unsigned char headerBuffer[14];  // Ethernet header size
                    PVOID dataBuffer;
                    ULONG dataLength;
                    
                    // Get pointer to data in first MDL
                    NdisQueryMdl(
                        NET_BUFFER_CURRENT_MDL(currNb),
                        &dataBuffer,
                        &dataLength,
                        NormalPagePriority | MdlMappingNoExecute
                    );
                    
                    if (dataBuffer != NULL && dataLength >= 14)
                    {
                        // Copy header safely
                        NdisMoveMemory(headerBuffer, 
                                      (PUCHAR)dataBuffer + NET_BUFFER_CURRENT_MDL_OFFSET(currNb), 
                                      14);
                        
                        // Extract EtherType (Big Endian at offset 12-13)
                        USHORT etherType = (headerBuffer[12] << 8) | headerBuffer[13];
                        
                        if (etherType == ETHERTYPE_PTP)  // IEEE 1588 PTP EtherType
                        {
                            // Intel miniport detects PTP via EtherType=0x88F7 + TSYNCTXCTL register;
                            // hardware latches SYSTIM at SFD without needing NBL OOB metadata.
                        }
                    }
                }
                
                CurrNbl = NET_BUFFER_LIST_NEXT_NBL(CurrNbl);
            }
        }

        NdisFSendNetBufferLists(pFilter->FilterHandle, NetBufferLists, PortNumber, SendFlags);


    }
    while (bFalse);

}

_Use_decl_annotations_
VOID
FilterReturnNetBufferLists(
    NDIS_HANDLE         FilterModuleContext,
    PNET_BUFFER_LIST    NetBufferLists,
    ULONG               ReturnFlags
    )
/*++

Routine Description:

    FilterReturnNetBufferLists handler.
    FilterReturnNetBufferLists is an optional function. If provided, NDIS calls
    FilterReturnNetBufferLists to return the ownership of one or more NetBufferLists
    and their embedded NetBuffers to the filter driver. If this handler is NULL, NDIS
    will skip calling this filter when returning NetBufferLists to the underlying
    miniport and will call the next lower driver in the stack. A filter that doesn't
    provide a FilterReturnNetBufferLists handler cannot originate a receive indication
    on its own.

Arguments:

    FilterInstanceContext       - our filter context area
    NetBufferLists              - a linked list of NetBufferLists that this
                                  filter driver indicated in a previous call to
                                  NdisFIndicateReceiveNetBufferLists
    ReturnFlags                 - flags specifying if the caller is at DISPATCH_LEVEL

--*/
{
    PMS_FILTER          pFilter = (PMS_FILTER)FilterModuleContext;
    PNET_BUFFER_LIST    CurrNbl = NetBufferLists;
    UINT                NumOfNetBufferLists = 0;
    BOOLEAN             DispatchLevel;
    ULONG               Ref;

    // BUGFIX: GitHub Issue #315 - Enhanced pointer validation to prevent crash
    if (pFilter == NULL || (ULONG_PTR)pFilter < FILTER_PTR_MIN_VALID || ((ULONG_PTR)pFilter & FILTER_PTR_KERNEL_MASK) != FILTER_PTR_KERNEL_MASK)
    {
        DEBUGP(DL_TRACE, "FilterReturnNetBufferLists: INVALID FilterModuleContext=%p! Cannot return NBLs.\n", pFilter);
        // Cannot call NdisFReturnNetBufferLists without valid filter handle
        // This should not happen in normal operation
        return;
    }

    DEBUGP(DL_TRACE, "===>ReturnNetBufferLists, NetBufferLists is %p.\n", NetBufferLists);


    //
    // If your filter injected any receive packets into the datapath to be
    // received, you must identify their NBLs here and remove them from the
    // chain.  Do not attempt to receive-return your NBLs down to the lower
    // layer.
    //

    //
    // If your filter has modified any NBLs (or NBs, MDLs, etc) in your
    // FilterReceiveNetBufferLists handler, you must undo the modifications here.
    // In general, NBLs must be returned in the same condition in which you had
    // you received them.  (Exceptions: the NBLs can be re-ordered on the linked
    // list, and the scratch fields are don't-care).
    //

    if (pFilter->TrackReceives)
    {
        while (CurrNbl)
        {
            NumOfNetBufferLists ++;
            CurrNbl = NET_BUFFER_LIST_NEXT_NBL(CurrNbl);
        }
    }


    // Return the received NBLs.  If you removed any NBLs from the chain, make
    // sure the chain isn't empty (i.e., NetBufferLists!=NULL).

    NdisFReturnNetBufferLists(pFilter->FilterHandle, NetBufferLists, ReturnFlags);

    if (pFilter->TrackReceives)
    {
        DispatchLevel = NDIS_TEST_RETURN_AT_DISPATCH_LEVEL(ReturnFlags);
        FILTER_ACQUIRE_LOCK(&pFilter->Lock, DispatchLevel);

        pFilter->OutstandingRcvs -= NumOfNetBufferLists;
        Ref = pFilter->OutstandingRcvs;
        FILTER_LOG_RCV_REF(3, pFilter, NetBufferLists, Ref);
        FILTER_RELEASE_LOCK(&pFilter->Lock, DispatchLevel);
    }


    DEBUGP(DL_TRACE, "<===ReturnNetBufferLists.\n");


}


_Use_decl_annotations_
VOID
FilterReceiveNetBufferLists(
    NDIS_HANDLE         FilterModuleContext,
    PNET_BUFFER_LIST    NetBufferLists,
    NDIS_PORT_NUMBER    PortNumber,
    ULONG               NumberOfNetBufferLists,
    ULONG               ReceiveFlags
    )
/*++

Routine Description:

    FilerReceiveNetBufferLists is an optional function for filter drivers.
    If provided, this function processes receive indications made by underlying
    NIC or lower level filter drivers. This function  can also be called as a
    result of loopback. If this handler is NULL, NDIS will skip calling this
    filter when processing a receive indication and will call the next higher
    driver in the stack. A filter that doesn't provide a
    FilterReceiveNetBufferLists handler cannot provide a
    FilterReturnNetBufferLists handler and cannot a initiate an original receive
    indication on its own.

Arguments:

    FilterModuleContext      - our filter context area.
    NetBufferLists           - a linked list of NetBufferLists
    PortNumber               - Port on which the receive is indicated
    ReceiveFlags             -

N.B.: It is important to check the ReceiveFlags in NDIS_TEST_RECEIVE_CANNOT_PEND.
    This controls whether the receive indication is an synchronous or
    asynchronous function call.

--*/
{

    PMS_FILTER          pFilter = (PMS_FILTER)FilterModuleContext;
    BOOLEAN             DispatchLevel;
    ULONG               Ref;
    BOOLEAN             bFalse = FALSE;
#if DBG
    ULONG               ReturnFlags;
#endif

    // BUGFIX: GitHub Issue #315 - NULL/invalid pointer check to prevent DRIVER_IRQL_NOT_LESS_OR_EQUAL (0xD1)
    // Race condition: NDIS may call this after FilterDetach if packets are in flight
    // Check for NULL OR invalid kernel pointers (e.g., 0x4 seen in crash dumps)
    // Valid x64 kernel pointers have upper bits set (>= 0xFFFF8000'00000000)
    if (pFilter == NULL || (ULONG_PTR)pFilter < FILTER_PTR_MIN_VALID || ((ULONG_PTR)pFilter & FILTER_PTR_KERNEL_MASK) != FILTER_PTR_KERNEL_MASK)
    {
        DEBUGP(DL_TRACE, "FilterReceiveNetBufferLists: INVALID FilterModuleContext=%p! Returning NBLs immediately.\n", pFilter);
        if (NDIS_TEST_RECEIVE_CAN_PEND(ReceiveFlags))
        {
            ULONG NblReturnFlags = 0;
            if (NDIS_TEST_RECEIVE_AT_DISPATCH_LEVEL(ReceiveFlags))
            {
                NDIS_SET_RETURN_FLAG(NblReturnFlags, NDIS_RETURN_FLAGS_DISPATCH_LEVEL);
            }
            NdisFReturnNetBufferLists(NULL, NetBufferLists, NblReturnFlags);
        }
        return;
    }

    DEBUGP(DL_TRACE, "===>ReceiveNetBufferList: NetBufferLists = %p.\n", NetBufferLists);
    do
    {

        DispatchLevel = NDIS_TEST_RECEIVE_AT_DISPATCH_LEVEL(ReceiveFlags);
#if DBG
        FILTER_ACQUIRE_LOCK(&pFilter->Lock, DispatchLevel);

        if (pFilter->State != FilterRunning)
        {
            FILTER_RELEASE_LOCK(&pFilter->Lock, DispatchLevel);

            if (NDIS_TEST_RECEIVE_CAN_PEND(ReceiveFlags))
            {
                ReturnFlags = 0;
                if (NDIS_TEST_RECEIVE_AT_DISPATCH_LEVEL(ReceiveFlags))
                {
                    NDIS_SET_RETURN_FLAG(ReturnFlags, NDIS_RETURN_FLAGS_DISPATCH_LEVEL);
                }

                NdisFReturnNetBufferLists(pFilter->FilterHandle, NetBufferLists, ReturnFlags);
            }
            break;
        }
        FILTER_RELEASE_LOCK(&pFilter->Lock, DispatchLevel);
#endif

        ASSERT(NumberOfNetBufferLists >= 1);

        //
        // Task 6a: PTP message detection for timestamp event generation
        // Implements: Issue #13 (REQ-F-TS-SUB-001) Task 6a - RX path
        //
        // Protocol-aware filtering (IEEE 1588-2019):
        //   - EtherType: 0x88F7 (PTP over Ethernet)
        //   - Message Types: Sync (0x0), Pdelay_Req (0x2), Pdelay_Resp (0x3),
        //                    Follow_Up (0x8), Pdelay_Resp_FU (0xA), Announce (0xB)
        //   - Read RX timestamp from RXSTMPL/H hardware registers
        //   - Post event via AvbPostTimestampEvent() (Task 6c)
        //
        // FIXED: Use per-adapter context instead of global g_AvbContext
        // This fixes multi-adapter support - each adapter has its own hardware context
        PAVB_DEVICE_CONTEXT avbCtx = (PAVB_DEVICE_CONTEXT)pFilter->AvbContext;
        if (avbCtx && avbCtx->hw_state >= AVB_HW_BAR_MAPPED) {
            PNET_BUFFER_LIST nbl = NetBufferLists;
            while (nbl) {
                PNET_BUFFER nb = NET_BUFFER_LIST_FIRST_NB(nbl);
                if (nb) {
                    ULONG dataLength = NET_BUFFER_DATA_LENGTH(nb);
                    if (dataLength >= 14 + 34) {  /* Ethernet (14) + PTP header (34) minimum */
                        PUCHAR pData = NULL;
                        __try {
                            pData = (PUCHAR)NdisGetDataBuffer(nb, 14 + 34, NULL, 1, 0);
                        } __except(EXCEPTION_EXECUTE_HANDLER) {
                            pData = NULL;
                        }
                        
                        if (pData) {
                            /* Parse Ethernet header */
                            USHORT etherType = ((USHORT)pData[12] << 8) | pData[13];
                            USHORT vlan_id = FILTER_VLAN_NONE;  /* No VLAN by default */
                            UCHAR pcp = 0xFF;         /* No PCP by default */
                            ULONG ptp_offset = 14;    /* PTP header offset after Ethernet */
                            
                            /* Debug: Show first few EtherTypes to verify packet parsing */
                            static int ethertype_count = 0;
                            if (ethertype_count < 30) {
                                DEBUGP(DL_TRACE, "!!! PKT #%d: EtherType=0x%04X, len=%u\n", 
                                       ethertype_count, etherType, dataLength);
                                ethertype_count++;
                            }
                            
                            /* Check for VLAN tag (0x8100) */
                            if (etherType == ETH_P_8021Q && dataLength >= 18 + 34) {
                                vlan_id = ((USHORT)pData[14] << 8) | pData[15];
                                pcp = (pData[14] >> 5) & 0x07;
                                vlan_id &= 0x0FFF;  /* Mask to 12 bits */
                                etherType = ((USHORT)pData[16] << 8) | pData[17];
                                ptp_offset = 18;  /* PTP after VLAN tag */
                                
                                if (ethertype_count <= 30) {
                                    DEBUGP(DL_TRACE, "!!! VLAN FOUND: ID=%u, PCP=%u, RealEtherType=0x%04X\n", 
                                           vlan_id, pcp, etherType);
                                }
                            }
                            
                            /* Check for PTP EtherType (0x88F7) */
                            if (etherType == ETHERTYPE_PTP) {
                                UCHAR messageType = pData[ptp_offset] & 0x0F;  /* Low 4 bits of transportSpecific/messageType */
                                
                                DEBUGP(DL_TRACE, "!!! PTP DETECTED: EtherType=0x%04X, msgType=0x%X, VLAN=%u, PCP=%u\n",
                                       etherType, messageType, vlan_id, pcp);
                                
                                /* Filter PTP message types (protocol-aware detection) */
                                /* IEEE 1588-2019 Table 36: "Values of messageType field"          */
                                /* Only EVENT messages (0x0-0x3) carry hardware RX timestamps.     */
                                /* General messages (0x8-0xD) do not cause a new RXSTMPL/H latch;  */
                                /* reading them would return a stale event-message timestamp.       */
                                BOOLEAN should_post = FALSE;
                                switch (messageType) {
                                    case 0x0:  /* Sync (Event) */
                                    case 0x1:  /* Delay_Req (Event) */
                                    case 0x2:  /* Pdelay_Req (Event) */
                                    case 0x3:  /* Pdelay_Resp (Event) */
                                        should_post = TRUE;
                                        DEBUGP(DL_TRACE, "!!! PTP MATCHED: msgType=0x%X will post event\n", messageType);
                                        break;
                                    case 0x8:  /* Follow_Up (General) - no HW timestamp */
                                    case 0x9:  /* Delay_Resp (General) - no HW timestamp */
                                    case 0xA:  /* Pdelay_Resp_Follow_Up (General) - no HW timestamp */
                                    case 0xB:  /* Announce (General) - no HW timestamp */
                                        DEBUGP(DL_TRACE, "PTP SKIP (general msg, no HW ts): msgType=0x%X\n", messageType);
                                        break;
                                    default:
                                        DEBUGP(DL_TRACE, "PTP SKIP: msgType=0x%X (Signaling/Management)\n", messageType);
                                        break;
                                }
                                
                                if (should_post) {
                                    /* Read RX timestamp from hardware using device HAL */
                                    avb_u64 timestamp_ns;
                                    device_t *dev;
                                    const intel_device_ops_t *ops;
                                    
                                    /* Initialize variables */
                                    timestamp_ns = 0;
                                    dev = &avbCtx->intel_device;
                                    ops = intel_get_device_ops(dev->device_type);

                                    /* Extract IEEE 1588 correctionField from PTP header bytes [8-15].
                                     * Big-endian signed 64-bit fixed-point, units of 2^-16 ns.
                                     * MUST be stored as INT64 (signed) — correctionField can be negative.
                                     * §9.5.9 of IEEE 1588-2019: correctionField = syncTimestamp correction
                                     * To convert to nanoseconds: (INT64)correction_field >> 16 */
                                    INT64 correction_field = 0;
                                    if (dataLength >= ptp_offset + 16) {
                                        /* Read 8 bytes big-endian from ptp_offset+8 */
                                        UINT64 cf_raw =
                                            ((UINT64)pData[ptp_offset + 8]  << 56) |
                                            ((UINT64)pData[ptp_offset + 9]  << 48) |
                                            ((UINT64)pData[ptp_offset + 10] << 40) |
                                            ((UINT64)pData[ptp_offset + 11] << 32) |
                                            ((UINT64)pData[ptp_offset + 12] << 24) |
                                            ((UINT64)pData[ptp_offset + 13] << 16) |
                                            ((UINT64)pData[ptp_offset + 14] <<  8) |
                                            ((UINT64)pData[ptp_offset + 15]);
                                        correction_field = (INT64)cf_raw;  /* reinterpret as signed */
                                    }
                                    
                                    /* Use device operations for HAL-compliant register access */
                                    if (ops && ops->read_rx_timestamp && 
                                        ops->read_rx_timestamp(dev, &timestamp_ns) == 0) {

                                        /* Apply IEEE 802.1AS ingress latency correction (timestampCorrectionPortDS).
                                         * ingress_latency_ns compensates for the delay between the physical
                                         * wire arrival and the hardware timestamp latch.  Default = 0. */
                                        timestamp_ns = (avb_u64)((INT64)timestamp_ns +
                                                       InterlockedCompareExchange64(
                                                           &avbCtx->ingress_latency_ns, 0, 0));
                                        
                                        DEBUGP(DL_TRACE, "!!! CALLING AvbPostTimestampEvent: ts=0x%llx, msgType=0x%x, cf=0x%llx\n", timestamp_ns, messageType, (UINT64)correction_field);
                                        
                                        /* Post event to matching subscriptions */
                                        /* FIXED: Pass per-adapter context for multi-adapter support */
                                        AvbPostTimestampEvent(
                                            avbCtx,  /* Pass THIS adapter's context */
                                            TS_EVENT_RX_TIMESTAMP,
                                            timestamp_ns,
                                            vlan_id,
                                            pcp,
                                            0,  /* queue - not easily available in filter driver */
                                            (avb_u16)dataLength,
                                            messageType,  /* Store PTP message type in trigger_source */
                                            correction_field
                                        );
                                    }
                                }
                            }
                        }
                    }
                }
                nbl = NET_BUFFER_LIST_NEXT_NBL(nbl);
            }
        }

        //
        // If you would like to drop a received packet, you must carefully
        // modify the NBL chain as follows:
        //
        //     if NDIS_TEST_RECEIVE_CANNOT_PEND(ReceiveFlags):
        //         For each NBL that is NOT dropped, temporarily unlink it from
        //         the linked list, and indicate it up alone with
        //         NdisFIndicateReceiveNetBufferLists and the
        //         NDIS_RECEIVE_FLAGS_RESOURCES flag set.  Then immediately
        //         relink the NBL back into the chain.  When all NBLs have been
        //         indicated up, you may return from this function.
        //     otherwise (NDIS_TEST_RECEIVE_CANNOT_PEND is FALSE):
        //         Divide the linked list of NBLs into two chains: one chain
        //         of packets to drop, and everything else in another chain.
        //         Return the first chain with NdisFReturnNetBufferLists, and
        //         indicate up the rest with NdisFIndicateReceiveNetBufferLists.
        //
        // Note: on the receive path for Ethernet packets, one NBL will have
        // exactly one NB.  So (assuming you are receiving on Ethernet, or are
        // attached above Native WiFi) you do not need to worry about dropping
        // one NB, but trying to indicate up the remaining NBs on the same NBL.
        // In other words, if the first NB should be dropped, drop the whole NBL.
        //

        //
        // If you would like to modify a packet, and can do so quickly, you may
        // do it here.  However, make sure you save enough information to undo
        // your modification in the FilterReturnNetBufferLists handler.
        //

        //
        // If necessary, queue the NetBufferLists in a local structure for later
        // processing.  However, do not queue them for "too long", or else the
        // system's performance may be degraded.  If you need to hold onto an
        // NBL for an unbounded amount of time, then allocate memory, perform a
        // deep copy, and return the original NBL.
        //

        if (pFilter->TrackReceives)
        {
            FILTER_ACQUIRE_LOCK(&pFilter->Lock, DispatchLevel);
            pFilter->OutstandingRcvs += NumberOfNetBufferLists;
            Ref = pFilter->OutstandingRcvs;

            FILTER_LOG_RCV_REF(1, pFilter, NetBufferLists, Ref);
            FILTER_RELEASE_LOCK(&pFilter->Lock, DispatchLevel);
        }

        NdisFIndicateReceiveNetBufferLists(
                   pFilter->FilterHandle,
                   NetBufferLists,
                   PortNumber,
                   NumberOfNetBufferLists,
                   ReceiveFlags);


        if (NDIS_TEST_RECEIVE_CANNOT_PEND(ReceiveFlags) &&
            pFilter->TrackReceives)
        {
            FILTER_ACQUIRE_LOCK(&pFilter->Lock, DispatchLevel);
            pFilter->OutstandingRcvs -= NumberOfNetBufferLists;
            Ref = pFilter->OutstandingRcvs;
            FILTER_LOG_RCV_REF(2, pFilter, NetBufferLists, Ref);
            FILTER_RELEASE_LOCK(&pFilter->Lock, DispatchLevel);
        }

    } while (bFalse);

    DEBUGP(DL_TRACE, "<===ReceiveNetBufferList: Flags = %8x.\n", ReceiveFlags);

}


_Use_decl_annotations_
VOID
FilterCancelSendNetBufferLists(
    NDIS_HANDLE             FilterModuleContext,
    PVOID                   CancelId
    )
/*++

Routine Description:

    This function cancels any NET_BUFFER_LISTs pended in the filter and then
    calls the NdisFCancelSendNetBufferLists to propagate the cancel operation.

    If your driver does not queue any send NBLs, you may omit this routine.
    NDIS will propagate the cancelation on your behalf more efficiently.

Arguments:

    FilterModuleContext      - our filter context area.
    CancelId                 - an identifier for all NBLs that should be dequeued

Return Value:

    None

*/
{
    PMS_FILTER  pFilter = (PMS_FILTER)FilterModuleContext;

    NdisFCancelSendNetBufferLists(pFilter->FilterHandle, CancelId);
}


_Use_decl_annotations_
NDIS_STATUS
FilterSetModuleOptions(
    NDIS_HANDLE             FilterModuleContext
    )
/*++

Routine Description:

    This function set the optional handlers for the filter

Arguments:

    FilterModuleContext: The FilterModuleContext given to NdisFSetAttributes

Return Value:

    NDIS_STATUS_SUCCESS
    NDIS_STATUS_RESOURCES
    NDIS_STATUS_FAILURE

--*/
{
   PMS_FILTER                               pFilter = (PMS_FILTER)FilterModuleContext;
   NDIS_FILTER_PARTIAL_CHARACTERISTICS      OptionalHandlers;
   NDIS_STATUS                              Status = NDIS_STATUS_SUCCESS;
   BOOLEAN                                  bFalse = FALSE;

   //
   // Demonstrate how to change send/receive handlers at runtime.
   //
   if (bFalse)
   {
       UINT      i;


       pFilter->CallsRestart++;

       i = pFilter->CallsRestart % 8;

       pFilter->TrackReceives = TRUE;
       pFilter->TrackSends = TRUE;

       NdisMoveMemory(&OptionalHandlers, &DefaultChars, sizeof(OptionalHandlers));
       OptionalHandlers.Header.Type = NDIS_OBJECT_TYPE_FILTER_PARTIAL_CHARACTERISTICS;
       OptionalHandlers.Header.Size = sizeof(OptionalHandlers);
       switch (i)
       {

            case 0:
                OptionalHandlers.ReceiveNetBufferListsHandler = NULL;
                pFilter->TrackReceives = FALSE;
                break;

            case 1:

                OptionalHandlers.ReturnNetBufferListsHandler = NULL;
                pFilter->TrackReceives = FALSE;
                break;

            case 2:
                OptionalHandlers.SendNetBufferListsHandler = NULL;
                pFilter->TrackSends = FALSE;
                break;

            case 3:
                OptionalHandlers.SendNetBufferListsCompleteHandler = NULL;
                pFilter->TrackSends = FALSE;
                break;

            case 4:
                OptionalHandlers.ReceiveNetBufferListsHandler = NULL;
                OptionalHandlers.ReturnNetBufferListsHandler = NULL;
                break;

            case 5:
                OptionalHandlers.SendNetBufferListsHandler = NULL;
                OptionalHandlers.SendNetBufferListsCompleteHandler = NULL;
                break;

            case 6:

                OptionalHandlers.ReceiveNetBufferListsHandler = NULL;
                OptionalHandlers.ReturnNetBufferListsHandler = NULL;
                OptionalHandlers.SendNetBufferListsHandler = NULL;
                OptionalHandlers.SendNetBufferListsCompleteHandler = NULL;
                break;

            case 7:
                break;
       }
       Status = NdisSetOptionalHandlers(pFilter->FilterHandle, (PNDIS_DRIVER_OPTIONAL_HANDLERS)&OptionalHandlers );
   }
   return Status;
}



_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_STATUS
filterDoInternalRequest(
    _In_ PMS_FILTER                   FilterModuleContext,
    _In_ NDIS_REQUEST_TYPE            RequestType,
    _In_ NDIS_OID                     Oid,
    _Inout_updates_bytes_to_(InformationBufferLength, *pBytesProcessed)
         PVOID                        InformationBuffer,
    _In_ ULONG                        InformationBufferLength,
    _In_opt_ ULONG                    OutputBufferLength,
    _In_ ULONG                        MethodId,
    _Out_ PULONG                      pBytesProcessed
    )
/*++

Routine Description:

    Utility routine that forms and sends an NDIS_OID_REQUEST to the
    miniport, waits for it to complete, and returns status
    to the caller.

    NOTE: this assumes that the calling routine ensures validity
    of the filter handle until this returns.

Arguments:

    FilterModuleContext - pointer to our filter module context
    RequestType - NdisRequest[Set|Query|method]Information
    Oid - the object being set/queried
    InformationBuffer - data for the request
    InformationBufferLength - length of the above
    OutputBufferLength  - valid only for method request
    MethodId - valid only for method request
    pBytesProcessed - place to return bytes read/written

Return Value:

    Status of the set/query request

--*/
{
    FILTER_REQUEST              FilterRequest;
    PNDIS_OID_REQUEST           NdisRequest = &FilterRequest.Request;
    NDIS_STATUS                 Status;

    ASSERT(pBytesProcessed != NULL);

    *pBytesProcessed = 0;

    NdisZeroMemory(NdisRequest, sizeof(NDIS_OID_REQUEST));

    NdisInitializeEvent(&FilterRequest.ReqEvent);

    NdisRequest->Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
    NdisRequest->Header.Revision = NDIS_OID_REQUEST_REVISION_1;
    NdisRequest->Header.Size = sizeof(NDIS_OID_REQUEST);
    NdisRequest->RequestType = RequestType;

    switch (RequestType)
    {
        case NdisRequestQueryInformation:
             NdisRequest->DATA.QUERY_INFORMATION.Oid = Oid;
             NdisRequest->DATA.QUERY_INFORMATION.InformationBuffer =
                                    InformationBuffer;
             NdisRequest->DATA.QUERY_INFORMATION.InformationBufferLength =
                                    InformationBufferLength;
            break;

        case NdisRequestSetInformation:
             NdisRequest->DATA.SET_INFORMATION.Oid = Oid;
             NdisRequest->DATA.SET_INFORMATION.InformationBuffer =
                                    InformationBuffer;
             NdisRequest->DATA.SET_INFORMATION.InformationBufferLength =
                                    InformationBufferLength;
            break;

        case NdisRequestMethod:
             NdisRequest->DATA.METHOD_INFORMATION.Oid = Oid;
             NdisRequest->DATA.METHOD_INFORMATION.MethodId = MethodId;
             NdisRequest->DATA.METHOD_INFORMATION.InformationBuffer =
                                    InformationBuffer;
             NdisRequest->DATA.METHOD_INFORMATION.InputBufferLength =
                                    InformationBufferLength;
             NdisRequest->DATA.METHOD_INFORMATION.OutputBufferLength = OutputBufferLength;
             break;



        default:

            ASSERTMSG("Invalid request type in filterDoInternalRequest",
                      FALSE);

            break;
    }

    NdisRequest->RequestId = (PVOID)FILTER_REQUEST_ID;


    Status = NdisFOidRequest(FilterModuleContext->FilterHandle,
                            NdisRequest);


    if (Status == NDIS_STATUS_PENDING)
    {
        NdisWaitEvent(&FilterRequest.ReqEvent, 0);
        Status = FilterRequest.Status;
    }



    if (Status == NDIS_STATUS_SUCCESS)
    {
        if (RequestType == NdisRequestSetInformation)
        {
            *pBytesProcessed = NdisRequest->DATA.SET_INFORMATION.BytesRead;
        }

        if (RequestType == NdisRequestQueryInformation)
        {
            *pBytesProcessed = NdisRequest->DATA.QUERY_INFORMATION.BytesWritten;
        }

        if (RequestType == NdisRequestMethod)
        {
            *pBytesProcessed = NdisRequest->DATA.METHOD_INFORMATION.BytesWritten;
        }

        //
        // The driver below should set the correct value to BytesWritten
        // or BytesRead. But now, we just truncate the value to InformationBufferLength
        //
        if (RequestType == NdisRequestMethod)
        {
            if (*pBytesProcessed > OutputBufferLength)
            {
                *pBytesProcessed = OutputBufferLength;
            }
        }
        else
        {

            if (*pBytesProcessed > InformationBufferLength)
            {
                *pBytesProcessed = InformationBufferLength;
            }
        }
    }


    return Status;
}

VOID
filterInternalRequestComplete(
    _In_ NDIS_HANDLE                  FilterModuleContext,
    _In_ PNDIS_OID_REQUEST            NdisRequest,
    _In_ NDIS_STATUS                  Status
    )
/*++

Routine Description:

    NDIS entry point indicating completion of a pended NDIS_OID_REQUEST.

Arguments:

    FilterModuleContext - pointer to our filter module context
    NdisRequest - pointer to NDIS request
    Status - status of request completion

Return Value:

    None

--*/
{
    PFILTER_REQUEST              FilterRequest;


    UNREFERENCED_PARAMETER(FilterModuleContext);

    //
    //  Get at the request context.
    //
    FilterRequest = CONTAINING_RECORD(NdisRequest, FILTER_REQUEST, Request);

    //
    //  Save away the completion status.
    //
    FilterRequest->Status = Status;

    //
    //  Wake up the thread blocked for this request to complete.
    //
    NdisSetEvent(&FilterRequest->ReqEvent);
}

