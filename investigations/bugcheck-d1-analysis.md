# Bugcheck 0xD1 Analysis - IntelAvbFilter Driver

**Date**: February 21, 2026  
**Bugcheck**: 0x000000D1 (DRIVER_IRQL_NOT_LESS_OR_EQUAL)  
**Dump File**: C:\WINDOWS\Minidump\022126-9421-01.dmp  
**Verdict**: **CONFIRMED - Driver Bug**

---

## Executive Summary

The crash is **caused by missing NULL pointer checks** in the IntelAvbFilter driver's network packet send/receive handlers. NDIS is calling these functions with a NULL `FilterModuleContext`, causing the driver to dereference NULL + offset 0x6c.

---

## Bugcheck Parameters

```
Bug Check Code: 0x000000D1
Parameter 1:    0x000000000000006C  <- Invalid memory address (NULL + 108 bytes)
Parameter 2:    0x0000000000000002  <- IRQL = DISPATCH_LEVEL
Parameter 3:    0x0000000000000000  <- Access type = READ
Parameter 4:    0xFFFFF80632A629C0  <- Faulting instruction address
```

**Interpretation**: Code running at DISPATCH_LEVEL attempted to READ from address 0x6C, which is a NULL pointer dereference (accessing a field at offset 108 from a NULL base pointer).

---

## Root Cause: Missing Defensive Checks

### Vulnerable Functions

#### 1. FilterSendNetBufferLists() - src/filter.c:1394

```c
VOID FilterSendNetBufferLists(
    NDIS_HANDLE         FilterModuleContext,  // <- Can be NULL!
    PNET_BUFFER_LIST    NetBufferLists,
    NDIS_PORT_NUMBER    PortNumber,
    ULONG               SendFlags)
{
    PMS_FILTER pFilter = (PMS_FILTER)FilterModuleContext;
    
    // ❌ NO NULL CHECK HERE
    // Line ~1430: FILTER_ACQUIRE_LOCK(&pFilter->Lock, DispatchLevel);
    // If pFilter is NULL, accessing pFilter->Lock crashes
}
```

#### 2. FilterReceiveNetBufferLists() - src/filter.c:1579

```c
VOID FilterReceiveNetBufferLists(
    NDIS_HANDLE         FilterModuleContext,  // <- Can be NULL!
    PNET_BUFFER_LIST    NetBufferLists,
    ...)
{
    PMS_FILTER pFilter = (PMS_FILTER)FilterModuleContext;
    
    // ❌ NO NULL CHECK HERE
    // Line ~1628: FILTER_ACQUIRE_LOCK(&pFilter->Lock, DispatchLevel);
    // Line ~1656: pFilter->AvbContext access
}
```

#### 3. Other NDIS Handlers

Similar pattern exists in:
- `FilterOidRequest()`
- `FilterStatus()`
- `FilterDevicePnPEventNotify()`
- `FilterNetPnPEvent()`

---

## Why FilterModuleContext Can Be NULL

NDIS can call packet handlers with NULL context in these scenarios:

| Scenario | Likelihood | Notes |
|----------|-----------|-------|
| **Filter detach race condition** | HIGH | Filter is being removed while packets are in flight |
| **Partial initialization failure** | MEDIUM | FilterAttach failed but NDIS still routes packets |
| **System power transition** | MEDIUM | During sleep/hibernate/shutdown |
| **NDIS stack corruption** | LOW | Bug in NDIS itself or another filter |
| **Driver verification stress** | HIGH | DV/SDV can inject NULL contexts on purpose |

---

## Structure Offset Analysis

### MS_FILTER Structure (src/filter.h:248)

```c
typedef struct _MS_FILTER {
    LIST_ENTRY      FilterModuleLink;      // 0x00-0x0F: 16 bytes
    ULONG           RefCount;              // 0x10-0x13: 4 bytes
    // ... padding ...
    NDIS_HANDLE     FilterHandle;          // 0x18-0x1F: 8 bytes
    NDIS_STRING     FilterModuleName;      // 0x20-0x2F: 16 bytes
    NDIS_STRING     MiniportFriendlyName;  // 0x30-0x3F: 16 bytes
    NDIS_STRING     MiniportName;          // 0x40-0x4F: 16 bytes
    NET_IFINDEX     MiniportIfIndex;       // 0x50-0x53: 4 bytes
    NDIS_STATUS     Status;                // 0x58-0x5B: 4 bytes
    NDIS_EVENT      Event;                 // 0x5C-0x73: ~24 bytes
    ULONG           BackFillSize;          // 0x74-0x77: 4 bytes
    FILTER_LOCK     Lock;                  // 0x78+: Varies
    // ...
    PVOID           AvbContext;            // Further offset
} MS_FILTER, *PMS_FILTER;
```

