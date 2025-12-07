---
description: "Phase 05 guidance for implementation following ISO/IEC/IEEE 12207:2017. Core XP practices: Test-Driven Development (TDD), pair programming, continuous integration, and refactoring."
applyTo: "05-implementation/**"
---

# Phase 05: Implementation (Construction)

**Standards**: ISO/IEC/IEEE 12207:2017 (Implementation Process)  
**XP Integration**: Test-Driven Development (TDD), Pair Programming, Continuous Integration, Refactoring

## 🎯 Phase Objectives

1. Implement design specifications as working code
2. Apply Test-Driven Development (Red-Green-Refactor)
3. Write clean, maintainable, tested code
4. Perform continuous integration
5. Practice pair programming and collective ownership
6. Refactor continuously to improve design

## 📋 Code Traceability to GitHub Issues

### ⭐ PRIMARY: Link Code to GitHub Issues

**Every source file and test MUST reference the GitHub Issues it implements or verifies.**

#### Code Header Traceability (Required)

Each source file MUST include a top-of-file comment block with GitHub Issue links.

**Example (TypeScript/JavaScript)**:
```typescript
/**
 * User Service - Handles user authentication and profile management
 * 
 * @module UserService
 * @implements #45 REQ-F-AUTH-001: User Login
 * @implements #46 REQ-NF-SECU-002: Session Security
 * @architecture #78 ADR-SECU-001: JWT Authentication
 * @architecture #79 ARC-C-AUTH: User Authentication Service
 * @verifiedBy #120 TEST-AUTH-LOGIN
 * 
 * @see https://github.com/zarfld/copilot-instructions-template/issues/45
 * @see https://github.com/zarfld/copilot-instructions-template/issues/78
 */
export class UserService {
  // Implementation
}
```

**Example (Python)**:
```python
"""
User Service - Handles user authentication and profile management

Implements: #45 REQ-F-AUTH-001: User Login
Implements: #46 REQ-NF-SECU-002: Session Security
Architecture: #78 ADR-SECU-001: JWT Authentication
Architecture: #79 ARC-C-AUTH: User Authentication Service
Verified by: #120 TEST-AUTH-LOGIN

See: https://github.com/zarfld/copilot-instructions-template/issues/45
See: https://github.com/zarfld/copilot-instructions-template/issues/78
"""

class UserService:
    """User authentication and profile management service."""
    pass
```

#### Test Traceability (Required)

Every test MUST reference the requirement(s) it verifies using `@verifies` or `Verifies:` syntax.

**Example (Jest/TypeScript)**:
```typescript
describe('User Login (Verifies #45)', () => {
  /**
   * @verifies #45 REQ-F-AUTH-001: User Login
   * @scenario Given valid credentials, When user logs in, Then auth token is returned
   */
  it('should authenticate user with valid credentials', async () => {
    // Arrange
    const credentials = { email: 'user@example.com', password: 'SecurePass123!' };
    
    // Act
    const result = await userService.login(credentials);
    
    // Assert
    expect(result.accessToken).toBeDefined();
    expect(result.refreshToken).toBeDefined();
  });
  
  /**
   * @verifies #45 REQ-F-AUTH-001: User Login (error handling)
   * @scenario Given invalid password, When user logs in, Then error is returned
   */
  it('should reject login with invalid password', async () => {
    const credentials = { email: 'user@example.com', password: 'wrong' };
    
    await expect(userService.login(credentials))
      .rejects.toThrow('Invalid credentials');
  });
});
```

**Example (pytest/Python)**:
```python
"""
Test User Login functionality

Verifies: #45 REQ-F-AUTH-001: User Login
Test Suite: TEST-AUTH-LOGIN
Priority: P0 (Critical)
"""

def test_user_login_with_valid_credentials():
    """
    Verifies: #45 REQ-F-AUTH-001: User Login
    Scenario: Given valid credentials, When user logs in, Then auth token is returned
    """
    # Arrange
    credentials = {'email': 'user@example.com', 'password': 'SecurePass123!'}
    
    # Act
    result = user_service.login(credentials)
    
    # Assert
    assert result['accessToken'] is not None
    assert result['refreshToken'] is not None

def test_user_login_with_invalid_password():
    """
    Verifies: #45 REQ-F-AUTH-001: User Login (error handling)
    Scenario: Given invalid password, When user logs in, Then error is returned
    """
    credentials = {'email': 'user@example.com', 'password': 'wrong'}
    
    with pytest.raises(InvalidCredentialsError):
        user_service.login(credentials)
```

