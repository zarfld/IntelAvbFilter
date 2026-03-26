# ISSUE-231-ERROR-RECOVERY-ORIGINAL.md

**Creation Date**: 2025-12-31
**Issue Number**: #231
**Original Content**: TEST-ERROR-RECOVERY-001: Error Recovery and Fault Tolerance Testing

---

# TEST-ERROR-RECOVERY-001: Error Recovery and Fault Tolerance Testing

## 🔗 Traceability
- Traces to: #1 (StR-CORE-001: AVB Filter Driver Core Requirements)
- Verifies: #59 (REQ-NF-PERFORMANCE-001: Performance and Scalability), #63 (REQ-NF-SECURITY-001: Security)
- Related Requirements: #14, #37, #38, #60
- Test Phase: Phase 07 - Verification & Validation
- Test Priority: P0 (Critical - Resilience)

## 📋 Test Objective

Validate driver error recovery mechanisms under fault injection scenarios including hardware failures, transient errors, resource exhaustion, invalid inputs, and concurrent failures. Ensure graceful degradation, rapid recovery (<5s), and system stability.

## 🧪 Test Coverage

### 10 Unit Tests

1. **PHC hardware failure recovery** (simulate read/write failure, verify fallback to system clock)
2. **Link down recovery** (disconnect cable, reconnect, verify PHC resync <30s)
3. **Memory allocation failure** (inject allocation failures, verify graceful degradation)
4. **DMA buffer error recovery** (corrupt DMA buffer, verify detection and reset)
5. **Interrupt storm handling** (flood with interrupts, verify throttling and stability)
6. **Invalid IOCTL recovery** (send malformed IOCTLs, verify driver remains stable)
7. **Concurrent error handling** (multiple simultaneous failures, verify prioritized recovery)
8. **Timeout recovery** (induce hardware timeouts, verify retry logic and fallback)
9. **Register corruption detection** (verify checksums, detect silent corruption, trigger reset)
10. **Watchdog timeout handling** (simulate hung ISR/DPC, verify watchdog recovery)

### 3 Integration Tests

1. **Cascading failure recovery** (PHC failure → TAS disabled → Class A rerouted to best-effort, <5s recovery)
2. **Resource exhaustion under load** (1 Gbps traffic + memory pressure, verify allocation throttling)
3. **Power failure simulation** (abrupt D0→D3 transition, verify state preservation and recovery)

### 2 V&V Tests

1. **24-hour fault injection** (random failures every 5-10 minutes, system remains stable, zero crashes)
2. **Production failure scenarios** (replicate real-world failures: cable disconnect, switch reboot, grandmaster loss)

## 🔧 Implementation Notes

### Error Recovery State Machine

```c
typedef enum _RECOVERY_STATE {
    RECOVERY_NORMAL,        // No errors
    RECOVERY_DETECTING,     // Error detected, analyzing
    RECOVERY_IN_PROGRESS,   // Actively recovering
    RECOVERY_DEGRADED,      // Operating in degraded mode
    RECOVERY_FAILED         // Recovery failed, offline
} RECOVERY_STATE;

typedef struct _ERROR_RECOVERY_CONTEXT {
    RECOVERY_STATE State;
    LARGE_INTEGER ErrorTimestamp;   // When error first detected
    UINT32 ErrorCode;               // Last error code
    UINT32 RetryCount;              // Number of recovery attempts
    UINT32 MaxRetries;              // Max attempts before giving up
    BOOLEAN DegradedMode;           // Operating in degraded mode
    CHAR LastErrorMessage[256];     // Human-readable error
} ERROR_RECOVERY_CONTEXT;

NTSTATUS HandlePhcFailure(ADAPTER_CONTEXT* adapter, ERROR_RECOVERY_CONTEXT* recovery) {
    DbgPrint("PHC failure detected, initiating recovery...\n");
    
    KeQueryPerformanceCounter(&recovery->ErrorTimestamp);
    recovery->State = RECOVERY_IN_PROGRESS;
    recovery->ErrorCode = ERROR_PHC_HARDWARE_FAILURE;
    
    // Step 1: Attempt hardware reset
    NTSTATUS status = ResetPhcHardware(adapter);
    
    if (NT_SUCCESS(status)) {
        // Verify PHC operational
        PHC_TIME time;
        status = PhcReadTime(adapter, &time);
        
        if (NT_SUCCESS(status)) {
            DbgPrint("  ✓ PHC recovered via hardware reset\n");
            recovery->State = RECOVERY_NORMAL;
            recovery->RetryCount = 0;
            return STATUS_SUCCESS;
        }
    }
    
    // Step 2: Fallback to system clock
    DbgPrint("  ⚠ PHC unrecoverable, falling back to system clock\n");
    
    adapter->PhcMode = PHC_MODE_SYSTEM_CLOCK_FALLBACK;
    recovery->State = RECOVERY_DEGRADED;
    recovery->DegradedMode = TRUE;
    
    snprintf(recovery->LastErrorMessage, sizeof(recovery->LastErrorMessage),
             "PHC hardware failure, using system clock (accuracy degraded)");
    
    // Disable TSN features that require PHC
    DisableTasScheduling(adapter);
    DisableLaunchTimeOffload(adapter);
    
    // Notify user via event log
    EventWritePhcFallback(adapter->PortNumber, recovery->LastErrorMessage);
    
    return STATUS_SUCCESS;  // Operating in degraded mode
}
```

