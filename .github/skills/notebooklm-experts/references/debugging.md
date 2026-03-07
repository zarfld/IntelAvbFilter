---
name: notebooklm-debugging
description: Per-layer debug workflow and recovery guide for the NotebookLM skill
---

# NotebookLM Skill — Debugging Guide

```bat
.\run.bat debug_skill.py               :: Quick check
.\run.bat debug_skill.py --no-browser  :: Skip Chrome (fastest)
.\run.bat debug_skill.py --check-links :: Full check including URLs
```

---

## Debug Flow (5 Layers)

```mermaid
stateDiagram-v2
    [*] --> SmokeTest: run.bat debug_skill.py

    SmokeTest --> L1_Check: Layer 1 Environment
    state L1_Check <<choice>>
    L1_Check --> L2: PASS
    L1_Check --> L1_Fix: FAIL ModuleNotFoundError
    L1_Fix --> L1_Check: python install.py

    L2 --> L2_Check: Layer 2 Auth
    state L2_Check <<choice>>
    L2_Check --> L3: PASS
    L2_Check --> L2_Fix: FAIL redirect / auth error
    L2_Fix --> L2_Check: auth_manager.py setup

    L3 --> L3_Check: Layer 3 Library
    state L3_Check <<choice>>
    L3_Check --> L4: PASS
    L3_Check --> L3_Fix: FAIL no notebooks / corrupt
    L3_Fix --> L3_Check: notebook_manager.py add

    L4 --> L4_Check: Layer 4 Browser
    state L4_Check <<choice>>
    L4_Check --> L5: PASS
    L4_Check --> L4_Fix: FAIL crash / timeout
    L4_Fix --> L4_Check: install.py or config.py

    L5 --> L5_Check: Layer 5 Links
    state L5_Check <<choice>>
    L5_Check --> AllGreen: PASS
    L5_Check --> L5_Fix: FAIL inactive / error
    L5_Fix --> L5_Check: update or remove notebook

    AllGreen --> [*]: All layers healthy
```

---

## Layer 1: Environment

```mermaid
stateDiagram-v2
    [*] --> CheckVenv

    CheckVenv --> chk1
    state chk1 <<choice>>
    chk1 --> CheckPatchright: .venv exists
    chk1 --> RunInstall: .venv missing

    RunInstall --> CheckVenv: python install.py

    CheckPatchright --> chk2
    state chk2 <<choice>>
    chk2 --> CheckChrome: import OK
    chk2 --> RunInstall: ModuleNotFoundError

    CheckChrome --> chk3
    state chk3 <<choice>>
    chk3 --> EnvOK: Chrome installed
    chk3 --> RunInstall: Chrome missing

    EnvOK --> [*]: PASS
```

```bat
:: Manual verify
.\.venv\Scripts\python.exe -c "import patchright; print('OK')"
```

---

## Layer 2: Auth

```mermaid
stateDiagram-v2
    [*] --> CheckState

    CheckState --> chk1
    state chk1 <<choice>>
    chk1 --> CheckAge: state.json exists
    chk1 --> NeedSetup: state.json missing

    CheckAge --> chk2
    state chk2 <<choice>>
    chk2 --> CheckAccount: Less than 7 days old
    chk2 --> NeedReauth: Older than 7 days

    CheckAccount --> chk3
    state chk3 <<choice>>
    chk3 --> AuthOK: Correct account
    chk3 --> NeedSetup: Wrong account

    NeedSetup --> AuthOK: auth_manager.py setup
    NeedReauth --> AuthOK: auth_manager.py reauth

    AuthOK --> [*]: PASS

    note right of NeedSetup
        Browser MUST be visible
        User logs in manually
    end note
```

```bat
.\run.bat auth_manager.py status
```

---

## Layer 3: Library

