#!/usr/bin/env python3
"""
Audit Issue Labels

Scans all open issues to find labeling problems:
- Issues without any labels
- Issues missing required type labels
- Issues missing priority labels
- Issues missing status labels
- Issues with multiple conflicting labels

Usage:
    python scripts/audit-issue-labels.py
    python scripts/audit-issue-labels.py --fix-auto  # Auto-fix issues
"""

import os
import re
import sys
import json
from typing import Dict, List, Set, Tuple
from dataclasses import dataclass, field

try:
    import requests
except ImportError:
    print("❌ Error: 'requests' module not installed")
    print("Install with: pip install requests")
    sys.exit(1)


@dataclass
class LabelIssue:
    """Represents a labeling problem with an issue."""
    issue_number: int
    issue_title: str
    severity: str  # 'error', 'warning', 'info'
    rule: str
    message: str
    current_labels: List[str]
    suggested_action: str
    auto_fixable: bool = False
    fix_labels_add: List[str] = field(default_factory=list)
    fix_labels_remove: List[str] = field(default_factory=list)


class GitHubIssueAuditor:
    """Audits GitHub issues for label compliance."""
    
    def __init__(self, owner: str, repo: str, token: str):
        self.owner = owner
        self.repo = repo
        self.token = token
        self.base_url = "https://api.github.com"
        self.headers = {
            "Authorization": f"Bearer {token}",
            "Accept": "application/vnd.github.v3+json"
        }
        
    def get_all_issues(self, state: str = "open") -> List[Dict]:
        """Fetch all issues from the repository."""
        issues = []
        page = 1
        
        print(f"📥 Fetching {state} issues from {self.owner}/{self.repo}...")
        
        while True:
            url = f"{self.base_url}/repos/{self.owner}/{self.repo}/issues"
            params = {
                "state": state,
                "per_page": 100,
                "page": page
            }
            
            response = requests.get(url, headers=self.headers, params=params)
            response.raise_for_status()
            
            page_issues = response.json()
            if not page_issues:
                break
                
            # Filter out pull requests
            issues.extend([i for i in page_issues if 'pull_request' not in i])
            page += 1
            
        print(f"✅ Found {len(issues)} {state} issues")
        return issues
    
    def validate_issue(self, issue: Dict) -> List[LabelIssue]:
        """Validate labels on a single issue."""
        problems = []
        number = issue['number']
        title = issue['title']
        labels = [label['name'] for label in issue['labels']]
        body = issue.get('body', '') or ''
        state = issue['state']
        
        # Categorize labels
        type_labels = [l for l in labels if l.startswith('type:')]
        priority_labels = [l for l in labels if l.startswith('priority:')]
        status_labels = [l for l in labels if l.startswith('status:')]
        phase_labels = [l for l in labels if l.startswith('phase:')]
        test_type_labels = [l for l in labels if l.startswith('test-type:')]
        pattern_labels = [l for l in labels if l.startswith('pattern:')]
        context_labels = [l for l in labels if l.startswith('context:')]
        
        # Check for special types
        is_bug = 'bug' in labels
        is_question = 'question' in labels
        is_docs = 'documentation' in labels
        
        # RULE 1: Must have at least one type label
        if not type_labels:
            # Try to infer type from title
            inferred_type = self._infer_type_from_title(title)
            
            if inferred_type:
                problems.append(LabelIssue(
                    issue_number=number,
                    issue_title=title,
                    severity='warning',
                    rule='Missing Type Label',
                    message='Every issue must have at least one type:* label',
                    current_labels=labels,
                    suggested_action=f'Add inferred type: {inferred_type}',
                    auto_fixable=True,
                    fix_labels_add=[inferred_type]
                ))
            else:
                problems.append(LabelIssue(
                    issue_number=number,
                    issue_title=title,
                    severity='error',
                    rule='Missing Type Label',
                    message='Every issue must have at least one type:* label (cannot infer from title)',
                    current_labels=labels,
                    suggested_action='Add appropriate type label (e.g., type:requirement:functional, type:architecture:decision, type:test)',
                    auto_fixable=False  # Requires human judgment
                ))
        
        # RULE 2: Should have exactly one priority (except bugs/questions)
        if not (is_bug or is_question or is_docs):
            if not priority_labels:
                # Try to infer priority from type
                inferred_priority = self._infer_priority(type_labels, title, body)
                
                problems.append(LabelIssue(
                    issue_number=number,
                    issue_title=title,
                    severity='warning',
                    rule='Missing Priority Label',
                    message='Non-bug issues should have a priority label',
                    current_labels=labels,
                    suggested_action=f'Add priority label (suggested: {inferred_priority})',
                    auto_fixable=True,
                    fix_labels_add=[inferred_priority]
                ))
            elif len(priority_labels) > 1:
                problems.append(LabelIssue(
                    issue_number=number,
                    issue_title=title,
                    severity='error',
                    rule='Multiple Priority Labels',
                    message=f'Issue has {len(priority_labels)} priority labels: {", ".join(priority_labels)}',
                    current_labels=labels,
                    suggested_action='Keep only one priority label (highest priority)',
                    auto_fixable=True,
                    fix_labels_remove=priority_labels[1:]  # Keep first, remove rest
                ))
        
        # RULE 3: Must have exactly one status
        if not status_labels:
            problems.append(LabelIssue(
                issue_number=number,
                issue_title=title,
                severity='warning',
                rule='Missing Status Label',
                message='Issues should have a status label',
                current_labels=labels,
                suggested_action='Add status:backlog for new issues',
                auto_fixable=True,
                fix_labels_add=['status:backlog']
            ))
        elif len(status_labels) > 1:
            problems.append(LabelIssue(
                issue_number=number,
                issue_title=title,
                severity='error',
                rule='Multiple Status Labels',
                message=f'Issue has {len(status_labels)} status labels: {", ".join(status_labels)}',
                current_labels=labels,
                suggested_action='Keep only the current status (remove old ones)',
                auto_fixable=False  # Requires knowledge of which is current
            ))
        
        # RULE 4: Closed issues should have status:closed
        if state == 'closed' and 'status:closed' not in labels:
            old_status = [l for l in status_labels if l != 'status:closed']
            problems.append(LabelIssue(
                issue_number=number,
                issue_title=title,
                severity='info',
                rule='Closed Issue Status',
                message='Closed issues should have status:closed label',
                current_labels=labels,
                suggested_action='Update to status:closed',
                auto_fixable=True,
                fix_labels_add=['status:closed'],
                fix_labels_remove=old_status
            ))
        
        # RULE 5: Test issues should have test-type
        if 'type:test' in type_labels and not test_type_labels:
            problems.append(LabelIssue(
                issue_number=number,
                issue_title=title,
                severity='warning',
                rule='Missing Test Type',
                message='Test issues should specify test type',
                current_labels=labels,
                suggested_action='Add test-type:* label (unit, integration, e2e, acceptance)',
                auto_fixable=False  # Requires human judgment
            ))
        
        # RULE 6: Pattern labels only on design/implementation
        if pattern_labels:
            valid_types = ['type:design', 'type:implementation', 'type:test']
            has_valid_type = any(t in type_labels for t in valid_types)
            
            if not has_valid_type:
                problems.append(LabelIssue(
                    issue_number=number,
                    issue_title=title,
                    severity='warning',
                    rule='Pattern Label Misuse',
                    message=f'Pattern labels ({", ".join(pattern_labels)}) should only be on design/implementation issues',
                    current_labels=labels,
                    suggested_action='Remove pattern labels or add appropriate type label',
                    auto_fixable=False
                ))
        
        # RULE 7: Should have phase label
        if not phase_labels and type_labels:
            inferred_phase = self._infer_phase(type_labels[0])
            if inferred_phase:
                problems.append(LabelIssue(
                    issue_number=number,
                    issue_title=title,
                    severity='info',
                    rule='Missing Phase Label',
                    message='Issue could benefit from phase label',
                    current_labels=labels,
                    suggested_action=f'Add {inferred_phase}',
                    auto_fixable=True,
                    fix_labels_add=[inferred_phase]
                ))
        
        return problems
    
    def _infer_type_from_title(self, title: str) -> str:
        """Infer issue type from title patterns."""
        title_upper = title.upper()
        
        # Requirement patterns
        if 'REQ-F-' in title_upper or title_upper.startswith('REQ-F'):
            return 'type:requirement:functional'
        if 'REQ-NF-' in title_upper or title_upper.startswith('REQ-NF'):
            return 'type:requirement:non-functional'
        if 'STR-' in title_upper or title_upper.startswith('STR'):
            return 'type:requirement:stakeholder'
        
        # Architecture patterns
        if 'ADR-' in title_upper or title_upper.startswith('ADR'):
            return 'type:architecture:decision'
        if 'ARC-C-' in title_upper or 'ARCH-' in title_upper:
            return 'type:architecture:component'
        if 'QA-SC-' in title_upper or 'QUALITY-' in title_upper:
            return 'type:architecture:quality-scenario'
        
        # Test patterns
        if 'TEST-' in title_upper or title_upper.startswith('TEST'):
            return 'type:test'
        if any(word in title_upper for word in ['TEST CASE', 'TEST:', 'UNIT TEST', 'INTEGRATION TEST']):
            return 'type:test'
        
        # Design patterns
        if 'DESIGN-' in title_upper or 'DES-' in title_upper:
            return 'type:design'
        
        # Implementation patterns
        if 'IMPL-' in title_upper or title_upper.startswith('IMPLEMENT'):
            return 'type:implementation'
        
        # Refactoring patterns
        if any(word in title_upper for word in ['REFACTOR', 'REFACTORING', 'CLEANUP', 'TECH DEBT']):
            return 'type:refactoring'
        
        # Cannot infer
        return ''
    
    def _infer_priority(self, type_labels: List[str], title: str, body: str) -> str:
        """Infer priority from context."""
        title_lower = title.lower()
        body_lower = body.lower()
        
        # P0 indicators
        if any(word in title_lower for word in ['critical', 'urgent', 'blocker', 'security']):
            return 'priority:p0'
        
        # P1 indicators
        if any(word in title_lower for word in ['important', 'must', 'required']):
            return 'priority:p1'
        
        # Architecture decisions are typically P1
        if 'type:architecture:decision' in type_labels:
            return 'priority:p1'
        
        # Tests are typically P2
        if 'type:test' in type_labels:
            return 'priority:p2'
        
        # Default to P2
        return 'priority:p2'
    
    def _infer_phase(self, type_label: str) -> str:
        """Infer phase from type label."""
        phase_map = {
            'type:requirement:stakeholder': 'phase:01-stakeholder-requirements',
            'type:requirement:functional': 'phase:02-requirements',
            'type:requirement:non-functional': 'phase:02-requirements',
            'type:architecture:decision': 'phase:03-architecture',
            'type:architecture:component': 'phase:03-architecture',
            'type:architecture:quality-scenario': 'phase:03-architecture',
            'type:design': 'phase:04-design',
            'type:implementation': 'phase:05-implementation',
            'type:refactoring': 'phase:05-implementation',
            'type:test': 'phase:07-verification-validation'
        }
        return phase_map.get(type_label, '')
    
    def apply_fixes(self, problem: LabelIssue, dry_run: bool = True) -> bool:
        """Apply automatic fixes to an issue."""
        if not problem.auto_fixable:
            return False
        
        actions = []
        
        # Add labels
        if problem.fix_labels_add:
            if not dry_run:
                url = f"{self.base_url}/repos/{self.owner}/{self.repo}/issues/{problem.issue_number}/labels"
                response = requests.post(url, headers=self.headers, json={"labels": problem.fix_labels_add})
                response.raise_for_status()
            actions.append(f"Add: {', '.join(problem.fix_labels_add)}")
        
        # Remove labels
        for label in problem.fix_labels_remove:
            if not dry_run:
                url = f"{self.base_url}/repos/{self.owner}/{self.repo}/issues/{problem.issue_number}/labels/{label}"
                response = requests.delete(url, headers=self.headers)
                # 404 is OK (label doesn't exist)
                if response.status_code not in [200, 204, 404]:
                    response.raise_for_status()
            actions.append(f"Remove: {label}")
        
        if actions:
            action_str = "; ".join(actions)
            print(f"  {'[DRY RUN] ' if dry_run else ''}Fixed #{problem.issue_number}: {action_str}")
            return True
        
        return False


