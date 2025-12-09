# Stakeholder Requirements Specification - Intel AVB Filter Driver

**Version**: 1.0  
**Date**: 2025-12-07  
**Status**: Draft - Initial Discovery  
**Project**: Intel AVB Filter Driver

**Standards Compliance**: ISO/IEC/IEEE 29148:2018 (Stakeholder Requirements)

---

## 1. Introduction

### 1.1 Purpose
This document specifies the stakeholder requirements for the Intel AVB Filter Driver project. It captures the needs, expectations, and constraints of stakeholders who will be affected by the system.

### 1.2 Scope
The Intel AVB Filter Driver is a Windows NDIS 6.30 lightweight filter driver that exposes AVB/TSN and PTP capabilities of Intel Ethernet controllers to user-mode applications.

**In Scope**:
- Intel I210, I217, I219, I225, I226 Ethernet controllers
- AVB/TSN features (PTP, TAS, Frame Preemption, CBS/QAV)
- User-mode IOCTL interface for hardware access
- Test-signed distribution for open-source community

**Out of Scope**:
- Non-Intel network controllers
- Full AVB/TSN protocol stacks (user-mode responsibility)
- Official Microsoft-signed production driver (v1.0)

### 1.3 Definitions and Acronyms

| Term | Definition |
|------|------------|
| AVB | Audio Video Bridging - IEEE 802.1 standards for time-sensitive networking |
| TSN | Time-Sensitive Networking - Evolution of AVB standards |
| PTP | Precision Time Protocol (IEEE 1588) |
| TAS | Time-Aware Shaper (IEEE 802.1Qbv) |
| CBS | Credit-Based Shaper (IEEE 802.1Qav) |
| IOCTL | Input/Output Control - Windows mechanism for user-kernel communication |
| NDIS | Network Driver Interface Specification |
| MMIO | Memory-Mapped I/O - Direct hardware register access |
| MDIO | Management Data Input/Output - PHY register access protocol |

---

## 2. Business Context

### 2.1 Business Opportunity
**Problem**: Modern Intel NICs contain hardware support for AVB/TSN but these capabilities are inaccessible on Windows without proprietary drivers.

**Market Gap**:
- Audio/video professionals need AVB support on Windows for studio workflows
- TSN researchers require open-source drivers for validation and experimentation
- Industrial engineers lack tools for TSN prototyping in Windows environments

**Solution**: Open-source driver providing honest, hardware-validated access to Intel NIC AVB/TSN capabilities.

### 2.2 Business Goals
1. **Enable AVB/TSN on Windows**: Make Intel hardware capabilities accessible to users
2. **Support Research Community**: Provide open-source foundation for IEEE 802.1 validation
3. **Establish Community Project**: Create sustainable, maintainable driver maintained by community
4. **Path to Production**: Document route to official signing for potential sponsors

---

## 3. Stakeholder Identification

### 3.1 Stakeholder Register

| ID | Stakeholder | Role | Interest | Influence | Priority |
|----|-------------|------|----------|-----------|----------|
| SH-01 | zarfld (Project Owner) | Developer/Maintainer | High - Project success, code quality | High - Final decision authority | Critical |
| SH-02 | AVB Tool Developers | Primary Users | High - Stable IOCTL API | Medium - Bug reports, contributions | High |
| SH-03 | TSN Researchers | Primary Users | High - Hardware validation | Medium - Research citations, testing | High |
| SH-04 | Pro Audio Integrators | Primary Users | Medium - Demo/testing capability | Low - Adoption, feedback | Medium |
| SH-05 | Industrial Engineers | Secondary Users | Medium - TSN prototyping | Low - Niche use case | Medium |
| SH-06 | Open Source Community | Contributors | Medium - Project sustainability | Medium - Code contributions | Medium |
| SH-07 | Intel Corporation | Hardware Vendor | Low - Indirect benefit | Low - No official involvement | Low |
| SH-08 | Future Sponsors | Funding Source | Low - Production deployment | High - Funding decisions | Low (v1.0) |

---

