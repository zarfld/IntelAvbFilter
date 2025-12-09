# Context Map

**Phase**: 03-Architecture  
**Standards**: ISO/IEC/IEEE 42010:2011  
**Purpose**: Visual representation of relationships between Bounded Contexts and integration patterns.

## 🎯 Purpose

The Context Map documents:

- All Bounded Contexts in the system
- Relationships and integration patterns between contexts
- Team ownership and responsibilities
- Translation mechanisms at context boundaries
- Strategic design decisions

**Traceability**: Links to ADRs documenting integration decisions

---

## 🗺️ Context Map Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        CONTEXT MAP - Project Name                            │
│                                                                               │
│  ┌──────────────────┐                                                        │
│  │  Authentication  │  U                                                     │
│  │    Context       │──────┐                                                 │
│  │  (Core Domain)   │      │ OHS                                             │
│  └──────────────────┘      │                                                 │
│         │                  │                                                 │
│         │ ACL              ▼                                                 │
│         │          ┌──────────────────┐                                      │
│         │          │   User Profile   │                                      │
│         └─────────▶│     Context      │                                      │
│                    │  (Supporting)    │                                      │
│                    └──────────────────┘                                      │
│                            │                                                 │
│                            │ CF                                              │
│                            │                                                 │
│  ┌──────────────────┐     │    ┌──────────────────┐                         │
│  │    Billing       │     │    │   Notification   │                         │
│  │    Context       │◀────┴───▶│     Context      │                         │
│  │  (Generic)       │  PL      │  (Generic)       │                         │
│  └──────────────────┘          └──────────────────┘                         │
│         │                              │                                     │
│         │ OHS                          │ OHS                                 │
│         │                              │                                     │
│         ▼                              ▼                                     │
│  ┌──────────────────┐          ┌──────────────────┐                         │
│  │     Payment      │          │      Email       │                         │
│  │    Gateway       │          │    Service       │                         │
│  │  (External)      │          │  (External)      │                         │
│  └──────────────────┘          └──────────────────┘                         │
│                                                                               │
│  Legend:                                                                      │
│  ───▶  Upstream/Downstream (U/D)    ACL: Anti-Corruption Layer              │
│  ◀──▶  Partnership (P)              OHS: Open Host Service                  │
│  ═══▶  Customer/Supplier (C/S)      PL: Published Language                  │
│                                     CF: Conformist                           │
│                                     SK: Shared Kernel                        │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 📋 Bounded Contexts Inventory

### Authentication Context

**Domain Type**: Core Domain  
**Team**: Security Team  
**Responsibility**: User authentication, session management, access control  
**Ubiquitous Language**: User, Session, Credential, Permission, Role  
**Key Entities**: User, Session, Role  
**Integration Pattern**: Open Host Service (OHS)  
**API**: RESTful API with JWT tokens  
**Traceability**: #ADR-SECU-001, #REQ-F-AUTH-001  

**Relationships**:
- → User Profile Context (ACL) - Provides user identity
- → Billing Context (OHS) - Provides authentication

---

### User Profile Context

**Domain Type**: Supporting Subdomain  
**Team**: Customer Management Team  
**Responsibility**: User profile data, preferences, settings  
**Ubiquitous Language**: Profile, Preference, Setting, Avatar  
**Key Entities**: UserProfile, Preference  
**Integration Pattern**: Conformist (CF) with Authentication  
**Traceability**: #ADR-USER-001, #REQ-F-PROFILE-001  

**Relationships**:
- ← Authentication Context (Conformist) - Consumes User identity
- → Notification Context (Published Language) - Profile updates trigger notifications

---

### Billing Context

**Domain Type**: Generic Subdomain  
**Team**: Finance Team  
**Responsibility**: Invoice generation, payment tracking, subscriptions  
**Ubiquitous Language**: Invoice, Payment, Subscription, Charge  
**Key Aggregates**: Invoice, Subscription  
**Integration Pattern**: Customer/Supplier with Payment Gateway  
**Traceability**: #ADR-BILLING-001, #REQ-F-BILLING-001  

**Relationships**:
- ← Authentication Context (OHS) - Gets authenticated user
- → Payment Gateway (Customer/Supplier) - External payment processing
- → Notification Context (Published Language) - Payment events

---

### Notification Context

**Domain Type**: Generic Subdomain  
**Team**: Infrastructure Team  
**Responsibility**: Send notifications via email, SMS, push  
**Ubiquitous Language**: Notification, Template, Channel, Delivery  
**Key Entities**: Notification, Template  
**Integration Pattern**: Open Host Service (OHS)  
**Traceability**: #ADR-NOTIF-001, #REQ-F-NOTIF-001  

