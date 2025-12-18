#!/usr/bin/env python3
"""Generate traceability matrix from GitHub Issues.

This script fetches all requirement issues from the GitHub repository and
generates a comprehensive traceability matrix showing parent-child relationships.

Standards: ISO/IEC/IEEE 29148:2018 (Requirements Traceability)

Usage:
    export GITHUB_TOKEN=ghp_xxx
    python scripts/github-traceability-report.py > reports/traceability.md

Output: Markdown traceability matrix with:
    - Issue number and title
    - Requirement type (StR, REQ-F, REQ-NF, ADR, etc.)
    - Upward traceability (Traces to)
    - Downward traceability (Verified by, Implemented by)
    - Dependency links
"""
import os
import re
import sys
import requests
from typing import Dict, List, Optional
from collections import defaultdict

GITHUB_TOKEN = os.environ.get('GITHUB_TOKEN')
# Auto-detect repository from GITHUB_REPOSITORY env var (set by GitHub Actions)
# Format: "owner/repo" or fallback to manual REPO_OWNER/REPO_NAME
GITHUB_REPOSITORY = os.environ.get('GITHUB_REPOSITORY', '')
if GITHUB_REPOSITORY and '/' in GITHUB_REPOSITORY:
    REPO_OWNER, REPO_NAME = GITHUB_REPOSITORY.split('/', 1)
else:
    REPO_OWNER = os.environ.get('REPO_OWNER', 'zarfld')
    REPO_NAME = os.environ.get('REPO_NAME', 'copilot-instructions-template')

API_BASE = f'https://api.github.com/repos/{REPO_OWNER}/{REPO_NAME}'

# Label categories for filtering
# This project uses COLON-SEPARATED label schemes (verified via GitHub API):
# 1. Type labels: type:stakeholder-requirement, type:requirement:functional, etc.
# 2. Phase labels: phase:01-stakeholder-requirements, phase:02-requirements, phase:03-architecture, etc.
REQUIREMENT_LABELS = [
    # Type labels (colon-separated, verified in repository):
    'type:stakeholder-requirement',
    'type:requirement:functional',
    'type:requirement:non-functional',
    'type:architecture:decision',
    'type:architecture:component',
    'type:architecture:quality-scenario',
    'type:test-plan',
    'type:test-case',
    # Phase labels (colon-separated after "phase", verified in repository):
    'phase:01-stakeholder-requirements',
    'phase:02-requirements',
    'phase:03-architecture',
    'phase:04-design',
    'phase:05-implementation',
    'phase:06-integration',
    'phase:07-verification-validation',
    'phase:08-transition',
    'phase:09-operation-maintenance',
]

def get_headers() -> Dict[str, str]:
    """Get API request headers with authentication."""
    headers = {
        'Accept': 'application/vnd.github+json',
        'X-GitHub-Api-Version': '2022-11-28'
    }
    if GITHUB_TOKEN:
        headers['Authorization'] = f'Bearer {GITHUB_TOKEN}'
    return headers

def fetch_all_requirements() -> List[Dict]:
    """Fetch all issues with requirement labels.
    
    Returns:
        List of issue dictionaries with number, title, labels, body, state.
    """
    issues = []
    page = 1
    
    while True:
        # Fetch all issues (open and closed) with pagination
        response = requests.get(
            f'{API_BASE}/issues',
            headers=get_headers(),
            params={
                'state': 'all',
                'per_page': 100,
                'page': page
            }
        )
        
        if response.status_code != 200:
            print(f"Error fetching issues: {response.status_code}", file=sys.stderr)
            print(response.text, file=sys.stderr)
            sys.exit(1)
        
        page_issues = response.json()
        
        if not page_issues:
            break
        
        # Filter for requirement labels
        for issue in page_issues:
            labels = [label['name'] for label in issue['labels']]
            if any(label in REQUIREMENT_LABELS for label in labels):
                issues.append(issue)
        
        page += 1
    
    return issues