## 4. Stakeholder Requirements

Each requirement is formatted per ISO/IEC/IEEE 29148:2018:

**Format**: `StR-<Category>-<Number>: <Title>`

**Attributes**:
- **Priority**: P0 (Critical), P1 (High), P2 (Medium), P3 (Low)
- **Source**: Stakeholder ID reference
- **Rationale**: Why this requirement exists
- **Verification**: How to validate the requirement is met

---

## StR-01: Hardware Accessibility

### StR-HWAC-001: Intel NIC AVB/TSN Feature Access
**Source**: SH-02 (AVB Tool Developers), SH-03 (TSN Researchers)

**Description**: Users shall be able to access AVB/TSN hardware features of supported Intel Ethernet controllers from user-mode applications on Windows.

**Business Value**:
- Enables development of AVB/TSN applications on Windows
- Provides research platform for IEEE 802.1 validation
- Unlocks hardware capabilities otherwise unusable on Windows

**Rationale**: Primary purpose of the driver - make Intel hardware capabilities accessible that are currently dormant on Windows.

**Acceptance Criteria**:
- [ ] User-mode applications can open device handle to driver
- [ ] IOCTL interface provides access to PTP, TAS, FP, CBS features
- [ ] Register-level access available for experimentation
- [ ] Feature support clearly reported per controller

**Priority**: P0 (Critical)  
**Verification**: User-mode test applications successfully configure AVB/TSN features

---

### StR-HWAC-002: Reliable Hardware Detection
**Source**: SH-02 (AVB Tool Developers), SH-01 (Maintainer)

**Description**: The driver shall reliably detect and enumerate all supported Intel Ethernet controllers present in the system.

**Business Value**:
- Users can identify which adapters support AVB/TSN
- Applications can programmatically discover available hardware
- Prevents confusion about which NICs will work

**Rationale**: Foundation requirement - driver must know what hardware it's managing before exposing features.

**Acceptance Criteria**:
- [ ] All supported Intel controllers detected (I210, I217, I219, I225, I226)
- [ ] Non-supported controllers explicitly rejected (no false positives)
- [ ] Adapter enumeration available via IOCTL
- [ ] Per-adapter capability reporting working

**Priority**: P0 (Critical)  
**Verification**: Test system with multiple NICs, verify detection matrix

---

### StR-HWAC-003: Safe Hardware Access
**Source**: SH-02 (AVB Tool Developers), SH-01 (Maintainer)

**Description**: All hardware access (MMIO, MDIO) shall be performed safely without risk of system crashes or data corruption.

**Business Value**:
- Users can trust driver in development and testing environments
- Reduces risk of data loss during experimentation
- Maintains system stability

**Rationale**: Kernel-mode code must never crash; BSOD is unacceptable for a filter driver.

**Acceptance Criteria**:
- [ ] Zero BSODs in 24-hour stress test
- [ ] Invalid IOCTL inputs handled gracefully (no crashes)
- [ ] Hardware timeouts properly handled (no hangs)
- [ ] Memory access bounds-checked

**Priority**: P0 (Critical)  
**Verification**: Stress testing with Driver Verifier enabled, fuzzing IOCTL inputs

---

## StR-02: Functional Correctness

### StR-FUNC-001: Honest Feature Reporting
**Source**: SH-02 (AVB Tool Developers), SH-03 (TSN Researchers)

**Description**: The driver shall accurately report which AVB/TSN features are supported and operational on each controller, without fabricating or simulating capabilities.

**Business Value**:
- Users can trust driver behavior for research and validation
- Eliminates wasted time debugging "features" that don't actually work
- Builds reputation for honesty and reliability

**Rationale**: Core principle - "no fake modes, no simulation, no stubs". Only report what actually works.

**Acceptance Criteria**:
- [ ] Per-controller capability matrix maintained and accurate
- [ ] Unsupported features return clear error codes (not fake success)
- [ ] Documentation differentiates: Working / Experimental / Broken / Needs Investigation
- [ ] All "Working" features have hardware validation evidence

