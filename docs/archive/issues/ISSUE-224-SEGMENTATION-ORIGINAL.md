# TEST-SEGMENTATION-001: Network Segmentation and Isolation Verification

## 🔗 Traceability
- Traces to: #1 (StR-CORE-001: AVB Filter Driver Core Requirements)
- Verifies: #63 (REQ-NF-SECURITY-001: Security and Access Control)
- Related Requirements: #14, #60, #61
- Test Phase: Phase 07 - Verification & Validation
- Test Priority: P1 (High - Security)

---

## 📋 Test Objective

**Primary Goal**: Validate network segmentation and isolation mechanisms including VLAN-based traffic separation, traffic filtering rules, security boundaries enforcement, and prevention of cross-segment leakage.

**Scope**:
- VLAN-based traffic isolation (IEEE 802.1Q)
- VLAN filtering and configuration
- MAC address filtering (whitelist/blacklist)
- IP address filtering (source/destination, subnets)
- Protocol-based filtering (EtherType)
- Port-based isolation (multi-port adapters)
- Multicast filtering (IGMP snooping)
- Broadcast storm protection
- VLAN hopping prevention (Q-in-Q rejection)
- Penetration testing and attack simulation

**Success Criteria**:
- ✅ 100% VLAN isolation (zero cross-VLAN leakage)
- ✅ MAC/IP/protocol filters enforced correctly
- ✅ Broadcast storms limited to <1000 pps
- ✅ VLAN hopping attacks blocked (Q-in-Q rejection)
- ✅ TSN QoS preserved within VLANs
- ✅ Filter lookup <1µs per frame

---

## 🧪 Test Coverage

### **10 Unit Tests**

**UT-SEGMENTATION-001: VLAN Isolation**
- Configure 2 VLANs: VLAN 100 and VLAN 200
- Transmit frames tagged VLAN 100
- Verify:
  - Frames received only on VLAN 100 ports
  - No frames leak to VLAN 200 ports
  - VLAN filter table lookup <1µs
  - Statistics updated correctly (FramesReceived, FramesDropped)
- Reverse test: VLAN 200 → verify no leakage to VLAN 100
- Test with 1000 frames/sec for 60 seconds

**UT-SEGMENTATION-002: VLAN Filtering Configuration**
- Add VLAN 100 via IOCTL_AVB_ADD_VLAN_FILTER
- Verify filter table entry:
  - VlanId = 100
  - Enabled = TRUE
  - AllowedPorts = configured mask
- Remove VLAN 100 via IOCTL_AVB_REMOVE_VLAN_FILTER
- Verify:
  - Entry removed from filter table
  - Frames with VLAN 100 now dropped
- Test filter table capacity: add 64 VLANs (MAX_VLAN_FILTERS)

**UT-SEGMENTATION-003: Default VLAN Handling (PVID)**
- Configure PVID (Port VLAN ID) = 100
- Transmit untagged frames
- Verify:
  - Driver assigns VLAN tag 100 to untagged frames
  - Frames routed correctly based on VLAN 100 rules
  - Statistics count PVID-assigned frames
- Change PVID to 200:
  - Verify new untagged frames assigned VLAN 200
  - Previous VLAN 100 traffic unaffected

**UT-SEGMENTATION-004: VLAN Priority Preservation**
- Transmit frames with VLAN tag: VID=100, PCP=6 (Class A)
- Verify:
  - PCP bits (802.1p priority) preserved across VLAN boundaries
  - TC (Traffic Class) mapping correct: PCP 6 → TC 6
  - QoS scheduler respects priority
- Test with all 8 PCP values (0-7)

**UT-SEGMENTATION-005: MAC Address Filtering**
- Configure MAC whitelist:
  - Allow: AA:BB:CC:DD:EE:01
  - Deny: AA:BB:CC:DD:EE:02
- Transmit frames from both MACs
- Verify:
  - Frames from AA:BB:CC:DD:EE:01 forwarded
  - Frames from AA:BB:CC:DD:EE:02 dropped
  - Filter hit count statistics accurate
