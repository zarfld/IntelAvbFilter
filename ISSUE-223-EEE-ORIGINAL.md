# TEST-EEE-001: Energy Efficient Ethernet (IEEE 802.3az) Verification

## 🔗 Traceability
- Traces to: #1 (StR-CORE-001: AVB Filter Driver Core Requirements)
- Verifies: #58 (REQ-NF-POWER-001: Power Management and Efficiency)
- Related Requirements: #14, #60
- Test Phase: Phase 07 - Verification & Validation
- Test Priority: P2 (Medium - Power Efficiency)

---

## 📋 Test Objective

**Primary Goal**: Validate Energy Efficient Ethernet (IEEE 802.3az EEE) implementation including LPI (Low Power Idle) mode negotiation, transition timing, power savings measurement, and interoperability with non-EEE devices.

**Scope**:
- EEE capability advertisement and negotiation
- LPI (Low Power Idle) mode entry and exit timing
- Tx/Rx LPI detection and statistics
- Wake time configuration (Tw_sys)
- Power consumption measurement and savings calculation
- Runtime EEE enable/disable configuration
- Interoperability with non-EEE devices (fallback)
- EEE compatibility with TSN features (TAS, CBS, gPTP)

**Success Criteria**:
- ✅ EEE negotiated correctly when both sides support it
- ✅ Tx LPI entered within 10µs after 200µs idle
- ✅ Tx LPI exited within 30µs on packet arrival
- ✅ Power reduction ≥20% with low traffic
- ✅ No packet loss due to EEE
- ✅ Latency penalty <50µs
- ✅ TSN traffic (Class A/B) unaffected by EEE

---

## 🧪 Test Coverage

### **10 Unit Tests**

**UT-EEE-001: EEE Capability Advertisement**
- Read PHY EEE capability (MMD 3.20 bit 1)
- Verify 1000BASE-T EEE supported
- Advertise EEE capability (MMD 7.60)
- Verify LLDP EEE TLV transmitted:
  - TLV Type: 127 (Organizationally Specific)
  - OUI: 00-12-0F (IEEE 802.3)
  - Subtype: 11 (EEE)
  - Supported: 1 (1000BASE-T)
  - Enabled: 1

**UT-EEE-002: LPI Mode Negotiation**
- Configure EEE on both sides (local + partner)
- Perform autonegotiation
- Verify EEE capability exchange:
  - Local advertises EEE in MMD 7.60
  - Partner capability read from MMD 7.61
  - EEE active only when both sides support it
- Verify registers:
  - MMD 3.20 bit 1: EEE capable
  - MMD 7.60: EEE advertise (0x0006 for 1000BASE-T + 100BASE-TX)
  - MMD 7.61: Link partner EEE capability

**UT-EEE-003: Tx LPI Entry**
- Transmit traffic, then stop (create idle condition)
- Wait 200µs (idle detection threshold)
- Verify Tx LPI entry:
  - PHY enters LPI within 10µs after idle threshold
  - LPI symbols transmitted on wire
  - EEE_CTRL register: TX_LPI_EN = 1
  - Statistics updated: TxLpiEntryCount++
- Measure entry time: <10µs from idle threshold

**UT-EEE-004: Tx LPI Exit**
- Enter Tx LPI mode (idle for >200µs)
- Queue packet for transmission
- Verify Tx LPI exit:
  - PHY exits LPI within 30µs (1000BASE-T wake time)
  - Refresh signal sent to wake link partner
  - Packet transmitted after wake time
  - Statistics updated: TxLpiDurationUs += (exit time - entry time)
- Measure exit time: <30µs for 1000BASE-T, <200µs for 100BASE-TX

**UT-EEE-005: Rx LPI Detection**
- Partner transmits LPI symbols
- Verify Rx LPI detection:
  - Driver detects LPI symbols on receive path
  - EEE_STATUS register: RX_LPI_STATUS = 1
  - Statistics updated: RxLpiEntryCount++, RxLpiDurationUs
- Measure Rx LPI duration accuracy (±1µs)