**Priority**: P0 (Critical)  
**Verification**: Code review for simulation/stub removal, test on actual hardware

---

### StR-FUNC-002: PTP Clock Functionality
**Source**: SH-02 (AVB Tool Developers), SH-03 (TSN Researchers)

**Description**: The driver shall provide reliable access to IEEE 1588 PTP hardware clocks on supported controllers.

**Business Value**:
- Enables time synchronization for AVB/TSN applications
- Supports precision timing research
- Foundation for all TSN features

**Rationale**: PTP is fundamental to AVB/TSN; without working clocks, other features are unusable.

**Acceptance Criteria**:
- [ ] I226: SYSTIM verified incrementing with <100ns resolution
- [ ] I210: SYSTIM no longer stuck at zero (known bug resolved)
- [ ] Clock configuration readable via IOCTL (`IOCTL_AVB_GET_CLOCK_CONFIG` working)
- [ ] Clock adjustment supported (frequency, offset)

**Priority**: P0 (Critical)  
**Verification**: Clock stability tests over 24+ hours, drift measurements

---

### StR-FUNC-003: TSN Traffic Shaping
**Source**: SH-02 (AVB Tool Developers), SH-03 (TSN Researchers)

**Description**: The driver shall enable configuration of TSN traffic shaping features where hardware supports them.

**Business Value**:
- Enables AVB/TSN traffic prioritization and scheduling
- Supports validation of IEEE 802.1Qbv/Qav standards
- Demonstrates real hardware enforcement (not just register writes)

**Rationale**: Traffic shaping is core TSN functionality; must prove it actually affects wire behavior.

**Acceptance Criteria**:
- [ ] TAS (Time-Aware Shaper) configurable on I226
- [ ] Packet captures prove TAS schedule enforcement
- [ ] CBS (Credit-Based Shaper) working on supported controllers
- [ ] Frame Preemption configurable and status reportable

**Priority**: P1 (High)  
**Verification**: Traffic generation + packet capture proving hardware enforcement

---

## StR-03: Performance & Stability

### StR-PERF-001: Minimal Performance Impact
**Source**: SH-02 (AVB Tool Developers), SH-04 (Pro Audio Integrators)

**Description**: The driver shall have minimal impact on network performance when not actively processing AVB/TSN traffic.

**Business Value**:
- Users can leave driver installed without degrading normal network performance
- Supports mixed-use scenarios (normal + AVB traffic)
- Demonstrates efficient implementation

**Rationale**: Filter driver overhead must be negligible to be acceptable for production-like testing.

**Acceptance Criteria**:
- [ ] Throughput degradation <5% vs. baseline Intel driver (iperf3 benchmark)
- [ ] Latency increase <10μs for forwarded packets
- [ ] CPU overhead <1% when filter idle (no active IOCTL usage)

**Priority**: P1 (High)  
**Verification**: iperf3 benchmarks before/after filter attachment

---

### StR-PERF-002: System Stability
**Source**: SH-01 (Maintainer), SH-02 (AVB Tool Developers)

**Description**: The driver shall operate stably for extended periods without crashes, memory leaks, or resource exhaustion.

**Business Value**:
- Users can run long-duration tests and experiments
- Supports 24/7 monitoring and data collection scenarios
- Builds trust in driver quality

**Rationale**: Instability renders driver unusable for research and development work.

**Acceptance Criteria**:
- [ ] Zero BSODs in 100+ hours continuous operation
- [ ] Zero memory leaks (Driver Verifier clean)
- [ ] Clean attach/detach cycles (no resource leaks)
- [ ] Handles rapid IOCTL bursts without degradation

**Priority**: P0 (Critical)  
**Verification**: Stress testing with Driver Verifier, memory profiling

---

## StR-04: Usability

### StR-USE-001: Clear Error Reporting
**Source**: SH-02 (AVB Tool Developers), SH-03 (TSN Researchers)

**Description**: All IOCTL operations shall return clear, actionable error codes when operations fail, with sufficient context for troubleshooting.

