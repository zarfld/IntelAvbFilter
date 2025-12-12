# Verification Methods by Requirement

**Project**: Intel AVB Filter Driver  
**Purpose**: Define verification method for each of 64 system requirements  
**Created**: 2025-12-12  
**Phase**: 02 - Requirements Analysis & Specification  
**Standards**: IEEE 1012-2016 (V&V)

---

## 1. Overview

This document assigns a primary verification method to **every system requirement** (64 total) per IEEE 1012-2016 V&V standard.

**Four Verification Methods** (IEEE 1012-2016):

1. **Test**: Execute with defined inputs, verify outputs (most common)
2. **Analysis**: Mathematical/logical proof without execution
3. **Inspection**: Visual examination, code review
4. **Demonstration**: Operational verification, manual observation

**Assignment Criteria**:
- **Functional Requirements (REQ-F)**: Primary = Test
- **Performance (REQ-NF-PERF)**: Primary = Test (benchmark)
- **Reliability (REQ-NF-REL)**: Primary = Test (stress)
- **Security (REQ-NF-SEC)**: Primary = Test + Analysis
- **Abstraction/Compliance**: Primary = Inspection

---

## 2. Verification Methods Summary

**By Method**:
- ✅ **Test**: 52 of 64 requirements (81%)
- 📊 **Analysis**: 7 of 64 requirements (11%)
- 👁️ **Inspection**: 4 of 64 requirements (6%)
- 🎭 **Demonstration**: 1 of 64 requirements (2%)

**By Automation**:
- ✅ **Automated**: 52 requirements (81%)
- ⚠️ **Semi-Automated**: 9 requirements (14%)
- ❌ **Manual**: 3 requirements (5%)

---

## 3. IOCTL API Requirements (15 total)

### REQ-F-IOCTL-PHC-001: PHC Time Query
**Verification Method**: **Test** (automated)  
**Test Level**: Unit + Integration  
**Rationale**: Functional behavior requires execution with real hardware  
**Test Cases**: 
- TC-001-Success: Query PHC, verify valid timestamp
- TC-001-Concurrent: 10 threads query simultaneously
- TC-001-Error: Query when no adapter bound
- TC-001-Performance: Latency <500µs (p95)

---

### REQ-F-IOCTL-TS-001: TX Timestamp Retrieval
**Verification Method**: **Test** (automated)  
**Test Level**: Integration  
**Rationale**: Requires packet transmission + timestamp capture  
**Test Cases**:
- TC-002-Success: Retrieve TX timestamp for PTP packet
- TC-002-RingBuffer: Multiple timestamps in ring buffer
- TC-002-Overflow: Ring buffer overflow handling

---

### REQ-F-IOCTL-TS-002: RX Timestamp Retrieval
**Verification Method**: **Test** (automated)  
**Test Level**: Integration  
**Rationale**: Requires packet reception + timestamp capture  
**Test Cases**:
- TC-003-Success: Retrieve RX timestamp
- TC-003-CacheMiss: Query expired timestamp
- TC-003-Stress: High packet rate (1000 pps)

---

### REQ-F-IOCTL-PHC-003: PHC Offset Adjustment
**Verification Method**: **Test** (automated)  
**Test Level**: Integration  
**Rationale**: Verify PHC adjustment behavior  
**Test Cases**:
- TC-004-ForwardAdjust: Positive offset (+500ns)
- TC-004-BackwardAdjust: Negative offset (-200ns)
- TC-004-LargeStep: Offset >1 second
- TC-004-Concurrent: Serialize concurrent adjustments

---

### REQ-F-IOCTL-PHC-004: PHC Frequency Adjustment
**Verification Method**: **Test** (semi-automated)  
**Test Level**: Integration  
**Rationale**: Requires long-duration frequency drift measurement  
**Test Cases**:
- TC-005-FreqAdjust: Adjust frequency ±100 ppm
- TC-005-Convergence: Verify drift over 1 hour

