# Domain-Driven Design (DDD) Implementation Guide

**Standards**: IEEE 1016-2009 (Software Design), ISO/IEC/IEEE 42010:2011 (Architecture)  
**Phase Integration**: Primarily Phase 02 (Requirements) and Phase 04 (Design)  
**XP Alignment**: Simple Design, Refactoring, Test-Driven Development

## 🎯 Overview

Domain-Driven Design (DDD) is an approach to building complex software systems by focusing intently on the **core domain** and creating a design that closely mirrors an evolving model of that domain. This guide integrates DDD practices with our standards-compliant software development lifecycle.

## 🔄 DDD Integration Across Lifecycle Phases

### Phase 01: Stakeholder Requirements → Domain Discovery


- **Activity**: Knowledge Crunching sessions with domain experts

- **GitHub Issues**: Create `type:stakeholder-requirement` issues for domain concepts

- **Deliverable**: Initial domain understanding and stakeholder register

### Phase 02: Requirements Analysis → Domain Modeling


- **Activity**: Develop Ubiquitous Language and identify Bounded Contexts

- **GitHub Issues**: Create `type:requirement:functional` issues using domain terminology

- **Deliverable**: Ubiquitous Language glossary and Context Map

### Phase 03: Architecture Design → Strategic Design


- **Activity**: Define Bounded Contexts boundaries and relationships

- **GitHub Issues**: Create `type:architecture:decision` issues for context boundaries

- **Deliverable**: Context Map and ADRs for strategic decisions

### Phase 04: Detailed Design → Tactical Design


- **Activity**: Apply tactical patterns (Entity, Value Object, Aggregate, etc.)

- **GitHub Issues**: Link design decisions to architecture component issues

- **Deliverable**: Domain model with tactical patterns

### Phase 05: Implementation → Model-Driven Code


- **Activity**: Code that directly reflects the domain model

- **XP Integration**: TDD with domain-focused tests

- **GitHub Issues**: Link PRs to domain concept issues

## I. Foundational Activities: Model and Language

### 1. Collaborative Knowledge Crunching

**Process**: Rigorous collaboration between developers and domain experts to distill information and discover abstract concepts.

**Implementation in Our Framework**:

- **Phase 01-02**: Schedule knowledge crunching sessions with stakeholders

- **GitHub Issues**: Create `type:stakeholder-requirement` issues for each domain concept discovered

- **Documentation**: Record insights in `01-stakeholder-requirements/domain-sessions/`

**Issue Template Example**:

```markdown
# Domain Concept: [Concept Name]

**Type**: Domain Concept Discovery
**Phase**: 01-Stakeholder-Requirements
**Domain Expert**: [Name]
**Session Date**: [Date]

## Definition
[Clear definition from domain expert perspective]

## Business Rules
- [Rule 1]
- [Rule 2]

## Relationships
- Related to: #[other domain concepts]
- Part of: #[larger concept]

## Questions for Next Session
- [Open questions]

## Traceability
- Discovered in session: [Session ID/Date]
- Validated by: [Domain Expert]

```


### 2. Cultivate a Ubiquitous Language (UL)

**Objective**: Create a shared vocabulary used consistently across all communication, documentation, and code.

**Implementation**:

- **Primary Location**: `02-requirements/ubiquitous-language.md`

- **Code Requirements**: Class names, method names, and variables MUST use UL terms

- **GitHub Integration**: All issues must use UL terminology

**Critical Rules**:
- ✅ Names in code come from the UL
- ✅ When UL changes, code must be refactored (rename classes/methods)
- ✅ All team communication uses UL terms
- ✅ Tests describe behavior using UL

**Example UL Entry**:

```markdown
### Order

**Context**: Sales, Fulfillment
**Definition**: A customer's request to purchase specific products with agreed pricing and delivery terms
**Type**: Entity (has identity and lifecycle)
**Synonyms**: Purchase Order, Customer Order (avoid using)
**Lifecycle States**: Draft → Submitted → Confirmed → Fulfilled → Cancelled
**Business Rules**:
- Order total must be > $0
- Cannot modify confirmed orders
- Cancelled orders retain history
**Code Mapping**: `Order` class, `OrderService`, `OrderRepository`
**Traceability**: #45 (REQ-F-ORDER-001), #67 (REQ-F-ORDER-CANCEL)

```