**Business Value**:
- Users can diagnose problems without kernel debugging
- Reduces support burden on maintainer
- Enables self-service troubleshooting

**Rationale**: Cryptic errors waste user time and generate support requests.

**Acceptance Criteria**:
- [ ] All error codes documented with meaning and resolution steps
- [ ] No silent failures (all operations return status)
- [ ] `ERROR_INVALID_FUNCTION` only for truly unsupported operations
- [ ] Debug logging available for advanced troubleshooting

**Priority**: P1 (High)  
**Verification**: Test error paths with user-mode tools, validate error messages

---

### StR-USE-002: Comprehensive Documentation
**Source**: SH-02 (AVB Tool Developers), SH-06 (Open Source Community)

**Description**: The driver shall be accompanied by complete documentation covering installation, usage, API reference, and troubleshooting.

**Business Value**:
- Users can onboard without maintainer assistance
- Enables community contributions
- Reduces repetitive support questions

**Rationale**: Good documentation is critical for open-source adoption.

**Acceptance Criteria**:
- [ ] Installation guide (test-signing setup, driver installation)
- [ ] IOCTL API reference (all codes, structures, usage examples)
- [ ] Per-controller capability matrix
- [ ] Troubleshooting guide (common issues, resolutions)
- [ ] Build instructions (prerequisites, steps)

**Priority**: P1 (High)  
**Verification**: Community feedback, documentation completeness audit

---

### StR-USE-003: Test Tools Provided
**Source**: SH-02 (AVB Tool Developers), SH-03 (TSN Researchers)

**Description**: The project shall include user-mode test tools demonstrating IOCTL usage and validating hardware functionality.

**Business Value**:
- Users have working examples of IOCTL usage
- Validation tools prove hardware is working
- Reduces barrier to entry for new users

**Rationale**: Test tools serve as both validation and usage examples.

**Acceptance Criteria**:
- [ ] CLI tools for each major feature (PTP, TAS, CBS, MDIO)
- [ ] Comprehensive IOCTL test suite
- [ ] Quick diagnostics tool for health checks
- [ ] Tools work on all supported controllers

**Priority**: P2 (Medium)  
**Verification**: Run test tools on fresh install, validate output

---

## StR-05: Distribution & Deployment

### StR-DIST-001: Test-Signed Distribution
**Source**: SH-01 (Maintainer), SH-02 (AVB Tool Developers)

**Description**: The driver shall be distributable as a test-signed package with automated setup scripts for development environments.

**Business Value**:
- Users can install driver without complex manual steps
- Supports rapid iteration during development
- Acceptable for research and testing use cases

**Rationale**: Test-signing is sufficient for v1.0 open-source release; production signing deferred to future sponsors.

**Acceptance Criteria**:
- [ ] Self-signed certificate generation automated (PowerShell script)
- [ ] Installation script handles test-signing mode setup
- [ ] Uninstallation script provided (clean removal)
- [ ] Requirements clearly documented (Secure Boot, test-signing mode)

**Priority**: P1 (High)  
**Verification**: Fresh Windows install, follow installation guide

---

### StR-DIST-002: Build Reproducibility
**Source**: SH-06 (Open Source Community), SH-01 (Maintainer)

**Description**: The driver build process shall be fully documented and reproducible using standard Windows development tools.

**Business Value**:
- Community members can build from source
- Enables independent verification of binary integrity
- Supports contributions and forks

**Rationale**: Open-source projects must be buildable by anyone.

**Acceptance Criteria**:
- [ ] Visual Studio solution builds cleanly on VS2019/2022
- [ ] WDK version requirements documented
- [ ] No proprietary dependencies (MIT license compatible)
- [ ] CI/CD pipeline optional but recommended (GitHub Actions)

**Priority**: P2 (Medium)  
**Verification**: Fresh checkout, build following instructions

---

## StR-06: Constraints & Compliance

### StR-CONS-001: Intel Hardware Only
**Source**: SH-01 (Maintainer)

**Description**: The driver shall support Intel Ethernet controllers only, with explicit rejection of non-Intel hardware.

