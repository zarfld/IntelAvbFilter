# Methodology Integration Summary

**Purpose**: Show how DDD, TDD, Real-Time, and Standards work together synergistically  
**Audience**: Teams adopting the standards-compliant template  
**Status**: Framework Complete

## 🎯 Complete Framework Overview

This template integrates **six complementary methodologies** to create a comprehensive software development framework:

| Methodology | Focus | Key Practices | Documentation |
|-------------|-------|---------------|---------------|
| **IEEE/ISO/IEC Standards** | Process compliance | 9-phase lifecycle, traceability, verification | `.github/copilot-instructions.md` |
| **Extreme Programming (XP)** | Agile practices | TDD, CI, Simple Design, YAGNI | `docs/xp-practices.md` |
| **Test-Driven Development (TDD)** | Empirical validation | Red-Green-Refactor, spike solutions, assertions | `docs/tdd-empirical-proof.md` |
| **Domain-Driven Design (DDD)** | Model-driven design | Ubiquitous Language, Bounded Context, tactical patterns | `docs/ddd-implementation-guide.md` |
| **Real-Time Systems** | Temporal correctness | Measurable constraints, terse ISRs, empirical timing | `docs/real-time-systems-guide.md` |
| **Reverse Engineering** | Knowledge recovery | Architecture recovery, domain modeling, validation | `docs/reverse-engineering-guide.md` |

## 🔗 How Methodologies Integrate

### Standards + XP: Agile Compliance

**Standards** provide the *what* (deliverables, traceability, verification), while **XP** provides the *how* (practices for delivery).

**Example**:
- **Standard (IEEE 29148)**: "Requirements shall be traced to design elements"
- **XP Practice**: Use GitHub Issues for traceability, integrate continuously, test before implementation

**Integration Points**:
```
Phase 02 (Requirements) ─┐
                         ├─> XP User Stories ─> GitHub Issues ─> TDD Tests
Phase 05 (Implementation)┘
```

### TDD + DDD: Behavior-Driven Domain Models

**TDD** ensures correctness through tests, while **DDD** ensures the code reflects the domain model.

**Example**:
```typescript
// 1. RED: Write failing test expressing domain behavior
describe('Order Aggregate (DDD)', () => {
  it('should reject order exceeding customer credit limit (domain rule)', () => {
    const customer = new Customer({ creditLimit: Money.of(1000) });
    const order = new Order(customer);
    
    expect(() => {
      order.addItem(new OrderItem({ price: Money.of(1500) }));
    }).toThrow(InsufficientCreditError);
  });
});

// 2. GREEN: Implement domain model to pass test
class Order {  // Aggregate Root (DDD)
  private items: OrderItem[] = [];
  
  addItem(item: OrderItem): void {
    // Domain rule (invariant enforcement)
    const newTotal = this.calculateTotal().add(item.price);
    if (newTotal.greaterThan(this.customer.creditLimit)) {
      throw new InsufficientCreditError();  // Domain exception
    }
    this.items.push(item);
  }
}

// 3. REFACTOR: Extract domain service if needed
```

**Integration Points**:
- Tests validate domain invariants (DDD business rules)
- Ubiquitous Language used in test names
- Aggregate boundaries enforced by tests
- Repository contracts verified by tests

### Real-Time + TDD: Temporal Correctness Testing

**Real-Time** adds *temporal* requirements, and **TDD** proves they're met through measurement.

**Example**:
```cpp
// 1. RED: Write test for temporal constraint
TEST(ControlLoop, MeetsHardDeadline) {
  ControlLoop control;
  
  const auto start = high_resolution_clock::now();
  control.update();  // Critical control algorithm
  const auto end = high_resolution_clock::now();
  
  const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  
  // Hard real-time: MUST meet deadline
  EXPECT_LT(duration, 100us) << "Exceeded hard deadline";
}

// 2. GREEN: Optimize implementation to pass
void ControlLoop::update() {
  // Terse ISR pattern (no blocking calls)
  const auto sensor_value = read_sensor_fast();
  const auto control_output = calculate_control_fast(sensor_value);
  write_output_fast(control_output);
}

// 3. REFACTOR: Extract to terse ISR + non-blocking task
```

