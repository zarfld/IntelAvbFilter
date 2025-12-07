---
description: "Phase 01 guidance for stakeholder requirements definition following ISO/IEC/IEEE 29148:2018. Covers stakeholder identification, requirements elicitation, and business context documentation."
applyTo: "01-stakeholder-requirements/**"
---

# Phase 01: Stakeholder Requirements Definition

**Standards**: ISO/IEC/IEEE 29148:2018 (Stakeholder Requirements), ISO/IEC/IEEE 12207:2017  
**XP Integration**: Planning Game, Customer Involvement, User Stories Foundation

## 🎯 Phase Objectives

1. Identify and document all stakeholders and their concerns
2. Elicit stakeholder needs and expectations
3. Define business context and constraints
4. Establish success criteria and acceptance measures
5. Create foundation for system requirements

## 📋 ISO/IEC/IEEE 29148:2018 Compliance

### Stakeholder Requirements Process Activities

1. **Stakeholder Identification**
   - Define all stakeholder classes
   - Identify representatives from each class
   - Document stakeholder roles and responsibilities
   - Map stakeholder concerns and interests

2. **Requirements Elicitation**
   - Conduct stakeholder interviews
   - Facilitate workshops and brainstorming sessions
   - Review existing documentation
   - Analyze competitor solutions
   - Document user pain points and needs

3. **Requirements Analysis**
   - Identify conflicting requirements
   - Prioritize stakeholder needs
   - Analyze feasibility and constraints
   - Define acceptance criteria

4. **Requirements Documentation**
   - Create Stakeholder Requirements Specification (StRS)
   - Document business context
   - Define scope and boundaries
   - Document assumptions and constraints

## 🎨 XP Practices for This Phase

### Planning Game
- **Exploration Phase**: Understand business value
- **Commitment Phase**: Establish iteration scope
- **Steering Phase**: Track progress and adjust

### Customer Involvement
- Embed customer representative in team
- Daily customer feedback
- Customer-defined acceptance tests
- Customer prioritization of features

### User Story Foundation
Start capturing requirements as user stories:
```markdown
As a [stakeholder role]
I want [goal/desire]
So that [benefit/value]
```

## 📝 Requirements Documentation Approach

### ⭐ PRIMARY: GitHub Issues (Recommended)

**Stakeholder requirements should be captured as GitHub Issues** using the Stakeholder Requirement template.

#### Creating Stakeholder Requirements as GitHub Issues

1. **Navigate to Issues → New Issue**
2. **Select Template**: "Stakeholder Requirement (StR)"
3. **Complete Required Fields**:
   - **Title**: Clear, concise requirement statement (e.g., "Support multi-language content")
   - **Stakeholder Source**: Which stakeholder class requested this
   - **Business Justification**: Why this requirement exists (business value, ROI, competitive advantage)
   - **Success Criteria**: How we'll know this requirement is satisfied (measurable)
   - **Priority**: Critical (P0) / High (P1) / Medium (P2) / Low (P3)
   - **Constraints**: Budget, timeline, technical, regulatory limitations
   - **Assumptions**: Dependencies that must hold true
   
4. **Apply Labels**:
   - `stakeholder-requirement` (auto-applied by template)
   - `phase-01` (lifecycle phase)
   - `priority-critical` / `priority-high` / `priority-medium` / `priority-low`
   - `integrity-1` through `integrity-4` (criticality per IEEE 1012-2016)
   
