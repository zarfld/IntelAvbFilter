# Reliability Workflow Integration Guide

**Standards**: IEEE 1633-2016 Software Reliability Engineering  
**Lifecycle**: ISO/IEC/IEEE 12207:2017  
**Version**: 1.0  
**Last Updated**: 2025-01-23  

## 📖 Overview

This guide shows **when and how to use the 6 reliability engineering prompts** throughout the software development lifecycle (SDLC). It follows **IEEE 1633-2016** and integrates reliability activities with the 9-phase SDLC defined by ISO/IEC/IEEE 12207:2017.

### Purpose

- Map reliability prompts to lifecycle phases
- Show inputs/outputs for each prompt
- Provide decision trees for prompt selection
- Define minimal vs comprehensive adoption paths
- Ensure traceability from requirements → design → code → tests → SRG → release

### Applicable Standards

| Standard | Title | Relevance |
|----------|-------|-----------|
| **IEEE 1633-2016** | Recommended Practice on Software Reliability | Primary standard for all reliability activities |
| **ISO/IEC/IEEE 12207:2017** | Software life cycle processes | SDLC framework (9 phases) |
| **ISO/IEC/IEEE 29148:2018** | Requirements engineering | Requirements definition and traceability |
| **IEEE 1012-2016** | Verification and Validation | Test planning and execution |

---

## 🎯 Quick Reference: 6 Reliability Prompts

| Prompt | IEEE Clause | Phase(s) | Priority | Purpose |
|--------|-------------|----------|----------|---------|
| **reliability-plan-create.prompt.md** | 5.1 | 01-02 | Essential (P1) | Create Software Reliability Program Plan (SRPP) |
| **operational-profile-create.prompt.md** | 5.1.1.3, 5.4 | 04-05 | Essential (P1) | Create Operational Profile (OP) with MCUM |
| **sfmea-create.prompt.md** | 5.2, Annex A | 04-05 | Essential (P1) | Software Failure Modes Effects Analysis |
| **reliability-test-design.prompt.md** | 5.4 | 06 | High Value (P2) | Design OP-driven reliability tests, setup SRG data collection |
| **srg-model-fit.prompt.md** | 5.4, 6.3 | 07 | High Value (P2) | Fit SRG models, predict MTBF, assess trends |
| **reliability-release-decision.prompt.md** | 5.5 | 07 | Valuable (P3) | Make evidence-based release decision |

---

## 🔄 Phase-by-Phase Reliability Workflow

### Phase 01-02: Stakeholder Requirements & Requirements Analysis

**Reliability Activities**:
1. **Identify reliability stakeholders** (customers, operations, support)
2. **Define reliability requirements**:
   - Quantitative: Target MTBF, failure rate, availability
   - Qualitative: Graceful degradation, error recovery, logging
3. **Create SRPP** using `reliability-plan-create.prompt.md`

**Prompt to Use**: `reliability-plan-create.prompt.md`

**Inputs**:
- Business context (from Phase 01)
- Stakeholder reliability needs
- System criticality (safety, financial, reputational impact)
- Budget and schedule constraints

**Outputs**:
- **Software Reliability Program Plan (SRPP)** with:
  - Reliability objectives (e.g., MTBF ≥ 200 hours)
  - Roles and responsibilities (reliability engineer, QA lead, etc.)
  - Lifecycle activities for Phases 01-09
  - Quality gates per phase (e.g., defect discovery rate thresholds)
  - SFMEA planning (when to perform, who participates)
  - Reliability predictions method (e.g., Musa-Okumoto model)
  - SRG testing strategy (OP-driven, usage-weighted)
  - Tools and standards (SRG tools, defect tracking)
  - Schedule and milestones
  - Appendices (FDSC, defect taxonomy, glossary)

**Deliverable Location**: `01-stakeholder-requirements/reliability/SRPP-[ProjectName]-v[X.Y].md`

**Quality Gate**: 
- SRPP reviewed by stakeholders ✅
- Reliability objectives approved ✅
- FDSC (Failure Definition and Scoring Criteria) defined ✅

**Next Phase Reference**: Store SRPP in shared location; it guides all subsequent reliability activities.

---

### Phase 03: Architecture Design

**Reliability Activities**:
1. **Allocate reliability to components** (from SRPP)
2. **Document architecture reliability patterns**:
   - Circuit breakers
   - Retry mechanisms
   - Graceful degradation
   - Health check endpoints
3. **Plan OP and SFMEA** for next phase

**Prompts to Use**: None directly (preparation only)

**Inputs**:
- SRPP reliability allocations (from Phase 01-02)
- Architecture diagrams (component boundaries)

**Outputs**:
- Architecture design with reliability allocations
- Architecture Decision Records (ADRs) for reliability patterns

**Quality Gate**: 
- Component reliability allocations sum to system target MTBF ✅
- Architecture patterns reviewed for reliability ✅

---