**Integration Points**:
- Walking Skeleton proves architecture meets timing
- GPIO instrumentation provides empirical proof
- Tests verify both logic AND timing
- Spike solutions explore timing of unknown libraries

### DDD + Real-Time: Domain Constraints with Temporal Guarantees

**DDD** models domain constraints, while **Real-Time** adds temporal dimensions.

**Example**:
```typescript
// Domain concept: Emergency Stop (safety-critical)
class EmergencyStopButton {  // Domain Entity (DDD)
  private state: ButtonState;
  
  // Domain invariant + temporal constraint
  async press(): Promise<void> {
    // Temporal requirement: 5ms hard deadline
    const deadline = 5ms;
    
    // Domain rule: Must shutdown ALL motors
    const motors = await motorRepository.findAll();
    
    const start = now();
    await Promise.all(motors.map(m => m.shutdown()));
    const duration = now() - start;
    
    // Verify temporal correctness
    if (duration > deadline) {
      throw new SafetyCriticalTimingViolation();
    }
    
    this.state = ButtonState.PRESSED;
  }
}
```

**Integration Points**:
- Domain events trigger real-time actions
- Temporal constraints documented in Ubiquitous Language
- Priority classes map to domain criticality
- Real-time tests verify domain safety properties

### Reverse Engineering + TDD: Test-Driven Understanding

**Reverse Engineering** recovers design knowledge from code, while **TDD** proves understanding through tests.

**Example**:
```typescript
// 1. Analyze legacy code behavior
function legacyTaxCalculation(amount: number, customer: any): number {
  // Complex logic, unclear intent
  if (customer.flags && customer.flags.includes('EXEMPT')) {
    return 0;
  }
  return amount * 0.08;  // Hardcoded rate?
}

// 2. Write tests expressing hypothesized behavior
describe('Legacy Tax Calculation (Reverse Engineered)', () => {
  it('should return zero for tax-exempt customers', () => {
    const customer = { flags: ['EXEMPT'] };
    expect(legacyTaxCalculation(100, customer)).toBe(0);
  });
  
  it('should apply 8% tax for regular customers', () => {
    const customer = { flags: [] };
    expect(legacyTaxCalculation(100, customer)).toBe(8);
  });
});

// 3. Run tests to validate understanding
// 4. Refactor with confidence (tests as safety net)
class TaxCalculator {  // Refactored with explicit intent
  calculate(amount: Money, customer: Customer): Money {
    if (customer.isTaxExempt()) {
      return Money.zero();
    }
    return amount.multiply(0.08);
  }
}
```

**Integration Points**:
- Tests validate recovered requirements
- Refactoring reveals hidden domain concepts
- Knowledge crunching fills documentation gaps
- Comparison testing proves behavior equivalence

### Reverse Engineering + DDD: Domain Model Recovery

**Reverse Engineering** extracts domain concepts from code, while **DDD** provides modeling vocabulary.

**Example**:
```markdown
## Knowledge Crunching Session (Legacy System)

**Code Fragment**:
```typescript
// What domain concepts are hidden here?
function processOrder(customerId, items) {
  let total = items.reduce((sum, item) => sum + item.price * item.qty, 0);
  
  if (vipCustomers.includes(customerId)) {
    total *= 0.9;  // VIP discount
  }
  
  return total * 1.08;  // Tax
}
```

**Recovered Domain Concepts**:
- **Customer** (Entity): Identity = customerId
- **CustomerTier** (Value Object): VIP vs. Regular
- **LineItem** (Value Object): Product + Quantity + Price
- **DiscountPolicy** (Domain Service): Tier-based discounts
- **TaxCalculation** (Domain Service): Location-based tax
- **Order** (Aggregate Root): Manages items, discounts, tax

**Ubiquitous Language Updated**:
- "VIP customer" → CustomerTier.VIP
- "Process order" → Order.calculateTotal()
- "Apply discount" → DiscountPolicy.calculate()
```

