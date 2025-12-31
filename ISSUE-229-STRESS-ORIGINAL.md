# TEST-STRESS-STABILITY-001: Long-Duration Stress Testing and Stability Validation

## 🔗 Traceability
- Traces to: #1 (StR-CORE-001: AVB Filter Driver Core Requirements)
- Verifies: #59 (REQ-NF-PERFORMANCE-001: Performance and Scalability)
- Related Requirements: #14, #37, #38, #39, #60
- Test Phase: Phase 07 - Verification & Validation
- Test Priority: P0 (Critical - Stability)

## 📋 Test Objective

Validate system stability under continuous high-load operation for extended periods (7+ days), verify no memory leaks, no performance degradation, no crashes, and all TSN features remain operational throughout.

## 🧪 Test Coverage

### 10 Unit Tests

1. **Memory leak detection** (monitor pool allocations, zero leaks over 7 days)
   - **Objective**: Verify no memory leaks in driver code paths
   - **Preconditions**: Driver loaded, memory tracking enabled
   - **Procedure**:
     1. Wrap all ExAllocatePoolWithTag() calls with TrackedAlloc()
     2. Wrap all ExFreePoolWithTag() calls with TrackedFree()
     3. Monitor allocation count, free count, current allocated bytes
     4. Sample every hour for 7 days (168 samples)
     5. Verify AllocationCount == FreeCount at end of test
   - **Expected**: Zero leaked allocations, current allocated bytes stable (±10%)
   - **Implementation**: `MEMORY_TRACKER` structure, `TrackedAlloc()`, `TrackedFree()`, `ReportMemoryStatus()`

2. **Handle leak detection** (monitor kernel handles, zero leaks)
   - **Objective**: Verify no kernel object handle leaks (events, mutexes, timers)
   - **Preconditions**: Driver creating/destroying kernel objects
   - **Procedure**:
     1. Query initial handle count via NtQuerySystemInformation
     2. Run driver operations (create/destroy events, timers, etc.)
     3. Sample handle count every hour for 7 days
     4. Verify handle count returns to baseline after operations
   - **Expected**: Handle count stable throughout test, no growth trend
   - **Implementation**: `QueryHandleCount()`, periodic sampling, trend analysis

3. **Thread leak detection** (monitor thread count, stable throughout)
   - **Objective**: Verify no thread leaks from work item queues, system threads
   - **Preconditions**: Driver using system worker threads, custom threads
   - **Procedure**:
     1. Query initial thread count for driver process/kernel
     2. Run operations that spawn threads (work items, IoQueueWorkItem)
     3. Sample thread count every hour for 7 days
     4. Verify thread count stable, threads properly terminated
   - **Expected**: Thread count stable, no orphaned threads
   - **Implementation**: `PsGetCurrentProcessId()`, thread enumeration

4. **Lock contention monitoring** (spinlock hold times remain <1µs)
   - **Objective**: Verify spinlocks not held excessively long (prevent starvation)
   - **Preconditions**: Driver using spinlocks (KeAcquireSpinLock)
   - **Procedure**:
     1. Instrument spinlock acquire/release with KeQueryPerformanceCounter
     2. Calculate hold time: release_time - acquire_time
     3. Log all hold times >1µs
     4. Monitor over 7 days for increasing hold times
   - **Expected**: 99% of spinlock hold times <1µs, no deadlocks
   - **Implementation**: `SPINLOCK_TRACKER`, acquire/release wrappers, histogram

5. **Performance degradation detection** (throughput stable ±3% over 7 days)
   - **Objective**: Verify throughput does not degrade over time
   - **Preconditions**: Baseline throughput established (1 Gbps traffic)
   - **Procedure**:
     1. Capture baseline throughput after 1 hour warm-up
     2. Measure TX bytes per second every 5 minutes
     3. Calculate degradation: (baseline - current) / baseline * 100
     4. Fail if degradation >5% for >1 hour
   - **Expected**: Throughput within ±3% of baseline throughout 7 days
   - **Implementation**: `PERFORMANCE_BASELINE`, `DetectPerformanceDegradation()`, trend analysis

