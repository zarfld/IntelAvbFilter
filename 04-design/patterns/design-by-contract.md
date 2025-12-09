# Design by Contract (DbC)

**Phase**: 04-Design, 05-Implementation  
**Standards**: ISO/IEC/IEEE 12207:2017  
**Purpose**: Formalize behavioral specifications for software components using preconditions, postconditions, and invariants.

## 🎯 Purpose

Design by Contract (DbC) is a software design approach where components specify:

1. **Preconditions** - What must be true before method execution
2. **Postconditions** - What will be true after method execution
3. **Invariants** - What must always be true for the object

**Benefits**:
- ✅ Clear component interfaces and responsibilities
- ✅ Self-documenting code
- ✅ Early bug detection
- ✅ Better testability
- ✅ Prevents defensive programming clutter

**Integration with DDD**: Contracts enforce domain invariants and business rules at the code level.

**Integration with TDD**: Contracts define expectations verified by tests.

---

## 📋 Contract Elements

### 1. Preconditions

**Definition**: Conditions that must be true before a method can execute correctly.

**Responsibility**: Caller ensures preconditions (not callee)

**Examples**:
- Non-null parameters
- Valid ranges (age > 0)
- Object state (account must be active)

**Implementation**:

```typescript
// Assertion-based (development)
public withdraw(amount: Money): void {
  // Preconditions
  assert(amount !== null, 'Amount cannot be null');
  assert(amount.isPositive(), 'Amount must be positive');
  assert(this.status === AccountStatus.Active, 'Account must be active');
  
  // Method implementation
  this.balance = this.balance.subtract(amount);
}

// Exception-based (production)
public withdraw(amount: Money): void {
  // Preconditions
  if (amount === null) {
    throw new PreconditionError('Amount cannot be null');
  }
  if (!amount.isPositive()) {
    throw new PreconditionError('Amount must be positive');
  }
  if (this.status !== AccountStatus.Active) {
    throw new PreconditionError('Account must be active to withdraw');
  }
  
  // Method implementation
  this.balance = this.balance.subtract(amount);
}
```

---

### 2. Postconditions

**Definition**: Conditions guaranteed to be true after method execution (if preconditions met).

**Responsibility**: Method ensures postconditions

**Examples**:
- Return value meets specification
- Object state changed correctly
- Side effects occurred

**Implementation**:

```typescript
public withdraw(amount: Money): void {
  // Preconditions
  assert(amount !== null && amount.isPositive(), 'Amount must be positive');
  assert(this.balance.isGreaterThanOrEqual(amount), 'Insufficient funds');
  
  // Capture old state
  const oldBalance = this.balance;
  
  // Method implementation
  this.balance = this.balance.subtract(amount);
  this.lastTransactionDate = new Date();
  
  // Postconditions
  assert(
    this.balance.equals(oldBalance.subtract(amount)),
    'Balance not updated correctly'
  );
  assert(
    this.lastTransactionDate !== null,
    'Transaction date not set'
  );
}
```

---

### 3. Invariants

**Definition**: Conditions that must always be true for an object (before and after every public method).

**Responsibility**: Object maintains invariants

**Examples**:
- Balance never negative (unless overdraft allowed)
- Collection sizes match expected values
- Aggregate consistency rules

**Implementation**:

```typescript
export class BankAccount {
  private balance: Money;
  private overdraftLimit: Money;
  private status: AccountStatus;
  
  // Invariant checker (called before/after public methods)
  private checkInvariants(): void {
    // Invariant 1: Balance must not go below negative overdraft limit
    const minimumBalance = this.overdraftLimit.negate();
    assert(
      this.balance.isGreaterThanOrEqual(minimumBalance),
      `Balance ${this.balance} below overdraft limit ${minimumBalance}`
    );
    
    // Invariant 2: Closed accounts must have zero balance
    if (this.status === AccountStatus.Closed) {
      assert(
        this.balance.isZero(),
        'Closed account must have zero balance'
      );
    }
    
    // Invariant 3: Overdraft limit must be non-negative
    assert(
      !this.overdraftLimit.isNegative(),
      'Overdraft limit cannot be negative'
    );
  }
  
  public withdraw(amount: Money): void {
    this.checkInvariants();  // Before
    
    // Preconditions
    assert(amount.isPositive(), 'Amount must be positive');
    
    // Implementation
    this.balance = this.balance.subtract(amount);
    
    this.checkInvariants();  // After
  }
  
  public deposit(amount: Money): void {
    this.checkInvariants();  // Before
    
    // Preconditions
    assert(amount.isPositive(), 'Amount must be positive');
    
    // Implementation
    this.balance = this.balance.add(amount);
    
    this.checkInvariants();  // After
  }
}
```