def print_report(problems: List[LabelIssue]):
    """Print a formatted report of all problems."""
    if not problems:
        print("\n✅ No labeling issues found! All issues are properly labeled.\n")
        return
    
    # Group by severity
    errors = [p for p in problems if p.severity == 'error']
    warnings = [p for p in problems if p.severity == 'warning']
    info = [p for p in problems if p.severity == 'info']
    
    print("\n" + "=" * 80)
    print("📊 LABEL AUDIT REPORT")
    print("=" * 80)
    
    print(f"\nTotal Issues: {len(set(p.issue_number for p in problems))}")
    print(f"Total Problems: {len(problems)}")
    print(f"  ❌ Errors: {len(errors)}")
    print(f"  ⚠️  Warnings: {len(warnings)}")
    print(f"  ℹ️  Info: {len(info)}")
    print(f"  🔧 Auto-fixable: {len([p for p in problems if p.auto_fixable])}")
    
    # Print errors
    if errors:
        print("\n" + "-" * 80)
        print("❌ ERRORS (Must Fix)")
        print("-" * 80)
        for problem in errors:
            print(f"\n#{problem.issue_number}: {problem.issue_title}")
            print(f"  Rule: {problem.rule}")
            print(f"  Problem: {problem.message}")
            print(f"  Current labels: {', '.join(problem.current_labels) if problem.current_labels else '(none)'}")
            print(f"  Action: {problem.suggested_action}")
            if problem.auto_fixable:
                print(f"  🔧 Auto-fixable: Yes")
    
    # Print warnings
    if warnings:
        print("\n" + "-" * 80)
        print("⚠️  WARNINGS (Should Fix)")
        print("-" * 80)
        for problem in warnings:
            print(f"\n#{problem.issue_number}: {problem.issue_title}")
            print(f"  Rule: {problem.rule}")
            print(f"  Problem: {problem.message}")
            print(f"  Current labels: {', '.join(problem.current_labels) if problem.current_labels else '(none)'}")
            print(f"  Recommended: {problem.suggested_action}")
            if problem.auto_fixable:
                print(f"  🔧 Auto-fixable: Yes")
    
    # Print info (only count, not details)
    if info:
        print(f"\nℹ️  {len(info)} informational suggestions (run with --verbose to see details)")
    
    print("\n" + "=" * 80)
    print(f"\n💡 To automatically fix {len([p for p in problems if p.auto_fixable])} auto-fixable issues:")
    print("   python scripts/audit-issue-labels.py --fix-auto")
    print("\n💡 To see detailed info messages:")
    print("   python scripts/audit-issue-labels.py --verbose")
    print()


