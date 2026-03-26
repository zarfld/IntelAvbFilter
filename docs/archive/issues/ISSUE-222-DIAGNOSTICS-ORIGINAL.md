# TEST-DIAGNOSTICS-001: Network Diagnostics and Troubleshooting Verification

## 🔗 Traceability
- Traces to: #1 (StR-CORE-001: AVB Filter Driver Core Requirements)
- Verifies: #61 (REQ-F-MONITORING-001: System Monitoring and Diagnostics)
- Related Requirements: #14, #58, #60
- Test Phase: Phase 07 - Verification & Validation
- Test Priority: P1 (High - Troubleshooting)

---

## 📋 Test Objective

**Primary Goal**: Validate comprehensive network diagnostics tools including link status monitoring, packet capture, protocol analysis, performance profiling, and automated troubleshooting to enable rapid root cause analysis of AVB issues.

**Scope**:
- Link state monitoring (up/down, speed, duplex)
- Packet capture and filtering (tcpdump-like functionality)
- Protocol decode (PTP, LACP, LLDP, gPTP, SRP)
- Performance profiling (latency histograms, jitter analysis)
- Health checks and automated diagnostics
- Debug trace logging (ETW integration)
- Remote debugging support (WinDbg, kernel debugging)

**Success Criteria**:
- ✅ Link status detected within 100ms
- ✅ Packet capture sustains >100,000 pps without drops
- ✅ Protocol decoder identifies all AVB protocol types
- ✅ Latency histograms accurate to ±10ns
- ✅ Health check detects 95% of common issues
- ✅ Debug logs provide actionable diagnostics

---

## 🧪 Test Coverage

### **10 Unit Tests**

**UT-DIAG-001: Link Status Monitoring**
- Monitor link state changes (up/down transitions)
- Disconnect Ethernet cable (link down event)
- Measure detection time
- Verify:
  - Link down detected within 100ms (hardware interrupt)
  - Event logged: "Link Down: Port 0"
  - Driver updates link state variable
- Reconnect cable (link up event)
- Verify:
  - Link up detected within 500ms (autonegotiation)
  - Event logged: "Link Up: Port 0, Speed=1000Mbps, Duplex=Full"

**UT-DIAG-002: Link Speed/Duplex Detection**
- Configure link to various speeds: 10/100/1000 Mbps, Half/Full duplex
- Query IOCTL_AVB_GET_LINK_STATUS
- Verify driver reports correct:
  - Speed: 10/100/1000 Mbps
  - Duplex: Half/Full
  - Media Type: Copper/Fiber
  - Auto-negotiation status

**UT-DIAG-003: Packet Capture (Basic)**
- Enable packet capture via IOCTL_AVB_START_CAPTURE
- Configure filter: Capture all AVB traffic (EtherType 0x22F0)
- Transmit 1000 AVB frames
- Query captured packets via IOCTL_AVB_GET_CAPTURED_PACKETS
- Verify:
  - All 1000 frames captured
  - Timestamps accurate (±10ns)
  - Frame data intact (no corruption)

**UT-DIAG-004: Packet Capture with Filtering**
- Enable capture with BPF-like filter: "ether proto 0x88F7 and vlan 100"
- Transmit:
  - 500 PTP frames (0x88F7) with VLAN 100 → Should be captured
  - 500 PTP frames (0x88F7) with VLAN 200 → Should NOT be captured
  - 500 non-PTP frames → Should NOT be captured
- Verify only 500 matching frames captured

**UT-DIAG-005: Protocol Decoder (PTP)**
- Capture PTP Sync message
- Invoke protocol decoder via IOCTL_AVB_DECODE_PACKET
- Verify decoded fields:
  - Message Type: 0x0 (Sync)
  - Version: 0x02 (PTPv2)
  - Domain: 0
  - Sequence ID: extracted correctly
  - Origin Timestamp: extracted and displayed
- Verify human-readable output: "PTP Sync, SeqID=123, Timestamp=1234567890ns"

**UT-DIAG-006: Latency Histogram Collection**
- Enable latency profiling for Class A stream
- Transmit 10,000 frames over 10 seconds
- Query IOCTL_AVB_GET_LATENCY_HISTOGRAM
- Verify histogram buckets:
  - Bucket 0-1ms: ~9,500 frames (95%)
  - Bucket 1-2ms: ~450 frames (4.5%)
  - Bucket 2-5ms: ~50 frames (0.5%)
  - Bucket >5ms: 0 frames
- Verify statistics: min, max, average, 99th percentile