---

## 🔧 Implementation Strategies

### Strategy 1: Assertions (Development Mode)

**Use Case**: Development, testing, debugging

**Pros**:
- Zero runtime cost when disabled
- Clear documentation
- Easy to toggle

**Cons**:
- Disabled in production (misses runtime violations)

**Implementation**:

```typescript
// TypeScript with assert library
import assert from 'assert';

export class Order {
  public addLine(product: Product, quantity: number): void {
    // Preconditions
    assert(product !== null, 'Product cannot be null');
    assert(quantity > 0, 'Quantity must be positive');
    assert(this.status === OrderStatus.Draft, 'Can only add to draft orders');
    
    // Implementation
    const line = new OrderLine(product, quantity);
    this.lines.push(line);
    
    // Postconditions
    assert(this.lines.length > 0, 'Line not added');
    assert(this.lines.includes(line), 'Specific line not found');
  }
}

// Disable in production
if (process.env.NODE_ENV === 'production') {
  // Assertions become no-ops
}
```

---

### Strategy 2: Exceptions (Production Mode)

**Use Case**: Production runtime validation

**Pros**:
- Always active
- Clear error messages
- Can log violations

**Cons**:
- Runtime performance cost
- More verbose

**Implementation**:

```typescript
export class ContractViolationError extends Error {
  constructor(
    public readonly violationType: 'precondition' | 'postcondition' | 'invariant',
    message: string
  ) {
    super(`${violationType} violation: ${message}`);
  }
}

export class Order {
  public addLine(product: Product, quantity: number): void {
    // Preconditions
    if (product === null) {
      throw new ContractViolationError('precondition', 'Product cannot be null');
    }
    if (quantity <= 0) {
      throw new ContractViolationError('precondition', 'Quantity must be positive');
    }
    if (this.status !== OrderStatus.Draft) {
      throw new ContractViolationError(
        'precondition',
        `Cannot add lines to ${this.status} order`
      );
    }
    
    // Implementation
    const line = new OrderLine(product, quantity);
    this.lines.push(line);
    
    // Postconditions
    if (!this.lines.includes(line)) {
      throw new ContractViolationError('postcondition', 'Line not added');
    }
  }
}
```

---

### Strategy 3: Decorator Pattern

**Use Case**: Non-intrusive contract checking

**Pros**:
- Separates contracts from business logic
- Can toggle per method
- Clear separation of concerns

**Cons**:
- More complex setup
- Harder to debug

**Implementation**:

```typescript
// Decorator for preconditions
function requires(condition: (args: any[]) => boolean, message: string) {
  return function (
    target: any,
    propertyKey: string,
    descriptor: PropertyDescriptor
  ) {
    const originalMethod = descriptor.value;
    
    descriptor.value = function (...args: any[]) {
      if (!condition.call(this, args)) {
        throw new ContractViolationError('precondition', message);
      }
      return originalMethod.apply(this, args);
    };
    
    return descriptor;
  };
}

// Decorator for postconditions
function ensures(condition: (result: any, args: any[]) => boolean, message: string) {
  return function (
    target: any,
    propertyKey: string,
    descriptor: PropertyDescriptor
  ) {
    const originalMethod = descriptor.value;
    
    descriptor.value = function (...args: any[]) {
      const result = originalMethod.apply(this, args);
      
      if (!condition.call(this, result, args)) {
        throw new ContractViolationError('postcondition', message);
      }
      
      return result;
    };
    
    return descriptor;
  };
}

// Usage
export class BankAccount {
  @requires(
    (args) => args[0] !== null && args[0].isPositive(),
    'Amount must be positive'
  )
  @ensures(
    function(result, args) {
      return this.balance.equals(this.oldBalance.subtract(args[0]));
    },
    'Balance not updated correctly'
  )
  public withdraw(amount: Money): void {
    this.oldBalance = this.balance;  // For postcondition check
    this.balance = this.balance.subtract(amount);
  }
}
```

