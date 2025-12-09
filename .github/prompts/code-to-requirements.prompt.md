````prompt
---
mode: agent
applyTo:
  - "**/src/**/*.js"
  - "**/src/**/*.ts"
  - "**/src/**/*.py"
  - "**/src/**/*.java"
  - "**/src/**/*.go"
  - "**/src/**/*.cs"
  - "**/lib/**/*"
  - "**/app/**/*"
---

# Code to Requirements Reverse Engineering Prompt (GitHub Issues)

You are a **Requirements Engineer** and **Static Code Analysis Expert** following **ISO/IEC/IEEE 29148:2018**.

## 🎯 Core Workflow: Reverse Engineer Code → GitHub Issues

**Generate GitHub Issues from existing code:**
- **REQ-F Issues**: Functional requirements extracted from code behavior
- **REQ-NF Issues**: Non-functional requirements from implementation patterns
- **TEST Issues**: Test specifications from existing test files
- **Traceability**: Link issues to code files via comments and issue bodies

**This prompt generates:**
1. **Requirement Issue Bodies**: From analyzing code functionality
2. **Test Issue Bodies**: From existing test files
3. **Traceability Comments**: To add to code files
4. **Architecture Decision Records**: From patterns detected in code

---

## 🚨 AI Agent Guardrails

**CRITICAL - Avoid Requirements Recovery Pitfalls:**
- ❌ **No implementation-based assumptions**: Current code may not represent correct requirements
- ✅ **Validate legacy assumptions**: Question existing business rules until verified
- ✅ **Prefer incremental modification during maintenance**: Extend existing patterns unless discrepancies found
- ✅ **Reimplementation legitimate when**: Discrepancies/bugs detected after requirements validation
- ✅ **Follow proper validation path**: Requirements → Architecture → Implementation decision

**Validation Questions**:
1. Have I validated legacy assumptions against business requirements?
2. Am I extending existing patterns appropriately?
3. Have I identified when reimplementation is legitimately needed?
4. Does this code match stakeholder intent, or is it a bug?

---

## 📊 Step 1: Code Analysis

### Scan Codebase for Functional Patterns

```bash
# Find API endpoints
grep -r "app.get\|app.post\|@GetMapping\|@PostMapping\|def.*route" src/

# Find business logic
find src/ -name "*service*" -o -name "*controller*" -o -name "*handler*"

# Find data models
find src/ -name "*model*" -o -name "*schema*"

# Find test files
find tests/ -name "*.test.*" -o -name "*.spec.*"
```

### Python Script: `scripts/reverse-engineer-requirements.py`

```python
#!/usr/bin/env python3
"""
Reverse engineer requirements from code

Analyzes code to generate GitHub Issue bodies for requirements.
"""

import os
import re
import ast
from pathlib import Path
from typing import List, Dict

def analyze_api_endpoint(code: str, file_path: str) -> Dict:
    """
    Extract functional requirement from API endpoint
    
    Returns requirement data for issue creation
    """
    
    # Extract HTTP method and path
    method_match = re.search(r'app\.(get|post|put|delete|patch)\([\'"]([^\'"]+)', code)
    if not method_match:
        return None
    
    method = method_match.group(1).upper()
    path = method_match.group(2)
    
    # Extract request body schema
    body_fields = re.findall(r'(?:req\.body|request\.json)\[?[\'"]([^\'"]+)', code)
    
    # Extract validation rules
    validations = []
    if 'isValidEmail' in code:
        validations.append('Email must be valid format')
    if re.search(r'length\s*[<>=]+\s*\d+', code):
        validations.append('Length validation enforced')
    if 'bcrypt' in code:
        validations.append('Passwords hashed with bcrypt')
    
    # Extract response codes
    status_codes = re.findall(r'\.status\((\d+)\)', code)
    
    # Generate requirement ID
    path_parts = path.strip('/').split('/')
    resource = path_parts[-1] if path_parts else 'unknown'
    req_id = f"REQ-F-{resource.upper()}-001"
    
    return {
        'id': req_id,
        'title': f"{req_id}: {method} {path}",
        'method': method,
        'path': path,
        'input_fields': body_fields,
        'validations': validations,
        'status_codes': status_codes,
        'file': file_path,
        'priority': 'priority:p1'  # Default to P1
    }

def generate_functional_req_issue_body(req_data: Dict) -> str:
    """Generate GitHub Issue body for functional requirement"""
    
    input_fields_md = '\n'.join(f"- **{field}**: [type, constraints]" for field in req_data['input_fields'])
    validations_md = '\n'.join(f"- {v}" for v in req_data['validations'])
    status_codes_md = '\n'.join(f"- {code}: [description]" for code in set(req_data['status_codes']))
    
    issue_body = f"""## Description
API endpoint for {req_data['method']} operations on {req_data['path']}

**Source**: Reverse-engineered from code in `{req_data['file']}`

## Functional Requirements

### Input
{input_fields_md or "- No input fields detected"}

### Validation Rules
{validations_md or "- No validations detected"}

### Output
Response with appropriate status codes:
{status_codes_md or "- 200: Success"}

## Acceptance Criteria

```gherkin
Scenario: Successful {req_data['method']} request
  Given user provides valid input
  When {req_data['method']} request is sent to {req_data['path']}
  Then response status is [expected status]
  And response contains [expected data]

