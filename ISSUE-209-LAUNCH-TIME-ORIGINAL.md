# TEST-LAUNCH-TIME-001: Launch Time Configuration and Offload Verification

## 🔗 Traceability

- Traces to: #1 (StR-CORE-001: AVB Filter Driver Core Requirements)
- Verifies: #6 (REQ-F-LAUNCH-001: Launch Time Offload)
- Related Requirements: #7, #8, #149
- Test Phase: Phase 07 - Verification & Validation
- Test Priority: P1 (High - Time-Sensitive Networking)

---

## 📋 Test Objective

**Primary Goal**: Validate per-packet launch time offload functionality per IEEE 802.1Qbv for deterministic packet transmission scheduling.

**Scope**:
- Launch time specification per packet (SYSTIM + offset)
- Hardware enforcement of launch time (packet held until scheduled time)
- Launch time accuracy measurement
- Interaction with TAS gate schedules
- Launch time error handling (missed deadlines)

**Success Criteria**:
- ✅ Launch time accuracy within ±100ns of scheduled time
- ✅ Packets transmitted only when scheduled (no early transmission)
- ✅ Launch time coordinates with TAS gate windows
- ✅ Missed launch time detection and reporting
- ✅ Per-packet launch time configuration via driver API

---

## 🧪 Test Coverage

### **10 Unit Tests**

**UT-LAUNCH-001: Single Packet Launch Time**
- Configure packet with launch time = current PHC + 1ms
- Transmit packet
- Measure actual transmission time via GPIO toggle
- Verify transmission within ±100ns of scheduled time

**UT-LAUNCH-002: Launch Time Range**
- Test minimum launch time offset: +1µs from current PHC
- Test maximum launch time offset: +1s from current PHC
- Verify both extremes work correctly
- Confirm packets transmit at scheduled times

**UT-LAUNCH-003: Multiple Packets with Different Launch Times**
- Queue 10 packets with staggered launch times (100µs apart)
- Schedule: t+100µs, t+200µs, ..., t+1000µs
- Verify packets transmit in correct order at scheduled times
- Confirm no reordering or collisions

**UT-LAUNCH-004: Launch Time Past Detection**
- Configure packet with launch time = current PHC - 1ms (already passed)
- Transmit packet
- Verify driver detects "launch time in past" error
- Confirm packet either dropped or transmitted immediately with error flag

**UT-LAUNCH-005: Launch Time + TAS Gate Open Window**
- Configure TAS: Gate open for TC6 from 0-500µs (500µs window)
- Schedule packet on TC6 with launch time = 250µs (mid-window)
- Verify packet transmits at 250µs (not at gate open)
- Confirm launch time takes precedence over gate open edge

**UT-LAUNCH-006: Launch Time + TAS Gate Closed Window**
- Configure TAS: Gate closed for TC6 from 500µs-1000µs
- Schedule packet on TC6 with launch time = 750µs (gate closed)
- Verify packet held until next gate open window
- Measure actual transmission time (should be at next cycle start)

**UT-LAUNCH-007: Launch Time Register Programming**
- Write launch time to TQAVLAUNCHTIME register
- Read back value
- Verify written value matches read value (register integrity)
- Test all 64 bits (full nanosecond precision)

**UT-LAUNCH-008: Launch Time Wraparound**
- Configure PHC near maximum value (UINT64_MAX - 1s)
- Schedule packet with launch time that wraps around to 0
- Verify hardware handles wraparound correctly
- Confirm packet transmits at correct time post-wraparound

**UT-LAUNCH-009: Concurrent Launch Times on Different Queues**
- Queue TC6 packet with launch time = t+500µs
- Queue TC5 packet with launch time = t+500µs (same time, different queue)
- Verify both packets transmit at scheduled time
- Confirm no queue interference

**UT-LAUNCH-010: Launch Time Disable (Immediate Transmission)**
- Configure packet with launch time = 0 (immediate mode)
- Transmit packet
- Verify packet transmits immediately (no delay)
- Confirm bypass of launch time logic

---

### **3 Integration Tests**

**IT-LAUNCH-001: Launch Time + gPTP Synchronization**
- Configure 2 adapters with gPTP synchronization
- Adapter 1: Schedule packet at t=1000000000 (1 second)
- Adapter 2: Schedule packet at t=1000000000 (same time)
- Run gPTP for 10 seconds to achieve sync
- Trigger both packets
- Measure transmission time difference between adapters
- Verify simultaneous transmission within ±1µs (gPTP sync accuracy)

**IT-LAUNCH-002: Launch Time + CBS Shaper**
- Configure CBS on TC6: 75% bandwidth reservation
- Queue 100 packets with launch times every 125µs
- Run for 12.5ms (100 packets)
- Verify all packets transmit at scheduled times
- Confirm CBS does not delay packets beyond launch time
- Measure bandwidth utilization (should be 75% ±1%)

**IT-LAUNCH-003: Production Audio Stream (Class A)**
- Configure 8 kHz audio stream (125µs sample period)
- Schedule 1000 packets with launch times every 125µs
- Run for 125ms (1000 samples)
- Verify all packets transmit within ±1µs of scheduled time
- Measure jitter: <1µs (99.99th percentile)
- Confirm zero packet loss

---

### **2 V&V Tests**

**VV-LAUNCH-001: 24-Hour Launch Time Stability**
- Configure continuous stream with 1ms launch time intervals
- Run for 24 hours (86,400,000 packets)
- Verify:
  - Launch time accuracy maintained: ±100ns mean, ±500ns max
  - Zero missed launch times
  - Zero packet loss
  - PHC drift compensated (if gPTP active)

