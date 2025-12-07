# Intel AVB Filter Driver - Project Charter

**Version**: 1.0  
**Date**: 2025-12-07  
**Status**: Draft - Initial Kickoff  
**Project Owner**: zarfld (Open Source Community)

---

## Executive Summary

**Project Name**: Intel AVB Filter Driver

**Elevator Pitch**: An open-source Windows NDIS 6.30 lightweight filter driver that exposes AVB/TSN and PTP capabilities of Intel Ethernet controllers (I210, I217, I219, I225, I226) to user-mode tools and higher-level stacks, with honest hardware-validated behaviour and no fake features.

**Repository**: https://github.com/zarfld/IntelAvbFilter

---

## Problem Statement

Modern Intel NICs ship with AVB/TSN/PTP hardware blocks that are largely inaccessible on Windows without proprietary or OEM-specific drivers. This blocks:

- **Audio/video professionals** from using AVB/TSN for live and studio workflows
- **TSN researchers** from using widely available Intel NICs under Windows
- **Embedded/industrial engineers** from prototyping TSN flows in common Windows labs

### Business Impact

**Current State**:
- Intel I210/I226 AVB/TSN hardware exists but is dormant on Windows
- Linux has open-source drivers (igb, igc) with full AVB/TSN support
- Windows users forced to use expensive proprietary solutions or abandon Windows platform

**Desired State**:
- Open-source, community-maintained driver enabling AVB/TSN on Windows
- Researchers can validate IEEE 802.1 standards implementations
- Audio/video integrators can build Windows-based AVB testbeds
- Industrial engineers can prototype TSN in common Windows labs

---

## Vision & Mission

### Vision (3-5 years)
Make Intel NIC AVB/TSN/PTP features **reliably usable** on Windows for:
- Real hardware (no simulation, no stubs)
- Open-source tooling and research
- Long-term, community-maintained experimentation and production use (where test-signing is acceptable)

### Mission (v1.0 - 12 months)
Deliver a **stable, test-signed driver** with:
1. Robust detection and enumeration of supported Intel controllers
2. Clean IOCTL surface for AVB/TSN/PTP operations
3. Hardware-validated feature set for at least I226 (hero NIC)
4. Honest documentation of what works, what doesn't, and why

---

## High-Level Goals

### Goal 1: Stable Driver Foundation (0-3 months)
**Objective**: Rock-solid device discovery and IOCTL surface

**Success Criteria**:
- ✅ All supported controllers detected reliably
- ✅ Device node `\\.\IntelAvbFilter` created without conflicts
- ✅ No IOCTL returns `ERROR_INVALID_FUNCTION` unless explicitly unsupported
- ✅ Zero BSODs in 24-hour stress test

**Key Deliverables**:
- Fix `IOCTL_AVB_GET_CLOCK_CONFIG` (currently not working)
- Comprehensive IOCTL test suite
- Clean error handling and logging

### Goal 2: PTP/IEEE 1588 Functionality (3-6 months)
**Objective**: Proven PTP clock capabilities per controller

**Success Criteria**:
- ✅ I226 SYSTIM verified running and stable
- ✅ I210 SYSTIM no longer stuck at zero
- ✅ Clock configuration/status readable via IOCTL
- ✅ Measurable synchronization accuracy

**Key Deliverables**:
- PTP clock initialization sequences per controller
- Drift/stability test results
- User-mode test tools for clock validation

### Goal 3: TSN Hardware Activation (6-9 months)
**Objective**: At least one TSN feature demonstrably working on wire

**Success Criteria**:
- ✅ I226 TAS (Time-Aware Shaper) affects traffic patterns
- ✅ Frame Preemption configuration accepted
- ✅ CBS/QAV (Credit-Based Shaper) functional on supported NICs
- ✅ Packet captures prove hardware enforcement

**Key Deliverables**:
- TSN configuration validation tools
- Traffic pattern verification scripts
- Per-controller capability matrix

### Goal 4: Community Production Ready (9-12 months)
**Objective**: Stable for community use with test-signing

**Success Criteria**:
- ✅ Performance degradation <5% vs. baseline Intel driver
- ✅ Comprehensive documentation and examples
- ✅ Reproducible build and installation process
- ✅ Active issue tracking and response

**Key Deliverables**:
- v1.0 release with signed binaries
- Complete user guide and API documentation
- Path-to-production roadmap for sponsors

