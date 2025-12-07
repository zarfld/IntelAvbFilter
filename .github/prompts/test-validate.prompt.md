````prompt
---
mode: agent
applyTo:
  - "**/test/**"
  - "**/tests/**"
  - "**/07-verification-validation/**"
  - "**/*test*"
---

# Test Validation Prompt (GitHub Issues)

You are a verification and validation (V&V) specialist enforcing **IEEE 1012-2016 - System, Software, and Hardware Verification and Validation** standards.

## 🎯 Core Workflow: GitHub Issues for Test Traceability

**ALL test artifacts are tracked as GitHub Issues:**
- **TEST Issues**: `type:test`, `phase:07-verification-validation`, `priority:p0/p1/p2`
- **Test Code**: Includes `Verifies: #N` in docstrings linking to requirement issues
- **PR Tests**: Include `Verifies: #N` comments showing which requirements are tested
- **Traceability**: TEST issues link to REQ issues via `Verifies: #N`, code tests reference both

**This prompt validates:**
1. **Requirements → TEST Issues**: Every REQ has TEST issue(s)
2. **TEST Issues → Test Code**: Every TEST issue has implementing test files/functions
3. **Test Code → Requirements**: Test docstrings include `Verifies: #N` or `@implements #N`
4. **Code Coverage**: Test files cover sufficient code paths
5. **Test Quality**: Tests follow AAA pattern, proper assertions, edge cases

---

## 📤 EXPECTED OUTPUT (ALWAYS DELIVER)

When validating tests, you **MUST** produce this report:

```markdown
# Test Validation Report (IEEE 1012-2016)

**Repository**: [owner/repo]
**Date**: [YYYY-MM-DD]
**Validator**: GitHub Copilot (IEEE 1012-2016)
**Scope**: [All tests / Phase / Sprint]

---

## 📊 Executive Summary

**Traceability Status**:
- Total Requirements (REQ): [N]
- Total TEST Issues: [N]
- Total Test Files: [N]
- Total Test Functions: [N]

**Coverage Metrics**:
- Requirements with TEST issues: [N] ([X]%) - Target: 100%
- TEST issues with implementing code: [N] ([X]%) - Target: 100%
- Test functions with `Verifies: #N`: [N] ([X]%) - Target: ≥80%
- Code coverage (lines): [X]% - Target: ≥80%
- Code coverage (branches): [X]% - Target: ≥75%

**Quality Metrics**:
- Tests following AAA pattern: [X]%
- Tests with proper assertions: [X]%
- Tests covering edge cases: [X]%
- Flaky tests: [N] (Target: 0)

**Overall Status**: ✅ PASS / ⚠️ NEEDS WORK / 🔴 CRITICAL GAPS

---

## 🔴 Critical Issues (Blockers)

### 1. Untested Requirements

**Requirements without TEST issues**:

| Requirement | Title | Priority | Status |
|-------------|-------|----------|--------|
| #25 | REQ-F-AUTH-001: User Login | P0 | 🔴 No TEST issue |
| #30 | REQ-F-PAY-001: Payment Processing | P0 | 🔴 No TEST issue |

**Action**:
1. Create TEST issues for each untested requirement
2. Use template: `.github/ISSUE_TEMPLATE/test-case.md`
3. Link to requirement: `Verifies: #25`

**Example Issue to Create**:
```markdown
Title: TEST-AUTH-001: User Login Tests
Labels: type:test, phase:07-verification-validation, priority:p0

## Description
Test suite for user authentication functionality.

## Verifies Requirements
- **Verifies**: #25 (REQ-F-AUTH-001: User Login)

## Test Scenarios
1. Successful login with valid credentials
2. Failed login with invalid password
3. Account lockout after 5 failed attempts
4. Session expiry after 30 minutes

## Acceptance Criteria (from #25)
- Given user has valid credentials
- When user submits login form
- Then user is authenticated and redirected to dashboard

## Test Implementation
- **File**: `tests/integration/auth/login.test.ts`
- **Functions**: `testSuccessfulLogin()`, `testFailedLogin()`, `testAccountLockout()`
- **Coverage Target**: >90% of auth service code
```

### 2. TEST Issues Without Implementing Code

**TEST issues missing test files**:

