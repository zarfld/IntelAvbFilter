# Acceptance Criteria Template for Requirements

**Purpose**: Standard template for adding acceptance criteria to functional and non-functional requirements to complete Phase 02 exit criteria.

**Standards**: ISO/IEC/IEEE 29148:2018, IEEE 1012-2016 (V&V)

---

## 📋 Template for REQ-F (Functional Requirements)

### Acceptance Criteria Format (Gherkin - Given-When-Then)

Add this section to each REQ-F GitHub issue:

```markdown
## Acceptance Criteria

### Scenario 1: [Primary Success Path]
**Given** [initial context/preconditions]
**And** [additional context]
**When** [action performed]
**Then** [expected outcome - measurable]
**And** [additional verification]

### Scenario 2: [Alternative Path or Edge Case]
**Given** [different context]
**When** [action performed]
**Then** [expected outcome]

### Scenario 3: [Error Handling]
**Given** [error condition]
**When** [action attempted]
**Then** [error handling behavior]
**And** [error message/code returned]

### Scenario 4: [Boundary Conditions]
**Given** [boundary condition - min/max values]
**When** [action performed]
**Then** [expected boundary behavior]
```

### Verification Method

Add this section to each REQ-F issue:

```markdown
## Verification Method

**Primary Method**: Test (automated unit + integration tests)

**Test Levels**:
- Unit Test: [Specific function/method to test]
- Integration Test: [Component interaction to verify]
- System Test: [End-to-end scenario]

**Success Criteria**:
- All acceptance scenarios pass
- Test coverage ≥80% for implementing code
- No critical defects found
```

---

## 📋 Template for REQ-NF (Non-Functional Requirements)

### Acceptance Criteria Format (Metrics-Based)

Add this section to each REQ-NF GitHub issue:

```markdown
## Acceptance Criteria

### Measurable Metrics

| Metric | Target | Measurement Method | Acceptance Threshold |
|--------|--------|-------------------|---------------------|
| [Performance metric] | [Target value] | [How measured] | [Pass/fail criteria] |
| [Quality attribute] | [Target value] | [Tool/method] | [Threshold] |

### Specific Scenarios

**Scenario 1: [Normal Load]**
- **Given**: [Load condition]
- **When**: [Operation performed]
- **Then**: Metric meets target ([value])

**Scenario 2: [Stress/Peak Load]**
- **Given**: [Stress condition]
- **When**: [Operation performed]
- **Then**: Metric degrades gracefully (≥[minimum threshold])

**Scenario 3: [Long-Duration Test]**
- **Given**: [Duration/endurance condition]
- **When**: [Operation repeated over time]
- **Then**: Metric remains stable ([variance threshold])
```

### Verification Method

Add this section to each REQ-NF issue:

```markdown
## Verification Method

**Primary Method**: Test + Analysis

**Test Strategy**:
- Performance Testing: [Tool - JMeter, custom harness, profiler]
- Load Testing: [Concurrent users/requests]
- Monitoring: [APM tool, instrumentation]

**Analysis**:
- Static Analysis: [Tool - code complexity, security scan]
- Mathematical Proof: [For algorithms, timing calculations]

**Success Criteria**:
- All metrics meet or exceed targets
- No degradation under expected load
- Performance stable over [duration]
```

---

## 🎯 Quick Examples by Requirement Type

### Example 1: IOCTL Requirement (REQ-F-IOCTL-PHC-001)

**Requirement**: System shall provide IOCTL to query PTP Hardware Clock (PHC) current time

```markdown
## Acceptance Criteria

### Scenario 1: Query PHC time successfully
**Given** Intel i225 adapter with PHC enabled
**And** Driver loaded and bound to adapter
**When** User-mode application sends IOCTL_AVB_PHC_QUERY
**Then** IOCTL returns STATUS_SUCCESS
**And** Response contains 64-bit TAI timestamp (nanoseconds)
**And** Timestamp is within ±1ms of system time (sanity check)
**And** Response time is <500µs (p95)

### Scenario 2: Query PHC from multiple threads concurrently
**Given** PHC operational
**When** 10 threads simultaneously send IOCTL_AVB_PHC_QUERY
**Then** All 10 requests succeed with STATUS_SUCCESS
**And** Timestamps are monotonically increasing
**And** No race conditions or deadlocks occur

### Scenario 3: Query PHC when adapter not bound
**Given** No adapter bound to filter
**When** User-mode application sends IOCTL_AVB_PHC_QUERY
**Then** IOCTL returns STATUS_DEVICE_NOT_READY
**And** Error logged with actionable message

### Scenario 4: Query PHC when hardware unavailable
**Given** Adapter bound but PHC hardware inaccessible
**When** IOCTL_AVB_PHC_QUERY sent
**Then** IOCTL returns STATUS_IO_DEVICE_ERROR
**And** Error logged with hardware context

## Verification Method

**Primary Method**: Test

**Test Levels**:
- Unit Test: `test_ioctl_phc_query()` validates handler logic
- Integration Test: End-to-end IOCTL from user mode to hardware
- Performance Test: Measure p50, p95, p99 latency under load

**Success Criteria**:
- All 4 scenarios pass
- Latency <500µs (p95) under 100 req/sec load
- Zero race conditions in 10,000 concurrent requests
```

---

### Example 2: Performance Requirement (REQ-NF-PERF-PHC-001)

**Requirement**: PHC query latency shall be <500µs (p95)