def extract_links(issue_body: str) -> Dict[str, List[int]]:
    """Parse issue body for traceability links.
    
    Extracts patterns like:
        - Traces to: #123
        - Depends on: #45, #67
        - Verified by: #89
        - Implemented by: #PR-15
        - Refined by: #234, #235
    
    Args:
        issue_body: Issue body markdown text
    
    Returns:
        Dictionary with link types as keys and issue numbers as values
    """
    if not issue_body:
        return {
            'traces_to': [],
            'depends_on': [],
            'verified_by': [],
            'implemented_by': [],
            'refined_by': []
        }
    
    # Extract different link types (handle Markdown formatting: - **Traces to**: #123)
    # Regex now matches: "Traces to: #123", "- **Traces to**: #123", "**Traces to**: #123"
    traces_to = re.findall(r'(?:-\s+)?\*?\*?[Tt]races?\s+to\*?\*?:?\s*#(\d+)', issue_body)
    depends_on = re.findall(r'(?:-\s+)?\*?\*?[Dd]epends?\s+on\*?\*?:?\s*#(\d+)', issue_body)
    verified_by = re.findall(r'(?:-\s+)?\*?\*?[Vv]erified\s+by\*?\*?:?\s*#(\d+)', issue_body)
    implemented_by = re.findall(r'(?:-\s+)?\*?\*?[Ii]mplemented\s+by\*?\*?:?\s*#(\d+)', issue_body)
    refined_by = re.findall(r'(?:-\s+)?\*?\*?[Rr]efined\s+by\*?\*?:?\s*#(\d+)', issue_body)
    
    return {
        'traces_to': [int(n) for n in traces_to],
        'depends_on': [int(n) for n in depends_on],
        'verified_by': [int(n) for n in verified_by],
        'implemented_by': [int(n) for n in implemented_by],
        'refined_by': [int(n) for n in refined_by]
    }

def get_requirement_type(title: str, labels: List[str]) -> str:
    """Determine requirement type from issue title prefix and labels.
    
    This project uses title prefixes (StR-001, REQ-F-001, etc.) as the primary
    identifier for requirement types, as seen in the issue templates.
    
    Args:
        title: Issue title (e.g., "REQ-F-001: Audio Clap Detection")
        labels: List of label names
    
    Returns:
        Requirement type abbreviation (StR, REQ-F, etc.)
    """
    # Extract type from title prefix (primary method)
    import re
    match = re.match(r'^(StR|REQ-F|REQ-NF|ADR|ARC-C|QA-SC|TEST|TEST-PLAN|DES-[A-Z])', title)
    if match:
        prefix = match.group(1)
        # Normalize design prefixes
        if prefix.startswith('DES-'):
            return 'DESIGN'
        return prefix
    
    # Fallback: check labels for type information
    label_map = {
        # Type labels (colon-separated as they exist in GitHub)
        'type:stakeholder-requirement': 'StR',
        'type:requirement:functional': 'REQ-F',
        'type:requirement:non-functional': 'REQ-NF',
        'type:architecture:decision': 'ADR',
        'type:architecture:component': 'ARC-C',
        'type:architecture:quality-scenario': 'QA-SC',
        'type:test-case': 'TEST',
        'type:test-plan': 'TEST-PLAN',
        # Phase labels (colon after "phase" as they exist in GitHub)
        'phase:01-stakeholder-requirements': 'StR',
        'phase:02-requirements': 'REQ',
        'phase:03-architecture': 'ARCH',
        'phase:04-design': 'DESIGN',
        'phase:07-verification-validation': 'TEST',
    }
    
    for label in labels:
        if label in label_map:
            return label_map[label]
    
    return 'UNKNOWN'

