---
description: "Test-specific instructions for maintaining requirement traceability via GitHub Issues"
applyTo: 
  - "**/tests/**"
  - "**/*.test.js"
  - "**/*.test.ts"
  - "**/*.test.py"
  - "**/*.spec.js"
  - "**/*.spec.ts"
  - "**/*.spec.py"
  - "**/test_*.py"
---

# Test Traceability Requirements

**Standards**: IEEE 1012-2016 (Verification & Validation), ISO/IEC/IEEE 29148:2018 (Requirements Traceability)

## Every Test MUST:

### 1. Link to Requirement Issue

Include explicit reference to the GitHub issue being verified:

```python
"""
Test user login functionality.

Verifies: #123 (REQ-F-AUTH-001: User Login)
Test Type: Integration
Priority: P0 (Critical)

Acceptance Criteria (from #123):
  Given user has valid credentials
  When user submits login form
  Then user is authenticated and redirected to dashboard
"""
def test_user_login_success():
    # Arrange
    user = create_test_user(email="test@example.com", password="secure123")
    
    # Act
    response = login(email="test@example.com", password="secure123")
    
    # Assert
    assert response.status_code == 200
    assert response.user.is_authenticated == True
    assert response.redirect_url == "/dashboard"
```

```typescript
describe('User Login (Verifies #123)', () => {
  /**
   * Verifies: REQ-F-AUTH-001 (Issue #123)
   * Acceptance Criteria: User can log in with valid credentials
   * Test Type: Integration
   * Priority: P0
   */
  it('should authenticate user with valid credentials', async () => {
    // Arrange
    const user = await createTestUser({
      email: 'test@example.com',
      password: 'secure123'
    });
    
    // Act
    const response = await login('test@example.com', 'secure123');
    
    // Assert
    expect(response.status).toBe(200);
    expect(response.user.isAuthenticated).toBe(true);
    expect(response.redirectUrl).toBe('/dashboard');
  });
});
```

### 2. Test Acceptance Criteria

Each test should map to specific acceptance criteria from the requirement issue:

- Copy Given/When/Then scenarios from issue
- Test each scenario separately
- Cover positive and negative cases
- Test boundary conditions
- Test error handling specified in requirement

### 3. Include Test Metadata

Provide context for test purpose and priority:

```python
# Verifies: #145 (REQ-NF-PERF-002: API Response Time)
# Test Type: Performance
# Priority: P1 (High)
# Threshold: Response time < 200ms for 95th percentile

@pytest.mark.performance
def test_api_response_time():
    """Verify API responds within 200ms for 95% of requests."""
    response_times = []
    
    for _ in range(100):
        start = time.time()
        response = api_client.get('/api/users')
        end = time.time()
        response_times.append((end - start) * 1000)
    
    p95 = np.percentile(response_times, 95)
    assert p95 < 200, f"95th percentile: {p95}ms exceeds 200ms threshold"
```

### 4. Maintain Traceability on Changes

When a requirement changes:
- ✅ Update all linked tests
- ✅ Reference the issue in commit message
- ✅ Add comment in test explaining change rationale
- ✅ Update test issue (create TEST issue if not exists)

