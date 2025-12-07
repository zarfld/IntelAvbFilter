````prompt
---
mode: agent
applyTo:
  - "**/src/**/*"
  - "**/lib/**/*"
  - "**/app/**/*"
  - "**/tests/**/*"
  - "**/test/**/*"
  - "**/__tests__/**/*"
---

# Test Gap Filler Prompt (GitHub Issues)

You are a **Test Engineer** following **TDD principles** and **IEEE 1012-2016** Verification & Validation standards.

## 🎯 Core Workflow: GitHub Issues for Test Traceability

**ALL test artifacts are tracked as GitHub Issues:**
- **REQ Issues**: Requirements with labels `type:requirement:functional` or `type:requirement:non-functional`
- **TEST Issues**: Test specifications with label `type:test`, linking to requirements via `Verifies: #N`
- **Test Code**: Implementation files with `Verifies: #N` in docstrings

**This prompt generates:**
1. **TEST Issues**: For untested requirements (create issue bodies)
2. **Test Code**: With traceability comments (`Verifies: #N`)
3. **Coverage Reports**: Showing REQ → TEST → Code chain
4. **Gap Analysis**: Requirements missing TEST issues or implementing code

---

## 📊 Step 1: Identify Untested Requirements

### Query GitHub Issues for Requirement Coverage

```bash
# Find all requirements
gh issue list --label "type:requirement:functional" --state all --json number,title,labels > requirements.json
gh issue list --label "type:requirement:non-functional" --state all --json number,title,labels >> requirements.json

# Find all TEST issues
gh issue list --label "type:test" --state all --json number,title,body > tests.json

# Analyze which requirements lack TEST issues
python scripts/find-untested-requirements.py
```

### Python Script: `scripts/find-untested-requirements.py`

```python
#!/usr/bin/env python3
"""
Find requirements without TEST issues

Scans requirement issues and checks for TEST issues with "Verifies: #N" links.
Generates report and TEST issue bodies for untested requirements.
"""

import json
import re
import os
from github import Github

def load_requirements():
    """Load all requirement issues from GitHub"""
    token = os.getenv('GITHUB_TOKEN')
    repo_name = os.getenv('GITHUB_REPOSITORY', 'zarfld/copilot-instructions-template')
    
    g = Github(token)
    repo = g.get_repo(repo_name)
    
    req_issues = []
    for label in ['type:requirement:functional', 'type:requirement:non-functional']:
        issues = repo.get_issues(labels=[label], state='all')
        for issue in issues:
            req_issues.append({
                'number': issue.number,
                'title': issue.title,
                'body': issue.body or '',
                'labels': [l.name for l in issue.labels],
                'priority': next((l.name for l in issue.labels if 'priority:' in l.name), 'priority:p2')
            })
    
    return req_issues

def load_test_issues():
    """Load all TEST issues and extract 'Verifies: #N' links"""
    token = os.getenv('GITHUB_TOKEN')
    repo_name = os.getenv('GITHUB_REPOSITORY', 'zarfld/copilot-instructions-template')
    
    g = Github(token)
    repo = g.get_repo(repo_name)
    
    test_issues = []
    issues = repo.get_issues(labels=['type:test'], state='all')
    for issue in issues:
        body = issue.body or ''
        
        # Extract "Verifies: #N" references
        verifies = re.findall(r'Verifies:\s*#(\d+)', body, re.IGNORECASE)
        
        test_issues.append({
            'number': issue.number,
            'title': issue.title,
            'verifies': [int(n) for n in verifies]
        })
    
    return test_issues

def find_untested_requirements(req_issues, test_issues):
    """Identify requirements without TEST issues"""
    
    # Build mapping: requirement number → list of TEST issues
    req_to_tests = {}
    for test in test_issues:
        for req_num in test['verifies']:
            if req_num not in req_to_tests:
                req_to_tests[req_num] = []
            req_to_tests[req_num].append(test['number'])
    
    # Find untested requirements
    untested = []
    for req in req_issues:
        if req['number'] not in req_to_tests:
            untested.append(req)
    
    return untested, req_to_tests

def generate_test_issue_body(req_issue):
    """Generate TEST issue body for untested requirement"""
    
    req_num = req_issue['number']
    req_title = req_issue['title']
    req_body = req_issue['body']
    
    # Extract requirement ID from title (e.g., "REQ-F-AUTH-001")
    req_id_match = re.search(r'(REQ-[FN][A-Z]?-[A-Z]+-\d+)', req_title)
    req_id = req_id_match.group(1) if req_id_match else 'REQ-UNK-001'
    
    # Convert REQ-F-AUTH-001 → TEST-AUTH-001
    test_id = req_id.replace('REQ-F-', 'TEST-').replace('REQ-NF-', 'TEST-')
    
    # Extract acceptance criteria from requirement body
    ac_match = re.search(r'## Acceptance Criteria\s*\n(.*?)(?=\n##|\Z)', req_body, re.DOTALL | re.IGNORECASE)
    acceptance_criteria = ac_match.group(1).strip() if ac_match else 'TBD'
    
    # Determine priority from requirement
    priority = req_issue.get('priority', 'priority:p2')
    
    # Generate TEST issue body
    test_issue_body = f"""## Description
Test suite for requirement #{req_num}: {req_title}

## Verifies Requirements
- **Verifies**: #{req_num} ({req_id}: {req_title})

## Test Scenarios

### Unit Tests
1. Happy path - valid inputs produce expected outputs
2. Invalid inputs - proper error handling and validation
3. Boundary conditions - min/max values, edge cases
4. Error scenarios - exception handling and recovery

### Integration Tests (if applicable)
1. End-to-end workflow - complete user journey
2. External service integration - API calls, database operations
3. Transaction handling - rollback scenarios

### Edge Cases
1. Empty/null inputs
2. Special characters (XSS, SQL injection attempts)
3. Concurrent operations
4. Network failures/timeouts

## Acceptance Criteria (from #{req_num})

{acceptance_criteria}

## Test Implementation

**Test File**: `tests/[category]/[module]/[feature].test.[ext]`

**Coverage Target**: >90% of implementing code

**Traceability**: All test functions must include:
```
/**
 * Test [description]
 * 
 * Verifies: #{req_num} ({req_id})
 * TEST Issue: [This issue number]
 */
