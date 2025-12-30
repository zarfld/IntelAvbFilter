# TEST-MULTI-ADAPTER-001: Multi-Adapter PHC Synchronization Verification

## 🔗 Traceability

- Traces to: #1 (StR-CORE-001: AVB Filter Driver Core Requirements)
- Verifies: #150 (REQ-F-MULTI-001: Multi-Adapter Support)
- Related Requirements: #2, #3, #149
- Test Phase: Phase 07 - Verification & Validation
- Test Priority: P1 (High - Multi-NIC deployments)

---

## 📋 Test Objective

**Primary Goal**: Validate correct operation of multiple AVB-capable network adapters in a single system with independent PHC management and optional cross-synchronization.

**Scope**:
- Concurrent operation of 2+ AVB filter instances
- Independent PHC management per adapter
- Cross-adapter PHC synchronization for redundancy
- Multi-adapter device enumeration and identification
- Resource isolation (no cross-contamination)

**Success Criteria**:
- ✅ Up to 4 adapters operate concurrently without interference
- ✅ Each adapter maintains independent PHC (±10ns isolation)
- ✅ Cross-adapter sync achievable within ±100ns when enabled
- ✅ Device enumeration correctly identifies all adapters
- ✅ IOCTL routing to correct adapter instance (100% accuracy)

---

## 🧪 Test Coverage

### **10 Unit Tests**

**UT-MULTI-001: Dual Adapter Initialization**
- Install filter on 2 network adapters simultaneously
- Verify both instances initialize successfully
- Confirm independent PHC initialization for each
- Check device handles are unique and correctly mapped

**UT-MULTI-002: Independent PHC Operation**
- Configure adapter 1 PHC: +100 ppm frequency adjustment
- Configure adapter 2 PHC: -100 ppm frequency adjustment
- Run for 10 seconds
- Verify each PHC runs at its configured rate (±1 ppb)
- Confirm no cross-influence between adapters

**UT-MULTI-003: Device Enumeration**
- Query system for all AVB-capable adapters
- Verify count matches installed adapters (2, 3, or 4)
- Confirm device paths unique and stable across reboots
- Test enumeration order consistency

**UT-MULTI-004: IOCTL Routing**
- Send IOCTL_AVB_PHC_QUERY to adapter 1
- Send IOCTL_AVB_PHC_QUERY to adapter 2
- Verify each IOCTL routes to correct adapter instance
- Confirm no cross-contamination of responses

**UT-MULTI-005: Concurrent Timestamp Retrieval**
- Transmit packets on both adapters simultaneously
- Query TX timestamps from both adapters
- Verify timestamps from correct PHCs (adapter 1 vs. adapter 2)
- Confirm no timestamp mix-up or corruption

**UT-MULTI-006: Cross-Adapter PHC Synchronization**
- Designate adapter 1 as master PHC
- Configure adapter 2 to sync to adapter 1
- Measure offset between PHCs over 1 minute
- Verify sync error <100ns (mean), <500ns (max)

**UT-MULTI-007: Adapter Hot-Plug**
- Start with 1 adapter operational
- Add 2nd adapter while system running
- Verify filter binds to new adapter automatically
- Confirm both adapters operate independently after hot-plug

**UT-MULTI-008: Adapter Removal**
- Start with 2 adapters operational
- Remove 1 adapter (simulate unplug)
- Verify remaining adapter continues normal operation
- Confirm no crashes or resource leaks

**UT-MULTI-009: Per-Adapter Resource Isolation**
- Configure TAS on adapter 1 only
- Configure CBS on adapter 2 only
- Verify TAS operates only on adapter 1
- Confirm CBS operates only on adapter 2
- Check no shared state or configuration bleed

**UT-MULTI-010: Maximum Adapter Count (4 Adapters)**
- Install filter on 4 adapters simultaneously
- Configure unique PHC settings on each
- Verify all 4 operate independently
- Measure CPU/memory overhead scales linearly

---

### **3 Integration Tests**

**IT-MULTI-001: Multi-Adapter gPTP Synchronization**
- Configure 2 adapters in same gPTP domain
- Adapter 1: gPTP master (grandmaster)
- Adapter 2: gPTP slave (syncs to adapter 1)
- Run for 10 minutes
- Verify adapter 2 syncs to adapter 1 within ±100ns
- Confirm stable steady-state synchronization

**IT-MULTI-002: Dual-Link AVB Redundancy (IEEE 802.1CB)**
- Configure 2 adapters for AVB stream redundancy
- Transmit same stream on both adapters (sequence numbers tagged)
- Listener receives from both, deduplicates based on sequence
- Simulate link failure on adapter 1
- Verify seamless failover to adapter 2 (<1ms switchover)

**IT-MULTI-003: Multi-Adapter Load Balancing**
- Configure 4 adapters with 4 AVB streams each (16 total)
- Each adapter handles 4 Class A streams (75% bandwidth)
- Run for 1 hour with full load on all adapters
- Verify all streams meet latency requirements (≤2ms)
- Confirm no adapter exceeds 80% CPU utilization

---

### **2 V&V Tests**

**VV-MULTI-001: 24-Hour Multi-Adapter Stability**
- Configure 4 adapters with realistic workload:
  - Adapter 1: 8 Class A streams (audio)
  - Adapter 2: 4 Class B streams (video)
  - Adapter 3: Mixed Class A/B
  - Adapter 4: gPTP grandmaster + control traffic