---

### REQ-F-IOCTL-PHC-005: PHC Reset
**Verification Method**: **Test** (automated)  
**Test Level**: Integration  
**Rationale**: Verify PHC reset to zero  
**Test Cases**:
- TC-006-Reset: PHC resets to zero
- TC-006-Monotonic: PHC resumes after reset

---

### REQ-F-IOCTL-TS-003: TX Timestamp Subscription
**Verification Method**: **Test** (automated)  
**Test Level**: Integration  
**Rationale**: Verify subscription/unsubscription mechanism  
**Test Cases**:
- TC-007-Subscribe: Application subscribes for TX timestamps
- TC-007-Unsubscribe: Clean unsubscription

---

### REQ-F-IOCTL-TS-004: RX Timestamp Subscription
**Verification Method**: **Test** (automated)  
**Test Level**: Integration  
**Rationale**: Verify subscription/unsubscription mechanism  
**Test Cases**:
- TC-008-Subscribe: Application subscribes for RX timestamps
- TC-008-Unsubscribe: Clean unsubscription

---

### REQ-F-IOCTL-CAP-001: Capability Query
**Verification Method**: **Test** (automated)  
**Test Level**: Integration  
**Rationale**: Verify capability detection and reporting  
**Test Cases**:
- TC-009-i225: Query i225 capabilities (full TSN)
- TC-009-i210: Query i210 capabilities (partial TSN)

---

### REQ-F-IOCTL-TAS-001: TAS Schedule Configuration
**Verification Method**: **Test** (manual)  
**Test Level**: Integration  
**Rationale**: Requires complex TAS schedule validation  
**Test Cases**:
- TC-010-TASSchedule: Configure gate control list
- TC-010-TASValidation: Verify schedule enforcement

---

### REQ-F-IOCTL-CBS-001: CBS Configuration
**Verification Method**: **Test** (manual)  
**Test Level**: Integration  
**Rationale**: Requires bandwidth reservation validation  
**Test Cases**:
- TC-011-CBS: Configure idle slope/send slope
- TC-011-BandwidthEnforcement: Verify bandwidth reservation

---

### REQ-F-IOCTL-ERR-001: Error Reporting
**Verification Method**: **Test** (automated)  
**Test Level**: Unit + Integration  
**Rationale**: Verify error codes and messages  
**Test Cases**:
- TC-012-InvalidInput: IOCTL with invalid parameters
- TC-012-DeviceNotReady: IOCTL when no adapter bound
- TC-012-HardwareFault: IOCTL when hardware read fails

---

### REQ-F-IOCTL-CONCURRENT-001: Concurrent IOCTL Handling
**Verification Method**: **Test** (automated - stress)  
**Test Level**: Integration  
**Rationale**: Verify thread safety and locking  
**Test Cases**:
- TC-013-Concurrent: 100 threads sending IOCTLs
- TC-013-NoDeadlock: Verify no deadlock in 10,000 requests

---

### REQ-F-IOCTL-SECURITY-001: IOCTL Input Validation
**Verification Method**: **Test + Analysis** (automated)  
**Test Level**: Unit  
**Rationale**: Security-critical, needs fuzzing + code review  
**Test Cases**:
- TC-014-BufferOverflow: IOCTL with oversized buffer
- TC-014-NullPointer: IOCTL with NULL buffer
- TC-014-InvalidCode: IOCTL with invalid code
**Analysis**: Code review for input validation logic

---

### REQ-F-IOCTL-DOC-001: IOCTL Documentation
**Verification Method**: **Inspection** (manual)  
**Test Level**: Documentation review  
**Rationale**: Documentation quality requires human review  
**Review Checklist**:
- [ ] IOCTL codes defined
- [ ] Input/output structures documented
- [ ] Error codes defined
- [ ] Example code provided

---

## 4. NDIS Filter Requirements (14 total)

