# Domain-Driven Design (DDD) Tactical Patterns

**Phase**: 04-Design  
**Standards**: IEEE 1016-2009, ISO/IEC/IEEE 12207:2017  
**Approach**: Model-Driven Design (MDD)

This guide provides practical templates and examples for implementing DDD tactical patterns in the Domain Layer.

## 📦 Pattern Overview

| Pattern | Purpose | When to Use |
|---------|---------|-------------|
| **Entity** | Objects with identity and continuity | When object identity matters across time |
| **Value Object** | Immutable objects defined by attributes | When only attributes matter, not identity |
| **Aggregate** | Cluster with consistency boundary | When enforcing invariants across multiple objects |
| **Repository** | Collection-like interface for Aggregates | When abstracting data access for Aggregate roots |
| **Factory** | Complex object creation logic | When construction is complex or requires validation |
| **Domain Service** | Stateless domain operations | When operation doesn't fit Entity or Value Object |
| **Specification** | Explicit predicate/rule | When validating or querying with complex criteria |

## 1. Entities

**Definition**: Objects defined by identity and continuity, not just attributes.

**Key Characteristics**:
- Has unique identity (ID)
- Mutable (state can change over time)
- Identity remains constant
- Equality based on ID, not attributes

### Entity Template

```typescript
// Entity: User
// ID: ENTITY-USER-001
// Traceability: Implements #REQ-F-AUTH-001

export class User {
  // Identity
  private readonly id: UserId;  // Value Object for type safety
  
  // Attributes (can change)
  private email: Email;         // Value Object
  private name: string;
  private status: UserStatus;   // Enum or Value Object
  private createdAt: Date;
  private updatedAt: Date;
  
  // Constructor (private - use Factory)
  private constructor(
    id: UserId,
    email: Email,
    name: string,
    status: UserStatus,
    createdAt: Date,
    updatedAt: Date
  ) {
    this.id = id;
    this.email = email;
    this.name = name;
    this.status = status;
    this.createdAt = createdAt;
    this.updatedAt = updatedAt;
  }
  
  // Identity accessor
  public getId(): UserId {
    return this.id;
  }
  
  // Domain behavior (not just getters/setters)
  public changeEmail(newEmail: Email): void {
    // Enforce invariants
    if (this.status === UserStatus.Suspended) {
      throw new DomainError('Cannot change email for suspended user');
    }
    
    this.email = newEmail;
    this.updatedAt = new Date();
    
    // Could emit domain event: UserEmailChanged
  }
  
  public suspend(reason: string): void {
    if (this.status === UserStatus.Suspended) {
      throw new DomainError('User already suspended');
    }
    
    this.status = UserStatus.Suspended;
    this.updatedAt = new Date();
    
    // Domain event: UserSuspended
  }
  
  // Equality based on identity
  public equals(other: User): boolean {
    if (!other) return false;
    return this.id.equals(other.id);
  }
  
  // Factory method (alternative to external Factory)
  public static create(
    email: Email,
    name: string
  ): User {
    return new User(
      UserId.generate(),
      email,
      name,
      UserStatus.Active,
      new Date(),
      new Date()
    );
  }
}
```

**Design by Contract (Preconditions/Postconditions)**:

```typescript
public changeEmail(newEmail: Email): void {
  // Preconditions (assertions)
  assert(newEmail !== null, 'Email cannot be null');
  assert(this.status !== UserStatus.Suspended, 'Cannot change email for suspended user');
  
  const oldEmail = this.email;
  this.email = newEmail;
  this.updatedAt = new Date();
  
  // Postconditions
  assert(this.email.equals(newEmail), 'Email not updated correctly');
  assert(this.updatedAt > oldUpdatedAt, 'Updated timestamp not set');
}
```

## 2. Value Objects

**Definition**: Immutable objects defined solely by their attributes, with no identity.

**Key Characteristics**:
- Immutable (create new instance for changes)
- No identity (ID)
- Equality based on attributes
- Often used to encapsulate domain concepts

### Value Object Template

