---
description: "Guidance for working with Git submodules and external nested repositories. Aligns with XP practices (Continuous Integration, Collective Ownership) and modular architecture (high cohesion, low coupling)."
applyTo: "**"
---

# Submodules and External Modules: Standards-Compliant Guidance

**Standards**: ISO/IEC/IEEE 12207:2017 (Configuration & Integration), ISO/IEC/IEEE 42010:2011 (Architecture), ISO/IEC/IEEE 29148:2018 (Requirements)
**XP Integration**: Continuous Integration (CI), Collective Ownership, Simple Design, Refactoring

## 🎯 Objectives

- Enable modular development with clear boundaries (high cohesion, low coupling)
- Support safe reuse while avoiding lock‑step releases and fragile dependencies
- Provide operational rules for adding, updating, and removing submodules (.gitmodules and external folders)
- Maintain traceability, CI predictability, and reproducible builds

## 📦 Scope and Definitions

- **Submodule**: A Git repository embedded in another repo, referenced by commit via `.gitmodules`.
- **External module (vendor/third‑party)**: Code brought in as a subtree, package, or nested folder without a Git link.
- **Bounded Context / Module**: A coherent unit that encapsulates its model and contracts; may be implemented as its own repo.
- **Shared Kernel**: A small, intentionally curated subset of shared model agreed by teams with explicit governance.
- **Adapter Layer**: Translation layer isolating consumers from a provider’s internal model or API changes.

## ✅ Always Do (Best Practices)

| Practice | Details and Rationale |
|---|---|
| Integrate continuously and frequently | Merge small, frequent changes; never let work sit unintegrated for > a few hours. Run all tests before integration. |
| Maintain cohesion and decoupling | Keep modules cohesive to a single purpose; minimize cross‑module dependencies; prefer narrow, explicit interfaces. |
| Run all tests before integrating | CI must execute all unit/integration tests for the whole system; merges must be green. |
| Align repo structure with ownership | Prefer one repo + one CI build per independently deployable module/microservice. |
| Encapsulate implementation details | Expose only the contract (API/schema); hide internal structures to allow independent evolution. |
| Define meaningful modules | Boundaries reflect domain concepts and ubiquitous language; refactor boundaries as understanding evolves. |
| Refactor module boundaries | Expect change; split/merge modules when cohesion/coupling data suggests it. |
| Use version control for everything | Source, docs, build, and release artifacts are tracked; `.gitmodules` is reviewed like code. |
| Pin submodule commits | Reference immutable SHAs (not branches) for reproducible builds; document intended compatibility. |
| Add an adapter layer | Decouple your domain from external module APIs; make upgrades local to adapters. |
| Document contracts | Version API/Schema; record breaking changes and migration steps in CHANGELOG/ADR. |
| Automate updates | Provide scripts or CI jobs to update, validate, and test submodule bumps consistently. |

## ❌ Never Do (Anti‑patterns to Avoid)

| Practice | Details and Rationale |
|---|---|
| Create coupling for code reuse | Don’t introduce cross‑module DRY that forces lock‑step releases; duplication across services is better than tight coupling. |
| Expose internal DB structures | Avoid shared databases across modules; treat databases as internal implementation details. |
| Monolithic builds for independent units | Don’t bind multiple independent services to one build; it prevents independent deployability. |
| Fragile dependencies | Don’t depend on unstable external properties or long chains of calls (train‑wrecks). |
| Arbitrary modularization | Don’t slice only by technical patterns; use domain‑meaningful modules. |
| Commit failing code/tests | CI must always be green; never comment out failing tests. |
| Let unintegrated changes persist | Long‑lived branches/submodule drifts increase risk and costs. |
| Track submodule to a moving branch | Avoid `branch = main` in `.gitmodules`; use pinned SHAs and explicit bumps. |

## 🧭 Decision Guide: Submodule vs. Package vs. Copy

- Use a **separate repository + package** (preferred) when:
  - Consumers are many and heterogeneous
  - SemVer publishing is feasible (npm, PyPI, NuGet)
  - Interfaces are stable and narrow
- Use a **submodule** when:
  - You need a precise snapshot of another repo and cannot publish a package
  - Legal/compliance requires vendored source
  - You can pin to SHAs and accept explicit bump workflow
- Use a **one‑time copy (vendoring)** when:
  - The reused code is small and stable; local modifications expected
  - Coupling must be avoided and a package or submodule is overkill

## 🔧 Operational Workflows

### A. Add a Submodule

1) Choose repository URL and commit to pin (immutable SHA)  
2) Add to `.gitmodules` and initialize locally  
3) Commit `.gitmodules` and submodule entry  
4) Add `README` in module folder documenting purpose, upstream URL, pinned SHA, update policy  
5) Add an adapter layer in your code; avoid direct domain coupling  
6) Update CI to fetch submodules and run tests

Checklist:
- [ ] Submodule added with pinned SHA (no branch tracking)
- [ ] Adapter layer introduced; no domain leakage
- [ ] CI fetches submodules (`git submodule update --init --recursive`)
- [ ] Ownership and review rules defined
- [ ] ADR recorded if boundary decision impacts architecture

### B. Update (Bump) a Submodule