### Phase 04: Detailed Design

**Reliability Activities**:
1. **Create Operational Profile (OP)** using `operational-profile-create.prompt.md`
2. **Perform initial SFMEA** using `sfmea-create.prompt.md`
3. **Design reliability mechanisms** (logging, error handling, monitoring)

**Prompts to Use**:
- `operational-profile-create.prompt.md`
- `sfmea-create.prompt.md`

#### Prompt 1: Operational Profile (OP)

**Inputs**:
- User personas (from Phase 02 use cases)
- Use case scenarios
- Expected usage patterns
- Mission profiles (e.g., "Submit Invoice Flow")

**Outputs**:
- **Operational Profile (OP)** with:
  - User classes (e.g., Admin, Customer, Guest)
  - Operations 3-level hierarchy:
    - Level 1: Modes (Login, Browse, Checkout)
    - Level 2: Tasks (View Product, Add to Cart)
    - Level 3: Input Events (Click Button, Submit Form)
  - Mission profiles (time-sequenced workflows)
  - Operational modes (Online, Offline, Degraded)
  - **MCUM (Markov Chain Usage Model)**: State machine with transition probabilities
  - Usage frequencies (Very Often ×10, Often ×3, Rare ÷10)
  - Test coverage targets (states 100%, transitions 100%, usage-weighted ≥80%)
  - Critical operations profile (80-20 rule: 20% operations account for 80% usage)

**Deliverable Location**: `04-design/reliability/OP-[ProjectName]-v[X.Y].md`

**Quality Gate**: 
- OP reviewed by customers/users ✅
- MCUM transition probabilities sum to 1.0 ✅
- Critical operations identified (80-20 rule) ✅

#### Prompt 2: SFMEA (Software Failure Modes Effects Analysis)

**Inputs**:
- Component designs (from Phase 04)
- Architecture diagrams
- Data flow diagrams
- OP (from previous step)

**Outputs**:
- **SFMEA Document** with:
  - Component descriptions
  - Failure modes (5 categories: Faulty Data, Timing, Sequencing, Error Handling, Logic)
  - Root cause analysis (Why does this failure occur?)
  - Consequences analysis (Component level, System level, Mission level)
  - Risk Priority Number (RPN) = Severity × Likelihood × Detectability (scale 1-10 each)
  - Mitigation actions (Elimination > Prevention > Detection > Recovery)
  - **Critical Items List (CIL)**: All items with RPN ≥ 200 (or threshold from SRPP)
  - CIL tracking (status, owner, verification evidence)

**Deliverable Location**: `04-design/reliability/SFMEA-[Component]-v[X.Y].md`

**Quality Gate**: 
- SFMEA completed for all critical components ✅
- CIL created with mitigation plans ✅
- High-RPN items (≥200) have design changes ✅

**Traceability**: 
```
Requirements → Design → SFMEA Failure Modes → Mitigation Actions → Design Changes
```

---

### Phase 05: Implementation

**Reliability Activities**:
1. **Review SRPP** (quality gates for Phase 05)
2. **Refine OP** with actual code structure
3. **Update SFMEA** based on implementation details
4. **Implement code-level reliability practices**:
   - Structured logging (operation start/end, success/failure)
   - Graceful degradation (try-catch with fallbacks)
   - Circuit breakers
   - Health check endpoints
5. **Track defects** and monitor defect discovery rate

**Prompts to Use**: 
- `operational-profile-create.prompt.md` (refinement)
- `sfmea-create.prompt.md` (update)

**Inputs**:
- SRPP quality gates
- OP from Phase 04 (to refine)
- SFMEA from Phase 04 (to update)

**Outputs**:
- Refined OP with actual operation names (e.g., `loginUser()`, `processPayment()`)
- Updated SFMEA with implementation-specific failure modes
- Updated CIL
- Code with reliability mechanisms implemented

**Quality Gate** (from SRPP):
- Defect discovery rate < [X] defects/KLOC ✅
- OP refined with actual code ✅
- SFMEA updated ✅
- CIL items addressed in code ✅
- Reliability mechanisms implemented (logging, error handling, graceful degradation) ✅

**Phase Instruction Reference**: `.github/instructions/phase-05-implementation.instructions.md`

---

### Phase 06: Integration

**Reliability Activities**:
1. **Design OP-driven reliability tests** using `reliability-test-design.prompt.md`
2. **Setup SRG data collection**
3. **Implement test adapters**
4. **Execute integration tests** with failure data logging
5. **Monitor continuous reliability metrics**
6. **Check integration quality gate**
7. **Update SFMEA** based on integration failures

**Prompt to Use**: `reliability-test-design.prompt.md`

#### Prompt 3: Reliability Test Design (OP-Driven)

**Inputs**:
- OP from Phase 05 (refined with actual operations)
- MCUM (state machine with transition probabilities)
- SFMEA CIL (critical failure modes to test)
- SRPP (SRG testing strategy)