#### Pull Request Workflow (Required)

**Every PR MUST link to the implementing issue(s) using `Fixes` or `Implements` keywords.**

**PR Title Format**:
```
feat(auth): Implement user login with JWT (#45)
```

**PR Description Template**:
```markdown
## Description
Implements user authentication with JWT tokens as specified in requirements and architecture decisions.

## Related Issues
Fixes #45 (REQ-F-AUTH-001: User Login)
Implements #46 (REQ-NF-SECU-002: Session Security)
Part of #78 (ADR-SECU-001: JWT Authentication)

## Implementation Details
- Added UserService with login/logout methods
- Integrated jsonwebtoken library for JWT generation
- Added bcrypt for password hashing
- Implemented refresh token mechanism

## Testing
- ✅ Unit tests: 15 tests added, all passing
- ✅ Integration tests: 8 tests added, all passing
- ✅ Coverage: 92% lines, 88% branches
- ✅ Manual testing: Tested in dev environment

## Traceability
- **Requirements**: #45, #46
- **Architecture**: #78, #79
- **Tests**: #120 (TEST-AUTH-LOGIN)
- **Documentation**: Updated API docs

## Checklist
- [x] Code follows project style guide
- [x] All tests pass locally
- [x] Coverage meets threshold (≥80%)
- [x] Traceability links added to code
- [x] Documentation updated
- [x] No new lint/type errors
- [x] Reviewed own code
```

**Commit Message Format**:
```bash
feat(auth): Implement JWT-based user login (#45)

- Add UserService with login/logout methods
- Integrate jsonwebtoken and bcrypt libraries
- Implement refresh token mechanism
- Add comprehensive unit and integration tests

Implements: #45, #46
Architecture: #78
Verified by: #120

Breaking changes: None
```

**GitHub Actions Validation**:
The CI workflow automatically:
- ✅ Validates that PR links to at least one issue
- ✅ Checks that linked issues exist and are open
- ✅ Verifies all tests pass
- ✅ Checks code coverage (≥80%)
- ✅ Runs lint and type checks
- ✅ Comments on PR with traceability summary

#### Test Structure and Naming (Required)

- **Unit tests**: Co-locate with code or place under `tests/unit/**` using `<module>.spec.(ts|js|py)` naming
- **Integration tests**: Under `tests/integration/**` using `<feature>.int.spec.(ts|js|py)` naming
- Each test file MUST include a header block with GitHub Issue links (`Verifies: #N`)
- Create TEST issues for complex test suites that verify multiple requirements

#### CI Quality Gates (Required)

- **Lint and type check**: Must pass on every push and PR
- **Tests**: 100% of unit tests must pass; integration tests must pass for changed areas
- **Coverage threshold**: ≥ 80% lines/branches for changed code; fail build if below
- **Issue links**: PR must link to issues using `Fixes` or `Implements` keywords
- **Fast feedback**: Fix broken builds immediately (target ≤ 10 minutes)

4) Reliability Hooks (alignment with IEEE 1633)

- Emit structured logs and metrics necessary for reliability evidence (e.g., error counts, retry counts, circuit-breaker opens) to support Phase 06/07 data collection.
- Provide feature flags or configuration to enable fault injection in non-prod environments.
- Avoid swallowing exceptions; propagate with context to support failure analysis.

ENFORCEMENT:
- PRs without code header traceability will be rejected.
- Test files must follow naming/location conventions and include traceability.
- CI must block merges if lint/typecheck/tests/coverage gates fail.
- Reliability hooks are required for components whose failures impact user-visible reliability targets.