When a test fails:
- ✅ Check if requirement changed (#issue-number)
- ✅ Create bug issue linking to requirement
- ✅ Update test issue with failure details

## Test Issue Creation

For complex test suites, create TEST issues:

**Template**: `.github/ISSUE_TEMPLATE/07-test-case.yml`

**Required Fields**:
- **Verified Requirements**: Links to REQ issues (e.g., #123, #124)
- **Test Type**: Unit, Integration, E2E, Performance, Security, etc.
- **Test Priority**: P0 (Critical) → P3 (Low)
- **Test Steps**: Given/When/Then format
- **Expected Results**: Observable outcomes
- **Automation Status**: Manual, Automated, Partially Automated

## Test Coverage Enforcement

### Requirement Without Tests = Incomplete

Every REQ-F and REQ-NF issue MUST have:
- At least one test case (or TEST issue)
- Passing test results before marking requirement as "Done"
- Test referenced in requirement issue body

### CI/CD Validation

GitHub Actions workflows check:
- All requirements have linked tests
- All tests reference valid requirement issues
- Test coverage meets threshold (>80%)
- Orphaned tests (no requirement link) trigger warnings

## Examples by Test Type

### Unit Test

```python
# Verifies: #156 (REQ-F-VALID-001: Email Validation)
# Test Type: Unit
# Component: validators.py

def test_email_validation_rejects_invalid_format():
    """
    Verify email validator rejects malformed addresses.
    
    Acceptance Criteria:
      Given an email without @ symbol
      When email is validated
      Then validation fails with clear error message
    """
    invalid_emails = ['notemail', 'test.com', '@example.com', 'test@']
    
    for email in invalid_emails:
        with pytest.raises(ValidationError) as exc_info:
            validate_email(email)
        assert 'Invalid email format' in str(exc_info.value)
```

### Integration Test

```typescript
// Verifies: #178 (REQ-F-DATA-003: User Data Persistence)
// Test Type: Integration
// Components: UserService, Database

describe('User Data Persistence (Verifies #178)', () => {
  it('should persist user data to database', async () => {
    // Arrange
    const userData = {
      email: 'test@example.com',
      name: 'Test User',
      role: 'user'
    };
    
    // Act
    const createdUser = await userService.create(userData);
    const retrievedUser = await userService.findById(createdUser.id);
    
    // Assert
    expect(retrievedUser).toBeDefined();
    expect(retrievedUser.email).toBe(userData.email);
    expect(retrievedUser.name).toBe(userData.name);
    expect(retrievedUser.role).toBe(userData.role);
  });
});
```

### End-to-End Test

```python
# Verifies: #189 (REQ-F-FLOW-001: Complete User Registration Flow)
# Test Type: E2E
# Priority: P0

@pytest.mark.e2e
def test_complete_user_registration_flow():
    """
    Test complete user registration flow from signup to first login.
    
    Scenario: New user registers and accesses dashboard
      Given user visits registration page
      When user completes signup form with valid data
      And user verifies email via confirmation link
      And user logs in with credentials
      Then user is redirected to personalized dashboard
    """
    # Navigate to registration page
    browser.get('/register')
    
    # Fill registration form
    browser.find_element(By.ID, 'email').send_keys('newuser@test.com')
    browser.find_element(By.ID, 'password').send_keys('SecurePass123!')
    browser.find_element(By.ID, 'confirm_password').send_keys('SecurePass123!')
    browser.find_element(By.ID, 'submit').click()
    
    # Verify confirmation email sent
    assert 'Check your email' in browser.page_source
    
    # Simulate email confirmation (via test API)
    confirmation_link = get_latest_confirmation_link('newuser@test.com')
    browser.get(confirmation_link)
    
    # Login with credentials
    browser.get('/login')
    browser.find_element(By.ID, 'email').send_keys('newuser@test.com')
    browser.find_element(By.ID, 'password').send_keys('SecurePass123!')
    browser.find_element(By.ID, 'login').click()
    
    # Verify dashboard access
    assert browser.current_url.endswith('/dashboard')
    assert 'Welcome, newuser' in browser.page_source
```

### Performance Test

```typescript
// Verifies: #201 (REQ-NF-PERF-005: Concurrent User Load)
// Test Type: Performance
// Target: 1000 concurrent users, <2% error rate

import { check } from 'k6';
import http from 'k6/http';

export const options = {
  vus: 1000, // 1000 concurrent users
  duration: '5m',
  thresholds: {
    http_req_duration: ['p(95)<500'], // 95% requests < 500ms
    http_req_failed: ['rate<0.02'],   // <2% error rate
  },
};

export default function () {
  // Verifies: #201 REQ-NF-PERF-005
  const response = http.get('https://api.example.com/users');
  
  check(response, {
    'status is 200': (r) => r.status === 200,
    'response time < 500ms': (r) => r.timings.duration < 500,
  });
}
```

### Security Test

```python
# Verifies: #215 (REQ-NF-SECU-003: SQL Injection Prevention)
# Test Type: Security
# Priority: P0

@pytest.mark.security
def test_sql_injection_prevention():
    """
    Verify system prevents SQL injection attacks.
    
    Acceptance Criteria:
      Given malicious SQL in user input
      When input is processed
      Then query is safely parameterized
      And no database error occurs
      And no sensitive data is exposed
    """
    malicious_inputs = [
        "' OR '1'='1",
        "'; DROP TABLE users; --",
        "1' UNION SELECT * FROM passwords--"
    ]
    
    for malicious_input in malicious_inputs:
        # Attempt SQL injection via search endpoint
        response = api_client.get(
            '/api/search',
            params={'query': malicious_input}
        )
        
        # Should return safe response, not database error
        assert response.status_code in [200, 400]
        assert 'sql' not in response.text.lower()
        assert 'syntax error' not in response.text.lower()
        assert 'database' not in response.text.lower()
```

## Test Naming Conventions

### Function/Method Names

```
test_<feature>_<scenario>_<expected_outcome>

Examples:
- test_user_login_with_valid_credentials_succeeds
- test_email_validation_with_missing_at_symbol_fails
- test_api_request_with_invalid_token_returns_401
```

### File Organization

```
tests/
├── unit/
│   ├── test_validators.py        # Verifies #156-160
│   ├── test_user_model.py         # Verifies #161-165
│   └── test_auth_service.py       # Verifies #123-125
├── integration/
│   ├── test_user_service.py       # Verifies #178-180
│   ├── test_api_endpoints.py      # Verifies #190-195
│   └── test_database_operations.py # Verifies #200-205
├── e2e/
│   ├── test_user_flows.py         # Verifies #189, #210
│   └── test_checkout_flow.py      # Verifies #220-225
├── performance/
│   └── load_test.js               # Verifies #201, #245
└── security/
    └── test_security_vulnerabilities.py # Verifies #215-218
```

## Checklists for Test Quality

Before submitting test PR, verify:

- [ ] Test links to requirement issue in docstring/comment
- [ ] Test covers acceptance criteria from issue
- [ ] Test follows Arrange-Act-Assert pattern
- [ ] Test is isolated (no dependencies on other tests)
- [ ] Test has clear, descriptive name
- [ ] Test includes positive and negative cases
- [ ] Test has appropriate assertions
- [ ] Test runs successfully in CI
- [ ] Test issue created (if applicable) and linked
- [ ] Requirement issue updated with test reference

## When Requirements Change

1. **Check linked tests**:
   ```bash
   # Find tests mentioning issue #123
   git grep -n "#123" tests/
   ```

2. **Update tests to match new acceptance criteria**

3. **Add comment explaining change**:
   ```python
   # Updated 2025-11-12: Requirement #123 changed to require 2FA
   # Previous behavior: Simple password login
   # New behavior: Password + OTP required
   ```

4. **Reference issue in commit**:
   ```bash
   git commit -m "test: Update login tests for 2FA requirement (#123)"
   ```

## CI Integration

Tests must pass GitHub Actions checks:

**.github/workflows/test-traceability.yml**:
```yaml
name: Test Traceability Check

on: [pull_request]

jobs:
  check-test-links:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      
      - name: Check test-requirement links
        run: |
          # Find tests without issue references
          if grep -r "def test_" tests/ --include="*.py" | \
             grep -v "Verifies: #"; then
            echo "❌ Tests found without requirement links"
            exit 1
          fi
          echo "✅ All tests link to requirements"
```

---

**Remember**: Tests are living documentation of requirements. Maintain traceability for quality, compliance, and team understanding! 🧪✅
