#!/usr/bin/env python3
"""Convert GitHub Issues to traceability.json format

Fetches requirements from GitHub Issues and generates the same traceability.json
format that build_trace_json.py produces from markdown specs.

This ensures compatibility with validate-trace-coverage.py and other tools
that expect the build/traceability.json format.

Output format:
{
    "metrics": {
        "requirement": {"coverage_pct": 82.0, "total": 50, "linked": 41},
        "requirement_to_ADR": {"coverage_pct": 75.0},
        "requirement_to_scenario": {"coverage_pct": 60.0},
        "requirement_to_test": {"coverage_pct": 40.0}
    },
    "items": [...],
    "forward_links": {...},
    "backward_links": {...}
}
"""
import os
import sys
import json
import re
from pathlib import Path
from collections import defaultdict
from github import Github

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build' / 'traceability.json'

def extract_issue_links(body: str) -> dict:
    """Extract ALL issue references from body text.
    
    Trusts GitHub's infrastructure: any #N reference is a link.
    Ignores markdown patterns - they're unreliable across issues.
    
    Returns all found references in 'traces_to' for generic linkage.
    """
    if not body:
        return {'traces_to': []}
    
    # Simple approach: Extract ALL #N references from body
    # Trust GitHub's infrastructure - any #N is a link
    all_refs = re.findall(r'#(\d+)', body)
    unique_refs = sorted(set(int(ref) for ref in all_refs))
    
    return {'traces_to': unique_refs}

