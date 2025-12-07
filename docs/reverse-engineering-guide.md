# Reverse Engineering Guide - Understanding Existing Software

**Phase**: 01-02 (Requirements Recovery), 03-04 (Architecture/Design Recovery), 07 (Validation)  
**Standards**: IEEE 1219-1998 (Software Maintenance), ISO/IEC/IEEE 12207:2017 (Reverse Engineering Process)  
**Purpose**: Produce logical/physical models from executable code to understand intent when documentation is incomplete  
**XP Integration**: TDD for validation, Refactoring for simplification, Knowledge Crunching for domain understanding

## 🎯 Overview

**Reverse Engineering Definition**: The production of a logical or physical model from executable code or the recovery of a design description from an existing implementation.

**Goal**: Generate **objective conclusions** about the code's:
- ✅ **Correctness** - Does it work as intended?
- ✅ **Completeness** - Are all features implemented?
- ✅ **Accuracy** - Does it meet specifications?
- ✅ **Usability** - Can it be maintained and extended?

**Core Principle**: Treat existing code as a **source of knowledge**, not speculation. Prove understanding through tests and validation.

## 📋 When to Apply Reverse Engineering

### Common Scenarios

| Scenario | Reverse Engineering Goal |
|----------|--------------------------|
| **Legacy System Migration** | Understand architecture to plan migration path |
| **Missing Documentation** | Recover requirements and design specifications |
| **Vendor Lock-In** | Understand proprietary system for replacement |
| **Code Archaeology** | Understand historical decisions and constraints |
| **Security Audit** | Identify vulnerabilities and data flows |
| **Performance Optimization** | Understand bottlenecks and inefficiencies |
| **Compliance Verification** | Verify adherence to standards and regulations |
| **Knowledge Transfer** | Document system for new team members |

## I. Establish Context and Gather Available Evidence

### 1. Analyze Existing Documentation and Artifacts

**Objective**: Extract all available knowledge before diving into code.

**Process**:
```markdown
## Documentation Inventory Checklist

### Product Documentation
- [ ] User manuals and guides
- [ ] API documentation
- [ ] Installation guides
- [ ] Release notes and changelogs
- [ ] Architecture diagrams (if available)
- [ ] Design documents (if available)

### Development Artifacts
- [ ] Source code comments
- [ ] Commit history and messages
- [ ] Issue tracker history (bugs, features)
- [ ] Code review comments
- [ ] Test cases (unit, integration, e2e)
- [ ] Build scripts and deployment configs

### Operational Documentation
- [ ] Monitoring and logging data
- [ ] Incident reports
- [ ] Performance benchmarks
- [ ] User feedback and support tickets
- [ ] Known issues and workarounds
```

**GitHub Issue Template** (`type:reverse-engineering:artifact-analysis`):
```markdown
## Artifact Analysis: [Component Name]

**Component**: [Name/Path]
**Purpose**: Analyze existing documentation to derive requirements

### Available Documentation
- User Manual: [Link/Location]
- API Docs: [Link/Location]
- Test Cases: [Link/Location]
- Issue History: [Link/Location]

### Derived Requirements (Provisional)
1. [Requirement 1 inferred from user manual]
2. [Requirement 2 inferred from test cases]
3. [Requirement 3 inferred from issue history]

### Gaps and Uncertainties
- ❓ [Unknown behavior in scenario X]
- ❓ [Undocumented configuration option Y]

### Next Steps
- [ ] Validate requirements with operational users
- [ ] Test code against derived requirements
- [ ] Interview original developers (if available)

**Traceability**: To be linked to recovered requirements
```

### 2. Determine Formal Intent (Traceability)

**Objective**: Trace code components to specifications (if they exist).

**Implementation**:
```typescript
/**
 * Traceability Analysis Tool
 * 
 * Maps code components to design specifications
 */
class TraceabilityAnalyzer {
  analyzeComponent(componentPath: string): TraceabilityReport {
    const code = readFile(componentPath);
    
    // Extract traceability hints from comments
    const reqRefs = this.extractRequirementReferences(code);
    const designRefs = this.extractDesignReferences(code);
    const testRefs = this.extractTestReferences(code);
    
    return {
      component: componentPath,
      requirements: reqRefs,
      design: designRefs,
      tests: testRefs,
      coverage: this.calculateCoverage(reqRefs, designRefs, testRefs),
      gaps: this.identifyGaps(reqRefs, designRefs, testRefs)
    };
  }
  
  private extractRequirementReferences(code: string): string[] {
    // Search for patterns like:
    // @implements REQ-F-001
    // Implements: #123
    // See: requirement-spec.md#section-4.2
    const patterns = [
      /@implements\s+(REQ-[\w-]+)/g,
      /Implements:\s+#(\d+)/g,
      /See:\s+([\w.-]+#[\w.-]+)/g
    ];
    
    const refs: string[] = [];
    patterns.forEach(pattern => {
      const matches = code.matchAll(pattern);
      for (const match of matches) {
        refs.push(match[1]);
      }
    });
    
    return refs;
  }
}
```

**Analysis Checklist**:
```markdown
## Traceability Analysis: [Component]

### Requirements Coverage
- [ ] All code traced to requirements
- [ ] All requirements traced to code
- [ ] No orphaned components

### Consistency Checks
- [ ] Code behavior matches specification
- [ ] Interface definitions match documentation
- [ ] Error handling matches specification

### Completeness Checks
- [ ] All specified features implemented
- [ ] All edge cases handled
- [ ] All error conditions handled

### Gaps Identified
- ⚠️ [Component X has no requirement traceability]
- ⚠️ [Requirement Y has no implementation]
- ⚠️ [Inconsistency between code and spec in Z]
```

### 3. Gather Operational History

**Objective**: Understand how the system performs in production.

**Interview Template** (Operational Users):
```markdown
## Operational User Interview: [System Name]

**Interviewee**: [Name, Role]
**Date**: [Date]
**Interviewer**: [Name]

### Current Usage Patterns
1. **Primary Use Cases**:
   - [What do users do with the system most often?]
   - [What workflows are critical?]

2. **Performance Characteristics**:
   - [How long do operations typically take?]
   - [What are the acceptable response times?]

3. **Known Issues and Workarounds**:
   - [What doesn't work as expected?]
   - [What manual steps are needed?]

4. **Context and Constraints**:
   - [What environments is this used in?]
   - [What are the peak load scenarios?]

5. **Expected Behavior**:
   - [In scenario X, what should happen?]
   - [How should the system respond to error Y?]

### Extracted Requirements (Provisional)
- REQ-OPS-001: [Requirement derived from operational use]
- REQ-OPS-002: [Performance requirement from user feedback]

### Follow-up Actions
- [ ] Test scenarios described by users
- [ ] Measure actual vs. expected performance
- [ ] Document workarounds as potential bugs
```

