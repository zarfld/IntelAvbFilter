# Issue #213 - Original Content (Reconstructed)

**Date**: 2025-12-31  
**Reconstruction**: Based on user-provided diff and IEEE 802.1Q standards  
**Issue**: TEST-VLAN-001: VLAN Tagging and Filtering Verification

---

# TEST-VLAN-001: VLAN Tagging and Filtering Verification

## 🔗 Traceability
- Traces to: #1 (StR-CORE-001: AVB Filter Driver Core Requirements)
- Verifies: #13 (REQ-F-VLAN-001: VLAN Support)
- Related Requirements: #11, #149
- Test Phase: Phase 07 - Verification & Validation
- Test Priority: P1 (High - Network Segmentation)

---

## 📋 Test Objective

**Primary Goal**: Validate IEEE 802.1Q VLAN tagging, filtering, and priority code point (PCP) mapping for AVB stream isolation and QoS enforcement.

**Scope**:
- VLAN tag insertion (802.1Q header)
- VLAN filtering (accept/drop based on VLAN ID)
- Priority Code Point (PCP) to Traffic Class (TC) mapping
- Double tagging (802.1ad QinQ) support
- VLAN membership management
- AVB stream VLAN assignment

**Success Criteria**:
- ✅ VLAN tags inserted correctly (TPID 0x8100, VID, PCP, DEI)
- ✅ VLAN filtering works (only registered VLANs accepted)
- ✅ PCP→TC mapping accurate (PCP 6→TC6, PCP 5→TC5, etc.)
- ✅ Double tagging supported (S-Tag + C-Tag)
- ✅ VLAN membership configurable per port
- ✅ AVB streams isolated by VLAN

---

## 🧪 Test Coverage

### **10 Unit Tests**

**UT-VLAN-001: VLAN Tag Insertion**
- Configure VLAN 100 for outgoing frames
- Transmit untagged frame
- Verify VLAN tag inserted:
  - TPID = 0x8100 (802.1Q)
  - VID = 100
  - PCP = 6 (for Class A traffic)
  - DEI = 0
- Capture and validate frame on wire

**UT-VLAN-002: VLAN Filtering (Accept)**
- Register VLAN 100, 200 in VLAN membership table
- Receive tagged frame with VID=100
- Verify frame accepted and passed to upper layer
- Confirm VLAN filter allows registered VLANs

**UT-VLAN-003: VLAN Filtering (Drop)**
- Register VLAN 100, 200 in VLAN membership table
- Receive tagged frame with VID=300 (not registered)
- Verify frame dropped at VLAN filter
- Confirm drop counter incremented

**UT-VLAN-004: PCP to TC Mapping**
- Configure PCP→TC mapping: PCP 7→TC7, PCP 6→TC6, ..., PCP 0→TC0
- Transmit frames with different PCP values (0-7)
- Verify frames queued to correct traffic classes
- Confirm mapping table programmed correctly

**UT-VLAN-005: Priority Override**
- Receive frame with PCP=6 (should map to TC6)
- Configure override: force all traffic to TC0
- Verify frame queued to TC0 (override takes precedence)
- Disable override, confirm normal PCP mapping resumes

**UT-VLAN-006: Untagged Frame Handling**
- Configure port VLAN ID (PVID) = 100
- Receive untagged frame
- Verify frame treated as VLAN 100 (PVID assigned)
- Confirm frame accepted if PVID registered

**UT-VLAN-007: Double Tagging (802.1ad QinQ)**
- Configure S-Tag (outer): VLAN 4000, TPID 0x88A8
- Configure C-Tag (inner): VLAN 100, TPID 0x8100
- Transmit frame
- Verify both tags present on wire:
  - Outer: TPID 0x88A8, VID 4000
  - Inner: TPID 0x8100, VID 100

**UT-VLAN-008: VLAN Membership Table Programming**
- Add VLANs 100, 200, 300 to membership table
- Read back VLAN table entries
- Verify VLANs programmed correctly
- Remove VLAN 200, confirm removal

**UT-VLAN-009: Maximum VLAN Support**
- Add 4094 VLANs to membership table (valid range: 1-4094)
- Verify all VLANs registered successfully
- Test boundary conditions: VID=1 (min), VID=4094 (max)
- Confirm VID=0 (priority tagged) and VID=4095 (reserved) rejected

**UT-VLAN-010: VLAN Statistics**
- Transmit 100 tagged frames (VLAN 100)
- Receive 50 tagged frames (VLAN 100)
- Receive 10 untagged frames (PVID=100)
- Drop 5 frames (VID not registered)
- Read VLAN statistics counters
- Verify counts accurate