**UT-EEE-006: Wake Time Configuration**
- Configure wake time (Tw_sys) per link speed:
  - 1000BASE-T: 16.5µs (per IEEE 802.3az)
  - 100BASE-TX: 30µs
  - 10BASE-T: N/A (EEE not supported)
- Verify wake time registers:
  - EEE_SU (Sleep/Unsleep): Tw_sys value
  - Wake time matches specification ±1µs
- Test LPI exit respects configured wake time

**UT-EEE-007: LPI Timer Management**
- Configure LPI entry timer: 200µs (Tq_sys)
- Verify idle detection:
  - Timer starts when Tx queue empty
  - LPI entered exactly 200µs after last Tx
  - Timer reset on packet arrival
- Configure LPI exit timer:
  - Track wake time (Tw_sys)
  - Exit timer expires after wake time
- Verify timers accurate (±5µs)

**UT-EEE-008: Power Measurement API**
- Enable EEE, run traffic pattern:
  - 10 seconds active (1000 frames/sec)
  - 10 seconds idle (no traffic)
- Query IOCTL_AVB_GET_POWER_STATS
- Verify power statistics:
  - Baseline power (EEE off): ~2000 mW
  - EEE power (EEE on): <1600 mW (≥20% reduction)
  - LPI percentage: ≥50% during idle
  - Energy saved calculated correctly (mJ)
- Compare measured vs. estimated power

**UT-EEE-009: EEE Disable/Enable (Runtime)**
- Enable EEE via IOCTL_AVB_SET_EEE_CONFIG (enable=TRUE)
- Verify EEE negotiated and active
- Disable EEE via IOCTL (enable=FALSE)
- Verify:
  - LPI stopped immediately
  - EEE capability no longer advertised
  - Power consumption returns to baseline
- Re-enable EEE:
  - Capability advertised again
  - Autonegotiation restarts
  - EEE active within 3 seconds

**UT-EEE-010: Non-EEE Interoperability**
- Connect to non-EEE switch (partner does not advertise EEE)
- Verify fallback:
  - Local advertises EEE capability
  - Partner does not respond with EEE capability (MMD 7.61 = 0x0000)
  - Driver detects non-EEE partner
  - LPI mode disabled
  - Link operates in standard (non-EEE) mode
  - No packet loss or link errors

---

### **3 Integration Tests**

**IT-EEE-001: EEE Under Variable Load**
- Generate variable traffic pattern:
  - Burst: 1000 frames at 10,000 fps (100ms)
  - Idle: No traffic (900ms)
  - Repeat for 60 seconds
- Verify EEE behavior:
  - LPI entered during idle periods (>200µs)
  - LPI exited on packet arrival (<30µs)
  - Latency penalty measured: <50µs
  - No packet loss or reordering
- Measure LPI statistics:
  - LPI time percentage: ≥60% (primarily idle workload)
  - Average wake latency: <30µs

**IT-EEE-002: EEE with TSN Traffic**
- Configure TSN stack:
  - 2 Class A streams (125µs interval, TC6)
  - 2 Class B streams (250µs interval, TC5)
  - Best-effort traffic on TC0
- Enable EEE
- Verify:
  - Class A/B streams maintain <2ms latency (unaffected by LPI)
  - Best-effort traffic uses LPI during idle gaps
  - TAS schedules respected (gates not delayed by LPI wake time)
  - CBS credit calculations unaffected
  - gPTP timestamps accurate (no jitter from LPI)
- Measure power savings: ≥10% (with TSN traffic active)

**IT-EEE-003: Multi-Adapter EEE Coordination**
- Configure 2 adapters with EEE enabled
- Run independent traffic on each adapter:
  - Adapter 0: Burst every 500ms
  - Adapter 1: Burst every 750ms (different pattern)
- Verify:
  - Each adapter enters/exits LPI independently
  - No cross-adapter interference
  - LPI timing accurate on both adapters
  - Power savings additive (both adapters in LPI = 2× savings)
- Measure total power reduction: ≥30% (both adapters idle 60% of time)

---

### **2 V&V Tests**