**Operational Data Analysis**:
```python
"""
Analyze operational logs to understand actual usage patterns
"""
import pandas as pd
import matplotlib.pyplot as plt

def analyze_operational_logs(log_file: str) -> dict:
    """
    Extract usage patterns from operational logs
    
    Returns:
    - Most frequent operations
    - Performance characteristics (p50, p95, p99)
    - Error rates and patterns
    - Usage trends over time
    """
    logs = pd.read_csv(log_file)
    
    # Most frequent operations
    operation_counts = logs['operation'].value_counts()
    
    # Performance analysis
    performance = logs.groupby('operation')['duration_ms'].describe(
        percentiles=[0.5, 0.95, 0.99]
    )
    
    # Error analysis
    error_rate = logs.groupby('operation')['success'].apply(
        lambda x: (x == False).sum() / len(x)
    )
    
    return {
        'top_operations': operation_counts.head(10),
        'performance': performance,
        'error_rates': error_rate,
        'recommendations': generate_recommendations(performance, error_rate)
    }

def generate_recommendations(performance: pd.DataFrame, error_rate: pd.Series) -> list:
    """Generate recommendations based on operational data"""
    recommendations = []
    
    # Identify slow operations (p95 > 1 second)
    slow_ops = performance[performance['95%'] > 1000].index
    for op in slow_ops:
        recommendations.append(f"REQ-PERF: {op} should complete in <1s (95th percentile)")
    
    # Identify error-prone operations (error rate > 5%)
    error_prone = error_rate[error_rate > 0.05].index
    for op in error_prone:
        recommendations.append(f"REQ-REL: {op} reliability should be >95%")
    
    return recommendations
```

### 4. Interview Developers and Analysts

**Objective**: Extract knowledge from original development team.

**Developer Interview Template**:
```markdown
## Developer Interview: [Component/Module]

**Interviewee**: [Developer Name]
**Component**: [Component Name]
**Date**: [Date]

### Design Intent
1. **Why was this component built?**
   - [What problem does it solve?]
   - [What were the original requirements?]

2. **What were the key design decisions?**
   - [Why this architecture vs. alternatives?]
   - [What trade-offs were made?]

3. **What are the key abstractions?**
   - [What are the main entities/concepts?]
   - [How do they relate to the domain?]

### Performance Characteristics
1. **What are the expected performance characteristics?**
   - [Typical response times?]
   - [Resource usage (CPU, memory)?]
   - [Scalability limits?]

2. **What are the known bottlenecks?**
   - [Where is optimization needed?]
   - [What were the performance targets?]

### Edge Cases and Constraints
1. **What edge cases should we know about?**
   - [What unusual scenarios exist?]
   - [What error conditions are handled?]

2. **What constraints exist?**
   - [Technical constraints (hardware, platform)?]
   - [Business constraints (compliance, regulations)?]

### Historical Context
1. **What has changed since initial development?**
   - [What features were added later?]
   - [What bugs were fixed?]

2. **What would you do differently?**
   - [What design decisions would you revisit?]
   - [What technical debt exists?]

### Extracted Insights
- ADR-XXX: [Architecture decision rationale]
- REQ-XXX: [Recovered requirement]
- TECH-DEBT-XXX: [Technical debt item]
```

### 5. Reverse Engineer Requirements from User Documentation

**Objective**: Derive system requirements from user manuals.

**Process**:
```markdown
## Requirements Extraction from User Manual

### User Manual: [Title/Version]

#### Section: [Section Name]
**User Instruction**: 
> "To process a transaction, click 'Submit' and wait for confirmation."

**Derived Requirements**:
- REQ-F-TXN-001: System shall provide a 'Submit' button for transaction processing
- REQ-F-TXN-002: System shall display confirmation after transaction processing
- REQ-NF-PERF-001: Transaction processing shall complete in reasonable time (derive specific value from operational data)

#### Section: [Another Section]
**User Instruction**:
> "If an error occurs, retry up to 3 times before contacting support."

**Derived Requirements**:
- REQ-F-ERR-001: System shall automatically retry failed operations up to 3 times
- REQ-F-ERR-002: System shall provide clear error messages after 3 failures
- REQ-F-ERR-003: System shall log errors for support analysis

### Test Plan
- [ ] Test that 'Submit' button exists and is enabled
- [ ] Test that confirmation is displayed after submission
- [ ] Test automatic retry behavior (1st, 2nd, 3rd attempt)
- [ ] Test error message after 3 failures
```

## II. Deep Dive into Code Structure and Logic

### 6. Analyze Structural Dependencies

**Objective**: Understand syntactic dependencies among files for architecture reconstruction.

**Tool Implementation**:
```typescript
/**
 * Dependency Analyzer
 * 
 * Maps imports, exports, and references to build dependency graph
 */
class DependencyAnalyzer {
  analyzeProject(projectRoot: string): DependencyGraph {
    const files = this.findSourceFiles(projectRoot);
    const graph = new DependencyGraph();
    
    files.forEach(file => {
      const imports = this.extractImports(file);
      const exports = this.extractExports(file);
      
      graph.addNode(file, { imports, exports });
      
      imports.forEach(imported => {
        graph.addEdge(file, imported);
      });
    });
    
    return graph;
  }
  
  identifyArchitecturalLayers(graph: DependencyGraph): LayerAnalysis {
    // Detect layered architecture patterns
    const layers = this.detectLayers(graph);
    
    // Identify violations (e.g., presentation layer importing data layer)
    const violations = this.detectLayerViolations(layers);
    
    // Identify circular dependencies
    const cycles = this.detectCycles(graph);
    
    return {
      layers,
      violations,
      cycles,
      modularity: this.calculateModularity(graph)
    };
  }
  
  private detectLayers(graph: DependencyGraph): Layer[] {
    // Use heuristics to identify layers:
    // - Presentation: depends on business logic, no dependents
    // - Business Logic: depends on data access
    // - Data Access: depends on external APIs/DBs
    
    const layers: Layer[] = [];
    
    // Find "leaf" nodes (presentation layer)
    const presentationNodes = graph.nodes.filter(n => 
      graph.getDependents(n).length === 0 &&
      this.hasUIPatterns(n)
    );
    layers.push(new Layer('Presentation', presentationNodes));
    
    // Find "root" nodes (data access layer)
    const dataAccessNodes = graph.nodes.filter(n =>
      graph.getDependencies(n).length === 0 &&
      this.hasDataAccessPatterns(n)
    );
    layers.push(new Layer('Data Access', dataAccessNodes));
    
    // Everything else is business logic
    const businessLogicNodes = graph.nodes.filter(n =>
      !presentationNodes.includes(n) && !dataAccessNodes.includes(n)
    );
    layers.push(new Layer('Business Logic', businessLogicNodes));
    
    return layers;
  }
}
```