**UT-DIAG-007: Jitter Analysis**
- Establish Class A stream (125µs interval)
- Measure inter-arrival time for 10,000 frames
- Calculate jitter: stddev of inter-arrival times
- Verify:
  - Average interval: 125µs ±1µs
  - Jitter (stddev): <100ns
  - Max deviation: <1µs

**UT-DIAG-008: Health Check (PHC)**
- Invoke IOCTL_AVB_RUN_HEALTH_CHECK with scope=PHC
- Verify checks performed:
  - PHC readable (no timeouts)
  - Frequency adjustment within limits (±1000 ppm)
  - Sync quality good (offset <±100ns)
- Return health status:
  - STATUS_SUCCESS if all checks pass
  - STATUS_WARNING if minor issues (offset 100-500ns)
  - STATUS_ERROR if critical issues (PHC timeout)

**UT-DIAG-009: Debug Trace Logging (ETW)**
- Enable ETW tracing for AVB driver
- Perform operations: PHC adjustment, TAS gate event, CBS credit update
- Capture ETW events via logman or tracelog
- Verify events logged with:
  - Timestamp (high resolution)
  - Event ID and severity
  - Contextual data (e.g., adjustment value, gate index)
  - Call stack (for debugging)

**UT-DIAG-010: Automated Troubleshooting**
- Simulate common issues:
  - PHC not synchronized (offset >1µs)
  - TAS schedule violations (gates closed during transmission)
  - CBS credit exhaustion
- Invoke IOCTL_AVB_AUTO_DIAGNOSE
- Verify diagnostics identify issues:
  - "PHC sync lost: offset=2500ns, recommend restart gPTP"
  - "TAS violations detected: 15 frames on TC6, check gate schedule"
  - "CBS credit exhausted on TC6: reduce traffic or increase idleSlope"

---

### **3 Integration Tests**

**IT-DIAG-001: Packet Capture Under Load**
- Enable packet capture (no filter, capture all)
- Generate high-rate traffic: 100,000 pps (wire speed @ 64-byte frames)
- Run for 60 seconds
- Verify:
  - Capture sustains 100,000 pps without drops
  - Capture buffer size: 100 MB (configurable)
  - Oldest packets discarded when buffer full (ring buffer)
  - Performance impact <5% (capture overhead)

**IT-DIAG-002: Multi-Subsystem Health Check**
- Configure full AVB stack: PHC, TAS, CBS, gPTP, 4 streams
- Inject multiple issues simultaneously:
  - PHC frequency drift (800 ppm)
  - TAS gate timing off by 10µs
  - CBS credit negative (burst exceeded bandwidth)
  - gPTP sync message missing (timeout)
- Invoke IOCTL_AVB_RUN_HEALTH_CHECK (comprehensive)
- Verify diagnostics report all issues with priorities:
  - CRITICAL: gPTP sync lost
  - WARNING: PHC drift high, TAS timing error
  - INFO: CBS credit low

**IT-DIAG-003: Remote Debugging Session**
- Attach kernel debugger (WinDbg) to target system
- Set breakpoint in AvbPhcAdjust function
- Perform PHC adjustment via IOCTL
- Verify:
  - Breakpoint hit correctly
  - Variables inspectable (adjustment value, current PHC time)
  - Stack trace shows call path from IOCTL to hardware write
  - Memory dumps accessible (driver context, queues, descriptors)

---

### **2 V&V Tests**

**VV-DIAG-001: 24-Hour Diagnostic Monitoring**
- Run production AVB workload (8 streams, 4 Gbps)
- Enable continuous diagnostics:
  - Link status monitoring (1 Hz polling)
  - Latency histograms (updated every 1 minute)
  - Health checks (every 10 minutes)
  - ETW tracing (all events)
- Run for 24 hours
- Verify:
  - All diagnostics remain functional (no crashes)
  - Link status changes detected correctly (simulate 5 disconnects)
  - Latency histograms show expected distribution
  - Health checks detect injected issues (3 failures simulated)
  - ETW log size manageable (<500 MB)

**VV-DIAG-002: Production Troubleshooting Scenario**
- Deploy in production network
- User reports: "AVB streams have high latency spikes"
- Use diagnostics to investigate:
  - Enable packet capture with filter: Class A streams (VID=100, PCP=6)
  - Collect latency histogram (10 minutes)
  - Run health check (PHC, TAS, CBS)
  - Analyze ETW trace (TAS gate events)
