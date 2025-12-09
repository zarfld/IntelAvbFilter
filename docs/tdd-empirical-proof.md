# Test-Driven Development: Replacing Speculation with Proof

**Phase**: 05-Implementation (Core Practice)  
**Standards**: ISO/IEC/IEEE 12207:2017 (Implementation Process)  
**XP Integration**: TDD, Continuous Integration, Simple Design  
**Purpose**: Replace speculative assumptions with empirical validation through automated testing

## 🎯 The Speculation Problem

**Common Anti-Pattern**: Developers rely on **speculative assumptions** rather than rigorous investigation and proof, driven by:
- Desire for speed ("I know this will work")
- Lack of immediate, concrete feedback
- Assumption that "I've done this before"
- Fear of admitting uncertainty

**The Cost**: Bugs discovered late, incorrect implementations, brittle designs, technical debt.

**The Solution**: Mandate **continuous learning, experimentation, and empirical validation** before, during, and after coding.

## 📋 Core Principle: Software Development is a Learning Process

Development is **not** about executing a known plan. It **is** about:
- 🔍 **Discovering** what the system should do
- 🧪 **Proving** that it actually does it
- 📊 **Learning** from concrete feedback
- 🔄 **Adapting** based on empirical evidence

## I. Mandate Empirical Investigation and Upfront Learning

### 1. Challenge Assumptions Explicitly

**Rule**: Making assumptions is inevitable, but **every assumption must be verified**.

**Do NOT**:
- ❌ Assume code works because "we've done this before"
- ❌ Skip testing because "it's simple"
- ❌ Copy-paste code without understanding
- ❌ Trust documentation without verification

**DO**:
- ✅ **Prove it works** in the current context
- ✅ Test with specific data and boundary conditions
- ✅ Question every "this should work" statement
- ✅ Write a test to verify the assumption

**Example - Challenging an Assumption**:

```typescript
/**
 * Assumption: JavaScript's sort() sorts numbers correctly
 * Let's PROVE it with a test before using it
 */
describe('Array.sort() for numbers', () => {
  it('should sort numbers correctly (NOT strings)', () => {
    // Common mistake: sort() converts to strings by default
    const numbers = [1, 5, 10, 2];
    
    // This FAILS - proves assumption is wrong
    expect(numbers.sort()).toEqual([1, 2, 5, 10]); // Actually: [1, 10, 2, 5]
    
    // Correct approach after proving assumption wrong
    expect(numbers.sort((a, b) => a - b)).toEqual([1, 2, 5, 10]); // ✅ PASSES
  });
});
```

**Lesson**: The test **proved** the assumption was wrong **before** we used it in production code.

### 2. Use Spike Solutions for Risk Reduction

**Spike Solution**: Time-boxed experiment to investigate unknowns and reduce risk.

**Purpose**: 
- Learn **how** to solve a problem
- **Not** to solve the problem entirely
- Validate feasibility of technical choices
- Discover hidden complexity

**Implementation**:

```markdown
# Spike: Can we integrate with Legacy SOAP API?

**Goal**: Prove we can authenticate and fetch customer data
**Time Box**: 4 hours maximum
**Success Criteria**: 
- [ ] Successfully authenticate with test credentials
- [ ] Fetch sample customer record
- [ ] Parse XML response into TypeScript object

**NOT in Scope**:
- Production-ready error handling
- Performance optimization
- Complete API coverage

## Results (Document Findings)

### What We Learned:
1. Authentication requires OAuth 2.0, not Basic Auth (documentation was wrong)
2. XML schema has undocumented nested elements
3. Rate limiting: 10 requests/minute

### Recommended Approach:
- Use axios-oauth-client for authentication
- Use fast-xml-parser for parsing
- Implement request queue with rate limiting

### Code Artifacts:
- `spikes/legacy-api-integration/` (temporary, will be deleted)
- Actual implementation will be in `src/services/legacy-api/`

**Decision**: Proceed with integration (feasible but complex)
```

**Process**:
1. **Identify Risk**: "We don't know if this external API works as documented"
2. **Create Spike Branch**: `git checkout -b spike/legacy-api`
3. **Time Box**: Set timer for 4 hours
4. **Experiment**: Write throwaway code to learn
5. **Document Findings**: Capture what you learned
6. **Delete Spike Code**: `git branch -D spike/legacy-api`
7. **Implement Properly**: Start fresh with TDD using knowledge gained