- Test MAC mask for OUI filtering:
  - Mask: FF:FF:FF:00:00:00
  - Match: AA:BB:CC:* (any device from vendor AA:BB:CC)
- Verify lookup time <1µs (hash table)

**UT-SEGMENTATION-006: IP Address Filtering**
- Configure IP filter rules:
  - Allow source: 192.168.1.0/24
  - Deny destination: 10.0.0.0/8
- Transmit IPv4 packets:
  - Source 192.168.1.100 → Destination 192.168.2.50 (allowed)
  - Source 192.168.1.100 → Destination 10.0.0.1 (denied)
  - Source 172.16.0.1 → Destination 192.168.1.100 (denied - source not in allowed subnet)
- Verify:
  - Subnet mask matching correct
  - Both source AND destination rules enforced
- Test IPv6 filtering (128-bit addresses)

**UT-SEGMENTATION-007: Protocol-Based Filtering**
- Configure protocol filters:
  - Allow: 0x88F7 (PTP)
  - Allow: 0x22F0 (AVB)
  - Deny: 0x0800 (IPv4)
- Transmit frames with different EtherTypes
- Verify:
  - PTP (0x88F7) frames forwarded
  - AVB (0x22F0) frames forwarded
  - IPv4 (0x0800) frames dropped
  - Unknown EtherTypes: configurable (default allow or deny)
- Statistics: FramesFiltered per protocol

**UT-SEGMENTATION-008: Port-Based Isolation**
- Configure multi-port adapter (2 ports):
  - Port 0: VLAN 100
  - Port 1: VLAN 200
- Transmit from Port 0 (VLAN 100)
- Verify:
  - Traffic stays within VLAN 100 (Port 0)
  - No traffic reaches Port 1 (VLAN 200) unless explicitly routed
- Test port bitmask in VLAN filter: AllowedPorts field

**UT-SEGMENTATION-009: Multicast Filtering (IGMP Snooping)**
- Configure multicast group: 224.0.0.100
- Register 2 subscribers on VLAN 100
- Transmit multicast to 224.0.0.100
- Verify:
  - Frames forwarded only to registered subscribers
  - Frames NOT forwarded to non-subscribers
  - IGMP Join/Leave messages processed correctly
- Test multicast statistics: FramesForwarded per group

**UT-SEGMENTATION-010: Broadcast Storm Protection**
- Configure broadcast limiter:
  - MaxBroadcastsPps = 1000
  - WindowSizeMs = 100
- Flood with 5000 broadcast frames/sec
- Verify:
  - Only ~1000 broadcasts forwarded per second
  - Excess broadcasts dropped
  - DroppedBroadcasts statistic accurate
- Verify sliding window resets correctly every 100ms

---

### **3 Integration Tests**

**IT-SEGMENTATION-001: Multi-VLAN TSN Traffic**
- Configure 2 VLANs with TSN streams:
  - VLAN 100: 2 Class A streams (125µs interval, TC6)
  - VLAN 200: 2 Class B streams (250µs interval, TC5)
- Run traffic for 60 minutes
- Verify:
  - Zero cross-VLAN leakage (VLAN 100 traffic never reaches VLAN 200)
  - Class A latency <2ms on VLAN 100
  - Class B latency <50ms on VLAN 200
  - QoS preserved within each VLAN
  - TAS schedules independent per VLAN
- Monitor statistics:
  - FramesReceived per VLAN
  - FramesDropped (should be zero)

**IT-SEGMENTATION-002: Security Policy Enforcement**
- Configure combined filtering rules:
  - VLAN filter: Allow VLAN 100, 200
  - MAC filter: Whitelist 10 MACs
  - IP filter: Allow subnet 192.168.1.0/24
  - Protocol filter: Allow PTP (0x88F7), AVB (0x22F0)
- Transmit traffic matching all rules
- Verify:
  - Only frames matching ALL filters forwarded
  - Single rule violation = frame dropped