**Outputs**:
- **Reliability Test Plan (RTP)** with:
  - Test case generation from MCUM:
    - **All-states coverage**: Visit every state in MCUM
    - **All-transitions coverage**: Traverse every transition
    - **Usage-weighted allocation**: Apply 80-20 rule (Tier 1 operations get 50% test effort)
  - **Test adapter design** (self-contained functions):
    ```typescript
    interface TestResult {
      passed: boolean;
      actualState: string;
      expectedState: string;
      failureSeverity?: number;
      executionTime: number;
      errorMessage?: string;
    }
    
    async function testLogin(userId: string, password: string): Promise<TestResult> {
      // Test adapter implementation
    }
    ```
  - Test effort allocation (Tier 1: 50%, Tier 2: 30%, Tier 3: 15%, Tier 4: 5%)
  - Coverage targets (states 100%, transitions 100%, usage-weighted ≥80%)
  - Test execution plan (order, dependencies, preconditions)
  - **SRG data collection setup**:
    ```typescript
    interface FailureRecord {
      failureNumber: number;
      failureTime: number; // hours or test case number
      testCase: string;
      operation: string; // from OP
      state: string; // from MCUM
      severity: number; // from FDSC (1-10)
      rootCause?: string;
      fixed: boolean;
      fixTime?: number;
    }
    
    async function logFailure(failure: FailureRecord): Promise<void> {
      // Log to database/file
    }
    
    function exportSRGData(): string {
      // Export to CSV format for SRG modeling
      // Format: FailureNumber,FailureTime,Severity,Operation,State,Fixed
    }
    ```
  - Test environment specification
  - Test validation criteria

**Deliverable Location**: `06-integration/reliability/RTP-[ProjectName]-v[X.Y].md`

**Quality Gate** (from SRPP):
- Integration pass rate ≥ 95% ✅
- Coverage targets met (states 100%, transitions 100%, usage-weighted ≥80%) ✅
- SRG data collection working ✅
- Failure data exported (M ≥ 20 failures recommended for Phase 07 SRG modeling) ✅
- MTBF trend increasing (no declining trend) ✅
- Critical failures = 0 ✅

**Phase Instruction Reference**: `.github/instructions/phase-06-integration.instructions.md`

---

### Phase 07: Verification & Validation

**Reliability Activities**:
1. **Fit SRG models** using `srg-model-fit.prompt.md`
2. **Make release decision** using `reliability-release-decision.prompt.md`
3. **Perform RDT (optional)** if required by customer
4. **Update Architecture Traceability Matrix** with reliability evidence
5. **Analyze defects** and document lessons learned

**Prompts to Use**:
- `srg-model-fit.prompt.md`
- `reliability-release-decision.prompt.md`

#### Prompt 4: SRG Model Fitting

**Inputs**:
- Failure data from Phase 06 (CSV export: FailureNumber, FailureTime, Severity, Operation, State, Fixed)
- Target MTBF from SRPP
- FDSC (Failure Definition and Scoring Criteria)

**Minimum Data**: M ≥ 20 failures for reliable model fitting

**Outputs**:
- **SRG Analysis Report** with:
  - **Failure data summary**:
    - Total failures (M)
    - Total test time (T hours)
    - TBF (Time Between Failures) calculations
    - Failure severity distribution
  - **Trend test results**:
    - **Laplace u-statistic**: 
      ```
      u = [Σ(t_i) / M - T/2] / [T * sqrt(1/(12*M))]
      
      Interpretation:
        u < -2: Reliability GROWING ✅ (proceed)
        -2 ≤ u ≤ 2: NO TREND ⚠️ (investigate)
        u > 2: Reliability DECLINING ❌ (don't release)
      ```
    - **Arithmetic Mean (AM) trend test**: Mean TBF increasing?
  - **Model selection and fitting** (3-4 models):
    - **Goel-Okumoto** (finite failures, NHPP): `μ(t) = N₀ * (1 - e^(-b*t))`
    - **Musa-Okumoto** (infinite failures, logarithmic): `λ(t) = λ₀ / (1 + θ*t)`
    - **Jelinski-Moranda** (finite failures, de-eutrophication): `λ_i = φ * (N₀ - (i-1))`
    - **Crow/AMSAA** (non-parametric, power law): `λ(t) = λ * β * t^(β-1)`
  - **Goodness-of-fit assessment**:
    - SSE (Sum of Squared Errors) - lower is better
    - R² (Coefficient of Determination) - higher is better (R² > 0.9 = excellent)
    - AIC (Akaike Information Criterion) - lower is better
  - **Best-fit model selection** with ranking table
  - **Current reliability metrics**:
    - Current MTBF = 1 / λ(T) hours
    - Current failure rate = λ(T) failures/hour
    - Residual defects = N₀ - μ(T) (for finite models)
  - **Reliability predictions**:
    - Time to reach target MTBF
    - Expected failures in next Δt hours
    - Confidence intervals (80%, 90%)
  - **Release recommendations**: GO / CONDITIONAL / NO-GO