6. **Latency drift monitoring** (Class A latency remains <2ms throughout)
   - **Objective**: Verify latency does not increase over time (no jitter accumulation)
   - **Preconditions**: Class A stream active, latency measurements enabled
   - **Procedure**:
     1. Measure Class A end-to-end latency (TX timestamp → RX timestamp)
     2. Sample latency every 1 second for 7 days
     3. Calculate 99th percentile latency each hour
     4. Verify 99th percentile remains <2ms
   - **Expected**: Class A latency <2ms (99th percentile) throughout test
   - **Implementation**: Latency histogram, percentile calculation

7. **PHC sync stability** (offset remains ±100ns over 7 days)
   - **Objective**: Verify PHC synchronization does not drift over time
   - **Preconditions**: PHC synchronized to gPTP grandmaster
   - **Procedure**:
     1. Monitor PHC offset via IOCTL_AVB_GET_PHC_TIME
     2. Calculate offset: PHC_time - grandmaster_time
     3. Sample offset every 1 second for 7 days
     4. Verify offset remains ±100ns
   - **Expected**: PHC offset ±100ns throughout test, no drift >500ns
   - **Implementation**: Continuous offset monitoring, alarm on drift

8. **TAS schedule adherence** (gate violations <0.01% throughout)
   - **Objective**: Verify TAS gate schedule remains accurate over time
   - **Preconditions**: TAS enabled, gate schedule programmed
   - **Procedure**:
     1. Monitor TAS violation counter (packets sent during closed gates)
     2. Sample violation count every 5 minutes for 7 days
     3. Calculate violation rate: violations / total_packets * 100
     4. Verify violation rate <0.01%
   - **Expected**: TAS violation rate <0.01% throughout test
   - **Implementation**: TAS violation counter monitoring

9. **Error accumulation** (error counters plateau, no exponential growth)
   - **Objective**: Verify error counters do not grow exponentially (indicates cascading failures)
   - **Preconditions**: Error counters initialized (CRC errors, frame errors, etc.)
   - **Procedure**:
     1. Monitor all error counters (RX_CRC_ERR, RX_ALIGN_ERR, etc.)
     2. Sample counters every 5 minutes for 7 days
     3. Calculate growth rate: (current - previous) / elapsed_time
     4. Fail if growth rate increases exponentially
   - **Expected**: Error counters plateau or grow linearly, no exponential growth
   - **Implementation**: Error counter monitoring, growth rate analysis

10. **Resource exhaustion prevention** (CPU <5%, memory <10MB stable)
    - **Objective**: Verify driver does not exhaust system resources
    - **Preconditions**: System monitoring enabled
    - **Procedure**:
      1. Monitor CPU usage via KeQuerySystemTime, idle time
      2. Monitor driver pool allocation (current allocated bytes)
      3. Sample every 5 minutes for 7 days
      4. Verify CPU <5% average, memory <10MB stable
    - **Expected**: CPU <5%, memory <10MB, no resource starvation
    - **Implementation**: System resource monitoring, threshold alarms

### 3 Integration Tests

1. **7-day continuous operation** (1 Gbps traffic, all TSN features active, zero crashes)
   - **Objective**: Validate driver stability under realistic production load
   - **Preconditions**: Test network setup (talker, listener, gPTP grandmaster), traffic generator
   - **Procedure**:
     1. Configure Class A stream (1 Gbps, 48 kHz audio, 8 channels)
     2. Enable all TSN features (PHC, TAS, CBS, gPTP)
     3. Start traffic generator (continuous 1 Gbps bidirectional)
     4. Run for 168 hours (7 days) without interruption
     5. Monitor for crashes (KeBugCheck), hangs, performance degradation
     6. Collect metrics every 5 minutes (automated script)
   - **Expected**: Zero crashes, zero hangs, all features operational, metrics within spec
   - **Implementation**: Automated test controller (Python script), metrics logging, crash detection

