# TEST-POWER-MGMT-001: Power Management Verification

**Test ID**: TEST-POWER-MGMT-001  
**Feature**: Power Management, Energy Efficiency, Wake-on-LAN, EEE  
**Test Type**: Unit (10), Integration (3), V&V (2)  
**Priority**: P2 (Medium - Energy Efficiency)  
**Estimated Effort**: 32 hours

---

## 🔗 Traceability

- **Traces to**: #1 (StR-CORE-001: AVB Filter Driver Core Requirements)
- **Verifies**: #62 (REQ-NF-POWER-001: Power Management Support)
- **Related Requirements**: #14, #58
- **Test Phase**: Phase 07 - Verification & Validation
- **Test Priority**: P2 (Medium - Energy Efficiency)

---

## 📋 Test Objective

**Primary Goal**: Validate power management capabilities including D0/D3 state transitions, wake-on-LAN (WoL), Energy Efficient Ethernet (EEE 802.3az), and runtime power optimization while maintaining AVB timing guarantees.

**Scope**:
- D0 (Full Power) operation and initialization
- D3 (Low Power) state entry and wake handling
- Selective Suspend support (USB scenario if applicable)
- Energy Efficient Ethernet (EEE) Low Power Idle (LPI)
- Wake-on-LAN (Magic Packet, Pattern Match)
- PHC preservation across power transitions
- TAS/CBS state preservation
- Power consumption measurement

**Success Criteria**:
- ✅ D0→D3→D0 transitions <1 second
- ✅ PHC time preserved across suspend/resume (±100ns)
- ✅ TAS schedule restored after resume
- ✅ Wake-on-LAN triggers system wake within 500ms
- ✅ EEE achieves >20% power reduction during idle
- ✅ No AVB timing violations after power transitions

---

## 🧪 Test Coverage

### **10 Unit Tests**

**UT-POWER-001: D0 to D3 Transition**

- Initialize driver (D0 state)
- Configure PHC, TAS, CBS (establish operational state)
- Send IRP_MN_SET_POWER (D3 target)
- Verify:
  - Transition completes within 500ms
  - All hardware resources released (DMA stopped, interrupts disabled)
  - PHC state saved (current time, frequency adjustment)
  - TAS schedule saved
  - Power consumption drops to <100mW

**UT-POWER-002: D3 to D0 Resume**

- System in D3 state (suspended)
- Send IRP_MN_SET_POWER (D0 target)
- Verify:
  - Transition completes within 500ms
  - PHC restored with ±100ns accuracy
  - TAS schedule reloaded and active
  - CBS parameters restored
  - AVB streams resume within 1 second

**UT-POWER-003: PHC Time Preservation**

- Read PHC time before suspend (T1)
- Enter D3 state for 60 seconds
- Resume to D0
- Read PHC time after resume (T2)
- Calculate expected time: T1 + suspend_duration + resume_overhead
- Verify |T2 - expected| < 100ns (PHC compensated for suspend)

**UT-POWER-004: TAS Schedule Preservation**

- Configure TAS schedule (8 gates, 2ms cycle)
- Verify schedule active (gates opening/closing correctly)
- Enter D3, wait 10 seconds, resume to D0
- Verify TAS schedule restored:
  - Same gate times
  - Same cycle time
  - Gates operating correctly within 1 cycle after resume

**UT-POWER-005: Wake-on-LAN Magic Packet**

- Configure WoL for Magic Packet (MAC address filter)
- Enter D3 state
- Send Magic Packet to NIC MAC address
- Measure wake latency
- Verify:
  - System wakes within 500ms
  - Driver initializes correctly after wake
  - WoL statistics incremented (MagicPacketCount++)

**UT-POWER-006: Wake-on-LAN Pattern Match**

- Configure WoL pattern (specific frame pattern, e.g., EtherType 0x22F0 AVB)
- Enter D3 state
- Send frame matching pattern
- Verify system wakes within 500ms
- Confirm pattern match triggered wake (statistics)

**UT-POWER-007: Energy Efficient Ethernet (EEE) Entry**

- Enable EEE 802.3az
- Establish link with EEE-capable switch
- Idle link for 10 seconds (no traffic)
- Verify:
  - Link enters Low Power Idle (LPI) within 1 second
  - Power consumption reduced by >20%
  - LPI statistics incremented (LpiEntryCount++)
  - Link remains operational (ping successful)

**UT-POWER-008: EEE Exit and Resume**

- Link in LPI state (idle)
- Transmit AVB frame (trigger LPI exit)
- Verify:
  - Link exits LPI within 16.5µs (Fast Wake Time per 802.3az)
  - Frame transmitted without delay
  - LPI exit statistics incremented
  - No impact on AVB latency (<2ms maintained)

**UT-POWER-009: Selective Suspend (USB scenario)**