**Integration Points**:
- Hidden concepts become explicit domain types
- Ubiquitous Language extracted from legacy code
- Bounded Contexts identified from module boundaries
- Tactical patterns applied during refactoring

## 📊 Methodology Application by Phase

### Phase 01: Stakeholder Requirements
- **Standards**: IEEE 29148 (Stakeholder Requirements)
- **DDD**: Knowledge Crunching sessions with domain experts
- **GitHub**: StR issues created
- **Output**: Business context, stakeholder needs

### Phase 02: Requirements Analysis
- **Standards**: IEEE 29148 (System Requirements)
- **XP**: User stories with acceptance criteria
- **DDD**: Ubiquitous Language glossary, Bounded Context identification
- **Real-Time**: Temporal requirements stated measurably
- **GitHub**: REQ-F/REQ-NF issues
- **Output**: Functional/non-functional requirements, domain model

### Phase 03: Architecture Design
- **Standards**: IEEE 42010 (Architecture)
- **DDD**: Context Map, Core Domain, Domain Layer isolation
- **Real-Time**: Time-frame architecture, priority classes
- **GitHub**: ADR, ARC-C, QA-SC issues
- **Output**: Architecture views, ADRs, component boundaries

### Phase 04: Detailed Design
- **Standards**: IEEE 1016 (Design Descriptions)
- **DDD**: Tactical patterns (Entity, Value Object, Aggregate, Repository, Factory, Domain Service)
- **Real-Time**: ISR design, runtime limits, bounded execution strategies
- **GitHub**: Design issues with pattern references
- **Output**: Component designs, interface specifications

### Phase 05: Implementation
- **Standards**: ISO/IEC/IEEE 12207, IEC 61508 (if safety-critical)
- **XP**: TDD (Red-Green-Refactor), Continuous Integration, Pair Programming, Simple Design
- **TDD**: Write tests BEFORE code (absolute rule), spike solutions, assertions
- **DDD**: Model-driven code, domain logic in domain layer
- **Real-Time**: Terse ISRs, non-blocking code, integer math, static polymorphism
- **GitHub**: PRs with `Fixes #N` links
- **Output**: Production code with >80% test coverage

### Phase 06: Integration
- **Standards**: ISO/IEC/IEEE 12207 (Integration)
- **XP**: Continuous Integration (multiple times daily)
- **Real-Time**: Timing validation across integrated components
- **GitHub**: Integration issues
- **Output**: Integrated system with automated deployment

### Phase 07: Verification & Validation
- **Standards**: IEEE 1012 (V&V)
- **TDD**: Automated test suite (unit, integration, e2e, acceptance)
- **Real-Time**: Empirical timing measurement (GPIO + oscilloscope)
- **GitHub**: TEST issues linked to requirements
- **Output**: Traceability matrix, test results, temporal compliance proof

### Phase 08: Transition
- **Standards**: ISO/IEC/IEEE 12207 (Transition)
- **DDD**: Ubiquitous Language in user documentation
- **Output**: Deployment, training, operational handoff

### Phase 09: Operation & Maintenance
- **Standards**: ISO/IEC/IEEE 12207 (Maintenance)
- **XP**: Refactoring, continuous improvement
- **Real-Time**: Runtime CPU load monitoring
- **Output**: Monitoring, incident response, maintenance logs

## 🎯 Practical Examples: Methodologies in Action

### Example 1: User Login Feature

