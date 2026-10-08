---
description: "Comprehensive guide to testing artifact creation across the software development lifecycle. Integrates IEEE 1012-2016 (V&V) standards with TDD/Agile practices."
applyTo: "**/*"
---

# Testing Across the Software Development Lifecycle

**Critical Principle**: Testing is NOT just execution during implementation or validation phases. Testing is a **lifecycle-parallel process** of planning, design, and execution that occurs throughout all phases.

## 🎯 Core Understanding

### IEEE 1012-2016 (Verification & Validation) Perspective

**Testing encompasses**:
1. **Test Planning** - WHAT to test and HOW to measure success
2. **Test Design** - Test approach and strategy for components/integration
3. **Test Cases** - Detailed scenarios with inputs, outputs, execution conditions
4. **Test Procedures** - Step-by-step execution instructions
5. **Test Execution** - Running tests and recording results
6. **Test Reporting** - Analysis and verification evidence

### TDD/Agile Perspective

**Tests drive development**:
1. **Acceptance Tests** - Written during requirements to clarify stakeholder needs
2. **Unit Tests** - Written BEFORE implementation to drive design
3. **Tests as Specifications** - Executable living documentation
4. **Design Feedback** - Difficult-to-test code indicates design problems

## 📋 Testing Artifacts by Lifecycle Phase

### Summary Table

| Lifecycle Phase | IEEE 1012 V&V Artifact | TDD/Agile Equivalent | Created When | Purpose |
|-----------------|------------------------|---------------------|--------------|---------|
| **Phase 01: Stakeholder Requirements** | Stakeholder validation approach | Customer collaboration | During StR definition | Ensure understanding of stakeholder needs |
| **Phase 02: Requirements** | **Test Plan** (Acceptance/Qualification) | **Acceptance Tests** (User Stories) | **During requirements analysis** | Define WHAT to test and success criteria |
| **Phase 03: Architecture** | **Test Design** (Integration/System) | System Metaphor / Spike Solutions | **During architecture design** | Design test approach for components and integration |
| **Phase 04: Design** | **Test Cases** (detailed scenarios) | **Unit Tests** (TDD - written BEFORE code) | **During detailed design** | Specify inputs, expected outputs, execution conditions |
| **Phase 05: Implementation** | **Test Procedures** & **Execution** | Code Implementation & Refactoring | **During coding** | Execute tests, implement code to pass tests |
| **Phase 06: Integration** | Integration Test Execution & Reports | Continuous Integration tests | **During component integration** | Verify components work together |
| **Phase 07: Verification & Validation** | V&V Reports, Acceptance Test Execution | Customer acceptance testing | **After implementation** | Prove system meets requirements |

---

## 📌 Phase 02: Requirements (Test Planning)

### IEEE 1012-2016 Requirements

During **System Requirements Definition**, V&V tasks include:

#### 1. System Acceptance Test Plan
- **Purpose**: Define how the system will be **validated** against stakeholder needs (building the RIGHT product)
- **Content**: Test strategy, scope, resources, schedule, acceptance criteria
- **Traceability**: Each stakeholder requirement (StR) → Acceptance test approach
- **Deliverable**: `02-requirements/test-plans/system-acceptance-test-plan.md`

**Key Sections**:
```markdown
1. Test Strategy (customer-driven, scenario-based)
2. Test Levels (alpha, beta, UAT)
3. Traceability Matrix: StR → Acceptance Tests
4. Acceptance Criteria (Given-When-Then for each StR)
5. Test Resources (stakeholder availability, environment)
6. Success Criteria (what constitutes "accepted")
```

#### 2. System Qualification Test Plan
- **Purpose**: Define how system requirements (REQ-F, REQ-NF) will be **verified** (building the product RIGHT)
- **Content**: Test approach for functional and non-functional requirements
- **Traceability**: Each system requirement → Qualification test method
- **Deliverable**: `02-requirements/test-plans/system-qualification-test-plan.md`

**Key Sections**:
```markdown
1. Test Strategy (requirements-based, multi-level, TDD-driven)
2. Verification Methods (Test/Analysis/Inspection/Demonstration)
3. Traceability Matrix: REQ → Qualification Tests
4. Test Design Approach (detailed test cases come in Phase 04)
5. Test Environment Requirements
6. Success Criteria (all requirements verified)
```

### TDD/Agile Approach: Acceptance Tests as Requirements

