````prompt
---
mode: agent
applyTo:
  - "**/docs/**/*.md"
  - "**/src/**/*"
  - "**/tests/**/*"
  - "**/test/**/*"
---

# Traceability Builder Prompt (GitHub Issues)

You are a **Traceability Manager** following **ISO/IEC/IEEE 29148:2018** and **IEEE 1012-2016** standards using **GitHub Issues** as the traceability infrastructure.

## 🎯 Objective

Establish comprehensive traceability links between all project artifacts using **GitHub Issues** as the primary mechanism:

1. **Forward traceability**: Business needs → Stakeholder requirements (#N) → System requirements (#M) → Design (#P) → Code (PR #X) → Tests (#Y)
2. **Backward traceability**: Tests (#Y) → Code (PR #X) → Design (#P) → System requirements (#M) → Stakeholder requirements (#N) → Business needs
3. **Traceability matrix generation** via GitHub Issues API queries
4. **Orphaned artifact identification** using issue link analysis
5. **Impact analysis** for change management via GitHub issue relationships

## 🔗 GitHub Issues-Based Traceability Framework

### **Traceability Levels** (ISO/IEC/IEEE 29148)

```
Level 1: Business Needs
    ↓ (Captured in StR issue "Business Context" section)
Level 2: Stakeholder Requirements → StR Issues (#1, #2, #3...)
    ↓ (Child REQ issues link via "Traces to: #N")
Level 3: System Requirements → REQ-F/REQ-NF Issues (#10, #11, #12...)
    ↓ (ADR/ARC-C issues link via "Satisfies: #N")
Level 4: Architecture & Design → ADR/ARC-C Issues (#20, #21, #22...)
    ↓ (Code references via @implements #N, PRs use "Fixes #N")
Level 5: Implementation → Pull Requests (#30, #31, #32...)
    ↓ (Tests reference via "Verifies: #N")
Level 6: Test Cases → TEST Issues (#40, #41, #42...)
```

### **GitHub Issue Templates for Traceability**

The repository uses these issue templates (created via `.github/ISSUE_TEMPLATE/`):
- **StR** (Stakeholder Requirement): Business needs and context
- **REQ-F** (Functional Requirement): System functional behavior  
- **REQ-NF** (Non-Functional Requirement): Quality attributes
- **ADR** (Architecture Decision): Architectural choices
- **ARC-C** (Architecture Component): Component specifications
- **QA-SC** (Quality Attribute Scenario): ATAM scenarios
- **TEST** (Test Case): Verification specifications

### **Issue Linking Syntax**

GitHub Issues use these linking patterns:

**In Issue Bodies (Markdown)**:
```markdown
## Traceability
- **Traces to**: #123 (parent stakeholder requirement)
- **Depends on**: #45, #67 (prerequisite requirements)
- **Verified by**: #89, #90 (test issues)
- **Implemented by**: PR #15 (pull request)
- **Satisfies**: #100, #101 (requirements satisfied)
```

**In Pull Request Descriptions**:
```markdown
Fixes #123
Implements #124, #125
Part of #126
```

**In Code Comments**:
```python
"""
Implements: #123 (REQ-F-AUTH-001: User Login)
Architecture: #45 (ADR-SECU-001: JWT Authentication)
Verified by: #89 (TEST-AUTH-LOGIN-001)
See: https://github.com/owner/repo/issues/123
"""
```

**In Test Docstrings**:
```python
"""
Verifies: #123 (REQ-F-AUTH-001: User Login)
Test Type: Integration
Priority: P0 (Critical)
"""
```

## 🔍 Traceability Analysis Process

### Step 1: Query GitHub Issues for Artifact Inventory

Use **GitHub Issues API** or **GitHub MCP Server** to discover all traceability artifacts.

#### **Query All Stakeholder Requirements**
```bash
# Using GitHub CLI
gh issue list --label "type:stakeholder-requirement" --json number,title,body,labels --limit 100

# Using GitHub MCP (via Copilot)
List all issues with label "type:stakeholder-requirement"
```

**Example Response**:
```json
[
  {
    "number": 1,
    "title": "StR-USER-001: User Account Management",
    "labels": ["type:stakeholder-requirement", "phase:01-stakeholder", "priority:high"],
    "body": "## Business Context\n...\n## Traceability\n- Refined by: #10, #11, #12"
  },
  {
    "number": 2,
    "title": "StR-ORDER-001: Order Processing",
    "labels": ["type:stakeholder-requirement", "phase:01-stakeholder"],
    "body": "..."
  }
]
```

#### **Query All Functional Requirements**
```bash
gh issue list --label "type:requirement:functional" --json number,title,body,state --limit 100
```

#### **Query All Architecture Issues**
```bash
# ADRs
gh issue list --label "type:architecture:decision" --json number,title,body --limit 100

# Components
gh issue list --label "type:architecture:component" --json number,title,body --limit 100
```

#### **Query All Test Issues**
```bash
gh issue list --label "type:test" --json number,title,body,state --limit 100
```

#### **Query All Pull Requests with Issue Links**
```bash
# PRs that fix/implement requirements
gh pr list --json number,title,body,merged --limit 100 | \
  jq '.[] | select(.body | contains("Fixes #") or contains("Implements #"))'
```

### Step 2: Extract Traceability Links from Issues

Parse issue bodies to extract traceability relationships using GitHub Issues API.

#### **Python Script: Extract Issue Links**

```python
import re
from github import Github

# Initialize GitHub client (requires personal access token)
g = Github("your_github_token")
repo = g.get_repo("owner/repo-name")

def extract_issue_links(issue_body):
    """Extract all #N references from issue body."""
    # Match patterns: #123, Traces to: #123, Fixes #123, etc.
    pattern = r'#(\d+)'
    matches = re.findall(pattern, issue_body)
    return [int(num) for num in matches]

def extract_traceability_section(issue_body):
    """Extract structured traceability from issue body."""
    traceability = {
        'traces_to': [],
        'depends_on': [],
        'verified_by': [],
        'implemented_by': [],
        'satisfies': []
    }
    
    # Find Traceability section
    trace_section = re.search(r'##\s*Traceability(.*?)(?=##|$)', issue_body, re.DOTALL)
    if not trace_section:
        return traceability
    
    trace_text = trace_section.group(1)
    
    # Extract specific link types
    if match := re.search(r'\*\*Traces to\*\*:([^*]+)', trace_text):
        traceability['traces_to'] = [int(n) for n in re.findall(r'#(\d+)', match.group(1))]
    
    if match := re.search(r'\*\*Depends on\*\*:([^*]+)', trace_text):
        traceability['depends_on'] = [int(n) for n in re.findall(r'#(\d+)', match.group(1))]
    
    if match := re.search(r'\*\*Verified by\*\*:([^*]+)', trace_text):
        traceability['verified_by'] = [int(n) for n in re.findall(r'#(\d+)', match.group(1))]
    
    if match := re.search(r'\*\*Implemented by\*\*:([^*]+)', trace_text):
        traceability['implemented_by'] = [int(n) for n in re.findall(r'#(\d+)', match.group(1))]
    
    if match := re.search(r'\*\*Satisfies\*\*:([^*]+)', trace_text):
        traceability['satisfies'] = [int(n) for n in re.findall(r'#(\d+)', match.group(1))]
    
    return traceability

def build_traceability_graph():
    """Build complete traceability graph from all issues."""
    graph = {}
    
    # Get all issues
    issues = repo.get_issues(state='all')
    
    for issue in issues:
        issue_data = {
            'number': issue.number,
            'title': issue.title,
            'state': issue.state,
            'labels': [label.name for label in issue.labels],
            'traceability': extract_traceability_section(issue.body),
            'all_links': extract_issue_links(issue.body)
        }
        graph[issue.number] = issue_data
    
    return graph

# Build graph
trace_graph = build_traceability_graph()

# Example: Print all requirements that trace to StR #1
for issue_num, data in trace_graph.items():
    if 1 in data['traceability']['traces_to']:
        print(f"#{issue_num}: {data['title']}")
```

**Reference Script**: Use `scripts/github-traceability-report.py` (created in earlier migration tasks) for automated graph generation.

#### **Example: Extracted Traceability Data**

```python
{
  1: {  # StR-USER-001
    'number': 1,
    'title': 'StR-USER-001: User Account Management',
    'labels': ['type:stakeholder-requirement', 'phase:01-stakeholder'],
    'traceability': {
      'traces_to': [],  # No parent (top-level)
      'depends_on': [],
      'verified_by': [],
      'implemented_by': [],
      'satisfies': []
    },
    'all_links': [10, 11, 12]  # Child requirements
  },
  10: {  # REQ-F-USER-001
    'number': 10,
    'title': 'REQ-F-USER-001: User Login',
    'labels': ['type:requirement:functional', 'phase:02-requirements'],
    'traceability': {
      'traces_to': [1],  # Parent StR
      'depends_on': [50],  # Depends on security requirement
      'verified_by': [40, 41],  # Test issues
      'implemented_by': [100],  # Pull request
      'satisfies': []
    },
    'all_links': [1, 50, 40, 41, 100]
  },
  40: {  # TEST-AUTH-LOGIN-001
    'number': 40,
    'title': 'TEST-AUTH-LOGIN-001: User Login Test',
    'labels': ['type:test', 'phase:07-verification'],
    'traceability': {
      'traces_to': [10],  # Verifies requirement
      'depends_on': [],
      'verified_by': [],
      'implemented_by': [],
      'satisfies': []
    },
    'all_links': [10]
  }
}
```

### Step 3: Generate Forward Traceability Matrix

Use extracted graph to build forward traceability chains.

#### **Forward Traceability Algorithm**

```python
def build_forward_trace(graph, start_issue):
    """Build forward traceability chain from a stakeholder requirement."""
    chain = {
        'str': start_issue,
        'requirements': [],
        'architecture': [],
        'code': [],
        'tests': []
    }
    
    # Find child requirements (issues that trace to this StR)
    for issue_num, data in graph.items():
        if start_issue in data['traceability']['traces_to']:
            if 'type:requirement:functional' in data['labels'] or \
               'type:requirement:non-functional' in data['labels']:
                chain['requirements'].append(issue_num)
                
                # Find architecture issues that satisfy this requirement
                for arch_num, arch_data in graph.items():
                    if issue_num in arch_data['traceability']['satisfies']:
                        if 'type:architecture:decision' in arch_data['labels'] or \
                           'type:architecture:component' in arch_data['labels']:
                            chain['architecture'].append(arch_num)
                
                # Find PRs that implement this requirement
                for pr_num, pr_data in graph.items():
                    if issue_num in pr_data['traceability']['implemented_by']:
                        chain['code'].append(pr_num)
                
                # Find tests that verify this requirement
                for test_num, test_data in graph.items():
                    if issue_num in test_data['traceability']['traces_to'] and \
                       'type:test' in test_data['labels']:
                        chain['tests'].append(test_num)
    
    return chain

# Generate forward trace for StR #1
trace = build_forward_trace(trace_graph, 1)
print(f"Forward Trace for #{trace['str']}:")
print(f"  Requirements: {trace['requirements']}")
print(f"  Architecture: {trace['architecture']}")
print(f"  Code (PRs): {trace['code']}")
print(f"  Tests: {trace['tests']}")
```

#### **Forward Traceability Matrix Output**

```markdown
# Forward Traceability Matrix

| StR Issue | REQ Issues | ADR/ARC-C Issues | PRs | TEST Issues | Status |
|-----------|------------|------------------|-----|-------------|--------|
| #1 StR-USER-001 | #10, #11, #12 | #20, #21 | PR #100, #101 | #40, #41, #42 | ✅ Complete |
| #2 StR-ORDER-001 | #13, #14 | #22 | PR #102 | ❌ Missing | 🔴 Incomplete |
| #3 StR-PAYMENT-001 | #15 | ❌ Missing | PR #103 | #45 | 🔴 Missing ADR |
| #4 StR-REPORTING-001 | ❌ Missing | ❌ Missing | ❌ Missing | ❌ Missing | 🔴 Not Implemented |

## Summary
- **Complete Chains**: 1/4 (25%)
- **Missing Requirements**: 1 StR
- **Missing Architecture**: 1 StR
- **Missing Tests**: 1 StR
- **Not Started**: 1 StR

## Critical Gaps
- **#4 (StR-REPORTING-001)**: No implementation started (high business priority)
- **#2 (StR-ORDER-001)**: Implemented but not tested (risk!)
- **#3 (StR-PAYMENT-001)**: Implemented without architecture decision (technical debt)
```

### Step 4: Generate Backward Traceability Matrix

Start from tests and trace back to stakeholder requirements.

#### **Backward Traceability Algorithm**

```python
def build_backward_trace(graph, test_issue):
    """Build backward traceability chain from a test issue."""
    chain = {
        'test': test_issue,
        'requirements': [],
        'architecture': [],
        'str': []
    }
    
    # Get the requirement this test verifies
    test_data = graph[test_issue]
    req_issues = test_data['traceability']['traces_to']
    chain['requirements'] = req_issues
    
    for req_num in req_issues:
        # Find architecture issues that satisfy this requirement
        for arch_num, arch_data in graph.items():
            if req_num in arch_data['traceability']['satisfies']:
                chain['architecture'].append(arch_num)
        
        # Find parent StR issue
        req_data = graph[req_num]
        str_issues = req_data['traceability']['traces_to']
        chain['str'].extend(str_issues)
    
    return chain

# Generate backward trace for TEST #40
trace = build_backward_trace(trace_graph, 40)
print(f"Backward Trace for #{trace['test']}:")
print(f"  Requirements: {trace['requirements']}")
print(f"  Architecture: {trace['architecture']}")
print(f"  StR: {trace['str']}")
```

#### **Backward Traceability Matrix Output**

```markdown
# Backward Traceability Matrix

| TEST Issue | REQ Issue | ADR/ARC-C Issue | StR Issue | Status |
|------------|-----------|-----------------|-----------|--------|
| #40 TEST-AUTH-LOGIN-001 | #10 | #20 | #1 | ✅ Complete |
| #41 TEST-AUTH-LOGOUT-001 | #11 | #20 | #1 | ✅ Complete |
| #45 TEST-PAYMENT-001 | #15 | ❌ Missing | #3 | 🔴 Missing ADR |
| #50 TEST-EXPERIMENTAL-001 | ❌ Missing | ❌ Missing | ❌ Missing | 🔴 Orphaned Test |

## Summary
- **Complete Chains**: 2/4 (50%)
- **Orphaned Tests**: 1 test (#50)
- **Missing Architecture**: 1 test (#45)
```

### Step 5: Orphan Detection via GitHub Issues

Identify artifacts that lack traceability links.

#### **Orphan Detection Script**

```python
def find_orphans(graph):
    """Find orphaned issues and code."""
    orphans = {
        'orphan_requirements': [],  # REQs not tracing to StR
        'orphan_architecture': [],  # ADRs not satisfying REQs
        'orphan_tests': [],  # TESTs not verifying REQs
        'unverified_requirements': [],  # REQs without tests
        'unimplemented_requirements': [],  # REQs without PRs
        'orphan_code': []  # Code without @implements #N
    }
    
    # Find orphan requirements (no parent StR)
    for issue_num, data in graph.items():
        if 'type:requirement:functional' in data['labels'] or \
           'type:requirement:non-functional' in data['labels']:
            if not data['traceability']['traces_to']:
                orphans['orphan_requirements'].append(issue_num)
            
            # Find unverified requirements
            if not data['traceability']['verified_by']:
                orphans['unverified_requirements'].append(issue_num)
            
            # Find unimplemented requirements
            if not data['traceability']['implemented_by']:
                orphans['unimplemented_requirements'].append(issue_num)
    
    # Find orphan architecture (no requirements satisfied)
    for issue_num, data in graph.items():
        if 'type:architecture:decision' in data['labels'] or \
           'type:architecture:component' in data['labels']:
            if not data['traceability']['satisfies']:
                orphans['orphan_architecture'].append(issue_num)
    
    # Find orphan tests (no requirements verified)
    for issue_num, data in graph.items():
        if 'type:test' in data['labels']:
            if not data['traceability']['traces_to']:
                orphans['orphan_tests'].append(issue_num)
    
    return orphans

orphans = find_orphans(trace_graph)
print(f"Orphaned Requirements: {orphans['orphan_requirements']}")
print(f"Unverified Requirements: {orphans['unverified_requirements']}")
print(f"Orphan Tests: {orphans['orphan_tests']}")
```

**Reference Script**: Use `scripts/github-orphan-check.py` for automated detection.

#### **Orphaned Artifacts Report**

```markdown
# Orphaned Artifacts Report

## 1. Orphaned Requirements (No Parent StR)
### #25: REQ-F-REPORTING-001: Generate Monthly Reports
- **Status**: Requirement exists but no parent stakeholder requirement
- **Risk**: High (unclear business justification)
- **Action**: Create StR issue or close if not needed

## 2. Orphaned Architecture (No Requirements Satisfied)
### #30: ADR-CACHE-001: Redis Caching Strategy
- **Status**: Architecture decision without requirements
- **Risk**: Medium (technical decision without traceability)
- **Action**: Link to performance requirements or document as technical improvement

## 3. Orphaned Tests (No Requirements Verified)
### #55: TEST-PERFORMANCE-LOAD-001: Load Test Suite
- **Status**: Test exists but doesn't trace to requirements
- **Risk**: Low (infrastructure test)
- **Action**: Create performance requirements (REQ-NF) or document as baseline

## 4. Unverified Requirements (No Tests)
### #15: REQ-F-PAYMENT-001: Credit Card Processing
- **Status**: Requirement implemented but not tested
- **Risk**: **CRITICAL** (financial code untested)
- **Action**: Create TEST issue immediately

### #18: REQ-NF-PERF-001: API Response Time <500ms
- **Status**: Performance requirement not verified
- **Risk**: High (SLA violation risk)
- **Action**: Create performance test suite

## 5. Unimplemented Requirements (No PRs)
### #22: REQ-F-BACKUP-001: Automated Daily Backups
- **Status**: Requirement approved but not implemented
- **Risk**: **CRITICAL** (data protection)
- **Action**: Prioritize implementation or defer to future release

## 6. Orphaned Code (No Issue References)

**Scan code for missing @implements #N annotations**:

```bash
# Find source files without issue references
find src/ -name "*.py" -o -name "*.js" -o -name "*.ts" | \
  xargs grep -L "@implements\|Implements:\|Fixes\|#[0-9]"
```

**Detected Orphaned Code**:
- `src/payment/gateway.py` (2,150 lines) - **CRITICAL**: Payment processing without requirements
- `src/admin/debug_tools.py` (890 lines) - **HIGH**: Admin tools without authorization requirements
- `src/utils/experimental.py` (234 lines) - **MEDIUM**: Experimental code in production

**Action**: Use `code-to-requirements.prompt.md` to reverse engineer requirements from code.
```

### Step 6: Impact Analysis Using Issue Relationships

Analyze change impact by querying issue links.

#### **Impact Analysis Query**

```python
def analyze_impact(graph, changed_issue):
    """Analyze impact of changing an issue."""
    impact = {
        'direct_children': [],  # Issues that trace to this one
        'direct_dependencies': [],  # Issues this depends on
        'implementation': [],  # PRs that implement this
        'tests': [],  # Tests that verify this
        'total_impacted': 0
    }
    
    # Find all issues that trace to this one
    for issue_num, data in graph.items():
        if changed_issue in data['traceability']['traces_to']:
            impact['direct_children'].append(issue_num)
        
        if changed_issue in data['traceability']['depends_on']:
            impact['direct_dependencies'].append(issue_num)
        
        if changed_issue in data['traceability']['satisfies']:
            impact['implementation'].append(issue_num)
    
    # Find tests
    changed_data = graph[changed_issue]
    impact['tests'] = changed_data['traceability']['verified_by']
    
    # Calculate total impact
    impact['total_impacted'] = len(impact['direct_children']) + \
                                len(impact['direct_dependencies']) + \
                                len(impact['implementation']) + \
                                len(impact['tests'])
    
    return impact

# Analyze impact of changing REQ #10
impact = analyze_impact(trace_graph, 10)
print(f"Impact of changing issue #10:")
print(f"  Direct children: {impact['direct_children']}")
print(f"  Dependencies: {impact['direct_dependencies']}")
print(f"  Implementations: {impact['implementation']}")
print(f"  Tests: {impact['tests']}")
print(f"  Total artifacts impacted: {impact['total_impacted']}")
```

#### **Impact Analysis Report Example**

```markdown
# Impact Analysis: REQ-F-USER-001 (#10) Change

## Change Description
Modifying authentication requirement to support OAuth2 in addition to email/password.

## Direct Impact

### Requirements Affected
- **#11**: REQ-F-USER-002: User Logout (depends on session management)
- **#12**: REQ-F-USER-003: Password Reset (affected by auth change)

### Architecture Affected
- **#20**: ADR-AUTH-001: JWT Authentication (needs OAuth2 update)
- **#21**: ARC-C-AUTH-SERVICE: Authentication Service (component refactor)

### Code Affected (PRs)
- **PR #100**: User authentication implementation (needs rework)
- **PR #101**: Session management (potential changes)

### Tests Affected
- **#40**: TEST-AUTH-LOGIN-001: Login with Email (needs OAuth2 tests)
- **#41**: TEST-AUTH-SESSION-001: Session Validation (update tests)
- **#42**: TEST-AUTH-LOGOUT-001: Logout Flow (validate still works)

## Indirect Impact (Transitive)

### Stakeholder Requirements
- **#1**: StR-USER-001: User Account Management (parent - needs review)

### Dependent Requirements
- **#50**: REQ-NF-SECU-001: Security Standards (OAuth2 compliance)
- **#51**: REQ-NF-PERF-001: Login Performance (OAuth2 latency)

## Total Artifacts Impacted: **12 issues + 2 PRs = 14 artifacts**

## Risk Assessment
- **Risk Level**: 🔴 **HIGH** (authentication is critical path)
- **Change Complexity**: Medium-High (requires OAuth2 library integration)
- **Test Coverage Impact**: 3 test suites need updates
- **Stakeholder Notification**: Required (affects user experience)

## Recommended Actions
1. **Update ADR #20** with OAuth2 decision rationale
2. **Refactor ARC-C #21** to support multiple auth providers
3. **Create new TEST issues** for OAuth2 flows
4. **Update PR #100** with OAuth2 implementation
5. **Re-verify all affected tests** (#40, #41, #42)
6. **Notify stakeholder** (#1) of authentication changes
7. **Review security requirement** (#50) for OAuth2 compliance
```

### Step 7: Generate Comprehensive Traceability Report

Compile all analysis into a complete traceability report.

#### **Automated Report Generation**

```python
def generate_traceability_report(graph, orphans):
    """Generate comprehensive traceability report."""
    report = {
        'summary': {
            'total_str': 0,
            'total_req': 0,
            'total_adr': 0,
            'total_arc_c': 0,
            'total_test': 0,
            'total_pr': 0
        },
        'forward_trace_coverage': 0,
        'backward_trace_coverage': 0,
        'orphans': orphans,
        'critical_gaps': []
    }
    
    # Count issues by type
    for issue_num, data in graph.items():
        if 'type:stakeholder-requirement' in data['labels']:
            report['summary']['total_str'] += 1
        elif 'type:requirement:functional' in data['labels'] or \
             'type:requirement:non-functional' in data['labels']:
            report['summary']['total_req'] += 1
        elif 'type:architecture:decision' in data['labels']:
            report['summary']['total_adr'] += 1
        elif 'type:architecture:component' in data['labels']:
            report['summary']['total_arc_c'] += 1
        elif 'type:test' in data['labels']:
            report['summary']['total_test'] += 1
    
    # Calculate coverage percentages
    complete_forward = 0
    complete_backward = 0
    
    # Forward: StR → REQ → ADR/ARC-C → PR → TEST
    for issue_num, data in graph.items():
        if 'type:stakeholder-requirement' in data['labels']:
            # Check if has complete forward chain
            has_req = any(i for i in graph.values() 
                         if issue_num in i['traceability']['traces_to'])
            if has_req:
                # Simplified check - in production, check full chain
                complete_forward += 1
    
    report['forward_trace_coverage'] = (complete_forward / report['summary']['total_str'] * 100) \
        if report['summary']['total_str'] > 0 else 0
    
    # Identify critical gaps
    if orphans['unverified_requirements']:
        for req_num in orphans['unverified_requirements']:
            req_data = graph[req_num]
            if 'priority:critical' in req_data['labels'] or 'priority:high' in req_data['labels']:
                report['critical_gaps'].append({
                    'issue': req_num,
                    'type': 'Untested Requirement',
                    'severity': 'CRITICAL',
                    'title': req_data['title']
                })
    
    return report

# Generate report
report = generate_traceability_report(trace_graph, orphans)

# Print summary
print(f"""
Traceability Report Summary
===========================
Total StR Issues: {report['summary']['total_str']}
Total REQ Issues: {report['summary']['total_req']}
Total ADR Issues: {report['summary']['total_adr']}
Total ARC-C Issues: {report['summary']['total_arc_c']}
Total TEST Issues: {report['summary']['total_test']}

Forward Traceability: {report['forward_trace_coverage']:.1f}%
Backward Traceability: {report['backward_trace_coverage']:.1f}%

Critical Gaps: {len(report['critical_gaps'])}
""")
```

**Reference Script**: Use `scripts/github-traceability-report.py` to generate full HTML/Markdown reports.

## 📊 Comprehensive Traceability Report Template

```markdown
# GitHub Issues Traceability Report

**Project**: [Repository Name]
**Date**: [Report Date]
**Report Generated By**: GitHub Copilot Traceability Agent
**Standards**: ISO/IEC/IEEE 29148:2018, IEEE 1012-2016

## Executive Summary

### Issue Inventory
- **Stakeholder Requirements (StR)**: [N] issues
- **Functional Requirements (REQ-F)**: [N] issues
- **Non-Functional Requirements (REQ-NF)**: [N] issues
- **Architecture Decisions (ADR)**: [N] issues
- **Architecture Components (ARC-C)**: [N] issues
- **Test Cases (TEST)**: [N] issues
- **Pull Requests with Traceability**: [N] PRs

### Traceability Scores
- **Forward Traceability** (StR → REQ → ADR/ARC-C → PR → TEST): **[X]%**
- **Backward Traceability** (TEST → PR → ADR/ARC-C → REQ → StR): **[Y]%**
- **Overall Traceability Health**: **[Z]%** (target: 95%+)

### Critical Findings
✅ **Strong Areas**:
- [List areas with good traceability]

⚠️ **Improvement Areas**:
- [List areas needing work]

🔴 **Critical Gaps**:
- [List critical missing links - untested requirements, orphaned code, etc.]

## Detailed Analysis

### 1. Forward Traceability Matrix

[Generated from `build_forward_trace()` function]

### 2. Backward Traceability Matrix

[Generated from `build_backward_trace()` function]

### 3. Orphaned Artifacts

[Generated from `find_orphans()` function]

### 4. Impact Analysis Examples

[Generated from `analyze_impact()` for high-change-risk issues]

## Recommendations

### Immediate Actions (This Week)
1. [Specific orphan to address]
2. [Critical untested requirement]
3. [Security/financial risk issue]

### Short-Term (This Sprint)
1. [Create missing links]
2. [Implement missing tests]
3. [Update architecture decisions]

### Long-Term (Next Quarter)
1. Automate traceability checking in CI/CD
2. Achieve 95%+ forward/backward traceability
3. Zero critical orphaned code

## Automation & Tools

### Scripts Used
- `scripts/github-traceability-report.py` - Generate this report
- `scripts/github-orphan-check.py` - Detect orphaned artifacts
- GitHub Issues API - Query traceability links
- GitHub MCP Server (Copilot) - Interactive issue queries

### CI/CD Integration
```yaml
# .github/workflows/traceability.yml
name: Traceability Check
on: [push, pull_request]

jobs:
  traceability:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Check Traceability
        run: |
          python scripts/github-traceability-report.py --check
          python scripts/github-orphan-check.py --fail-on-critical
```

## Compliance Status

**ISO/IEC/IEEE 29148:2018 Requirements**:
- § 6.4.3.1 Forward Traceability: [Status]
- § 6.4.3.2 Backward Traceability: [Status]
- § 6.4.3.3 Bidirectional Traceability: [Status]

**Overall Compliance**: [COMPLIANT / NON-COMPLIANT]
```

## 🚀 Usage Examples

### Full Repository Traceability Analysis

```
/traceability-builder.prompt.md Analyze complete project traceability using GitHub Issues.

Query all issues (StR, REQ, ADR, ARC-C, TEST) and extract traceability links.
Build forward and backward traceability matrices.
Identify orphaned requirements, architecture, tests, and code.
Generate comprehensive traceability report with recommendations.
```

### Specific Requirement Chain Analysis

```
/traceability-builder.prompt.md Trace issue #10 (REQ-F-USER-001) through all artifacts.

Show complete traceability chain:
- Parent StR issue
- Child architecture issues (ADR/ARC-C)
- Implementing PRs
- Verifying TEST issues

Include bidirectional links and impact analysis.
```

### Orphan Detection

```
/traceability-builder.prompt.md Find all orphaned issues and code.

Query GitHub Issues to find:
- Requirements without parent StR (#N links)
- Architecture without requirements (ADR/ARC-C)
- Tests without requirements (TEST issues)
- Code without @implements #N annotations (scan src/)

Prioritize by risk (critical: payments/auth/security).
```

### Impact Analysis for Planned Change

```
/traceability-builder.prompt.md Analyze impact of changing issue #10 (REQ-F-USER-001).

Show all issues that:
- Trace to #10 (child requirements)
- Satisfy #10 (architecture)
- Implement #10 (PRs)
- Verify #10 (tests)
- Depend on #10 (transitive dependencies)

Provide risk assessment and change checklist.
```

## 🔧 Best Practices for GitHub Issues Traceability

### 1. Create Issues Before Code

**Workflow**:
```
1. Stakeholder discusses need → Create StR issue (#1)
2. Analyze requirement → Create REQ-F issue (#10) with "Traces to: #1"
3. Design solution → Create ADR issue (#20) with "Satisfies: #10"
4. Implement → Create PR with "Fixes #10, Implements #20"
5. Test → Create TEST issue (#40) with "Verifies: #10"
```

### 2. Use Consistent Issue Linking Syntax

**In Issue Bodies**:
```markdown
## Traceability
- **Traces to**: #1, #2 (parent issues)
- **Depends on**: #5 (prerequisites)
- **Verified by**: #40, #41 (test issues)
- **Implemented by**: PR #100
```

**In PR Descriptions**:
```markdown
Fixes #10
Implements #20
Part of #1

## Traceability
- **Requirements**: #10 (REQ-F-USER-001)
- **Architecture**: #20 (ADR-AUTH-001)
- **Tests**: #40, #41
```

**In Code**:
```python
"""
User authentication service.

Implements: #10 (REQ-F-USER-001: User Login)
Architecture: #20 (ADR-AUTH-001: JWT Authentication)
Verified by: #40 (TEST-AUTH-LOGIN-001)

See: https://github.com/owner/repo/issues/10
"""
```

### 3. Use Labels for Lifecycle Phases

```
type:stakeholder-requirement
type:requirement:functional
type:requirement:non-functional
type:architecture:decision
type:architecture:component
type:test
priority:critical
priority:high
priority:medium
priority:low
phase:01-stakeholder
phase:02-requirements
phase:03-architecture
phase:05-implementation
phase:07-verification
```

### 4. Automate Traceability Checks in CI/CD

```yaml
# .github/workflows/traceability-check.yml
name: Traceability Validation
on: 
  pull_request:
    types: [opened, synchronize]

jobs:
  check-traceability:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      
      - name: Verify PR has issue links
        run: |
          if ! grep -E "(Fixes|Implements|Part of) #[0-9]+" <<< "${{ github.event.pull_request.body }}"; then
            echo "❌ PR must link to issues using 'Fixes #N' or 'Implements #N'"
            exit 1
          fi
      
      - name: Check code has @implements annotations
        run: |
          # Find modified code files
          git diff --name-only origin/main | grep -E '\.(py|js|ts)$' | \
            while read file; do
              if ! grep -q "@implements\|Implements:" "$file"; then
                echo "❌ $file missing @implements #N annotation"
                exit 1
              fi
            done
      
      - name: Run orphan check
        run: |
          python scripts/github-orphan-check.py --pr-check
```

### 5. Regular Traceability Audits

**Monthly/Quarterly**:
```bash
# Generate full traceability report
python scripts/github-traceability-report.py --output report.html

# Check for new orphans
python scripts/github-orphan-check.py --detailed

# Analyze high-change-risk issues
python scripts/github-traceability-report.py --impact-analysis --threshold 5
```

## 📚 References

- **Scripts**: 
  - `scripts/github-traceability-report.py` - Generate traceability reports
  - `scripts/github-orphan-check.py` - Detect orphaned artifacts
- **Phase Instructions**:
  - `.github/instructions/phase-02-requirements.instructions.md` - REQ issue guidance
  - `.github/instructions/phase-03-architecture.instructions.md` - ADR/ARC-C guidance
  - `.github/instructions/phase-05-implementation.instructions.md` - PR workflow
  - `.github/instructions/phase-07-verification-validation.instructions.md` - TEST issue guidance
- **GitHub Docs**:
  - [Linking Issues and Pull Requests](https://docs.github.com/en/issues/tracking-your-work-with-issues/linking-a-pull-request-to-an-issue)
  - [GitHub Issues API](https://docs.github.com/en/rest/issues)
- **Standards**:
  - ISO/IEC/IEEE 29148:2018 - Requirements engineering
  - IEEE 1012-2016 - Verification and validation

---

**Every requirement connected via GitHub Issues!** 🔗
````
