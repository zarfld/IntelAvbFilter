# Software Development Lifecycle Guide

This guide walks you through the complete software development lifecycle using this template repository, integrating **IEEE/ISO/IEC standards** with **Extreme Programming (XP) practices**.

---

## Overview

The lifecycle consists of 9 phases, each with specific objectives, deliverables, and copilot instructions:

```
Phase 01: Stakeholder Requirements → Phase 02: Requirements Analysis
    ↓                                           ↓
Phase 03: Architecture Design → Phase 04: Detailed Design
    ↓                                           ↓
Phase 05: Implementation → Phase 06: Integration
    ↓                                           ↓
Phase 07: Verification & Validation → Phase 08: Transition
    ↓
Phase 09: Operation & Maintenance (Continuous)
```

---

## GitHub Issues as Traceability Infrastructure

**All requirements, architecture decisions, and tests are tracked as GitHub Issues** with bidirectional links. This provides:

- **Single Source of Truth**: Issues are the authoritative record
- **Automated Traceability**: Scripts validate issue links
- **CI/CD Integration**: Workflows block merges without proper links
- **Team Visibility**: Everyone sees status, progress, relationships

### Issue Types by Phase

| Phase | Issue Types Created | Labels | Example |
|-------|---------------------|--------|---------|
| **Phase 01** | Stakeholder Requirements | `type:stakeholder-requirement`, `phase:01-stakeholder-requirements` | `StR-001: User Authentication` |
| **Phase 02** | Functional & Non-Functional Requirements | `type:requirement:functional`, `type:requirement:non-functional`, `phase:02-requirements` | `REQ-F-AUTH-001: Login`, `REQ-NF-PERF-001: Response time` |
| **Phase 03** | Architecture Decisions, Components, Quality Scenarios | `type:architecture:decision`, `type:architecture:component`, `type:architecture:quality-scenario`, `phase:03-architecture` | `ADR-SECU-001: JWT`, `ARC-C-AUTH-001: Auth Service`, `QA-SC-PERF-001: Load test` |
| **Phase 04** | Component Designs (as issues or linked ADRs) | `type:design`, `phase:04-design` | `DES-AUTH-API-001: Login endpoint design` |
| **Phase 05** | Pull Requests linking to requirements/designs | `phase:05-implementation` | `Implements: #2, #5` |
| **Phase 06** | Integration issues, deployment tracking | `type:integration`, `phase:06-integration` | `INT-001: Deploy auth service to staging` |
| **Phase 07** | Test Cases | `type:test`, `test-type:unit/integration/e2e/acceptance`, `phase:07-verification-validation` | `TEST-AUTH-LOGIN-001: Valid login test` |
| **Phase 08** | Deployment issues, user documentation | `type:deployment`, `phase:08-transition` | `DEP-001: Production deployment` |
| **Phase 09** | Incidents, maintenance tasks | `type:incident`, `type:maintenance`, `phase:09-operation-maintenance` | `INC-001: Login timeout`, `MAINT-001: Update dependencies` |

### Traceability Flow

```
StR Issue (#1) 
  ↓ "Traces to"
REQ-F Issue (#2) 
  ↓ "Implemented by"
Code/PR (#PR-10) 
  ↓ "Verified by"
TEST Issue (#7)
  ↓ "Validates"
REQ-F Issue (#2)
```

Every artifact includes traceability:

```markdown
## Traceability
- Traces to:  #1 (parent StR issue)
- **Depends on**: #3, #4 (prerequisite issues)
- **Verified by**: #7, #8 (test issues)
- **Implemented by**: #PR-10 (pull request)
```

### Workflow Pattern

1. **Create Issue First** (before any work)
2. **Link in Code/Tests** (`Implements: #N`, `Verifies: #N`)
3. **Reference in PR** (`Fixes #N`, `Part of #N`)
4. **Validate in CI** (automated traceability check)
5. **Close on Merge** (automated via `Fixes #N`)

---