**Relationships**:
- ← Multiple contexts (OHS) - Receives notification requests
- → Email Service (Open Host Service) - External email delivery

---

### Payment Gateway (External)

**Domain Type**: External System  
**Vendor**: Stripe / PayPal  
**Integration Pattern**: Anti-Corruption Layer (ACL) in Billing Context  
**Why ACL**: Protect domain model from external API changes  
**Traceability**: #ADR-BILLING-002  

---

## 🔗 Integration Patterns

### Upstream/Downstream (U/D)

**Definition**: One context (upstream) influences another (downstream), but not vice versa.

**Example**: Authentication (U) → User Profile (D)

**Characteristics**:
- Upstream defines API/interface
- Downstream consumes it
- Changes flow one direction

**When to Use**: Clear dependency, one context provides services to another

---

### Customer/Supplier (C/S)

**Definition**: Downstream (customer) needs can influence upstream (supplier) development.

**Example**: Billing (C) → Payment Gateway (S)

**Characteristics**:
- Negotiated relationship
- Both teams collaborate on interface
- Customer feedback shapes supplier API

**When to Use**: Both teams in same organization, need collaboration

---

### Partnership (P)

**Definition**: Two contexts succeed or fail together, requiring close coordination.

**Example**: Order Context ↔ Inventory Context

**Characteristics**:
- Mutual dependency
- Coordinated releases
- Shared success criteria

**When to Use**: Tightly coupled business processes, must change together

---

### Shared Kernel (SK)

**Definition**: Small subset of domain model shared between contexts.

**Example**: Shared identity/address types between contexts

**Characteristics**:
- Shared code/database
- High coordination overhead
- Changes require both teams' approval
- Keep very small

**When to Use**: Core concepts truly shared, high cohesion, only if benefits outweigh cost

**Warning**: High maintenance cost, prefer other patterns

---

### Conformist (CF)

**Definition**: Downstream context conforms to upstream model without translation.

**Example**: User Profile Context conforms to Authentication Context

**Characteristics**:
- No translation layer
- Uses upstream's model directly
- Simpler but couples to upstream

**When to Use**: Upstream model fits well, low cost of conforming, upstream is stable

---

### Anti-Corruption Layer (ACL)

**Definition**: Translation layer protecting downstream context from upstream changes.

**Example**: Billing Context uses ACL to interact with Payment Gateway

**Characteristics**:
- Isolates domain model
- Translates between models
- Protects from external changes

**When to Use**:
- Integrating with external systems
- Upstream model doesn't fit domain
- Upstream changes frequently
- Legacy system integration

**Implementation**:

```typescript
// Anti-Corruption Layer for Payment Gateway
export class PaymentGatewayAdapter {
  constructor(private gateway: StripeAPI) {}
  
  // Translate domain concept to external API
  public async processPayment(payment: Payment): Promise<PaymentResult> {
    // Domain model
    const amount = payment.getAmount();
    const currency = payment.getCurrency();
    
    // Translate to external API format
    const stripeRequest = {
      amount: amount.toMinorUnits(),  // $10.00 → 1000 cents
      currency: currency.toLowerCase(), // USD → usd
      source: payment.getPaymentMethodId(),
      description: payment.getDescription()
    };
    
    // Call external API
    const stripeResponse = await this.gateway.charges.create(stripeRequest);
    
    // Translate response back to domain model
    return PaymentResult.create({
      transactionId: TransactionId.create(stripeResponse.id),
      status: this.mapStatus(stripeResponse.status),
      processedAt: new Date(stripeResponse.created * 1000)
    });
  }
  
  private mapStatus(stripeStatus: string): PaymentStatus {
    // Protect domain from external changes
    switch (stripeStatus) {
      case 'succeeded': return PaymentStatus.Completed;
      case 'pending': return PaymentStatus.Processing;
      case 'failed': return PaymentStatus.Failed;
      default: throw new Error(`Unknown Stripe status: ${stripeStatus}`);
    }
  }
}
```

---

### Open Host Service (OHS)

**Definition**: Context provides well-defined API/protocol for all consumers.

**Example**: Authentication Context exposes RESTful API

**Characteristics**:
- Public, versioned API
- Documentation (OpenAPI/Swagger)
- Backward compatibility maintained
- Multiple consumers

**When to Use**: Multiple contexts need access, context is stable enough to define API

---

### Published Language (PL)

**Definition**: Shared, well-documented data format for integration (JSON, XML, events).

**Example**: Notification events use standardized JSON schema

**Characteristics**:
- Standardized format
- Schema versioning
- Documentation
- Language-agnostic

**When to Use**: Event-driven architecture, async communication, multiple consumers

---

## 📝 Context Relationship Template

