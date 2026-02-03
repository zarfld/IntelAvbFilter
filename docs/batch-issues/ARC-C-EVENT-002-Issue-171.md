# ARC-C-EVENT-002: PTP Hardware Timestamp Event Handler

**Issue**: #171  
**Type**: Architecture Component (ARC-C)  
**Priority**: P0 (Critical)  
**Status**: Backlog  
**Phase**: 03 - Architecture Design

## Component Overview

Captures PTP hardware timestamps (t1, t2, t3, t4) from Intel I210/I225 NIC registers and emits events to registered observers. Operates in interrupt context with sub-microsecond latency.

## Architecture Decisions

Traces to: #166
Traces to: #168
Traces to: #165
Traces to: #161

## Requirements Addressed

- #168 (REQ-F-EVENT-001: Emit PTP Hardware Timestamp Capture Events)
- #165 (REQ-NF-EVENT-001: Event Notification Latency \u003c 1 microsecond)
- #161 (REQ-NF-EVENT-002: Zero Polling Overhead)

## Component Responsibilities

1. **Interrupt Handling**
   - Register ISR for timestamp interrupts (MSI-X vector)
   - Acknowledge timestamp interrupt in hardware
   - Schedule DPC for timestamp processing

2. **Timestamp Retrieval**
   - Read timestamp from hardware registers (RXSTMPL/H, TXSTMPL/H)
   - Extract message metadata from descriptor
   - Validate timestamp and metadata integrity

3. **Event Emission**
   - Construct PTP_TIMESTAMP_EVENT structure
   - Notify observers via EVENT_SUBJECT
   - Handle notification errors gracefully

## Interfaces

### Initialization and Cleanup
```c
NTSTATUS InitializePtpTimestampHandler(
    AVB_ADAPTER* Adapter,
    EVENT_SUBJECT* EventSubject
);

VOID CleanupPtpTimestampHandler(
    AVB_ADAPTER* Adapter
);
```

### Interrupt Service Routine
```c
BOOLEAN PtpTimestampISR(
    NDIS_HANDLE InterruptContext,
    PULONG TargetProcessors
);
```

### Deferred Procedure Call
```c
VOID PtpTimestampDPC(
    NDIS_HANDLE InterruptContext,
    PVOID Context,
    PULONG Reserved1,
    PULONG Reserved2
);
```

### Enable/Disable Operations
```c
NTSTATUS EnablePtpTimestampInterrupts(
    AVB_ADAPTER* Adapter
);

VOID DisablePtpTimestampInterrupts(
    AVB_ADAPTER* Adapter
);
```

## Data Structures

### Component Context
```c
typedef struct _PTP_TIMESTAMP_HANDLER {
    AVB_ADAPTER* Adapter;
    EVENT_SUBJECT* EventSubject;
    
    // Hardware state
    ULONG MsixVectorIndex;
    BOOLEAN InterruptsEnabled;
    
    // Statistics
    UINT64 TotalTimestampsCaptured;
    UINT64 TimestampOverflows;
    UINT64 InvalidTimestamps;
    
    // Synchronization
    KSPIN_LOCK StateLock;
} PTP_TIMESTAMP_HANDLER;
```

### Hardware Register Offsets (Intel I210/I225)
```c
#define I210_RXSTMPL    0x0B700  // Rx timestamp low
#define I210_RXSTMPH    0x0B704  // Rx timestamp high
#define I210_TXSTMPL    0x0B708  // Tx timestamp low
#define I210_TXSTMPH    0x0B70C  // Tx timestamp high
#define I210_TSICR      0x0B66C  // Timestamp interrupt control
#define I210_TSISR      0x0B670  // Timestamp interrupt status

// Interrupt control bits
#define TSICR_TXTS_INT_EN   (1 \u003c\u003c 0)  // Tx timestamp interrupt enable
#define TSICR_RXTS_INT_EN   (1 \u003c\u003c 1)  // Rx timestamp interrupt enable

// Interrupt status bits
#define TSISR_TXTS          (1 \u003c\u003c 0)  // Tx timestamp captured
#define TSISR_RXTS          (1 \u003c\u003c 1)  // Rx timestamp captured
```

## Sequence Diagrams

