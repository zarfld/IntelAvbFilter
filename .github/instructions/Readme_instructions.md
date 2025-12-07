---
description: "Comprehensive guidance for writing, editing, and maintaining README.md files following documentation best practices and IEEE/ISO/IEC standards."
applyTo: 
  - "**/README.md"
  - "**/readme.md"
  - "**/Readme.md"
---

# README Documentation Instructions

**Purpose**: Ensure README files serve as effective, accurate, and maintainable entry points to project documentation  
**Standards**: IEEE/ISO/IEC documentation principles, DRY principle, Markdown best practices

## 🎯 Core Principles

The `README.md` file is **the most important file in your project**. It serves as:
- The **primary entry point** for understanding what the project does
- A **communication vehicle** for current and future developers
- A **living document** that evolves with the software
- A **navigation guide** directing readers to detailed documentation

## 📋 Essential Structure

### Required Sections (In Order)

Every README.md MUST contain these sections:

#### 1. Title

```markdown
# Project Name
```

- Match the repository name, folder name, and package manager name exactly
- Use H1 heading (single `#`)

#### 2. Short Description (The Hook)

```markdown
A brief, compelling description of what your project does in a nutshell.
```

- **Maximum 2-3 sentences**
- Answer: "What does this do?" and "Why should I care?"
- Place immediately after title, before any other content

#### 3. Purpose/Objectives (Optional but Recommended)

```markdown
## 🎯 Purpose

This project provides:
- Key benefit 1
- Key benefit 2
- Key benefit 3
```

#### 4. Table of Contents (For Long READMEs)

```markdown
## 📚 Table of Contents

- [Installation](#installation)
- [Usage](#usage)
- [Contributing](#contributing)
- [License](#license)
```

- Include only if README exceeds ~300 lines
- Use anchor links to sections

#### 5. Installation Instructions

```markdown
## 🚀 Installation

\`\`\`bash
# Step-by-step installation commands
npm install package-name
\`\`\`
```

- Provide **concrete, executable commands**
- Include prerequisites
- Cover common platforms (Windows, macOS, Linux)

#### 6. Usage Instructions

```markdown
## 📖 Usage

\`\`\`javascript
// Clear, runnable examples
const result = doSomething();
\`\`\`
```
- Show **realistic examples**
- Include expected output
- Cover common use cases

#### 7. Contribution Instructions
```markdown
## 🤝 Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.
```
- Link to separate CONTRIBUTING.md if detailed
- Or include brief guidelines inline

#### 8. License
```markdown
## 📄 License

This project is licensed under [License Name] - see [LICENSE.md](LICENSE.md).

```
- **MANDATORY** - users need to know usage rights
- Link to full license file

### Recommended Additional Sections

#### Project Status/Badges

```markdown
![Build Status](https://img.shields.io/badge/build-passing-brightgreen)
![Version](https://img.shields.io/badge/version-1.0.0-blue)
```

#### Features

```markdown
## ✨ Features

- Feature 1: Brief description
- Feature 2: Brief description
```

#### Documentation Links

```markdown
## 📚 Documentation

- [Architecture Overview](docs/architecture.md)
- [API Reference](docs/api.md)
- [User Guide](docs/user-guide.md)
```

#### Troubleshooting/FAQ

```markdown
## ❓ FAQ / Troubleshooting

**Q: Common question?**  
A: Clear answer with solution.
```

## ✅ ALWAYS DO: Quality Practices

### 1. Structure and Legibility

✅ **Write in Markdown (`.md` extension)**

- Utilize Markdown's formatting capabilities: **bold**, *italics*, `code`, lists
- Use proper heading hierarchy (H1 → H2 → H3)
- Include code blocks with language specification:

  ````markdown
  ```python
  # Python code here
  ```
  ````

✅ **Optimize for Readability**

- Keep paragraphs short (3-5 sentences max)
- Use bullet points for lists
- Add visual breaks with horizontal rules (`---`) when needed
- Use emojis sparingly for section markers (🎯, 📚, ⚠️)

✅ **Define All Terminology**

- Include a **Glossary** section or inline definitions
- Define all acronyms on first use: "TDD (Test-Driven Development)"
- Link to detailed terminology documentation if extensive

✅ **Include Supporting Documents**

- `LICENSE.md` - **MANDATORY** for open-source projects
- `CODE_OF_CONDUCT.md` - Establishes interaction rules
- `CONTRIBUTING.md` - Detailed contribution guidelines
- `CHANGELOG.md` - Version history and changes

### 2. Honest Status and Intent

✅ **Report Status Honestly**

- Use status badges showing **actual** build/test status
- Clearly mark sections as "Planned", "In Progress", "Complete"
- Include version number matching actual releases
- Document known issues and limitations openly

✅ **Use Precise Language for Commitment**

