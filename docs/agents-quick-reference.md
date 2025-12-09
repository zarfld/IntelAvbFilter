# GitHub Copilot Agents - Quick Reference

## 🚀 Quick Start

### Which agent should I use?

**Lifecycle Phase Work:**
```
┌─ Defining requirements? → Requirements Analyst
├─ Designing architecture? → Architecture Strategist  
├─ Writing code? → TDD Driver
└─ Need guidance? → Standards Compliance Advisor
```

**Specialized Support Work:**
```
┌─ Need tests? → Testing Specialist
├─ Need documentation? → Documentation Expert
├─ Security concerns? → Security Analyst
└─ General guidance? → Standards Compliance Advisor
```

## 📋 Agents Overview

### Lifecycle Phase Agents

| Agent | Phase | Use When | Key Output |
|-------|-------|----------|------------|
| **Standards Compliance Advisor** | All (01-09) | Need lifecycle guidance, standards compliance | Phase guidance, traceability validation |
| **Requirements Analyst** | 01-02 | Defining what to build | StR, REQ-F, REQ-NF issues, user stories |
| **Architecture Strategist** | 03 | Designing system structure | ADR, ARC-C, QA-SC issues, C4 diagrams |
| **TDD Driver** | 05 | Writing code | Unit tests, production code, refactoring |

### Specialized Support Agents

| Agent | Focus | Use When | Key Output |
|-------|-------|----------|------------|
| **Testing Specialist** | Test quality & coverage | Need tests or coverage analysis | Unit/integration/e2e tests, coverage reports |
| **Documentation Expert** | Technical writing | Need API docs, guides, or ADRs | API documentation, user guides, tutorials |
| **Security Analyst** | Vulnerability detection | Security concerns or code review | Security findings, threat models, mitigations |

## 💬 Example Prompts

### Lifecycle Phase Agents

**Requirements Analyst:**
- "Generate a REQ-F issue for user logout, tracing to StR-001"
- "Write user story for password reset with Given-When-Then acceptance criteria"
- "Validate this requirement for ISO 29148 compliance: [paste requirement]"

**Architecture Strategist:**
- "Generate an ADR issue for database selection, considering PostgreSQL vs MongoDB"
- "Create an ARC-C issue for the authentication service with interfaces"
- "Generate a quality scenario for availability testing under peak load"
- "Create a C4 context diagram showing authentication service dependencies"

**TDD Driver:**
- "Generate a unit test for requirement #2 (User Login) following TDD"
- "Write minimal code to make this failing test pass: [paste test]"
- "Refactor this code to remove duplication while keeping tests green"
- "Generate an integration test for user login with database connection"

### Specialized Support Agents

**Testing Specialist:**
- "Analyze coverage and generate tests for untested code paths"
- "Review these tests and suggest improvements following AAA pattern"
- "Generate parameterized tests for this function with edge cases"
- "Create integration tests for the authentication service"

**Documentation Expert:**
- "Generate API documentation for AuthService with all public methods"
- "Create a getting-started guide for new developers"
- "Document this architecture decision with alternatives and rationale"
- "Write JSDoc comments for this class with usage examples"

**Security Analyst:**
- "Scan this code for SQL injection vulnerabilities"
- "Review authentication implementation for security issues"
- "Generate threat model for user authentication using STRIDE"
- "Check for hardcoded secrets and weak password hashing"

## 🔄 Typical Workflow

### 1. Start with Standards Compliance Advisor
```
"I want to implement user authentication. Guide me through the phases."
```

### 2. Create Requirements (Requirements Analyst)
```
"Create StR issue for user authentication"
"Generate REQ-F issues for login, logout, password reset"
```

### 3. Design Architecture (Architecture Strategist)
```
"Create ADR for JWT authentication vs session-based"
"Design authentication service component with interfaces"
```

### 4. Implement with TDD (TDD Driver)
```
"Generate unit test for user login (requirement #2)"
"Implement minimal code to pass the test"
"Refactor to use bcrypt for password hashing"
```

### 5. Validate Traceability (Standards Compliance Advisor)
```
"Validate traceability: REQ-F-AUTH-001 → ADR-SECU-001 → Code → TEST"
"Check Phase 05 exit criteria"
```

## 📊 Traceability Chain

```
StR Issue (#1) 
  ↓ Traces to
REQ-F Issue (#2)
  ↓ Satisfies  
ADR Issue (#5)
  ↓ Implements
ARC-C Issue (#6)
  ↓ Implemented by
Code (PR #10)
  ↓ Verified by
TEST Issue (#15)
```

## ⚡ Quick Tips

### ✅ Best Practices
- Always reference GitHub issue numbers (#N)
- Be specific about phase and context
- Follow the lifecycle (don't skip phases)
- Validate traceability regularly

### ❌ Common Mistakes
- Writing code without tests (use TDD Driver)
- Creating requirements without StR issue (use Requirements Analyst)
- Making architecture decisions without ADR (use Architecture Strategist)
- Skipping phases (use Standards Compliance Advisor for guidance)

## 📚 Full Documentation

- **Detailed Guide**: `.github/agents/README.md`
- **Implementation Summary**: `docs/improvement_ideas/agents-implementation-summary.md`
- **Agent Files**: `.github/agents/*.md`
- **Root Agent**: `AGENTS.md` (repository root)

## 🎯 Getting Help

Not sure which agent to use? Start with:

```
Standards Compliance Advisor (root AGENTS.md):
"I want to implement feature X. Which agent should I use and what are the steps?"
```

The advisor will guide you to the right specialized agent! 🚀