### 3. Test Initial Architecture with Walking Skeleton

**Walking Skeleton**: Minimal end-to-end implementation that proves architecture works.

**Purpose**:
- Prove build, deploy, and runtime choices **work** together
- Validate architectural assumptions **early**
- Expose integration issues **before** significant feature work

**Implementation Steps**:

```bash
# Step 1: Define the simplest end-to-end scenario
# Example: "User can ping the API and get a response"

# Step 2: Implement bare minimum across all layers

# Database (empty schema)
CREATE TABLE health_check (
  id SERIAL PRIMARY KEY,
  timestamp TIMESTAMP DEFAULT NOW()
);

# Backend API (minimal endpoint)
// src/api/health.ts
export async function healthCheck(req, res) {
  const result = await db.query('SELECT NOW() as timestamp');
  res.json({ status: 'ok', db_connected: true, timestamp: result.rows[0].timestamp });
}

# Frontend (minimal UI)
// src/App.tsx
function App() {
  const [health, setHealth] = useState(null);
  
  useEffect(() => {
    fetch('/api/health')
      .then(r => r.json())
      .then(setHealth);
  }, []);
  
  return <div>Status: {health?.status}</div>;
}

# CI/CD Pipeline (minimal)
name: Deploy Walking Skeleton
on: [push]
jobs:
  deploy:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - run: npm install
      - run: npm test
      - run: npm run build
      - run: npm run deploy:staging

# Step 3: Prove it works end-to-end
# - Build succeeds
# - Tests run
# - Deploys to staging
# - Health check endpoint returns 200 OK
# - Database connection works
# - Frontend renders

# Step 4: Now build real features with confidence
```

**What This Proves**:
- ✅ Build tooling works (Webpack, TypeScript, etc.)
- ✅ Database connection works
- ✅ API framework works (Express, Fastify, etc.)
- ✅ Frontend framework works (React, Vue, etc.)
- ✅ CI/CD pipeline works
- ✅ Deployment process works

**When**: **Phase 06 (Integration)** - Before major feature development

### 4. Design Results in Experiments, Not Documents

**Traditional Approach** (Speculative):
```
Design Session → Detailed UML Diagrams → Implementation Begins → Problems Discovered
```

**Empirical Approach** (Proof-Based):
```
Design Session → Questions Identified → Experiments Created → Results Validated → Implementation Begins
```

**Example Design Session Output**:

```markdown
# Design Session: User Authentication Architecture

## Decisions Made:
1. Use JWT for session management
2. Use bcrypt for password hashing
3. Use PostgreSQL for user storage

## Questions Raised (TO BE PROVEN):
1. **Q**: Can bcrypt handle 10,000 password checks/second under load?
   - **Experiment**: Load test with bcrypt rounds=10
   - **Owner**: @alice
   - **Time Box**: 2 hours
   - **Success Criteria**: <100ms per hash, <10% CPU under load

2. **Q**: Will JWT token size cause header bloat with our permissions model?
   - **Experiment**: Generate sample JWT with realistic permissions
   - **Owner**: @bob
   - **Time Box**: 1 hour
   - **Success Criteria**: Token size <4KB (typical header limit)

3. **Q**: Can PostgreSQL handle our expected user table growth (1M users/year)?
   - **Experiment**: Load test with 10M user records
   - **Owner**: @charlie
   - **Time Box**: 4 hours
   - **Success Criteria**: <50ms for user lookup by email

## Next Steps:
- [ ] Complete experiments (by end of week)
- [ ] Review results in next design session
- [ ] Make final architectural decisions based on empirical data
```

**Key Point**: Don't assume these choices work - **PROVE THEM** before committing.

## II. Adopt Test-Driven Development (TDD)

### 5. The TDD Mantra (Sacred Law)

```
🔴 RED → 🟢 GREEN → 🔵 REFACTOR
```