**Business Value**:
- Maintains focus on Intel AVB/TSN hardware
- Prevents scope creep and maintenance burden
- Clear expectations for users

**Rationale**: Intel NICs have specific AVB/TSN hardware; supporting other vendors requires entirely different implementations.

**Acceptance Criteria**:
- [ ] Vendor ID check (0x8086) in device enumeration
- [ ] Non-Intel adapters explicitly rejected with clear message
- [ ] Documentation lists supported controllers only

**Priority**: P0 (Critical)  
**Verification**: Test with non-Intel NIC, verify rejection

---

### StR-CONS-002: Windows 10/11 Target Platform
**Source**: SH-01 (Maintainer), SH-02 (AVB Tool Developers)

**Description**: The driver shall target Windows 10 (1809+) and Windows 11 using NDIS 6.30+ filter driver model.

**Business Value**:
- Focuses on modern Windows versions with good driver support
- Avoids legacy compatibility issues
- Aligns with Intel driver requirements

**Rationale**: Windows 10 1809+ provides stable NDIS 6.30+ platform; earlier versions not worth support burden.

**Acceptance Criteria**:
- [ ] Driver loads on Windows 10 1809 and later
- [ ] Windows 11 fully supported
- [ ] x64 architecture supported
- [ ] ARM64 basic support (not performance-optimized)

**Priority**: P1 (High)  
**Verification**: Test on Windows 10 1809, 21H2, Windows 11

---

### StR-CONS-003: Open Source MIT License
**Source**: SH-01 (Maintainer), SH-06 (Open Source Community)

**Description**: The project shall be licensed under MIT License allowing free use, modification, and distribution.

**Business Value**:
- Encourages adoption and contributions
- Enables commercial use in products
- Supports academic research

**Rationale**: Permissive license maximizes project utility and adoption.

**Acceptance Criteria**:
- [ ] MIT LICENSE file in repository root
- [ ] License headers in all source files
- [ ] No GPL or proprietary dependencies

**Priority**: P0 (Critical)  
**Verification**: License audit, dependency review

---

## 5. Requirements Traceability

### 5.1 Stakeholder → System Requirements Mapping

Each StR requirement will be refined into system requirements (REQ-F, REQ-NF) in Phase 02.

**Example Traceability**:
```
StR-HWAC-001 (Intel NIC AVB/TSN Feature Access)
  ├── REQ-F-DEV-001: Device Node Creation
  ├── REQ-F-IOCTL-001: IOCTL Interface Definition
  ├── REQ-F-PTP-001: PTP Clock Access
  └── REQ-F-TSN-001: TSN Feature Configuration

StR-FUNC-001 (Honest Feature Reporting)
  ├── REQ-F-CAP-001: Capability Matrix
  ├── REQ-F-ERR-001: Error Code Standards
  └── REQ-NF-QUAL-001: No Simulation/Fake Features
```

Full traceability matrix will be created in Phase 02 as GitHub Issues.

---

## 6. Assumptions & Dependencies

### 6.1 Assumptions
1. **Hardware Availability**: Maintainer has access to I210 and I226 for validation
2. **Datasheets**: Intel register specifications are accurate and complete
3. **User Competency**: Users understand Windows driver development constraints (test-signing)
4. **Community Support**: Community will contribute testing on hardware variants
5. **Windows Stability**: Microsoft won't break NDIS filter driver model in updates

### 6.2 Dependencies
1. **Windows Driver Kit (WDK)**: Microsoft provides stable WDK releases
2. **Intel Base Driver**: Intel miniport driver coexists with filter driver
3. **Hardware Capability**: NICs actually have functional AVB/TSN hardware blocks
4. **Documentation**: Intel datasheets remain accessible
5. **Test Equipment**: Users have packet capture tools for validation

### 6.3 Constraints
1. **Budget**: Zero budget for v1.0 (no EV certificate, no HLK infrastructure)
2. **Time**: Volunteer maintainer time limited to evenings/weekends
3. **Hardware**: Limited access to some controller variants (I217, I219, I225)
4. **Signing**: Test-signing only for v1.0 (Secure Boot implications)

