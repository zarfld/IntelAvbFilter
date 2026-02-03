# REQ-NF-EVENT-002: Zero Polling Overhead Requirement

**Issue**: #161  
**Type**: Non-Functional Requirement  
**Priority**: P0 (Critical)  
**Status**: status:backlog  
**Phase**: Phase 02 - Requirements

## Description

Event notification system must support sustained event rates of up to 10,000 events per second without message loss or latency degradation. This ensures the system can handle bursts of AVB/TSN events (e.g., rapid timestamp captures during stream startup) without compromising real-time guarantees.

## Acceptance Criteria

**Given** the event notification system is operational  
**When** events are generated at 10,000 events/second sustained rate  
**Then** the system must:
- Deliver 100% of events without loss
- Maintain <10µs latency for each event (per #165)
- Consume <5% CPU overhead on consumer thread
- Not exceed 16 MB ring buffer memory footprint

**Load Test Scenarios**:
1. **Sustained load**: 10,000 events/sec for 60 seconds (600,000 events total)
2. **Burst load**: 0 events for 5 sec, then 50,000 events over 1 sec, repeat
3. **Mixed event types**: Timestamp, link state, diagnostic events intermixed

**Success Criteria**:
- Zero events lost (ring buffer overrun counter = 0)
- 99.99th percentile latency <15µs (allows for occasional scheduler jitter)
- Average latency <10µs

## Constraints

- Ring buffer size: 4096 events (configurable up to 16,384)
- Event payload: ≤128 bytes per event
- Total memory footprint: ≤16 MB (ring buffer + metadata)
- CPU overhead: <5% on event consumer thread at peak load

## Dependencies

- Depends on: #167 (StR-EVENT-001: Event-driven architecture requirement)
- Depends on: #19 (REQ-F-TSRING-001: Shared memory ring buffer)
- Depends on: #165 (REQ-NF-EVENT-001: Latency constraint <10µs)

## Traceability

- Traces to: #167 (StR-EVENT-001: Real-Time Event Notifications for Critical AVB/TSN State Changes)
- Verified by: #241 (TEST-EVENT-NF-002: Verify Event Throughput ≥10,000 events/sec)

## Related Requirements

- #168 (REQ-F-EVENT-001: PTP timestamp events - high frequency event source)
- #164 (REQ-F-EVENT-004: Diagnostic counter events - burst event source)
- #162 (REQ-F-EVENT-003: Link state events - low frequency event source)

## Architecture Links

- #163 (ADR-EVENT-001: Event notification architecture)
- #166 (ADR-EVENT-002: Lock-free ring buffer design for zero-copy event delivery)
- #171 (ARC-C-EVENT-001: Event dispatcher component)

## Notes

**Throughput Analysis**:
- 10,000 events/sec = 1 event every 100µs
- With <10µs latency constraint, processing must complete in <10% of inter-event time
- This leaves 90µs margin for producer/consumer synchronization

**Ring Buffer Sizing**:
```
Events/sec: 10,000
Ring size: 4,096 events
Buffer full time: 4096 / 10,000 = 409.6 ms
```
If consumer lags by >409ms, ring buffer overruns and events are lost.

**Backpressure Strategy**:
- **Option 1**: Drop oldest events (FIFO overwrite) → Acceptable for diagnostic counters
- **Option 2**: Block producer (wait for space) → Unacceptable (violates real-time constraint)
- **Option 3**: Larger ring buffer (16K events) → 1.6 second buffer time
- **Recommendation**: Use Option 1 with overrun counter visible to applications

**Memory Footprint**:
```
Event size: 128 bytes
Ring size: 16,384 events (max)
Total: 16,384 * 128 = 2,097,152 bytes (~2 MB ring buffer)
Add metadata: ~16 MB total (includes producer/consumer indices, locks)
```

**CPU Overhead**:
- Event production: <5µs (ISR + ring buffer write)
- Event consumption: ~5µs per event (user-space callback dispatch)
- At 10K events/sec: 5µs * 10,000 = 50ms/sec = 5% CPU utilization
- Acceptable overhead for real-time event system

**Testing**:
- #241 (TEST-EVENT-NF-002) validates sustained throughput under load
- Stress test: Multiple event sources firing simultaneously
- Failure detection: Ring buffer overrun counter increment

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
