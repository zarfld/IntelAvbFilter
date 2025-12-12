# Critical Requirements (P0) - Acceptance Criteria

**Purpose**: Detailed acceptance criteria for 15 critical (P0) requirements to enable Phase 03 architecture work

**Created**: 2025-12-12  
**Status**: Phase 02 Completion - Critical Path  
**Standards**: ISO/IEC/IEEE 29148:2018, IEEE 1012-2016

---

## 📋 Critical Requirements List (15 total)

### PTP Hardware Clock (PHC) - Core (4 requirements)
1. **REQ-F-IOCTL-PHC-001**: PHC Time Query
2. **REQ-F-IOCTL-TS-001**: TX Timestamp Retrieval  
3. **REQ-F-IOCTL-TS-002**: RX Timestamp Retrieval
4. **REQ-F-IOCTL-PHC-003**: PHC Offset Adjustment

### NDIS Filter Core (4 requirements)
5. **REQ-F-NDIS-ATTACH-001**: FilterAttach/FilterDetach
6. **REQ-F-NDIS-SEND-001**: FilterSend
7. **REQ-F-NDIS-RECEIVE-001**: FilterReceive
8. **REQ-F-HW-DETECT-001**: Hardware Capability Detection

### intel_avb Integration (2 requirements)
9. **REQ-F-INTEL-AVB-003**: Register Access Abstraction
10. **REQ-F-REG-ACCESS-001**: Safe Register Access Coordination

### Standards & Time Epoch (1 requirement)
11. **REQ-F-PTP-EPOCH-001**: PTP Epoch (TAI) Support

### Non-Functional Critical (4 requirements)
12. **REQ-NF-PERF-NDIS-001**: Packet Forwarding Latency (<1µs)
13. **REQ-NF-REL-PHC-001**: PHC Monotonicity
14. **REQ-NF-REL-NDIS-001**: No BSOD (1000 attach/detach cycles)
15. **REQ-NF-PERF-PTP-001**: PTP Sync Accuracy (<100ns)

---

## 1️⃣ REQ-F-IOCTL-PHC-001: PHC Time Query

**Requirement**: System shall provide IOCTL to query PTP Hardware Clock (PHC) current time in TAI format.

**Traces to**: #28 (StR-GPTP-001: gPTP Synchronization)

### Acceptance Criteria

#### Scenario 1: Query PHC time successfully (happy path)
**Given** Intel i225 adapter bound to NDIS filter  
**And** PHC hardware initialized and running  
**And** User-mode application with valid device handle  
**When** Application sends `IOCTL_AVB_PHC_QUERY` via DeviceIoControl  
**Then** IOCTL returns `STATUS_SUCCESS` (0x00000000)  
**And** Output buffer contains 64-bit TAI timestamp in nanoseconds  
**And** Timestamp value is monotonically increasing (T[n+1] > T[n])  
**And** Timestamp is within ±10ms of system time (sanity check against time drift)  
**And** Response time (kernel entry to return) is <500µs (p95)  

#### Scenario 2: Query PHC from multiple threads concurrently
**Given** PHC operational  
**And** 10 concurrent user-mode threads  
**When** All 10 threads simultaneously send `IOCTL_AVB_PHC_QUERY`  
**Then** All 10 requests complete successfully with `STATUS_SUCCESS`  
**And** All timestamps are unique and monotonically ordered  
**And** No kernel deadlock or race condition occurs  
**And** Average response time remains <500µs per request  

#### Scenario 3: Query PHC when no adapter bound (error handling)
**Given** NDIS filter loaded but no Intel AVB adapter attached  
**When** User-mode application sends `IOCTL_AVB_PHC_QUERY`  
**Then** IOCTL returns `STATUS_DEVICE_NOT_READY` (0xC00000A3)  
**And** Output buffer contains error code `AVB_ERROR_NO_DEVICE`  
**And** Error logged to ETW with message "No AVB-capable adapter bound"  
**And** No system crash or hang occurs  

#### Scenario 4: Query PHC when hardware read fails (error handling)
**Given** Adapter bound but PHC register read returns invalid data (e.g., all 0xFFFFFFFF)  
**When** `IOCTL_AVB_PHC_QUERY` sent  
**Then** IOCTL returns `STATUS_IO_DEVICE_ERROR` (0xC0000185)  
**And** Output buffer contains error code `AVB_ERROR_HARDWARE_FAULT`  
**And** Error logged with register address and failure context  
**And** Driver attempts recovery (re-read, reset if needed)  

#### Scenario 5: Query PHC at maximum rate (stress test)
**Given** Single-threaded application  
**When** Application queries PHC at 10,000 Hz (100µs interval) for 60 seconds  
**Then** All 600,000 requests succeed  
**And** p95 latency remains <500µs  
**And** p99 latency <1ms  
**And** Max latency <5ms (excluding OS scheduler delays)  
**And** No memory leaks (verified via Driver Verifier)  

### Verification Method

**Primary Method**: Test (automated unit + integration + performance tests)

**Test Levels**:
- **Unit Test**: `test_ioctl_phc_query_handler()` validates IOCTL dispatch logic, error handling paths
- **Integration Test**: User-mode test app → IOCTL → Kernel driver → intel_avb → i225 hardware
- **Performance Test**: Custom harness with `QueryPerformanceCounter` for microsecond-precision latency measurement
- **Stress Test**: Multi-threaded concurrent requests, sustained high-frequency polling

**Test Plan Reference**: System Qualification Test Plan, Section 4.1 "PHC IOCTL Tests"

**Success Criteria**:
- ✅ All 5 scenarios pass (100% success rate)
- ✅ Latency p95 <500µs, p99 <1ms under 100 req/sec load
- ✅ Zero race conditions in 10,000 concurrent requests
- ✅ Zero crashes or hangs in 1 million sequential requests
- ✅ Test coverage ≥90% for PHC IOCTL code path

---

## 2️⃣ REQ-F-IOCTL-TS-001: TX Timestamp Retrieval