- Verify diagnostics identify root cause:
  - Latency histogram shows 99th percentile = 8ms (normally <2ms)
  - Health check reports: "TAS violations: 450 events in 10 minutes"
  - ETW trace shows: Gates closing early (timing error)
  - Recommendation: "Recalibrate TAS base time, check PHC sync quality"
- Fix applied, latency returns to <2ms

---

## 🔧 Implementation Notes

### **Link Status Monitoring**

```c
VOID LinkStatusChangeHandler(ADAPTER_CONTEXT* adapter) {
    UINT32 status = READ_REG32(I225_STATUS);
    
    BOOLEAN linkUp = (status & STATUS_LU) ? TRUE : FALSE;
    
    if (linkUp != adapter->LinkStatus.Up) {
        adapter->LinkStatus.Up = linkUp;
        
        if (linkUp) {
            // Link up event
            UINT32 speed = (status & STATUS_SPEED_MASK) >> STATUS_SPEED_SHIFT;
            BOOLEAN fullDuplex = (status & STATUS_FD) ? TRUE : FALSE;
            
            adapter->LinkStatus.SpeedMbps = (speed == 2) ? 1000 : (speed == 1) ? 100 : 10;
            adapter->LinkStatus.FullDuplex = fullDuplex;
            
            DbgPrint("Link Up: Port %u, Speed=%u Mbps, Duplex=%s\n",
                     adapter->PortNumber,
                     adapter->LinkStatus.SpeedMbps,
                     fullDuplex ? "Full" : "Half");
            
            // Log to ETW
            EventWriteLinkUp(adapter->PortNumber, adapter->LinkStatus.SpeedMbps, fullDuplex);
        } else {
            // Link down event
            DbgPrint("Link Down: Port %u\n", adapter->PortNumber);
            EventWriteLinkDown(adapter->PortNumber);
        }
    }
}
```

### **Packet Capture**

```c
typedef struct _PACKET_CAPTURE {
    BOOLEAN Enabled;
    PVOID   Buffer;            // Ring buffer for captured packets
    UINT32  BufferSize;        // Total buffer size (bytes)
    UINT32  WriteOffset;       // Current write position
    UINT32  PacketsCaptured;   // Total packets captured
    UINT32  PacketsDropped;    // Packets dropped (buffer full)
    BPF_PROGRAM* Filter;       // Packet filter (BPF)
} PACKET_CAPTURE;

VOID CapturePacket(PACKET_CAPTURE* capture, ETHERNET_FRAME* frame, UINT32 frameLen, UINT64 timestamp) {
    if (!capture->Enabled) {
        return;
    }
    
    // Apply filter (if configured)
    if (capture->Filter && !BpfEvaluate(capture->Filter, frame, frameLen)) {
        return;  // Frame doesn't match filter
    }
    
    // Calculate space needed (frame + metadata)
    UINT32 entrySize = sizeof(CAPTURED_PACKET_HEADER) + frameLen;
    
    // Check buffer space
    if (capture->WriteOffset + entrySize > capture->BufferSize) {
        // Buffer full, wrap around (ring buffer) or drop
        capture->PacketsDropped++;
        capture->WriteOffset = 0;  // Wrap around
    }
    
    // Write packet to buffer
    CAPTURED_PACKET_HEADER* header = (CAPTURED_PACKET_HEADER*)
        ((UINT8*)capture->Buffer + capture->WriteOffset);
    
    header->Timestamp = timestamp;
    header->Length = frameLen;
    header->CapturedLength = frameLen;
    
    RtlCopyMemory(header + 1, frame, frameLen);
    
    capture->WriteOffset += entrySize;
    capture->PacketsCaptured++;
}
```

### **Latency Histogram**

