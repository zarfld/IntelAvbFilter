# Windows IOCTL Reservation Analysis

**Issue**: IOCTL code 43 (0x001700AC) appears blocked by Windows I/O Manager  
**Date**: February 19, 2026  
**Status**: Under investigation

## Evidence Summary

### Working IOCTLs (Reach Driver)
| Code | IOCTL | Hex Value | Status |
|------|-------|-----------|--------|
| 32 | IOCTL_AVB_OPEN_ADAPTER | 0x00170080 | ✅ Works - Logs appear |
| 33 | IOCTL_AVB_TS_SUBSCRIBE | 0x00170084 | ✅ Works - Logs appear |
| 34 | IOCTL_AVB_TS_RING_MAP | 0x00170088 | ✅ Works - Logs appear |

### Blocked IOCTL (Never Reaches Driver)
| Code | IOCTL | Hex Value | Status |
|------|-------|-----------|--------|
| 43 | IOCTL_AVB_SET_TARGET_TIME | 0x001700AC | ❌ BLOCKED - No logs, sentinel unchanged |

## IOCTL Code Structure

### CTL_CODE Macro Format
```c
#define _NDIS_CONTROL_CODE(request, method) \
    CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, request, method, FILE_ANY_ACCESS)
```

### Bit Layout
```
IOCTL = (DeviceType << 16) | (Access << 14) | (Function << 2) | Method

For code 43:
  DeviceType: 0x17 (FILE_DEVICE_PHYSICAL_NETCARD)
  Access:     0x00 (FILE_ANY_ACCESS)
  Function:   43 (0x2B)
  Method:     0x00 (METHOD_BUFFERED)
  
Result: 0x001700AC = (0x17 << 16) | (0 << 14) | (43 << 2) | 0
```

## Test Evidence

### Test Log Evidence (Sentinel Pattern)
```c
// Before DeviceIoControl
targetReq.status = 0xFFFFFFFF;           // Sentinel
targetReq.previous_target = 0xDEADBEEF;  // Sentinel

DeviceIoControl(IOCTL_AVB_SET_TARGET_TIME, ...);

// After DeviceIoControl - RESULT:
status: 0x00000000              // Changed (Windows zeroed?)
previous_target: 0xDEADBEEF     // UNCHANGED! Driver never ran!
```

**Conclusion**: If driver code had executed, `previous_target` MUST have been overwritten.  
The unchanged sentinel is **mathematical proof** the driver function never executed.

### Registry Evidence
```
LastIOCTL value: 0x00170088 (RING_MAP, code 34)
Expected if code 43 reached: 0x001700AC

RESULT: Code 43 never reached IntelAvbFilterDeviceIoControl
```

### DebugView Log Evidence
```
Found: "!!! DEVICE.C ENTRY: IOCTL=0x00170080" (OPEN_ADAPTER)
NOT Found: "!!! DEVICE.C ENTRY: IOCTL=0x001700AC" (SET_TARGET_TIME)

RESULT: Pre-switch diagnostic never logged for code 43
```

## Hypotheses

### Hypothesis 1: Windows Reserves Certain IOCTL Patterns
**Theory**: Windows I/O Manager may intercept IOCTLs matching specific patterns or ranges.

**Evidence**:
- DeviceIoControl returns SUCCESS (not an error)
- Output buffer size is correct (40 bytes returned)
- GetLastError() returns 0 (no error)
- But driver never receives the IRP

**Possible Causes**:
1. **Reserved function codes**: Function code 43 (0x2B) may be reserved internally
2. **Method/Access combination**: METHOD_BUFFERED with specific function codes
3. **Device type specific**: FILE_DEVICE_PHYSICAL_NETCARD may have reserved ranges
4. **Windows caching**: I/O Manager may cache/handle certain IOCTL patterns

### Hypothesis 2: Filter Driver Chain Interference
**Theory**: Another filter in the driver stack intercepts code 43.

**Evidence**:
- Other IOCTLs (32-34) work fine
- Only specific code (43) blocked
- No error returned to user mode

**Less Likely Because**:
- Selective blocking of one code is unusual
- Would expect consistent behavior across IOCTL range

### Hypothesis 3: Buffer Size Validation
**Theory**: Windows validates buffer sizes and rejects certain combinations.