**Deliverable Location**: `07-verification-validation/test-results/srg-analysis-[Version]-[Date].md`

**Quality Gate** (from SRPP):
- Estimated MTBF ≥ Target MTBF ✅
- SRG trend positive (u < -2) ✅
- Best-fit model R² > 0.9 ✅

#### Prompt 5: Release Decision

**Inputs**:
- SRG analysis report (from previous step)
- Test results (pass rates, coverage)
- SFMEA CIL status (% complete)
- Quality gate results (all phases 05-08)

**Outputs**:
- **Release Decision Report** with:
  - **Executive summary** (1 page: GO/CONDITIONAL/NO-GO with rationale)
  - **Release readiness criteria** (overview)
  - **Quality gate assessment**:
    | Phase | Quality Gate | Threshold | Status |
    |-------|--------------|-----------|--------|
    | Phase 05 | Defect Discovery Rate | < [X] def/KLOC | [✅/❌] |
    | Phase 06 | Integration Pass Rate | ≥ 95% | [✅/❌] |
    | Phase 07 | Estimated MTBF | ≥ [Target] hours | [✅/❌] |
    | Phase 08 | Acceptance Pass Rate | 100% | [✅/❌] |
  - **Reliability evidence**:
    - SRG model results (MTBF, failure rate, residual defects)
    - Test pass rates
    - CIL completion status
  - **Mandatory release criteria checklist** (10/10 required):
    - [ ] All critical defects fixed (FDSC Severity = 10, count = 0)
    - [ ] CIL 100% complete (all high-RPN items mitigated and verified)
    - [ ] Acceptance tests 100% passed
    - [ ] SRG trend positive (Laplace u < -2, reliability growing)
    - [ ] Target MTBF achieved (current MTBF ≥ target)
    - [ ] Security vulnerabilities addressed (all critical/high)
    - [ ] User documentation complete
    - [ ] Deployment plan approved
    - [ ] Rollback plan tested
    - [ ] Stakeholder sign-off obtained
  - **Risk assessment** with mitigation plans:
    | Risk | Likelihood | Impact | Risk Level | Mitigation |
    |------|------------|--------|------------|------------|
    | Critical defect in production | Low/Med/High | Critical | Red/Yellow/Green | [Plan] |
  - **Stakeholder approval table**:
    | Stakeholder | Role | Decision | Comments |
    |-------------|------|----------|----------|
    | Product Owner | [Name] | [Go/No-Go/Conditional] | |
    | Engineering Manager | [Name] | [Go/No-Go/Conditional] | |
    | QA Lead | [Name] | [Go/No-Go/Conditional] | |
    | Reliability Engineer | [Name] | [Go/No-Go/Conditional] | |
  - **Release decision** (one of three scenarios):
    - **✅ GO FOR RELEASE**: All criteria met (10/10), all gates passed, target MTBF achieved, low risk
    - **⏳ CONDITIONAL GO**: Most criteria met (8-9/10), MTBF close to target (gap < 20%), specific time-bound actions required
    - **❌ NO-GO**: Critical criteria not met (< 8/10), MTBF significantly below target (gap > 20%), high risk
  - **Post-release monitoring plan**:
    - Metrics/thresholds (MTBF, error rate, response time)
    - Incident classification (P1-P4)
    - Hotfix process
    - Maintenance release schedule

**Deliverable Location**: `07-verification-validation/test-results/release-decision-[Version]-[Date].md`

**Quality Gate**: 
- All mandatory criteria met (10/10) ✅
- Release decision approved by stakeholders ✅
- Post-release plan defined ✅

**Phase Instruction Reference**: `.github/instructions/phase-07-verification-validation.instructions.md`

---

### Phase 08: Transition (Deployment)

**Reliability Activities**:
1. **Execute deployment plan** (approved in Phase 07)
2. **Monitor post-release metrics** (from release decision report)
3. **Verify rollback plan** is ready
4. **Activate incident response** process

**Prompts to Use**: None (execution only)

**Inputs**:
- Release decision report (GO approval)
- Deployment plan
- Post-release monitoring plan

**Outputs**:
- Deployed system in production
- Real-time reliability dashboard
- Incident response team on standby

**Quality Gate** (from SRPP):
- Acceptance pass rate 100% ✅
- Deployment successful ✅
- Monitoring active ✅
- Rollback tested ✅

---

### Phase 09: Operation & Maintenance

**Reliability Activities**:
1. **Monitor operational reliability**:
   - Actual MTBF vs predicted MTBF
   - Error rates
   - Incident response times
2. **Track post-release defects**
3. **Update SFMEA** based on production failures
4. **Plan maintenance releases**
5. **Lessons learned** for next release

**Prompts to Use**: 
- `sfmea-create.prompt.md` (update with production failures)
- `operational-profile-create.prompt.md` (update with actual usage data)