```typescript
// Value Object: Email
// ID: VO-EMAIL-001
// Traceability: Implements #REQ-F-AUTH-002

export class Email {
  private readonly value: string;
  
  // Private constructor enforces validation
  private constructor(value: string) {
    this.value = value;
  }
  
  // Factory method with validation
  public static create(value: string): Email {
    // Preconditions
    if (!value) {
      throw new ValidationError('Email cannot be empty');
    }
    
    const trimmed = value.trim().toLowerCase();
    
    // Domain validation
    const emailRegex = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
    if (!emailRegex.test(trimmed)) {
      throw new ValidationError('Invalid email format');
    }
    
    // Postcondition
    return new Email(trimmed);
  }
  
  // Accessor
  public getValue(): string {
    return this.value;
  }
  
  // Equality based on attributes
  public equals(other: Email): boolean {
    if (!other) return false;
    return this.value === other.value;
  }
  
  // Value Objects are often used in expressions
  public toString(): string {
    return this.value;
  }
  
  // Immutable transformation (returns new instance)
  public changeDomain(newDomain: string): Email {
    const localPart = this.value.split('@')[0];
    return Email.create(`${localPart}@${newDomain}`);
  }
}
```

**More Value Object Examples**:

```typescript
// Money Value Object
export class Money {
  private readonly amount: number;
  private readonly currency: Currency;
  
  private constructor(amount: number, currency: Currency) {
    if (amount < 0) {
      throw new ValidationError('Amount cannot be negative');
    }
    this.amount = amount;
    this.currency = currency;
  }
  
  public static create(amount: number, currency: Currency): Money {
    return new Money(amount, currency);
  }
  
  // Side-effect-free functions (returns new instance)
  public add(other: Money): Money {
    if (!this.currency.equals(other.currency)) {
      throw new DomainError('Cannot add money with different currencies');
    }
    return new Money(this.amount + other.amount, this.currency);
  }
  
  public multiply(factor: number): Money {
    return new Money(this.amount * factor, this.currency);
  }
  
  public equals(other: Money): boolean {
    return this.amount === other.amount && 
           this.currency.equals(other.currency);
  }
}

// Address Value Object
export class Address {
  private readonly street: string;
  private readonly city: string;
  private readonly postalCode: string;
  private readonly country: string;
  
  private constructor(
    street: string,
    city: string,
    postalCode: string,
    country: string
  ) {
    this.street = street;
    this.city = city;
    this.postalCode = postalCode;
    this.country = country;
  }
  
  public static create(
    street: string,
    city: string,
    postalCode: string,
    country: string
  ): Address {
    // Validation
    if (!street || !city || !postalCode || !country) {
      throw new ValidationError('All address fields are required');
    }
    return new Address(street, city, postalCode, country);
  }
  
  public equals(other: Address): boolean {
    return this.street === other.street &&
           this.city === other.city &&
           this.postalCode === other.postalCode &&
           this.country === other.country;
  }
}
```

## 3. Aggregates

**Definition**: Cluster of Entities and Value Objects with a consistency boundary, accessed through an Aggregate Root.

**Key Characteristics**:
- Has one Aggregate Root (Entity)
- Enforces invariants across all objects in cluster
- External objects hold references only to root
- Root controls access to internal objects
- Transaction boundary

### Aggregate Template

