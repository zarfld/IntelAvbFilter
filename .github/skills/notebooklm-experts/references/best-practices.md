---
name: notebooklm
version: 1.0.0
description: Best practices and workflow patterns
---

# Best Practices

## Workflow Patterns

### Pattern 1: Research Session

```mermaid
stateDiagram-v2
    [*] --> CheckLib: notebook_manager.py list

    CheckLib --> ActivateNB: activate --id nb_docs
    ActivateNB --> BroadQ: Step 1 Broad question
    BroadQ --> AskBroad: ask_question.py overview
    AskBroad --> SpecificQ: Step 2 Specific questions

    SpecificQ --> Q1: ask_question.py specific Q1
    Q1 --> Q2: ask_question.py specific Q2
    Q2 --> Q3: ask_question.py specific Q3
    Q3 --> Synthesize: Step 3 Combine answers
    Synthesize --> [*]
```

---

### Pattern 2: Multi-Notebook Research

```mermaid
stateDiagram-v2
    [*] --> QueryNB1: ask_question.py --notebook-id nb_api
    QueryNB1 --> QueryNB2: ask_question.py --notebook-id nb_arch
    QueryNB2 --> QueryNB3: ask_question.py --notebook-id nb_changelog
    QueryNB3 --> CombineAnswers: Synthesize across notebooks
    CombineAnswers --> [*]
```

---

### Pattern 3: Discovery Before Add

```mermaid
stateDiagram-v2
    [*] --> QueryContent: ask_question.py --notebook-url URL

    QueryContent --> ExtractMeta: Parse name, desc, topics
    ExtractMeta --> AddWithMeta: notebook_manager.py add
    AddWithMeta --> [*]: Notebook registered with accurate metadata

    note right of QueryContent
        Question: What is the content?
        What topics are covered?
    end note
```

---

## Question Strategies

```mermaid
stateDiagram-v2
    [*] --> PickStrategy

    state PickStrategy <<choice>>
    PickStrategy --> BroadToNarrow: Start broad, then narrow
    PickStrategy --> RequestExamples: Ask for examples
    PickStrategy --> AskComparisons: Compare A vs B
    PickStrategy --> Troubleshoot: Debug specific issue

    BroadToNarrow --> Overview: What does this cover?
    Overview --> Specific: How do I implement X?
    Specific --> [*]

    RequestExamples --> Example: Show example of X
    Example --> [*]

    AskComparisons --> Diff: Differences between A and B?
    Diff --> [*]

    Troubleshoot --> Errors: Common errors when doing X?
    Errors --> [*]
```

### Anti-Patterns

| Avoid | Instead |
|-------|---------|
| "Tell me everything" | "What are the main sections?" |
| "What is on page 47?" | "Configuration options for X?" |
| "Fix this code" | "How to implement Y feature?" |
| "Why did they design it?" | "What does docs say about Z decision?" |

---

## Library Organization

```mermaid
stateDiagram-v2
    [*] --> ChooseStrategy

    state ChooseStrategy <<choice>>
    ChooseStrategy --> ByProject: Organize by project
    ChooseStrategy --> ByTopic: Organize by topic
    ChooseStrategy --> ByType: Organize by type

    state ByProject {
        [*] --> ProjA_Docs: Project A Docs
        ProjA_Docs --> ProjA_API: Project A API
        ProjA_API --> ProjB_Docs: Project B Docs
        ProjB_Docs --> [*]
    }

    state ByTopic {
        [*] --> FrontendGuide: frontend, react, css
        FrontendGuide --> BackendStd: backend, api, rest
        BackendStd --> DBSchema: database, postgres
        DBSchema --> [*]
    }

    state ByType {
        [*] --> UserDocs: reference, guide
        UserDocs --> APIRef: api, endpoints
        APIRef --> ADRs: architecture, decisions
        ADRs --> [*]
    }

    ByProject --> [*]
    ByTopic --> [*]
    ByType --> [*]
```