- **'shall'** = MANDATORY requirement (use sparingly)
- **'will'** = Statement of fact or planned future capability
- **'should'** = Recommendation or preference (not mandatory)
- **'must'** = Critical necessity

Example:

```markdown
## Installation Requirements

- You **must** have Node.js 18+ installed
- You **should** use npm 9+ for best compatibility
- The system **will** validate dependencies on startup
```

✅ **Document Assumptions**

```markdown
## Assumptions

- Users have basic JavaScript knowledge
- Development environment includes Git
- Target platform is Node.js 18+
```

✅ **Maintain Consistency with Reality**

- Update README when code behavior changes
- Sync version numbers with actual releases
- Remove or update outdated examples
- Reflect current project state, not aspirational state

### 3. Maintenance and Evolution

✅ **Treat README as a Living Document**

- Update with every significant code change
- Review and update during each release
- Add dates to time-sensitive information:

  ```markdown
  > Last updated: November 2025
  ```

✅ **Ensure Consistency**
- Use consistent terminology throughout
- Match naming conventions with code (class names, function names)
- Align with ubiquitous language from domain models
- Cross-reference related documentation

✅ **Focus on Insight, Not Duplication**
- **Don't duplicate code comments** - link to source files instead
- **Don't copy API signatures** - link to generated API docs
- **Do explain** design intent, architecture decisions, "why" not "what"
- **Do provide context** that code alone can't convey

✅ **Verify Before Committing**
- [ ] All code examples run successfully
- [ ] All links resolve correctly (no 404s)
- [ ] Installation instructions tested on clean environment
- [ ] Spelling and grammar checked
- [ ] Markdown renders correctly on GitHub

## ❌ NEVER DO: Practices to Avoid

### 1. Avoid Ambiguity and Subjectivity

❌ **Do NOT Use Vague Terms**
- ~~"easy to use"~~ → "requires 3 commands to install"
- ~~"best performance"~~ → "processes 10,000 requests/second"
- ~~"user friendly"~~ → "includes guided setup wizard"
- ~~"cost effective"~~ → "reduces deployment time by 40%"

❌ **Avoid Superlatives Without Evidence**
- ~~"the best solution"~~
- ~~"most efficient"~~
- ~~"fastest implementation"~~

❌ **Don't Use Absolute Terms Carelessly**
- ~~"always works"~~ → "works in 99.9% of standard configurations"
- ~~"never fails"~~ → "has failover mechanisms for common errors"
- ~~"all platforms"~~ → "supports Windows, macOS, and Linux"

❌ **Avoid Loopholes and Weasel Words**
- ~~"if possible"~~
- ~~"as appropriate"~~
- ~~"as applicable"~~
- ~~"to the extent practical"~~

### 2. Avoid Pre-Speculation and Over-Documentation

❌ **Do NOT Build for Tomorrow**
- Don't document features that don't exist yet
- Don't include "Future Plans" unless in clearly marked section
- Don't say "We're gonna need..." - implement when actually needed
- Focus on **current capabilities**

❌ **Do NOT Include TBD/TBS/TBR**
- ~~"Installation instructions TBD"~~ → Omit section until ready
- ~~"API reference to be specified"~~ → Mark as "Coming Soon" with date
- Complete sections before publishing or clearly mark as drafts

❌ **Do NOT Duplicate Knowledge (DRY)**
```markdown
<!-- ❌ BAD: Duplicating code -->
## API Reference

### calculateTotal(items)
Calculates the total price of items.

Parameters:
- items (Array): Array of items with price property
Returns: Number

<!-- ✅ GOOD: Linking to source of truth -->
## API Reference

See [API Documentation](docs/api.md) for complete reference.

Key functions:
- `calculateTotal()` - [Source](src/utils/math.ts) - Calculates totals
```

❌ **Do NOT Produce Unused Artifacts**
- Don't create sections "because templates have them"
- Remove sections that don't apply to your project
- Delete outdated examples rather than leaving them as misleading artifacts

### 3. Maintain Process Integrity

❌ **Do NOT Leave Broken Examples**
```markdown
<!-- ❌ BAD: Example doesn't work -->
\`\`\`javascript
// This is broken but left here anyway
const result = deprecatedFunction();
\`\`\`

<!-- ✅ GOOD: Working current example -->
\`\`\`javascript
// Current approach (as of v2.0)
const result = newFunction();
\`\`\`
```

❌ **Do NOT Mix Different Concerns**
- Keep setup/installation separate from usage
- Don't mix user documentation with developer documentation
- Separate "Getting Started" from "Advanced Configuration"

❌ **Do NOT Make False Promises**
- Estimates are not promises
- Use "estimated", "approximately", "typically" for time-based claims
- Example: "Installation typically takes 5-10 minutes" not "Installation takes 5 minutes"