**Architecture Recovery Report**:
```markdown
## Architecture Recovery Report: [System Name]

### Identified Layers
1. **Presentation Layer** (23 files)
   - `src/ui/components/`
   - `src/controllers/`
   - Dependencies: Business Logic layer

2. **Business Logic Layer** (45 files)
   - `src/services/`
   - `src/domain/`
   - Dependencies: Data Access layer

3. **Data Access Layer** (12 files)
   - `src/repositories/`
   - `src/database/`
   - Dependencies: External (PostgreSQL, Redis)

### Architectural Patterns Detected
- ✅ **Layered Architecture** (3-tier)
- ✅ **Repository Pattern** (data access abstraction)
- ⚠️ **Some violations**: Presentation accessing Data Access directly

### Violations Identified
- ⚠️ `src/ui/UserProfile.tsx` imports `src/database/UserRepository.ts` (layer violation)
- ⚠️ Circular dependency: `OrderService` ↔ `CustomerService`

### Modularity Metrics
- **Cohesion Score**: 7.2/10 (good)
- **Coupling Score**: 4.5/10 (moderate coupling)
- **Cyclomatic Complexity**: Average 8.3 (acceptable)

### Recommendations
- [ ] Refactor layer violations (use service layer)
- [ ] Break circular dependencies
- [ ] Document architectural decisions (ADRs)
```

### 7. Examine Code for Correctness and Compliance

**Objective**: Evaluate code for quality attributes and compliance.

**Code Quality Checklist**:
```markdown
## Code Quality Analysis: [Component]

### Correctness
- [ ] Logic matches expected behavior
- [ ] Error handling covers all cases
- [ ] Boundary conditions handled
- [ ] Null/undefined checks present

### Consistency
- [ ] Naming conventions followed
- [ ] Coding standards applied
- [ ] Design patterns used consistently
- [ ] Error handling approach uniform

### Completeness
- [ ] All features implemented
- [ ] All edge cases handled
- [ ] All error conditions handled
- [ ] Logging and monitoring present

### Accuracy
- [ ] Calculations verified
- [ ] Data transformations correct
- [ ] Rounding/truncation handled properly
- [ ] Time zone handling correct

### Readability
- [ ] Clear variable names
- [ ] Functions under 50 lines
- [ ] Comments explain "why", not "what"
- [ ] Complex logic documented

### Testability
- [ ] Functions are pure where possible
- [ ] Dependencies injectable
- [ ] Side effects isolated
- [ ] Test coverage >80%
```

**Compliance Verification**:
```typescript
/**
 * Compliance Checker
 * 
 * Verifies code against external standards and regulations
 */
class ComplianceChecker {
  checkStandards(code: string, standards: Standard[]): ComplianceReport {
    const violations: Violation[] = [];
    
    standards.forEach(standard => {
      // Check coding standards (e.g., MISRA C++, JSLint)
      violations.push(...this.checkCodingStandard(code, standard));
      
      // Check security standards (e.g., OWASP)
      violations.push(...this.checkSecurityStandard(code, standard));
      
      // Check regulatory requirements (e.g., GDPR, HIPAA)
      violations.push(...this.checkRegulatoryCompliance(code, standard));
    });
    
    return {
      compliant: violations.length === 0,
      violations,
      score: this.calculateComplianceScore(violations)
    };
  }
  
  private checkSecurityStandard(code: string, standard: Standard): Violation[] {
    const violations: Violation[] = [];
    
    // SQL Injection checks
    if (code.includes('SELECT') && code.includes('+')) {
      violations.push({
        rule: 'OWASP-A1: SQL Injection',
        severity: 'CRITICAL',
        message: 'Potential SQL injection vulnerability (string concatenation)'
      });
    }
    
    // XSS checks
    if (code.includes('innerHTML') && !code.includes('sanitize')) {
      violations.push({
        rule: 'OWASP-A7: XSS',
        severity: 'HIGH',
        message: 'Potential XSS vulnerability (unsanitized innerHTML)'
      });
    }
    
    // Hardcoded secrets
    const secretPatterns = [
      /password\s*=\s*["'].*["']/i,
      /api[_-]?key\s*=\s*["'].*["']/i,
      /secret\s*=\s*["'].*["']/i
    ];
    
    secretPatterns.forEach(pattern => {
      if (pattern.test(code)) {
        violations.push({
          rule: 'OWASP-A2: Broken Authentication',
          severity: 'CRITICAL',
          message: 'Hardcoded secret detected'
        });
      }
    });
    
    return violations;
  }
}
```

### 8. Trace Logic and Data Flows

**Objective**: Validate sequences of states and data transformations.

**Data Flow Analysis**:
```typescript
/**
 * Data Flow Tracer
 * 
 * Tracks how data moves through the system
 */
class DataFlowTracer {
  traceDataFlow(entryPoint: string, inputData: any): FlowTrace {
    const trace: FlowTrace = {
      steps: [],
      transformations: [],
      validations: []
    };
    
    // Instrument code to capture flow
    const instrumented = this.instrumentCode(entryPoint);
    
    // Execute with input
    try {
      instrumented.execute(inputData, (step: FlowStep) => {
        trace.steps.push(step);
        
        // Capture transformations
        if (step.type === 'TRANSFORM') {
          trace.transformations.push({
            input: step.input,
            output: step.output,
            function: step.function
          });
        }
        
        // Capture validations
        if (step.type === 'VALIDATE') {
          trace.validations.push({
            rule: step.rule,
            passed: step.passed,
            value: step.value
          });
        }
      });
    } catch (error) {
      trace.error = error;
    }
    
    return trace;
  }
  
  validatePrecision(trace: FlowTrace): PrecisionReport {
    // Check for truncation issues
    const truncations = trace.transformations.filter(t =>
      this.isTruncation(t.input, t.output)
    );
    
    // Check for rounding issues
    const roundings = trace.transformations.filter(t =>
      this.isRounding(t.input, t.output)
    );
    
    return {
      truncations,
      roundings,
      precisionLoss: this.calculatePrecisionLoss(trace)
    };
  }
}
```

**Example: Order Processing Flow**:
```markdown
## Data Flow Analysis: Order Processing

### Flow Trace
1. **Input**: `POST /api/orders { userId: 123, items: [...] }`
2. **Validation**: Check user exists → ✅ PASS
3. **Validation**: Check items in stock → ✅ PASS
4. **Transformation**: Calculate subtotal → $123.45
5. **Transformation**: Apply tax (8.5%) → $133.94
6. **Transformation**: Apply shipping → $143.94
7. **Validation**: Check credit limit → ✅ PASS
8. **Persistence**: Save order → Order #456
9. **Event**: Emit OrderCreatedEvent
10. **Output**: `{ orderId: 456, total: 143.94 }`

### Precision Analysis
- ⚠️ **Potential Truncation**: Tax calculation rounds to 2 decimals
- ✅ **No Rounding Issues**: All currency calculations use Decimal type

### Validation Rules Verified
- ✅ User must exist
- ✅ All items must be in stock
- ✅ Order total must not exceed credit limit
- ✅ Email notification sent after order creation
```

### 9. Analyze Low-Level Implementation (If Needed)

**Objective**: Understand compiler optimizations and assembly-level behavior.

**When to Use**:
- Performance-critical code (hot paths)
- Real-time systems with strict timing
- Understanding specific compiler optimizations
- Reproducing highly efficient algorithms