**Requirement**: System shall provide IOCTL to retrieve hardware TX timestamp for a transmitted packet.

**Traces to**: #28 (StR-GPTP-001: gPTP Synchronization)

### Acceptance Criteria

#### Scenario 1: Retrieve TX timestamp for PTP Sync message
**Given** Application registered for TX timestamp notifications (subscription active)  
**And** PTP Sync packet transmitted with TX timestamp request flag set  
**And** Hardware captured TX timestamp at MAC egress  
**When** Application sends `IOCTL_AVB_TX_TIMESTAMP_GET` with packet sequence ID  
**Then** IOCTL returns `STATUS_SUCCESS`  
**And** Output buffer contains 64-bit TAI timestamp (nanoseconds)  
**And** Timestamp accuracy within ±8ns of actual transmission time (hardware spec)  
**And** Response time <1µs (timestamp retrieved from ring buffer)  

#### Scenario 2: Retrieve TX timestamp for non-PTP packet (no timestamp available)
**Given** Regular Ethernet packet transmitted without timestamp request  
**When** Application attempts `IOCTL_AVB_TX_TIMESTAMP_GET` for that packet  
**Then** IOCTL returns `STATUS_NOT_FOUND` (0xC0000225)  
**And** Output buffer contains error code `AVB_ERROR_NO_TIMESTAMP`  
**And** Error message: "TX timestamp not captured for packet [seq]"  

#### Scenario 3: Retrieve TX timestamp from ring buffer with multiple timestamps
**Given** 16 TX timestamps queued in circular ring buffer (i210 supports up to 16)  
**When** Application retrieves timestamps in FIFO order  
**Then** All 16 timestamps retrieved successfully  
**And** Timestamps are in chronological order (monotonic)  
**And** No timestamp loss or corruption  

#### Scenario 4: Ring buffer overflow (timestamp loss scenario)
**Given** 16 TX timestamps already in ring buffer (full)  
**And** Additional TX timestamp arrives from hardware  
**When** Application queries oldest timestamp  
**Then** IOCTL returns `STATUS_SUCCESS` but includes warning flag `AVB_WARNING_OVERFLOW`  
**And** ETW event logged: "TX timestamp ring buffer overflow, oldest timestamp discarded"  
**And** Application can detect potential timestamp loss  

#### Scenario 5: Concurrent TX timestamp retrieval (multi-stream scenario)
**Given** 4 concurrent PTP streams transmitting (e.g., 4 AVTP streams)  
**When** Each stream retrieves its own TX timestamps independently  
**Then** No timestamp cross-contamination between streams  
**And** Each stream retrieves correct timestamps for its packets  
**And** Latency <1µs per retrieval under load  

### Verification Method

**Primary Method**: Test

**Test Levels**:
- **Unit Test**: Ring buffer management, timestamp matching logic
- **Integration Test**: End-to-end PTP packet TX with timestamp retrieval
- **Timing Test**: Verify timestamp accuracy against known transmission time (loopback test)
- **Stress Test**: High-rate TX (1000 packets/sec) with concurrent timestamp retrieval

**Test Plan Reference**: System Qualification Test Plan, Section 4.2 "TX Timestamp Tests"

**Success Criteria**:
- ✅ Timestamp accuracy within ±8ns (hardware limitation)
- ✅ Zero timestamp loss under normal load (<16 pending timestamps)
- ✅ Overflow detected and logged when >16 timestamps pending
- ✅ Latency <1µs (p95) for retrieval from ring buffer
- ✅ Test coverage ≥85% for TX timestamp code path

---

## 3️⃣ REQ-F-IOCTL-TS-002: RX Timestamp Retrieval

**Requirement**: System shall provide IOCTL to retrieve hardware RX timestamp for a received packet.

**Traces to**: #28 (StR-GPTP-001: gPTP Synchronization)

### Acceptance Criteria

#### Scenario 1: Retrieve RX timestamp for PTP Sync message
**Given** PTP Sync packet received with RX timestamp captured by hardware  
**And** Packet delivered to user-mode application via normal receive path  
**When** Application sends `IOCTL_AVB_RX_TIMESTAMP_GET` with packet identifier  
**Then** IOCTL returns `STATUS_SUCCESS`  
**And** Output buffer contains 64-bit TAI RX timestamp (nanoseconds)  
**And** Timestamp represents MAC ingress time (not kernel receive time)  
**And** Timestamp accuracy within ±8ns of actual reception time  
**And** Response time <1µs  

#### Scenario 2: Retrieve RX timestamp immediately after packet receive
**Given** PTP packet just received (<100µs ago)  
**When** Application queries RX timestamp via IOCTL  
**Then** Timestamp retrieved successfully from recent timestamp cache  
**And** Latency <500ns (cache hit)  

#### Scenario 3: Retrieve RX timestamp for old packet (cache miss)
**Given** PTP packet received >1 second ago  
**And** Timestamp no longer in cache (evicted)  
**When** Application queries RX timestamp  
**Then** IOCTL returns `STATUS_NOT_FOUND`  
**And** Error message: "RX timestamp expired (cache TTL exceeded)"  

#### Scenario 4: Retrieve RX timestamp for non-timestamped packet
**Given** Regular Ethernet packet received without RX timestamp request  
**When** Application queries RX timestamp  
**Then** IOCTL returns `STATUS_NOT_FOUND`  
**And** Error code `AVB_ERROR_NO_TIMESTAMP`  

#### Scenario 5: High-rate RX timestamp retrieval (stress test)
**Given** 1000 PTP packets/second received  
**When** Application retrieves RX timestamp for each packet  
**Then** All timestamps retrieved successfully  
**And** Average latency <1µs  
**And** No timestamp loss due to cache overflow  

### Verification Method

**Primary Method**: Test

**Test Levels**:
- **Unit Test**: RX timestamp cache management, timestamp lookup logic
- **Integration Test**: Loopback test (TX packet → RX timestamp retrieval)
- **Timing Test**: Compare RX timestamp to known transmission time (latency measurement)
- **Stress Test**: High packet rate (1000 pkt/s) with concurrent timestamp queries