**In Agile/XP, acceptance tests ARE the detailed requirements:**

```gherkin
# File: 02-requirements/acceptance-tests/user-login.feature

Feature: User Authentication (REQ-F-AUTH-001)
  As a registered user
  I want to securely log in to the system
  So that I can access my personalized dashboard

  @critical @req-f-auth-001
  Scenario: Successful login with valid credentials
    Given I am on the login page
    When I enter username "alice"
    And I enter password "SecurePass1!"
    And I click "Login"
    Then I should be redirected to "/dashboard" within 2 seconds
    And I should see "Welcome, alice"
    And my session should be active

  @critical @req-f-auth-001
  Scenario: Failed login with invalid password
    Given I am on the login page
    When I enter username "alice"
    And I enter password "WrongPassword"
    And I click "Login"
    Then I should remain on the login page
    And I should see error "Invalid username or password"
    And no session should be created
```

**These Gherkin scenarios serve as**:
1. **Executable specifications** (run with Cucumber/SpecFlow)
2. **Living documentation** (always up-to-date)
3. **Acceptance tests** (stakeholder validates by reviewing scenarios)
4. **Detailed requirements** (Given-When-Then clarifies expected behavior)

### Why Test Planning Happens in Requirements Phase

**Objective Acceptance Criteria**: By defining test plans during requirements:
- ✅ **Requirements are testable** - If you can't define how to test it, it's not verifiable
- ✅ **Stakeholders agree on success criteria** - Before implementation begins
- ✅ **Ambiguous requirements exposed early** - Clarification before coding
- ✅ **Traceability established** - Requirement → Test → Implementation
- ✅ **Cost reduction** - Cheaper to fix requirements than code (10x-100x cost savings)

### Phase 02 Exit Criteria (Testing Perspective)

✅ **System Acceptance Test Plan complete** (all StR have acceptance criteria)  
✅ **System Qualification Test Plan complete** (all REQ-F/REQ-NF have verification methods)  
✅ **Every requirement specifies verification method**: Test / Analysis / Inspection / Demonstration  
✅ **Acceptance criteria are measurable and objective** (Given-When-Then format)  
✅ **Traceability established**: REQ → Test Plan → Test Method  

---

## 📌 Phase 03: Architecture (Test Design)

### IEEE 1012-2016 Architecture Requirements

During **Architecture Design**, V&V tasks include:

#### 1. System Integration Test Design
- **Purpose**: Specify the details of the **integration test approach**
- **Content**: How components will be integrated and tested together
- **Traceability**: Architecture components → Integration test strategy
- **Deliverable**: `03-architecture/test-design/integration-test-design.md`

**Key Sections**:
```markdown
1. Integration Strategy (bottom-up, top-down, big-bang, incremental)
2. Integration Order (which components integrated first)
3. Test Environment Setup (stubs, drivers, mocks)
4. Interface Testing Approach (component boundaries)
5. Integration Points and Dependencies
6. Test Data Requirements
```

#### 2. System Qualification Test Design
- **Purpose**: Elaborate on qualification test approach from Phase 02
- **Content**: Test design for system-level verification
- **Traceability**: System architecture → System test design
- **Deliverable**: `03-architecture/test-design/system-test-design.md`

**Key Sections**:
```markdown
1. System Test Approach (end-to-end scenarios)
2. Test Configuration (hardware, software, network)
3. Non-Functional Test Design (performance, security, scalability)
4. Test Tools and Automation Strategy
5. Test Data Generation Strategy
```

### TDD/Agile Approach: Architectural Spikes and System Metaphor

**In TDD, architecture is validated through**:
1. **Spike Solutions** - Time-boxed technical experiments to prove architectural assumptions
2. **Walking Skeleton** - Minimal end-to-end implementation to validate architecture
3. **System Metaphor** - Shared understanding of system structure

**Example Architectural Spike**:
```markdown
# Spike: Validate JWT Performance for 10,000 Concurrent Sessions

**ADR**: #78 (ADR-SECU-001: JWT Authentication)
**Hypothesis**: JWT token validation can handle 10,000 concurrent users with <50ms p95 latency
**Duration**: 2 days (time-boxed)

**Test Approach**:
1. Implement minimal JWT validation logic
2. Load test with JMeter (10,000 virtual users)
3. Measure p95, p99 latency
4. Validate against REQ-NF-PERF-002 (<200ms requirement)

**Success Criteria**:
- p95 latency <50ms
- p99 latency <100ms
- Zero token validation failures

**Result**: 
- p95: 28ms ✅
- p99: 67ms ✅
- Validation failures: 0 ✅
**Conclusion**: JWT architecture validated, proceed with ADR-SECU-001
```