def extract_issue_links_OLD_COMPLEX(body: str) -> dict:
    """OLD COMPLEX VERSION - keeping for reference but not used"""
    links = defaultdict(list)
    
    # Pattern 1: Bold inline format (with or without markdown **)
    # Matches: **Traces to**: #123 or Traces to: #123 or **Parent**: #1 (Description)
    patterns_OLD = {
        'traces_to': [
            r'\*\*(?:Traces?\s+to|Parent|Traces-to)\*\*:\s*#(\d+)',  # **Traces to**: #N
            r'(?:^|\n)(?:Traces?\s+to|Parent|Traces-to):\s*#(\d+)',  # Traces to: #N (no bold)
        ],
        'depends_on': [
            r'\*\*(?:Depends?\s+on|Depends-on)\*\*:\s*#(\d+)',
            r'(?:^|\n)(?:Depends?\s+on|Depends-on):\s*#(\d+)',
        ],
        'verified_by': [
            r'\*\*(?:Verified\s+by|Test|Verified-by|Verifies\s+Requirements?)\*\*:\s*#(\d+)',
            r'(?:^|\n)(?:Verified\s+by|Test|Verified-by|Verifies\s+Requirements?):\s*#(\d+)',
        ],
        'implemented_by': [
            r'\*\*(?:Implemented\s+by|Implements?|Implemented-by)\*\*:\s*#(\d+)',
            r'(?:^|\n)(?:Implemented\s+by|Implements?|Implemented-by):\s*#(\d+)',
        ],
    }
    
    # Pattern 2: Multi-word section labels with lists
    # Matches: **Implements Requirements**:\n- #2 (REQ-F-001)
    section_patterns = {
        'traces_to': r'\*\*(?:Traces?\s+to|Parent|Satisfies|Addresses)(?:\s+Requirements?)?\*\*:[^#]*?(?:^|\n)\s*-?\s*#(\d+)',
        'depends_on': r'\*\*(?:Depends?\s+on|Dependencies|Required)\*\*:[^#]*?(?:^|\n)\s*-?\s*#(\d+)',
        'verified_by': r'\*\*(?:Verified\s+by|Test|Validates?|Verifies)(?:\s+Requirements?)?\*\*:[^#]*?(?:^|\n)\s*-?\s*#(\d+)',
        'implemented_by': r'\*\*(?:Implemented\s+by|Implements?)(?:\s+Requirements?)?\*\*:[^#]*?(?:^|\n)\s*-?\s*#(\d+)',
    }
    
    # Additional patterns for architecture issues - find sections then extract all #N
    architecture_section_labels = {
        'traces_to': [
            'Addresses Requirements?',
            'Satisfies Requirements?',
            'Requirements? Satisfied',
        ],
        'implemented_by': [
            'Components? Affected',
            'Architecture Decisions?',
        ],
        'verified_by': [
            'Quality Scenarios?',
            'Requirements? Verified',
        ],
    }
    
    # Extract all patterns
    for link_type, pattern_list in patterns.items():
        for pattern in pattern_list:
            matches = re.findall(pattern, body, re.IGNORECASE | re.MULTILINE)
            links[link_type].extend(int(m) for m in matches)
    
    for link_type, pattern in section_patterns.items():
        matches = re.findall(pattern, body, re.IGNORECASE | re.MULTILINE | re.DOTALL)
        links[link_type].extend(int(m) for m in matches)
    
    # For architecture patterns, find the labeled section and extract ALL #N references
    for link_type, label_list in architecture_section_labels.items():
        for label in label_list:
            # Find sections with this label, extract up to next bold label or end
            section_pattern = rf'\*\*(?:{label})\*\*:(.*?)(?=\*\*|\n##|\Z)'
            sections = re.findall(section_pattern, body, re.IGNORECASE | re.MULTILINE | re.DOTALL)
            for section in sections:
                # Extract all #N references from this section
                all_refs = re.findall(r'#(\d+)', section)
                links[link_type].extend(int(ref) for ref in all_refs)
    
    # Generic pattern: find all issue references in traceability sections
    # Look for various traceability section headers and extract all #N references
    
    # For ## headers (level 2), match until next ## header (not ###)
    level2_headers = [
        r'##\s+(?:Traceability|Traces\s+To)',           # ## Traceability
        r'##\s+(?:Requirements?\s+Satisfied)',          # ## Requirements Satisfied
    ]
    
    for header_pattern in level2_headers:
        sections = re.findall(
            rf'{header_pattern}.*?(?=\n##[^#]|$)',
            body,
            re.IGNORECASE | re.MULTILINE | re.DOTALL
        )
        
        if sections:
            print(f"Debug extract_issue_links: Found {len(sections)} level2 sections matching {header_pattern[:30]}...", file=sys.stderr)
            for section in sections[:1]:  # Show first section preview
                preview = section[:500].replace('\n', '\\n')
                print(f"  Section (500 chars): {preview}", file=sys.stderr)
                refs_found = re.findall(r'#(\d+)', section)
                print(f"  Issue refs in section: {refs_found[:20]}", file=sys.stderr)
        
        for section in sections:
            # Extract all #N references from the section (including subsections)
            all_refs = re.findall(r'#(\d+)', section)
            # Add to traces_to if not already captured
            for ref in all_refs:
                ref_int = int(ref)
                if ref_int not in links['traces_to']:
                    links['traces_to'].append(ref_int)
    
    # For ### headers (level 3), match until next ### or ## header
    level3_headers = [
        r'###\s+(?:Functional\s+Requirements?)',        # ### Functional Requirements
        r'###\s+(?:Non-Functional\s+Requirements?)',    # ### Non-Functional Requirements
        r'###\s+(?:Stakeholder\s+Needs?)',              # ### Stakeholder Need
    ]
    
    for header_pattern in level3_headers:
        sections = re.findall(
            rf'{header_pattern}.*?(?=\n###|\n##|$)',
            body,
            re.IGNORECASE | re.MULTILINE | re.DOTALL
        )
        
        for section in sections:
            # Extract all #N references from the section
            all_refs = re.findall(r'#(\d+)', section)
            # Add to traces_to if not already captured
            for ref in all_refs:
                ref_int = int(ref)
                if ref_int not in links['traces_to']:
                    links['traces_to'].append(ref_int)
    
    # Remove duplicates while preserving order
    for key in links:
        links[key] = list(dict.fromkeys(links[key]))
    
    return dict(links)