| TEST Issue | Requirement | Status |
|------------|-------------|--------|
| #50 | TEST-AUTH-002: Password Reset | #28 (REQ-F-AUTH-003) | 🔴 No test file found |
| #55 | TEST-PAY-005: Refund Processing | #40 (REQ-F-PAY-005) | 🔴 No test file found |

**Action**:
1. Create test files: `tests/integration/auth/password-reset.test.ts`
2. Implement test functions with `Verifies: #28` in docstrings
3. Update TEST issue with file location
4. Run tests and update issue with results

### 3. Test Code Without Traceability

**Test files missing `Verifies: #N` references**:

| File | Functions | Status |
|------|-----------|--------|
| `tests/unit/utils/format.test.ts` | 12 tests | ⚠️ No traceability comments |
| `tests/integration/api/health.test.ts` | 3 tests | ⚠️ No `Verifies: #N` |

**Action**: Add traceability to test docstrings:

```typescript
/**
 * Test user login with valid credentials
 * 
 * Verifies: #25 (REQ-F-AUTH-001: User Login)
 * TEST Issue: #50 (TEST-AUTH-001)
 * 
 * Acceptance Criteria (from #25):
 *   Given user has valid credentials
 *   When user submits login form
 *   Then user is authenticated
 */
describe('User Login (Verifies #25)', () => {
  it('should authenticate user with valid credentials', async () => {
    // Test implementation
  });
});
```

---

## ⚠️ Warnings (Needs Improvement)

### Low Code Coverage Modules

| Module | Lines | Branches | Status |
|--------|-------|----------|--------|
| `src/auth/permissions.ts` | 45% | 38% | 🔴 Critical (security code!) |
| `src/payment/refund.ts` | 62% | 55% | ⚠️ Below target |
| `src/user/profile.ts` | 72% | 68% | ⚠️ Below target |

**Action**: Add tests targeting uncovered code paths

### Flaky Tests

**Tests failing intermittently**:

| Test | File | Failure Rate | Reason |
|------|------|--------------|--------|
| `testConcurrentLogins` | `auth/login.test.ts` | 15% | Race condition |
| `testPaymentTimeout` | `payment/stripe.test.ts` | 8% | Network dependency |

**Action**: Fix race conditions, add proper mocking

---

## ✅ Good Coverage Areas

**Well-tested modules** (>90% coverage):

- ✅ `src/auth/login.ts` - 95% lines, 92% branches
- ✅ `src/user/registration.ts` - 98% lines, 95% branches
- ✅ `src/payment/charge.ts` - 93% lines, 88% branches

---

## 📋 Detailed Traceability Matrix

### Requirements → TEST Issues → Test Code

| REQ Issue | TEST Issue | Test Files | Coverage | Status |
|-----------|------------|------------|----------|--------|
| #25 (REQ-F-AUTH-001) | #50 (TEST-AUTH-001) | `tests/integration/auth/login.test.ts` | 95% | ✅ |
| #26 (REQ-F-AUTH-002) | #51 (TEST-AUTH-002) | - | 0% | 🔴 No tests |
| #30 (REQ-F-PAY-001) | #60 (TEST-PAY-001) | `tests/integration/payment/charge.test.ts` | 88% | ✅ |
| #35 (REQ-NF-PERF-001) | #70 (TEST-PERF-001) | `tests/performance/load-test.jmx` | N/A | ✅ |

**Summary**:
- ✅ Complete traceability: 45 requirements (90%)
- ⚠️ Partial (TEST issue exists, no code): 3 requirements (6%)
- 🔴 Missing TEST issues: 2 requirements (4%)

---

## 🔍 Test Quality Analysis

### AAA Pattern Compliance

**Arrange-Act-Assert structure**:
- ✅ Following AAA: 180 tests (85%)
- ⚠️ Missing AAA: 32 tests (15%)

**Example of non-compliant test**:
```typescript
// ❌ BAD: No clear AAA structure
it('test login', () => {
  const result = login('user@test.com', 'pass');
  expect(result.authenticated).toBe(true);
});
```