**Test Plan Reference**: System Qualification Test Plan, Section 4.3 "RX Timestamp Tests"

**Success Criteria**:
- ✅ Timestamp accuracy within ±8ns
- ✅ Cache hit latency <500ns
- ✅ Cache miss returns error (no crash)
- ✅ Zero timestamp loss at 1000 pkt/s rate
- ✅ Test coverage ≥85%

---

## 4️⃣ REQ-F-IOCTL-PHC-003: PHC Offset Adjustment

**Requirement**: System shall provide IOCTL to adjust PHC by a time offset (nanoseconds) for clock synchronization.

**Traces to**: #28 (StR-GPTP-001: gPTP Synchronization)

### Acceptance Criteria

#### Scenario 1: Adjust PHC forward by small offset (<1ms)
**Given** PHC currently at 1000000000 ns  
**And** gPTP calculates offset of +500 ns (slave clock behind)  
**When** Application sends `IOCTL_AVB_PHC_ADJUST_OFFSET` with offset=+500 ns  
**Then** IOCTL returns `STATUS_SUCCESS`  
**And** PHC advances to 1000000500 ns (current time + offset)  
**And** Adjustment completes in <10µs  
**And** PHC remains monotonic (no backward jump)  

#### Scenario 2: Adjust PHC backward (negative offset)
**Given** PHC at 1000000000 ns  
**And** Offset = -200 ns (slave clock ahead)  
**When** `IOCTL_AVB_PHC_ADJUST_OFFSET` sent with offset=-200 ns  
**Then** IOCTL returns `STATUS_SUCCESS`  
**And** PHC adjusted to 999999800 ns  
**And** Adjustment method: Either step adjustment or gradual frequency adjustment (implementation choice)  
**And** Monotonicity preserved (if gradual) or documented step backward (if step)  

#### Scenario 3: Large offset adjustment (>1 second - step mode)
**Given** PHC significantly out of sync (offset = +5 seconds)  
**When** `IOCTL_AVB_PHC_ADJUST_OFFSET` sent with offset=+5,000,000,000 ns  
**Then** IOCTL returns `STATUS_SUCCESS`  
**And** PHC steps forward by 5 seconds immediately  
**And** ETW event logged: "PHC stepped by +5.000s"  

#### Scenario 4: Concurrent offset adjustments (serialize requests)
**Given** Two gPTP processes attempting simultaneous adjustments  
**When** Both send `IOCTL_AVB_PHC_ADJUST_OFFSET` concurrently  
**Then** Requests serialized (lock acquired)  
**And** Both complete successfully without race condition  
**And** Final PHC value = initial + offset1 + offset2 (cumulative)  

#### Scenario 5: Offset adjustment range validation
**Given** Application sends extreme offset (e.g., ±1 year)  
**When** `IOCTL_AVB_PHC_ADJUST_OFFSET` sent with offset > ±10^15 ns  
**Then** IOCTL returns `STATUS_INVALID_PARAMETER`  
**And** Error message: "Offset out of range (max ±10^15 ns)"  
**And** PHC not modified  

### Verification Method

**Primary Method**: Test + Analysis

**Test Levels**:
- **Unit Test**: Offset calculation, range validation, locking
- **Integration Test**: Verify actual PHC hardware register writes
- **Timing Test**: Measure adjustment latency, verify monotonicity
- **Concurrency Test**: Multi-threaded offset adjustments

**Analysis**:
- Code review: Verify atomic operations, lock correctness
- Static analysis: Check for race conditions in adjustment logic

**Test Plan Reference**: System Qualification Test Plan, Section 4.4 "PHC Adjustment Tests"

**Success Criteria**:
- ✅ Small offsets (<1ms) adjust within ±10ns accuracy
- ✅ Large offsets (>1s) step correctly
- ✅ Monotonicity preserved (or step documented)
- ✅ Adjustment latency <10µs
- ✅ Zero race conditions in concurrent adjustments
- ✅ Test coverage ≥90%

---

## 5️⃣ REQ-F-NDIS-ATTACH-001: FilterAttach/FilterDetach

**Requirement**: System shall implement NDIS FilterAttach and FilterDetach callbacks to bind/unbind to Intel AVB-capable network adapters.

**Traces to**: #31 (StR-NDIS-FILTER-001: NDIS Filter Driver)

### Acceptance Criteria

#### Scenario 1: Attach to Intel i225 adapter successfully
**Given** Intel i225 Ethernet adapter present in system  
**And** NDIS filter driver loaded  
**When** NDIS runtime calls `FilterAttach` callback  
**Then** Filter binds to adapter with `NDIS_STATUS_SUCCESS`  
**And** AVB context allocated and initialized for adapter  
**And** Hardware capabilities detected (PHC, TAS, CBS)  
**And** PHC initialized and running  
**And** Filter marked as operational (ready for IOCTLs)  
**And** ETW event logged: "AVB Filter attached to i225 [MAC address]"  

#### Scenario 2: Attach fails gracefully (non-AVB adapter)
**Given** Non-Intel adapter (e.g., Realtek NIC)  
**When** NDIS calls `FilterAttach`  
**Then** Filter returns `NDIS_STATUS_NOT_SUPPORTED`  
**And** No resources allocated  
**And** ETW event logged: "AVB Filter skipped non-Intel adapter [device ID]"  
**And** No system impact (filter remains loaded for other adapters)  

#### Scenario 3: Detach from adapter cleanly
**Given** Filter attached to i225 adapter  
**And** Active IOCTLs and pending operations exist  
**When** NDIS calls `FilterDetach` (e.g., adapter disabled, driver unload)  
**Then** All pending IOCTLs completed or cancelled gracefully  
**And** PHC stopped  
**And** AVB context destroyed (memory freed)  
**And** Filter detaches with `NDIS_STATUS_SUCCESS`  
**And** No memory leaks (verified via Driver Verifier)  
**And** ETW event logged: "AVB Filter detached from i225 [MAC address]"  

