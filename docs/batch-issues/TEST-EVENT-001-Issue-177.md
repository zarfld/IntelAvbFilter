# TEST-EVENT-001: Verify PTP Timestamp Event Latency and Zero Polling

**Issue**: #177  
**Type**: Test Case (TEST)  
**Priority**: P0 (Critical)  
**Status**: Backlog  
**Phase**: 07 - Verification & Validation

## Test Objective

Verify that PTP hardware timestamp capture events are emitted within 1 microsecond of the hardware timestamp capture event, using hardware interrupts with zero polling overhead.

## Traceability
- Traces to: #168 (REQ-F-EVENT-001: Emit PTP Hardware Timestamp Capture Events) #165 (REQ-NF-EVENT-001: Event Notification Latency < 1 microsecond) #161 (REQ-NF-EVENT-002: Zero Polling Overhead)

## Test Setup

### Hardware Configuration
- Intel I210/I225 NIC with PTP support
- PTP message generator (external device or software simulator)
- Oscilloscope with 2+ channels (min 100 MHz bandwidth)
- GPIO breakout from test system

### Software Configuration
- Driver compiled with test instrumentation enabled
- GPIO toggle at key points:
  - GPIO1: Hardware timestamp capture (ISR entry)
  - GPIO2: Observer notification (OnEvent callback)
  
### Test Harness
```c
// Observer for test measurement
static VOID TestObserver_OnEvent(
    PVOID Context,
    EVENT_TYPE Type,
    PVOID EventData
) {
    if (Type == EVENT_PTP_TIMESTAMP_CAPTURED) {
        // Toggle GPIO2 to mark observer notification
        ToggleGPIO(GPIO_PIN_2);
        
        // Record timestamp for statistical analysis
        PTP_TIMESTAMP_EVENT* Event = (PTP_TIMESTAMP_EVENT*)EventData;
        RecordTimestamp(Event->Timestamp);
    }
}
```

## Test Procedure

### Test Case 1: Latency Measurement (GPIO + Oscilloscope)

**Steps**:
1. Configure oscilloscope:
   - Channel 1: GPIO1 (ISR entry)
   - Channel 2: GPIO2 (Observer notification)
   - Trigger: Rising edge on Channel 1
   - Time base: 100 ns/division

2. Register test observer with EVENT_SUBJECT

3. Send 1000 PTP Sync messages at 125 µs intervals (8 kHz rate)

4. Capture time delta between GPIO1 and GPIO2 for each message

5. Calculate statistics:
   - Mean latency
   - Maximum latency
   - Standard deviation (jitter)
   - 99th percentile latency

**Expected Results**:
```
Mean latency:     < 500 ns
Maximum latency:  < 1000 ns (1 µs)
Std deviation:    < 100 ns
99th percentile:  < 900 ns
```

### Test Case 2: Event Data Validation

**Steps**:
1. Send PTP Sync message with known parameters:
   - Sequence number: 0x1234
   - Message type: SYNC (0x00)

2. Capture event data in observer callback

3. Verify event contains:
   - Correct timestamp value
   - Correct message type
   - Correct sequence number

**Expected Results**:
- All fields match transmitted PTP message
- Timestamp is non-zero and monotonically increasing

### Test Case 3: Zero Polling Verification

**Steps**:
1. Run Windows Performance Analyzer (WPA) with CPU sampling enabled

2. Generate PTP events for 60 seconds

3. Analyze CPU profile for event handler threads

4. Verify no polling loops present in hot path

**Expected Results**:
- No `while` loops in ISR or DPC
- No `Sleep()` or busy-wait patterns
- All events triggered by hardware interrupts (IDT entries)

### Test Case 4: Stress Test (High Event Rate)

**Steps**:
1. Configure PTP hardware to generate timestamps at maximum rate:
   - 1000 Sync messages/second
   - 1000 Delay_Req messages/second
   - Total: 2000 events/second

2. Run for 10 minutes (1.2 million events)

3. Verify:
   - Zero events dropped
   - Latency remains < 1 µs
   - No memory leaks or resource exhaustion

**Expected Results**:
- 100% event delivery (no drops)
- Latency statistics within specification
- Memory usage stable

### Test Case 5: Multiple Observers

**Steps**:
1. Register 4 observers with different priorities:
   - Logger (priority 0)
   - Diagnostics (priority 10)
   - Control plane (priority 20)
   - Test observer (priority 30)

2. Generate 1000 timestamp events

3. Verify:
   - All observers receive all events
   - Observers called in priority order
   - Total latency < 1 µs

**Expected Results**:
- Each observer receives 1000 events
- Execution order matches priority
- Cumulative latency < 1 µs (< 250 ns per observer)

## Pass/Fail Criteria

### Pass Criteria ✅
- [ ] Mean latency < 500 ns
- [ ] Maximum latency < 1 µs
- [ ] Jitter (std dev) < 100 ns
- [ ] Event data matches PTP message fields
- [ ] Zero polling detected in CPU profile
- [ ] 100% event delivery under stress
- [ ] Multiple observers all receive events in priority order

### Fail Criteria ❌
- Any latency measurement > 1 µs
- Missing or incorrect event data
- Polling loops detected in event path
- Event drops under normal load
- Observer not called or called out of order

## Test Automation

### Automated Test Script
```python
import ptp_test_harness
import statistics

def test_ptp_timestamp_event_latency():
    """Automated test for PTP timestamp event latency"""
    
    # Setup
    harness = ptp_test_harness.PtpTestHarness()
    harness.configure_oscilloscope(channels=2, timebase_ns=100)
    harness.register_test_observer()
    
    # Execute
    latencies = []
    for i in range(1000):
        harness.send_ptp_sync(sequence=i)
        delta_ns = harness.measure_gpio_delta()
        latencies.append(delta_ns)
    
    # Verify
    mean = statistics.mean(latencies)
    maximum = max(latencies)
    stdev = statistics.stdev(latencies)
    p99 = statistics.quantiles(latencies, n=100)[98]
    
    assert mean < 500, f"Mean latency {mean}ns exceeds 500ns"
    assert maximum < 1000, f"Max latency {maximum}ns exceeds 1µs"
    assert stdev < 100, f"Jitter {stdev}ns exceeds 100ns"
    assert p99 < 900, f"99th percentile {p99}ns exceeds 900ns"
    
    print(f"✅ PASS: Mean={mean}ns, Max={maximum}ns, Jitter={stdev}ns")
```

## Test Environment

- **OS**: Windows 10/11 x64 (NDIS 6.x)
- **Driver**: Debug build with test instrumentation
- **Hardware**: Intel I210 or I225 NIC
- **Tools**: 
  - Oscilloscope (Tektronix MDO3000 or equivalent)
  - Windows Performance Analyzer
  - PTP test harness (custom Python tool)

## Traceability

Verifies: #168, #165, #161  
Implements: #171 (ARC-C-EVENT-002: PTP Timestamp Handler)

## Test Status

- **Status**: Backlog
- **Priority**: P0 (Critical)
- **Estimated Effort**: 4 hours
- **Dependencies**: #171 implementation complete

## Notes

- Oscilloscope measurement is the gold standard for latency verification
- CPU profiler confirms zero polling overhead
- Multiple iterations recommended for statistical confidence