**VV-LAUNCH-002: Industrial Control Scenario (Cyclic I/O)**
- Simulate PLC cyclic I/O:
  - 1ms cycle time
  - 8 TC6 packets per cycle (8 I/O modules)
  - Launch times: t+0, t+125µs, t+250µs, ..., t+875µs
- Run for 1 hour (3,600,000 cycles, 28,800,000 packets)
- Verify:
  - All packets transmit within ±10µs of scheduled time
  - Cycle time jitter <100µs
  - Zero control failures (simulated PLC logic)

---

## 🔧 Implementation Notes

### **Launch Time API (Driver Interface)**

```c
typedef struct _AVB_LAUNCH_TIME_CONFIG {
    UINT64 LaunchTimeNanoseconds;  // Absolute PHC time (0 = immediate)
    UINT8 TrafficClass;            // TC0-TC7
    BOOLEAN EnableLaunchTime;      // TRUE = use launch time, FALSE = immediate
} AVB_LAUNCH_TIME_CONFIG;

// IOCTL: Configure launch time for next packet
#define IOCTL_AVB_SET_LAUNCH_TIME \
    CTL_CODE(FILE_DEVICE_NETWORK, 0x820, METHOD_BUFFERED, FILE_ANY_ACCESS)

NTSTATUS SetLaunchTime(HANDLE hDevice, AVB_LAUNCH_TIME_CONFIG* Config) {
    DWORD bytesReturned;
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_AVB_SET_LAUNCH_TIME,
        Config,
        sizeof(AVB_LAUNCH_TIME_CONFIG),
        NULL,
        0,
        &bytesReturned,
        NULL
    );
    return result ? STATUS_SUCCESS : GetLastError();
}
```

### **Hardware Register Programming**

```c
// Program launch time for traffic class
VOID ProgramLaunchTime(UINT8 trafficClass, UINT64 launchTimeNs) {
    // Calculate register offset for TC
    UINT32 launchTimeReg = I225_TQAVLAUNCHTIME_BASE + (trafficClass * 8);
    
    // Write launch time (64-bit value)
    WRITE_REG32(launchTimeReg, (UINT32)(launchTimeNs & 0xFFFFFFFF));      // Lower 32 bits
    WRITE_REG32(launchTimeReg + 4, (UINT32)(launchTimeNs >> 32));         // Upper 32 bits
    
    // Enable launch time mode for this TC
    UINT32 ctrl = READ_REG32(I225_TQAVCTRL);
    ctrl |= (1 << (8 + trafficClass));  // Bit[8+TC] enables launch time
    WRITE_REG32(I225_TQAVCTRL, ctrl);
    
    DbgPrint("Launch time programmed: TC%d = %llu ns\n", trafficClass, launchTimeNs);
}
```

### **Launch Time Accuracy Measurement**

```c
// Measure actual transmission time vs. scheduled launch time
VOID MeasureLaunchTimeAccuracy(UINT64 scheduledTime, UINT64* actualTime, INT64* error) {
    // Trigger GPIO on packet transmission (hardware event)
    // Capture PHC time at GPIO edge
    UINT64 phcAtTransmit = CapturePhcAtGpio();
    
    *actualTime = phcAtTransmit;
    *error = (INT64)phcAtTransmit - (INT64)scheduledTime;
    
    // Log if error exceeds threshold
    if (abs(*error) > 100) {  // ±100ns threshold
        DbgPrint("Launch time error: scheduled=%llu, actual=%llu, error=%lld ns\n",
                 scheduledTime, phcAtTransmit, *error);
    }
}
```

### **Launch Time + TAS Interaction**

```c
// Check if launch time falls within TAS gate open window
BOOLEAN IsLaunchTimeValid(UINT64 launchTime, UINT8 trafficClass) {
    // Get current TAS state
    UINT64 currentPhc = ReadPhcTime();
    UINT32 cycleTime = READ_REG32(I225_CYCLETIME);
    UINT64 cycleOffset = currentPhc % cycleTime;
    
    // Find active GCL entry
    GCL_ENTRY* activeEntry = GetActiveGclEntry(cycleOffset);
    
    // Check if gate is open for this TC
    BOOLEAN gateOpen = (activeEntry->GateStates & (1 << trafficClass)) != 0;
    
    if (!gateOpen) {
        DbgPrint("Launch time falls in closed gate window: TC%d, time=%llu\n",
                 trafficClass, launchTime);
        return FALSE;
    }
    
    return TRUE;
}
```

---

## 📊 Performance Targets

| Metric | Target | Measurement |
|--------|--------|-------------|
| Launch Time Accuracy (mean) | ±100ns | GPIO timestamp capture |
| Launch Time Accuracy (max) | ±500ns | 99.99th percentile |
| Minimum Launch Time Offset | +1µs | From current PHC |
| Maximum Launch Time Offset | +1s | From current PHC |
| Launch Time Jitter (audio stream) | <1µs | 99.99th percentile |
| Missed Launch Time Rate | 0% | Over 24-hour test |

---

## 📈 Acceptance Criteria

- [ ] All 10 unit tests pass
- [ ] All 3 integration tests pass
- [ ] All 2 V&V tests pass
- [ ] Launch time accuracy ±100ns (mean)
- [ ] Launch time accuracy ±500ns (max)
- [ ] Zero missed launch times in 24-hour test
- [ ] gPTP sync within ±1µs (multi-adapter)
- [ ] Industrial control scenario: ±10µs accuracy

---

**Standards**: IEEE 802.1Qbv, IEEE 1012-2016, ISO/IEC/IEEE 12207:2017  
**XP Practice**: TDD - Tests defined before implementation