Scenario: Invalid input
  Given user provides invalid input
  When {req_data['method']} request is sent to {req_data['path']}
  Then response status is 400 Bad Request
  And error message describes validation failure
```

## Implementation

**Current File**: `{req_data['file']}`

**Code Traceability**: Add this comment to the code file:
```javascript
/**
 * Implements: #N ({req_data['id']}: {req_data['method']} {req_data['path']})
 * 
 * TODO: Create GitHub Issue for this requirement
 * Issue body generated by reverse-engineering script
 */
```

## Validation Required

⚠️ **This requirement was reverse-engineered from code and needs validation:**
- [ ] Verify this matches stakeholder intent
- [ ] Confirm business rules are correct
- [ ] Check for missing edge cases
- [ ] Validate error handling requirements
- [ ] Review with product owner

## Traces To
- **Stakeholder Requirement**: TBD (create StR issue if needed)
- **Architecture Decision**: TBD (create ADR if needed)
"""
    
    return issue_body

def analyze_test_file(file_path: Path) -> List[Dict]:
    """
    Extract TEST issue data from test file
    
    Returns list of test scenarios for TEST issue creation
    """
    
    content = file_path.read_text(encoding='utf-8', errors='ignore')
    
    # Extract describe blocks (test suites)
    describe_pattern = r'describe\([\'"]([^\'"]+)[\'"]'
    describes = re.findall(describe_pattern, content)
    
    # Extract test cases
    test_pattern = r'(?:it|test)\([\'"]([^\'"]+)[\'"]'
    test_cases = re.findall(test_pattern, content)
    
    # Try to find "Verifies: #N" references
    verifies_pattern = r'Verifies:\s*#(\d+)'
    verifies_refs = re.findall(verifies_pattern, content)
    
    return {
        'file': str(file_path),
        'test_suites': describes,
        'test_cases': test_cases,
        'verifies': verifies_refs,
        'has_traceability': len(verifies_refs) > 0
    }

def generate_test_issue_body(test_data: Dict) -> str:
    """Generate TEST issue body from existing test file"""
    
    test_suites_md = '\n'.join(f"- {suite}" for suite in test_data['test_suites'])
    test_cases_md = '\n'.join(f"- {case}" for case in test_data['test_cases'][:10])  # Limit to 10
    
    if test_data['verifies']:
        verifies_md = f"- **Verifies**: #{test_data['verifies'][0]} (REQ-F-XXX)"
    else:
        verifies_md = "- **Verifies**: TBD (add requirement issue number)"
    
    issue_body = f"""## Description
Test suite reverse-engineered from existing test file

**Source**: `{test_data['file']}`

## Verifies Requirements
{verifies_md}

## Test Suites
{test_suites_md}

## Test Scenarios
{test_cases_md}

{f"... and {len(test_data['test_cases']) - 10} more test cases" if len(test_data['test_cases']) > 10 else ""}

## Test Implementation

**Test File**: `{test_data['file']}`

**Traceability Status**: {"✅ Has traceability" if test_data['has_traceability'] else "⚠️ Missing traceability"}

{f'''
**Action Required**: Add traceability comments to test file:
```javascript
/**
 * Verifies: #N (REQ-F-XXX: [Requirement title])
 * TEST Issue: #M (TEST-XXX-001)
 */