### 3. Practice Continuous Refactoring and Iteration

**XP Integration**: The modeling and design process is a single iterative loop where code changes reflect model evolution.

**Implementation**:

- **Hands-On Modelers**: Developers actively participate in domain modeling

- **Code-Model Alignment**: Changing code changes the model and vice versa

- **Refactoring**: When UL evolves, refactor code to maintain alignment

**Process**:

1. **Model Change** → Update UL in `02-requirements/ubiquitous-language.md`

2. **Code Refactoring** → Rename classes/methods to match new UL

3. **Test Updates** → Update test descriptions to use new terminology

4. **Documentation** → Update all references to use new terms

## II. Strategic Design: Managing the Big Picture

### 4. Define Bounded Contexts and Context Map

**Bounded Context**: Explicit boundary within which a domain model is unified and logically consistent.

**Implementation**:

- **Phase 03**: Create `type:architecture:decision` issues for each Bounded Context

- **Documentation**: `03-architecture/context-map.md`

- **Principle**: Contexts defined around business capabilities, not technical concerns

**Context Map Template** (`03-architecture/context-map.md`):

```markdown
# Context Map

**Architecture Issues**: #78 (ADR-CONTEXT-001), #79 (ADR-CONTEXT-002)
**Requirements Traced**: #45, #46, #47

## Bounded Contexts

### Sales Context

- **Responsibility**: Handle customer orders, pricing, promotions

- **Core Entities**: Customer, Order, Product, Price

- **GitHub Issues**: #78 (ADR-SALES-CONTEXT)

- **Team**: Sales Development Team

### Inventory Context  

- **Responsibility**: Track product availability, warehouse management

- **Core Entities**: Product, Stock, Warehouse, Shipment

- **GitHub Issues**: #79 (ADR-INVENTORY-CONTEXT)

- **Team**: Fulfillment Team

## Context Relationships

### Sales → Inventory (Customer-Supplier)

- **Integration**: Sales requests stock availability

- **Protocol**: REST API calls to Inventory Service

- **Shared Kernel**: Product ID only

- **Anti-Corruption Layer**: Sales translates Inventory responses to Sales model

```


### 5. Distill the Core Domain

**Objective**: Identify the most valuable and specialized concepts that differentiate the business.

**Implementation**:

- **Phase 02**: Create `priority:p0` labels for Core Domain requirements

- **Core Domain**: Apply top talent and greatest effort

- **Generic Subdomains**: Consider off-the-shelf solutions or separate modules

**Classification in GitHub Issues**:

```markdown
Labels for Domain Classification:
- domain:core - Core business differentiator
- domain:supporting - Important but not differentiating  
- domain:generic - Common functionality (consider buying)

```


### 6. Isolate the Domain Layer

**Architecture**: Adopt Layered Architecture to separate domain logic from infrastructure.

**Implementation**:

- **Phase 04**: Create `type:architecture:component` issues for each layer

- **Domain Layer**: All domain model code in isolated layer

- **Structure**: `05-implementation/src/domain/` (isolated from UI, persistence, etc.)

**Layer Structure**:

```

05-implementation/src/
├── domain/           # Core domain model (isolated)
│   ├── entities/
│   ├── value-objects/
│   ├── aggregates/
│   ├── services/
│   └── specifications/
├── application/      # Application services (use cases)
├── infrastructure/   # Persistence, external services
└── presentation/     # UI, controllers, APIs

```


## III. Tactical Patterns: Modeling Detail Elements

### 7. Model Objects as Entities, Value Objects, or Services

**Classification Decision Tree**:


```markdown
Does it need to be tracked over time with identity?
├─ YES → **ENTITY**
│   └─ Focus on identity and lifecycle
└─ NO → Does it describe something else?
    ├─ YES → **VALUE OBJECT**  
    │   └─ Make immutable, focus on attributes
    └─ NO → Is it an important business operation?
        └─ YES → **DOMAIN SERVICE**
            └─ Make stateless, model as behavior

```


#### Entity Pattern

**Definition**: Object defined by continuity and identity throughout lifecycle.

**Implementation**:

```typescript
/**
 * Customer Entity
 * Implements: #45 (REQ-F-CUSTOMER-001)
 * UL Term: Customer (see ubiquitous-language.md)
 */
class Customer {
  constructor(
    private readonly id: CustomerId,  // Value Object for identity
    private name: CustomerName,       // Value Object
    private email: Email             // Value Object
  ) {}
  
  // Focus on identity-based operations
  changeEmail(newEmail: Email): void {
    // Business rule validation
    this.email = newEmail;
  }
  
  // Equality based on identity, not attributes
  equals(other: Customer): boolean {
    return this.id.equals(other.id);
  }
}

```


#### Value Object Pattern

**Definition**: Describes state of something else; defined by attributes; immutable.

**Implementation**:

```typescript
/**
 * Email Value Object
 * UL Term: Email (see ubiquitous-language.md)
 */
class Email {
  constructor(private readonly value: string) {
    if (!this.isValid(value)) {
      throw new Error('Invalid email format');
    }
  }
  
  // Equality based on attributes
  equals(other: Email): boolean {
    return this.value === other.value;
  }
  
  toString(): string {
    return this.value;
  }
  
  private isValid(email: string): boolean {
    // Validation logic
    return /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email);
  }
}

```


#### Domain Service Pattern

**Definition**: Stateless operation that doesn't belong to any Entity or Value Object.

**Implementation**:

```typescript
/**
 * Pricing Service
 * Implements: #67 (REQ-F-PRICING-001)
 * UL Term: Pricing (see ubiquitous-language.md)  
 */
class PricingService {
  calculateOrderTotal(
    order: Order, 
    customer: Customer, 
    promotions: Promotion[]
  ): Money {
    // Complex pricing logic that doesn't belong 
    // to Order or Customer
    let total = order.getSubtotal();
    
    for (const promotion of promotions) {
      if (promotion.appliesTo(customer, order)) {
        total = promotion.apply(total);
      }
    }
    
    return total;
  }
}

```


### 8. Define Aggregates to Enforce Integrity

**Aggregate**: Cluster of Entities and Value Objects with consistency boundary.

**Rules**:
- ✅ One Entity is the Aggregate Root
- ✅ External access only through Root
- ✅ Enforce invariants within boundary
- ✅ Transaction boundary = Aggregate boundary

**Implementation**:

```typescript
/**
 * Order Aggregate Root
 * Implements: #45 (REQ-F-ORDER-001)
 * Aggregate: Order (Root), OrderLine (Entity), Money (VO)
 */
class Order {
  constructor(
    private readonly id: OrderId,
    private customerId: CustomerId,
    private lines: OrderLine[] = []
  ) {}
  
  // All access to OrderLine goes through Order (Root)
  addLine(productId: ProductId, quantity: Quantity, price: Money): void {
    // Enforce aggregate invariants
    if (this.status === OrderStatus.CONFIRMED) {
      throw new Error('Cannot modify confirmed order');
    }
    
    const line = new OrderLine(productId, quantity, price);
    this.lines.push(line);
  }
  
  // Aggregate ensures consistency
  confirm(): void {
    if (this.lines.length === 0) {
      throw new Error('Cannot confirm empty order');
    }
    
    this.status = OrderStatus.CONFIRMED;
    // Domain event: OrderConfirmed
  }
  
  getTotal(): Money {
    return this.lines.reduce(
      (total, line) => total.add(line.getSubtotal()),
      Money.zero()
    );
  }
}

```


### 9. Encapsulate Complex Lifecycle Management

#### Factory Pattern

**Purpose**: Abstract complex object creation while protecting internal structure.

**Implementation**:

```typescript
/**
 * Order Factory
 * Creates complex Order aggregates
 */
class OrderFactory {
  static createFromCustomerCart(
    customerId: CustomerId, 
    cartItems: CartItem[]
  ): Order {
    // Complex assembly logic
    const order = new Order(OrderId.generate(), customerId);
    
    for (const item of cartItems) {
      const price = this.pricingService.getPrice(item.productId, customerId);
      order.addLine(item.productId, item.quantity, price);
    }
    
    return order;
  }
  
  static reconstitute(orderData: OrderData): Order {
    // Rebuild from persistence
    const order = new Order(orderData.id, orderData.customerId);
    // ... restore state
    return order;
  }
}

```