**Phase 02 (Requirements)**:
```markdown
## User Story (XP)
As a registered user
I want to log in with email and password
So that I can access my account

## Acceptance Criteria (TDD targets)
- Given valid credentials, user is authenticated in <500ms (Real-Time)
- Given invalid credentials, error shown
- Given locked account, error shown

## Domain Concepts (DDD)
- User (Entity): identity = email
- Credentials (Value Object): email + password
- AuthenticationService (Domain Service): stateless authentication logic

## Temporal Constraint (Real-Time)
95% of login attempts shall complete in <500ms (soft real-time)

Traceability: #123 (StR-001: User Management)
```

**Phase 05 (Implementation - TDD)**:
```typescript
// 1. RED: Write failing test
describe('AuthenticationService', () => {
  it('should authenticate user with valid credentials in <500ms', async () => {
    const service = new AuthenticationService(mockUserRepo);
    const credentials = new Credentials('user@example.com', 'password123');
    
    const start = Date.now();
    const result = await service.authenticate(credentials);
    const duration = Date.now() - start;
    
    expect(result.isSuccess()).toBe(true);
    expect(duration).toBeLessThan(500);  // Temporal constraint
  });
});

// 2. GREEN: Implement to pass
class AuthenticationService {  // Domain Service (DDD)
  async authenticate(credentials: Credentials): Promise<Result<User>> {
    // Domain logic
    const user = await this.userRepository.findByEmail(credentials.email);
    
    if (!user || !user.verifyPassword(credentials.password)) {
      return Result.failure(new InvalidCredentialsError());
    }
    
    if (user.isLocked()) {
      return Result.failure(new AccountLockedError());
    }
    
    return Result.success(user);
  }
}

// 3. REFACTOR: Extract password verification
class User {  // Entity (DDD)
  verifyPassword(plaintext: string): boolean {
    return bcrypt.compareSync(plaintext, this.passwordHash);
  }
}
```

### Example 2: Motor Control Loop (Real-Time + DDD)

**Phase 02 (Requirements)**:
```markdown
## Temporal Requirement (Real-Time)
Motor control loop shall execute at exactly 10kHz (100µs period) with <10µs jitter

## Domain Concepts (DDD)
- Motor (Entity): identity = motor_id, speed setpoint
- ControlAlgorithm (Domain Service): PID controller
- MotorRepository (Repository): access motor configurations

## Boundary Context (DDD)
Control Subsystem (hard real-time) vs. Monitoring Subsystem (soft real-time)

Traceability: #456 (StR-CTRL-001: Motor Control)
```

**Phase 05 (Implementation - Real-Time TDD)**:
```cpp
// 1. RED: Test temporal constraint
TEST(MotorController, MeetsHardDeadline) {
  MotorController controller;
  
  // Measure 1000 iterations
  std::vector<std::chrono::microseconds> durations;
  for (int i = 0; i < 1000; ++i) {
    const auto start = high_resolution_clock::now();
    controller.update();
    const auto end = high_resolution_clock::now();
    durations.push_back(std::chrono::duration_cast<std::chrono::microseconds>(end - start));
  }
  
  // Hard real-time: ALL must meet deadline
  for (const auto& d : durations) {
    EXPECT_LT(d, 100us) << "Missed hard deadline";
  }
  
  // Verify jitter
  const auto [min, max] = std::minmax_element(durations.begin(), durations.end());
  const auto jitter = *max - *min;
  EXPECT_LT(jitter, 10us) << "Jitter exceeds spec";
}

// 2. GREEN: Implement terse control loop
class MotorController {
  // Domain entity (DDD)
  Motor motor_;
  
  // Real-time constraint: <100µs execution
  void update() {
    // Terse ISR pattern (no blocking, no heap allocation)
    const int16_t sensor_value = read_sensor_fast();  // <5µs
    const int16_t control_output = calculate_pid_fast(sensor_value);  // <50µs
    write_pwm_fast(control_output);  // <5µs
    // Total: ~60µs typical, <100µs worst-case
  }
  
private:
  // Integer math only (no FPU) for speed
  int16_t calculate_pid_fast(int16_t error) {
    // Fixed-point PID calculation
    static int32_t integral = 0;
    static int16_t prev_error = 0;
    
    integral += error;  // I term
    const int16_t derivative = error - prev_error;  // D term
    prev_error = error;
    
    // P + I + D with fixed-point coefficients
    return (Kp * error + Ki * integral + Kd * derivative) >> 8;
  }
};

// 3. REFACTOR: Extract domain service
class PIDController {  // Domain Service (DDD)
  int16_t calculate(int16_t error) { /* ... */ }
};
```