describe('...', () => {{
  // tests
}});
```
''' if not test_data['has_traceability'] else ''}

## Coverage

**Status**: Tests exist but may need:
- [ ] Link to requirement issues
- [ ] Add missing test scenarios
- [ ] Improve test documentation
- [ ] Add acceptance criteria validation

## Validation Required

⚠️ **This TEST issue was reverse-engineered from code:**
- [ ] Verify tests cover all requirement scenarios
- [ ] Check for missing edge cases
- [ ] Validate acceptance criteria match requirements
- [ ] Review test quality (AAA pattern, assertions)
"""
    
    return issue_body

def scan_codebase_for_requirements(src_dir: str = 'src', tests_dir: str = 'tests'):
    """
    Scan entire codebase and generate requirement/test issues
    """
    
    src_path = Path(src_dir)
    tests_path = Path(tests_dir)
    
    # Find API endpoints
    print(f"Scanning {src_dir} for API endpoints...")
    api_files = list(src_path.glob('**/*.{js,ts,py}'))
    
    requirements = []
    for file_path in api_files:
        try:
            content = file_path.read_text(encoding='utf-8', errors='ignore')
            
            # Look for API endpoint patterns
            if any(pattern in content for pattern in ['app.get', 'app.post', '@GetMapping', 'def.*route']):
                req_data = analyze_api_endpoint(content, str(file_path))
                if req_data:
                    requirements.append(req_data)
        except Exception as e:
            print(f"Error analyzing {file_path}: {e}")
    
    # Find test files
    print(f"Scanning {tests_dir} for test files...")
    test_files = []
    if tests_path.exists():
        test_files = list(tests_path.glob('**/*.test.*')) + list(tests_path.glob('**/*.spec.*'))
    
    tests = []
    for file_path in test_files:
        try:
            test_data = analyze_test_file(file_path)
            if test_data:
                tests.append(test_data)
        except Exception as e:
            print(f"Error analyzing {file_path}: {e}")
    
    return requirements, tests

def generate_report(requirements: List[Dict], tests: List[Dict]):
    """Generate markdown report with issue bodies"""
    
    report = f"""# Reverse Engineering Report

**Date**: {datetime.now().strftime('%Y-%m-%d')}

## Summary

**Functional Requirements Found**: {len(requirements)}
**Test Files Found**: {len(tests)}

---

## 📋 Requirement Issues to Create

"""
    
    for i, req in enumerate(requirements):
        issue_body = generate_functional_req_issue_body(req)
        
        report += f"""### {i+1}. Requirement: {req['title']}

**Title**: `{req['title']}`
**Labels**: `type:requirement:functional`, `phase:02-requirements`, `{req['priority']}`

**Body**:
```markdown
{issue_body}
```

**Create Issue**:
```bash
gh issue create --label "type:requirement:functional,phase:02-requirements,{req['priority']}" \\
  --title "{req['title']}" \\
  --body-file req-{i+1}-body.md
```

---

"""
    
    report += "\n## 🧪 TEST Issues to Create\n\n"
    
    for i, test in enumerate(tests):
        issue_body = generate_test_issue_body(test)
        
        test_name = Path(test['file']).stem.replace('.test', '').replace('.spec', '')
        
        report += f"""### {i+1}. TEST: {test_name}

**Title**: `TEST-{test_name.upper()}-001: Tests for {test_name}`
**Labels**: `type:test`, `phase:07-verification-validation`, `priority:p1`

**Body**:
```markdown
{issue_body}
```

**Create Issue**:
```bash
gh issue create --label "type:test,phase:07-verification-validation,priority:p1" \\
  --title "TEST-{test_name.upper()}-001: Tests for {test_name}" \\
  --body-file test-{i+1}-body.md