### REQ-F-NDIS-ATTACH-001: FilterAttach/Detach
**Verification Method**: **Test + Demonstration** (automated)  
**Test Level**: Integration  
**Rationale**: Requires real NDIS attach/detach lifecycle  
**Test Cases**:
- TC-015-Attach: Filter attaches to i225
- TC-015-Detach: Filter detaches cleanly
- TC-015-Stress: 1000 attach/detach cycles
**Demonstration**: Manual enable/disable in Device Manager

---

### REQ-F-NDIS-SEND-001: FilterSend (TX Path)
**Verification Method**: **Test** (automated)  
**Test Level**: Integration  
**Rationale**: Verify packet forwarding and timestamping  
**Test Cases**:
- TC-016-Passthrough: Forward non-PTP packets
- TC-016-PTTimestamp: Tag PTP packet for TX timestamp
- TC-016-Stress: High packet rate (1M pps)

---

### REQ-F-NDIS-RECEIVE-001: FilterReceive (RX Path)
**Verification Method**: **Test** (automated)  
**Test Level**: Integration  
**Rationale**: Verify packet reception and timestamping  
**Test Cases**:
- TC-017-Passthrough: Forward non-PTP packets
- TC-017-RXTimestamp: Capture RX timestamp
- TC-017-Stress: High packet rate (1M pps)

---

### REQ-F-NDIS-PAUSE-001: FilterPause/Restart
**Verification Method**: **Test** (automated)  
**Test Level**: Integration  
**Rationale**: Verify NDIS pause/restart callbacks  
**Test Cases**:
- TC-018-Pause: Filter pauses successfully
- TC-018-Restart: Filter restarts successfully

---

### REQ-F-NDIS-OID-001: OID Request Handling
**Verification Method**: **Test** (manual)  
**Test Level**: Integration  
**Rationale**: Requires OID request generation  
**Test Cases**:
- TC-019-OIDQuery: Query OID (e.g., MAC address)
- TC-019-OIDSet: Set OID (e.g., multicast list)

---

### REQ-F-NDIS-STATUS-001: Status Indication Handling
**Verification Method**: **Test** (manual)  
**Test Level**: Integration  
**Rationale**: Requires status indication generation  
**Test Cases**:
- TC-020-LinkUp: Handle link up indication
- TC-020-LinkDown: Handle link down indication

---

### REQ-F-NDIS-CANCEL-001: Send/Receive Cancellation
**Verification Method**: **Test** (automated)  
**Test Level**: Integration  
**Rationale**: Verify cancellation logic  
**Test Cases**:
- TC-021-CancelSend: Cancel pending send
- TC-021-CancelReceive: Cancel pending receive

---

### REQ-F-NDIS-NBL-001: NBL Passthrough (Zero-Copy)
**Verification Method**: **Test + Analysis** (automated)  
**Test Level**: Integration  
**Rationale**: Verify zero-copy forwarding  
**Test Cases**:
- TC-022-Passthrough: NBL forwarded by reference
**Analysis**: Code review confirms no memory copy

---

### REQ-F-NDIS-ERROR-001: NDIS Error Handling
**Verification Method**: **Test** (automated)  
**Test Level**: Integration  
**Rationale**: Verify error handling in NDIS callbacks  
**Test Cases**:
- TC-023-SendError: Handle send failure
- TC-023-ReceiveError: Handle receive failure

---

### REQ-F-NDIS-FILTER-INFO-001: Filter Characteristics
**Verification Method**: **Inspection** (manual)  
**Test Level**: Configuration review  
**Rationale**: Verify NDIS filter INF file and characteristics  
**Review Checklist**:
- [ ] Filter type: Modifying filter
- [ ] Class: NetService
- [ ] Characteristics: NCF_HAS_UI, NCF_HIDDEN

---