**Inputs**:
- Production monitoring data
- Incident reports
- User feedback

**Outputs**:
- Updated OP with actual usage frequencies
- Updated SFMEA with production failure modes
- Maintenance release schedule
- Lessons learned document

**Quality Gate**: 
- Operational MTBF ≥ Target MTBF ✅
- P1 incidents < [threshold] per month ✅
- Hotfix SLA met ✅

---

## 🌳 Decision Trees

### Decision Tree 1: Which Prompt to Use?

```
START
│
├─ Phase 01-02 (Requirements)?
│  └─ YES → Use reliability-plan-create.prompt.md (SRPP)
│
├─ Phase 04-05 (Design/Implementation)?
│  ├─ Need Operational Profile?
│  │  └─ YES → Use operational-profile-create.prompt.md (OP)
│  └─ Need Failure Analysis?
│     └─ YES → Use sfmea-create.prompt.md (SFMEA)
│
├─ Phase 06 (Integration)?
│  └─ Need Test Plan?
│     └─ YES → Use reliability-test-design.prompt.md (RTP)
│
└─ Phase 07 (V&V)?
   ├─ Have Failure Data?
   │  └─ YES → Use srg-model-fit.prompt.md (SRG)
   └─ Need Release Decision?
      └─ YES → Use reliability-release-decision.prompt.md
```

### Decision Tree 2: Minimal vs Comprehensive Adoption

```
START: What is system criticality?
│
├─ LOW CRITICALITY (informational websites, internal tools)
│  └─ MINIMAL ADOPTION PATH:
│     1. Create SRPP (lightweight, 5-page version)
│     2. Define target MTBF (e.g., 100 hours)
│     3. Track defects during testing
│     4. Calculate simple MTBF = Total Test Time / Total Failures
│     5. Release if MTBF ≥ Target
│
├─ MEDIUM CRITICALITY (business applications, e-commerce)
│  └─ STANDARD ADOPTION PATH:
│     1. Create SRPP (full 10-section version)
│     2. Create OP (lightweight, main workflows only)
│     3. Perform SFMEA for critical components
│     4. Design OP-driven tests
│     5. Fit 2 SRG models (Goel-Okumoto, Musa-Okumoto)
│     6. Release decision report (structured)
│
└─ HIGH CRITICALITY (safety, financial, medical, infrastructure)
   └─ COMPREHENSIVE ADOPTION PATH:
      1. Create SRPP (detailed, with reliability allocations)
      2. Create OP (detailed, with MCUM state machine)
      3. Perform SFMEA for ALL components
      4. Design OP-driven tests (usage-weighted allocation)
      5. Fit 4 SRG models (all models, goodness-of-fit assessment)
      6. Release decision report (comprehensive, stakeholder sign-off)
      7. Perform RDT (Reliability Demonstration Test) with statistical confidence
      8. Post-release monitoring (real-time dashboard)
```

---

## 📊 Traceability Matrix: Prompts → Phases → Deliverables → Templates

| Prompt | IEEE Clause | Phase(s) | Input Documents | Output Deliverable | Template Location |
|--------|-------------|----------|-----------------|-------------------|-------------------|
| **reliability-plan-create.prompt.md** | 5.1 | 01-02 | Business context, stakeholder needs | SRPP-[Project]-v[X.Y].md | spec-kit-templates/software-reliability-program-plan.md |
| **operational-profile-create.prompt.md** | 5.1.1.3, 5.4 | 04-05 | Use cases, user personas, workflows | OP-[Project]-v[X.Y].md | spec-kit-templates/operational-profile.md |
| **sfmea-create.prompt.md** | 5.2, Annex A | 04-05 | Component designs, data flows, OP | SFMEA-[Component]-v[X.Y].md | (Prompt delivers full template) |
| **reliability-test-design.prompt.md** | 5.4 | 06 | OP with MCUM, SFMEA CIL, SRPP | RTP-[Project]-v[X.Y].md | (Prompt delivers full template) |
| **srg-model-fit.prompt.md** | 5.4, 6.3 | 07 | Failure data (CSV), target MTBF, FDSC | srg-analysis-[Version]-[Date].md | (Prompt delivers full template) |
| **reliability-release-decision.prompt.md** | 5.5 | 07 | SRG analysis, test results, CIL status | release-decision-[Version]-[Date].md | (Prompt delivers full template) |

**Template Usage**:
- Templates in `spec-kit-templates/` are starting points
- Prompts deliver **complete markdown templates** as "Expected Output"
- Users populate templates with project-specific data
- Completed documents stored in phase folders (e.g., `01-stakeholder-requirements/reliability/`)

---

## 🚦 Adoption Paths

### Path 1: Minimal Adoption (Low Criticality Systems)

**Objective**: Basic reliability engineering with minimal overhead

