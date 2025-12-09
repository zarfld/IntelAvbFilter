---
description: "Phase 02 guidance for requirements analysis and specification following ISO/IEC/IEEE 29148:2018. Covers functional/non-functional requirements, user stories, and traceability."
applyTo: "02-requirements/**"
---

# Phase 02: Requirements Analysis & Specification

**Standards**: ISO/IEC/IEEE 29148:2018 (System Requirements), ISO/IEC/IEEE 12207:2017  
**XP Integration**: User Stories, Acceptance Tests, YAGNI Principle

## 🎯 Phase Objectives

1. Transform stakeholder requirements into system requirements
2. Define functional and non-functional requirements
3. Create detailed use cases and user stories
4. Establish requirements traceability
5. Define testable acceptance criteria

## 📋 Requirements Capture Method

### GitHub Issues (Primary and Recommended)

**All requirements MUST be captured as GitHub Issues** using the Functional Requirement (REQ-F) or Non-Functional Requirement (REQ-NF) templates.

**Benefits**:
- ✅ No YAML front matter required - metadata captured via issue fields, labels, and project columns
- ✅ Traceability via `#N` syntax in issue bodies
- ✅ ISO/IEC/IEEE 29148:2018 compliance via issue templates
- ✅ Automated validation via GitHub Actions
- ✅ Real-time collaboration and discussion
- ✅ Integration with pull requests and CI/CD
- ✅ Searchable and filterable with labels and milestones

## 📋 ISO/IEC/IEEE 29148:2018 Compliance

### System Requirements Process Activities

1. **Requirements Analysis**
   - Decompose stakeholder requirements
   - Identify system boundaries
   - Define interfaces
   - Analyze feasibility
   - Resolve conflicts

2. **System Requirements Specification**
   - Define functional requirements
   - Define non-functional requirements (quality attributes)
   - Specify constraints
   - Define interfaces
   - Create System Requirements Specification (SyRS)

3. **Requirements Validation**
   - Review for completeness
   - Check consistency
   - Verify traceability to stakeholder requirements
   - Validate with stakeholders

## 🎨 XP Practices for This Phase

### User Stories
Transform requirements into user stories:
```markdown
As a [user role]
I want to [action/capability]
So that [business value]

Acceptance Criteria:
- Given [context]
- When [action]
- Then [outcome]
```

### YAGNI (You Aren't Gonna Need It)
- Only specify requirements for current iteration + 1
- Avoid speculative features
- Keep requirements minimal and focused

### Acceptance Test-Driven Development
- Define acceptance tests BEFORE implementation
- Make acceptance criteria executable
- Customer defines acceptance tests

## 📝 Requirements Documentation Approach

### ⭐ PRIMARY: GitHub Issues (Recommended)

**System requirements should be captured as GitHub Issues** using the Functional Requirement (REQ-F) or Non-Functional Requirement (REQ-NF) templates.

#### Creating Functional Requirements as GitHub Issues

1. **Navigate to Issues → New Issue**
2. **Select Template**: "Functional Requirement (REQ-F)"
3. **Complete Required Fields**:
   - **Title**: Clear capability statement (e.g., "User can export data to CSV format")
   - **Parent Stakeholder Requirement**: Link to parent StR issue using `#N` syntax
   - **Requirement Description**: What the system shall do (use "shall" for mandatory)
   - **Acceptance Criteria**: Testable conditions (Given-When-Then format)
   - **Priority**: Critical (P0) / High (P1) / Medium (P2) / Low (P3)
   - **Integrity Level**: 1 (highest) through 4 (lowest) per IEEE 1012-2016
   - **Dependencies**: Other requirements this depends on
   - **Verification Method**: Inspection / Analysis / Demonstration / Test
   
4. **Apply Labels**:
   - `functional-requirement` (auto-applied by template)
   - `phase-02` (lifecycle phase)
   - `priority-*` and `integrity-*` labels
   