```mermaid
stateDiagram-v2
    [*] --> CheckLibrary

    CheckLibrary --> chk1
    state chk1 <<choice>>
    chk1 --> CheckActive: Has notebooks
    chk1 --> AddNotebook: Library empty

    CheckActive --> chk2
    state chk2 <<choice>>
    chk2 --> CheckURL: Active notebook set
    chk2 --> ActivateNB: No active notebook

    CheckURL --> chk3
    state chk3 <<choice>>
    chk3 --> LibOK: URL present
    chk3 --> UpdateNB: URL missing

    AddNotebook --> CheckLibrary: notebook_manager.py add
    ActivateNB --> CheckActive: notebook_manager.py activate --id ID
    UpdateNB --> CheckURL: notebook_manager.py update --id ID --url URL

    LibOK --> [*]: PASS
```

```bat
.\run.bat notebook_manager.py list
.\run.bat notebook_manager.py add --url URL --name NAME --description DESC --topics TAGS
```

---

## Layer 4: Browser

```mermaid
stateDiagram-v2
    [*] --> LaunchChrome

    LaunchChrome --> chk1
    state chk1 <<choice>>
    chk1 --> LoadPage: Chrome launches OK
    chk1 --> ChromeFix: Crash or exception

    ChromeFix --> chk_fix
    state chk_fix <<choice>>
    chk_fix --> LaunchChrome: python install.py
    chk_fix --> ConfigFix: GPU / sandbox error

    ConfigFix --> LaunchChrome: Add --disable-gpu to config.py

    LoadPage --> chk2
    state chk2 <<choice>>
    chk2 --> BrowserOK: Page loads
    chk2 --> TimeoutFix: Timeout
    chk2 --> ReauthFix: Redirect to login

    TimeoutFix --> LoadPage: Increase PAGE_LOAD_TIMEOUT
    ReauthFix --> LoadPage: auth_manager.py reauth

    BrowserOK --> [*]: PASS
```

```bat
:: Debug with visible browser
.\run.bat ask_question.py --question "ping" --headful
```

---

## Layer 5: Notebook Links

```mermaid
stateDiagram-v2
    [*] --> CheckLinks: check_notebooks.py

    CheckLinks --> chk1
    state chk1 <<choice>>
    chk1 --> LinkActive: active
    chk1 --> LinkInactive: inactive
    chk1 --> LinkError: error

    LinkActive --> [*]: PASS

    LinkInactive --> FixInactive
    state FixInactive <<choice>>
    FixInactive --> UpdateURL: URL changed
    FixInactive --> RemoveNB: Notebook deleted

    UpdateURL --> CheckLinks: notebook_manager.py update --id ID --url URL
    RemoveNB --> CheckLinks: notebook_manager.py remove --id ID

    LinkError --> VerifyURL: Check URL format
    VerifyURL --> CheckLinks

    note right of LinkActive
        Valid URL format:
        notebooklm.google.com/notebook/ID
    end note
```

```bat
.\run.bat check_notebooks.py
.\run.bat notebook_manager.py update --id ID --url NEW_URL
.\run.bat notebook_manager.py remove --id ID
```

---

## Full Reset (Last Resort)

```mermaid
stateDiagram-v2
    [*] --> CleanRuntime: cleanup_manager.py --preserve-library
    CleanRuntime --> Reauth: auth_manager.py setup
    Reauth --> Verify: debug_skill.py

    Verify --> chk1
    state chk1 <<choice>>
    chk1 --> [*]: All PASS
    chk1 --> WipeAll: Still broken

    WipeAll --> NukeLib: cleanup_manager.py
    NukeLib --> ReaddNBs: notebook_manager.py add ...
    ReaddNBs --> Verify
```

---

## Smoke Test Output

| Symbol | Meaning | Action |
|--------|---------|--------|
| `[PASS]` | Healthy | None |
| `[WARN]` | Degraded | Fix when convenient |
| `[FAIL]` | Critical | Fix before using skill |
| `[SKIP]` | Not checked | Use --check-links |
