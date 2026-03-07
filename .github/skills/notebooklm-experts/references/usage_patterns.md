# NotebookLM Skill Usage Patterns

## Pattern 1: Initial Setup

```mermaid
stateDiagram-v2
    [*] --> CheckAuth: Step 1

    CheckAuth --> chk1: auth_manager.py status
    state chk1 <<choice>>
    chk1 --> Authenticated: Valid session
    chk1 --> NeedSetup: Not authenticated

    NeedSetup --> DoSetup: Step 2
    DoSetup --> VisibleBrowser: auth_manager.py setup
    VisibleBrowser --> WaitLogin: User logs in
    WaitLogin --> Authenticated

    Authenticated --> AddNotebook: Step 3
    AddNotebook --> DiscoverContent: ask_question.py overview query
    DiscoverContent --> RegisterNB: notebook_manager.py add
    RegisterNB --> [*]: Ready to query

    note right of VisibleBrowser
        Browser MUST be visible
    end note

    note right of AddNotebook
        ASK user for metadata
        or use smart discovery
    end note
```

---

## Pattern 2: Adding Notebooks

```mermaid
stateDiagram-v2
    [*] --> UserSharesURL

    UserSharesURL --> chk1
    state chk1 <<choice>>
    chk1 --> SmartDiscovery: Option A Recommended
    chk1 --> AskUser: Option B Fallback

    state SmartDiscovery {
        [*] --> QueryNB: ask_question.py overview
        QueryNB --> ExtractInfo: Parse name, desc, topics
        ExtractInfo --> AddWithInfo: notebook_manager.py add
        AddWithInfo --> [*]
    }

    state AskUser {
        [*] --> AskMeta: Ask user for metadata
        AskMeta --> AddManual: notebook_manager.py add
        AddManual --> [*]
    }

    SmartDiscovery --> [*]: Notebook registered
    AskUser --> [*]: Notebook registered

    note right of SmartDiscovery
        NEVER guess content
        NEVER use generic descriptions
    end note
```

---

## Pattern 3: Daily Research

```mermaid
stateDiagram-v2
    [*] --> CheckLib: notebook_manager.py list

    CheckLib --> SelectNB: Pick relevant notebook
    SelectNB --> AskDetailed: ask_question.py --question
    AskDetailed --> GotAnswer

    GotAnswer --> chk1
    state chk1 <<choice>>
    chk1 --> FollowUp: Needs more detail
    chk1 --> [*]: Complete

    FollowUp --> AskDetailed: Follow-up with context
```

---

## Pattern 4: Follow-Up Questions

```mermaid
stateDiagram-v2
    [*] --> AskInitial: ask_question.py

    AskInitial --> CheckResponse

    CheckResponse --> chk1
    state chk1 <<choice>>
    chk1 --> AnalyzeGaps: "Is that ALL you need?"
    chk1 --> Synthesize: Answer complete

    AnalyzeGaps --> chk2
    state chk2 <<choice>>
    chk2 --> AskFollowUp: Gaps found
    chk2 --> Synthesize: No gaps

    AskFollowUp --> CheckResponse: Specific follow-up
    Synthesize --> RespondUser: Combine all answers
    RespondUser --> [*]

    note right of AnalyzeGaps
        STOP before responding to user
        ANALYZE answer completeness
    end note
```

---

## Pattern 5: Multi-Notebook Research

```mermaid
stateDiagram-v2
    [*] --> ActivateNB1: notebook_manager.py activate --id nb1
    ActivateNB1 --> QueryNB1: ask_question.py --question
    QueryNB1 --> ActivateNB2: notebook_manager.py activate --id nb2
    ActivateNB2 --> QueryNB2: ask_question.py same question
    QueryNB2 --> Compare: Synthesize answers
    Compare --> [*]
```

---

## Pattern 6: Error Recovery