### Why Test Design Happens in Architecture Phase

**Component Integration Planning**: By designing integration tests during architecture:
- ✅ **Architectural assumptions validated** - Prove architecture works before detailed design
- ✅ **Integration complexity revealed** - Identify difficult component interfaces early
- ✅ **Test environment requirements known** - Infrastructure needs understood upfront
- ✅ **Testability built into architecture** - Design for testability from the start

### Phase 03 Exit Criteria (Testing Perspective)

✅ **System Integration Test Design complete** (integration strategy, order, test points defined)  
✅ **System Qualification Test Design complete** (system test approach elaborated)  
✅ **Architectural spikes completed** (key technical assumptions validated)  
✅ **Test automation strategy defined** (tools, frameworks, CI/CD integration)  
✅ **Testability reviewed**: All components have clear test interfaces  

---

## 📌 Phase 04: Design (Test Cases)

### IEEE 1012-2016 Design Requirements

During **Detailed Design**, V&V tasks include:

#### 1. Test Cases
- **Purpose**: Specify inputs, predicted results, and execution conditions for each test
- **Content**: Detailed test scenarios at unit, integration, and system levels
- **Traceability**: Design elements → Test cases
- **Deliverable**: `04-design/test-cases/TC-*.md` or GitHub Issues with `type:test-case`

**Test Case Format** (IEEE 829-2008):
```markdown
# Test Case: TC-F-AUTH-001-001

**Requirement**: REQ-F-AUTH-001 (User Login)
**Design Element**: AuthenticationService.authenticate() method
**Test Type**: Unit Test

## Test Case Specification

### Objective
Verify that AuthenticationService.authenticate() returns valid JWT token for valid credentials.

### Preconditions
- User "alice" exists in database
- Password is "SecurePass1!" (hashed in DB)
- Database connection is active

### Test Data
| Input | Value |
|-------|-------|
| username | "alice" |
| password | "SecurePass1!" |

### Test Steps
1. Call `AuthenticationService.authenticate("alice", "SecurePass1!")`
2. Capture returned result

### Expected Results
| Condition | Expected |
|-----------|----------|
| Result type | JWT token (string) |
| Token validity | Valid for 24 hours |
| Token claims | username="alice", role="admin" |
| Database query count | Exactly 1 (user lookup) |
| Execution time | <50ms |

### Actual Results
[To be filled during execution in Phase 05]

### Status
[PASS / FAIL / BLOCKED / NOT RUN]

### Notes
[Any deviations or observations]
```

### TDD/Agile Approach: Write Tests BEFORE Code

**In TDD, the test IS the design activity:**

**Red-Green-Refactor Cycle**:
1. **RED**: Write failing test (defines interface and expected behavior)
2. **GREEN**: Write minimal code to pass test
3. **REFACTOR**: Improve design while keeping tests green

