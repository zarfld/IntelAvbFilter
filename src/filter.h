/*++

Module Name:

    Filter.h

Abstract:

    This module contains all prototypes and macros for filter code.

--*/
#ifndef _FILT_H
#define _FILT_H

#pragma warning(disable:28930) // Unused assignment of pointer, by design in samples
#pragma warning(disable:28931) // Unused assignment of variable, by design in samples

// AVB Filter Driver pool tags for memory leak tracking
#define FILTER_REQUEST_ID          'AvbR'  // AVB Request
#define FILTER_ALLOC_TAG           'AvbM'  // AVB Memory
#define FILTER_TAG                 'AvbF'  // AVB Filter

// Using NDIS 6.0 for maximum Windows compatibility  
// This ensures the driver works on Windows 7/8/10/11
#define FILTER_MAJOR_NDIS_VERSION   6
#define FILTER_MINOR_NDIS_VERSION   0

// Ensure we have the right NDIS support flags
#ifndef NDIS_SUPPORT_NDIS60
#define NDIS_SUPPORT_NDIS60 1
#endif

//
// NDIS 6.82+ Hardware Timestamping Support (for TX timestamp capture)
// These are defined in ntddndis.h in Windows SDK 10.0.22621.0+
// We only add fallback definitions if not already present.
//

// NBL Info type for hardware timestamp requests (likely already defined)
// NdisHardwareTimestampInfo: DO NOT define a fallback value here.
// The WDK's ndis/nbltimestamp.h (NDIS 6.82+) uses NetBufferListInfoReserved3
// for the NBL timestamp, not a constant named NdisHardwareTimestampInfo.
// Our old fallback of 30 caused a 32-byte OOB write (MaxNetBufferListInfo==26
// on AMD64/Win10-26100) → KERNEL_MODE_HEAP_CORRUPTION BugCheck 0x13A_17.
// The TX timestamp path uses TSYNCTXCTL + IOCTL_AVB_GET_TX_TIMESTAMP (FIFO poll)
// and requires no NBL OOB fields; this define is dead code and was removed.

// ---- NDIS 6.82 TaggedTransmitHw backfill (for NDIS 6.30 filter builds) ----
//
// The filter declares NDIS 6.30 for maximum Windows compatibility, but targets
// Win10/11 hardware where igc.sys (NDIS 6.82+) supports per-packet TX timestamps.
//
// NDIS_NBL_FLAGS_CAPTURE_TIMESTAMP_ON_TRANSMIT (nbl.h, guarded by NDIS_SUPPORT_NDIS682):
//   Setting this bit in NBL.NblFlags before NdisFSendNetBufferLists requests the
//   miniport to latch a hardware TX timestamp and return it on send-complete.
//
// AVB_TX_TIMESTAMP_SLOT (nblinfo.h, guarded by NDIS_SUPPORT_NDIS680):
//   Index into NET_BUFFER_LIST.NetBufferListInfo[] where the miniport writes the
//   ULONG64 timestamp on send-complete. This equals NetBufferListInfoReserved3 = 26
//   on AMD64/Win11 (after GftFlowEntryId=25, GftOffloadInformation=24 from NDIS650).
//   Derivation: 0-10 base, 11-12 NDIS61, 13-17 NDIS620, 18-20 NDIS630 AMD64,
//               21-23 NDIS630, 24-25 NDIS650 AMD64, 26 NDIS680 AMD64 (_WIN64).
//   MaxNetBufferListInfo = 27 on Win11 AMD64 → valid indexed range is [0..26].
//   The old NdisHardwareTimestampInfo = 30 OOB bug was at 30 > 26 → BSOD 0x13A_17.
//
#ifndef NDIS_NBL_FLAGS_CAPTURE_TIMESTAMP_ON_TRANSMIT
#define NDIS_NBL_FLAGS_CAPTURE_TIMESTAMP_ON_TRANSMIT  0x00010000UL
#endif

#if NDIS_SUPPORT_NDIS680
// Enum value is already visible — convert to int to use as array index
#define AVB_TX_TIMESTAMP_SLOT  ((int)(NetBufferListInfoReserved3))
#else
// NDIS 6.80 enum not available; use the known AMD64/Win11 numeric value
#define AVB_TX_TIMESTAMP_SLOT  26
#endif
// ---- End NDIS 6.82 TaggedTransmitHw backfill ----

// PTP EtherType for packet detection (IEEE 1588)
#define ETHERTYPE_PTP   0x88F7  // PTP over Ethernet (Layer 2)

// 802.1Q VLAN EtherType
#define ETH_P_8021Q     0x8100

// VLAN "not set" sentinel (16-bit, all-ones)
#define FILTER_VLAN_NONE  0xFFFF