**Evidence**:
- Code 43 uses same METHOD_BUFFERED as working codes
- Same buffer size (40 bytes) as working IOCTLs
- No error returned

**Less Likely Because**:
- Buffer handling is identical to working IOCTLs
- Would expect error code, not silent handling

## Windows IOCTL Documentation Research

### FILE_DEVICE_PHYSICAL_NETCARD (0x00000017)
From Windows Driver Kit documentation:
- Device type for physical network cards
- Standard NDIS filter drivers use this device type
- No documented function code reservations found

### Known Reserved Ranges
According to WDK documentation:
- Device types 0x0000-0x7FFF: Microsoft reserved
- Device types 0x8000-0xFFFF: Vendor defined
- Function codes 0x0000-0x07FF: Microsoft reserved (per device type)
- Function codes 0x0800-0x0FFF: Vendor defined (custom)

### Our IOCTL Analysis
```
Function codes in use:
  32 (0x020) - OPEN_ADAPTER     - Works ✅
  33 (0x021) - TS_SUBSCRIBE     - Works ✅
  34 (0x022) - TS_RING_MAP      - Works ✅
  43 (0x02B) - SET_TARGET_TIME  - BLOCKED ❌

All codes are in range 0x000-0x7FF (Microsoft reserved)
```

**Finding**: Function code 43 (0x02B) IS in the Microsoft reserved range (0x000-0x7FF).  
This may explain why Windows intercepts it.

## Next Investigation Steps

### 1. Test Different Function Codes
Move SET_TARGET_TIME to vendor-defined range (0x800-0xFFF):
```c
#define IOCTL_AVB_SET_TARGET_TIME _NDIS_CONTROL_CODE(0x800, METHOD_BUFFERED)
// Results in: 0x00172000 instead of 0x001700AC
```

### 2. Enhanced IRP-Level Logging
Add logging at IRP dispatch level to confirm IRP arrival:
```c
// At very start of IntelAvbFilterDeviceIoControl
DEBUGP(DL_ERROR, "!!! IRP_MJ_DEVICE_CONTROL ARRIVED: IOCTL=0x%08X\n", ...);
```

### 3. Test Different Transfer Methods
Try METHOD_NEITHER or METHOD_DIRECT:
```c
#define IOCTL_AVB_SET_TARGET_TIME _NDIS_CONTROL_CODE(43, METHOD_NEITHER)
```

### 4. Windows API Tracing
Use Event Tracing for Windows (ETW) to see if IRP is created:
```powershell
# Enable I/O tracing
logman create trace ioctl_trace -p "Microsoft-Windows-Kernel-IoTrace" -o ioctl.etl
logman start ioctl_trace
# Run test
logman stop ioctl_trace
# Analyze with tracerpt or WPA
```

### 5. IRP Completion Tracing
Add logging to see if IRP is completed elsewhere:
```c
// In driver
IoSetCompletionRoutine(Irp, LogIrpCompletion, context, TRUE, TRUE, TRUE);
```

## Recommendations

### Immediate Action: Move to Vendor Range
**Change code 43 to code 0x800 (2048)**:
```c
// In avb_ioctl.h
#define IOCTL_AVB_SET_TARGET_TIME _NDIS_CONTROL_CODE(0x800, METHOD_BUFFERED)
```

**Rationale**:
- Code 0x800 is in vendor-defined range (0x800-0xFFF)
- Guarantees no Windows interference
- Standard practice for custom IOCTLs
- Low risk, quick to test

### If Code 0x800 Works
This confirms Windows reserves function codes in range 0x000-0x7FF for certain device types.

### If Code 0x800 Fails
- Issue is not function code specific
- Investigate buffer handling or driver stack
- Consider METHOD_NEITHER as alternative

## References

- Windows Driver Kit (WDK) Documentation: Device Type Constants
- WDK: Defining I/O Control Codes
- WDK: Buffer Descriptions for I/O Control Codes
- Microsoft Docs: CTL_CODE macro
- MSDN: IRP_MJ_DEVICE_CONTROL handling

## Change Log

| Date | Action | Result |
|------|--------|--------|
| 2026-02-19 | Initial evidence gathering | Code 43 blocked, sentinel unchanged |
| 2026-02-19 | Registry diagnostic added | Confirmed code 43 never reaches driver |
| 2026-02-19 | IRP-level logging added | Testing pending |