```markdown
## Acceptance Criteria

### Measurable Metrics

| Metric | Target | Measurement Method | Acceptance Threshold |
|--------|--------|-------------------|---------------------|
| p50 latency | <200µs | High-resolution timer (QueryPerformanceCounter) | ≤200µs |
| p95 latency | <500µs | High-resolution timer | ≤500µs |
| p99 latency | <1ms | High-resolution timer | ≤1ms |
| Max latency | <5ms | High-resolution timer | ≤5ms (outlier rejection) |

### Specific Scenarios

**Scenario 1: Normal Load (100 queries/second)**
- **Given**: Single-threaded application querying PHC at 100 Hz
- **When**: 10,000 queries executed
- **Then**: p95 latency ≤500µs, p50 ≤200µs

**Scenario 2: Concurrent Load (10 threads, 1000 queries/second total)**
- **Given**: 10 threads querying PHC in parallel
- **When**: Each thread executes 1,000 queries
- **Then**: p95 latency ≤500µs (aggregated across threads)

**Scenario 3: Stress Test (1000 queries/second for 1 hour)**
- **Given**: High-frequency polling for extended duration
- **When**: 3.6 million queries over 1 hour
- **Then**: p95 latency remains ≤500µs, no degradation over time

## Verification Method

**Primary Method**: Test

**Test Strategy**:
- Custom performance harness with QueryPerformanceCounter
- Statistical analysis (histogram, percentiles, outliers)
- Continuous monitoring during stress test

**Success Criteria**:
- p95 ≤500µs in all scenarios
- Latency distribution stable (no drift over time)
- No outliers >5ms (excluding OS scheduler delays)
```

---

### Example 3: Reliability Requirement (REQ-NF-REL-PHC-001)

**Requirement**: PHC shall maintain monotonicity (never go backwards)

```markdown
## Acceptance Criteria

### Measurable Metrics

| Metric | Target | Measurement Method | Acceptance Threshold |
|--------|--------|-------------------|---------------------|
| Monotonicity violations | 0 | Compare successive PHC reads | Zero violations in 10M reads |
| Backward jumps | 0 | Timestamp delta analysis | Zero negative deltas |
| Frequency drift | ≤±100 ppb | Compare PHC vs. system time over 24h | Within spec |

### Specific Scenarios

**Scenario 1: Sequential reads never decrease**
- **Given**: PHC operational
- **When**: Read PHC 10 million times sequentially
- **Then**: Every timestamp T[n+1] ≥ T[n] (no backward jumps)

**Scenario 2: Concurrent reads maintain ordering**
- **Given**: 10 threads reading PHC in parallel
- **When**: Each thread reads 1 million times
- **Then**: Timestamps from all threads form globally monotonic sequence

**Scenario 3: Monotonicity during frequency adjustment**
- **Given**: PHC running normally
- **When**: Frequency adjusted by +100 ppb
- **Then**: Timestamp continues monotonically (no step backward)

**Scenario 4: Monotonicity after driver reload**
- **Given**: PHC running with known timestamp
- **When**: Driver unloaded and reloaded
- **Then**: New PHC value ≥ previous value (no reset to zero)

## Verification Method

**Primary Method**: Test + Analysis

**Test Strategy**:
- Automated test: 10M sequential PHC reads, verify T[n+1] ≥ T[n]
- Concurrent test: Multi-threaded reads, timestamp ordering analysis
- Long-duration test: 24-hour monotonicity check

**Analysis**:
- Code review: Verify no code paths write backward timestamp
- Static analysis: Verify atomic 64-bit read operations

**Success Criteria**:
- Zero monotonicity violations in 10M+ reads
- Zero violations during frequency adjustments
- Zero violations across driver reloads
```

---

## ✅ Checklist for Adding Acceptance Criteria

When updating a requirement issue with acceptance criteria:

- [ ] **Identify requirement type**: REQ-F (functional) or REQ-NF (non-functional)
- [ ] **Use appropriate template**: Gherkin (REQ-F) or Metrics (REQ-NF)
- [ ] **Define 3-4 scenarios**: Success path, alternative, error handling, boundary
- [ ] **Make criteria measurable**: Numbers, thresholds, objective pass/fail
- [ ] **Specify verification method**: Test / Analysis / Inspection / Demonstration
- [ ] **Define test levels**: Unit, Integration, System, Performance
- [ ] **Set success criteria**: Coverage targets, performance thresholds, defect limits
- [ ] **Link to test plan**: Reference System Acceptance or Qualification Test Plan section
- [ ] **Review with stakeholders**: Ensure criteria match expectations
- [ ] **Update traceability**: Link requirement → acceptance criteria → test cases

---

## 🚀 Batch Update Strategy

To efficiently add acceptance criteria to 64 requirements:

### Phase 1: Critical Requirements (P0) - ~15 requirements
- Focus on core functionality (PTP, NDIS, gPTP)
- Complete acceptance criteria first
- Validate with detailed scenarios

### Phase 2: High Priority (P1) - ~25 requirements
- Use templates with minor customization
- Group similar requirements (all IOCTLs, all NDIS callbacks)

### Phase 3: Medium/Low Priority (P2/P3) - ~24 requirements
- Streamlined criteria for less critical features
- Reference similar requirements for consistency

---

**Next Steps**:
1. Identify which requirements need acceptance criteria added
2. Use this template to systematically update each requirement issue
3. Cross-reference with System Acceptance Test Plan and System Qualification Test Plan
4. Validate Phase 02 exit criteria completion
