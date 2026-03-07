# Research Method Guide

```mermaid
stateDiagram-v2
    [*] --> ChooseMethod

    state ChooseMethod <<choice>>
    ChooseMethod --> SingleQuery: Single notebook
    ChooseMethod --> MultiResearch: Multi-notebook

    SingleQuery --> AskQuestion: ask_question.py
    AskQuestion --> [*]

    MultiResearch --> SelectDepth: research_method.py
    SelectDepth --> RunResearch
    RunResearch --> [*]
```

---

## Research Depths

```mermaid
stateDiagram-v2
    [*] --> SelectDepth

    state SelectDepth <<choice>>
    SelectDepth --> Summary: --depth summary
    SelectDepth --> Comprehensive: --depth comprehensive
    SelectDepth --> Detailed: --depth detailed

    state Summary {
        [*] --> S_Query: 1 query per notebook
        S_Query --> S_Execute: Run across notebooks
        S_Execute --> S_Output: No synthesis
        S_Output --> [*]
    }

    state Comprehensive {
        [*] --> C_Q1: Q1 Definitions
        C_Q1 --> C_Q2: Q2 Methods
        C_Q2 --> C_Q3: Q3 Challenges
        C_Q3 --> C_Execute: Run all queries
        C_Execute --> C_Synth: AI synthesis
        C_Synth --> [*]
    }

    state Detailed {
        [*] --> D_Q1: Q1 Concepts
        D_Q1 --> D_Q2: Q2 Techniques
        D_Q2 --> D_Q3: Q3 Best practices
        D_Q3 --> D_Q4: Q4 Pitfalls
        D_Q4 --> D_Q5: Q5 Trends
        D_Q5 --> D_Execute: Run all queries
        D_Execute --> D_Synth: Deep synthesis
        D_Synth --> [*]
    }

    Summary --> [*]: ~5-10s per notebook
    Comprehensive --> [*]: ~15-30s per notebook
    Detailed --> [*]: ~30-60s per notebook
```

---

## Commands

```bash
# Single topic
.\run.bat research_method.py --topics "Authentication methods"

# Multiple topics
.\run.bat research_method.py --topics "REST APIs" "GraphQL" "gRPC" --depth comprehensive

# Specific notebooks
.\run.bat research_method.py --topics "Cloud architecture" --notebooks nb_123 nb_456

# Specific profile
.\run.bat research_method.py --topics "DevOps" --profile work-account

# Show browser
.\run.bat research_method.py --topics "Testing" --show-browser

# Export
.\run.bat research_method.py --topics "Performance" --export json
.\run.bat research_method.py --topics "Security" --export markdown
```

---

## Research Strategies

```mermaid
stateDiagram-v2
    [*] --> PickStrategy

    state PickStrategy <<choice>>
    PickStrategy --> Survey: Landscape overview
    PickStrategy --> DepthFirst: Single topic deep-dive
    PickStrategy --> Comparison: Compare two topics
    PickStrategy --> Progressive: Start shallow, deepen

    state Survey {
        [*] --> SurveyRun: comprehensive across all notebooks
        SurveyRun --> [*]
    }

    state DepthFirst {
        [*] --> DepthRun: detailed on selected notebooks
        DepthRun --> [*]
    }

    state Comparison {
        [*] --> CmpTopicA: Research topic A
        CmpTopicA --> CmpTopicB: Research topic B
        CmpTopicB --> CmpAnalyze: Compare answers
        CmpAnalyze --> [*]
    }

    state Progressive {
        [*] --> ProgSummary: summary first
        ProgSummary --> ProgCheck
        state ProgCheck <<choice>>
        ProgCheck --> ProgComp: Interesting, go deeper
        ProgCheck --> ProgDone: Skip
        ProgComp --> ProgDetailed: comprehensive
        ProgDetailed --> [*]
        ProgDone --> [*]
    }

    Survey --> [*]
    DepthFirst --> [*]
    Comparison --> [*]
    Progressive --> [*]
```

