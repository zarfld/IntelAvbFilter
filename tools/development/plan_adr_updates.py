#!/usr/bin/env python3
"""Batch update requirements with ADR links.

This script adds "Relates to:" links for architectural decisions
to requirements that are missing them.
"""

import json
import sys

# Define the mappings from analysis
adr_mappings = {
    # Device Abstraction Requirements → ADR-ARCH-003, ADR-HAL-001
    '#40': ['#92', '#126'],  # REQ-F-DEVICE-ABS-003
    '#52': ['#92'],           # REQ-F-DEVICE-ABS-005
    '#55': ['#92', '#126'],  # REQ-F-DEVICE-ABS-001 (DONE)
    '#56': ['#92', '#126'],  # REQ-F-DEVICE-ABS-002
    
    # Event/Ring Buffer
    '#19': ['#93', '#147'],  # REQ-F-TSRING-001 → Ring Buffer ADR
    
    # Multi-NIC
    '#76': ['#134'],  # REQ-F-MULTIPLE-NIC-001
    '#75': ['#134'],  # REQ-F-HOT-PLUG-001
    
    # Error Handling
    '#68': ['#131'],  # REQ-F-EVENT-LOG-001
    '#59': ['#131'],  # REQ-NF-REL-DRIVER-001
    '#53': ['#131'],  # REQ-F-INTEL-AVB-004
    '#80': ['#131'],  # REQ-F-GRACEFUL-DEGRADE-001
    
    # Security/IOCTL
    '#25': ['#118', '#122'],  # REQ-F-PTP-IOCTL-001
    '#70': ['#122'],           # REQ-F-IOCTL-PHC-002
    '#72': ['#122'],           # REQ-NF-SEC-IOCTL-001
    '#74': ['#118'],           # REQ-F-IOCTL-BUFFER-001
    
    # Performance
    '#46': ['#90', '#91'],  # REQ-NF-PERF-NDIS-001
    '#71': ['#118', '#91'], # REQ-NF-PERF-IOCTL-001
    
    # gPTP/TSN
    '#119': ['#117'],  # REQ-F-GPTP-COMPAT-001
    '#120': ['#133'],  # REQ-F-FPE-001
    '#63': ['#133'],   # REQ-NF-INTEROP-TSN-001
    '#54': ['#133'],   # REQ-F-TSN-SEMANTICS-001
    
    # Hardware/Firmware
    '#44': ['#126'],  # REQ-F-HW-DETECT-001
    '#77': ['#126'],  # REQ-F-FIRMWARE-DETECT-001
    '#79': ['#126'],  # REQ-F-HARDWARE-QUIRKS-001
    
    # Windows Compatibility
    '#69': ['#90'],  # REQ-NF-COMPAT-WIN10-001
    
    # Documentation
    '#86': ['#135'],  # REQ-NF-DOC-DEPLOY-001
}

# Load ADR titles for reference
with open('build/build/traceability.json', 'r', encoding='utf-8') as f:
    data = json.load(f)

adrs = {item['id']: item['title'] for item in data['items'] if item['type'] in ['ADR', 'ARC-C']}
reqs = {item['id']: item['title'] for item in data['items'] if item['type'] in ['REQ-F', 'REQ-NF']}

print("ADR Linkage Update Plan")
print("=" * 80)
print(f"\nTotal requirements to update: {len(adr_mappings)}")
print(f"Total ADR links to add: {sum(len(v) for v in adr_mappings.values())}")

print("\n" + "=" * 80)
print("UPDATE PLAN:")
print("=" * 80 + "\n")

for req_id in sorted(adr_mappings.keys(), key=lambda x: int(x.replace('#', ''))):
    adr_ids = adr_mappings[req_id]
    print(f"{req_id}: {reqs.get(req_id, 'UNKNOWN')}")
    for adr_id in adr_ids:
        print(f"  + Add link to {adr_id}: {adrs.get(adr_id, 'UNKNOWN')}")
    print()

print("=" * 80)
print("NEW ADRs NEEDED (Not yet created):")
print("=" * 80 + "\n")

needed_adrs = [
    "ADR-RECOVERY-001: Hardware Fault Recovery Strategy (for #81)",
    "ADR-IOCTL-VERSION-001: IOCTL API Versioning Strategy (for #64)",
    "ADR-INTEROP-001: Milan Interoperability Strategy (for #61)",
    "ADR-NAMING-001: Naming Convention Standards (for #51, #62)",
    "ADR-MAINT-001: Code Quality/Maintainability Standards (for #85)",
    "ADR-POWER-001: Power Management Strategy (for #66)",
    "ADR-TELEMETRY-001: Telemetry/Statistics Architecture (for #67, #83)",
    "ADR-CONFIG-001: Configuration Management Strategy (for #78)",
]

for adr in needed_adrs:
    print(f"  - {adr}")

print(f"\n{'='*80}")
print("COVERAGE IMPACT ESTIMATE:")
print(f"{'='*80}\n")

current_linked = 56  # From CI
to_update = len(adr_mappings)  # 26 requirements
new_linked = current_linked + to_update
total_reqs = 92
new_coverage = (new_linked / total_reqs) * 100

print(f"Current ADR linkage: {current_linked}/{total_reqs} = 60.9%")
print(f"After adding links:  {new_linked}/{total_reqs} = {new_coverage:.1f}%")
print(f"Target threshold:   70.0%")
print(f"Gap after updates:  {70.0 - new_coverage:.1f}pp {'✅ THRESHOLD MET' if new_coverage >= 70.0 else '❌ Still short'}")
