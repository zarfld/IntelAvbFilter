# Build 18 - DPC Timer Implementation Complete

**Status**: ✅ **READY FOR BUILD**  
**Date**: February 21, 2026  
**Implements**: Task 7 (Target Time DPC Polling)

---

## 📊 What Was Implemented

### 1. ✅ Added Timer Fields to AVB_DEVICE_CONTEXT

**File**: `src/avb_integration.h` (lines 100-106)

```c
// Target Time DPC Timer (Task 7 - Issue #13)
KTIMER target_time_timer;                             // Windows kernel timer object
KDPC target_time_dpc;                                 // Deferred Procedure Call object
BOOLEAN target_time_poll_active;                      // Flag: Timer is running
ULONG target_time_poll_interval_ms;                   // Poll interval (default: 10ms)
volatile LONG target_time_dpc_call_count;             // Diagnostic: DPC invocation counter
```

**Purpose**: Storage for timer/DPC objects and state tracking.

---

### 2. ✅ Implemented DPC Routine

**File**: `devices/intel_i226_impl.c` (lines 54-232)

```c
static VOID AvbTimerDpcRoutine(
    PKDPC Dpc,
    PVOID DeferredContext,
    PVOID SystemArgument1,
    PVOID SystemArgument2)
```

**What it does**:
- Fires every 10ms to poll TSICR register
- Detects TT0/TT1 interrupt flags
- Reads auxiliary timestamps (AUXSTMPL0/H0, AUXSTMPL1/H1)
- Posts events to subscribers via `AvbPostTimestampEvent()`
- Clears interrupt flags (RW1C)
- Writes diagnostics every 100 calls (~1 second)

**Key diagnostics written**:
- `DPC_CallCount_Total` - Total DPC invocations
- `DPC_Last_IRQL` - Current IRQL (should be DISPATCH_LEVEL = 2)
- `DPC_AuxTS0_Low/High` - Auxiliary timestamp when TT0 fires
- `DPC_TT0_DetectedAt` - Call count when TT0 detected

---

### 3. ✅ Added Timer Initialization

**File**: `devices/intel_i226_impl.c` (lines 723-767)

**Location**: In `i226_set_target_time()`, after interrupt enable section

```c
// Access AVB context via device_t.private_data (already set in avb_bar0_discovery.c)
PAVB_DEVICE_CONTEXT avb_context = (PAVB_DEVICE_CONTEXT)dev->private_data;

if (avb_context && !avb_context->target_time_poll_active) {
    // Initialize kernel timer
    KeInitializeTimer(&avb_context->target_time_timer);
    
    // Initialize DPC
    KeInitializeDpc(&avb_context->target_time_dpc, AvbTimerDpcRoutine, avb_context);
    
    // Set periodic timer (10ms interval)
    LARGE_INTEGER dueTime;
    dueTime.QuadPart = -100000LL;  // 10ms (negative = relative time)
    KeSetTimerEx(&avb_context->target_time_timer, dueTime, 10, &avb_context->target_time_dpc);
    
    avb_context->target_time_poll_active = TRUE;
    avb_context->target_time_poll_interval_ms = 10;
    avb_context->target_time_dpc_call_count = 0;
    
    // Diagnostic: Log timer initialization SUCCESS
    ULONG timer_init_success = 1;
    RtlInitUnicodeString(&valueName, L"Timer_Init_Called");
    ZwSetValueKey(hKey, &valueName, 0, REG_DWORD, &timer_init_success, sizeof(timer_init_success));
    
    // Log timer object and context addresses
    // ... (Timer_ObjectAddress, Timer_ContextAddress)
}
```

**New diagnostics**:
- `Timer_Init_Called` = 1 (timer successfully initialized)
- `Timer_ObjectAddress` = address of KTIMER object
- `Timer_ContextAddress` = address of AVB_DEVICE_CONTEXT
- `Timer_AlreadyActive` = 1 (if timer was already running)
- `Timer_NullContext` = 1 (if context was NULL - error case)

---

### 4. ✅ Added Timer Cleanup

**File**: `src/avb_integration_fixed.c` (lines 994-1001)

**Location**: In `AvbCleanupDevice()`, after TX poll timer cleanup

```c
/* CRITICAL: Cancel Target Time polling timer (Task 7) */
if (AvbContext->target_time_poll_active) {
    BOOLEAN cancelled = FALSE;
    AvbContext->target_time_poll_active = FALSE;  // Signal DPC to stop
    KeCancelTimer(&AvbContext->target_time_timer);  // Cancel timer
    
    // Wait for any pending DPC to complete
    KeFlushQueuedDpcs();
    
    DEBUGP(DL_WARN, "!!! TIMER STOPPED: Target Time DPC was called %d times\n", 
           AvbContext->target_time_dpc_call_count);
}
```

**Purpose**: Safe shutdown of timer on driver unload/device detach.

---

## 🎯 How It Works

### Initialization Flow

