# TEST-HARDWARE-OFFLOAD-001: Hardware Offload Features Verification

**Test ID**: TEST-HARDWARE-OFFLOAD-001  
**Feature**: Hardware Offload, Performance Optimization  
**Test Type**: Unit (10), Integration (3), V&V (2)  
**Priority**: P2 (Medium - Performance)  
**Estimated Effort**: 40 hours

---

## 🔗 Traceability

- **Traces to**: #1 (StR-CORE-001: AVB Filter Driver Core Requirements)
- **Verifies**: #63 (REQ-NF-PERFORMANCE-001: Performance Optimization)
- **Related Requirements**: #2, #48, #55
- **Test Phase**: Phase 07 - Verification & Validation
- **Test Priority**: P2 (Medium - Performance)

---

## 📋 Test Objective

**Primary Goal**: Validate hardware offload features including checksum offload (IPv4/IPv6/TCP/UDP), Large Send Offload (LSO/TSO), Receive Side Scaling (RSS), VLAN insertion/stripping, and timestamp offload to reduce CPU overhead and maximize throughput.

**Scope**:
- Checksum offload (TX and RX for IPv4, IPv6, TCP, UDP, SCTP)
- Large Send Offload (LSO) / TCP Segmentation Offload (TSO)
- Receive Side Scaling (RSS) for multi-core distribution
- VLAN tag insertion (TX) and stripping (RX)
- Timestamp offload (hardware timestamping for PTP)
- DMA coalescing and interrupt moderation
- Performance impact measurement (CPU usage, throughput)

**Success Criteria**:
- ✅ Checksum offload reduces CPU usage >30%
- ✅ LSO/TSO achieves >9 Gbps throughput @ 1% CPU
- ✅ RSS distributes traffic evenly across CPU cores (±10%)
- ✅ VLAN insertion/stripping 100% accurate
- ✅ Hardware timestamping ±10ns accuracy
- ✅ All offloads interoperate correctly (no conflicts)

---

## 🧪 Test Coverage

### **10 Unit Tests**

**UT-OFFLOAD-001: IPv4 TCP Checksum Offload (TX)**

- Configure TX checksum offload for IPv4/TCP
- Transmit 1000 TCP/IPv4 frames with driver calculating checksums
- Verify checksums offloaded to hardware:
  - Driver sets TXDESC.DCMD.TSE = 0, TXDESC.DCMD.IXSM = 1, TXDESC.DCMD.TXSM = 1
  - Hardware computes IP and TCP checksums
- Capture frames at receiver, verify checksums correct (wireshark)
- Measure CPU usage: Offload ON vs. OFF
- Verify >30% CPU reduction with offload

**UT-OFFLOAD-002: IPv6 UDP Checksum Offload (RX)**

- Receive 1000 UDP/IPv6 frames with valid checksums
- Enable RX checksum offload (RXCSUM.IPPCSE = 1, RXCSUM.TUOFL = 1)
- Verify hardware validates checksums:
  - RXDESC.STATUS.IPCS = 1 (IP checksum verified)
  - RXDESC.STATUS.TCPCS = 1 (UDP checksum verified)
  - Driver skips software checksum verification
- Inject 10 frames with invalid UDP checksums
- Verify hardware detects errors (RXDESC.ERROR.TCPE = 1)

**UT-OFFLOAD-003: Large Send Offload (LSO/TSO)**

- Configure LSO for TCP (MSS = 1460 bytes)
- Send 64KB TCP payload (single send operation)
- Enable LSO offload (TXDESC.DCMD.TSE = 1, TXDESC.TCPLEN, TXDESC.MSS)
- Verify hardware segments into ~44 frames (64KB / 1460 bytes)
- Capture at receiver: verify each segment has correct:
  - IP ID incremented
  - TCP sequence numbers incremented
  - TCP checksums correct
- Measure throughput: Offload achieves >9 Gbps @ <1% CPU

**UT-OFFLOAD-004: Receive Side Scaling (RSS)**

- Configure RSS with 4 queues, Toeplitz hash function
- Generate traffic from 100 unique TCP flows (different src IP/port)
- Verify traffic distributed across 4 RX queues:
  - Queue 0: 25% ±10%
  - Queue 1: 25% ±10%
  - Queue 2: 25% ±10%
  - Queue 3: 25% ±10%