### Timestamp Capture Flow
```
Hardware               ISR                DPC              Observers
   |                    |                  |                   |
   |-- Timestamp ------\u003e|                  |                   |
   |    Captured        |                  |                   |
   |                    |                  |                   |
   |                    |-- Read TSISR ---\u003e|                   |
   |                    |\u003c-- Status -------|                   |
   |                    |                  |                   |
   |                    |-- Ack Interrupt-\u003e|                   |
   |                    |                  |                   |
   |                    |-- Queue DPC ----\u003e|                   |
   |                    |                  |                   |
   |                    |\u003c-- Return -------|                   |
   |                    |                  |                   |
   |                    |                  |-- Read RXSTMPL/H-\u003e|
   |                    |                  |\u003c-- Timestamp -----|
   |                    |                  |                   |
   |                    |                  |-- Read Metadata-\u003e|
   |                    |                  |\u003c-- Msg Type ------|
   |                    |                  |                   |
   |                    |                  |-- NotifyObservers-\u003e
   |                    |                  |                   |
   |                    |                  |                   |--\u003eOnEvent()
   |                    |                  |                   |\u003c--Return
```

## Timing Analysis

### Latency Budget (Target: \u003c 1 microsecond)

| Operation | Target Latency | Measurement Method |
|-----------|----------------|-------------------|
| HW capture → IRQ raised | \u003c 100 ns | Hardware spec |
| IRQ delivery → ISR entry | \u003c 200 ns | OS kernel metric |
| ISR execution | \u003c 500 ns | GPIO toggle |
| DPC scheduling | \u003c 200 ns | OS kernel metric |
| DPC → Observer notify | \u003c 500 ns | GPIO toggle |
| **Total** | **\u003c 1.5 μs** | **Oscilloscope** |

### ISR Execution Time Breakdown
```c
BOOLEAN PtpTimestampISR(...) {
    // ~50ns: Read interrupt status register
    TsIsr = ReadRegister(Adapter, I210_TSISR);
    
    // ~20ns: Check if our interrupt
    if (TsIsr == 0) return FALSE;
    
    // ~50ns: Acknowledge interrupt
    WriteRegister(Adapter, I210_TSISR, TsIsr);
    
    // ~100ns: Queue DPC
    NdisMQueueDpcEx(Adapter-\u003eInterruptHandle, 0, TargetProcessors);
    
    return TRUE;
    // Total: ~220ns (within 500ns budget)
}
```

## Error Handling

### Error Scenarios and Mitigation

1. **Timestamp FIFO Overflow**
   - Detection: Check overflow bit in status register
   - Mitigation: Log error, increment counter, notify observers of data loss
   - Recovery: Flush FIFO, resume normal operation

2. **Invalid Timestamp Value**
   - Detection: Timestamp == 0 or timestamp regression
   - Mitigation: Discard timestamp, log warning
   - Recovery: Wait for next valid timestamp

3. **Observer Callback Exception**
   - Detection: SEH (Structured Exception Handling) in NotifyObservers
   - Mitigation: Catch exception, log error, continue with next observer
   - Recovery: Disable faulty observer, prevent system crash

4. **Interrupt Storm**
   - Detection: \u003e 10,000 interrupts/second
   - Mitigation: Disable interrupts temporarily, log critical error
   - Recovery: Investigate root cause, re-enable after delay

## Performance Optimizations

1. **Pre-allocated Event Structures**
   - Allocate EVENT_DATA pool at initialization
   - Eliminates allocation overhead in DPC

2. **Register Read Coalescing**
   - Read timestamp registers in single burst
   - Reduces PCIe transaction overhead

3. **Inline Observer Notification**
   - Inline NotifyObservers for critical path
   - Eliminates function call overhead

4. **Cache-Friendly Data Structures**
   - Align structures to cache line boundaries
   - Minimize false sharing in SMP systems

## Testing Strategy

### Unit Tests
1. ISR functionality (interrupt acknowledgment)
2. Timestamp register reading
3. Event data construction
4. Observer notification

### Integration Tests
1. End-to-end timestamp capture flow
2. Latency measurement (GPIO + oscilloscope)
3. Stress test (high event rate)
4. Error injection (overflow, invalid data)

### Acceptance Tests
1. Sub-microsecond latency verification (99th percentile)
2. Zero event loss under normal load
3. Graceful degradation under overflow

## Dependencies

- Intel I210/I225 NIC hardware
- NDIS 6.x interrupt handling
- EVENT_SUBJECT component (#170)
- MSI-X interrupt support

## Traceability

Traces to: #166
Traces to: #168
Traces to: #165
Traces to: #161
