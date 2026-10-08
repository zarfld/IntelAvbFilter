---
description: "Mandatory README authoring and maintenance rules; loads the preserved detailed README guidance only when README files are in scope."
applyTo: "**/README.md,**/readme.md,**/Readme.md"
---

# README Documentation Instructions

When creating, editing, reviewing, or materially validating a README file, **consult and apply the complete preserved guidance in** `.github/docs/README-INSTRUCTIONS-REFERENCE.md` before making substantive changes.

Moving the detailed guidance out of this auto-applied file is a context-routing optimization only. The preserved rules remain intentional and applicable to README work; do not treat them as optional merely because they are stored as reference material.

Repository-wide rules in `.github/copilot-instructions.md`, current CI/contracts, and authoritative project state override the preserved reference if an actual conflict exists. Report material conflicts instead of silently choosing the easier rule.

At minimum, README changes must remain:

- accurate to the current repository and honest about status/capabilities;
- concrete and executable for commands/examples that are presented as working;
- consistent with repository terminology and authoritative documentation;
- free of unnecessary duplication when an authoritative source can be linked;
- maintained alongside relevant behavior, setup, dependency, documentation, process, and release changes;
- checked for broken links, stale examples, misleading claims, and Markdown rendering problems before completion.