### Fault Injection Framework

```c
#ifdef DBG  // Debug builds only

typedef enum _FAULT_TYPE {
    FAULT_MEMORY_ALLOC,     // Fail ExAllocatePoolWithTag
    FAULT_REGISTER_READ,    // Fail READ_REG32
    FAULT_REGISTER_WRITE,   // Fail WRITE_REG32
    FAULT_DMA_ALLOC,        // Fail DMA buffer allocation
    FAULT_INTERRUPT,        // Inject spurious interrupt
    FAULT_LINK_DOWN,        // Simulate link down
    FAULT_TIMEOUT           // Simulate hardware timeout
} FAULT_TYPE;

typedef struct _FAULT_INJECTOR {
    BOOLEAN Enabled;
    FAULT_TYPE FaultType;
    UINT32 TriggerCount;    // Fail every N operations
    UINT32 CurrentCount;
    UINT32 InjectionCount;  // Total faults injected
} FAULT_INJECTOR;

FAULT_INJECTOR g_FaultInjector = {FALSE, FAULT_MEMORY_ALLOC, 10, 0, 0};

PVOID FaultInjected_ExAllocatePoolWithTag(POOL_TYPE poolType, SIZE_T size, ULONG tag) {
    if (g_FaultInjector.Enabled && g_FaultInjector.FaultType == FAULT_MEMORY_ALLOC) {
        g_FaultInjector.CurrentCount++;
        
        if (g_FaultInjector.CurrentCount >= g_FaultInjector.TriggerCount) {
            g_FaultInjector.CurrentCount = 0;
            g_FaultInjector.InjectionCount++;
            
            DbgPrint("*** FAULT INJECTION: Memory allocation failure #%u ***\n",
                     g_FaultInjector.InjectionCount);
            
            return NULL;  // Simulate allocation failure
        }
    }
    
    return ExAllocatePoolWithTag(poolType, size, tag);
}

#define ExAllocatePoolWithTag(poolType, size, tag) \
    FaultInjected_ExAllocatePoolWithTag(poolType, size, tag)

#endif  // DBG
```

### Recovery Validation Test