## 📋 ISO/IEC/IEEE 12207:2017 Compliance

### Implementation Process Activities

1. **Software Construction**
   - Implement software units
   - Develop unit tests
   - Update documentation

2. **Software Unit Verification**
   - Execute unit tests
   - Verify against design
   - Fix defects

3. **Software Integration**
   - Integrate units into components
   - Verify interfaces
   - Resolve integration issues

## 🎨 XP Core Practices for Implementation

### 1. Test-Driven Development (TDD)

**Red-Green-Refactor Cycle**:

```
🔴 RED: Write a failing test
  ↓
🟢 GREEN: Write minimal code to pass
  ↓
🔵 REFACTOR: Improve design while keeping tests green
  ↓
Repeat
```

**Example TDD Flow**:
```typescript
// 1. RED: Write failing test first
describe('UserService', () => {
  it('should create a new user with valid data', async () => {
    // Arrange
    const userData = { username: 'john', email: 'john@example.com' };
    const mockRepository = createMockRepository();
    const service = new UserService(mockRepository);
    
    // Act
    const user = await service.createUser(userData);
    
    // Assert
    expect(user).toBeDefined();
    expect(user.username).toBe('john');
    expect(mockRepository.save).toHaveBeenCalledWith(expect.objectContaining(userData));
  });
});

// 2. GREEN: Write minimal implementation to pass
class UserService {
  constructor(private repository: IUserRepository) {}
  
  async createUser(userData: CreateUserData): Promise<User> {
    const user = new User(userData);
    return await this.repository.save(user);
  }
}

// 3. REFACTOR: Improve design
class UserService {
  constructor(
    private repository: IUserRepository,
    private validator: IValidator,
    private logger: ILogger
  ) {}
  
  async createUser(userData: CreateUserData): Promise<User> {
    this.logger.info('Creating user', { username: userData.username });
    
    // Validate input
    await this.validator.validate(userData, CreateUserSchema);
    
    // Create and save user
    const user = new User(userData);
    const savedUser = await this.repository.save(user);
    
    this.logger.info('User created', { userId: savedUser.id });
    return savedUser;
  }
}
```

### 2. Pair Programming

**Roles**:
- **Driver**: Types the code
- **Navigator**: Reviews, thinks strategically, suggests improvements

**Benefits**:
- Continuous code review
- Knowledge sharing
- Better design decisions
- Fewer defects

**Best Practices**:
- Switch roles every 30 minutes
- Communicate constantly
- Respect each other
- Take breaks together

### 3. Continuous Integration

**CI Practice**:
```yaml
# .github/workflows/ci.yml
name: Continuous Integration

on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Setup Node.js
        uses: actions/setup-node@v3
        with:
          node-version: '18'
      
      - name: Install dependencies
        run: npm ci
      
      - name: Run linter
        run: npm run lint
      
      - name: Run tests
        run: npm test -- --coverage
      
      - name: Check coverage
        run: |
          if [ $(grep -oP '\d+(?=%)' coverage/coverage-summary.json | head -1) -lt 80 ]; then
            echo "Coverage below 80%"
            exit 1
          fi
```

**CI Rules**:
- Integrate multiple times per day
- Run all tests before committing
- Fix broken builds immediately (within 10 minutes)
- Never commit on a broken build

### 4. Collective Code Ownership

**Principles**:
- Anyone can modify any code
- No "my code" or "your code"
- Team is responsible for quality
- Share knowledge through pairing

**Practices**:
- Consistent coding standards
- Comprehensive tests protect against breakage
- Code reviews for all changes
- Pair rotation

### 5. Coding Standards

**Enforce with tools**:
```json
// .eslintrc.json
{
  "extends": ["eslint:recommended", "plugin:@typescript-eslint/recommended"],
  "rules": {
    "max-lines": ["error", 300],
    "max-lines-per-function": ["error", 50],
    "complexity": ["error", 10],
    "max-depth": ["error", 3],
    "max-params": ["error", 4]
  }
}
```

### 6. Refactoring

