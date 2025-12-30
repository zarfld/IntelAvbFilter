# Backup: Issue #84 Current State (Before Reconstruction)

**Backup Date**: 2025-12-30  
**Issue URL**: https://github.com/zarfld/IntelAvbFilter/issues/84  
**Issue Number**: 84  
**Issue State**: Open  
**Created**: 2025-12-07T23:34:50Z  
**Last Updated**: 2025-12-19T17:19:50Z  

## Problem Identified

**Title says**: REQ-NF-PORTABILITY-001: Hardware Portability via Device Abstraction Layer  
**Body describes**: REQ-NF-PERF-BENCHMARKS-001: Performance Benchmarks

**Mismatch detected** - The issue body content does not match the title.

---

## Current Issue Title

REQ-NF-PORTABILITY-001: Hardware Portability via Device Abstraction Layer

---

## Current Issue Body (As Retrieved)

## Requirement Information

**Requirement ID**: REQ-NF-PERF-BENCHMARKS-001  
**Type**: Non-Functional Requirement  
**Priority**: P2 (Performance)  
**Status**: Draft  

## Description

The driver MUST provide comprehensive performance benchmarks covering latency, throughput, CPU usage, and memory footprint to establish baseline performance and detect regressions.

### Performance Metrics

**1. Latency Benchmarks**:

**IOCTL Latency** (user-mode to kernel-mode round-trip):
- `IOCTL_PHC_GET_TIME`: P95 &lt;100µs
- `IOCTL_PHC_SET_TIME`: P95 &lt;200µs
- `IOCTL_TAS_GET_CONFIG`: P95 &lt;150µs
- `IOCTL_TAS_SET_CONFIG`: P95 &lt;500µs

Rationale: Low-latency clock access critical for AVB synchronization

**Packet Processing Latency** (interrupt to DPC completion):
- P95 &lt;50µs for 64-byte frames
- Rationale: Fast packet handling prevents queue buildup

**2. Throughput Benchmarks**:

**TCP Performance**:
- Bidirectional: &gt;900 Mbps (Gigabit Ethernet)
- Rationale: Near line-rate performance for bulk data

**UDP Performance**:
- Unidirectional: &gt;950 Mbps, &lt;0.1% packet loss
- Rationale: Minimize overhead for real-time streams

**Small Frame Performance**:
- 64-byte UDP: &gt;148,000 packets/second
- Rationale: AVB audio streams use small frames

**3. CPU Usage**:
- DPC time: &lt;10% (single core) at 500 Mbps
- Interrupt time: &lt;5% (single core) at 500 Mbps
- Total driver overhead: &lt;15% CPU at 500 Mbps

Rationale: Low CPU usage leaves headroom for applications

**4. Memory Footprint**:
- Non-paged pool: &lt;10 MB
- Paged pool: &lt;5 MB
- No memory leaks (stable over 24-hour stress test)

Rationale: Kernel memory is limited resource

### Acceptance Criteria

**AC1: IOCTL Latency**:
- Given 1000 iterations of each IOCTL
- When benchmark executes
- Then P95 latency meets targets (GET &lt;100µs, SET &lt;500µs)
- And results logged to JSON file

**AC2: Throughput**:
- Given iperf3 test for 60 seconds
- When TCP bidirectional traffic runs
- Then throughput &gt;900 Mbps
- And packet loss &lt;0.1%

**AC3: CPU Usage**:
- Given 500 Mbps bidirectional traffic
- When performance monitor samples every second
- Then DPC+Interrupt time &lt;15% CPU
- And CPU usage stable over 5-minute run

**AC4: Memory Footprint**:
- Given 24-hour stress test (900 Mbps continuous)
- When memory pools monitored hourly
- Then driver footprint &lt;15 MB total
- And no leaks detected (Driver Verifier enabled)

**AC5: Regression Detection**:
- Given baseline benchmark results
- When new driver build benchmarked
- Then metrics within ±10% of baseline
- And CI fails if regression &gt;10%

## Traceability

- Traces to: #31 (StR-REQ-001: Stakeholder Requirements Definition)
- Verified by: #253 (TEST-PERF-BENCHMARKS-001: Verify Performance Benchmarks)

## Rationale

**Why This Requirement**:
- Performance directly impacts AVB stream quality (audio/video glitches if slow)
- Baseline metrics enable regression detection (prevent accidental slowdowns)
- CPU/memory limits constrain embedded and industrial systems
- Benchmarks guide optimization efforts (focus on actual bottlenecks)

**Industry Context**:
- IEEE 802.1AS (gPTP): Requires &lt;100µs sync accuracy
- AVB Class A: &lt;2ms end-to-end latency budget
- AVB Class B: &lt;50ms latency budget
- Driver overhead must be small fraction of total budget

