# Quick Start: GitHub Issues for Requirements Traceability

**Standards**: ISO/IEC/IEEE 29148:2018 (Requirements Engineering)  
**Last Updated**: 2025-11-12

## 🎯 Overview

This repository uses **GitHub Issues** to track all requirements, architecture decisions, and tests with full bidirectional traceability. Each issue type has a template that enforces structure and linking.

## 📝 Creating Requirements

### Step 1: Navigate to Issues

1. Go to **Issues** tab
2. Click **New Issue**
3. You'll see the template chooser with 7 options

### Step 2: Select Template

| Template | When to Use | ID Pattern |
|----------|-------------|------------|
| **Stakeholder Requirement** | Business needs and context | Auto-assigned #N |
| **Functional Requirement** | System SHALL behavior | Auto-assigned #N |
| **Non-Functional Requirement** | Quality attributes (performance, security, etc.) | Auto-assigned #N |
| **Architecture Decision** | Major design choices | Auto-assigned #N |
| **Architecture Component** | Component specifications | Auto-assigned #N |
| **Quality Scenario** | ATAM quality attribute scenarios | Auto-assigned #N |
| **Test Case** | Verification specifications | Auto-assigned #N |

### Step 3: Fill Required Fields

**Required fields have red asterisks (*)** - you must complete these:

#### Example: Functional Requirement

```markdown
Title: [REQ-F] User Login with Email/Password

Stakeholder Requirement Link: #123

Priority: High

Description:
The system SHALL allow users to log in using email and password credentials.

Rationale:
Users need a secure way to authenticate to access personalized features.

Acceptance Criteria:
Scenario: Successful login
  Given user has valid credentials
  When user submits login form
  Then user is authenticated and redirected to dashboard

Scenario: Invalid credentials
  Given user has incorrect password
  When user submits login form
  Then login fails with error message "Invalid credentials"
```

### Step 4: Link to Parent

**Every requirement (except StR) MUST link to parent:**

```markdown
## Traceability
- Traces to:  #123 (parent StR issue)
```

**Linking Rules**:
- REQ-F → StR (functional requirement traces to business need)
- REQ-NF → StR (non-functional requirement traces to business need)
- ADR → REQ (architecture decision implements requirement)
- ARC-C → ADR (component realizes architecture decision)
- TEST → REQ (test verifies requirement)

### Step 5: Submit