---

## Stakeholders

### Primary Stakeholders

#### 1. Project Owner/Maintainer
- **Who**: zarfld (and future co-maintainers)
- **Concerns**: Code quality, maintainability, community growth, long-term sustainability
- **Success Measures**: Active contributors, low bug count, clear documentation

#### 2. AVB/TSN Tool Developers
- **Who**: Developers building Windows applications using AVB/TSN capabilities
- **Concerns**: Stable IOCTL API, predictable behavior, clear error handling
- **Success Measures**: Can build working applications without BSOD, reliable feature detection

#### 3. TSN/PTP Researchers
- **Who**: University labs, research institutions
- **Concerns**: Hardware access for validation, reproducible results, IEEE standards compliance
- **Success Measures**: Can publish papers citing driver behavior, repeatable experiments

#### 4. Pro Audio/Video Integrators
- **Who**: System integrators building AVB testbeds
- **Concerns**: Interoperability with Milan/AVDECC endpoints, stability in live environments
- **Success Measures**: Can demonstrate AVB workflows, acceptable reliability for demos/testing

### Secondary Stakeholders

#### 5. Industrial/Automation Engineers
- **Who**: Engineers prototyping TSN in Windows R&D labs
- **Concerns**: TSN feature validation, queue control, traffic shaping
- **Success Measures**: Can validate TSN concepts before production Linux deployment

#### 6. Open Source Ecosystem
- **Who**: gPTP implementations, Milan controllers, monitoring tools
- **Concerns**: Interoperability, standard compliance, open development process
- **Success Measures**: Driver works with existing open-source AVB/TSN tools

### Future Stakeholders

#### 7. Potential Sponsors
- **Who**: Hardware vendors, research grants, commercial users
- **Concerns**: Official signing path (HLK/attestation), production readiness, support
- **Success Measures**: Clear roadmap to production-signed driver, documented HLK path

---

## User Personas

### Persona 1: "Anja – AVB Tool Developer"
**Profile**:
- Software developer building Windows applications
- Uses Windows 10/11 with Intel I226 NIC
- Comfortable with test-signing and development tools

**Needs**:
- Stable IOCTL API for PTP, TAS, Frame Preemption, queue configuration
- Clear documentation of supported features per controller
- Predictable error handling (no silent failures)

**Pain Points**:
- Hates undocumented behavior and "half-implemented" features
- BSOD risk is unacceptable for development workflow
- Time wasted debugging driver vs. application logic

**Success Story**:
"I can build my AVB monitoring tool on top of IntelAvbFilter's IOCTL API and observe real, reproducible hardware behavior. When features aren't supported, I get clear error messages explaining why."

---

### Persona 2: "Marco – TSN Researcher"
**Profile**:
- PhD student in computer networking
- University lab with mixed Linux/Windows environment
- Needs empirical data for IEEE 802.1 validation

**Needs**:
- Low-level register access for experimentation
- Ability to configure TSN features and measure effects
- Reproducible results for publication

**Pain Points**:
- Can't cite closed-source or undocumented drivers in papers
- Needs confidence that hardware is actually enforcing configurations
- Time-limited for thesis completion

**Success Story**:
"I can run my TSN validation scripts repeatedly on Windows with stable results. The driver's open-source nature means I can verify implementation details and cite them in my research papers."

---

### Persona 3: "Sven – Pro Audio Integrator"
**Profile**:
- Builds AVB systems for live sound and broadcast
- Comfortable with hardware but not kernel development
- Needs Windows support for customer installations

**Needs**:
- Reasonable stability for demos and testing
- Interoperability with Milan endpoints and stage boxes
- Clear installation and troubleshooting guides

**Pain Points**:
- Can't risk system crashes during live demonstrations
- Needs transparency about limitations
- Limited time for complex setup procedures

**Success Story**:
"I can build a Windows-based AVB testbed for customer demos. I accept test-signing constraints because the driver is honest about what works and provides stable performance for my use cases."

---

## Success Criteria

### Functional Success (v1.0 "Community-Ready")

#### Per-Controller Minimum (I210, I219, I225, I226)

**Device Discovery & Context**:
- ✅ Detection working (device appears as "Working" in capability matrix)
- ✅ MMIO register read/write verified via test tools
- ✅ Device node `\\.\IntelAvbFilter` created reliably on supported hardware
- ✅ BAR0 mapping successful with correct address ranges

