# TEST-TAS-CONFIG-001: Time-Aware Shaper Configuration Verification

## 🔗 Traceability

- Traces to: #1 (StR-CORE-001: AVB Filter Driver Core Requirements)
- Verifies: #8 (REQ-F-TAS-001: Time-Aware Shaper Support)
- Related Requirements: #7, #149, #58
- Test Phase: Phase 07 - Verification & Validation
- Test Priority: P1 (High - TSN Traffic Shaping)

---

## 📋 Test Objective

**Primary Goal**: Validate Time-Aware Shaper (TAS) configuration per IEEE 802.1Qbv for scheduled traffic gate control and deterministic latency.

**Scope**:
- Gate Control List (GCL) configuration (up to 256 entries)
- Per-traffic class gate open/close scheduling
- Base time and cycle time configuration
- Gate state transitions synchronized to PHC
- Integration with launch time and target time features

**Success Criteria**:
- ✅ GCL configuration accepted for up to 256 entries
- ✅ Gate transitions occur within ±100ns of scheduled time
- ✅ Traffic class isolation (TC0-TC7 independent scheduling)
- ✅ Cycle time accuracy ±1µs over 1 hour
- ✅ Zero packet loss during correct gate open windows

---

## 🧪 Test Coverage

### **10 Unit Tests**

**UT-TAS-001: Basic GCL Configuration (8 Entries)**
- Configure 8-entry GCL with alternating TC0/TC1 windows
- Each window 125µs duration (8 Hz gPTP-aligned)
- Verify register writes to BASET, CYCLETIME, GCL entries
- Confirm gate states match configuration

**UT-TAS-002: Maximum GCL Size (256 Entries)**
- Configure maximum 256-entry GCL
- Mix all 8 traffic classes across entries
- Verify all entries stored correctly in hardware
- Confirm no truncation or overflow

**UT-TAS-003: Minimum Gate Window (1µs)**
- Configure GCL with 1µs gate open windows
- Verify hardware accepts minimum duration
- Measure actual gate open time (should be ≥1µs)

**UT-TAS-004: Maximum Gate Window (1 Second)**
- Configure GCL with 1-second gate open window
- Verify hardware accepts and applies long duration
- Confirm gate remains open for full duration

**UT-TAS-005: Base Time Synchronization**
- Set base time to future PHC timestamp (+10ms)
- Verify GCL activation delayed until base time
- Measure actual start time vs. base time (±100ns)

**UT-TAS-006: Cycle Time Accuracy**
- Configure 1ms cycle time with 4 GCL entries
- Run for 1000 cycles (1 second)
- Measure cycle duration: Mean 1ms ±1µs, jitter <100ns

**UT-TAS-007: Per-TC Gate Control**
- Configure TC0 gate open, all other TCs closed
- Transmit packets from all 8 TCs
- Verify only TC0 packets transmitted, others held

**UT-TAS-008: Gate State Transition Timing**
- Configure alternating 100µs windows (TC0/TC1)
- Measure gate transition edges with GPIO/oscilloscope
- Verify transitions occur within ±100ns of schedule

**UT-TAS-009: GCL Update During Operation**
- Start TAS with initial GCL (4 entries)
- Update GCL with new configuration (8 entries)
- Verify seamless transition at next cycle boundary
- Confirm no packet loss during update

**UT-TAS-010: TAS Disable and Re-Enable**
- Configure and enable TAS
- Disable TAS (all gates open)
- Re-enable with same configuration
- Verify correct resumption at next base time

---

### **3 Integration Tests**

**IT-TAS-001: TAS + Launch Time Coordination**
- Configure TAS with 8-entry GCL (125µs windows)
- Use launch time to schedule packets within open windows
- Verify all packets transmitted during correct windows
- Measure zero packet loss, zero gate violations

**IT-TAS-002: TAS Multi-TC Scheduling (802.1Qbv Example)**
- Configure realistic TSN schedule:
  - TC7 (Control): 10µs every 1ms
  - TC6 (Class A): 125µs every 1ms
  - TC5 (Class B): 250µs every 1ms
  - TC0-4 (Best Effort): Remaining time