Click **Submit new issue** → GitHub auto-assigns unique issue number (e.g., #145)

## 🔗 Linking Issues

### In Issue Bodies

Use markdown syntax to create traceability links:

```markdown
## Traceability
- Traces to:  #123 (parent requirement)
- **Depends on**: #45, #67 (prerequisite requirements)
- **Verified by**: #89 (test issue)
- **Implemented by**: #PR-15 (pull request)
```

GitHub will:
- ✅ Auto-link to referenced issues
- ✅ Show backlinks in parent issues
- ✅ Provide autocomplete when typing `#`

### In Pull Requests

**Every PR MUST link to implementing issue:**

```markdown
## Description
Implement user login functionality with email/password authentication.

## Related Issues
Fixes #145
Implements #146, #147

## Traceability
- **Requirements**: #145 (REQ-F-AUTH-001)
- **Design**: #50 (ADR-SECU-002)
- **Tests**: #200 (TEST-AUTH-LOGIN-001)

## Changes
- Added LoginService with bcrypt password hashing
- Created /api/login endpoint
- Added JWT token generation
```

**Commit messages** should also reference issues:

```bash
git commit -m "feat: Implement user login endpoint (Fixes #145)"
```

### In Code

**Include issue references in docstrings/comments:**

```python
"""
User authentication service.

Implements: #145 (REQ-F-AUTH-001: User Login)
Architecture: #50 (ADR-SECU-002: JWT Authentication)
Verified by: #200 (TEST-AUTH-LOGIN-001)

Standards: OAuth 2.0, NIST SP 800-63B
"""
class AuthenticationService:
    def login(self, email: str, password: str) -> User:
        """
        Authenticate user with email and password.
        
        Acceptance Criteria (from #145):
          Given user has valid credentials
          When user submits login form
          Then user is authenticated and receives JWT token
        """
        pass
```

```typescript
/**
 * User login API endpoint
 * 
 * @implements #145 REQ-F-AUTH-001: User Login
 * @see https://github.com/zarfld/IntelAvbFilter/issues/145
 */
export async function POST(request: Request) {
  // Implementation
}
```

## 🧪 Creating Tests

### Link Tests to Requirements

**Every test MUST reference verified requirement:**

```python
"""
Test user login functionality.

Verifies: #145 (REQ-F-AUTH-001: User Login)
Test Type: Integration
Priority: P0 (Critical)

Acceptance Criteria (from #145):
  Given user has valid credentials
  When user submits login form
  Then user is authenticated and redirected to dashboard
"""
def test_user_login_with_valid_credentials():
    # Arrange
    user = create_test_user(email="test@example.com", password="secure123")
    
    # Act
    response = login(email="test@example.com", password="secure123")
    
    # Assert
    assert response.status_code == 200
    assert response.user.is_authenticated == True
    assert response.redirect_url == "/dashboard"
```

### Create Test Issue (Optional)

For complex test suites, create a **TEST** issue:

**Template**: `.github/ISSUE_TEMPLATE/07-test-case.yml`

**Required fields**:
- Verified Requirements: #145, #146
- Test Type: Unit / Integration / E2E
- Test Priority: P0 - P3
- Test Steps (Given/When/Then)
- Expected Results
- Automation Status

## 🔍 Finding Requirements

### Filter by Labels

Use GitHub's built-in filters:

```
label:functional-requirement
label:non-functional
label:phase-02
label:priority-critical
```

**Combined filters**:
```
is:open label:functional-requirement label:priority-high
is:closed label:test-case
```

### Filter by Phase

| Phase | Label | Description |
|-------|-------|-------------|
| Phase 01 | `phase-01` | Stakeholder Requirements |
| Phase 02 | `phase-02` | Requirements Analysis |
| Phase 03 | `phase-03` | Architecture Design |
| Phase 04 | `phase-04` | Detailed Design |
| Phase 05 | `phase-05` | Implementation |
| Phase 06 | `phase-06` | Integration |
| Phase 07 | `phase-07` | Verification & Validation |
| Phase 08 | `phase-08` | Transition |
| Phase 09 | `phase-09` | Operation & Maintenance |

### Search by Content

Use GitHub's search:

```
is:issue "user login" label:functional-requirement
is:issue "performance" label:non-functional
```

### View Traceability

**Option 1**: Use GitHub Projects (if configured)
- Go to **Projects** tab
- View **Traceability Matrix** table
- Filter by phase, type, priority

**Option 2**: Run traceability script
```bash
export GITHUB_TOKEN=ghp_xxx
python scripts/github-traceability-report.py > reports/traceability.md
```

## ✅ Validation

### Automatic Validation (GitHub Actions)

Two workflows validate traceability automatically:

#### 1. Issue Validation (`.github/workflows/issue-validation.yml`)

**Runs on**: Issue create/edit

**Checks**:
- ✅ Non-StR requirements have parent link
- ✅ Parent issue exists
- ✅ Test cases link to verified requirements

**Failure**: Bot comments with guidance

#### 2. Traceability Check (`.github/workflows/traceability-check.yml`)

**Runs on**: PR open/sync, issue changes

**Checks**:
- ✅ No orphaned requirements
- ✅ All REQ-F/REQ-NF have test coverage
- ✅ Generates traceability report

**Output**: Artifact uploaded, comment on PR

### Manual Validation

**Check for orphans**:
```bash
export GITHUB_TOKEN=ghp_xxx
python scripts/github-orphan-check.py
```

**Generate report**:
```bash
export GITHUB_TOKEN=ghp_xxx
python scripts/github-traceability-report.py
```

## 🚀 Workflow Example

### End-to-End: New Feature

#### 1. Create Stakeholder Requirement

```markdown
Title: [StR] Users need secure authentication

Description: As a product owner, I need users to authenticate securely
so that only authorized users can access their data.

Success Criteria:
- Users can log in with email/password
- Passwords are securely hashed
- Session management with secure tokens
```

Submit → Assigned **#123**

#### 2. Create Functional Requirements

```markdown
Title: [REQ-F] User Login with Email/Password

Stakeholder Requirement Link: #123

Description: System SHALL allow users to log in with email and password.

Acceptance Criteria:
  Given user has valid credentials
  When user submits login form
  Then user is authenticated with JWT token
  
## Traceability
- Traces to:  #123 (StR: Secure Authentication)
```

Submit → Assigned **#145**

#### 3. Create Architecture Decision

```markdown
Title: [ADR] Use JWT for Stateless Authentication

Status: Accepted

Context: Requirement #145 needs session management.

Decision: Use JWT tokens for stateless authentication.

Consequences:
- Positive: Scalable, no server-side session storage
- Negative: Token revocation complexity

## Traceability
- **Satisfies**: #145 (REQ-F: User Login)
```

Submit → Assigned **#150**

#### 4. Create Test Case

```markdown
Title: [TEST] User Login Integration Test

Verified Requirements: #145

Test Type: Integration
Priority: P0 (Critical)

Test Steps:
1. Create test user with known credentials
2. Call /api/login endpoint with credentials
3. Verify 200 response with JWT token
4. Verify token is valid and contains user ID

Expected Results:
- Response status: 200
- Response body contains valid JWT token
- Token payload contains user ID and expiry
```

Submit → Assigned **#200**

#### 5. Implement (Create PR)

```bash
git checkout -b feature/user-login
# Implement code with issue references in docstrings
git commit -m "feat: Implement user login endpoint (Fixes #145)"
git push origin feature/user-login
```

**PR Description**:
```markdown
## Description
Implement user login functionality with JWT authentication.

## Related Issues
Fixes #145
Implements ADR #150

## Traceability
- **Requirements**: #145 (REQ-F: User Login)
- **Architecture**: #150 (ADR: JWT Authentication)
- **Tests**: #200 (TEST: User Login Integration)

## Changes
- Added AuthenticationService
- Implemented /api/login endpoint
- Added JWT token generation
- All acceptance criteria from #145 met
```

#### 6. Verify

- GitHub Actions validate traceability
- Tests pass
- PR merged → Issue #145 auto-closed

#### 7. Update Parent

Add to StR #123 body:
```markdown
## Traceability
- **Implemented by**: #145 (REQ-F: User Login)
- **Verified by**: #200 (TEST: User Login)
```

## 📊 Reports and Metrics

### Traceability Matrix

Generate comprehensive matrix:

```bash
export GITHUB_TOKEN=ghp_xxx
python scripts/github-traceability-report.py > reports/traceability-$(date +%Y%m%d).md
```

**Output includes**:
- Summary statistics (total requirements, by type, by state)
- Full traceability matrix table
- Orphaned requirements list
- Requirements without tests
- Legend and definitions

### Coverage Analysis

**Requirements coverage**:
```bash
# Count requirements with tests
python scripts/github-traceability-report.py | grep "Requirements Without Tests"
```

**Orphan detection**:
```bash
python scripts/github-orphan-check.py
```

### CI Integration

Traceability reports are generated automatically:
- On every PR
- On issue changes
- Can be triggered manually via Actions tab

**Access reports**:
1. Go to **Actions** tab
2. Select **Traceability Validation** workflow
3. Download **traceability-report** artifact

## 🆘 Troubleshooting

### Issue Template Not Showing

**Problem**: Template chooser doesn't show templates

**Solution**:
1. Ensure templates are in `.github/ISSUE_TEMPLATE/`
2. Filenames must be `.yml` (not `.yaml`)
3. Push to GitHub (templates must be on remote)
4. Check `config.yml` doesn't disable templates

### Parent Link Not Detected

**Problem**: Validation fails but link exists

**Solution**:
1. Use exact syntax: `Traces to:  #N`
2. Ensure `#N` is actual issue number, not placeholder
3. Check parent issue exists and is not closed
4. Re-edit issue to trigger validation

### Tests Not Linking

**Problem**: Test doesn't show in requirement's "Verified by"

**Solution**:
1. Add `Verifies: #N` in test docstring
2. Or create TEST issue with "Verified Requirements: #N"
3. Update requirement body with `**Verified by**: #N`

### GitHub Actions Failing

**Problem**: Traceability check workflow fails

**Solution**:
1. Check script errors in workflow logs
2. Verify `GITHUB_TOKEN` has correct permissions
3. Ensure `requests` package is installed in workflow
4. Check for orphaned requirements with manual script

## 📚 Additional Resources

- **Migration Plan**: `docs/improvement_ideas/MIGRATION-PLAN-file-to-github-issues.md`
- **Copilot Instructions**: `.github/copilot-instructions.md` (enforces issue workflow)
- **Test Instructions**: `.github/instructions/tests.instructions.md`
- **Scripts README**: `scripts/README.md`
- **Issue Templates**: `.github/ISSUE_TEMPLATE/`
- **Lifecycle Guide**: `docs/lifecycle-guide.md`

## 🎓 Standards Reference

- **ISO/IEC/IEEE 29148:2018** - Requirements Engineering
- **ISO/IEC/IEEE 42010:2011** - Architecture Description
- **IEEE 1012-2016** - Verification & Validation
- **IEEE 1633** - Software Reliability (Integrity Levels)

---

**Questions?** Open a discussion or contact the maintainer.

**Last Updated**: 2025-11-12