**IOCTL Surface**:
- ✅ No IOCTL returns `ERROR_INVALID_FUNCTION` unless explicitly marked "not supported on this NIC"
- ✅ `IOCTL_AVB_GET_CLOCK_CONFIG` returns valid configuration or clear, documented error
- ✅ All IOCTLs properly validated and tested with user-mode tools
- ✅ Error codes documented with troubleshooting guidance

**PTP/Clock Success**:
- ✅ **I226**: SYSTIM continues incrementing and passes stability tests (already verified)
- ✅ **I210**: SYSTIM no longer stuck at zero; increments reliably after initialization
- ✅ **I219/I225**: Clock status determinable (supported or not supported per hardware)
- ✅ **I217**: Status documented (experimental, needs hardware validation)

**TSN Success (Minimum Viable)**:
- ✅ **I226**: At least one TSN feature proven working on wire:
  - TAS configuration accepted and reflected in registers
  - Traffic pattern demonstrably affected by TAS schedule (packet capture proof)
  - Frame Preemption configuration accepted or rejected with clear error
- ✅ **Other NICs**: TSN capability accurately reported (supported/not supported)

### Non-Functional Success

#### Stability
- ✅ **Zero BSODs** in 24-hour stress test with:
  - Filter driver attached to active NIC
  - Traffic load (1Gbps sustained)
  - Continuous IOCTL calls (100 calls/second)
- ✅ **Clean detach**: Driver uninstall/disable without system restart
- ✅ **Error recovery**: Graceful handling of invalid IOCTL inputs

#### Performance
- ✅ **Throughput degradation <5%** vs. baseline Intel driver:
  - Measured with iperf3 (TCP and UDP)
  - Filter attached but idle (no active IOCTL usage)
  - 1Gbps and 2.5Gbps speeds tested
- ✅ **Latency impact <10μs** for forwarded packets
- ✅ **CPU overhead <1%** when filter idle

#### Observability
- ✅ **Clear logging** at appropriate debug levels:
  - DL_INFO: Device discovery, IOCTL operations
  - DL_WARN: Configuration errors, degraded states
  - DL_ERROR: Critical failures, unexpected hardware states
- ✅ **DebugView/WinDbg integration** for troubleshooting
- ✅ **IOCTL call tracing** for user-mode debugging

#### Honesty/Transparency
- ✅ **README/STATUS files** clearly differentiate:
  - ✅ Working (hardware-validated, reproducible)
  - ⚠️ Experimental (code exists, needs hardware validation)
  - ❌ Broken (known issues, tracked in GitHub Issues)
  - 🔍 Needs Investigation (unknown state, prioritized for testing)
- ✅ **Per-controller capability matrix** maintained and accurate
- ✅ **No fake/simulated features**: If not tested on hardware, marked as unsupported

---

## Constraints

### Technical Constraints

#### Platform
- **OS**: Windows 10 1809+ and Windows 11
- **Driver Model**: NDIS 6.30+ lightweight filter driver
- **Architecture**: x64 and ARM64 (x64 primary focus for v1.0)

#### Hardware
- **Supported NICs**: Intel I210, I217, I219, I225, I226
- **Explicitly Out of Scope**: 
  - Non-Intel controllers
  - Legacy chips (82574L, 82575, 82576, 82580, I350)
  - I226 variants without TSN hardware blocks

#### Development Tools
- **IDE**: Visual Studio 2019/2022
- **SDK**: Windows Driver Kit (WDK) 10/11
- **Build**: MSBuild with NDIS filter driver targets
- **Testing**: WDK testing frameworks, DebugView, WinDbg

#### Architecture Decision
- **Filter Driver** (not miniport, not user-mode only)
  - Rationale: Minimal interference with Intel base driver
  - Trade-off: Limited control vs. stability and compatibility

### Resource Constraints

#### Budget
- **Current**: Effectively zero
- **Impact**: 
  - No EV code signing certificate (~$300-500/year)
  - No HLK test infrastructure
  - No paid testing services
- **Mitigation**: Test-signing path well-documented and automated

#### Time
- **Maintainer Availability**: Limited (open-source, volunteer-driven)
- **Impact**: Slower release cycles, prioritization critical
- **Mitigation**: Clear roadmap, community contributions encouraged