**Assembly Analysis Tool**:
```cpp
/**
 * Assembly Analysis for Performance-Critical Code
 * 
 * Compile with: g++ -S -O2 -fverbose-asm code.cpp
 */

// Original C++ code
int fast_modulo_power_of_2(int value, int divisor) {
  // Question: Does compiler optimize modulo to bitwise AND?
  return value % divisor;
}

// Assembly output (x86-64, GCC 11.2, -O2):
// fast_modulo_power_of_2(int, int):
//   mov eax, edi          ; eax = value
//   lea ecx, [rsi-1]      ; ecx = divisor - 1
//   test esi, ecx         ; test if divisor is power of 2
//   jne .L_slow_path      ; if not, use slow division
//   and eax, ecx          ; Fast path: value & (divisor - 1)
//   ret
// .L_slow_path:
//   cdq                   ; Sign-extend for division
//   idiv esi              ; Slow path: actual division
//   mov eax, edx          ; Return remainder
//   ret

/**
 * Analysis Result:
 * 
 * ✅ Compiler DOES optimize modulo to bitwise AND when divisor is power of 2
 * ⚠️ For non-power-of-2, falls back to slow division
 * 
 * Recommendation: If divisor is always power of 2, explicitly use bitwise AND
 * for clarity and guaranteed optimization.
 */
int fast_modulo_explicit(int value, int divisor) {
  assert((divisor & (divisor - 1)) == 0);  // Assert power of 2
  return value & (divisor - 1);
}
```

### 10. Analyze Interfaces

**Objective**: Verify external and internal interfaces are correct and complete.

**Interface Analysis Template**:
```markdown
## Interface Analysis: [Interface Name]

### Interface Definition
```typescript
interface PaymentGateway {
  processPayment(request: PaymentRequest): Promise<PaymentResult>;
  refund(transactionId: string, amount: number): Promise<RefundResult>;
  getTransactionStatus(transactionId: string): Promise<TransactionStatus>;
}
```

### Correctness Checks
- [ ] All methods have clear contracts (pre/post conditions)
- [ ] Return types match expected outcomes
- [ ] Error cases defined and handled
- [ ] Thread-safety documented (if applicable)

### Consistency Checks
- [ ] Naming conventions followed
- [ ] Parameter ordering consistent
- [ ] Error handling approach uniform
- [ ] Documentation format consistent

### Completeness Checks
- [ ] All required operations present
- [ ] All data fields included
- [ ] All error conditions covered
- [ ] All edge cases handled

### Data Format Verification
- [ ] **PaymentRequest**:
  - `amount`: Decimal (2 decimal places)
  - `currency`: ISO 4217 code (3 letters)
  - `paymentMethod`: Enum (CARD, PAYPAL, CRYPTO)
- [ ] **PaymentResult**:
  - `transactionId`: UUID v4
  - `status`: Enum (SUCCESS, FAILED, PENDING)
  - `timestamp`: ISO 8601 datetime

### Performance Criteria
- [ ] `processPayment`: <500ms (95th percentile)
- [ ] `refund`: <1s (95th percentile)
- [ ] `getTransactionStatus`: <100ms (95th percentile)
- [ ] **Timeout**: 30s maximum
- [ ] **Retry**: 3 attempts with exponential backoff

### Security Requirements
- [ ] API key authentication required
- [ ] HTTPS only (TLS 1.2+)
- [ ] Request signing (HMAC-SHA256)
- [ ] PCI DSS compliance

### Compatibility
- [ ] Backward compatible with v1.x
- [ ] Breaking changes documented
- [ ] Migration guide provided
```

## III. Extracting Intent Through Modeling and Simplification

### 11. Perform Collaborative Knowledge Crunching

**Objective**: Collaborate with domain experts to distill business knowledge from legacy code.

**Knowledge Crunching Session Template**:
```markdown
## Knowledge Crunching Session: [Domain Area]

**Date**: [Date]
**Participants**: 
- Domain Experts: [Names]
- Developers: [Names]
- Facilitator: [Name]

**Code Under Analysis**: `src/billing/invoice-calculator.ts`

### Session Agenda
1. Walk through code behavior (30 min)
2. Identify domain concepts (20 min)
3. Map concepts to Ubiquitous Language (20 min)
4. Document business rules (20 min)
5. Identify missing concepts (10 min)

### Code Walkthrough Findings

**Current Code Structure**:
```typescript
function calculateInvoice(customerId: string, items: Item[]): number {
  let total = 0;
  
  // Apply item prices
  items.forEach(item => {
    total += item.price * item.quantity;
  });
  
  // Apply discounts (complex nested logic)
  if (isVIPCustomer(customerId)) {
    if (total > 1000) {
      total *= 0.85;  // 15% discount
    } else {
      total *= 0.90;  // 10% discount
    }
  } else if (isSeasonalPromotion()) {
    total *= 0.95;  // 5% discount
  }
  
  // Apply tax
  total *= 1.08;  // 8% tax
  
  return total;
}
```

### Domain Concepts Identified

**Explicit Concepts** (already in code):
- ✅ **Customer** (referenced by `customerId`)
- ✅ **Item** (product being purchased)
- ✅ **Invoice** (result of calculation)

**Implicit Concepts** (lurking in code):
- ⚠️ **Customer Tier** (VIP vs. Regular) - Hidden in `isVIPCustomer()`
- ⚠️ **Discount Policy** - Complex nested logic, not explicit
- ⚠️ **Pricing Rule** - Different discounts based on tier and amount
- ⚠️ **Tax Jurisdiction** - Hardcoded 8%, but varies by location

### Business Rules Extracted

1. **VIP Customer Discount Rule**:
   - IF customer is VIP AND order total > $1000 THEN 15% discount
   - IF customer is VIP AND order total ≤ $1000 THEN 10% discount

2. **Seasonal Promotion Rule**:
   - IF seasonal promotion active AND customer is not VIP THEN 5% discount

3. **Tax Calculation Rule**:
   - Tax rate is 8% (Question: Does this vary by state?)

### Ubiquitous Language Updates

| Term | Definition | Code Mapping |
|------|------------|--------------|
| **Customer Tier** | Classification of customer (VIP, Regular) | `CustomerTier` enum |
| **Discount Policy** | Rules for applying discounts based on tier and amount | `DiscountPolicy` class |
| **Pricing Strategy** | Algorithm for calculating final price | `PricingStrategy` interface |
| **Tax Jurisdiction** | Geographic area determining tax rate | `TaxJurisdiction` value object |

### Missing Concepts (Need Clarification)

1. ❓ **Volume Discount**: Are there discounts for bulk purchases beyond VIP?
2. ❓ **Coupon Codes**: Can customers apply additional discount codes?
3. ❓ **Tax Exemption**: Are some customers tax-exempt (non-profits, government)?
4. ❓ **Rounding Policy**: How should fractional cents be handled?

### Proposed Refactoring (Making Intent Explicit)

```typescript
// Explicit domain model
class CustomerTier {
  static readonly VIP = new CustomerTier('VIP', 0.10);
  static readonly REGULAR = new CustomerTier('REGULAR', 0);
  
