# ADR Linkage Analysis - 36 Unlinked Requirements

Based on analysis of requirements without ADR linkage, here are the architectural decisions each should reference:

## EVENT-RELATED (2 requirements)

### #19: REQ-F-TSRING-001: Shared Memory Ring Buffer for Timestamp Events
**Should link to:**
- #93: ADR-PERF-004: Kernel-Mode Shared Memory Ring Buffer (EXACT MATCH)
- #147: ADR Event Emission Architecture

### #68: REQ-F-EVENT-LOG-001: Windows Event Log Integration  
**Should link to:**
- #131: ADR-ERR-001: Error Reporting Strategy (logging architecture)

## PTP/TIMESTAMPING (4 requirements)

### #25: REQ-F-PTP-IOCTL-001: Production-Safe PTP Clock Control IOCTLs
**Should link to:**
- #118: ADR-PERF-SEC-001: Performance vs Security Trade-offs in IOCTL Path
- #122: ADR-SEC-001: Debug vs Release Security Boundaries

### #70: REQ-F-IOCTL-PHC-002: PHC Time Set (Initialization Only)
**Should link to:**
- #122: ADR-SEC-001: Debug vs Release Security Boundaries (initialization-only = security decision)

### #81: REQ-F-ERROR-RECOVERY-001: Hardware Error Recovery (Link Down, PHC Fault)
**Should link to:**
- #131: ADR-ERR-001: Error Reporting Strategy
- **MISSING ADR**: Need "ADR-RECOVERY-001: Hardware Fault Recovery Strategy"

### #119: REQ-F-GPTP-COMPAT-001: Compatibility with External gPTP Daemons
**Should link to:**
- #117: ADR-SCOPE-001: gPTP Architecture Separation (EXACT MATCH)

## IOCTL-RELATED (2 requirements)

### #64: REQ-F-IOCTL-VERSIONING-001: IOCTL API Versioning
**Should link to:**
- #123: ADR-SSOT-001: Single Source of Truth for IOCTL Headers
- **MISSING ADR**: Need "ADR-IOCTL-VERSION-001: IOCTL API Versioning Strategy"

### #74: REQ-F-IOCTL-BUFFER-001: IOCTL Buffer Validation and Memory Safety
**Should link to:**
- #118: ADR-PERF-SEC-001: Performance vs Security Trade-offs (buffer validation = security)

## DEVICE/HARDWARE (7 requirements)

### #40: REQ-F-DEVICE-ABS-003: Register Access via Device Abstraction Layer
**Should link to:**
- #92: ADR-ARCH-003: Device Abstraction via Strategy Pattern
- #126: ADR-HAL-001: Hardware Abstraction Layer

### #44: REQ-F-HW-DETECT-001: Hardware Capability Detection
**Should link to:**
- #126: ADR-HAL-001: Hardware Abstraction Layer

### #52: REQ-F-DEVICE-ABS-005: Thread-Safe Device Operations
**Should link to:**
- #92: ADR-ARCH-003: Device Abstraction via Strategy Pattern

### #55: REQ-F-DEVICE-ABS-001: Device Abstraction Layer Initialization
**Should link to:**
- #92: ADR-ARCH-003: Device Abstraction via Strategy Pattern
- #126: ADR-HAL-001: Hardware Abstraction Layer

### #56: REQ-F-DEVICE-ABS-002: Device Capability Query Interface
**Should link to:**
- #92: ADR-ARCH-003: Device Abstraction via Strategy Pattern
- #126: ADR-HAL-001: Hardware Abstraction Layer

### #76: REQ-F-MULTIPLE-NIC-001: Support for Multiple Intel NICs Simultaneously
**Should link to:**
- #134: ADR-MULTI-001: Multi-NIC Support Architecture (EXACT MATCH)

### #79: REQ-F-HARDWARE-QUIRKS-001: Hardware-Specific Workarounds (Errata Handling)
**Should link to:**
- #126: ADR-HAL-001: Hardware Abstraction Layer (quirks = HAL responsibility)

## TSN FEATURES (1 requirement)

### #120: REQ-F-FPE-001: Frame Preemption (IEEE 802.1Qbu/802.3br)
**Should link to:**
- #133: ADR-TSN-001: TSN Feature Prioritization (FPE = TSN feature)

## NON-FUNCTIONAL (10 requirements)

### #46: REQ-NF-PERF-NDIS-001: Packet Forwarding Performance <1us
**Should link to:**
- #90: ADR-ARCH-001: NDIS 6.0 Compatibility (performance constraint)
- #91: ADR-PERF-002: Direct BAR0 MMIO Access (<1us latency requirement)

### #59: REQ-NF-REL-DRIVER-001: Driver Stability and IOCTL Error Handling
**Should link to:**
- #131: ADR-ERR-001: Error Reporting Strategy

### #61: REQ-NF-INTEROP-MILAN-001: AVnu Milan Interoperability
**Should link to:**
- **MISSING ADR**: Need "ADR-INTEROP-001: Milan Interoperability Strategy"

