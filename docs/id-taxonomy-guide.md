# ID Taxonomy & Category Identifiers

## Overview

This project uses a structured identifier taxonomy for traceability across stakeholder requirements, system requirements, architecture, design, and tests. All IDs support optional **4-character category prefixes** for complex projects requiring organizational segmentation.

## Core ID Patterns

### Basic Pattern (Simple Projects)
```
<PREFIX>-<TYPE>-<NUMBER>
```

### Extended Pattern (Complex Projects)
```
<PREFIX>-<CATEGORY>-<TYPE>-<NUMBER>
```

Where:
- **PREFIX**: Entity type (StR, REQ, ADR, ARC, QA, TEST)
- **CATEGORY**: Optional 4-character uppercase code (e.g., AUTH, CORE, INFR, PERF)
- **TYPE**: Sub-type where applicable (F for Functional, NF for Non-Functional, C for Component)
- **NUMBER**: Zero-padded 3-digit sequence (001-999)

## Supported ID Types

### 1. Stakeholder Requirements (StR)

**Basic**: `StR-001`, `StR-002`, ...  
**Categorized**: `StR-CORE-001`, `StR-BUS-002`, `StR-COMP-003`

Example categories:
- `CORE`: Core business requirements
- `COMP`: Compliance/regulatory
- `USER`: User-facing needs
- `TECH`: Technical stakeholder needs

### 2. System Requirements (REQ)

**Functional Basic**: `REQ-F-001`, `REQ-F-002`, ...  
**Functional Categorized**: `REQ-AUTH-F-001`, `REQ-PAYMT-F-002`, `REQ-NOTIF-F-003`

**Non-Functional Basic**: `REQ-NF-001`, `REQ-NF-002`, ...  
**Non-Functional Categorized**: `REQ-PERF-NF-001`, `REQ-SECU-NF-002`, `REQ-USAB-NF-003`

Example categories:
- `AUTH`: Authentication/authorization
- `PAYMT`: Payment processing
- `NOTIF`: Notifications
- `PERF`: Performance
- `SECU`: Security
- `USAB`: Usability
- `AVAIL`: Availability/reliability

### 3. Architecture Decision Records (ADR)

**Basic**: `ADR-001`, `ADR-002`, ...  
**Categorized**: `ADR-INFR-001`, `ADR-DATA-002`, `ADR-INTG-003`

Example categories:
- `INFR`: Infrastructure decisions
- `DATA`: Data architecture
- `INTG`: Integration patterns
- `SECU`: Security architecture
- `PERF`: Performance architecture

### 4. Architecture Components (ARC-C)

**Basic**: `ARC-C-001`, `ARC-C-002`, ...  
**Categorized**: `ARC-C-CORE-001`, `ARC-C-EDGE-002`, `ARC-C-DATA-003`

Example categories:
- `CORE`: Core domain components
- `EDGE`: Edge/gateway components
- `DATA`: Data layer components
- `INTG`: Integration components

### 5. Quality Attribute Scenarios (QA-SC)

**Basic**: `QA-SC-001`, `QA-SC-002`, ...  
**Categorized**: `QA-SC-PERF-001`, `QA-SC-SECU-002`, `QA-SC-USAB-003`

Example categories:
- `PERF`: Performance scenarios
- `SECU`: Security scenarios
- `USAB`: Usability scenarios
- `RELI`: Reliability scenarios
- `MAINT`: Maintainability scenarios

### 6. Tests (TEST)

**Basic**: `TEST-LOGIN-001`, `TEST-PAYMENT-001`, ...  
**Categorized**: `TEST-AUTH-LOGIN-001`, `TEST-PAYMT-PROCESS-001`, `TEST-NOTIF-EMAIL-001`

Example categories:
- `AUTH`: Authentication tests
- `PAYMT`: Payment tests
- `NOTIF`: Notification tests
- `INTG`: Integration tests
- `E2E`: End-to-end tests
- `PERF`: Performance tests
- `SECU`: Security tests

## When to Use Categories

### Use Categories When:

✅ **Large, complex projects** with multiple subsystems or domains  
✅ **Multiple teams** working on separate modules needing identifier namespaces  
✅ **Regulatory/compliance** requirements demanding clear segmentation  
✅ **Long-term projects** (3+ years) where ID collision risk is high  
✅ **Microservices architecture** with bounded contexts requiring isolation

### Skip Categories When:

❌ **Small projects** (<50 requirements) with single team  
❌ **Prototypes** or proof-of-concepts with short lifecycle  
❌ **Simple domains** with minimal subsystem boundaries  
❌ **Early stages** before domain boundaries are clear

## Migration Strategy

### Starting Simple → Adding Categories Later

1. **Phase 1**: Use basic IDs (REQ-F-001, ADR-001)
2. **Phase 2**: Introduce categories when pain emerges (>100 IDs, multiple domains)
3. **Phase 3**: Migrate gradually:
   ```
   REQ-F-001 → REQ-AUTH-F-001 (add category prefix)
   ADR-001 → ADR-INFR-001
   ```
4. **Maintain backward compatibility**: Scripts support both patterns

### Category Naming Conventions

- **Always 4 uppercase characters** (ABCD format)
- **Mnemonic**: Choose recognizable abbreviations (AUTH > AUTC, PERF > PRFM)
- **Consistent**: Document categories in `docs/id-categories.md`
- **Avoid conflicts**: Check existing categories before adding new

## Traceability Examples

### Basic (No Categories)
```
StR-001 → REQ-F-001 → ADR-001 → ARC-C-001 → TEST-001
```

### Categorized (Complex Project)
```
StR-CORE-001 → REQ-AUTH-F-001 → ADR-SECU-001 → ARC-C-AUTH-001 → TEST-AUTH-LOGIN-001
                                             → QA-SC-SECU-001
```

### Mixed (Transition Period)
```
StR-001 → REQ-AUTH-F-001 → ADR-SECU-001 → ARC-C-001 → TEST-001
          (new category)    (categorized)  (old)      (old)
```

## Tooling Support

All scripts in `scripts/` directory support both basic and categorized patterns:
- `validate-spec-structure.py` - Validates both ID formats
- `generate-traceability-matrix.py` - Parses both formats
- `spec_parser.py` - Indexes both formats
- `build_trace_json.py` - Traces across both formats

## Documentation

When creating new categories, document in:
- `docs/id-categories.md` (project-specific category registry)
- Spec front matter (optional `idCategory` field)
- Prompt instructions (guide Copilot on category usage)

## Examples in Templates

Templates provide examples with both patterns:
- `spec-kit-templates/requirements-spec.md` - Shows REQ-F-001 (basic)
- `spec-kit-templates/architecture-spec.md` - Shows ADR-001 (basic)

To adopt categories, replace:
```markdown
### REQ-F-001: User Login
```
With:
```markdown
### REQ-AUTH-F-001: User Login
```

---

**Summary**: Category identifiers are **optional** and **backward-compatible**. Start simple; add categories when complexity demands it. All tooling supports both patterns transparently.