- Run for 24 hours
- Verify:
  - Zero adapter crashes or hangs
  - PHC sync maintained (<100ns drift)
  - All streams meet latency bounds
  - No memory leaks or resource exhaustion

**VV-MULTI-002: Production Multi-Site Deployment**
- Simulate factory automation scenario:
  - 4 adapters at different physical locations
  - Adapter 1: PLC control network
  - Adapter 2: Robot coordination
  - Adapter 3: Vision system
  - Adapter 4: Safety network (redundant path)
- Run for 1 hour with realistic traffic patterns
- Verify:
  - Deterministic latency (<100µs control messages)
  - Zero cross-adapter interference
  - Failover works within 1ms (redundant paths)

---

## 🔧 Implementation Notes

### **Multi-Adapter Device Enumeration**

```c
typedef struct _ADAPTER_INFO {
    WCHAR DevicePath[256];     // Device symbolic link
    UINT32 AdapterIndex;       // Sequential index (0, 1, 2, ...)
    UINT64 MacAddress;         // Unique MAC address
    HANDLE DeviceHandle;       // Handle for IOCTLs
} ADAPTER_INFO;

// Enumerate all AVB adapters
NTSTATUS EnumerateAvbAdapters(ADAPTER_INFO** Adapters, UINT32* Count) {
    HDEVINFO deviceInfoSet = SetupDiGetClassDevs(
        &GUID_DEVCLASS_NET,
        NULL,
        NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    );
    
    UINT32 adapterCount = 0;
    ADAPTER_INFO* adapterList = NULL;
    
    SP_DEVICE_INTERFACE_DATA interfaceData;
    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    
    for (UINT32 i = 0; SetupDiEnumDeviceInterfaces(deviceInfoSet, NULL, &GUID_DEVCLASS_NET, i, &interfaceData); i++) {
        // Get device path
        SP_DEVICE_INTERFACE_DETAIL_DATA* detailData = GetDeviceInterfaceDetail(&interfaceData);
        
        // Query if AVB-capable
        HANDLE hDevice = CreateFile(detailData->DevicePath, ...);
        if (IsAvbCapable(hDevice)) {
            // Add to adapter list
            adapterList = realloc(adapterList, (adapterCount + 1) * sizeof(ADAPTER_INFO));
            wcscpy(adapterList[adapterCount].DevicePath, detailData->DevicePath);
            adapterList[adapterCount].AdapterIndex = adapterCount;
            adapterList[adapterCount].DeviceHandle = hDevice;
            adapterCount++;
        }
    }
    
    *Adapters = adapterList;
    *Count = adapterCount;
    return STATUS_SUCCESS;
}
```

### **Cross-Adapter PHC Synchronization**

```c
// Sync adapter 2 PHC to adapter 1 PHC
VOID SyncAdapterPhcs(HANDLE masterAdapter, HANDLE slaveAdapter) {
    // Read master PHC time
    AVB_PHC_QUERY_RESPONSE masterResp;
    DeviceIoControl(masterAdapter, IOCTL_AVB_PHC_QUERY, ...);
    UINT64 masterTime = masterResp.CurrentTime;
    
    // Read slave PHC time
    AVB_PHC_QUERY_RESPONSE slaveResp;
    DeviceIoControl(slaveAdapter, IOCTL_AVB_PHC_QUERY, ...);
    UINT64 slaveTime = slaveResp.CurrentTime;
    
    // Calculate offset
    INT64 offset = (INT64)masterTime - (INT64)slaveTime;
    
    // Apply offset adjustment to slave
    AVB_OFFSET_ADJUST_REQUEST offsetReq;
    offsetReq.OffsetNanoseconds = offset;
    DeviceIoControl(slaveAdapter, IOCTL_AVB_OFFSET_ADJUST, &offsetReq, ...);
    
    // Apply frequency adjustment for continuous sync (PI controller)
    INT32 freqAdjPpb = CalculateFrequencyAdjustment(offset);
    AVB_FREQUENCY_ADJUST_REQUEST freqReq;
    freqReq.FrequencyAdjustmentPpb = freqAdjPpb;
    DeviceIoControl(slaveAdapter, IOCTL_AVB_FREQUENCY_ADJUST, &freqReq, ...);
}
```

---

## 📊 Performance Targets

| Metric | Target | Measurement |
|--------|--------|-------------|
| Max Concurrent Adapters | 4 | System limit |
| PHC Isolation | ±10ns | Cross-adapter interference |
| Cross-Adapter Sync Accuracy | ±100ns | Master-slave offset |
| IOCTL Routing Accuracy | 100% | No mis-routed calls |
| Enumeration Latency | <100ms | All adapters discovered |
| Hot-Plug Detection | <1s | New adapter operational |

---

## 📈 Acceptance Criteria

- [ ] All 10 unit tests pass
- [ ] All 3 integration tests pass
- [ ] All 2 V&V tests pass
- [ ] 4 adapters operate concurrently
- [ ] PHC isolation ±10ns
- [ ] Cross-adapter sync ±100ns
- [ ] IOCTL routing 100% accurate
- [ ] 24-hour stability test passes
- [ ] Hot-plug/removal handled gracefully

---

**Standards**: IEEE 802.1CB (Redundancy), IEEE 1012-2016, ISO/IEC/IEEE 12207:2017  
**XP Practice**: TDD - Tests defined before implementation