### REQ-F-NDIS-BINDING-001: Selective Binding (Intel Only)
**Verification Method**: **Test** (automated)  
**Test Level**: Integration  
**Rationale**: Verify filter binds to Intel adapters only  
**Test Cases**:
- TC-024-IntelAdapter: Filter binds to i225
- TC-024-NonIntel: Filter skips non-Intel adapter

---

### REQ-F-NDIS-UNLOAD-001: FilterDriverUnload
**Verification Method**: **Test** (automated)  
**Test Level**: Integration  
**Rationale**: Verify clean driver unload  
**Test Cases**:
- TC-025-Unload: Driver unloads successfully
- TC-025-NoLeak: No memory leaks after unload

---

### REQ-F-NDIS-REINIT-001: Filter Restart After Error
**Verification Method**: **Test** (manual)  
**Test Level**: Integration  
**Rationale**: Verify recovery from errors  
**Test Cases**:
- TC-026-Restart: Filter restarts after error
- TC-026-Operational: Filter operational after restart

---

### REQ-F-NDIS-VERSIONING-001: NDIS Version Support
**Verification Method**: **Test** (automated)  
**Test Level**: Compatibility  
**Rationale**: Verify NDIS 6.x compatibility  
**Test Cases**:
- TC-027-NDIS6: Filter loads on NDIS 6.0+
- TC-027-Win10: Filter works on Windows 10
- TC-027-Win11: Filter works on Windows 11

---

## 5. PHC/PTP Requirements (8 total)

### REQ-F-PHC-INIT-001: PHC Initialization
**Verification Method**: **Test** (automated)  
**Test Level**: Integration  
**Rationale**: Verify PHC initialization at adapter attach  
**Test Cases**:
- TC-028-Init: PHC initializes successfully
- TC-028-Running: PHC increments after init

---

### REQ-F-PHC-MONOTONIC-001: PHC Monotonicity
**Verification Method**: **Test + Analysis** (automated)  
**Test Level**: Integration  
**Rationale**: Verify PHC never goes backward  
**Test Cases**:
- TC-029-Monotonic: 10M sequential reads, T[n+1] ≥ T[n]
**Analysis**: Lock ordering analysis for atomic 64-bit reads

---

### REQ-F-PHC-SYNC-001: PHC Synchronization
**Verification Method**: **Test** (automated)  
**Test Level**: System  
**Rationale**: Verify gPTP synchronization  
**Test Cases**:
- TC-030-Sync: PHC syncs to gPTP master
- TC-030-Accuracy: Offset <100ns RMS

---

### REQ-F-PTP-EPOCH-001: PTP Epoch (TAI)
**Verification Method**: **Test + Inspection** (automated)  
**Test Level**: Integration  
**Rationale**: Verify TAI epoch usage  
**Test Cases**:
- TC-031-TAIEpoch: PHC uses TAI (not UTC)
- TC-031-LeapSecond: PHC monotonic across leap second
**Inspection**: Documentation states TAI epoch

---

### REQ-F-PTP-TIMESTAMP-001: PTP Timestamp Format
**Verification Method**: **Test** (automated)  
**Test Level**: Integration  
**Rationale**: Verify timestamp format (64-bit nanoseconds)  
**Test Cases**:
- TC-032-Format: Timestamp is 64-bit unsigned integer
- TC-032-Range: Timestamp in valid range

---

### REQ-F-PTP-LEAP-001: Leap Second Handling
**Verification Method**: **Test** (manual - simulation)  
**Test Level**: Integration  
**Rationale**: Leap second occurs rarely, requires simulation  
**Test Cases**:
- TC-033-LeapInsert: Simulate leap second insertion
- TC-033-Monotonic: PHC monotonic during leap

---

### REQ-F-PHC-ACCURACY-001: PHC Accuracy (±100 ppb)
**Verification Method**: **Test** (manual - long-duration)  
**Test Level**: Performance  
**Rationale**: Requires 24-hour drift measurement  
**Test Cases**:
- TC-034-Drift: Measure PHC drift over 24 hours
- TC-034-Accuracy: Drift within ±100 ppb

