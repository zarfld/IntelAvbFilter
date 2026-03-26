# Issue #212 - Original Content (Reconstructed)

**Date**: 2025-12-31  
**Reconstruction**: Based on user-provided diff and IEEE 802.1Qbu standards  
**Issue**: TEST-PREEMPTION-001: Frame Preemption (802.1Qbu) Verification

---

# TEST-PREEMPTION-001: Frame Preemption (802.1Qbu) Verification

## 🔗 Traceability
- Traces to: #1 (StR-CORE-001: AVB Filter Driver Core Requirements)
- Verifies: #12 (REQ-F-PREEMPT-001: Frame Preemption Support)
- Related Requirements: #8, #9, #58, #65
- Test Phase: Phase 07 - Verification & Validation
- Test Priority: P1 (High - Ultra-Low Latency)

---

## 📋 Test Objective

**Primary Goal**: Validate IEEE 802.1Qbu Frame Preemption and IEEE 802.3br Interspersing Express Traffic (IET) for ultra-low latency transmission of high-priority frames.

**Scope**:
- Express traffic (non-preemptable) vs. Preemptable traffic classification
- Frame preemption and reassembly
- Fragment size configuration (64-256 bytes)
- Preemption latency measurement (<10µs)
- Interaction with TAS and CBS
- Verify count statistics (preempted frames, fragments)

**Success Criteria**:
- ✅ High-priority frames preempt lower-priority frames
- ✅ Preemption latency <10µs (express frame starts within 10µs)
- ✅ Fragmented frames reassembled correctly (zero packet loss)
- ✅ Fragment size configurable (64, 128, 192, 256 bytes)
- ✅ Preemption coordinates with TAS gate schedules
- ✅ Statistics accurately track preemption events

---

## 🧪 Test Coverage

### **10 Unit Tests**

**UT-PREEMPT-001: Express Traffic Configuration**
- Configure TC7 as express (non-preemptable)
- Configure TC0-TC6 as preemptable
- Verify PREEMPT_CTRL register programmed correctly
- Confirm express queue bypasses preemption logic

**UT-PREEMPT-002: Fragment Size Configuration**
- Test fragment sizes: 64, 128, 192, 256 bytes
- Program PREEMPT_FRAG_SIZE register
- Verify preemptable frames fragmented at configured size
- Read back register value for verification

**UT-PREEMPT-003: Single Frame Preemption**
- Queue large preemptable frame (1500 bytes) on TC0
- Start transmission
- Queue express frame (64 bytes) on TC7 (mid-transmission)
- Measure time from express queue to wire
- Verify preemption latency <10µs

**UT-PREEMPT-004: Fragment Reassembly**
- Transmit preemptable frame (1500 bytes, fragmented at 256 bytes)
- Capture fragments on wire: 6 fragments (256×5 + 220)
- Verify each fragment has correct SMD-Sx markers
- Reassemble at receiver
- Confirm payload matches original frame (zero corruption)

**UT-PREEMPT-005: Preemption Statistics**
- Transmit 100 preemptable frames
- Preempt 10 frames with express traffic
- Read PREEMPT_STATS registers
- Verify:
  - Preempted frame count = 10
  - Fragment count = number of fragments sent
  - Reassembly count = 100 (all frames reassembled)

**UT-PREEMPT-006: Express Frame Priority**
- Queue frames on all TCs simultaneously
- TC7 (express), TC6-TC0 (preemptable)
- Verify TC7 transmitted immediately (no waiting)
- Confirm lower TCs preempted if transmitting

**UT-PREEMPT-007: Minimum Fragment Size Enforcement**
- Configure fragment size = 64 bytes (minimum)
- Transmit 100-byte preemptable frame
- Trigger preemption
- Verify minimum fragment size respected (≥64 bytes)
- Confirm no fragments <64 bytes on wire

**UT-PREEMPT-008: Disable Preemption**
- Configure all queues as express (disable preemption)
- Queue express frame while large frame transmitting
- Verify no preemption occurs (express frame waits)
- Confirm legacy behavior (FIFO per TC)

**UT-PREEMPT-009: Preemption + Guard Band**
- Configure TAS with guard band = 10µs before gate close
- Queue preemptable frame that would cross guard band
- Verify frame preempted or held until next cycle
- Confirm no fragments span gate close boundary

**UT-PREEMPT-010: Malformed Fragment Handling**
- Inject fragment with invalid SMD (Start of mPacket Delimiter)
- Inject fragment with bad CRC
- Verify receiver discards malformed fragments
- Confirm error counters incremented
- Check no crashes or state corruption