---

## 7. Validation Plan

### 7.1 Requirements Validation Methods

| Requirement Category | Validation Method | Success Criteria |
|---------------------|-------------------|------------------|
| Hardware Accessibility | User-mode test tools | Tools successfully access hardware features |
| Functional Correctness | Hardware validation | Packet captures prove feature operation |
| Performance & Stability | Benchmarking & stress testing | Metrics within specified thresholds |
| Usability | User feedback | Community can install and use without assistance |
| Distribution | Fresh install testing | Reproducible on clean Windows system |
| Constraints | Code review & testing | Constraints enforced in code |

### 7.2 Stakeholder Review Schedule
- **Week 2**: Initial StR review with maintainer
- **Week 4**: Community feedback on GitHub Issues
- **Week 8**: Post-implementation validation (did we meet needs?)
- **Week 12**: v1.0 release review

---

## 8. Approval

### 8.1 Stakeholder Sign-Off

| Stakeholder | Role | Approval Date | Signature |
|-------------|------|---------------|-----------|
| zarfld | Project Owner | TBD | ____________ |
| Community Representatives | Primary Users | TBD | (GitHub Issue Approval) |

### 8.2 Change Control
All changes to stakeholder requirements must:
1. Be proposed via GitHub Issue
2. Include rationale and impact analysis
3. Be approved by project owner
4. Update this document with version history

---

## 9. Next Steps

### Immediate Actions (Week 1-2)
1. ✅ Create GitHub Issue templates (StR, REQ-F, REQ-NF, TEST, ADR)
2. ✅ Create initial StR issues for tracking
3. ⏭️ Begin Phase 02: Requirements Analysis (refine StR → REQ-F/REQ-NF)
4. ⏭️ Execute code-to-requirements analysis on existing implementation

### Phase Transition Criteria
**Ready for Phase 02 when**:
- [ ] All stakeholder requirements reviewed and approved
- [ ] GitHub Issue tracking configured
- [ ] Initial requirement issues created
- [ ] Code-to-requirements analysis plan defined

---

## Appendix A: Stakeholder Requirement Summary

| ID | Title | Priority | Stakeholder |
|----|-------|----------|-------------|
| StR-HWAC-001 | Intel NIC AVB/TSN Feature Access | P0 | SH-02, SH-03 |
| StR-HWAC-002 | Reliable Hardware Detection | P0 | SH-02, SH-01 |
| StR-HWAC-003 | Safe Hardware Access | P0 | SH-02, SH-01 |
| StR-FUNC-001 | Honest Feature Reporting | P0 | SH-02, SH-03 |
| StR-FUNC-002 | PTP Clock Functionality | P0 | SH-02, SH-03 |
| StR-FUNC-003 | TSN Traffic Shaping | P1 | SH-02, SH-03 |
| StR-PERF-001 | Minimal Performance Impact | P1 | SH-02, SH-04 |
| StR-PERF-002 | System Stability | P0 | SH-01, SH-02 |
| StR-USE-001 | Clear Error Reporting | P1 | SH-02, SH-03 |
| StR-USE-002 | Comprehensive Documentation | P1 | SH-02, SH-06 |
| StR-USE-003 | Test Tools Provided | P2 | SH-02, SH-03 |
| StR-DIST-001 | Test-Signed Distribution | P1 | SH-01, SH-02 |
| StR-DIST-002 | Build Reproducibility | P2 | SH-06, SH-01 |
| StR-CONS-001 | Intel Hardware Only | P0 | SH-01 |
| StR-CONS-002 | Windows 10/11 Target Platform | P1 | SH-01, SH-02 |
| StR-CONS-003 | Open Source MIT License | P0 | SH-01, SH-06 |

---

## Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2025-12-07 | zarfld + AI | Initial stakeholder requirements based on project charter and codebase analysis |

---

**Next Document**: `02-requirements/SYSTEM-REQUIREMENTS-SPECIFICATION.md` (Phase 02)