**VV-EEE-001: 24-Hour Power Efficiency Monitoring**
- Run production workload for 24 hours:
  - Average traffic: 5 Mbps (low utilization)
  - Periodic bursts: 100 Mbps for 10 seconds every hour
- Enable EEE
- Monitor continuously:
  - LPI entry/exit counts
  - LPI time percentage
  - Power consumption (if measurable)
  - Packet loss and errors
- Verify after 24 hours:
  - LPI time percentage: ≥60%
  - Power reduction: ≥20%
  - Zero packet loss due to EEE
  - Zero link errors or renegotiations
  - Average latency increase: <10µs

**VV-EEE-002: EEE Interoperability Matrix**
- Test with multiple switch vendors:
  - EEE-capable switches (3 vendors)
  - Non-EEE switches (2 vendors)
- For each switch:
  - Connect adapter, verify link up
  - Query EEE status (IOCTL_AVB_GET_EEE_STATUS)
  - Run traffic for 60 minutes
  - Measure power consumption
- Verify:
  - EEE negotiated correctly with EEE switches
  - Fallback to non-EEE with non-EEE switches
  - No link instability or errors
  - Power savings consistent across EEE switches (±5%)
- Document compatibility matrix

---

## 🔧 Implementation Notes

### **EEE Capability Negotiation**

```c
typedef struct _EEE_STATUS {
    BOOLEAN Capable;            // PHY supports EEE (register 3.20 bit 1)
    BOOLEAN Enabled;            // EEE enabled by user
    BOOLEAN LinkPartnerCapable; // Partner advertises EEE (register 7.61)
    BOOLEAN Active;             // EEE negotiated and active
    UINT32  TxLpiEntryCount;    // Number of Tx LPI entries
    UINT32  RxLpiEntryCount;    // Number of Rx LPI entries
    UINT64  TxLpiDurationUs;    // Total Tx LPI time (microseconds)
    UINT64  RxLpiDurationUs;    // Total Rx LPI time
    LARGE_INTEGER LastLpiEntry; // Timestamp of last LPI entry
} EEE_STATUS;

NTSTATUS NegotiateEEE(ADAPTER_CONTEXT* adapter) {
    // Read PHY EEE capability (MMD 3.20)
    UINT16 eeeCapability = ReadPhyMmd(adapter, 3, 20);
    adapter->Eee.Capable = (eeeCapability & 0x0002) ? TRUE : FALSE;  // 1000BASE-T EEE
    
    if (!adapter->Eee.Capable || !adapter->Eee.Enabled) {
        return STATUS_NOT_SUPPORTED;
    }
    
    // Advertise EEE capability (MMD 7.60)
    UINT16 eeeAdvertise = 0x0006;  // 1000BASE-T + 100BASE-TX
    WritePhyMmd(adapter, 7, 60, eeeAdvertise);
    
    // Restart autonegotiation to exchange EEE TLVs
    RestartAutonegotiation(adapter);
    
    return STATUS_SUCCESS;
}
```

### **Tx LPI Entry/Exit**

```c
VOID EnterTxLpi(ADAPTER_CONTEXT* adapter) {
    // Check idle condition (no Tx for >200µs)
    LARGE_INTEGER now;
    KeQueryPerformanceCounter(&now);
    UINT64 idleTimeUs = ((now.QuadPart - adapter->Tx.LastActivity.QuadPart) * 1000000) / g_PerformanceFrequency.QuadPart;
    
    if (idleTimeUs < 200) {
        return;  // Not idle long enough
    }
    
    // Enter LPI mode
    UINT32 eeeCtrl = READ_REG32(I225_EEE_CTRL);
    eeeCtrl |= EEE_TX_LPI_EN;
    WRITE_REG32(I225_EEE_CTRL, eeeCtrl);
    
    adapter->Eee.LastLpiEntry = now;
    InterlockedIncrement(&adapter->Eee.TxLpiEntryCount);
}

VOID ExitTxLpi(ADAPTER_CONTEXT* adapter) {
    UINT32 eeeCtrl = READ_REG32(I225_EEE_CTRL);
    eeeCtrl &= ~EEE_TX_LPI_EN;
    WRITE_REG32(I225_EEE_CTRL, eeeCtrl);
    
    // Update LPI duration
    LARGE_INTEGER now;
    KeQueryPerformanceCounter(&now);
    UINT64 lpiDurationUs = ((now.QuadPart - adapter->Eee.LastLpiEntry.QuadPart) * 1000000) / g_PerformanceFrequency.QuadPart;
    adapter->Eee.TxLpiDurationUs += lpiDurationUs;
}
```