## 🔍 README Quality Checklist

Use this checklist before committing README changes:

### Content Completeness
- [ ] Title matches repository name
- [ ] Short description answers "What does this do?"
- [ ] Installation instructions are complete and tested
- [ ] Usage examples are runnable and current
- [ ] License is clearly specified
- [ ] Contributing guidelines are linked or included

### Technical Accuracy
- [ ] All code examples execute successfully
- [ ] Version numbers match actual releases
- [ ] Dependencies list is current
- [ ] System requirements are accurate
- [ ] Links resolve correctly (no 404s)

### Clarity and Style
- [ ] No ambiguous terms (vague, subjective language)
- [ ] Acronyms defined on first use
- [ ] Consistent terminology throughout
- [ ] Proper Markdown formatting
- [ ] Readable by target audience

### Maintenance
- [ ] Reflects current state of code
- [ ] Outdated information removed or updated
- [ ] No TBD/TBS/TBR placeholders
- [ ] No broken examples
- [ ] Date of last update included if time-sensitive

### Standards Compliance
- [ ] Follows DRY principle (no unnecessary duplication)
- [ ] Links to authoritative sources rather than copying
- [ ] Honest about project status and limitations
- [ ] Uses precise commitment language (shall/will/should)

## 📝 Example README Template

```markdown
# Project Name

A concise description of what this project does and why it matters.

## 🎯 Purpose

This project provides:
- Key benefit or capability 1
- Key benefit or capability 2
- Key benefit or capability 3

## 🚀 Installation

\`\`\`bash
# Step by step installation
npm install project-name
\`\`\`

**Requirements:**
- Node.js 18+
- npm 9+

## 📖 Usage

\`\`\`javascript
// Simple, runnable example
import { doSomething } from 'project-name';

const result = doSomething({ option: 'value' });
console.log(result);
\`\`\`

## ✨ Features

- Feature 1: Brief explanation
- Feature 2: Brief explanation
- Feature 3: Brief explanation

## 📚 Documentation

- [Architecture Overview](docs/architecture.md)
- [API Reference](docs/api.md)
- [User Guide](docs/user-guide.md)

## 🤝 Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidelines.

## 📄 License

This project is licensed under MIT - see [LICENSE.md](LICENSE.md).

## 🔗 References

- [Related Project](https://example.com)
- [Documentation](https://docs.example.com)

---

Last updated: November 2025
\`\`\`

## 🔄 Update Triggers

Update the README when:

1. **Feature Changes**
   - New features added → Update Features section
   - Features deprecated → Update and mark as deprecated
   - Breaking changes → Update Usage examples

2. **Dependency Changes**
   - New requirements → Update Installation section
   - Version requirements change → Update Requirements

3. **Documentation Changes**
   - New documentation added → Update links
   - Documentation restructured → Update navigation

4. **Process Changes**
   - Contribution process changes → Update Contributing section
   - Build/test process changes → Update relevant sections

5. **Release Milestones**
   - Version releases → Update version badges
   - Status changes (alpha → beta → stable) → Update status

## 🛠️ Tools and Validation

### Markdown Linters
- Use markdown linters (markdownlint, remark-lint)
- Validate link integrity (markdown-link-check)
- Check spelling (cspell, vale)

### Automated Checks
```yaml
# Example GitHub Action check
- name: Validate README
  run: |
    npx markdownlint README.md
    npx markdown-link-check README.md
```

### Manual Review
- Read through as if you're a new user
- Test all installation instructions
- Run all code examples
- Click all links

## 🎓 Writing Style Guidelines

### Voice and Tone
- **Active voice**: "Install the package" not "The package should be installed"
- **Direct address**: "You can..." not "One can..."
- **Present tense**: "The system processes..." not "The system will process..."

### Formatting Conventions
- **Code elements**: Use `backticks` for code, commands, file names, variable names
- **Emphasis**: Use **bold** for important concepts, *italics* for introducing new terms
- **Lists**: Use `-` for unordered lists, `1.` for ordered lists
- **Section markers**: Use emoji consistently (🎯, 📚, ⚠️, ✅, ❌)

### Length Guidelines
- **Short description**: 1-3 sentences max
- **Paragraphs**: 3-5 sentences max
- **Code examples**: 5-15 lines (extract to separate file if longer)
- **Total README**: Aim for 200-500 lines (use TOC if exceeding 300 lines)

---

## Summary: README Excellence

A great README is:
- **Clear**: Understandable by the target audience
- **Accurate**: Reflects current project state
- **Complete**: Contains all essential sections
- **Maintainable**: Updated alongside code changes
- **Honest**: Truthful about capabilities and limitations
- **Focused**: Provides insight without duplication

Remember: **The README is the front door to your project. Make it welcoming, accurate, and helpful.** 🚀