def main():
    import argparse
    
    parser = argparse.ArgumentParser(description="Audit GitHub issue labels")
    parser.add_argument('--owner', help='Repository owner (default: from git remote)')
    parser.add_argument('--repo', help='Repository name (default: from git remote)')
    parser.add_argument('--token', help='GitHub token (default: from GITHUB_TOKEN env)')
    parser.add_argument('--fix-auto', action='store_true', help='Automatically fix auto-fixable issues')
    parser.add_argument('--dry-run', action='store_true', help='Show what would be fixed without applying')
    parser.add_argument('--state', default='open', choices=['open', 'closed', 'all'], help='Issue state to check')
    parser.add_argument('--verbose', action='store_true', help='Show all info messages')
    
    args = parser.parse_args()
    
    # Get GitHub token
    token = args.token or os.environ.get('GITHUB_TOKEN')
    if not token:
        print("❌ Error: GitHub token required")
        print("Set GITHUB_TOKEN environment variable or use --token")
        sys.exit(1)
    
    # Get owner/repo from git remote if not provided
    owner = args.owner
    repo = args.repo
    
    if not owner or not repo:
        try:
            import subprocess
            remote_url = subprocess.check_output(['git', 'remote', 'get-url', 'origin'], text=True).strip()
            # Match GitHub URL and extract owner/repo (handle .git suffix and repo names with dots)
            match = re.search(r'github\.com[:/]([^/]+)/(.+?)(?:\.git)?$', remote_url)
            if match:
                owner = owner or match.group(1)
                repo = repo or match.group(2)
        except Exception:
            pass
    
    if not owner or not repo:
        print("❌ Error: Could not determine repository owner/name")
        print("Use --owner and --repo arguments")
        sys.exit(1)
    
    print(f"🔍 Auditing labels for {owner}/{repo}\n")
    
    # Create auditor and fetch issues
    auditor = GitHubIssueAuditor(owner, repo, token)
    
    try:
        issues = auditor.get_all_issues(state=args.state)
    except requests.exceptions.RequestException as e:
        print(f"❌ Error fetching issues: {e}")
        sys.exit(1)
    
    if not issues:
        print(f"✅ No {args.state} issues found")
        return
    
    # Validate all issues
    all_problems = []
    for issue in issues:
        problems = auditor.validate_issue(issue)
        all_problems.extend(problems)
    
    # Filter out info messages unless verbose
    if not args.verbose:
        display_problems = [p for p in all_problems if p.severity != 'info']
    else:
        display_problems = all_problems
    
    # Print report
    print_report(display_problems)
    
    # Apply fixes if requested
    if args.fix_auto or args.dry_run:
        fixable = [p for p in all_problems if p.auto_fixable]
        
        if not fixable:
            print("No auto-fixable issues found")
            return
        
        print(f"\n{'[DRY RUN] ' if args.dry_run else ''}Applying fixes to {len(fixable)} issues...\n")
        
        fixed_count = 0
        for problem in fixable:
            if auditor.apply_fixes(problem, dry_run=args.dry_run):
                fixed_count += 1
        
        print(f"\n✅ {'Would fix' if args.dry_run else 'Fixed'} {fixed_count} issues")
    
    # Exit with error code if there are errors (only when not fixing)
    errors = [p for p in all_problems if p.severity == 'error']
    if errors and not (args.fix_auto or args.dry_run):
        sys.exit(1)


if __name__ == '__main__':
    main()