---

## Session Management

```mermaid
stateDiagram-v2
    [*] --> DailyWorkflow

    state DailyWorkflow {
        [*] --> Morning: notebook_manager.py list
        Morning --> DuringWork: ask_question.py as needed
        DuringWork --> EndOfDay: No cleanup needed
        EndOfDay --> [*]
    }

    DailyWorkflow --> WeeklyMaint

    state WeeklyMaint {
        [*] --> CheckStale: notebook_manager.py stats
        CheckStale --> RemoveOld: remove --id nb_old
        RemoveOld --> CleanTemp: cleanup --preserve-library
        CleanTemp --> [*]
    }

    WeeklyMaint --> [*]
```

---

## Rate Limit Management

```mermaid
stateDiagram-v2
    [*] --> TrackUsage: 50 queries/day

    TrackUsage --> chk1
    state chk1 <<choice>>
    chk1 --> BatchQs: Strategy 1: Batch questions
    chk1 --> UseActive: Strategy 2: Set active notebook once
    chk1 --> CacheAnswers: Strategy 3: Save important answers

    BatchQs --> SingleQ: Combine 3 questions into 1 comprehensive
    SingleQ --> [*]

    UseActive --> ActivateOnce: activate --id nb_main
    ActivateOnce --> QueryNoId: ask_question.py --question (no --notebook-id)
    QueryNoId --> [*]

    CacheAnswers --> SaveLocal: Save responses to local files
    SaveLocal --> ReferenceLocal: Read local instead of re-query
    ReferenceLocal --> [*]
```

---

## Security

```mermaid
stateDiagram-v2
    [*] --> DataProtection

    state DataProtection {
        [*] --> GitIgnore: Never commit data/
        GitIgnore --> LocalOnly: Library data is local only
        LocalOnly --> SessionIsolation: Each query is independent
        SessionIsolation --> [*]
    }

    DataProtection --> [*]

    note right of DataProtection
        .gitignore must include:
        data/
        .venv/
        __pycache__/
    end note
```

---

## Integration

```mermaid
stateDiagram-v2
    [*] --> UserAsks: User question

    UserAsks --> CheckNB: Query NotebookLM for internal info
    CheckNB --> GotInternal: Got notebook answer

    GotInternal --> chk1
    state chk1 <<choice>>
    chk1 --> Synthesize: Answer sufficient
    chk1 --> WebSearch: Need external context

    WebSearch --> GotExternal: Got web results
    GotExternal --> Synthesize

    Synthesize --> RespondUser: Combine all sources
    RespondUser --> [*]
```

---

## Performance Optimization

### Query Optimization

**Fast queries:**
- Specific, focused questions
- Reference known sections
- Request specific formats

```bash
.\\run.bat ask_question.py \
  --question "List the three authentication methods from the API reference"
```

**Slower queries:**
- Open-ended questions
- Synthesis across documents
- Complex comparisons

```bash
.\\run.bat ask_question.py \
  --question "Analyze the evolution of our architecture decisions"
```

### Notebook Optimization

**For faster responses:**
- Upload focused documents (not entire wikis)
- Remove duplicate content
- Organize by topic
- Limit to 50-100 sources per notebook

---

## Common Workflows

### Onboarding New Team Member

```bash
# 1. Add onboarding docs
.\\run.bat notebook_manager.py add \
  --url "..." \
  --name "Engineering Onboarding" \
  --description "New hire guide and setup instructions" \
  --topics "onboarding,engineering,setup"

# 2. Answer their questions as they come up
.\\run.bat ask_question.py \
  --question "How do I set up the development environment?"
```

### Pre-Meeting Prep

```bash
# Query meeting notes from previous sessions
.\\run.bat ask_question.py \
  --question "What were the action items from last week's architecture review?"
```

### Code Review Context

```bash
# Query architecture docs for context
.\\run.bat ask_question.py \
  --question "What are the coding standards for this module?"
```