**Red Phase** - Write a failing test:
```typescript
describe('Order.calculateTotal', () => {
  it('should sum line item prices', () => {
    // Arrange
    const order = new Order();
    order.addLine(new Product('Widget', Money.from(10)), Quantity.from(2));
    order.addLine(new Product('Gadget', Money.from(5)), Quantity.from(3));
    
    // Act
    const total = order.calculateTotal();
    
    // Assert
    expect(total.equals(Money.from(35))).toBe(true); // 2*10 + 3*5 = 35
  });
});
```

**Test Output**: ❌ `TypeError: order.addLine is not a function`

**Green Phase** - Make it pass (commit sins if necessary):
```typescript
export class Order {
  private lines: OrderLine[] = [];
  
  addLine(product: Product, quantity: Quantity): void {
    this.lines.push({ product, quantity });
  }
  
  calculateTotal(): Money {
    // Quick and dirty - just make test pass
    return this.lines.reduce(
      (sum, line) => sum.add(line.product.getPrice().multiply(line.quantity.getValue())),
      Money.zero()
    );
  }
}
```

**Test Output**: ✅ PASS

**Refactor Phase** - Clean up duplication:
```typescript
export class Order {
  private lines: OrderLine[] = [];
  
  addLine(product: Product, quantity: Quantity): void {
    const line = new OrderLine(product, quantity);
    this.lines.push(line);
  }
  
  calculateTotal(): Money {
    return this.lines.reduce(
      (sum, line) => sum.add(line.getSubtotal()), // Extract method
      Money.zero()
    );
  }
}

class OrderLine {
  constructor(
    private readonly product: Product,
    private readonly quantity: Quantity
  ) {}
  
  getSubtotal(): Money {
    return this.product.getPrice().multiply(this.quantity.getValue());
  }
}
```