// Pointer validation thresholds (GitHub Issue #315 – crash prevention)
// Any valid kernel pointer on x64 must be >= FILTER_PTR_MIN_VALID and
// have the upper 16 bits equal to FILTER_PTR_KERNEL_MASK.
#define FILTER_PTR_MIN_VALID    0x10000
#define FILTER_PTR_KERNEL_MASK  0xFFFF000000000000ULL

// Circular log buffer capacity (64K entries)
#define FILTER_LOG_ARRAY_SIZE   0x10000

//
// Global variables
//
extern NDIS_HANDLE         FilterDriverHandle; // NDIS handle for filter driver
extern NDIS_HANDLE         FilterDriverObject;
extern NDIS_HANDLE         NdisFilterDeviceHandle;
extern PDEVICE_OBJECT      NdisDeviceObject;

extern FILTER_LOCK         FilterListLock;
extern LIST_ENTRY          FilterModuleList;

/* ETW provider registration handle (DriverEntry → FilterUnload)
 * Implements: #65 (REQ-F-EVENT-LOG-001) */
extern REGHANDLE           g_EtwHandle;

#define FILTER_FRIENDLY_NAME        L"IntelAvbFilter NDIS LightWeight Filter"
#define FILTER_UNIQUE_NAME          L"{3f74ae86-14f9-4e79-9445-5b1e52ccd192}" //unique name, quid name
#define FILTER_SERVICE_NAME         L"IntelAvbFilter"

//
// The filter needs to handle IOCTLs
//
#define LINKNAME_STRING             L"\\DosDevices\\IntelAvbFilter"
#define NTDEVICE_STRING             L"\\Device\\IntelAvbFilter"

//
 // Types and macros to manipulate packet queue
//
typedef struct _QUEUE_ENTRY
{
    struct _QUEUE_ENTRY * Next;
}QUEUE_ENTRY, *PQUEUE_ENTRY;

typedef struct _QUEUE_HEADER
{
    PQUEUE_ENTRY     Head;
    PQUEUE_ENTRY     Tail;
} QUEUE_HEADER, PQUEUE_HEADER;


#if TRACK_RECEIVES
UINT         filterLogReceiveRefIndex = 0;
ULONG_PTR    filterLogReceiveRef[FILTER_LOG_ARRAY_SIZE];
#endif

#if TRACK_SENDS
UINT         filterLogSendRefIndex = 0;
ULONG_PTR    filterLogSendRef[FILTER_LOG_ARRAY_SIZE];
#endif

#if TRACK_RECEIVES
#define   FILTER_LOG_RCV_REF(_O, _Instance, _NetBufferList, _Ref)    \
    {\
        filterLogReceiveRef[filterLogReceiveRefIndex++] = (ULONG_PTR)(_O); \
        filterLogReceiveRef[filterLogReceiveRefIndex++] = (ULONG_PTR)(_Instance); \
        filterLogReceiveRef[filterLogReceiveRefIndex++] = (ULONG_PTR)(_NetBufferList); \
        filterLogReceiveRef[filterLogReceiveRefIndex++] = (ULONG_PTR)(_Ref); \
        if (filterLogReceiveRefIndex >= (FILTER_LOG_ARRAY_SIZE - 5))                    \
        {                                                              \
            filterLogReceiveRefIndex = 0;                                 \
        }                                                              \
    }
#else
#define   FILTER_LOG_RCV_REF(_O, _Instance, _NetBufferList, _Ref)
#endif

#if TRACK_SENDS
#define   FILTER_LOG_SEND_REF(_O, _Instance, _NetBufferList, _Ref)    \
    {\
        filterLogSendRef[filterLogSendRefIndex++] = (ULONG_PTR)(_O); \
        filterLogSendRef[filterLogSendRefIndex++] = (ULONG_PTR)(_Instance); \
        filterLogSendRef[filterLogSendRefIndex++] = (ULONG_PTR)(_NetBufferList); \
        filterLogSendRef[filterLogSendRefIndex++] = (ULONG_PTR)(_Ref); \
        if (filterLogSendRefIndex >= (FILTER_LOG_ARRAY_SIZE - 5))                    \
        {                                                              \
            filterLogSendRefIndex = 0;                                 \
        }                                                              \
    }

#else
#define   FILTER_LOG_SEND_REF(_O, _Instance, _NetBufferList, _Ref)
#endif


//
// DEBUG related macros.
//
#if DBG
#define FILTER_ALLOC_MEM(_NdisHandle, _Size)    \
    filterAuditAllocMem(                        \
            _NdisHandle,                        \
           _Size,                               \
           __FILENUMBER,                        \
           __LINE__);

#define FILTER_FREE_MEM(_pMem)                  \
    filterAuditFreeMem(_pMem);

#else
#define FILTER_ALLOC_MEM(_NdisHandle, _Size)     \
    NdisAllocateMemoryWithTagPriority(_NdisHandle, _Size, FILTER_ALLOC_TAG, LowPoolPriority)

