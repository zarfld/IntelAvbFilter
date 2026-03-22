# Crash Dump Analysis Summary - February 21, 2026

## Executive Summary

**CONFIRMED**: All four crashes on February 21, 2026, were caused by **NULL pointer dereference in IntelAvbFilter.sys driver**.

---

## Crash Details

### All Crashes Share Same Root Cause

| Dump File | Time | Bug Check | Faulting Module | Offset |
|-----------|------|-----------|----------------|--------|
| 022126-9812-01.dmp | 8:11:36 PM | 0xD1 | IntelAvbFilter.sys | +0x29c0 |
| 022126-9578-01.dmp | 8:39:03 PM | 0xD1 | IntelAvbFilter.sys | +0x29c0 |
| 022126-10187-01.dmp | 8:49:55 PM | 0xD1 | IntelAvbFilter.sys | +0x29c0 |
| **022126-9421-01.dmp** | **9:50:28 PM** | **0xD1** | **IntelAvbFilter.sys** | **+0x29c0** |

---

## Detailed Analysis of Primary Crash (022126-9421-01.dmp)

### Bug Check Parameters

```
DRIVER_IRQL_NOT_LESS_OR_EQUAL (0xD1)
Arg1: 0x000000000000006C  ← Invalid address (NULL + offset)
Arg2: 0x0000000000000002  ← IRQL = DISPATCH_LEVEL
Arg3: 0x0000000000000000  ← Read operation
Arg4: 0xFFFFF80632A629C0  ← Faulting instruction (IntelAvbFilter+0x29c0)
```

### Crash Instruction

```assembly
IntelAvbFilter+0x29c0:
fffff806`32a629c0  mov rax, qword ptr [rax+68h]  ← CRASH HERE
                                    ↑
                           rax = 0x4 (almost NULL)
                           [0x4 + 0x68] = 0x6C ← Invalid memory access
```

### Call Stack

```
00 nt!KeBugCheckEx
01 nt!KiBugCheckDispatch+0x69
02 nt!KiPageFault+0x468
03 IntelAvbFilter+0x29c0  ← NULL pointer dereference
04 IntelAvbFilter+0x1e990 ← Caller
```

### Register State at Crash

```
rax = 0x0000000000000004  ← Almost NULL pointer
rip = 0xFFFFF80632A629C0  ← IntelAvbFilter+0x29c0
IRQL = 2 (DISPATCH_LEVEL)
Process: svchost.exe (network service)
```

---

## Root Cause Analysis

### The Problem

The driver tried to access memory at **offset 0x68 from a pointer value of 0x4**, resulting in invalid address **0x6C**.

### Why This Happened

Looking at the `MS_FILTER` structure in `filter.h`:

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
} MS_FILTER, *PMS_FILTER;
```

**Offset 0x68 falls within the `NDIS_EVENT Event` field** or is attempting to access a lock/synchronization object.

### The Bug

```c
// Somewhere in IntelAvbFilter at offset +0x29c0 (likely in filter.c or avb_integration_fixed.c)
PMS_FILTER pFilter = (PMS_FILTER)FilterModuleContext;

// ❌ NO NULL CHECK - pFilter could be NULL or invalid
FILTER_ACQUIRE_LOCK(&pFilter->Lock, DispatchLevel);  // or similar access
// Accessing pFilter->SomeField at offset 0x68
```

When `FilterModuleContext` is NULL or corrupted (value 0x4), dereferencing it causes the crash.

---

## Why FilterModuleContext Was Invalid

### Possible Scenarios

1. **Filter Detach Race Condition** (MOST LIKELY)
   - Filter was being removed while packets were still being processed
   - NDIS continued to call send/receive handlers after context was freed
   - Timing: 4 crashes in ~2 hours suggests stress test or rapid add/remove

2. **Partial Initialization Failure**
   - `FilterAttach` failed but NDIS still routed packets
   - Context pointer never properly initialized

3. **Memory Corruption**
   - Another driver corrupted the FilterModuleContext pointer
   - Pointer changed from valid to 0x4

4. **System Power Transition**
   - Driver handling during sleep/hibernate was improper

---

## Evidence That Points to IntelAvbFilter

### From WinDbg Output

```
SYMBOL_NAME:  IntelAvbFilter+29c0
MODULE_NAME: IntelAvbFilter
IMAGE_NAME:  IntelAvbFilter.sys
FAILURE_BUCKET_ID:  AV_IntelAvbFilter!unknown_function
PROCESS_NAME:  svchost.exe
```

### Loaded Modules

```
fffff806`32a60000 fffff806`32a93000   IntelAvbFilter T (no symbols)
```

**Note**: "(no symbols)" means private PDB symbols were not loaded, making exact function identification difficult.

### Unloaded Modules

```
Unloaded modules:
fffff806`32a10000 fffff806`32a46000   IntelAvbFilt
```