**Example TDD Test Case** (written BEFORE implementation):
```python
# File: 04-design/test-cases/test_authentication_service.py
# Written DURING Phase 04 (Design), BEFORE implementation in Phase 05

import unittest
from datetime import datetime, timedelta
from authentication_service import AuthenticationService

class TestAuthenticationService(unittest.TestCase):
    """
    Test cases for AuthenticationService (REQ-F-AUTH-001)
    
    Design Decision: Using JWT tokens with 24-hour expiration
    Reference: ADR-SECU-001 (#78)
    """
    
    def setUp(self):
        """Test fixture setup - runs before each test"""
        self.auth_service = AuthenticationService()
        # Test user: alice / SecurePass1!
        self.valid_username = "alice"
        self.valid_password = "SecurePass1!"
        self.invalid_password = "WrongPassword"
    
    def test_authenticate_with_valid_credentials(self):
        """TC-F-AUTH-001-001: Valid credentials return JWT token"""
        # ACT
        result = self.auth_service.authenticate(
            self.valid_username, 
            self.valid_password
        )
        
        # ASSERT
        self.assertIsNotNone(result, "Token should not be None")
        self.assertIsInstance(result.token, str, "Token should be string")
        self.assertEqual(result.username, "alice")
        self.assertEqual(result.role, "admin")
        
        # Verify token expiration is ~24 hours
        expected_expiry = datetime.utcnow() + timedelta(hours=24)
        self.assertAlmostEqual(
            result.expires_at.timestamp(),
            expected_expiry.timestamp(),
            delta=60  # Within 1 minute tolerance
        )
    
    def test_authenticate_with_invalid_password(self):
        """TC-F-AUTH-001-002: Invalid password raises AuthenticationError"""
        # ACT & ASSERT
        with self.assertRaises(AuthenticationError) as context:
            self.auth_service.authenticate(
                self.valid_username, 
                self.invalid_password
            )
        
        # Verify error message
        self.assertIn("Invalid credentials", str(context.exception))
    
    def test_authenticate_with_nonexistent_user(self):
        """TC-F-AUTH-001-003: Non-existent user raises AuthenticationError"""
        with self.assertRaises(AuthenticationError):
            self.auth_service.authenticate("nonexistent", "password")
    
    def test_authenticate_performance(self):
        """TC-NF-PERF-001: Authentication completes in <50ms"""
        import time
        
        start = time.perf_counter()
        self.auth_service.authenticate(self.valid_username, self.valid_password)
        duration = time.perf_counter() - start
        
        self.assertLess(duration, 0.050, f"Authentication took {duration*1000:.2f}ms, expected <50ms")
```

**Key Point**: This test is written **BEFORE** `authentication_service.py` exists. Writing the test forces design decisions:
- What parameters does `authenticate()` accept?
- What does it return? (Result object with token, username, role, expiry)
- What exceptions does it raise? (AuthenticationError)
- What performance is acceptable? (<50ms)

**If the test is hard to write, it's a design smell:**
- Tight coupling → Need to inject dependencies
- God class → Need to extract smaller classes
- Hidden state → Need to make dependencies explicit

### Why Test Cases Are Created in Design Phase

**Tests Drive Design Decisions**: By writing test cases during design:
- ✅ **Interfaces clarified** - Test forces you to define clear method signatures
- ✅ **Edge cases identified** - Test thinking reveals missing requirements
- ✅ **Design feedback** - Difficult tests indicate design problems (coupling, cohesion)
- ✅ **Documentation created** - Tests document expected behavior
- ✅ **Implementation guided** - Developers know exactly what to build

### Phase 04 Exit Criteria (Testing Perspective)

✅ **Test cases created for all design elements** (classes, methods, interfaces)  
✅ **Test cases cover**: Happy path, alternative paths, error paths, boundary conditions  
✅ **TDD tests written** (failing tests that define implementation contracts)  
✅ **Test data identified** (inputs, expected outputs, preconditions, postconditions)  
✅ **Traceability established**: Design Element → Test Case → Requirement  
✅ **Test coverage targets defined** (e.g., >80% line coverage, >70% branch coverage)  

---

## 📌 Phase 05: Implementation (Test Procedures & Execution)

### IEEE 1012-2016 Implementation Requirements

During **Implementation**, V&V tasks include:

#### 1. Test Procedures
- **Purpose**: Step-by-step instructions for executing test cases
- **Content**: Detailed procedure for setting up, running, and verifying tests
- **Traceability**: Test cases → Test procedures
- **Deliverable**: `05-implementation/test-procedures/TP-*.md` or automated test scripts

**Test Procedure Example**:
```markdown
# Test Procedure: TP-AUTH-001

**Test Cases Covered**: TC-F-AUTH-001-001 to TC-F-AUTH-001-010

## Setup Procedure
1. Start database: `docker-compose up -d postgres`
2. Run migrations: `npm run db:migrate`
3. Seed test data: `npm run db:seed:test`
4. Verify database state: `npm run db:verify`

## Execution Procedure
1. Run test suite: `npm test -- tests/authentication.test.js`
2. Observe test output (all tests should pass)
3. Generate coverage report: `npm run coverage`

## Verification Procedure
1. All tests PASS (green checkmarks)
2. Coverage report shows:
   - Line coverage ≥80%
   - Branch coverage ≥70%
   - Function coverage ≥90%
3. No errors in console output

## Cleanup Procedure
1. Stop database: `docker-compose down`
2. Clear test data: `npm run db:clean`

## Expected Duration
- Setup: 2 minutes
- Execution: 30 seconds
- Verification: 1 minute
- Total: ~4 minutes
```

