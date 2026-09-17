# GitHub Copilot Agents for Standards-Compliant Development

This directory contains specialized GitHub Copilot agents configured for each phase of the software development lifecycle, following IEEE/ISO/IEC standards and Extreme Programming (XP) practices.

## 📋 Available Agents

### Root Agent: Standards Compliance Advisor
**File**: [`AGENTS.md`](../AGENTS.md) (repository root)  
**Model**: `reasoning`  
**Purpose**: Strategic guide for navigating all 9 lifecycle phases

**When to use**:
- Starting new project or phase
- Need guidance on lifecycle phase transitions
- Questions about standards compliance
- Traceability validation
- Phase-specific best practices

**Key capabilities**:
- Navigate 9-phase lifecycle (Phase 01 → Phase 09)
- Enforce ISO/IEC/IEEE standards compliance
- Manage GitHub Issues traceability workflow
- Validate exit criteria before phase transitions
- Integrate XP practices (TDD, CI, Pair Programming)

---

### Phase 02: Requirements Analyst
**File**: [`requirements-analyst.md`](./requirements-analyst.md)  
**Model**: `reasoning`  
**Standards**: ISO/IEC/IEEE 29148:2018  
**Purpose**: Transform stakeholder needs into precise, testable requirements

**When to use**:
- Defining stakeholder requirements (Phase 01)
- Creating functional requirements (REQ-F)
- Creating non-functional requirements (REQ-NF)
- Writing user stories with acceptance criteria
- Validating requirements quality

**Key capabilities**:
- Create StR, REQ-F, REQ-NF GitHub Issues
- Write Given-When-Then acceptance criteria
- Define verification methods
- Maintain traceability (StR → REQ → TEST)
- Validate ISO 29148 compliance

**Example prompts**:
```
"Generate a REQ-F issue for user logout functionality, tracing to StR-001"
"Write a user story for password reset with Given-When-Then acceptance criteria"
"Validate this requirement for ISO 29148 compliance: [paste requirement]"
```

---

### Phase 03: Architecture Strategist
**File**: [`architecture-strategist.md`](./architecture-strategist.md)  
**Model**: `reasoning`  
**Standards**: ISO/IEC/IEEE 42010:2011  
**Purpose**: Design system architecture with quality attribute focus

**When to use**:
- Defining system architecture
- Creating Architecture Decision Records (ADRs)
- Designing component boundaries
- Evaluating quality scenarios (ATAM)
- Creating C4 diagrams

**Key capabilities**:
- Create ADR, ARC-C, QA-SC GitHub Issues
- Document architectural decisions with rationale
- Define component interfaces
- Generate C4 diagrams (Context, Container, Component)
- Apply architecture tactics (performance, security, scalability)
- Evaluate trade-offs

**Example prompts**:
```
"Generate an ADR issue for database selection, considering PostgreSQL vs MongoDB"
"Create an ARC-C issue for the authentication service with interfaces"
"Generate a quality scenario for availability testing under peak load"
"Generate a C4 context diagram showing authentication service dependencies"
```

---

### Phase 05: TDD Driver
**File**: [`tdd-driver.md`](./tdd-driver.md)  
**Model**: `reasoning`  
**XP Focus**: Test-Driven Development  
**Purpose**: Tactical code implementation following Red-Green-Refactor

**When to use**:
- Writing code (Phase 05)
- Implementing TDD cycle (Red → Green → Refactor)
- Creating unit/integration/e2e tests
- Refactoring code while keeping tests green
- Continuous integration workflows

**Key capabilities**:
- Write failing tests first (Red phase)
- Implement minimal code to pass (Green phase)
- Refactor with all tests green (Refactor phase)
- Generate test templates from requirements
- Maintain >80% code coverage
- Enforce coding standards

**Example prompts**:
```
"Generate a unit test for requirement #2 (User Login) following TDD"
"Write minimal code to make this failing test pass: [paste test]"
"Refactor this code to remove duplication while keeping tests green"
"Generate an integration test for user login with database"
```

---

## 🛠️ Specialized Support Agents

### Testing Specialist
**File**: [`testing-specialist.md`](./testing-specialist.md)  
**Model**: `reasoning`  
**Focus**: Test quality, coverage, and test generation  
**Boundary**: Write-only to `tests/` directory

**When to use**:
- Analyzing test coverage gaps
- Generating unit/integration/e2e tests
- Improving test quality
- Fixing flaky tests
- Creating test data builders

**Key capabilities**:
- Analyze coverage reports and identify untested code
- Generate tests with AAA pattern (Arrange-Act-Assert)
- Create parameterized tests for multiple scenarios
- Write test data builders and fixtures
- Recommend test improvements
- Target >80% coverage

