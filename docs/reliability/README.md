# Software Reliability Assets

This repo includes minimal, IEEE 1633-aligned scaffolding to plan, test, and make release decisions based on software reliability evidence.

## What’s Included

- Template: `spec-kit-templates/software-reliability-program-plan.md` (SRPP)
- Template: `spec-kit-templates/operational-profile.md` (OP)
- Gap Analysis: `standards-compliance/reviews/IEEE-1633-gap-analysis.md`
- Phase updates: `.github/instructions/phase-06-integration.instructions.md` and `.github/instructions/phase-07-verification-validation.instructions.md`

## How To Use

1. Create an SRPP for your product/release

- Copy the template into `05-implementation/docs/` (or your project docs area)
- Fill objectives (reliability, availability, residual defects), roles, data collection, models

1. Create an Operational Profile

- Copy the OP template into `05-implementation/docs/`
- Define states, transitions, and relative frequencies
- Map abstract actions to executable test adapters

1. Wire into Phase 06 (Integration)

- Ensure OP-driven tests are present and measure duty time + failures
- Define SRG model selection/fitting steps and evidence storage locations

1. Produce Reliability Evidence in Phase 07 (V&V)

- Fit models (e.g., Musa-Okumoto, GO, Crow/AMSAA) and verify accuracy
- Report reliability, availability (with restore time), residual defects, and confidence
- Gate release per the “Reliability Evidence and Release Decision” section

## Tips

- Start small: it’s fine to begin with one OP and one SRG model, then iterate
- Use CI to run OP-driven tests and accumulate reliability data continuously
- Keep evidence versioned alongside code and test artifacts

## References

- IEEE 1633-2016 Software Reliability (local copy ingested during analysis)
- ISO/IEC/IEEE: 12207, 29148, 42010, IEEE 1016, IEEE 1012