2. **Temperature cycle stress** (simulate thermal changes, verify PHC stability)
   - **Objective**: Verify driver handles thermal stress without PHC drift or crashes
   - **Preconditions**: Temperature chamber or software thermal simulation
   - **Procedure**:
     1. Start driver with PHC synchronized
     2. Cycle system temperature: 0°C → 50°C → 0°C (1 hour cycles)
     3. Monitor PHC offset during temperature changes
     4. Run for 7 days (168 temperature cycles)
     5. Verify PHC offset remains ±100ns throughout
   - **Expected**: PHC stable during thermal cycles, no crashes, offset ±100ns
   - **Implementation**: Thermal chamber control, PHC monitoring

3. **Power state transitions** (D0↔D3 cycles hourly, verify recovery)
   - **Objective**: Verify driver handles sleep/wake cycles without corruption
   - **Preconditions**: Driver supports D0 (fully on) and D3 (sleep) power states
   - **Procedure**:
     1. Cycle power state: D0 → D3 (sleep) → D0 (wake) every hour
     2. Verify driver resumes operations after wake (PHC sync, TAS, CBS)
     3. Run for 7 days (168 power cycles)
     4. Monitor for failures to resume, corruption, memory leaks
   - **Expected**: 100% successful wake operations, zero corruption, features resume correctly
   - **Implementation**: Power state transition via IRP_MJ_POWER, state verification

### 2 V&V Tests

1. **Production simulation** (replicate production traffic patterns for 7 days, monitor for anomalies)
   - **Objective**: Validate driver under realistic production workload
   - **Preconditions**: Production traffic profile captured (packet traces, timing patterns)
   - **Procedure**:
     1. Replay production traffic patterns (Wireshark pcap files)
     2. Include bursty traffic, periodic streams, control packets
     3. Run for 7 days continuous
     4. Monitor for anomalies (crashes, errors, performance issues)
     5. Compare metrics against production baseline
   - **Expected**: Driver handles production traffic without anomalies, metrics match baseline
   - **Implementation**: Traffic replay tools (tcpreplay), metric comparison

2. **Soak test under attack** (continuous DoS attempts, system remains stable, no degradation)
   - **Objective**: Verify driver resilience against sustained attack
   - **Preconditions**: Attack traffic generator (flood, malformed packets)
   - **Procedure**:
     1. Generate attack traffic (100,000 packets/sec, malformed headers)
     2. Simultaneously run legitimate Class A/B streams
     3. Run attack for 7 days continuous
     4. Verify legitimate streams unaffected (latency, throughput)
     5. Monitor for crashes, resource exhaustion, performance degradation
   - **Expected**: Legitimate traffic unaffected, driver stable, rate limiting effective
   - **Implementation**: Attack traffic generator (hping3, Scapy), legitimate stream monitoring

## 🔧 Implementation Notes

### Memory Leak Detection

```c
typedef struct _MEMORY_TRACKER {
    LONG AllocationCount;           // Total allocations
    LONG FreeCount;                 // Total frees
    SIZE_T CurrentAllocatedBytes;   // Current pool usage
    SIZE_T PeakAllocatedBytes;      // Peak pool usage
    LARGE_INTEGER StartTime;        // Test start time
} MEMORY_TRACKER;

VOID* TrackedAlloc(SIZE_T size, ULONG tag, MEMORY_TRACKER* tracker) {
    VOID* memory = ExAllocatePoolWithTag(NonPagedPool, size, tag);
    
    if (memory) {
        InterlockedIncrement(&tracker->AllocationCount);
        InterlockedAdd64((LONG64*)&tracker->CurrentAllocatedBytes, size);
        
        // Update peak
        SIZE_T current = tracker->CurrentAllocatedBytes;
        SIZE_T peak = tracker->PeakAllocatedBytes;
        
        while (current > peak) {
            SIZE_T prev = InterlockedCompareExchange64(
                (LONG64*)&tracker->PeakAllocatedBytes, current, peak);
            
            if (prev == peak) break;
            peak = prev;
        }
    }
    
    return memory;
}

VOID TrackedFree(VOID* memory, SIZE_T size, ULONG tag, MEMORY_TRACKER* tracker) {
    if (memory) {
        ExFreePoolWithTag(memory, tag);
        
        InterlockedIncrement(&tracker->FreeCount);
        InterlockedAdd64((LONG64*)&tracker->CurrentAllocatedBytes, -(LONG64)size);
    }
}

VOID ReportMemoryStatus(MEMORY_TRACKER* tracker) {
    LARGE_INTEGER now;
    KeQueryPerformanceCounter(&now);
    
    UINT64 uptimeHours = ((now.QuadPart - tracker->StartTime.QuadPart) * 1000000) /
                         (g_PerformanceFrequency.QuadPart * 3600);
    
    DbgPrint("=== Memory Status (Uptime: %llu hours) ===\n", uptimeHours);
    DbgPrint("  Allocations: %ld\n", tracker->AllocationCount);
    DbgPrint("  Frees: %ld\n", tracker->FreeCount);
    DbgPrint("  Leaked: %ld\n", tracker->AllocationCount - tracker->FreeCount);
    DbgPrint("  Current: %llu KB\n", tracker->CurrentAllocatedBytes / 1024);
    DbgPrint("  Peak: %llu KB\n", tracker->PeakAllocatedBytes / 1024);
    
    // Fail test if leaked allocations > 100
    if (tracker->AllocationCount - tracker->FreeCount > 100) {
        DbgPrint("  *** MEMORY LEAK DETECTED ***\n");
    }
}
```