- Run for 10 seconds with traffic on all TCs
- Verify schedule adherence: All packets in correct windows

**IT-TAS-003: TAS + gPTP Synchronization**
- Align TAS base time with gPTP sync messages (8 Hz)
- Configure GCL to open TC6 gate during Sync transmission
- Run for 1 hour with active gPTP
- Verify Sync messages never blocked by TAS gates

---

### **2 V&V Tests**

**VV-TAS-001: 24-Hour TAS Stability**
- Configure TAS with production schedule (256 entries)
- Run for 24 hours with line-rate traffic
- Measure:
  - Gate transition accuracy (±100ns maintained)
  - Cycle time drift (<1µs per hour)
  - Zero unexpected gate violations
  - No packet loss in open windows

**VV-TAS-002: Production TSN Workload**
- Configure 802.1Qbv schedule for industrial control:
  - 500µs cycle time
  - 4 TCs with deterministic windows
  - Jitter budget: <1µs end-to-end
- Run for 1 hour with mixed traffic:
  - Control messages: 10 Mbps
  - Video stream: 100 Mbps
  - Best effort: 500 Mbps
- Verify:
  - Control latency <100µs (99.99th percentile)
  - Zero missed control deadlines
  - Video jitter <10µs

---

## 🔧 Implementation Notes

### **TAS Register Configuration (i225)**

```c
#define I225_BASET_L 0x3578     // Base Time Low (64-bit)
#define I225_BASET_H 0x357C     // Base Time High
#define I225_CYCLETIME 0x3580   // Cycle Time (32-bit, nanoseconds)
#define I225_GCLL 0x3594        // Gate Control List Low
#define I225_GCLH 0x3598        // Gate Control List High

typedef struct _GCL_ENTRY {
    UINT32 TimeInterval;  // Duration in nanoseconds
    UINT8 GateStates;     // Bitmap: bit[i] = TC[i] gate state (1=open, 0=closed)
} GCL_ENTRY;

VOID ConfigureTAS(UINT64 baseTime, UINT32 cycleTime, GCL_ENTRY* entries, UINT32 count) {
    // Set base time
    WRITE_REG32(I225_BASET_L, (UINT32)(baseTime & 0xFFFFFFFF));
    WRITE_REG32(I225_BASET_H, (UINT32)(baseTime >> 32));
    
    // Set cycle time
    WRITE_REG32(I225_CYCLETIME, cycleTime);
    
    // Write GCL entries
    for (UINT32 i = 0; i < count; i++) {
        WRITE_REG32(I225_GCLL, (entries[i].TimeInterval << 8) | entries[i].GateStates);
        WRITE_REG32(I225_GCLH, i); // Entry index
    }
    
    // Enable TAS
    UINT32 ctrl = READ_REG32(I225_TQAVCTRL);
    ctrl |= (1 << 0); // Enable TAS
    WRITE_REG32(I225_TQAVCTRL, ctrl);
}
```

---

## 📊 Performance Targets

| Metric | Target | Measurement |
|--------|--------|-------------|
| Gate Transition Accuracy | ±100ns | GPIO timing measurement |
| Cycle Time Accuracy | ±1µs | Long-term average |
| Maximum GCL Entries | 256 | Hardware limit |
| Minimum Gate Window | 1µs | Shortest schedulable |
| Gate State Jitter | <100ns | Stddev of transitions |
| Configuration Latency | <1ms | Enable to first cycle |

---

## 📈 Acceptance Criteria

- [ ] All 10 unit tests pass
- [ ] All 3 integration tests pass
- [ ] All 2 V&V tests pass
- [ ] GCL configuration up to 256 entries
- [ ] Gate transitions within ±100ns
- [ ] Cycle time accuracy ±1µs
- [ ] Zero packet loss in open windows
- [ ] 24-hour stability test passes

---

**Standards**: IEEE 802.1Qbv (TAS), IEEE 1012-2016, ISO/IEC/IEEE 12207:2017  
**XP Practice**: TDD - Tests defined before implementation