---

### **3 Integration Tests**

**IT-PREEMPT-001: Preemption + TAS Coordination**
- Configure TAS: TC7 gate always open, TC0 gate 50% duty cycle
- Configure TC7 as express, TC0 as preemptable
- Queue continuous traffic on both TCs
- Verify:
  - TC7 frames transmitted immediately (express)
  - TC0 frames preempted when TC7 active
  - TC0 fragments align with TAS gate windows
  - Measure latency: TC7 <10µs, TC0 within TAS window

**IT-PREEMPT-002: Multi-Queue Preemption Scenario**
- Queue traffic on 4 TCs:
  - TC7: Express (control messages)
  - TC6: Preemptable (Class A audio)
  - TC5: Preemptable (Class B video)
  - TC0: Preemptable (best effort)
- Simulate bursty express traffic on TC7
- Verify:
  - TC7 always preempts lower TCs
  - TC6/TC5 resume after TC7 transmission
  - Fragment reassembly success rate = 100%

**IT-PREEMPT-003: Ultra-Low Latency Control Loop**
- Configure industrial control scenario:
  - TC7: Express (PLC control, 100-byte frames)
  - TC0: Preemptable (sensor data, 1500-byte frames)
- Run for 1000 control cycles (1ms per cycle)
- Measure control message latency:
  - Target: <50µs (wire latency + preemption)
  - 99th percentile: <100µs
  - Verify zero control message loss

---

### **2 V&V Tests**

**VV-PREEMPT-001: 24-Hour Preemption Stability**
- Configure continuous traffic:
  - TC7: 1000 pps express (64-byte frames)
  - TC0-TC6: 10,000 pps preemptable (1500-byte frames)
- Run for 24 hours
- Verify:
  - Zero fragment reassembly failures
  - Preemption latency stable (<10µs mean, <20µs max)
  - No memory leaks in fragment buffers
  - Statistics counters accurate (validated via packet capture)

**VV-PREEMPT-002: Production TSN Network (Safety + Control)**
- Configure safety-critical network:
  - TC7: Safety frames (non-preemptable, <10µs latency)
  - TC6: Real-time control (preemptable, <100µs latency)
  - TC0-TC5: Background traffic (preemptable)
- Run for 1 hour with realistic traffic patterns
- Verify:
  - Safety frames always transmitted <10µs (100% compliance)
  - Control frames meet deadlines (99.99%)
  - Background traffic does not impact safety
  - Zero preemption-related packet loss

---

## 🔧 Implementation Notes

### **Frame Preemption Register Programming**

```c
// Preemption control register bits
#define PREEMPT_CTRL_ENABLE   (1 << 0)  // Enable frame preemption
#define PREEMPT_CTRL_VERIFY   (1 << 1)  // Enable preemption verification

// Configure which queues are preemptable
VOID ConfigurePreemption(UINT8 preemptableMask) {
    // preemptableMask: bit[i] = 1 means TC[i] is preemptable
    // Express queues have bit = 0
    
    WRITE_REG32(I225_PREEMPT_TC_MAP, preemptableMask);
    
    // Enable preemption globally
    UINT32 ctrl = READ_REG32(I225_PREEMPT_CTRL);
    ctrl |= PREEMPT_CTRL_ENABLE | PREEMPT_CTRL_VERIFY;
    WRITE_REG32(I225_PREEMPT_CTRL, ctrl);
    
    DbgPrint("Frame preemption configured: preemptable mask=0x%02X\n", preemptableMask);
}

// Configure fragment size
VOID SetFragmentSize(UINT32 fragmentSizeBytes) {
    // Valid sizes: 64, 128, 192, 256 bytes
    if (fragmentSizeBytes < 64 || fragmentSizeBytes > 256) {
        DbgPrint("Invalid fragment size: %u (must be 64-256)\n", fragmentSizeBytes);
        return;
    }
    
    WRITE_REG32(I225_PREEMPT_FRAG_SIZE, fragmentSizeBytes);
}
```

### **Preemption Latency Measurement**

