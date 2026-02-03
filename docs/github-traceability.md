Generating report for: zarfld/IntelAvbFilter

# Requirements Traceability Matrix

**Repository**: zarfld/IntelAvbFilter
**Generated**: 2026-02-02 10:01:26 UTC
**Standard**: ISO/IEC/IEEE 29148:2018

## Summary

Total requirements: **312**

### By Type

- **ADR**: 19
- **ARC-C**: 19
- **DESIGN**: 7
- **QA-SC**: 28
- **REQ**: 4
- **REQ-F**: 62
- **REQ-NF**: 33
- **StR**: 8
- **TEST**: 128
- **UNKNOWN**: 4

### By State

- **Open**: 292
- **Closed**: 20

## Traceability Matrix

| Issue | Type | Title | State | Traces To | Depends On | Verified By | Implemented By |
|-------|------|-------|-------|-----------|------------|-------------|----------------|
| #1 | StR | StR-HWAC-001: Intel NIC AVB/TSN Feature Access | 🔵 | - | - | - | - |
| #2 | REQ-F | REQ-F-PTP-001: PTP Hardware Clock Get/Set Times... | 🔵 | #1 | - | #295 | - |
| #3 | REQ-F | REQ-F-PTP-002: PTP Clock Frequency Adjustment (... | 🔵 | #33 | #2 | #192, #296 | - |
| #4 | REQ | BUG: IOCTL_AVB_GET_CLOCK_CONFIG Not Working (IO... | ✅ | #1 | - | - | - |
| #5 | REQ-F | REQ-F-PTP-003: Hardware Timestamping Control (I... | 🔵 | #1 | - | - | - |
| #6 | REQ-F | REQ-F-PTP-004: Rx Packet Timestamping Configura... | 🔵 | #1 | - | - | - |
| #7 | REQ-F | REQ-F-PTP-005: Target Time & Auxiliary Timestam... | 🔵 | #1 | #5, #12 | - | - |
| #8 | REQ-F | REQ-F-QAV-001: Credit-Based Shaper (IOCTL 35 - ... | 🔵 | #1 | - | #207 | - |
| #9 | REQ-F | REQ-F-TAS-001: Time-Aware Scheduler (IOCTL 26 -... | 🔵 | #1 | - | - | - |
| #10 | REQ-F | REQ-F-MDIO-001: MDIO/PHY Register Access (IOCTL... | 🔵 | #30 | - | - | - |
| #11 | REQ-F | REQ-F-FP-001 & PTM-001: Frame Preemption & PTM ... | 🔵 | #120 | - | - | - |
| #12 | REQ-F | REQ-F-DEV-001: Device Lifecycle Management (IOC... | 🔵 | #1 | - | - | - |
| #13 | REQ-F | REQ-F-TS-SUB-001: Timestamp Event Subscription ... | 🔵 | #117 | - | #314 | - |
| #14 | REQ | Master: All 25 IOCTL Requirements Tracking | 🔵 | #1 | - | - | - |
| #15 | REQ-F | REQ-F-MULTIDEV-001: Multi-Adapter Management an... | ✅ | #1 | - | - | - |
| #16 | REQ-F | REQ-F-LAZY-INIT-001: Lazy Initialization and On... | ✅ | #1 | - | - | - |
| #17 | REQ-NF | REQ-NF-DIAG-REG-001: Registry-Based Diagnostics... | ✅ | #1 | - | - | - |
| #18 | REQ-F | REQ-F-HWCTX-001: Hardware Context Lifecycle Sta... | ✅ | #1 | - | - | - |
| #19 | REQ-F | REQ-F-TSRING-001: Shared Memory Ring Buffer for... | 🔵 | #1 | - | - | - |
| #20 | REQ-F | REQ-DISCOVERY-001: Requirements Discovery Repor... | 🔵 | #1 | - | - | - |
| #21 | REQ-NF | REQ-NF-REGS-001: Eliminate Magic Numbers Using ... | 🔵 | #31 | - | - | - |
| #22 | REQ-NF | REQ-NF-SSOT-EVENT-001: Event ID Single Source o... | 🔵 | #2 | - | - | - |
| #23 | REQ-NF | REQ-NF-SEC-DEBUG-001: Debug-Only Raw Register A... | 🔵 | #2 | - | - | - |
| #24 | REQ-NF | REQ-NF-SSOT-001: Single Source of Truth (SSOT) ... | ✅ | #1 | - | #301 | - |
| #25 | REQ-F | REQ-F-PTP-IOCTL-001: Production-Safe PTP Clock ... | 🔵 | #95, #27 | - | - | - |
| #26 | REQ | REQ-DISCOVERY-002: Requirements Discovery Sessi... | 🔵 | #84, #24 | - | - | - |
| #27 | REQ-NF | REQ-NF-SCRIPTS-001: Consolidate and Organize 80... | ✅ | #31 | - | #292 | - |
| #28 | StR | StR-GPTP-HARDWARE-001: Hardware Abstraction for... | 🔵 | #31 | - | - | - |
| #29 | StR | StR-002: Intel AVB Register Abstraction Compati... | 🔵 | - | - | - | - |
| #30 | StR | StR-003: Standards Semantics Alignment (libmedi... | 🔵 | - | - | - | - |
| #31 | StR | StR-005: Future Windows PTP/TSN Management Serv... | 🔵 | - | - | - | - |
| #32 | StR | StR-004: NDIS Miniport Integration (Intel NIC D... | 🔵 | - | - | - | - |
| #33 | StR | StR-006: AVB/TSN Endpoint Interoperability (Mil... | 🔵 | - | - | - | - |
| #34 | REQ-F | REQ-F-IOCTL-PHC-001: PHC Time Query | 🔵 | #28, #28 | #29 | - | - |
| #35 | REQ-F | REQ-F-IOCTL-TS-001: TX Timestamp Retrieval | 🔵 | #28 | - | - | - |
| #36 | REQ-F | REQ-F-NDIS-ATTACH-001: FilterAttach/FilterDetach | 🔵 | #31 | - | - | - |
| #37 | REQ-F | REQ-F-IOCTL-TS-002: RX Timestamp Retrieval | 🔵 | #28 | - | - | - |
| #38 | REQ-F | REQ-F-IOCTL-PHC-003: PHC Offset Adjustment | 🔵 | #28 | - | - | - |
| #39 | REQ-F | REQ-F-IOCTL-PHC-004: PHC Frequency Adjustment | 🔵 | #28 | - | - | - |
| #40 | REQ-F | REQ-F-DEVICE-ABS-003: Register Access via Devic... | ✅ | #29 | - | - | - |
| #41 | REQ-F | REQ-F-PTP-EPOCH-001: PTP Epoch and Time Scale (... | 🔵 | #30 | - | - | - |
| #42 | REQ-F | REQ-F-NDIS-SEND-001: FilterSend / FilterSendNet... | 🔵 | #31 | - | - | - |
| #43 | REQ-F | REQ-F-NDIS-RECEIVE-001: FilterReceive / FilterR... | 🔵 | #31 | - | - | - |
| #44 | REQ-F | REQ-F-HW-DETECT-001: Hardware Capability Detection | 🔵 | #31 | - | - | - |
| #45 | REQ-F | REQ-F-REG-ACCESS-001: Safe Register Access via ... | 🔵 | #31 | - | - | - |
| #46 | REQ-NF | REQ-NF-PERF-NDIS-001: Packet Forwarding Perform... | 🔵 | #31 | - | #286 | - |
| #47 | REQ-NF | REQ-NF-REL-PHC-001: PHC Monotonicity and Reliab... | 🔵 | #31 | - | - | - |
| #48 | REQ-F | REQ-F-IOCTL-XSTAMP-001: Cross-Timestamp Query (... | 🔵 | #28 | - | - | - |
| #49 | REQ-F | REQ-F-CBS-001: Credit-Based Shaper (Qav) Config... | 🔵 | #33 | - | - | - |
| #50 | REQ-F | REQ-F-TAS-001: Time-Aware Shaper (Qbv) Configur... | 🔵 | #33 | - | - | - |
| #51 | REQ-F | REQ-F-NAMING-001: IEEE 802.1AS Naming Conventions | 🔵 | #30 | - | - | - |
| #52 | REQ-F | REQ-F-DEVICE-ABS-005: Thread-Safe Device Operat... | 🔵 | #40 | - | #279 | - |
| #53 | REQ-F | REQ-F-INTEL-AVB-004: Error Handling and Status ... | 🔵 | #29 | - | #280 | - |
| #54 | REQ-F | REQ-F-TSN-SEMANTICS-001: TSN vs AVB Terminology | 🔵 | #31 | - | #278 | - |
| #55 | REQ-F | REQ-F-DEVICE-ABS-001: Device Abstraction Layer ... | 🔵 | #33 | - | #277 | - |
| #56 | REQ-F | REQ-F-DEVICE-ABS-002: Device Capability Query I... | 🔵 | #29 | - | #275 | - |
| #57 | REQ-F | REQ-F-VLAN-001: VLAN Priority Code Point (PCP) ... | 🔵 | #31 | - | #276 | - |
| #58 | REQ-NF | REQ-NF-PERF-PHC-001: PHC Query Latency <500ns | 🔵 | #28 | - | #274 | - |
| #59 | REQ-NF | REQ-NF-REL-DRIVER-001: Driver Stability and IOC... | 🔵 | #29 | - | - | - |
| #60 | REQ-F | REQ-F-NIC-IDENTITY-002: NIC Identity Informatio... | 🔵 | #31 | - | - | - |
| #61 | REQ-NF | REQ-NF-INTEROP-MILAN-001: AVnu Milan Interopera... | 🔵 | #33 | - | - | - |
| #62 | REQ-NF | REQ-NF-STD-NAMING-001: Standards-Compliant Func... | 🔵 | #30 | - | - | - |
| #63 | REQ-NF | REQ-NF-INTEROP-TSN-001: IEEE 802.1 TSN Conformance | 🔵 | #33 | - | - | - |
| #64 | REQ-F | REQ-F-IOCTL-VERSIONING-001: IOCTL API Versioning | 🔵 | #31, #31, #31 | - | #273, #273 | - |
| #65 | REQ-NF | REQ-NF-PERF-TS-001: Timestamp Retrieval Latency... | 🔵 | #28 | - | #272 | - |
| #66 | REQ-F | REQ-F-POWER-MGMT-001: Power Management (Sleep/W... | 🔵 | #31 | - | #271 | - |
| #67 | REQ-F | REQ-F-STATISTICS-001: Driver Statistics Counters | 🔵 | #32 | - | #270 | - |
| #68 | REQ-F | REQ-F-EVENT-LOG-001: Windows Event Log Integration | 🔵 | #32 | - | #269 | - |
| #69 | REQ-NF | REQ-NF-COMPAT-WIN10-001: Windows 10/11 Compatib... | 🔵 | #31 | - | #268 | - |
| #70 | REQ-F | REQ-F-IOCTL-PHC-002: PHC Time Set (Initializati... | 🔵 | #28 | - | #266 | - |
| #71 | REQ-NF | REQ-NF-DOC-API-001: IOCTL API Documentation | 🔵 | #31, #31 | - | #267 | - |
| #72 | REQ-NF | REQ-NF-TEST-COVERAGE-001: Unit Test Coverage ≥80% | 🔵 | #28 | - | #265 | - |
| #73 | REQ-NF | REQ-NF-SEC-IOCTL-001: IOCTL Access Control | 🔵 | #31 | - | #264 | - |
| #74 | REQ-F | REQ-F-IOCTL-BUFFER-001: IOCTL Buffer Validation... | 🔵 | #31 | - | #263 | - |
| #75 | REQ-F | REQ-F-HOT-PLUG-001: Hot-Plug Detection and Adap... | 🔵 | #1 | - | #262 | - |
| #76 | REQ-F | REQ-F-MULTIPLE-NIC-001: Support for Multiple In... | 🔵 | #31 | - | #261 | - |
| #77 | REQ-F | REQ-F-FIRMWARE-DETECT-001: Firmware Version Det... | 🔵 | #31 | - | #260 | - |
| #78 | REQ-F | REQ-F-CONFIG-PERSIST-001: Configuration Persist... | 🔵 | #31 | - | #259 | - |
| #79 | REQ-F | REQ-F-HARDWARE-QUIRKS-001: Hardware-Specific Wo... | 🔵 | #31 | - | #258 | - |
| #80 | REQ-F | REQ-F-GRACEFUL-DEGRADE-001: Graceful Degradatio... | 🔵 | #31 | - | #257 | - |
| #81 | REQ-F | REQ-F-ERROR-RECOVERY-001: Hardware Error Recove... | 🔵 | #31 | - | #256 | - |
| #82 | REQ-F | REQ-F-DEBUG-TRACE-001: ETW Tracing for Driver D... | 🔵 | #31 | - | #255 | - |
| #83 | REQ-F | REQ-F-PERF-MONITOR-001: Performance Counter Mon... | 🔵 | #31 | - | #254 | - |
| #84 | REQ-NF | REQ-NF-PORTABILITY-001: Hardware Portability vi... | ✅ | #31 | - | - | - |
| #85 | REQ-NF | REQ-NF-MAINTAIN-001: Code Maintainability Stand... | 🔵 | #31 | - | #252 | - |
| #86 | REQ-NF | REQ-NF-DOC-DEPLOY-001: Deployment Guide and Tro... | 🔵 | #31 | - | #251 | - |
| #87 | REQ-NF | REQ-NF-TEST-INTEGRATION-001: Integration Test S... | 🔵 | #31 | - | #250 | - |
| #88 | REQ-NF | REQ-NF-COMPAT-NDIS-001: NDIS 6.50+ Compatibilit... | 🔵 | #31 | - | #249 | - |
| #89 | REQ-NF | REQ-NF-SECURITY-BUFFER-001: Buffer Overflow Pro... | 🔵 | #31 | - | #248 | - |
| #90 | ADR | ADR-ARCH-001: Use NDIS 6.0 (not NDIS 6.50+) for... | 🔵 | #31 | - | - | - |
| #91 | ADR | ADR-PERF-002: Direct BAR0 MMIO Access (Bypass M... | 🔵 | #2 | - | - | - |
| #92 | ADR | ADR-ARCH-003: Device Abstraction via Strategy P... | 🔵 | #1 | - | - | - |
| #93 | ADR | ADR-PERF-004: Kernel-Mode Shared Memory Ring Bu... | 🔵 | #6 | - | - | - |
| #94 | ARC-C | ARC-C-NDIS-001: NDIS Filter Core (Lifecycle & P... | 🔵 | #31 | - | - | - |
| #95 | REQ-NF | REQ-NF-DEBUG-REG-001: Registry-Based Debug Sett... | 🔵 | #31 | - | #247 | - |
| #96 | REQ-NF | REQ-NF-COMPAT-WDF-001: Windows Driver Framework... | 🔵 | #31 | - | #246 | - |
| #97 | REQ-NF | REQ-NF-COMPAT-NDIS-001: NDIS Compatibility and ... | 🔵 | #32 | #32, #18 | #242 | - |
| #98 | REQ-NF | REQ-NF-BUILD-CI-001: Continuous Integration and... | 🔵 | #27 | #27, #99 | #244 | - |
| #99 | REQ-NF | REQ-NF-BUILD-SIGN-001: Driver Code Signing and ... | 🔵 | #27 | #27, #98, #95 | #243 | - |
| #100 | ARC-C | ARC-C-I225-008: Intel i225/i226 Device Implemen... | 🔵 | #2 | - | - | - |
| #101 | ARC-C | ARC-C-I219-007: Intel i219 Device Implementation | 🔵 | #2 | - | - | - |
| #102 | ARC-C | ARC-C-GENERIC-012: Generic Fallback Device Impl... | 🔵 | #1 | - | - | - |
| #103 | ARC-C | ARC-C-I350-009: Intel i350 Device Implementation | 🔵 | #2 | - | - | - |
| #104 | ARC-C | ARC-C-82580-011: Intel 82580 Device Implementation | 🔵 | #2 | - | - | - |
| #105 | ARC-C | ARC-C-82575-010: Intel 82575/82576 Device Imple... | 🔵 | #2 | - | - | - |
| #106 | QA-SC | QA-SC-PERF-001: PTP Clock Read Latency Under Pe... | 🔵 | #2, #58 | - | - | - |
| #107 | QA-SC | QA-SC-SECU-001: Unauthorized BAR0 MMIO Access P... | 🔵 | #1 | - | - | - |
| #108 | QA-SC | QA-SC-AVAIL-001: Driver Stability Under Continu... | 🔵 | #31, #59, #81 | - | - | - |
| #109 | QA-SC | QA-SC-SCAL-001: Multi-Adapter System Scalabilit... | 🔵 | #31, #15, #76 | - | - | - |
| #110 | QA-SC | QA-SC-MAINT-001: Device Abstraction Extensibili... | 🔵 | #85 | - | - | - |
| #111 | QA-SC | QA-SC-RELI-001: PHC Fault Detection and Recover... | 🔵 | #72 | - | - | - |
| #112 | QA-SC | QA-SC-COMPAT-001: Windows Version Compatibility... | 🔵 | #88 | - | - | - |
| #113 | QA-SC | QA-SC-PERF-002: TAS Configuration Latency (8-Sl... | 🔵 | #8, #9, #50 | - | - | - |
| #114 | QA-SC | QA-SC-PORT-001: Hardware Portability Across Int... | 🔵 | #84 | - | - | - |
| #115 | QA-SC | QA-SC-DEPLOY-001: Driver Installation Success R... | 🔵 | #86 | - | - | - |
| #116 | QA-SC | QA-SC-TESTABILITY-001: Automated Test Suite wit... | 🔵 | #87, #72 | - | - | - |
| #117 | ADR | ADR-SCOPE-001: gPTP Architecture Separation - D... | 🔵 | #28 | - | - | - |
| #118 | ADR | ADR-PERF-SEC-001: Performance vs Security Trade... | 🔵 | #58 | - | - | - |
| #119 | REQ-F | REQ-F-GPTP-COMPAT-001: Compatibility with Exter... | 🔵 | #33 | #33, #149, #28 | #240 | - |
| #120 | REQ-F | REQ-F-FPE-001: Frame Preemption (IEEE 802.1Qbu/... | 🔵 | #120 | #120, #18, #40 | #239 | - |
| #122 | ADR | ADR-SEC-001: Debug vs Release Security Boundari... | 🔵 | #23 | - | - | - |
| #123 | ADR | ADR-SSOT-001: Single Source of Truth (SSOT) for... | ✅ | - | - | #301 | - |
| #124 | ADR | ADR-SUBM-001: Intel-Ethernet-Regs Git Submodule... | 🔵 | #21 | - | - | - |
| #125 | ADR | ADR-SCRIPT-001: Consolidated Script Architecture | 🔵 | #27 | - | - | - |
| #126 | ADR | ADR-HAL-001: Hardware Abstraction Layer for Mul... | 🔵 | #84 | - | - | - |
| #127 | QA-SC | QA-SC-PERF-001: IOCTL Response Time Performance | 🔵 | #29, #71 | - | - | - |
| #128 | QA-SC | QA-SC-PERF-002: NDIS Filter Packet Processing L... | 🔵 | #28, #31, #46 | - | - | - |
| #129 | QA-SC | QA-SC-SEC-001: Buffer Overflow Prevention Quali... | 🔵 | #28, #31, #89 | - | - | - |
| #130 | REQ | Requirements Completeness Audit - P0/P1 Action ... | 🔵 | - | - | - | - |
| #131 | ADR | [ADR-ERR-001] Error Reporting Strategy - Unifie... | 🔵 | #32 | - | - | - |
| #132 | ADR | [ADR-TEST-001] Test Infrastructure Architecture... | 🔵 | #33 | - | - | - |
| #133 | ADR | [ADR-TSN-001] TSN Feature Prioritization & Impl... | 🔵 | #30 | - | - | - |
| #134 | ADR | [ADR-MULTI-001] Multi-NIC Support Architecture ... | 🔵 | #29 | - | - | - |
| #135 | ADR | [ADR-DOC-001] Documentation Generation & Mainte... | 🔵 | #31 | - | - | - |
| #136 | QA-SC | QA-SC-RELI-001: Driver Recovery from Hardware F... | 🔵 | #29, #32 | - | - | - |
| #137 | QA-SC | QA-SC-TEST-001: Driver Testability - IOCTL Beha... | 🔵 | #33 | - | - | - |
| #138 | QA-SC | QA-SC-MAIN-001: Driver Maintainability - Adding... | 🔵 | #33 | - | - | - |
| #139 | ARC-C | ARC-C-AVB-002: AVB Integration Layer (Bridge Be... | 🔵 | #31, #35, #36, #37, #41, #42, #43 | - | - | - |
| #140 | ARC-C | ARC-C-HW-003: Hardware Access Layer (Direct BAR... | 🔵 | #31 | - | - | - |
| #141 | ARC-C | ARC-C-DEVICE-004: Device Abstraction Layer (Reg... | 🔵 | #31 | - | - | - |
| #142 | ARC-C | ARC-C-IOCTL-005: IOCTL Dispatcher (Centralized ... | 🔵 | #30 | - | - | - |
| #143 | ARC-C | ARC-C-CTX-006: Adapter Context Manager (Multi-A... | 🔵 | #11 | - | - | - |
| #144 | ARC-C | ARC-C-TS-007: Timestamp Event Subscription (Eve... | 🔵 | #16 | - | - | - |
| #145 | ARC-C | ARC-C-CFG-008: Configuration Manager (Registry-... | 🔵 | #95 | - | - | - |
| #146 | ARC-C | ARC-C-LOG-009: Error Handler & Centralized Logg... | 🔵 | #22 | - | - | - |
| #147 | ADR | [ADR] Event Emission Architecture for PTP Times... | 🔵 | #149 | - | - | - |
| #149 | REQ-F | REQ-F-PTP-001: Hardware Timestamp Correlation f... | 🔵 | #1 | #1, #18, #40 | #238 | - |
| #150 | UNKNOWN | Monitor MDIO Polling Performance Under Contenti... | 🔵 | - | - | - | - |
| #151 | UNKNOWN | Implement I219 Direct MDIO Register Access (Pos... | 🔵 | - | - | - | - |
| #152 | UNKNOWN | MDIO Bus Contention Detection and Monitoring | 🔵 | - | - | - | - |
| #153 | UNKNOWN | I219 PCH-Based MDIO and PTP Access Implementation | 🔵 | - | - | - | - |
| #154 | DESIGN | DES-C-NDIS-001: NDIS Filter Core Design (Phase 04) | 🔵 | #31, #88, #20, #21, #22, #23 | - | - | - |
| #155 | DESIGN | DES-C-DEVICE-004: Device Abstraction Layer Desi... | 🔵 | #31, #89, #24, #25, #26, #27 | - | - | - |
| #156 | DESIGN | DES-C-IOCTL-005: IOCTL Dispatcher Design (Phase... | 🔵 | #31, #28, #29, #30, #32 | - | - | - |
| #157 | DESIGN | DES-C-CTX-006: Adapter Context Manager Design (... | 🔵 | #31, #33, #34, #35, #36 | - | - | - |
| #158 | DESIGN | DES-C-AVB-007: AVB Integration Layer Design (Ph... | 🔵 | #31, #37, #38, #39, #40, #41 | - | - | - |
| #159 | DESIGN | DES-C-HW-008: Hardware Access Wrappers Design (... | 🔵 | #31, #42, #43, #44, #45, #46 | - | - | - |
| #160 | DESIGN | DES-C-CFG-009: Configuration Manager Design (Ph... | 🔵 | #31, #47, #48, #49, #50 | - | - | - |
| #161 | REQ-NF | REQ-NF-EVENT-002: Zero Polling Overhead Require... | 🔵 | #167 | #167, #19, #165 | #241 | - |
| #162 | REQ-F | REQ-F-EVENT-003: Emit ATDECC Unsolicited Notifi... | 🔵 | #167 | #167, #19, #13 | #236 | - |
| #163 | ADR | ADR-EVENT-001: Observer Pattern for Event Distr... | 🔵 | #168 | - | - | - |
| #164 | REQ-F | REQ-F-EVENT-004: Emit Diagnostic Counter Events... | 🔵 | #167 | #167 | #234 | - |
| #165 | REQ-NF | REQ-NF-EVENT-001: Event Notification Latency Re... | 🔵 | #167 | #167, #19, #163 | #245 | - |
| #166 | ADR | ADR-EVENT-002: Hardware Interrupt-Driven Event ... | 🔵 | #168 | - | - | - |
| #167 | StR | StR-EVENT-001: Event-Driven Time-Sensitive Netw... | 🔵 | - | - | - | - |
| #168 | REQ-F | REQ-F-EVENT-001: Emit PTP Hardware Timestamp Ca... | 🔵 | #167 | #167, #19, #5 | #237 | - |
| #169 | REQ-F | REQ-F-EVENT-002: Emit AVTP Timestamp Uncertain ... | 🔵 | #167 | - | - | - |
| #170 | ARC-C | ARC-C-EVENT-004: ATDECC Event Dispatcher | 🔵 | #163, #162, #163, #162 | - | - | - |
| #171 | ARC-C | ARC-C-EVENT-002: PTP Hardware Timestamp Event H... | 🔵 | #166, #168, #165, #161, #166, #168, #165, #161 | - | - | - |
| #172 | ARC-C | ARC-C-EVENT-001: Event Subject and Observer Inf... | 🔵 | #163, #165, #161, #163, #165, #161 | - | - | - |
| #173 | ARC-C | ARC-C-EVENT-003: AVTP Stream Event Monitor | 🔵 | #163, #169, #164, #163, #169, #164 | - | - | - |
| #174 | TEST | TEST-EVENT-004: Verify AVTP Diagnostic Counter ... | 🔵 | #164 | - | - | - |
| #175 | TEST | TEST-EVENT-002: Verify AVTP Timestamp Uncertain... | 🔵 | #169 | - | - | - |
| #176 | TEST | TEST-EVENT-003: Verify ATDECC Unsolicited Notif... | 🔵 | #162 | - | - | - |
| #177 | TEST | TEST-EVENT-001: Verify PTP Timestamp Event Late... | 🔵 | #168 | - | - | - |
| #178 | TEST | TEST-EVENT-006: Verify Zero Polling Overhead Re... | 🔵 | #161 | - | - | - |
| #179 | TEST | TEST-EVENT-005: Verify Event Notification Laten... | 🔵 | #165 | - | - | - |
| #180 | QA-SC | QA-SC-EVENT-001: PTP Event Latency Performance | 🔵 | #165, #162 | - | - | - |
| #181 | QA-SC | QA-SC-EVENT-002: Zero CPU Polling Overhead | 🔵 | #161, #162 | - | - | - |
| #182 | QA-SC | QA-SC-EVENT-003: Observer Dispatch Performance | 🔵 | #165, #162 | - | - | - |
| #183 | QA-SC | QA-SC-EVENT-004: Event Queue Overflow Handling | 🔵 | #164, #162 | - | - | - |
| #184 | QA-SC | QA-SC-REL-003: Reliability - Observer Fault Iso... | 🔵 | #53 | - | - | - |
| #185 | QA-SC | QA-SC-PERF-002: Performance - DPC Latency Under... | 🔵 | #46 | - | - | - |
| #186 | QA-SC | QA-SC-DEPLOY-004: Deployability - Multi-NIC NUM... | 🔵 | #44 | - | - | - |
| #187 | QA-SC | QA-SC-TEST-006: Testability - Mock Hardware Eve... | 🔵 | #68 | - | - | - |
| #188 | QA-SC | QA-SC-MOD-001: Modifiability - Add Observer to ... | 🔵 | #68 | - | - | - |
| #189 | QA-SC | QA-SC-MAINT-005: Maintainability - Event Payloa... | 🔵 | #68 | - | - | - |
| #190 | QA-SC | QA-SC-SECU-007: Security - Secure Observer Regi... | 🔵 | #67 | - | - | - |
| #191 | TEST | [TEST] TEST-PTP-PHC-001: PHC Hardware Clock Get... | 🔵 | #233, #59 | - | - | - |
| #192 | TEST | [TEST] TEST-PTP-FREQ-001: PTP Clock Frequency A... | 🔵 | #3 | - | - | - |
| #193 | TEST | [TEST] TEST-IOCTL-PHC-QUERY-001: PHC Time Query... | 🔵 | #34 | - | - | - |
| #194 | TEST | [TEST] TEST-IOCTL-OFFSET-001: PHC Time Offset A... | 🔵 | #38 | - | - | - |
| #195 | TEST | [TEST] TEST-IOCTL-SET-001: PHC Time Set IOCTL V... | 🔵 | #70 | - | - | - |
| #196 | TEST | [TEST] TEST-PTP-EPOCH-001: TAI Epoch Initializa... | 🔵 | #41 | - | - | - |
| #197 | TEST | [TEST] TEST-PHC-MONOTONIC-001: PHC Monotonicity... | 🔵 | #47 | - | - | - |
| #198 | TEST | [TEST] TEST-IOCTL-XSTAMP-001: Cross-Timestamp I... | 🔵 | #48 | - | - | - |
| #199 | TEST | [TEST] TEST-PTP-CORR-001: PTP Hardware Correlat... | 🔵 | #149 | - | - | - |
| #200 | TEST | [TEST] TEST-PERF-PHC-001: PHC Read Latency Veri... | 🔵 | #58 | - | - | - |
| #201 | TEST | [TEST] TEST-PERF-TS-001: Packet Timestamp Retri... | 🔵 | #65 | - | - | - |
| #202 | TEST | [TEST] TEST-IOCTL-TX-TS-001: TX Timestamp Retri... | 🔵 | #35 | - | - | - |
| #203 | TEST | [TEST] TEST-IOCTL-RX-TS-001: RX Timestamp Retri... | 🔵 | #37 | - | - | - |
| #204 | TEST | [TEST] TEST-PTP-TARGET-001: Target Time Interru... | 🔵 | #7 | - | - | - |
| #205 | TEST | [TEST] TEST-IOCTL-FREQ-001: Frequency Adjustmen... | 🔵 | #39 | - | - | - |
| #206 | TEST | [TEST] TEST-TAS-CONFIG-001: Time-Aware Shaper C... | 🔵 | #1 | - | - | - |
| #207 | TEST | [TEST] TEST-CBS-CONFIG-001: Credit-Based Shaper... | 🔵 | #1 | - | - | - |
| #208 | TEST | [TEST] TEST-MULTI-ADAPTER-001: Multi-Adapter PH... | 🔵 | #1 | - | - | - |
| #209 | TEST | [TEST] TEST-LAUNCH-TIME-001: Launch Time Config... | 🔵 | #1 | - | - | - |
| #210 | TEST | [TEST] TEST-GPTP-001: IEEE 802.1AS gPTP Stack I... | 🔵 | #1 | - | - | - |
| #211 | TEST | [TEST] TEST-SRP-001: Stream Reservation Protoco... | 🔵 | #1 | - | - | - |
| #212 | TEST | [TEST] TEST-PREEMPTION-001: Frame Preemption (8... | 🔵 | #1 | - | - | - |
| #213 | TEST | [TEST] TEST-VLAN-001: VLAN Tagging and Filterin... | 🔵 | #1 | - | - | - |
| #214 | TEST | [TEST] TEST-CROSS-SYNC-001: Cross-Adapter PHC S... | 🔵 | #1, #150 | - | - | - |
| #215 | TEST | [TEST] TEST-ERROR-HANDLING-001: Error Detection... | 🔵 | #1 | - | - | - |
| #216 | TEST | [TEST] TEST-QUEUE-PRIORITY-001: Queue Priority ... | 🔵 | #1 | - | - | - |
| #217 | TEST | [TEST] TEST-STATISTICS-001: Statistics and Coun... | 🔵 | #1 | - | - | - |
| #218 | TEST | [TEST] TEST-POWER-MGMT-001: Power Management Ve... | 🔵 | #1 | - | - | - |
| #219 | TEST | [TEST] TEST-PFC-001: Priority Flow Control (802... | 🔵 | #1 | - | - | - |
| #220 | TEST | [TEST] TEST-HARDWARE-OFFLOAD-001: Hardware Offl... | 🔵 | #1 | - | - | - |
| #221 | TEST | [TEST] TEST-LACP-001: Link Aggregation (802.1AX... | 🔵 | #1 | - | - | - |
| #222 | TEST | [TEST] TEST-DIAGNOSTICS-001: Network Diagnostic... | 🔵 | #1 | - | - | - |
| #223 | TEST | [TEST] TEST-EEE-001: Energy Efficient Ethernet ... | 🔵 | #1 | - | - | - |
| #224 | TEST | [TEST] TEST-SEGMENTATION-001: Network Segmentat... | 🔵 | #1 | - | - | - |
| #225 | TEST | [TEST] TEST-PERF-REGRESSION-001: Performance Re... | 🔵 | #1 | - | - | - |
| #226 | TEST | [TEST] TEST-SECURITY-001: Security Validation a... | 🔵 | #1 | - | - | - |
| #227 | TEST | [TEST] TEST-API-DOCS-001: API Documentation Ver... | 🔵 | #1 | - | - | - |
| #228 | TEST | [TEST] TEST-USER-DOCS-001: User Documentation V... | 🔵 | #1 | - | - | - |
| #229 | TEST | [TEST] TEST-STRESS-7DAY-001: 7-Day Continuous S... | 🔵 | #1 | - | - | - |
| #230 | TEST | [TEST] TEST-COMPAT-MULTIVENDOR-001: Multi-Vendo... | 🔵 | #1 | - | - | - |
| #231 | TEST | [TEST] TEST-ERROR-RECOVERY-001: Error Recovery ... | 🔵 | #1 | - | - | - |
| #232 | TEST | [TEST] TEST-BENCHMARK-001: Performance Benchmar... | 🔵 | #233, #59, #1, #60, #63, #14 | - | - | - |
| #233 | TEST | TEST-PLAN-001: AVB Filter Driver Verification a... | 🔵 | #1 | - | - | - |
| #234 | TEST | TEST-EVENT-004: Verify AVTP Diagnostic Counter ... | 🔵 | #164 | - | - | - |
| #235 | TEST | TEST-EVENT-002: Verify AVTP Timestamp Uncertain... | 🔵 | #169 | - | - | - |
| #236 | TEST | TEST-EVENT-003: Verify ATDECC Unsolicited Notif... | 🔵 | #162 | - | - | - |
| #237 | TEST | TEST-EVENT-001: Verify PTP Hardware Timestamp C... | 🔵 | #168 | - | - | - |
| #238 | TEST | TEST-PTP-001: Verify PTP Hardware Timestamp Cor... | 🔵 | #149 | - | - | - |
| #239 | TEST | TEST-FPE-001: Verify Frame Preemption (IEEE 802... | 🔵 | #120 | - | - | - |
| #240 | TEST | TEST-GPTP-COMPAT-001: Verify Compatibility with... | 🔵 | #119 | - | - | - |
| #241 | TEST | TEST-EVENT-NF-002: Validate Zero Polling Overhe... | 🔵 | #161 | - | - | - |
| #242 | TEST | TEST-COMPAT-NDIS-001: Verify NDIS 6.x Compatibi... | 🔵 | #97 | - | - | - |
| #243 | TEST | TEST-BUILD-SIGN-001: Verify Code Signing and WH... | 🔵 | #99 | - | - | - |
| #244 | TEST | TEST-BUILD-CI-001: Verify CI/CD Pipeline and Au... | 🔵 | #98 | - | - | - |
| #245 | TEST | TEST-EVENT-NF-001: Validate Event Notification ... | 🔵 | #165 | - | - | - |
| #246 | TEST | TEST-COMPAT-WDF-001: Verify Windows Driver Fram... | 🔵 | #96 | - | - | - |
| #247 | TEST | TEST-DEBUG-REG-001: Verify Registry-Based Debug... | 🔵 | #95 | - | - | - |
| #248 | TEST | TEST-SECURITY-BUFFER-001: Verify Buffer Overflo... | 🔵 | #89 | - | - | - |
| #249 | TEST | TEST-COMPAT-NDIS-001: Verify NDIS 6.50+ Compati... | 🔵 | #88 | - | - | - |
| #250 | TEST | TEST-INTEGRATION-SUITE-001: Verify Integration ... | 🔵 | #87 | - | - | - |
| #251 | TEST | TEST-DOC-DEPLOY-001: Verify Deployment Guide an... | 🔵 | #86 | - | - | - |
| #252 | TEST | TEST-MAINTAIN-001: Verify Code Maintainability ... | 🔵 | #85 | - | - | - |
| #253 | TEST | TEST-PERF-BENCHMARKS-001: Verify Performance Be... | 🔵 | #84 | - | - | - |
| #254 | TEST | TEST-ERROR-INJECT-001: Verify Error Injection T... | 🔵 | #83 | - | - | - |
| #255 | TEST | TEST-SECURITY-AUDIT-001: Verify Security Auditing | 🔵 | #82 | - | - | - |
| #256 | TEST | TEST-COMPAT-WIN11-001: Verify Windows 11 Compat... | 🔵 | #81 | - | - | - |
| #257 | TEST | TEST-COMPAT-WIN10-001: Verify Windows 10 Compat... | 🔵 | #80 | - | - | - |
| #258 | TEST | TEST-COMPAT-WIN7-001: Verify Windows 7 SP1 Comp... | 🔵 | #79 | - | - | - |
| #259 | TEST | TEST-COMPAT-SERVER-001: Verify Windows Server C... | 🔵 | #78 | - | - | - |
| #260 | TEST | TEST-COMPAT-I225-001: Verify Intel I225 Control... | 🔵 | #77 | - | - | - |
| #261 | TEST | TEST-COMPAT-I219-001: Verify Intel I219 Control... | 🔵 | #76 | - | - | - |
| #262 | TEST | TEST-HOT-PLUG-001: Verify Hot-Plug Detection an... | 🔵 | #75 | - | - | - |
| #263 | TEST | TEST-IOCTL-BUFFER-001: Verify IOCTL Buffer Vali... | 🔵 | #74 | - | - | - |
| #264 | TEST | TEST-SEC-IOCTL-001: Verify IOCTL Access Control... | 🔵 | #73 | - | - | - |
| #265 | TEST | TEST-COVERAGE-001: Verify Unit Test Coverage ≥8... | 🔵 | #72 | - | - | - |
| #266 | TEST | TEST-PHC-SET-001: Verify PHC Time Set (Initiali... | 🔵 | #70 | - | - | - |
| #267 | TEST | TEST-DOC-API-001: Verify IOCTL API Documentatio... | 🔵 | #71 | - | - | - |
| #268 | TEST | TEST-COMPAT-WIN10-001: Verify Windows 10/11 Com... | 🔵 | #69 | - | - | - |
| #269 | TEST | TEST-EVENT-LOG-001: Verify Windows Event Log In... | 🔵 | #68 | - | - | - |
| #270 | TEST | TEST-STATISTICS-001: Verify Driver Statistics C... | 🔵 | #67 | - | - | - |
| #271 | TEST | TEST-POWER-MGMT-S3-001: Verify S3 Sleep/Wake St... | 🔵 | #66 | - | - | - |
| #272 | TEST | TEST-PERF-TS-001: Verify Timestamp Retrieval La... | 🔵 | #65 | - | - | - |
| #273 | TEST | TEST-IOCTL-VERSION-001: Verify IOCTL API Versio... | 🔵 | #64 | - | - | - |
| #274 | TEST | TEST-PERF-PHC-001: Verify PHC Query Latency <50... | 🔵 | #58 | - | - | - |
| #275 | TEST | TEST-DEVICE-CAP-001: Verify Device Capability Q... | 🔵 | #56 | - | - | - |
| #276 | TEST | TEST-VLAN-PCP-001: Verify VLAN PCP to Traffic C... | 🔵 | #57 | - | - | - |
| #277 | TEST | TEST-DAL-INIT-001: Verify Device Abstraction La... | 🔵 | #55 | - | - | - |
| #278 | TEST | TEST-TSN-TERMS-001: Verify TSN Terminology Comp... | 🔵 | #54 | - | - | - |
| #279 | TEST | TEST-DEVICE-ABS-THREADING-001: Verify Thread-Sa... | 🔵 | #52 | - | - | - |
| #280 | TEST | TEST-ERROR-MAP-001: Verify intel_avb Error Code... | 🔵 | #53 | - | - | - |
| #281 | TEST | TEST-TAS-CONFIG-001: Verify Time-Aware Shaper (... | 🔵 | #50 | - | - | - |
| #282 | TEST | TEST-NAMING-CONV-001: Verify IEEE 802.1AS-2020 ... | 🔵 | #51 | - | - | - |
| #283 | TEST | TEST-CBS-CONFIG-001: Verify Credit-Based Shaper... | 🔵 | #49 | - | - | - |
| #284 | TEST | TEST-XSTAMP-QUERY-001: Verify Cross-Timestamp Q... | 🔵 | #48 | - | - | - |
| #285 | TEST | TEST-PHC-MONOTONIC-001: Verify PHC Monotonicity... | 🔵 | #47 | - | - | - |
| #286 | TEST | TEST-NDIS-FASTPATH-001: Verify Packet Forwardin... | 🔵 | #46 | - | - | - |
| #287 | TEST | TEST-REG-ACCESS-LOCK-001: Verify Safe Register ... | 🔵 | #45 | - | - | - |
| #288 | TEST | TEST-HW-DETECT-CAPS-001: Verify Hardware Capabi... | 🔵 | #44, #44 | - | - | - |
| #289 | TEST | TEST-EVENT-ID-SSOT-001: Verify Event ID Single ... | 🔵 | #22, #22 | - | - | - |
| #290 | TEST | TEST-NDIS-RECEIVE-PATH-001: Verify NDIS FilterR... | ✅ | #43, #43 | - | - | - |
| #291 | TEST | TEST-NDIS-SEND-PATH-001: Verify NDIS FilterSend... | ✅ | #42, #42 | - | - | - |
| #292 | TEST | TEST-SCRIPTS-CONSOLIDATE-001: Verify Script Con... | ✅ | #27 | - | - | - |
| #293 | REQ-NF | REQ-NF-CLEANUP-002: Archive Obsolete/Redundant ... | ✅ | #31 | - | #294 | - |
| #294 | TEST | TEST-CLEANUP-ARCHIVE-001: Verify Obsolete File ... | 🔵 | #293 | - | - | - |
| #295 | TEST | TEST-PTP-CLOCK-001: Verify PTP Clock Get/Set Ti... | 🔵 | #2 | - | - | - |
| #296 | TEST | TEST-PTP-FREQ-001: Verify PTP Clock Frequency A... | 🔵 | #3 | - | - | - |
| #297 | TEST | TEST-PTP-HW-TS-001: Verify Hardware Timestampin... | 🔵 | #5 | - | - | - |
| #298 | TEST | TEST-PTP-RX-TS-001: Verify Rx Packet Timestampi... | 🔵 | #6 | #5 | - | - |
| #299 | TEST | TEST-PTP-TARGET-TIME-001: Verify Target Time an... | 🔵 | #7 | #5 | - | - |
| #300 | TEST | TEST-SSOT-002: Verify CI Workflow Catches SSOT ... | 🔵 | #24 | - | - | - |
| #301 | TEST | TEST-SSOT-001: Verify No Duplicate IOCTL Defini... | ✅ | #24 | - | - | - |
| #302 | TEST | TEST-SSOT-003: Verify All Files Use SSOT Header... | ✅ | #24 | - | - | - |
| #303 | TEST | TEST-SSOT-004: Verify SSOT Header Contains All ... | ✅ | #24 | - | - | - |
| #304 | TEST | TEST-REGS-001: Verify Build with intel-ethernet... | 🔵 | #21 | - | - | - |
| #305 | TEST | TEST-REGS-002: Detect Magic Numbers in Register... | 🔵 | #21 | - | - | - |
| #306 | TEST | TEST-REGS-003: Verify Register Constants Match ... | 🔵 | #21 | - | - | - |
| #307 | REQ-NF | REQ-NF-PERF-BENCHMARKS-001: Performance Benchmarks | 🔵 | #31 | - | - | - |
| #308 | TEST | TEST-PORTABILITY-HAL-001: Hardware Abstraction ... | ✅ | #84 | - | - | - |
| #309 | TEST | TEST-PORTABILITY-HAL-002: Error Scenario Tests | ✅ | #84 | - | - | - |
| #310 | TEST | TEST-PORTABILITY-HAL-003: Performance Metrics T... | ✅ | #84 | - | - | - |
| #311 | TEST | TEST-PTP-RX-TS-001: Verify Rx Packet Timestampi... | 🔵 | #6 | - | - | - |
| #312 | TEST | [TEST] TEST-MDIO-PHY-001: MDIO/PHY Register Acc... | 🔵 | #10 | - | - | - |
| #313 | TEST | [TEST] TEST-DEV-LIFECYCLE-001: Device Lifecycle... | 🔵 | #12 | - | - | - |
| #314 | TEST | [TEST] TEST-TS-EVENT-SUB-001: Timestamp Event S... | 🔵 | #13 | - | - | - |

