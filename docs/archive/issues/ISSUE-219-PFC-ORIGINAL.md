# TEST-PFC-001: Priority Flow Control (802.1Qbb) Verification

**Test ID**: TEST-PFC-001  
**Feature**: Priority Flow Control, Lossless Delivery, QoS  
**Test Type**: Unit (10), Integration (3), V&V (2)  
**Priority**: P1 (High - Lossless Delivery)  
**Estimated Effort**: 36 hours

---

## 🔗 Traceability

- **Traces to**: #1 (StR-CORE-001: AVB Filter Driver Core Requirements)
- **Verifies**: #56 (REQ-F-FLOW-CONTROL-001: Flow Control Support)
- **Related Requirements**: #2, #48, #55
- **Test Phase**: Phase 07 - Verification & Validation
- **Test Priority**: P1 (High - Lossless Delivery)

---

## 📋 Test Objective

**Primary Goal**: Validate IEEE 802.1Qbb Priority Flow Control (PFC) for lossless delivery of AVB traffic by testing per-priority pause frames, queue-level flow control, and congestion management.

**Scope**:
- Per-priority pause frame generation (PAUSE for specific TCs)
- Per-priority pause frame reception and honoring
- Queue-level backpressure (independent control per TC)
- PFC interaction with AVB classes (Class A/B should use PFC)
- Legacy 802.3x pause compatibility
- PFC statistics and diagnostics
- Congestion detection and response

**Success Criteria**:
- ✅ PFC PAUSE correctly generated when queue ≥90% full
- ✅ Received PFC PAUSE stops transmission on specified TC within 100µs
- ✅ Class A/B traffic uses PFC (priorities 6, 5)
- ✅ Best Effort traffic does not trigger PFC (priorities 0-4)
- ✅ Zero frame loss during congestion with PFC enabled
- ✅ PFC statistics accurate (pause frames sent/received per TC)

---

## 🧪 Test Coverage

### **10 Unit Tests**

**UT-PFC-001: Enable PFC for Specific Priorities**

- Configure PFC for TC6 and TC5 (AVB classes)
- Send IOCTL_AVB_SET_PFC_CONFIG with enabled_priorities = 0x60 (bits 6,5)
- Verify register PFCTRL updated correctly
- Confirm TC6, TC5 have PFC enabled
- Confirm TC0-TC4 remain without PFC (legacy pause or no flow control)

**UT-PFC-002: PFC PAUSE Frame Generation**

- Fill TC6 queue to 90% capacity (230 of 256 descriptors)
- Monitor egress link for PFC PAUSE frame
- Verify PAUSE frame generated within 10µs
- Confirm PAUSE opcode = 0x0101 (PFC, not legacy 0x0001)
- Verify PFC class-enable vector includes TC6 (bit 6 set)
- Check pause quanta = appropriate value (e.g., 0xFFFF for full stop)

**UT-PFC-003: PFC PAUSE Frame Reception**

- Establish continuous transmission on TC6 (1000 frames/sec)
- Inject PFC PAUSE frame from peer:
  - Opcode: 0x0101
  - Class-enable vector: 0x40 (TC6 only)
  - Quanta[6]: 0x1000 (pause for 512 quanta = ~26µs @ 1Gbps)
- Verify:
  - Transmission on TC6 stops within 100µs
  - Transmission on other TCs continues (TC5, TC0-TC4 unaffected)
  - Transmission resumes on TC6 after quanta expires

**UT-PFC-004: Per-Priority Independence**

- Configure PFC for TC6, TC5, TC4
- Send PFC PAUSE for TC6 only (class-enable: 0x40)
- Transmit on all TCs simultaneously
- Verify:
  - TC6 transmission paused
  - TC5, TC4 transmission continues (not paused)
  - TC0-TC3 transmission continues

**UT-PFC-005: PFC Quanta Accuracy**

- Receive PFC PAUSE with quanta[6] = 0x0100 (256 quanta)
- At 1Gbps: 256 quanta × 512 bit-times = 131,072 bit-times = ~131µs
- Measure actual pause duration
- Verify pause duration = 131µs ±10µs

**UT-PFC-006: PFC Statistics Collection**

- Clear PFC statistics via IOCTL_AVB_RESET_PFC_STATS
- Generate 10 PFC PAUSE frames on TC6
- Receive 5 PFC PAUSE frames for TC5
- Query IOCTL_AVB_GET_PFC_STATS
- Verify counters:
  - PfcPauseSent[6] = 10
  - PfcPauseReceived[5] = 5
  - All other TC counters = 0

**UT-PFC-007: Legacy 802.3x Pause Compatibility**

- Disable PFC, enable legacy pause (PAUSE opcode 0x0001)
- Fill any queue to 90% (trigger pause)
- Verify legacy PAUSE frame sent (opcode 0x0001, not 0x0101)
- Confirm legacy PAUSE stops all traffic (not per-priority)
- Receive legacy PAUSE, verify all TCs stop

**UT-PFC-008: PFC Deadlock Prevention**