```

## Definition of Done
- [ ] All test scenarios implemented
- [ ] Tests pass in CI/CD pipeline
- [ ] Code coverage >90% for implementing code
- [ ] Traceability comments added
- [ ] TEST issue updated with file locations
- [ ] Linked to requirement issue #{req_num}
"""
    
    return test_id, test_issue_body

def generate_gap_report(untested_reqs, req_to_tests, all_reqs):
    """Generate markdown report of test coverage gaps"""
    
    total_reqs = len(all_reqs)
    tested_reqs = total_reqs - len(untested_reqs)
    coverage_pct = (tested_reqs / total_reqs * 100) if total_reqs > 0 else 0
    
    # Group by priority
    untested_by_priority = {'priority:p0': [], 'priority:p1': [], 'priority:p2': []}
    for req in untested_reqs:
        priority = req.get('priority', 'priority:p2')
        if priority in untested_by_priority:
            untested_by_priority[priority].append(req)
    
    report = f"""# Test Coverage Gap Analysis

**Date**: {datetime.now().strftime('%Y-%m-%d')}
**Repository**: {os.getenv('GITHUB_REPOSITORY', 'unknown')}

## 📊 Summary

**Requirements Coverage**:
- Total Requirements: {total_reqs}
- Requirements with TEST issues: {tested_reqs} ({coverage_pct:.1f}%)
- **Untested Requirements**: {len(untested_reqs)} ({100-coverage_pct:.1f}%)

**Target**: 100% of P0/P1 requirements tested, 90%+ overall

**Status**: {'✅ PASS' if coverage_pct >= 90 else '🔴 CRITICAL GAPS'}

---

## 🔴 Untested Requirements (CRITICAL)

"""
    
    # P0 requirements
    if untested_by_priority['priority:p0']:
        report += f"### Priority P0 (Blocker) - {len(untested_by_priority['priority:p0'])} requirements\n\n"
        report += "| Issue | Requirement | Action |\n"
        report += "|-------|-------------|--------|\n"
        for req in untested_by_priority['priority:p0']:
            report += f"| #{req['number']} | {req['title'][:60]} | Create TEST issue |\n"
        report += "\n"
    
    # P1 requirements
    if untested_by_priority['priority:p1']:
        report += f"### Priority P1 (High) - {len(untested_by_priority['priority:p1'])} requirements\n\n"
        report += "| Issue | Requirement | Action |\n"
        report += "|-------|-------------|--------|\n"
        for req in untested_by_priority['priority:p1'][:10]:  # Limit to 10
            report += f"| #{req['number']} | {req['title'][:60]} | Create TEST issue |\n"
        
        if len(untested_by_priority['priority:p1']) > 10:
            report += f"\n... and {len(untested_by_priority['priority:p1']) - 10} more P1 requirements\n"
        report += "\n"
    
    # P2 requirements
    if untested_by_priority['priority:p2']:
        report += f"### Priority P2 (Medium) - {len(untested_by_priority['priority:p2'])} requirements\n\n"
        report += f"Total: {len(untested_by_priority['priority:p2'])} requirements\n"
        report += "(Expand this section for details)\n\n"
    
    report += """---

## ✅ Actions Required

### 1. Create TEST Issues

For each untested requirement, create a TEST issue using:

```bash
# Use GitHub CLI
gh issue create --label "type:test,phase:07-verification-validation,priority:p0" \\
  --title "TEST-[MODULE]-001: [Feature] Tests" \\
  --body-file test-issue-body.md