```typescript
// Aggregate Root: Order
// ID: AGG-ORDER-001
// Traceability: Implements #REQ-F-ORDER-001, #REQ-F-ORDER-002

export class Order {
  // Aggregate Root Identity
  private readonly id: OrderId;
  
  // Attributes
  private customerId: CustomerId;  // Reference to another Aggregate
  private status: OrderStatus;
  private createdAt: Date;
  
  // Internal entities (not exposed directly)
  private orderLines: OrderLine[];  // Collection of entities within aggregate
  
  // Aggregate-level calculated value
  private total: Money;
  
  private constructor(
    id: OrderId,
    customerId: CustomerId,
    status: OrderStatus,
    orderLines: OrderLine[],
    createdAt: Date
  ) {
    this.id = id;
    this.customerId = customerId;
    this.status = status;
    this.orderLines = orderLines;
    this.createdAt = createdAt;
    this.total = this.calculateTotal();
    
    // Enforce aggregate invariants
    this.validateInvariants();
  }
  
  // Factory method
  public static create(customerId: CustomerId): Order {
    return new Order(
      OrderId.generate(),
      customerId,
      OrderStatus.Draft,
      [],
      new Date()
    );
  }
  
  // Domain behavior (maintains aggregate consistency)
  public addLine(productId: ProductId, quantity: number, price: Money): void {
    // Preconditions
    if (this.status !== OrderStatus.Draft) {
      throw new DomainError('Cannot add lines to non-draft order');
    }
    
    // Create internal entity
    const line = OrderLine.create(
      OrderLineId.generate(),
      productId,
      quantity,
      price
    );
    
    this.orderLines.push(line);
    this.total = this.calculateTotal();
    
    // Invariant check
    this.validateInvariants();
    
    // Domain event
    // this.addDomainEvent(new OrderLineAdded(this.id, line.getId()));
  }
  
  public removeLine(lineId: OrderLineId): void {
    // Precondition
    if (this.status !== OrderStatus.Draft) {
      throw new DomainError('Cannot remove lines from non-draft order');
    }
    
    const index = this.orderLines.findIndex(l => l.getId().equals(lineId));
    if (index === -1) {
      throw new DomainError('Order line not found');
    }
    
    this.orderLines.splice(index, 1);
    this.total = this.calculateTotal();
    
    this.validateInvariants();
  }
  
  public submit(): void {
    // Precondition
    if (this.status !== OrderStatus.Draft) {
      throw new DomainError('Order already submitted');
    }
    
    // Invariant: Order must have at least one line
    if (this.orderLines.length === 0) {
      throw new DomainError('Cannot submit empty order');
    }
    
    this.status = OrderStatus.Submitted;
    
    // Domain event
    // this.addDomainEvent(new OrderSubmitted(this.id, this.total));
  }
  
  // Aggregate invariants (always maintained)
  private validateInvariants(): void {
    // Invariant 1: Total must match sum of lines
    const calculatedTotal = this.calculateTotal();
    if (!this.total.equals(calculatedTotal)) {
      throw new InvariantViolationError('Order total inconsistent');
    }
    
    // Invariant 2: All order lines must have positive quantity
    if (this.orderLines.some(line => line.getQuantity() <= 0)) {
      throw new InvariantViolationError('Order line quantity must be positive');
    }
  }
  
  private calculateTotal(): Money {
    return this.orderLines.reduce(
      (sum, line) => sum.add(line.getLineTotal()),
      Money.zero()
    );
  }
  
  // Expose read-only copy (never expose mutable internal collection)
  public getLines(): ReadonlyArray<OrderLine> {
    return [...this.orderLines];
  }
  
  public getTotal(): Money {
    return this.total;
  }
}

// Internal Entity (not Aggregate Root)
class OrderLine {
  private readonly id: OrderLineId;
  private readonly productId: ProductId;
  private quantity: number;
  private price: Money;
  
  private constructor(
    id: OrderLineId,
    productId: ProductId,
    quantity: number,
    price: Money
  ) {
    this.id = id;
    this.productId = productId;
    this.quantity = quantity;
    this.price = price;
  }
  
  public static create(
    id: OrderLineId,
    productId: ProductId,
    quantity: number,
    price: Money
  ): OrderLine {
    if (quantity <= 0) {
      throw new ValidationError('Quantity must be positive');
    }
    return new OrderLine(id, productId, quantity, price);
  }
  
  public getId(): OrderLineId {
    return this.id;
  }
  
  public getQuantity(): number {
    return this.quantity;
  }
  
  public getLineTotal(): Money {
    return this.price.multiply(this.quantity);
  }
}
```

**Aggregate Design Rules**:
1. ✅ Reference other Aggregates by ID only (not object reference)
2. ✅ One transaction = one Aggregate modification
3. ✅ Root enforces invariants for entire aggregate
4. ✅ External access only through root
5. ✅ Keep aggregates small (2-3 entities max)