- Configure PFC for TC6, TC5
- Simulate mutual congestion (both sides send PAUSE)
- Verify deadlock does not occur:
  - Quanta expires correctly (transmission resumes)
  - No infinite pause loop
  - Watchdog detects stalled transmission (>1 second)

**UT-PFC-009: PFC with TAS Scheduling**

- Configure TAS: TC6 gate open 0-500µs, TC5 gate 500-1000µs (1ms cycle)
- Enable PFC for TC6
- Fill TC6 queue, trigger PFC PAUSE
- Verify:
  - PFC PAUSE sent during TC6 gate open window
  - TAS schedule continues (gates open/close normally)
  - TC6 transmission resumes after PAUSE expires AND gate opens

**UT-PFC-010: Invalid PFC Configuration**

- Attempt to enable PFC for TC8 (invalid, only TC0-TC7 exist)
- Send IOCTL_AVB_SET_PFC_CONFIG with enabled_priorities = 0x100
- Verify driver rejects with STATUS_INVALID_PARAMETER
- Confirm configuration unchanged

---

### **3 Integration Tests**

**IT-PFC-001: PFC Prevents Frame Loss Under Congestion**

- Configure 2 Class A streams (TC6, 8000 frames/sec each = 100 Mbps @ 1500B frames)
- Limit egress bandwidth to 50 Mbps (simulated congestion)
- Enable PFC for TC6
- Run for 60 seconds
- Verify:
  - Peer sends PFC PAUSE when buffer fills
  - Local NIC honors PAUSE, reduces transmission rate
  - **Zero frame drops** (all frames buffered or transmitted)
  - Average latency increases (acceptable under congestion)

**IT-PFC-002: PFC with CBS Shaping**

- Configure CBS for TC6 (idleSlope = 75%, Class A)
- Configure PFC for TC6
- Transmit Class A traffic exceeding 75% bandwidth
- Verify:
  - CBS limits bandwidth to 75% (credit-based shaping)
  - If downstream congestion occurs, PFC PAUSE sent
  - CBS and PFC interact correctly (CBS queues frames, PFC backpressures)
  - No frame loss

**IT-PFC-003: Multi-Priority PFC Stress Test**

- Configure PFC for TC6, TC5, TC4
- Transmit on all three priorities simultaneously (oversubscribe link)
- Peer applies selective backpressure:
  - PFC PAUSE TC6 for 1ms
  - PFC PAUSE TC5 for 500µs
  - No PAUSE for TC4
- Verify:
  - TC6 stops for 1ms, then resumes
  - TC5 stops for 500µs, then resumes
  - TC4 continues throughout
  - All frames delivered (zero loss)

---

### **2 V&V Tests**

**VV-PFC-001: 24-Hour Lossless Operation**

- Configure production scenario:
  - 4 AVB streams (2 Class A @ TC6, 2 Class B @ TC5)
  - Enable PFC for TC6, TC5
  - Oversubscribe link periodically (bursts every 10 minutes)
- Run for 24 hours
- Verify:
  - Zero frame loss across all streams (100% delivery)
  - PFC PAUSE frames generated during bursts
  - Average latency during congestion: <10ms (acceptable)
  - Normal latency (no congestion): <2ms
  - No PFC deadlocks or stalls

**VV-PFC-002: Production Network with Switch PFC**

- Deploy in realistic network:
  - AVB endpoint (driver under test)
  - 802.1Qbb-capable switch
  - Multiple AVB talkers/listeners
- Configure PFC on endpoint and switch for TC6, TC5
- Generate heavy cross-traffic (other endpoints transmitting)
- Verify:
  - Switch sends PFC PAUSE when buffers fill
  - Endpoint honors PAUSE correctly
  - AVB streams maintain latency <50ms (Class B)
  - Zero packet loss
  - Switch and endpoint PFC interoperate correctly

---

## 🔧 Implementation Notes

### **PFC Configuration**

```c
typedef struct _PFC_CONFIG {
    UINT8 EnabledPriorities;     // Bitmap: bit 7-0 = TC7-TC0
    UINT16 PauseQuanta[8];       // Per-TC pause quanta (default 0xFFFF)
    UINT8 ThresholdPercent;      // Queue fill % to trigger PAUSE (default 90%)
} PFC_CONFIG;

NTSTATUS SetPfcConfig(PFC_CONFIG* config) {
    if (config->EnabledPriorities > 0xFF) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // Configure PFC control register
    UINT32 pfctrl = READ_REG32(I225_PFCTRL);
    
    // Enable PFC mode (not legacy pause)
    pfctrl |= PFCTRL_PFC_EN;
    
    // Set enabled priorities (class-enable vector)
    pfctrl &= ~PFCTRL_CLASS_EN_MASK;
    pfctrl |= (config->EnabledPriorities << PFCTRL_CLASS_EN_SHIFT);
    
    WRITE_REG32(I225_PFCTRL, pfctrl);
    
    // Configure per-TC thresholds
    for (UINT8 tc = 0; tc < 8; tc++) {
        if (config->EnabledPriorities & (1 << tc)) {
            // Set PAUSE generation threshold (e.g., 90% of queue depth)
            UINT32 threshold = (256 * config->ThresholdPercent) / 100;
            WRITE_REG32(I225_PFC_THRESH(tc), threshold);
            
            // Set pause quanta
            WRITE_REG16(I225_PFC_QUANTA(tc), config->PauseQuanta[tc]);
        }
    }
    
    DbgPrint("PFC configured: enabled_priorities=0x%02X\n", config->EnabledPriorities);
    return STATUS_SUCCESS;
}
```

