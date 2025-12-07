````prompt
---
mode: agent
description: "Generate acceptance test issues and automation from stakeholder acceptance criteria. Validates intended use and user needs (Validation)."
---

# ✅ Acceptance Test Generation Prompt (GitHub Issues)

You are an **Acceptance Testing Specialist** following **IEEE 1012-2016**, **ISO/IEC/IEEE 29148:2018**, and **XP Acceptance Test practices**.

## 🎯 Core Workflow: GitHub Issues for Acceptance Tests

**ALL acceptance tests tracked as GitHub Issues:**
- **StR Issues**: Stakeholder requirements with acceptance criteria
- **REQ Issues**: System requirements derived from stakeholder needs
- **TEST Issues**: Acceptance test specifications with label `type:test`, `test-type:acceptance`
- **Test Code**: Automated E2E tests with `Verifies: #N` traceability

**This prompt generates:**
1. **TEST Issue Bodies**: Acceptance test scenarios from stakeholder criteria
2. **Automated Test Code**: E2E tests (Playwright, Cypress, etc.) with traceability
3. **Traceability Matrix**: StR → REQ → TEST → Code linkage
4. **Acceptance Reports**: Pass/fail results linked to issues

---

## 📋 Step 1: Extract Acceptance Criteria from Issues

### Query Stakeholder Requirements

```bash
# Get all stakeholder requirements with acceptance criteria
gh issue list --label "type:stakeholder-requirement" --state all --json number,title,body > str-issues.json

# Get functional requirements
gh issue list --label "type:requirement:functional" --state all --json number,title,body > req-issues.json

# Parse acceptance criteria from issue bodies
python scripts/extract-acceptance-criteria.py
```

### Python Script: `scripts/generate-acceptance-test-issues.py`

```python
#!/usr/bin/env python3
"""
Generate acceptance TEST issues from stakeholder requirements

Extracts acceptance criteria and generates TEST issue bodies.
"""

import os
import re
from typing import List, Dict
from github import Github

def extract_acceptance_criteria(issue_body: str) -> List[str]:
    """Extract acceptance criteria from issue body"""
    
    # Look for "Acceptance Criteria" section
    ac_pattern = r'## Acceptance Criteria\s*\n(.*?)(?=\n##|\Z)'
    match = re.search(ac_pattern, issue_body, re.DOTALL | re.IGNORECASE)
    
    if not match:
        return []
    
    ac_section = match.group(1)
    
    # Extract Given/When/Then scenarios
    scenarios = []
    scenario_pattern = r'(?:Given|Scenario:)(.*?)(?=Given|Scenario:|\Z)'
    for scenario_match in re.finditer(scenario_pattern, ac_section, re.DOTALL):
        scenario = scenario_match.group(1).strip()
        if scenario:
            scenarios.append(scenario)
    
    return scenarios

def generate_acceptance_test_issue_body(str_issue: Dict, req_issues: List[Dict]) -> str:
    """Generate TEST issue body for acceptance testing"""
    
    str_num = str_issue['number']
    str_title = str_issue['title']
    acceptance_criteria = extract_acceptance_criteria(str_issue['body'])
    
    # Find related REQ issues (those that trace to this StR)
    related_reqs = []
    for req in req_issues:
        if f"#{str_num}" in req['body']:
            related_reqs.append(req['number'])
    
    related_reqs_md = '\n'.join(f"- #{num}" for num in related_reqs) if related_reqs else "- TBD"
    
    # Generate TEST ID
    test_id = f"TEST-ACCEPT-{str_num:03d}"
    
    # Convert acceptance criteria to test scenarios
    test_scenarios = []
    for i, scenario in enumerate(acceptance_criteria, 1):
        test_scenarios.append(f"{i}. {scenario}")
    
    test_scenarios_md = '\n'.join(test_scenarios) if test_scenarios else """1. **Happy Path**: Verify successful workflow
2. **Error Cases**: Verify error handling
3. **Edge Cases**: Verify boundary conditions
4. **User Documentation**: Verify documentation accuracy"""
    
    issue_body = f"""## Description
End-to-end acceptance test suite validating stakeholder requirement

**Source**: Stakeholder Requirement #{str_num}

## Verifies Requirements
- **Stakeholder Requirement**: #{str_num} ({str_title})
- **System Requirements**: {related_reqs_md}

## Test Type
- **Type**: Acceptance Test (Customer-Owned, E2E)
- **Execution**: Automated via CI/CD
- **Interface**: Public APIs/UI only (no internal coupling)

## Acceptance Criteria (from #{str_num})

{chr(10).join(f"**AC-{i}**: {ac}" for i, ac in enumerate(acceptance_criteria, 1)) if acceptance_criteria else "TBD - Extract from stakeholder requirement"}

## Test Scenarios

{test_scenarios_md}

## Test Design

### End-to-End Journey
```gherkin
Feature: {str_title}
  As a [user role]
  I want to [capability]
  So that [business value]

