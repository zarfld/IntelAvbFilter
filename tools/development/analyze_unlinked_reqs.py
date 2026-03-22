#!/usr/bin/env python3
"""Identify the 36 requirements without ANY ADR linkage (forward or backward)."""

import json

with open('build/build/traceability.json', 'r', encoding='utf-8') as f:
    data = json.load(f)

# Get all ADR/ARC-C issue IDs
adr_ids = {item['id'] for item in data['items'] if item['type'] in ['ADR', 'ARC-C']}

# Get all requirements
reqs = {item['id']: item for item in data['items'] if item['type'] in ['REQ-F', 'REQ-NF']}

# Find requirements with forward links to ADRs
reqs_with_forward_links = set()
for req_id, req in reqs.items():
    refs = req.get('references', [])
    if any(ref in adr_ids for ref in refs):
        reqs_with_forward_links.add(req_id)

# Find requirements with backward links FROM ADRs
reqs_with_backward_links = set()
adrs = [item for item in data['items'] if item['type'] in ['ADR', 'ARC-C']]
for adr in adrs:
    refs = adr.get('references', [])
    for ref in refs:
        if ref in reqs:
            reqs_with_backward_links.add(ref)

# Combined: requirements with ANY linkage
reqs_with_any_adr_link = reqs_with_forward_links | reqs_with_backward_links

# The 36 unlinked requirements
unlinked_reqs = {req_id: reqs[req_id] for req_id in reqs if req_id not in reqs_with_any_adr_link}

print(f"Total requirements: {len(reqs)}")
print(f"With forward links to ADRs: {len(reqs_with_forward_links)}")
print(f"With backward links from ADRs: {len(reqs_with_backward_links)}")
print(f"With ANY ADR linkage: {len(reqs_with_any_adr_link)}")
print(f"WITHOUT any ADR linkage: {len(unlinked_reqs)}")

print(f"\n{'='*100}")
print(f"UNLINKED REQUIREMENTS ({len(unlinked_reqs)} total):")
print(f"{'='*100}\n")

# Group by domain/category for easier analysis
event_reqs = []
ptp_reqs = []
ioctl_reqs = []
device_reqs = []
ndis_reqs = []
tsn_reqs = []
nonfunc_reqs = []
other_reqs = []

for req_id, req in sorted(unlinked_reqs.items(), key=lambda x: int(x[0].replace('#', ''))):
    title = req['title']
    labels = req.get('labels', [])
    
    if req['type'] == 'REQ-NF':
        nonfunc_reqs.append((req_id, title, labels))
    elif 'EVENT' in title.upper() or 'event' in ' '.join(labels):
        event_reqs.append((req_id, title, labels))
    elif 'PTP' in title.upper() or 'PHC' in title.upper() or 'TIMESTAMP' in title.upper():
        ptp_reqs.append((req_id, title, labels))
    elif 'IOCTL' in title.upper():
        ioctl_reqs.append((req_id, title, labels))
    elif 'DEVICE' in title.upper() or 'HARDWARE' in title.upper() or 'NIC' in title.upper():
        device_reqs.append((req_id, title, labels))
    elif 'NDIS' in title.upper() or 'FILTER' in title.upper():
        ndis_reqs.append((req_id, title, labels))
    elif any(x in title.upper() for x in ['TAS', 'CBS', 'QAV', 'QBV', 'FPE', 'VLAN']):
        tsn_reqs.append((req_id, title, labels))
    else:
        other_reqs.append((req_id, title, labels))

def print_group(name, items):
    if items:
        print(f"\n{name} ({len(items)}):")
        for req_id, title, labels in items:
            print(f"  {req_id}: {title}")

print_group("EVENT-RELATED", event_reqs)
print_group("PTP/TIMESTAMPING", ptp_reqs)
print_group("IOCTL-RELATED", ioctl_reqs)
print_group("DEVICE/HARDWARE", device_reqs)
print_group("NDIS/FILTER", ndis_reqs)
print_group("TSN FEATURES", tsn_reqs)
print_group("NON-FUNCTIONAL", nonfunc_reqs)
print_group("OTHER FUNCTIONAL", other_reqs)

print(f"\n{'='*100}")
print(f"AVAILABLE ADRs/ARC-Cs TO LINK TO:")
print(f"{'='*100}\n")

for adr in sorted(adrs, key=lambda x: int(x['id'].replace('#', ''))):
    req_refs = [ref for ref in adr.get('references', []) if ref in reqs]
    print(f"{adr['id']}: {adr['title']}")
    if req_refs:
        print(f"  Currently links to {len(req_refs)} requirements: {', '.join(sorted(req_refs, key=lambda x: int(x.replace('#', '')))[:5])}{'...' if len(req_refs) > 5 else ''}")
    else:
        print(f"  Links to 0 requirements")
