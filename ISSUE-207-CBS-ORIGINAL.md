# TEST-CBS-CONFIG-001: Credit-Based Shaper Configuration Verification

## 🔗 Traceability

- Traces to: #1 (StR-CORE-001: AVB Filter Driver Core Requirements)
- Verifies: #8 (REQ-F-QAV-001: Credit-Based Shaper Support)
- Related Requirements: #8, #149, #58
- Test Phase: Phase 07 - Verification & Validation
- Test Priority: P1 (High - AVB Traffic Shaping)

---

## 📋 Test Objective

**Primary Goal**: Validate Credit-Based Shaper (CBS) configuration per IEEE 802.1Qav for AVB Class A/B traffic bandwidth reservation and latency guarantees.

**Scope**:
- CBS algorithm implementation (idle slope, send slope, hi/lo credit)
- Per-traffic class bandwidth reservation (Class A: 75%, Class B: 75%)
- Credit accumulation and depletion rates
- Integration with SR (Stream Reservation Protocol)
- Multi-stream scheduling fairness

**Success Criteria**:
- ✅ Bandwidth reservation accurate within ±1% of configured rate
- ✅ Class A max latency ≤2ms (125µs observation interval)
- ✅ Class B max latency ≤50ms (250µs observation interval)
- ✅ Credit accumulation/depletion rates match idle/send slopes
- ✅ Fair scheduling across 4 concurrent AVB streams

---

## 🧪 Test Coverage

### **10 Unit Tests**

**UT-CBS-001: Idle Slope Configuration (Class A)**
- Configure CBS for Class A (TC6): 75% bandwidth
- Set idle slope for 100 Mbps link (75 Mbps reserved)
- Verify TQAVCC register values match calculations
- Confirm credit accumulates at idle slope rate when queue empty

**UT-CBS-002: Send Slope Configuration (Class A)**
- Configure send slope = -25% (100 Mbps - 75 Mbps)
- Transmit Class A packets at line rate
- Measure credit depletion rate
- Verify matches calculated send slope (±1%)

**UT-CBS-003: Hi Credit Limit (maxInterferenceSize)**
- Set hi credit = maxInterferenceSize (1522 bytes for Ethernet)
- Transmit until hi credit reached
- Verify transmission stops when hi credit limit hit
- Confirm credit accumulation resumes when transmission paused

**UT-CBS-004: Lo Credit Limit**
- Configure lo credit = -(sendSlope × observationInterval)
- Deplete credits to lo credit limit
- Verify queue blocked until credits recover
- Measure recovery time matches idle slope

**UT-CBS-005: Dual CBS (Class A + Class B)**
- Configure CBS for both TC6 (Class A) and TC5 (Class B)
- Class A: 75% @ 125µs interval
- Class B: 75% @ 250µs interval
- Verify independent credit tracking for each class
- Confirm no cross-interference

**UT-CBS-006: CBS + Best Effort Coexistence**
- Configure CBS for Class A (TC6)
- Transmit simultaneous Class A and BE (TC0) traffic
- Verify Class A gets reserved bandwidth (75%)
- Confirm BE traffic fills remaining 25% capacity

**UT-CBS-007: Credit Accumulation During Idle**
- Configure CBS, transmit no packets for 10ms
- Measure credit accumulation over idle period
- Verify accumulation rate = idle slope
- Confirm credit caps at hi credit limit

**UT-CBS-008: Zero Bandwidth Reservation**
- Set idle slope = 0 (disable CBS for queue)
- Verify queue behaves as strict priority
- Confirm no credit accumulation/depletion

**UT-CBS-009: Link Speed Change (100M → 1G)**
- Configure CBS for 100 Mbps link
- Simulate link speed change to 1 Gbps
- Verify CBS parameters recalculated automatically
- Confirm bandwidth reservation percentage maintained

**UT-CBS-010: CBS Register Read-Back**
- Write CBS configuration to TQAVCC/TQAVHC/TQAVLC registers
- Read back all register values
- Verify bit-accurate storage and retrieval

---

### **3 Integration Tests**

**IT-CBS-001: AVB Class A Stream (8 kHz Audio)**
- Configure CBS for Class A: 75% of 100 Mbps
- Transmit 8 kHz audio stream (24-bit, 48 channels)
- Bandwidth: ~9 Mbps
- Measure end-to-end latency over 10 seconds
- Verify max latency ≤2ms, jitter <125µs

**IT-CBS-002: Multi-Stream Fairness (4 Class A Streams)**
- Configure 4 concurrent Class A streams
- Each stream: ~18 Mbps (total 72 Mbps < 75% reservation)
- Run for 1 minute
- Verify all streams get equal bandwidth (±2%)
- Confirm no stream starvation