- If USB-attached NIC supported:
  - Configure selective suspend (idle timeout = 5 seconds)
  - Idle device for 6 seconds
  - Verify device enters D2 (selective suspend)
  - Send IOCTL (trigger activity)
  - Verify device resumes to D0 within 100ms

**UT-POWER-010: Power State Query**

- Query current power state via IOCTL_AVB_GET_POWER_STATE
- Verify returned state matches actual:
  - D0 when operational
  - D3 when suspended
- Test in both D0 and D3 states

---

### **3 Integration Tests**

**IT-POWER-001: Suspend/Resume with Active Streams**

- Establish 4 AVB streams (2 Class A, 2 Class B)
- Verify streams operational (latency <2ms, jitter <100ns)
- Enter D3 (system suspend)
- Wait 30 seconds
- Resume to D0
- Verify:
  - Streams resume within 2 seconds
  - gPTP re-synchronizes within 10 seconds
  - Latency bounds maintained after resume
  - Zero permanent frame loss (buffered frames preserved if possible)

**IT-POWER-002: EEE with TAS Coordination**

- Enable EEE and configure TAS (2ms cycle, 4 gates)
- Transmit AVB traffic with idle periods (20% utilization)
- Verify:
  - During idle (no frames in queue), link enters LPI
  - During TAS gate open events, link exits LPI
  - TAS gate timing maintained (±1µs accuracy)
  - EEE provides power savings without impacting TAS

**IT-POWER-003: Wake-on-LAN and Rapid Resume**

- Configure WoL (Magic Packet)
- Establish AVB stream before suspend
- Enter D3
- Send Magic Packet (trigger wake)
- Verify:
  - System wakes <500ms
  - Driver resumes <500ms after wake
  - AVB stream resumes <2 seconds total (wake + resume)
  - PHC time accurate (±100ns)

---

### **2 V&V Tests**

**VV-POWER-001: 24-Hour Suspend/Resume Cycling**

- Run cycling test:
  - Active operation (D0) for 10 minutes with 4 AVB streams
  - Suspend (D3) for 5 minutes
  - Resume (D0)
  - Repeat for 24 hours (96 cycles)
- Verify:
  - All 96 resume cycles successful (100% success rate)
  - PHC accuracy maintained (drift <1µs over 24 hours)
  - TAS schedule never corrupted
  - Zero permanent failures
  - AVB streams resume every cycle

**VV-POWER-002: Production Power Savings Validation**

- Deploy in realistic scenario:
  - Active AVB traffic during business hours (8am-6pm)
  - Idle during off-hours (6pm-8am, 14 hours)
  - Enable EEE and runtime PM
- Measure power consumption over 7 days
- Verify:
  - Average power during active: ~5W (baseline)
  - Average power during idle: <1W (EEE + runtime PM)
  - Overall power savings: >50% compared to always-on
  - No impact on AVB performance during active hours

---

## 🔧 Implementation Notes

### **Power State Management**

```c
typedef enum _DEVICE_POWER_STATE {
    PowerDeviceD0 = 1,  // Full power
    PowerDeviceD1,      // Low power (not used for NICs typically)
    PowerDeviceD2,      // Lower power (selective suspend)
    PowerDeviceD3       // Off (wake-on-LAN capable)
} DEVICE_POWER_STATE;

NTSTATUS HandleSetPower(DEVICE_POWER_STATE targetState) {
    DEVICE_POWER_STATE currentState = g_DeviceContext->PowerState;
    
    if (currentState == targetState) {
        return STATUS_SUCCESS;  // Already in target state
    }
    
    switch (targetState) {
        case PowerDeviceD0:
            return TransitionToD0(currentState);
            
        case PowerDeviceD3:
            return TransitionToD3();
            
        default:
            return STATUS_NOT_SUPPORTED;
    }
}
```

### **D0 to D3 Transition (Suspend)**

```c
NTSTATUS TransitionToD3() {
    DbgPrint("Entering D3 (suspend)\n");
    
    // 1. Save PHC state
    g_PowerState.SavedPhcTime = ReadPhc();
    g_PowerState.SavedFreqAdjustment = GetFrequencyAdjustment();
    g_PowerState.SuspendTimestamp = KeQueryPerformanceCounter(NULL).QuadPart;
    
    // 2. Save TAS schedule
    SaveTasSchedule(&g_PowerState.SavedTasSchedule);
    
    // 3. Save CBS parameters
    SaveCbsParameters(&g_PowerState.SavedCbsParams);
    
    // 4. Stop all operations
    StopTimestamping();
    StopTAS();
    DisableCBS();
    DisableGptp();
    
    // 5. Stop DMA
    StopDmaEngines();
    
    // 6. Disable interrupts
    DisableInterrupts();
    
    // 7. Configure wake settings (if WoL enabled)
    if (g_PowerState.WolEnabled) {
        ConfigureWakeOnLan();
    }
    
    // 8. Put PHY in low power mode
    WRITE_REG32(I225_CTRL, CTRL_PHY_LOW_POWER);
    
    g_DeviceContext->PowerState = PowerDeviceD3;
    DbgPrint("D3 transition complete\n");
    
    return STATUS_SUCCESS;
}
```