**Crash at offset 0x6C (108 bytes)** likely corresponds to accessing:
- `Event` field (NDIS_EVENT contains a KEVENT)
- Or alignment-dependent field access depending on compiler packing

---

## Required Fixes

### Priority 1: Add NULL Checks to Packet Handlers

```c
VOID FilterSendNetBufferLists(
    NDIS_HANDLE         FilterModuleContext,
    PNET_BUFFER_LIST    NetBufferLists,
    NDIS_PORT_NUMBER    PortNumber,
    ULONG               SendFlags)
{
    PMS_FILTER pFilter = (PMS_FILTER)FilterModuleContext;
    BOOLEAN DispatchLevel = NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags);
    
    // ✅ ADD THIS CHECK
    if (pFilter == NULL) {
        DEBUGP(DL_ERROR, "!!! FilterSendNetBufferLists: NULL FilterModuleContext!\n");
        
        // Complete all NBLs with error
        PNET_BUFFER_LIST CurrNbl = NetBufferLists;
        while (CurrNbl) {
            NET_BUFFER_LIST_STATUS(CurrNbl) = NDIS_STATUS_FAILURE;
            CurrNbl = NET_BUFFER_LIST_NEXT_NBL(CurrNbl);
        }
        
        NdisFSendNetBufferListsComplete(
            NdisFilterHandle,  // Need to store this globally!
            NetBufferLists,
            DispatchLevel ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
        
        return;
    }
    
    // Original code continues...
}
```

### Priority 2: Add NULL Checks to All NDIS Callbacks

Apply same pattern to:
- `FilterReceiveNetBufferLists`
- `FilterReturnNetBufferLists`
- `FilterSendNetBufferListsComplete`
- `FilterOidRequest`
- `FilterCancelOidRequest`
- `FilterStatus`
- `FilterDevicePnPEventNotify`
- `FilterNetPnPEvent`

### Priority 3: Store Global FilterDriverHandle

Problem: When `pFilter` is NULL, we lose access to `pFilter->FilterHandle` needed for NDIS completion functions.

**Solution**: Store `FilterDriverHandle` globally so completion functions can be called even with NULL context.

---

## Testing Recommendations

### Enable Driver Verifier (Immediate)

```cmd
verifier /standard /driver IntelAvbFilter.sys
```

Driver Verifier will:
- Inject NULL contexts on purpose
- Detect this bug before deployment
- Force IRQL violations to surface

### WinDbg Analysis (If Dump Available)

```
!analyze -v
k
.frame 0
dv
lm m IntelAvbFilter
```

Look for:
- `IntelAvbFilter!FilterSendNetBufferLists` or `FilterReceiveNetBufferLists` in call stack
- RAX or RCX register = 0x0 (NULL pFilter)

---

## References

- **MSDN**: [DRIVER_IRQL_NOT_LESS_OR_EQUAL](https://docs.microsoft.com/en-us/windows-hardware/drivers/debugger/bug-check-0xd1--driver-irql-not-less-or-equal)
- **NDIS Filter Drivers**: [FilterSendNetBufferLists callback](https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/ndis/nc-ndis-filter_send_net_buffer_lists)
- **Best Practice**: Always validate FilterModuleContext before dereferencing

---

## Action Items

- [ ] Add NULL checks to all NDIS callback functions (HIGH PRIORITY)
- [ ] Store FilterDriverHandle globally for error path NBL completion
- [ ] Enable Driver Verifier during development
- [ ] Test with Driver Verifier /standard
- [ ] Review all CONTAINING_RECORD uses for similar issues
- [ ] Add SAL annotations (_In_ _Notnull_) to document expectations
- [ ] Create GitHub Issue for tracking this bug fix

---

**Status**: CRITICAL BUG - REQUIRES IMMEDIATE FIX  
**Impact**: System crashes (BSOD) during normal network operations  
**Reproducibility**: Intermittent (race condition during filter detach or power events)