---

### **3 Integration Tests**

**IT-VLAN-001: AVB Stream VLAN Isolation**
- Configure 2 AVB streams on same adapter:
  - Stream 1: VLAN 100, Class A (TC6)
  - Stream 2: VLAN 200, Class A (TC6)
- Transmit both streams simultaneously
- Verify each stream tagged with correct VLAN
- Confirm no cross-contamination (Stream 1 only on VLAN 100)
- Receiver filters by VLAN, receives only subscribed stream

**IT-VLAN-002: PCP-Based QoS Enforcement**
- Configure network with 3 traffic classes:
  - PCP 6 (Class A) → TC6 → 75% bandwidth (CBS)
  - PCP 5 (Class B) → TC5 → 25% bandwidth (CBS)
  - PCP 0 (Best Effort) → TC0 → Remaining bandwidth
- Transmit mixed traffic with different PCPs
- Verify bandwidth allocation matches PCP priorities
- Measure latency:
  - PCP 6: ≤2ms (Class A)
  - PCP 5: ≤50ms (Class B)
  - PCP 0: Best effort (no guarantee)

**IT-VLAN-003: Multi-VLAN gPTP Synchronization**
- Configure 3 gPTP domains on different VLANs:
  - VLAN 100: Domain 0 (factory floor)
  - VLAN 200: Domain 1 (office network)
  - VLAN 300: Domain 2 (test network)
- Run gPTP on all VLANs simultaneously
- Verify independent synchronization per VLAN
- Confirm no cross-domain interference

---

### **2 V&V Tests**

**VV-VLAN-001: 24-Hour Multi-VLAN Stability**
- Configure 10 VLANs with mixed AVB/TSN traffic
- Run for 24 hours with continuous streams
- Verify:
  - Zero VLAN tag errors
  - VLAN filtering 100% accurate
  - No VLAN membership table corruption
  - PCP→TC mapping stable

**VV-VLAN-002: Production Network with VLAN Segmentation**
- Configure enterprise AVB network:
  - VLAN 100: Audio streams (10 talkers, 20 listeners)
  - VLAN 200: Video streams (5 talkers, 10 listeners)
  - VLAN 300: Control traffic (PLC, SCADA)
  - VLAN 400: Management (gPTP, SRP)
- Run for 1 hour with realistic traffic
- Verify:
  - All streams isolated by VLAN (zero leakage)
  - QoS enforced per VLAN (latency bounds met)
  - VLAN filtering prevents unauthorized access
  - No performance degradation with 4 active VLANs

---

## 🔧 Implementation Notes

### **VLAN Tag Structure**

```c
#pragma pack(push, 1)

// IEEE 802.1Q VLAN tag
typedef struct _VLAN_TAG {
    UINT16 TPID;  // Tag Protocol Identifier (0x8100 for 802.1Q)
    UINT16 TCI;   // Tag Control Information
    // TCI bits:
    //   [15:13] PCP (Priority Code Point)
    //   [12]    DEI (Drop Eligible Indicator)
    //   [11:0]  VID (VLAN Identifier)
} VLAN_TAG;

// IEEE 802.1ad double tag (QinQ)
typedef struct _QINQ_TAG {
    UINT16 S_TPID;  // Service TPID (0x88A8)
    UINT16 S_TCI;   // Service Tag Control Info
    UINT16 C_TPID;  // Customer TPID (0x8100)
    UINT16 C_TCI;   // Customer Tag Control Info
} QINQ_TAG;

#pragma pack(pop)

// Extract fields from TCI
#define VLAN_GET_PCP(tci)  (((tci) >> 13) & 0x7)
#define VLAN_GET_DEI(tci)  (((tci) >> 12) & 0x1)
#define VLAN_GET_VID(tci)  ((tci) & 0xFFF)

// Build TCI
#define VLAN_BUILD_TCI(pcp, dei, vid) \
    ((((pcp) & 0x7) << 13) | (((dei) & 0x1) << 12) | ((vid) & 0xFFF))
```

### **VLAN Tag Insertion**

```c
// Insert VLAN tag into outgoing frame
VOID InsertVlanTag(UINT8* frame, UINT32* length, UINT16 vlanId, UINT8 pcp) {
    // VLAN tag goes between MAC SA and EtherType
    // Original: [DA(6)][SA(6)][EtherType(2)][Payload]
    // Tagged:   [DA(6)][SA(6)][TPID(2)][TCI(2)][EtherType(2)][Payload]
    
    // Shift payload right by 4 bytes to make room for VLAN tag
    memmove(frame + 16, frame + 12, *length - 12);
    
    // Insert VLAN tag
    VLAN_TAG* vlanTag = (VLAN_TAG*)(frame + 12);
    vlanTag->TPID = htons(0x8100);  // 802.1Q
    vlanTag->TCI = htons(VLAN_BUILD_TCI(pcp, 0, vlanId));
    
    *length += 4;  // Frame now 4 bytes longer
}
```