5. **Submit** → GitHub auto-assigns unique issue number (e.g., #1, #2)

#### Traceability via GitHub Issues

Stakeholder requirements have **no parent** (they are root requirements):
```markdown
## Traceability
- **Traces to**: N/A (root stakeholder requirement)
- **Refined by**: #45, #46, #47 (system requirements in Phase 02)
- **Implemented by**: #PR-12 (pull request)
- **Verified by**: #89 (test case)
```

Child requirements link back using `#N` syntax:
```markdown
## Traceability
- **Traces to**: #1 (parent stakeholder requirement)
```

#### Example: Creating StR Issue via UI

**Title**: Multi-Language Support for Customer Portal

**Stakeholder Source**:
```markdown
**Stakeholder Class**: International Customers (Europe, Asia)
**Representatives**: 
- Maria Schmidt (EU Customer Success Manager)
- Yuki Tanaka (APAC Sales Director)
```

**Business Justification**:
```markdown
**Problem**: 65% of international customers report difficulty using English-only interface
**Impact**: Projected 30% increase in international sales with localized content
**Competitive Gap**: Main competitors offer 12+ languages
**ROI**: Estimated $2M additional revenue in Year 1
```

**Success Criteria**:
```markdown
1. Customer can select preferred language from 10+ options
2. All UI text, help documentation, and error messages localized
3. Language preference persists across sessions
4. Translation accuracy verified by native speakers
5. Page load time increase < 200ms with localization
```

**Constraints**:
```markdown
- Must support right-to-left languages (Arabic, Hebrew)
- Translation budget: $50K for initial 10 languages
- Must comply with GDPR for language preference storage
- Launch target: Q2 2024 for initial 5 languages
```

**Assumptions**:
```markdown
- Content management system supports localization
- Existing translation vendor can scale to support additional languages
- Customer database schema can store language preferences
```

After submission → Issue #1 created → Children link to it with `#1`

#### Querying Stakeholder Requirements

Use GitHub's search and filters:
```
is:issue label:stakeholder-requirement label:phase-01 is:open
```

Or via GitHub MCP:
```
List all stakeholder requirements (label: stakeholder-requirement)
```

### 📝 Supplementary Documentation (Optional)

While **GitHub Issues are the single source of truth** for stakeholder requirements, you may create supplementary files in `01-stakeholder-requirements/` for:
- Stakeholder register summary (must reference issues)
- Business context diagrams and models
- Background research and analysis

**Critical Rule**: All supplementary files MUST reference the canonical GitHub Issue(s) using `#N` syntax.

**Example Reference in File**:
```markdown
# Stakeholder Register

See GitHub Issues for authoritative stakeholder requirements:
- #1 (StR: Multi-language support)
- #2 (StR: Mobile-first experience)
- #3 (StR: GDPR compliance)
```
- Market analysis
- Competitive landscape
- Business constraints
- Regulatory requirements
- Standards compliance needs

### 3. Stakeholder Requirements Specification (StRS)

#### With GitHub Issues (Recommended)

The **collection of all stakeholder requirement issues** forms your Stakeholder Requirements Specification. Generate the specification:

```bash
# Generate traceability report from issues
python scripts/github-traceability-report.py --type stakeholder
```

This produces a markdown report structured per ISO/IEC/IEEE 29148:
- Introduction (from repository README)
- All StR issues grouped by category (labels)
- Traceability matrix showing parent-child relationships
- Acceptance criteria summary
- Status dashboard

### 4. Initial User Stories

**All user stories MUST be captured as GitHub Issues**.

User stories can be captured as **Stakeholder Requirement issues** with the user story format in the title:

**Title Format**: `As a [role] I want [goal] so that [benefit]`

**Example**:
- **Title**: As an international customer I want to view content in my native language so that I can understand product features
- **Issue Body**: Complete with business justification, success criteria, constraints
- **Labels**: `stakeholder-requirement`, `phase-01`, `priority-high`
- **Project**: Add to "Requirements Traceability System" project

Link epic stories by creating parent-child relationships:
```markdown
## Traceability
- **Epic**: #5 (Multi-Language Support Epic)
- **Refined by**: #23, #24, #25 (child stories for specific languages)
```

## Acceptance Criteria
Given [context]
When [action]
Then [expected result]

## Constraints
- [Technical constraints]
- [Business constraints]

## Priority: [Critical/High/Medium/Low]
## Estimated Value Points: [1-100]
```

## 🚨 Critical Requirements for This Phase

### Always Do
✅ Interview all stakeholder classes  
✅ Document conflicting requirements  
✅ Prioritize with customer involvement (Planning Game)  
✅ Define measurable acceptance criteria  
✅ Validate requirements with stakeholders  
✅ Document assumptions explicitly and prove them  
✅ Identify constraints early (budget, technical, regulatory, business rules)  
✅ Create traceable requirement IDs (StR-XXX)  
✅ Use "shall" for mandatory binding provisions  
✅ Use active voice in requirements (e.g., "The actor does X")  
✅ Ensure requirements are necessary, appropriate, unambiguous, complete, singular, feasible, verifiable, and correct  
✅ Trace requirements to one or more stakeholders and their needs  
✅ Use objective acceptance criteria to make requirements testable  
✅ Focus on open, honest communication (conversation is preferred)  
✅ Document rationale (why) for each requirement  
✅ Use ubiquitous language (domain vocabulary) consistently  

### Never Do
❌ Proceed without stakeholder validation  
❌ Mix stakeholder requirements with system requirements  
❌ Skip non-functional requirements  
❌ Ignore conflicting requirements  
❌ Assume implicit requirements  
❌ Define technical solutions (that comes later)  
❌ Skip business context documentation  
❌ Use vague or general terms (superlatives like "best", subjective language like "user-friendly", ambiguous terms like "and/or")  
❌ Use loopholes (such as "if possible", "as appropriate", "as applicable")  
❌ Use the term "must" (use "shall" instead for mandatory requirements)  
❌ Write negative requirements (e.g., "shall not")  
❌ Build for tomorrow / gold plating (implement only what is necessary for current needs)  
❌ Write requirements valued only by developers (e.g., dictating specific infrastructure technology)  
❌ Try to freeze requirements before starting implementation (requirements evolve)  
❌ Run on autopilot; constantly think critically about every requirement

## 🔍 Clarifying Questions to Ask

When gathering stakeholder requirements, always ask:

### About the Business
1. What business problem are we solving?
2. What are the measurable business goals?
3. What is the expected ROI or business value?
4. What happens if we don't build this?
5. Who are the competitors, and how do they solve this?

### About Users/Stakeholders
1. Who are all the stakeholders (not just users)?
2. What are each stakeholder's goals and pain points?
3. What are the most critical needs vs. nice-to-haves?
4. Are there conflicting stakeholder needs?
5. Who makes final priority decisions?

### About Constraints
1. What is the budget and timeline?
2. Are there regulatory or compliance requirements?
3. What are the technical platform constraints?
4. Are there integration requirements with existing systems?
5. What are the security and privacy requirements?

### About Success
1. How will we measure success?
2. What are the acceptance criteria for the project?
3. What would make stakeholders consider this a failure?
4. What are the key performance indicators (KPIs)?

### Example Clarification Request
```markdown
## Clarification Needed: [Topic]

**Context**: You mentioned that "[stakeholder statement]"

**Questions**:
1. Can you elaborate on the specific goals or outcomes you expect?
2. Are there any constraints or limitations we should know about?
3. How will we know when this requirement is satisfied?
4. What is the priority of this requirement compared to others?
5. Are there regulatory or compliance aspects to consider?

**Why This Matters**: This information will help us:
- Define clear, testable requirements
- Prioritize development work
- Ensure compliance with standards
- Avoid rework later in the lifecycle
```

## 📊 Phase Entry Criteria

✅ Project charter approved  
✅ Initial stakeholders identified  
✅ Resources allocated  
✅ Business opportunity defined

## 📊 Phase Exit Criteria

✅ All stakeholder classes identified and documented  
✅ Stakeholder Requirements Specification (StRS) completed  
✅ Business context documented  
✅ Requirements reviewed and approved by stakeholders  
✅ Conflicts resolved or documented  
✅ Priorities established  
✅ Acceptance criteria defined  
✅ Traceability IDs assigned (StR-XXX format)  
✅ Baseline established for requirements

## 🔗 Traceability

### With GitHub Issues

Establish forward traceability via issue links:
```
Issue #1 (Stakeholder Requirement - StR)
  ↓ Refined by
Issue #45, #46, #47 (System Requirements - REQ-F/REQ-NF in Phase 02)
  ↓ Implemented by
Pull Request #12
  ↓ Verified by
Issue #89 (Test Case - TEST in Phase 07)
```

**In Issue Bodies**:
```markdown
## Traceability
- **Traces to**: N/A (root requirement)
- **Refined by**: #45, #46, #47
- **Implemented by**: #PR-12
- **Verified by**: #89
```

**Automated Validation**: GitHub Actions workflow validates that:
- All non-StR requirements have parent links
- Parent issues exist and are open or closed
- No circular dependencies
- No orphaned requirements

**Generate Reports**:
```bash
# Traceability matrix
python scripts/github-traceability-report.py

# Find orphaned requirements
python scripts/github-orphan-check.py
```

## 📚 Standards References

- **ISO/IEC/IEEE 29148:2018** - Section 5.2 (Stakeholder Requirements Process)
- **ISO/IEC/IEEE 12207:2017** - Section 6.4.1 (Stakeholder Requirements Definition)
- **XP Practices** - Planning Game, Customer Involvement

## 🎯 Next Phase

Once this phase is complete, proceed to:
**Phase 02: Requirements Analysis & Specification** (`02-requirements/`)

---

**Remember**: Stakeholder requirements describe WHAT stakeholders need and WHY. They should be solution-independent. Technical HOW comes in later phases.