## Phase-by-Phase Workflow

### Phase 01: Stakeholder Requirements Definition

**When to use**: At project inception

**Key Activities**:

1. Identify all stakeholders
2. Conduct stakeholder interviews
3. Document business context
4. **Create Stakeholder Requirement Issues (StR-XXX)**

**Deliverables**:

- **GitHub Issues** with label `type:stakeholder-requirement`, `phase:01-stakeholder-requirements`
- `01-stakeholder-requirements/stakeholder-requirements-specification.md` (optional summary document)
- `01-stakeholder-requirements/stakeholders/stakeholder-register.md`
- `01-stakeholder-requirements/business-context/business-context.md`

**GitHub Issues Workflow**:

```bash
# Step 1: Create StR issue using GitHub CLI
gh issue create \
  --title "StR-001: User Authentication" \
  --label "type:stakeholder-requirement,phase:01-stakeholder-requirements,priority:critical" \
  --body "## Business Need
Users need secure authentication to access the system.

## Stakeholders
- Product Owner: Jane Doe
- End Users: Customers

## Business Context
[Context here]

## Acceptance Criteria
- [ ] Users can register with email/password
- [ ] Users can log in securely
- [ ] Password reset functionality

## Traceability
- **Verified by**: (to be linked in Phase 07)"

# Step 2: Issue #1 is created, use this number in Phase 02
```

**Copilot Support**:
Navigate to `01-stakeholder-requirements/` and use prompts:

```
"Generate a GitHub Issue body for stakeholder requirement: User Authentication"
"Help me structure stakeholder interviews for an e-commerce system"
"Create acceptance criteria for mobile app performance requirement"
```

Copilot will:

- Generate complete issue bodies with all required sections
- Suggest stakeholder categories to consider
- Help structure requirements per IEEE 29148
- Create traceability placeholders
- Remind you about priority labels

---

### Phase 02: Requirements Analysis & Specification

**When to use**: After stakeholder requirements are approved

**Key Activities**:

1. Transform stakeholder requirements into system requirements
2. **Create REQ-F and REQ-NF issues** linked to StR issues
3. Write use cases (as issues or documents)
4. Create user stories (XP practice)
5. Establish requirements traceability via issue links

**Deliverables**:

- **GitHub Issues** with labels `type:requirement:functional`, `type:requirement:non-functional`, `phase:02-requirements`
- `02-requirements/system-requirements-specification.md` (optional summary)
- `02-requirements/functional/` - Functional requirements (markdown specs)
- `02-requirements/non-functional/nfr-specification.md`
- `02-requirements/use-cases/*.md`
- `02-requirements/user-stories/*.md`

**GitHub Issues Workflow**:

```bash
# Step 1: Create REQ-F issue from StR issue #1
gh issue create \
  --title "REQ-F-AUTH-001: User Login with Credentials" \
  --label "type:requirement:functional,phase:02-requirements,priority:critical" \
  --body "## Description
System shall allow users to log in with username and password.

## Acceptance Criteria
- [ ] User can enter username and password
- [ ] System validates credentials against database
- [ ] User redirected to dashboard on success
- [ ] Error message displayed on failure
- [ ] Session token generated (JWT)

## Traceability
- Traces to:  #1 (StR-001: User Authentication)
- **Depends on**: (none)
- **Verified by**: (to be linked in Phase 07)
- **Implemented by**: (to be linked in Phase 05)

## Non-Functional Impact
- Performance: Login must complete in <200ms (see #3)
- Security: Passwords hashed with bcrypt (see #4)"

# Issue #2 is created

# Step 2: Create REQ-NF issues for quality attributes
gh issue create \
  --title "REQ-NF-PERF-001: Login Response Time" \
  --label "type:requirement:non-functional,phase:02-requirements,priority:high" \
  --body "## Description
Login operations shall complete within 200ms at 95th percentile.

## Measurable Criteria
- [ ] 95% of logins complete in <200ms
- [ ] 99% of logins complete in <500ms
- [ ] Under load of 1000 concurrent users

## Test Approach
Load testing with JMeter/k6

## Traceability
- Traces to:  #1 (StR-001: User Authentication)
- **Related**: #2 (REQ-F-AUTH-001: User Login)
- **Verified by**: (performance test issue to be created)"

# Issue #3 is created
```

