#!/usr/bin/env python3
"""
Validate GitHub issue extraction and traceability linkage.
Checks each issue individually and reports any issues.
"""
import os
import sys
import re
from github import Github

def extract_all_refs(body):
    """Extract all #N references from body"""
    if not body:
        return []
    refs = re.findall(r'#(\d+)', body)
    return sorted(set(int(r) for r in refs))

def main():
    token = os.environ.get('GITHUB_TOKEN')
    if not token:
        print('ERROR: GITHUB_TOKEN required', file=sys.stderr)
        return 1
    
    repo_name = os.environ.get('GITHUB_REPOSITORY', 'zarfld/IntelAvbFilter')
    g = Github(token)
    repo = g.get_repo(repo_name)
    
    print(f"Validating all issues in {repo_name}...")
    print("=" * 80)
    
    # Get all issues
    all_issues = list(repo.get_issues(state='all'))
    print(f"\nTotal issues to validate: {len(all_issues)}\n")
    
    issues_with_problems = []
    issues_validated = 0
    
    for issue in sorted(all_issues, key=lambda i: i.number):
        issue_id = f"#{issue.number}"
        print(f"\n[{issue_id}] {issue.title}")
        print(f"  State: {issue.state}")
        print(f"  Labels: {[l.name for l in issue.labels]}")
        
        # Extract refs
        refs = extract_all_refs(issue.body or "")
        print(f"  References found ({len(refs)}): {refs[:20]}")
        
        if len(refs) > 20:
            print(f"    ... and {len(refs) - 20} more")
        
        # Check for issues
        problems = []
        
        # Check if body is truncated
        if issue.body and len(issue.body) > 60000:
            problems.append(f"Body very long ({len(issue.body)} chars) - may be truncated by API")
        
        # Check for self-references
        if issue.number in refs:
            problems.append(f"Self-reference detected: issue references itself")
        
        # Report problems
        if problems:
            print(f"  ⚠️  PROBLEMS:")
            for p in problems:
                print(f"      - {p}")
            issues_with_problems.append((issue_id, problems))
        else:
            print(f"  ✅ Validated")
            issues_validated += 1
    
    # Summary
    print("\n" + "=" * 80)
    print(f"\nValidation Summary:")
    print(f"  Total issues: {len(all_issues)}")
    print(f"  Validated OK: {issues_validated}")
    print(f"  With problems: {len(issues_with_problems)}")
    
    if issues_with_problems:
        print(f"\n⚠️  Issues requiring attention:")
        for issue_id, problems in issues_with_problems:
            print(f"  {issue_id}:")
            for p in problems:
                print(f"    - {p}")
        return 1
    else:
        print(f"\n✅ All issues validated successfully!")
        return 0

if __name__ == '__main__':
    sys.exit(main())