---

## 🧪 Contracts and Testing (TDD Integration)

Contracts define what tests should verify.

### Test Case from Contract

**Contract**:

```typescript
/**
 * Transfer money between accounts
 * 
 * Preconditions:
 * - amount > 0
 * - fromAccount has sufficient balance
 * - Both accounts are active
 * 
 * Postconditions:
 * - fromAccount.balance decreased by amount
 * - toAccount.balance increased by amount
 * - Transaction recorded
 * 
 * Invariants:
 * - Total money in system unchanged
 */
public transfer(from: Account, to: Account, amount: Money): void {
  // Implementation
}
```

**Test Cases Generated from Contract**:

```typescript
describe('TransferMoneyService.transfer', () => {
  // Test preconditions
  it('should reject negative amount (precondition)', () => {
    const amount = Money.create(-100, Currency.USD);
    expect(() => service.transfer(from, to, amount))
      .toThrow(ContractViolationError);
  });
  
  it('should reject insufficient balance (precondition)', () => {
    from.setBalance(Money.create(50, Currency.USD));
    const amount = Money.create(100, Currency.USD);
    expect(() => service.transfer(from, to, amount))
      .toThrow(ContractViolationError);
  });
  
  // Test postconditions
  it('should decrease from account balance (postcondition)', () => {
    const initialBalance = from.getBalance();
    const amount = Money.create(100, Currency.USD);
    
    service.transfer(from, to, amount);
    
    expect(from.getBalance()).toEqual(initialBalance.subtract(amount));
  });
  
  it('should increase to account balance (postcondition)', () => {
    const initialBalance = to.getBalance();
    const amount = Money.create(100, Currency.USD);
    
    service.transfer(from, to, amount);
    
    expect(to.getBalance()).toEqual(initialBalance.add(amount));
  });
  
  // Test invariants
  it('should preserve total money in system (invariant)', () => {
    const totalBefore = from.getBalance().add(to.getBalance());
    const amount = Money.create(100, Currency.USD);
    
    service.transfer(from, to, amount);
    
    const totalAfter = from.getBalance().add(to.getBalance());
    expect(totalAfter).toEqual(totalBefore);
  });
});
```

---

## 📝 Documentation Format

Document contracts in code comments using JSDoc/TSDoc:

```typescript
/**
 * Withdraw money from account
 * 
 * @param amount - Amount to withdraw
 * 
 * @precondition amount !== null
 * @precondition amount > 0
 * @precondition this.status === AccountStatus.Active
 * @precondition this.balance >= amount - this.overdraftLimit
 * 
 * @postcondition this.balance === old(this.balance) - amount
 * @postcondition this.lastTransactionDate === now()
 * 
 * @invariant this.balance >= -this.overdraftLimit
 * @invariant this.status === Active => this.balance defined
 * 
 * @throws {PreconditionError} if preconditions not met
 * @throws {InvariantViolationError} if invariant violated
 * 
 * @traceability Implements #REQ-F-ACCOUNT-002
 */
public withdraw(amount: Money): void {
  // Implementation
}
```

---

## ✅ Best Practices

### Always Do

✅ **Document contracts explicitly** - Use JSDoc annotations  
✅ **Check preconditions early** - Fail fast with clear messages  
✅ **Verify postconditions** - Ensure guarantees held  
✅ **Maintain invariants** - Check before/after every public method  
✅ **Generate tests from contracts** - Every contract element becomes test  
✅ **Use assertions in dev, exceptions in prod** - Configurable checking  
✅ **Make contracts visible** - Part of public API documentation  

### Never Do

❌ **Don't duplicate contract checks** - Caller checks preconditions, not callee  
❌ **Don't check preconditions inside method** - That's defensive programming, increases coupling  
❌ **Don't mutate state in contract checks** - Contracts should be side-effect-free  
❌ **Don't ignore invariants** - They're the backbone of object consistency  
❌ **Don't disable contracts in production** - Use lightweight checks or exceptions  
❌ **Don't use contracts for error handling** - Contracts are for logic errors (bugs), not user errors  

