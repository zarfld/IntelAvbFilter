# Object-Oriented Design Principles

**Phase**: 03-Architecture, 04-Design, 05-Implementation  
**Purpose**: Core OO principles for reusable, extensible, maintainable systems  
**Integration**: Complements DDD tactical patterns and TDD practices

## 🎯 Core OO Principles

### 1. Encapsulate What Varies
**Principle**: Identify aspects that vary and separate them from what stays the same.

```typescript
// ❌ BAD: Variation mixed with stable code
class PaymentProcessor {
  process(type: string, amount: Money): void {
    if (type === 'CARD') {
      // Card processing logic
    } else if (type === 'PAYPAL') {
      // PayPal logic
    } else if (type === 'CRYPTO') {
      // Crypto logic
    }
  }
}

// ✅ GOOD: Variation encapsulated
interface PaymentStrategy {
  process(amount: Money): Result<void>;
}

class CardPayment implements PaymentStrategy {
  process(amount: Money): Result<void> { /* ... */ }
}

class PayPalPayment implements PaymentStrategy {
  process(amount: Money): Result<void> { /* ... */ }
}

class PaymentProcessor {
  constructor(private strategy: PaymentStrategy) {}
  
  process(amount: Money): Result<void> {
    return this.strategy.process(amount);
  }
}
```

### 2. Favor Composition Over Inheritance
**Principle**: Use "has-a" (composition) instead of "is-a" (inheritance) when relationship isn't permanent.

```typescript
// ❌ BAD: Inheritance for behavior reuse
class Animal {
  move() { console.log('Moving'); }
}

class Dog extends Animal {
  bark() { console.log('Woof'); }
}

class Robot extends Animal {  // Robot is-a Animal? Wrong!
  beep() { console.log('Beep'); }
}

// ✅ GOOD: Composition for flexibility
interface Movable {
  move(): void;
}

class WalkingMovement implements Movable {
  move() { console.log('Walking'); }
}

class FlyingMovement implements Movable {
  move() { console.log('Flying'); }
}

class Dog {
  constructor(private movement: Movable) {}
  move() { this.movement.move(); }
  bark() { console.log('Woof'); }
}

class Robot {
  constructor(private movement: Movable) {}
  move() { this.movement.move(); }
  beep() { console.log('Beep'); }
}
```

### 3. Program to Interfaces, Not Implementations
**Principle**: Depend on abstractions to decouple clients from concrete implementations.

```typescript
// ❌ BAD: Coupled to concrete implementation
class OrderService {
  private repo = new MySQLOrderRepository();  // Concrete dependency
  
  save(order: Order): void {
    this.repo.save(order);  // Tightly coupled to MySQL
  }
}

// ✅ GOOD: Programmed to interface
interface OrderRepository {
  save(order: Order): void;
  findById(id: string): Order | null;
}

class OrderService {
  constructor(private repo: OrderRepository) {}  // Abstract dependency
  
  save(order: Order): void {
    this.repo.save(order);  // Works with any repository
  }
}

// Can swap implementations easily
const service1 = new OrderService(new MySQLOrderRepository());
const service2 = new OrderService(new MongoOrderRepository());
const service3 = new OrderService(new InMemoryOrderRepository());  // For tests
```

### 4. Open-Closed Principle (OCP)
**Principle**: Classes open for extension, closed for modification.

```typescript
// ❌ BAD: Must modify class to add new discount type
class DiscountCalculator {
  calculate(type: string, amount: Money): Money {
    if (type === 'PERCENTAGE') {
      return amount.multiply(0.1);
    } else if (type === 'FIXED') {
      return Money.of(10);
    }
    // Adding new type requires modifying this class!
  }
}

// ✅ GOOD: Open for extension via interface
interface DiscountStrategy {
  calculate(amount: Money): Money;
}

class PercentageDiscount implements DiscountStrategy {
  constructor(private rate: number) {}
  calculate(amount: Money): Money {
    return amount.multiply(this.rate);
  }
}

class FixedDiscount implements DiscountStrategy {
  constructor(private fixed: Money) {}
  calculate(amount: Money): Money {
    return this.fixed;
  }
}

// NEW: Add without modifying existing code
class BuyOneGetOneDiscount implements DiscountStrategy {
  calculate(amount: Money): Money {
    return amount.multiply(0.5);
  }
}

class DiscountCalculator {
  constructor(private strategy: DiscountStrategy) {}
  
  calculate(amount: Money): Money {
    return this.strategy.calculate(amount);
  }
}
```

### 5. Dependency Inversion Principle
**Principle**: Depend on abstractions, not concrete classes.

```typescript
// ❌ BAD: High-level depends on low-level
class EmailNotifier {
  send(to: string, message: string): void { /* SMTP logic */ }
}

class OrderService {
  private notifier = new EmailNotifier();  // Depends on concrete class
  
  placeOrder(order: Order): void {
    // ...
    this.notifier.send(order.customer.email, 'Order placed');
  }
}

// ✅ GOOD: Both depend on abstraction
interface Notifier {
  send(to: string, message: string): void;
}

class EmailNotifier implements Notifier {
  send(to: string, message: string): void { /* SMTP */ }
}

class SmsNotifier implements Notifier {
  send(to: string, message: string): void { /* SMS API */ }
}

class OrderService {
  constructor(private notifier: Notifier) {}  // Depends on abstraction
  
  placeOrder(order: Order): void {
    // ...
    this.notifier.send(order.customer.contact, 'Order placed');
  }
}
```