#define FILTER_FREE_MEM(_pMem)    NdisFreeMemory(_pMem, 0, 0)

#endif //DBG

#if DBG_SPIN_LOCK
#define FILTER_INIT_LOCK(_pLock)                          \
    filterAllocateSpinLock(_pLock, __FILENUMBER, __LINE__)

#define FILTER_FREE_LOCK(_pLock)       filterFreeSpinLock(_pLock)


#define FILTER_ACQUIRE_LOCK(_pLock, DispatchLevel)  \
    filterAcquireSpinLock(_pLock, __FILENUMBER, __LINE__, DisaptchLevel)

#define FILTER_RELEASE_LOCK(_pLock, DispatchLevel)      \
    filterReleaseSpinLock(_pLock, __FILENUMBER, __LINE__, DispatchLevel)

#else
#define FILTER_INIT_LOCK(_pLock)      NdisAllocateSpinLock(_pLock)

#define FILTER_FREE_LOCK(_pLock)      NdisFreeSpinLock(_pLock)

#define FILTER_ACQUIRE_LOCK(_pLock, DispatchLevel)              \
    {                                                           \
        if (DispatchLevel)                                      \
        {                                                       \
            NdisDprAcquireSpinLock(_pLock);                     \
        }                                                       \
        else                                                    \
        {                                                       \
            NdisAcquireSpinLock(_pLock);                        \
        }                                                       \
    }

#define FILTER_RELEASE_LOCK(_pLock, DispatchLevel)              \
    {                                                           \
        if (DispatchLevel)                                      \
        {                                                       \
            NdisDprReleaseSpinLock(_pLock);                     \
        }                                                       \
        else                                                    \
        {                                                       \
            NdisReleaseSpinLock(_pLock);                        \
        }                                                       \
    }
#endif //DBG_SPIN_LOCK


#define NET_BUFFER_LIST_LINK_TO_ENTRY(_pNBL)    ((PQUEUE_ENTRY)(NET_BUFFER_LIST_NEXT_NBL(_pNBL)))
#define ENTRY_TO_NET_BUFFER_LIST(_pEnt)         (CONTAINING_RECORD((_pEnt), NET_BUFFER_LIST, Next))

#define InitializeQueueHeader(_QueueHeader)             \
{                                                       \
    (_QueueHeader)->Head = (_QueueHeader)->Tail = NULL; \
}

//
// Macros for queue operations
//
#define IsQueueEmpty(_QueueHeader)      ((_QueueHeader)->Head == NULL)

#define RemoveHeadQueue(_QueueHeader)                   \
    (_QueueHeader)->Head;                               \
    {                                                   \
        PQUEUE_ENTRY pNext;                             \
        ASSERT((_QueueHeader)->Head);                   \
        pNext = (_QueueHeader)->Head->Next;             \
        (_QueueHeader)->Head = pNext;                   \
        if (pNext == NULL)                              \
            (_QueueHeader)->Tail = NULL;                \
    }

#define InsertHeadQueue(_QueueHeader, _QueueEntry)                  \
    {                                                               \
        ((PQUEUE_ENTRY)(_QueueEntry))->Next = (_QueueHeader)->Head; \
        (_QueueHeader)->Head = (PQUEUE_ENTRY)(_QueueEntry);         \
        if ((_QueueHeader)->Tail == NULL)                           \
            (_QueueHeader)->Tail = (PQUEUE_ENTRY)(_QueueEntry);     \
    }

#define InsertTailQueue(_QueueHeader, _QueueEntry)                      \
    {                                                                   \
        ((PQUEUE_ENTRY)(_QueueEntry))->Next = NULL;                     \
        if ((_QueueHeader)->Tail)                                       \
            (_QueueHeader)->Tail->Next = (PQUEUE_ENTRY)(_QueueEntry);   \
        else                                                            \
            (_QueueHeader)->Head = (PQUEUE_ENTRY)(_QueueEntry);         \
        (_QueueHeader)->Tail = (PQUEUE_ENTRY)(_QueueEntry);             \
    }


//
 // Enum of filter's states
// Filter can only be in one state at one time
//
typedef enum _FILTER_STATE
{
    FilterStateUnspecified,
    FilterInitialized,
    FilterPausing,
    FilterPaused,
    FilterRunning,
    FilterRestarting,
    FilterDetaching
} FILTER_STATE;


typedef struct _FILTER_REQUEST
{
    NDIS_OID_REQUEST       Request;
    NDIS_EVENT             ReqEvent;
    NDIS_STATUS            Status;
} FILTER_REQUEST, *PFILTER_REQUEST;

