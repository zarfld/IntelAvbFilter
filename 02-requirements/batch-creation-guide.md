# Phase 02 Requirements - Batch Creation Guide

**Created**: 2025-12-07  
**Status**: 3 of 56 requirements created  
**Remaining**: 53 requirements

## ✅ Issues Created So Far

| Issue # | Requirement ID | Title | Priority | Traces To |
|---------|---------------|-------|----------|-----------|
| #34 | REQ-F-IOCTL-PHC-001 | PHC Time Query | Critical | #28 (gPTP) |
| #35 | REQ-F-IOCTL-TS-001 | TX Timestamp Retrieval | Critical | #28 (gPTP) |
| #36 | REQ-F-NDIS-ATTACH-001 | FilterAttach/FilterDetach | Critical | #31 (NDIS) |

## 📋 Remaining Requirements (53)

### Critical Priority (8 remaining)

| Requirement ID | Title | Traces To | Description |
|---------------|-------|-----------|-------------|
| REQ-F-IOCTL-TS-002 | RX Timestamp Retrieval | #28 | Retrieve hardware RX timestamps |
| REQ-F-IOCTL-PHC-003 | PHC Offset Adjustment | #28 | Adjust PHC by offset (ns) |
| REQ-F-IOCTL-PHC-004 | PHC Frequency Adjustment | #28 | Adjust PHC frequency (ppb) |
| REQ-F-INTEL-AVB-003 | Register Access Abstraction | #29 | All HW access via intel_avb |
| REQ-F-PTP-EPOCH-001 | PTP Epoch (TAI) | #30 | Use IEEE 1588 TAI epoch |
| REQ-F-NDIS-SEND-001 | FilterSend | #31 | NDIS packet send callbacks |
| REQ-F-NDIS-RECEIVE-001 | FilterReceive | #31 | NDIS packet receive callbacks |
| REQ-F-HW-DETECT-001 | Hardware Capability Detection | #31 | Detect PHC/TAS/CBS support |
| REQ-F-REG-ACCESS-001 | Safe Register Access | #31 | Coordinate register access |
| REQ-NF-PERF-NDIS-001 | Packet Forwarding (<1µs) | #31 | NDIS filter overhead <1µs |
| REQ-NF-REL-PHC-001 | PHC Monotonicity | #28 | PHC never goes backwards |
| REQ-NF-REL-NDIS-001 | No BSOD (1000 cycles) | #31 | Survive 1000 attach/detach |
| REQ-NF-PERF-PTP-001 | PTP Sync (<100ns) | #33 | PTP sync accuracy |
| REQ-NF-COMPAT-NDIS-001 | Windows Compatibility | #31 | Windows 10 1809+ support |

### High Priority (23 remaining)

From StR-001 (gPTP):
- REQ-F-IOCTL-XSTAMP-001: Cross Timestamp Query
- REQ-F-IOCTL-ERROR-001: Error Code Enumeration
- REQ-F-IOCTL-CAP-001: Hardware Capability Query
- REQ-NF-PERF-PHC-001: PHC Query Latency (<500µs)
- REQ-NF-PERF-TS-001: Timestamp Retrieval (<1µs)
- REQ-NF-REL-TS-001: Timestamp Loss (<0.01%)

From StR-002 (intel_avb):
- REQ-F-INTEL-AVB-001: Submodule Integration
- REQ-F-INTEL-AVB-002: Kernel-Mode Compatibility
- REQ-F-INTEL-AVB-004: Type Mapping
- REQ-F-INTEL-AVB-005: Error Propagation
- REQ-NF-REL-LIB-001: Test Coverage (>90%)
- REQ-NF-MAINT-LIB-001: Single Source of Truth

From StR-003 (Standards):
- REQ-F-NAMING-001: IEEE Standards Naming
- REQ-F-PTP-LEAPSEC-001: Leap Second Handling
- REQ-F-TSN-SEMANTICS-001: TSN Feature Semantics
- REQ-NF-STD-NAMING-001: IEEE Naming (100%)
- REQ-NF-INTEROP-STANDARDS-001: Standards-Based Interop

From StR-004 (NDIS):
- REQ-F-NDIS-OID-001: FilterOidRequest
- REQ-F-NDIS-STATUS-001: FilterStatus
- REQ-F-HW-CONFIG-001: Hardware Configuration Query
- REQ-F-IOCTL-NIC-001: NIC Identity Query
- REQ-NF-PERF-REG-001: Register Access (<10µs)

From StR-005 (Future Service):
- REQ-F-CONFIG-ATOMIC-001: Configuration Atomicity
- REQ-F-ERROR-ACTIONABLE-001: Actionable Error Messages

From StR-006 (Endpoints):
- REQ-F-TAS-001: TAS Schedule Configuration
- REQ-F-CBS-001: CBS Parameter Configuration
- REQ-F-VLAN-001: VLAN Tag Handling
- REQ-F-PTP-INTEROP-001: PTP Interoperability
- REQ-F-QAV-001: Qav Bandwidth Reservation
- REQ-NF-INTEROP-MILAN-001: Milan Compatibility (3+ vendors)
- REQ-NF-INTEROP-TSN-001: TSN Switch Compatibility (2+ vendors)

### Medium Priority (22 remaining)

From StR-003 (Standards):
- REQ-F-8021AS-STATE-001: 802.1AS State Machines
- REQ-F-STANDARDS-REF-001: IEEE Clause References

