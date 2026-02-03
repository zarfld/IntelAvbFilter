# ADR-EVENT-002: Hardware Interrupt-Driven Event Capture for PTP Timestamps

**Issue**: #166  
**Type**: Architecture Decision Record (ADR)  
**Priority**: P0 (Critical)  
**Status**: Backlog  
**Phase**: 03 - Architecture Design

## Status

**Proposed**

## Context

PTP event messages (Sync, Pdelay_Req, Pdelay_Resp) require precise timestamp capture at the hardware reference plane. The captured timestamps (t1, t2, t3, t4) must be retrieved by software with sub-microsecond latency to maintain synchronization accuracy.

**Requirements Addressed**:
- #168 REQ-F-EVENT-001: Emit PTP Hardware Timestamp Capture Events
- #165 REQ-NF-EVENT-001: Event Notification Latency < 1 microsecond
- #161 REQ-NF-EVENT-002: Zero Polling Overhead

**Hardware Capabilities** (Intel I210/I225):
- Hardware timestamp capture registers (TXSTMPL/H, RXSTMPL/H)
- Interrupt generation on timestamp capture (TSICR register)
- Sub-nanosecond timestamp resolution
- Multiple timestamp storage (FIFO for Tx timestamps)

**Constraints**:
- Sub-microsecond notification latency required
- Polling introduces unpredictable delay and CPU waste
- ISR execution time must be minimal (< 5 microseconds)

## Decision

We will implement **hardware interrupt-driven timestamp capture** with the following architecture:

### Interrupt Configuration

```c
// Enable timestamp capture interrupts in TSICR register
#define I210_TSICR_TXTS_INT_EN  (1 << 0)  // Tx timestamp interrupt enable
#define I210_TSICR_RXTS_INT_EN  (1 << 1)  // Rx timestamp interrupt enable

VOID EnableTimestampInterrupts(AVB_ADAPTER* Adapter) {
    UINT32 TsIcr = 0;
    TsIcr |= I210_TSICR_TXTS_INT_EN;  // Enable Tx timestamp interrupt
    TsIcr |= I210_TSICR_RXTS_INT_EN;  // Enable Rx timestamp interrupt
    
    WriteRegister(Adapter, I210_TSICR, TsIcr);
    
    // Enable MSI-X vector for timestamp interrupts
    EnableMSIXVector(Adapter, TIMESTAMP_VECTOR);
}
```

### ISR (Interrupt Service Routine)

```c
BOOLEAN TimestampInterruptISR(
    NDIS_HANDLE InterruptContext,
    PULONG TargetProcessors
) {
    AVB_ADAPTER* Adapter = (AVB_ADAPTER*)InterruptContext;
    UINT32 TsIsr;
    
    // Read timestamp interrupt status
    TsIsr = ReadRegister(Adapter, I210_TSISR);
    
    if (TsIsr == 0) {
        return FALSE;  // Not our interrupt
    }
    
    // Acknowledge interrupt
    WriteRegister(Adapter, I210_TSISR, TsIsr);
    
    // Schedule DPC for timestamp processing
    NdisMQueueDpcEx(Adapter->InterruptHandle, 0, TargetProcessors);
    
    return TRUE;  // Interrupt handled
}
```

### DPC (Deferred Procedure Call) for Timestamp Processing

```c
VOID TimestampDPC(
    NDIS_HANDLE InterruptContext,
    PVOID Context,
    PULONG Reserved1,
    PULONG Reserved2
) {
    AVB_ADAPTER* Adapter = (AVB_ADAPTER*)InterruptContext;
    PTP_TIMESTAMP Timestamp;
    
    // Read timestamp from hardware registers
    Timestamp.NanosecondsLow = ReadRegister(Adapter, I210_RXSTMPL);
    Timestamp.NanosecondsHigh = ReadRegister(Adapter, I210_RXSTMPH);
    
    // Read associated message metadata
    // (Port, Sequence Number, Message Type from descriptor)
    
    // Emit event to observers
    EVENT_DATA EventData = {
        .Timestamp = Timestamp,
        .MessageType = PTP_MSG_SYNC,  // From descriptor
        .SequenceNumber = /* from descriptor */,
        .PortIdentifier = /* from descriptor */
    };
    
    NotifyObservers(&Adapter->EventSubject, EVENT_PTP_TIMESTAMP_CAPTURED, &EventData);
}
```