**Activities**:
1. **Phase 01-02**: Create lightweight SRPP (5 pages)
   - Define target MTBF (e.g., 100 hours)
   - Define FDSC (severity scale 1-10)
   - Identify quality gates
2. **Phase 04-05**: Optional OP (main workflows only)
3. **Phase 06**: Track defects during integration testing
   - Log failure times
   - Calculate simple MTBF = Total Test Time / Total Failures
4. **Phase 07**: Simple release decision
   - Check if MTBF ≥ Target
   - Verify critical defects = 0
   - GO/NO-GO decision

**Prompts Used**:
- reliability-plan-create.prompt.md (lightweight version)
- Optional: operational-profile-create.prompt.md (main workflows only)

**Estimated Effort**: 2-3 days

**Suitable For**:
- Informational websites
- Internal tools
- Low-traffic applications
- Prototypes

---

### Path 2: Standard Adoption (Medium Criticality Systems)

**Objective**: Balanced reliability engineering following IEEE 1633 core activities

**Activities**:
1. **Phase 01-02**: Create full SRPP (10 sections)
2. **Phase 04-05**: 
   - Create OP with MCUM (main workflows)
   - Perform SFMEA for critical components (RPN ≥ 200)
3. **Phase 06**: 
   - Design OP-driven tests (usage-weighted)
   - Setup SRG data collection
4. **Phase 07**: 
   - Fit 2 SRG models (Goel-Okumoto, Musa-Okumoto)
   - Calculate current MTBF
   - Structured release decision report

**Prompts Used**:
- reliability-plan-create.prompt.md ✅
- operational-profile-create.prompt.md ✅
- sfmea-create.prompt.md (critical components only)
- reliability-test-design.prompt.md ✅
- srg-model-fit.prompt.md (2 models)
- reliability-release-decision.prompt.md ✅

**Estimated Effort**: 1-2 weeks

**Suitable For**:
- Business applications
- E-commerce platforms
- Customer-facing SaaS
- Most commercial software

---

### Path 3: Comprehensive Adoption (High Criticality Systems)

**Objective**: Full IEEE 1633 compliance with statistical rigor

**Activities**:
1. **Phase 01-02**: Create detailed SRPP with reliability allocations
2. **Phase 04-05**: 
   - Create detailed OP with MCUM (all workflows)
   - Perform SFMEA for ALL components
   - Update CIL continuously
3. **Phase 06**: 
   - Design OP-driven tests (all coverage targets)
   - Setup comprehensive SRG data collection
   - Real-time reliability dashboard
4. **Phase 07**: 
   - Fit 4 SRG models with goodness-of-fit assessment
   - Perform RDT (Reliability Demonstration Test)
   - Comprehensive release decision with stakeholder sign-off
5. **Phase 08-09**: 
   - Post-release monitoring (real-time MTBF tracking)
   - Incident response with SLAs

**Prompts Used**:
- reliability-plan-create.prompt.md ✅
- operational-profile-create.prompt.md ✅
- sfmea-create.prompt.md ✅ (all components)
- reliability-test-design.prompt.md ✅
- srg-model-fit.prompt.md ✅ (all 4 models)
- reliability-release-decision.prompt.md ✅

**Estimated Effort**: 2-4 weeks

**Suitable For**:
- Safety-critical systems (medical, aerospace, automotive)
- Financial systems (trading, payment processing)
- Infrastructure systems (cloud platforms, telecom)
- Regulated industries (healthcare, defense)

---

## 🔗 Traceability: Requirements → Tests → SRG → Release

### Traceability Flow

```
┌─────────────────────────────────────────────────────────────────┐
│ Phase 01-02: Requirements                                       │
│ - REQ-REL-001: MTBF ≥ 200 hours                                 │
│ - REQ-REL-002: λ ≤ 0.005 failures/hour                          │
└────────────────────┬────────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────────┐
│ Phase 04-05: Design + OP + SFMEA                                │
│ - OP: LoginOperation (frequency: Very Often ×10)                │
│ - SFMEA: FM-001 "LoginService crashes" (RPN = 240, CRITICAL)   │
│ - Mitigation: Add circuit breaker, retry logic                  │
└────────────────────┬────────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────────┐
│ Phase 06: Integration + Test Design                             │
│ - TC-001: Test LoginOperation (usage-weighted, 50% effort)      │
│ - TC-002: Test circuit breaker (verify FM-001 mitigation)       │
│ - Failure Data: Log all failures with operation/state           │
└────────────────────┬────────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────────┐
│ Phase 07: SRG Modeling                                           │
│ - Failure Data: 25 failures in 1000 hours                       │
│ - Best Model: Goel-Okumoto (R² = 0.95)                          │
│ - Current MTBF: 250 hours ✅ (exceeds target 200 hours)         │
│ - Current λ: 0.004 failures/hour ✅ (below target 0.005)        │
└────────────────────┬────────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────────────────┐
│ Phase 07: Release Decision                                       │
│ - REQ-REL-001: ✅ VERIFIED (MTBF 250 > 200)                     │
│ - REQ-REL-002: ✅ VERIFIED (λ 0.004 < 0.005)                    │
│ - Decision: ✅ GO FOR RELEASE                                   │
└─────────────────────────────────────────────────────────────────┘
```