  constructor(
    readonly name: string,
    readonly baseDiscount: number
  ) {}
}

class DiscountPolicy {
  calculate(tier: CustomerTier, subtotal: Money): Money {
    const baseDiscount = tier.baseDiscount;
    
    // VIP tier-based discount
    if (tier === CustomerTier.VIP) {
      const discountRate = subtotal.greaterThan(Money.of(1000)) 
        ? 0.15  // High-value VIP discount
        : 0.10; // Standard VIP discount
      return subtotal.multiply(discountRate);
    }
    
    // Seasonal promotion (non-VIP only)
    if (this.isSeasonalPromotionActive() && tier === CustomerTier.REGULAR) {
      return subtotal.multiply(0.05);
    }
    
    return Money.zero();
  }
}

class Invoice {  // Aggregate Root (DDD)
  constructor(
    readonly customer: Customer,
    readonly items: LineItem[],
    readonly taxJurisdiction: TaxJurisdiction
  ) {}
  
  calculateTotal(): Money {
    // Explicit steps (reveals intent)
    const subtotal = this.calculateSubtotal();
    const discount = this.applyDiscountPolicy(subtotal);
    const taxableAmount = subtotal.subtract(discount);
    const tax = this.calculateTax(taxableAmount);
    
    return taxableAmount.add(tax);
  }
  
  private applyDiscountPolicy(subtotal: Money): Money {
    const policy = new DiscountPolicy();
    return policy.calculate(this.customer.tier, subtotal);
  }
  
  private calculateTax(amount: Money): Money {
    return amount.multiply(this.taxJurisdiction.taxRate);
  }
}
```

### Follow-Up Actions
- [ ] Validate proposed model with domain experts
- [ ] Write tests for extracted business rules
- [ ] Refactor code to use explicit domain model
- [ ] Update Ubiquitous Language glossary
- [ ] Document ADR for invoice calculation redesign
```

### 12. Model Hidden Concepts

**Objective**: Recognize implicit concepts causing code complexity.

**Hidden Concept Detection Checklist**:
```markdown
## Hidden Concept Analysis

### Symptoms of Missing Concepts

1. **Awkward Code Structures**:
   - [ ] Long parameter lists (>4 parameters)
   - [ ] Complex boolean conditions (>3 conditions)
   - [ ] Nested if-else chains (>3 levels)
   - [ ] Primitive obsession (strings/numbers instead of types)

2. **Duplication**:
   - [ ] Similar logic repeated in multiple places
   - [ ] Same validation rules duplicated
   - [ ] Common transformations scattered

3. **Comments Explaining Intent**:
   - [ ] Comments like "// Calculate discount based on customer type"
   - [ ] Comments explaining complex formulas
   - [ ] Comments describing business rules

4. **God Classes**:
   - [ ] Classes with >10 public methods
   - [ ] Classes handling multiple responsibilities
   - [ ] Classes with names like "Manager", "Helper", "Utility"

### Example: Hidden "Money" Concept

**Before** (Primitive Obsession):
```typescript
// ❌ Using primitives for money
function addPrices(a: number, b: number): number {
  return a + b;  // What about rounding? Currency? Precision?
}

const price1 = 10.50;
const price2 = 20.75;
const total = addPrices(price1, price2);  // 31.25... but is this correct for money?
```

**After** (Explicit Money Value Object):
```typescript
// ✅ Explicit Money concept
class Money {  // Value Object (DDD)
  private constructor(
    readonly amount: number,  // Stored as cents (integer)
    readonly currency: Currency
  ) {}
  
  static of(amount: number, currency = Currency.USD): Money {
    return new Money(Math.round(amount * 100), currency);
  }
  
  add(other: Money): Money {
    if (this.currency !== other.currency) {
      throw new Error('Cannot add different currencies');
    }
    return new Money(this.amount + other.amount, this.currency);
  }
  
  toDollars(): number {
    return this.amount / 100;
  }
}

// Usage
const price1 = Money.of(10.50);
const price2 = Money.of(20.75);
const total = price1.add(price2);  // Type-safe, correct rounding
```

### Hidden Concept Extraction Process

1. **Identify Symptoms**: Look for code smells (complex logic, duplication)
2. **Name the Concept**: Find domain term for the implicit idea
3. **Extract Class/Type**: Create explicit abstraction
4. **Refactor Callers**: Replace primitive with new type
5. **Validate with Tests**: Ensure behavior unchanged
6. **Update Ubiquitous Language**: Add to glossary
```

### 13. Use Intentional Refactoring

**Objective**: Simplify code to reveal intent without changing behavior.

**Extract Method Refactoring**:
```typescript
/**
 * Before: Complex method hiding intent
 */
function processOrder(order: any): void {
  // Validate order
  if (!order.customerId) {
    throw new Error('Customer ID required');
  }
  if (!order.items || order.items.length === 0) {
    throw new Error('Order must have items');
  }
  
  // Calculate total
  let total = 0;
  order.items.forEach((item: any) => {
    total += item.price * item.quantity;
  });
  
  // Check inventory
  order.items.forEach((item: any) => {
    const stock = inventory.get(item.productId);
    if (stock < item.quantity) {
      throw new Error(`Insufficient stock for ${item.productId}`);
    }
  });
  
  // Reserve inventory
  order.items.forEach((item: any) => {
    inventory.reserve(item.productId, item.quantity);
  });
  
  // Save order
  database.save('orders', order);
  
  // Send notification
  email.send(order.customerId, 'Order Confirmed', `Total: $${total}`);
}

/**
 * After: Intent revealed through extracted methods
 */
function processOrder(order: Order): void {
  validateOrder(order);
  const total = calculateOrderTotal(order);
  checkInventoryAvailability(order);
  reserveInventory(order);
  saveOrder(order);
  sendOrderConfirmation(order, total);
}

// Each method has single responsibility and clear intent
function validateOrder(order: Order): void {
  if (!order.customerId) {
    throw new OrderValidationError('Customer ID required');
  }
  if (!order.hasItems()) {
    throw new OrderValidationError('Order must have items');
  }
}

function calculateOrderTotal(order: Order): Money {
  return order.items.reduce(
    (sum, item) => sum.add(item.subtotal()),
    Money.zero()
  );
}

function checkInventoryAvailability(order: Order): void {
  order.items.forEach(item => {
    const available = inventory.getAvailableStock(item.productId);
    if (available < item.quantity) {
      throw new InsufficientStockError(item.productId, available, item.quantity);
    }
  });
}
```