5. **Submit** → GitHub assigns issue number (e.g., #45)

6. **Set Status in GitHub Project** (see [Status Management Guide](../../docs/github-issue-status-management.md#2️⃣-system-requirements-req)):
   - **Initial Status**: "Specified"
   - **Progression**: Specified → Analyzed → Traceable → Approved → Implemented → Verified
   - **Close When**: Fully implemented and verified (all tests pass)

#### Creating Non-Functional Requirements as GitHub Issues

1. **Navigate to Issues → New Issue**
2. **Select Template**: "Non-Functional Requirement (REQ-NF)"
3. **Complete Required Fields**:
   - **Title**: Quality attribute with metric (e.g., "System response time under 200ms for 95th percentile")
   - **Parent Stakeholder Requirement**: Link to parent StR issue (`#N`)
   - **Quality Attribute Category**: Performance / Security / Usability / Reliability / Maintainability / Portability
   - **Requirement Description**: Specific quality constraint
   - **Measurable Criteria**: Objective metrics and targets
   - **Verification Method**: How to test/measure compliance
   - **Priority** and **Integrity Level**
   
4. **Apply Labels**:
   - `non-functional-requirement`
   - `phase-02`
   - Category-specific labels if available

5. **Set Status in GitHub Project**: Same progression as REQ-F (Specified → Analyzed → Traceable → Approved → Implemented → Verified)

#### Example: Creating REQ-F Issue

**Title**: User can filter product list by multiple categories

**Parent Stakeholder Requirement**:
```markdown
Traces to:  #1 (StR: Improve product discovery experience)
```

**Requirement Description**:
```markdown
The system **shall** allow users to:
1. Select multiple product categories simultaneously (e.g., "Electronics" AND "Sale Items")
2. Apply filters with AND/OR logic
3. See real-time count of matching products
4. Clear all filters with single action
5. Save filter combinations for future use
```

**Acceptance Criteria**:
```markdown
### Scenario 1: Apply multiple filters
**Given** user is on product listing page  
**And** at least 100 products exist across 5+ categories  
**When** user selects "Electronics" AND "Sale Items" filters  
**Then** system displays only products matching both categories  
**And** product count updates to show "23 items"  
**And** response time is < 500ms

### Scenario 2: Clear filters
**Given** user has 3 filters applied  
**When** user clicks "Clear All Filters" button  
**Then** all filters are removed  
**And** full product list is displayed  
**And** filter count shows "0 active filters"

### Scenario 3: Save filter combination
**Given** user has applied 2+ filters  
**When** user clicks "Save Filters" and enters name "My Electronics Deals"  
**Then** filter combination is saved to user profile  
**And** appears in "Saved Filters" dropdown on next visit
```

**Priority**: High (P1)  
**Integrity Level**: 2 (high criticality)  
**Dependencies**: REQ-F-012 (Product catalog API), REQ-NF-015 (Database query performance)  
**Verification Method**: Test (automated integration tests + manual UAT)

**Labels**: `functional-requirement`, `phase-02`, `priority-high`, `integrity-2`

After submission → Issue #45 created

#### Example: Creating REQ-NF Issue

**Title**: Search results return within 200ms for 95% of queries

**Parent Stakeholder Requirement**:
```markdown
Traces to:  #2 (StR: Fast, responsive user experience)
```

**Quality Attribute Category**: Performance

**Requirement Description**:
```markdown
The system **shall** return search results with the following performance characteristics:
- **95th percentile**: ≤ 200ms response time
- **99th percentile**: ≤ 500ms response time
- **Average**: ≤ 100ms response time
- **Database queries**: Maximum 3 queries per search
- **Concurrent users**: Performance maintained under 1000 concurrent users
```

**Measurable Criteria**:
```markdown
| Metric | Target | Measurement Method |
|--------|--------|-------------------|
| P95 response time | ≤ 200ms | APM tool (New Relic) |
| P99 response time | ≤ 500ms | APM tool |
| Average response | ≤ 100ms | APM tool |
| Query count | ≤ 3 queries | Database profiler |
| Load capacity | 1000 concurrent | Load testing (JMeter) |
```

**Verification Method**:
```markdown
1. **Performance Testing**: JMeter load test with 1000 virtual users
2. **Production Monitoring**: APM tool continuous monitoring
3. **Acceptance Criteria**: All metrics met for 7 consecutive days in production
```

**Priority**: Critical (P0)  
**Integrity Level**: 1 (highest - customer-facing performance)

**Labels**: `non-functional-requirement`, `phase-02`, `priority-critical`, `integrity-1`

After submission → Issue #46 created

#### Traceability via GitHub Issues

Requirements trace to parent stakeholder requirements:
```markdown
## Traceability
- Traces to:  #1, #2 (parent StR issues)
- **Depends on**: #12, #15 (prerequisite requirements)
- **Refined by**: #78, #79 (child design decisions in Phase 03)
- **Implemented by**: #PR-25 (pull request)
- **Verified by**: #120, #121 (test case issues)
```

**Automated Validation**: GitHub Actions validates:
- Every REQ-F/REQ-NF has at least one parent StR link
- Parent issues exist and are labeled `stakeholder-requirement`
- No circular dependencies
- All dependencies resolved before marking requirement complete

#### Querying Requirements

Use GitHub search:
```
# All functional requirements
is:issue label:functional-requirement label:phase-02

# High priority non-functional requirements
is:issue label:non-functional-requirement label:priority-high

# Performance requirements
is:issue label:non-functional-requirement "performance" in:title

# Requirements tracing to specific StR
is:issue label:functional-requirement "#1" in:body
```

Or use GitHub MCP via Copilot:
```
List all functional requirements for Phase 02
Show non-functional requirements with priority critical
```

#### Generate Requirements Specification from Issues

```bash
# Generate System Requirements Specification document
python scripts/github-traceability-report.py --type requirements --output SyRS.md
```

Produces ISO/IEC/IEEE 29148-compliant specification document from issues.

#### Status Management Workflow

**ISO/IEC/IEEE 29148:2018** requires maintaining requirements attributes including baseline status and verification status. See [GitHub Issue Status Management Guide](../../docs/github-issue-status-management.md#2️⃣-system-requirements-req) for detailed workflow.

**Quick Reference - REQ Status States**:

| Status | Definition | When to Apply |
|--------|------------|---------------|
| **Specified** | Requirement written in clear, testable form | Passes EARS template check |
| **Analyzed** | Checked for ambiguity, consistency, necessity | Criticality assigned, conflicts resolved |
| **Traceable** | Linked upward (StR) and downward (ADC) | All traceability links present |
| **Approved** | Formally approved and baselined | Stakeholder/technical approval |
| **Implemented** | Design and code completed | PR merged with REQ reference |
| **Verified** | Objective evidence confirms fulfillment | All test cases pass |

**Updating Status** (add comment to issue):
```markdown
## Status Update (2025-12-10)
**Previous Status**: Specified
**New Status**: Analyzed
**Analysis Results**:
- Ambiguity check: PASS (clear EARS format)
- Consistency check: PASS (no conflicts with #46)
- Necessity check: PASS (traces to StR #1)
- **Criticality**: High (security-related, integrity level IL-3)
- **Risk**: Medium (third-party API dependency)

**Next Steps**: Add traceability links to architecture components (ADR #78, ARC-C #79)
```

**Verification Evidence** (when moving to "Verified"):
```markdown
## Verification Complete (2025-12-18)
**Status**: Verified → Ready to Close
**Test Cases**: #120 (PASS), #121 (PASS), #122 (PASS)
**Coverage**: 94% lines, 89% branches
**Acceptance Criteria**: All 5 scenarios passed
**Evidence**: 
- Unit tests: 15 tests, 100% pass rate
- Integration tests: 8 tests, 100% pass rate
- Manual verification: Staging deployment (2025-12-17)

**Verified By**: John Doe (QA Lead)
**Closure Recommendation**: Close after production deployment
```

**Best Practices**:
- ✅ Set status to "Specified" upon creation (after template validation)
- ✅ Update status with evidence at each transition
- ✅ Don't skip "Analyzed" or "Traceable" states (required for compliance)
- ✅ Close only when fully verified (tests pass with objective evidence)
- ✅ Link PRs and test results in status update comments
- ❌ Never move to "Approved" without criticality analysis
- ❌ Don't move to "Implemented" without PR link
- ❌ Don't move to "Verified" without test evidence

### 📝 Supplementary Documentation Files

While **GitHub Issues are the single source of truth**, you may create supplementary markdown files in `02-requirements/` folders for:
- Detailed use case descriptions (reference issue #N)
- Complex domain models and diagrams
- Background research and analysis
- Reference documentation

**Critical Rule**: All supplementary files MUST reference the canonical GitHub Issue(s) using `#N` syntax.

### 1. Use Cases (Optional Supplements to Issues)
**Location**: `use-cases/UC-XXX-[name].md`
**References**: Must link to REQ-F issue(s)

Follow "Writing Effective Use Cases" (Alistair Cockburn) format:

```markdown
# Use Case: UC-001 [Use Case Name]

## Brief Description
[One paragraph summary]

## Actors
- **Primary Actor**: [User role who initiates]
- **Secondary Actors**: [Supporting actors]
- **Stakeholders and Interests**:
  - [Stakeholder]: [Interest]

## Preconditions
- [State that must be true before use case starts]

## Postconditions
- **Success End Condition**: [System state after success]
- **Failure End Condition**: [System state after failure]

## Main Success Scenario
1. [Actor action]
2. [System response]
3. [Next actor action]
4. [System response]
...

## Extensions (Alternative Flows)
### 3a. [Alternative condition]
- 3a1. [Alternative action]
- 3a2. [System response]

## Special Requirements
- [Non-functional requirements specific to this use case]

## Technology and Data Variations List
- [Variations in implementation]

## Frequency of Occurrence
[How often this use case occurs]

## Trace to Requirements
- REQ-F-001
- REQ-NF-005
```

### 3. User Stories
**Location**: `user-stories/STORY-XXX-[name].md`

```markdown
# User Story: STORY-001 [Story Title]

## Story
As a [user role]
I want to [action/feature]
So that [business benefit]

## Trace to Requirements
- StR-XXX
- REQ-F-XXX

## Acceptance Criteria
### Scenario 1: [Success Path]
Given [initial context]
And [additional context]
When [action taken]
Then [expected outcome]
And [additional outcome]

### Scenario 2: [Alternative Path]
Given [different context]
When [action taken]
Then [expected outcome]

### Scenario 3: [Error Handling]
Given [error condition]
When [action taken]
Then [error handling expected]

## Definition of Done
- [ ] Code implemented
- [ ] Unit tests pass (TDD)
- [ ] Acceptance tests pass
- [ ] Code reviewed
- [ ] Documentation updated
- [ ] Integrated into main branch

## Story Points
[Estimate: 1, 2, 3, 5, 8, 13]

## Priority
[Critical/High/Medium/Low]

## Dependencies
- STORY-XXX (must complete first)

## Technical Notes
[Implementation hints, constraints, risks]

## Questions/Clarifications Needed
- [ ] [Question 1]
- [ ] [Question 2]
```

### 4. Non-Functional Requirements Document
**Location**: `non-functional/nfr-specification.md`

```markdown
# Non-Functional Requirements Specification

## 1. Performance Requirements

### REQ-NF-P-001: Response Time
- **Description**: System response time for user interactions
- **Metric**: 95th percentile response time
- **Target**: < 200ms
- **Test Method**: Load testing with 1000 concurrent users
- **Trace to**: StR-XXX

### REQ-NF-P-002: Throughput
- **Description**: Transaction processing capacity
- **Metric**: Transactions per second (TPS)
- **Target**: 10,000 TPS sustained
- **Test Method**: Performance testing under load

## 2. Security Requirements

### REQ-NF-S-001: Authentication
- **Description**: User authentication mechanism
- **Requirement**: Multi-factor authentication required
- **Standard**: NIST 800-63B compliance
- **Test Method**: Security audit

### REQ-NF-S-002: Data Encryption
- **Description**: Data protection in transit and at rest
- **Requirement**: AES-256 encryption
- **Standard**: FIPS 140-2 compliance

## 3. Usability Requirements

### REQ-NF-U-001: Learnability
- **Description**: Time for new user to become productive
- **Metric**: Task completion time
- **Target**: 80% of users complete core tasks in < 10 minutes
- **Test Method**: Usability testing

## 4. Reliability Requirements

### REQ-NF-R-001: Availability
- **Description**: System uptime
- **Metric**: Percentage uptime
- **Target**: 99.9% (8.76 hours downtime/year max)
- **Test Method**: Availability monitoring

### REQ-NF-R-002: Mean Time Between Failures (MTBF)
- **Target**: > 720 hours

## 5. Maintainability Requirements

### REQ-NF-M-001: Code Quality
- **Metric**: Code coverage
- **Target**: > 80% unit test coverage
- **Standard**: XP TDD practices

### REQ-NF-M-002: Technical Debt
- **Metric**: Maintainability Index
- **Target**: > 75 (Visual Studio metric)

## 6. Scalability Requirements

### REQ-NF-SC-001: Horizontal Scaling
- **Description**: Ability to add capacity by adding nodes
- **Target**: Linear scaling up to 10 nodes

## 7. Compliance Requirements

### REQ-NF-C-001: [Regulatory Requirement]
- **Standard**: [e.g., GDPR, HIPAA, SOC 2]
- **Requirements**: [Specific compliance requirements]
```

## 🚨 Critical Requirements for This Phase

### Always Do
✅ Trace every system requirement to stakeholder requirement(s)  
✅ Define testable acceptance criteria for every requirement  
✅ Include non-functional requirements (not just functional)  
✅ Use consistent requirement IDs (REQ-F-XXX, REQ-NF-XXX)  
✅ Write user stories in Given-When-Then format  
✅ Prioritize with customer involvement (Planning Game)  
✅ Document assumptions and dependencies explicitly  
✅ Validate requirements with stakeholders  
✅ Use "shall" for mandatory binding provisions  
✅ Write requirements that specify "what" not "how" (avoid design constraints)  
✅ Make requirements verifiable and testable with objective criteria  
✅ Use active voice in requirement statements  
✅ Apply YAGNI: specify only for current iteration + 1  
✅ Define acceptance tests BEFORE implementation (ATDD)  
✅ Customer defines acceptance tests  
✅ Focus on delivering value; maximize work NOT done  
✅ Recognize that requirements constantly change; embrace evolution  

### Never Do
❌ Create untraceable requirements  
❌ Use ambiguous language ("fast," "user-friendly," superlatives, subjective terms)  
❌ Mix requirements with design solutions  
❌ Skip non-functional requirements  
❌ Create requirements without acceptance criteria  
❌ Proceed with inconsistent or conflicting requirements  
❌ Specify requirements for "future" features (YAGNI / No gold plating)  
❌ Use loopholes ("if possible", "as appropriate", "as applicable")  
❌ Use "must" (use "shall" for mandatory, "should" for desired)  
❌ Write negative requirements ("shall not")  
❌ Wait for requirements to be "finished" before design/implementation  
❌ Build for tomorrow based on hypothetical needs  
❌ Value requirements only for developers (e.g., dictating specific tech stack)  

## 🔍 Clarifying Questions to Ask

### About Functional Requirements
1. What exactly should the system do in this scenario?
2. What are all the possible outcomes/results?
3. What happens in error conditions?
4. Are there any special cases or exceptions?
5. What data is needed as input?
6. What data should be produced as output?

### About Non-Functional Requirements
1. What are the performance expectations?
   - Response time?
   - Throughput?
   - Number of concurrent users?
2. What are the security requirements?
   - Authentication method?
   - Authorization levels?
   - Data encryption needs?
3. What are the availability requirements?
   - Uptime percentage?
   - Maintenance windows?
4. What are the usability requirements?
   - Accessibility standards?
   - Browser support?
   - Mobile support?
5. What are the scalability needs?
   - Expected growth?
   - Peak load scenarios?

### About Acceptance Criteria
1. How will we know this requirement is satisfied?
2. What test scenarios must pass?
3. What would constitute a failure?
4. Who defines "done" for this requirement?

### Example Clarification Request
```markdown
## Clarification Needed: REQ-F-042 User Login

**Current Understanding**: "Users should be able to log in securely"

**Questions**:
1. **Authentication Method**: 
   - Username/password only?
   - Multi-factor authentication required?
   - Social login (Google, Microsoft) supported?

2. **Session Management**:
   - Session timeout duration?
   - "Remember me" functionality needed?
   - Concurrent session handling?

3. **Security Requirements**:
   - Password complexity rules?
   - Account lockout after failed attempts?
   - Password reset process?

4. **Error Handling**:
   - What feedback for invalid credentials?
   - How to handle account locked scenarios?

5. **Acceptance Criteria**:
   - Response time requirement?
   - Success/failure scenarios to test?

**Impact**: These details are needed to:
- Create complete, testable requirements
- Design proper security measures
- Write comprehensive acceptance tests
- Estimate implementation effort accurately
```

## 📊 Requirements Quality Checklist

Each requirement must be:
- [ ] **Complete** - Fully describes the capability
- [ ] **Correct** - Accurately represents stakeholder need
- [ ] **Consistent** - No conflicts with other requirements
- [ ] **Unambiguous** - Only one interpretation possible
- [ ] **Verifiable** - Can be tested/verified
- [ ] **Traceable** - Linked to stakeholder requirement
- [ ] **Feasible** - Technically and economically achievable
- [ ] **Necessary** - Required for success (YAGNI)
- [ ] **Prioritized** - Relative importance defined
- [ ] **Atomic** - Single, specific requirement

## 📊 Phase Entry Criteria

✅ Stakeholder Requirements Specification (StRS) approved  
✅ Stakeholders available for clarification  
✅ Business context understood  
✅ Technical feasibility assessed

## 📊 Phase Exit Criteria

✅ System Requirements Specification (SyRS) complete  
✅ All functional requirements documented with acceptance criteria  
✅ All non-functional requirements documented with metrics  
✅ Use cases written for key scenarios  
✅ User stories created with acceptance tests  
✅ Traceability matrix complete (REQ → StR)  
✅ Requirements reviewed and approved by stakeholders  
✅ Requirements baseline established  
✅ No unresolved conflicts or ambiguities  

## 🔗 Traceability

Establish complete traceability chain:
```
StR-XXX (Stakeholder Requirement)
  ↓
REQ-F-XXX (Functional Requirement)
REQ-NF-XXX (Non-Functional Requirement)
  ↓
UC-XXX (Use Case)
STORY-XXX (User Story)
  ↓
[Next Phase: Architecture - ARC-XXX]
```

## 📚 Standards References

- **ISO/IEC/IEEE 29148:2018** - Section 5.3 (System Requirements)
- **ISO/IEC/IEEE 12207:2017** - Section 6.4.2 (System Requirements Analysis)
- **Writing Effective Use Cases** - Alistair Cockburn
- **XP Practices** - User Stories, Acceptance Tests, YAGNI

## 🎯 Next Phase

Once this phase is complete, proceed to:
**Phase 03: Architecture Design** (`03-architecture/`)

---

**Remember**: Requirements describe WHAT the system must do, not HOW it will be implemented. Every requirement must be testable and traceable. When in doubt, ask clarifying questions!
