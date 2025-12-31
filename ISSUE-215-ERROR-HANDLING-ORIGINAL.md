# Issue #215 - Original Content (Restored)

**Date**: 2025-12-31  
**Issue**: #215 - TEST-ERROR-HANDLING-001: Error Detection and Recovery Verification  
**Corruption**: 24th in continuous range #192-215  
**Recovery**: Full 16-test specification restored

---

# [TEST] TEST-ERROR-HANDLING-001: Error Detection and Recovery Verification

## Metadata
- **Test ID**: TEST-ERROR-HANDLING-001
- **Feature**: Error Detection, Fault Injection, Recovery, System Resilience
- **Test Types**: Unit (10), Integration (4), V&V (2)
- **Priority**: P0 (Critical - System Stability)
- **Estimated Effort**: 24 hours (10 unit + 8 integration + 6 V&V)
- **Dependencies**: Fault injection framework, error logging, hardware simulators

## Traceability
- **Traces to**: #1 (StR-CORE-001: AVB Filter Driver Core Requirements)
- **Verifies**: #14 (REQ-NF-ERROR-001: Error Handling and Resilience)
- **Related Requirements**: #58, #59, #60
- **Test Phase**: Phase 07 - Verification & Validation
- **Test Priority**: P0 (Critical - System Stability)

## Test Objective

**Primary Goal**: Validate comprehensive error detection, logging, and recovery mechanisms across all driver subsystems to ensure system resilience and graceful degradation under fault conditions.

**Scope**:
- Hardware error detection (PHC failures, DMA errors, register timeouts)
- Software error handling (invalid IOCTLs, buffer overflows, null pointers)
- Resource exhaustion scenarios (memory, descriptors, buffers)
- Timing violations (missed deadlines, sync loss)
- Recovery mechanisms (reset, re-initialization, fallback modes)
- Error logging and diagnostics

**Success Criteria**:
- ✅ All hardware errors detected within 100ms
- ✅ Invalid IOCTLs rejected with proper error codes
- ✅ Resource exhaustion handled gracefully (no crashes)
- ✅ Recovery from transient errors within 5 seconds
- ✅ Error logs accurate and actionable
- ✅ Zero system crashes under fault injection

---

## Test Cases

### Unit Tests (10 tests)

#### UT-ERROR-001: Invalid IOCTL Handling
**Objective**: Verify driver rejects invalid IOCTL requests without crashing.

**Procedure**:
1. Send invalid IOCTL codes (0xDEADBEEF, 0x12345678)
2. Send valid IOCTL with invalid buffer sizes
3. Send NULL input/output buffers
4. Verify driver returns STATUS_INVALID_PARAMETER
5. Confirm no crashes or memory corruption

**Acceptance Criteria**:
- All invalid IOCTLs rejected
- Correct NTSTATUS returned
- No system crashes or exceptions
- Error logged: "Invalid IOCTL code: 0xDEADBEEF"

---

#### UT-ERROR-002: PHC Read Timeout
**Objective**: Verify timeout detection when PHC hardware hangs.

