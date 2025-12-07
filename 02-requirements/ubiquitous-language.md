# Ubiquitous Language Glossary

**Phase**: 02-Requirements  
**Standards**: ISO/IEC/IEEE 29148:2018  
**Purpose**: Single source of truth for domain terminology used consistently across requirements, design, code, and communication.

## 🎯 Purpose

The Ubiquitous Language is a shared vocabulary used by all team members (developers, domain experts, stakeholders) to ensure consistent understanding. Terms defined here MUST be used in:

- ✅ Requirements specifications
- ✅ Architecture decisions (ADRs)
- ✅ Code (class names, method names, variables)
- ✅ Tests
- ✅ Documentation
- ✅ Conversations with domain experts

**DDD Integration**: This glossary drives Model-Driven Design - when terms change here, code must be refactored to match. The domain model in code should be a direct reflection of the language captured here.

## 📚 Glossary Format

Each term entry must include:

| Field | Description |
|-------|-----------|
| **Term** | The canonical name used everywhere |
| **Context** | Bounded Context(s) where term applies |
| **Definition** | Clear, concise meaning from domain expert perspective |
| **Type** | DDD Pattern: Entity, Value Object, Aggregate, Service, Event |
| **Synonyms** | Alternative names (avoid using these) |
| **Relationships** | Related terms, parent concepts |
| **Examples** | Real-world usage examples |
| **Rules** | Business rules or constraints |
| **Code Mapping** | Class/interface names in implementation |
| **Traceability** | Link to requirements/issues |

---

## 📖 Domain Terms

### Account

**Context**: Banking, Finance  
**Definition**: A record of financial transactions for a customer, including balance and transaction history.  
**Type**: Entity (has identity and lifecycle)  
**Synonyms**: Bank Account, Financial Account (avoid using)  
**Relationships**:
- Parent: Customer (Entity)
- Composed of: AccountNumber (Value Object), Balance (Value Object)
- Related: Transaction (Entity), OverdraftLimit (Value Object)
**Examples**:
- Checking account with $1,500 balance
- Savings account with 2.5% interest rate
- Business account with $10,000 overdraft limit
**Business Rules**:
- Account balance cannot go below overdraft limit
- Each account has unique account number (immutable)
- Account status can be: Active, Suspended, Closed
- Only active accounts can process transactions
**Code Mapping**: `Account` class, `AccountRepository`, `AccountService`  
**Traceability**: #REQ-F-ACCOUNT-001, #REQ-NF-SECURITY-003  
**Examples**:
- Checking account with $1,500 balance
- Savings account with 2% interest rate

---

### Aggregate

**Context**: Software Design (DDD)  
**Definition**: A cluster of associated objects (Entities and Value Objects) treated as a single unit for data changes, with one Entity as the Aggregate Root.  
**Type**: Pattern  
**Relationships**:
- Contains: Entity (root), Entity (internal), Value Object
- Related: Aggregate Root, Bounded Context
**Business Rules**:
- All changes go through Aggregate Root
- External references by ID only
- One transaction modifies one Aggregate
**Traceability**: Architecture pattern (see `04-design/patterns/ddd-tactical-patterns.md`)  
**Examples**:
- Order Aggregate: Order (root) + OrderLines (internal entities)
- User Aggregate: User (root) + UserProfile (internal)

---

### Customer

**Context**: Sales, Support, Billing  
**Definition**: An individual or organization that purchases or uses our products/services.  
**Type**: Entity (has identity)  
**Synonyms**: Client, User (avoid - "User" means authenticated person)  
**Relationships**:
- Has: Account, Order, Invoice
- Related: CustomerProfile, BillingAddress
**Business Rules**:
- Must have valid email address
- Can have multiple accounts
- Customer status: Prospect, Active, Inactive, Suspended
**Traceability**: #REQ-F-CUSTOMER-001  
**Examples**:
- Individual customer: John Doe, john@example.com
- Corporate customer: Acme Corp, billing@acme.com

---

### Entity

**Context**: Software Design (DDD)  
**Definition**: An object defined by its identity rather than attributes, with continuity through time and different states.  
**Type**: Pattern  
**Distinguishing Feature**: Has unique identifier (ID)  
**Contrast With**: Value Object (defined by attributes, no identity)  
**Relationships**:
- Part of: Aggregate
- Contains: Value Objects
**Business Rules**:
- Equality based on ID only
- Must have immutable identity
- Can be mutable (state changes)
**Traceability**: Design pattern (see `04-design/patterns/ddd-tactical-patterns.md`)  
**Examples**:
- User (ID: user-123, email changes but ID stays same)
- Order (ID: order-456, status changes over time)

