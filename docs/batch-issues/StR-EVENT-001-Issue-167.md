# StR-EVENT-001: Event-Driven Time-Sensitive Networking Monitoring

**Issue**: #167  
**Type**: Stakeholder Requirement  
**Priority**: Critical  
**Status**: status:backlog  
**Phase**: Phase 01 - Stakeholder Requirements

## Stakeholder Source

**Stakeholder Class**: System Integrators, Pro Audio Vendors (Milan), Industrial Automation (IEC 60802)

**Representatives**:
- Pro Audio TSN System Integrators implementing Milan standard
- Industrial Automation vendors requiring IEC/IEEE 60802 compliance
- Real-time system developers requiring sub-microsecond precision

## Business Justification

**Problem**: Polling-based approaches introduce unpredictable latency and resource waste that violate time-sensitive networking requirements. Sub-microsecond timing precision cannot be achieved with cyclic polling.

**Impact**: 
- Milan standard compliance REQUIRES immediate event-driven responses for gPTP synchronization
- IEC 60802 industrial automation demands deterministic, interrupt-driven timing
- Polling introduces jitter that compromises synchronization accuracy

**Competitive Gap**: Industry-standard TSN implementations use hardware interrupt-driven event architectures for PTP/gPTP, AVTP stream monitoring, and ATDECC control plane operations.

**ROI**: Event-driven architecture enables:
- Standards compliance (Milan, IEC 60802) opening new market segments
- Reduced CPU overhead (no polling waste)
- Improved system reliability through immediate fault detection

## Success Criteria

1. System achieves sub-microsecond event notification latency for timing-critical events
2. Zero CPU overhead from polling during normal operation
3. Hardware interrupt-driven timestamp capture for PTP event messages
4. Full compliance with Milan and IEC/IEEE 60802 event notification requirements
5. Observer pattern enables multiple subscribers without tight coupling

## Constraints

- Hardware must support interrupt-driven timestamp capture (Intel I210/I225 TSN NICs)
- Event notification latency must not exceed 1 microsecond for timing-critical events
- System must handle event bursts without dropping events
- Event architecture must support Windows kernel driver environment (NDIS filter)

## Priority

**Critical** - Blocks Milan/IEC 60802 standards compliance

## Traceability

- Traces to: N/A (root stakeholder requirement)
- **Refined by**: (will be linked when REQ issues created)
- **Implemented by**: (will be linked when implementation complete)
- **Verified by**: (will be linked when test cases created)

## References

- IEEE 802.1AS (gPTP timing synchronization)
- IEEE 1722 (AVTP Audio Video Transport Protocol)
- IEEE 1722.1 (ATDECC/AVDECC Device Discovery and Control)
- AVNU Alliance Milan Protocol Stack Specification
- IEC/IEEE 60802 (TSN Profile for Industrial Automation)
- Documentation: `docs/input_EventEmitting.md`, `docs/input_eventEmitting4Milan and 60802.md`

## Traceability Update

**Refined by** (System Requirements):
- #168 REQ-F-EVENT-001: Emit PTP Hardware Timestamp Capture Events
- #169 REQ-F-EVENT-002: Emit AVTP Timestamp Uncertain Bit Change Events
- #162 REQ-F-EVENT-003: Emit ATDECC Unsolicited Notification Events
- #164 REQ-F-EVENT-004: Emit Diagnostic Counter Events for AVTP Stream Errors
- #165 REQ-NF-EVENT-001: Event Notification Latency Requirement
- #161 REQ-NF-EVENT-002: Zero Polling Overhead Requirement

**Addressed by** (Architecture Decisions):
- #163 ADR-EVENT-001: Observer Pattern for Event Distribution
- #166 ADR-EVENT-002: Hardware Interrupt-Driven Event Capture for PTP Timestamps

## Phase 04 Design Review - APPROVED ✅

**Review Date**: 2025-12-17  
**Reviewer**: Design Review Board  
**Status**: **APPROVED for Phase 05 Implementation**

### Overall Assessment

The Phase 04 Detailed Design artifacts form a **highly coherent, standards-compliant, and robust architectural foundation**, successfully transitioning the project toward an event-driven model essential for achieving hard real-time performance requirements.

### Key Strengths

1. **Event-Driven Core (Observer Pattern)**
   - Correct architectural choice to meet sub-microsecond latency goals
   - Replaces inefficient polling with interrupt-driven event delivery
   - Target: < 1.3 µs total latency (hardware event → observer notification)

2. **Standards Integration**
   - **PTP Handler (#171)**: Leverages Hardware Access Wrappers for atomic timestamp capture (IEEE 802.1AS)
   - **AVTP Monitor (#173)**: Monitors timestamp uncertain (tu) bit for Grandmaster tracking (IEEE 1722/Milan)
   - **ATDECC Dispatcher (#170)**: Handles Unsolicited Notifications (AENs) per IEEE 1722.1

3. **High-Quality Design Practices**
   - Strategy Pattern for device abstraction
   - Observer Pattern for event decoupling
   - Test-Driven Development (all TEST issues created with traceability)
   - Rigorous verification strategy (oscilloscope latency measurement)

### Recommendation

**APPROVED** for Phase 05 Implementation with the following confirmation:
- Development team acknowledges MDIO operations are explicitly excluded from "Zero Polling Overhead" requirement
- This exclusion is documented in design summary and TEST-EVENT-006

### Next Steps

**Phase 05: TDD Implementation**
1. Implement ARC-C #172 (Event Subject/Observer Infrastructure) - P0
2. Implement ARC-C #171 (PTP Timestamp Handler) - P0
3. Implement ARC-C #173 (AVTP Stream Monitor) - P1
4. Implement ARC-C #170 (ATDECC Event Dispatcher) - P1

**Follow Red-Green-Refactor Workflow**:
- Red: Write failing test (from TEST issue acceptance criteria)
- Green: Implement minimal code to pass
- Refactor: Clean up while keeping tests green

**Date**: 2025-12-17  
**Approved By**: Design Review Board