1) Pull upstream and select target commit/tag  
2) Update submodule pointer to that commit; do not track a branch  
3) Run full test suite; validate contracts via adapter tests  
4) Update CHANGELOG with notable changes and migration steps  
5) Open PR titled "chore(submodule): bump <name> to <sha|tag>"  
6) Merge only when CI is green and consumers validated

Checklist:
- [ ] Pointer updated to immutable SHA/tag
- [ ] Breaking changes assessed; adapters updated
- [ ] All tests green (unit/integration/e2e)
- [ ] CHANGELOG/ADR updated
- [ ] Release notes linked (if available)

### C. Remove a Submodule

1) Verify no consumers depend on it (code search + owners ACK)  
2) Replace with package/copy if needed; migrate adapters  
3) Deinitialize and remove from `.gitmodules` and Git config  
4) Delete folder and clean git metadata  
5) Update docs and CI; remove fetch steps

Checklist:
- [ ] Consumers migrated or removed
- [ ] `.gitmodules` entry removed; config cleaned
- [ ] CI updated; no more submodule fetch
- [ ] Docs updated (README/ADR)

## ⚙️ Configuration: `.gitmodules`

Example:
```ini
[submodule "external/shared-kernel"]
	path = external/shared-kernel
	url = https://github.com/org/shared-kernel.git
	# Avoid branch tracking for reproducibility; pin to commits in the main repo
	# branch = main   ← Not recommended
```

Tips:
- Keep submodules under a dedicated folder (e.g., `external/`)
- Avoid nested submodules where possible (complex fetch/update)
- Review `.gitmodules` changes like code (require PR and approval)

## 🧱 Architectural Boundaries and Contracts

- Treat submodules as separate countries: interact only via explicit, narrow interfaces.
- Use an **Adapter Layer** to translate between your domain and the submodule’s API or model.
- If you must share model elements, define a **Shared Kernel**:
  - Minimal, stable, and curated subset only
  - High‑overhead governance: versioned contracts, reviews, ADRs
  - Changes require consumer sign‑off

Contract Checklist:
- [ ] Public API documented and versioned (SemVer)
- [ ] No leakage of submodule internals into your domain
- [ ] Backwards compatibility policy defined
- [ ] Deprecations announced with timelines

## 🔄 CI/CD Integration

- Ensure CI performs: `git submodule update --init --recursive` before builds
- Cache submodule clones to speed up pipelines
- Run the full test pyramid (unit → integration → e2e) after any submodule change
- Block merges if:
  - Tests fail
  - Coverage drops below threshold
  - Contracts break (consumer adapter tests fail)

Example (GitHub Actions snippet):
```yaml
- name: Checkout repository (with submodules)
  uses: actions/checkout@v5
  with:
    submodules: recursive

- name: Install dependencies and build
  run: |
    npm ci
    npm run build

- name: Run tests
  run: npm test -- --coverage
```

## 🧪 Testing Strategy for Submodules

- Write consumer‑centric tests around the Adapter Layer
- Use contract tests to pin expected behavior
- For shared kernels, maintain a shared contract test suite versioned with the kernel
- Simulate failure modes of the submodule in integration tests (timeouts, errors)

## 🧹 Hygiene and Maintenance

- Review submodule pointers quarterly (or per release)
- Track upstream security advisories; patch promptly
- Auto‑open PRs for new tags with changelog summaries
- Keep local forks to a minimum; upstream contributions preferred

## 🚨 Security and Compliance

- Scan submodule code with SCA/linters/security tools
- Verify license compatibility; record license files under `external/<module>/LICENSE`
- Avoid including secrets or deploy keys in submodule history

## 📚 Traceability and Documentation

- Record architectural decisions involving submodules in `03-architecture/decisions/` (ADRs)
- Cross‑link requirements that motivate the submodule (e.g., performance, compliance)
- Update architecture views (context and component diagrams) to show boundaries

## ✅ Checklists

### Submodule Change PR Checklist
- [ ] Purpose and scope explained (why bump/add/remove)
- [ ] Pinned SHA/tag referenced; link to upstream release
- [ ] Contracts unchanged or migration steps documented
- [ ] Adapter tests added/updated and passing
- [ ] CI green; coverage within thresholds
- [ ] Security/license checks passed
- [ ] ADR created/updated if boundaries changed

### Repository Readiness Checklist (using Submodules)
- [ ] `.gitmodules` present and reviewed
- [ ] CI configured to fetch submodules
- [ ] `external/` folder organized with READMEs per module
- [ ] Adapter layer exists; no domain leakage
- [ ] Contract tests in place
- [ ] Update policy documented (cadence, owners)

## 🧭 When Not to Use Submodules

Prefer packages or copies when:
- You need SemVer releases and broad consumption
- Consumers cannot tolerate frequent SHA bumps
- The dependency is small and stable (cheaper to copy)
- Tooling or team discipline for submodules is limited

## 🔗 References

- ISO/IEC/IEEE 12207:2017 — Software life cycle processes
- ISO/IEC/IEEE 42010:2011 — Architecture description
- ISO/IEC/IEEE 29148:2018 — Requirements engineering
- Martin Fowler — Microservices, Bounded Contexts, Shared Kernel
- XP practices — Continuous Integration, Collective Ownership, Refactoring