When documenting a relationship, use this template:

```markdown
### [Upstream Context] → [Downstream Context]

**Pattern**: [U/D | C/S | P | CF | ACL | OHS | PL]  
**Direction**: [→ | ← | ↔]  
**Integration Method**: [REST API | Events | Shared Database | Message Queue]  
**Data Format**: [JSON | XML | Protobuf]  
**Authentication**: [JWT | OAuth2 | API Key | None]  
**Translation**: [ACL in Downstream | None | Shared Kernel]  

**Exposed Concepts** (from Upstream):
- Concept 1 (example: User with ID, email, status)
- Concept 2

**Consumed Concepts** (by Downstream):
- How concept is used in downstream context

**Anti-Corruption Layer** (if applicable):
- Translation logic location: `src/adapters/[upstream]-adapter.ts`
- Maps: Upstream.User → Downstream.AuthenticatedUser

**Traceability**: #ADR-XXX-YYY, #REQ-F-ZZZ-NNN
```

---

## 🎯 Strategic Design Decisions

### Core Domain

**Contexts**: Authentication, Order Management  
**Investment**: High (custom development, domain experts, best developers)  
**Why**: Competitive advantage, unique business logic

### Supporting Subdomain

**Contexts**: User Profile, Reporting  
**Investment**: Medium (custom but not differentiating)  
**Why**: Necessary but not competitive advantage

### Generic Subdomain

**Contexts**: Notification, Email, Logging  
**Investment**: Low (prefer off-the-shelf solutions)  
**Why**: Solved problems, no competitive advantage

**Buy vs. Build Decision Matrix**:

| Context | Type | Decision | Rationale |
|---------|------|----------|-----------|
| Authentication | Core | Build | Unique security requirements |
| Notification | Generic | Buy (SendGrid) | Commodity service |
| Payment | Generic | Buy (Stripe) | Compliance, PCI-DSS |
| Billing | Supporting | Build | Custom business rules |

**Traceability**: #ADR-STRAT-001

---

## 🚧 Evolution and Changes

### Adding a New Context

1. **Identify Bounded Context**
   - Define responsibility
   - Identify ubiquitous language
   - Determine domain type (Core/Supporting/Generic)

2. **Update Context Map**
   - Add context to diagram
   - Define relationships with existing contexts
   - Choose integration patterns

3. **Create ADR**
   - Document decision to add context
   - Justify integration patterns
   - Link to requirements

4. **Implement Boundaries**
   - Create separate module/service
   - Define public API
   - Implement ACL if needed

**Traceability**: #ADR-[CONTEXT]-001

### Changing Integration Pattern

**Example**: Move from Shared Kernel to Open Host Service

**Reason**: Reduce coupling, allow independent evolution

**Steps**:
1. Create ADR documenting change
2. Define new API contract
3. Implement API in upstream context
4. Create adapter in downstream context
5. Migrate consumers
6. Remove shared kernel

**Traceability**: #ADR-[CONTEXT]-002

---

## ✅ Context Map Validation Checklist

### Completeness
- [ ] All contexts identified and documented
- [ ] All relationships mapped
- [ ] Integration patterns chosen for each relationship
- [ ] Team ownership assigned

### Correctness
- [ ] No circular dependencies (Upstream/Downstream)
- [ ] ACLs exist for external systems
- [ ] Shared Kernels kept minimal (<5% of model)
- [ ] Core/Supporting/Generic classification clear

### Traceability
- [ ] Each context links to ADRs
- [ ] Integration patterns link to requirements
- [ ] API contracts documented

### Maintainability
- [ ] Diagram kept current with changes
- [ ] Changes trigger Context Map review
- [ ] New team members review map during onboarding

---

## 🔗 Related Documentation

- [Ubiquitous Language Glossary](../../02-requirements/ubiquitous-language.md)
- [Submodules and Modules Guide](../../.github/instructions/submodules.instructions.md)
- [Architecture Decisions](../decisions/)
- [DDD Tactical Patterns](../../04-design/patterns/ddd-tactical-patterns.md)

---

## 📊 Context Map Metrics

| Metric | Target | Status |
|--------|--------|--------|
| Contexts identified | All | ✅ |
| Integration patterns documented | 100% | ✅ |
| ACLs for external systems | 100% | ✅ |
| Shared Kernels | <2 | ✅ |
| Context ownership assigned | 100% | ✅ |

---

**Standards Alignment**:
- ISO/IEC/IEEE 42010:2011 (Architecture Description)
- ISO/IEC/IEEE 12207:2017 (System Architecture Process)
- Domain-Driven Design (Evans, 2003)

**Version**: 1.0  
**Last Updated**: 2025-11-27  
**Owner**: Architecture Team