**Should be**:
```typescript
// ✅ GOOD: Clear AAA structure
it('should authenticate user with valid credentials', () => {
  // ARRANGE
  const email = 'user@test.com';
  const password = 'ValidPass123!';
  const expectedToken = expect.any(String);
  
  // ACT
  const result = login(email, password);
  
  // ASSERT
  expect(result).toMatchObject({
    authenticated: true,
    token: expectedToken,
    expiresIn: 3600
  });
});
```

### Assertion Quality

**Specific vs vague assertions**:
- ✅ Specific assertions: 165 tests (78%)
- ⚠️ Vague assertions (truthy/falsy): 47 tests (22%)

**Examples of vague assertions to fix**:
```typescript
// ❌ BAD: Too vague
expect(result).toBeTruthy();
expect(error).toBeDefined();

// ✅ GOOD: Specific
expect(result).toMatchObject({ authenticated: true, token: expect.any(String) });
expect(error).toMatchObject({ message: 'Invalid credentials', statusCode: 401 });
```

### Edge Case Coverage

**Boundary and edge case testing**:
- ✅ Edge cases tested: 120 scenarios (60%)
- ⚠️ Missing edge cases: 80 scenarios (40%)

**Common missing edge cases**:
- Empty string inputs
- Null/undefined inputs
- Maximum length inputs
- Special characters (XSS, SQL injection)
- Concurrent operations
- Network failures/timeouts

---

## 🎯 Action Items by Priority

### P0 - CRITICAL (Block Release)

- [ ] Create TEST issues for #25, #30 (untested P0 requirements)
- [ ] Implement tests for #50, #55 (TEST issues without code)
- [ ] Fix security: Add tests for `src/auth/permissions.ts` (45% coverage)
- [ ] Fix flaky tests: `testConcurrentLogins`, `testPaymentTimeout`

**Estimated Effort**: 2 days

### P1 - HIGH (Complete This Sprint)

- [ ] Add traceability comments to 15 test files without `Verifies: #N`
- [ ] Increase coverage for `src/payment/refund.ts` to >80%
- [ ] Add edge case tests for 20 functions
- [ ] Fix 32 tests not following AAA pattern

**Estimated Effort**: 3 days

### P2 - MEDIUM (Before Next Release)

- [ ] Improve assertion specificity in 47 tests
- [ ] Add performance tests for all REQ-NF-PERF requirements
- [ ] Add security tests (OWASP Top 10 coverage)
- [ ] Create test data factory pattern

**Estimated Effort**: 5 days

---

## ✅ Exit Criteria for Release

| Criterion | Current | Target | Status |
|-----------|---------|--------|--------|
| Requirements with TEST issues | 96% | 100% | ⚠️ |
| TEST issues with implementing code | 94% | 100% | ⚠️ |
| Code coverage (lines) | 87% | ≥80% | ✅ |
| Code coverage (branches) | 81% | ≥75% | ✅ |
| P0/P1 requirements tested | 95% | 100% | ⚠️ |
| Flaky tests | 2 | 0 | 🔴 |
| Security-critical code coverage | 45% | 100% | 🔴 |

**Release Readiness**: 🔴 NOT READY (2 critical criteria not met)

**Recommendation**: Address P0 items before release (2 days effort).

---

## 📊 Test Metrics Dashboard

### Test Distribution

```
Total Tests: 212

By Type:
- Unit Tests: 145 (68%) ✅
- Integration Tests: 45 (21%) ✅
- System/E2E Tests: 15 (7%) ⚠️ (target: 10%+)
- Performance Tests: 5 (2%) ⚠️ (target: 5%+)
- Security Tests: 2 (1%) 🔴 (target: 5%+)

By Status:
- Passing: 208 (98%) ✅
- Failing: 2 (1%) 🔴
- Flaky: 2 (1%) 🔴
```

### Code Coverage Trends

| Week | Lines | Branches | Status |
|------|-------|----------|--------|
| W45 | 82% | 76% | ✅ |
| W46 | 85% | 79% | ✅ |
| W47 (current) | 87% | 81% | ✅ |

**Trend**: +2.5%/week 📈 (Good!)

---

## 🔧 Automation Recommendations

### CI/CD: Automated Test Validation

Create `.github/workflows/test-validation.yml`:

