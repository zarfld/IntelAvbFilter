#!/usr/bin/env python3
"""Proper analysis: Which requirements lack ADR references."""

import json
import sys

with open('build/build/traceability.json', 'r', encoding='utf-8') as f:
    data = json.load(f)

# Get all ADR issue numbers
adrs = {item['id'] for item in data['items'] if item['type'] == 'ADR'}
print(f"Found {len(adrs)} ADRs:")
for adr_id in sorted(adrs, key=lambda x: int(x.replace('#', ''))):
    adr = next(item for item in data['items'] if item['id'] == adr_id)
    print(f"  {adr_id}: {adr['title']}")

print(f"\n{'='*100}\n")

# Analyze requirements
reqs = [item for item in data['items'] if item['type'] in ['REQ-F', 'REQ-NF']]

requirements_with_adr = []
requirements_without_adr = []

for req in reqs:
    # Check if any references point to ADRs
    refs = req.get('references', [])
    adr_refs = [ref for ref in refs if ref in adrs]
    
    if adr_refs:
        requirements_with_adr.append((req, adr_refs))
    else:
        requirements_without_adr.append(req)

print(f"SUMMARY:")
print(f"  Total requirements: {len(reqs)}")
print(f"  WITH ADR references: {len(requirements_with_adr)} ({100*len(requirements_with_adr)/len(reqs):.1f}%)")
print(f"  WITHOUT ADR references: {len(requirements_without_adr)} ({100*len(requirements_without_adr)/len(reqs):.1f}%)")

print(f"\n{'='*100}")
print(f"REQUIREMENTS WITHOUT ADR REFERENCES ({len(requirements_without_adr)} total):")
print(f"{'='*100}\n")

# Group by category
functional = [r for r in requirements_without_adr if r['type'] == 'REQ-F']
non_functional = [r for r in requirements_without_adr if r['type'] == 'REQ-NF']

print(f"\nFUNCTIONAL ({len(functional)}):")
for req in functional:
    title_short = req['title'][:80] + "..." if len(req['title']) > 80 else req['title']
    print(f"  {req['id']}: {title_short}")

print(f"\nNON-FUNCTIONAL ({len(non_functional)}):")
for req in non_functional:
    title_short = req['title'][:80] + "..." if len(req['title']) > 80 else req['title']
    print(f"  {req['id']}: {title_short}")

print(f"\n{'='*100}")
print(f"REQUIREMENTS WITH ADR REFERENCES ({len(requirements_with_adr)} total):")
print(f"{'='*100}\n")

for req, adr_refs in requirements_with_adr:
    title_short = req['title'][:70] + "..." if len(req['title']) > 70 else req['title']
    print(f"{req['id']}: {title_short}")
    print(f"  -> ADRs: {', '.join(adr_refs)}")