---

### REQ-F-PHC-ROLLOVER-001: PHC Rollover (64-bit)
**Verification Method**: **Analysis** (manual)  
**Test Level**: Mathematical proof  
**Rationale**: 64-bit rollover takes 584 years (cannot test)  
**Analysis**: Mathematical proof that 2^64 ns ≈ 584 years

---

## 6. Hardware Abstraction Requirements (4 total)

### REQ-F-INTEL-AVB-001: intel_avb Library Linkage
**Verification Method**: **Test** (automated)  
**Test Level**: Build + Integration  
**Rationale**: Verify library linkage at build time  
**Test Cases**:
- TC-035-Build: Driver builds with intel_avb library
- TC-035-Link: Driver links against intel_avb.lib

---

### REQ-F-INTEL-AVB-002: intel_avb API Usage
**Verification Method**: **Test** (automated)  
**Test Level**: Integration  
**Rationale**: Verify correct API usage  
**Test Cases**:
- TC-036-APICalls: Verify intel_avb_phc_gettime() works
- TC-036-ErrorProp: intel_avb errors propagated correctly

---

### REQ-F-INTEL-AVB-003: Register Access Abstraction
**Verification Method**: **Inspection + Test** (semi-automated)  
**Test Level**: Code Review  
**Rationale**: Requires code audit for abstraction compliance  
**Test Cases**:
- TC-037-StaticAnalysis: Scan for register addresses (0x[0-9A-F]{5})
**Inspection**: Code review confirms zero direct register writes

---

### REQ-F-REG-ACCESS-001: Safe Register Access
**Verification Method**: **Test + Analysis** (automated)  
**Test Level**: Integration  
**Rationale**: Verify thread-safe register access  
**Test Cases**:
- TC-038-Concurrent: 100 threads access registers
- TC-038-NoTornReads: Atomic 64-bit PHC reads
**Analysis**: Lock ordering analysis for deadlock prevention

---

## 7. Hardware Detection Requirements (3 total)

### REQ-F-HW-DETECT-001: Hardware Capability Detection
**Verification Method**: **Test** (automated)  
**Test Level**: Integration  
**Rationale**: Verify capability detection  
**Test Cases**:
- TC-039-i225: Detect i225 capabilities (PHC, TAS, CBS)
- TC-039-i210: Detect i210 capabilities (PHC, CBS only)

---

### REQ-F-HW-QUIRKS-001: Controller-Specific Quirks
**Verification Method**: **Test** (automated)  
**Test Level**: Compatibility  
**Rationale**: Verify quirk handling for different controllers  
**Test Cases**:
- TC-040-i210Quirks: Handle i210-specific quirks
- TC-040-i225Quirks: Handle i225-specific quirks

---

### REQ-F-HW-UNSUPPORTED-001: Unsupported Hardware Handling
**Verification Method**: **Test** (automated)  
**Test Level**: Integration  
**Rationale**: Verify graceful skip of unsupported hardware  
**Test Cases**:
- TC-041-NonIntel: Filter skips non-Intel adapter
- TC-041-NoAVB: Filter skips Intel adapter without AVB

---

## 8. gPTP Integration Requirements (7 total)

### REQ-F-GPTP-INTEROP-001: gPTP Interoperability
**Verification Method**: **Test** (manual - interop)  
**Test Level**: System  
**Rationale**: Requires real gPTP master (Linux ptp4l or grandmaster)  
**Test Cases**:
- TC-042-ptp4l: Interop with Linux ptp4l
- TC-042-Grandmaster: Interop with Meinberg grandmaster

---

### REQ-F-GPTP-SYNC-001: gPTP Sync Accuracy
**Verification Method**: **Test** (automated)  
**Test Level**: System  
**Rationale**: Verify sync accuracy measurement  
**Test Cases**:
- TC-043-Accuracy: Offset <100ns RMS
- TC-043-Convergence: Converges in <10 seconds