\`\`\`yaml
name: Test Validation & Traceability

on:
  pull_request:
    types: [opened, synchronize]
  push:
    branches: [main, master]
  schedule:
    - cron: '0 2 * * *'  # Daily at 2 AM

jobs:
  test-validation:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      
      - name: Setup Python
        uses: actions/setup-python@v5
        with:
          python-version: '3.11'
      
      - name: Install dependencies
        run: |
          pip install requests PyGithub pytest pytest-cov
      
      - name: Run tests with coverage
        run: |
          pytest --cov=src --cov-report=json --cov-report=html
      
      - name: Validate traceability
        env:
          GITHUB_TOKEN: \${{ secrets.GITHUB_TOKEN }}
        run: |
          python scripts/github-test-traceability.py
      
      - name: Check coverage thresholds
        run: |
          python scripts/check-coverage-threshold.py --min-line=80 --min-branch=75
      
      - name: Post validation report
        if: github.event_name == 'pull_request'
        uses: actions/github-script@v7
        with:
          script: |
            const fs = require('fs');
            const report = fs.readFileSync('test-validation-report.md', 'utf8');
            
            github.rest.issues.createComment({
              owner: context.repo.owner,
              repo: context.repo.repo,
              issue_number: context.issue.number,
              body: report
            });
      
      - name: Fail if coverage below threshold
        run: |
          COVERAGE=\$(jq '.totals.percent_covered' coverage.json)
          if (( \$(echo "\$COVERAGE < 80" | bc -l) )); then
            echo "❌ Coverage \$COVERAGE% is below 80% threshold"
            exit 1
          fi
\`\`\`

### Python Script: `scripts/github-test-traceability.py`

\`\`\`python
#!/usr/bin/env python3
"""
GitHub Issues Test Traceability Validator

Validates:
1. Every REQ issue has TEST issue(s)
2. Every TEST issue has test file(s)
3. Test files have Verifies: #N comments
4. Coverage meets thresholds

Usage:
    python scripts/github-test-traceability.py
    
Environment:
    GITHUB_TOKEN - GitHub personal access token
    GITHUB_REPOSITORY - owner/repo
"""

import os
import re
import json
from typing import Dict, List, Set
from github import Github
from pathlib import Path

def get_requirement_issues(repo) -> List[Dict]:
    """Get all requirement issues (REQ-F, REQ-NF, StR)"""
    issues = repo.get_issues(
        labels=['phase:02-requirements'],
        state='all'
    )
    
    result = []
    for issue in issues:
        labels = [l.name for l in issue.labels]
        if any('requirement' in l for l in labels):
            result.append({
                'number': issue.number,
                'title': issue.title,
                'labels': labels,
                'priority': next((l for l in labels if 'priority' in l), None)
            })
    return result

def get_test_issues(repo) -> List[Dict]:
    """Get all TEST issues"""
    issues = repo.get_issues(
        labels=['type:test'],
        state='all'
    )
    
    result = []
    for issue in issues:
        body = issue.body or ''
        
        # Extract "Verifies: #N" links
        verifies = re.findall(r'Verifies:\s*#(\d+)', body, re.IGNORECASE)
        
        result.append({
            'number': issue.number,
            'title': issue.title,
            'verifies': [int(n) for n in verifies],
            'labels': [l.name for l in issue.labels]
        })
    return result

def find_test_files() -> List[Path]:
    """Find all test files in repository"""
    test_patterns = ['**/test/**/*.py', '**/tests/**/*.py', '**/*_test.py', '**/test_*.py',
                     '**/test/**/*.ts', '**/tests/**/*.ts', '**/*.test.ts', '**/*.spec.ts']
    
    files = []
    for pattern in test_patterns:
        files.extend(Path('.').glob(pattern))
    
    return list(set(files))  # Deduplicate

def extract_verifies_from_file(file_path: Path) -> Set[int]:
    """Extract Verifies: #N comments from test file"""
    content = file_path.read_text(encoding='utf-8', errors='ignore')
    
    # Find all Verifies: #N or @implements #N references
    matches = re.findall(r'(?:Verifies|@implements):\s*#(\d+)', content, re.IGNORECASE)
    
    return set(int(n) for n in matches)

def validate_traceability(repo_name: str):
    """Main validation logic"""
    token = os.getenv('GITHUB_TOKEN')
    g = Github(token)
    repo = g.get_repo(repo_name)
    
    # Fetch data
    req_issues = get_requirement_issues(repo)
    test_issues = get_test_issues(repo)
    test_files = find_test_files()
    
    # Build mappings
    req_to_test = {}  # REQ issue → TEST issues
    test_to_files = {}  # TEST issue → test files
    file_verifies = {}  # test file → REQ issues
    
    for test_issue in test_issues:
        for req_num in test_issue['verifies']:
            if req_num not in req_to_test:
                req_to_test[req_num] = []
            req_to_test[req_num].append(test_issue['number'])
    
    for file_path in test_files:
        verifies = extract_verifies_from_file(file_path)
        file_verifies[str(file_path)] = verifies
        
        for req_num in verifies:
            # Find TEST issues for this requirement
            if req_num in req_to_test:
                for test_num in req_to_test[req_num]:
                    if test_num not in test_to_files:
                        test_to_files[test_num] = []
                    test_to_files[test_num].append(str(file_path))
    
    # Validation checks
    untested_reqs = []
    test_no_code = []
    files_no_trace = []
    
    for req in req_issues:
        if req['number'] not in req_to_test:
            untested_reqs.append(req)
    
    for test_issue in test_issues:
        if test_issue['number'] not in test_to_files:
            test_no_code.append(test_issue)
    
    for file_path, verifies in file_verifies.items():
        if not verifies:
            files_no_trace.append(file_path)
    
    # Generate report
    report = generate_report(
        req_issues, test_issues, test_files,
        untested_reqs, test_no_code, files_no_trace,
        req_to_test, test_to_files, file_verifies
    )
    
    # Write report
    with open('test-validation-report.md', 'w') as f:
        f.write(report)
    
    # Exit with error if critical issues
    if untested_reqs or test_no_code:
        print(f"❌ Validation failed: {len(untested_reqs)} untested requirements, {len(test_no_code)} TEST issues without code")
        return 1
    else:
        print(f"✅ Validation passed: All requirements have tests")
        return 0

def generate_report(req_issues, test_issues, test_files, untested_reqs, test_no_code, files_no_trace, req_to_test, test_to_files, file_verifies):
    """Generate markdown validation report"""
    
    total_reqs = len(req_issues)
    total_tests = len(test_issues)
    total_files = len(test_files)
    
    tested_reqs = total_reqs - len(untested_reqs)
    test_with_code = total_tests - len(test_no_code)
    files_with_trace = total_files - len(files_no_trace)
    
    req_coverage = (tested_reqs / total_reqs * 100) if total_reqs > 0 else 0
    test_impl = (test_with_code / total_tests * 100) if total_tests > 0 else 0
    file_trace = (files_with_trace / total_files * 100) if total_files > 0 else 0
    
    status = '✅ PASS' if not untested_reqs and not test_no_code else '🔴 CRITICAL GAPS'
    
    report = f"""# Test Validation Report (IEEE 1012-2016)

**Repository**: {os.getenv('GITHUB_REPOSITORY', 'unknown')}
**Date**: {datetime.now().strftime('%Y-%m-%d')}
**Validator**: GitHub Copilot (IEEE 1012-2016)

## 📊 Executive Summary

**Traceability Status**:
- Total Requirements (REQ): {total_reqs}
- Total TEST Issues: {total_tests}
- Total Test Files: {total_files}

**Coverage Metrics**:
- Requirements with TEST issues: {tested_reqs} ({req_coverage:.1f}%) - Target: 100%
- TEST issues with implementing code: {test_with_code} ({test_impl:.1f}%) - Target: 100%
- Test files with `Verifies: #N`: {files_with_trace} ({file_trace:.1f}%) - Target: ≥80%

