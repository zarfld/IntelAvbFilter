# System Acceptance Test Plan

**Project**: Intel AVB Filter Driver  
**Purpose**: Define stakeholder validation approach for acceptance testing  
**Created**: 2025-12-12  
**Phase**: 02 - Requirements Analysis & Specification  
**Standards**: IEEE 1012-2016 (V&V), ISO/IEC/IEEE 29148:2018

---

## 1. Document Control

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2025-12-12 | Standards Compliance Advisor | Initial creation - Phase 02 exit criteria |

**Approval**:
- [ ] Stakeholder Representative: ________________________ Date: __________
- [ ] Project Manager: ________________________ Date: __________
- [ ] Quality Assurance: ________________________ Date: __________

---

## 2. Executive Summary

This System Acceptance Test Plan defines how the Intel AVB Filter Driver will be validated against **stakeholder requirements** (StR #1, #28-#33) to ensure the delivered system meets business and user needs.

**Key Distinctions**:
- **Acceptance Testing**: Validates RIGHT product built (stakeholder needs met)
- **Qualification Testing**: Verifies product built RIGHT (requirements met) - see separate plan
- **V&V Strategy**: IEEE 1012-2016 Section 6.3.2 (Acceptance Test Process)

**Test Approach**:
- Customer-driven scenarios (real-world use cases)
- Executable acceptance criteria (Gherkin Given-When-Then)
- Multi-phase testing (Alpha → Beta → User Acceptance Testing)
- Stakeholder involvement at each phase

**Success Criteria**:
- ✅ All P0 stakeholder requirements validated (100%)
- ✅ 95% of P1 stakeholder requirements validated
- ✅ Zero critical defects (P0/P1 blockers)
- ✅ Stakeholder sign-off obtained

---

## 3. Scope and Objectives

### 3.1 In-Scope (Acceptance Testing)

**What We Validate**:
- ✅ Stakeholder Requirements (StR #1, #28-#33) met
- ✅ Real-world use cases work end-to-end
- ✅ System usable by target users (driver developers, TSN integrators)
- ✅ System solves original business problem (Intel NIC AVB/TSN support)

**Stakeholder Requirements Covered**:

| StR Issue | Title | Priority | Validation Approach |
|-----------|-------|----------|---------------------|
| #1 | StR-INTEL-NIC-001: Intel NIC Support | P0 | Integration test on i210, i225, i226 |
| #28 | StR-GPTP-001: gPTP Synchronization | P0 | Interoperability test with gPTP master |
| #29 | StR-INTEL-AVB-LIB: Library Integration | P1 | Code audit + functional test |
| #30 | StR-STANDARDS-COMPLIANCE: IEEE 802.1 TSN | P1 | Standards compliance checklist |
| #31 | StR-NDIS-FILTER-001: NDIS Filter Driver | P0 | Driver lifecycle test (attach/detach/IOCTL) |
| #32 | StR-FUTURE-SERVICE: Future User-Mode Service | P3 | Out of scope (deferred to future release) |
| #33 | StR-API-ENDPOINTS: IOCTL API Endpoints | P0 | API functional test + documentation review |

### 3.2 Out-of-Scope

**What We Do NOT Validate Here**:
- ❌ Detailed requirement verification (see System Qualification Test Plan)
- ❌ Unit-level testing (covered in Phase 05 implementation)
- ❌ Performance benchmarking (covered in qualification testing)
- ❌ Future features (StR #32: User-Mode Service)

### 3.3 Objectives

**Primary Objective**: Demonstrate that the Intel AVB Filter Driver meets stakeholder needs and is ready for deployment.

**Specific Objectives**:
1. **Validate Use Cases**: All critical use cases (gPTP sync, packet timestamping, NDIS lifecycle) work end-to-end
2. **Confirm Usability**: System can be integrated by TSN developers without excessive effort
3. **Verify Interoperability**: Works with real gPTP implementations (e.g., Linux ptp4l, Meinberg grandmaster)
4. **Assess Quality**: System is stable, reliable, and performs adequately for production use
5. **Obtain Sign-Off**: Stakeholders approve system for release

---

## 4. Test Strategy

### 4.1 Testing Philosophy

**Customer-Centric Approach**:
- Test from **user's perspective** (not developer's perspective)
- Scenarios reflect **real-world usage** (not synthetic stress tests)
- **Executable specifications**: Gherkin scenarios are acceptance criteria
- **Stakeholder involvement**: Users validate system meets their needs

**Acceptance vs. Qualification**:

| Aspect | Acceptance Testing | Qualification Testing |
|--------|-------------------|----------------------|
| **Focus** | RIGHT product (needs) | Product built RIGHT (specs) |
| **Who Validates** | Stakeholders/customers | QA/test team |
| **Criteria** | Use cases, scenarios | Requirements, metrics |
| **Example** | "Can sync with gPTP master" | "PHC offset <100ns RMS" |
| **IEEE 1012 Section** | 6.3.2 (Acceptance) | 6.3.1 (Qualification) |

### 4.2 Test Levels

**Alpha Testing** (Internal - Week 1-2):
- **Environment**: Development lab (controlled)
- **Testers**: Development team + internal stakeholders
- **Focus**: Core functionality, basic use cases
- **Exit Criteria**: All P0 scenarios pass, zero critical bugs

**Beta Testing** (Limited Release - Week 3-4):
- **Environment**: Partner sites (2-3 early adopters)
- **Testers**: TSN integrators, driver developers
- **Focus**: Real-world integration, edge cases, usability
- **Exit Criteria**: 95% scenarios pass, user feedback positive

**User Acceptance Testing (UAT)** (Pre-Release - Week 5-6):
- **Environment**: Production-like (customer environments)
- **Testers**: All stakeholders (GitHub issue #1, #28-#33 reporters)
- **Focus**: Final validation, sign-off
- **Exit Criteria**: Stakeholder approval, zero P0/P1 defects

### 4.3 Test Environment

**Hardware Requirements**:
- Intel i210 Ethernet Controller (PCIe)
- Intel i225 Ethernet Controller (PCIe)
- Intel i226 Ethernet Controller (optional, for future support)
- Test PC: Windows 10/11 (x64), minimum 8GB RAM, PCIe slots available

**Software Requirements**:
- Windows 10 21H2 or Windows 11 22H2 (x64)
- Visual Studio 2022 (for driver development)
- WinDbg Preview (for kernel debugging)
- gPTP Implementation: Linux ptp4l or Meinberg grandmaster (for interoperability)
- Network Analyzer: Wireshark + PTP dissector (for protocol validation)

**Network Topology**:
```
[gPTP Master] ─── Switch ─── [Windows PC + Intel i225]
                 (802.1AS)        (AVB Filter Driver)
```

**Test Data**:
- Sample PTP packets (Sync, Follow_Up, Delay_Req, Delay_Resp)
- Sample AVTP streams (Audio/Video Transport Protocol)
- Configuration files (gPTP settings, TAS schedules)

---

## 5. Acceptance Test Cases (Traceability Matrix)

### 5.1 StR #1: Intel NIC Support

**Stakeholder Requirement**: System shall support Intel i210, i225, and i226 Ethernet controllers with AVB/TSN capabilities.

**Acceptance Criteria**:

#### AT-001: Multi-Controller Support
**Given** System has Intel i210, i225, i226 adapters installed  
**When** NDIS filter driver loaded  
**Then** Filter binds to all supported adapters  
**And** Unsupported adapters (non-Intel) are skipped gracefully  
**And** Each adapter reports correct hardware capabilities  

**Test Procedure**:
1. Install i210, i225, i226 adapters in test PC
2. Load AVB filter driver
3. Verify filter attachment via `devcon status`
4. Query capabilities via `IOCTL_AVB_GET_CAPABILITIES`
5. Compare detected capabilities to hardware datasheets

**Expected Results**:
- ✅ i210: PHC ✅, TAS ❌, CBS ✅
- ✅ i225: PHC ✅, TAS ✅, CBS ✅
- ✅ i226: PHC ✅, TAS ✅, CBS ✅ (future support)

**Pass Criteria**: All supported adapters detected and configured correctly

**Traces to**: #1 (StR-INTEL-NIC-001), #34-#36 (REQ-F hardware detection)

---

### 5.2 StR #28: gPTP Synchronization

**Stakeholder Requirement**: System shall support IEEE 802.1AS gPTP synchronization for time-sensitive networking.

**Acceptance Criteria**:

#### AT-002: gPTP Master-Slave Synchronization
**Given** gPTP master clock running (Linux ptp4l or Meinberg)  
**And** Windows PC with Intel i225 + AVB filter driver as gPTP slave  
**When** gPTP synchronization active for >1 minute  
**Then** PHC synchronized to master with offset <100ns RMS  
**And** Sync interval 125ms (8 Sync messages/second)  
**And** Convergence time <10 seconds after startup  

**Test Procedure**:
1. Configure gPTP master (ptp4l or grandmaster)
2. Start AVB filter driver on Windows slave
3. Start gPTP daemon on Windows (using PHC IOCTL API)
4. Monitor PHC offset via `ptp_clock_control_test.exe`
5. Capture 1000 Sync messages via Wireshark
6. Calculate RMS offset, convergence time, stability

**Expected Results**:
- ✅ Offset RMS: <100ns
- ✅ Offset peak: <500ns (99th percentile)
- ✅ Sync interval: 125ms ±5ms
- ✅ Convergence: <10 seconds
- ✅ Wireshark shows valid IEEE 1588 PTP messages

**Pass Criteria**: Synchronization accuracy meets IEEE 802.1AS requirements (<1µs spec, <100ns target)

**Traces to**: #28 (StR-GPTP-001), #34-#36 (REQ-F PHC IOCTLs), #47-#49 (REQ-NF Performance)

---

#### AT-003: TX/RX Timestamp Accuracy
**Given** gPTP Sync message transmitted from slave to master  
**When** Slave retrieves TX timestamp via `IOCTL_AVB_TX_TIMESTAMP_GET`  
**Then** TX timestamp accurate within ±8ns of actual transmission time  
**And** RX timestamp (at master) matches TX timestamp within ±50ns (propagation delay)  

**Test Procedure**:
1. Transmit PTP Sync message from Windows slave
2. Retrieve TX timestamp via IOCTL
3. Capture message at master with hardware RX timestamp
4. Compare TX timestamp to RX timestamp (accounting for propagation delay)
5. Repeat for 1000 messages, calculate accuracy

**Expected Results**:
- ✅ TX timestamp accuracy: ±8ns (hardware spec)
- ✅ RX timestamp accuracy: ±8ns
- ✅ Propagation delay consistent (±50ns across messages)

**Pass Criteria**: Timestamp accuracy meets hardware specification

**Traces to**: #28 (gPTP), #35 (TX timestamp), #36 (RX timestamp)

---

### 5.3 StR #29: intel_avb Library Integration

**Stakeholder Requirement**: System shall integrate intel_avb library for hardware abstraction.

**Acceptance Criteria**:

#### AT-004: Register Access Abstraction
**Given** AVB filter driver source code  
**When** Code audit performed  
**Then** Zero direct register writes found (no `0x15C18` patterns)  
**And** All hardware access via intel_avb API (`intel_avb_phc_*`, `intel_avb_tas_*`)  
**And** Filter works on i210, i225 without controller-specific code  

**Test Procedure**:
1. Static analysis: Scan filter driver code for register addresses (regex: `0x[0-9A-F]{5}`)
2. Code review: Manual inspection for direct hardware access
3. Functional test: Same filter binary works on i210 and i225 (portability)
4. Verify intel_avb library handles controller differences

**Expected Results**:
- ✅ Static analysis clean (zero direct register accesses)
- ✅ Code review approval: Abstraction layer correctly used
- ✅ Portability verified: Filter works on multiple controllers

**Pass Criteria**: 100% abstraction compliance, zero direct register access

**Traces to**: #29 (StR-INTEL-AVB-LIB), #79 (REQ-F-INTEL-AVB-003)

---

### 5.4 StR #30: IEEE 802.1 TSN Standards Compliance

**Stakeholder Requirement**: System shall comply with IEEE 802.1AS (gPTP), 802.1Qav (CBS), 802.1Qbv (TAS).

**Acceptance Criteria**:

#### AT-005: IEEE 802.1AS Compliance (gPTP)
**Given** IEEE 802.1AS-2020 standard document  
**When** gPTP implementation reviewed  
**Then** All mandatory features implemented:
- ✅ Sync message transmission (master mode)
- ✅ Sync message reception (slave mode)
- ✅ Follow_Up message support
- ✅ Delay_Req/Delay_Resp mechanism
- ✅ Path delay measurement  
**And** Compliance checklist 100% complete  

**Test Procedure**:
1. Review IEEE 802.1AS-2020 compliance checklist
2. Test each mandatory feature (Sync, Follow_Up, Delay mechanism)
3. Interoperability test with certified 802.1AS device
4. Wireshark packet analysis: Verify message formats

**Expected Results**:
- ✅ All mandatory features present
- ✅ Interoperability with certified gPTP device
- ✅ Packet formats conform to standard (Wireshark validation)

**Pass Criteria**: IEEE 802.1AS compliance checklist 100% complete

**Traces to**: #30 (StR-STANDARDS-COMPLIANCE), #28 (gPTP)

---

#### AT-006: IEEE 802.1Qav Compliance (Credit-Based Shaper)
**Given** IEEE 802.1Qav standard  
**When** CBS (Credit-Based Shaper) configured for AVTP stream  
**Then** Traffic shaping enforces bandwidth reservation  
**And** Idle slope/send slope calculations correct  
**And** AVTP stream latency meets requirements  

**Test Procedure**:
1. Configure CBS for AVTP Class A (bandwidth reservation: 75%)
2. Transmit AVTP stream at 125 Mbps
3. Measure bandwidth utilization, latency, jitter
4. Verify CBS credits algorithm (idle slope, send slope)

**Expected Results**:
- ✅ Bandwidth reservation enforced (75% Class A traffic)
- ✅ Latency <2ms (Class A requirement)
- ✅ CBS algorithm correct (per IEEE 802.1Qav equations)

**Pass Criteria**: CBS compliant with IEEE 802.1Qav

**Traces to**: #30 (Standards Compliance), CBS requirements (future)

---

#### AT-007: IEEE 802.1Qbv Compliance (Time-Aware Shaper - TAS)
**Given** IEEE 802.1Qbv standard  
**When** TAS gate control list configured (scheduled traffic)  
**Then** Time slots enforced (e.g., 500µs window for Class A)  
**And** Gate opens/closes at correct times (synchronized to gPTP)  
**And** Scheduled latency deterministic (<1µs variance)  

**Test Procedure**:
1. Configure TAS schedule: Class A (500µs window every 1ms)
2. Transmit mixed traffic (Class A + best-effort)
3. Measure gate timing accuracy (compare to gPTP PHC)
4. Verify scheduled traffic meets latency requirements

**Expected Results**:
- ✅ Gate timing accurate (±100ns vs. PHC)
- ✅ Class A latency deterministic (<1µs variance)
- ✅ Best-effort traffic blocked during Class A windows

**Pass Criteria**: TAS compliant with IEEE 802.1Qbv

**Traces to**: #30 (Standards Compliance), TAS requirements (future)

---

### 5.5 StR #31: NDIS Filter Driver

**Stakeholder Requirement**: System shall implement NDIS 6.x filter driver with minimal packet forwarding overhead.

**Acceptance Criteria**:

#### AT-008: NDIS Filter Lifecycle (Attach/Detach)
**Given** Intel i225 adapter installed  
**When** Adapter enabled/disabled via Device Manager  
**Then** Filter attaches successfully (FilterAttach callback)  
**And** Filter detaches cleanly (FilterDetach callback)  
**And** No memory leaks (Driver Verifier clean)  
**And** No BSOD in 100 attach/detach cycles  

**Test Procedure**:
1. Enable Driver Verifier (Special Pool, Force IRQL Checking)
2. Automate adapter enable/disable (devcon or PowerShell)
3. Run 100 attach/detach cycles
4. Monitor for crashes, check Driver Verifier logs
5. Verify memory usage stable (no leaks)

**Expected Results**:
- ✅ All 100 attaches succeed
- ✅ All 100 detaches complete cleanly
- ✅ Driver Verifier clean (zero violations)
- ✅ Zero BSODs

**Pass Criteria**: NDIS lifecycle robust, no stability issues

**Traces to**: #31 (StR-NDIS-FILTER-001), #36 (REQ-F-NDIS-ATTACH-001)

---

#### AT-009: NDIS Packet Forwarding (TX/RX)
**Given** Normal TCP/IP traffic (non-PTP)  
**When** Packets forwarded through filter driver  
**Then** Forwarding overhead <1µs (p95 latency)  
**And** Zero packet loss at 10,000 pps  
**And** Throughput ≥1 Gbps (line rate for GbE)  

**Test Procedure**:
1. Generate TCP traffic (iperf3) at 1 Gbps
2. Measure latency: Filter entry → exit (RDTSC timestamps)
3. Monitor packet counters (dropped packets)
4. Verify zero-copy forwarding (NBL passthrough)

**Expected Results**:
- ✅ Latency p95: <1µs
- ✅ Packet loss: 0%
- ✅ Throughput: 1 Gbps sustained

**Pass Criteria**: Minimal overhead, line-rate performance

**Traces to**: #31 (NDIS Filter), #42-#43 (REQ-F-NDIS-SEND/RECEIVE)

---

### 5.6 StR #33: IOCTL API Endpoints

**Stakeholder Requirement**: System shall provide well-documented IOCTL API for user-mode applications.

**Acceptance Criteria**:

#### AT-010: IOCTL API Functional Test
**Given** User-mode test application  
**When** Application uses IOCTL API to:
- Query PHC time (`IOCTL_AVB_PHC_QUERY`)
- Adjust PHC offset (`IOCTL_AVB_PHC_ADJUST_OFFSET`)
- Retrieve TX/RX timestamps (`IOCTL_AVB_TX_TIMESTAMP_GET`)
- Query hardware capabilities (`IOCTL_AVB_GET_CAPABILITIES`)  
**Then** All IOCTLs succeed with correct responses  
**And** Error handling correct (invalid parameters, device not ready)  
**And** Concurrent IOCTLs work (no race conditions)  

**Test Procedure**:
1. Develop test app using IOCTL API
2. Test each IOCTL with valid inputs (happy path)
3. Test error handling (invalid inputs, no adapter)
4. Test concurrency (10 threads calling IOCTLs simultaneously)
5. Verify results against hardware (PHC time vs. system time)

**Expected Results**:
- ✅ All IOCTLs succeed with correct data
- ✅ Error handling robust (no crashes on invalid input)
- ✅ Concurrency safe (no deadlocks, race conditions)

**Pass Criteria**: IOCTL API fully functional, robust error handling

**Traces to**: #33 (StR-API-ENDPOINTS), #34-#36 (REQ-F-IOCTL-*)

---

#### AT-011: IOCTL API Documentation Quality
**Given** API documentation (README or header file comments)  
**When** Developer reviews documentation  
**Then** Documentation includes:
- ✅ IOCTL code definitions (`0x9C400001`, etc.)
- ✅ Input/output buffer structures
- ✅ Error code definitions and meanings
- ✅ Example code snippets (C)
- ✅ Usage guidelines (when to use each IOCTL)  
**And** Documentation accurate (matches implementation)  

**Test Procedure**:
1. Code review: Verify documentation completeness
2. Developer usability test: Give documentation to new developer, observe integration
3. Cross-reference docs to implementation (ensure accuracy)

**Expected Results**:
- ✅ Documentation complete and accurate
- ✅ New developer can integrate API in <2 hours (usability target)

**Pass Criteria**: Documentation sufficient for independent integration

**Traces to**: #33 (API Endpoints), Documentation requirements (future)

---

## 6. Test Execution

### 6.1 Test Schedule

**Phase 02 Completion** (Current):
- Week 1: Test plan creation, test environment setup
- Week 2: Test case development, automation scripts

**Phase 05-06 (Implementation/Integration)**:
- Week 10-12: Alpha Testing (internal)
- Week 13-14: Beta Testing (partners)
- Week 15-16: UAT (stakeholders)

**Milestones**:
- Alpha Exit: All P0 scenarios pass, <10 P1 defects
- Beta Exit: 95% scenarios pass, positive user feedback
- UAT Exit: Stakeholder sign-off, zero P0/P1 defects

### 6.2 Test Resources

**Team**:
- Test Lead: [Name]
- Test Engineers: [Names] (2-3 engineers)
- Stakeholder Representatives: GitHub issue #1, #28-#33 reporters
- Development Liaison: [Name]

**Tools**:
- Test Management: Azure DevOps Test Plans (or GitHub Projects)
- Automation: PowerShell scripts, C# test harness
- Packet Analysis: Wireshark + PTP dissector
- gPTP Implementation: Linux ptp4l (reference)
- Debugging: WinDbg, ETW traces

### 6.3 Defect Management

**Severity Levels**:
- **P0 (Critical)**: Acceptance criteria failure, system unusable (e.g., BSOD, gPTP sync fails)
- **P1 (High)**: Major feature broken (e.g., timestamp accuracy >1µs)
- **P2 (Medium)**: Minor functionality issue (e.g., error message unclear)
- **P3 (Low)**: Cosmetic, documentation typo

**Defect Workflow**:
1. Defect found during testing → Create GitHub issue with `type:defect` label
2. Triage: Assign severity (P0-P3)
3. Fix: Development team resolves defect
4. Retest: Test team verifies fix
5. Close: Defect closed when fix verified

**Exit Criteria Impact**:
- **P0 defects**: Block release (must fix)
- **P1 defects**: Block release unless waived by stakeholders
- **P2/P3 defects**: Can defer to future release

---

## 7. Success Criteria and Exit Criteria

### 7.1 Acceptance Test Exit Criteria

**Alpha Testing Exit Criteria**:
- ✅ All P0 scenarios pass (100%)
- ✅ 90% P1 scenarios pass
- ✅ Zero P0 defects, <10 P1 defects
- ✅ Internal stakeholder approval

**Beta Testing Exit Criteria**:
- ✅ 95% P0 + P1 scenarios pass
- ✅ Zero P0 defects, <5 P1 defects
- ✅ User feedback positive (>80% satisfaction)
- ✅ Partner sign-off (2-3 early adopters)

**UAT Exit Criteria**:
- ✅ 100% P0 scenarios pass
- ✅ 95% P1 scenarios pass
- ✅ Zero P0/P1 defects open
- ✅ All stakeholders approve (GitHub issue #1, #28-#33)
- ✅ Documentation complete and accurate
- ✅ Release notes approved

### 7.2 Stakeholder Sign-Off

**Sign-Off Process**:
1. UAT completion → Test summary report generated
2. Report sent to stakeholders for review
3. Stakeholders validate system meets their needs
4. Sign-off obtained (email approval or GitHub issue comment)

**Sign-Off Template** (GitHub Issue Comment):
```markdown
## Stakeholder Sign-Off: [StR Issue Number]

**Stakeholder**: [Name]
**StR Requirement**: [Title]
**Test Cases Executed**: AT-001, AT-002, ... (list)

**Validation Results**:
- [ ] All acceptance criteria met
- [ ] System meets my needs as stakeholder
- [ ] Quality acceptable for release
- [ ] Documentation sufficient

**Sign-Off**: ✅ APPROVED / ❌ REJECTED

**Comments**: [Any notes, concerns, or conditions]

**Date**: 2025-MM-DD
```

---

## 8. Test Traceability Summary

### 8.1 Traceability Matrix: StR → Acceptance Tests

| StR Issue | Title | Priority | Acceptance Tests | Status |
|-----------|-------|----------|------------------|--------|
| #1 | StR-INTEL-NIC-001 | P0 | AT-001 | Planned |
| #28 | StR-GPTP-001 | P0 | AT-002, AT-003 | Planned |
| #29 | StR-INTEL-AVB-LIB | P1 | AT-004 | Planned |
| #30 | StR-STANDARDS-COMPLIANCE | P1 | AT-005, AT-006, AT-007 | Planned |
| #31 | StR-NDIS-FILTER-001 | P0 | AT-008, AT-009 | Planned |
| #32 | StR-FUTURE-SERVICE | P3 | Out of Scope | Deferred |
| #33 | StR-API-ENDPOINTS | P0 | AT-010, AT-011 | Planned |

**Coverage Summary**:
- ✅ P0 Stakeholder Requirements: 5 of 5 covered (100%)
- ✅ P1 Stakeholder Requirements: 2 of 2 covered (100%)
- ⚠️ P3 Stakeholder Requirements: 0 of 1 covered (deferred)

---

## 9. Appendices

### Appendix A: Gherkin Scenario Examples

**Example 1: gPTP Synchronization (AT-002)**
```gherkin
Feature: gPTP Clock Synchronization (StR #28)

Scenario: Slave synchronizes to master within 10 seconds
  Given gPTP master clock running at 192.168.1.100
  And Windows slave with Intel i225 adapter at 192.168.1.101
  And AVB filter driver loaded on slave
  When gPTP daemon started on slave (using PHC IOCTL API)
  Then PHC offset from master converges to <100ns within 10 seconds
  And Sync messages received at 125ms intervals (8/sec)
  And PHC offset remains stable (<100ns RMS) for 60 seconds

Scenario: TX timestamp accurate for PTP Sync message
  Given gPTP slave sends Sync message to master
  When slave retrieves TX timestamp via IOCTL_AVB_TX_TIMESTAMP_GET
  Then TX timestamp captured at MAC egress (not kernel time)
  And Timestamp accuracy within ±8ns of actual transmission time
  And Retrieval latency <1µs
```

### Appendix B: Test Environment Setup Guide

**Hardware Setup**:
1. Install Intel i225 PCIe adapter in test PC
2. Connect Ethernet cable to gPTP master (Linux PC or grandmaster)
3. Verify physical link (1 Gbps, full duplex)

**Software Setup**:
1. Install Windows 10/11 x64
2. Install Visual Studio 2022 + WDK
3. Load AVB filter driver (test-signed)
4. Install gPTP daemon (Windows port of ptp4l)
5. Install Wireshark + PTP dissector plugin

**gPTP Master Configuration** (Linux ptp4l):
```bash
# /etc/ptp4l.conf
[global]
priority1 128
priority2 128
logSyncInterval -3  # 125ms (2^-3 seconds)
logMinDelayReqInterval -3
transportSpecific 0x1  # 802.1AS
network_transport L2
time_stamping hardware
```

**Windows Slave Configuration**:
```powershell
# Start AVB filter driver
sc start IntelAvbFilter

# Query hardware capabilities
.\avb_test_i210_um.exe --query-capabilities

# Start gPTP daemon (using PHC IOCTL API)
.\ptp4l.exe -i "Ethernet 2" -f ptp4l_slave.conf -m
```

### Appendix C: Acceptance Test Report Template

**Test Summary Report**
```markdown
# Acceptance Test Summary Report

**Test Phase**: Alpha / Beta / UAT
**Test Period**: [Start Date] to [End Date]
**Tester**: [Name]

## Test Execution Summary

| Metric | Result |
|--------|--------|
| Total Test Cases | 11 (AT-001 to AT-011) |
| Passed | X |
| Failed | Y |
| Blocked | Z |
| Pass Rate | X/11 = N% |

## Defects Summary

| Severity | Open | Resolved | Total |
|----------|------|----------|-------|
| P0 (Critical) | 0 | X | X |
| P1 (High) | Y | Z | Y+Z |
| P2 (Medium) | ... | ... | ... |

## Stakeholder Feedback

**StR #1 (Intel NIC Support)**: ✅ Validated - All adapters detected correctly
**StR #28 (gPTP Sync)**: ✅ Validated - Sync accuracy <100ns RMS
...

## Recommendation

- [ ] ✅ **PASS** - System ready for release
- [ ] ⚠️ **CONDITIONAL PASS** - Minor issues, release with known limitations
- [ ] ❌ **FAIL** - Critical issues, do not release

**Signed**: [Test Lead Name], [Date]
```

---

## 10. Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2025-12-12 | Standards Compliance Advisor | Initial creation |

---

**Document Approval**:
- [ ] Test Lead: ________________________ Date: __________
- [ ] Development Manager: ________________________ Date: __________
- [ ] Stakeholder Representative: ________________________ Date: __________

---

**Next Steps**:
1. ✅ System Acceptance Test Plan created (this document)
2. ⏭️ Create System Qualification Test Plan (detailed requirement verification)
3. ⏭️ Define verification methods for all 64 requirements
4. ⏭️ Validate Phase 02 exit criteria complete
5. ⏭️ Execute architecture-starter.prompt.md (Phase 03)