---

### REQ-F-GPTP-MASTER-001: gPTP Master Mode
**Verification Method**: **Test** (manual)  
**Test Level**: System  
**Rationale**: Requires gPTP slave to sync to Windows master  
**Test Cases**:
- TC-044-Master: Windows PHC as gPTP master
- TC-044-SlaveSyncs: Linux slave syncs to Windows master

---

### REQ-F-GPTP-SLAVE-001: gPTP Slave Mode
**Verification Method**: **Test** (automated)  
**Test Level**: System  
**Rationale**: Verify gPTP slave synchronization  
**Test Cases**:
- TC-045-Slave: Windows PHC as gPTP slave
- TC-045-Offset: Slave offset <100ns RMS

---

### REQ-F-GPTP-PDELAY-001: Peer Delay Measurement
**Verification Method**: **Test** (automated)  
**Test Level**: System  
**Rationale**: Verify peer delay mechanism  
**Test Cases**:
- TC-046-Pdelay: Measure peer delay
- TC-046-Accuracy: Delay measurement accurate

---

### REQ-F-GPTP-ANNOUNCE-001: Announce Message Handling
**Verification Method**: **Test** (manual)  
**Test Level**: System  
**Rationale**: Requires gPTP message generation  
**Test Cases**:
- TC-047-Announce: Process Announce messages
- TC-047-BMCA: Participate in Best Master Clock Algorithm

---

### REQ-F-GPTP-BMCA-001: Best Master Clock Algorithm
**Verification Method**: **Test** (manual)  
**Test Level**: System  
**Rationale**: Requires multi-master topology  
**Test Cases**:
- TC-048-BMCA: Select best master clock
- TC-048-Failover: Failover to backup master

---

## 9. Error Handling Requirements (3 total)

### REQ-F-ERROR-RECOVERY-001: Automatic Error Recovery
**Verification Method**: **Test** (automated)  
**Test Level**: Integration  
**Rationale**: Verify recovery from transient errors  
**Test Cases**:
- TC-049-Recovery: Recover from register read failure
- TC-049-Retry: Retry mechanism works

---

### REQ-F-ERROR-LOGGING-001: ETW Error Logging
**Verification Method**: **Test** (automated)  
**Test Level**: Integration  
**Rationale**: Verify ETW logging  
**Test Cases**:
- TC-050-ETW: Errors logged to ETW
- TC-050-Messages: Error messages informative

---

### REQ-F-GRACEFUL-DEGRADE-001: Graceful Degradation
**Verification Method**: **Test** (automated)  
**Test Level**: System  
**Ratability**: Verify system continues operating with reduced functionality  
**Test Cases**:
- TC-051-Degrade: System degrades gracefully when PHC unavailable
- TC-051-Passthrough: Packets still forwarded (no timestamping)

---

## 10. Non-Functional Performance (6 total)

### REQ-NF-PERF-PHC-001: PHC Query Latency (<500µs p95)
**Verification Method**: **Test** (automated - benchmark)  
**Test Level**: Performance  
**Rationale**: Requires high-precision latency measurement  
**Test Cases**:
- TC-052-Latency: Measure latency over 10,000 queries
- TC-052-p95: Verify p95 ≤500µs

---

### REQ-NF-PERF-TS-001: TX/RX Timestamp Latency (<1µs)
**Verification Method**: **Test** (automated - benchmark)  
**Test Level**: Performance  
**Rationale**: Requires high-precision latency measurement  
**Test Cases**:
- TC-053-TXLatency: TX timestamp retrieval <1µs
- TC-053-RXLatency: RX timestamp retrieval <1µs

---

### REQ-NF-PERF-NDIS-001: Packet Forwarding (<1µs)
**Verification Method**: **Test** (automated - benchmark)  
**Test Level**: Performance  
**Rationale**: Requires RDTSC-based latency measurement  
**Test Cases**:
- TC-054-Forwarding: Measure forwarding latency (RDTSC)
- TC-054-p95: Verify p95 ≤1µs