**Refactoring Pattern Catalog**:
```markdown
## Common Refactoring Patterns for Reverse Engineering

### 1. Extract Method
**When**: Long method with multiple responsibilities  
**Goal**: Split into smaller, single-purpose methods  
**Benefit**: Reveals intent through method names

### 2. Extract Class
**When**: God class handling too many concerns  
**Goal**: Split responsibilities into separate classes  
**Benefit**: Explicit domain concepts

### 3. Replace Conditional with Polymorphism
**When**: Complex switch/if-else on type codes  
**Goal**: Use inheritance/interfaces  
**Benefit**: Open-closed principle

### 4. Introduce Parameter Object
**When**: Long parameter lists  
**Goal**: Group related parameters into object  
**Benefit**: Explicit concept, easier to extend

### 5. Replace Magic Numbers with Named Constants
**When**: Numeric literals scattered in code  
**Goal**: Define constants with meaningful names  
**Benefit**: Intent clear, easy to change

### 6. Introduce Explaining Variable
**When**: Complex expressions  
**Goal**: Assign to variable with descriptive name  
**Benefit**: Self-documenting code
```

### 14. Apply Object-Oriented Re-Architecturing

**Objective**: Transform functional decomposition into object-oriented design.

**Law of Inversion (Data First)**:
```markdown
## Re-Architecturing Process: Functional → Object-Oriented

### Step 1: Identify Data Abstractions

**Original Functional Code**:
```c
// Functional decomposition (data structures + functions)
typedef struct {
  char name[50];
  float balance;
  char account_number[20];
} Account;

void deposit(Account* account, float amount) {
  account->balance += amount;
  log_transaction(account->account_number, "DEPOSIT", amount);
}

void withdraw(Account* account, float amount) {
  if (account->balance >= amount) {
    account->balance -= amount;
    log_transaction(account->account_number, "WITHDRAW", amount);
  } else {
    printf("Insufficient funds\n");
  }
}

float get_balance(Account* account) {
  return account->balance;
}
```

**Identify Data Abstractions**:
- **Account** (data structure) → **Account** (class/entity)
- Operations on Account → Methods of Account class

### Step 2: Repackage Functions into Classes

**Object-Oriented Redesign**:
```typescript
class Account {  // Entity (DDD)
  constructor(
    readonly accountNumber: AccountNumber,  // Value Object
    private name: string,
    private balance: Money  // Value Object
  ) {}
  
  deposit(amount: Money): void {
    // Business rule: Cannot deposit negative amount
    if (amount.isNegative()) {
      throw new InvalidOperationError('Cannot deposit negative amount');
    }
    
    this.balance = this.balance.add(amount);
    this.logTransaction('DEPOSIT', amount);
  }
  
  withdraw(amount: Money): Result<void, InsufficientFundsError> {
    // Business rule: Cannot overdraft
    if (this.balance.lessThan(amount)) {
      return Result.failure(new InsufficientFundsError(this.balance, amount));
    }
    
    this.balance = this.balance.subtract(amount);
    this.logTransaction('WITHDRAW', amount);
    
    return Result.success();
  }
  
  getBalance(): Money {
    return this.balance;
  }
  
  private logTransaction(type: string, amount: Money): void {
    TransactionLogger.log(this.accountNumber, type, amount);
  }
}
```

### Step 3: Discover Abstractions Through Refactoring

**Identify Common Patterns**:
```typescript
// Pattern: Transaction types (Deposit, Withdrawal, Transfer)
// Extract polymorphic hierarchy

interface Transaction {
  execute(account: Account): Result<void, Error>;
  getType(): string;
  getAmount(): Money;
}

class DepositTransaction implements Transaction {
  constructor(private amount: Money) {}
  
  execute(account: Account): Result<void, Error> {
    return account.deposit(this.amount);
  }
  
  getType(): string { return 'DEPOSIT'; }
  getAmount(): Money { return this.amount; }
}

class WithdrawalTransaction implements Transaction {
  constructor(private amount: Money) {}
  
  execute(account: Account): Result<void, Error> {
    return account.withdraw(this.amount);
  }
  
  getType(): string { return 'WITHDRAW'; }
  getAmount(): Money { return this.amount; }
}

// Now can add new transaction types without modifying Account class
```
```

## IV. Validation and Proof

### 15. Use Automated Testing

**Objective**: Prove understanding through executable specifications.

**Test-Driven Reverse Engineering**:
```typescript
/**
 * Step 1: Write tests expressing expected behavior
 * (derived from user manual, operational data, interviews)
 */
describe('Order Processing (Reverse Engineered)', () => {
  it('should calculate total with VIP discount for orders >$1000', () => {
    // Arrange
    const customer = new Customer({ tier: CustomerTier.VIP });
    const items = [
      new LineItem({ price: Money.of(800), quantity: 1 }),
      new LineItem({ price: Money.of(300), quantity: 1 })
    ];
    const order = new Order(customer, items);
    
    // Act
    const total = order.calculateTotal();
    
    // Assert
    const expectedSubtotal = Money.of(1100);
    const expectedDiscount = Money.of(165);  // 15% discount
    const expectedTotal = Money.of(935);
    
    expect(total).toEqual(expectedTotal);
  });
  
  it('should apply 10% VIP discount for orders ≤$1000', () => {
    const customer = new Customer({ tier: CustomerTier.VIP });
    const items = [new LineItem({ price: Money.of(500), quantity: 2 })];
    const order = new Order(customer, items);
    
    const total = order.calculateTotal();
    
    // 10% discount (not 15%)
    expect(total).toEqual(Money.of(900));
  });
  
  it('should apply 5% seasonal discount for regular customers', () => {
    // Mock seasonal promotion active
    jest.spyOn(PromotionService, 'isSeasonalActive').mockReturnValue(true);
    
    const customer = new Customer({ tier: CustomerTier.REGULAR });
    const items = [new LineItem({ price: Money.of(100), quantity: 1 })];
    const order = new Order(customer, items);
    
    const total = order.calculateTotal();
    
    // 5% seasonal discount
    expect(total).toEqual(Money.of(95));
  });
});

/**
 * Step 2: Run tests against EXISTING code
 * 
 * Result: Tests FAIL because legacy code has bugs/different logic
 */

/**
 * Step 3: Investigate discrepancies
 * 
 * - Interview users: "Is the behavior in tests correct?"
 * - Check operational logs: "What actually happens in production?"
 * - Consult domain experts: "What should happen?"
 */

/**
 * Step 4: Update tests or fix code based on findings
 * 
 * - If tests are wrong: Update tests to match actual requirements
 * - If code is wrong: Fix code to match correct requirements
 * - Document decisions in ADR
 */
```

### 16. Test Against Specifications (Design by Contract)

**Objective**: Verify code honors its contract.