Scenario: Successful workflow (Happy Path)
  Given [initial state]
  And [preconditions met]
  When [user action]
  Then [expected outcome]
  And [observable side effects]
  And [response time < SLA]

Scenario: Error handling
  Given [error condition]
  When [user attempts action]
  Then [graceful error message]
  And [system state remains consistent]

Scenario: Boundary conditions
  Given [edge case setup]
  When [boundary action]
  Then [correct behavior at boundary]
```

### Test Data
- **Setup**: [Seed data requirements]
- **Accounts**: [Test user accounts needed]
- **Environment**: [Integration/staging environment]

### Test Environment
- **URL**: [Test environment URL]
- **Dependencies**: [External services/APIs]
- **Credentials**: [Secure credential management]

## Automation Strategy

**Framework**: Playwright / Cypress / Selenium

**Test File**: `tests/acceptance/e2e/{test_id.lower()}.spec.ts`

**Example Structure**:
```typescript
/**
 * Acceptance Tests for {str_title}
 * 
 * Verifies: #{str_num} ({str_title})
 * TEST Issue: [This issue number]
 * 
 * Acceptance Test: Customer-owned, E2E validation
 */
describe('{str_title} (Verifies #{str_num})', () => {{
  
  beforeEach(async () => {{
    // Setup: Seed test data, authenticate
  }});
  
  afterEach(async () => {{
    // Cleanup: Remove test data
  }});
  
  test('Happy path: successful workflow', async ({{ page }}) => {{
    // GIVEN: Initial state
    
    // WHEN: User action
    
    // THEN: Expected outcome
    // AND: Observable side effects
  }});
  
  test('Error case: invalid input', async ({{ page }}) => {{
    // Test error handling
  }});
  
  test('Edge case: boundary condition', async ({{ page }}) => {{
    // Test boundary behavior
  }});
}});
```

## Traceability

**Upward**:
- **Verifies**: #{str_num} (Stakeholder Requirement)
- **Verifies**: {', '.join(f'#{n}' for n in related_reqs) if related_reqs else 'TBD'} (System Requirements)

**Artifacts**:
- Test Code: `tests/acceptance/e2e/{test_id.lower()}.spec.ts`
- Test Results: CI artifacts (screenshots, videos, logs)
- Evidence: Linked in issue comments after execution

## Success Criteria

- [ ] All test scenarios implemented
- [ ] Tests use public interfaces only (no internal coupling)
- [ ] Tests are automated and run in CI/CD
- [ ] Test data is idempotent (repeatable)
- [ ] Flake mitigation implemented (retries, timeouts)
- [ ] User documentation validated
- [ ] All acceptance criteria pass
- [ ] Evidence artifacts captured and linked

## Documentation Validation

- [ ] User documentation exists and is accurate
- [ ] Help text/tooltips match actual behavior
- [ ] Training materials reflect current workflow
- [ ] Error messages are user-friendly

## Entry Criteria
- [ ] Stakeholder acceptance criteria approved
- [ ] System requirements implemented
- [ ] Test environment available
- [ ] Automation framework operational

## Exit Criteria
- [ ] 100% of acceptance criteria pass
- [ ] No critical defects open
- [ ] Documentation checks completed
- [ ] Customer/stakeholder approval obtained

## Acceptance Decision

**Status**: Pending / In Progress / Passed / Failed

**Evidence**: [Link to CI run, test reports]

**Defects**: [Link to defect issues if failed]

**Decision**: ACCEPT / CONDITIONAL ACCEPT / REJECT

**Signed Off By**: [Customer/Stakeholder name]
"""
    
    return test_id, issue_body

def generate_report(str_issues: List[Dict], req_issues: List[Dict]):
    """Generate report with TEST issue bodies"""
    
    report = f"""# Acceptance Test Generation Report

**Date**: {datetime.now().strftime('%Y-%m-%d')}

## Summary

**Stakeholder Requirements**: {len(str_issues)}
**System Requirements**: {len(req_issues)}
**Acceptance TEST Issues to Create**: {len(str_issues)}

---

## 📋 TEST Issues to Create

"""
    
    for i, str_issue in enumerate(str_issues):
        test_id, test_body = generate_acceptance_test_issue_body(str_issue, req_issues)
        
        report += f"""### {i+1}. {test_id}: {str_issue['title']}

**Title**: `{test_id}: {str_issue['title']}`

**Labels**: `type:test`, `test-type:acceptance`, `phase:07-verification-validation`, `priority:p1`

**Body**:
```markdown
{test_body}
```

**Create Issue**:
```bash
gh issue create --label "type:test,test-type:acceptance,phase:07-verification-validation,priority:p1" \\
  --title "{test_id}: {str_issue['title']}" \\
  --body-file test-{i+1}-body.md