**IT-CBS-003: CBS + TAS Integration**
- Configure TAS with 125µs windows for Class A
- Configure CBS with 75% bandwidth reservation
- Transmit Class A traffic during TAS open windows
- Verify CBS and TAS work together correctly
- Confirm Class A traffic never exceeds TAS window or CBS reservation

---

### **2 V&V Tests**

**VV-CBS-001: 24-Hour AVB Production Workload**
- Configure CBS for Class A and Class B
- Transmit 8 Class A + 4 Class B streams continuously
- Monitor for 24 hours
- Verify:
  - Zero packet loss for AVB traffic
  - Class A latency always ≤2ms
  - Class B latency always ≤50ms
  - Bandwidth reservation maintained (±1%)

**VV-CBS-002: AVB Talker/Listener Simulation**
- Simulate 802.1BA AVB system:
  - 4 talkers (2 Class A, 2 Class B)
  - 8 listeners
  - Mixed audio/video traffic
- Run for 1 hour
- Measure:
  - Stream delivery ratio: ≥99.99%
  - Latency bounds met: 100%
  - Bandwidth fairness: ±2%

---

## 🔧 Implementation Notes

### **CBS Register Configuration (i225)**

```c
#define I225_TQAVCTRL 0x3570 // Qav Control
#define I225_TQAVCC_TC6 0x3590 // Qav Credit Control (Class A, TC6)
#define I225_TQAVCC_TC5 0x3594 // Qav Credit Control (Class B, TC5)
#define I225_TQAVHC_TC6 0x35A0 // Qav Hi Credit (TC6)
#define I225_TQAVLC_TC6 0x35A4 // Qav Lo Credit (TC6)

typedef struct _CBS_CONFIG {
    UINT32 IdleSlope; // Credits/sec when queue idle
    INT32 SendSlope; // Credits/sec when transmitting (negative)
    UINT32 HiCredit; // Maximum credit accumulation
    INT32 LoCredit; // Maximum credit depletion (negative)
} CBS_CONFIG;

// Calculate CBS parameters for Class A (75% @ 100 Mbps)
CBS_CONFIG CalculateCBS_ClassA(UINT32 linkSpeedBps, UINT32 reservationPercent) {
    CBS_CONFIG config;
    
    // Idle slope = reservation % of link speed
    config.IdleSlope = (linkSpeedBps * reservationPercent) / 100;
    
    // Send slope = -(100% - reservation%) of link speed
    config.SendSlope = -((INT32)linkSpeedBps * (100 - reservationPercent)) / 100;
    
    // Hi credit = maxInterferenceSize (1522 bytes * 8 bits)
    config.HiCredit = 1522 * 8;
    
    // Lo credit = sendSlope × observationInterval (125µs for Class A)
    config.LoCredit = (config.SendSlope * 125) / 1000000; // 125µs
    
    return config;
}

VOID ConfigureCBS(UINT8 trafficClass, CBS_CONFIG config) {
    UINT32 tcOffset = (trafficClass == 6) ? I225_TQAVCC_TC6 : I225_TQAVCC_TC5;
    
    // Write idle and send slopes
    UINT32 cc = ((config.IdleSlope & 0xFFFF) << 16) | (config.SendSlope & 0xFFFF);
    WRITE_REG32(tcOffset, cc);
    
    // Write hi/lo credit limits
    WRITE_REG32(tcOffset + 0x10, config.HiCredit);
    WRITE_REG32(tcOffset + 0x14, config.LoCredit);
    
    // Enable CBS for traffic class
    UINT32 ctrl = READ_REG32(I225_TQAVCTRL);
    ctrl |= (1 << trafficClass); // Enable CBS for TC
    WRITE_REG32(I225_TQAVCTRL, ctrl);
}
```

---

## 📊 Performance Targets

| Metric | Target | Measurement |
|--------|--------|-------------|
| Bandwidth Reservation Accuracy | ±1% | Long-term average |
| Class A Max Latency | ≤2ms | 99.99th percentile |
| Class B Max Latency | ≤50ms | 99.99th percentile |
| Credit Accumulation Rate | ±1% of idle slope | Measured over 1ms |
| Credit Depletion Rate | ±1% of send slope | Measured during TX |
| Multi-Stream Fairness | ±2% | Bandwidth distribution |

---

## 📈 Acceptance Criteria

- [ ] All 10 unit tests pass
- [ ] All 3 integration tests pass
- [ ] All 2 V&V tests pass
- [ ] Bandwidth reservation ±1% accurate
- [ ] Class A latency ≤2ms (100% compliance)
- [ ] Class B latency ≤50ms (100% compliance)
- [ ] Credit rates match idle/send slopes (±1%)
- [ ] 24-hour stability test passes
- [ ] Multi-stream fairness ±2%

---

**Standards**: IEEE 802.1Qav (CBS), IEEE 802.1BA (AVB), IEEE 1012-2016  
**XP Practice**: TDD - Tests defined before implementation