## Orphaned Requirements

Requirements without parent links (excluding StR which are top-level):

| Issue | Type | Title |
|-------|------|-------|
| #153 | UNKNOWN | I219 PCH-Based MDIO and PTP Access Implementation |
| #152 | UNKNOWN | MDIO Bus Contention Detection and Monitoring |
| #151 | UNKNOWN | Implement I219 Direct MDIO Register Access (Post-MVP) |
| #150 | UNKNOWN | Monitor MDIO Polling Performance Under Contention (Post-MVP) |
| #130 | REQ | Requirements Completeness Audit - P0/P1 Action Items (87.... |
| #123 | ADR | ADR-SSOT-001: Single Source of Truth (SSOT) for IOCTL Hea... |

## Requirements Without Tests

Functional and non-functional requirements without verification:

| Issue | Type | Title |
|-------|------|-------|
| #307 | REQ-NF | REQ-NF-PERF-BENCHMARKS-001: Performance Benchmarks |
| #61 | REQ-NF | REQ-NF-INTEROP-MILAN-001: AVnu Milan Interoperability |
| #62 | REQ-NF | REQ-NF-STD-NAMING-001: Standards-Compliant Function Naming |
| #43 | REQ-F | REQ-F-NDIS-RECEIVE-001: FilterReceive / FilterReceiveNetB... |
| #42 | REQ-F | REQ-F-NDIS-SEND-001: FilterSend / FilterSendNetBufferLists |
| #40 | REQ-F | REQ-F-DEVICE-ABS-003: Register Access via Device Abstract... |
| #36 | REQ-F | REQ-F-NDIS-ATTACH-001: FilterAttach/FilterDetach |
| #27 | REQ-NF | REQ-NF-SCRIPTS-001: Consolidate and Organize 80+ Redundan... |
| #25 | REQ-F | REQ-F-PTP-IOCTL-001: Production-Safe PTP Clock Control IO... |
| #23 | REQ-NF | REQ-NF-SEC-DEBUG-001: Debug-Only Raw Register Access (NDE... |
| #20 | REQ-F | REQ-DISCOVERY-001: Requirements Discovery Report - Revers... |
| #19 | REQ-F | REQ-F-TSRING-001: Shared Memory Ring Buffer for Timestamp... |
| #18 | REQ-F | REQ-F-HWCTX-001: Hardware Context Lifecycle State Machine |
| #17 | REQ-NF | REQ-NF-DIAG-REG-001: Registry-Based Diagnostics for IOCTL... |
| #16 | REQ-F | REQ-F-LAZY-INIT-001: Lazy Initialization and On-Demand Co... |
| #15 | REQ-F | REQ-F-MULTIDEV-001: Multi-Adapter Management and Selection |
| #11 | REQ-F | REQ-F-FP-001 & PTM-001: Frame Preemption & PTM (IOCTL 27/28) |
| #9 | REQ-F | REQ-F-TAS-001: Time-Aware Scheduler (IOCTL 26 - 802.1Qbv ... |
| #8 | REQ-F | REQ-F-QAV-001: Credit-Based Shaper (IOCTL 35 - 802.1Qav CBS) |

## Legend

- **StR**: Stakeholder Requirement (top-level business need)
- **REQ-F**: Functional Requirement (system SHALL behavior)
- **REQ-NF**: Non-Functional Requirement (quality attribute)
- **ADR**: Architecture Decision Record
- **ARC-C**: Architecture Component
- **QA-SC**: Quality Attribute Scenario (ATAM)
- **TEST**: Test Case

- 🔵: Open issue
- ✅: Closed issue

---

*Generated by `scripts/github-traceability-report.py`*