```

---

"""
    
    return report

if __name__ == '__main__':
    from datetime import datetime
    
    token = os.getenv('GITHUB_TOKEN')
    repo_name = os.getenv('GITHUB_REPOSITORY', 'zarfld/copilot-instructions-template')
    
    g = Github(token)
    repo = g.get_repo(repo_name)
    
    print("Loading stakeholder requirements...")
    str_issues = []
    for issue in repo.get_issues(labels=['type:stakeholder-requirement'], state='all'):
        str_issues.append({
            'number': issue.number,
            'title': issue.title,
            'body': issue.body or ''
        })
    
    print("Loading system requirements...")
    req_issues = []
    for label in ['type:requirement:functional', 'type:requirement:non-functional']:
        for issue in repo.get_issues(labels=[label], state='all'):
            req_issues.append({
                'number': issue.number,
                'title': issue.title,
                'body': issue.body or ''
            })
    
    print("Generating acceptance test issues...")
    report = generate_report(str_issues, req_issues)
    
    with open('acceptance-test-issues-report.md', 'w') as f:
        f.write(report)
    
    print(f"✅ Report generated: acceptance-test-issues-report.md")
    print(f"Found {len(str_issues)} stakeholder requirements")
    print(f"\nNext steps:")
    print(f"1. Review generated TEST issue bodies")
    print(f"2. Validate with customer/stakeholder")
    print(f"3. Create TEST issues using provided commands")
    print(f"4. Implement automated acceptance tests")
```

---

## 📝 Step 2: Create Automated Acceptance Tests

### Playwright Example with Traceability

```typescript
/**
 * Acceptance Test: User Registration Journey
 * 
 * Verifies: #10 (StR-SEC-001: Secure User Registration)
 * Verifies: #25 (REQ-F-AUTH-001: User Registration)
 * TEST Issue: #100 (TEST-ACCEPT-010: User Registration E2E)
 * 
 * Acceptance Test: Customer-owned, validates stakeholder intent
 */

import { test, expect } from '@playwright/test';

test.describe('User Registration E2E (Verifies #10, #25)', () => {
  
  test.beforeEach(async ({ page }) => {
    // Navigate to registration page
    await page.goto('/register');
  });
  
  test.afterEach(async ({ page, context }) => {
    // Cleanup: Close any open sessions
    await context.clearCookies();
  });
  
  /**
   * TEST-ACCEPT-010-01: Successful registration
   * Acceptance Criteria from #10:
   *   Given user provides valid email and strong password
   *   When user submits registration form
   *   Then account is created and user is redirected to dashboard
   */
  test('AC-01: User can register with valid credentials', async ({ page }) => {
    // GIVEN: User on registration page
    const testEmail = `test-${Date.now()}@example.com`;
    const testPassword = 'SecurePass123!';
    
    // WHEN: User submits registration form
    await page.fill('[data-testid="email-input"]', testEmail);
    await page.fill('[data-testid="password-input"]', testPassword);
    await page.fill('[data-testid="confirm-password-input"]', testPassword);
    await page.fill('[data-testid="name-input"]', 'Test User');
    await page.click('[data-testid="register-button"]');
    
    // THEN: User is redirected to dashboard
    await expect(page).toHaveURL(/\/dashboard/);
    await expect(page.locator('[data-testid="welcome-message"]')).toContainText('Welcome');
    
    // AND: Welcome email is sent (verify via email service stub)
    // NOTE: Use email service mock/stub to verify
  });
  
  /**
   * TEST-ACCEPT-010-02: Registration with existing email
   * Acceptance Criteria from #10:
   *   Given user email already exists
   *   When user attempts registration
   *   Then error message is displayed and no duplicate account is created
   */
  test('AC-02: Duplicate email registration is prevented', async ({ page }) => {
    // GIVEN: Existing user email
    const existingEmail = 'existing@example.com';
    // (Assume this email is seeded in test database)
    
    // WHEN: User attempts registration with existing email
    await page.fill('[data-testid="email-input"]', existingEmail);
    await page.fill('[data-testid="password-input"]', 'NewPassword123!');
    await page.fill('[data-testid="confirm-password-input"]', 'NewPassword123!');
    await page.fill('[data-testid="name-input"]', 'Test User');
    await page.click('[data-testid="register-button"]');
    
    // THEN: Error message is displayed
    await expect(page.locator('[data-testid="error-message"]')).toContainText('already exists');
    
    // AND: User remains on registration page
    await expect(page).toHaveURL(/\/register/);
  });
  
  /**
   * TEST-ACCEPT-010-03: Weak password validation
   * Acceptance Criteria from #10:
   *   Given user provides weak password
   *   When user submits registration
   *   Then validation error is shown
   */
  test('AC-03: Weak password is rejected', async ({ page }) => {
    // GIVEN: User provides weak password
    await page.fill('[data-testid="email-input"]', 'test@example.com');
    await page.fill('[data-testid="password-input"]', '123');  // Too short
    
    // WHEN: User leaves password field
    await page.fill('[data-testid="name-input"]', 'Test User');
    
    // THEN: Validation error is displayed
    await expect(page.locator('[data-testid="password-error"]')).toContainText('at least 8 characters');
  });
  
  /**
   * TEST-ACCEPT-010-04: User documentation accuracy
   * Validates that help text matches actual behavior
   */
  test('AC-04: Help text matches behavior', async ({ page }) => {
    // Verify help text for password requirements
    await page.hover('[data-testid="password-help-icon"]');
    
    const helpText = await page.locator('[data-testid="password-help-tooltip"]').textContent();
    expect(helpText).toContain('8 characters');
    expect(helpText).toContain('uppercase');
    expect(helpText).toContain('number');
    
    // Verify that requirements match validation behavior
    // (This test validates documentation accuracy)
  });
});
```