//
// Define the filter struct
//
typedef struct _MS_FILTER
{
    LIST_ENTRY                     FilterModuleLink;
    //Reference to this filter
    ULONG                           RefCount;

    NDIS_HANDLE                     FilterHandle;
    NDIS_STRING                     FilterModuleName;
    NDIS_STRING                     MiniportFriendlyName;
    NDIS_STRING                     MiniportName;
    NET_IFINDEX                     MiniportIfIndex;

    NDIS_STATUS                     Status;
    NDIS_EVENT                      Event;
    ULONG                           BackFillSize;
    FILTER_LOCK                     Lock;    // Lock for protection of state and outstanding sends and recvs

    FILTER_STATE                    State;   // Which state the filter is in
    ULONG                           OutstandingSends;
    ULONG                           OutstandingRequest;
    ULONG                           OutstandingRcvs;
    FILTER_LOCK                     SendLock;
    FILTER_LOCK                     RcvLock;
    QUEUE_HEADER                    SendNBLQueue;
    QUEUE_HEADER                    RcvNBLQueue;


    NDIS_STRING                     FilterName;
    ULONG                           CallsRestart;
    BOOLEAN                         TrackReceives;
    BOOLEAN                         TrackSends;
#if DBG
    BOOLEAN                         bIndicating;
#endif

    PNDIS_OID_REQUEST               PendingOidRequest;

    // AVB Integration
    PVOID                           AvbContext;      // Points to AVB_DEVICE_CONTEXT
    BOOLEAN                         HwTimestampEnabled;  // TRUE if HW timestamping configured via OID_TIMESTAMP_CONFIG

}MS_FILTER, * PMS_FILTER;


typedef struct _FILTER_DEVICE_EXTENSION
{
    ULONG            Signature;
    NDIS_HANDLE      Handle;
} FILTER_DEVICE_EXTENSION, *PFILTER_DEVICE_EXTENSION;


#define FILTER_READY_TO_PAUSE(_Filter)      \
    ((_Filter)->State == FilterPausing)

//
// The driver should maintain a list of NDIS filter handles
//
typedef struct _FL_NDIS_FILTER_LIST
{
    LIST_ENTRY              Link;
    NDIS_HANDLE             ContextHandle;
    NDIS_STRING             FilterInstanceName;
} FL_NDIS_FILTER_LIST, *PFL_NDIS_FILTER_LIST;

//
// The context inside a cloned request
//
typedef struct _NDIS_OID_REQUEST *FILTER_REQUEST_CONTEXT,**PFILTER_REQUEST_CONTEXT;


//
// function prototypes
//

EXTERN_C_START

DRIVER_INITIALIZE DriverEntry;

FILTER_SET_OPTIONS FilterRegisterOptions;

FILTER_ATTACH FilterAttach;

FILTER_DETACH FilterDetach;

DRIVER_UNLOAD FilterUnload;

FILTER_RESTART FilterRestart;

FILTER_PAUSE FilterPause;

FILTER_OID_REQUEST FilterOidRequest;

FILTER_CANCEL_OID_REQUEST FilterCancelOidRequest;

FILTER_STATUS FilterStatus;

FILTER_DEVICE_PNP_EVENT_NOTIFY FilterDevicePnPEventNotify;

FILTER_NET_PNP_EVENT FilterNetPnPEvent;

FILTER_OID_REQUEST_COMPLETE FilterOidRequestComplete;

FILTER_SEND_NET_BUFFER_LISTS FilterSendNetBufferLists;

FILTER_RETURN_NET_BUFFER_LISTS FilterReturnNetBufferLists;

FILTER_SEND_NET_BUFFER_LISTS_COMPLETE FilterSendNetBufferListsComplete;

FILTER_RECEIVE_NET_BUFFER_LISTS FilterReceiveNetBufferLists;

FILTER_CANCEL_SEND_NET_BUFFER_LISTS FilterCancelSendNetBufferLists;

FILTER_SET_MODULE_OPTIONS FilterSetModuleOptions;


_IRQL_requires_max_(PASSIVE_LEVEL)
NDIS_STATUS
IntelAvbFilterRegisterDevice(
    VOID
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
IntelAvbFilterDeregisterDevice(
    VOID
    );

DRIVER_DISPATCH IntelAvbFilterDispatch;

DRIVER_DISPATCH IntelAvbFilterDeviceIoControl;

_IRQL_requires_max_(DISPATCH_LEVEL)
PMS_FILTER
filterFindFilterModule(
    _In_reads_bytes_(BufferLength)
         PUCHAR                   Buffer,
    _In_ ULONG                    BufferLength
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
PMS_FILTER
AvbFindIntelFilterModule(
    VOID
    );

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
    );

VOID
filterInternalRequestComplete(
    _In_ NDIS_HANDLE                  FilterModuleContext,
    _In_ PNDIS_OID_REQUEST            NdisRequest,
    _In_ NDIS_STATUS                  Status
    );

EXTERN_C_END

#endif  //_FILT_H