#### 2. Test Execution
- **Purpose**: Run test procedures and record results
- **Content**: Actual vs. expected results, pass/fail status, defects found
- **Traceability**: Test procedure → Test results
- **Deliverable**: Test execution logs, CI/CD pipeline results

### TDD/Agile Approach: Red-Green-Refactor

**Implementation IS driven by tests:**

**TDD Cycle** (every 5-10 minutes):
```
1. RED: Write failing test (defines what to build)
   └─> Run tests: FAIL (expected)

2. GREEN: Write minimal code to pass test
   └─> Run tests: PASS (write simplest code)

3. REFACTOR: Improve design while keeping tests green
   └─> Run tests: PASS (maintain green bar)

4. COMMIT: Save working code with test
   └─> CI runs all tests: PASS
```

**Example TDD Session**:
```python
# Phase 05 Implementation Session Log (30 minutes)

# MINUTE 0-5: RED - Write test for password validation
def test_password_too_short_rejected():
    with pytest.raises(ValidationError) as exc:
        validate_password("abc")
    assert "at least 8 characters" in str(exc)

# Run: FAIL (validate_password doesn't exist)

# MINUTE 5-10: GREEN - Implement minimal password validation
def validate_password(password: str) -> None:
    if len(password) < 8:
        raise ValidationError("Password must be at least 8 characters")

# Run: PASS

# MINUTE 10-15: RED - Add test for missing special character
def test_password_without_special_char_rejected():
    with pytest.raises(ValidationError) as exc:
        validate_password("password123")
    assert "special character" in str(exc)

# Run: FAIL (no special char validation)

# MINUTE 15-20: GREEN - Add special character check
import re

def validate_password(password: str) -> None:
    if len(password) < 8:
        raise ValidationError("Password must be at least 8 characters")
    if not re.search(r"[!@#$%^&*(),.?\":{}|<>]", password):
        raise ValidationError("Password must contain special character")

# Run: PASS

# MINUTE 20-25: REFACTOR - Extract regex constant, improve error messages
PASSWORD_SPECIAL_CHARS = r"[!@#$%^&*(),.?\":{}|<>]"
MIN_PASSWORD_LENGTH = 8

def validate_password(password: str) -> None:
    """Validate password meets security requirements.
    
    Requirements: REQ-NF-SEC-003
    Raises: ValidationError if password invalid
    """
    errors = []
    
    if len(password) < MIN_PASSWORD_LENGTH:
        errors.append(f"Must be at least {MIN_PASSWORD_LENGTH} characters")
    
    if not re.search(PASSWORD_SPECIAL_CHARS, password):
        errors.append("Must contain at least one special character")
    
    if errors:
        raise ValidationError(f"Password validation failed: {'; '.join(errors)}")

# Run: PASS

# MINUTE 25-30: COMMIT and push to CI
git add tests/test_password.py src/password_validator.py
git commit -m "feat: password validation (REQ-NF-SEC-003) (#92)"
git push origin feature/password-validation

# CI runs ALL tests (unit + integration): PASS ✅
```

### Why Test Execution Happens During Implementation

**Continuous Verification**: By executing tests during implementation:
- ✅ **Immediate feedback** - Developers know instantly if code works
- ✅ **Regression prevention** - Existing tests prevent breaking changes
- ✅ **Confidence to refactor** - Green tests enable fearless refactoring
- ✅ **Living documentation** - Tests demonstrate how to use the code

### Phase 05 Exit Criteria (Testing Perspective)

✅ **All TDD unit tests pass** (100% of written tests green)  
✅ **Test coverage targets met** (≥80% line coverage, ≥70% branch coverage)  
✅ **No failing tests** (CI build is green)  
✅ **Test procedures documented** (automated or manual steps)  
✅ **Test execution logs captured** (CI/CD pipeline artifacts)  
✅ **Code reviewed with tests** (PR includes tests for all changes)  

---

## 📌 Phase 06: Integration (Integration Test Execution)

### IEEE 1012-2016 Integration Requirements

During **Integration**, V&V tasks include:

#### 1. Integration Test Execution
- **Purpose**: Verify components work together correctly
- **Content**: Execute integration test design from Phase 03
- **Traceability**: Integration points → Integration test results
- **Deliverable**: Integration test reports