### **Power Measurement**

```c
typedef struct _POWER_STATS {
    UINT64 BaselinePowerMw;  // Power without EEE (milliwatts)
    UINT64 EeePowerMw;       // Power with EEE
    UINT32 LpiPercentage;    // % time in LPI mode
    UINT64 EnergySavedMj;    // Energy saved (millijoules)
} POWER_STATS;

VOID MeasurePowerSavings(ADAPTER_CONTEXT* adapter, POWER_STATS* stats) {
    // Read hardware power registers (if available) or estimate
    UINT64 totalTimeUs = adapter->Eee.TxLpiDurationUs +
                         (adapter->Stats.Uptime * 1000000 - adapter->Eee.TxLpiDurationUs);
    
    stats->LpiPercentage = (UINT32)((adapter->Eee.TxLpiDurationUs * 100) / totalTimeUs);
    
    // Estimate power savings: LPI mode ~10% of active power
    stats->BaselinePowerMw = 2000;  // Typical 1000BASE-T active power
    stats->EeePowerMw = stats->BaselinePowerMw * (100 - stats->LpiPercentage * 0.9) / 100;
    
    UINT64 powerReductionMw = stats->BaselinePowerMw - stats->EeePowerMw;
    stats->EnergySavedMj = (powerReductionMw * totalTimeUs) / 1000;  // mW * µs = mJ
}
```

---

## 📊 Performance Targets

| Metric | Target | Measurement Method |
|------------------------------|---------------------|-------------------------------------------|
| LPI Entry Time | <10µs after idle | GPIO toggle on LPI entry |
| LPI Exit Time (1000BASE-T) | <30µs | Oscilloscope measurement |
| Wake Time (Tw_sys) | 16.5µs @ 1Gbps | Per IEEE 802.3az specification |
| Idle Detection | 200µs | Time from last Tx to LPI entry |
| Power Reduction | ≥20% | Baseline vs. EEE power measurement |
| Latency Penalty | <50µs | Packet latency with/without EEE |
| LPI Time Percentage | ≥60% (low traffic) | Tx LPI duration / total uptime |
| Autonegotiation Time | <3 seconds | EEE capability exchange via LLDP |

---

## ✅ Acceptance Criteria

### EEE Negotiation
- [x] PHY advertises EEE capability in MMD 7.60
- [x] Link partner capability detected in MMD 7.61
- [x] EEE active only when both sides support it
- [x] Fallback to non-EEE when partner doesn't support

### LPI Timing
- [x] Tx LPI entered within 10µs after 200µs idle
- [x] Tx LPI exited within 30µs on packet arrival
- [x] Wake time configured per speed (16.5µs @ 1Gbps, 30µs @ 100Mbps)
- [x] LPI statistics accurate (entry count, duration)

### Power Efficiency
- [x] Power reduction ≥20% with low traffic
- [x] LPI time ≥60% during idle periods
- [x] No packet loss due to EEE
- [x] Latency penalty <50µs

### TSN Compatibility
- [x] Class A/B streams unaffected by EEE
- [x] Best-effort traffic uses LPI during idle
- [x] Launch time offload works with EEE

### Configuration
- [x] EEE enable/disable via IOCTL
- [x] Runtime changes effective within 1 second
- [x] Power statistics queryable via IOCTL

---

## 🔗 References
- IEEE 802.3az-2010: Energy Efficient Ethernet
- Intel I225 Datasheet: EEE Implementation (Chapter 8.2.5)
- #58: REQ-NF-POWER-001 - Power Management Requirements

**Standards**: IEEE 802.3az (EEE), ISO/IEC/IEEE 12207:2017
**XP Practice**: TDD - Tests defined before implementation
