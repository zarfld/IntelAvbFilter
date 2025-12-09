# Create Phase 02 Requirements Issues
# This script creates all REQ-F and REQ-NF GitHub issues from the elicitation report

$owner = "zarfld"
$repo = "IntelAvbFilter"

# Define requirements to create
$requirements = @(
    # From StR-001 (gPTP Stack) - #28
    @{
        id = "REQ-F-IOCTL-TS-002"
        title = "RX Timestamp Retrieval"
        priority = "critical"
        traces_to = 28
        type = "functional"
        description = "Provide IOCTL to retrieve hardware RX timestamps for received PTP packets"
    },
    @{
        id = "REQ-F-IOCTL-PHC-003"
        title = "PHC Offset Adjustment"
        priority = "critical"
        traces_to = 28
        type = "functional"
        description = "Provide IOCTL to adjust PHC time by offset (nanoseconds)"
    },
    @{
        id = "REQ-F-IOCTL-PHC-004"
        title = "PHC Frequency Adjustment"
        priority = "critical"
        traces_to = 28
        type = "functional"
        description = "Provide IOCTL to adjust PHC frequency (ppb)"
    },
    @{
        id = "REQ-F-IOCTL-XSTAMP-001"
        title = "Cross Timestamp Query"
        priority = "high"
        traces_to = 28
        type = "functional"
        description = "Provide IOCTL to query simultaneous PHC and system time (cross-timestamp)"
    },
    @{
        id = "REQ-F-IOCTL-ERROR-001"
        title = "Error Code Enumeration"
        priority = "high"
        traces_to = 28
        type = "functional"
        description = "Define enumeration of all IOCTL error codes with descriptions"
    },
    @{
        id = "REQ-F-IOCTL-CAP-001"
        title = "Hardware Capability Query"
        priority = "high"
        traces_to = 28
        type = "functional"
        description = "Provide IOCTL to query PHC/TAS/CBS capability flags per NIC"
    },
    
    # From StR-002 (Intel AVB Library) - #29
    @{
        id = "REQ-F-INTEL-AVB-001"
        title = "Submodule Integration"
        priority = "high"
        traces_to = 29
        type = "functional"
        description = "Integrate intel_avb library as Git submodule"
    },
    @{
        id = "REQ-F-INTEL-AVB-002"
        title = "Kernel-Mode Compatibility"
        priority = "high"
        traces_to = 29
        type = "functional"
        description = "Ensure intel_avb API is kernel-mode compatible (IRQL, no malloc)"
    },
    @{
        id = "REQ-F-INTEL-AVB-003"
        title = "Register Access Abstraction"
        priority = "critical"
        traces_to = 29
        type = "functional"
        description = "All hardware register access must go through intel_avb library"
    },
    @{
        id = "REQ-F-INTEL-AVB-004"
        title = "Type Mapping (IOCTL to intel_avb)"
        priority = "high"
        traces_to = 29
        type = "functional"
        description = "Map IOCTL structures to intel_avb data types"
    },
    @{
        id = "REQ-F-INTEL-AVB-005"
        title = "Error Propagation"
        priority = "high"
        traces_to = 29
        type = "functional"
        description = "Translate intel_avb errors to NTSTATUS codes"
    },
    
    # From StR-003 (Standards Semantics) - #30
    @{
        id = "REQ-F-NAMING-001"
        title = "IEEE Standards Naming"
        priority = "high"
        traces_to = 30
        type = "functional"
        description = "Use IEEE-compliant names (grandmasterID not gmId)"
    },
    @{
        id = "REQ-F-PTP-EPOCH-001"
        title = "PTP Epoch (TAI)"
        priority = "critical"
        traces_to = 30
        type = "functional"
        description = "PHC time uses IEEE 1588 epoch (1970-01-01 TAI, not UTC)"
    },
    @{
        id = "REQ-F-PTP-LEAPSEC-001"
        title = "Leap Second Handling"
        priority = "high"
        traces_to = 30
        type = "functional"
        description = "Handle leap seconds per IEEE 1588-2019"
    },
    @{
        id = "REQ-F-8021AS-STATE-001"
        title = "802.1AS State Machines"
        priority = "medium"
        traces_to = 30
        type = "functional"
        description = "Implement 802.1AS state machines per Figure 10-1"
    },
    @{
        id = "REQ-F-TSN-SEMANTICS-001"
        title = "TSN Feature Semantics"
        priority = "high"
        traces_to = 30
        type = "functional"
        description = "TAS/CBS/Qav semantics match IEEE 802.1Q definitions"
    },
    @{
        id = "REQ-F-STANDARDS-REF-001"
        title = "IEEE Clause References"
        priority = "medium"
        traces_to = 30
        type = "functional"
        description = "All features cite specific IEEE standard clauses"
    },
    
    # From StR-004 (NDIS Miniport) - #31
    @{
        id = "REQ-F-NDIS-ATTACH-001"
        title = "FilterAttach/FilterDetach"
        priority = "critical"
        traces_to = 31
        type = "functional"
        description = "Implement NDIS FilterAttach/FilterDetach callbacks"
    },
    @{
        id = "REQ-F-NDIS-SEND-001"
        title = "FilterSend/FilterSendNetBufferLists"
        priority = "critical"
        traces_to = 31
        type = "functional"
        description = "Implement NDIS FilterSend callbacks for packet transmission"
    },
    @{
        id = "REQ-F-NDIS-RECEIVE-001"
        title = "FilterReceive/FilterReceiveNetBufferLists"
        priority = "critical"
        traces_to = 31
        type = "functional"
        description = "Implement NDIS FilterReceive callbacks for packet reception"
    },
    @{
        id = "REQ-F-NDIS-OID-001"
        title = "FilterOidRequest"
        priority = "high"
        traces_to = 31
        type = "functional"
        description = "Implement FilterOidRequest to query hardware capabilities"
    },
    @{
        id = "REQ-F-NDIS-STATUS-001"
        title = "FilterStatus"
        priority = "high"
        traces_to = 31
        type = "functional"
        description = "Implement FilterStatus to handle miniport status indications"
    },
    @{
        id = "REQ-F-HW-DETECT-001"
        title = "Hardware Capability Detection"
        priority = "critical"
        traces_to = 31
        type = "functional"
        description = "Detect PHC/TAS/CBS/timestamp support per NIC model"
    },
    @{
        id = "REQ-F-HW-CONFIG-001"
        title = "Hardware Configuration Query"
        priority = "high"
        traces_to = 31
        type = "functional"
        description = "Query current TAS/CBS/PHC configuration from hardware"
    },
    @{
        id = "REQ-F-REG-ACCESS-001"
        title = "Safe Register Access"
        priority = "critical"
        traces_to = 31
        type = "functional"
        description = "Coordinate register access with miniport (spin locks, IRQL)"
    },
    @{
        id = "REQ-F-IOCTL-NIC-001"
        title = "NIC Identity Query"
        priority = "high"
        traces_to = 31
        type = "functional"
        description = "Provide IOCTL to query NIC identity (PCI location, friendly name)"
    },
    
    # From StR-005 (Future Service) - #32
    @{
        id = "REQ-F-IOCTL-VERSIONING-001"
        title = "IOCTL API Versioning"
        priority = "medium"
        traces_to = 32
        type = "functional"
        description = "Implement IOCTL versioning for API evolution"
    },
    @{
        id = "REQ-F-CONFIG-ATOMIC-001"
        title = "Configuration Atomicity"
        priority = "high"
        traces_to = 32
        type = "functional"
        description = "Rollback configuration changes on partial failure"
    },
    @{
        id = "REQ-F-ERROR-ACTIONABLE-001"
        title = "Actionable Error Messages"
        priority = "high"
        traces_to = 32
        type = "functional"
        description = "Error messages include clear descriptions and remediation steps"
    },
    @{
        id = "REQ-F-NIC-IDENTITY-002"
        title = "Stable NIC Identity"
        priority = "medium"
        traces_to = 32
        type = "functional"
        description = "NIC identity stable across reboots (PCI location + friendly name)"
    },
    @{
        id = "REQ-F-RELOAD-PREDICTABLE-001"
        title = "Predictable Reload Behavior"
        priority = "medium"
        traces_to = 32
        type = "functional"
        description = "Driver reload preserves PHC/TAS/CBS state where possible"
    },
    
    # From StR-006 (Endpoints) - #33
    @{
        id = "REQ-F-TAS-001"
        title = "TAS Schedule Configuration"
        priority = "high"
        traces_to = 33
        type = "functional"
        description = "Provide IOCTL to configure TAS (Qbv) gate schedules"
    },
    @{
        id = "REQ-F-CBS-001"
        title = "CBS Parameter Configuration"
        priority = "high"
        traces_to = 33
        type = "functional"
        description = "Provide IOCTL to configure CBS (Qav) shaper parameters"
    },
    @{
        id = "REQ-F-VLAN-001"
        title = "VLAN Tag Handling"
        priority = "high"
        traces_to = 33
        type = "functional"
        description = "Properly handle VLAN Priority Code Point (PCP) for TSN traffic"
    },
    @{
        id = "REQ-F-AVTP-001"
        title = "AVTP Stream Support"
        priority = "medium"
        traces_to = 33
        type = "functional"
        description = "Support AVTP Class A/B streams with latency guarantees"
    },
    @{
        id = "REQ-F-MILAN-001"
        title = "Milan Discovery Support"
        priority = "medium"
        traces_to = 33
        type = "functional"
        description = "Support Milan AVDECC device enumeration"
    },
    @{
        id = "REQ-F-PTP-INTEROP-001"
        title = "PTP Interoperability"
        priority = "high"
        traces_to = 33
        type = "functional"
        description = "Interoperate with multi-vendor PTP grandmasters"
    },
    @{
        id = "REQ-F-QAV-001"
        title = "Qav Bandwidth Reservation"
        priority = "high"
        traces_to = 33
        type = "functional"
        description = "Implement Qav bandwidth reservation per 802.1Q Annex L"
    },
    
    # Non-Functional Requirements (Performance)
    @{
        id = "REQ-NF-PERF-PHC-001"
        title = "PHC Query Latency (<500µs P95)"
        priority = "high"
        traces_to = 28
        type = "non-functional"
        category = "performance"
        description = "PHC time queries complete in <500µs at 95th percentile"
    },
    @{
        id = "REQ-NF-PERF-TS-001"
        title = "Timestamp Retrieval (<1µs)"
        priority = "high"
        traces_to = 28
        type = "non-functional"
        category = "performance"
        description = "TX/RX timestamp retrieval completes in <1µs"
    },
    @{
        id = "REQ-NF-PERF-NDIS-001"
        title = "Packet Forwarding (<1µs overhead)"
        priority = "critical"
        traces_to = 31
        type = "non-functional"
        category = "performance"
        description = "NDIS filter adds <1µs packet forwarding overhead"
    },
    @{
        id = "REQ-NF-PERF-REG-001"
        title = "Register Access (<10µs)"
        priority = "high"
        traces_to = 31
        type = "non-functional"
        category = "performance"
        description = "Hardware register access completes in <10µs"
    },
    @{
        id = "REQ-NF-PERF-IOCTL-001"
        title = "IOCTL Latency (<10ms config)"
        priority = "medium"
        traces_to = 32
        type = "non-functional"
        category = "performance"
        description = "Configuration IOCTLs complete in <10ms, statistics in <1ms"
    },
    @{
        id = "REQ-NF-PERF-PTP-001"
        title = "PTP Sync Accuracy (<100ns offset)"
        priority = "critical"
        traces_to = 33
        type = "non-functional"
        category = "performance"
        description = "PTP sync achieves <100ns offset, <1µs convergence"
    },
    
    # Non-Functional Requirements (Reliability)
    @{
        id = "REQ-NF-REL-PHC-001"
        title = "PHC Monotonicity Guarantee"
        priority = "critical"
        traces_to = 28
        type = "non-functional"
        category = "reliability"
        description = "PHC time never goes backwards (monotonic)"
    },
    @{
        id = "REQ-NF-REL-NDIS-001"
        title = "No BSOD (1000 attach/detach cycles)"
        priority = "critical"
        traces_to = 31
        type = "non-functional"
        category = "reliability"
        description = "Driver survives 1000 attach/detach cycles without BSOD"
    },
    @{
        id = "REQ-NF-REL-LIB-001"
        title = "Test Coverage (>90% intel_avb)"
        priority = "high"
        traces_to = 29
        type = "non-functional"
        category = "reliability"
        description = "intel_avb integration has >90% test coverage"
    },
    @{
        id = "REQ-NF-REL-TS-001"
        title = "Timestamp Loss (<0.01% under load)"
        priority = "high"
        traces_to = 28
        type = "non-functional"
        category = "reliability"
        description = "Timestamp loss rate <0.01% under load"
    },
    
    # Non-Functional Requirements (Standards/Compliance)
    @{
        id = "REQ-NF-STD-NAMING-001"
        title = "IEEE Naming (100% compliance)"
        priority = "high"
        traces_to = 30
        type = "non-functional"
        category = "standards"
        description = "100% of PTP/TSN terms match IEEE definitions"
    },
    @{
        id = "REQ-NF-STD-MILAN-001"
        title = "AVnu Milan Compliance"
        priority = "medium"
        traces_to = 33
        type = "non-functional"
        category = "standards"
        description = "Passes AVnu Milan certification tests (long-term goal)"
    },
    
    # Non-Functional Requirements (Maintainability)
    @{
        id = "REQ-NF-MAINT-LIB-001"
        title = "Single Source of Truth (intel_avb)"
        priority = "high"
        traces_to = 29
        type = "non-functional"
        category = "maintainability"
        description = "Register layouts defined only in intel_avb library"
    },
    @{
        id = "REQ-NF-MAINT-API-001"
        title = "API Versioning Strategy"
        priority = "medium"
        traces_to = 32
        type = "non-functional"
        category = "maintainability"
        description = "Breaking API changes detected at compile time"
    },
    
    # Non-Functional Requirements (Interoperability)
    @{
        id = "REQ-NF-INTEROP-MILAN-001"
        title = "Milan Endpoint Compatibility (3+ vendors)"
        priority = "high"
        traces_to = 33
        type = "non-functional"
        category = "interoperability"
        description = "Works with 3+ Milan endpoint vendors"
    },
    @{
        id = "REQ-NF-INTEROP-TSN-001"
        title = "TSN Switch Compatibility (2+ vendors)"
        priority = "high"
        traces_to = 33
        type = "non-functional"
        category = "interoperability"
        description = "Works with 2+ TSN switch vendors"
    },
    @{
        id = "REQ-NF-INTEROP-STANDARDS-001"
        title = "Standards-Based Interoperability"
        priority = "high"
        traces_to = 30
        type = "non-functional"
        category = "interoperability"
        description = "Interoperates via IEEE standards compliance"
    },
    
    # Non-Functional Requirements (Compatibility)
    @{
        id = "REQ-NF-COMPAT-NDIS-001"
        title = "Windows Compatibility (10 1809+)"
        priority = "critical"
        traces_to = 31
        type = "non-functional"
        category = "compatibility"
        description = "Supports Windows 10 1809+ and Intel miniports 2019+"
    }
)

