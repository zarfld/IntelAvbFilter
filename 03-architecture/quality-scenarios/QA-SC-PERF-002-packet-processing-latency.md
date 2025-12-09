# QA-SC-PERF-002: NDIS Filter Packet Processing Latency

**Status**: Draft  
**Date**: 2025-12-09  
**Phase**: Phase 03 - Architecture Design  
**Quality Attribute**: Performance (Latency)  
**Priority**: Critical (P0)

---

## Scenario Overview

This quality scenario defines and validates the latency overhead introduced by the IntelAvbFilter NDIS 6.0 filter driver when forwarding network packets through the driver stack. The scenario ensures that the filter driver adds minimal latency (<1µs) to packet processing, preserving the low-latency requirements of IEEE 802.1AS (gPTP) and IEEE 802.1Qav/Qat (AVB/TSN) traffic.

**Key Insight**: For AVB/TSN applications requiring <1µs timestamp precision, the NDIS filter must be nearly transparent in the data plane. Any latency >1µs would violate gPTP sync accuracy requirements and degrade TSN traffic shaping effectiveness.

---

## ATAM Structured Quality Scenario

### Source
Network traffic (Ethernet frames) arriving at or departing from the network interface card (NIC).

**Traffic Types**:
- **AVB/TSN Control Frames**: gPTP (802.1AS) sync/follow-up packets requiring <1µs precision
- **AVB Streams**: Audio/video data with latency budgets <2ms (Class A) or <50ms (Class B)
- **Best-Effort Traffic**: Non-TSN traffic that should not be significantly impacted

### Stimulus
A packet (NET_BUFFER_LIST) arrives at the filter driver's `FilterReceiveNetBufferLists` or `FilterSendNetBufferLists` handler for processing and forwarding.

**Typical Scenarios**:
1. **RX Path**: Packet received from NIC → Filter inspects → Forwards to protocol stack
2. **TX Path**: Packet sent from protocol stack → Filter inspects → Forwards to NIC
3. **Timestamp Capture**: Filter captures TX/RX hardware timestamps for gPTP packets

### Environment
**System State**: Normal operation
- **Load**: Typical network load (1000 packets/second for AVB streams, up to 10,000 packets/second burst)
- **IRQL**: DISPATCH_LEVEL (ISR/DPC context for receive path)
- **CPU**: Single core (worst case for latency measurement)
- **Hardware**: Intel I226-V/I225-V/I210 NIC with DMA enabled

**Filter State**: Active and forwarding
- Filter attached to adapter
- No blocking IOCTLs in progress
- Timestamp capture enabled (if applicable)

### Artifact
**NDIS Filter Driver**: IntelAvbFilter packet forwarding path

**Key Components**:
- `FilterReceiveNetBufferLists()` - RX packet handler (filter.c)
- `FilterSendNetBufferLists()` - TX packet handler (filter.c)
- Packet inspection logic (AVB ethertype detection, VLAN tag parsing)
- Timestamp capture hooks (optional, for gPTP packets)

### Response
The filter driver processes the packet and forwards it to the next layer (miniport or protocol driver) with minimal added latency.

**Processing Steps**:
1. **Receive NBL**: Accept NET_BUFFER_LIST from NDIS
2. **Inspect Headers**: Check ethertype (AVB, gPTP, VLAN tags) - fast path
3. **Capture Timestamp** (if applicable): Read hardware timestamp from NIC descriptor
4. **Forward NBL**: Call `NdisFIndicateReceiveNetBufferLists()` or `NdisFSendNetBufferLists()`
5. **Complete**: Return control to NDIS

### Response Measure
**Quantified Latency Targets**:

| **Metric** | **Target (p95)** | **Target (p99)** | **Maximum** |
|------------|------------------|------------------|-------------|
| **RX Forwarding Latency** | <500ns | <1µs | <2µs |
| **TX Forwarding Latency** | <500ns | <1µs | <2µs |
| **Timestamp Capture Overhead** | +200ns | +500ns | +1µs |
| **Overall Filter Overhead** | <1µs (p95) | <2µs (p99) | <5µs (worst) |

