# AI Agent Guardrails for Working Principles

## Context
The working principles you've defined are **anti-patterns for AI agent work** and should be strategically embedded into specific prompts to prevent common AI pitfalls.

## Principle Analysis & Application

### 1. **"No stubs/simulations in PRODUCTIVE code"**
**Intent**: Prevent AI from suggesting placeholder/stub code in production environments
**Clarification**: Testing stubs/mocks are legitimate and necessary

#### **Target Prompts**:
- `compile.prompt.md` - Ensure compilation checks reject stubs in production paths
- `tdd-compile.prompt.md` - Distinguish between test doubles (OK) and production stubs (NOT OK)
- `phase-gate-check.prompt.md` - Gate validation must catch production stubs
- `test-validate.prompt.md` - Validate test doubles are properly isolated

#### **Suggested Guardrail Text**:
```markdown
## 🚨 AI Agent Guardrails
**CRITICAL**: Prevent production stub contamination:
- ❌ **No stubs/simulations in PRODUCTIVE code**: Test doubles belong in test code only
- ✅ **Test mocks are acceptable**: Use dependency injection for testability
- ❌ **No "TODO" or placeholder implementations**: Complete implementations only
- ✅ **Clear test/production boundaries**: Maintain strict separation
```

### 2. **"Prefer incremental modification over reimplementation"**
**Intent**: Prevent AI from suggesting complete rewrites during maintenance
**Context**: Applies during code maintenance, NOT when discrepancies/bugs detected

#### **Target Prompts**:
- `code-to-requirements.prompt.md` - During requirements recovery from legacy code
- `repository-audit.prompt.md` - When analyzing existing code for improvements  
- `test-gap-filler.prompt.md` - Adding tests to existing systems
- `traceability-builder.prompt.md` - Building traceability for existing code

#### **Suggested Guardrail Text**:
```markdown
## 🚨 AI Agent Guardrails
**CRITICAL**: Prevent unnecessary reimplementation:
- ❌ **Avoid reimplementation during maintenance**: Extend existing patterns when possible
- ✅ **Reimplementation legitimate when**: Discrepancies/bugs detected after requirements validation
- ❌ **No "rewrite from scratch" suggestions**: Unless architecture fundamentally broken
- ✅ **Follow proper validation path**: Requirements → Architecture → Implementation decision
```

### 3. **"No implementation-based assumptions"**
**Intent**: Force AI to ask for specifications rather than guessing from code
**Critical**: Prevents codifying bugs from legacy assumptions

#### **Target Prompts**:
- `requirements-elicit.prompt.md` - Never guess stakeholder needs
- `requirements-complete.prompt.md` - Validate all assumptions against specs
- `architecture-starter.prompt.md` - Prevent architectural assumptions
- `code-to-requirements.prompt.md` - Don't assume existing code is correct

#### **Suggested Guardrail Text**:
```markdown
## 🚨 AI Agent Guardrails  
**CRITICAL**: Prevent assumption-based development:
- ❌ **No implementation-based assumptions**: Always reference specifications
- ❌ **No guessing stakeholder intent**: Ask for clarification when unclear
- ❌ **Don't assume legacy code is correct**: Validate against business requirements
- ✅ **Always trace to specification**: Every decision must have documented rationale
```

### 4. **"Understand architecture and patterns before coding"**
**Intent**: Prevent AI from diving into implementation without architectural understanding

#### **Target Prompts**:
- `architecture-starter.prompt.md` - Foundation principle
- `compile.prompt.md` - Validate architectural conformance
- `tdd-compile.prompt.md` - Ensure tests align with architecture
- `phase-gate-check.prompt.md` - Gate architectural understanding

#### **Suggested Guardrail Text**:
```markdown
## 🚨 AI Agent Guardrails
**CRITICAL**: Architecture-first development:
- ❌ **No coding without architectural understanding**: Analyze patterns first
- ❌ **No quick implementation fixes**: Consider architectural impact
- ✅ **Reference existing patterns**: Understand before extending
- ✅ **Document architectural decisions**: Update ADRs for deviations
```

## Implementation Strategy

### Phase 1: Critical Safety Guardrails
Add guardrails to prompts that directly impact production code quality:
1. `compile.prompt.md` - Production stub prevention
2. `code-to-requirements.prompt.md` - Legacy assumption validation
3. `requirements-elicit.prompt.md` - Specification-driven development

### Phase 2: Maintenance & Evolution Guardrails  
Add guardrails to prompts dealing with existing code:
1. `repository-audit.prompt.md` - Incremental vs. reimplementation decisions
2. `test-gap-filler.prompt.md` - Safe test addition patterns
3. `traceability-builder.prompt.md` - Existing code analysis

### Phase 3: Architecture & Design Guardrails
Add guardrails to design-phase prompts:
1. `architecture-starter.prompt.md` - Architecture-first principles
2. `phase-gate-check.prompt.md` - Comprehensive validation gates

## Template Guardrail Section

For any prompt file, add this section near the top:

```markdown
## 🚨 AI Agent Guardrails
**CRITICAL**: Prevent common AI pitfalls in [PROMPT_CONTEXT]:
- ❌ **[SPECIFIC_ANTI_PATTERN]**: [Explanation]
- ✅ **[CORRECT_APPROACH]**: [Explanation]  
- ❌ **[ANOTHER_ANTI_PATTERN]**: [Explanation]
- ✅ **[PREFERRED_METHOD]**: [Explanation]

**Validation Questions**:
1. Have I validated against specifications rather than assumptions?
2. Am I preserving architectural integrity?
3. Am I distinguishing between test and production code appropriately?
```

## Benefits

1. **Prevents AI Hallucination**: Forces specification validation
2. **Maintains Code Quality**: Prevents production contamination  
3. **Preserves Architecture**: Ensures understanding before modification
4. **Reduces Technical Debt**: Prevents quick-fix mentality
5. **Improves Traceability**: Forces documentation of decisions

## Next Steps

1. Review each prompt file and identify applicable guardrails
2. Add guardrail sections to high-risk prompts first
3. Test guardrails with real AI agent interactions
4. Refine based on observed AI behavior patterns
5. Document guardrail effectiveness and iterate

---

**Key Insight**: Your working principles are essentially **AI behavioral contracts** that should be embedded contextually into prompts to prevent the most common failure modes of AI-assisted development.