**Test Output**: ✅ STILL PASSES (refactoring didn't break anything)

**Commit**: `git commit -m "feat: implement Order.calculateTotal (TDD)"`

### 6. Write Tests BEFORE Coding (Absolute Rule)

**The Golden Rule**: Write new code **ONLY** if an automated test has failed.

**Why**:
- Forces you to clarify intent **before** implementation
- Defines acceptance criteria **upfront**
- Turns testing into a **design** activity
- Prevents speculative "nice-to-have" code

**Process - Start with Asserts**:

```typescript
// STEP 1: Write the assert first (what success looks like)
it('should reject invalid email format', () => {
  // TODO: How do we want to express this?
  expect(() => Email.from('invalid-email')).toThrow(InvalidEmailError);
});

// STEP 2: What needs to exist for this assert to make sense?
// - Email class with static from() method
// - InvalidEmailError exception class

// STEP 3: Write the full test
it('should reject invalid email format', () => {
  // Arrange - nothing needed
  
  // Act & Assert
  expect(() => Email.from('invalid-email')).toThrow(InvalidEmailError);
  expect(() => Email.from('missing@domain')).toThrow(InvalidEmailError);
  expect(() => Email.from('@example.com')).toThrow(InvalidEmailError);
});

// STEP 4: Run test - watch it FAIL
// ❌ ReferenceError: Email is not defined

// STEP 5: NOW write minimal implementation to pass
```

**Anti-Pattern (Speculation)**:
```typescript
// ❌ DON'T DO THIS - Writing code without a failing test
export class Email {
  constructor(private value: string) {
    // Maybe we need validation? Let me add it just in case...
    if (!this.isValid(value)) {
      throw new Error('Invalid email');
    }
  }
  
  // Do we need this? Probably... let me add it anyway
  public getDomain(): string {
    return this.value.split('@')[1];
  }
  
  // What about this? Might be useful later...
  public getLocalPart(): string {
    return this.value.split('@')[0];
  }
  
  private isValid(email: string): boolean {
    // Copied regex from StackOverflow without understanding
    return /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email);
  }
}
```

**Correct Approach (TDD)**:
```typescript
// ✅ DO THIS - Let failing tests drive implementation
// Test 1: Basic validation
it('should reject invalid email format', () => {
  expect(() => Email.from('invalid')).toThrow(InvalidEmailError);
});

// Implement ONLY what test needs
export class Email {
  static from(value: string): Email {
    if (!value.includes('@')) {
      throw new InvalidEmailError('Email must contain @');
    }
    return new Email(value);
  }
  
  private constructor(private value: string) {}
}

// Test 2: Accept valid emails
it('should accept valid email format', () => {
  const email = Email.from('user@example.com');
  expect(email).toBeInstanceOf(Email);
});

// Enhance validation to pass test
static from(value: string): Email {
  if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(value)) {
    throw new InvalidEmailError('Invalid email format');
  }
  return new Email(value);
}

// Test 3: ONLY add getDomain() when a test needs it
it('should extract domain from email', () => {
  const email = Email.from('user@example.com');
  expect(email.getDomain()).toBe('example.com');
});

// NOW implement getDomain()
public getDomain(): string {
  return this.value.split('@')[1];
}
```

**Result**: Every line of code exists because a test demanded it.

### 7. Test Boundaries and Constraints with Assertions

**Assertions**: Runtime checks for conditions that "should never happen".

**Purpose**:
- Express correctness arguments explicitly
- Fail fast when assumptions are violated
- Document preconditions and invariants

**Implementation**:

```typescript
/**
 * Account Entity - Withdraw Operation
 * 
 * Preconditions (Assertions):
 * - Amount must be positive
 * - Account must have sufficient funds (including overdraft)
 * - Account must be active
 */
export class Account {
  withdraw(amount: Money): void {
    // Assertion 1: Boundary check
    this.assertPositiveAmount(amount);
    
    // Assertion 2: Business rule precondition
    this.assertSufficientFunds(amount);
    
    // Assertion 3: State precondition
    this.assertAccountActive();
    
    // Perform operation
    this.balance = this.balance.subtract(amount);
    this.addTransaction(TransactionType.WITHDRAWAL, amount);
    
    // Assertion 4: Invariant maintained
    this.assertBalanceIntegrity();
  }
  
  private assertPositiveAmount(amount: Money): void {
    if (amount.isNegativeOrZero()) {
      throw new InvalidOperationError(
        'Withdrawal amount must be positive',
        { amount: amount.getValue(), accountId: this.id }
      );
    }
  }
  
  private assertSufficientFunds(amount: Money): void {
    const availableBalance = this.balance.add(this.overdraftLimit);
    
    if (amount.isGreaterThan(availableBalance)) {
      throw new InsufficientFundsError(
        'Withdrawal exceeds available balance',
        {
          requested: amount.getValue(),
          available: availableBalance.getValue(),
          balance: this.balance.getValue(),
          overdraft: this.overdraftLimit.getValue()
        }
      );
    }
  }
  
  private assertAccountActive(): void {
    if (this.status !== AccountStatus.ACTIVE) {
      throw new AccountNotActiveError(
        'Cannot withdraw from inactive account',
        { accountId: this.id, status: this.status }
      );
    }
  }
  
  private assertBalanceIntegrity(): void {
    const calculatedBalance = this.calculateBalanceFromTransactions();
    
    if (!this.balance.equals(calculatedBalance)) {
      throw new InvariantViolationError(
        'Balance does not match transaction history',
        {
          storedBalance: this.balance.getValue(),
          calculatedBalance: calculatedBalance.getValue(),
          transactionCount: this.transactions.length
        }
      );
    }
  }
}
```

**Tests for Assertions**:

```typescript
describe('Account.withdraw assertions', () => {
  let account: Account;
  
  beforeEach(() => {
    account = Account.create(
      AccountId.generate(),
      Money.from(100),
      Money.from(50) // $50 overdraft
    );
  });
  
  it('should reject negative withdrawal amounts', () => {
    expect(() => account.withdraw(Money.from(-10)))
      .toThrow(InvalidOperationError);
  });
  
  it('should reject zero withdrawal amounts', () => {
    expect(() => account.withdraw(Money.zero()))
      .toThrow(InvalidOperationError);
  });
  
  it('should reject withdrawal exceeding available funds', () => {
    // Balance: $100, Overdraft: $50, Available: $150
    expect(() => account.withdraw(Money.from(151)))
      .toThrow(InsufficientFundsError);
  });
  
  it('should allow withdrawal within overdraft limit', () => {
    account.withdraw(Money.from(120)); // Uses $20 of overdraft
    expect(account.getBalance().equals(Money.from(-20))).toBe(true);
  });
  
  it('should reject withdrawal from suspended account', () => {
    account.suspend();
    
    expect(() => account.withdraw(Money.from(10)))
      .toThrow(AccountNotActiveError);
  });
  
  it('should maintain balance integrity after withdrawal', () => {
    account.withdraw(Money.from(30));
    
    // This test would fail if assertBalanceIntegrity() fails
    expect(account.getBalance().equals(Money.from(70))).toBe(true);
  });
});
```

### 8. Use Tests to Communicate Intent

**Tests as Living Documentation**: Tests express how the system **actually works**, not how we **hope** it works.

**Better than Design Documents**:
```typescript
// Instead of writing this in a Word doc:
// "The system shall calculate order totals by summing line item prices,
//  applying customer-specific discounts if applicable, and adding tax
//  based on shipping address."

// Write THIS (executable specification):
describe('Order Total Calculation', () => {
  it('should sum line item prices', () => {
    const order = new Order();
    order.addLine(product1, quantity2); // $10 * 2
    order.addLine(product2, quantity1); // $5 * 1
    
    expect(order.getSubtotal().equals(Money.from(25))).toBe(true);
  });
  
  it('should apply VIP customer discount', () => {
    const order = Order.createForVIPCustomer(vipCustomer);
    order.addLine(product1, quantity1); // $100
    
    // VIP gets 10% discount
    expect(order.getTotal().equals(Money.from(90))).toBe(true);
  });
  
  it('should calculate tax based on shipping address', () => {
    const order = new Order();
    order.addLine(product1, quantity1); // $100
    order.setShippingAddress(californiaAddress); // 10% tax
    
    expect(order.getTotal().equals(Money.from(110))).toBe(true);
  });
});
```

**Communicating Through Tests**:

```typescript
// Someone questions your design choice?
// Instead of arguing, show them a failing test:

describe('Design Decision: Email must be Value Object (immutable)', () => {
  it('should prevent email mutation after creation', () => {
    const email = Email.from('original@example.com');
    
    // Try to mutate (should not be possible)
    // @ts-expect-error - email.value is private/readonly
    email.value = 'hacked@example.com';
    
    // Email should still be original
    expect(email.getValue()).toBe('original@example.com');
  });
  
  it('should allow email replacement via Customer.changeEmail', () => {
    const customer = Customer.create(
      CustomerId.generate(),
      Email.from('old@example.com')
    );
    
    // This is the correct way to change email
    customer.changeEmail(Email.from('new@example.com'));
    
    expect(customer.getEmail().getValue()).toBe('new@example.com');
  });
});
```

**The Test Proves the Design Choice**: "This is worth a thousand hours of discussion about design aesthetics."

## III. Adopt Incremental, Feedback-Driven Practices

### 9. Keep Steps Small and Rapid

**The Feedback Loop Speed Limit**: The rate of feedback determines your development speed.

**Target**: Red-Green-Refactor cycle in **minutes**, not hours.

**Bad (Speculative - Large Steps)**:
```
Day 1-2: Write entire User class with 15 methods
Day 3: Write tests for all 15 methods
Day 4: Fix bugs discovered during testing
Day 5: Refactor tangled mess
```

**Good (Empirical - Small Steps)**:
```
Minutes 0-5: Test for User.create()
Minutes 5-10: Implement User.create()
Minutes 10-12: Refactor
Minutes 12-17: Test for User.changeEmail()
Minutes 17-22: Implement User.changeEmail()
Minutes 22-24: Refactor
... continue for other methods
```

**Process**:
```bash
# Start timer
time {
  # Write test
  vim tests/user.test.ts
  
  # Run test (should FAIL)
  npm test -- user.test.ts
  
  # Implement
  vim src/user.ts
  
  # Run test (should PASS)
  npm test -- user.test.ts
  
  # Refactor
  vim src/user.ts
  
  # Run test (should STILL PASS)
  npm test -- user.test.ts
  
  # Commit
  git commit -am "feat: add User.changeEmail (TDD)"
}

# Target: <5 minutes total
```

**If cycle takes >10 minutes**:
- ❌ Step is too large - break it down
- ❌ Tests are too slow - optimize test setup
- ❌ Implementation is too complex - simplify design

### 10. Build Incrementally (End-to-End Slices)

**Tracer Bullet / Vertical Slice**: Implement thin end-to-end functionality early.

**Anti-Pattern (Horizontal Layers)**:
```
Week 1: Complete all database models
Week 2: Complete all API endpoints
Week 3: Complete all frontend components
Week 4: Integrate everything (SURPRISE! Nothing works)
```

**Best Practice (Vertical Slices)**:
```
Day 1: User can register (database + API + UI)
Day 2: User can login (database + API + UI)
Day 3: User can update profile (database + API + UI)
Day 4: User can delete account (database + API + UI)
```

**Implementation**:

```markdown
# Vertical Slice: User Registration

## Step 1: Database (Minimal)
CREATE TABLE users (
  id UUID PRIMARY KEY,
  email VARCHAR(255) UNIQUE NOT NULL
);

## Step 2: API (Minimal)
POST /api/users
Body: { "email": "user@example.com" }
Response: { "id": "uuid", "email": "user@example.com" }

## Step 3: Frontend (Minimal)
<form onSubmit={handleRegister}>
  <input type="email" name="email" />
  <button>Register</button>
</form>

## Step 4: Test End-to-End
describe('User Registration (E2E)', () => {
  it('should create user from frontend to database', async () => {
    // Arrange
    await browser.goto('/register');
    
    // Act
    await browser.fill('[name=email]', 'test@example.com');
    await browser.click('button:text("Register")');
    
    // Assert
    await expect(page.locator('.success')).toBeVisible();
    
    // Verify database
    const user = await db.users.findOne({ email: 'test@example.com' });
    expect(user).toBeDefined();
  });
});
```

**Benefits**:
- ✅ Integration issues discovered **immediately**
- ✅ System is **always working** (even if feature-incomplete)
- ✅ Demonstrates progress to stakeholders
- ✅ Reduces risk by testing riskiest parts first

### 11. Avoid Gold-Plating (YAGNI - You Aren't Gonna Need It)

**Rule**: Build only what you **need NOW**, not what you **might need later**.

**Speculation Example** (Gold-Plating):
```typescript
// ❌ Building for imaginary future requirements
export class User {
  // "We might need this later"
  private preferences: Map<string, any> = new Map();
  private metadata: Record<string, unknown> = {};
  private tags: Set<string> = new Set();
  
  // "This could be useful someday"
  public addTag(tag: string): void { /*...*/ }
  public removeTag(tag: string): void { /*...*/ }
  public hasTag(tag: string): boolean { /*...*/ }
  
  // "Let's make it flexible"
  public setPreference(key: string, value: any): void { /*...*/ }
  public getPreference<T>(key: string): T { /*...*/ }
}
```

**Problems**:
- ❌ No tests exist for this code (it's speculative)
- ❌ Adds complexity to codebase
- ❌ May never be used (wasted effort)
- ❌ Harder to understand and maintain
- ❌ Might not match actual future requirements

**YAGNI Approach** (Build What Tests Demand):
```typescript
// ✅ Only what tests require RIGHT NOW
describe('User', () => {
  it('should store email', () => {
    const user = User.create(Email.from('user@example.com'));
    expect(user.getEmail().getValue()).toBe('user@example.com');
  });
  
  it('should allow email change', () => {
    const user = User.create(Email.from('old@example.com'));
    user.changeEmail(Email.from('new@example.com'));
    expect(user.getEmail().getValue()).toBe('new@example.com');
  });
});

// Implementation - ONLY what tests need
export class User {
  constructor(
    private readonly id: UserId,
    private email: Email
  ) {}
  
  static create(email: Email): User {
    return new User(UserId.generate(), email);
  }
  
  changeEmail(newEmail: Email): void {
    this.email = newEmail;
  }
  
  getEmail(): Email {
    return this.email;
  }
}

// When we ACTUALLY need tags, we'll write tests for them first
// Then add them. Not before.
```

**When to Add Features**:
1. ✅ Customer explicitly requests it
2. ✅ Existing code would be simpler with it
3. ✅ Next feature requires it
4. ❌ "We might need it someday" (NO!)
5. ❌ "It's easy to add now" (Still NO!)

### 12. Listen to Customer Frequently (Short Iterations)

**Customer = Driver**: The customer drives the project direction through frequent feedback.

**Anti-Pattern (Long Iterations)**:
```
Quarter 1: Gather all requirements
Quarter 2: Implement everything
Quarter 3: Customer sees first demo
Customer: "This isn't what I wanted at all"
```

**Best Practice (Weekly/Bi-weekly Iterations)**:
```
Week 1:
  - Implement Feature A
  - Demo to customer Friday
  - Customer: "Good, but needs X adjustment"

Week 2:
  - Adjust Feature A based on feedback
  - Implement Feature B
  - Demo both to customer Friday
  - Customer: "Feature B is wrong direction, pivot to C"

Week 3:
  - Implement Feature C instead
  - Customer: "Perfect! This is what I needed"
```

**Implementation**:

```markdown
# Sprint 1 (2 weeks)

## Planned Features:
- User registration
- User login
- User profile page

## Demo Schedule:
- **Day 3**: Show registration to customer (internal demo)
- **Day 7**: Mid-sprint review with customer
- **Day 14**: End-of-sprint demo with customer

## Customer Feedback (Day 7):
- ✅ Registration flow approved
- ❌ Login needs "Remember Me" checkbox
- ❌ Profile page should show activity history
- 🆕 New request: Password reset flow

## Adjustments:
- Add "Remember Me" to login (2 days)
- Add activity history to profile (3 days)
- **Defer** password reset to Sprint 2 (not critical path)

## Actual Delivery:
- User registration ✅
- User login with Remember Me ✅
- User profile with activity history ✅
```

**Benefits**:
- ✅ Requirements clarified through **working software**, not documents
- ✅ Customer learns what they **actually want** by using the system
- ✅ Course corrections are **cheap** (1-2 weeks wasted, not months)
- ✅ Team builds **trust** through frequent delivery

## ✅ Implementation Checklist

### Empirical Investigation
- [ ] All assumptions explicitly documented and verified
- [ ] Spike solutions used for technical unknowns (time-boxed)
- [ ] Walking skeleton proves architecture end-to-end
- [ ] Design sessions result in experiments, not just diagrams

### Test-Driven Development
- [ ] Every line of production code has a test
- [ ] Tests written BEFORE implementation (Red-Green-Refactor)
- [ ] Assertions validate boundaries and invariants
- [ ] Tests communicate design intent and serve as documentation

### Incremental Development
- [ ] Development in small steps (minutes, not hours)
- [ ] Vertical slices implemented end-to-end
- [ ] YAGNI enforced (no speculative features)
- [ ] Frequent customer demos (weekly/bi-weekly)

### Continuous Feedback
- [ ] Red-Green-Refactor cycle <10 minutes
- [ ] CI runs on every commit
- [ ] Build breaks fixed immediately (<10 minutes)
- [ ] Customer sees working software every iteration

## 🚨 Warning Signs of Speculative Development

Watch for these red flags:

❌ **"I'm pretty sure this will work"** - PROVE IT with a test  
❌ **"We'll need this later"** - YAGNI - wait for actual requirement  
❌ **"This is too simple to test"** - ESPECIALLY test simple things  
❌ **"Let me finish coding, then I'll write tests"** - RED → GREEN → REFACTOR (tests first!)  
❌ **"The documentation says it works this way"** - Documentation lies, tests don't  
❌ **"I've done this before"** - Context is different, PROVE it works here  
❌ **"Writing tests will slow me down"** - Speculation slows you MORE (debugging production)  
❌ **"We'll get customer feedback after release"** - Get it DURING development (weekly demos)  

## 📚 Integration with Other Practices

### TDD + DDD
- Tests express domain concepts using Ubiquitous Language
- Domain model evolves through Red-Green-Refactor
- Aggregates proven to maintain invariants through tests

### TDD + Standards Compliance
- Tests verify requirements traceability (REQ → TEST linkage)
- Test coverage measured and enforced (>80%)
- IEEE 1012-2016 validation requirements met through automated tests

### TDD + CI/CD
- Every commit triggers automated test suite
- Build breaks are empirical proof of problems
- Green builds empirically prove system works

---

**Remember: The antidote to speculation is PROOF. Let automated tests be your empirical evidence that the system works as intended!** 🔬✅