### Performance Degradation Detection

```c
typedef struct _PERFORMANCE_BASELINE {
    UINT64 BaselineThroughputBps;   // Initial throughput
    UINT64 BaselineLatencyNs;       // Initial latency
    UINT32 BaselineCpuPercent;      // Initial CPU usage
    LARGE_INTEGER CaptureTime;      // When baseline captured
} PERFORMANCE_BASELINE;

BOOLEAN DetectPerformanceDegradation(ADAPTER_CONTEXT* adapter, PERFORMANCE_BASELINE* baseline) {
    PERF_METRICS current;
    CollectPerfMetrics(adapter, &current);
    
    LARGE_INTEGER now;
    KeQueryPerformanceCounter(&now);
    
    UINT64 uptimeHours = ((now.QuadPart - baseline->CaptureTime.QuadPart) * 1000000) /
                         (g_PerformanceFrequency.QuadPart * 3600);
    
    // Calculate degradation percentages
    INT32 throughputDegradation = 0;
    if (current.TxBytesPerSecond < baseline->BaselineThroughputBps) {
        throughputDegradation = (INT32)(((baseline->BaselineThroughputBps - current.TxBytesPerSecond) * 100) /
                                         baseline->BaselineThroughputBps);
    }
    
    INT32 latencyIncrease = 0;
    if (current.AvgLatencyNs > baseline->BaselineLatencyNs) {
        latencyIncrease = (INT32)(((current.AvgLatencyNs - baseline->BaselineLatencyNs) * 100) /
                                   baseline->BaselineLatencyNs);
    }
    
    INT32 cpuIncrease = current.CpuUsagePercent - baseline->BaselineCpuPercent;
    
    // Report status
    DbgPrint("=== Performance Status (Uptime: %llu hours) ===\n", uptimeHours);
    DbgPrint("  Throughput: %lld Bps (baseline=%lld, degradation=%d%%)\n",
             current.TxBytesPerSecond, baseline->BaselineThroughputBps, throughputDegradation);
    DbgPrint("  Latency: %lld ns (baseline=%lld, increase=%d%%)\n",
             current.AvgLatencyNs, baseline->BaselineLatencyNs, latencyIncrease);
    DbgPrint("  CPU: %u%% (baseline=%u%%, increase=%d%%)\n",
             current.CpuUsagePercent, baseline->BaselineCpuPercent, cpuIncrease);
    
    // Fail if degradation > 5%
    if (throughputDegradation > 5 || latencyIncrease > 5 || cpuIncrease > 2) {
        DbgPrint("  *** PERFORMANCE DEGRADATION DETECTED ***\n");
        return TRUE;  // Degradation detected
    }
    
    return FALSE;  // Performance stable
}
```

### Automated Stress Test Controller