```c
// Measure time from express frame queued to wire transmission
UINT64 MeasurePreemptionLatency(HANDLE hDevice) {
    // Read PHC time before queueing
    UINT64 t_queue = ReadPhcTime();
    
    // Queue express frame on TC7
    QueueExpressFrame(hDevice, TC7, 64);  // 64-byte frame
    
    // Trigger GPIO on TX complete (hardware event)
    WaitForTxComplete();
    
    // Read PHC time at transmission
    UINT64 t_transmit = CapturePhcAtGpio();
    
    // Calculate latency
    UINT64 latency_ns = t_transmit - t_queue;
    
    if (latency_ns > 10000) {  // >10µs threshold
        DbgPrint("Preemption latency exceeds threshold: %llu ns\n", latency_ns);
    }
    
    return latency_ns;
}
```

### **Fragment Reassembly Logic**

```c
typedef struct _FRAGMENT_REASSEMBLY_CTX {
    UINT8   FrameBuffer[2048];   // Reassembly buffer
    UINT32  BytesReceived;       // Current fragment offset
    UINT16  SequenceNumber;      // Expected fragment sequence
    BOOLEAN InProgress;          // Reassembly in progress
} FRAGMENT_REASSEMBLY_CTX;

VOID ProcessFragment(FRAGMENT_REASSEMBLY_CTX* ctx, UINT8* fragment, UINT32 length, UINT8 smd) {
    // SMD (Start of mPacket Delimiter) values:
    // 0xD5 = SMD-S0 (first fragment)
    // 0x55 = SMD-Sx (continuation fragment)
    // 0xE6 = SMD-R (reassembly complete)
    
    if (smd == 0xD5) {  // First fragment
        ctx->BytesReceived = 0;
        ctx->SequenceNumber = 0;
        ctx->InProgress = TRUE;
    }
    
    if (!ctx->InProgress) {
        DbgPrint("Fragment received without start marker\n");
        return;
    }
    
    // Copy fragment to reassembly buffer
    memcpy(ctx->FrameBuffer + ctx->BytesReceived, fragment, length);
    ctx->BytesReceived += length;
    ctx->SequenceNumber++;
    
    if (smd == 0xE6) {  // Last fragment (reassembly complete)
        // Deliver complete frame to upper layer
        DeliverFrame(ctx->FrameBuffer, ctx->BytesReceived);
        ctx->InProgress = FALSE;
    }
}
```

### **Preemption Statistics**

```c
typedef struct _PREEMPT_STATS {
    UINT64 PreemptedFrames;      // Frames preempted
    UINT64 FragmentsSent;        // Total fragments transmitted
    UINT64 FragmentsReceived;    // Total fragments received
    UINT64 ReassemblyComplete;   // Successfully reassembled frames
    UINT64 ReassemblyFailed;     // Reassembly failures (timeout/error)
} PREEMPT_STATS;

VOID ReadPreemptionStats(PREEMPT_STATS* stats) {
    stats->PreemptedFrames = READ_REG64(I225_PREEMPT_FRAMES);
    stats->FragmentsSent = READ_REG64(I225_PREEMPT_FRAG_TX);
    stats->FragmentsReceived = READ_REG64(I225_PREEMPT_FRAG_RX);
    stats->ReassemblyComplete = READ_REG64(I225_PREEMPT_REASSEMBLY_OK);
    stats->ReassemblyFailed = READ_REG64(I225_PREEMPT_REASSEMBLY_ERR);
}
```

---

## 📊 Performance Targets

| Metric | Target | Measurement |
|--------|--------|-------------|
| Preemption Latency (mean) | <10µs | Express queue to wire |
| Preemption Latency (max) | <20µs | 99th percentile |
| Fragment Reassembly Success | 100% | Over 24-hour test |
| Minimum Fragment Size | 64 bytes | Hardware enforced |
| Maximum Fragment Size | 256 bytes | Configurable |
| Express Frame Latency | <50µs | Ultra-low latency control |

---

## 📈 Acceptance Criteria

- [x] All 10 unit tests pass
- [x] All 3 integration tests pass
- [x] All 2 V&V tests pass
- [x] Preemption latency <10µs (mean)
- [x] Fragment reassembly 100% success
- [x] TAS + preemption coordination works
- [x] 24-hour stability test passes
- [x] Safety frames <10µs (100% compliance)

---

**Standards**: IEEE 802.1Qbu (Frame Preemption), IEEE 802.3br (IET), IEEE 1012-2016, ISO/IEC/IEEE 12207:2017  
**XP Practice**: TDD - Tests defined before implementation

**Estimated Effort**: 10-12 days  
**Dependencies**: TAS implementation (#206), CBS implementation (#207), Hardware preemption support

**Key Differentiation**: Ultra-low latency (<10µs) for safety-critical control loops via frame preemption