## 4. Repositories

**Definition**: Collection-like interface for accessing Aggregate Roots, abstracting persistence.

**Key Characteristics**:
- Works with Aggregate Roots only
- Provides collection-like interface (add, remove, find)
- Abstracts database/storage technology
- Returns fully reconstituted Aggregates

### Repository Template

```typescript
// Repository Interface (in Domain Layer)
// ID: REPO-USER-001
// Traceability: Supports #REQ-F-AUTH-001

export interface IUserRepository {
  // Add/Update
  save(user: User): Promise<void>;
  
  // Retrieval by identity
  findById(id: UserId): Promise<User | null>;
  
  // Query methods (return Aggregates or collections)
  findByEmail(email: Email): Promise<User | null>;
  findActive(): Promise<User[]>;
  
  // Query with Specification pattern
  findMatching(spec: Specification<User>): Promise<User[]>;
  
  // Removal
  remove(user: User): Promise<void>;
  
  // Existence check
  exists(id: UserId): Promise<boolean>;
}

// Implementation (in Infrastructure Layer)
export class UserRepositoryImpl implements IUserRepository {
  constructor(private db: Database) {}
  
  public async save(user: User): Promise<void> {
    // Map domain object to database schema
    const record = {
      id: user.getId().getValue(),
      email: user.getEmail().getValue(),
      name: user.getName(),
      status: user.getStatus(),
      created_at: user.getCreatedAt(),
      updated_at: user.getUpdatedAt()
    };
    
    await this.db.users.upsert(record);
  }
  
  public async findById(id: UserId): Promise<User | null> {
    const record = await this.db.users.findOne({
      where: { id: id.getValue() }
    });
    
    if (!record) return null;
    
    // Reconstitute domain object from database record
    return this.toDomain(record);
  }
  
  public async findByEmail(email: Email): Promise<User | null> {
    const record = await this.db.users.findOne({
      where: { email: email.getValue() }
    });
    
    return record ? this.toDomain(record) : null;
  }
  
  public async findMatching(spec: Specification<User>): Promise<User[]> {
    // Convert Specification to database query
    const query = spec.toQuery();
    const records = await this.db.users.findAll(query);
    return records.map(r => this.toDomain(r));
  }
  
  private toDomain(record: any): User {
    // Factory or reconstitution method
    return User.reconstitute(
      UserId.create(record.id),
      Email.create(record.email),
      record.name,
      record.status,
      record.created_at,
      record.updated_at
    );
  }
}

// In-Memory Implementation (for testing)
export class InMemoryUserRepository implements IUserRepository {
  private users: Map<string, User> = new Map();
  
  public async save(user: User): Promise<void> {
    this.users.set(user.getId().getValue(), user);
  }
  
  public async findById(id: UserId): Promise<User | null> {
    return this.users.get(id.getValue()) || null;
  }
  
  public async findByEmail(email: Email): Promise<User | null> {
    return Array.from(this.users.values())
      .find(u => u.getEmail().equals(email)) || null;
  }
  
  public async remove(user: User): Promise<void> {
    this.users.delete(user.getId().getValue());
  }
}
```

## 5. Factories

**Definition**: Encapsulates complex object creation logic, hiding internal structure.

**Key Characteristics**:
- Handles complex construction
- Enforces invariants during creation
- Shields client from internal structure
- Can create entire Aggregates

### Factory Template