**Impact of Non-Compliance**:
- Slow IOCTLs → Applications miss gPTP sync deadlines → Audio/video drift
- High CPU usage → System overload → Packet drops
- Memory leaks → System instability over time → Reboot required
- No regression testing → Silent performance degradation → User complaints

## Implementation Notes

**Benchmarking Tools**:

**1. IOCTL Latency** (custom tool):
```c
// avb_test_i210_um.c (benchmark mode)
void BenchmarkIoctl(HANDLE Device, ULONG Ioctl, ULONG Iterations) {
    LARGE_INTEGER frequency, start, end;
    QueryPerformanceFrequency(&frequency);
    
    ULONGLONG* latencies = malloc(Iterations * sizeof(ULONGLONG));
    
    for (ULONG i = 0; i < Iterations; i++) {
        QueryPerformanceCounter(&start);
        DeviceIoControl(Device, Ioctl, ...);
        QueryPerformanceCounter(&end);
        
        latencies[i] = (end.QuadPart - start.QuadPart) * 1000000 / frequency.QuadPart; // µs
    }
    
    // Calculate P50, P95, P99
    qsort(latencies, Iterations, sizeof(ULONGLONG), compare);
    printf(&#34;P50: %llu µs, P95: %llu µs, P99: %llu µs\n&#34;,
           latencies[Iterations/2],
           latencies[Iterations*95/100],
           latencies[Iterations*99/100]);
}
```

**2. Throughput** (iperf3):
```bash
# TCP bidirectional
iperf3 -c <server> -t 60 --bidir --json > tcp_throughput.json

# UDP unidirectional
iperf3 -c <server> -u -b 1000M -t 60 --json > udp_throughput.json

# Small frames
iperf3 -c <server> -u -b 100M -l 64 -t 60 --json > small_frames.json
```

**3. CPU Usage** (ETW tracing):
```powershell
# Start trace
xperf -on PROC_THREAD+DPC+INTERRUPT -stackwalk Profile

# Run traffic (5 minutes)
iperf3 -c <server> -t 300 -b 500M

# Stop trace
xperf -d cpu_trace.etl

# Analyze (CPU time by driver)
wpaexporter cpu_trace.etl -outputfolder results
```

**4. Memory Footprint** (WMI query):
```powershell
# Before traffic
$before = Get-WmiObject Win32_PerfRawData_PerfOS_Memory | Select NonPagedBytes

# Run traffic (24 hours)
iperf3 -c <server> -t 86400

# After traffic
$after = Get-WmiObject Win32_PerfRawData_PerfOS_Memory | Select NonPagedBytes

# Driver footprint
$delta = $after.NonPagedBytes - $before.NonPagedBytes
Write-Host &#34;Driver memory: $($delta / 1MB) MB&#34;
```

**Automation** (CI integration):
```yaml
# .github/workflows/performance-benchmarks.yml
name: Performance Benchmarks

on:
  schedule:
    - cron: &#39;0 2 * * *&#39;  # Nightly at 2 AM
  workflow_dispatch:

jobs:
  benchmark:
    runs-on: [self-hosted, performance-test-rig]  # Dedicated hardware
    steps:
      - uses: actions/checkout@v4
      
      - name: Build driver
        run: msbuild IntelAvbFilter.sln /p:Configuration=Release
      
      - name: Install driver
        run: |
          pnputil /add-driver IntelAvbFilter.inf /install
          sc start IntelAvbFilter
      
      - name: Run IOCTL benchmark
        run: ./avb_test_i210_um.exe --benchmark-ioctl --json ioctl_results.json
      
      - name: Run throughput benchmark
        run: |
          iperf3 -c ${{ secrets.PERF_SERVER_IP }} -t 60 --bidir --json > tcp_throughput.json
          iperf3 -c ${{ secrets.PERF_SERVER_IP }} -u -b 1000M -t 60 --json > udp_throughput.json
      
      - name: Analyze results
        run: python scripts/analyze_benchmarks.py --compare-to baseline.json
      
      - name: Upload results
        uses: actions/upload-artifact@v4
        with:
          name: performance-results
          path: |
            ioctl_results.json
            tcp_throughput.json
            udp_throughput.json
      
      - name: Fail on regression
        run: |
          # CI fails if any metric >10% worse than baseline
          python scripts/check_regression.py --threshold 0.10
```

**Regression Detection**:
```python
# scripts/check_regression.py
import json

def check_regression(current, baseline, threshold=0.10):
    &#34;&#34;&#34;Fail if current performance >10% worse than baseline&#34;&#34;&#34;
    for metric, value in current.items():
        baseline_value = baseline.get(metric)
        if baseline_value:
            regression = (value - baseline_value) / baseline_value
            if regression > threshold:
                print(f&#34;❌ Regression detected: {metric} {regression*100:.1f}% worse&#34;)
                return False
    return True

with open(&#34;current_results.json&#34;) as f:
    current = json.load(f)
with open(&#34;baseline.json&#34;) as f:
    baseline = json.load(f)

if not check_regression(current, baseline):
    exit(1)  # Fail CI
```

