# Test Plan: New IOCTL Requirements (Issues #312, #313, #314)

**Document ID**: TEST-PLAN-IOCTL-NEW-2025-12-31  
**Created**: 2025-12-31  
**Status**: 🟡 Draft  
**Phase**: 07-verification-validation  
**Standards**: IEEE 1012-2016 (Verification & Validation)  

## Traceability

- **Test Issues**:
  - [#312 - TEST-MDIO-PHY-001: MDIO/PHY Register Access](https://github.com/zarfld/IntelAvbFilter/issues/312)
  - [#313 - TEST-DEV-LIFECYCLE-001: Device Lifecycle Management](https://github.com/zarfld/IntelAvbFilter/issues/313)
  - [#314 - TEST-TS-EVENT-SUB-001: Timestamp Event Subscription](https://github.com/zarfld/IntelAvbFilter/issues/314)

- **Requirements**:
  - [#10 - REQ-F-MDIO-001: MDIO/PHY Access](https://github.com/zarfld/IntelAvbFilter/issues/10)
  - [#12 - REQ-F-DEV-001: Device Lifecycle Management](https://github.com/zarfld/IntelAvbFilter/issues/12)
  - [#13 - REQ-F-TS-SUB-001: Timestamp Event Subscription](https://github.com/zarfld/IntelAvbFilter/issues/13)

- **Master Tracker**: [#14 - IOCTL Support Implementation & Verification](https://github.com/zarfld/IntelAvbFilter/issues/14)

---

## 1. Test Plan Overview

### 1.1 Objectives

Execute comprehensive integration tests for three new IOCTL requirement areas:
1. **MDIO/PHY Access** (IOCTLs 29-30) - PHY register read/write operations
2. **Device Lifecycle Management** (IOCTLs 20-21, 31-32, 37) - Device initialization, enumeration, state management
3. **Timestamp Event Subscription** (IOCTLs 33-34) - Event subscription and ring buffer management

### 1.2 Scope

**In Scope**:
- User-mode IOCTL interface testing
- Hardware interaction validation (where applicable)
- Error handling and boundary condition testing
- Concurrent access scenarios
- Resource management (allocation/deallocation)

**Out of Scope**:
- Kernel-mode unit tests (separate test suite)
- Performance benchmarking (future work)
- Stress testing beyond basic concurrency
- Security penetration testing

### 1.3 Test Strategy

Following existing test infrastructure patterns:
- **Build System**: Extend `Build-Tests.ps1` with new test definitions
- **Execution**: Integrate with `Run-Tests.ps1` framework
- **Test Type**: User-mode integration tests using IOCTL interface
- **Test Harness**: Reuse existing `test_ioctl_*.c` patterns
- **Reporting**: Standard pass/fail with detailed logging

---

## 2. Test Environment

### 2.1 Hardware Requirements

**Minimum Configuration**:
- Intel I210/I225/I226 network adapter
- PCIe slot (for desktop) or Thunderbolt/USB adapter
- Active network connection (for link state tests)

**Recommended Configuration**:
- Multiple Intel AVB-capable adapters (for enumeration tests)
- PHY chip with documented register map
- Oscilloscope/logic analyzer (optional, for MDIO bus verification)

### 2.2 Software Requirements

- **Operating System**: Windows 10/11 (x64)
- **Driver**: IntelAvbFilter.sys (Debug or Release build)
- **Build Tools**: Visual Studio 2019/2022, Windows DDK
- **Test Framework**: Existing `Build-Tests.ps1` / `Run-Tests.ps1` infrastructure
- **Privileges**: Administrator access for IOCTL operations

### 2.3 Dependencies

- Driver must be installed and service running
- Test executables built via `Build-Tests.ps1`
- HAL layer functional (prerequisite from Issue #84)
- IOCTL handler implementations complete

---

## 3. Test Execution Plan

### 3.1 Test Sequence

**Phase 1: Build Verification** (Day 1)
```powershell
# Add new test definitions to Build-Tests.ps1
.\tools\build\Build-Tests.ps1 -TestName test_mdio_phy -Configuration Debug
.\tools\build\Build-Tests.ps1 -TestName test_dev_lifecycle -Configuration Debug
.\tools\build\Build-Tests.ps1 -TestName test_ts_event_sub -Configuration Debug
```

**Phase 2: Individual Test Execution** (Days 2-3)
```powershell
# Run each test independently
.\tools\test\Run-Tests.ps1 -TestExecutable test_mdio_phy.exe -Configuration Debug
.\tools\test\Run-Tests.ps1 -TestExecutable test_dev_lifecycle.exe -Configuration Debug
.\tools\test\Run-Tests.ps1 -TestExecutable test_ts_event_sub.exe -Configuration Debug
```

**Phase 3: Full Regression** (Day 4)
```powershell
# Run all IOCTL tests together
.\tools\test\Run-Tests.ps1 -Full -Configuration Debug
```

### 3.2 Test Coverage Matrix

| Test Executable | Issue | IOCTLs Tested | Test Cases | Priority | Estimated Time |
|-----------------|-------|---------------|------------|----------|----------------|
| `test_mdio_phy.exe` | #312 | 29, 30 | 15 | P1 | 2 hours |
| `test_dev_lifecycle.exe` | #313 | 20, 21, 31, 32, 37 | 19 | P0 | 3 hours |
| `test_ts_event_sub.exe` | #314 | 33, 34 | 19 | P1 | 3 hours |
| **Total** | - | **9 IOCTLs** | **53** | - | **8 hours** |

### 3.3 Entry Criteria

- ✅ Driver code implements all target IOCTLs (29-30, 20-21, 31-32, 37, 33-34)
- ✅ Test source files created under `tests/test_ioctl_*.c`
- ✅ Build-Tests.ps1 updated with new test definitions
- ✅ Hardware available and driver installed
- ✅ Previous IOCTL tests passing (baseline: Issues #295-299)

### 3.4 Exit Criteria

- ✅ All 53 test cases executed
- ✅ Pass rate ≥95% (50/53 tests passing)
- ✅ All P0 tests passing (test_dev_lifecycle.exe)
- ✅ Failures documented with GitHub issues
- ✅ Traceability verified (requirements ↔ tests)

---

## 4. Test Case Details

### 4.1 Test #312: MDIO/PHY Register Access

**Test Executable**: `test_mdio_phy.exe`  
**Source**: `tests/test_ioctl_mdio_phy.c`  
**IOCTLs**: 29 (MDIO_READ), 30 (MDIO_WRITE)  
**Test Cases**: 15 (UT-MDIO-001 through UT-MDIO-015)  

**Key Test Scenarios**:
1. Basic MDIO read from standard PHY register (Control, Status)
2. Basic MDIO write to configuration register
3. Multi-page PHY access (page switching)
4. Invalid PHY address rejection
5. Out-of-range register rejection
6. Read-only register write protection
7. MDIO bus timeout handling
8. Concurrent access serialization (spinlock verification)
9. Extended register access (Clause 45)
10. PHY reset via MDIO
11. Auto-negotiation status read
12. Link partner ability read
13. Cable diagnostics (vendor-specific registers)
14. MDIO access during low-power states
15. Bulk register read optimization

**Expected Duration**: 2 hours (including setup/teardown)

---

### 4.2 Test #313: Device Lifecycle Management

**Test Executable**: `test_dev_lifecycle.exe`  
**Source**: `tests/test_ioctl_dev_lifecycle.c`  
**IOCTLs**: 20 (INIT_DEVICE), 21 (GET_DEVICE_INFO), 31 (ENUM_ADAPTERS), 32 (OPEN_ADAPTER), 37 (GET_HW_STATE)  
**Test Cases**: 19 (UT-DEV-* and UT-DEV-LIFECYCLE-*)  
**Priority**: P0 (Critical)  

**Key Test Scenarios**:
1. First-time device initialization (IOCTL 20)
2. Duplicate initialization prevention
3. Device information retrieval (IOCTL 21) - MAC, Device ID, firmware
4. Device info before initialization (graceful degradation)
5. Single adapter enumeration (IOCTL 31)
6. Multiple adapter enumeration
7. Enumeration with no adapters (edge case)
8. Open adapter by index (IOCTL 32)
9. Open adapter by device path
10. Invalid adapter index rejection
11. Concurrent open requests (shared vs. exclusive mode)
12. Hardware state retrieval in D0 (IOCTL 37)
13. Hardware state during D3 transition
14. Link state detection (up/down transitions)
15. Resource allocation status verification
16. Full lifecycle sequence (init → open → use → close)
17. Recovery from failed initialization
18. Hot-plug device detection
19. Graceful shutdown and PnP remove/re-add

**Expected Duration**: 3 hours (includes PnP event testing)

---

### 4.3 Test #314: Timestamp Event Subscription

**Test Executable**: `test_ts_event_sub.exe`  
**Source**: `tests/test_ioctl_ts_event_sub.c`  
**IOCTLs**: 33 (SUBSCRIBE_TS_EVENTS), 34 (MAP_TS_RING_BUFFER)  
**Test Cases**: 19 (UT-TS-SUB-*, UT-TS-RING-*, UT-TS-EVENT-*, UT-TS-PERF-*, UT-TS-ERROR-*)  

**Key Test Scenarios**:
1. Basic event subscription (IOCTL 33) - RX, TX, TARGET_TIME
2. Selective event type subscription (filtering)
3. Multiple concurrent subscriptions
4. Unsubscribe operation (clean resource release)
5. Ring buffer mapping (IOCTL 34) - shared memory access
6. Ring buffer size negotiation (4KB, 16KB)
7. Ring buffer wraparound behavior (circular buffer)
8. Lock-free read synchronization (user-mode reader, kernel writer)
9. RX timestamp event delivery
10. TX timestamp event delivery
11. Target time reached event
12. Auxiliary timestamp event
13. Event sequence numbering (monotonic, wraparound)
14. Event filtering by criteria (queue-specific)
15. Ring buffer unmap operation
16. High event rate performance (10K events/sec)
17. Invalid subscription handle rejection
18. Ring buffer allocation failure (insufficient memory)
19. Event overflow notification (buffer full)

**Expected Duration**: 3 hours (includes performance and stress scenarios)

---

## 5. Test Data and Scenarios

### 5.1 Test Data Requirements

**MDIO/PHY Tests**:
- PHY register map documentation (I210/I225/I226 datasheet)
- Known-good register values (Control=0x1140, Status=0x796D typical)
- Test patterns: 0xA5A5, 0x5A5A, 0xFFFF, 0x0000

**Device Lifecycle Tests**:
- Valid device paths: `\\.\IntelAvbFilter0`, `\\Device\\IntelAvbFilter0`
- Expected Device IDs: 0x15F2 (I225), 0x15F3 (I225-LM), 0x125C (I226)
- Expected Vendor ID: 0x8086 (Intel)

**Timestamp Event Tests**:
- Ring buffer sizes: 4KB, 8KB, 16KB
- Event types: RX_TIMESTAMP, TX_TIMESTAMP, TARGET_TIME, AUX_TIMESTAMP
- Test event rate: 1 event/sec (baseline), 100 events/sec, 10K events/sec (stress)

### 5.2 Error Injection Scenarios

- **MDIO Tests**: Simulate PHY unresponsive (timeout), invalid addresses
- **Lifecycle Tests**: Force D3 transition, remove device during operation
- **Event Tests**: Allocate large buffers (trigger OOM), generate overflow

---

## 6. Defect Management

### 6.1 Defect Reporting

**Critical (P0)** - System crash, BSOD, data corruption:
- Immediate halt of testing
- Create GitHub issue with `priority:p0` label
- Attach WinDbg dump and repro steps

**High (P1)** - Feature not working, incorrect behavior:
- Document failure in test log
- Create GitHub issue with `priority:p1` label
- Continue with remaining tests

**Medium (P2)** - Minor issues, edge cases:
- Log warning in test output
- Create GitHub issue with `priority:p2` label
- Defer fix to future iteration

### 6.2 Defect Tracking Template

```markdown
## Test Failure: [Test Case ID]

**Test**: [Test executable and case number]
**Issue**: #[requirement issue number]
**Expected**: [Expected behavior]
**Actual**: [Actual behavior]
**Severity**: P0/P1/P2
**Reproducible**: Always / Intermittent / Once

### Repro Steps:
1. [Step 1]
2. [Step 2]
...

### Logs:
[Attach test output, driver logs, WinDbg dump]
```

---

## 7. Risks and Mitigations

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| PHY register writes cause link failure | Medium | High | Use non-critical registers; implement rollback |
| Device removal during test causes BSOD | Low | Critical | Verify PnP handling; test in VM first |
| Ring buffer memory exhaustion | Medium | Medium | Implement resource limits; test cleanup |
| Hardware-specific failures (I210 vs I225) | High | Medium | Test on multiple adapter models |
| Concurrent access race conditions | Medium | High | Enable Driver Verifier; run with IRQL checks |

---

## 8. Resource Requirements

### 8.1 Personnel

- **Test Engineer**: 1 person, 4 days
- **Driver Developer**: 0.5 person (support for failures)
- **Reviewer**: 1 person, 0.5 day (results review)

### 8.2 Equipment

- Test PC with Windows 10/11 (x64)
- Intel I210/I225/I226 adapters (minimum 1, recommend 3)
- Network switch (for link state tests)
- Optional: Logic analyzer (MDIO bus verification)

### 8.3 Schedule

| Day | Activity | Deliverable |
|-----|----------|-------------|
| 1 | Build verification, environment setup | All tests compile successfully |
| 2 | Execute Test #312 (MDIO/PHY) | Test results logged |
| 2-3 | Execute Test #313 (Device Lifecycle) | Test results logged |
| 3 | Execute Test #314 (Timestamp Events) | Test results logged |
| 4 | Full regression, defect triage | Test report, GitHub issues |

---

## 9. Test Deliverables

### 9.1 Test Artifacts

- ✅ Test source files: `tests/test_ioctl_*.c`
- ✅ Test executables: `build/tools/avb_test/x64/Debug/*.exe`
- ✅ Test logs: `07-verification-validation/test-results/TEST-RUN-*.log`
- ✅ Test report: `07-verification-validation/test-reports/TEST-REPORT-*.md`
- ✅ Traceability matrix: Updated in Issue #14

### 9.2 Test Report Template

```markdown
# Test Execution Report: [Date]

## Summary
- **Test Plan**: TEST-PLAN-IOCTL-NEW-2025-12-31
- **Configuration**: Debug/Release
- **Test Engineer**: [Name]
- **Execution Date**: [Date]

## Results
- **Total Test Cases**: 53
- **Passed**: X
- **Failed**: Y
- **Skipped**: Z
- **Pass Rate**: XX%

## Test Details
### Test #312: MDIO/PHY (15 cases)
- Passed: X/15
- Failed: [List failures]

### Test #313: Device Lifecycle (19 cases)
- Passed: X/19
- Failed: [List failures]

### Test #314: Timestamp Events (19 cases)
- Passed: X/19
- Failed: [List failures]

## Defects Found
- [Link to GitHub issues]

## Recommendations
- [Next steps, improvements]
```

---

## 10. Approvals

| Role | Name | Signature | Date |
|------|------|-----------|------|
| Test Plan Author | [TBD] | | 2025-12-31 |
| Technical Reviewer | [TBD] | | |
| Test Manager | [TBD] | | |

---

**Revision History**:
- v1.0 (2025-12-31): Initial test plan created for Issues #312, #313, #314