**Definition**: Latency = Time from NBL received by filter → Time forwarded to next layer

**Measurement Method**: High-resolution timing using `KeQueryPerformanceCounter()` at entry/exit of filter handlers, or ETW (Event Tracing for Windows) with microsecond timestamps.

---

## Rationale

### Why <1µs Latency Matters

#### 1. gPTP Synchronization Accuracy (IEEE 802.1AS)
**Requirement**: gPTP achieves <100ns time offset between master and slave clocks.

**Impact of Filter Latency**:
- **Symmetric Path Delay**: gPTP assumes symmetric network delays (TX ≈ RX latency)
- **Asymmetry = Timing Error**: If filter adds 1µs RX but 0ns TX, the asymmetry introduces 500ns offset error
- **Budget**: Total allowed asymmetry <500ns → Filter must contribute <100ns asymmetry

**Consequence**: Excessive filter latency (>1µs) breaks gPTP sync accuracy, causing audio/video glitches in AVB streams.

#### 2. TSN Traffic Shaping (IEEE 802.1Qav Credit-Based Shaper)
**Requirement**: CBS credits updated every 1µs based on transmission slots.

**Impact of Filter Latency**:
- **Credit Leak**: If filter delays packet forwarding by 5µs, CBS credits continue leaking during that time
- **Bandwidth Loss**: 5µs delay per packet @ 1000 packets/sec = 5ms/sec = 0.5% bandwidth waste
- **Class A Streams**: 2ms latency budget → 5µs per hop consumes 0.25% of budget

**Consequence**: Filter latency >2µs reduces effective AVB stream bandwidth and increases end-to-end latency.

#### 3. TSN Gate Control (IEEE 802.1Qbv Time-Aware Shaper)
**Requirement**: TAS gates open/close on 1µs boundaries.

**Impact of Filter Latency**:
- **Gate Miss**: If filter delays packet by 2µs, packet may miss its gate window (e.g., 5µs window)
- **Dropped Packets**: Packet arrives after gate closes → dropped or delayed to next cycle
- **Jitter**: Variable filter latency (500ns-5µs) causes unpredictable gate hit/miss patterns

**Consequence**: Filter latency >1µs risks TAS gate misses, violating TSN deterministic guarantees.

#### 4. System-Wide Latency Budget
**TSN Class A Stream Budget**: 2ms end-to-end latency (7 hops max)
- **Per-Hop Budget**: 2ms / 7 hops = 285µs/hop
- **Filter Contribution**: <1µs = 0.35% of hop budget (acceptable)
- **If Filter = 10µs**: 10µs × 7 hops = 70µs = 3.5% of budget (marginal)
- **If Filter = 100µs**: 100µs × 7 hops = 700µs = 35% of budget (**unacceptable**)

**Guideline**: Keep filter overhead <1% of per-hop budget → <2.85µs. We target <1µs for safety margin.

---

## Related Requirements

### Primary Requirement
- **#31** (StR-004: NDIS Filter Driver Implementation)  
  *"The system shall implement an NDIS 6.0 filter driver to intercept and process network traffic."*

### Performance Requirements (Implicit from Script)
- **REQ-NF-PERF-NDIS-001**: Packet Forwarding (<1µs overhead) - Priority: Critical  
  *"NDIS filter adds <1µs packet forwarding overhead"* (from `create-phase02-requirements.ps1`)

### Related Quality Attributes
- **#127** QA-SC-PERF-001: IOCTL Response Time (<100µs for control plane operations)
- **Timestamp Precision**: TX/RX timestamp retrieval must complete in <1µs (separate from forwarding)

---

## Related ADRs

### Architecture Decisions
- **#90** (ADR-ARCH-001: NDIS 6.0 Compatibility)  
  *"Use NDIS 6.0 for maximum Windows compatibility while maintaining <1µs latency"*

- **#117** (ADR-PERF-002: Direct BAR0 MMIO Access)  
  *"Use direct memory-mapped I/O for hardware register access to achieve <500ns latency"*