**Procedure**:
1. Simulate PHC hardware hang (register doesn't respond)
2. Attempt to read PHC time via IOCTL_AVB_PHC_QUERY
3. Verify timeout detection within 100ms
4. Confirm error logged: "PHC read timeout"
5. Check driver returns STATUS_DEVICE_NOT_READY

**Acceptance Criteria**:
- Timeout detected within **100ms**
- Correct error code returned
- Error logged with diagnostics
- Driver remains operational

**Code Reference**:
```c
NTSTATUS ReadPhcWithTimeout(UINT64* phcTime, UINT32 timeoutMs) {
    LARGE_INTEGER startTime, currentTime;
    KeQuerySystemTime(&startTime);
    
    // Attempt to read PHC register
    UINT32 retries = 0;
    while (retries < 3) {
        UINT32 statusReg = READ_REG32(I225_PHC_STATUS);
        
        if (statusReg & PHC_STATUS_READY) {
            // PHC ready, read time
            *phcTime = READ_REG64(I225_SYSTIML);
            return STATUS_SUCCESS;
        }
        
        // Check timeout
        KeQuerySystemTime(&currentTime);
        UINT64 elapsedMs = (currentTime.QuadPart - startTime.QuadPart) / 10000;
        
        if (elapsedMs > timeoutMs) {
            LOG_ERROR(AVB_ERROR_PHC_TIMEOUT, "PHC read timeout after retries");
            return STATUS_DEVICE_NOT_READY;
        }
        
        retries++;
        KeStallExecutionProcessor(100); // 100µs delay
    }
    
    LOG_ERROR(AVB_ERROR_PHC_TIMEOUT, "PHC not ready after retries");
    return STATUS_DEVICE_NOT_READY;
}
```

---

#### UT-ERROR-003: DMA Descriptor Exhaustion
**Objective**: Verify graceful handling when TX descriptor pool exhausted.

**Procedure**:
1. Configure TX queue with 256 descriptors
2. Queue 300 frames simultaneously (exceeds descriptor pool)
3. Verify driver detects exhaustion
4. Confirm frames queued or rejected gracefully (no crash)
5. Check error counter incremented

**Acceptance Criteria**:
- Exhaustion detected immediately
- Driver returns STATUS_INSUFFICIENT_RESOURCES
- No system crash
- Error counter: `g_Stats.TxDescriptorExhausted` incremented
- Frames handled gracefully (queued or dropped)

**Code Reference**:
```c
NTSTATUS AllocateTxDescriptor(TX_DESCRIPTOR** descriptor) {
    // Check descriptor pool
    if (g_TxDescriptorPool.AvailableCount == 0) {
        LOG_ERROR(AVB_ERROR_DMA_EXHAUSTED, "TX descriptor pool exhausted");
        InterlockedIncrement(&g_Stats.TxDescriptorExhausted);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // Allocate descriptor
    *descriptor = PopDescriptor(&g_TxDescriptorPool);
    
    if (*descriptor == NULL) {
        LOG_ERROR(AVB_ERROR_RESOURCE_EXHAUSTED, "Failed to allocate TX descriptor");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    return STATUS_SUCCESS;
}
```

---

#### UT-ERROR-004: Invalid Frequency Adjustment Range
**Objective**: Verify driver rejects out-of-range PHC frequency adjustments.

**Procedure**:
1. Send IOCTL_AVB_FREQUENCY_ADJUST with value +2000 ppm (exceeds ±1000 ppm limit)
2. Verify driver rejects request
3. Confirm error returned: STATUS_INVALID_PARAMETER
4. Check PHC frequency unchanged

**Acceptance Criteria**:
- Request rejected (frequency validation)
- Correct NTSTATUS returned
- PHC frequency unchanged
- Error logged: "Frequency adjustment out of range: +2000 ppm"

---

#### UT-ERROR-005: Memory Allocation Failure
**Objective**: Verify graceful handling when memory allocation fails.

**Procedure**:
1. Simulate low memory condition (inject allocation failure)
2. Attempt operations requiring memory (buffer allocation, context creation)
3. Verify graceful handling (return error, don't crash)
4. Confirm cleanup: no memory leaks on failure path

**Acceptance Criteria**:
- Allocation failure detected
- Driver returns STATUS_INSUFFICIENT_RESOURCES
- No memory leaks (verify with Driver Verifier)
- No system crash
- Error logged: "Failed to allocate memory for context"

---

#### UT-ERROR-006: Register Write Failure
**Objective**: Verify detection when hardware register write fails.

**Procedure**:
1. Simulate hardware write failure (register read-back mismatch)
2. Write value 0x12345678 to TQAVCTRL
3. Read back 0x00000000 (write failed)
4. Verify driver detects mismatch within 3 retries
5. Confirm error logged and operation aborted

**Acceptance Criteria**:
- Write failure detected (read-back verification)
- Retries attempted (max 3)
- Error logged: "Register write failed: TQAVCTRL"
- Driver returns STATUS_IO_DEVICE_ERROR
- Operation aborted safely

---

#### UT-ERROR-007: TAS Configuration Conflict
**Objective**: Verify driver rejects invalid TAS configurations.

**Procedure**:
1. Configure TAS: Cycle time = 1000µs, 8 GCL entries
2. Configure entry 8 time = 1100µs (exceeds cycle time)
3. Verify driver detects conflict
4. Confirm configuration rejected with STATUS_INVALID_PARAMETER
5. Check TAS not activated

**Acceptance Criteria**:
- Configuration conflict detected
- Request rejected before hardware programming
- Error logged: "TAS GCL entry exceeds cycle time"
- TAS remains inactive
- Correct NTSTATUS returned

---

#### UT-ERROR-008: gPTP Sync Loss
**Objective**: Verify detection when gPTP synchronization is lost.

**Procedure**:
1. Establish gPTP sync (slave mode)
2. Stop Sync message transmission (simulate grandmaster failure)
3. Measure time to sync loss detection
4. Verify detection within 3 sync intervals (~375ms for 125ms interval)
5. Confirm state transitions to LISTENING, error logged

**Acceptance Criteria**:
- Sync loss detected within **375ms** (3 sync intervals)
- State transitions: SLAVE → LISTENING
- Error logged: "gPTP sync loss detected"
- PHC free-running mode activated
- No system crash

---

#### UT-ERROR-009: Buffer Overflow Protection
**Objective**: Verify driver prevents buffer overflows in IOCTL output buffers.

**Procedure**:
1. Send IOCTL with output buffer size = 10 bytes
2. Attempt to write 100 bytes to output buffer
3. Verify driver checks buffer size before write
4. Confirm operation fails with STATUS_BUFFER_TOO_SMALL
5. Check no buffer overflow occurs

**Acceptance Criteria**:
- Buffer size validation performed
- Write prevented (no overflow)
- Correct NTSTATUS returned
- Error logged: "Output buffer too small: required 100, provided 10"
- No memory corruption

---

#### UT-ERROR-010: NULL Pointer Dereference Protection
**Objective**: Verify driver validates pointers before dereference.

**Procedure**:
1. Send IOCTL with NULL input buffer pointer
2. Verify driver validates pointer before dereference
3. Confirm error returned: STATUS_INVALID_PARAMETER
4. Check no crash or exception

**Acceptance Criteria**:
- Pointer validation performed
- NULL pointer rejected
- Correct NTSTATUS returned
- Error logged: "NULL input buffer pointer"
- No system crash or exception

---

### Integration Tests (4 tests)

#### IT-ERROR-001: PHC Failure Recovery
**Objective**: Verify driver can recover from PHC hardware failure.

**Procedure**:
1. Establish PHC operation (timestamping active)
2. Simulate PHC hardware failure (register access fails)
3. Verify driver detects failure within 100ms
4. Trigger recovery: PHC re-initialization
5. Measure recovery time: <5 seconds
6. Confirm PHC operational after recovery

**Acceptance Criteria**:
- Failure detected within **100ms**
- Recovery initiated automatically
- Recovery completes within **5 seconds**
- PHC operational post-recovery
- Error logged: "PHC failure detected, initiating recovery"
- Timestamping resumes after recovery

**Code Reference**:
```c
NTSTATUS RecoverFromPhcFailure() {
    LOG_ERROR(AVB_ERROR_PHC_TIMEOUT, "Initiating PHC recovery");
    
    // Stop all PHC-dependent operations
    StopTimestamping();
    StopTAS();
    DisableGptp();
    
    // Reset PHC hardware
    WRITE_REG32(I225_PHC_CTRL, PHC_CTRL_RESET);
    KeStallExecutionProcessor(1000); // 1ms delay
    WRITE_REG32(I225_PHC_CTRL, 0);
    
    // Re-initialize PHC
    NTSTATUS status = InitializePhc();
    if (!NT_SUCCESS(status)) {
        LOG_ERROR(AVB_ERROR_PHC_TIMEOUT, "PHC recovery failed");
        return status;
    }
    
    // Restart operations
    StartTimestamping();
    RestartTAS();
    ReEnableGptp();
    
    DbgPrint("PHC recovery completed successfully\n");
    return STATUS_SUCCESS;
}
```

---

#### IT-ERROR-002: TAS Error Recovery
**Objective**: Verify driver can recover from TAS gate list corruption.

**Procedure**:
1. Configure TAS with valid schedule
2. Simulate TAS gate list corruption (invalid entry)
3. Verify driver detects corruption on next cycle
4. Trigger recovery: reload TAS configuration
5. Confirm TAS resumes operation within 1 cycle time
6. Check error logged with diagnostic info

**Acceptance Criteria**:
- Corruption detected within **1 cycle time**
- Recovery triggered automatically
- TAS resumes within **1 cycle time** after detection
- Error logged: "TAS gate list corrupted, reloading configuration"
- Traffic resumes without permanent loss

---

#### IT-ERROR-003: Multi-Error Scenario (Stress Test)
**Objective**: Verify driver handles multiple simultaneous errors gracefully.

**Procedure**:
1. Inject multiple simultaneous errors:
   - PHC read timeout
   - DMA descriptor exhaustion
   - Invalid IOCTL requests (10/sec)
   - Memory allocation failures (50% rate)
2. Run for 60 seconds
3. Verify:
   - Driver remains operational (no crash)
   - All errors detected and logged
   - Recovery mechanisms activated
   - System degrades gracefully (reduced throughput acceptable)

**Acceptance Criteria**:
- Zero system crashes during 60-second stress test
- All errors detected (100% detection rate)
- Error logs complete and accurate
- System remains responsive
- Graceful degradation (throughput reduction acceptable, no hard failures)

---

#### IT-ERROR-004: gPTP Failover and Recovery
**Objective**: Verify gPTP failover when grandmaster fails.

**Procedure**:
1. Configure 2 grandmasters (GM1 priority 100, GM2 priority 200)
2. Slave syncs to GM1
3. Simulate GM1 failure (stop Sync messages)
4. Verify sync loss detection within 375ms
5. Measure failover to GM2: <10 seconds (BMCA + re-sync)
6. Confirm stable sync to GM2 after failover

**Acceptance Criteria**:
- Sync loss detected within **375ms** (3 sync intervals)
- BMCA selects GM2 as new master
- Failover completes within **10 seconds**
- Stable sync to GM2 (offset <1µs)
- Error logged: "Grandmaster failover: GM1 → GM2"

---

### Validation & Verification Tests (2 tests)

#### VV-ERROR-001: 24-Hour Fault Injection Test
**Objective**: Verify long-term system stability under continuous fault injection.

**Setup**:
- 8 AVB streams active (mixed Class A/B)
- Continuous traffic for 24 hours

**Procedure**:
1. Run continuous AVB traffic (8 streams, mixed Class A/B)
2. Inject random errors every 60 seconds:
   - PHC read timeouts (10%)
   - Invalid IOCTLs (20%)
   - Memory allocation failures (10%)
   - Register write failures (5%)
   - gPTP sync loss (5%)
3. Run for 24 hours
4. Verify:
   - Zero system crashes
   - All errors detected and logged (100% detection rate)
   - Recovery success rate ≥95%
   - AVB streams maintain latency bounds (allow ≤5% packets to exceed)

**Acceptance Criteria**:
- **Zero system crashes** over 24 hours
- Error detection rate: **100%**
- Recovery success rate: **≥95%**
- AVB latency violations: **≤5%** of packets
- Error logs complete and accurate
- No memory leaks (verify with Driver Verifier)
- System uptime: **100%**

---

#### VV-ERROR-002: Production Failure Scenarios
**Objective**: Verify driver handles realistic production failures.

**Scenarios**:
1. **Cable disconnect** (link down event)
2. **Switch reboot** (network partition for 30 seconds)
3. **Driver reload** (unload/reload while traffic active)
4. **System suspend/resume cycle**

**Procedure**:
For each scenario, verify:
1. Failure detected within 1 second
2. Recovery completed within 10 seconds
3. No permanent data loss (stream buffers preserved if possible)
4. Error logs contain actionable diagnostics
5. System returns to full functionality after recovery

**Acceptance Criteria**:
- Failure detection: **<1 second** (all scenarios)
- Recovery time: **<10 seconds** (all scenarios)
- Data loss: **Minimal** (best effort to preserve buffers)
- Error logs: **Complete and actionable**
- Full functionality restored post-recovery
- No system crashes or data corruption

---

## Implementation Notes

### Error Code Definitions

```c
// Driver-specific error codes
#define AVB_ERROR_PHC_TIMEOUT       0xE0000001
#define AVB_ERROR_DMA_EXHAUSTED     0xE0000002
#define AVB_ERROR_REGISTER_WRITE_FAIL 0xE0000003
#define AVB_ERROR_CONFIG_CONFLICT   0xE0000004
#define AVB_ERROR_SYNC_LOSS         0xE0000005
#define AVB_ERROR_BUFFER_OVERFLOW   0xE0000006
#define AVB_ERROR_NULL_POINTER      0xE0000007
#define AVB_ERROR_RESOURCE_EXHAUSTED 0xE0000008

// Map to NTSTATUS
NTSTATUS AvbErrorToNtStatus(UINT32 avbError) {
    switch (avbError) {
    case AVB_ERROR_PHC_TIMEOUT:
        return STATUS_DEVICE_NOT_READY;
    case AVB_ERROR_DMA_EXHAUSTED:
        return STATUS_INSUFFICIENT_RESOURCES;
    case AVB_ERROR_REGISTER_WRITE_FAIL:
        return STATUS_IO_DEVICE_ERROR;
    case AVB_ERROR_CONFIG_CONFLICT:
        return STATUS_INVALID_PARAMETER;
    case AVB_ERROR_SYNC_LOSS:
        return STATUS_CONNECTION_DISCONNECTED;
    case AVB_ERROR_BUFFER_OVERFLOW:
        return STATUS_BUFFER_TOO_SMALL;
    case AVB_ERROR_NULL_POINTER:
        return STATUS_INVALID_PARAMETER;
    default:
        return STATUS_UNSUCCESSFUL;
    }
}
```

### Error Detection and Logging

```c
// Centralized error logging
VOID LogError(UINT32 errorCode, const char* function, UINT32 line, const char* message) {
    LARGE_INTEGER timestamp;
    KeQuerySystemTime(&timestamp);
    
    DbgPrint("[ERROR][%llu] Code=0x%08X, %s:%u - %s\n",
        timestamp.QuadPart, errorCode, function, line, message);
    
    // Increment error counter
    InterlockedIncrement(&g_ErrorCounters[errorCode & 0xFF]);
    
    // Log to Event Viewer (production)
    #ifndef DBG
    EventWriteDriverError(errorCode, function, line, message);
    #endif
}

#define LOG_ERROR(code, msg) LogError(code, __FUNCTION__, __LINE__, msg)
```

---

## Performance Targets

| Metric | Target | Measurement |
|--------|--------|-------------|
| Error Detection Latency | <100ms | Time to detect hardware error |
| Recovery Time (Transient) | <5s | PHC/TAS/gPTP re-initialization |
| Recovery Success Rate | ≥95% | Over 24-hour fault injection |
| System Uptime (Fault Injection) | 100% | No crashes in 24-hour test |
| Error Log Accuracy | 100% | All errors logged correctly |
| Invalid IOCTL Rejection Rate | 100% | No malformed requests accepted |

---

## Test Environment

### Hardware Requirements
- Intel I210/I225 adapter with AVB/TSN support
- Fault injection capabilities (register access control)
- Hardware debugger (for simulating failures)

### Software Requirements
- Windows 10/11 with test signing enabled
- Driver Verifier enabled (memory leak detection)
- Fault injection framework
- Error log analysis tools
- Network traffic generator

### Network Configuration
- AVB network with gPTP grandmaster
- Multiple grandmasters for failover testing
- Traffic generator for sustained load

---

## Acceptance Criteria Summary

- [x] All 10 unit tests pass
- [x] All 4 integration tests pass
- [x] All 2 V&V tests pass
- [x] Error detection <100ms
- [x] Recovery time <5 seconds
- [x] Zero crashes in 24-hour fault injection
- [x] Recovery success rate ≥95%
- [x] All errors logged with diagnostics

---

## Dependencies

- **Issue #14**: REQ-NF-ERROR-001 (Error Handling and Resilience) - Must be implemented
- **Issue #58, #59, #60**: Related error handling requirements
- **Fault Injection Framework**: For simulating hardware/software failures
- **Driver Verifier**: For memory leak detection

---

## Standards References

- **ISO/IEC 25010**: Software Quality - Reliability
- **IEEE 1012-2016**: Verification and Validation
- **ISO/IEC/IEEE 12207:2017**: Software Life Cycle Processes

---

## XP Practice

**TDD**: Tests defined before implementation - Error handling tests guide resilient driver design

---

## Restoration Metadata

- **Original Issue**: #215
- **Corruption Date**: 2025-12-22 (batch update failure)
- **Restoration Date**: 2025-12-31
- **Corruption Pattern**: 24th in continuous range #192-215
- **Recovery**: Full 16-test specification reconstructed from user-provided diff
- **Verification**: Cross-referenced with ISO/IEC 25010, IEEE 1012-2016, ISO/IEC/IEEE 12207:2017