#### Scenario 4: Attach/Detach stress test (1000 cycles)
**Given** Intel i225 adapter  
**When** Adapter enabled/disabled 1000 times (triggering Attach/Detach)  
**Then** All 1000 Attach operations succeed  
**And** All 1000 Detach operations complete cleanly  
**And** No memory leaks after 1000 cycles  
**And** No system crash or BSOD  
**And** Performance remains consistent (no degradation)  

#### Scenario 5: Concurrent Attach to multiple adapters
**Given** 4 Intel i225 adapters in system  
**When** NDIS attaches filter to all 4 adapters simultaneously  
**Then** All 4 adapters bind successfully  
**And** Each adapter has independent AVB context  
**And** No resource contention or race conditions  
**And** Each adapter can be queried independently via IOCTL  

### Verification Method

**Primary Method**: Test + Demonstration

**Test Levels**:
- **Unit Test**: Attach/Detach callback logic, resource allocation/cleanup
- **Integration Test**: Full NDIS filter lifecycle (load → attach → detach → unload)
- **Stress Test**: 1000 attach/detach cycles with Driver Verifier enabled
- **Multi-Adapter Test**: 4 concurrent adapters

**Demonstration**:
- Manual test: Enable/disable adapter in Device Manager
- Visual verification: ETW traces show correct attach/detach sequences

**Test Plan Reference**: System Qualification Test Plan, Section 5.1 "NDIS Lifecycle Tests"

**Success Criteria**:
- ✅ 100% success rate for attach to Intel adapters
- ✅ Graceful skip of non-Intel adapters
- ✅ Zero memory leaks (Driver Verifier clean)
- ✅ Zero crashes in 1000 attach/detach cycles
- ✅ Multi-adapter support verified (4+ adapters)
- ✅ Test coverage ≥95% for attach/detach code paths

---

## 6️⃣ REQ-F-NDIS-SEND-001: FilterSend (TX Path)

**Requirement**: System shall implement FilterSend callback to intercept transmitted packets for TX timestamping (PTP packets).

**Traces to**: #31 (StR-NDIS-FILTER-001: NDIS Filter Driver)

### Acceptance Criteria

#### Scenario 1: Pass-through normal packets (no timestamp request)
**Given** Regular Ethernet packet sent from TCP/IP stack  
**And** Packet does NOT require TX timestamp  
**When** NDIS calls `FilterSend` callback  
**Then** Filter forwards packet immediately to lower driver (NIC)  
**And** Packet transmitted without modification  
**And** Overhead latency <100ns (passthrough)  
**And** No memory copy (zero-copy forwarding)  

#### Scenario 2: Tag PTP packet for TX timestamping
**Given** PTP Sync packet sent from gPTP application  
**And** Packet marked for TX timestamp (via socket option or descriptor flag)  
**When** NDIS calls `FilterSend`  
**Then** Filter sets hardware TX timestamp request flag  
**And** Packet forwarded to NIC with timestamp request enabled  
**And** Filter registers packet sequence ID for later timestamp retrieval  
**And** Overhead latency <500ns  

#### Scenario 3: Handle TX completion with timestamp
**Given** PTP packet transmitted with timestamp request  
**When** NIC generates TX completion interrupt with timestamp  
**Then** Filter captures timestamp from hardware  
**And** Timestamp stored in TX ring buffer (indexed by sequence ID)  
**And** Timestamp available for IOCTL retrieval  
**And** Completion processing <1µs  

#### Scenario 4: High-rate packet forwarding (stress test)
**Given** 10 Gbps Ethernet line rate (approx 14.88 Mpps for 64-byte packets)  
**When** Filter processes 1 million packets/second (realistic load)  
**Then** All packets forwarded successfully  
**And** Packet loss rate <0.01%  
**And** Average latency <1µs per packet  
**And** CPU usage <20% (efficient processing)  

#### Scenario 5: Concurrent TX from multiple applications
**Given** 4 applications transmitting simultaneously (e.g., 2 PTP streams + 2 TCP flows)  
**When** All applications send packets concurrently  
**Then** All packets processed correctly (no cross-contamination)  
**And** PTP packets timestamped independently  
**And** No packet reordering within flows  
**And** No deadlock or race condition  

### Verification Method

**Primary Method**: Test

**Test Levels**:
- **Unit Test**: FilterSend logic, packet tagging, passthrough path
- **Integration Test**: End-to-end TX with timestamp capture
- **Performance Test**: High packet rate (1M pps), measure latency and CPU usage
- **Stress Test**: Sustained 10 Gbps line rate for 1 hour

**Test Plan Reference**: System Qualification Test Plan, Section 5.2 "NDIS TX Path Tests"

**Success Criteria**:
- ✅ Passthrough latency <100ns
- ✅ PTP timestamping latency <500ns
- ✅ Packet loss <0.01% at 1M pps
- ✅ Zero memory leaks in stress test
- ✅ Test coverage ≥85%

---

## 7️⃣ REQ-F-NDIS-RECEIVE-001: FilterReceive (RX Path)

**Requirement**: System shall implement FilterReceive callback to intercept received packets for RX timestamping (PTP packets).

**Traces to**: #31 (StR-NDIS-FILTER-001: NDIS Filter Driver)

### Acceptance Criteria

#### Scenario 1: Pass-through normal packets (no timestamp)
**Given** Regular Ethernet packet received from NIC  
**And** Packet is NOT PTP (no timestamp needed)  
**When** NDIS calls `FilterReceive` callback  
**Then** Filter forwards packet immediately to upper driver (TCP/IP stack)  
**And** Packet delivered without modification  
**And** Overhead latency <100ns  
**And** No memory copy (zero-copy forwarding)  

#### Scenario 2: Capture RX timestamp for PTP packet
**Given** PTP Sync packet received with hardware RX timestamp  
**And** NIC captured timestamp at MAC ingress  
**When** NDIS calls `FilterReceive`  
**Then** Filter extracts RX timestamp from hardware descriptor  
**And** Timestamp stored in RX timestamp cache (indexed by packet identifier)  
**And** Packet forwarded to application (with metadata if needed)  
**And** Timestamp available for IOCTL retrieval  
**And** Processing latency <500ns  

