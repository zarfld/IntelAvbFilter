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

// Using NDIS 6.20 for better Windows 7/8/10/11 compatibility  
// This provides good balance of features and compatibility
#define FILTER_MAJOR_NDIS_VERSION   6

// Default to NDIS 6.20 which has excellent compatibility
#ifndef FILTER_MINOR_NDIS_VERSION
#define FILTER_MINOR_NDIS_VERSION   20
#endif

// Ensure we're targeting the right NDIS revision
#if !defined(NDIS_SUPPORT_NDIS61)
#define NDIS_SUPPORT_NDIS61 1
#endif

//
// Global variables
//
extern NDIS_HANDLE         FilterDriverHandle; // NDIS handle for filter driver
extern NDIS_HANDLE         FilterDriverObject;
extern NDIS_HANDLE         NdisFilterDeviceHandle;
extern PDEVICE_OBJECT      NdisDeviceObject;

extern FILTER_LOCK         FilterListLock;
extern LIST_ENTRY          FilterModuleList;

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
ULONG_PTR    filterLogReceiveRef[0x10000];
#endif

#if TRACK_SENDS
UINT         filterLogSendRefIndex = 0;
ULONG_PTR    filterLogSendRef[0x10000];
#endif

#if TRACK_RECEIVES
#define   FILTER_LOG_RCV_REF(_O, _Instance, _NetBufferList, _Ref)    \
    {\
        filterLogReceiveRef[filterLogReceiveRefIndex++] = (ULONG_PTR)(_O); \
        filterLogReceiveRef[filterLogReceiveRefIndex++] = (ULONG_PTR)(_Instance); \
        filterLogReceiveRef[filterLogReceiveRefIndex++] = (ULONG_PTR)(_NetBufferList); \
        filterLogReceiveRef[filterLogReceiveRefIndex++] = (ULONG_PTR)(_Ref); \
        if (filterLogReceiveRefIndex >= (0x10000 - 5))                    \
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
        if (filterLogSendRefIndex >= (0x10000 - 5))                    \
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


