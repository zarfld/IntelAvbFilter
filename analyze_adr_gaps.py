#!/usr/bin/env python3
"""Analyze which requirements lack ADR links."""

import json

with open('build/build/traceability.json', 'r', encoding='utf-8') as f:
    data = json.load(f)

# Find all requirements (REQ-F and REQ-NF)
reqs = [item for item in data['items'] if item['type'] in ['REQ-F', 'REQ-NF']]

# Separate linked and unlinked
unlinked = []
linked = []

for r in reqs:
    adr_links = r.get('link_details', {}).get('ADR', [])
    if not adr_links:
        unlinked.append(r)
    else:
        linked.append(r)

print(f"Total requirements: {len(reqs)}")
print(f"Requirements WITH ADR links: {len(linked)}")
print(f"Requirements WITHOUT ADR links: {len(unlinked)}")
print(f"\n{'='*80}")
print(f"UNLINKED REQUIREMENTS ({len(unlinked)} total):")
print(f"{'='*80}\n")

# Group by phase/domain for easier analysis
functional = [r for r in unlinked if 'type:requirement:functional' in r.get('labels', [])]
non_functional = [r for r in unlinked if 'type:requirement:non-functional' in r.get('labels', [])]

print(f"\nFUNCTIONAL REQUIREMENTS ({len(functional)}):")
for r in functional:
    print(f"  {r['id']}: {r['title']}")

print(f"\nNON-FUNCTIONAL REQUIREMENTS ({len(non_functional)}):")
for r in non_functional:
    print(f"  {r['id']}: {r['title']}")

# Show what ADRs exist
print(f"\n{'='*80}")
print(f"EXISTING ADRs:")
print(f"{'='*80}\n")
adrs = [item for item in data['items'] if item['type'] == 'ADR']
for adr in adrs:
    adr_links = adr.get('link_details', {}).get('REQ', [])
    print(f"{adr['id']}: {adr['title']}")
    print(f"  Links to {len(adr_links)} requirements: {', '.join(adr_links) if adr_links else 'NONE'}")