- **#93** (ADR-PERF-004: Kernel Ring Buffer for Timestamp Events)  
  *"Use lock-free ring buffer for zero-copy timestamp delivery (<1µs per event)"*

- **#126** (ADR-HAL-001: Hardware Abstraction Layer)  
  *"Function pointer-based HAL for multi-controller support with zero runtime overhead"*

---

## Validation Method

### Performance Budget Breakdown (RX Path)

| **Component** | **Operation** | **Budget (ns)** | **Cumulative (ns)** |
|---------------|---------------|-----------------|---------------------|
| **NDIS Dispatch** | Call FilterReceiveNetBufferLists | ~50ns | 50ns |
| **NBL Validation** | Check for NULL, validate pointers | ~20ns | 70ns |
| **Header Inspection** | Read EtherType from NET_BUFFER | ~50ns | 120ns |
| **AVB Detection** | Compare EtherType (0x22F0) | ~10ns | 130ns |
| **Timestamp Capture** (optional) | Read DMA descriptor timestamp | ~200ns | 330ns |
| **Context Update** | Increment packet counter | ~30ns | 360ns |
| **NDIS Forward** | NdisFIndicateReceiveNetBufferLists | ~100ns | 460ns |
| **Return** | Function epilogue | ~20ns | 480ns |
| **Total (Fast Path)** | No timestamp capture | **~260ns** | ✅ Well within <500ns p95 |
| **Total (Timestamp)** | With timestamp capture | **~480ns** | ✅ Within <500ns p95 |

**Worst Case (p99)**: +300ns for cache misses, context switches → **~780ns** (still <1µs ✅)

### TX Path Budget (Similar)

| **Component** | **Operation** | **Budget (ns)** |
|---------------|---------------|-----------------|
| **NDIS Dispatch** | Call FilterSendNetBufferLists | ~50ns |
| **Header Inspection** | Check packet headers | ~70ns |
| **Timestamp Arm** (optional) | Set TX timestamp request bit | ~100ns |
| **NDIS Forward** | NdisFSendNetBufferLists | ~100ns |
| **Total** | TX forwarding | **~320ns** ✅ |

### Benchmark Implementation

**Tool**: `packet_latency_benchmark.c` (to be created in `tools/avb_test/`)

**Methodology**:
1. **Kernel Instrumentation**: Insert `KeQueryPerformanceCounter()` calls at entry/exit of filter handlers
2. **Sample Collection**: Capture 100,000 packet forwarding events under various load conditions
3. **Histogram Analysis**: Calculate p50, p95, p99, max latencies
4. **ETW Alternative**: Use Event Tracing for Windows with high-resolution timestamps (preferred for production)

**Pseudo-Code**:
```c
// In FilterReceiveNetBufferLists()
LARGE_INTEGER StartTime, EndTime, Frequency;
LONGLONG ElapsedNs;

KeQueryPerformanceCounter(&Frequency);
StartTime = KeQueryPerformanceCounter(NULL);

// Filter processing logic
FilterInspectPacket(NetBufferLists);
NdisFIndicateReceiveNetBufferLists(FilterHandle, NetBufferLists, ...);

EndTime = KeQueryPerformanceCounter(NULL);
ElapsedNs = ((EndTime.QuadPart - StartTime.QuadPart) * 1000000000) / Frequency.QuadPart;

// Log to ring buffer or ETW
LogLatency(ElapsedNs);
```

**Test Environment**:
- **Hardware**: Intel I226-V NIC @ PCIe Gen3 x1
- **Traffic Generator**: pktgen-dpdk or custom user-mode sender
- **Load Profiles**:
  1. **Idle**: 10 packets/sec (baseline latency)
  2. **Typical AVB**: 1000 packets/sec (audio stream)
  3. **Burst**: 10,000 packets/sec (stress test)
  4. **Mixed**: 80% best-effort + 20% AVB (realistic)

**Pass Criteria**:
- ✅ p95 latency <500ns (all load profiles)
- ✅ p99 latency <1µs (all load profiles)
- ✅ Max latency <5µs (excluding OS scheduler delays)

---

## Current Status