```
User Application
    ↓ IOCTL_AVB_SET_TARGET_TIME (enable_interrupt=1)
    ↓
i226_set_target_time()
    ↓ Access context via dev->private_data
    ↓ Check: target_time_poll_active == FALSE?
    ↓ YES:
    ├─→ KeInitializeTimer(&timer_obj)
    ├─→ KeInitializeDpc(&timer_dpc, AvbTimerDpcRoutine, context)
    ├─→ KeSetTimerEx(&timer_obj, -100000LL, 10, &timer_dpc)  // Start 10ms periodic
    └─→ target_time_poll_active = TRUE
```

### Runtime Flow

```
Every 10ms (100 times/second):
    ↓
AvbTimerDpcRoutine() called at DISPATCH_LEVEL
    ↓
Read TSICR register
    ↓
Check: TSICR.TT0 bit set? (bit 3)
    ↓ YES:
    ├─→ Read AUXSTMPL0/H0 (auxiliary timestamp)
    ├─→ AvbPostTimestampEvent(context, 0x1, timestamp, 0, 0)
    ├─→ User ring buffer updated
    ├─→ Write TSICR.TT0 = 1 (clear interrupt flag - RW1C)
    └─→ Log to registry: DPC_AuxTS0_Low/High, DPC_TT0_DetectedAt
    ↓
Check: TSICR.TT1 bit set? (bit 4)
    ↓ YES:
    └─→ Similar handling for timer 1
```

### Cleanup Flow

```
Driver Unload / Device Detach
    ↓
FilterDetach() / AvbCleanupDevice()
    ↓
Check: target_time_poll_active == TRUE?
    ↓ YES:
    ├─→ target_time_poll_active = FALSE  // Signal DPC to stop
    ├─→ KeCancelTimer(&target_time_timer)
    ├─→ KeFlushQueuedDpcs()  // Wait for any pending DPC
    └─→ Log total call count
```

---

## 📊 Expected Test Results (Build 18)

### Registry Keys - Before Target Time Set

```powershell
# (None - timer not initialized yet)
```

### Registry Keys - After `set_target_time(enable_interrupt=1)`

```powershell
# Timer Initialization (NEW in Build 18):
Timer_Init_Called                 : 1           ✅ Timer initialized
Timer_ObjectAddress               : <address>   ✅ KTIMER address
Timer_ContextAddress              : <address>   ✅ Context address

# Hardware Configuration (Existing from Build 17):
i226_TSAUXC_After                 : 2013267221  ✅ EN_TT0 enabled
i226_TSIM_After                   : 8           ✅ TT0 interrupt mask set
i226_EIMS_After                   : 2147483679  ✅ EIMS configured

# Diagnostics NO LONGER PRESENT (replaced by working code):
SetTargetTime_TimerCodeMISSING    : <deleted>   ✅ Code implemented!
Missing_KTIMER_Object             : <deleted>   ✅ Not missing!
Missing_KDPC_Object               : <deleted>   ✅ Not missing!
Missing_KeSetTimerEx_Call         : <deleted>   ✅ Timer started!
Missing_DPC_Routine               : <deleted>   ✅ DPC exists!
```

### Registry Keys - After DPC Running (Polled 100+ times)

```powershell
# DPC Activity (NEW in Build 18):
DPC_CallCount_Total               : 150         ✅ DPC firing every 10ms
DPC_Last_IRQL                     : 2           ✅ DISPATCH_LEVEL
```

### Registry Keys - When Target Time Reached

```powershell
# DPC Detection (NEW in Build 18):
DPC_AuxTS0_Low                    : 2345775635  ✅ Timestamp lower 32 bits
DPC_AuxTS0_High                   : 422         ✅ Timestamp upper 32 bits
DPC_TT0_DetectedAt                : 156         ✅ Call count when detected
```

### DebugView Logs - Expected Output

```
!!! ENTRY: i226_set_target_time(timer=0, target=0x00001A9B8BAB5413, enable_int=1)
!!! TIMER INITIALIZED: 10ms periodic DPC started (context=FFFF968F993DC010)
!!! EXIT: i226_set_target_time() returning SUCCESS
!!! SUMMARY: Configured hardware registers (TRGTTIML0/H0, TSAUXC, TSIM, EIMS)

[After 1 second]
!!! DPC: Polled 100 times (every 10ms)

[After 2 seconds]
!!! DPC: Polled 200 times (every 10ms)

[When target time reached]
!!! DPC: TSICR=0x00000008 (TT0=1, TT1=0)
!!! DPC DETECTED: TT0 interrupt fired! (TSICR=0x00000008)
!!! DPC: AUX TIMESTAMP 0 = 0x00001A9B8BAB5413 (1893748321000 ns)
!!! DPC: Posted target time event to subscribers
!!! DPC: Cleared TSICR.TT0 interrupt
```

### User Application - Expected Behavior