#### Hardware
- **Available**: I210, I226 (maintainer hardware)
- **Limited/None**: I217, I219, I225 physical access
- **Impact**: Some features remain "code-ready but unvalidated"
- **Mitigation**: Community testing program, virtual validation where possible

### Signing & Distribution Constraints

#### Short-Term (v1.0)
- **Method**: Test-signing only
- **Requirements**:
  - `bcdedit /set testsigning on`
  - Self-signed certificate via PowerShell scripts
  - Secure Boot disabled or configured for test certificates
- **Pros**: Free, immediate, full control
- **Cons**: Users must accept development-mode constraints, less trusted

#### Medium-Term (v2.0+)
- **Option A: Windows HLK (Hardware Lab Kit)**
  - Requires: HLK test infrastructure, time investment (weeks)
  - Cost: Free (but high time cost)
  - Benefit: Official Microsoft signature, full trust
  - Blocker: Significant setup complexity for volunteer project

- **Option B: EV Code Signing + Attestation**
  - Requires: Business entity, EV certificate purchase
  - Cost: $300-500/year
  - Benefit: Trusted by Windows, moderate effort
  - Blocker: Requires funding/sponsorship

#### Long-Term (Production)
- **Path**: Documented for future sponsors
- **Outcome**: Clear roadmap to production-signed driver if funding becomes available

---

## Project Rules & Principles

### Core Principles

#### 1. No Fake Modes
- ❌ **Prohibited**: Simulation, stubs, fake success responses
- ✅ **Required**: Only report features that are hardware-validated
- **Rationale**: User trust depends on honest behavior

#### 2. Evidence-Based Development
- ❌ **Prohibited**: Assumptions about register behavior without datasheet/testing
- ✅ **Required**: All features backed by Intel datasheets or empirical testing
- **Rationale**: Prevents subtle bugs and unexpected hardware interactions

#### 3. Graceful Degradation
- ❌ **Prohibited**: Silent failures or misleading success codes
- ✅ **Required**: Clear error messages explaining why feature is unavailable
- **Rationale**: Users can adapt to limitations if clearly communicated

#### 4. Defensive Programming
- ✅ **Required**: Validate all IOCTL inputs, bounds checks on arrays, NULL pointer guards
- ✅ **Required**: Error handling for all hardware access (timeouts, invalid responses)
- **Rationale**: Kernel-mode code must never crash; BSOD is unacceptable

### Development Practices

#### Code Quality
- All new code must pass static analysis (SDV, Code Analysis)
- No compiler warnings at `/W4` level
- Consistent coding style per NDIS filter conventions

#### Testing
- All features require user-mode test tool demonstrating functionality
- Regression tests for known bugs (e.g., IOCTL_AVB_GET_CLOCK_CONFIG)
- Performance benchmarks before/after major changes

#### Documentation
- README/TODO/STATUS updated with every feature change
- Per-controller capability matrix maintained
- CHANGELOG follows Keep a Changelog format

---

## Scope Definition

### In Scope for v1.0

#### 1. Supported Controllers (Baseline Feature Set)

**I210, I219, I225, I226**:
- ✅ Detection and context creation
- ✅ MMIO register access (validated)
- ✅ Basic PTP/TSN IOCTL surface
- ✅ Device-specific initialization sequences

**I217**:
- ⚠️ Code exists but requires hardware validation
- ⚠️ Goal: Bring to same level as others OR clearly mark as "experimental"
- ⚠️ May remain community-tested if no hardware access

#### 2. Feature Categories

**PTP / IEEE 1588**:
- ✅ Clock discovery per controller
- ✅ Clock configuration and status (including `IOCTL_AVB_GET_CLOCK_CONFIG`)
- ✅ SYSTIM increment verification and stability tests
- ✅ Basic frequency adjustment
- ⚠️ Advanced PTP protocol stack (out of scope; user-mode responsibility)

**TSN / AVB Features**:
- ✅ **TAS (Time-Aware Shaper)**: Configuration, validation, on-wire proof
- ✅ **Frame Preemption (FP)**: Configuration, status reporting
- ✅ **Credit-Based Shaper / QAV**: Where applicable per controller
- ✅ **MDIO/PHY**: Link speed, EEE, PHY register access
- ⚠️ **PTM (Precision Time Measurement)**: Basic support if hardware allows

**Device Management**:
- ✅ Adapter enumeration via IOCTL
- ✅ Per-adapter capability reporting
- ✅ Hardware state tracking (AVB_HW_STATE machine)
- ✅ Safe attach/detach lifecycle