```typescript
// Factory: Order Factory
// ID: FACTORY-ORDER-001
// Traceability: Supports #REQ-F-ORDER-003

export class OrderFactory {
  constructor(
    private productRepository: IProductRepository,
    private pricingService: PricingService
  ) {}
  
  // Create from shopping cart
  public async createFromCart(
    customerId: CustomerId,
    cart: ShoppingCart
  ): Promise<Order> {
    // Validate preconditions
    if (cart.isEmpty()) {
      throw new DomainError('Cannot create order from empty cart');
    }
    
    // Create aggregate root
    const order = Order.create(customerId);
    
    // Populate order lines from cart items
    for (const item of cart.getItems()) {
      // Fetch current product details
      const product = await this.productRepository.findById(item.productId);
      if (!product) {
        throw new DomainError(`Product ${item.productId} not found`);
      }
      
      // Get current price (may differ from cart)
      const price = await this.pricingService.getPrice(
        item.productId,
        item.quantity
      );
      
      // Add line to order
      order.addLine(item.productId, item.quantity, price);
    }
    
    // Postcondition: Order has all cart items
    if (order.getLines().length !== cart.getItemCount()) {
      throw new FactoryError('Order creation failed: line count mismatch');
    }
    
    return order;
  }
  
  // Reconstitute from database (alternative to Repository mapping)
  public reconstitute(
    id: OrderId,
    customerId: CustomerId,
    status: OrderStatus,
    lines: OrderLineData[],
    createdAt: Date
  ): Order {
    // Use private constructor or reflection to bypass validation
    // This is for loading existing valid data from database
    return Order.reconstitute(id, customerId, status, lines, createdAt);
  }
}
```

## 6. Domain Services

**Definition**: Stateless operations that don't naturally fit on Entity or Value Object.

**Key Characteristics**:
- Stateless (no instance state)
- Operates on domain objects
- Encapsulates domain logic
- Named with verbs (actions)

### Domain Service Template

```typescript
// Domain Service: Transfer Money Service
// ID: SVC-TRANSFER-001
// Traceability: Implements #REQ-F-PAYMENT-005

export class TransferMoneyService {
  // Domain service coordinates across Aggregates
  public transfer(
    fromAccount: Account,
    toAccount: Account,
    amount: Money
  ): void {
    // Preconditions
    if (amount.isNegative()) {
      throw new DomainError('Transfer amount must be positive');
    }
    
    // Enforce domain rules
    if (!fromAccount.canWithdraw(amount)) {
      throw new InsufficientFundsError(
        `Account ${fromAccount.getId()} has insufficient funds`
      );
    }
    
    // Coordinate across two Aggregates
    fromAccount.withdraw(amount);
    toAccount.deposit(amount);
    
    // Domain event
    // DomainEvents.raise(new MoneyTransferred(
    //   fromAccount.getId(),
    //   toAccount.getId(),
    //   amount
    // ));
  }
}

// Domain Service: Password Hashing (infrastructure concern in domain)
export interface IPasswordHasher {
  hash(plaintext: string): Promise<string>;
  verify(plaintext: string, hash: string): Promise<boolean>;
}

// Domain Service: Email uniqueness check
export class EmailUniquenessService {
  constructor(private userRepository: IUserRepository) {}
  
  public async isUnique(email: Email): Promise<boolean> {
    const existing = await this.userRepository.findByEmail(email);
    return existing === null;
  }
}
```

## 7. Specifications

**Definition**: Explicit predicate defining business rules for validation or selection.

**Key Characteristics**:
- Encapsulates business rule
- Reusable across contexts
- Composable (AND, OR, NOT)
- Can generate queries

### Specification Template

