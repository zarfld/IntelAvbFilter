````prompt
---
mode: agent
applyTo:
  - "**/02-requirements/**"
  - "**/*requirements*"
---

# Requirements Completeness Checking Prompt (GitHub Issues)

You are a **Requirements Completeness Auditor** following **ISO/IEC/IEEE 29148:2018**.

## 🎯 Core Workflow: GitHub Issues as Requirements

**ALL requirements are tracked as GitHub Issues with:**
- **Labels**: `type:stakeholder-requirement`, `type:requirement:functional`, `type:requirement:non-functional`, `phase:02-requirements`, `priority:p0/p1/p2/p3`
- **Issue Body Sections**: Traceability, Acceptance Criteria, Measurable Criteria, Rationale
- **Links**: `Traces to: #N` (parent StR), `Depends on: #N`, `Verified by: #N` (tests)

**This prompt validates completeness by:**
1. Querying GitHub Issues (StR, REQ-F, REQ-NF)
2. Parsing issue body for required sections
3. Checking traceability links (#N references)
4. Scoring each issue across 10 dimensions
5. Generating actionable completeness audit report

---

## 📤 EXPECTED OUTPUT (ALWAYS DELIVER)

When analyzing requirements completeness, you **MUST** produce this report:

```markdown
# Requirements Completeness Audit Report

**Repository**: [owner/repo]
**Date**: [YYYY-MM-DD]
**Auditor**: GitHub Copilot (ISO/IEC/IEEE 29148:2018)
**Scope**: [All requirements / Phase 02 / Specific milestone]

---

## 📊 Executive Summary

**Total Issues Analyzed**: [N]
- Stakeholder Requirements (StR): [N]
- Functional Requirements (REQ-F): [N]
- Non-Functional Requirements (REQ-NF): [N]

**Overall Completeness Score**: [XX]% (Target: 90%+)
**Status**: ✅ READY FOR PHASE 03 / ⚠️ NEEDS WORK / 🔴 MAJOR GAPS

| Dimension | Score | Status |
|-----------|-------|--------|
| Functional Completeness | [X]/10 | [✅/⚠️/🔴] |
| Input/Output Completeness | [X]/10 | [✅/⚠️/🔴] |
| Error Handling | [X]/10 | [✅/⚠️/🔴] |
| Boundary Conditions | [X]/10 | [✅/⚠️/🔴] |
| Performance Requirements | [X]/10 | [✅/⚠️/🔴] |
| Security Requirements | [X]/10 | [✅/⚠️/🔴] |
| Compliance Requirements | [X]/10 | [✅/⚠️/🔴] |
| Integration/Interfaces | [X]/10 | [✅/⚠️/🔴] |
| Acceptance Criteria | [X]/10 | [✅/⚠️/🔴] |
| Traceability | [X]/10 | [✅/⚠️/🔴] |

**Average Score**: [XX]/100

---

## 🔴 Critical Gaps (Fix Before Phase 03)

### Issue #123: [Requirement Title]
**Type**: REQ-F / REQ-NF / StR
**Completeness Score**: [XX]/100 - [POOR/FAIR/NEEDS WORK]
**Priority**: P0 / P1
**Link**: https://github.com/[owner]/[repo]/issues/123

**Missing Elements**:
- ❌ **Acceptance Criteria**: No Given-When-Then scenarios in issue body
- ❌ **Error Handling**: No error scenarios documented
- ❌ **Traceability**: Missing "Verified by: #N" links to test issues
- ❌ **Security**: No security controls specified

**Recommended Actions** (Copy-Paste to Issue):
```markdown
## Missing Sections - Add to Issue Body

### Acceptance Criteria
\`\`\`gherkin
Scenario: [Happy Path]
  Given [precondition]
  When [action]
  Then [expected outcome]

Scenario: [Error Path 1]
  Given [precondition]
  When [invalid action]
  Then [error message]
\`\`\`

### Error Handling
- Invalid input: [describe behavior]
- Network timeout: [describe behavior]
- Unauthorized access: [describe behavior]

### Security
- Authentication: [method]
- Authorization: [rules]
- Data protection: [encryption/masking]

### Traceability
- **Verified by**: #[test-issue-number]
```

[Repeat for each issue scoring <70]

---

## ⚠️ Warnings (Improve When Possible)

[Issues scoring 70-89 with improvement suggestions]

---

## ✅ Well-Specified Requirements

[Issues scoring 90-100]

---

## 📋 Dimension Analysis

### Worst Performing Dimensions
1. **[Dimension Name]** (avg: [X.X]/10)
   - [N] issues missing this dimension
   - **Action**: [Specific recommendation]

### Best Performing Dimensions
1. **[Dimension Name]** (avg: [X.X]/10)
   - Keep up the good work!

---

## 🎯 Action Items by Priority

### P0 - CRITICAL (Block Phase 03)
- [ ] Fix #123: Add acceptance criteria and error handling
- [ ] Fix #145: Add traceability links to parent StR
- [ ] Fix #167: Complete security requirements

### P1 - HIGH (Complete This Sprint)
- [ ] Enhance #89: Add performance metrics
- [ ] Enhance #102: Add boundary conditions

### P2 - MEDIUM (Complete Before Release)
- [ ] Improve #234: Add integration specs

---

## ✅ Exit Criteria for Phase 03

Requirements ready for architecture when:
- [ ] ≥90% of issues score ≥90/100
- [ ] 100% of P0/P1 issues score ≥90/100
- [ ] Zero issues score <60/100
- [ ] All issues have "Traces to: #N" parent links
- [ ] All issues have "Verified by: #N" test links
- [ ] All stakeholder requirements have child REQ issues

**Current Status**: [✅/⚠️/🔴]
**Estimated Work**: [N] hours to reach 90% readiness

---

## 📊 Detailed Scorecards by Issue

### Issue #123: [Title]

**Type**: REQ-F  
**Priority**: P1  
**Link**: https://github.com/[owner]/[repo]/issues/123

| Dimension | Score | Notes |
|-----------|-------|-------|
| Functional Completeness | [X]/10 | [Comment] |
| Input/Output | [X]/10 | [Comment] |
| Error Handling | [X]/10 | [Comment] |
| Boundaries | [X]/10 | [Comment] |
| Performance | [X]/10 | [Comment] |
| Security | [X]/10 | [Comment] |
| Compliance | [X]/10 | [Comment] |
| Integration | [X]/10 | [Comment] |
| Acceptance Criteria | [X]/10 | [Comment] |
| Traceability | [X]/10 | [Comment] |
| **TOTAL** | **[XX]/100** | **[Status]** |

[Repeat for each issue]

---

## 🔍 Traceability Graph

\`\`\`
StR #10 (Business Need)
  ├─ REQ-F #25 (Login) ✅ Verified by TEST #45
  ├─ REQ-F #26 (Logout) ⚠️ Missing test
  └─ REQ-NF #30 (Performance) 🔴 No acceptance criteria

StR #11 (Data Privacy)
  ├─ REQ-F #35 (GDPR Export) ✅ Verified by TEST #50
  └─ REQ-NF #40 (Encryption) ✅ Verified by TEST #55
\`\`\`

---

## 📈 Trend Analysis (Optional - If Historical Data Available)

| Week | Avg Score | Issues <60 | Issues ≥90 |
|------|-----------|------------|------------|
| W1   | 65%       | 12         | 8          |
| W2   | 72%       | 8          | 15         |
| W3   | 78%       | 3          | 25         |
| Current | [X]%   | [N]        | [M]        |

**Velocity**: [+X]% per week  
**Projection**: Ready for Phase 03 in [N] weeks
```

---

## 🎯 Your Task

When user requests completeness check, you will:

**STEP 1**: Query GitHub Issues  
**STEP 2**: Parse issue bodies for required sections  
**STEP 3**: Score each issue across 10 dimensions  
**STEP 4**: Generate complete audit report (see Expected Output above)  

**Always deliver the full report!** Do not summarize or skip sections.

---

## 🔍 Step 1: Query GitHub Issues

### Using GitHub CLI

```bash
# Get all requirements issues (StR, REQ-F, REQ-NF)
gh issue list \
  --label "phase:02-requirements" \
  --state all \
  --json number,title,labels,body,state \
  --limit 1000

# Filter by type
gh issue list --label "type:requirement:functional" --state all --json number,title,body
gh issue list --label "type:requirement:non-functional" --state all --json number,title,body
gh issue list --label "type:stakeholder-requirement" --state all --json number,title,body

# Filter by priority
gh issue list --label "priority:p0" --label "phase:02-requirements" --state all --json number,title,body
```

### Using GitHub MCP Server (Copilot)

```typescript
// List all requirements issues
const issues = await github.issues.listForRepo({
  owner: 'zarfld',
  repo: 'copilot-instructions-template',
  labels: 'phase:02-requirements',
  state: 'all',
  per_page: 100
});

// Filter functional requirements
const functionalReqs = issues.filter(i => 
  i.labels.some(l => l.name === 'type:requirement:functional')
);
```

### Python Script Example

```python
import requests
import re

GITHUB_TOKEN = "ghp_..."
REPO = "zarfld/IntelAvbFilter"

def get_requirements_issues():
    """Fetch all requirements issues from GitHub"""
    url = f"https://api.github.com/repos/{REPO}/issues"
    headers = {"Authorization": f"token {GITHUB_TOKEN}"}
    params = {
        "labels": "phase:02-requirements",
        "state": "all",
        "per_page": 100
    }
    
    response = requests.get(url, headers=headers, params=params)
    return response.json()

def classify_issue(issue):
    """Classify issue by type label"""
    labels = [l['name'] for l in issue['labels']]
    
    if 'type:stakeholder-requirement' in labels:
        return 'StR'
    elif 'type:requirement:functional' in labels:
        return 'REQ-F'
    elif 'type:requirement:non-functional' in labels:
        return 'REQ-NF'
    else:
        return 'UNKNOWN'

# Usage
issues = get_requirements_issues()
print(f"Total requirements: {len(issues)}")

for issue in issues:
    issue_type = classify_issue(issue)
    print(f"#{issue['number']}: {issue['title']} [{issue_type}]")
```

---

## 🔍 Step 2: Parse Issue Bodies

### Extract Required Sections

```python
def parse_issue_body(body: str) -> dict:
    """Parse issue body for required sections"""
    sections = {
        'description': '',
        'acceptance_criteria': '',
        'measurable_criteria': '',
        'error_handling': '',
        'security': '',
        'performance': '',
        'traceability': {
            'traces_to': [],
            'depends_on': [],
            'verified_by': []
        }
    }
    
    # Extract traceability links
    traces_to = re.findall(r'(?:Traces to|Parent):\s*#(\d+)', body, re.IGNORECASE)
    depends_on = re.findall(r'Depends on:\s*#(\d+)', body, re.IGNORECASE)
    verified_by = re.findall(r'Verified by:\s*#(\d+)', body, re.IGNORECASE)
    
    sections['traceability']['traces_to'] = [int(n) for n in traces_to]
    sections['traceability']['depends_on'] = [int(n) for n in depends_on]
    sections['traceability']['verified_by'] = [int(n) for n in verified_by]
    
    # Extract acceptance criteria (Gherkin blocks)
    acceptance_match = re.search(
        r'##\s*Acceptance Criteria\s*(.*?)(?=##|\Z)',
        body,
        re.DOTALL | re.IGNORECASE
    )
    if acceptance_match:
        sections['acceptance_criteria'] = acceptance_match.group(1).strip()
    
    # Extract measurable criteria
    measurable_match = re.search(
        r'##\s*Measurable Criteria\s*(.*?)(?=##|\Z)',
        body,
        re.DOTALL | re.IGNORECASE
    )
    if measurable_match:
        sections['measurable_criteria'] = measurable_match.group(1).strip()
    
    # Extract error handling
    error_match = re.search(
        r'##\s*Error Handling\s*(.*?)(?=##|\Z)',
        body,
        re.DOTALL | re.IGNORECASE
    )
    if error_match:
        sections['error_handling'] = error_match.group(1).strip()
    
    # Extract security
    security_match = re.search(
        r'##\s*Security\s*(.*?)(?=##|\Z)',
        body,
        re.DOTALL | re.IGNORECASE
    )
    if security_match:
        sections['security'] = security_match.group(1).strip()
    
    return sections

# Usage
issue_body = """
## Description
User can log in with email and password.

## Acceptance Criteria
```gherkin
Scenario: Successful login
  Given user has valid credentials
  When user submits login form
  Then user is authenticated
```

## Traceability
- Traces to:  #10 (StR: User Authentication)
- **Verified by**: #45 (TEST: Login Flow)
"""

sections = parse_issue_body(issue_body)
print(f"Parent StR: #{sections['traceability']['traces_to']}")
print(f"Test issues: {sections['traceability']['verified_by']}")
print(f"Has acceptance criteria: {bool(sections['acceptance_criteria'])}")
```

---

## 📊 Step 3: Score Each Issue (10 Dimensions)

### Scoring Algorithm

```python
def score_issue_completeness(issue: dict, parsed_body: dict) -> dict:
    """Score issue across 10 dimensions (0-10 each)"""
    scores = {}
    
    # 1. Functional Completeness (body has clear description)
    scores['functional'] = 10 if len(parsed_body['description']) > 100 else 5
    
    # 2. Input/Output Completeness (mentions inputs, outputs, validation)
    body_lower = issue['body'].lower()
    has_inputs = 'input' in body_lower or 'parameter' in body_lower
    has_outputs = 'output' in body_lower or 'return' in body_lower
    has_validation = 'validat' in body_lower or 'check' in body_lower
    scores['input_output'] = sum([has_inputs, has_outputs, has_validation]) * 3.33
    
    # 3. Error Handling (has Error Handling section)
    scores['error_handling'] = 10 if parsed_body['error_handling'] else 0
    
    # 4. Boundary Conditions (mentions limits, ranges, edge cases)
    has_boundaries = any(kw in body_lower for kw in [
        'minimum', 'maximum', 'limit', 'range', 'boundary', 'edge case'
    ])
    scores['boundaries'] = 10 if has_boundaries else 3
    
    # 5. Performance (has measurable performance criteria)
    has_performance = any(kw in body_lower for kw in [
        'response time', 'throughput', 'latency', 'ms', 'seconds'
    ])
    scores['performance'] = 10 if has_performance else 5
    
    # 6. Security (has Security section or mentions auth/encryption)
    has_security = parsed_body['security'] or any(kw in body_lower for kw in [
        'authentication', 'authorization', 'encryption', 'security'
    ])
    scores['security'] = 10 if has_security else 0
    
    # 7. Compliance (mentions regulations or standards)
    has_compliance = any(kw in body_lower for kw in [
        'gdpr', 'hipaa', 'pci', 'sox', 'compliance', 'regulation'
    ])
    scores['compliance'] = 10 if has_compliance else 5
    
    # 8. Integration (mentions external systems or APIs)
    has_integration = any(kw in body_lower for kw in [
        'api', 'endpoint', 'integration', 'external system', 'third-party'
    ])
    scores['integration'] = 10 if has_integration else 7
    
    # 9. Acceptance Criteria (has Gherkin scenarios)
    has_gherkin = 'given' in body_lower and 'when' in body_lower and 'then' in body_lower
    scenario_count = body_lower.count('scenario:')
    scores['acceptance_criteria'] = min(10, scenario_count * 2) if has_gherkin else 0
    
    # 10. Traceability (has parent and verification links)
    has_parent = bool(parsed_body['traceability']['traces_to'])
    has_tests = bool(parsed_body['traceability']['verified_by'])
    priority_label = next((l for l in issue['labels'] if 'priority' in l['name']), None)
    has_priority = bool(priority_label)
    
    traceability_score = sum([has_parent, has_tests, has_priority]) * 3.33
    scores['traceability'] = min(10, traceability_score)
    
    # Total score (out of 100)
    total = sum(scores.values())
    
    return {
        'scores': scores,
        'total': round(total),
        'rating': get_rating(total)
    }

def get_rating(score):
    """Convert numeric score to rating"""
    if score >= 90:
        return '✅ Complete'
    elif score >= 75:
        return '⚠️ Nearly Complete'
    elif score >= 60:
        return '🟡 Incomplete'
    else:
        return '🔴 Severely Incomplete'

# Usage
result = score_issue_completeness(issue, parsed_body)
print(f"Total: {result['total']}/100 - {result['rating']}")
```

---

## 📋 10 Completeness Dimensions (GitHub Issues Context)

### 1. **Functional Completeness** 🔧

**What to Check in Issue Body**:
- [ ] Clear description of functionality
- [ ] All user-facing features described
- [ ] All business rules documented
- [ ] All workflows/processes defined
- [ ] All CRUD operations covered

**Example - Complete Issue**:
```markdown
## Description
User authentication system with email/password login.

Features:
- Email/password login
- Password reset via email
- Account lockout after 5 failed attempts
- Session management (30 min timeout)
- Remember me option (7 days)
- Logout functionality
```

**Scoring**: Description >100 chars = 10, >50 chars = 7, else 3

### 2. **Input/Output Completeness** 📥📤

**What to Check in Issue Body**:
- [ ] Inputs section with types and validation
- [ ] Outputs section with format
- [ ] Validation rules specified

**Example**:
```markdown
## Inputs
- `email`: string, valid email format, max 255 chars
- `password`: string, 8-64 chars, must include uppercase, lowercase, number

## Outputs
- `authToken`: JWT string, 1 hour expiry
- `refreshToken`: string, 7 day expiry
- `user`: object {id, email, name, roles}

## Validation
- Email must be verified before login allowed
- Password checked against breach database (HaveIBeenPwned API)
```

**Scoring**: Has inputs + outputs + validation = 10, partial = 5, none = 0

### 3. **Error Handling Completeness** ⚠️

**What to Check in Issue Body**:
- [ ] Error Handling section exists
- [ ] All error scenarios documented
- [ ] User-facing error messages defined
- [ ] System actions specified

**Example**:
```markdown
## Error Handling

| Error Condition | User Message | System Action | HTTP Code |
|----------------|--------------|---------------|-----------|
| Invalid credentials | "Email or password incorrect" | Log attempt, increment fail counter | 401 |
| Account locked | "Account locked. Try again in 15 minutes" | Send security email | 403 |
| Unverified email | "Please verify your email first" | Offer resend verification | 403 |
| Network timeout | "Connection error. Please try again" | Log error, retry once | 503 |
```

**Scoring**: Has Error Handling section = 10, mentions errors = 5, none = 0

### 4. **Boundary Conditions Completeness** 📏

**What to Check in Issue Body**:
- [ ] Minimum/maximum values specified
- [ ] Edge cases documented
- [ ] Data type limits mentioned

**Keywords to Search**: minimum, maximum, limit, range, boundary, edge case, zero, empty, null

**Example**:
```markdown
## Boundary Conditions
- Email: min 5 chars, max 255 chars
- Password: min 8 chars, max 64 chars
- Login attempts: max 5 per 15 minutes
- Session duration: 30 minutes (idle), max 8 hours (absolute)
- Concurrent sessions: max 3 per user
- Empty password: rejected before API call
- SQL injection attempts: sanitized + logged + account flagged
```

**Scoring**: Mentions limits/boundaries = 10, implicit = 5, none = 0

### 5. **Performance Requirements Completeness** ⚡

**What to Check in Issue Body** (Especially for REQ-NF):
- [ ] Response time targets
- [ ] Throughput requirements
- [ ] Concurrency limits
- [ ] Resource usage limits

**Keywords**: response time, latency, throughput, ms, seconds, concurrent, TPS, RPS

**Example**:
```markdown
## Performance Requirements (REQ-NF)
- Login response: <500ms (95th percentile)
- Throughput: 100 logins/second sustained
- Concurrency: 500 simultaneous login attempts
- Database query: <100ms
- Token generation: <50ms
- Peak load (Black Friday): 1000 logins/second for 1 hour
```

**Scoring**: Has measurable metrics = 10, vague mention = 5, none = 0

### 6. **Security Requirements Completeness** 🔒

**What to Check in Issue Body**:
- [ ] Security section exists
- [ ] Authentication method specified
- [ ] Authorization rules defined
- [ ] Data protection mentioned (encryption)
- [ ] OWASP considerations

**Keywords**: authentication, authorization, encryption, security, OWASP, bcrypt, JWT

**Example**:
```markdown
## Security
- Password hashing: bcrypt cost 12
- JWT signing: HS256 with 256-bit secret
- Token storage: httpOnly cookie + localStorage
- Session fixation: New session ID after login
- Brute force: Rate limit 5 attempts/15 min
- OWASP A07: Multi-factor authentication (future phase)
- Audit logging: All login attempts logged (success + failure)
```

**Scoring**: Has Security section = 10, mentions auth/encryption = 5, none = 0

### 7. **Regulatory/Compliance Completeness** 📜

**What to Check in Issue Body**:
- [ ] Applicable regulations identified
- [ ] Compliance controls specified

**Keywords**: GDPR, HIPAA, PCI-DSS, SOX, CCPA, COPPA, compliance, regulation

**Example**:
```markdown
## Compliance
- GDPR Article 5: Log login attempts for 90 days only
- GDPR Article 15: User can export login history
- CCPA: Provide "Do Not Sell" option (not applicable to auth)
```

**Scoring**: Mentions specific regulations = 10, vague compliance = 5, none = 5 (N/A often)

### 8. **Integration/Interface Completeness** 🔗

**What to Check in Issue Body**:
- [ ] External systems identified
- [ ] API endpoints specified
- [ ] Integration error handling

**Keywords**: API, endpoint, integration, external, third-party, webhook

**Example**:
```markdown
## Integrations
- **HaveIBeenPwned API**: Check password against breach database
  - Endpoint: GET https://api.pwnedpasswords.com/range/{hash}
  - Timeout: 2 seconds
  - Fallback: Allow login if API unavailable (log warning)

- **SendGrid**: Email for password reset
  - Timeout: 5 seconds
  - Retry: 3 attempts with exponential backoff
```

**Scoring**: API details specified = 10, mentions integration = 7, none = 5

### 9. **Acceptance Criteria Completeness** ✅

**What to Check in Issue Body**:
- [ ] Acceptance Criteria section exists
- [ ] Gherkin scenarios (Given-When-Then)
- [ ] Happy path scenario
- [ ] Error path scenarios
- [ ] Edge case scenarios

**Scoring**: Count scenarios:
- 0 scenarios = 0 points
- 1 scenario = 3 points
- 2 scenarios = 5 points
- 3+ scenarios = 8 points
- 5+ scenarios = 10 points

**Example**:
```markdown
## Acceptance Criteria

```gherkin
Scenario: Successful login with valid credentials
  Given user "alice@example.com" has verified account
  And password is "ValidPass123"
  When user submits login form
  Then user is authenticated
  And JWT token is returned
  And user redirected to dashboard

Scenario: Failed login with invalid password
  Given user "alice@example.com" exists
  When user submits login with wrong password
  Then error "Email or password incorrect"
  And login attempt logged
  And fail counter incremented

Scenario: Account lockout after 5 failed attempts
  Given user has 4 failed login attempts
  When user submits wrong password again
  Then error "Account locked. Try again in 15 minutes"
  And account locked for 15 minutes
  And security email sent to user

Scenario: Login with unverified email
  Given user "bob@example.com" has unverified account
  When user submits login form
  Then error "Please verify your email first"
  And option to resend verification email shown
```gherkin
```

**Scoring**: Has Gherkin with multiple scenarios = 10, one scenario = 5, none = 0

### 10. **Traceability Completeness** 🔗

**What to Check in Issue**:
- [ ] Has `Traces to: #N` (parent StR)
- [ ] Has `Verified by: #N` (test issues)
- [ ] Has priority label (`priority:p0/p1/p2/p3`)
- [ ] Has status label or project status

**Example**:
```markdown
## Traceability
- Traces to:  #10 (StR: User Authentication System)
- **Depends on**: #15 (REQ-F: Email Verification)
- **Verified by**: #45 (TEST: Login Flow), #46 (TEST: Account Lockout)
- **Implemented in**: PR #78
```

**Scoring**:
- Has parent link = +3.33
- Has test link(s) = +3.33
- Has priority label = +3.33
- Total = 10

---

## 🔍 Step 4: Generate Completeness Report

### Report Template (Markdown)

```markdown
# Requirements Completeness Audit Report

**Repository**: zarfld/IntelAvbFilter
**Date**: 2024-11-13
**Auditor**: GitHub Copilot (ISO/IEC/IEEE 29148:2018)
**Scope**: All Phase 02 Requirements

## 📊 Executive Summary

**Total Issues Analyzed**: 45
- Stakeholder Requirements (StR): 8
- Functional Requirements (REQ-F): 30
- Non-Functional Requirements (REQ-NF): 7

**Overall Completeness**: 78% (Target: 90%+)
**Status**: ⚠️ NEEDS WORK

| Dimension | Avg Score | Status |
|-----------|-----------|--------|
| 1. Functional Completeness | 8.7/10 | ✅ |
| 2. Input/Output | 6.5/10 | ⚠️ |
| 3. Error Handling | 3.2/10 | 🔴 |
| 4. Boundaries | 5.1/10 | ⚠️ |
| 5. Performance | 6.8/10 | ⚠️ |
| 6. Security | 4.5/10 | 🔴 |
| 7. Compliance | 7.2/10 | ⚠️ |
| 8. Integration | 6.9/10 | ⚠️ |
| 9. Acceptance Criteria | 4.1/10 | 🔴 |
| 10. Traceability | 9.1/10 | ✅ |

**Distribution**:
- ✅ Complete (≥90): 12 issues (27%)
- ⚠️ Nearly Complete (75-89): 20 issues (44%)
- 🟡 Incomplete (60-74): 10 issues (22%)
- 🔴 Severely Incomplete (<60): 3 issues (7%)

## 🔴 Critical Gaps (Blockers for Phase 03)

### Issue #25: User Login (REQ-F-AUTH-001)
**Score**: 45/100 🔴 Severely Incomplete
**Priority**: P0
**Link**: https://github.com/zarfld/IntelAvbFilter/issues/25

**Dimension Scores**:
| Dimension | Score | Status |
|-----------|-------|--------|
| Functional | 8/10 | ✅ |
| Input/Output | 6/10 | ⚠️ |
| Error Handling | 0/10 | 🔴 **BLOCKER** |
| Boundaries | 4/10 | 🔴 |
| Performance | 5/10 | ⚠️ |
| Security | 0/10 | 🔴 **BLOCKER** |
| Compliance | 7/10 | ⚠️ |
| Integration | 6/10 | ⚠️ |
| Acceptance Criteria | 0/10 | 🔴 **BLOCKER** |
| Traceability | 9/10 | ✅ |

**Missing Elements**:
- ❌ No Error Handling section
- ❌ No Security section (authentication/encryption not specified)
- ❌ No Acceptance Criteria (no Gherkin scenarios)
- ❌ No "Verified by: #N" test links

**Actionable Fix** (Edit Issue #25):

Add these sections to issue body:

\`\`\`markdown
## Error Handling

| Error Condition | User Message | System Action | HTTP Code |
|----------------|--------------|---------------|-----------|
| Invalid credentials | "Email or password incorrect" | Log attempt, increment fail counter | 401 |
| Account locked | "Account locked. Try again in 15 minutes" | Send security email | 403 |
| Unverified email | "Please verify your email first" | Offer resend verification | 403 |
| Network timeout | "Connection error. Please try again" | Retry once, then show error | 503 |
| Database unavailable | "System temporarily unavailable" | Alert on-call engineer | 503 |

## Security
- **Password Hashing**: bcrypt cost 12
- **JWT Signing**: HS256 with 256-bit secret, 1 hour expiry
- **Token Storage**: httpOnly cookie (web), secure storage (mobile)
- **Rate Limiting**: 5 login attempts per 15 minutes per IP
- **Brute Force Protection**: Account lockout after 5 failed attempts
- **Audit Logging**: All login attempts logged (success + failure)

## Acceptance Criteria

\`\`\`gherkin
Scenario: Successful login
  Given user "alice@example.com" has verified account
  And password is "ValidPass123"
  When user submits login form
  Then user is authenticated
  And JWT token returned
  And user redirected to dashboard

Scenario: Failed login - invalid password
  Given user "alice@example.com" exists
  When user submits wrong password
  Then error "Email or password incorrect"
  And login attempt logged

Scenario: Account lockout
  Given user has 4 failed login attempts
  When user submits wrong password again
  Then error "Account locked. Try again in 15 minutes"
  And security email sent
\`\`\`

## Traceability
- **Verified by**: #45 (TEST: Login Flow), #46 (TEST: Account Lockout)
\`\`\`

**After Fix**: Re-run completeness check to verify score improves to ≥90/100

---

[Repeat for each issue <70]

## ⚠️ Warnings (Issues 70-89)

### Issue #30: Password Reset (REQ-F-AUTH-003)
**Score**: 82/100 ⚠️ Nearly Complete
**Link**: https://github.com/zarfld/IntelAvbFilter/issues/30

**Minor Gaps**:
- Missing boundary condition: Max password reset requests per hour
- Security: Token expiry not specified

**Quick Fix**: Add to issue body:
\`\`\`markdown
## Boundary Conditions
- Max reset requests: 3 per hour per email
- Token expiry: 1 hour
- Token length: 32 bytes (256-bit)
\`\`\`

---

## ✅ Well-Specified Requirements (≥90)

- ✅ #35: GDPR Data Export (98/100)
- ✅ #40: Encryption at Rest (95/100)
- ✅ #42: API Rate Limiting (92/100)
[... list all complete issues]

---

## 📋 Dimension Analysis

### Worst Performing Dimensions

1. **Error Handling** (avg: 3.2/10) 🔴
   - 28 issues have no Error Handling section
   - **Action**: Add Error Handling table to all REQ-F issues
   - **Template**: See examples above

2. **Acceptance Criteria** (avg: 4.1/10) 🔴
   - 22 issues have no Gherkin scenarios
   - **Action**: Add Given-When-Then scenarios (minimum 3 per issue)
   - **Template**: See requirements-elicit.prompt.md

3. **Security** (avg: 4.5/10) 🔴
   - 18 issues mention no security controls
   - **Action**: Security review using OWASP checklist
   - **Reference**: https://owasp.org/www-project-top-ten/

### Best Performing Dimensions

1. **Traceability** (avg: 9.1/10) ✅
   - All issues have parent links and priority labels
   - Good job on issue linking!

2. **Functional Completeness** (avg: 8.7/10) ✅
   - Clear functional descriptions

---

## 🎯 Action Items by Priority

### P0 - CRITICAL (Block Phase 03 - Complete by EOW)
- [ ] Fix #25: Add Error Handling, Security, Acceptance Criteria (8h)
- [ ] Fix #27: Add Acceptance Criteria and test links (4h)
- [ ] Fix #32: Add Error Handling section (3h)
**Total**: 15 hours

### P1 - HIGH (Complete This Sprint)
- [ ] Add Error Handling to 25 remaining REQ-F issues (25h)
- [ ] Add Acceptance Criteria to 19 remaining issues (38h)
- [ ] Security review for 15 issues (15h)
**Total**: 78 hours

### P2 - MEDIUM (Complete Before Release)
- [ ] Add performance metrics to 8 REQ-NF issues (8h)
- [ ] Add boundary conditions to 12 issues (12h)
**Total**: 20 hours

---

## ✅ Exit Criteria for Phase 03

| Criterion | Current | Target | Status |
|-----------|---------|--------|--------|
| Avg completeness score | 78% | ≥90% | 🔴 |
| Issues ≥90/100 | 27% | ≥90% | 🔴 |
| P0/P1 issues ≥90/100 | 45% | 100% | 🔴 |
| Issues <60/100 | 3 | 0 | 🔴 |
| All issues have parent links | 100% | 100% | ✅ |
| All issues have test links | 35% | ≥80% | 🔴 |

**Overall Status**: 🔴 NOT READY FOR PHASE 03

**Estimated Work**: 113 hours (P0 + P1)  
**With 2 engineers**: ~3 weeks  
**Recommended**: Focus on P0 issues first (15h) to unblock critical path

---

## 🔍 Traceability Graph (Top-Level)

\`\`\`
StR #10: User Authentication System
  ├─ REQ-F #25: Login ⚠️ 45/100 (BLOCKER)
  ├─ REQ-F #26: Logout ✅ 92/100
  ├─ REQ-F #30: Password Reset ⚠️ 82/100
  ├─ REQ-NF #35: Login Performance ✅ 95/100
  └─ REQ-NF #40: Password Encryption ✅ 98/100

StR #11: Data Privacy & GDPR
  ├─ REQ-F #50: GDPR Export ✅ 98/100
  ├─ REQ-F #51: GDPR Deletion ⚠️ 78/100
  └─ REQ-NF #55: Encryption at Rest ✅ 95/100

StR #12: Payment Processing
  ├─ REQ-F #60: Stripe Integration 🔴 55/100 (BLOCKER)
  ├─ REQ-F #61: Refunds ⚠️ 72/100
  └─ REQ-NF #65: PCI-DSS Compliance ⚠️ 68/100
\`\`\`

**Orphan Issues** (no parent StR):
- 🔴 REQ-F #75: Email Notifications (no "Traces to: #N")
- 🔴 REQ-NF #80: Database Backup (no parent)

**Action**: Add "Traces to: #N" links to orphan issues

---

## 📈 Trend Analysis

*(This section requires historical data - run this report weekly)*

| Week | Avg Score | Issues <60 | Issues ≥90 | Total Issues |
|------|-----------|------------|------------|--------------|
| 2024-W45 | 65% | 12 | 5 | 40 |
| 2024-W46 | 72% | 8 | 10 | 42 |
| Current (W47) | 78% | 3 | 12 | 45 |

**Velocity**: +6% per week  
**Projection**: Ready for Phase 03 in ~2 weeks (if velocity maintained)

---

## 🔧 Automation Recommendations

### CI/CD: Automated Completeness Checks

Create `.github/workflows/requirements-completeness.yml`:

\`\`\`yaml
name: Requirements Completeness Check

on:
  issues:
    types: [opened, edited, labeled]
  pull_request:
    types: [opened, synchronize]
  schedule:
    - cron: '0 9 * * 1'  # Weekly Monday 9 AM

jobs:
  completeness-check:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      
      - name: Setup Python
        uses: actions/setup-python@v5
        with:
          python-version: '3.11'
      
      - name: Install dependencies
        run: |
          pip install requests PyGithub
      
      - name: Run completeness audit
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
        run: |
          python scripts/github-completeness-audit.py
      
      - name: Post comment with results
        if: github.event_name == 'issues'
        uses: actions/github-script@v7
        with:
          script: |
            const fs = require('fs');
            const report = fs.readFileSync('completeness-report.md', 'utf8');
            
            github.rest.issues.createComment({
              owner: context.repo.owner,
              repo: context.repo.repo,
              issue_number: context.issue.number,
              body: report
            });
      
      - name: Fail if score < 70
        run: |
          SCORE=$(cat completeness-score.txt)
          if [ "$SCORE" -lt 70 ]; then
            echo "❌ Completeness score $SCORE% is below threshold (70%)"
            exit 1
          fi
\`\`\`

### Python Script: `scripts/github-completeness-audit.py`

\`\`\`python
#!/usr/bin/env python3
"""
GitHub Issues Completeness Audit

Scores requirements issues across 10 dimensions and generates
completeness report.

Usage:
    python scripts/github-completeness-audit.py
    
Environment:
    GITHUB_TOKEN - GitHub personal access token
    GITHUB_REPOSITORY - owner/repo (optional, reads from git remote)
"""

import os
import re
import sys
import json
from typing import Dict, List
from github import Github

# [Insert complete scoring algorithm from Step 3 above]

def main():
    token = os.getenv('GITHUB_TOKEN')
    repo_name = os.getenv('GITHUB_REPOSITORY', 'zarfld/IntelAvbFilter')
    
    g = Github(token)
    repo = g.get_repo(repo_name)
    
    # Fetch all requirements issues
    issues = repo.get_issues(
        labels=['phase:02-requirements'],
        state='all'
    )
    
    results = []
    for issue in issues:
        parsed = parse_issue_body(issue.body or '')
        score_result = score_issue_completeness(issue, parsed)
        
        results.append({
            'issue': issue,
            'parsed': parsed,
            'score': score_result
        })
    
    # Generate report
    report = generate_report(results)
    
    # Write outputs
    with open('completeness-report.md', 'w') as f:
        f.write(report)
    
    avg_score = sum(r['score']['total'] for r in results) / len(results)
    with open('completeness-score.txt', 'w') as f:
        f.write(str(int(avg_score)))
    
    print(f"✅ Report generated: completeness-report.md")
    print(f"📊 Average score: {avg_score:.1f}/100")

if __name__ == '__main__':
    main()
\`\`\`

---

## 🚀 Usage

### Manual Audit (Copilot Chat)

\`\`\`
@workspace Audit requirements completeness for all Phase 02 issues
\`\`\`

### Automated (GitHub Actions)

\`\`\`bash
# Install workflow
cp .github/workflows/requirements-completeness.yml.example \\
   .github/workflows/requirements-completeness.yml

# Run locally
python scripts/github-completeness-audit.py

# View report
cat completeness-report.md
\`\`\`

### Query Specific Issues

\`\`\`bash
# Check single issue
gh issue view 25 --json body,labels,number

# List incomplete issues
gh issue list --label "phase:02-requirements" --json number,title | \\
  jq '.[] | select(.completeness < 70)'
\`\`\`

---

## ✅ Checklist for Issue Completeness

Use this when creating/reviewing requirement issues:

- [ ] **Description**: Clear functional description (>100 words)
- [ ] **Inputs**: All inputs with types, ranges, validation rules
- [ ] **Outputs**: All outputs with formats
- [ ] **Error Handling**: Table with all error scenarios
- [ ] **Boundaries**: Min/max values, edge cases, limits
- [ ] **Performance**: Response time, throughput, concurrency (for REQ-NF)
- [ ] **Security**: Auth, authz, encryption, OWASP considerations
- [ ] **Compliance**: Applicable regulations (GDPR, HIPAA, etc.)
- [ ] **Integration**: External systems, APIs, timeouts, fallbacks
- [ ] **Acceptance Criteria**: 3+ Gherkin scenarios (happy + error paths)
- [ ] **Traceability**: "Traces to: #N" (parent StR)
- [ ] **Verification**: "Verified by: #N" (test issues)
- [ ] **Labels**: type:requirement:functional/non-functional, phase:02-requirements, priority:p0/p1/p2/p3

**Score Target**: ≥90/100 before proceeding to Phase 03 (Architecture)

---

**Remember**: Complete requirements prevent costly rework! Invest time upfront to save 10x effort during implementation. 🎯
````