```

Or use the issue template: `.github/ISSUE_TEMPLATE/test-case.md`

### 2. Implement Test Code

After creating TEST issues, implement test files with traceability:

```typescript
/**
 * Test user login functionality
 * 
 * Verifies: #25 (REQ-F-AUTH-001: User Login)
 * TEST Issue: #50 (TEST-AUTH-001)
 */
describe('User Login (Verifies #25)', () => {
  it('should authenticate user with valid credentials', () => {
    // Test implementation
  });
});
```

### 3. Link TEST Issues to Requirements

In the TEST issue body, include:

```markdown
## Verifies Requirements
- **Verifies**: #25 (REQ-F-AUTH-001: User Login)
```

In the requirement issue (#25), add comment:

```markdown
## Verified By
- #50 (TEST-AUTH-001: User Login Tests)
```

---

## 📋 Generated TEST Issue Bodies

Below are generated TEST issue bodies for untested requirements. Copy and paste when creating issues.

"""
    
    # Generate issue bodies for first 5 untested requirements
    for i, req in enumerate(untested_reqs[:5]):
        test_id, test_body = generate_test_issue_body(req)
        
        report += f"""### {i+1}. TEST Issue for Requirement #{req['number']}

**Title**: `{test_id}: {req['title'].replace('REQ-F-', '').replace('REQ-NF-', '')} Tests`

**Labels**: `type:test`, `phase:07-verification-validation`, `{req.get('priority', 'priority:p2')}`

**Body**:
```markdown
{test_body}
```

---

"""
    
    if len(untested_reqs) > 5:
        report += f"\n... and {len(untested_reqs) - 5} more TEST issue bodies (run script to generate all)\n"
    
    return report

if __name__ == '__main__':
    from datetime import datetime
    
    print("Loading requirements...")
    req_issues = load_requirements()
    
    print("Loading TEST issues...")
    test_issues = load_test_issues()
    
    print("Analyzing coverage gaps...")
    untested_reqs, req_to_tests = find_untested_requirements(req_issues, test_issues)
    
    print("Generating report...")
    report = generate_gap_report(untested_reqs, req_to_tests, req_issues)
    
    # Write report
    with open('test-coverage-gap-report.md', 'w') as f:
        f.write(report)
    
    print(f"✅ Report generated: test-coverage-gap-report.md")
    print(f"Found {len(untested_reqs)} untested requirements out of {len(req_issues)} total")
    
    # Exit with error if critical gaps
    if untested_reqs:
        print(f"⚠️ Action required: Create TEST issues for {len(untested_reqs)} requirements")
        exit(1)
    else:
        print("✅ All requirements have TEST issues!")
        exit(0)
```

---

## 📝 Step 2: Generate Test Code with Traceability

### Test Code Template (TypeScript/JavaScript)

