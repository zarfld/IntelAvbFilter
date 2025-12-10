#!/usr/bin/env python3
"""Integrity Level Scanner for GitHub Issues

Scans GitHub Issues for high-priority requirements (P0/P1) and generates
the same JSON format that integrity_level_scan.py produces for markdown specs.

This ensures compatibility with downstream tools that expect build/integrity-scan.json.

Output format:
{
  "highIntegrity": true/false,
  "files": [ {"path": str, "level": int, "issue": int} ],
  "targetLevel": 3
}

Priority mapping to integrity levels:
- P0 (Critical) = Level 4
- P1 (High) = Level 3
- P2 (Medium) = Level 2
- P3+ (Low) = Level 1
"""
import os
import sys
import json
from pathlib import Path
from github import Github

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build' / 'integrity-scan.json'
TARGET_LEVEL = 3  # P1 and above

PRIORITY_TO_LEVEL = {
    'priority:p0': 4,
    'priority:p1': 3,
    'priority:p2': 2,
    'priority:p3': 1,
}

def main() -> int:
    token = os.environ.get('GITHUB_TOKEN')
    if not token:
        print('ERROR: GITHUB_TOKEN environment variable required', file=sys.stderr)
        return 1
    
    repo_name = os.environ.get('GITHUB_REPOSITORY', 'zarfld/IntelAvbFilter')
    
    try:
        g = Github(token)
        repo = g.get_repo(repo_name)
    except Exception as e:
        print(f'ERROR: Failed to connect to GitHub: {e}', file=sys.stderr)
        return 1
    
    print(f"Scanning {repo_name} for high-priority requirements...")
    
    results = []
    
    # Scan for each priority level
    for priority_label, integrity_level in PRIORITY_TO_LEVEL.items():
        if integrity_level < TARGET_LEVEL:
            continue  # Skip lower priorities
        
        try:
            issues = list(repo.get_issues(labels=[priority_label], state='all'))
            
            for issue in issues:
                # Check if it's a requirement (not just any issue)
                labels = [l.name for l in issue.labels]
                is_requirement = any(
                    'requirement' in l or 'architecture' in l or 'test' in l
                    for l in labels
                )
                
                if is_requirement:
                    results.append({
                        'path': f"GitHub Issue #{issue.number}",
                        'level': integrity_level,
                        'issue': issue.number,
                        'title': issue.title,
                        'priority': priority_label,
                        'state': issue.state,
                        'url': issue.html_url
                    })
                    
            print(f"  {priority_label}: {len([r for r in results if r['level'] == integrity_level])} requirements")
            
        except Exception as e:
            print(f"Warning: Could not fetch {priority_label}: {e}", file=sys.stderr)
    
    # Build output in same format as original script
    data = {
        'highIntegrity': any(r['level'] >= TARGET_LEVEL for r in results),
        'files': results,
        'targetLevel': TARGET_LEVEL,
        'source': 'github-issues',
        'repository': repo_name,
    }
    
    OUT.parent.mkdir(exist_ok=True)
    OUT.write_text(json.dumps(data, indent=2), encoding='utf-8')
    
    print(f"\n✅ Integrity scan complete")
    print(f"   High integrity present: {data['highIntegrity']}")
    print(f"   Total P0/P1 requirements: {len(results)}")
    print(f"   Output: {OUT}")
    
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