**Using Copilot to Generate Issues**:

```
"Generate a REQ-F issue body for user login functionality, traces to #1"
"Create a REQ-NF issue for password security requirements"
"Help me decompose StR-001 into functional requirements"
"Suggest non-functional requirements I might have missed for authentication"
```

**XP Practice: User Stories** (as issues):

```bash
# Create user story as GitHub Issue
gh issue create \
  --title "STORY-001: User can log in to access dashboard" \
  --label "type:user-story,phase:02-requirements,priority:critical" \
  --body "## User Story
As a registered user
I want to log in with my credentials
So that I can access my dashboard

## Acceptance Criteria
- [ ] Login form accepts username/password
- [ ] Successful login redirects to dashboard
- [ ] Failed login shows error message

## Story Points: 5

## Tasks
- [ ] Design login UI
- [ ] Implement authentication logic
- [ ] Write tests

## Traceability
- **Implements**: #1 (StR-001), #2 (REQ-F-AUTH-001)"
```

**Exit Criteria Checklist**:

```markdown
- [ ] All functional requirements documented as issues (REQ-F-XXX)
- [ ] All non-functional requirements with measurable metrics (REQ-NF-XXX)
- [ ] Every requirement traced to StR issue (#N)
- [ ] Every requirement has acceptance criteria
- [ ] Use cases written for complex interactions
- [ ] User stories created for all features (as issues or docs)
- [ ] Requirements reviewed by stakeholders
- [ ] Requirements approved and labeled `status:approved`
```

---

### Phase 03: Architecture Design

**When to use**: After requirements are approved

**Key Activities**:

1. Define system architecture
2. **Create Architecture Decision Record (ADR) issues**
3. **Create Architecture Component (ARC-C) issues**
4. **Create Quality Scenario (QA-SC) issues** for ATAM evaluation
5. Design architecture views (C4 model)
6. Define component boundaries

**Deliverables**:

- **GitHub Issues** with labels `type:architecture:decision`, `type:architecture:component`, `type:architecture:quality-scenario`, `phase:03-architecture`
- `03-architecture/architecture-description.md` (summary document)
- `03-architecture/decisions/*.md` - ADR documents (linked to issues)
- `03-architecture/diagrams/` - C4 diagrams
- `03-architecture/views/` - Architecture views

**GitHub Issues Workflow**:

```bash
# Step 1: Create ADR issue for key architectural decision
gh issue create \
  --title "ADR-SECU-001: Use JWT for Authentication" \
  --label "type:architecture:decision,phase:03-architecture,priority:critical" \
  --body "## Status
Proposed

## Context
Requirement #2 (REQ-F-AUTH-001) requires secure user authentication.
We need stateless authentication to support horizontal scaling.

## Decision
Use JWT (JSON Web Tokens) for stateless authentication.

## Alternatives Considered
1. Session-based auth (rejected: requires sticky sessions)
2. OAuth 2.0 (overkill for internal auth)
3. API keys (rejected: no user context)

## Consequences
### Positive
- Stateless: enables horizontal scaling
- Standard: widely supported libraries
- Secure: signed tokens prevent tampering

### Negative
- Token revocation complex (need blacklist)
- Token size larger than session IDs

## Requirements Satisfied
- #2 (REQ-F-AUTH-001: User Login)
- #4 (REQ-NF-SECU-001: Secure authentication)

## Traceability
- **Addresses**: #2, #4
- **Implemented by**: (to be linked in Phase 05)"

# Issue #5 is created

# Step 2: Create component specification issue
gh issue create \
  --title "ARC-C-AUTH-001: Authentication Service Component" \
  --label "type:architecture:component,phase:03-architecture,priority:critical" \
  --body "## Component Overview
Microservice responsible for user authentication and JWT token management.

## Responsibilities
- Validate user credentials
- Generate JWT tokens
- Refresh tokens
- Validate incoming tokens (middleware)

## Interfaces
### REST API
- POST /auth/login
- POST /auth/refresh
- POST /auth/logout

### Dependencies
- User Database (PostgreSQL)
- Redis (token blacklist)

## Quality Attributes
- Availability: 99.9%
- Performance: <200ms response time
- Security: JWT signing, password hashing (bcrypt)

## Traceability
- **Implements**: #5 (ADR-SECU-001: JWT)
- **Satisfies**: #2 (REQ-F-AUTH-001), #4 (REQ-NF-SECU-001)
- **Verified by**: #10 (QA-SC-PERF-001: Load test)"

# Issue #6 is created

# Step 3: Create quality scenario for ATAM evaluation
gh issue create \
  --title "QA-SC-PERF-001: Peak Load Authentication" \
  --label "type:architecture:quality-scenario,phase:03-architecture,priority:high" \
  --body "## Stimulus
1000 concurrent users attempt to log in simultaneously (peak load).

## Source
End users via web and mobile clients.

## Artifact
Authentication Service (#6)

## Environment
Production environment, normal operations.

## Response
System authenticates all users successfully.

## Measure
- 95% of requests complete in <200ms
- 99% of requests complete in <500ms
- 0% error rate

## Architecture Tactics
- Stateless JWT (horizontal scaling)
- Redis caching for user lookup
- Database connection pooling

## Traceability
- **Validates**: #6 (ARC-C-AUTH-001)
- **Tests**: #3 (REQ-NF-PERF-001: Login Response Time)"
```

**Using Copilot to Generate Issues**:

```
"Generate an ADR issue for database selection, considering PostgreSQL vs MongoDB"
"Create an ARC-C issue for the authentication service component"
"Generate a quality scenario for availability testing of the payment service"
"Help me evaluate architecture tradeoffs for microservices vs monolith"
```

**C4 Diagrams** (generated by Copilot, linked in issues):

```
"Generate a C4 context diagram showing authentication service and external dependencies"
"Create a component diagram for the authentication microservice architecture"
```

---

### Phase 04: Detailed Design

**When to use**: After architecture is approved

**Key Activities**:

1. Design individual components
2. Define class structures and interfaces
3. Specify data models
4. Document algorithms
5. Apply design patterns

**Deliverables**:

- `04-design/software-design-description.md`
- `04-design/components/*.md` - Component designs
- `04-design/data-models/data-model-specification.md`
- `04-design/interfaces/api-design.md`

**Copilot Support**:

```bash
cd 04-design

# Copilot helps you:
# - Design classes following SOLID principles
# - Apply appropriate design patterns
# - Create complete interface specifications
# - Design for testability (XP: TDD-ready)
```

---

### Phase 05: Implementation (The XP Core!)

**When to use**: After design is complete

**Key Activities** (XP Focus):