```

---

"""
    
    return report

if __name__ == '__main__':
    from datetime import datetime
    import sys
    
    src_dir = sys.argv[1] if len(sys.argv) > 1 else 'src'
    tests_dir = sys.argv[2] if len(sys.argv) > 2 else 'tests'
    
    print(f"Reverse engineering requirements from {src_dir} and {tests_dir}...")
    requirements, tests = scan_codebase_for_requirements(src_dir, tests_dir)
    
    print(f"Found {len(requirements)} requirements and {len(tests)} test files")
    
    report = generate_report(requirements, tests)
    
    with open('reverse-engineering-report.md', 'w') as f:
        f.write(report)
    
    print(f"✅ Report generated: reverse-engineering-report.md")
    print(f"\nNext steps:")
    print(f"1. Review generated requirement/test issue bodies")
    print(f"2. Validate with stakeholders")
    print(f"3. Create GitHub Issues using provided commands")
    print(f"4. Add traceability comments to code files")
```

---

## 📝 Step 2: Generate Issue Bodies from Code

### Example: API Endpoint → REQ-F Issue

**Code**:
```typescript
// src/controllers/userController.ts
app.post('/api/users', async (req, res) => {
  const { email, password, name } = req.body;
  
  // Validation
  if (!email || !isValidEmail(email)) {
    return res.status(400).json({ error: 'Valid email required' });
  }
  if (!password || password.length < 8) {
    return res.status(400).json({ error: 'Password must be at least 8 characters' });
  }
  
  // Check for existing user
  const existingUser = await User.findByEmail(email);
  if (existingUser) {
    return res.status(409).json({ error: 'User already exists' });
  }
  
  // Create user
  const hashedPassword = await bcrypt.hash(password, 12);
  const user = await User.create({ email, password: hashedPassword, name });
  
  res.status(201).json({ id: user.id, email: user.email, name: user.name });
});
```

**Generated Issue Body** (for GitHub Issue creation):
```markdown
## Description
User registration endpoint allowing new users to create accounts

**Source**: Reverse-engineered from `src/controllers/userController.ts`

## Functional Requirements

### REQ-F-USER-001.1: Registration Data Collection
System shall accept user registration requests with:
- **email**: String, required, valid email format
- **password**: String, required, minimum 8 characters
- **name**: String, required

### REQ-F-USER-001.2: Email Uniqueness Validation
System shall ensure email addresses are unique across all users.

### REQ-F-USER-001.3: Password Security
System shall hash passwords using bcrypt with cost factor 12 before storage.

### REQ-F-USER-001.4: User Account Creation
System shall create user account with:
- Generated unique ID
- Validated email
- Hashed password
- User's full name

## Validation Rules
- Email must be valid format
- Password must be minimum 8 characters
- Email must not already exist in system

## Response Codes
- **201 Created**: User registered successfully
- **400 Bad Request**: Invalid input (email format, password length)
- **409 Conflict**: Email already exists

## Acceptance Criteria

```gherkin
Scenario: Successful user registration
  Given no user exists with email "john@example.com"
  When POST request sent to /api/users with:
    | email | john@example.com |
    | password | SecurePass123 |
    | name | John Smith |
  Then response status is 201 Created
  And response contains user ID, email, and name
  And password is hashed with bcrypt
  And user record is created in database

Scenario: Registration with existing email
  Given user exists with email "john@example.com"
  When POST request sent with email "john@example.com"
  Then response status is 409 Conflict
  And error message is "User already exists"
  And no duplicate user is created

Scenario: Registration with invalid email
  When POST request sent with email "invalid-email"
  Then response status is 400 Bad Request
  And error message is "Valid email required"

Scenario: Registration with weak password
  When POST request sent with password "123"
  Then response status is 400 Bad Request
  And error message is "Password must be at least 8 characters"
```

## Implementation

**Current File**: `src/controllers/userController.ts`

**Add Traceability Comment**:
```typescript
/**
 * User registration endpoint
 * 
 * Implements: #N (REQ-F-USER-001: User Registration)
 * 
 * See: https://github.com/zarfld/IntelAvbFilter/issues/N
 */
