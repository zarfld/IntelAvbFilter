---
description: "Phase 07 guidance for verification and validation following IEEE 1012-2016. Covers test planning, execution, traceability, acceptance testing, and defect management."
applyTo: "07-verification-validation/**"
---

# Phase 07: Verification & Validation (V&V)

**Standards**: IEEE 1012-2016 (System, Software, and Hardware Verification and Validation)  
**XP Integration**: Acceptance Testing, Customer Tests, Test-Driven Development

## 🎯 Phase Objectives

1. Verify software against requirements and design
2. Validate that software meets stakeholder needs
3. Execute comprehensive test plans
4. Ensure requirements traceability
5. Perform acceptance testing with customer
6. Document test results and defects
7. **Report test results with courage and honesty** - See [Critical Self-Reflection Guide](../../docs/critical-self-reflection-honest-reporting.md)

## 📋 Test Case Documentation Approach

### ⭐ PRIMARY: GitHub Issues (Recommended)

**Test cases should be captured as GitHub Issues** using the Test Case (TEST) template.

#### Creating Test Cases as GitHub Issues

1. **Navigate to Issues → New Issue**
2. **Select Template**: "Test Case (TEST)"
3. **Complete Required Fields**:
   - **Title**: Test case description (e.g., "Verify user can login with valid credentials")
   - **Verified Requirements**: Link to requirements using `#N` syntax
   - **Test Type**: Unit / Integration / System / Acceptance / Performance / Security
   - **Test Steps**: Detailed procedure
   - **Expected Results**: What should happen
   - **Test Data**: Required data/fixtures
   - **Priority**: Critical / High / Medium / Low
   