**Continuous Refactoring**:
- Refactor when you see code smells
- Keep tests green while refactoring
- Small, safe refactorings
- Leave code better than you found it (Boy Scout Rule)

**Common Refactorings**:
```typescript
// Before: Long method
class OrderProcessor {
  processOrder(order: Order) {
    // Validate order (20 lines)
    // Calculate prices (30 lines)
    // Apply discounts (25 lines)
    // Process payment (40 lines)
    // Send notifications (15 lines)
  }
}

// After: Extract methods
class OrderProcessor {
  processOrder(order: Order) {
    this.validateOrder(order);
    const total = this.calculateTotal(order);
    const discounted = this.applyDiscounts(total, order);
    this.processPayment(discounted, order);
    this.sendNotifications(order);
  }
  
  private validateOrder(order: Order): void { /* ... */ }
  private calculateTotal(order: Order): number { /* ... */ }
  private applyDiscounts(total: number, order: Order): number { /* ... */ }
  private processPayment(amount: number, order: Order): void { /* ... */ }
  private sendNotifications(order: Order): void { /* ... */ }
}
```

## 📝 Implementation Guidelines

### File Organization

```
src/
├── domain/                  # Domain models (business logic)
│   ├── entities/           # Domain entities
│   ├── value-objects/      # Value objects
│   └── services/           # Domain services
├── application/            # Application services (use cases)
│   ├── commands/           # Command handlers
│   ├── queries/            # Query handlers
│   └── dtos/               # Data transfer objects
├── infrastructure/         # External concerns
│   ├── database/           # Database implementation
│   ├── http/               # HTTP clients
│   └── messaging/          # Message queues
└── presentation/           # User interface
    ├── controllers/        # API controllers
    ├── views/              # UI views
    └── middleware/         # Request/response middleware

tests/
├── unit/                   # Unit tests (co-located preferred)
├── integration/            # Integration tests
└── fixtures/               # Test data and mocks
```

### Code Quality Standards

#### 1. SOLID Principles

**Single Responsibility Principle (SRP)**:
```typescript
// Bad: Class has multiple responsibilities
class User {
  save() { /* database logic */ }
  sendEmail() { /* email logic */ }
  validate() { /* validation logic */ }
}

// Good: Each class has single responsibility
class User {
  // Only domain logic
}

class UserRepository {
  save(user: User) { /* database logic */ }
}

class EmailService {
  sendWelcomeEmail(user: User) { /* email logic */ }
}

class UserValidator {
  validate(user: User) { /* validation logic */ }
}
```

**Open/Closed Principle (OCP)**:
```typescript
// Open for extension, closed for modification
interface PaymentProcessor {
  process(amount: number): Promise<void>;
}

class CreditCardProcessor implements PaymentProcessor {
  async process(amount: number): Promise<void> { /* ... */ }
}

class PayPalProcessor implements PaymentProcessor {
  async process(amount: number): Promise<void> { /* ... */ }
}

// Adding new payment method doesn't modify existing code
class CryptoProcessor implements PaymentProcessor {
  async process(amount: number): Promise<void> { /* ... */ }
}
```

**Liskov Substitution Principle (LSP)**: Subtypes must be substitutable for base types

**Interface Segregation Principle (ISP)**: Many specific interfaces > one general interface

**Dependency Inversion Principle (DIP)**: Depend on abstractions, not concretions

#### 2. Clean Code Practices

**Meaningful Names**:
```typescript
// Bad
const d = new Date();
function getData(u) { }

// Good
const currentDate = new Date();
function getUserById(userId: string) { }
```

**Small Functions**:
```typescript
// Bad: Function does too much
function processUserRegistration(userData) {
  // Validate (20 lines)
  // Hash password (5 lines)
  // Save to database (10 lines)
  // Send email (15 lines)
  // Log audit (5 lines)
}

// Good: Small, focused functions
function processUserRegistration(userData: UserData): Promise<User> {
  const validatedData = validateUserData(userData);
  const user = createUser(validatedData);
  const savedUser = await saveUser(user);
  await sendWelcomeEmail(savedUser);
  logUserCreation(savedUser);
  return savedUser;
}
```