### #62: REQ-NF-STD-NAMING-001: Standards-Compliant Function Naming
**Should link to:**
- **MISSING ADR**: Need "ADR-NAMING-001: Naming Convention Standards" OR update existing docs

### #63: REQ-NF-INTEROP-TSN-001: IEEE 802.1 TSN Conformance
**Should link to:**
- #133: ADR-TSN-001: TSN Feature Prioritization (conformance = strategic decision)

### #69: REQ-NF-COMPAT-WIN10-001: Windows 10/11 Compatibility
**Should link to:**
- #90: ADR-ARCH-001: NDIS 6.0 Compatibility (Windows compatibility decision)

### #71: REQ-NF-PERF-IOCTL-001: IOCTL Latency <100us
**Should link to:**
- #118: ADR-PERF-SEC-001: Performance vs Security Trade-offs (latency requirement)
- #91: ADR-PERF-002: Direct BAR0 MMIO Access (low-latency architecture)

### #72: REQ-NF-SEC-IOCTL-001: IOCTL Access Control
**Should link to:**
- #122: ADR-SEC-001: Debug vs Release Security Boundaries

### #85: REQ-NF-MAINTAIN-001: Code Maintainability Standards
**Should link to:**
- **MISSING ADR**: Need "ADR-MAINT-001: Code Quality and Maintainability Standards"

### #86: REQ-NF-DOC-DEPLOY-001: Deployment Guide Documentation
**Should link to:**
- #135: ADR-DOC-001: Documentation Generation Architecture

## OTHER FUNCTIONAL (10 requirements)

### #51: REQ-F-NAMING-001: IEEE 802.1AS Naming Conventions
**Should link to:**
- **Same as #62** - need naming standards ADR

### #53: REQ-F-INTEL-AVB-004: Error Handling and Status Codes
**Should link to:**
- #131: ADR-ERR-001: Error Reporting Strategy

### #54: REQ-F-TSN-SEMANTICS-001: TSN vs AVB Terminology
**Should link to:**
- #133: ADR-TSN-001: TSN Feature Prioritization (terminology = strategic decision)

### #66: REQ-F-POWER-MGMT-001: Power Management (Sleep/Wake)
**Should link to:**
- **MISSING ADR**: Need "ADR-POWER-001: Power Management Strategy"

### #67: REQ-F-STATISTICS-001: Driver Statistics Counters
**Should link to:**
- **MISSING ADR**: Need "ADR-TELEMETRY-001: Telemetry and Statistics Architecture"

### #75: REQ-F-HOT-PLUG-001: Hot-Plug Detection and Adapter Enumeration
**Should link to:**
- #134: ADR-MULTI-001: Multi-NIC Support (hot-plug = multi-NIC concern)

### #77: REQ-F-FIRMWARE-DETECT-001: Firmware Version Detection
**Should link to:**
- #126: ADR-HAL-001: Hardware Abstraction Layer (firmware detection = HAL)

### #78: REQ-F-CONFIG-PERSIST-001: Configuration Persistence
**Should link to:**
- **MISSING ADR**: Need "ADR-CONFIG-001: Configuration Management Strategy"

### #80: REQ-F-GRACEFUL-DEGRADE-001: Graceful Degradation
**Should link to:**
- #131: ADR-ERR-001: Error Reporting Strategy (degradation = error handling)

### #83: REQ-F-PERF-MONITOR-001: Performance Counter Monitoring
**Should link to:**
- **MISSING ADR**: Need "ADR-TELEMETRY-001: Telemetry and Statistics Architecture"

---

## Summary

**Can link to existing ADRs**: 26 requirements  
**Need NEW ADRs**: 10 requirements (8 distinct new ADRs needed)

### New ADRs Required:
1. **ADR-RECOVERY-001**: Hardware Fault Recovery Strategy (#81)
2. **ADR-IOCTL-VERSION-001**: IOCTL API Versioning Strategy (#64)
3. **ADR-INTEROP-001**: Milan Interoperability Strategy (#61)
4. **ADR-NAMING-001**: Naming Convention Standards (#51, #62)
5. **ADR-MAINT-001**: Code Quality/Maintainability Standards (#85)
6. **ADR-POWER-001**: Power Management Strategy (#66)
7. **ADR-TELEMETRY-001**: Telemetry/Statistics Architecture (#67, #83)
8. **ADR-CONFIG-001**: Configuration Management Strategy (#78)

### Quick Wins (Add links to existing ADRs):
- Device abstraction reqs (#40, #52, #55, #56) → #92, #126
- Multi-NIC reqs (#76, #75) → #134
- Error handling reqs (#68, #59, #53, #80) → #131
- Security reqs (#25, #70, #72, #74) → #118, #122
- Performance reqs (#46, #71) → #90, #91, #118
- TSN reqs (#120, #63, #54) → #133