### **VLAN Filtering**

```c
// VLAN membership table (4096 VLANs, bit vector)
UINT32 VlanMembershipTable[128];  // 4096 bits / 32 = 128 words

// Add VLAN to membership table
VOID AddVlan(UINT16 vlanId) {
    if (vlanId == 0 || vlanId >= 4095) {
        DbgPrint("Invalid VLAN ID: %u\n", vlanId);
        return;
    }
    
    UINT32 wordIndex = vlanId / 32;
    UINT32 bitIndex = vlanId % 32;
    VlanMembershipTable[wordIndex] |= (1 << bitIndex);
    
    // Program hardware VLAN filter table
    WRITE_REG32(I225_VLAN_FILTER_BASE + (wordIndex * 4), VlanMembershipTable[wordIndex]);
}

// Check if VLAN is registered
BOOLEAN IsVlanRegistered(UINT16 vlanId) {
    UINT32 wordIndex = vlanId / 32;
    UINT32 bitIndex = vlanId % 32;
    return (VlanMembershipTable[wordIndex] & (1 << bitIndex)) != 0;
}

// Filter incoming frame by VLAN
BOOLEAN FilterVlan(UINT8* frame) {
    // Check if frame is VLAN-tagged
    UINT16 etherType = ntohs(*(UINT16*)(frame + 12));
    
    if (etherType == 0x8100) {  // VLAN-tagged
        VLAN_TAG* vlanTag = (VLAN_TAG*)(frame + 12);
        UINT16 vlanId = VLAN_GET_VID(ntohs(vlanTag->TCI));
        
        if (!IsVlanRegistered(vlanId)) {
            // Drop frame (VLAN not registered)
            IncrementDropCounter();
            return FALSE;
        }
    } else {
        // Untagged frame, use PVID
        UINT16 pvid = GetPortVlanId();
        if (!IsVlanRegistered(pvid)) {
            return FALSE;
        }
    }
    
    return TRUE;  // Accept frame
}
```

### **PCP to TC Mapping**

```c
// Default PCP→TC mapping (1:1)
UINT8 PcpToTcMap[8] = {0, 1, 2, 3, 4, 5, 6, 7};

// Configure PCP→TC mapping
VOID ConfigurePcpMapping(UINT8* mapping) {
    memcpy(PcpToTcMap, mapping, 8);
    
    // Program hardware mapping table
    for (UINT8 i = 0; i < 8; i++) {
        UINT32 reg = READ_REG32(I225_PCP_TC_MAP);
        reg &= ~(0xF << (i * 4));  // Clear mapping for PCP[i]
        reg |= (PcpToTcMap[i] << (i * 4));
        WRITE_REG32(I225_PCP_TC_MAP, reg);
    }
}

// Get traffic class from PCP
UINT8 GetTrafficClassFromPcp(UINT8 pcp) {
    return PcpToTcMap[pcp & 0x7];
}
```

---

## 📊 Performance Targets

| Metric | Target | Measurement |
|--------|--------|-------------|
| VLAN Tag Insertion Overhead | <1µs | Per frame |
| VLAN Filtering Latency | <100ns | Hardware lookup |
| Max VLANs Supported | 4094 | Per adapter |
| PCP→TC Mapping Accuracy | 100% | All frames |
| VLAN Statistics Accuracy | 100% | Counter validation |
| Multi-VLAN Throughput | Line rate | No degradation |

---

## 📈 Acceptance Criteria

- [x] All 10 unit tests pass
- [x] All 3 integration tests pass
- [x] All 2 V&V tests pass
- [x] VLAN tags inserted correctly (100% accuracy)
- [x] VLAN filtering 100% accurate
- [x] PCP→TC mapping correct for all PCPs (0-7)
- [x] 24-hour stability test passes
- [x] Production network: zero VLAN leakage

---

**Standards**: IEEE 802.1Q (VLAN), IEEE 802.1ad (QinQ), IEEE 1012-2016, ISO/IEC/IEEE 12207:2017  
**XP Practice**: TDD - Tests defined before implementation

**Estimated Effort**: 8-10 days  
**Dependencies**: PCP mapping, Traffic Class support, Hardware VLAN filter

**Key Differentiation**: Complete VLAN isolation with QinQ support for enterprise AVB network segmentation