```typescript
/**
 * Test suite for user authentication
 * 
 * Verifies: #25 (REQ-F-AUTH-001: User Login)
 * TEST Issue: #50 (TEST-AUTH-001)
 * 
 * Acceptance Criteria (from #25):
 *   Given user has valid credentials
 *   When user submits login form
 *   Then user is authenticated and redirected to dashboard
 */
describe('User Login (Verifies #25)', () => {
  
  beforeEach(() => {
    jest.clearAllMocks();
  });

  /**
   * TEST-AUTH-001-01: Successful login with valid credentials
   * Verifies: #25 (REQ-F-AUTH-001)
   */
  it('should authenticate user with valid credentials', async () => {
    // ARRANGE
    const credentials = {
      email: 'user@test.com',
      password: 'ValidPass123!'
    };
    
    const expectedToken = 'jwt-token-12345';
    authService.login.mockResolvedValue({ token: expectedToken, userId: 1 });
    
    // ACT
    const result = await login(credentials);
    
    // ASSERT
    expect(result).toMatchObject({
      authenticated: true,
      token: expectedToken,
      userId: 1
    });
    expect(authService.login).toHaveBeenCalledWith(credentials);
  });

  /**
   * TEST-AUTH-001-02: Failed login with invalid password
   * Verifies: #25 (REQ-F-AUTH-001) - error handling
   */
  it('should reject login with invalid password', async () => {
    // ARRANGE
    const credentials = {
      email: 'user@test.com',
      password: 'WrongPassword'
    };
    
    authService.login.mockRejectedValue(new AuthError('Invalid credentials'));
    
    // ACT & ASSERT
    await expect(login(credentials)).rejects.toThrow('Invalid credentials');
    expect(authService.login).toHaveBeenCalledWith(credentials);
  });

  /**
   * TEST-AUTH-001-03: Account lockout after failed attempts
   * Verifies: #26 (REQ-NF-SECU-001: Account lockout policy)
   */
  it('should lock account after 5 failed login attempts', async () => {
    // ARRANGE
    const credentials = { email: 'user@test.com', password: 'WrongPass' };
    
    // ACT - Simulate 5 failed attempts
    for (let i = 0; i < 5; i++) {
      await login(credentials).catch(() => {});
    }
    
    // ASSERT
    const account = await Account.findByEmail(credentials.email);
    expect(account.locked).toBe(true);
    expect(account.lockedUntil).toBeInstanceOf(Date);
  });
});
```

### Test Code Template (Python)

```python
"""
Test suite for order discount calculation

Verifies: #30 (REQ-F-ORD-004: Discount calculation)
TEST Issue: #60 (TEST-ORD-004)

Acceptance Criteria (from #30):
  Given customer has valid discount code
  When order subtotal meets minimum requirement
  Then discount is applied according to code type and customer tier
"""

import pytest
from unittest.mock import Mock, patch
from order_service import calculate_discount
from exceptions import InvalidDiscountError, ExpiredDiscountError

class TestCalculateDiscount:
    """
    Test discount calculation functionality
    
    Verifies: #30 (REQ-F-ORD-004)
    TEST Issue: #60 (TEST-ORD-004)
    """
    
    def setup_method(self):
        """Set up test fixtures"""
        self.mock_discount = Mock()
        self.mock_discount.minimum_order = 50.0
        self.mock_discount.max_discount = None
        
    @patch('order_service.DiscountCode.get_by_code')
    def test_percentage_discount_calculation(self, mock_get_code):
        """
        TEST-ORD-004-01: Calculate percentage discount
        
        Verifies: #30 (REQ-F-ORD-004)
        """
        # ARRANGE
        self.mock_discount.is_active.return_value = True
        self.mock_discount.type = 'percentage'
        self.mock_discount.value = 10.0  # 10%
        mock_get_code.return_value = self.mock_discount
        
        # ACT
        result = calculate_discount(100.0, 'SAVE10', 'bronze')
        
        # ASSERT
        assert result == 10.0  # 10% of $100
        
    @patch('order_service.DiscountCode.get_by_code')
    def test_invalid_discount_code(self, mock_get_code):
        """
        TEST-ORD-004-02: Reject invalid discount code
        
        Verifies: #30 (REQ-F-ORD-004) - error handling
        """
        # ARRANGE
        mock_get_code.return_value = None
        
        # ACT & ASSERT
        with pytest.raises(InvalidDiscountError) as exc_info:
            calculate_discount(100.0, 'INVALID', 'bronze')
        
        assert "Invalid discount code: INVALID" in str(exc_info.value)
    
    @pytest.mark.parametrize("tier,multiplier,expected", [
        ('bronze', 1.0, 10.0),
        ('silver', 1.1, 11.0),
        ('gold', 1.2, 12.0),
        ('platinum', 1.5, 15.0),
    ])
    @patch('order_service.DiscountCode.get_by_code')
    def test_customer_tier_multipliers(self, mock_get_code, tier, multiplier, expected):
        """
        TEST-ORD-004-03: Apply tier-based multipliers
        
        Verifies: #31 (REQ-F-ORD-005: Tier-based discounts)
        """
        # ARRANGE
        self.mock_discount.is_active.return_value = True
        self.mock_discount.type = 'percentage'
        self.mock_discount.value = 10.0
        mock_get_code.return_value = self.mock_discount
        
        # ACT
        result = calculate_discount(100.0, 'SAVE10', tier)
        
        # ASSERT
        assert result == expected
```