#### 3. Tooling & Testing

**CLI/Test Tools** (existing, to be maintained):
- ✅ `avb_test_i210_um.exe` - I210 validation
- ✅ `avb_test_i226.exe` - I226 validation (to be created/enhanced)
- ✅ `check_ioctl.c` - IOCTL surface validation
- ✅ `comprehensive_ioctl_test.exe` - Full IOCTL regression suite
- ✅ `quick_diagnostics.exe` - Fast health check

**Documentation**:
- ✅ `README.md` - Setup, build, installation
- ✅ `IMPLEMENTATION_STATUS_SUMMARY.md` - Feature status per controller
- ✅ `TODO.md` - Prioritized work items
- ✅ `CHANGELOG.md` - Release history
- ✅ `include/avb_ioctl.md` - IOCTL API reference

#### 4. Build & Deployment

- ✅ Visual Studio solution builds cleanly on VS2019/VS2022
- ✅ Automated test-signing scripts (PowerShell/batch)
- ✅ Installation/uninstallation procedures documented
- ✅ GitHub Actions CI for build validation (optional enhancement)

### Out of Scope for v1.0

#### Not Included
- ❌ Official Microsoft-signed driver (HLK/attestation)
- ❌ Non-Intel NICs (Broadcom, Realtek, Marvell)
- ❌ Legacy Intel chips lacking AVB/TSN (82574L, 82575, 82576, 82580, I350)
- ❌ Full Milan/AVDECC protocol stack (driver only exposes hardware)
- ❌ User-mode gPTP daemon (separate project)
- ❌ GUI configuration tool (CLI/IOCTL API only)

#### Explicitly Deferred
- ⏭️ ARM64 optimization (basic support only, not performance-tuned)
- ⏭️ Windows 10 RS3 and earlier (target 1809+)
- ⏭️ Advanced power management (D3 states, wake patterns)
- ⏭️ RSS/VMQ interaction optimization

---

## Risks & Mitigation

### Risk 1: Hardware Behavior Uncertainty
**Description**: Intel datasheets may omit undocumented initialization sequences (e.g., I226 TAS, EEE enable), causing features to remain "hardware-blocked."

**Impact**: HIGH - Core features unusable despite correct register writes

**Probability**: MEDIUM - Already encountered with I226 PTP initialization

**Mitigation**:
- ✅ Per-controller status tracking (explicit "Needs Investigation" state)
- ✅ Expose low-level register access for community experimentation
- ✅ Document all quirks and workarounds discovered
- ✅ Community testing program for hardware variants

---

### Risk 2: Windows Update Compatibility
**Description**: Future Windows or Intel driver updates could interact badly with filter driver attaching to AVB/TSN hardware.

**Impact**: MEDIUM - Driver stops working after OS/driver update

**Probability**: LOW - Filter driver model is designed for stability

**Mitigation**:
- ✅ Safe uninstall path documented
- ✅ Quick rollback guidance in README
- ✅ Version tracking for tested Windows/Intel driver combinations
- ✅ Community reports of compatibility issues

---

### Risk 3: Signing & Trust Limitations
**Description**: Test-signed drivers are less acceptable in some organizations; users may abandon project.

**Impact**: MEDIUM - Limits adoption in corporate/production environments

**Probability**: HIGH - Test-signing requires Secure Boot changes

**Mitigation**:
- ✅ Clear documentation of test-signing requirements
- ✅ Provide alternatives (HLK path documented for sponsors)
- ✅ Target users who accept development-mode constraints
- ✅ High code quality makes future HLK run realistic

---

### Risk 4: Single-Maintainer Bottleneck
**Description**: Single-maintainer project vulnerable to burnout or life changes.

**Impact**: HIGH - Project stagnation or abandonment

**Probability**: MEDIUM - Common issue in volunteer open-source

**Mitigation**:
- ✅ Clean repo structure encourages external contributors
- ✅ Contribution guidelines (CONTRIBUTING.md)
- ✅ Clear TODO and status docs for onboarding
- ✅ Modular architecture allows incremental contributions
- ✅ Liberal MIT license encourages forks if needed

---

### Risk 5: Limited Hardware Access
**Description**: Maintainer lacks physical access to some controllers (I217, I219, I225).

**Impact**: MEDIUM - Features remain unvalidated for those controllers