**Phase 07 (Verification - Empirical Proof)**:
```cpp
// GPIO instrumentation for oscilloscope measurement
void MotorController::update() {
  GPIO_SET(DEBUG_PIN);  // Pin HIGH at entry
  
  // Control algorithm here
  
  GPIO_CLEAR(DEBUG_PIN);  // Pin LOW at exit
}

// Oscilloscope results:
// Pulse width: 58-62µs (typical), max 68µs
// Jitter: 4µs
// Frequency: 10.000kHz ± 0.01%
// ✅ PASSES hard real-time requirements
```

## ✅ Integration Checklist

When starting a new project with this template:

### Setup Phase
- [ ] Review all five methodologies (Standards, XP, TDD, DDD, Real-Time)
- [ ] Identify which are applicable (e.g., real-time only if needed)
- [ ] Configure GitHub Issues with appropriate templates
- [ ] Set up CI/CD pipelines for continuous integration
- [ ] Establish team conventions for Ubiquitous Language

### Phase 01-02 (Requirements)
- [ ] Conduct Knowledge Crunching sessions (DDD)
- [ ] Create StR issues (Standards)
- [ ] Write user stories with acceptance criteria (XP)
- [ ] Define Ubiquitous Language glossary (DDD)
- [ ] State temporal requirements measurably (Real-Time)
- [ ] Create REQ-F/REQ-NF issues with traceability

### Phase 03-04 (Architecture/Design)
- [ ] Identify Bounded Contexts (DDD)
- [ ] Create Context Map (DDD)
- [ ] Define priority classes and runtime limits (Real-Time)
- [ ] Select time-frame architecture if hard real-time
- [ ] Create ADRs for major decisions (Standards)
- [ ] Apply tactical patterns (DDD)

### Phase 05 (Implementation)
- [ ] Set up TDD workflow (Red-Green-Refactor)
- [ ] Implement domain model in domain layer (DDD)
- [ ] Keep ISRs terse (<5µs/<50µs) (Real-Time)
- [ ] Use integer math if no FPU (Real-Time)
- [ ] Integrate continuously (multiple times daily) (XP)
- [ ] Maintain >80% test coverage (TDD)

### Phase 07 (Verification)
- [ ] Create TEST issues linked to requirements
- [ ] Run automated test suite
- [ ] Perform empirical timing measurement (Real-Time)
- [ ] Generate traceability matrix (Standards)
- [ ] Validate all temporal constraints met
- [ ] Document test results

## 🚀 Success Criteria

A project successfully integrating all methodologies will demonstrate:

✅ **Standards Compliance**: Meets all IEEE/ISO/IEC requirements, complete traceability  
✅ **XP Practices**: TDD in use, frequent integration, simple design  
✅ **Empirical Validation**: Tests prove correctness, spike solutions replace speculation  
✅ **Domain-Driven**: Code reflects domain model, Ubiquitous Language used consistently  
✅ **Temporal Correctness** (if applicable): All deadlines met, empirically measured  
✅ **Quality**: >80% test coverage, no critical bugs  
✅ **Maintainability**: Clear architecture, well-documented decisions, modular design  

## 📚 Documentation Index