- Test attack scenarios:
  - Correct VLAN, wrong MAC → dropped
  - Correct MAC, wrong IP → dropped
  - Correct IP, wrong protocol → dropped
- Verify filter evaluation order optimized (most restrictive first)

**IT-SEGMENTATION-003: Segment Isolation Under Attack**
- Configure 2 VLANs: VLAN 100 (production), VLAN 200 (test)
- Flood VLAN 200 with 100,000 frames/sec (DoS attack)
- Simultaneously run TSN traffic on VLAN 100
- Verify:
  - VLAN 100 traffic unaffected by VLAN 200 flood
  - Class A latency on VLAN 100 remains <2ms
  - QoS scheduler isolates VLANs
  - Broadcast storm protection limits VLAN 200 broadcasts
- Monitor CPU usage: <20% during attack

---

### **2 V&V Tests**

**VV-SEGMENTATION-001: 24-Hour Isolation Validation**
- Configure 4 VLANs with continuous traffic:
  - VLAN 100: Class A TSN (125µs)
  - VLAN 200: Class B TSN (250µs)
  - VLAN 300: Best-effort (1 Mbps average)
  - VLAN 400: Multicast (100 groups)
- Run for 24 hours with cross-VLAN monitoring
- Verify:
  - Zero cross-VLAN leakage detected
  - All filter rules active for entire duration
  - Statistics accurate (billions of frames)
  - No memory leaks in filter tables
  - Log any VLAN violations (expect zero)
- Verify after 24 hours:
  - Filter lookup time still <1µs
  - Filter table integrity intact

**VV-SEGMENTATION-002: Penetration Testing**
- Perform security audits simulating attacks:
  
  **VLAN Hopping (Q-in-Q)**:
  - Transmit double-tagged frames (0x88A8 + 0x8100)
  - Verify: All Q-in-Q frames rejected (100% blocked)
  - Log: "VLAN hopping attempt detected: Q-in-Q frame rejected"
  
  **MAC Spoofing**:
  - Transmit frames with spoofed source MAC (not in whitelist)
  - Verify: Frames dropped, HitCount incremented
  - Log: "MAC filter hit: <MAC> -> action=DENY"
  
  **IP Spoofing**:
  - Transmit packets with spoofed source IP (outside allowed subnet)
  - Verify: Packets dropped at IP filter stage
  
  **Broadcast Flood**:
  - Flood with 10,000 broadcasts/sec
  - Verify: Limited to <1000 pps, excess dropped
  
  **VLAN Tag Manipulation**:
  - Send frames with invalid VLAN IDs (0, 4095, out of range)
  - Verify: Frames rejected, statistics logged
  
  **Protocol Confusion**:
  - Send IPv6 frames on IPv4-only VLAN
  - Verify: Protocol filter blocks frames

- After penetration testing:
  - Zero successful attacks
  - All violations logged
  - System remains stable (no crashes)

---

## 🔧 Implementation Notes

### **VLAN Filtering Table**

```c
#define MAX_VLAN_FILTERS 64

typedef struct _VLAN_FILTER_ENTRY {
    UINT16 VlanId;              // VLAN ID (1-4094)
    BOOLEAN Enabled;            // Filter active
    UINT8 AllowedPorts;         // Bitmask of ports (multi-port adapters)
    UINT32 FramesReceived;      // Statistics
    UINT32 FramesDropped;       // Filtered frames
} VLAN_FILTER_ENTRY;

typedef struct _VLAN_FILTER_TABLE {
    VLAN_FILTER_ENTRY Entries[MAX_VLAN_FILTERS];
    UINT32 Count;               // Active entries
    UINT16 DefaultVlanId;       // PVID for untagged frames
    KSPIN_LOCK Lock;
} VLAN_FILTER_TABLE;

BOOLEAN IsVlanAllowed(VLAN_FILTER_TABLE* table, UINT16 vlanId, UINT8 portNum) {
    KIRQL oldIrql;
    KeAcquireSpinLock(&table->Lock, &oldIrql);
    
    for (UINT32 i = 0; i < table->Count; i++) {
        if (table->Entries[i].VlanId == vlanId && table->Entries[i].Enabled) {
            BOOLEAN allowed = (table->Entries[i].AllowedPorts & (1 << portNum)) != 0;
            KeReleaseSpinLock(&table->Lock, oldIrql);
            return allowed;
        }
    }
    
    KeReleaseSpinLock(&table->Lock, oldIrql);
    return FALSE;  // VLAN not in filter table, drop
}
```

