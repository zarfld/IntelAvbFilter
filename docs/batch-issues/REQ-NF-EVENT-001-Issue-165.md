# REQ-NF-EVENT-001: Event Notification Latency Requirement

**Issue**: #165  
**Type**: Non-Functional Requirement  
**Priority**: P0 (Critical)  
**Status**: status:backlog  
**Phase**: Phase 02 - Requirements

## Description

Event notification system must guarantee delivery latency of less than 10 microseconds from hardware event occurrence to application notification. This is a critical non-functional requirement to support real-time audio/video streaming where timing jitter directly impacts quality.

## Acceptance Criteria

**Given** a hardware event (e.g., timestamp capture, link state change, or diagnostic counter threshold)  
**When** the event is triggered by the NIC hardware  
**Then** the filter driver must deliver the event notification to the subscribed application within 10µs (measured from hardware interrupt to user-space callback invocation)

**Measurement Method**:
- Use GPIO toggling synchronized with hardware events
- Measure latency using oscilloscope (hardware interrupt assertion to user-space event delivery)
- Test under various system loads (idle, moderate CPU load, memory pressure)

## Constraints

- Interrupt Service Routine (ISR) execution time: <5µs (hard real-time constraint)
- Total interrupt-to-notification path: <10µs (end-to-end latency budget)
- Event delivery must use lock-free ring buffer to avoid kernel lock contention
- Supports up to 10,000 events/second without latency degradation

## Dependencies

- Depends on: #167 (StR-EVENT-001: Stakeholder requirement for event notifications)
- Depends on: #19 (REQ-F-TSRING-001: Shared memory ring buffer)
- Depends on: #163 (ADR-EVENT-001: Event notification architecture decision)

## Traceability

- Traces to: #167 (StR-EVENT-001: Real-Time Event Notifications for Critical AVB/TSN State Changes)
- Verified by: #245 (TEST-EVENT-NF-001: Verify Event Notification Latency <10µs)

## Related Requirements

- #168 (REQ-F-EVENT-001: PTP hardware timestamp events) - latency budget consumer
- #162 (REQ-F-EVENT-003: Link state change events) - latency budget consumer
- #164 (REQ-F-EVENT-004: Diagnostic counter events) - latency budget consumer

## Architecture Links

- #163 (ADR-EVENT-001: Event notification architecture)
- #166 (ADR-EVENT-002: Lock-free ring buffer design)
- #171 (ARC-C-EVENT-001: Event dispatcher component)

## Notes

**Temporal Constraints**:
- ISR terse constraint: <5µs (log event to ring buffer, signal user-space)
- User-space wakeup overhead: ~3-4µs (IOCP or keyed event signaling)
- Application callback dispatch: ~1-2µs (function call overhead)
- **Total budget: 10µs** (5µs ISR + 5µs notification path)

**Measurement Strategy**:
1. GPIO pin toggled in ISR (marks hardware event time)
2. GPIO pin toggled in user-space callback (marks delivery time)
3. Oscilloscope measures pulse width = end-to-end latency

**Risk Factors**:
- Windows scheduler non-determinism (mitigated by high-priority threads)
- IRQL transitions (mitigated by terse ISR design)
- Memory allocations in hot path (eliminated - preallocated ring buffer)

**Validation Approach**:
- #245 (TEST-EVENT-NF-001) provides empirical latency measurements
- Must pass under 3 load conditions: idle, CPU load, memory pressure
- Failure criterion: Any latency sample >10µs triggers test failure

## Comments

### Comment 1 (github-actions)
## ❌ Traceability Link Missing

This requirement must link to its parent issue.

### Required Action

Add the following to your issue body:

```markdown
## Traceability
- Traces to:  #N (parent issue)
```

### Traceability Rules

- **REQ-F / REQ-NF** → Link to parent StR issue
- **ADR** → Link to requirements it satisfies
- **ARC-C** → Link to ADRs and requirements
- **TEST** → Link to requirements being verified
- **QA-SC** → Link to related requirements

**Standard**: ISO/IEC/IEEE 29148:2018 (Bidirectional Traceability)

### Comment 2-8 (github-actions)
## 🤖 Status Sync
    
**Automatic status update** based on issue body:
- **Body status**: Not specified
- **Previous label(s)**: (none)
- **New label**: `status:backlog`

This sync ensures consistency between issue status and labels for tracking purposes.

---
*Automated by [project-sync.yml](.github/workflows/project-sync.yml)*