**Probability**: HIGH - Already documented in status matrix

**Mitigation**:
- ✅ Community testing program (call for testers with specific hardware)
- ✅ Code marked as "ready but unvalidated"
- ✅ Clear expectations: users with hardware can validate and report
- ✅ Virtual testing where possible (register-level simulation)

---

## Success Metrics & KPIs

### Development Metrics

**Code Quality**:
- ✅ Zero compiler warnings at `/W4`
- ✅ Zero static analysis errors (SDV clean)
- ✅ Test coverage >80% for critical paths (IOCTL handlers, hardware access)

**Stability**:
- ✅ Zero BSODs in 100+ hours of stress testing
- ✅ Zero memory leaks detected (Driver Verifier clean)
- ✅ <5 crashes per 1000 driver loads in community testing

**Performance**:
- ✅ Throughput degradation <5% (iperf3 benchmark)
- ✅ Latency increase <10μs (packet forwarding)
- ✅ CPU overhead <1% when idle

### Community Metrics

**Adoption**:
- ✅ 100+ GitHub stars (indicates interest)
- ✅ 10+ community contributors (pull requests merged)
- ✅ 50+ successful installations reported

**Support**:
- ✅ <48 hour median response time on GitHub Issues
- ✅ >90% of reported bugs have actionable status update
- ✅ Clear documentation eliminates repetitive questions

**Feedback**:
- ✅ 3+ user testimonials/success stories
- ✅ 1+ research paper citing driver
- ✅ 1+ commercial project using driver in testing/demo capacity

---

## Initial Roadmap

### Phase 0: Baseline Documentation Sync (Week 1-2)

**Goal**: Align all documentation and establish GitHub Issues workflow

**Tasks**:
1. ✅ Create `01-stakeholder-requirements/PROJECT-CHARTER.md` (this document)
2. ✅ Create GitHub Issue templates (StR, REQ-F, REQ-NF, TEST, ADR)
3. ✅ Define v1.0 target scope per controller
4. ✅ Sync README/TODO/IMPLEMENTATION_STATUS_SUMMARY (eliminate contradictions)
5. ✅ Create initial stakeholder requirement issues

**Deliverables**:
- `01-stakeholder-requirements/` populated
- GitHub Issue templates configured
- `docs/IntelAvbFilter-v1.0-Scope.md`

---

### Phase 1: IOCTL Surface Hardening (Week 3-6)

**Goal**: Clean, predictable IOCTL API behavior before deep feature work

**Priority 1 Tasks**:
1. 🔧 **Fix `IOCTL_AVB_GET_CLOCK_CONFIG`** (current blocker)
   - Instrument with debug logging (DebugView)
   - Trace device selection and register reads
   - Define exact error contract (success vs. known errors)
   - Test on all controllers (I210, I219, I225, I226)

2. 📋 **Document IOCTL Registry**
   - All IOCTL codes, purpose, structures, per-controller support
   - Resolve code conflicts (e.g., 39→45 change for GET_CLOCK_CONFIG)
   - Create `docs/IOCTL-API-REFERENCE.md`

3. 🧪 **Expand IOCTL Test Suite**
   - Enhance `check_ioctl.c` / `comprehensive_ioctl_test.exe`
   - Assert expected behavior per IOCTL
   - No `ERROR_INVALID_FUNCTION` unless intentionally unsupported
   - CI integration (GitHub Actions)

**Deliverables**:
- `IOCTL_AVB_GET_CLOCK_CONFIG` working on all controllers or documented as unsupported
- `docs/IOCTL-API-REFERENCE.md` complete
- Comprehensive IOCTL regression test suite

---

### Phase 2: PTP Stabilization (Week 7-10)

**Goal**: Proven PTP clock functionality on I226 and I210

**Priority 1 Tasks**:
1. 🔬 **Stabilize I226 PTP Clock**
   - Use SYSTIM readouts with external reference
   - Validate stability and drift over 24+ hours
   - Document clock configuration sequence
   - Measure synchronization accuracy

2. 🔧 **Unstick I210 Clock**
   - Investigate initialization sequences (KeStallExecutionProcessor timings)
   - Test timeout adjustments and register write ordering
   - Document I210-specific quirks discovered
   - Verify SYSTIM increments reliably

3. 📊 **Clock Status for Other Controllers**
   - I219: Determine PTP support status (likely limited)
   - I225: Validate clock capabilities
   - I217: Document status (needs hardware validation)