**Example prompts**:
```
"Analyze coverage and generate tests for untested code paths in AuthService"
"Create parameterized tests for email validation with valid/invalid cases"
"Generate integration test for user login with database interaction"
```

---

### Documentation Expert
**File**: [`documentation-expert.md`](./documentation-expert.md)  
**Model**: `reasoning`  
**Focus**: Technical writing, API docs, user guides  
**Boundary**: Read-only `src/`, write-only `docs/`

**When to use**:
- Creating API documentation
- Writing user guides and tutorials
- Documenting architecture (ADRs, C4 diagrams)
- Generating code comments (JSDoc/TSDoc)
- Creating troubleshooting guides

**Key capabilities**:
- Generate API reference documentation
- Write getting-started guides with examples
- Create Architecture Decision Records (ADRs)
- Document code with JSDoc/TSDoc/docstrings
- Write troubleshooting guides
- Create OpenAPI/Swagger specifications

**Example prompts**:
```
"Generate API documentation for AuthService with all public methods"
"Write a getting-started guide for the authentication package"
"Create an ADR for JWT authentication with alternatives considered"
"Add JSDoc comments to all public methods with examples"
```

---

### Security Analyst
**File**: [`security-analyst.md`](./security-analyst.md)  
**Model**: `reasoning`  
**Focus**: Vulnerability detection, security risk analysis  
**Standards**: OWASP Top 10, CWE, CVE

**When to use**:
- Scanning code for vulnerabilities
- Performing threat modeling (STRIDE)
- Auditing dependencies for CVEs
- Reviewing secure coding practices
- Detecting hardcoded secrets

**Key capabilities**:
- Detect SQL injection, XSS, CSRF vulnerabilities
- Identify hardcoded secrets (API keys, passwords)
- Scan dependencies for known CVEs
- Perform threat modeling (STRIDE framework)
- Generate security findings with severity ratings
- Recommend secure alternatives and mitigations

**Example prompts**:
```
"Scan this code for SQL injection vulnerabilities"
"Identify hardcoded secrets in this repository"
"Perform threat modeling for the authentication service using STRIDE"
"Review this code for OWASP Top 10 vulnerabilities"
```

---

## 🎯 Agent Selection Guide

Use this decision tree to choose the right agent:

```
┌─ What are you working on?
│
├─ "Need lifecycle guidance" → Standards Compliance Advisor (root AGENTS.md)
│
├─ "Defining what to build"
│  └─ Phase 01-02 → Requirements Analyst
│
├─ "Designing system structure"
│  └─ Phase 03 → Architecture Strategist
│
├─ "Writing code"
│  └─ Phase 05 → TDD Driver
│
├─ "Need tests" → Testing Specialist
│
├─ "Need documentation" → Documentation Expert
│
├─ "Security concerns" → Security Analyst
│
├─ "Need help with all phases" → Standards Compliance Advisor
│
└─ "Unsure" → Start with Standards Compliance Advisor
```

## 🔄 Typical Workflow with Agents

### Starting a New Project

1. **Consult Standards Compliance Advisor** (root `AGENTS.md`)
   - "I want to start a new authentication feature. Guide me through the phases."
   - Agent will guide you to Phase 01 → 02 → 03 → 05

2. **Phase 01-02: Requirements Analyst**
   - "Create stakeholder requirement for user authentication"
   - "Generate REQ-F issues for login, logout, password reset"
   - "Write user story for login with acceptance criteria"

3. **Phase 03: Architecture Strategist**
   - "Create ADR for JWT authentication vs session-based"
   - "Design authentication service component with interfaces"
   - "Generate quality scenario for login performance under load"

4. **Phase 05: TDD Driver**
   - "Generate unit test for user login (requirement #2)"
   - "Implement minimal code to pass the test"
   - "Refactor to use bcrypt for password hashing"

5. **Back to Standards Compliance Advisor**
   - "Validate traceability: REQ-F-AUTH-001 → ADR-SECU-001 → Code → TEST"
   - "Check Phase 05 exit criteria before moving to Phase 06"

## 📊 Agent Comparison

### Lifecycle Phase Agents

| Agent | Phase | Standards | Key Deliverables | Model |
|-------|-------|-----------|------------------|-------|
| **Standards Compliance Advisor** | All (01-09) | ISO 12207, 29148, 42010, IEEE 1016, 1012 | Phase guidance, traceability validation | `reasoning` |
| **Requirements Analyst** | 01-02 | ISO/IEC/IEEE 29148:2018 | StR, REQ-F, REQ-NF issues, user stories | `reasoning` |
| **Architecture Strategist** | 03 | ISO/IEC/IEEE 42010:2011 | ADR, ARC-C, QA-SC issues, C4 diagrams | `reasoning` |
| **TDD Driver** | 05 | XP Practices (TDD, CI) | Unit tests, production code, refactoring | `reasoning` |