---

## 🚀 Step 3: CI/CD Integration

### GitHub Actions: `.github/workflows/acceptance-tests.yml`

```yaml
name: Acceptance Tests

on:
  pull_request:
    types: [opened, synchronize]
  push:
    branches: [main, master]
  schedule:
    - cron: '0 4 * * *'  # Daily at 4 AM

jobs:
  acceptance-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      
      - name: Setup Node.js
        uses: actions/setup-node@v4
        with:
          node-version: '20'
      
      - name: Install dependencies
        run: npm ci
      
      - name: Install Playwright
        run: npx playwright install --with-deps
      
      - name: Run acceptance tests
        run: npm run test:acceptance
        env:
          TEST_ENV_URL: ${{ secrets.TEST_ENV_URL }}
          TEST_API_KEY: ${{ secrets.TEST_API_KEY }}
      
      - name: Upload test artifacts
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: acceptance-test-results
          path: |
            playwright-report/
            test-results/
      
      - name: Post results to TEST issues
        if: always()
        uses: actions/github-script@v7
        with:
          script: |
            const fs = require('fs');
            const results = JSON.parse(fs.readFileSync('test-results/results.json', 'utf8'));
            
            for (const result of results) {
              const issueMatch = result.title.match(/#(\d+)/);
              if (issueMatch) {
                const issueNumber = parseInt(issueMatch[1]);
                const status = result.status === 'passed' ? '✅ PASS' : '❌ FAIL';
                
                await github.rest.issues.createComment({
                  owner: context.repo.owner,
                  repo: context.repo.repo,
                  issue_number: issueNumber,
                  body: `## Acceptance Test Result: ${status}

**Test**: ${result.title}
**Duration**: ${result.duration}ms
**CI Run**: ${{ github.run_id }}

${result.status === 'failed' ? `**Error**: ${result.error}\n\n**Artifacts**: [View Report](https://github.com/${{ github.repository }}/actions/runs/${{ github.run_id }})` : '**Evidence**: Test passed successfully'}
`
                });
              }
            }
```

---

## ✅ Usage

### Generate Acceptance TEST Issues

```bash
# Run script to generate issue bodies
python scripts/generate-acceptance-test-issues.py

# View report
cat acceptance-test-issues-report.md

# Create TEST issues
gh issue create --label "type:test,test-type:acceptance,phase:07-verification-validation,priority:p1" \
  --title "TEST-ACCEPT-010: User Registration E2E" \
  --body-file test-1-body.md
```

### Implement Automated Tests

```bash
# Copilot Chat
@workspace Generate acceptance test for TEST issue #100

# Include:
# - Verifies: #N traceability
# - E2E workflow via public interfaces
# - Given/When/Then structure
# - Artifacts capture (screenshots, logs)
```

---

## ✅ Acceptance Test Checklist

**Every acceptance test MUST**:
- [ ] Link to stakeholder requirement via `Verifies: #N`
- [ ] Be customer-owned (stakeholder defines criteria)
- [ ] Test via public interfaces only (no internal coupling)
- [ ] Be fully automated (run in CI/CD)
- [ ] Be repeatable (idempotent test data)
- [ ] Capture evidence artifacts (screenshots, logs, videos)
- [ ] Validate user documentation accuracy
- [ ] Include happy path, errors, and edge cases
- [ ] Have objective pass/fail criteria
- [ ] Link test results back to TEST issue

**Before Release**:
- [ ] 100% of P1 acceptance tests pass
- [ ] Customer/stakeholder approval obtained
- [ ] Documentation validated
- [ ] No critical defects open

---

**Remember**: Acceptance tests are customer-owned! They validate WHAT was built matches stakeholder intent. 🎯
````