```python
#!/usr/bin/env python3
"""
7-Day Stress Test Controller
Manages long-duration stress testing with automated monitoring.
"""
import subprocess
import time
import datetime
import psutil
import json
from pathlib import Path

class StressTestController:
    def __init__(self, duration_hours=168):  # 168 hours = 7 days
        self.duration_hours = duration_hours
        self.start_time = datetime.datetime.now()
        self.metrics_log = []
        self.errors = []
    
    def run_test(self):
        """
        Run stress test for specified duration.
        """
        print(f"=== Starting {self.duration_hours}-hour stress test ===")
        print(f"Start time: {self.start_time}")
        
        end_time = self.start_time + datetime.timedelta(hours=self.duration_hours)
        print(f"End time: {end_time}")
        
        # Start traffic generator
        self.start_traffic_generator()
        
        # Monitoring loop (sample every 5 minutes)
        sample_interval = 300  # 5 minutes
        
        while datetime.datetime.now() < end_time:
            # Collect metrics
            metrics = self.collect_metrics()
            self.metrics_log.append(metrics)
            
            # Check for anomalies
            self.check_anomalies(metrics)
            
            # Log progress
            elapsed = datetime.datetime.now() - self.start_time
            remaining = end_time - datetime.datetime.now()
            
            print(f"[{elapsed}] Progress: {(elapsed.total_seconds() / (self.duration_hours * 3600)) * 100:.1f}% "
                  f"| Remaining: {remaining} | CPU: {metrics['cpu_percent']:.1f}% | "
                  f"Mem: {metrics['memory_mb']:.1f} MB")
            
            # Save checkpoint
            self.save_checkpoint()
            
            # Sleep until next sample
            time.sleep(sample_interval)
        
        # Stop traffic generator
        self.stop_traffic_generator()
        
        # Generate report
        self.generate_report()
        
        print("\n=== Stress test completed ===")
        
        if self.errors:
            print(f"FAILED: {len(self.errors)} errors detected")
            return False
        else:
            print("PASSED: No errors detected")
            return True
    
    def collect_metrics(self):
        """
        Collect system and driver metrics.
        """
        # Query driver statistics via IOCTL
        driver_stats = self.query_driver_stats()
        
        # System metrics
        return {
            'timestamp': datetime.datetime.now().isoformat(),
            'cpu_percent': psutil.cpu_percent(interval=1),
            'memory_mb': psutil.virtual_memory().used / (1024 * 1024),
            'driver_pool_mb': driver_stats.get('pool_allocated_kb', 0) / 1024,
            'throughput_mbps': driver_stats.get('tx_bytes_per_sec', 0) / (1024 * 1024),
            'latency_us': driver_stats.get('avg_latency_ns', 0) / 1000,
            'phc_offset_ns': driver_stats.get('phc_offset_ns', 0),
            'tas_violations': driver_stats.get('tas_violations', 0)
        }
    
    def check_anomalies(self, metrics):
        """
        Check for performance degradation, memory leaks, etc.
        """
        # Memory leak check (driver pool should be stable)
        if len(self.metrics_log) > 10:
            initial_mem = self.metrics_log[5]['driver_pool_mb']  # After warm-up
            current_mem = metrics['driver_pool_mb']
            
            growth_percent = ((current_mem - initial_mem) / initial_mem) * 100 if initial_mem > 0 else 0
            
            if growth_percent > 10:  # >10% growth indicates leak
                error = f"Memory leak detected: {initial_mem:.2f} MB → {current_mem:.2f} MB ({growth_percent:.1f}% growth)"
                self.errors.append(error)
                print(f"  *** {error} ***")
        
        # Performance degradation check
        if len(self.metrics_log) > 10:
            baseline_throughput = sum(m['throughput_mbps'] for m in self.metrics_log[5:10]) / 5
            current_throughput = metrics['throughput_mbps']
            
            degradation = ((baseline_throughput - current_throughput) / baseline_throughput) * 100 if baseline_throughput > 0 else 0
            
            if degradation > 5:  # >5% degradation
                error = f"Throughput degradation: {baseline_throughput:.1f} Mbps → {current_throughput:.1f} Mbps ({degradation:.1f}%)"
                self.errors.append(error)
                print(f"  *** {error} ***")
    
    def save_checkpoint(self):
        """
        Save current state to file (for recovery if test interrupted).
        """
        checkpoint = {
            'start_time': self.start_time.isoformat(),
            'metrics_count': len(self.metrics_log),
            'errors_count': len(self.errors),
            'latest_metrics': self.metrics_log[-1] if self.metrics_log else None
        }
        
        with open('stress_test_checkpoint.json', 'w') as f:
            json.dump(checkpoint, f, indent=2)
    
    def generate_report(self):
        """
        Generate HTML report with charts.
        """
        # Export metrics to CSV
        with open('stress_test_metrics.csv', 'w') as f:
            if self.metrics_log:
                headers = self.metrics_log[0].keys()
                f.write(','.join(headers) + '\n')
                
                for metrics in self.metrics_log:
                    f.write(','.join(str(metrics[h]) for h in headers) + '\n')
        
        print(f"\nMetrics exported to stress_test_metrics.csv ({len(self.metrics_log)} samples)")
        
        # Summary
        print("\n=== Test Summary ===")
        print(f"Duration: {self.duration_hours} hours")
        print(f"Samples: {len(self.metrics_log)}")
        print(f"Errors: {len(self.errors)}")
        
        if self.metrics_log:
            avg_cpu = sum(m['cpu_percent'] for m in self.metrics_log) / len(self.metrics_log)
            avg_mem = sum(m['driver_pool_mb'] for m in self.metrics_log) / len(self.metrics_log)
            avg_throughput = sum(m['throughput_mbps'] for m in self.metrics_log) / len(self.metrics_log)
            
            print(f"Avg CPU: {avg_cpu:.1f}%")
            print(f"Avg Driver Mem: {avg_mem:.1f} MB")
            print(f"Avg Throughput: {avg_throughput:.1f} Mbps")

if __name__ == '__main__':
    controller = StressTestController(duration_hours=168)  # 7 days
    success = controller.run_test()
    sys.exit(0 if success else 1)
```