def get_requirement_type(title: str, labels: list) -> str:
    """Determine requirement type from title and labels.
    
    Prioritizes title prefix, then checks labels (including colon-separated variants).
    """
    # Extract from title prefix (most reliable)
    match = re.match(r'^(StR|REQ-F|REQ-NF|ADR|ARC-C|QA-SC|TEST)', title, re.IGNORECASE)
    if match:
        return match.group(1).upper()
    
    # Fallback to labels (handle both hyphen and colon separators)
    label_map = {
        # Colon-separated (current project standard)
        'type:stakeholder-requirement': 'StR',
        'type:requirement:functional': 'REQ-F',
        'type:requirement:non-functional': 'REQ-NF',
        'type:architecture:decision': 'ADR',
        'type:architecture:component': 'ARC-C',
        'type:architecture:quality-scenario': 'QA-SC',
        'type:test-case': 'TEST',
        'type:test-plan': 'TEST',
        
        # Hyphen-separated (legacy/alternative)
        'stakeholder-requirement': 'StR',
        'functional-requirement': 'REQ-F',
        'non-functional': 'REQ-NF',
        'architecture-decision': 'ADR',
        'architecture-component': 'ARC-C',
        'quality-scenario': 'QA-SC',
        'test-case': 'TEST',
        'test-plan': 'TEST',
    }
    
    for label in labels:
        # Check exact match first
        if label in label_map:
            return label_map[label]
        
        # Check if label contains any key as substring (partial match)
        label_lower = label.lower()
        if 'stakeholder' in label_lower:
            return 'StR'
        elif 'functional' in label_lower and 'non' not in label_lower:
            return 'REQ-F'
        elif 'non-functional' in label_lower:
            return 'REQ-NF'
        elif 'decision' in label_lower:
            return 'ADR'
        elif 'component' in label_lower:
            return 'ARC-C'
        elif 'quality' in label_lower or 'scenario' in label_lower:
            return 'QA-SC'
        elif 'test' in label_lower:
            return 'TEST'
    
    return 'UNKNOWN'