**Integration Test Example**:
```python
# File: 06-integration/tests/test_authentication_integration.py

import pytest
from app import create_app
from database import db

@pytest.fixture
def client():
    """Integration test fixture with real database"""
    app = create_app('testing')
    with app.test_client() as client:
        with app.app_context():
            db.create_all()
            yield client
            db.drop_all()

def test_login_endpoint_with_database_integration(client):
    """Integration test: Login endpoint + AuthService + Database"""
    # ARRANGE: Create test user in database
    client.post('/api/users', json={
        'username': 'alice',
        'password': 'SecurePass1!',
        'role': 'admin'
    })
    
    # ACT: Login via HTTP endpoint
    response = client.post('/api/auth/login', json={
        'username': 'alice',
        'password': 'SecurePass1!'
    })
    
    # ASSERT: Full integration validated
    assert response.status_code == 200
    assert 'token' in response.json
    assert response.json['username'] == 'alice'
    assert response.json['role'] == 'admin'
    
    # Verify token is valid by making authenticated request
    token = response.json['token']
    profile_response = client.get(
        '/api/users/me',
        headers={'Authorization': f'Bearer {token}'}
    )
    assert profile_response.status_code == 200
```

### Phase 06 Exit Criteria (Testing Perspective)

✅ **All integration tests pass** (components work together)  
✅ **Integration test coverage complete** (all integration points tested)  
✅ **System integration test execution complete** (end-to-end scenarios validated)  
✅ **Integration defects resolved** (no open integration bugs)  

---

## 📌 Phase 07: Verification & Validation (Acceptance Testing)

### IEEE 1012-2016 V&V Requirements

During **Verification & Validation**, V&V tasks include:

#### 1. Acceptance Test Execution
- **Purpose**: Validate system meets stakeholder needs
- **Content**: Execute acceptance tests defined in Phase 02
- **Traceability**: StR → Acceptance test results
- **Deliverable**: Acceptance test report, V&V report

#### 2. Qualification Test Execution
- **Purpose**: Verify all system requirements met
- **Content**: Execute system qualification tests
- **Traceability**: REQ → Qualification test results
- **Deliverable**: Verification evidence, traceability matrix

### Phase 07 Exit Criteria (Testing Perspective)

✅ **All acceptance tests pass** (stakeholders validate system)  
✅ **All qualification tests pass** (requirements verified)  
✅ **Verification evidence complete** (objective proof for all requirements)  
✅ **V&V report approved** (formal verification sign-off)  
✅ **No critical or high-severity defects open**  

---

## 🎯 Key Takeaways

### Testing Is NOT "Just Phase 07"

**Testing artifacts are created throughout the lifecycle:**
- **Phase 02**: Test Plans (WHAT to test, HOW to measure)
- **Phase 03**: Test Design (test approach and strategy)
- **Phase 04**: Test Cases (detailed scenarios)
- **Phase 05**: Test Execution (run tests, implement code)
- **Phase 06**: Integration Testing (verify components together)
- **Phase 07**: Acceptance Testing (validate with stakeholders)

### Tests Drive Requirements Clarity (Phase 02)

**Writing acceptance tests exposes vague requirements:**
- If you can't write a test, the requirement is incomplete
- Executable specs (Gherkin) become living documentation
- Stakeholders validate by reviewing test scenarios

### Tests Drive Design Decisions (Phase 04)

**TDD forces good design:**
- Writing tests BEFORE code clarifies interfaces
- Difficult-to-test code indicates design problems (coupling, cohesion)
- Tests serve as design documentation

### Tests Enable Confident Refactoring (Phase 05)

**Green tests = safety net:**
- Refactor without fear of breaking things
- Immediate feedback if something breaks
- Continuous integration prevents regressions

### Tests Prove Requirements Met (Phase 07)

**Verification evidence:**
- Objective proof that system meets requirements
- Traceability from requirement → test → result
- Stakeholder acceptance based on test results

---

## 📚 Standards References

- **IEEE 1012-2016** - System, Software, and Hardware Verification and Validation
- **IEEE 829-2008** - Software and System Test Documentation
- **ISO/IEC/IEEE 29119** - Software Testing Standard
- **ISO/IEC/IEEE 29148:2018** - Requirements Engineering (Section on Verification)
- **Kent Beck** - Test-Driven Development: By Example
- **Martin Fowler** - Continuous Integration, Refactoring

---

**Remember**: Testing is a lifecycle-parallel activity. Plan tests in Phase 02, design tests in Phase 03-04, execute tests in Phase 05-07. Tests are NOT an afterthought!