#### Repository Pattern

**Purpose**: Provide illusion of in-memory collection for Aggregate Roots.

**Implementation**:

```typescript
/**
 * Order Repository Interface (Domain Layer)
 * Implements: #89 (ADR-REPO-001: Repository Pattern)
 */
interface OrderRepository {
  save(order: Order): Promise<void>;
  findById(id: OrderId): Promise<Order | null>;
  findByCustomer(customerId: CustomerId): Promise<Order[]>;
  findConfirmedOrders(): Promise<Order[]>;
}

/**
 * Order Repository Implementation (Infrastructure Layer)
 */
class SqlOrderRepository implements OrderRepository {
  constructor(private db: Database) {}
  
  async save(order: Order): Promise<void> {
    // Translate domain model to database schema
    const orderData = OrderMapper.toPersistence(order);
    await this.db.orders.upsert(orderData);
  }
  
  async findById(id: OrderId): Promise<Order | null> {
    const data = await this.db.orders.findOne({ id: id.value });
    return data ? OrderMapper.toDomain(data) : null;
  }
}

```


## 🔗 GitHub Issues Integration

### DDD-Specific Issue Types

Add DDD-focused issue templates:


```markdown
# Domain Concept Template
name: Domain Concept
about: Define a core domain concept or business rule
title: "[DOMAIN] "
labels: ["type:domain-concept", "phase:02-requirements"]

## Domain Concept
**Ubiquitous Language Term**: 

## Definition
[From domain expert perspective]

## Business Rules
- 

## Relationships  
- Parent concept: #
- Related concepts: #

## Implementation Impact
**Suggested Pattern**: [ ] Entity [ ] Value Object [ ] Service [ ] Aggregate
**Rationale**: 

## Acceptance Criteria
- [ ] Added to Ubiquitous Language glossary
- [ ] Validated by domain expert
- [ ] Implementation pattern decided

```


### Traceability for DDD


```markdown
Domain Concept (#45) → Functional Requirement (#67) → Architecture Decision (#78) → Design Component (#89) → Implementation (PR #12) → Domain Test (#99)

```


## ✅ DDD Quality Checklist

### Model-Driven Design
- [ ] Code directly reflects domain model
- [ ] Class/method names come from Ubiquitous Language
- [ ] Domain logic isolated in domain layer
- [ ] Business rules enforced in domain objects

### Ubiquitous Language
- [ ] Consistent terminology across code, tests, docs
- [ ] Domain experts can read and understand code structure
- [ ] No technical jargon in domain model
- [ ] UL glossary maintained and current

### Strategic Design
- [ ] Bounded Contexts explicitly defined
- [ ] Context boundaries align with team ownership
- [ ] Core Domain identified and prioritized
- [ ] Context Map documents relationships

### Tactical Patterns
- [ ] Entities focus on identity and lifecycle
- [ ] Value Objects are immutable and behavior-rich
- [ ] Aggregates enforce consistency boundaries
- [ ] Repositories provide collection illusion
- [ ] Domain Services model pure business operations

## 📚 Resources and Further Reading


- **Ubiquitous Language**: `02-requirements/ubiquitous-language.md`

- **Context Map**: `03-architecture/context-map.md`

- **Tactical Patterns**: `04-design/patterns/ddd-tactical-patterns.md`

- **Domain Layer Structure**: `05-implementation/src/domain/README.md`

## 🚨 Common Anti-Patterns to Avoid

❌ **Anemic Domain Model**: Domain objects with only getters/setters and no behavior  
❌ **Smart UI**: Business logic in presentation layer  
❌ **Shared Database**: Multiple Bounded Contexts sharing same database  
❌ **Generic Subdomains in Core**: Treating utilities as core domain  
❌ **Technical Ubiquitous Language**: Using technical terms instead of business language  
❌ **God Aggregates**: Aggregates that are too large and violate consistency boundaries  
❌ **Repository for Everything**: Creating repositories for non-Aggregate Roots  

---

**Remember**: DDD is about making implicit concepts explicit and aligning code with business understanding. Start with the domain, not the database! 🚀