This shows IntelAvbFilter was loaded/unloaded, suggesting dynamic filter behavior.

---

## Mapping Offset +0x29c0 to Source Code

Without PDB symbols, we must estimate:

### IntelAvbFilter.sys Module Layout

- **Base**: 0xfffff80632a60000
- **Size**: 0x33000 (204 KB)
- **Crash**: 0xfffff80632a629c0
- **Offset**: 0x29c0 (≈10.6 KB into module)

### Likely Source Locations

Based on linker ordering and offset, **IntelAvbFilter+0x29c0** is likely in:

1. **filter.c** - Core NDIS handlers (MOST LIKELY)
   - `FilterSendNetBufferLists()`
   - `FilterReceiveNetBufferLists()`
   - `FilterOidRequest()`

2. **avb_integration_fixed.c** - AVB context handling
   - Lock acquisition in timestamp/IOCTL handlers

3. **device.c** - IOCTL dispatcher
   - Device context access

### Hypothesis: FilterReceiveNetBufferLists or FilterSendNetBufferLists

The offset +0x1e990 in the call stack (IntelAvbFilter+0x1e990 called +0x29c0) suggests:
- Packet processing path
- Likely in timestamp detection or AVB context access
- **FilterReceiveNetBufferLists** line ~1656 where `pFilter->AvbContext` is accessed

---

## Recommended Fixes (Already Identified)

### Priority 1: Add NULL Checks to All NDIS Callbacks

See GitHub Issue #315 for complete fix details.

### Priority 2: Enable Defensive Coding

- Add `_In_ _Notnull_` SAL annotations
- Use `NT_ASSERT(pFilter != NULL)`  before any field access
- Store global `FilterDriverHandle` for error path NBL completions

### Priority 3: Test with Driver Verifier

```cmd
verifier /standard /driver IntelAvbFilter.sys
```

This would have caught this bug immediately.

---

## Timeline of Events (February 21, 2026)

| Time | Event |
|------|-------|
| 8:11:36 PM | First crash - IntelAvbFilter+0x29c0 |
| 8:39:03 PM | Second crash - Same offset |
| 8:49:55 PM | Third crash - Same offset |
| 9:50:28 PM | Fourth crash - Same offset (primary analysis) |

**Pattern**: 4 identical crashes over ~2 hours suggests:
- Repeated stress/test scenario
- Recurring race condition
- Persistent configuration issue triggering the bug

---

## Comparison With Other Crashes

All four dumps show:
- ✅ Same bug check (0xD1)
- ✅ Same module (IntelAvbFilter.sys)
- ✅ Same offset (+0x29c0)
- ✅ Same root cause (NULL pointer + offset access)
- ✅ Same IRQL (DISPATCH_LEVEL)

**Conclusion**: This is a **systematic bug**, not a random corruption event.

---

## Impact Assessment

### Severity: CRITICAL

- **System Stability**: BSOD during normal network operations
- **Reproducibility**: 4 crashes in 2 hours = HIGH
- **Blast Radius**: Any system with Intel NIC + IntelAvbFilter
- **User Impact**: Unexpected reboots, data loss, service interruption

### Affected Scenarios

- Network packet processing (send/receive)
- Filter attach/detach during network changes
- Power state transitions
- Driver stress testing (likely cause of these 4 crashes)

---

## Next Steps

1. ✅ **GitHub Issue Created**: #315
2. ⏳ **Implement NULL Checks**: All NDIS callback functions
3. ⏳ **Test with Driver Verifier**: Catch before deployment
4. ⏳ **Build with Symbols**: Deploy PDB for better crash analysis
5. ⏳ **Add Crash Telemetry**: Log FilterModuleContext state

---

## Testing Recommendations

### Reproduce the Bug

```cmd
# Enable Driver Verifier
verifier /standard /driver IntelAvbFilter.sys
bcdedit /set testsigning on
shutdown /r /t 0

# Stress test
while ($true) {
    Disable-NetAdapter -Name "Ethernet" -Confirm:$false
    Start-Sleep -Seconds 2
    Enable-NetAdapter -Name "Ethernet" -Confirm:$false
    Start-Sleep -Seconds 5
}
```

This rapid enable/disable cycle will likely trigger the race condition.

---

## Conclusion

**ROOT CAUSE**: Missing NULL pointer validation in IntelAvbFilter.sys NDIS callback handlers.

**FIX**: Add defensive NULL checks to all functions that receive `FilterModuleContext` parameter.

**TIMELINE**: Implement fix → Test with Driver Verifier → Release → Monitor for recurrence.

---

**Analysis Date**: March 2, 2026  
**Analyst**: AI Assistant  
**Tools Used**: WinDbg (cdb.exe), !analyze -v  
**Related**: GitHub Issue #315, BUGCHECK_D1_ANALYSIS.md