```typescript
// Specification: Abstract Base
export abstract class Specification<T> {
  public abstract isSatisfiedBy(candidate: T): boolean;
  
  // Composable operations
  public and(other: Specification<T>): Specification<T> {
    return new AndSpecification(this, other);
  }
  
  public or(other: Specification<T>): Specification<T> {
    return new OrSpecification(this, other);
  }
  
  public not(): Specification<T> {
    return new NotSpecification(this);
  }
  
  // For repository queries (optional)
  public abstract toQuery(): any;
}

// Concrete Specification: Overdue Invoice
export class OverdueInvoiceSpecification extends Specification<Invoice> {
  private readonly daysOverdue: number;
  
  constructor(daysOverdue: number = 30) {
    super();
    this.daysOverdue = daysOverdue;
  }
  
  public isSatisfiedBy(invoice: Invoice): boolean {
    if (invoice.isPaid()) {
      return false;
    }
    
    const daysSinceDue = invoice.getDaysSinceDueDate();
    return daysSinceDue > this.daysOverdue;
  }
  
  public toQuery(): any {
    // Convert to database query
    const cutoffDate = new Date();
    cutoffDate.setDate(cutoffDate.getDate() - this.daysOverdue);
    
    return {
      status: 'unpaid',
      dueDate: { $lt: cutoffDate }
    };
  }
}

// Usage in domain logic
export class InvoiceService {
  constructor(private invoiceRepository: IInvoiceRepository) {}
  
  public async sendOverdueReminders(): Promise<void> {
    const spec = new OverdueInvoiceSpecification(30);
    const overdueInvoices = await this.invoiceRepository.findMatching(spec);
    
    for (const invoice of overdueInvoices) {
      // Send reminder
    }
  }
  
  // Composite specification
  public async findCriticalInvoices(): Promise<Invoice[]> {
    const overdueSpec = new OverdueInvoiceSpecification(60);
    const highValueSpec = new HighValueInvoiceSpecification(10000);
    
    const criticalSpec = overdueSpec.and(highValueSpec);
    
    return this.invoiceRepository.findMatching(criticalSpec);
  }
}

// Composable specifications
class AndSpecification<T> extends Specification<T> {
  constructor(
    private left: Specification<T>,
    private right: Specification<T>
  ) {
    super();
  }
  
  public isSatisfiedBy(candidate: T): boolean {
    return this.left.isSatisfiedBy(candidate) && 
           this.right.isSatisfiedBy(candidate);
  }
  
  public toQuery(): any {
    return {
      $and: [this.left.toQuery(), this.right.toQuery()]
    };
  }
}
```

## 🎯 Pattern Selection Guide

```
Question: Does the object have a unique identity that matters over time?
├─ YES → Entity
└─ NO → Value Object

Question: Do multiple objects need to be treated as a unit for consistency?
├─ YES → Aggregate (with one Entity as root)
└─ NO → Standalone Entity or Value Object

Question: How do I access Aggregates from outside?
└─ Repository (for Aggregate Roots only)

Question: Is object creation complex?
├─ YES → Factory
└─ NO → Constructor or Factory Method on Entity

Question: Does this operation span multiple Entities or Aggregates?
├─ YES → Domain Service
└─ NO → Method on Entity

Question: Do I need to validate or query with complex business rules?
└─ Specification
```

## 📝 Checklist for Domain Model Design

### Entity Design
- [ ] Has unique identity (ID)
- [ ] Identity type is Value Object
- [ ] Equality based on ID only
- [ ] Domain behavior methods (not just getters/setters)
- [ ] Invariants enforced in methods
- [ ] Factory method or Factory for creation

### Value Object Design
- [ ] Immutable (no setters)
- [ ] Equality based on all attributes
- [ ] Validation in factory method
- [ ] Side-effect-free functions
- [ ] Used to encapsulate domain concepts

### Aggregate Design
- [ ] Has one Aggregate Root (Entity)
- [ ] Root enforces all invariants
- [ ] External references by ID only
- [ ] Kept small (2-3 entities max)
- [ ] Transaction boundary clearly defined
- [ ] Internal entities not exposed directly

### Repository Design
- [ ] Interface in Domain Layer
- [ ] Implementation in Infrastructure Layer
- [ ] Works with Aggregate Roots only
- [ ] Collection-like interface
- [ ] Returns fully reconstituted objects
- [ ] In-memory implementation for testing

### Domain Service Design
- [ ] Stateless (no instance variables)
- [ ] Named with verbs (actions)
- [ ] Coordinates across multiple Aggregates
- [ ] Encapsulates pure domain logic

## 🔗 Related Documentation

- [Ubiquitous Language Glossary](../ubiquitous-language.md)
- [Context Map](../../03-architecture/context-map.md)
- [Design by Contract](./design-by-contract.md)
- [XP Practices - Simple Design](../../docs/xp-practices.md#4-simple-design-)
- [Phase 05 Implementation](../../.github/instructions/phase-05-implementation.instructions.md)

---

**Standards Alignment**:
- IEEE 1016-2009 (Design Descriptions)
- ISO/IEC/IEEE 12207:2017 (Software Design Process)
- Model-Driven Design (MDD) principles