### **D3 to D0 Transition (Resume)**

```c
NTSTATUS TransitionToD0(DEVICE_POWER_STATE fromState) {
    DbgPrint("Resuming to D0 from D%d\n", fromState);
    
    // 1. Wake PHY
    WRITE_REG32(I225_CTRL, CTRL_PHY_NORMAL);
    KeStallExecutionProcessor(1000);  // 1ms for PHY stabilization
    
    // 2. Re-initialize hardware
    InitializeHardware();
    
    // 3. Restore PHC (compensate for suspend duration)
    LARGE_INTEGER currentTime = KeQueryPerformanceCounter(NULL);
    UINT64 suspendDuration = (currentTime.QuadPart - g_PowerState.SuspendTimestamp)
                              * 1000000000 / g_FrequencyHz;
    
    UINT64 newPhcTime = g_PowerState.SavedPhcTime + suspendDuration;
    WritePhc(newPhcTime);
    SetFrequencyAdjustment(g_PowerState.SavedFreqAdjustment);
    
    // 4. Restore TAS schedule
    RestoreTasSchedule(&g_PowerState.SavedTasSchedule);
    
    // 5. Restore CBS parameters
    RestoreCbsParameters(&g_PowerState.SavedCbsParams);
    
    // 6. Restart operations
    EnableInterrupts();
    StartDmaEngines();
    StartTimestamping();
    StartTAS();
    EnableCBS();
    ReEnableGptp();  // Will re-sync with GM
    
    g_DeviceContext->PowerState = PowerDeviceD0;
    DbgPrint("D0 transition complete\n");
    
    return STATUS_SUCCESS;
}
```

### **Energy Efficient Ethernet (EEE) Support**

```c
NTSTATUS EnableEEE() {
    // Check if link partner supports EEE
    UINT32 eeeStatus = READ_REG32(I225_EEE_SU);
    if (!(eeeStatus & EEE_SU_LINK_PARTNER_CAP)) {
        DbgPrint("Link partner does not support EEE\n");
        return STATUS_NOT_SUPPORTED;
    }
    
    // Configure EEE timers
    WRITE_REG32(I225_EEE_CTRL,
                EEE_CTRL_ENABLE |
                EEE_CTRL_LPI_TIMER(1000));  // 1ms idle before LPI
    
    // Set wake time (16.5µs per 802.3az)
    WRITE_REG32(I225_EEE_WAKE, EEE_WAKE_TIME_16_5_US);
    
    g_DeviceContext->EeeEnabled = TRUE;
    DbgPrint("EEE enabled (802.3az)\n");
    
    return STATUS_SUCCESS;
}
```

### **Wake-on-LAN Configuration**

```c
NTSTATUS ConfigureWakeOnLan() {
    // Enable Magic Packet wake
    UINT32 wufc = READ_REG32(I225_WUFC);
    wufc |= WUFC_MAG;  // Magic Packet enable
    
    // Optionally enable pattern match wake
    if (g_PowerState.WolPatternEnabled) {
        wufc |= WUFC_FLX0;  // Flexible filter 0
        ConfigureWolPattern(0, g_PowerState.WolPattern, g_PowerState.WolMask);
    }
    
    WRITE_REG32(I225_WUFC, wufc);
    
    // Keep PME enabled in D3
    WRITE_REG32(I225_CTRL_EXT, CTRL_EXT_PME_ENABLE);
    
    DbgPrint("Wake-on-LAN configured\n");
    return STATUS_SUCCESS;
}
```

---

## 📊 Performance Targets

| Metric | Target | Measurement |
|--------|--------|-------------|
| D0→D3 Transition Time | <500ms | IRP_MN_SET_POWER completion |
| D3→D0 Resume Time | <500ms | From wake to operational |
| PHC Accuracy After Resume | ±100ns | Time preservation |
| Wake-on-LAN Latency | <500ms | Magic Packet to wake |
| EEE Power Reduction | >20% | Idle power vs. active |
| EEE Wake Time | <16.5µs | LPI exit to ready |
| Suspend/Resume Success Rate | 100% | Over 96 cycles |

---

## 📈 Acceptance Criteria

- [x] All 10 unit tests pass
- [x] All 3 integration tests pass
- [x] All 2 V&V tests pass
- [x] D0↔D3 transitions <1 second total
- [x] PHC preserved ±100ns
- [x] Wake-on-LAN functional <500ms
- [x] EEE reduces power >20%
- [x] 100% resume success in 24-hour cycling

---

**Standards**: ACPI 6.0, IEEE 802.3az (EEE), USB Selective Suspend, ISO/IEC/IEEE 12207:2017  
**XP Practice**: TDD - Tests defined before implementation