Write-Host "Total requirements to create: $($requirements.Count)" -ForegroundColor Cyan
Write-Host ""

# Note: This script structure is ready for GitHub CLI (gh issue create)
# To use, install GitHub CLI and run:
#   gh auth login
#   foreach ($req in $requirements) { 
#       gh issue create --repo zarfld/IntelAvbFilter --title "$($req.id): $($req.title)" --body "..." --label "..." 
#   }

# Export to JSON for processing
$requirements | ConvertTo-Json -Depth 10 | Out-File "d:\Repos\IntelAvbFilter\02-requirements\requirements-batch.json" -Encoding UTF8

Write-Host "Requirements exported to: 02-requirements/requirements-batch.json" -ForegroundColor Green
Write-Host ""
Write-Host "Summary by Category:" -ForegroundColor Cyan
Write-Host "  Functional (REQ-F): $($requirements.Where({$_.type -eq 'functional'}).Count)" -ForegroundColor Yellow
Write-Host "  Non-Functional (REQ-NF): $($requirements.Where({$_.type -eq 'non-functional'}).Count)" -ForegroundColor Yellow
Write-Host ""
Write-Host "Non-Functional by Category:" -ForegroundColor Cyan
$nfr = $requirements.Where({$_.type -eq 'non-functional'})
$nfr | Group-Object category | ForEach-Object {
    Write-Host "  $($_.Name): $($_.Count)" -ForegroundColor Magenta
}