4. **Apply Labels**: `test-case`, `phase-07`, `verify-test`
5. **Submit** → Issue assigned (e.g., #120)

**Example TEST Issue**:

**Title**: TEST-AUTH-LOGIN-001: Verify user authentication with valid credentials

**Verified Requirements**:
```markdown
Verifies:
- #45 (REQ-F-AUTH-001: User Login)
- #46 (REQ-NF-SECU-002: Session Security)
```

**Test Type**: Integration

**Test Steps**:
```markdown
1. Navigate to login page
2. Enter valid email: test@example.com
3. Enter valid password: SecurePass123!
4. Click "Login" button
5. Observe redirect and token generation
```

**Expected Results**:
```markdown
- User is authenticated successfully
- Access token and refresh token are generated
- User is redirected to dashboard
- Session is created with 15-minute expiry
- Tokens are stored securely (httpOnly cookie)
```

**Test Data**:
```markdown
User: { email: 'test@example.com', password: 'SecurePass123!' }
Expected: 200 OK response with tokens
```

**Priority**: Critical (P0)

**Test Results**: Track in issue comments:
```markdown
## Test Run: 2025-11-12 14:30 UTC
**Status**: ✅ PASS
**Environment**: Staging
**Tester**: @johndoe
**Build**: v1.2.3-rc1
**Duration**: 2.5s
**Notes**: All acceptance criteria met
```

#### Traceability: Tests → Requirements

```markdown
## Traceability
- **Verifies**: #45, #46 (requirements)
- **Test Suite**: Integration Tests
- **Automated**: Yes (src/tests/integration/auth/login.spec.ts)
- **Coverage**: Lines 92%, Branches 88%
```

## 📋 IEEE 1012-2016 Compliance

### V&V Process Overview

**Verification**: "Are we building the product right?"
- Confirms that work products properly reflect requirements
- Technical correctness evaluation
- **Tracked via**: TEST issues with `verify-test` label

**Validation**: "Are we building the right product?"
- Confirms that product fulfills intended use
- Fitness for purpose evaluation
- **Tracked via**: Acceptance test issues with `verify-demonstration` label

### V&V Activities by Lifecycle Phase

1. **Management V&V Activities**
   - V&V planning
   - Resource allocation
   - Risk management
   - Test environment setup

2. **Verification Activities**
   - Requirements verification
   - Design verification
   - Code verification (unit tests)
   - Integration verification

3. **Validation Activities**
   - Acceptance testing
   - Operational readiness
   - Customer validation

## 🎨 XP Practices for V&V

### Acceptance Testing
**Customer-Defined Tests**:
- Customer writes acceptance tests
- Executable specifications
- Automated acceptance test suites
- Tests run continuously

**Acceptance Test Format**:
```gherkin
Feature: User Registration
  As a new user
  I want to register for an account
  So that I can access the system

  Scenario: Successful registration with valid data
    Given I am on the registration page
    When I enter valid registration information:
      | username | newuser           |
      | email    | new@example.com   |
      | password | SecurePass123!    |
    And I submit the registration form
    Then I should see a success message
    And I should receive a welcome email
    And my account should be created in the system
```

### Test-First Development
- Write tests before features
- Acceptance tests define "done"
- All tests must pass for acceptance

## 📝 Required Deliverables

### 1. Verification & Validation Plan (V&V Plan)
**Location**: `vv-plan.md`

```markdown
# Verification & Validation Plan

## 1. Introduction
### 1.1 Purpose
### 1.2 Scope
### 1.3 V&V Objectives
- Verify compliance with IEEE 29148, 1016, 42010
- Validate against stakeholder requirements
- Ensure >80% test coverage (XP requirement)
- Zero critical defects at release

### 1.4 Reference Documents
- Stakeholder Requirements Specification (StRS)
- System Requirements Specification (SyRS)
- Architecture Description
- Software Design Description (SDD)

## 2. V&V Overview

### 2.1 Organization
- **V&V Team Lead**: [Name]
- **Test Engineers**: [Names]
- **Automation Engineers**: [Names]
- **Customer Representatives**: [Names]

### 2.2 V&V Tasks by Phase

| Phase | Verification Tasks | Validation Tasks |
|-------|-------------------|------------------|
| Requirements | Requirements review | Stakeholder validation |
| Architecture | Architecture review | Quality attributes validation |
| Design | Design review, Code inspection | N/A |
| Implementation | Unit tests, Code review | N/A |
| Integration | Integration tests | N/A |
| System | System tests | Acceptance tests |

### 2.3 V&V Schedule
[Gantt chart or timeline]

## 3. Verification Tasks

### 3.1 Requirements Verification
**Objective**: Verify SyRS against StRS

**Method**: Requirements review

**Criteria**:
- [ ] All stakeholder requirements traced to system requirements
- [ ] All system requirements testable
- [ ] No conflicting requirements
- [ ] All requirements have acceptance criteria

**Deliverable**: Requirements Verification Report

### 3.2 Design Verification
**Objective**: Verify design implements requirements

**Method**: Design review, traceability analysis

**Criteria**:
- [ ] All requirements addressed in design
- [ ] Design conforms to architecture
- [ ] Interfaces properly specified
- [ ] Design patterns appropriately applied

**Deliverable**: Design Verification Report

### 3.3 Code Verification
**Objective**: Verify code implements design

**Methods**:
- Static code analysis
- Code review
- Unit testing (TDD)

**Criteria**:
- [ ] Code implements design specifications
- [ ] Unit test coverage >80%
- [ ] No critical code smells
- [ ] Coding standards compliance
- [ ] Cyclomatic complexity <10

**Tools**:
- SonarQube
- ESLint/Pylint
- Jest/PyTest

**Deliverable**: Code Verification Report

### 3.4 Integration Verification
**Objective**: Verify component integration

**Method**: Integration testing

**Criteria**:
- [ ] All interfaces tested
- [ ] Component interactions verified
- [ ] External integrations tested
- [ ] Error handling verified

**Deliverable**: Integration Test Report

## 4. Validation Tasks

### 4.1 Acceptance Testing
**Objective**: Validate system meets stakeholder needs

**Method**: Customer acceptance tests

**Test Types**:
- Functional acceptance tests
- Non-functional acceptance tests (performance, usability, etc.)
- User acceptance testing (UAT)

**Acceptance Criteria**:
- [ ] All critical user stories accepted
- [ ] All acceptance tests passing
- [ ] Customer sign-off obtained
- [ ] No blocking defects

**Deliverable**: Acceptance Test Report

### 4.2 System Validation
**Objective**: Validate complete system

**Test Types**:
- End-to-end testing
- Regression testing
- Performance testing
- Security testing
- Usability testing

**Deliverable**: System Validation Report

## 5. Test Levels

### 5.1 Unit Testing (Verification)
- **Responsibility**: Developers (TDD)
- **Coverage Target**: >80%
- **Execution**: Automated, continuous
- **Tools**: Jest, PyTest, JUnit

### 5.2 Integration Testing (Verification)
- **Responsibility**: Developers
- **Coverage**: All component interfaces
- **Execution**: Automated, on integration
- **Tools**: Postman, TestContainers

### 5.3 System Testing (Verification)
- **Responsibility**: QA Team
- **Coverage**: All requirements
- **Execution**: Automated + Manual
- **Tools**: Selenium, Cypress, k6

### 5.4 Acceptance Testing (Validation)
- **Responsibility**: Customer + QA Team
- **Coverage**: All user stories
- **Execution**: Automated (BDD)
- **Tools**: Cucumber, SpecFlow

## 6. Test Environment

### 6.1 Test Environments
- **Unit Test**: Local dev environment
- **Integration Test**: CI environment with test containers
- **System Test**: Dedicated test environment
- **Acceptance Test**: Staging environment (production-like)

### 6.2 Test Data
- **Unit Tests**: Mocked data
- **Integration Tests**: Test fixtures
- **System Tests**: Anonymized production data
- **Acceptance Tests**: Customer-provided test data

## 7. Defect Management

### 7.1 Defect Classification
- **Critical**: System crash, data loss, security breach
- **High**: Major functionality broken
- **Medium**: Minor functionality issue
- **Low**: Cosmetic issue

### 7.2 Defect Workflow
```
New → Assigned → In Progress → Fixed → Verified → Closed
                              ↓ Reopen if not fixed
```

### 7.3 Exit Criteria
- Zero critical defects
- Zero high defects
- Medium/Low defects accepted by customer

## 8. Traceability

### 8.1 Requirements Traceability Matrix (RTM)
| Req ID | Test Case ID | Test Result | Status |
|--------|-------------|-------------|---------|
| REQ-F-001 | TC-001, TC-002 | Pass | Verified |
| REQ-F-002 | TC-003 | Pass | Verified |
| REQ-NF-001 | TC-P-001 | Pass | Verified |

### 8.2 Bi-directional Traceability
```
StR-XXX ← REQ-XXX ← TC-XXX (Test Cases)
                 ← CODE-XXX (Implementation)
```

## 9. Test Metrics

### 9.1 Coverage Metrics
- Requirements coverage: 100% of critical requirements tested
- Code coverage: >80% line coverage
- Branch coverage: >70%

### 9.2 Quality Metrics
- Defect density: < 1 defect per 1000 LOC
- Test pass rate: >95%
- Mean time to detect (MTTD): < 1 day
- Mean time to resolve (MTTR): < 3 days

### 9.3 Reliability Metrics and Evidence (IEEE 1633 5.4)
- Operational Profile (OP) model coverage: target ≥90% transitions
- Failure intensity and MTBF/MTBCF trends (per duty time)
- Availability with restore time (MTSWR) estimates
- Residual defects estimate and confidence bounds
- Model selection and fit quality (e.g., Musa-Okumoto, GO, Crow/AMSAA) with accuracy verification against most recent observed MTBF (see 5.4.7)

## 10. V&V Reporting

### 10.1 Test Reports
- Daily: Test execution summary
- Weekly: Defect status report
- Sprint end: Test coverage report
- Phase end: V&V Phase Report

### 10.2 V&V Deliverables
- [ ] V&V Plan
- [ ] Test Plans (unit, integration, system, acceptance)
- [ ] Test Cases
- [ ] Test Results
- [ ] Defect Reports
- [ ] Requirements Traceability Matrix
- [ ] V&V Summary Report
 - [ ] Reliability Evidence Package (OP coverage, SRG fits, estimates, accuracy verification)

## 11. Reliability Evidence and Release Decision (IEEE 1633 5.5)

Before recommending release, compile and review reliability evidence:

- OP-driven reliability test coverage meets target
- SRG model(s) fitted and validated (accuracy check within acceptable error)
- Estimated reliability and availability meet objectives at stated confidence
- Residual defects are within target; no open critical items in CIL (if SFMEA performed)
- Optional: Reliability Demonstration Test (RDT) plan/results if selected
```

### 2. Test Case Specifications
**Location**: `test-cases/TC-XXX-[name].md`

```markdown
# Test Case: TC-001 User Login with Valid Credentials

**Trace to**: REQ-F-005 (User Authentication)

## Test Information
- **Test ID**: TC-001
- **Test Type**: Functional
- **Test Level**: System Test
- **Priority**: Critical
- **Author**: [Name]
- **Date Created**: 2025-01-15

## Preconditions
- User account exists in system
- Username: `testuser`
- Password: `TestPass123!`
- User account is active

## Test Steps

| Step | Action | Expected Result | Actual Result | Status |
|------|--------|----------------|---------------|---------|
| 1 | Navigate to login page | Login page displays | | |
| 2 | Enter username: `testuser` | Username field populated | | |
| 3 | Enter password: `TestPass123!` | Password field shows dots | | |
| 4 | Click "Login" button | Loading indicator appears | | |
| 5 | | User redirected to dashboard | | |
| 6 | | Welcome message displays with username | | |

## Expected Results
- User successfully authenticated
- Session token created
- User redirected to dashboard
- Audit log entry created

## Postconditions
- User is logged in
- Session is active
- Last login timestamp updated

## Test Data
```json
{
  "username": "testuser",
  "password": "TestPass123!",
  "expectedUserId": "123e4567-e89b-12d3-a456-426614174000"
}
```

## Automation Script
```typescript
describe('TC-001: User Login', () => {
  it('should login user with valid credentials', async () => {
    await page.goto('/login');
    await page.fill('#username', 'testuser');
    await page.fill('#password', 'TestPass123!');
    await page.click('#login-button');
    
    await expect(page).toHaveURL('/dashboard');
    await expect(page.locator('.welcome-message')).toContainText('testuser');
  });
});
```

## Notes
- Test can be automated using Playwright
- Run in both Chrome and Firefox
- Test on mobile viewport as well
```

### 3. Acceptance Test Specifications (BDD)
**Location**: `test-cases/acceptance/user-registration.feature`

```gherkin
Feature: User Registration
  As a potential user
  I want to register for an account
  So that I can access the system
  
  Background:
    Given the system is running
    And the database is empty
  
  @critical @regression
  Scenario: Successful registration with valid information
    Given I am on the registration page
    When I fill in the registration form:
      | Field    | Value                |
      | Username | newuser              |
      | Email    | newuser@example.com  |
      | Password | SecurePass123!       |
    And I submit the registration form
    Then I should see a success message "Registration successful"
    And I should receive a welcome email at "newuser@example.com"
    And a user account should be created with:
      | Field    | Value                |
      | Username | newuser              |
      | Email    | newuser@example.com  |
      | Status   | Active               |
  
  @validation
  Scenario Outline: Registration validation errors
    Given I am on the registration page
    When I fill in the registration form:
      | Field    | Value      |
      | Username | <username> |
      | Email    | <email>    |
      | Password | <password> |
    And I submit the registration form
    Then I should see an error message "<error>"
    And no user account should be created
    
    Examples:
      | username | email           | password    | error                           |
      | ab       | valid@email.com | Pass123!    | Username must be 3-50 chars     |
      | validuser| invalidemail    | Pass123!    | Email must be valid format      |
      | validuser| valid@email.com | short       | Password must be at least 8 chars|
  
  @security
  Scenario: Duplicate username prevention
    Given a user exists with username "existinguser"
    When I try to register with username "existinguser"
    Then I should see an error "Username already exists"
    And no duplicate account should be created
  
  @performance
  Scenario: Registration performance
    Given I am on the registration page
    When I submit valid registration information
    Then the registration should complete within 2 seconds
```

### 4. Requirements Traceability Matrix
**Location**: `traceability/requirements-traceability-matrix.md`

```markdown
# Requirements Traceability Matrix (RTM)

## Purpose
Ensure all requirements are:
1. Implemented in design and code
2. Tested and verified
3. Validated by customer

## Traceability

### Functional Requirements

| StR ID | SyRS ID | Design ID | Code Module | Unit Tests | Integration Tests | System Tests | Acceptance Tests | Status |
|--------|---------|-----------|-------------|------------|------------------|--------------|-----------------|---------|
| StR-001 | REQ-F-001 | DES-C-001 | UserService | UT-001 | IT-001 | ST-001 | AT-001 | ✅ Verified |
| StR-001 | REQ-F-002 | DES-C-001 | UserValidator | UT-002 | IT-001 | ST-001 | AT-001 | ✅ Verified |
| StR-002 | REQ-F-003 | DES-C-002 | AuthService | UT-003, UT-004 | IT-002 | ST-002 | AT-002 | ✅ Verified |
| StR-003 | REQ-F-004 | DES-C-003 | PaymentService | UT-005 | IT-003 | ST-003 | AT-003 | ⚠️ In Progress |

### Non-Functional Requirements

| StR ID | SyRS ID | Test Type | Test ID | Target | Actual | Status |
|--------|---------|-----------|---------|--------|--------|---------|
| StR-010 | REQ-NF-001 | Performance | PERF-001 | <200ms | 150ms | ✅ Pass |
| StR-010 | REQ-NF-002 | Load | LOAD-001 | 10k TPS | 12k TPS | ✅ Pass |
| StR-011 | REQ-NF-003 | Security | SEC-001 | OWASP Top 10 | All pass | ✅ Pass |
| StR-012 | REQ-NF-004 | Usability | USA-001 | SUS >70 | SUS=75 | ✅ Pass |

## Coverage Summary

### Requirements Coverage
- Total Requirements: 42
- Tested Requirements: 40
- Coverage: 95.2%
- Untested: REQ-F-038, REQ-F-041 (low priority)

### Test Coverage
- Unit Test Coverage: 85%
- Integration Test Coverage: 78%
- System Test Coverage: 95%
- Acceptance Test Coverage: 100% (critical paths)

## Orphan Analysis

### Orphan Requirements (No Tests)
- None (all critical requirements tested)

### Orphan Tests (No Requirements)
- UT-042: Remove or link to requirement

## Gap Analysis
| Gap | Impact | Action Required |
|-----|--------|----------------|
| REQ-NF-005 not tested | Medium | Create performance test PERF-005 |
```

## � Reliability Engineering Activities (IEEE 1633)

### 1. Software Reliability Growth (SRG) Model Fitting

**Prompt**: Use `.github/prompts/srg-model-fit.prompt.md`

During Phase 07, fit SRG models to failure data collected in Phase 06:

**Step 1: Collect Failure Data**
From integration/system testing (Phase 06), you should have:
- Failure times (hours or test case numbers)
- Failure severity (per FDSC)
- Operation that failed (from OP)
- State when failure occurred (from MCUM)
- Fix status (boolean)

**Minimum Data Requirement**: M ≥ 20 failures for reliable model fitting

**Step 2: Perform Trend Tests**
Before fitting models, verify reliability is actually growing:

**Laplace Trend Test**:
```
u = [Σ(t_i) / M - T/2] / [T * sqrt(1/(12*M))]

Where:
  M = number of failures
  T = total test time
  t_i = time of i-th failure

Interpretation:
  u < -2: Reliability GROWING ✅ (proceed with modeling)
  -2 ≤ u ≤ 2: NO TREND ⚠️ (investigate, don't release)
  u > 2: Reliability DECLINING ❌ (serious problem, don't release)
```

**Arithmetic Mean (AM) Trend Test**:
- Calculate mean TBF (Time Between Failures) for early failures (e.g., failures 1-10)
- Calculate mean TBF for later failures (e.g., failures 11-20)
- If mean TBF increasing → reliability growing ✅
- If mean TBF flat/decreasing → reliability not growing ⚠️

**Step 3: Fit Multiple SRG Models**

Select and fit 3-4 models using Maximum Likelihood Estimation (MLE):

**Model 1: Goel-Okumoto (Finite Failures)**
```
μ(t) = N₀ * (1 - e^(-b*t))   [Expected cumulative failures]
λ(t) = N₀ * b * e^(-b*t)       [Failure intensity]

Parameters to estimate:
  N₀ = initial number of defects
  b = defect detection rate (1/hour)
```

**Model 2: Musa-Okumoto (Infinite Failures)**
```
λ(t) = λ₀ / (1 + θ*t)          [Failure intensity]
μ(t) = (1/θ) * ln(1 + λ₀*θ*t)  [Expected cumulative failures]

Parameters to estimate:
  λ₀ = initial failure intensity
  θ = failure intensity decay parameter
```

**Model 3: Jelinski-Moranda (Simple Finite)**
```
λ_i = φ * (N₀ - (i-1))   [Failure intensity after (i-1) failures]

Parameters to estimate:
  N₀ = initial number of defects
  φ = hazard rate per defect
```

**Model 4: Crow/AMSAA (Non-parametric)**
```
λ(t) = λ * β * t^(β-1)     [Failure intensity]
μ(t) = λ * t^β             [Expected cumulative failures]

Parameters to estimate:
  λ = scale parameter
  β = shape parameter (β < 1: growing, β > 1: declining)
```

**Step 4: Assess Goodness-of-Fit**

Calculate for each model:
- **SSE (Sum of Squared Errors)**: Lower is better
- **R² (Coefficient of Determination)**: Higher is better (R² > 0.9 = excellent fit)
- **AIC (Akaike Information Criterion)**: Lower is better

Select best-fit model based on lowest SSE/AIC and highest R².

**Step 5: Calculate Current Reliability Metrics**

Using best-fit model:
```
Current MTBF = 1 / λ(T)   [Hours between failures]
Current Failure Rate = λ(T)  [Failures per hour]
Residual Defects = N₀ - μ(T)  [For finite models]
```

**Step 6: Make Predictions**

**Time to reach target MTBF**:
```
Solve: 1 / λ(t) = Target MTBF
  → Calculate t_target
  → Additional test time = t_target - T_current
```

**Expected failures in next period**:
```
Next Δt hours → Expected failures = μ(T + Δt) - μ(T)
```

**Deliverable**: 
- Complete SRG Analysis Report
- Model parameters (N₀, b, λ₀, θ, φ, β, etc.)
- Current MTBF estimate with confidence interval
- Prediction: time to reach target MTBF
- Prediction: residual defects
- Goodness-of-fit assessment
- Model validation (prequential likelihood)

**Location**: `07-verification-validation/test-results/srg-analysis-[Version]-[Date].md`

### 2. Release Decision Analysis

**Prompt**: Use `.github/prompts/reliability-release-decision.prompt.md`

At the end of Phase 07, make evidence-based release decision:

**Step 1: Gather Reliability Evidence**
- SRG analysis results (MTBF, failure rate, residual defects)
- Test results (pass rates, coverage)
- SFMEA CIL status (% complete)
- Quality gate results (all phases)

**Step 2: Evaluate Quality Gates** (from SRPP Section 4)

| Phase | Quality Gate | Threshold | Status |
|-------|--------------|-----------|--------|
| Phase 05 | Defect Discovery Rate | < [X] def/KLOC | [✅/❌] |
| Phase 06 | Integration Pass Rate | ≥ 95% | [✅/❌] |
| Phase 07 | Estimated MTBF | ≥ [Target] hours | [✅/❌] |
| Phase 08 | Acceptance Pass Rate | 100% | [✅/❌] |

**Step 3: Check Mandatory Release Criteria**

**ALL of the following MUST be met**:
- [ ] All critical defects fixed (FDSC Severity = 10, count = 0)
- [ ] CIL 100% complete (all high-RPN items mitigated and verified)
- [ ] Acceptance tests 100% passed
- [ ] SRG trend positive (Laplace u < -2, reliability growing)
- [ ] Target MTBF achieved (current MTBF ≥ target)
- [ ] Security vulnerabilities addressed (all critical/high)
- [ ] User documentation complete
- [ ] Deployment plan approved
- [ ] Rollback plan tested
- [ ] Stakeholder sign-off obtained

**Step 4: Make Release Decision**

**Scenario A: ✅ GO FOR RELEASE**
- All mandatory criteria met (10/10)
- All quality gates passed (4/4)
- Target MTBF achieved
- SRG trend strongly positive
- Low risk

**Scenario B: ⏳ CONDITIONAL GO**
- Most criteria met (8-9/10)
- Some quality gates passed (3/4)
- MTBF close to target (gap < 20%)
- SRG trend weakly positive
- Medium risk, specific conditions required

**Scenario C: ❌ NO-GO**
- Critical criteria NOT met (< 8/10)
- Quality gates failed (< 3/4)
- MTBF significantly below target (gap > 20%)
- SRG trend flat or negative
- High risk, additional work required

**Step 5: Risk Assessment**

Identify release risks:
| Risk | Likelihood | Impact | Risk Level | Mitigation |
|------|------------|--------|------------|------------|
| Critical defect in production | Low/Med/High | Critical | Red/Yellow/Green | [Plan] |
| MTBF lower than predicted | Low/Med/High | High | Red/Yellow/Green | [Plan] |
| Performance degrades under load | Low/Med/High | High | Red/Yellow/Green | [Plan] |

Ensure rollback plan is ready and tested.

**Step 6: Obtain Stakeholder Approval**

| Stakeholder | Role | Decision | Comments |
|-------------|------|----------|----------|
| Product Owner | [Name] | [Go/No-Go/Conditional] | |
| Engineering Manager | [Name] | [Go/No-Go/Conditional] | |
| QA Lead | [Name] | [Go/No-Go/Conditional] | |
| Reliability Engineer | [Name] | [Go/No-Go/Conditional] | |
| Security Lead | [Name] | [Go/No-Go/Conditional] | |

**Deliverable**: 
- Complete Release Decision Report
- Go/Conditional/No-Go recommendation with rationale
- Risk assessment with mitigation plans
- Stakeholder sign-off
- Post-release monitoring plan

**Location**: `07-verification-validation/test-results/release-decision-[Version]-[Date].md`

### 3. Reliability Demonstration Test (RDT) - Optional

**Purpose**: Statistically demonstrate that target reliability has been achieved

**When Required**: If customer/stakeholder requires statistical confidence (e.g., safety-critical systems)

**RDT Parameters**:
- **Target MTBF**: [X] hours (from SRPP)
- **Confidence Level**: 90% (typical) or 95% (high confidence)
- **Discrimination Ratio**: 2.0 (typical) - ratio of acceptable to rejectable MTBF
- **Test Duration**: Calculate based on target MTBF and confidence
- **Allowed Failures**: Calculate based on parameters

**RDT Procedure**:
1. Run system under operational profile for test duration
2. Count failures
3. Compare to allowed failures:
   - If failures ≤ allowed → **PASS** (target MTBF demonstrated)
   - If failures > allowed → **FAIL** (target MTBF NOT demonstrated)

**RDT Example** (Target MTBF = 200 hours, Confidence = 90%, Discrimination = 2.0):
```
Test Duration = 300 hours
Allowed Failures = 2

Result: Observed 1 failure → PASS ✅
Result: Observed 3 failures → FAIL ❌
```

**Deliverable**: RDT report with pass/fail result (if RDT performed)

### 4. V&V Traceability Matrix Update

Update Architecture Traceability Matrix to include reliability evidence:

| Requirement | Design | Implementation | Test | SRG Evidence |
|-------------|--------|---------------|------|--------------|
| REQ-REL-001: MTBF ≥ 200 hr | ARCH-001 | UserService | TC-XXX | SRG: MTBF = 250 hr ✅ |
| REQ-REL-002: λ ≤ 0.005 fail/hr | ARCH-002 | PaymentGateway | TC-YYY | SRG: λ = 0.004 fail/hr ✅ |

**Deliverable**: Updated Architecture Traceability Matrix with reliability evidence

### 5. Defect Analysis and Lessons Learned

**Defect Root Cause Analysis**:
- Review all defects found in Phase 06-07
- Classify by root cause (requirements, design, implementation, integration)
- Identify patterns (e.g., "most defects in payment module")
- Propose preventive actions for next release

**Defect Profile**:
| Root Cause | Count | % | Preventive Action |
|------------|-------|---|-------------------|
| Requirements | [N] | [%] | Improve requirements reviews |
| Design | [N] | [%] | More design reviews, SFMEA earlier |
| Implementation | [N] | [%] | More code reviews, TDD enforcement |
| Integration | [N] | [%] | Better integration testing |

**Lessons Learned**:
- What went well in reliability engineering?
- What could be improved?
- Actions for next release

**Deliverable**: Defect analysis report with lessons learned

### 6. Corrective-Action Loop (V&V)

For each verified gap/anomaly discovered during V&V, execute a full corrective-action loop to route the issue back to the originating lifecycle phase, implement the fix with tests-first, re-integrate, and re-verify.

**Prompt**: Use `.github/prompts/corrective-action-loop.prompt.md`

Key steps:
- Document the anomaly and classify severity/integrity level
- Perform root cause analysis; identify origin phase (requirements/design/code)
- Write failing unit and system/integration tests that reproduce the defect (TDD)
- Apply minimal-scoped fix; treat as development (requirements → design → code → test)
- Execute impact-based regression suite and reliability checks (if affected)
- Update traceability (Requirement ↔ Design ↔ Code ↔ Test ↔ CAP ↔ SFMEA)
- Close with objective evidence (CI runs, reports) and stakeholder sign-off

## �🚨 Critical Requirements for This Phase

### Always Do
✅ Maintain complete requirements traceability  
✅ Execute all test levels (unit, integration, system, acceptance)  
✅ Involve customer in acceptance testing (customer writes/defines tests)  
✅ Automate tests where possible (especially acceptance tests)  
✅ Document all test results  
✅ Track and resolve all defects  
✅ Verify >80% code coverage  
✅ Validate against stakeholder needs  
✅ Practice TDD: Red → Green → Refactor (write failing test BEFORE any code)  
✅ Test everything that could possibly break  
✅ Ensure all tests run flawlessly  
✅ Make acceptance criteria executable  
✅ Define acceptance tests BEFORE implementation  
✅ Perform V&V in parallel with all lifecycle stages (not just at end)  
✅ Test scenarios, stress/boundary conditions, and fault recovery  
✅ Verify products conform to requirements (correctness, completeness, consistency)  
✅ Validate products satisfy intended use and user needs  

### Never Do
❌ Skip acceptance tests  
❌ Release with critical defects  
❌ Test without traceability  
❌ Ignore non-functional requirements  
❌ Skip customer validation  
❌ Disable failing tests  
❌ Write new functionality without a failing test first  
❌ Let time pressure cause you to skip tests ("Test Later" = "Test Never")  
❌ Promote prototypes to production without careful validation  
❌ Use unreliable test conditions (exact timestamps, specific error wording)  
❌ Perform V&V only at conclusion of phases (must be in parallel)  

## 📊 Phase Exit Criteria

✅ V&V Plan executed completely  
✅ All test levels completed  
✅ Requirements traceability verified (100%)  
✅ Test coverage >80%  
✅ Zero critical defects  
✅ Zero high-priority defects  
✅ Customer acceptance obtained  
✅ All acceptance tests passing  
✅ V&V Summary Report approved  
✅ Reliability evidence reviewed; release decision supported per IEEE 1633 5.5  

**Reliability exit criteria** (IEEE 1633):
✅ **SRG analysis complete** (using srg-model-fit.prompt.md):
  - Failure data collected (M ≥ 20 failures recommended)
  - Trend test passed (Laplace u-statistic < -2, reliability growing)
  - Multiple SRG models fitted (3-4 models: Goel-Okumoto, Musa-Okumoto, Jelinski-Moranda, Crow/AMSAA)
  - Best-fit model selected (lowest SSE/AIC, highest R² > 0.9)
  - Goodness-of-fit assessment documented
✅ **Current MTBF calculated** with confidence interval  
✅ **Target MTBF achieved** (current MTBF ≥ target MTBF from SRPP) OR additional test time calculated  
✅ **Reliability predictions documented**:
  - Time to reach target MTBF
  - Residual defects estimate (for finite models)
  - Expected failures in next period
✅ **All mandatory release criteria met** (10/10 from reliability-release-decision.prompt.md):
  - All critical defects fixed (FDSC Severity = 10, count = 0)
  - CIL 100% complete (all high-RPN items mitigated and verified)
  - Acceptance tests 100% passed
  - SRG trend positive (u < -2)
  - Target MTBF achieved
  - Security vulnerabilities addressed
  - Documentation complete
  - Deployment plan approved
  - Rollback plan tested
  - Stakeholder sign-off obtained
✅ **Release decision report complete** (using reliability-release-decision.prompt.md):
  - Quality gate assessment (all phases 05-08)
  - Go/Conditional/No-Go recommendation with rationale
  - Risk assessment with mitigation plans
  - Stakeholder approval table completed
  - Post-release monitoring plan defined
✅ **Architecture Traceability Matrix updated** with reliability evidence (SRG MTBF/λ)  
✅ **Defect analysis complete** with root cause classification and lessons learned  
✅ **(Optional) RDT passed** if required (Reliability Demonstration Test with statistical confidence)  

## 📊 Honest Test Result Reporting

### Critical Principle: Report Truth, Not Hope

**XP Value - Courage**: "Tell the truth straight out, whether the data is **good or bad**."

#### Test Report Template

```markdown
## Test Report - [Iteration/Build ID]

### Summary
- **Total Tests**: 1,245
- **Passing**: 1,198 ✅ (96.2%)
- **Failing**: 47 ❌ (3.8%)
- **Trend**: Failing tests increased from 12 (1%) last week 🚨

### Failed Tests by Category
| Category | Count | Priority | Root Cause |
|----------|-------|----------|------------|
| Integration (API) | 25 | 🚨 High | Third-party API changed format |
| Unit (OrderProcessor) | 15 | 🔴 High | Refactoring introduced regression |
| E2E (Checkout flow) | 5 | 🟡 Medium | Flaky tests (timing issues) |
| UI (Layout) | 2 | 🟢 Low | CSS regression |

### Action Plan
- **Today**: Fix API adapter (2 hours) → Unblock 25 tests
- **Tomorrow**: Fix OrderProcessor regression (4 hours)
- **Next Sprint**: Stabilize flaky E2E tests (technical debt)

**Status**: 🔴 RED (failure rate >3% threshold)  
**Blocking Release**: Yes (critical failures)  
**Owner**: Dev Team (All)  
**Next Update**: End of day Friday
```

#### Honest Status vs. Wishful Thinking

| ❌ Dishonest Reporting | ✅ Honest Reporting |
|-------------------------|----------------------|
| "90% of tests pass" | "47 tests failing, including 25 critical API tests" |
| "Almost ready to ship" | "Blocked by API integration issues; ETA 2 days" |
| "Minor issues only" | "15 unit test failures due to regression (high priority)" |
| "We're catching up" | "Test failures trending up; need corrective action" |

### Early Warning System

**Report immediately when**:
- Test failure rate >3% (threshold breach)
- Critical tests fail (P0/P1 priority)
- Trend shows increasing failures (week-over-week)
- Flaky tests impact CI reliability
- Coverage drops below 80%

**Notification Protocol**:
```markdown
## Test Status Alert 🚨

**Date**: 2025-11-28 14:30 UTC  
**To**: Team Lead, Product Owner

**Problem**: Integration test failures blocking deployment

**Impact**:
- Cannot deploy to staging
- Release delayed by estimated 2 days
- 25 API tests failing (20% of integration suite)

**Root Cause**: Third-party weather API changed response format without notice

**Options**:
1. Fix adapter immediately (2 days) → Delay release to Monday
2. Rollback to old API version (4 hours) → On-time release, but vendor forces upgrade in 2 months
3. Ship without weather feature (1 day) → Reduced scope, on-time release

**Recommendation**: Option 1 (fix adapter properly)

**Why telling you NOW**: Gives max reaction time for stakeholder communication

**Promise**: Daily updates at 5pm until resolved
```

### Five Whys for Test Failures

When tests fail, dig to root cause:

**Example**:
```markdown
## Root Cause Analysis: Authentication Tests Failing

**Symptom**: 15 authentication tests failing since Tuesday

**Five Whys**:
1. **Why** are auth tests failing?  
   → JWT token validation returns 401 Unauthorized.

2. **Why** is token validation failing?  
   → Token signature is invalid.

3. **Why** is signature invalid?  
   → Secret key changed in environment config.

4. **Why** did secret key change?  
   → DevOps rotated keys as part of security policy.

5. **Why** didn't we know about key rotation?  
   → No notification process for environment changes.

**Root Cause**: Lack of communication between DevOps and Dev teams

**Systemic Solution** (team problem, not individual blame):
- [ ] Add key rotation to change management process
- [ ] Notify dev team 24 hours before environment changes
- [ ] Add automated test for key expiration (warn 7 days before)
```

### Velocity-Based Prediction

**Use actual test completion velocity to predict release readiness**:

```markdown
## Test Execution Progress

| Week | Tests Executed | Tests Remaining | Velocity (tests/week) |
|------|----------------|-----------------|----------------------|
| 1    | 150            | 850             | 150                  |
| 2    | 200            | 650             | 175 (avg: 162)       |
| 3    | 180            | 470             | 176 (avg: 176)       |
| 4    | ?              | ?               | ?                    |

**Prediction** (based on actual velocity):
- Average velocity: 176 tests/week
- Remaining: 470 tests
- **Estimated completion**: 2.7 weeks (not 2 weeks as planned)

**Options**:
1. Continue at current pace → Release delayed by 1 week
2. Increase test automation → Accelerate velocity (risky if rushed)
3. Reduce test scope → Remove low-priority tests (review with stakeholders)

**Honest Recommendation**: Option 1 (realistic timeline based on data)
```

## 🎯 Next Phase

**Phase 08: Transition (Deployment)** (`08-transition/`)

---

**Remember**: 
- **Verification checks correctness. Validation checks fitness for purpose. Both are essential!**
- **Customer involvement in acceptance testing is mandatory (XP practice).**
- **Report test results with courage and honesty - bad news delivered early gives stakeholders maximum reaction time.**
- **See [Critical Self-Reflection and Honest Reporting Guide](../../docs/critical-self-reflection-honest-reporting.md) for detailed practices.**