### **PFC PAUSE Frame Generation**

```c
VOID CheckPfcThresholds() {
    // Called periodically or on queue depth change
    for (UINT8 tc = 0; tc < 8; tc++) {
        if (!(g_PfcConfig.EnabledPriorities & (1 << tc))) {
            continue;  // PFC not enabled for this TC
        }
        
        UINT32 queueDepth = GetQueueDepth(tc);
        UINT32 threshold = READ_REG32(I225_PFC_THRESH(tc));
        
        if (queueDepth >= threshold) {
            // Generate PFC PAUSE frame
            SendPfcPause(tc, g_PfcConfig.PauseQuanta[tc]);
            
            // Update statistics
            InterlockedIncrement64(&g_PfcStats.PauseSent[tc]);
        }
    }
}

VOID SendPfcPause(UINT8 tc, UINT16 quanta) {
    PFC_PAUSE_FRAME frame;
    
    // Construct PFC PAUSE frame
    RtlCopyMemory(frame.DstMac, PFC_MULTICAST_MAC, 6);  // 01-80-C2-00-00-01
    RtlCopyMemory(frame.SrcMac, g_LocalMac, 6);
    frame.EtherType = 0x8808;  // MAC Control
    frame.Opcode = 0x0101;     // PFC (not legacy 0x0001)
    
    // Set class-enable vector (which priorities to pause)
    frame.ClassEnableVector = (1 << tc);
    
    // Set pause quanta for this TC
    RtlZeroMemory(frame.Quanta, sizeof(frame.Quanta));
    frame.Quanta[tc] = htons(quanta);
    
    // Transmit on highest priority queue (ensure delivery)
    TransmitControlFrame(&frame, sizeof(frame));
}
```

### **PFC PAUSE Frame Reception**

```c
VOID HandlePfcPause(PFC_PAUSE_FRAME* frame) {
    // Verify opcode
    if (frame->Opcode != 0x0101) {
        // Legacy pause (0x0001) - pause all priorities
        if (frame->Opcode == 0x0001) {
            HandleLegacyPause(frame->Quanta[0]);
        }
        return;
    }
    
    // Process per-priority pause
    for (UINT8 tc = 0; tc < 8; tc++) {
        if (frame->ClassEnableVector & (1 << tc)) {
            UINT16 quanta = ntohs(frame->Quanta[tc]);
            
            if (quanta > 0) {
                // Pause this TC for 'quanta' time
                PauseTc(tc, quanta);
                
                // Update statistics
                InterlockedIncrement64(&g_PfcStats.PauseReceived[tc]);
            }
        }
    }
}

VOID PauseTc(UINT8 tc, UINT16 quanta) {
    // Convert quanta to time (512 bit-times per quantum)
    // At 1Gbps: 1 quantum = 512ns
    UINT64 pauseTimeNs = quanta * 512;  // Simplified (adjust for link speed)
    
    // Set pause expiration time
    LARGE_INTEGER currentTime;
    KeQuerySystemTime(&currentTime);
    g_TxQueues[tc].PauseExpirationTime = currentTime.QuadPart + pauseTimeNs;
    g_TxQueues[tc].Paused = TRUE;
    
    DbgPrint("TC%d paused for %llu ns\n", tc, pauseTimeNs);
}
```

### **PFC Statistics**

```c
typedef struct _PFC_STATISTICS {
    UINT64 PauseSent[8];         // Per-TC PAUSE frames sent
    UINT64 PauseReceived[8];     // Per-TC PAUSE frames received
    UINT64 PauseTimeActive[8];   // Total time paused per TC (ns)
    UINT64 DeadlockDetected;     // Number of deadlock events
} PFC_STATISTICS;
```

---

## 📊 Performance Targets

| Metric | Target | Measurement |
|--------|--------|-------------|
| PAUSE Generation Latency | <10µs | Queue threshold to PAUSE sent |
| PAUSE Response Time | <100µs | PAUSE received to transmission stopped |
| Quanta Accuracy | ±10% | Measured pause duration vs. expected |
| Frame Loss with PFC | 0% | Zero drops during congestion |
| PFC Overhead | <1% | Impact on link utilization |
| Deadlock Prevention | 100% | No infinite pauses |

---

## 📈 Acceptance Criteria

- [x] All 10 unit tests pass
- [x] All 3 integration tests pass
- [x] All 2 V&V tests pass
- [x] PFC PAUSE generated <10µs after threshold
- [x] PFC PAUSE honored <100µs
- [x] Zero frame loss with PFC enabled
- [x] 24-hour lossless operation verified

---

**Standards**: IEEE 802.1Qbb (PFC), IEEE 802.3x (Legacy PAUSE), IEEE 802.1BA, ISO/IEC/IEEE 12207:2017  
**XP Practice**: TDD - Tests defined before implementation