### **MAC Address Filtering**

```c
typedef enum _MAC_FILTER_ACTION {
    MAC_FILTER_ALLOW,
    MAC_FILTER_DENY,
    MAC_FILTER_LOG_AND_ALLOW,
    MAC_FILTER_LOG_AND_DENY
} MAC_FILTER_ACTION;

typedef struct _MAC_FILTER_RULE {
    UINT8 MacAddress[6];
    UINT8 MacMask[6];           // For subnet matching (e.g., OUI filter)
    MAC_FILTER_ACTION Action;
    UINT32 HitCount;            // Statistics
} MAC_FILTER_RULE;

BOOLEAN ApplyMacFilter(MAC_FILTER_RULE* rules, UINT32 ruleCount, UINT8* srcMac, UINT8* dstMac) {
    for (UINT32 i = 0; i < ruleCount; i++) {
        BOOLEAN match = TRUE;
        
        // Check if source MAC matches rule
        for (UINT32 j = 0; j < 6; j++) {
            if ((srcMac[j] & rules[i].MacMask[j]) != (rules[i].MacAddress[j] & rules[i].MacMask[j])) {
                match = FALSE;
                break;
            }
        }
        
        if (match) {
            InterlockedIncrement(&rules[i].HitCount);
            
            if (rules[i].Action == MAC_FILTER_LOG_AND_ALLOW || rules[i].Action == MAC_FILTER_LOG_AND_DENY) {
                DbgPrint("MAC filter hit: %02X:%02X:%02X:%02X:%02X:%02X -> action=%d\n",
                         srcMac[0], srcMac[1], srcMac[2], srcMac[3], srcMac[4], srcMac[5], rules[i].Action);
            }
            
            return (rules[i].Action == MAC_FILTER_ALLOW || rules[i].Action == MAC_FILTER_LOG_AND_ALLOW);
        }
    }
    
    return TRUE;  // Default allow if no rule matches
}
```

### **Protocol-Based Filtering**

```c
typedef struct _PROTOCOL_FILTER {
    UINT16 EtherType;           // 0x88F7 (PTP), 0x0800 (IPv4), 0x86DD (IPv6), etc.
    BOOLEAN Allow;
    UINT32 FramesFiltered;
} PROTOCOL_FILTER;

BOOLEAN IsProtocolAllowed(PROTOCOL_FILTER* filters, UINT32 count, UINT16 etherType) {
    for (UINT32 i = 0; i < count; i++) {
        if (filters[i].EtherType == etherType) {
            if (!filters[i].Allow) {
                InterlockedIncrement(&filters[i].FramesFiltered);
            }
            return filters[i].Allow;
        }
    }
    
    return TRUE;  // Default allow unknown protocols (or configure to deny)
}
```

### **Broadcast Storm Protection**

```c
typedef struct _BROADCAST_LIMITER {
    UINT32 MaxBroadcastsPps;    // Maximum broadcasts per second
    UINT32 WindowSizeMs;        // Sliding window (e.g., 100ms)
    UINT32 BroadcastCount;      // Count in current window
    LARGE_INTEGER WindowStart;  // Window start time
    UINT32 DroppedBroadcasts;   // Statistics
} BROADCAST_LIMITER;

BOOLEAN CheckBroadcastLimit(BROADCAST_LIMITER* limiter) {
    LARGE_INTEGER now;
    KeQueryPerformanceCounter(&now);
    
    UINT64 elapsedMs = ((now.QuadPart - limiter->WindowStart.QuadPart) * 1000) / g_PerformanceFrequency.QuadPart;
    
    // Reset window if expired
    if (elapsedMs >= limiter->WindowSizeMs) {
        limiter->BroadcastCount = 0;
        limiter->WindowStart = now;
    }
    
    limiter->BroadcastCount++;
    
    UINT32 maxAllowed = (limiter->MaxBroadcastsPps * limiter->WindowSizeMs) / 1000;
    
    if (limiter->BroadcastCount > maxAllowed) {
        InterlockedIncrement(&limiter->DroppedBroadcasts);
        return FALSE;  // Drop broadcast
    }
    
    return TRUE;  // Allow broadcast
}
```