## Dependencies

- Dedicated performance test hardware (consistent results)
- iperf3 server (network peer)
- Windows Performance Toolkit (WPT)
- Baseline results (initial benchmark run)

## Risks

- **Hardware Variability**: Results differ across systems → Baseline per test rig
- **Background Noise**: Other processes interfere → Dedicated test system, minimal services
- **False Positives**: Transient spikes trigger false regressions → Use P95, not max latency
- **Mitigation**: Run benchmarks 3x, take median result

## Performance Targets Summary

| Metric                | Target              | Rationale                          |
|-----------------------|---------------------|---------------------------------------|
| IOCTL GET latency     | P95 &lt;100µs          | gPTP sync accuracy                 |
| IOCTL SET latency     | P95 &lt;500µs          | Config changes low-latency         |
| Packet latency        | P95 &lt;50µs           | Minimize queueing delay            |
| TCP throughput        | &gt;900 Mbps           | Near line-rate performance         |
| UDP throughput        | &gt;950 Mbps           | Minimize protocol overhead         |
| Small frame rate      | &gt;148k pps           | AVB audio streams                  |
| CPU usage (500 Mbps)  | &lt;15%                | Leave headroom for applications    |
| Memory footprint      | &lt;15 MB              | Kernel memory limit                |
| Stress test           | 24 hours stable     | Long-running system stability      |

## References

- IEEE 802.1AS-2020 (gPTP timing requirements)
- IEEE 802.1Q-2018 (AVB latency budgets)
- Windows Performance Toolkit: https://learn.microsoft.com/windows-hardware/test/wpt/
- iperf3: https://software.es.net/iperf/

---

**Created**: 2025-11-15  
**Last Updated**: 2025-12-19  
**Status**: Draft

---

## Issue Comments

### Comment 1 (github-actions[bot], 2025-12-09T09:54:49Z)

## ❌ Traceability Link Missing

This requirement must link to its parent issue.

### Required Action

Add the following to your issue body:

```markdown
## Traceability
- **Traces to**: #N (parent issue)
```

### Traceability Rules

- **REQ-F / REQ-NF** → Link to parent StR issue
- **ADR** → Link to requirements it satisfies
- **ARC-C** → Link to ADRs and requirements
- **TEST** → Link to requirements being verified
- **QA-SC** → Link to related requirements

**Standard**: ISO/IEC/IEEE 29148:2018 (Bidirectional Traceability)

### Comment 2 (zarfld, 2025-12-10T19:30:29Z)

## ✅ Enhancement Summary (Batch 6 - Issue 4/5)

**Enhanced**: REQ-NF-PORTABILITY-001 (Hardware Portability via Device Abstraction Layer)

### Additions:
- ✅ **10 Error Scenarios** (ES-PORT-HAL-001 through ES-PORT-HAL-010)
  - Unsupported device ID, NULL operations, capability mismatches
  - Register offset bounds, initialization failures, version mismatches
  - Concurrent operations, state overflow, missing implementations
- ✅ **9 Performance Metrics** (PM-PORT-HAL-001 through PM-PORT-HAL-009)
  - Operation call <20ns, code size reduction >30%
  - New device integration <8 hours, mock test coverage >90%
  - 100% register definitions from submodule, <512 bytes overhead
- ✅ **Event IDs**: 17301-17310 (Hardware abstraction events)
- ✅ **Gherkin Acceptance Criteria**: 5 scenarios covering HAL usage
- ✅ **ISO/IEC/IEEE 29148:2018 Compliance**: Complete specification

### Coverage:
- **HAL Design**: Operation tables, device-specific contexts, capability detection
- **Supported Hardware**: i210, i211, i225, i226 (future), mock (testing)
- **Maintainability**: Single source of truth per operation, no device branching in core

**Category**: Portability (Architecture)  
**Event ID Range**: 17301-17310

---

## Labels

- phase:02-requirements
- type:requirement:non-functional
- priority:medium
- category:architecture
- status:in-progress

---

## Analysis

Based on the comment from 2025-12-10, it appears the issue was meant to be about **REQ-NF-PORTABILITY-001** (Hardware Portability via Device Abstraction Layer) as indicated by:
1. The issue title
2. The enhancement comment mentioning "REQ-NF-PORTABILITY-001"
3. References to HAL design, device abstraction, error scenarios for hardware abstraction

The current body content about **REQ-NF-PERF-BENCHMARKS-001** (Performance Benchmarks) appears to be incorrect content that was mistakenly placed in this issue.

The original issue should have described hardware portability requirements through a device abstraction layer (HAL).