**Overall Status**: {status}

---

## 🔴 Critical Issues

### 1. Untested Requirements ({len(untested_reqs)})

"""
    
    if untested_reqs:
        report += "| Requirement | Title | Priority | Action |\n"
        report += "|-------------|-------|----------|--------|\n"
        for req in untested_reqs[:10]:  # Limit to 10
            report += f"| #{req['number']} | {req['title'][:50]} | {req.get('priority', 'N/A')} | Create TEST issue |\n"
        
        if len(untested_reqs) > 10:
            report += f"\n... and {len(untested_reqs) - 10} more\n"
    else:
        report += "✅ All requirements have TEST issues!\n"
    
    report += "\n### 2. TEST Issues Without Implementing Code ({len(test_no_code)})\n\n"
    
    if test_no_code:
        report += "| TEST Issue | Verifies | Action |\n"
        report += "|------------|----------|--------|\n"
        for test in test_no_code[:10]:
            verifies_str = ', '.join(f"#{n}" for n in test['verifies'])
            report += f"| #{test['number']} | {verifies_str} | Implement test file |\n"
        
        if len(test_no_code) > 10:
            report += f"\n... and {len(test_no_code) - 10} more\n"
    else:
        report += "✅ All TEST issues have implementing code!\n"
    
    report += "\n### 3. Test Files Without Traceability ({len(files_no_trace)})\n\n"
    
    if files_no_trace:
        report += "| File | Action |\n"
        report += "|------|--------|\n"
        for file_path in files_no_trace[:10]:
            report += f"| `{file_path}` | Add `Verifies: #N` comments |\n"
        
        if len(files_no_trace) > 10:
            report += f"\n... and {len(files_no_trace) - 10} more\n"
    else:
        report += "✅ All test files have traceability comments!\n"
    
    report += "\n---\n\n"
    report += "## ✅ Exit Criteria\n\n"
    report += "| Criterion | Current | Target | Status |\n"
    report += "|-----------|---------|--------|--------|\n"
    report += f"| Requirements with TEST issues | {req_coverage:.1f}% | 100% | {'✅' if req_coverage == 100 else '🔴'} |\n"
    report += f"| TEST issues with code | {test_impl:.1f}% | 100% | {'✅' if test_impl == 100 else '🔴'} |\n"
    report += f"| Test files with traceability | {file_trace:.1f}% | ≥80% | {'✅' if file_trace >= 80 else '⚠️'} |\n"
    
    return report

if __name__ == '__main__':
    import sys
    from datetime import datetime
    
    repo_name = os.getenv('GITHUB_REPOSITORY', 'zarfld/copilot-instructions-template')
    sys.exit(validate_traceability(repo_name))
\`\`\`

---

## 🚀 Usage

### Manual Validation (Copilot Chat)

\`\`\`
@workspace Validate test traceability for all requirements

# Or specific scope
@workspace Validate tests for Phase 02 requirements (issues #20-40)

# Or specific module
@workspace Validate test coverage for src/auth/**
\`\`\`

### Automated (GitHub Actions)

\`\`\`bash
# Run validation script
python scripts/github-test-traceability.py

# View report
cat test-validation-report.md
\`\`\`

### Query Test Issues (GitHub CLI)

\`\`\`bash
# List all TEST issues
gh issue list --label "type:test" --state all

# Find TEST issues for specific requirement
gh issue list --label "type:test" --search "Verifies: #25"

# Find requirements without TEST issues
gh issue list --label "type:requirement:functional" --json number,title | \\
  jq '.[] | select(.title | contains("REQ-F"))'
\`\`\`

---

## ✅ Checklist for Test Quality

**Every test file MUST**:
- [ ] Include `Verifies: #N` in docstring or describe block
- [ ] Follow AAA pattern (Arrange-Act-Assert)
- [ ] Use descriptive test names (what/when/expected)
- [ ] Test happy path + error paths + edge cases
- [ ] Use specific assertions (not just truthy/falsy)
- [ ] Clean up after itself (no test pollution)
- [ ] Be independent (can run in any order)

**Every TEST issue MUST**:
- [ ] Link to requirement(s) via `Verifies: #N`
- [ ] List test scenarios (happy path, errors, edges)
- [ ] Specify test file location
- [ ] Define coverage target (e.g., >90%)
- [ ] Include acceptance criteria from requirement

**Before Release**:
- [ ] 100% of P0/P1 requirements have TEST issues
- [ ] 100% of TEST issues have implementing code
- [ ] ≥80% line coverage, ≥75% branch coverage
- [ ] 0 flaky tests
- [ ] 100% of security-critical code tested

---

**Remember**: Every requirement MUST have tests! Use GitHub Issues for traceability. 🧪
````