---

## 🚀 Step 3: CI/CD Workflow for Test Gap Detection

### GitHub Actions: `.github/workflows/test-gap-detection.yml`

```yaml
name: Test Gap Detection

on:
  pull_request:
    types: [opened, synchronize]
  schedule:
    - cron: '0 6 * * 1'  # Weekly on Monday at 6 AM

jobs:
  detect-test-gaps:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      
      - name: Setup Python
        uses: actions/setup-python@v5
        with:
          python-version: '3.11'
      
      - name: Install dependencies
        run: |
          pip install PyGithub
      
      - name: Find untested requirements
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
        run: |
          python scripts/find-untested-requirements.py
      
      - name: Post gap report to PR
        if: github.event_name == 'pull_request'
        uses: actions/github-script@v7
        with:
          script: |
            const fs = require('fs');
            const report = fs.readFileSync('test-coverage-gap-report.md', 'utf8');
            
            github.rest.issues.createComment({
              owner: context.repo.owner,
              repo: context.repo.repo,
              issue_number: context.issue.number,
              body: `## Test Coverage Gap Analysis\n\n${report}`
            });
      
      - name: Fail if critical gaps exist
        run: |
          UNTESTED_P0=$(cat test-coverage-gap-report.md | grep -c "priority:p0" || true)
          
          if [ "$UNTESTED_P0" -gt 0 ]; then
            echo "❌ Found $UNTESTED_P0 untested P0 requirements"
            exit 1
          fi
          
          echo "✅ All P0 requirements have TEST issues"
```

---

## ✅ Test Quality Checklist

**Every test MUST**:
- [ ] Include `Verifies: #N` in docstring/describe block
- [ ] Follow AAA pattern (Arrange-Act-Assert)
- [ ] Use descriptive names (what/when/expected)
- [ ] Test happy path + error paths + edge cases
- [ ] Use specific assertions (not just truthy/falsy)
- [ ] Be independent (can run in any order)
- [ ] Clean up after itself (no test pollution)

**Every TEST issue MUST**:
- [ ] Link to requirement via `Verifies: #N`
- [ ] List test scenarios (unit/integration/edge cases)
- [ ] Specify test file location
- [ ] Define coverage target (e.g., >90%)
- [ ] Include acceptance criteria from requirement

---

## 🎯 Usage

### Find Untested Requirements

```bash
# Run gap analysis
python scripts/find-untested-requirements.py

# View report
cat test-coverage-gap-report.md
```

### Create TEST Issue for Untested Requirement

```bash
# Generate issue body
python scripts/find-untested-requirements.py --generate-issue-body --req-number 25

# Create issue via GitHub CLI
gh issue create --label "type:test,phase:07-verification-validation,priority:p0" \
  --title "TEST-AUTH-001: User Login Tests" \
  --body-file test-issue-body.md
```

### Generate Test Code with Traceability

```bash
# Copilot Chat
@workspace Generate tests for requirement #25 with traceability

# Include in test file:
# - Verifies: #25 in docstring
# - TEST Issue reference
# - Acceptance criteria from #25
# - AAA pattern
```

---

**Remember**: Every requirement MUST have TEST issues! Use GitHub Issues for traceability. 🧪
````