```mermaid
stateDiagram-v2
    [*] --> ErrorType

    state ErrorType <<choice>>
    ErrorType --> AuthFail: Auth error
    ErrorType --> BrowserCrash: Browser error
    ErrorType --> RateLimit: Rate limited

    AuthFail --> CheckStatus: auth_manager.py status
    CheckStatus --> DoReauth: auth_manager.py reauth
    DoReauth --> [*]: Resolved

    BrowserCrash --> Cleanup: cleanup --preserve-library
    Cleanup --> Reauth: auth_manager.py setup
    Reauth --> [*]: Resolved

    RateLimit --> chk1
    state chk1 <<choice>>
    chk1 --> WaitHour: Wait
    chk1 --> SwitchAcct: auth_manager.py reauth
    WaitHour --> [*]
    SwitchAcct --> [*]
```

---

## Pattern 7: Batch Processing

```mermaid
stateDiagram-v2
    [*] --> LoadQuestions: Prepare question list

    LoadQuestions --> NextQ: Pick next question
    NextQ --> AskQ: ask_question.py --question Q --notebook-id ID
    AskQ --> WaitDelay: sleep 2s (rate limit)

    WaitDelay --> chk1
    state chk1 <<choice>>
    chk1 --> NextQ: More questions
    chk1 --> [*]: All done
```

---

## Pattern 8: Notebook Organization

```mermaid
stateDiagram-v2
    [*] --> OrganizeByDomain

    state OrganizeByDomain {
        [*] --> Backend: api, rest, backend
        [*] --> Frontend: react, css, frontend
        [*] --> Infra: database, devops
    }

    OrganizeByDomain --> SearchByDomain: notebook_manager.py search --query DOMAIN
    SearchByDomain --> [*]

    note right of OrganizeByDomain
        Use consistent topic tags
        ALWAYS ask user for descriptions
    end note
```

---

## Pattern 9: Library Management

```mermaid
stateDiagram-v2
    [*] --> Register: notebook_manager.py add (once per notebook)

    Register --> ListIDs: notebook_manager.py list
    ListIDs --> SetActive: activate --id project-beta

    SetActive --> QueryDefault: ask_question.py uses active by default
    QueryDefault --> QueryOverride: --notebook-id overrides active
    QueryOverride --> [*]

    note right of SetActive
        Persistent across sessions
    end note
```

---

## Copilot Workflow: User Sends URL

```mermaid
stateDiagram-v2
    [*] --> DetectURL: notebooklm.google.com in message

    DetectURL --> CheckLibrary: notebook_manager.py list
    CheckLibrary --> chk1
    state chk1 <<choice>>
    chk1 --> QueryExisting: URL already in library
    chk1 --> AskUserMeta: URL not in library

    AskUserMeta --> GetName: What to call this notebook?
    GetName --> GetDesc: What does it contain?
    GetDesc --> GetTopics: What topics?
    GetTopics --> AddNB: notebook_manager.py add
    AddNB --> QueryExisting

    QueryExisting --> Answer: ask_question.py
    Answer --> [*]
```

---

## Copilot Workflow: Research Task

```mermaid
stateDiagram-v2
    [*] --> UnderstandTask

    UnderstandTask --> FormulateQs: Craft comprehensive questions
    FormulateQs --> AskQ: ask_question.py --question Q

    AskQ --> CheckAnswer
    state CheckAnswer <<choice>>
    CheckAnswer --> AskFollowUp: "Is that ALL you need?"
    CheckAnswer --> NextQ: Answer complete

    AskFollowUp --> AskQ: Specific follow-up
    NextQ --> chk1
    state chk1 <<choice>>
    chk1 --> FormulateQs: More questions
    chk1 --> Synthesize: All answered

    Synthesize --> Implement: Build from answers
    Implement --> [*]
```
9. **Test auth regularly** - Before important tasks
10. **Document everything** - Keep notes on notebooks

## Quick Reference

```bash
# Always use run.py!
.\\run.bat [script].py [args]

# Common operations
run.py auth_manager.py status          # Check auth
run.py auth_manager.py setup           # Login (browser visible!)
run.py notebook_manager.py list        # List notebooks
run.py notebook_manager.py add ...     # Add (ask user for metadata!)
run.py ask_question.py --question ...  # Query
run.py cleanup_manager.py ...          # Clean up
```

**Remember:** When in doubt, use run.py and ask the user for notebook details!