---

## Topic Decomposition

```mermaid
stateDiagram-v2
    [*] --> CompoundTopic: Complex question

    CompoundTopic --> fork1
    state fork1 <<fork>>
    fork1 --> Sub1: Sub-topic 1
    fork1 --> Sub2: Sub-topic 2
    fork1 --> Sub3: Sub-topic 3

    Sub1 --> join1
    Sub2 --> join1
    Sub3 --> join1
    state join1 <<join>>

    join1 --> Synthesize: Combine results
    Synthesize --> [*]
```

```bash
.\run.bat research_method.py \
  --topics "What is cloud computing?" "Cloud deployment models?" "Cloud service models?" \
  --depth comprehensive
```

---

## Export Formats

| Format | File | Content |
|--------|------|---------|
| JSON | `research_TOPIC_TIMESTAMP.json` | Structured data with metadata |
| Markdown | `research_TOPIC_TIMESTAMP.md` | Formatted report |
}
```

**Use JSON when:** Programmatic analysis, importing into databases, version control

### Markdown Export

**File:** `research_topic_timestamp.md`

Human-readable format with all queries and answers organized by notebook.

**Use Markdown when:** Sharing with humans, converting to docs, further editing

---

## Best Practices

### ✅ DO

- **Use specific, detailed topics** — "How do we implement caching?" vs "Caching"
- **Start with Summary** for unfamiliar topics to find relevant notebooks
- **Export and review** results before making decisions
- **Use Comprehensive for most work** — best balance of thoroughness and speed
- **Combine with ask_question.py** — follow-up on interesting findings with deeper questions

### ❌ DON'T

- **Use Detailed for casual research** — too slow, overkill for most use cases
- **Skip synthesis review** — read AI summaries critically, verify against answers
- **Research without related notebooks** — ensure your notebooks cover topic
- **Treat NotebookLM as ground truth** — always verify critical information
- **Ignore error messages** — research will note which notebooks failed

---

## Troubleshooting

### No results or empty answers

**Cause:** Topic not covered in notebooks

**Solution:**
1. Check notebook content: `.\run.bat notebook_manager.py list`
2. Add more relevant notebooks: `.\run.bat notebook_manager.py add --url ...`
3. Rephrase topic more specifically

### Research takes too long

**Cause:** Using `--depth detailed` with many notebooks

**Solution:**
1. Start with `--depth summary`
2. Use `--depth comprehensive` instead
3. Limit notebooks: `--notebooks nb1 nb2` (instead of all)

### Some notebooks fail

**Cause:** Notebook content changed or became inaccessible

**Solution:**
1. Verify notebook still works: `.\run.bat ask_question.py --notebook-id <id> --question "test"`
2. Remove inaccessible notebook: `.\run.bat notebook_manager.py remove --id <id>`
3. Re-add notebook: `.\run.bat notebook_manager.py add --url ...`

### Rate limit exceeded

**Cause:** Too many queries too fast on free Google account

**Solution:**
1. Wait until tomorrow (reset at ~midnight PST)
2. Use different Google account: `.\run.bat auth_manager.py setup --name "Account2"`
3. Use Comprehensive instead of Detailed to reduce queries

---

## Integration with ask_question.py

**Research reveals interesting findings → ask deeper questions:**

```bash
# Step 1: Research broadly
.\run.bat research_method.py --topics "Observability" --depth comprehensive

# Step 2: Pick most relevant notebook from results
# (e.g., "Platform Engineering Best Practices")

# Step 3: Ask follow-up question
.\run.bat ask_question.py \
  --question "How do we implement trace-based observability?" \
  --notebook-id <id-from-research>
```

---

## See Also

- **`api-reference.md`** — Complete API & parameters
- **`best-practices.md`** — General workflow patterns
- **`troubleshooting.md`** — Error resolution