#### Scenario 3: High-rate packet reception (stress test)
**Given** 10 Gbps Ethernet line rate  
**When** Filter receives 1 million packets/second  
**Then** All packets processed successfully  
**And** Packet loss <0.01%  
**And** Average latency <1µs per packet  
**And** CPU usage <20%  

#### Scenario 4: Concurrent RX from multiple streams
**Given** 4 concurrent AVTP streams receiving  
**When** All streams receive packets simultaneously  
**Then** All packets processed correctly  
**And** RX timestamps captured independently per stream  
**And** No cross-stream timestamp contamination  

#### Scenario 5: RX timestamp cache overflow handling
**Given** RX timestamp cache limited to 1000 entries  
**And** >1000 PTP packets received in short burst  
**When** Cache overflows  
**Then** Oldest timestamps evicted (FIFO)  
**And** ETW event logged: "RX timestamp cache overflow"  
**And** No crash or memory corruption  

### Verification Method

**Primary Method**: Test

**Test Levels**:
- **Unit Test**: FilterReceive logic, timestamp extraction, cache management
- **Integration Test**: End-to-end RX with timestamp retrieval
- **Performance Test**: High packet rate (1M pps), measure latency
- **Stress Test**: Sustained line rate for 1 hour

**Test Plan Reference**: System Qualification Test Plan, Section 5.3 "NDIS RX Path Tests"

**Success Criteria**:
- ✅ Passthrough latency <100ns
- ✅ RX timestamping latency <500ns
- ✅ Packet loss <0.01% at 1M pps
- ✅ Cache overflow handled gracefully
- ✅ Test coverage ≥85%

---

## 8️⃣ REQ-F-HW-DETECT-001: Hardware Capability Detection

**Requirement**: System shall detect and report hardware capabilities (PHC, TAS, CBS, FP) at FilterAttach time.

**Traces to**: #31 (StR-NDIS-FILTER-001: NDIS Filter Driver)

### Acceptance Criteria

#### Scenario 1: Detect i225 capabilities (full AVB/TSN support)
**Given** Intel i225 adapter (full TSN support)  
**When** FilterAttach reads hardware registers  
**Then** Detected capabilities:
- PHC: ✅ Present (TSYNCRXCTL/TSYNCTXCTL registers accessible)
- TAS (802.1Qbv): ✅ Present (BASET/BASETIME registers accessible)
- CBS (802.1Qav): ✅ Present (QAV registers accessible)
- Frame Preemption (802.1Qbu): ✅ Present  
**And** Capability bitmask stored in adapter context  
**And** IOCTL_AVB_GET_CAPABILITIES returns full capability set  

#### Scenario 2: Detect i210 capabilities (partial AVB support)
**Given** Intel i210 adapter (limited TSN - no TAS)  
**When** FilterAttach probes hardware  
**Then** Detected capabilities:
- PHC: ✅ Present
- TAS: ❌ Not supported
- CBS: ✅ Present
- Frame Preemption: ❌ Not supported  
**And** Capability bitmask reflects partial support  
**And** IOCTL returns accurate capability report  

#### Scenario 3: Detect non-AVB adapter (minimal support)
**Given** Intel i219 adapter (basic Ethernet, no AVB)  
**When** FilterAttach probes hardware  
**Then** Detected capabilities:
- PHC: ❌ Not supported
- TAS/CBS/FP: ❌ Not supported  
**And** FilterAttach returns `NDIS_STATUS_NOT_SUPPORTED`  
**And** Filter does not bind to adapter  

#### Scenario 4: Capability query via IOCTL
**Given** Filter attached to i225 adapter  
**When** Application sends `IOCTL_AVB_GET_CAPABILITIES`  
**Then** IOCTL returns capability structure:
```c
struct AVB_CAPABILITIES {
    BOOLEAN phc_present;
    BOOLEAN tas_present;
    BOOLEAN cbs_present;
    BOOLEAN fp_present;
    UINT32 phc_resolution_ns;  // e.g., 1 ns
    UINT16 num_tc_queues;       // e.g., 8
};
```
**And** All fields accurate per hardware  

#### Scenario 5: Capability detection error handling
**Given** Hardware register read fails (e.g., device not responding)  
**When** FilterAttach attempts capability detection  
**Then** FilterAttach returns `NDIS_STATUS_DEVICE_FAILED`  
**And** Error logged: "Hardware capability detection failed"  
**And** No crash or hang  

### Verification Method

**Primary Method**: Test + Inspection

**Test Levels**:
- **Unit Test**: Register read/parse logic, capability bitmask encoding
- **Integration Test**: Test on real i225, i210, i219 hardware
- **Manual Inspection**: Visual verification of detected capabilities vs. datasheet

**Test Plan Reference**: System Qualification Test Plan, Section 6.1 "Hardware Detection Tests"

**Success Criteria**:
- ✅ 100% accurate detection on i225, i210, i219
- ✅ Graceful handling of unsupported adapters
- ✅ IOCTL returns correct capability report
- ✅ Error handling for hardware failures
- ✅ Test coverage ≥90%

---

## 9️⃣ REQ-F-INTEL-AVB-003: Register Access Abstraction

**Requirement**: All hardware register access shall be abstracted through intel_avb library functions (no direct register writes in filter driver).

**Traces to**: #29 (StR-INTEL-AVB-LIB: Library Integration)

### Acceptance Criteria

#### Scenario 1: PHC time query via intel_avb abstraction
**Given** Filter needs to read PHC current time  
**When** Filter calls `intel_avb_phc_gettime()`  
**Then** Function internally reads SYSTIML/SYSTIMH registers  
**And** Returns 64-bit TAI timestamp  
**And** Filter code contains NO direct register addresses (e.g., 0x15C18)  
**And** All register access isolated in intel_avb library  