**Contract-Based Testing**:
```typescript
/**
 * Account Class Contract
 * 
 * Invariants:
 * - Balance is always >= 0 (no overdraft)
 * - Account number is immutable
 */
class Account {
  // Invariant check (can be enabled in development)
  private checkInvariants(): void {
    if (this.balance.isNegative()) {
      throw new InvariantViolation('Balance cannot be negative');
    }
  }
  
  /**
   * Withdraw operation
   * 
   * Preconditions:
   * - amount > 0
   * - amount <= balance
   * 
   * Postconditions:
   * - balance decreased by amount
   * - transaction logged
   */
  withdraw(amount: Money): Result<void, Error> {
    // Precondition checks
    if (amount.isNegative() || amount.isZero()) {
      throw new PreconditionViolation('Amount must be positive');
    }
    
    if (this.balance.lessThan(amount)) {
      return Result.failure(new InsufficientFundsError());
    }
    
    // Execute operation
    const oldBalance = this.balance;
    this.balance = this.balance.subtract(amount);
    this.logTransaction('WITHDRAW', amount);
    
    // Postcondition checks
    assert(
      this.balance.equals(oldBalance.subtract(amount)),
      'Balance should decrease by withdrawal amount'
    );
    
    this.checkInvariants();
    
    return Result.success();
  }
}

/**
 * Contract Tests
 */
describe('Account Contract', () => {
  describe('Invariants', () => {
    it('should never allow negative balance', () => {
      const account = new Account(AccountNumber.generate(), 'Test', Money.of(100));
      
      // Try to violate invariant
      const result = account.withdraw(Money.of(150));
      
      // Should fail gracefully
      expect(result.isFailure()).toBe(true);
      expect(account.getBalance().isNegative()).toBe(false);
    });
  });
  
  describe('Withdraw Preconditions', () => {
    it('should reject negative withdrawal amounts', () => {
      const account = new Account(AccountNumber.generate(), 'Test', Money.of(100));
      
      expect(() => {
        account.withdraw(Money.of(-50));
      }).toThrow(PreconditionViolation);
    });
    
    it('should reject zero withdrawal amounts', () => {
      const account = new Account(AccountNumber.generate(), 'Test', Money.of(100));
      
      expect(() => {
        account.withdraw(Money.zero());
      }).toThrow(PreconditionViolation);
    });
  });
  
  describe('Withdraw Postconditions', () => {
    it('should decrease balance by exact withdrawal amount', () => {
      const account = new Account(AccountNumber.generate(), 'Test', Money.of(100));
      const withdrawAmount = Money.of(30);
      
      account.withdraw(withdrawAmount);
      
      expect(account.getBalance()).toEqual(Money.of(70));
    });
    
    it('should log transaction after successful withdrawal', () => {
      const logSpy = jest.spyOn(TransactionLogger, 'log');
      const account = new Account(AccountNumber.generate(), 'Test', Money.of(100));
      
      account.withdraw(Money.of(30));
      
      expect(logSpy).toHaveBeenCalledWith(
        expect.any(AccountNumber),
        'WITHDRAW',
        Money.of(30)
      );
    });
  });
});
```

### 17. Validate Assumptions (Prototype/Comparison)

**Objective**: When uncertain about functionality, compare against a reference implementation.

**Comparison Testing Approach**:
```typescript
/**
 * Scenario: Legacy tax calculation logic is unclear
 * Question: Does it handle tax exemptions correctly?
 */

// 1. Create reference implementation (from specification)
class ReferenceTaxCalculator {
  calculate(amount: Money, jurisdiction: TaxJurisdiction, customer: Customer): Money {
    // From tax specification document
    if (customer.isTaxExempt()) {
      return Money.zero();
    }
    
    return amount.multiply(jurisdiction.taxRate);
  }
}

// 2. Extract legacy implementation
class LegacyTaxCalculator {
  calculate(amount: Money, jurisdiction: TaxJurisdiction, customer: Customer): Money {
    // Legacy code (complex, unclear)
    const rate = jurisdiction.taxRate;
    const exemptFlag = customer.getFlag('tax_exempt');  // Unclear field
    
    if (exemptFlag === 'Y' || exemptFlag === '1') {  // Multiple representations?
      return Money.zero();
    }
    
    // More complex logic...
    return amount.multiply(rate);
  }
}

// 3. Compare outputs across test cases
describe('Tax Calculation Comparison', () => {
  const testCases = [
    {
      name: 'Regular customer',
      customer: new Customer({ taxExempt: false }),
      amount: Money.of(100),
      jurisdiction: new TaxJurisdiction('CA', 0.08),
      expectedTax: Money.of(8)
    },
    {
      name: 'Tax-exempt customer (non-profit)',
      customer: new Customer({ taxExempt: true }),
      amount: Money.of(100),
      jurisdiction: new TaxJurisdiction('CA', 0.08),
      expectedTax: Money.zero()
    },
    {
      name: 'High-value transaction',
      customer: new Customer({ taxExempt: false }),
      amount: Money.of(10000),
      jurisdiction: new TaxJurisdiction('CA', 0.08),
      expectedTax: Money.of(800)
    }
  ];
  
  testCases.forEach(testCase => {
    it(`should match reference implementation for: ${testCase.name}`, () => {
      const reference = new ReferenceTaxCalculator();
      const legacy = new LegacyTaxCalculator();
      
      const referenceTax = reference.calculate(
        testCase.amount,
        testCase.jurisdiction,
        testCase.customer
      );
      
      const legacyTax = legacy.calculate(
        testCase.amount,
        testCase.jurisdiction,
        testCase.customer
      );
      
      // Compare outputs
      expect(legacyTax).toEqual(referenceTax);
      expect(legacyTax).toEqual(testCase.expectedTax);
    });
  });
  
  it('should identify discrepancies in edge cases', () => {
    // Edge case: Customer with 'Y' flag vs. boolean true
    const customerWithFlag = { getFlag: () => 'Y', isTaxExempt: () => true };
    const customerWithBoolean = { getFlag: () => null, isTaxExempt: () => true };
    
    const reference = new ReferenceTaxCalculator();
    const legacy = new LegacyTaxCalculator();
    
    // Both should return zero tax
    const tax1 = legacy.calculate(Money.of(100), jurisdiction, customerWithFlag);
    const tax2 = legacy.calculate(Money.of(100), jurisdiction, customerWithBoolean);
    
    expect(tax1).toEqual(Money.zero());
    expect(tax2).toEqual(Money.zero());  // May fail if legacy has bug
  });
});

/**
 * Analysis Results:
 * 
 * ✅ Legacy matches reference for standard cases
 * ⚠️ Legacy has bug: Doesn't handle boolean `false` correctly (treats as truthy)
 * ⚠️ Legacy uses multiple representations for tax exemption ('Y', '1', true)
 * 
 * Recommendation: Refactor to use consistent boolean representation
 */
```

### 18. Verbalize Assumptions (Rubber Ducking)

**Objective**: Explain code step-by-step to uncover faulty assumptions.