### Implementation Status
**As of 2025-12-09**: ⚠️ **Partially Implemented**

**What Exists**:
- ✅ NDIS 6.0 filter driver skeleton (`filter.c`, `FilterReceiveNetBufferLists`, `FilterSendNetBufferLists`)
- ✅ Basic packet forwarding (pass-through mode)
- ✅ Header inspection logic (EtherType detection for AVB packets)

**What's Missing**:
- ❌ Latency instrumentation (no timing measurements in current code)
- ❌ Performance benchmarking tool (`packet_latency_benchmark.c`)
- ❌ ETW event provider for production latency monitoring
- ❌ Optimized fast-path logic (currently uses generic NDIS patterns)

### Measured Performance
**Status**: ⚠️ **Not Yet Measured**

**Estimated Performance** (based on similar NDIS filter drivers):
- Current implementation likely adds **<1µs** latency (NDIS 6.0 optimized path)
- Timestamp capture adds **~200-500ns** (reading DMA descriptor)
- Worst-case spikes during CPU contention: **~5-10µs** (acceptable for AVB)

**Action Required**: Create `packet_latency_benchmark.c` and run validation tests to confirm <1µs target.

---

## Acceptance Criteria (Gherkin Format)

```gherkin
Feature: NDIS Filter Packet Processing Latency
  As an AVB/TSN network filter driver
  I want to forward packets with minimal latency overhead
  So that gPTP synchronization and TSN traffic shaping remain accurate

  Background:
    Given the IntelAvbFilter driver is loaded and attached to Intel I226-V NIC
    And the NIC is receiving/transmitting network traffic
    And packet latency instrumentation is enabled (KeQueryPerformanceCounter)

  Scenario: RX packet forwarding latency under typical load
    Given the system is receiving AVB audio stream packets at 1000 packets/sec
    When the filter receives a NET_BUFFER_LIST in FilterReceiveNetBufferLists
    And the filter inspects packet headers (EtherType check)
    And the filter forwards the NBL via NdisFIndicateReceiveNetBufferLists
    Then the p95 latency shall be <500ns
    And the p99 latency shall be <1µs
    And the maximum latency (excluding OS scheduler delays) shall be <5µs

  Scenario: TX packet forwarding latency under burst load
    Given the system is transmitting 10,000 packets/sec (burst test)
    When the filter receives NBLs in FilterSendNetBufferLists
    And the filter forwards NBLs via NdisFSendNetBufferLists
    Then the p95 latency shall be <500ns
    And the p99 latency shall be <1µs
    And no packets shall be dropped due to filter processing delays

  Scenario: Timestamp capture overhead for gPTP packets
    Given the filter is capturing TX/RX hardware timestamps for gPTP packets
    When a gPTP sync packet passes through the filter
    And the filter reads the timestamp from the NIC DMA descriptor
    Then the additional overhead shall be <500ns (p95)
    And the total forwarding latency (inspection + timestamp) shall be <1µs (p95)

  Scenario: Latency stability under mixed traffic
    Given the system is handling 80% best-effort traffic + 20% AVB traffic
    When the filter processes mixed packet types
    Then AVB packet latency shall remain <1µs (p95)
    And latency jitter (p99 - p50) shall be <500ns
    And best-effort traffic shall not impact AVB latency targets
```

---

## Performance Optimization Strategies

### Fast-Path Optimizations

#### 1. Branch Prediction Hints
```c
// Use __builtin_expect for common paths (GCC/Clang)
if (__builtin_expect(IsAvbPacket(EtherType), 0)) {
    // Rare: AVB packet processing (1% of traffic)
    CaptureTimestamp(NetBuffer);
} else {
    // Common: Best-effort traffic (99% of traffic)
    // Fall through to fast forward
}
```

#### 2. Cache-Friendly Data Structures
- Keep filter context structure <64 bytes (one cache line)
- Align frequently accessed counters to cache line boundaries
- Use atomic operations for lock-free packet counting

#### 3. Minimize Function Call Overhead
- Inline critical functions (`FilterInspectPacket()`)
- Use direct NDIS calls (avoid wrapper layers)
- Prefetch next NBL in linked list during processing

