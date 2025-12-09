## Requirement Information

**Requirement ID**: REQ-F-REG-ACCESS-001  
**Title**: Safe Register Access via Spin Locks  
**Priority**: Critical

## Requirement Statement

**The system shall** protect all hardware register access with NDIS spin locks (per-device) to prevent race conditions in multi-threaded NDIS callbacks (FilterSend/FilterReceive/IOCTL).

### Rationale
NDIS Filter callbacks are concurrent: FilterSend and FilterReceive can execute on different CPUs simultaneously. Hardware register access (PHC read/write, timestamp retrieval) must be atomic to prevent corrupted state (StR-004: NDIS Miniport Integration).

## Detailed Specification

### Locking Strategy
1. **Rule 1**: Allocate NDIS_SPIN_LOCK in DEVICE_CONTEXT during FilterAttach
2. **Rule 2**: Acquire lock with NdisAcquireSpinLock() **before** any register MMIO operation
3. **Rule 3**: Hold lock for **minimal duration** (only during register read/write, not during computation)
4. **Rule 4**: Release lock with NdisReleaseSpinLock() **immediately** after register operation
5. **Rule 5**: Never call blocking functions (e.g., memory allocation) while holding lock
6. **Rule 6**: All intel_avb library calls that access registers must occur within lock protection

### Protected Operations
| Operation | Register Access | Lock Required |
|-----------|----------------|---------------|
| PHC Time Read | SYSTIML/SYSTIMH (0x0C04/0x0C08) | ✅ Yes (atomic 64-bit read) |
| PHC Time Write | SYSTIML/SYSTIMH (0x0C04/0x0C08) | ✅ Yes (atomic 64-bit write) |
| PHC Offset Adjust | SYSTIML/SYSTIMH (read-add-write) | ✅ Yes (critical section) |
| PHC Frequency Adjust | TIMINCA (0x0C34) | ✅ Yes (single register write) |
| TX Timestamp Read | TXSTMPL/TXSTMPH (0x0C1C/0x0C20) | ✅ Yes (atomic 64-bit read) |
| RX Timestamp Read | RXSTMPL/RXSTMPH (0x0C18/0x0C1C) | ✅ Yes (atomic 64-bit read) |
| TAS Configuration | Multiple TAS registers | ✅ Yes (multi-register sequence) |
| CBS Configuration | TQAVCTRL (0x3570) + queue regs | ✅ Yes (multi-register sequence) |

### DEVICE_CONTEXT Structure
```c
typedef struct _DEVICE_CONTEXT {
    NDIS_SPIN_LOCK RegisterLock;   // Protects register access
    ULONG RegisterLockIrql;        // Saved IRQL (for debugging)
    BOOLEAN LockInitialized;       // TRUE after NdisAllocateSpinLock()
    // ... other fields
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;
```

### Example Usage Pattern
```c
// PHC Offset Adjustment (atomic read-add-write)
NTSTATUS AdjustPhcOffset(PDEVICE_CONTEXT DeviceContext, INT64 OffsetNs) {
    UINT32 LowOld, HighOld, LowNew, HighNew;
    
    // Acquire lock (raised to DISPATCH_LEVEL)
    NdisAcquireSpinLock(&DeviceContext->RegisterLock);
    
    // Read current PHC time (atomic)
    LowOld = intel_avb_read_register(SYSTIML);
    HighOld = intel_avb_read_register(SYSTIMH);
    
    // Compute new time (stays in critical section)
    UINT64 CurrentTime = ((UINT64)HighOld << 32) | LowOld;
    UINT64 NewTime = CurrentTime + OffsetNs;
    LowNew = (UINT32)(NewTime & 0xFFFFFFFF);
    HighNew = (UINT32)(NewTime >> 32);
    
    // Write new PHC time (atomic)
    intel_avb_write_register(SYSTIML, LowNew);
    intel_avb_write_register(SYSTIMH, HighNew);
    
    // Release lock
    NdisReleaseSpinLock(&DeviceContext->RegisterLock);
    
    return STATUS_SUCCESS;
}
```

## Performance Considerations

### Lock Hold Time Targets
- **Single Register Read/Write**: <1µs
- **Atomic 64-bit PHC Read**: <2µs
- **PHC Offset Adjustment (read-add-write)**: <5µs
- **TAS Configuration (multi-register)**: <50µs

### Deadlock Prevention
- **Rule 1**: Never acquire multiple spin locks (single lock per device)
- **Rule 2**: Never call functions that acquire other locks while holding RegisterLock
- **Rule 3**: Never call NdisWaitEvent() or blocking primitives while holding RegisterLock

## Acceptance Criteria (Gherkin Format)

### Scenario 1: Concurrent PHC Access (Race Condition Prevention)
```gherkin
Given FilterSend on CPU 0 reads PHC time
  And FilterReceive on CPU 1 adjusts PHC offset
  And both operations execute simultaneously
When spin lock protects all register access
Then no corrupted register reads occur
  And PHC time remains monotonic
  And both operations complete successfully
```

### Scenario 2: Lock Hold Time <5µs
```gherkin
Given driver adjusts PHC offset by +500 ns
When lock hold time is measured (GPIO or profiling)
Then lock hold duration < 5µs
  And system remains responsive
```

### Scenario 3: No Deadlocks Under Load
```gherkin
Given 10 Gbps traffic load on 8-core system
  And 1000 concurrent PHC queries per second
When driver runs for 24 hours
Then no deadlocks occur
  And no lock timeouts detected
  And packet loss < 0.01%
```

## Traceability

- Traces to:  #31 (StR-004: NDIS Miniport Integration - concurrent callback safety)
- **Depends on**: #40 (REQ-F-INTEL-AVB-003: register access via intel_avb), #36 (FilterAttach allocates locks)
- **Related**: All IOCTL requirements (#34-#39: register access), #42-#43 (concurrent NDIS callbacks)
- **Verified by**: TEST-REG-ACCESS-001 (Phase 07: race condition testing)
- **Satisfies**: Success Criteria #4 in StR-004 (Thread-safe operation)

## Priority Justification

- **Business Impact**: **CRITICAL** - Prevents data corruption and crashes in production
- **User Impact**: All users (100%) - safety requirement
- **Estimated Effort**: S (Small) - ~2 days