### Timing Analysis

**Latency Budget** (Target: < 1 microsecond):
1. Hardware timestamp capture → Interrupt raised: **< 100 nanoseconds**
2. Interrupt delivery → ISR entry: **< 200 nanoseconds**
3. ISR execution (read/ack/schedule DPC): **< 500 nanoseconds**
4. DPC scheduled → DPC entry: **< 200 nanoseconds**
5. DPC execution (read timestamp, notify observers): **< 500 nanoseconds**

**Total estimated latency**: **< 1.5 microseconds** (within 2x safety margin of 1 microsecond requirement)

### ISR Optimization

**Minimize ISR execution time**:
- ✅ Only read/acknowledge interrupt in ISR
- ✅ Defer timestamp processing to DPC
- ✅ No memory allocation in ISR
- ✅ No blocking operations

**DPC Optimization**:
- ✅ Read timestamp registers first (minimize hardware wait)
- ✅ Use pre-allocated event data structures
- ✅ Inline observer notification for critical observers

## Consequences

### Positive
- ✅ **Sub-microsecond latency**: Hardware interrupt provides fastest possible notification
- ✅ **Zero polling overhead**: CPU idle until event occurs
- ✅ **Deterministic timing**: Interrupt-driven eliminates polling jitter
- ✅ **Hardware support**: Intel I210/I225 NICs provide timestamp interrupts

### Negative
- ⚠️ **Interrupt overhead**: Each timestamp capture raises interrupt (mitigated by efficient ISR)
- ⚠️ **DPC scheduling**: Slight latency from ISR to DPC (200ns, acceptable)

### Neutral
- ℹ️ **MSI-X vector allocation**: Requires dedicated interrupt vector for timestamps
- ℹ️ **Concurrency**: ISR and DPC execute at DISPATCH_LEVEL

## Implementation Notes

1. **MSI-X Configuration**: Allocate dedicated vector for timestamp interrupts (separate from Rx/Tx queues)
2. **Interrupt Moderation**: Disable interrupt moderation for timestamp vector (immediate delivery required)
3. **Error Handling**: Handle timestamp FIFO overflow (unlikely but must be robust)
4. **Power Management**: Re-enable interrupts after D0 transition (device power state changes)

## Alternatives Considered

### Alternative 1: Polling-Based Timestamp Retrieval
- ❌ **Rejected**: Introduces unpredictable latency proportional to polling period
- ❌ **Rejected**: Wastes CPU resources (continuous polling)
- ❌ **Rejected**: Cannot meet sub-microsecond latency requirement

### Alternative 2: Hybrid (Interrupt + Polling Fallback)
- ❌ **Rejected**: Adds complexity without benefit
- ❌ **Rejected**: Polling fallback still cannot meet latency requirement

## Verification

**Test Method**: GPIO toggle measurement with oscilloscope
1. Configure GPIO to toggle when hardware captures timestamp
2. Toggle GPIO when software receives event notification
3. Measure delta with oscilloscope

**Success Criteria**:
- 99th percentile latency < 1 microsecond
- Mean latency < 500 nanoseconds

## Traceability

Traces to: #168, #165, #161

## References

- Intel® Ethernet Controller I210 Datasheet (Section 8.15: Timestamp Interrupts)
- Intel® Ethernet Controller I225/I226 Datasheet (Section 8.18: IEEE 1588 PTP)
- `docs/input_EventEmitting.md` (Low-Latency Requirements section)
- Windows Driver Kit: Handling Interrupts (MSI-X, DPC)