#### Scenario 2: Code audit - zero direct register writes
**Given** Filter driver source code  
**When** Static analysis scans for register addresses (0x1XXXX patterns)  
**Then** Zero direct register writes found in filter driver code  
**And** All hardware access via intel_avb API calls:
- `intel_avb_phc_*()` for PTP clock
- `intel_avb_tas_*()` for TAS
- `intel_avb_cbs_*()` for CBS
- `intel_avb_timestamp_*()` for TX/RX timestamps  
**And** Single Source of Truth: Register definitions in intel_avb only  

#### Scenario 3: Multi-controller support via abstraction
**Given** Intel i210, i225, i226 adapters (different register layouts)  
**When** Filter uses intel_avb abstraction layer  
**Then** Same filter code works on all 3 controllers  
**And** intel_avb handles controller-specific differences internally  
**And** No `#ifdef I210` or `#ifdef I225` in filter code  

#### Scenario 4: Error propagation from intel_avb
**Given** intel_avb register read fails (hardware fault)  
**When** Filter calls `intel_avb_phc_gettime()`  
**Then** Function returns error code (e.g., `AVB_ERR_HARDWARE_FAULT`)  
**And** Filter propagates error to IOCTL caller  
**And** Error logged with context from intel_avb  

#### Scenario 5: Compile-time enforcement (linker check)
**Given** Filter driver built with strict settings  
**When** Filter code attempts direct register access (e.g., `*(UINT32*)0x15C18`)  
**Then** Compilation fails or static analyzer flags violation  
**And** Build enforces "intel_avb abstraction only" policy  

### Verification Method

**Primary Method**: Analysis + Inspection

**Verification Strategy**:
- **Static Analysis**: Scan filter code for register address patterns (regex: `0x[0-9A-F]{4,5}`)
- **Code Review**: Manual inspection of filter driver to verify NO direct hardware access
- **Linker Check**: Ensure filter driver does not link against raw hardware definitions
- **Functional Test**: Verify filter works on i210, i225, i226 (abstraction portability)

**Test Plan Reference**: System Acceptance Test Plan, Section 7.1 "Abstraction Layer Compliance"

**Success Criteria**:
- ✅ Zero direct register accesses in filter driver code (static analysis clean)
- ✅ 100% hardware access via intel_avb API
- ✅ Single Source of Truth: Register definitions only in intel_avb
- ✅ Filter works on multiple controller types (portability verified)
- ✅ Code review approval: Abstraction layer correctly used

---

## 🔟 REQ-F-REG-ACCESS-001: Safe Register Access Coordination

**Requirement**: Register access shall be coordinated to prevent conflicts between NDIS filter and intel_avb library.

**Traces to**: #31 (StR-NDIS-FILTER-001: NDIS Filter Driver)

### Acceptance Criteria

#### Scenario 1: Serialize concurrent PHC reads (spinlock protection)
**Given** 10 threads querying PHC simultaneously  
**When** All call `intel_avb_phc_gettime()` concurrently  
**Then** Register reads serialized via spinlock  
**And** All reads complete successfully with valid timestamps  
**And** No torn reads (partial 64-bit value corruption)  
**And** Average latency <500µs per read (including lock contention)  

#### Scenario 2: Atomic 64-bit PHC read (SYSTIML + SYSTIMH)
**Given** PHC running (incrementing every nanosecond)  
**When** Filter reads SYSTIML (lower 32 bits) then SYSTIMH (upper 32 bits)  
**Then** Read sequence is atomic (no rollover during read)  
**And** Value consistent (e.g., not 0x00000000FFFFFFFF → 0x00000001FFFFFFFF)  
**And** Locking ensures no interrupt between SYSTIML/SYSTIMH reads  

#### Scenario 3: Prevent register access during adapter detach
**Given** FilterDetach in progress (adapter being removed)  
**When** IOCTL attempts PHC query during detach  
**Then** IOCTL blocked or fails gracefully with `STATUS_DEVICE_NOT_READY`  
**And** No access to freed adapter context  
**And** No system crash due to race condition  

#### Scenario 4: Coordinate PHC write operations (offset adjustment)
**Given** gPTP adjusting PHC offset  
**When** Concurrent PHC read occurs during adjustment  
**Then** Read either sees pre-adjustment or post-adjustment value (atomic)  
**And** No partial adjustment visible (torn write)  
**And** Spinlock serializes read/write operations  

#### Scenario 5: Deadlock prevention (lock ordering)
**Given** Multiple locks in system (adapter lock, PHC lock, timestamp ring buffer lock)  
**When** Complex operation requires multiple locks  
**Then** Locks acquired in consistent order (e.g., adapter → PHC → ring buffer)  
**And** No deadlock scenario exists (verified via lock hierarchy analysis)  
**And** Timeout mechanism if lock held >1ms (detect potential deadlock)  

### Verification Method

**Primary Method**: Test + Analysis

**Test Levels**:
- **Unit Test**: Spinlock logic, atomic operations, lock ordering
- **Concurrency Test**: 100 threads hammering PHC reads/writes
- **Stress Test**: Attach/detach during heavy IOCTL load
- **Static Analysis**: Deadlock detection (SAL annotations, static analyzer)

**Analysis**:
- Code review: Verify lock hierarchy documented and enforced
- Thread Safety Analysis (MSVC /analyze): Detect missing locks

**Test Plan Reference**: System Qualification Test Plan, Section 6.2 "Register Access Safety Tests"

**Success Criteria**:
- ✅ Zero torn reads in 1 million concurrent PHC queries
- ✅ Zero deadlocks in stress test (1000 attach/detach cycles)
- ✅ All register accesses protected by spinlock
- ✅ Static analysis clean (no lock ordering violations)
- ✅ Test coverage ≥95% for synchronization code

---

## 1️⃣1️⃣ REQ-F-PTP-EPOCH-001: PTP Epoch (TAI) Support

**Requirement**: PHC timestamps shall use PTP epoch (TAI - International Atomic Time, 1 January 1970 00:00:00 TAI).

**Traces to**: #30 (StR-STANDARDS-COMPLIANCE: IEEE 802.1 TSN Standards)