1. **Test-Driven Development (TDD)**: Write tests FIRST (link to TEST issues)
2. **Link code to issues**: All code references implementing requirement issues
3. **Pair Programming**: Complex code done in pairs
4. **Continuous Integration**: Integrate multiple times daily
5. **Refactoring**: Keep code clean continuously
6. **Simple Design**: YAGNI (You Aren't Gonna Need It)

**Deliverables**:

- `05-implementation/src/` - Source code (with issue references in docstrings)
- `05-implementation/tests/` - Test suites (with `Verifies: #N` comments)
- **Pull Requests** with `Implements: #N`, `Fixes #N` links
- `05-implementation/docs/` - Code documentation

**GitHub Issues Workflow (TDD with Traceability)**:

```bash
cd 05-implementation

# Step 0: Ensure TEST issue exists (from Phase 07, or create now)
# TEST-AUTH-LOGIN-001 (#10) already exists

# Step 1: Write test FIRST referencing TEST issue (Red)
cat > tests/auth-service.test.ts << 'EOF'
/**
 * Authentication service tests
 * 
 * Verifies: #10 (TEST-AUTH-LOGIN-001: Valid login test)
 * Tests: #2 (REQ-F-AUTH-001: User Login)
 * 
 * @see https://github.com/org/repo/issues/10
 */
describe('AuthService - Verifies #10', () => {
  it('should authenticate user with valid credentials', async () => {
    // Acceptance Criteria from #2:
    // [x] User can enter username and password
    // [x] System validates credentials
    // [x] User redirected on success
    
    const result = await authService.login('user@example.com', 'password123');
    expect(result.success).toBe(true);
    expect(result.token).toBeDefined();
  });
});
EOF

# Step 2: Run test - it FAILS (Red)
npm test
# ❌ FAIL: AuthService is not defined

# Step 3: Write minimal implementation referencing requirements (Green)
cat > src/auth-service.ts << 'EOF'
/**
 * Authentication Service
 * 
 * Implements: #2 (REQ-F-AUTH-001: User Login)
 * Architecture: #5 (ADR-SECU-001: JWT Authentication)
 * Component: #6 (ARC-C-AUTH-001: Authentication Service)
 * Verified by: #10 (TEST-AUTH-LOGIN-001)
 * 
 * @see https://github.com/org/repo/issues/2
 */
export class AuthService {
  /**
   * Authenticate user with credentials
   * Implements: #2 (REQ-F-AUTH-001)
   */
  async login(email: string, password: string): Promise<AuthResult> {
    // Minimal implementation to make test pass
    return { success: true, token: 'jwt-token' };
  }
}
EOF

# Step 4: Run test - it PASSES (Green)
npm test
# ✅ PASS: Authentication test passed

# Step 5: Refactor while keeping tests green
# Copilot suggests: "Extract JWT generation to separate function"
code src/auth-service.ts

# Step 6: Commit with issue references
git add .
git commit -m "feat: implement user login authentication

Implements: #2 (REQ-F-AUTH-001: User Login)
Architecture: #5 (ADR-SECU-001: JWT)
Verified by: #10 (TEST-AUTH-LOGIN-001)

- TDD RED-GREEN-REFACTOR cycle
- JWT token generation
- Password validation
- Tests passing (100% coverage)"

# Step 7: Create Pull Request with issue links
git push origin feature/user-login

gh pr create \
  --title "feat: User Login Authentication" \
  --body "## Description
Implements user login with JWT authentication.

## Related Issues
Implements #2 (REQ-F-AUTH-001: User Login)
Fixes #5 (ADR-SECU-001: JWT)
Part of #1 (StR-001: User Authentication)

## Traceability
- **Requirements**: #2 (REQ-F-AUTH-001)
- **Design**: #5 (ADR-SECU-001), #6 (ARC-C-AUTH-001)
- **Tests**: #10 (TEST-AUTH-LOGIN-001)

## Checklist
- [x] Tests written first (TDD)
- [x] All tests passing
- [x] Coverage >80%
- [x] Issue references in code
- [x] Traceability complete"
```

**Copilot Support for XP**:

- **TDD**: Copilot generates tests from TEST issues, suggests test cases
- **Pair Programming**: One person types, Copilot acts as second pair member
- **Refactoring**: Copilot suggests refactorings while keeping tests green
- **Simple Design**: Copilot warns about over-engineering
- **Traceability**: Copilot reminds to add issue references in docstrings

**Quality Gates**:

```bash
# Before every commit:
npm run lint                     # Code style
npm test                         # All tests
npm run coverage                 # Check >80% coverage
python scripts/validate-traceability.py  # Issue links present

# If all pass:
git add .
git commit -m "feat: implement feature (Implements: #N)"
git push

# CI runs automatically:
# - Linting
# - Tests
# - Coverage check (block if <80%)
# - Security scan
# - Traceability validation (block if missing issue links)
```

---

### Phase 06: Integration

**When to use**: Continuously during implementation

**Key Activities**:

1. Integrate code multiple times per day (XP)
2. Run automated integration tests
3. Deploy to staging environment
4. Monitor integration health

**Deliverables**:

- `06-integration/integration-tests/` - Integration test suites
- `06-integration/ci-config/` - CI/CD configurations
- `06-integration/deployment/` - Deployment scripts

**Continuous Integration Workflow**:

```bash
# Workflow (happens automatically on push):
1. Developer commits code
2. GitHub Actions triggers
3. Build runs
4. All tests run (unit + integration)
5. If tests pass → Deploy to staging
6. If tests fail → Alert team, fix immediately (<10 min)

# Manual integration test run:
cd 06-integration
npm run test:integration

# Copilot helps with:
# - Writing integration tests
# - Debugging integration failures
# - Configuring CI/CD pipelines
```

---

### Phase 07: Verification & Validation

**When to use**: Throughout development, final validation before release

**Key Activities**:

1. **Create TEST issues** for each requirement
2. Verify code against design (verification)
3. Validate system meets user needs (validation)
4. Execute test plans
5. Run acceptance tests with customer (XP)
6. **Validate traceability**: REQ → TEST → Code chain complete

**Deliverables**:

- **GitHub Issues** with label `type:test`, `test-type:unit/integration/e2e/acceptance`, `phase:07-verification-validation`
- `07-verification-validation/vv-plan.md`
- `07-verification-validation/test-cases/` - Test case implementations
- `07-verification-validation/test-results/` - Results
- **Automated traceability matrix** generated from GitHub Issues

**GitHub Issues Workflow**:

```bash
# Step 1: Create TEST issue for requirement #2
gh issue create \
  --title "TEST-AUTH-LOGIN-001: Valid User Login Test" \
  --label "type:test,test-type:integration,phase:07-verification-validation,priority:critical" \
  --body "## Test Objective
Verify that users can successfully log in with valid credentials.

## Verifies
- **Requirement**: #2 (REQ-F-AUTH-001: User Login)
- **Component**: #6 (ARC-C-AUTH-001: Authentication Service)

## Preconditions
- User account exists in database
- Authentication service is running

## Test Steps
1. Send POST /auth/login with valid credentials
2. Verify response status 200
3. Verify JWT token in response
4. Verify token is valid and contains user claims

## Expected Result
- Status: 200 OK
- Response contains: { success: true, token: '<jwt>' }
- Token is valid for 1 hour
- Token contains user ID and roles

## Test Data
- Username: test@example.com
- Password: Test123!

## Acceptance Criteria (from #2)
- [x] User can enter username and password
- [x] System validates credentials
- [x] User redirected on success
- [x] Error message on failure

## Test Implementation
File: `tests/integration/auth-login.test.ts`

## Traceability
- **Verifies**: #2 (REQ-F-AUTH-001)
- **Validated by**: #PR-10 (implementation PR)
- **Part of**: #1 (StR-001: User Authentication)"

# Issue #10 is created

# Step 2: Create acceptance TEST issue from StR
gh issue create \
  --title "TEST-AUTH-E2E-001: End-to-End User Login Flow" \
  --label "type:test,test-type:acceptance,test-type:e2e,phase:07-verification-validation,priority:critical" \
  --body "## Test Objective
Validate complete user login flow from customer perspective.

## Verifies
- **Stakeholder Requirement**: #1 (StR-001: User Authentication)
- **Functional Requirement**: #2 (REQ-F-AUTH-001)

## User Story
As a registered user
I want to log in to the system
So that I can access my dashboard

## Test Scenario (BDD)
\`\`\`gherkin
Feature: User Login
  Scenario: Successful login
    Given I am on the login page
    When I enter valid credentials
      | email           | password  |
      | test@example.com | Test123!  |
    And I click the 'Login' button
    Then I should see the dashboard
    And I should see my username in the header
\`\`\`

## Acceptance Criteria (from #1)
- [x] Users can register with email/password
- [x] Users can log in securely
- [ ] Password reset functionality (separate test)

## Test Tool
Playwright (E2E)

## Customer Approval Required
Yes - Product Owner must witness test execution

## Traceability
- **Verifies**: #1 (StR-001), #2 (REQ-F-AUTH-001)
- **Implemented by**: tests/e2e/login.spec.ts"
```

**Acceptance Testing (XP) with Issues**:

```typescript
/**
 * E2E Acceptance Test for User Login
 * 
 * Verifies: #11 (TEST-AUTH-E2E-001: End-to-End User Login Flow)
 * Validates: #1 (StR-001: User Authentication)
 * 
 * Customer-owned acceptance criteria from #1
 */
test('User Login E2E (Verifies #11)', async ({ page }) => {
  // Given: User on login page
  await page.goto('/login');
  
  // When: User enters credentials
  await page.fill('[name="email"]', 'test@example.com');
  await page.fill('[name="password"]', 'Test123!');
  await page.click('button[type="submit"]');
  
  // Then: User sees dashboard
  await expect(page).toHaveURL('/dashboard');
  await expect(page.locator('.username')).toContainText('test@example.com');
});
```

**Traceability Verification**:

```bash
# Generate traceability matrix from GitHub Issues
python scripts/generate-traceability-matrix.py

# Output: HTML report showing:
# StR-001 → REQ-F-AUTH-001 → TEST-AUTH-LOGIN-001 → PR-10 ✅
# StR-001 → REQ-F-AUTH-001 → TEST-AUTH-E2E-001 → tests/e2e/login.spec.ts ✅

# Validate all requirements have tests
python scripts/trace_unlinked_requirements.py

# Output:
# ✅ All requirements have linked TEST issues
# ✅ All TEST issues have implementations
# ❌ REQ-NF-PERF-001 (#3) missing performance test issue

# Create missing test issue
gh issue create --title "TEST-PERF-LOGIN-001: Login Performance Test" ...
```

**CI/CD Validation**:

```yaml
# .github/workflows/validate-traceability.yml
- name: Validate Traceability
  run: |
    python scripts/validate-issue-traceability.py
    # Blocks merge if:
    # - Requirements without TEST issues
    # - TEST issues without implementations
    # - PRs without "Implements: #N" links
```

---

### Phase 08: Transition (Deployment)

**When to use**: When V&V is complete

**Key Activities** (XP: Small Releases):

1. Deploy to production
2. Train users
3. Provide documentation
4. Monitor closely post-deployment

**Deliverables**:

- `08-transition/deployment-plans/production-deployment-plan.md`
- `08-transition/user-documentation/user-guide.md`
- `08-transition/user-documentation/operations-manual.md`

**Deployment Workflow**:

```bash
cd 08-transition

# Review deployment plan
code deployment-plans/production-deployment-plan.md

# XP Practice: Small, Frequent Releases
# Deploy weekly or bi-weekly, not big-bang

# Deployment process (automated):
1. All tests pass in staging
2. Security scan clean
3. Product Owner approval
4. Deploy to production (blue-green)
5. Smoke tests
6. Monitor for 1 hour
7. If issues → Rollback (<5 min)
8. If success → Celebrate! 🎉

# Copilot helps with:
# - Deployment scripts
# - User documentation
# - Operations procedures
```

---

### Phase 09: Operation & Maintenance

**When to use**: After deployment, continuously

**Key Activities**:

1. Monitor system 24/7
2. Respond to incidents
3. Perform maintenance (corrective, adaptive, perfective)
4. Continuously improve (XP: Retrospectives)

**Deliverables**:

- `09-operation-maintenance/monitoring/operational-procedures.md`
- `09-operation-maintenance/incident-response/incident-response-playbook.md`
- `09-operation-maintenance/maintenance-logs/` - Change logs

**Daily Operations**:

```bash
cd 09-operation-maintenance

# Morning checklist
- Review overnight alerts
- Check system health dashboard
- Review error logs
- Verify backups

# Incident response
- Detect → Assess → Respond → Communicate → Resolve → Document

# Continuous improvement (XP)
- Bi-weekly retrospectives
- Act on lessons learned
- Update processes
```

**XP Practice: Sustainable Pace**:

- No heroics, no death marches
- 40-hour weeks
- Prevent burnout
- Collective ownership of production

---

## Using GitHub Copilot Throughout the Lifecycle

### Phase-Specific Instructions

Copilot automatically loads phase-specific instructions based on your location:

```bash
# Working in requirements?
cd 02-requirements
# Copilot knows to:
# - Help write requirements per IEEE 29148
# - Create Given-When-Then scenarios
# - Maintain traceability

# Working in implementation?
cd 05-implementation
# Copilot knows to:
# - Suggest writing tests first (TDD)
# - Help with refactoring
# - Enforce coding standards
```

### Asking Copilot for Help

**Example Prompts**:

```
Phase 01:
"Help me identify stakeholder classes for an e-commerce system"
"Generate interview questions for product managers"

Phase 02:
"Convert this stakeholder requirement into system requirements"
"Write acceptance criteria for user login feature"
"Generate use case for checkout process"

Phase 03:
"Help me choose between microservices and monolith architecture"
"Create an ADR for database selection"
"Generate C4 context diagram for this system"

Phase 04:
"Design a class structure for order processing"
"Suggest appropriate design pattern for this problem"

Phase 05:
"Write unit tests for this function (TDD)"
"Refactor this code to improve maintainability"
"Suggest performance optimizations"

Phase 07:
"Generate test cases for this requirement"
"Create BDD scenarios for user registration"
```

---

## XP Practices Summary

Throughout all phases, follow XP practices:

| XP Practice | How to Apply | When |
|-------------|-------------|------|
| **TDD** | Write tests before code | Phase 05 (Implementation) |
| **Pair Programming** | Two developers, one computer | Complex/critical code |
| **Continuous Integration** | Integrate multiple times daily | Phase 06 (Integration) |
| **Simple Design** | YAGNI, keep it minimal | All design/implementation |
| **Refactoring** | Improve continuously | While keeping tests green |
| **Collective Ownership** | Anyone can change any code | All phases |
| **Coding Standards** | Enforce with linters | Phase 05 |
| **Sustainable Pace** | 40-hour weeks, no burnout | All phases |
| **Customer Involvement** | Customer on team | All phases |
| **Small Releases** | Deploy frequently | Phase 08 |
| **Acceptance Testing** | Customer-defined tests | Phase 07 |

---

## Quick Reference

### Standards Checklist

- [ ] **IEEE 29148**: Requirements specification format
- [ ] **IEEE 42010**: Architecture description
- [ ] **IEEE 1016**: Software design descriptions
- [ ] **IEEE 1012**: Verification & validation
- [ ] **IEEE 12207**: Overall lifecycle process

### Quality Metrics Targets

- **Test Coverage**: >80%
- **Code Complexity**: <10 (cyclomatic)
- **Documentation**: 100% of public APIs
- **Defect Density**: <1 per 1000 LOC
- **Requirements Traceability**: 100%
- **Availability**: 99.9%

---

## Getting Help

**Documentation**:

- This guide
- Phase-specific `copilot-instructions.md` in each folder
- Spec-Kit templates in `spec-kit-templates/`
- Standards references in `docs/standards-reference.md`

**Copilot**:

- Ask questions in natural language
- Copilot provides context-aware suggestions
- Copilot enforces standards automatically

**Team**:

- Pair programming
- Code reviews
- Retrospectives

---

**Remember**: Standards provide the structure, XP provides the agility. Together they enable high-quality, maintainable software delivered iteratively with customer involvement. 🚀