def main() -> int:
    token = os.environ.get('GITHUB_TOKEN')
    if not token:
        print('ERROR: GITHUB_TOKEN environment variable required', file=sys.stderr)
        return 1
    
    repo_name = os.environ.get('GITHUB_REPOSITORY', 'zarfld/copilot-instructions-template')
    
    try:
        g = Github(token)
        repo = g.get_repo(repo_name)
    except Exception as e:
        print(f'ERROR: Failed to connect to GitHub: {e}', file=sys.stderr)
        return 1
    
    print(f"Fetching issues from {repo_name}...")
    
    # Fetch all requirement issues
    requirement_labels = [
        # Primary labels (colon-separated)
        'type:stakeholder-requirement',
        'type:requirement:functional',
        'type:requirement:non-functional',
        'type:architecture:decision',
        'type:architecture:component',
        'type:architecture:quality-scenario',
        'type:test-case',
        'type:test-plan',
        
        # Phase labels (to catch issues tagged by phase)
        'phase:01-stakeholder-requirements',
        'phase:02-requirements',
        'phase:03-architecture',
        'phase:07-verification-validation',
    ]
    
    all_issues = []
    seen_numbers = set()  # Avoid duplicates
    
    for label in requirement_labels:
        try:
            issues = list(repo.get_issues(labels=[label], state='all'))
            for issue in issues:
                if issue.number not in seen_numbers:
                    all_issues.append(issue)
                    seen_numbers.add(issue.number)
        except Exception as e:
            print(f"Warning: Could not fetch label {label}: {e}", file=sys.stderr)
    
    # If no labeled issues found, try fetching all open issues and filter by title prefix
    if not all_issues:
        print("Warning: No issues found with requirement labels, trying title-based detection...", file=sys.stderr)
        try:
            all_open_issues = list(repo.get_issues(state='all'))
            for issue in all_open_issues:
                if re.match(r'^(StR|REQ-F|REQ-NF|ADR|ARC-C|QA-SC|TEST)', issue.title, re.IGNORECASE):
                    if issue.number not in seen_numbers:
                        all_issues.append(issue)
                        seen_numbers.add(issue.number)
        except Exception as e:
            print(f"Warning: Could not fetch all issues: {e}", file=sys.stderr)
    
    print(f"Found {len(all_issues)} requirement issues")
    
    # Build traceability structure
    items = []
    forward_links = {}
    backward_links = defaultdict(list)
    
    # Track requirements by type for metrics
    requirements = []  # REQ-F, REQ-NF
    requirements_with_adr = set()
    requirements_with_scenario = set()
    requirements_with_test = set()
    requirements_with_any_link = set()
    
    # First pass: collect all issue types
    issue_types = {}  # issue_id -> type
    for issue in all_issues:
        issue_id = f"#{issue.number}"
        labels = [l.name for l in issue.labels]
        req_type = get_requirement_type(issue.title, labels)
        issue_types[issue_id] = req_type
    
    for issue in all_issues:
        issue_id = f"#{issue.number}"
        labels = [l.name for l in issue.labels]
        req_type = get_requirement_type(issue.title, labels)
        
        links = extract_issue_links(issue.body or "")
        
        # Build item entry
        item = {
            'id': issue_id,
            'type': req_type,
            'title': issue.title,
            'state': issue.state,
            'url': issue.html_url,
            'labels': labels,  # Include labels for debugging
            'references': [],
            'link_details': {}  # Categorized links for debugging
        }
        
        # Collect all referenced issues
        all_refs = set()
        for link_type, link_list in links.items():
            all_refs.update(f"#{n}" for n in link_list)
            if link_list:
                item['link_details'][link_type] = [f"#{n}" for n in link_list]
        
        item['references'] = sorted(all_refs, key=lambda x: int(x[1:]))  # Sort numerically
        
        items.append(item)
        forward_links[issue_id] = item['references']
        
        # Build backward links
        for ref in item['references']:
            backward_links[ref].append(issue_id)
        
        # Track metrics for requirements
        if req_type in ['REQ-F', 'REQ-NF']:
            requirements.append(issue_id)
            
            if item['references']:
                requirements_with_any_link.add(issue_id)
            
            # Forward linkage: Check what this requirement links to
            all_linked_issues = set()
            for link_list in links.values():
                all_linked_issues.update(link_list)
            
            for ref_num in all_linked_issues:
                ref_id = f"#{ref_num}"
                ref_type = issue_types.get(ref_id, 'UNKNOWN')
                
                # Track linkage to ADRs
                if ref_type == 'ADR' or ref_type == 'ARC-C':
                    requirements_with_adr.add(issue_id)
                # Track linkage to Quality Scenarios
                elif ref_type == 'QA-SC':
                    requirements_with_scenario.add(issue_id)
                # Track linkage to Tests
                elif ref_type == 'TEST':
                    requirements_with_test.add(issue_id)
        
        # Backward linkage: If this is an ADR/ARC-C/QA-SC/TEST linking to requirements
        elif req_type in ['ADR', 'ARC-C', 'QA-SC', 'TEST']:
            # Check what this artifact links to
            all_linked_issues = set()
            for link_list in links.values():
                all_linked_issues.update(link_list)
            
            for ref_num in all_linked_issues:
                ref_id = f"#{ref_num}"
                ref_type = issue_types.get(ref_id, 'UNKNOWN')
                
                # If linking to a requirement, count reverse linkage
                if ref_type in ['REQ-F', 'REQ-NF']:
                    if req_type in ['ADR', 'ARC-C']:
                        requirements_with_adr.add(ref_id)
                    elif req_type == 'QA-SC':
                        requirements_with_scenario.add(ref_id)
                    elif req_type == 'TEST':
                        requirements_with_test.add(ref_id)
    
    # Calculate metrics
    total_reqs = len(requirements)
    metrics = {
        'requirement': {
            'coverage_pct': (len(requirements_with_any_link) / total_reqs * 100) if total_reqs else 0,
            'total': total_reqs,
            'linked': len(requirements_with_any_link)
        }
    }
    
    if total_reqs > 0:
        metrics['requirement_to_ADR'] = {
            'coverage_pct': len(requirements_with_adr) / total_reqs * 100,
            'total': total_reqs,
            'linked': len(requirements_with_adr)
        }
        metrics['requirement_to_scenario'] = {
            'coverage_pct': len(requirements_with_scenario) / total_reqs * 100,
            'total': total_reqs,
            'linked': len(requirements_with_scenario)
        }
        metrics['requirement_to_test'] = {
            'coverage_pct': len(requirements_with_test) / total_reqs * 100,
            'total': total_reqs,
            'linked': len(requirements_with_test)
        }
    
    # Build output
    output = {
        'source': 'github-issues',
        'repository': repo_name,
        'generated_at': __import__('datetime').datetime.utcnow().isoformat(),
        'metrics': metrics,
        'items': items,
        'forward_links': forward_links,
        'backward_links': {k: list(v) for k, v in backward_links.items()}
    }
    
    # Write output
    OUT.parent.mkdir(exist_ok=True)
    OUT.write_text(json.dumps(output, indent=2), encoding='utf-8')
    
    print(f"\n✅ Generated {OUT}")
    print(f"   Total items: {len(items)}")
    print(f"   Requirements: {total_reqs}")
    if total_reqs > 0:
        print(f"   Overall coverage: {metrics['requirement']['coverage_pct']:.1f}%")
        print(f"   ADR linkage: {metrics.get('requirement_to_ADR', {}).get('coverage_pct', 0):.1f}%")
        print(f"   Scenario linkage: {metrics.get('requirement_to_scenario', {}).get('coverage_pct', 0):.1f}%")
        print(f"   Test linkage: {metrics.get('requirement_to_test', {}).get('coverage_pct', 0):.1f}%")
    
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