### Traceability Matrix Example

| Requirement | Design | SFMEA Failure Mode | Test Case | SRG Evidence | Release Verification |
|-------------|--------|-------------------|-----------|--------------|---------------------|
| REQ-REL-001: MTBF ≥ 200 hr | ARCH-001 | FM-001: LoginService crash (RPN=240) | TC-001: LoginTest (50% effort) | MTBF = 250 hr (Goel-Okumoto) | ✅ VERIFIED |
| REQ-REL-002: λ ≤ 0.005 fail/hr | ARCH-002 | FM-005: PaymentGateway timeout (RPN=180) | TC-005: PaymentTest (30% effort) | λ = 0.004 fail/hr (Musa-Okumoto) | ✅ VERIFIED |
| REQ-REL-003: Critical failures = 0 | ARCH-003 | FM-010: DataLoss (RPN=300, CIL) | TC-010: DataIntegrityTest | 0 critical failures (FDSC=10) | ✅ VERIFIED |

**Traceability Tools**:
- Manual: Use `07-verification-validation/traceability/architecture-traceability-matrix.md`
- Automated: Use `scripts/generate-traceability-matrix.py`

---

## 🛠️ Tools and Templates

### Prompts (in `.github/prompts/`)

1. **reliability-plan-create.prompt.md** - SRPP creation
2. **operational-profile-create.prompt.md** - OP creation with MCUM
3. **sfmea-create.prompt.md** - SFMEA with CIL tracking
4. **reliability-test-design.prompt.md** - OP-driven test design
5. **srg-model-fit.prompt.md** - SRG model fitting
6. **reliability-release-decision.prompt.md** - Release decision

### Templates (in `spec-kit-templates/`)

1. **software-reliability-program-plan.md** - SRPP template (10 sections)
2. **operational-profile.md** - OP template with MCUM
3. Other templates embedded in prompts

### Scripts (in `scripts/`)

1. **generate-traceability-matrix.py** - Auto-generate traceability matrix
2. **validate-traceability.py** - Verify requirements-to-tests traceability
3. **trace_unlinked_requirements.py** - Find requirements without tests
4. **integrity_level_scan.py** - Scan for reliability requirements

### Phase Instructions (in `.github/instructions/`)

1. **phase-05-implementation.instructions.md** - Reliability section added
2. **phase-06-integration.instructions.md** - Reliability section added
3. **phase-07-verification-validation.instructions.md** - Reliability section added

---

## 💡 Best Practices

### 1. Start Early (Phase 01-02)

❌ **Anti-pattern**: "We'll add reliability testing later"  
✅ **Best practice**: Create SRPP in Phase 01-02, define target MTBF, plan SRG testing strategy

**Why**: Reliability is a design constraint, not an afterthought. Early planning prevents costly rework.

### 2. Use OP for Test Design (Phase 04-06)

❌ **Anti-pattern**: "We'll test everything equally"  
✅ **Best practice**: Use OP to prioritize tests via 80-20 rule (20% operations get 80% usage)

**Why**: Usage-weighted testing finds defects where users will encounter them most.

### 3. Collect Failure Data Continuously (Phase 06)

❌ **Anti-pattern**: "We'll collect failure data manually at the end"  
✅ **Best practice**: Setup automated failure logging from Day 1 of integration testing

**Why**: SRG modeling requires M ≥ 20 failures. Automated logging ensures data quality.

### 4. Fit Multiple SRG Models (Phase 07)

❌ **Anti-pattern**: "We'll just use one SRG model"  
✅ **Best practice**: Fit 3-4 models, assess goodness-of-fit, select best-fit model

**Why**: No single model fits all failure patterns. Model selection improves prediction accuracy.

### 5. Make Evidence-Based Decisions (Phase 07)

❌ **Anti-pattern**: "We feel ready to release"  
✅ **Best practice**: Use mandatory criteria checklist (10/10), check all quality gates, verify MTBF ≥ target

**Why**: Subjective decisions lead to post-release failures. Evidence-based decisions reduce risk.

### 6. Integrate Reliability with XP Practices

❌ **Anti-pattern**: "Reliability engineering is separate from agile development"  
✅ **Best practice**: Integrate reliability into TDD (test adapters), continuous integration (SRG data collection), refactoring (SFMEA updates)

**Why**: Agile and reliability engineering are complementary. TDD ensures tests exist; reliability engineering ensures tests are usage-driven.

### 7. Update SFMEA Continuously (Phases 04-09)

❌ **Anti-pattern**: "SFMEA is a Phase 04 deliverable, then we're done"  
✅ **Best practice**: Update SFMEA in Phase 05 (implementation), Phase 06 (integration), Phase 09 (production failures)