### Acceptance Criteria

#### Scenario 1: PHC reports TAI epoch (not Unix UTC)
**Given** System time is 2025-12-12 12:00:00 UTC  
**And** Current TAI offset is +37 seconds (UTC + 37 leap seconds = TAI)  
**When** Application queries PHC via `IOCTL_AVB_PHC_QUERY`  
**Then** PHC timestamp represents TAI (not UTC)  
**And** PHC value = Unix time + 37 seconds  
**And** No leap second smearing (TAI is continuous, no jumps)  

#### Scenario 2: PHC survives leap second insertion
**Given** UTC leap second insertion at 23:59:60  
**And** System time jumps from 23:59:59 → 23:59:60 → 00:00:00  
**When** PHC queried before, during, and after leap second  
**Then** PHC continues monotonically (no jump)  
**And** TAI does not observe leap second (TAI is continuous)  
**And** PHC increments normally: ...999 → 000 (no 60th second)  

#### Scenario 3: Interoperability with gPTP (IEEE 1588)
**Given** gPTP master clock using TAI epoch  
**And** gPTP Sync message contains TAI timestamp  
**When** Local PHC synchronized with master  
**Then** Local PHC offset from master <100ns  
**And** Both clocks use same TAI epoch (no epoch mismatch)  

#### Scenario 4: PHC initialization to current TAI
**Given** Driver loaded at 2025-12-12 12:00:00 UTC  
**And** Current TAI = 1765612837 seconds (Unix + 37)  
**When** PHC initialized  
**Then** PHC set to approximately 1765612837000000000 ns (TAI nanoseconds)  
**And** PHC synchronized within ±10ms of system TAI  

#### Scenario 5: TAI epoch documentation
**Given** API documentation for `IOCTL_AVB_PHC_QUERY`  
**When** Developer reads documentation  
**Then** Documentation explicitly states:
- "Timestamp is in TAI (International Atomic Time)"
- "Epoch: 1 January 1970 00:00:00 TAI"
- "Does NOT include leap seconds (continuous time)"  
**And** Example code shows TAI → UTC conversion  

### Verification Method

**Primary Method**: Test + Inspection

**Test Levels**:
- **Unit Test**: TAI epoch initialization, UTC-to-TAI conversion
- **Integration Test**: gPTP interoperability test with TAI-based master clock
- **Simulation Test**: Leap second insertion scenario (system time jump)
- **Manual Inspection**: Verify documentation explicitly states TAI epoch

**Test Plan Reference**: System Acceptance Test Plan, Section 8.1 "PTP Epoch Compliance"

**Success Criteria**:
- ✅ PHC uses TAI epoch (verified via timestamp comparison)
- ✅ PHC monotonic across leap seconds (simulation test)
- ✅ Interoperability with IEEE 1588 gPTP (TAI-based)
- ✅ Documentation clearly states TAI epoch
- ✅ No UTC/TAI confusion in code or docs

---

## 1️⃣2️⃣ REQ-NF-PERF-NDIS-001: Packet Forwarding Latency (<1µs)

**Requirement**: NDIS filter packet forwarding overhead shall be <1µs (p95) for non-timestamped packets.

**Traces to**: #31 (StR-NDIS-FILTER-001: NDIS Filter Driver)

### Acceptance Criteria

### Measurable Metrics

| Metric | Target | Measurement Method | Acceptance Threshold |
|--------|--------|-------------------|---------------------|
| p50 latency | <300ns | High-resolution timestamp (RDTSC) | ≤300ns |
| p95 latency | <1µs | RDTSC at FilterSend entry/exit | ≤1µs |
| p99 latency | <5µs | RDTSC | ≤5µs |
| Max latency | <10µs | RDTSC (excluding OS delays) | ≤10µs |

### Specific Scenarios

**Scenario 1: Normal Load (1000 packets/second)**
- **Given**: Single TCP flow at 1000 pps
- **When**: Packets forwarded through filter (passthrough path)
- **Then**: p95 latency ≤1µs, p50 ≤300ns

**Scenario 2: High Load (100,000 packets/second)**
- **Given**: High packet rate (10 Gbps line rate)
- **When**: Non-PTP packets forwarded
- **Then**: p95 latency ≤1µs (no degradation under load)