**Deliverables**:
- I226 PTP validated stable (drift/stability report)
- I210 SYSTIM unstuck and documented
- Per-controller PTP capability matrix updated

---

### Phase 3: TSN Hardware Activation (Week 11-16)

**Goal**: At least one TSN feature proven working on wire (I226 focus)

**Priority 1 Tasks**:
1. 🎯 **I226 TAS (Time-Aware Shaper) MVP**
   - Take existing TAS IOCTL path to on-wire effects
   - Document required preconditions (link speed, duplex, etc.)
   - Packet capture proving TAS schedule enforcement
   - Create reproducible test scenario

2. 🔀 **Frame Preemption Verification**
   - Configuration via IOCTL
   - Traffic generation + capture to prove preemption
   - Document hardware/link partner requirements

3. 📈 **Credit-Based Shaper (CBS/QAV)**
   - I226 CBS configuration and validation
   - Traffic shaping verification
   - Bandwidth reservation proof

**Deliverables**:
- I226 TAS working with packet capture proof
- Frame Preemption configuration validated
- TSN feature status matrix updated

---

### Phase 4: Production Readiness (Week 17-24)

**Goal**: v1.0 release ready for community use

**Priority 1 Tasks**:
1. 📊 **Performance Benchmarking**
   - iperf3 baseline vs. filter-attached
   - Latency measurements (packet forwarding)
   - CPU overhead profiling

2. 🔒 **Stability & Stress Testing**
   - 24-hour stress test (traffic + IOCTL load)
   - Driver Verifier validation (no leaks)
   - Clean attach/detach cycles (100+ iterations)

3. 📚 **Documentation Completion**
   - User guide (installation, usage, troubleshooting)
   - Developer guide (architecture, contribution guidelines)
   - API reference (complete IOCTL documentation)
   - Release notes and CHANGELOG

4. 🚀 **v1.0 Release**
   - Tag release in GitHub
   - Build signed binaries (test-signing)
   - Announce to community (Reddit, HackerNews, AVB forums)

**Deliverables**:
- v1.0 release binaries (test-signed)
- Complete documentation suite
- Performance/stability reports
- Community announcement

---

## Governance & Decision Making

### Project Ownership
- **Current Owner**: zarfld
- **Decision Authority**: Benevolent dictator model (maintainer has final say)
- **Future**: Consider co-maintainers if project grows

### Contribution Process
1. Fork repository
2. Create feature branch
3. Submit pull request with:
   - Clear description of change
   - Test results on actual hardware (if applicable)
   - Updated documentation
4. Code review by maintainer
5. Merge after approval

### Issue Prioritization
- **P0 (Blocker)**: Crashes, data corruption, security issues
- **P1 (High)**: Core features not working (e.g., IOCTL_AVB_GET_CLOCK_CONFIG)
- **P2 (Medium)**: Feature enhancements, quality improvements
- **P3 (Low)**: Nice-to-have, documentation, optimization

---

## Budget & Resources

### Current State (Open Source)
- **Budget**: $0
- **Hardware**: I210, I226 (maintainer-owned)
- **Time**: Volunteer (evenings/weekends)
- **Infrastructure**: GitHub (free for open source)

### Needs for Production Signing (Future)
- **EV Code Signing Certificate**: $300-500/year
- **HLK Test Infrastructure**: Time investment (weeks of setup)
- **Additional Hardware**: I217, I219, I225 for validation (~$100-300)

### Sponsorship Opportunities
- **Hardware Donations**: Intel NICs for testing
- **Funding**: Patreon/Open Collective for signing certificate
- **Corporate Sponsorship**: Companies using driver in products

---

## Appendix: Standards Compliance

This project follows:
- **ISO/IEC/IEEE 12207:2017**: Software lifecycle processes
- **ISO/IEC/IEEE 29148:2018**: Requirements engineering
- **IEEE 1016-2009**: Design documentation
- **Extreme Programming (XP)**: Test-driven development, continuous integration

---

## Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2025-12-07 | zarfld + AI | Initial project charter based on existing codebase and documentation |

---

**Next Steps**: 
1. Review and approve this charter
2. Create initial stakeholder requirement issues
3. Begin Phase 1: IOCTL Surface Hardening (fix `IOCTL_AVB_GET_CLOCK_CONFIG`)