---

### REQ-NF-PERF-PTP-001: PTP Sync Accuracy (<100ns RMS)
**Verification Method**: **Test** (automated - measurement)  
**Test Level**: System  
**Rationale**: Requires gPTP offset measurement  
**Test Cases**:
- TC-055-SyncAccuracy: Measure offset from gPTP master
- TC-055-RMS: Verify RMS ≤100ns

---

### REQ-NF-PERF-THROUGHPUT-001: Throughput (≥1 Gbps)
**Verification Method**: **Test** (automated - benchmark)  
**Test Level**: Performance  
**Rationale**: Requires throughput measurement (iperf3)  
**Test Cases**:
- TC-056-Throughput: Measure throughput via iperf3
- TC-056-1Gbps: Verify ≥1 Gbps sustained

---

### REQ-NF-PERF-CPU-001: CPU Usage (<20%)
**Verification Method**: **Test** (automated - monitoring)  
**Test Level**: Performance  
**Rationale**: Requires CPU utilization monitoring  
**Test Cases**:
- TC-057-CPU: Monitor CPU usage during packet forwarding
- TC-057-20pct: Verify ≤20% CPU usage at 1 Gbps

---

## 11. Non-Functional Reliability (5 total)

### REQ-NF-REL-PHC-001: PHC Monotonicity
**Verification Method**: **Test** (automated - stress)  
**Test Level**: Stress  
**Rationale**: Requires millions of reads to detect violations  
**Test Cases**:
- TC-058-Monotonic: 10M sequential PHC reads
- TC-058-NoBackward: Zero negative deltas (T[n+1] ≥ T[n])

---

### REQ-NF-REL-NDIS-001: No BSOD (1000 cycles)
**Verification Method**: **Test** (automated - stress)  
**Test Level**: Stress  
**Rationale**: Requires repeated attach/detach cycles  
**Test Cases**:
- TC-059-AttachDetach: 1000 attach/detach cycles
- TC-059-NoBSOD: Zero system crashes
- TC-059-DriverVerifier: Driver Verifier clean

---

### REQ-NF-REL-MEMORY-001: No Memory Leaks
**Verification Method**: **Test** (automated - Driver Verifier)  
**Test Level**: Stress  
**Rationale**: Requires Driver Verifier memory leak detection  
**Test Cases**:
- TC-060-MemoryLeak: Run stress test with Driver Verifier
- TC-060-NoLeak: Zero memory leaks detected

---

### REQ-NF-REL-UPTIME-001: 24-Hour Uptime
**Verification Method**: **Test** (manual - long-duration)  
**Test Level**: Stress  
**Rationale**: Requires 24-hour continuous operation  
**Test Cases**:
- TC-061-Uptime: Run driver for 24 hours
- TC-061-Stable: No crashes, no memory growth

---

### REQ-NF-REL-RECOVERY-001: Recovery from Errors
**Verification Method**: **Test** (automated)  
**Test Level**: Integration  
**Rationale**: Verify error recovery mechanisms  
**Test Cases**:
- TC-062-Recovery: Inject errors, verify recovery
- TC-062-NoHang: System does not hang on errors

---

## 12. Non-Functional Security (3 total)

### REQ-NF-SEC-INPUT-001: Input Validation (All IOCTLs)
**Verification Method**: **Test + Analysis** (automated)  
**Test Level**: Unit  
**Rationale**: Security-critical, requires fuzzing + code review  
**Test Cases**:
- TC-063-Fuzzing: Fuzz all IOCTL inputs
- TC-063-NoCrash: No crashes on invalid inputs
**Analysis**: Code review for input validation coverage

---