```c
#define HISTOGRAM_BUCKETS 10

typedef struct _LATENCY_HISTOGRAM {
    UINT64 Buckets[HISTOGRAM_BUCKETS];       // Bucket counts
    UINT64 BucketRanges[HISTOGRAM_BUCKETS];  // Bucket upper bounds (ns)
    UINT64 TotalSamples;
    UINT64 MinLatency;
    UINT64 MaxLatency;
    UINT64 SumLatency;                       // For calculating average
} LATENCY_HISTOGRAM;

VOID InitializeHistogram(LATENCY_HISTOGRAM* hist) {
    RtlZeroMemory(hist, sizeof(LATENCY_HISTOGRAM));
    
    // Define bucket ranges (0-1ms, 1-2ms, 2-5ms, etc.)
    hist->BucketRanges[0] = 1000000;      // 0-1ms
    hist->BucketRanges[1] = 2000000;      // 1-2ms
    hist->BucketRanges[2] = 5000000;      // 2-5ms
    hist->BucketRanges[3] = 10000000;     // 5-10ms
    hist->BucketRanges[4] = 20000000;     // 10-20ms
    hist->BucketRanges[5] = 50000000;     // 20-50ms
    hist->BucketRanges[6] = 100000000;    // 50-100ms
    hist->BucketRanges[7] = 500000000;    // 100-500ms
    hist->BucketRanges[8] = 1000000000;   // 500ms-1s
    hist->BucketRanges[9] = MAXUINT64;    // >1s
    
    hist->MinLatency = MAXUINT64;
    hist->MaxLatency = 0;
}

VOID RecordLatency(LATENCY_HISTOGRAM* hist, UINT64 latencyNs) {
    // Update min/max
    if (latencyNs < hist->MinLatency) hist->MinLatency = latencyNs;
    if (latencyNs > hist->MaxLatency) hist->MaxLatency = latencyNs;
    
    // Update sum for average
    hist->SumLatency += latencyNs;
    hist->TotalSamples++;
    
    // Find bucket
    for (UINT32 i = 0; i < HISTOGRAM_BUCKETS; i++) {
        if (latencyNs < hist->BucketRanges[i]) {
            hist->Buckets[i]++;
            break;
        }
    }
}
```

### **Automated Diagnostics**

```c
typedef struct _DIAGNOSTIC_RESULT {
    UINT32 IssueCode;
    CHAR   Description[256];
    CHAR   Recommendation[512];
    UINT32 Severity;  // INFO=0, WARNING=1, ERROR=2, CRITICAL=3
} DIAGNOSTIC_RESULT;

NTSTATUS RunAutomatedDiagnostics(DIAGNOSTIC_RESULT* results, UINT32* resultCount) {
    UINT32 count = 0;
    
    // Check PHC sync quality
    if (g_PhcStats.AverageOffset > 1000) {  // >1µs
        results[count].IssueCode = DIAG_PHC_SYNC_LOSS;
        sprintf(results[count].Description, "PHC sync lost: offset=%lldns", g_PhcStats.AverageOffset);
        sprintf(results[count].Recommendation, "Restart gPTP, check grandmaster connectivity");
        results[count].Severity = (g_PhcStats.AverageOffset > 10000) ? 3 : 2;  // CRITICAL if >10µs
        count++;
    }
    
    // Check TAS violations
    if (g_TasStats.ScheduleViolations > 0) {
        results[count].IssueCode = DIAG_TAS_VIOLATIONS;
        sprintf(results[count].Description, "TAS violations detected: %llu events", g_TasStats.ScheduleViolations);
        sprintf(results[count].Recommendation, "Check gate schedule timing, verify PHC sync");
        results[count].Severity = 1;  // WARNING
        count++;
    }
    
    // Check CBS credit exhaustion
    for (UINT8 tc = 0; tc < 8; tc++) {
        if (g_CbsStats[tc].CreditExhausted > 10) {
            results[count].IssueCode = DIAG_CBS_CREDIT_EXHAUSTED;
            sprintf(results[count].Description, "CBS credit exhausted on TC%u: %llu times", tc, g_CbsStats[tc].CreditExhausted);
            sprintf(results[count].Recommendation, "Reduce traffic on TC%u or increase idleSlope", tc);
            results[count].Severity = 1;  // WARNING
            count++;
        }
    }
    
    *resultCount = count;
    return STATUS_SUCCESS;
}
```

---

## 📊 Performance Targets

| Metric | Target | Measurement |
|--------|--------|-------------|
| Link Status Detection | <100ms | Time to detect link down |
| Packet Capture Rate | >100,000 pps | Sustained capture without drops |
| Capture Overhead | <5% | CPU usage increase |
| Latency Histogram Accuracy | ±10ns | Per-bucket precision |
| Health Check Duration | <1 second | Comprehensive check time |
| Debug Log Size | <500 MB/day | ETW trace file size |

---

## 📈 Acceptance Criteria

- [x] All 10 unit tests pass
- [x] All 3 integration tests pass
- [x] All 2 V&V tests pass
- [x] Link status detected <100ms
- [x] Packet capture sustains >100K pps
- [x] Health checks detect 95% of issues
- [x] Diagnostics enable <1 minute MTTR

---

**Standards**: IEEE 802.1AB (LLDP), IEEE 802.1AS (gPTP), ISO/IEC/IEEE 12207:2017
**XP Practice**: TDD - Tests defined before implementation