---

### Invoice

**Context**: Billing, Accounting  
**Definition**: A document requesting payment for goods or services provided, including line items, amounts, and due date.  
**Type**: Entity (has identity)  
**Synonyms**: Bill  
**Relationships**:
- Belongs to: Customer
- References: Order
- Contains: InvoiceLines (internal entities)
**Business Rules**:
- Must have unique invoice number
- Total equals sum of line items
- Status: Draft, Issued, Paid, Overdue, Cancelled
- Cannot modify issued invoice (only cancel and reissue)
**Traceability**: #REQ-F-BILLING-001, #REQ-F-BILLING-002  
**Examples**:
- Invoice #2024-001: $1,250.00, Due: 2024-12-15, Status: Issued

---

### Order

**Context**: Sales, Fulfillment  
**Definition**: A customer's request to purchase products or services, including items, quantities, and total amount.  
**Type**: Aggregate Root (Entity)  
**Synonyms**: Purchase Order  
**Relationships**:
- Placed by: Customer
- Contains: OrderLines (internal entities)
- Generates: Invoice
**Business Rules**:
- Must have at least one order line
- Status: Draft, Submitted, Confirmed, Shipped, Delivered, Cancelled
- Cannot modify submitted order (only cancel)
- Total equals sum of order lines
**Invariants**:
- Order total = sum of (quantity × price) for all lines
- All line quantities must be positive
**Traceability**: #REQ-F-ORDER-001, #ADR-SALES-001  
**Examples**:
- Order #5678: Customer C-123, 3 items, $350.00, Status: Submitted

---

### Repository

**Context**: Software Design (DDD)  
**Definition**: An interface providing collection-like access to Aggregate Roots, abstracting persistence mechanism.  
**Type**: Pattern  
**Synonyms**: Data Repository (avoid - implies data-centric)  
**Relationships**:
- Works with: Aggregate Root
- Implemented in: Infrastructure Layer
- Defined in: Domain Layer
**Business Rules**:
- One Repository per Aggregate Root
- Interface in Domain Layer, implementation in Infrastructure
- Returns fully reconstituted domain objects
**Traceability**: Design pattern (see `04-design/patterns/ddd-tactical-patterns.md`)  
**Examples**:
- `IOrderRepository.findById(orderId)` → Order aggregate
- `IUserRepository.save(user)` → Persist User aggregate

---

### User

**Context**: Authentication, Authorization  
**Definition**: An authenticated person who can access the system with specific permissions.  
**Type**: Entity (has identity)  
**Synonyms**: Account Holder (avoid context confusion)  
**Contrast With**: Customer (purchases products, may not have login)  
**Relationships**:
- May be: Customer (if registered customer has login)
- Has: Roles, Permissions
**Business Rules**:
- Must have unique email address
- Must have hashed password
- Status: Active, Suspended, Locked, Inactive
- Cannot have duplicate email addresses
**Traceability**: #REQ-F-AUTH-001, #REQ-NF-SECURITY-001  
**Examples**:
- User: alice@example.com, Role: Admin, Status: Active

---

### Value Object

**Context**: Software Design (DDD)  
**Definition**: An immutable object defined by its attributes with no conceptual identity, used to describe characteristics of things.  
**Type**: Pattern  
**Distinguishing Feature**: No identity (ID), equality based on all attributes  
**Contrast With**: Entity (has identity)  
**Relationships**:
- Contained by: Entity, Aggregate
- Examples: Email, Money, Address
**Business Rules**:
- Immutable (no setters)
- Equality = attribute equality
- Side-effect-free functions
- Create new instance for changes
**Traceability**: Design pattern (see `04-design/patterns/ddd-tactical-patterns.md`)  
**Examples**:
- Email: "user@example.com" (string value, no ID)
- Money: {amount: 100.00, currency: "USD"}
- Address: {street, city, postalCode, country}

---

## 🔄 Context-Specific Terms

Some terms have different meanings in different Bounded Contexts. Document these clearly:

### "Account" - Context Variations