**DRY (Don't Repeat Yourself)**:
```typescript
// Bad: Duplication
function calculatePriceForAdult(basePrice) {
  return basePrice * 1.1 + basePrice * 0.15;
}

function calculatePriceForChild(basePrice) {
  return basePrice * 0.5 * 1.1 + basePrice * 0.5 * 0.15;
}

// Good: Extract common logic
function calculatePrice(basePrice: number, multiplier: number): number {
  const withTax = basePrice * multiplier * 1.1;
  const withServiceFee = withTax + (basePrice * multiplier * 0.15);
  return withServiceFee;
}

function calculatePriceForAdult(basePrice: number): number {
  return calculatePrice(basePrice, 1.0);
}

function calculatePriceForChild(basePrice: number): number {
  return calculatePrice(basePrice, 0.5);
}
```

#### 3. Error Handling

```typescript
// Define error hierarchy
class ApplicationError extends Error {
  constructor(
    message: string,
    public readonly code: string,
    public readonly statusCode: number = 500
  ) {
    super(message);
    this.name = this.constructor.name;
    Error.captureStackTrace(this, this.constructor);
  }
}

class ValidationError extends ApplicationError {
  constructor(message: string, public readonly field?: string) {
    super(message, 'VALIDATION_ERROR', 400);
  }
}

class NotFoundError extends ApplicationError {
  constructor(resource: string, id: string) {
    super(`${resource} with id ${id} not found`, 'NOT_FOUND', 404);
  }
}

// Use in code
async function getUser(userId: string): Promise<User> {
  if (!isValidUuid(userId)) {
    throw new ValidationError('Invalid user ID format', 'userId');
  }
  
  const user = await userRepository.findById(userId);
  
  if (!user) {
    throw new NotFoundError('User', userId);
  }
  
  return user;
}

// Global error handler
app.use((error: Error, req: Request, res: Response, next: NextFunction) => {
  if (error instanceof ApplicationError) {
    return res.status(error.statusCode).json({
      error: error.code,
      message: error.message,
      ...(error instanceof ValidationError && { field: error.field })
    });
  }
  
  logger.error('Unhandled error', error);
  return res.status(500).json({
    error: 'INTERNAL_ERROR',
    message: 'An unexpected error occurred'
  });
});
```

#### 4. Logging

```typescript
interface ILogger {
  debug(message: string, context?: object): void;
  info(message: string, context?: object): void;
  warn(message: string, context?: object): void;
  error(message: string, error: Error, context?: object): void;
}

class UserService {
  constructor(
    private repository: IUserRepository,
    private logger: ILogger
  ) {}
  
  async createUser(userData: CreateUserData): Promise<User> {
    this.logger.info('Creating user', { username: userData.username });
    
    try {
      const user = await this.repository.save(new User(userData));
      this.logger.info('User created successfully', { userId: user.id });
      return user;
    } catch (error) {
      this.logger.error('Failed to create user', error as Error, { userData });
      throw error;
    }
  }
}
```

## 🚨 Critical Requirements for This Phase

### Always Do (XP Practices)
✅ **Write tests first (TDD)** - Red → Green → Refactor (write failing test BEFORE any code)  
✅ **Integrate continuously** - Multiple times per day, no code unintegrated >couple hours  
✅ **Pair program** - Write ALL production code in pairs  
✅ **Refactor mercilessly** - Refactor early, refactor often (daily activity)  
✅ **Follow coding standards** - Use linters and formatters  
✅ **Collective ownership** - Anyone can modify any code to fix/improve  
✅ **Keep it simple** - YAGNI, assume simplicity, simplest design that works  
✅ **Run all tests** - Before every commit; all tests must run flawlessly  
✅ **Test thoroughly** - Test everything that could possibly break  
✅ **Deliver frequently** - Small releases on very short cycles  
✅ **Take small steps** - Always deliberate, check for feedback before proceeding  

### Always Do (Standards Compliance & Best Practices)
✅ Implement per design specifications  
✅ Trace code to design elements  
✅ Document public APIs (but build documentation in, don't bolt it on)  
✅ Handle all error cases (analyze all errors, don't assume they can't happen)  
✅ Log significant events (structured logs for reliability evidence)  
✅ Validate all inputs  
✅ Maintain >80% test coverage  
✅ Use assertions (preconditions, postconditions, invariants) to document interfaces  
✅ Use ubiquitous language (domain vocabulary) consistently  
✅ Eliminate duplication (DRY: single authoritative representation)  
✅ Write intention-revealing code (clear names, self-documenting)  
✅ Adhere to SOLID principles  
✅ Test assumptions; prove them, don't assume  
✅ Automate everything (operations, tests, deployment)  
✅ Program deliberately, not by coincidence  
✅ Communicate clearly (conversation is preferred form)  

### Never Do
❌ Write code without tests (breaks TDD) - **"Test Later" means "Test Never"**  
❌ Let time pressure cause you to skip tests  
❌ Commit on broken build  
❌ Skip refactoring ("we'll do it later")  
❌ Create long methods (>50 lines)  
❌ Create complex methods (cyclomatic complexity >10)  
❌ Ignore code smells  
❌ Skip error handling  
❌ Hard-code configuration  
❌ Build for tomorrow / gold plate (implement only what's needed today)  
❌ Duplicate logic (violate DRY)  
❌ Run on autopilot; constantly think critically  
❌ Write comments that paraphrase code (explain "why", not "how")  
❌ Catch and re-raise exceptions unnecessarily  
❌ Use global data or Singletons as globals  
❌ Test code from others (unless you distrust it)  
❌ Store secrets, API keys, or credentials in source code  
❌ Produce documents that aren't being actively used  
❌ Leave code unintegrated longer than a few hours  
❌ Become attached to your own ideas (be ready to replace them)  

## 📊 Code Quality Metrics

### Target Metrics
- **Test Coverage**: >80% (unit tests)
- **Cyclomatic Complexity**: <10 per method
- **Method Length**: <50 lines
- **Class Length**: <300 lines
- **Method Parameters**: <4 parameters
- **Maintainability Index**: >75
- **Code Duplication**: <3%

### Quality Gates
```yaml
# quality-gates.yml
coverage:
  minimum: 80
  
complexity:
  maximum: 10
  
code-smells:
  maximum: 0
  
security-hotspots:
  maximum: 0
  
duplications:
  maximum: 3
```

## � Reliability Engineering Activities (IEEE 1633)

### 1. Software Reliability Program Plan (SRPP) Review

**Prompt**: Use `.github/prompts/reliability-plan-create.prompt.md`

At the start of Phase 05, review the SRPP created in Phases 01-02:
- Review reliability objectives (MTBF targets, failure rate limits)
- Review reliability allocation per component
- Understand quality gates for this phase:
  - **Defect Discovery Rate**: Must be < [X] defects/KLOC (from SRPP Section 4.2)
  - **Estimated MTBF**: Track prediction vs actual (from SRPP Section 6)

**Deliverable**: Updated SRPP Section 4.2 (Phase 05 activities status)

### 2. Operational Profile (OP) Creation/Refinement

**Prompt**: Use `.github/prompts/operational-profile-create.prompt.md`

During Phase 05, create or refine the Operational Profile:
- **User Classes**: Identify who will use the software (roles, skill levels, usage %)
- **Operations**: List all operations users perform (hierarchical: Level 1-3)
- **Mission Profiles**: Define typical usage sessions (sequences of operations)
- **MCUM (Markov Chain Usage Model)**: Create state machine representing user flows
  - States: S-000 (Idle), S-001 (Authenticated), S-002 (Editing), etc.
  - Transitions: T-001 (Login), T-002 (Open Document), etc.
  - Probabilities: Annotate transitions with usage frequencies
- **Usage Distribution**: Apply 80-20 rule (top 20% operations = 80% usage)

**Why Now**: OP is needed before reliability testing (Phase 06-07) to generate usage-driven tests

**Deliverable**: 
- Complete Operational Profile document (`docs/reliability/operational-profile-[Component]-[Version].md`)
- MCUM state machine (states, transitions, probabilities)
- Usage frequency table (operations ranked by frequency)

**Location**: `spec-kit-templates/operational-profile.md` (template)

### 3. Software Failure Modes Effects Analysis (SFMEA) - Initial

**Prompt**: Use `.github/prompts/sfmea-create.prompt.md`

At end of Phase 05 (before integration), perform SFMEA on critical components:
- **Identify Failure Modes**: Use IEEE 1633 Annex A categories:
  - Faulty Data (wrong value, null pointer, buffer overflow)
  - Faulty Timing (timeout, race condition, deadlock)
  - Faulty Sequencing (steps out of order, step skipped)
  - Faulty Error Handling (error not detected, incorrect recovery)
  - Faulty Logic (incorrect algorithm, off-by-one)
- **Root Cause Analysis**: Identify why each failure mode could occur
- **Risk Assessment (RPN)**: Calculate RPN = Severity × Likelihood × Detectability
- **Mitigation Actions**: Define actions for high-RPN items (RPN ≥ 200)
- **Critical Items List (CIL)**: Track all items with RPN ≥ 200 (must be 100% complete before release)

**Why Now**: Early SFMEA (on design/early code) prevents defects before integration

**Deliverable**: 
- SFMEA document for critical components
- Critical Items List (CIL) with mitigation actions
- Design updates based on SFMEA findings

**Location**: Create in `docs/reliability/` or `04-design/` (if design-level SFMEA)

### 4. Code-Level Reliability Practices

**Implement Reliability Mechanisms**:
```typescript
// 1. Structured Logging (for reliability evidence)
logger.info('Operation completed', {
  operation: 'createUser',
  duration: 145, // ms
  success: true,
  userId: user.id
});

logger.error('Operation failed', {
  operation: 'createUser',
  error: error.message,
  errorCode: 'VALIDATION_ERROR',
  duration: 50,
  success: false
});

// 2. Failure Detection and Reporting
class OperationResult<T> {
  constructor(
    public readonly success: boolean,
    public readonly data?: T,
    public readonly error?: ApplicationError
  ) {}
  
  static success<T>(data: T): OperationResult<T> {
    return new OperationResult(true, data);
  }
  
  static failure<T>(error: ApplicationError): OperationResult<T> {
    return new OperationResult(false, undefined, error);
  }
}

// 3. Graceful Degradation
async function getUserProfile(userId: string): Promise<UserProfile> {
  try {
    // Try primary data source
    return await primaryDatabase.getUser(userId);
  } catch (error) {
    logger.warn('Primary database unavailable, using cache', { userId });
    
    // Fallback to cache
    try {
      return await cache.get(`user:${userId}`);
    } catch (cacheError) {
      logger.error('Cache also unavailable', { userId });
      
      // Return minimal profile
      return { id: userId, name: 'Unknown', status: 'degraded' };
    }
  }
}

// 4. Circuit Breaker Pattern (prevent cascading failures)
class CircuitBreaker {
  private failureCount = 0;
  private state: 'CLOSED' | 'OPEN' | 'HALF_OPEN' = 'CLOSED';
  private lastFailureTime?: Date;
  
  async execute<T>(operation: () => Promise<T>): Promise<T> {
    if (this.state === 'OPEN') {
      if (this.shouldAttemptReset()) {
        this.state = 'HALF_OPEN';
      } else {
        throw new Error('Circuit breaker is OPEN');
      }
    }
    
    try {
      const result = await operation();
      this.onSuccess();
      return result;
    } catch (error) {
      this.onFailure();
      throw error;
    }
  }
  
  private onSuccess() {
    this.failureCount = 0;
    this.state = 'CLOSED';
  }
  
  private onFailure() {
    this.failureCount++;
    this.lastFailureTime = new Date();
    
    if (this.failureCount >= 5) {
      this.state = 'OPEN';
      logger.error('Circuit breaker opened', { failureCount: this.failureCount });
    }
  }
  
  private shouldAttemptReset(): boolean {
    if (!this.lastFailureTime) return false;
    const timeSinceFailure = Date.now() - this.lastFailureTime.getTime();
    return timeSinceFailure > 60000; // 60 seconds
  }
}
```

**Reliability Code Checklist**:
- [ ] Structured logging with operation success/failure
- [ ] Error codes defined (per FDSC - Failure Definition and Scoring Criteria)
- [ ] Graceful degradation for non-critical failures
- [ ] Circuit breakers for external dependencies
- [ ] Timeouts on all external calls
- [ ] Retry logic with exponential backoff
- [ ] Health check endpoints
- [ ] Metrics emission (operation counts, durations, errors)

### 5. Defect Tracking and Quality Gate Monitoring

**Track Defect Discovery Rate**:
```
Defect Discovery Rate = Total Defects Found / Code Size (KLOC)

Quality Gate Threshold (SRPP Section 4.2): < [X] defects/KLOC
```

**Monitor During Phase 05**:
- Track defects found in code reviews and unit tests
- Calculate defect density per component
- Alert if defect rate exceeds threshold (indicates design problems)

**Defect Classification** (per FDSC):
| Severity | FDSC Score | Description |
|----------|-----------|-------------|
| Critical | 10 | Safety hazard, mission failure, data loss |
| High | 7-9 | Major function degraded |
| Medium | 4-6 | Minor function impacted, workaround available |
| Low | 1-3 | Cosmetic, no functional impact |

**Deliverable**: Defect log with severity classifications (feeds into SRG modeling in Phase 06-07)

## �📊 Phase Entry Criteria

✅ Design specifications complete and approved  
✅ Development environment set up  
✅ CI/CD pipeline configured  
✅ Coding standards defined  
✅ Test framework configured  
✅ **Reliability entry criteria**:  
  ✅ SRPP reviewed (reliability objectives understood)  
  ✅ Operational Profile started (user classes and operations identified)  
  ✅ Quality gate thresholds defined (defect discovery rate)  

## 📊 Phase Exit Criteria

✅ All code implemented per design  
✅ Unit tests written for all code (TDD)  
✅ Test coverage >80%  
✅ All tests passing  
✅ Code reviewed and approved  
✅ Coding standards compliance verified  
✅ No critical bugs  
✅ Documentation updated  
✅ Code integrated into main branch  
✅ Traceability established (code → design)  
✅ **Reliability exit criteria**:  
  ✅ Operational Profile complete (MCUM with states/transitions/probabilities)  
  ✅ SFMEA performed on critical components  
  ✅ Critical Items List (CIL) created and mitigation actions defined  
  ✅ Defect discovery rate meets quality gate (< [X] defects/KLOC per SRPP)  
  ✅ Reliability mechanisms implemented (logging, error handling, graceful degradation)  
  ✅ Health check endpoints implemented  
  ✅ Metrics emission configured  

## 🔗 Traceability

```
DES-C-XXX (Design Component/Class)
  ↓
CODE-XXX (Implementation)
TEST-UNIT-XXX (Unit Tests)
  ↓
[Next Phase: Integration - INT-XXX]
```

## 📚 Standards and Resources

- **ISO/IEC/IEEE 12207:2017** - Implementation process
- **Test-Driven Development By Example** - Kent Beck
- **Clean Code** - Robert C. Martin
- **Refactoring** - Martin Fowler
- **XP Explained** - Kent Beck

## 🎯 Next Phase

Once this phase is complete, proceed to:
**Phase 06: Integration** (`06-integration/`)

---

**Remember**: 
- **TDD**: Tests first, always! Red → Green → Refactor
- **CI**: Integrate frequently, fix breaks immediately
- **Refactor**: Keep code clean continuously
- **Simple**: Do the simplest thing that could possibly work (YAGNI)
- **Quality**: >80% coverage, low complexity, no duplication

**The XP mantra**: "Make it work, make it right, make it fast" (in that order!)
