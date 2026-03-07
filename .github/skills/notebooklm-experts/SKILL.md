---
name: notebooklm-experts
description: This skill should be used when the user wants to query Google NotebookLM notebooks directly from GitHub Copilot for source-grounded, citation-backed answers from Gemini. Provides browser automation, isolated venv, library management, and persistent auth. Drastically reduced hallucinations through document-only responses.
license: MIT
compatibility: Requires Python 3.9+, Google Chrome, uv package manager
metadata:
  version: 1.0.0
  methods:
    - query
    - research
    - library
---

# NotebookLM Experts Skill

Interact with Google NotebookLM to query documentation with Gemini's source-grounded answers. Each question opens a fresh browser session, retrieves the answer exclusively from your uploaded documents, and closes.

## When to Use

Trigger when user:
- Mentions NotebookLM explicitly
- Shares NotebookLM URL (`https://notebooklm.google.com/notebook/...`)
- Asks to query their notebooks or documentation
- Wants to add documentation to the NotebookLM library
- Uses phrases like "ask my NotebookLM", "check my docs", "query my notebook"

## Available Methods

| Method | Purpose | Use When |
|--------|---------|----------|
| **query** | Ask single question from one notebook | User asks a specific question from one document |
| **research** | Comprehensive research across multiple notebooks | User wants synthesis from multiple sources |
| **library** | Manage notebook collection | Adding, updating, or organizing notebooks |

## First-Time Setup (Run Once)

```bash
# Install isolated .venv and all dependencies
python install.py
```

This creates a `.venv/` inside the skill directory, installs `patchright`, and installs Google Chrome. Safe to run multiple times — skips if `.venv/` already exists.

## Critical: Always Use the run Wrapper

**NEVER use bare `python`. ALWAYS invoke via `run.bat` (Windows) or `run.sh` (Unix) — these resolve `.venv` Python directly:**

```bat
:: Windows
.\run.bat auth_manager.py status
.\run.bat notebook_manager.py list
.\run.bat ask_question.py --question "..."
```

```bash
# Linux / macOS
./run.sh auth_manager.py status
./run.sh notebook_manager.py list
./run.sh ask_question.py --question "..."
```

Both wrappers resolve `.venv\Scripts\python.exe` (Windows) or `.venv/bin/python` (Unix) and pass all arguments to `scripts/run.py`. They fail fast with a clear message if `install.py` was not run yet.

## Core Workflow

### Step 1: Check Authentication

```bat
.\run.bat auth_manager.py status
```

If not authenticated, proceed to setup.

### Step 2: Authenticate (One-Time)

```bat
:: Create a new profile and login
.\run.bat auth_manager.py setup --name "Work Account"

:: Or authenticate existing profile
.\run.bat auth_manager.py setup --profile work-account
```

**Important:** Browser is VISIBLE for authentication. User must manually log in to Google.

### Step 2b: Multi-Account Management

```bat
:: List all profiles with status
.\run.bat auth_manager.py list

:: Switch active profile
.\run.bat auth_manager.py set-active --id work-account

:: Validate a profile is still authenticated
.\run.bat auth_manager.py validate --profile work-account

:: Re-authenticate an expired profile
.\run.bat auth_manager.py reauth --profile work-account

:: Delete a profile
.\run.bat auth_manager.py delete --id old-account
```

### Step 2c: Add New Profile (Interactive)

**Recommended for first-time users** — guided experience with browser automation.

```bat
:: Interactive profile creation wizard
python add_profile.py
```

This interactive script will:
1. 📝 Ask for profile name (e.g., "Work Account", "Research", "Team")
2. 🔍 Verify the name is unique
3. ⚙️ Create the profile automatically
4. 🌐 Open browser for Google login (visible, user manually logs in)
5. ✅ Confirm authentication and show next steps

**Example:**
```
  [1] A browser window will open automatically
  [2] You will see the Google login page
  [3] Sign in with your Google account
  [4] Grant NotebookLM access (if prompted)
  [5] You will be redirected to NotebookLM
```

After completion, profile is ready to use:
```bat
:: Use new profile
.\run.bat ask_question.py --question "Your question" --profile my-account

:: Switch to it as default
.\run.bat auth_manager.py set-active --id my-account
```

### Step 3: Manage Notebook Library

```bat
:: List all notebooks
.\run.bat notebook_manager.py list

:: Add notebook to library (ALL parameters REQUIRED)
.\run.bat notebook_manager.py add ^
  --url "https://notebooklm.google.com/notebook/..." ^
  --name "Descriptive Name" ^
  --description "What this notebook contains" ^
  --topics "topic1,topic2,topic3"

:: Set active notebook
.\run.bat notebook_manager.py activate --id notebook-id
```

