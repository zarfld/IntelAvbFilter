# REQ-F-EVENT-001: Emit PTP Hardware Timestamp Capture Events

**Issue**: #168  
**Type**: Functional Requirement  
**Priority**: P0 (Critical)  
**Status**: In Progress  
**Phase**: 02 - Requirements Analysis

## Description

The NDIS filter driver must emit hardware timestamp capture events immediately when the Intel NIC captures a PTP hardware timestamp (TX or RX), enabling user-space applications to correlate timestamps with packet transmission/reception in real-time.

## Rationale

IEEE 802.1AS (gPTP) and IEEE 1588 (PTP) require precise timestamp correlation between hardware capture events and packet processing. Applications cannot poll for timestamps without introducing unacceptable latency and jitter. Event-driven notifications ensure deterministic timestamp delivery within hardware interrupt latency bounds.

## Acceptance Criteria

**Given** a packet transmitted with hardware timestamping enabled  
**When** the NIC captures the TX hardware timestamp  
**Then** the driver shall emit a TX timestamp capture event within 10µs of hardware interrupt  
**And** the event shall include timestamp value, packet identifier, and capture metadata

**Given** a packet received with RX timestamping enabled  
**When** the NIC captures the RX hardware timestamp  
**Then** the driver shall emit an RX timestamp capture event within 10µs of hardware interrupt  
**And** the event shall include timestamp value, packet metadata, and queue identifier

**Given** a user-space gPTP daemon subscribed to timestamp events  
**When** timestamp capture events are emitted  
**Then** the daemon shall receive events via zero-copy shared memory ring buffer (REQ-F-TSRING-001)  
**And** event delivery latency shall not exceed 50µs end-to-end (hardware interrupt + ring buffer notification)

## Constraints

- Event payload: ≤128 bytes (timestamp, metadata, packet ID)
- Event rate: Up to 10,000 events/second (line-rate PTP traffic)
- Interrupt-driven: Events triggered by hardware timestamp capture interrupts (TSICR register)
- Zero polling: User-space applications must not poll; events must push notifications

## Dependencies

- Depends on: #167 (StR-EVENT-001: Event-driven architecture for time-sensitive networking)
- Depends on: #19 (REQ-F-TSRING-001: Shared memory ring buffer for timestamp events)
- Depends on: #5 (REQ-F-PTP-003: Hardware timestamping control)

## Traceability

- Traces to: #167 (StR-EVENT-001: Real-Time Event Notifications for Critical AVB/TSN State Changes)
- Verified by: #237 (TEST-EVENT-001: Verify PTP Hardware Timestamp Capture Events)

## Related Requirements

- #164 (REQ-F-EVENT-004: Diagnostic counter events for AVTP streams)
- #162 (REQ-F-EVENT-003: ATDECC unsolicited notification events)
- #149 (REQ-F-PTP-001: Hardware timestamp correlation for PTP/gPTP)

## Architecture Links

- #163 (ADR-EVENT-001: Observer pattern for event distribution)
- #166 (ADR-EVENT-002: Hardware interrupt-driven event pipeline)
- #171 (ARC-C-EVENT-002: PTP hardware timestamp event handler)

## Notes

- Intel i210/i211/i225 NICs provide TSICR (Timestamp Interrupt Cause Register) to signal timestamp capture
- TX timestamps: Triggered by TSYNCTXCTL.VALID flag after packet transmission
- RX timestamps: Triggered by TSYNCRXCTL.VALID flag after packet reception
- Must coordinate with #13 (REQ-F-TS-SUB-001: Timestamp event subscription) for subscription management