### Specialized Support Agents

| Agent | Focus Area | Boundary | Key Deliverables | Model |
|-------|------------|----------|------------------|-------|
| **Testing Specialist** | Test quality & coverage | Write-only `tests/` | Unit/integration/e2e tests, coverage reports | `reasoning` |
| **Documentation Expert** | Technical writing | Read `src/`, write `docs/` | API docs, user guides, ADRs | `reasoning` |
| **Security Analyst** | Vulnerability detection | Read-only code scan | Security findings, threat models | `reasoning` |

## 🛠️ Configuration

### Enabling Agents

Agents are automatically available when working in this repository. GitHub Copilot reads:
1. Root `AGENTS.md` (always loaded)
2. Phase-specific instructions in `.github/instructions/phase-NN-*.instructions.md`
3. Specialized agents in `.github/agents/*.md`

### Customizing Agents

To customize an agent:
1. Edit the agent file (e.g., `requirements-analyst.md`)
2. Modify YAML frontmatter (name, description, tools, model)
3. Update markdown content (roles, responsibilities, examples)
4. Save and test with Copilot

### Creating New Agents

Use the template from [`docs/improvement_ideas/agents_md_create_prompt_example.md`](../../docs/improvement_ideas/agents_md_create_prompt_example.md):

```markdown
---
name: YourAgentName
description: Brief description of agent's role and expertise
tools: ["read", "search", "edit", "githubRepo"]
model: reasoning
---

# Your Agent Name

You are an **Expert [Role]** specializing in [domain].

## Role and Core Responsibilities
...
```

## 📚 Additional Resources

### Referenced Documents
- [Lifecycle Guide](../../docs/lifecycle-guide.md) - Complete 9-phase workflow
- [XP Practices Guide](../../docs/xp-practices.md) - Extreme Programming techniques
- [Standards Compliance Checklists](../../standards-compliance/checklists/) - Quality gates
- [Copilot Usage Guide](../../docs/copilot-usage.md) - How to use Copilot effectively

### Agent Design References
- [agents_md_architecture.md](../../docs/improvement_ideas/agents_md_architecture.md) - Architecture phase agent design
- [agents_md_requirements.md](../../docs/improvement_ideas/agents_md_requirments.md) - Requirements phase agent design
- [agents_md_implementation_driver.md](../../docs/improvement_ideas/agents_md_implementation_driver.md) - TDD driver role
- [agents_md_implementation_navigator.md](../../docs/improvement_ideas/agents_md_implementation_navigator.md) - Strategic reviewer role

## 🎓 Best Practices

### When Working with Agents

1. **Be Specific**: Provide context (phase, requirement issue, architecture decision)
   - ❌ "Help me write code"
   - ✅ "Generate unit test for requirement #2 (User Login) following TDD"

2. **Reference Issues**: Always mention GitHub issue numbers
   - ❌ "Create a requirement for login"
   - ✅ "Create REQ-F issue for login, tracing to StR-001 (#1)"

3. **Follow the Lifecycle**: Don't skip phases
   - ❌ Start coding without requirements
   - ✅ Phase 01 → 02 → 03 → 05 (requirements → architecture → code)

4. **Validate Traceability**: Check links between issues
   - Use scripts: `python scripts/validate-traceability.py`
   - Use agents: "Validate traceability for requirement #2"

5. **Ask Clarifying Questions**: Agents are trained to ask when unclear
   - Agent: "Should this requirement support multi-tenancy?"
   - You: Provide clear answer

## ⚠️ Known Limitations

- **Model**: Currently using `reasoning` model (placeholder - will use production models)
- **Tools**: Some tools (`read`, `runCommands`) may not be supported in all environments
- **Context**: Agents work best with explicit issue references and phase context
- **Traceability**: Manual validation still required; agents assist but don't replace validation scripts

## 🔗 Integration with GitHub Copilot

These agents work seamlessly with:
- **GitHub Copilot Chat** (VS Code, Visual Studio, GitHub.com)
- **GitHub Copilot CLI** (terminal commands)
- **GitHub Copilot for Pull Requests** (PR descriptions, summaries)
- **GitHub Actions** (CI/CD workflows)

---

**Need help choosing an agent?** Start with the **Standards Compliance Advisor** (root `AGENTS.md`) and ask:
```
"I want to implement feature X. Which agent should I use and what are the steps?"
```

The advisor will guide you through the lifecycle and recommend the appropriate specialized agents! 🚀