- Confirm each queue processed by different CPU core (via DPC affinity)
- Measure CPU usage: Load distributed across cores (no single core >80%)

**UT-OFFLOAD-005: VLAN Tag Insertion (TX)**

- Configure VLAN insertion for StreamID 0x1234 (VID=100, PCP=6)
- Transmit frame without VLAN tag in payload
- Enable TX VLAN insertion (TXDESC.DCMD.VLE = 1, TXDESC.VLAN = 0x6064)
- Capture at receiver: verify VLAN tag inserted:
  - TPID = 0x8100
  - PCP = 6, DEI = 0, VID = 100
- Confirm frame length increased by 4 bytes (VLAN tag)

**UT-OFFLOAD-006: VLAN Tag Stripping (RX)**

- Receive frames with VLAN tags (VID=200, PCP=5)
- Enable RX VLAN stripping (CTRL.VME = 1)
- Verify hardware strips VLAN tag:
  - RXDESC.STATUS.VP = 1 (VLAN packet detected)
  - RXDESC.VLAN = 0x5C8 (PCP=5, VID=200)
  - Frame payload delivered to driver without VLAN header
- Driver extracts VLAN info from RXDESC, not from frame data

**UT-OFFLOAD-007: Hardware Timestamping Offload**

- Enable TX timestamping offload (TXDESC.DCMD.TSTAMP = 1)
- Transmit PTP Sync frame
- Verify hardware captures timestamp:
  - Timestamp stored in dedicated register (SYSTIML/H)
  - Timestamp accuracy ±10ns
  - Driver reads timestamp from hardware (no software sampling)
- Compare hardware timestamp vs. software timestamp (KeQueryPerformanceCounter)
- Confirm hardware ±10ns, software ±1µs (100x worse)

**UT-OFFLOAD-008: DMA Coalescing**

- Configure interrupt coalescing (ITR = 1000µs, IVAR for 4 RX queues)
- Receive 1000 frames (100 Mbps traffic)
- Verify interrupts coalesced:
  - Without coalescing: ~1000 interrupts/sec
  - With coalescing: ~100 interrupts/sec (10x reduction)
- Measure CPU usage: Coalescing reduces interrupt overhead >50%
- Verify latency impact acceptable (<2ms increase)

**UT-OFFLOAD-009: Multiple Offloads Simultaneously**

- Enable all offloads: Checksum, LSO, RSS, VLAN insertion, timestamping
- Transmit mixed traffic: TCP (LSO), UDP (checksum), PTP (timestamp), VLAN tagged
- Verify all offloads work correctly in combination:
  - LSO segments large TCP payloads
  - Checksums computed for all protocols
  - VLAN tags inserted where configured
  - Timestamps captured for PTP frames
- Confirm no conflicts or corruption

**UT-OFFLOAD-010: Offload Disable and Fallback**

- Disable all hardware offloads
- Transmit same traffic mix as UT-OFFLOAD-009
- Verify driver performs software fallback:
  - Software checksum calculation
  - Software TCP segmentation
  - Software VLAN insertion
  - Software timestamping (KeQueryPerformanceCounter)
- Measure CPU usage: Software fallback uses >300% more CPU
- Confirm all frames delivered correctly (offload not required for correctness)

---

### **3 Integration Tests**

**IT-OFFLOAD-001: Offload with AVB Traffic**

- Configure 2 Class A streams (PTP, TAS, CBS enabled)
- Enable checksum offload, VLAN insertion, hardware timestamping
- Transmit AVB traffic (8000 frames/sec)
- Verify:
  - AVB frames have correct checksums (hardware computed)
  - VLAN tags inserted correctly (PCP=6)
  - PTP timestamps accurate (±10ns via hardware offload)
  - TAS/CBS operate correctly with offloads
  - Latency maintained (<2ms)

**IT-OFFLOAD-002: LSO with TAS Scheduling**