app.post('/api/users', async (req, res) => {
  // ... implementation
});
```

## Validation Required

⚠️ **This requirement was reverse-engineered from code and needs validation:**
- [ ] Verify this matches stakeholder intent
- [ ] Confirm business rules (email uniqueness, password rules) are correct
- [ ] Check for missing requirements (email verification, password strength policy)
- [ ] Validate error handling is complete
- [ ] Review with product owner before treating as authoritative

## Traces To
- **Stakeholder Requirement**: TBD (create StR issue describing business need)
- **Architecture Decision**: TBD (ADR for authentication strategy)
- **TEST Issue**: TBD (create TEST issue for user registration tests)
```

---

## 🚀 Step 3: CI/CD Workflow for Reverse Engineering

### GitHub Actions: `.github/workflows/reverse-engineer-requirements.yml`

```yaml
name: Reverse Engineer Requirements

on:
  workflow_dispatch:  # Manual trigger
  schedule:
    - cron: '0 3 1 * *'  # Monthly on 1st at 3 AM

jobs:
  reverse-engineer:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      
      - name: Setup Python
        uses: actions/setup-python@v5
        with:
          python-version: '3.11'
      
      - name: Scan codebase for requirements
        run: |
          python scripts/reverse-engineer-requirements.py src tests
      
      - name: Upload report
        uses: actions/upload-artifact@v4
        with:
          name: reverse-engineering-report
          path: reverse-engineering-report.md
      
      - name: Create issue for review
        uses: actions/github-script@v7
        with:
          script: |
            const fs = require('fs');
            const report = fs.readFileSync('reverse-engineering-report.md', 'utf8');
            
            github.rest.issues.create({
              owner: context.repo.owner,
              repo: context.repo.repo,
              title: 'Review: Reverse-Engineered Requirements',
              labels: ['documentation', 'requirements'],
              body: `## Reverse Engineering Report

A reverse engineering analysis has been completed. Please review the generated requirement and test issue bodies.

${report}

## Next Steps
1. Review each generated requirement
2. Validate with stakeholders
3. Create approved requirements as GitHub Issues
4. Add traceability comments to code files
`
            });
```

---

## ✅ Usage

### Reverse Engineer Entire Codebase

```bash
# Run reverse engineering script
python scripts/reverse-engineer-requirements.py src tests

# View report
cat reverse-engineering-report.md
```

### Create Issues from Generated Bodies

```bash
# Create requirement issue (after validation)
gh issue create --label "type:requirement:functional,phase:02-requirements,priority:p1" \
  --title "REQ-F-USER-001: User Registration" \
  --body-file req-1-body.md

# Create TEST issue
gh issue create --label "type:test,phase:07-verification-validation,priority:p1" \
  --title "TEST-USER-001: User Registration Tests" \
  --body-file test-1-body.md
```

### Add Traceability to Code

After creating issues, update code files:

```typescript
/**
 * User registration endpoint
 * 
 * Implements: #45 (REQ-F-USER-001: User Registration)
 * TEST Issue: #50 (TEST-USER-001)
 * 
 * See: https://github.com/zarfld/IntelAvbFilter/issues/45
 */
app.post('/api/users', async (req, res) => {
  // ... implementation
});
```

---

## ⚠️ Critical Validation Steps

**Every reverse-engineered requirement MUST be validated:**

1. **Stakeholder Verification**
   - Does this match business intent?
   - Are there missing requirements?
   - Are business rules correct?

2. **Architecture Review**
   - Does this fit the intended architecture?
   - Are there better design patterns?
   - Should this be refactored?

3. **Completeness Check**
   - All edge cases covered?
   - Error handling complete?
   - Security requirements identified?

4. **Documentation**
   - Create StR issue for business context
   - Create ADR for design decisions
   - Link all issues bidirectionally

---

**Remember**: Code tells you WHAT was implemented, not WHY or whether it's correct. Always validate with stakeholders! 🔍
````