### **VLAN Hopping Prevention**

```c
VOID ValidateVlanTag(ETHERNET_FRAME* frame, UINT32 frameLen, BOOLEAN* isValid, UINT16* vlanId) {
    *isValid = FALSE;
    *vlanId = 0;
    
    if (frameLen < 18) {  // Min frame with VLAN tag
        return;
    }
    
    UINT16 tpid = ntohs(*(UINT16*)(frame->Data + 12));
    
    // Check for valid VLAN TPID
    if (tpid == 0x8100) {  // Standard VLAN
        UINT16 tci = ntohs(*(UINT16*)(frame->Data + 14));
        *vlanId = tci & 0x0FFF;
        
        // Validate VLAN ID (1-4094, 0 and 4095 reserved)
        if (*vlanId >= 1 && *vlanId <= 4094) {
            *isValid = TRUE;
        }
    } else if (tpid == 0x88A8) {  // Q-in-Q (reject double tagging)
        DbgPrint("VLAN hopping attempt detected: Q-in-Q frame rejected\n");
        *isValid = FALSE;
    }
}
```

---

## 📊 Performance Targets

| Metric | Target | Measurement Method |
|------------------------------|---------------------|-------------------------------------------|
| VLAN Isolation | 100% (zero leakage) | Cross-VLAN traffic monitoring |
| MAC Filter Lookup | <1µs | Hash table lookup time |
| Filter Table Size | ≥64 VLANs | Concurrent VLAN configurations |
| Broadcast Rate Limit | <1000 pps | Configurable per-port limit |
| Policy Enforcement Overhead | <2% | Impact on forwarding performance |
| VLAN Hopping Prevention | 100% | All double-tag frames rejected |
| Filter Rule Capacity | ≥256 MAC rules | Combined VLAN + MAC + IP rules |
| Configuration Latency | <10ms | Time to apply new filter rule |

---

## ✅ Acceptance Criteria

### VLAN Isolation
- ✅ Frames tagged VLAN 100 never reach VLAN 200
- ✅ Untagged frames assigned PVID correctly
- ✅ PCP bits preserved across VLANs
- ✅ Zero cross-VLAN leakage in 24-hour test

### Filtering Accuracy
- ✅ MAC whitelist: only allowed MACs forwarded
- ✅ MAC blacklist: blocked MACs dropped
- ✅ IP filtering: source/destination rules enforced
- ✅ Protocol filtering: only allowed EtherTypes forwarded

### Attack Prevention
- ✅ VLAN hopping (Q-in-Q) blocked 100%
- ✅ MAC spoofing detected and logged
- ✅ Broadcast storm limited to <1000 pps
- ✅ DoS attacks on one segment don't affect others

### TSN Compatibility
- ✅ Class A/B streams routed per VLAN
- ✅ QoS preserved within VLANs
- ✅ Launch time offload works with VLAN tagging

### Performance
- ✅ Filter lookup <1µs per frame
- ✅ Filtering overhead <2%
- ✅ Configuration changes applied <10ms

---

## 🔗 References
- IEEE 802.1Q-2018: VLANs and Bridges
- IEEE 802.1AE-2018: MAC Security (MACsec) - for encryption reference
- #63: REQ-NF-SECURITY-001 - Security Requirements

**Standards**: IEEE 802.1Q (VLANs), ISO/IEC/IEEE 12207:2017
**XP Practice**: TDD - Tests defined before implementation