**Rubber Duck Debugging Process**:
```markdown
## Rubber Duck Session: Understanding Payment Processing Logic

**Code Under Analysis**: `processPayment()` function

**Explanation (Step-by-Step)**:

"Okay, rubber duck, let me explain this payment processing code to you:

1. First, we receive a `PaymentRequest` object with amount, payment method, and customer ID.

2. Then, we call `validatePaymentMethod()` to check if... wait, what exactly does this validate? Let me look...
   
   *[Reads code]*
   
   Oh, it checks if the payment method is one of: CARD, PAYPAL, or CRYPTO. But wait, the function also checks if the customer has this payment method registered. So it's validating TWO things, not just one. **Assumption challenged**: I thought it only validated the method type.

3. Next, we call `authorizePayment()` which contacts the payment gateway. But hold on... 
   
   *[Reads code]*
   
   This is an async operation, but there's no timeout handling! **Assumption challenged**: I assumed network calls had timeouts. What happens if the gateway is down?

4. Then, if authorization succeeds, we save the transaction to the database. But... wait a minute...
   
   *[Traces logic]*
   
   If the `save()` call fails (database error), we've already charged the customer! There's no rollback! **Assumption challenged**: I thought this was transactional. This is a critical bug!"

**Findings from Rubber Duck Session**:
- ⚠️ **Bug**: `validatePaymentMethod()` has dual responsibility (SRP violation)
- ⚠️ **Bug**: No timeout on payment gateway calls (could hang indefinitely)
- 🐛 **Critical Bug**: No transaction rollback if database save fails (customer charged but order not recorded)

**Follow-Up Actions**:
- [ ] Add timeout to `authorizePayment()` (30s max)
- [ ] Implement compensating transaction (refund if save fails)
- [ ] Split `validatePaymentMethod()` into two functions
- [ ] Add integration test for timeout scenario
- [ ] Add integration test for database failure scenario
```

### 19. Check for Unintended Consequences

**Objective**: Validate code doesn't cause unexpected side effects.

**Side Effect Analysis Checklist**:
```markdown
## Side Effect Analysis: [Component]

### Global State Modifications
- [ ] Check for global variable modifications
- [ ] Check for singleton state changes
- [ ] Check for environment variable modifications
- [ ] Check for process-level settings changes

### Data Mutations
- [ ] Check for in-place array/object mutations
- [ ] Check for shared data structure modifications
- [ ] Check for cache invalidation side effects
- [ ] Check for event emission side effects

### External System Interactions
- [ ] Check for database writes
- [ ] Check for file system changes
- [ ] Check for network calls (APIs, services)
- [ ] Check for message queue publications

### Temporal Side Effects
- [ ] Check for timer/interval creation
- [ ] Check for promise/callback registration
- [ ] Check for event listener registration
- [ ] Check for resource allocation (memory, file handles)

### Example Analysis

**Code**:
```typescript
function updateUserProfile(userId: string, updates: Partial<User>): void {
  const user = userCache.get(userId);  // ⚠️ Global cache access
  
  Object.assign(user, updates);  // ⚠️ In-place mutation
  
  database.save('users', user);  // ⚠️ Database write
  
  eventBus.emit('user.updated', user);  // ⚠️ Event emission
  
  analytics.track('profile_updated', { userId });  // ⚠️ External API call
}
```

**Side Effects Identified**:
1. ⚠️ **Global Cache Mutation**: Modifies shared cache (affects other callers)
2. ⚠️ **Object Mutation**: Modifies user object in-place (may affect callers)
3. ⚠️ **Database Write**: Persistent side effect (cannot be undone easily)
4. ⚠️ **Event Emission**: Triggers unknown listeners (cascade effects)
5. ⚠️ **Analytics Call**: Network call (may fail, slow down function)

**Unintended Consequences**:
- If analytics call fails, entire operation fails (should be fire-and-forget)
- Event listeners may modify the same user object (race condition)
- Cache mutation visible to concurrent threads (thread-safety issue)

**Recommendations**:
- [ ] Make analytics call async/non-blocking
- [ ] Clone user object before mutation
- [ ] Use transaction for database + cache update
- [ ] Document side effects in function contract
```

## ✅ Reverse Engineering Workflow Checklist

### Phase 1: Gather Evidence (1-2 weeks)
- [ ] Inventory all available documentation
- [ ] Analyze operational logs and metrics
- [ ] Interview operational users (3-5 sessions)
- [ ] Interview original developers (if available)
- [ ] Extract requirements from user manual
- [ ] Analyze V&V results and test history
- [ ] Create traceability map (existing)

### Phase 2: Structural Analysis (2-3 weeks)
- [ ] Run dependency analysis tool
- [ ] Identify architectural layers
- [ ] Detect layer violations
- [ ] Find circular dependencies
- [ ] Analyze code quality metrics
- [ ] Check standards compliance
- [ ] Map data flows and interfaces

### Phase 3: Domain Modeling (1-2 weeks)
- [ ] Conduct knowledge crunching sessions (5-8 sessions)
- [ ] Extract Ubiquitous Language
- [ ] Identify explicit concepts
- [ ] Discover hidden concepts
- [ ] Map domain concepts to code
- [ ] Create Context Map (Bounded Contexts)
- [ ] Document business rules

### Phase 4: Validation (2-4 weeks)
- [ ] Write automated tests (unit, integration, e2e)
- [ ] Implement contract tests (preconditions, postconditions, invariants)
- [ ] Perform comparison testing (prototype vs. legacy)
- [ ] Conduct rubber duck sessions for complex logic
- [ ] Validate against derived requirements
- [ ] Check for unintended side effects
- [ ] Generate traceability matrix

### Phase 5: Documentation (1-2 weeks)
- [ ] Create requirements specification (recovered)
- [ ] Create architecture documentation
- [ ] Create design documentation (component diagrams, class diagrams)
- [ ] Document ADRs (architecture decisions discovered)
- [ ] Update Ubiquitous Language glossary
- [ ] Create developer onboarding guide
- [ ] Archive findings in GitHub Issues

## 📊 Integration with Framework

### Standards Compliance
- **IEEE 1219-1998**: Software Maintenance (includes reverse engineering)
- **ISO/IEC/IEEE 12207:2017**: Reverse Engineering Process
- **IEEE 1016-2009**: Design documentation format
- **IEEE 42010**: Architecture recovery

### XP Integration
- **TDD**: Write tests to validate understanding
- **Refactoring**: Simplify code to reveal intent
- **Knowledge Crunching**: Collaborate with domain experts
- **Continuous Integration**: Validate changes don't break behavior

### DDD Integration
- **Ubiquitous Language**: Extract from legacy code and domain experts
- **Bounded Context**: Identify logical boundaries in existing system
- **Tactical Patterns**: Recognize Entity, Value Object, Aggregate, Repository patterns
- **Model-Driven Design**: Refactor toward explicit domain model

## 📚 Related Documentation

- **Phase 01-02**: Use recovered requirements for `01-stakeholder-requirements/` and `02-requirements/`
- **Phase 03-04**: Document recovered architecture in `03-architecture/` and `04-design/`
- **Phase 07**: Use tests for validation in `07-verification-validation/`
- **TDD Guide**: `docs/tdd-empirical-proof.md` - Testing strategies
- **DDD Guide**: `docs/ddd-implementation-guide.md` - Domain modeling
- **Refactoring**: Apply patterns from `docs/tdd-empirical-proof.md`

---

**Remember: Reverse engineering is detective work. Gather evidence, form hypotheses, and prove understanding through automated tests!** 🔍✅
