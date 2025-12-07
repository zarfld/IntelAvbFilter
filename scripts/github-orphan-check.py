#!/usr/bin/env python3
"""Check for orphaned requirements (no parent/child links).

This script identifies requirements that lack proper traceability links,
helping maintain ISO/IEC/IEEE 29148:2018 compliance.

Standards: ISO/IEC/IEEE 29148:2018 (Requirements Traceability)

Usage:
    export GITHUB_TOKEN=ghp_xxx
    python scripts/github-orphan-check.py

Exit Codes:
    0 - No orphans found
    1 - Orphans found or error occurred
"""
import os
import re
import sys
import requests
from typing import Dict, List

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

REQUIREMENT_LABELS = [
    'functional-requirement',
    'non-functional',
    'architecture-decision',
    'architecture-component',
    'quality-scenario',
    'test-case'
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
    """Fetch all requirement issues (excluding StR which are top-level)."""
    issues = []
    page = 1
    
    while True:
        response = requests.get(
            f'{API_BASE}/issues',
            headers=get_headers(),
            params={
                'state': 'open',  # Only check open issues
                'per_page': 100,
                'page': page
            }
        )
        
        if response.status_code != 200:
            print(f"Error fetching issues: {response.status_code}", file=sys.stderr)
            sys.exit(1)
        
        page_issues = response.json()
        
        if not page_issues:
            break
        
        # Filter for requirement labels (exclude stakeholder-requirement)
        for issue in page_issues:
            labels = [label['name'] for label in issue['labels']]
            if any(label in REQUIREMENT_LABELS for label in labels):
                issues.append(issue)
        
        page += 1
    
    return issues

def extract_parent_links(issue_body: str) -> List[int]:
    """Extract parent requirement links (Traces to: #N)."""
    if not issue_body:
        return []
    
    traces_to = re.findall(r'[Tt]races?\s+to:?\s*#(\d+)', issue_body)
    return [int(n) for n in traces_to]

def find_orphans():
    """Find requirements without traceability links."""
    print("# Orphaned Requirements Check\n")
    print("Checking for requirements without parent links...\n")
    
    issues = fetch_all_requirements()
    orphans = []
    
    for issue in issues:
        body = issue.get('body', '')
        parent_links = extract_parent_links(body)
        
        if not parent_links:
            labels = [label['name'] for label in issue['labels']]
            orphans.append({
                'number': issue['number'],
                'title': issue['title'],
                'labels': labels,
                'url': issue['html_url']
            })
    
    if not orphans:
        print("✅ **No orphaned requirements found!**\n")
        print("All requirements properly linked to parent issues.")
        return 0
    
    print(f"❌ **Found {len(orphans)} orphaned requirement(s)**\n")
    print("Requirements without parent links:\n")
    print("| Issue | Labels | Title |")
    print("|-------|--------|-------|")
    
    for orphan in sorted(orphans, key=lambda x: x['number']):
        title = orphan['title']
        if len(title) > 50:
            title = title[:47] + '...'
        
        labels_str = ', '.join(orphan['labels'][:2])  # Show first 2 labels
        if len(orphan['labels']) > 2:
            labels_str += f" +{len(orphan['labels']) - 2}"
        
        print(f"| [#{orphan['number']}]({orphan['url']}) | {labels_str} | {title} |")
    
    print("\n## Required Action\n")
    print("Each requirement MUST link to its parent using:\n")
    print("```markdown")
    print("## Traceability")
    print("- **Traces to**: #N (parent issue)")
    print("```\n")
    print("**Traceability Rules**:")
    print("- REQ-F/REQ-NF → Link to parent StR issue")
    print("- ADR → Link to requirements it satisfies")
    print("- ARC-C → Link to ADRs and requirements")
    print("- TEST → Link to requirements being verified")
    print("- QA-SC → Link to related requirements\n")
    
    return 1

if __name__ == '__main__':
    if not GITHUB_TOKEN:
        print("Error: GITHUB_TOKEN environment variable not set", file=sys.stderr)
        print("Usage: export GITHUB_TOKEN=ghp_xxx && python scripts/github-orphan-check.py", file=sys.stderr)
        print("\nOptional: Set GITHUB_REPOSITORY=owner/repo or REPO_OWNER and REPO_NAME separately", file=sys.stderr)
        sys.exit(1)
    
    print(f"Checking repository: {REPO_OWNER}/{REPO_NAME}\n", file=sys.stderr)
    exit_code = find_orphans()
    sys.exit(exit_code)