From StR-005 (Future Service):
- REQ-F-IOCTL-VERSIONING-001: IOCTL API Versioning
- REQ-F-NIC-IDENTITY-002: Stable NIC Identity
- REQ-F-RELOAD-PREDICTABLE-001: Predictable Reload
- REQ-NF-PERF-IOCTL-001: IOCTL Latency (<10ms)
- REQ-NF-MAINT-API-001: API Versioning Strategy

From StR-006 (Endpoints):
- REQ-F-AVTP-001: AVTP Stream Support
- REQ-F-MILAN-001: Milan Discovery Support
- REQ-NF-STD-MILAN-001: AVnu Milan Compliance

---

## 🚀 Batch Creation Methods

### Method 1: GitHub Web Interface (Manual but Accurate)

For each requirement:

1. Navigate to: https://github.com/zarfld/IntelAvbFilter/issues/new/choose
2. Select template: **Functional Requirement (REQ-F)** or **Non-Functional Requirement (REQ-NF)**
3. Copy issue body from `02-requirements/requirements-elicitation-report-phase02.md`
4. Update Traces to:  Replace `#28` with actual parent StR issue number
5. Add labels:
   - `type:requirement:functional` or `type:requirement:non-functional`
   - `phase:02-requirements`
   - `priority:critical` / `priority:high` / `priority:medium`
   - For NFRs: `nfr:performance` / `nfr:reliability` / `nfr:standards` / `nfr:maintainability` / `nfr:interoperability` / `nfr:compatibility`
6. Submit → Record issue number

**Estimated Time**: 53 requirements × 5 minutes = ~4.5 hours

### Method 2: GitHub CLI (Faster, Scripted)

Install GitHub CLI: https://cli.github.com/

```powershell
# Install GitHub CLI (if not installed)
winget install --id GitHub.cli

# Authenticate
gh auth login

# Create requirements from JSON
$json = Get-Content "02-requirements/requirements-batch.json" | ConvertFrom-Json

foreach ($req in $json) {
    $title = "$($req.id): $($req.title)"
    $labels = if ($req.type -eq "functional") {
        "type:requirement:functional,phase:02-requirements,priority:$($req.priority)"
    } else {
        "type:requirement:non-functional,phase:02-requirements,priority:$($req.priority),nfr:$($req.category)"
    }
    
    # Body template (minimal version, full version in elicitation report)
    $body = @"
## Requirement Information

**Requirement ID**: $($req.id)
**Title**: $($req.title)
**Priority**: $($req.priority)
**Status**: Draft

## Requirement Statement

**The system shall** $($req.description)

## Traceability

- Traces to:  #$($req.traces_to) (StR parent issue)
- **Verified by**: TEST-$($req.id.Replace('REQ-', '')) (Phase 07)

## Priority Justification

- **Business Impact**: Per StR-#$($req.traces_to)
- **Estimated Effort**: TBD

**Note**: Full specification in requirements-elicitation-report-phase02.md
"@

    # Create issue
    gh issue create `
        --repo zarfld/IntelAvbFilter `
        --title $title `
        --body $body `
        --label $labels
    
    Write-Host "Created: $title" -ForegroundColor Green
    Start-Sleep -Seconds 2  # Rate limiting
}
```

**Estimated Time**: 53 requirements × 5 seconds = ~5 minutes (automated)

### Method 3: Copilot-Assisted Creation (Best Balance)

I can continue creating issues via GitHub MCP tools. This is most accurate but slower.

**Pros**:
- Full issue bodies from elicitation report
- Proper traceability links
- Correct labels

**Cons**:
- Takes ~1-2 minutes per issue
- Total time: ~90-120 minutes

---

## 📊 Creation Progress Tracking

| Priority | Total | Created | Remaining | % Complete |
|----------|-------|---------|-----------|------------|
| Critical | 14 | 3 | 11 | 21% |
| High | 26 | 0 | 26 | 0% |
| Medium | 16 | 0 | 16 | 0% |
| **Total** | **56** | **3** | **53** | **5%** |

---

## 🎯 Recommended Approach

Given the volume (53 remaining issues), I recommend:

### Option A: GitHub CLI (FASTEST - 5 minutes)
Run the PowerShell script above to batch-create all issues with minimal bodies, then enrich them later.

### Option B: Copilot Continues (MOST ACCURATE - 2 hours)
I continue creating issues one-by-one with full bodies from the elicitation report.

### Option C: Hybrid Approach (BALANCED - 30 minutes)
- **Copilot creates**: Critical priority (11 issues) = ~20 minutes
- **You create via CLI**: High/Medium priority (42 issues) = ~10 minutes

---

## 📋 Next Steps After Creation

1. **Update Parent StR Issues** (#28-#33)
   - Add comments: "Refined by: REQ-F-XXX, REQ-F-YYY, REQ-NF-ZZZ"

2. **Create Traceability Matrix** (`02-requirements/traceability-matrix.md`)
   - Map: StR → REQ-F → REQ-NF → TEST (Phase 07)

3. **Requirements Review**
   - Schedule stakeholder review of all 56 requirements
   - Prioritize implementation order

4. **Begin Phase 04 (Detailed Design)**
   - Create ADRs for architecture decisions
   - Design component interfaces
   - Create class diagrams

---

**Which approach would you like to use?**

- **A**: Copilot continues creating all 53 issues (2 hours, most accurate)
- **B**: Use GitHub CLI script (5 minutes, minimal bodies)
- **C**: Hybrid (Copilot does critical 11, CLI does rest 42)