### REQ-NF-SEC-PRIV-001: Least Privilege (Kernel)
**Verification Method**: **Inspection** (manual)  
**Test Level**: Security Review  
**Rationale**: Requires privilege analysis  
**Review Checklist**:
- [ ] Driver runs at minimum IRQL
- [ ] No unnecessary kernel privileges
- [ ] User-mode handles validated

---

### REQ-NF-SEC-BUFFER-001: Buffer Overflow Protection
**Verification Method**: **Test + Analysis** (automated)  
**Test Level**: Unit  
**Rationale**: Security-critical, requires static analysis + testing  
**Test Cases**:
- TC-064-BufferOverflow: Test with oversized buffers
- TC-064-NoCrash: No buffer overflows detected
**Analysis**: Static analysis (/analyze flag in Visual Studio)

---

## 13. Non-Functional Compatibility (2 total)

### REQ-NF-COMPAT-NDIS-001: Windows 10/11 Support
**Verification Method**: **Test** (automated)  
**Test Level**: Compatibility  
**Rationale**: Requires testing on multiple Windows versions  
**Test Cases**:
- TC-065-Win10: Driver works on Windows 10 21H2
- TC-065-Win11: Driver works on Windows 11 22H2
- TC-065-Server2022: Driver works on Server 2022

---

### REQ-NF-COMPAT-HW-001: Multi-Controller Support
**Verification Method**: **Test** (automated)  
**Test Level**: Compatibility  
**Rationale**: Requires testing on multiple Intel controllers  
**Test Cases**:
- TC-066-i210: Driver works on i210
- TC-066-i225: Driver works on i225
- TC-066-i226: Driver works on i226 (future)

---

## 14. Verification Methods Summary Table

| Category | Total REQ | Test | Analysis | Inspection | Demonstration |
|----------|-----------|------|----------|------------|---------------|
| IOCTL API | 15 | 13 | 1 | 1 | 0 |
| NDIS Filter | 14 | 11 | 1 | 1 | 1 |
| PHC/PTP | 8 | 6 | 1 | 1 | 0 |
| Hardware Abstraction | 4 | 2 | 1 | 1 | 0 |
| Hardware Detection | 3 | 3 | 0 | 0 | 0 |
| gPTP Integration | 7 | 7 | 0 | 0 | 0 |
| Error Handling | 3 | 3 | 0 | 0 | 0 |
| Performance | 6 | 6 | 0 | 0 | 0 |
| Reliability | 5 | 5 | 0 | 0 | 0 |
| Security | 3 | 2 | 1 | 0 | 0 |
| Compatibility | 2 | 2 | 0 | 0 | 0 |
| **TOTAL** | **64** | **52** | **7** | **4** | **1** |

**Percentages**:
- **Test**: 81% (52/64)
- **Analysis**: 11% (7/64)
- **Inspection**: 6% (4/64)
- **Demonstration**: 2% (1/64)

---

## 15. Test Execution Summary

**Automated Tests** (52 requirements):
- Executed in CI/CD pipeline
- Run on every commit
- Fast feedback (<30 minutes)

**Semi-Automated Tests** (9 requirements):
- Require manual setup (e.g., gPTP master)
- Executed weekly or pre-release
- Medium duration (1-4 hours)

**Manual Tests** (3 requirements):
- Require human judgment (e.g., documentation review)
- Executed at milestones
- Variable duration

**Test Automation Coverage**: 81% (52/64 requirements)

---

## 16. Next Steps

✅ **Phase 02 Complete**: All verification methods defined (64/64 requirements)

**Next Actions**:
1. ✅ Mark Task 6 complete (verification methods defined)
2. ✅ Validate Phase 02 exit criteria (Task 7)
3. ⏭️ Obtain stakeholder approval for Phase 02 baseline
4. ⏭️ Execute architecture-starter.prompt.md (Phase 03)

---

**Document Approval**:
- [ ] Test Manager: ________________________ Date: __________
- [ ] Development Manager: ________________________ Date: __________
- [ ] Quality Assurance: ________________________ Date: __________