**Scenario 3: Mixed Traffic (PTP + TCP)**
- **Given**: 10% PTP packets (timestamped) + 90% TCP (passthrough)
- **When**: Mixed traffic processed
- **Then**: TCP passthrough latency ≤1µs (PTP processing doesn't slow passthrough)

**Scenario 4: Zero-Copy Verification**
- **Given**: Large packets (1500 byte MTU)
- **When**: Packets forwarded
- **Then**: Zero memory copy (NBL forwarded by reference)
- **And**: Latency independent of packet size (<1µs for 64-byte and 1500-byte packets)

### Verification Method

**Primary Method**: Test

**Test Strategy**:
- Custom performance harness with RDTSC (CPU timestamp counter)
- Measure latency: Entry to FilterSend → Exit from FilterSend
- Statistical analysis: p50, p95, p99, max
- Profiler (ETW): Identify any hot spots

**Success Criteria**:
- ✅ p95 ≤1µs in all scenarios
- ✅ Zero-copy forwarding (no memory allocation per packet)
- ✅ Latency constant across packet sizes
- ✅ No performance regression under high load

**Test Plan Reference**: System Qualification Test Plan, Section 9.1 "NDIS Performance Tests"

---

## 1️⃣3️⃣ REQ-NF-REL-PHC-001: PHC Monotonicity

**Requirement**: PHC shall maintain monotonicity (never go backwards) except during explicit step adjustments.

**Traces to**: #28 (StR-GPTP-001: gPTP Synchronization)

### Acceptance Criteria

[**See detailed acceptance criteria in Template Example 3 above**]

### Measurable Metrics

| Metric | Target | Measurement Method | Acceptance Threshold |
|--------|--------|-------------------|---------------------|
| Monotonicity violations | 0 | Compare successive PHC reads | Zero violations in 10M reads |
| Backward jumps | 0 | Timestamp delta analysis | Zero negative deltas |
| Frequency drift | ≤±100 ppb | Compare PHC vs. system time over 24h | Within spec |

### Verification Method

**Primary Method**: Test + Analysis

**Test Plan Reference**: System Qualification Test Plan, Section 9.2 "PHC Monotonicity Tests"

---

## 1️⃣4️⃣ REQ-NF-REL-NDIS-001: No BSOD (1000 Cycles)

**Requirement**: NDIS filter shall survive 1000 attach/detach cycles without BSOD or memory leak.

**Traces to**: #31 (StR-NDIS-FILTER-001: NDIS Filter Driver)

### Acceptance Criteria

### Measurable Metrics

| Metric | Target | Measurement Method | Acceptance Threshold |
|--------|--------|-------------------|---------------------|
| BSOD count | 0 | System crash detection | Zero crashes in 1000 cycles |
| Memory leaks | 0 bytes | Driver Verifier + Performance Monitor | Zero leaks |
| Handle leaks | 0 | Handle count monitoring | No leaked handles |
| Attach success rate | 100% | Test harness logging | All attaches succeed |

### Specific Scenarios

**Scenario 1: 1000 Attach/Detach Cycles (Driver Verifier Enabled)**
- **Given**: Intel i225 adapter
- **And**: Driver Verifier enabled (Special Pool, Force IRQL Checking, etc.)
- **When**: Adapter enabled/disabled 1000 times (via devcon or Device Manager automation)
- **Then**: Zero BSODs
- **And**: Zero memory leaks (Driver Verifier clean)
- **And**: All 1000 attaches succeed

**Scenario 2: Concurrent Attach/Detach (4 Adapters)**
- **Given**: 4 Intel adapters in system
- **When**: All 4 adapters enabled/disabled concurrently 250 times each
- **Then**: Zero crashes, zero leaks
- **And**: All adapters functional after test

**Scenario 3: Stress Test with Active IOCTLs**
- **Given**: IOCTL workload running (PHC queries at 10 Hz)
- **When**: Adapter detached while IOCTLs active
- **Then**: IOCTLs cancelled gracefully
- **And**: No BSOD due to race condition

### Verification Method

**Primary Method**: Test + Demonstration

**Test Strategy**:
- Automated test script: 1000 attach/detach cycles
- Driver Verifier enabled (all checks)
- Performance Monitor: Track memory and handle counts
- Manual observation: Verify system stability

**Success Criteria**:
- ✅ Zero BSODs in 1000 cycles
- ✅ Zero memory leaks (Driver Verifier clean)
- ✅ Zero handle leaks
- ✅ 100% attach success rate

**Test Plan Reference**: System Qualification Test Plan, Section 9.3 "Reliability Stress Tests"

---

## 1️⃣5️⃣ REQ-NF-PERF-PTP-001: PTP Sync Accuracy (<100ns)

**Requirement**: PTP clock synchronization accuracy shall be <100ns RMS when synchronized with gPTP master.

**Traces to**: #33 (StR-API-ENDPOINTS: IOCTL API Endpoints), #28 (gPTP)

### Acceptance Criteria

### Measurable Metrics

| Metric | Target | Measurement Method | Acceptance Threshold |
|--------|--------|-------------------|---------------------|
| Offset from master (RMS) | <100ns | gPTP offset measurements | ≤100ns RMS |
| Offset from master (max) | <500ns | gPTP offset peak | ≤500ns peak |
| Sync interval | 125ms | gPTP Sync message rate | 8 Sync/sec (IEEE 802.1AS default) |
| Sync convergence time | <10 seconds | Time to achieve <100ns offset | ≤10s after startup |

### Specific Scenarios

**Scenario 1: Sync with gPTP Master (Normal Operation)**
- **Given**: gPTP master clock (e.g., Meinberg grandmaster)
- **And**: Local PHC as gPTP slave
- **When**: gPTP running for >1 minute (steady state)
- **Then**: Offset from master <100ns RMS
- **And**: Peak offset <500ns (99th percentile)

**Scenario 2: Sync Convergence After Startup**
- **Given**: gPTP slave just started (PHC unsynchronized)
- **When**: gPTP begins synchronization
- **Then**: Offset converges to <100ns within 10 seconds
- **And**: Offset remains stable (<100ns RMS) thereafter

**Scenario 3: Sync Stability Under Load**
- **Given**: System under heavy network load (1 Gbps traffic)
- **When**: gPTP synchronization running concurrently
- **Then**: Offset remains <100ns RMS (load does not degrade sync)

**Scenario 4: Multi-Hop Sync (2 Bridges)**
- **Given**: gPTP slave → Bridge 1 → Bridge 2 → Master
- **When**: Synced across 2 intermediate bridges
- **Then**: Offset <200ns RMS (cumulative across hops)

### Verification Method

**Primary Method**: Test

**Test Strategy**:
- gPTP interoperability test with certified grandmaster clock
- Offset measurement: gPTP reports offset every Sync interval (125ms)
- Statistical analysis: RMS, peak, convergence time
- Long-duration test: 24-hour sync stability

**Success Criteria**:
- ✅ Offset <100ns RMS in steady state
- ✅ Convergence <10 seconds after startup
- ✅ Stable sync under network load
- ✅ Interoperability with IEEE 802.1AS-compliant devices

**Test Plan Reference**: System Acceptance Test Plan, Section 10.1 "gPTP Synchronization Accuracy"

---

## ✅ Next Steps: Test Plan Creation

Now that critical requirements have detailed acceptance criteria, the next step is to create the **System Acceptance Test Plan** and **System Qualification Test Plan** that reference these criteria.

**Todo Progress**:
- ✅ Task 1: Analyze existing requirements (complete)
- ✅ Task 2: Create acceptance criteria template (complete)
- ✅ Task 3: Generate acceptance criteria for P0 requirements (complete)
- ⏭️ Task 4: Create System Acceptance Test Plan (next)
- ⏭️ Task 5: Create System Qualification Test Plan (next)