```c
// test_ts_event_sub.exe output:
Subscribed (ring_id=14)
Ring buffer mapped (32840 bytes)
Current SYSTIM: 292148213145 ns
Target time set to 294148213145 ns (+ 2.0s)
[Waiting for target time event...]

✅ EVENT RECEIVED: event_type=0x1 (TARGET_TIME), timestamp=0x00001A9B8BAB5413
✅ Timestamp matches target time!
✅ Test PASSED: UT-TS-EVENT-003 (Target Time Reached Event)
```

---

## 🔧 Build & Test Commands

```powershell
# Build Build 18
.\tools\build\Build-Driver.ps1 -Configuration Debug -Clean

# Install Build 18
.\tools\setup\Install-Driver-Elevated.ps1 -Configuration Debug -Action Reinstall

# Test Target Time Event
.\tests\test_ts_event_sub.exe

# Check Registry (Before test)
Get-ItemProperty -Path 'HKLM:\Software\IntelAvb' | Select-Object Timer_*

# Check Registry (During test - after timer initialized)
Get-ItemProperty -Path 'HKLM:\Software\IntelAvb' | Select-Object Timer_*,DPC_*

# Check Registry (After target time reached)
Get-ItemProperty -Path 'HKLM:\Software\IntelAvb' | Select-Object DPC_*

# Watch DebugView logs
# Open DebugView, filter: "intelavbfilter*" or "!!! DPC"
```

---

## 🎯 Success Criteria

### Build 18 is successful if:

1. ✅ **Driver compiles** without errors
2. ✅ **Timer initialization succeeds**: `Timer_Init_Called = 1` in registry
3. ✅ **DPC fires periodically**: `DPC_CallCount_Total` increments every check
4. ✅ **Target time detected**: `DPC_TT0_DetectedAt` appears when target time reached
5. ✅ **Event posted to user**: `test_ts_event_sub.exe` receives event and passes test
6. ✅ **Timer cleanup works**: Driver unloads without BSOD

---

## 📝 Code Changes Summary

| File | Lines Changed | Type | Description |
|------|---------------|------|-------------|
| `src/avb_integration.h` | +6 lines | Addition | Timer fields in AVB_DEVICE_CONTEXT |
| `devices/intel_i226_impl.c` | +178 lines | Addition | DPC routine implementation |
| `devices/intel_i226_impl.c` | +54 lines | Addition | Timer initialization code |
| `src/avb_integration_fixed.c` | +13 lines | Addition | Timer cleanup code |
| **TOTAL** | **+251 lines** | **Implementation** | **Complete DPC timer solution** |

---

## 🚨 What Changed from Build 17

### Removed Diagnostics (No Longer Needed)

```
SetTargetTime_TimerCodeMISSING        : <deleted>  ✅ Code now implemented
Missing_KTIMER_Object                 : <deleted>  ✅ KTIMER now exists
Missing_KDPC_Object                   : <deleted>  ✅ KDPC now exists
Missing_KeSetTimerEx_Call             : <deleted>  ✅ Timer now started
Missing_DPC_Routine                   : <deleted>  ✅ DPC routine now exists
```

### Added Diagnostics (New in Build 18)

```
Timer_Init_Called                     : 1          ✅ Timer initialization successful
Timer_ObjectAddress                   : <address>  ✅ KTIMER object address
Timer_ContextAddress                  : <address>  ✅ AVB context address
DPC_CallCount_Total                   : <count>    ✅ Total DPC invocations
DPC_Last_IRQL                         : 2          ✅ Current IRQL
DPC_AuxTS0_Low                        : <value>    ✅ Timestamp when TT0 fires
DPC_AuxTS0_High                       : <value>    ✅ Timestamp when TT0 fires
DPC_TT0_DetectedAt                    : <count>    ✅ Call count at detection
```

---

## 🎯 Root Cause Resolution

**Original Problem** (Task 7 failing in Builds 1-17):
- Hardware registers configured correctly ✅
- But target time events never received ❌
- Root cause: **NO DPC TIMER POLLING TSICR** ❌

**Solution** (Build 18):
- Added KTIMER + KDPC objects to AVB_DEVICE_CONTEXT ✅
- Implemented `AvbTimerDpcRoutine()` to poll TSICR every 10ms ✅
- Initialize timer in `i226_set_target_time()` when `enable_interrupt=1` ✅
- Cleanup timer in `AvbCleanupDevice()` ✅
- Post events to subscribers when TT0/TT1 detected ✅

**Result**: Task 7 should now work end-to-end! 🎉

---

## 📚 References

- Implementation Document: `DPC_TIMER_IMPLEMENTATION.md`
- Diagnostic Analysis: `TIMER_DIAGNOSTIC_ANALYSIS.md`
- Intel I226 Datasheet: `C:/Users/dzarf/SynologyDrive/MCP/Intel/2407151103_Intel-Altera-KTI226V-S-RKTU_C26159200.pdf`
- Build 17 Test Results: `logs/test_ts_event_sub_20260221_091358.log`

---

**Next Step**: **BUILD AND TEST!** 🚀

```powershell
.\tools\build\Build-Driver.ps1 -Configuration Debug -Clean
```