#### 4. Zero-Copy Forwarding
- **Never allocate memory** in packet forwarding path
- **Never copy packets** unless required for inspection
- Use NET_BUFFER_LIST clone for timestamp capture (if needed)

### Hardware-Specific Optimizations

#### Intel I226/I225: Descriptor Prefetching
- Enable RX descriptor prefetching in NIC configuration
- Reduces timestamp read latency from 500ns → 200ns

#### PCIe Latency Tuning
- Configure PCIe Max Read Request Size (MRRS) = 512 bytes
- Enable PCIe Extended Tags for higher throughput
- Tune PCIe Active State Power Management (ASPM) for latency vs. power

---

## Risks and Mitigations

| **Risk** | **Impact** | **Likelihood** | **Mitigation** |
|----------|------------|----------------|----------------|
| **CPU Scheduler Preemption** | 10µs-100µs latency spikes | Medium | Run filter at DISPATCH_LEVEL (non-preemptible), disable interrupts during critical sections |
| **Cache Misses** | +300ns per miss | High (first packet) | Prefetch packet headers, keep filter context cache-hot |
| **NIC DMA Latency** | +500ns for timestamp read | Low (hardware) | Use descriptor prefetching, ensure PCIe Gen3 x1 minimum |
| **NDIS Overhead** | +100-200ns per NDIS call | Low (API design) | Use inline forwarding when possible, minimize NDIS API calls |
| **Contention (Multi-Core)** | +1-5µs for lock waits | Medium | Lock-free packet counters, per-CPU data structures |
| **ETW Logging Overhead** | +500ns-2µs per event | High (if enabled) | Use ring buffer for production, ETW only for diagnostics |

---

## Comparison to Control Plane (IOCTL Latency)

| **Aspect** | **Data Plane (Packets)** | **Control Plane (IOCTLs)** |
|------------|--------------------------|----------------------------|
| **Latency Target** | <1µs (p95) | <100µs (p95) |
| **Frequency** | 1000-10,000 ops/sec | 1-100 ops/sec |
| **Context** | DISPATCH_LEVEL (ISR/DPC) | PASSIVE_LEVEL (user thread) |
| **Optimization** | Zero-copy, inline, lock-free | Validation, HAL dispatch, MMIO |
| **Bottleneck** | CPU cache, PCIe latency | System call overhead, NDIS dispatch |
| **Consequence** | Impacts gPTP sync, TSN shaping | User experience (CLI responsiveness) |

**Key Difference**: Data plane latency directly affects TSN correctness; control plane latency affects user experience but not real-time guarantees.

---

## Dependencies

### Prerequisites
- **#90** (ADR-ARCH-001: NDIS 6.0 Compatibility) - Filter driver framework
- **#117** (ADR-PERF-002: Direct BAR0 MMIO Access) - Low-latency register access
- **#126** (ADR-HAL-001: Hardware Abstraction Layer) - Zero-overhead controller ops

### Enables
- **gPTP Synchronization**: <100ns time offset achievable with <1µs filter latency
- **TSN Traffic Shaping**: CBS/TAS operate correctly with minimal filter interference
- **AVB Stream Quality**: Audio/video streams meet latency budgets (2ms Class A, 50ms Class B)

---

## Standards Compliance

- **ISO/IEC 25010:2011**: Quality Model - Performance Efficiency (Time Behavior)
- **ISO/IEC/IEEE 42010:2011**: Architecture Description - Quality Attribute Scenarios
- **IEEE 802.1AS-2020**: Timing and Synchronization (gPTP) - Symmetric delay assumption
- **IEEE 802.1Qav-2009**: Credit-Based Shaper - 1µs credit update granularity
- **IEEE 802.1Qbv-2015**: Time-Aware Shaper - 1µs gate control granularity

---

## Verification Strategy

### Test Levels

1. **Unit Tests** (Isolated Components)
   - Test `FilterInspectPacket()` with synthetic NBLs
   - Measure header parsing latency in isolation
   - Validate fast-path vs. slow-path branching logic