**Why**: SFMEA is a living document. New failure modes emerge during implementation/integration/operation.

### 8. Monitor Post-Release Reliability (Phase 09)

❌ **Anti-pattern**: "We released, our work is done"  
✅ **Best practice**: Monitor operational MTBF, compare to predicted MTBF, update OP with actual usage

**Why**: SRG models are predictions. Actual operational data validates models and improves future predictions.

---

## ⚠️ Common Pitfalls

### Pitfall 1: Insufficient Failure Data

**Problem**: Only 10 failures collected, trying to fit SRG models  
**Impact**: Model parameters unreliable, predictions inaccurate  
**Solution**: Collect M ≥ 20 failures (recommended: M ≥ 30 for 4 models)

### Pitfall 2: Ignoring Trend Tests

**Problem**: Fitting SRG models without checking Laplace u-statistic  
**Impact**: Modeling declining reliability as growing reliability  
**Solution**: Always perform trend tests BEFORE fitting models. If u > 2 (declining), don't release!

### Pitfall 3: Overfitting SRG Models

**Problem**: Fitting 10 models, selecting one with highest R²  
**Impact**: Overfitting, poor generalization to future failures  
**Solution**: Fit 3-4 standard models, use AIC to penalize complexity, validate with prequential likelihood

### Pitfall 4: Skipping SFMEA

**Problem**: "We don't have time for SFMEA, let's just code"  
**Impact**: Failure modes discovered in production, costly fixes, reliability targets missed  
**Solution**: Perform lightweight SFMEA for critical components (at minimum). Full SFMEA for high-criticality systems.

### Pitfall 5: Inconsistent FDSC (Failure Definition)

**Problem**: Different testers classify failures differently (one calls it "critical", another "high")  
**Impact**: Unreliable severity data, SRG models use mixed data  
**Solution**: Define FDSC in SRPP (Phase 01-02), train team, enforce consistently

### Pitfall 6: Testing Without OP

**Problem**: Writing tests without understanding usage patterns  
**Impact**: Tests focus on rare operations, miss critical operations  
**Solution**: Create OP before test design, use MCUM to generate tests, apply 80-20 rule

### Pitfall 7: Releasing Without Evidence

**Problem**: "Target MTBF not achieved, but we're releasing anyway"  
**Impact**: Post-release failures, customer dissatisfaction, reputation damage  
**Solution**: Use mandatory criteria checklist (10/10). If criteria not met, choose CONDITIONAL GO or NO-GO.

---

## 📚 Related Documentation

### Standards

- **IEEE 1633-2016**: Software Reliability Recommended Practice (Primary)
- **ISO/IEC/IEEE 12207:2017**: Software life cycle processes
- **ISO/IEC/IEEE 29148:2018**: Requirements engineering
- **IEEE 1012-2016**: Verification and Validation
- **IEEE 1016-2009**: Software design descriptions

### Internal Documentation

- **Lifecycle Guide**: `docs/lifecycle-guide.md`
- **XP Practices Guide**: `docs/xp-practices.md`
- **Spec-Driven Development**: `docs/spec-driven-development.md`
- **Traceability Guide**: `docs/id-taxonomy-guide.md`
- **Gap Analysis**: `standards-compliance/reviews/IEEE-1633-gap-analysis.md`

### Phase Instructions

- **Phase 05**: `.github/instructions/phase-05-implementation.instructions.md`
- **Phase 06**: `.github/instructions/phase-06-integration.instructions.md`
- **Phase 07**: `.github/instructions/phase-07-verification-validation.instructions.md`

---

## 🎯 Summary

| **When** | **Prompt** | **Deliverable** | **Why** |
|----------|-----------|-----------------|---------|
| **Phase 01-02** | reliability-plan-create.prompt.md | SRPP | Define reliability objectives, plan activities |
| **Phase 04-05** | operational-profile-create.prompt.md | OP with MCUM | Understand usage patterns, prioritize testing |
| **Phase 04-05** | sfmea-create.prompt.md | SFMEA with CIL | Identify failure modes, mitigate high-RPN items |
| **Phase 06** | reliability-test-design.prompt.md | RTP | Design usage-driven tests, setup SRG data collection |
| **Phase 07** | srg-model-fit.prompt.md | SRG Analysis Report | Predict MTBF, assess trends, verify reliability growth |
| **Phase 07** | reliability-release-decision.prompt.md | Release Decision Report | Evidence-based GO/NO-GO decision |

**Key Takeaway**: Reliability engineering is not a single activity—it's a continuous process integrated throughout the SDLC. Use the right prompt at the right phase, maintain traceability, and make evidence-based decisions.

---

**Version**: 1.0  
**Maintainer**: Reliability Engineering Team  
**Last Updated**: 2025-01-23  
**Feedback**: Submit issues to project repository