---

## 🎯 Contract Levels

### Level 1: Null Checks

**Basic safety**:

```typescript
public process(user: User): void {
  assert(user !== null, 'User cannot be null');
  // Implementation
}
```

### Level 2: Type and Range Validation

**Value constraints**:

```typescript
public setAge(age: number): void {
  assert(age >= 0 && age <= 150, 'Age must be between 0 and 150');
  this.age = age;
}
```

### Level 3: State Validation

**Object state requirements**:

```typescript
public submit(): void {
  assert(this.status === OrderStatus.Draft, 'Only draft orders can be submitted');
  assert(this.lines.length > 0, 'Order must have at least one line');
  this.status = OrderStatus.Submitted;
}
```

### Level 4: Business Rule Enforcement

**Complex domain rules**:

```typescript
public addLine(product: Product, quantity: number): void {
  assert(quantity > 0, 'Quantity must be positive');
  assert(this.lines.length < MAX_ORDER_LINES, 'Order cannot exceed max lines');
  
  // Business rule: Cannot add duplicate products
  assert(
    !this.lines.some(line => line.product.equals(product)),
    'Product already in order'
  );
  
  // Implementation
}
```

### Level 5: Invariant Preservation

**Aggregate consistency**:

```typescript
private validateInvariants(): void {
  // Invariant: Total must equal sum of lines
  const calculatedTotal = this.lines.reduce(
    (sum, line) => sum.add(line.getTotal()),
    Money.zero()
  );
  assert(
    this.total.equals(calculatedTotal),
    'Order total inconsistent with line items'
  );
  
  // Invariant: All lines have positive quantities
  assert(
    this.lines.every(line => line.quantity > 0),
    'All line quantities must be positive'
  );
}
```

---

## 🔗 Integration with GitHub Issues

When creating TEST issues, reference contracts:

```markdown
# Test Case: Withdraw Money from Account

**ID**: TEST-ACCOUNT-WITHDRAW-001  
**Type**: Unit Test  
**Priority**: P0  

## Traceability
- **Verifies**: #REQ-F-ACCOUNT-002 (Withdraw Money)
- **Tests Contract**: `BankAccount.withdraw(amount)`

## Contract Being Verified

**Preconditions**:
- `amount !== null` ✅ Test 1
- `amount > 0` ✅ Test 2
- `this.status === Active` ✅ Test 3
- `this.balance >= amount - overdraft` ✅ Test 4

**Postconditions**:
- `this.balance === old(balance) - amount` ✅ Test 5
- `this.lastTransactionDate === now()` ✅ Test 6

**Invariants**:
- `this.balance >= -overdraftLimit` ✅ Test 7

## Test Cases

### Test 1: Reject null amount (Precondition)
```typescript
expect(() => account.withdraw(null)).toThrow(PreconditionError);
```

### Test 2: Reject negative amount (Precondition)
...

[Continue for all contract elements]
```

---

## 📊 Contract Coverage Metrics

| Metric | Target | Purpose |
|--------|--------|---------|
| Precondition coverage | 100% | All preconditions tested |
| Postcondition coverage | 100% | All postconditions tested |
| Invariant checks | 100% | All public methods check invariants |
| Contract documentation | 100% | All public methods documented |

---

## 🔗 Related Documentation

- [DDD Tactical Patterns](./ddd-tactical-patterns.md) - Entity, Value Object, Aggregate contracts
- [XP Practices - TDD](../../docs/xp-practices.md) - Testing contracts
- [Phase 05 Implementation](../../.github/instructions/phase-05-implementation.instructions.md)
- [Test Case Template](../../07-verification-validation/test-cases/)

---

**Standards Alignment**:
- ISO/IEC/IEEE 12207:2017 (Software Construction)
- IEEE 1012-2016 (Verification and Validation)
- Design by Contract (Meyer, 1986)

**Version**: 1.0  
**Last Updated**: 2025-11-27  
**Owner**: Development Team