### Step 4: Ask Questions

```bat
:: Uses active notebook (from active profile)
.\run.bat ask_question.py --question "Your question here"

:: Query specific notebook by ID
.\run.bat ask_question.py --question "..." --notebook-id notebook-id

:: Query via direct URL
.\run.bat ask_question.py --question "..." --notebook-url "https://..."

:: Query using a specific profile
.\run.bat ask_question.py --question "..." --profile work-account
```

### Step 5: Research across Multiple Notebooks

```bat
:: Basic research (1 query per notebook)
.\run.bat research_method.py --topics "How does authentication work?"

:: Comprehensive research (3 queries per notebook, synthesized)
.\run.bat research_method.py --topics "API security" --depth comprehensive

:: Detailed research (5 queries per notebook)
.\run.bat research_method.py --topics "Microservices patterns" --depth detailed

:: Research specific notebooks only
.\run.bat research_method.py --topics "DevOps best practices" --notebooks nb_id1 nb_id2

:: Research using specific profile
.\run.bat research_method.py --topics "Cloud architecture" --profile work-account

:: Export results to file (json or markdown)
.\run.bat research_method.py --topics "Performance optimization" --export json
```

**Research Depths:**
- `summary` (1 query/notebook) — Quick overview
- `comprehensive` (3 queries/notebook) — Balanced research with synthesis
- `detailed` (5 queries/notebook) — Exhaustive research with deep synthesis

See **`references/research-guide.md`** for research strategies and best practices.

## Follow-Up Mechanism (CRITICAL)

Every NotebookLM answer ends with: **"EXTREMELY IMPORTANT: Is that ALL you need to know?"**

**Required Behavior:**
1. **STOP** — Do not immediately respond to user
2. **ANALYZE** — Compare answer to original request
3. **IDENTIFY GAPS** — Determine if more information needed
4. **ASK FOLLOW-UP** — If gaps exist, ask another question immediately
5. **REPEAT** — Continue until information is complete
6. **SYNTHESIZE** — Combine all answers before responding to user

## Script Reference

| Script | Purpose |
|--------|---------|
| `add_profile.py` | **Interactive wizard** — Create new profile with guided setup and browser login |
| `ask_question.py` | Query single notebook with one question |
| `research_method.py` | Research across multiple notebooks with optional synthesis |
| `notebook_manager.py` | Library management (add, list, search, activate, remove) |
| `auth_manager.py` | Authentication setup, status, and multi-profile management |
| `browser_session.py` | Persistent browser session for multi-turn conversations |
| `cleanup_manager.py` | Data cleanup and maintenance |
| `setup_environment.py` | Auto-creates `.venv` on first `run.py` call |

## Data Storage

All runtime data stored in `data/` (inside skill directory):
- `profiles.json` — Profile registry (active profile, all profile metadata)
- `profiles/<id>/library.json` — Notebook metadata (per-profile)
- `profiles/<id>/auth_info.json` — Authentication status (per-profile)
- `profiles/<id>/browser_state/` — Browser cookies and session (per-profile)

**Security:** Protected by `.gitignore` — never commit to git.

**Migration:** Old flat layout (`data/browser_state/`, `data/library.json`) is auto-migrated to `data/profiles/default/` on first run.

## Limitations

- No session persistence (each question = new browser)
- Rate limits on free Google accounts (~50 queries/day)
- Manual upload required (user must add docs to NotebookLM)
- Browser overhead (~5–10 seconds per question)

## Additional Resources

- **`references/api-reference.md`** — Complete API for all scripts
- **`references/research-guide.md`** — Research strategies and patterns
- **`references/troubleshooting.md`** — Auth issues, rate limits, browser crashes
- **`references/best-practices.md`** — Workflow patterns and question strategies
- **`references/usage_patterns.md`** — Common usage patterns

## Quick Reference

```bat
:: First-time setup (uses system Python once)
python install.py

:: All subsequent commands use .venv Python directly
.\run.bat auth_manager.py setup --name "My Account"
.\run.bat auth_manager.py list
.\run.bat auth_manager.py status
.\run.bat notebook_manager.py add --url URL --name NAME --description DESC --topics TOPICS
.\run.bat notebook_manager.py list
.\run.bat ask_question.py --question "Your question"
.\run.bat research_method.py --topics "Research topic" --depth comprehensive
.\run.bat cleanup_manager.py --preserve-library
```