def generate_matrix():
    """Generate traceability matrix report."""
    print("# Requirements Traceability Matrix\n")
    print(f"**Repository**: {REPO_OWNER}/{REPO_NAME}")
    print(f"**Generated**: {__import__('datetime').datetime.now().strftime('%Y-%m-%d %H:%M:%S UTC')}")
    print(f"**Standard**: ISO/IEC/IEEE 29148:2018\n")
    
    print("## Summary\n")
    issues = fetch_all_requirements()
    print(f"Total requirements: **{len(issues)}**\n")
    
    # Count by type
    type_counts = defaultdict(int)
    state_counts = defaultdict(int)
    for issue in issues:
        labels = [label['name'] for label in issue['labels']]
        req_type = get_requirement_type(issue['title'], labels)
        type_counts[req_type] += 1
        state_counts[issue['state']] += 1
    
    print("### By Type\n")
    for req_type, count in sorted(type_counts.items()):
        print(f"- **{req_type}**: {count}")
    
    print(f"\n### By State\n")
    print(f"- **Open**: {state_counts['open']}")
    print(f"- **Closed**: {state_counts['closed']}\n")
    
    print("## Traceability Matrix\n")
    print("| Issue | Type | Title | State | Traces To | Depends On | Verified By | Implemented By |")
    print("|-------|------|-------|-------|-----------|------------|-------------|----------------|")
    
    for issue in sorted(issues, key=lambda x: x['number']):
        labels = [label['name'] for label in issue['labels']]
        req_type = get_requirement_type(issue['title'], labels)
        links = extract_links(issue.get('body', ''))
        
        # Format link lists
        traces_to = ', '.join(f"#{n}" for n in links['traces_to']) or '-'
        depends_on = ', '.join(f"#{n}" for n in links['depends_on']) or '-'
        verified_by = ', '.join(f"#{n}" for n in links['verified_by']) or '-'
        implemented_by = ', '.join(f"#{n}" for n in links['implemented_by']) or '-'
        
        # Truncate title if too long
        title = issue['title']
        if len(title) > 50:
            title = title[:47] + '...'
        
        state_badge = '✅' if issue['state'] == 'closed' else '🔵'
        
        print(f"| #{issue['number']} | {req_type} | {title} | {state_badge} | {traces_to} | {depends_on} | {verified_by} | {implemented_by} |")
    
    print("\n## Orphaned Requirements\n")
    print("Requirements without parent links (excluding StR which are top-level):\n")
    
    orphans = []
    for issue in issues:
        labels = [label['name'] for label in issue['labels']]
        req_type = get_requirement_type(issue['title'], labels)
        links = extract_links(issue.get('body', ''))
        
        # StR issues should not have parent links
        if req_type == 'StR':
            continue
        
        # All other types should trace to parent
        if not links['traces_to']:
            orphans.append((issue['number'], req_type, issue['title']))
    
    if orphans:
        print("| Issue | Type | Title |")
        print("|-------|------|-------|")
        for number, req_type, title in orphans:
            if len(title) > 60:
                title = title[:57] + '...'
            print(f"| #{number} | {req_type} | {title} |")
    else:
        print("✅ No orphaned requirements found! All requirements properly linked.\n")
    
    print("\n## Requirements Without Tests\n")
    print("Functional and non-functional requirements without verification:\n")
    
    # Build map of verified requirements
    verified_reqs = set()
    for issue in issues:
        labels = [label['name'] for label in issue['labels']]
        if 'test-case' in labels:
            links = extract_links(issue.get('body', ''))
            verified_reqs.update(links['traces_to'])
    
    unverified = []
    for issue in issues:
        labels = [label['name'] for label in issue['labels']]
        req_type = get_requirement_type(issue['title'], labels)
        
        if req_type in ['REQ-F', 'REQ-NF']:
            if issue['number'] not in verified_reqs:
                unverified.append((issue['number'], req_type, issue['title']))
    
    if unverified:
        print("| Issue | Type | Title |")
        print("|-------|------|-------|")
        for number, req_type, title in unverified:
            if len(title) > 60:
                title = title[:57] + '...'
            print(f"| #{number} | {req_type} | {title} |")
    else:
        print("✅ All requirements have test coverage!\n")
    
    print("\n## Legend\n")
    print("- **StR**: Stakeholder Requirement (top-level business need)")
    print("- **REQ-F**: Functional Requirement (system SHALL behavior)")
    print("- **REQ-NF**: Non-Functional Requirement (quality attribute)")
    print("- **ADR**: Architecture Decision Record")
    print("- **ARC-C**: Architecture Component")
    print("- **QA-SC**: Quality Attribute Scenario (ATAM)")
    print("- **TEST**: Test Case\n")
    print("- 🔵: Open issue")
    print("- ✅: Closed issue\n")
    
    print("---\n")
    print("*Generated by `scripts/github-traceability-report.py`*")

if __name__ == '__main__':
    if not GITHUB_TOKEN:
        print("Error: GITHUB_TOKEN environment variable not set", file=sys.stderr)
        print("Usage: export GITHUB_TOKEN=ghp_xxx && python scripts/github-traceability-report.py", file=sys.stderr)
        print("\nOptional: Set GITHUB_REPOSITORY=owner/repo or REPO_OWNER and REPO_NAME separately", file=sys.stderr)
        sys.exit(1)
    
    print(f"Generating report for: {REPO_OWNER}/{REPO_NAME}\n", file=sys.stderr)
    generate_matrix()
