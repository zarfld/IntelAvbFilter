# Copilot Agent Catalog

This catalog is documentation only. It lives outside `.github/agents/` deliberately so VS Code does not interpret the catalog itself as a custom agent.

## Repository agents

| Agent | File | Primary responsibility |
|---|---|---|
| Standards Compliance Advisor | `.github/agents/standards-compliance-advisor.agent.md` | Cross-phase lifecycle, standards, governance and phase-routing guidance |
| Requirements Analyst | `.github/agents/requirements-analyst.md` | Requirements elicitation, refinement, validation, and GitHub-Issue requirement work |
| Architecture Strategist | `.github/agents/architecture-strategist.md` | Architecture decisions, components, quality attributes, and architecture traceability |
| TDD Driver | `.github/agents/tdd-driver.md` | Test-first implementation workflow |
| Testing Specialist | `.github/agents/testing-specialist.md` | Test planning, design, execution strategy, and evidence review |
| Security Analyst | `.github/agents/security-analyst.md` | Driver/security analysis and security-focused review |
| Documentation Expert | `.github/agents/documentation-expert.md` | Maintained technical documentation and documentation quality |
| Skill Creator | `.github/agents/skill_creator.md` | Creating/refining Agent Skills |

Current GitHub and VS Code support repository custom agents as Markdown files in `.github/agents/`; `.agent.md` is the preferred explicit form in creation workflows, while plain `.md` agent profiles in that directory remain supported. Do not rename working agents solely for cosmetic consistency.

The historical pre-P0.1 agent-directory README is preserved at `.github/docs/legacy/AGENTS-README-LEGACY.md`; it is not an executable current catalog.