| Guide | Purpose | Target Audience |
|-------|---------|-----------------|
| `.github/copilot-instructions.md` | Root framework instructions | All developers |
| `docs/lifecycle-guide.md` | 9-phase lifecycle overview | Project managers, leads |
| `docs/xp-practices.md` | Extreme Programming practices | Development teams |
| `docs/tdd-empirical-proof.md` | Test-Driven Development (1200+ lines) | Developers |
| `docs/tdd-quick-reference.md` | TDD daily reference (210 lines) | Developers (quick lookup) |
| `docs/ddd-implementation-guide.md` | Domain-Driven Design (800+ lines) | Architects, developers |
| `docs/real-time-systems-guide.md` | Real-Time programming (1200+ lines) | Embedded developers |
| `docs/github-issue-workflow.md` | Issue tracking and traceability | All team members |

## 🔗 Visual Integration Map

```
┌─────────────────────────────────────────────────────────────────┐
│                    STANDARDS (IEEE/ISO/IEC)                      │
│                   9-Phase Lifecycle Framework                    │
└───────────┬─────────────────────────────────────────────────────┘
            │
    ┌───────▼────────┐
    │   Phase 01-02  │  ◄─── DDD: Knowledge Crunching
    │  Requirements  │  ◄─── XP: User Stories
    └────────┬───────┘  ◄─── Real-Time: Measurable Constraints
             │          ◄─── GitHub: StR, REQ-F, REQ-NF Issues
    ┌────────▼───────┐
    │   Phase 03-04  │  ◄─── DDD: Bounded Context, Tactical Patterns
    │ Architecture   │  ◄─── Real-Time: Time-Frame Architecture
    │ & Design       │  ◄─── GitHub: ADR, ARC-C Issues
    └────────┬───────┘
    ┌────────▼───────┐
    │   Phase 05     │  ◄─── TDD: Red-Green-Refactor (ABSOLUTE RULE)
    │ Implementation │  ◄─── XP: Continuous Integration, Simple Design
    └────────┬───────┘  ◄─── DDD: Model-Driven Code
             │          ◄─── Real-Time: Terse ISRs, Integer Math
    ┌────────▼───────┐  ◄─── GitHub: PRs with Fixes #N
    │   Phase 06-07  │
    │  Integration   │  ◄─── XP: Continuous Integration
    │ & Verification │  ◄─── TDD: Automated Tests
    └────────┬───────┘  ◄─── Real-Time: Empirical Timing (GPIO)
             │          ◄─── GitHub: TEST Issues
    ┌────────▼───────┐
    │  Phase 08-09   │  ◄─── Standards: Deployment, Maintenance
    │  Transition &  │  ◄─── XP: Continuous Improvement
    │   Operation    │  ◄─── Real-Time: Runtime Monitoring
    └────────────────┘
```

## 🎓 Learning Path

For teams new to this template:

### Week 1: Foundations
- Read root copilot instructions
- Review lifecycle guide
- Understand GitHub Issues workflow
- Learn XP core practices

### Week 2: Development Practices
- Study TDD empirical proof guide
- Practice Red-Green-Refactor cycle
- Complete TDD katas using quick reference
- Understand spike solutions and walking skeleton

### Week 3: Domain Modeling
- Study DDD implementation guide
- Practice Ubiquitous Language creation
- Identify Bounded Contexts in sample project
- Implement tactical patterns (Entity, Value Object, Aggregate)

### Week 4: Real-Time (If Applicable)
- Study real-time systems guide
- Understand hard vs. soft real-time
- Practice terse ISR patterns
- Learn empirical timing measurement

### Week 5+: Production Project
- Apply all methodologies to real project
- Maintain continuous integration
- Track all work via GitHub Issues
- Prove requirements through tests and measurement

---

**Remember**: These methodologies are **complementary, not competing**. Standards provide structure, XP provides practices, TDD provides proof, DDD provides clarity, and Real-Time provides temporal guarantees. Together, they create a robust framework for building reliable, maintainable software! 🚀