```python
#!/usr/bin/env python3
"""
Error Recovery Fault Injection Test
Validates driver recovery under simulated failures.
"""
import subprocess
import time
import random

class FaultInjectionTest:
    def __init__(self):
        self.fault_types = [
            'FAULT_MEMORY_ALLOC',
            'FAULT_REGISTER_READ',
            'FAULT_LINK_DOWN',
            'FAULT_DMA_ALLOC',
            'FAULT_TIMEOUT'
        ]
        
        self.results = []
    
    def inject_fault(self, fault_type, trigger_count=10):
        """
        Inject fault via IOCTL (requires debug driver build).
        """
        print(f"Injecting fault: {fault_type} (trigger every {trigger_count} ops)")
        
        # Send IOCTL to enable fault injection
        cmd = [
            'FaultInjectorTool.exe',
            '/enable',
            f'/type:{fault_type}',
            f'/trigger:{trigger_count}'
        ]
        
        result = subprocess.run(cmd, capture_output=True, text=True)
        
        if result.returncode != 0:
            print(f"  ✗ Fault injection failed: {result.stderr}")
            return False
        
        print(f"  ✓ Fault injection enabled")
        return True
    
    def monitor_recovery(self, timeout=30):
        """
        Monitor driver for error and recovery.
        """
        print(f"  Monitoring recovery (timeout: {timeout}s)...")
        
        start_time = time.time()
        error_detected = False
        recovered = False
        
        while time.time() - start_time < timeout:
            # Query driver status
            status = self.query_driver_status()
            
            if status['error_state']:
                error_detected = True
                print(f"  ⚠ Error detected: {status['error_message']}")
            
            if error_detected and status['state'] == 'RECOVERY_NORMAL':
                recovered = True
                recovery_time = time.time() - start_time
                print(f"  ✓ Recovered in {recovery_time:.2f}s")
                break
            
            if error_detected and status['state'] == 'RECOVERY_DEGRADED':
                recovered = True
                recovery_time = time.time() - start_time
                print(f"  ⚠ Recovered to degraded mode in {recovery_time:.2f}s")
                break
            
            time.sleep(0.5)
        
        if not error_detected:
            print(f"  ✗ Error NOT detected (fault injection may have failed)")
            return False
        
        if not recovered:
            print(f"  ✗ Recovery FAILED (timeout after {timeout}s)")
            return False
        
        # Check recovery time (<5s requirement)
        if recovery_time > 5:
            print(f"  ✗ Recovery too slow ({recovery_time:.2f}s > 5s)")
            return False
        
        return True
    
    def disable_fault_injection(self):
        """
        Disable fault injection after test.
        """
        cmd = ['FaultInjectorTool.exe', '/disable']
        subprocess.run(cmd, capture_output=True)
    
    def test_fault_type(self, fault_type):
        """
        Test recovery for specific fault type.
        """
        print(f"\n=== Testing {fault_type} ===")
        
        # Inject fault
        if not self.inject_fault(fault_type):
            return False
        
        # Trigger operations to activate fault
        self.trigger_operations(fault_type)
        
        # Monitor recovery
        success = self.monitor_recovery(timeout=30)
        
        # Disable fault injection
        self.disable_fault_injection()
        
        # Record result
        self.results.append({
            'fault_type': fault_type,
            'success': success
        })
        
        return success
    
    def trigger_operations(self, fault_type):
        """
        Trigger operations that will hit the injected fault.
        """
        if fault_type == 'FAULT_MEMORY_ALLOC':
            # Trigger allocations
            for i in range(20):
                cmd = ['DriverStressTool.exe', '/allocate', '/size:1024']
                subprocess.run(cmd, capture_output=True)
        
        elif fault_type == 'FAULT_LINK_DOWN':
            # Simulate link down
            cmd = ['DriverStressTool.exe', '/simulate-link-down']
            subprocess.run(cmd, capture_output=True)
        
        # ... other fault types
    
    def run_all_tests(self):
        """
        Run recovery tests for all fault types.
        """
        for fault_type in self.fault_types:
            self.test_fault_type(fault_type)
        
        # Summary
        print("\n=== Recovery Test Summary ===")
        
        for result in self.results:
            status = "✓ PASS" if result['success'] else "✗ FAIL"
            print(f"{result['fault_type']:30} {status}")
        
        all_passed = all(r['success'] for r in self.results)
        
        return all_passed

if __name__ == '__main__':
    tester = FaultInjectionTest()
    success = tester.run_all_tests()
    
    if success:
        print("\n✓ All recovery tests PASSED")
        sys.exit(0)
    else:
        print("\n✗ Some recovery tests FAILED")
        sys.exit(1)
```

## 📊 Error Recovery Targets

| Metric                       | Target              | Measurement Method                        |
|------------------------------|---------------------|-------------------------------------------|
| Recovery Time                | <5 seconds          | Time from error detection to normal/degraded operation |
| Recovery Success Rate        | ≥95%                | % of injected faults successfully recovered |
| Crash Rate                   | Zero                | System remains stable under all faults    |
| Graceful Degradation         | 100%                | Always fallback to safe mode, never hang  |
| PHC Fallback Accuracy        | ±10µs               | System clock accuracy when PHC fails      |
| Concurrent Error Handling    | 100%                | Prioritized recovery for multiple simultaneous errors |
| Watchdog Timeout             | <10 seconds         | Watchdog triggers recovery if hung        |
| Error Logging                | 100%                | All errors logged to ETW with context     |

## ✅ Acceptance Criteria

### Recovery Functionality
- ✅ PHC failure → fallback to system clock (<5s)
- ✅ Link down → PHC resync on link up (<30s)
- ✅ Memory allocation failure → graceful degradation (throttling)
- ✅ DMA error → buffer reset and retry (<5s)
- ✅ Interrupt storm → throttling and stability

### Recovery Time
- ✅ Transient errors recovered <5 seconds
- ✅ Hardware resets completed <10 seconds
- ✅ Link resync after disconnect <30 seconds

### Stability Under Faults
- ✅ Zero crashes during fault injection
- ✅ Zero hangs (watchdog prevents deadlocks)
- ✅ System remains responsive (IOCTLs <100ms)

### Graceful Degradation
- ✅ Always fallback to safe mode (never offline)
- ✅ TSN features disabled cleanly when PHC fails
- ✅ Best-effort forwarding continues when QoS fails

### Error Reporting
- ✅ All errors logged to ETW with timestamps
- ✅ Error messages include recovery actions taken
- ✅ User notified of degraded mode via event log

### Concurrent Errors
- ✅ Multiple simultaneous failures prioritized correctly
- ✅ Critical errors (PHC) handled before warnings (stats)
- ✅ No cascading failures (isolation effective)

## 🔗 References

- #59: REQ-NF-PERFORMANCE-001 - Performance Requirements
- #63: REQ-NF-SECURITY-001 - Security Requirements
- Windows Driver Error Handling Best Practices
- Fault Injection Testing Guide