2. **Integration Tests** (Full Filter Path)
   - Deploy filter on test NIC (Intel I226-V)
   - Generate traffic with pktgen-dpdk (controlled load)
   - Measure end-to-end latency with `packet_latency_benchmark.c`
   - Collect 100,000 samples per test scenario (idle, typical, burst, mixed)

3. **System Tests** (Real-World Scenarios)
   - Run gPTP daemon (linuxptp or AVnu gptp) over filtered interface
   - Measure gPTP sync offset (should remain <100ns)
   - Stream AVB audio (Talker → Listener) and measure glitches
   - Monitor CBS credit exhaustion (should not occur under normal load)

4. **Stress Tests** (Worst-Case Conditions)
   - 100% CPU load on all cores (simulated with `stress-ng`)
   - 10Gbps wire-rate traffic (burst)
   - Simultaneous IOCTLs + packet forwarding
   - Validate p99 latency remains <2µs under stress

### Continuous Monitoring (Production)

- **ETW Events**: Log p95/p99 latencies every 60 seconds
- **Performance Counters**: Expose packet latency histograms via `\\IntelAvbFilter\\PacketLatency`
- **Alerts**: Trigger warning if p95 exceeds 1µs or p99 exceeds 5µs

---

## Traceability

### Requirements
- **#31** (StR-004: NDIS Filter Driver Implementation)
- **REQ-NF-PERF-NDIS-001** (Packet Forwarding <1µs overhead) - Implicit from script

### Architecture
- **#90** (ADR-ARCH-001: NDIS 6.0 Compatibility)
- **#117** (ADR-PERF-002: Direct BAR0 MMIO Access)
- **#93** (ADR-PERF-004: Kernel Ring Buffer Timestamp Events)
- **#126** (ADR-HAL-001: Hardware Abstraction Layer)

### Quality Scenarios
- **#127** (QA-SC-PERF-001: IOCTL Response Time) - Control plane latency
- **#128** (QA-SC-PERF-002: Packet Processing Latency) - **This Document**

### Verification
- **TEST-PERF-PACKET-001** (Packet Forwarding Latency Benchmark) - To be created in Phase 07
- **TEST-PERF-GPTP-001** (gPTP Sync Accuracy Validation) - To be created in Phase 07

---

## Notes

### Implementation Priorities

**Phase 05 (Implementation)**:
1. ✅ Basic packet forwarding (pass-through) - **Already Implemented**
2. ⚠️ Add latency instrumentation (`KeQueryPerformanceCounter()`) - **TODO**
3. ⚠️ Optimize fast-path (inline functions, branch hints) - **TODO**
4. ⚠️ Implement `packet_latency_benchmark.c` tool - **TODO**

**Phase 07 (Verification)**:
5. Run latency benchmarks (idle, typical, burst, mixed load)
6. Validate p95 <500ns, p99 <1µs targets
7. Integrate with gPTP daemon and measure sync offset
8. Document performance baselines per NIC model (I226/I225/I210)

### Hardware Dependencies

**NIC Models** (from ADR-HAL-001):
- **I226-V/I226-IT**: Full TSN support, best performance (<300ns latency)
- **I225-V**: Partial TSN support, similar latency to I226
- **I210/I211**: Legacy PHC, +100-200ns latency due to stuck workaround
- **I219-LM**: Laptop NIC, higher latency (~1-2µs, acceptable but marginal)

### Future Enhancements

1. **RSS (Receive Side Scaling)**: Distribute packets across CPUs for higher throughput (maintain per-CPU latency <1µs)
2. **XDP (eXpress Data Path)**: Explore XDP-like bypass for ultra-low latency (<100ns) if needed
3. **Hardware Offload**: Use NIC-native packet filtering (Intel FlexFilter) to bypass software filter entirely for non-AVB traffic
4. **eBPF Integration**: Consider eBPF-based packet classification for programmable filtering with <500ns overhead

---

**Created**: 2025-12-09  
**Author**: AI Standards Compliance Advisor  
**Reviewed**: Pending  
**Approved**: Pending