### 6. Law of Demeter (Principle of Least Knowledge)
**Principle**: Talk only to your immediate friends, not strangers.

```typescript
// ❌ BAD: Reaching through objects (train wreck)
class OrderController {
  processOrder(order: Order): void {
    const street = order.getCustomer().getAddress().getStreet();  // Chain of calls
    const city = order.getCustomer().getAddress().getCity();
    // Knows too much about object structure
  }
}

// ✅ GOOD: Tell, don't ask
class Order {
  getShippingAddress(): Address {
    return this.customer.getShippingAddress();  // Delegate
  }
}

class OrderController {
  processOrder(order: Order): void {
    const address = order.getShippingAddress();  // Single call
    // Controller doesn't know about customer or address internals
  }
}
```

## 🏗️ OO Analysis & Design Workflow

### Phase 01-02: Analysis (OOA)

**Goal**: Identify key abstractions from problem domain vocabulary.

```markdown
## Analysis Workflow

1. **Extract Nouns from Requirements** (Initial candidates)
   - Customer, Order, Product, Payment, Invoice
   
2. **Classify as Analysis Classes** (Domain concepts)
   - Physical objects: Product, Warehouse
   - Conceptual things: Order, Policy, Transaction
   
3. **Use CRC Cards** (Class-Responsibility-Collaboration)
   ```
   Class: Order
   Responsibilities:
   - Track order items
   - Calculate total
   - Validate credit limit
   Collaborators: Customer, LineItem, DiscountPolicy
   ```

4. **Model Relationships**
   - Client (has-a): Order has-a Customer
   - Inheritance (is-a): PremiumCustomer is-a Customer

5. **Specify Contracts** (Design by Contract)
   - Preconditions: Order.addItem requires item.quantity > 0
   - Postconditions: After addItem, order.items.length increases
   - Invariants: Order.total always >= 0
```

### Phase 03-04: Design (OOD)

**Goal**: Create architectural abstractions and collaboration mechanisms.

```markdown
## Design Workflow

1. **Apply Design Principles**
   - Encapsulate variations (Strategy pattern)
   - Favor composition (Decorator pattern)
   - Program to interfaces (Repository pattern)

2. **Use Design Patterns**
   - Strategy: Payment methods
   - Factory: Object creation
   - Observer: Event notifications
   - Decorator: Add features dynamically

3. **Measure Quality**
   - High cohesion: Class methods work together
   - Low coupling: Minimal dependencies
   
4. **Command-Query Separation**
   - Queries: Return data, no side effects
   - Commands: Modify state, return void/Result
```

### Phase 05: Implementation

**Goal**: Code as expression of the model.

```markdown
## Implementation Checklist

- [ ] Use Ubiquitous Language for class/method names
- [ ] Implement contracts as assertions or tests
- [ ] Refactor continuously (Red-Green-Refactor)
- [ ] Leverage polymorphism for flexibility
- [ ] Wrap legacy code in OO interfaces
```

## 🔗 Integration with Existing Framework

### OO + DDD
- **Analysis Classes** = **Domain Entities/Value Objects**
- **Design Classes** = **Aggregates, Repositories, Services**
- **Ubiquitous Language** = Class/method naming
- **Bounded Contexts** = OO module boundaries

### OO + TDD
- **Contracts** = Test preconditions/postconditions
- **Refactoring** = Improve OO design while tests stay green
- **Interface-based design** = Easy mocking in tests

### OO + Real-Time
- **Polymorphism** = Use static (templates) for time-critical paths
- **Encapsulation** = Isolate timing-critical code
- **Composition** = Build complex behaviors from simple, fast components

### OO + Reverse Engineering
- **Law of Inversion** = Convert functional code to OO (data first)
- **CRC Cards** = Analyze legacy code responsibilities
- **Refactoring** = Extract classes from monolithic functions

## ✅ Quick Reference

| Principle | When to Apply | Pattern Example |
|-----------|---------------|-----------------|
| **Encapsulate variation** | Behavior changes based on conditions | Strategy, State |
| **Composition over inheritance** | Relationship not permanent | Decorator, Composite |
| **Program to interfaces** | Need flexibility, testability | Repository, Factory |
| **Open-Closed** | Adding new features | Strategy, Template Method |
| **Dependency Inversion** | Decouple high/low level modules | Dependency Injection |
| **Law of Demeter** | Reduce coupling | Facade, Mediator |

## 📚 Related Documentation

- `docs/ddd-implementation-guide.md` - Tactical patterns (Entity, Value Object, Aggregate)
- `docs/tdd-empirical-proof.md` - Test-driven refactoring
- `docs/reverse-engineering-guide.md` - OO re-architecturing
- `.github/instructions/phase-04-design.instructions.md` - Design standards

---

**Remember: Good OO design = Reusable + Extensible + Maintainable!** 🏛️