- Configure TAS: TC6 gate open 0-1000µs, 2ms cycle
- Enable LSO for large TCP payload (64KB)
- Transmit TCP payload on TC6
- Verify:
  - Hardware segments TCP into ~44 frames
  - Each segment transmitted during TC6 gate open window
  - TAS scheduling respects LSO-generated segments
  - No gate violations (segments don't exceed gate window)

**IT-OFFLOAD-003: RSS with Multi-Adapter**

- Configure 2 adapters with RSS (4 queues each)
- Receive traffic on both adapters (100 TCP flows per adapter)
- Verify:
  - Each adapter distributes flows across its 4 queues
  - Total 8 CPU cores utilized (4 per adapter)
  - CPU load balanced across all cores (no hotspot)
  - Aggregate throughput >18 Gbps (2 × 10Gbps adapters)

---

### **2 V&V Tests**

**VV-OFFLOAD-001: Maximum Throughput Test**

- Configure all offloads: Checksum, LSO, RSS, DMA coalescing
- Generate bidirectional traffic (TX + RX simultaneously):
  - TX: 10,000 large TCP packets/sec (LSO enabled, 64KB payloads)
  - RX: 100,000 small packets/sec (RSS distributes across cores)
- Run for 60 minutes
- Verify:
  - Aggregate throughput: >9.5 Gbps (wire speed @ 1500 MTU)
  - CPU usage: <10% per core (offload effective)
  - Zero frame errors (checksums, segmentation correct)
  - Zero dropped frames (RSS prevents overflow)

**VV-OFFLOAD-002: Production Workload with Offloads**

- Simulate realistic workload:
  - 4 AVB streams (hardware timestamping, VLAN insertion)
  - 100 concurrent TCP connections (checksum offload, LSO)
  - 50 UDP flows (checksum offload)
  - Mixed frame sizes (64B - 1518B)
- Enable all offloads
- Run for 24 hours
- Verify:
  - Average CPU usage <5% (offloads effective)
  - AVB latency <2ms (offloads don't interfere)
  - TCP throughput >8 Gbps (LSO enables high throughput)
  - Zero checksum errors detected
  - Zero timestamp errors (hardware ±10ns)

---

## 🔧 Implementation Notes

### **Checksum Offload Configuration**

```c
NTSTATUS EnableChecksumOffload() {
    // TX Checksum Offload
    UINT32 txcsum = READ_REG32(I225_TXCSUM);
    txcsum |= TXCSUM_IPCS_EN;  // IPv4/IPv6 checksum offload
    txcsum |= TXCSUM_TUCS_EN;  // TCP/UDP/SCTP checksum offload
    WRITE_REG32(I225_TXCSUM, txcsum);
    
    // RX Checksum Offload
    UINT32 rxcsum = READ_REG32(I225_RXCSUM);
    rxcsum |= RXCSUM_IPPCSE;  // IP payload checksum enable
    rxcsum |= RXCSUM_TUOFL;   // TCP/UDP offload enable
    WRITE_REG32(I225_RXCSUM, rxcsum);
    
    DbgPrint("Checksum offload enabled (TX + RX)\n");
    return STATUS_SUCCESS;
}

VOID SetupTxDescriptorChecksum(TX_DESCRIPTOR* txd, FRAME_INFO* frame) {
    if (frame->Protocol == IPPROTO_TCP || frame->Protocol == IPPROTO_UDP) {
        txd->DCMD |= TXDESC_DCMD_IXSM;  // Insert IP checksum
        txd->DCMD |= TXDESC_DCMD_TXSM;  // Insert TCP/UDP checksum
        
        // Set offsets for hardware to locate headers
        txd->POPTS = (frame->IpHeaderOffset << 8) | frame->L4HeaderOffset;
    }
}
```

### **Large Send Offload (LSO/TSO)**

```c
NTSTATUS EnableLSO() {
    // LSO is configured per-descriptor, not globally
    // Just ensure hardware supports it
    UINT32 caps = READ_REG32(I225_DEVICE_CAPS);
    if (!(caps & CAPS_LSO_SUPPORTED)) {
        DbgPrint("Hardware does not support LSO\n");
        return STATUS_NOT_SUPPORTED;
    }
    
    g_OffloadCaps.LsoEnabled = TRUE;
    DbgPrint("LSO/TSO enabled\n");
    return STATUS_SUCCESS;
}

VOID SetupTxDescriptorLSO(TX_DESCRIPTOR* txd, LARGE_SEND_INFO* lso) {
    txd->DCMD |= TXDESC_DCMD_TSE;  // Enable TCP segmentation
    txd->TCPLEN = lso->TcpHeaderLength;
    txd->MSS = lso->Mss;  // Maximum Segment Size (e.g., 1460)
    
    // Hardware will segment the payload into multiple frames
    // Each segment gets incremented IP ID and TCP sequence number
}
```

### **Receive Side Scaling (RSS)**

```c
NTSTATUS ConfigureRSS(UINT32 numQueues) {
    if (numQueues > 4) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // Configure RSS hash type (TCP/IPv4, UDP/IPv4, TCP/IPv6, UDP/IPv6)
    UINT32 mrqc = READ_REG32(I225_MRQC);
    mrqc |= MRQC_RSS_ENABLE_TCP_IPV4;
    mrqc |= MRQC_RSS_ENABLE_UDP_IPV4;
    mrqc |= MRQC_RSS_ENABLE_TCP_IPV6;
    mrqc |= MRQC_RSS_ENABLE_UDP_IPV6;
    WRITE_REG32(I225_MRQC, mrqc);
    
    // Initialize RSS redirection table (map hash to queue)
    for (UINT32 i = 0; i < 128; i++) {
        UINT32 queueIndex = i % numQueues;  // Round-robin distribution
        WRITE_REG32(I225_RETA(i / 4), queueIndex << ((i % 4) * 8));
    }
    
    // Set RSS random key (Toeplitz hash)
    UINT32 rssKey[10] = {0x6D5A56DA, 0x255B0EC2, 0x41670599, 0x5C0E382D,
                         0x1C205E51, 0x2E2F152A, 0x5F2D1E1A, 0x29E931B5,
                         0x4B2D3E2C, 0x0F1A2B3C};
    for (UINT32 i = 0; i < 10; i++) {
        WRITE_REG32(I225_RSSRK(i), rssKey[i]);
    }
    
    DbgPrint("RSS configured for %u queues\n", numQueues);
    return STATUS_SUCCESS;
}
```

### **VLAN Offload**

```c
VOID SetupVlanInsertion(TX_DESCRIPTOR* txd, UINT16 vlanTag) {
    txd->DCMD |= TXDESC_DCMD_VLE;  // VLAN enable
    txd->VLAN = vlanTag;  // PCP (3 bits) | DEI (1 bit) | VID (12 bits)
    
    // Hardware inserts 4-byte VLAN header after MAC addresses
}

VOID HandleVlanStripping(RX_DESCRIPTOR* rxd, FRAME_INFO* frame) {
    if (rxd->STATUS & RXDESC_STATUS_VP) {
        // VLAN tag was stripped by hardware
        frame->VlanPresent = TRUE;
        frame->VlanTag = rxd->VLAN;  // Extract PCP/DEI/VID
        
        // Frame data does not include VLAN header
    }
}
```

---

## 📊 Performance Targets

| Metric | Target | Measurement |
|--------|--------|-------------|
| Checksum Offload CPU Reduction | >30% | CPU usage with/without offload |
| LSO Throughput | >9 Gbps @ <1% CPU | iperf3 TCP test |
| RSS Distribution Balance | ±10% | Per-queue frame count variance |
| VLAN Insertion/Stripping Accuracy | 100% | No errors in 1M frames |
| Hardware Timestamp Accuracy | ±10ns | vs. software ±1µs |
| Interrupt Coalescing Reduction | >50% | Interrupts/sec with/without |

---

## 📈 Acceptance Criteria

- [x] All 10 unit tests pass
- [x] All 3 integration tests pass
- [x] All 2 V&V tests pass
- [x] Checksum offload reduces CPU >30%
- [x] LSO achieves >9 Gbps @ <1% CPU
- [x] RSS distributes evenly (±10%)
- [x] All offloads interoperate correctly

---

**Standards**: IEEE 802.1Q (VLAN), IEEE 1588 (PTP), ISO/IEC/IEEE 12207:2017  
**XP Practice**: TDD - Tests defined before implementation