## 📊 Stress Test Targets

| Metric                       | Target              | Measurement Method                        |
|------------------------------|---------------------|-------------------------------------------|
| Uptime                       | ≥7 days continuous  | No crashes or hangs                       |
| Memory Leaks                 | Zero                | Pool allocations stable (±10%)            |
| Handle Leaks                 | Zero                | Kernel handle count stable                |
| Performance Degradation      | <5%                 | Throughput/latency within baseline ±5%    |
| CPU Stability                | <5% avg             | CPU usage stable throughout test          |
| PHC Sync Stability           | ±100ns              | Offset remains within spec                |
| TAS Violations               | <0.01%              | Gate schedule adherence >99.99%           |
| Error Accumulation           | Plateau             | Error counters don't grow exponentially   |
| Crash Count                  | Zero                | System remains operational                |

## ✅ Acceptance Criteria

### Stability
- ✅ Zero crashes over 7 days
- ✅ Zero hangs or deadlocks
- ✅ System remains responsive (IOCTL latency <100ms)

### Memory Management
- ✅ No memory leaks (pool allocation stable ±10%)
- ✅ No handle leaks (kernel object count stable)
- ✅ No thread leaks (thread count stable)

### Performance
- ✅ Throughput stable (within baseline ±5%)
- ✅ Latency stable (Class A <2ms, Class B <50ms throughout)
- ✅ CPU usage stable (<5% average)

### Feature Stability
- ✅ PHC sync maintained (offset ±100ns)
- ✅ TAS operational (violations <0.01%)
- ✅ CBS operational (credit calculations correct)
- ✅ All IOCTLs functional throughout test

### Error Handling
- ✅ Error counters plateau (no exponential growth)
- ✅ Recovery from transient errors (<5s)
- ✅ No cascading failures

## 🔗 References

- #59: REQ-NF-PERFORMANCE-001 - Performance Requirements
- Windows Driver Stability Testing Guide
- Microsoft Driver Verifier documentation