| Context | Meaning | Example |
|---------|---------|---------|
| **Banking** | Financial account with balance | Checking Account #1234 |
| **Authentication** | User login credentials | User account: alice@example.com |
| **Accounting** | General ledger account | Revenue Account #4000 |

**Resolution**: Use qualified names when crossing contexts:
- BankAccount (Banking)
- UserAccount (Authentication)
- LedgerAccount (Accounting)

---

### "User" - Context Variations

| Context | Meaning | Example |
|---------|---------|---------|
| **Authentication** | Person with login credentials | Authenticated user |
| **Customer Management** | Person who purchases | Customer (prefer this term) |
| **Support** | Person receiving support | Support case assignee |

**Resolution**: Prefer specific terms:
- Use "User" only in Authentication context
- Use "Customer" in Sales/Support contexts
- Use "Operator" for admin/system users

---

## 📋 Term Addition Process

When adding a new term to the glossary:

1. **Confirm with Domain Expert** - Ensure term is accurate and agreed upon
2. **Create GitHub Issue** - Create requirement or architecture issue
3. **Add to Glossary** - Use template above
4. **Update Code** - Refactor existing code to use new term
5. **Update Tests** - Ensure tests use canonical term
6. **Document in ADR** - If term affects architecture, create ADR

**Template for New Term**:

```markdown
### [Term Name]

**Context**: [Bounded Context(s)]  
**Definition**: [Clear definition from domain expert]  
**Type**: [Entity | Value Object | Service | Concept]  
**Synonyms**: [Alternative names to avoid]  
**Relationships**:
- [Relationship type]: [Related term(s)]
**Business Rules**:
- [Rule 1]
- [Rule 2]
**Traceability**: [#Issue numbers]  
**Examples**:
- [Example 1]
- [Example 2]
```

---

## ✅ Glossary Maintenance Checklist

### When Creating Requirements (Phase 02)
- [ ] Identify domain terms in user stories
- [ ] Add new terms to glossary
- [ ] Use canonical terms in requirement text

### When Designing (Phase 04)
- [ ] Class names match glossary terms
- [ ] Method names use ubiquitous language
- [ ] Design patterns reflect domain concepts

### When Implementing (Phase 05)
- [ ] Variable names use glossary terms
- [ ] Code comments use canonical terminology
- [ ] Avoid technical jargon not in glossary

### When Testing (Phase 07)
- [ ] Test names use ubiquitous language
- [ ] Test scenarios match domain examples
- [ ] Acceptance criteria use canonical terms

### When Documenting (Phase 08)
- [ ] User documentation uses glossary terms
- [ ] Training materials consistent with glossary
- [ ] Help text uses canonical terminology

---

## 🚨 Anti-Patterns to Avoid

❌ **Don't use technical terms when domain term exists**:
- Bad: `DataAccessObject`, `EntityManager`
- Good: `Repository`, `Order`

❌ **Don't use abbreviations not agreed by domain experts**:
- Bad: `Cust`, `Ord`, `Inv`
- Good: `Customer`, `Order`, `Invoice`

❌ **Don't use same term for different concepts**:
- Bad: "User" for both Customer and AuthenticatedUser
- Good: "Customer" (Sales context), "User" (Auth context)

❌ **Don't create synonyms in code**:
- Bad: `Client`, `Customer`, `Purchaser` used interchangeably
- Good: Pick one canonical term (`Customer`) and use consistently

---

## 🔗 Related Documentation

- [Context Map](../03-architecture/context-map.md) - Bounded Context relationships
- [DDD Tactical Patterns](../04-design/patterns/ddd-tactical-patterns.md) - Pattern definitions
- [Requirements Specification Template](../spec-kit-templates/requirements-spec.md)
- [XP Practices - Simple Design](../docs/xp-practices.md#4-simple-design-)

---

## 📊 Glossary Metrics

| Metric | Target | Purpose |
|--------|--------|---------|
| Terms defined | 20+ core terms | Comprehensive coverage |
| Terms with examples | 100% | Clarity for developers |
| Terms with traceability | 100% | Link to requirements |
| Context conflicts resolved | All | No ambiguity |

---

**Standards Alignment**:
- ISO/IEC/IEEE 29148:2018 (Requirements Engineering)
- ISO/IEC/IEEE 12207:2017 (Software Life Cycle Processes)
- Domain-Driven Design (Evans, 2003)

**Version**: 1.0  
**Last Updated**: 2025-11-27  
**Owner**: Requirements Team
