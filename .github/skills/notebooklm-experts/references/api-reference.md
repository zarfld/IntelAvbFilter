---
name: notebooklm
version: 1.0.0
description: Detailed API reference for all notebooklm scripts
---

# API Reference

## Overview

```mermaid
stateDiagram-v2
    direction LR
    [*] --> AuthManager: First time setup
    AuthManager --> NotebookMgr: Authenticated
    NotebookMgr --> AskQuestion: Notebooks ready
    AskQuestion --> CleanupMgr: Maintenance
    CleanupMgr --> [*]
```

---

## Authentication Manager (`auth_manager.py`)

```mermaid
stateDiagram-v2
    [*] --> Unauthenticated

    Unauthenticated --> BrowserOpen: setup
    BrowserOpen --> WaitLogin: Chrome opens visible
    WaitLogin --> Authenticated: User completes Google login

    Authenticated --> chk1
    state chk1 <<choice>>
    chk1 --> Authenticated: status = valid
    chk1 --> SessionExpired: status = expired

    SessionExpired --> BrowserOpen: reauth

    Authenticated --> Cleared: clear
    Cleared --> Unauthenticated

    Authenticated --> [*]

    note right of BrowserOpen
        MUST be visible browser
        Never headless
    end note

    note left of Authenticated
        Saved to
        data/browser_state/
    end note
```

```bash
.\run.bat auth_manager.py setup    # First time / new account
.\run.bat auth_manager.py status   # Check session
.\run.bat auth_manager.py reauth   # Refresh expired session
.\run.bat auth_manager.py clear    # Remove all auth data
```

---

## Notebook Manager (`notebook_manager.py`)

```mermaid
stateDiagram-v2
    [*] --> EmptyLib

    EmptyLib --> HasNotebooks: add --url --name --description --topics

    state HasNotebooks {
        [*] --> Registered
        Registered --> Listed: list
        Registered --> SearchResult: search --query
        Registered --> ActiveNB: activate --id
        ActiveNB --> ReadyToQuery
    }

    HasNotebooks --> HasNotebooks: add more notebooks
    HasNotebooks --> chk1
    state chk1 <<choice>>
    chk1 --> EmptyLib: remove last notebook
    chk1 --> HasNotebooks: remove, others remain

    HasNotebooks --> [*]: stats
```

```bash
.\run.bat notebook_manager.py add --url URL --name NAME --description DESC --topics TAGS
.\run.bat notebook_manager.py list
.\run.bat notebook_manager.py search --query KEYWORD
.\run.bat notebook_manager.py activate --id ID
.\run.bat notebook_manager.py remove --id ID
.\run.bat notebook_manager.py stats
```

| Parameter | Required | Example |
|-----------|----------|---------|
| `--url` | Yes | `https://notebooklm.google.com/notebook/...` |
| `--name` | Yes | `"API Documentation"` |
| `--description` | Yes | `"Complete REST API docs"` |
| `--topics` | Yes | `"api,rest,docs"` |

---

## Question Interface (`ask_question.py`)

```mermaid
stateDiagram-v2
    [*] --> SelectNB

    state SelectNB <<choice>>
    SelectNB --> UseActive: no --notebook-id/url
    SelectNB --> UseById: --notebook-id ID
    SelectNB --> UseByUrl: --notebook-url URL

    UseActive --> SendQuery
    UseById --> SendQuery
    UseByUrl --> SendQuery

    SendQuery --> WaitResponse: Submit question
    WaitResponse --> GotAnswer

    GotAnswer --> chk1
    state chk1 <<choice>>
    chk1 --> FollowUp: Needs more detail
    chk1 --> Complete: Answer sufficient

    FollowUp --> SendQuery: Ask follow-up
    Complete --> [*]

    note right of SendQuery
        --show-browser for debug
    end note
```

```bash
.\run.bat ask_question.py --question "..."                          # Active notebook
.\run.bat ask_question.py --question "..." --notebook-id nb_abc123  # By ID
.\run.bat ask_question.py --question "..." --notebook-url URL       # By URL
.\run.bat ask_question.py --question "..." --show-browser           # Debug mode
```

| Parameter | Required | Description |
|-----------|----------|-------------|
| `--question` | Yes | Question to ask |
| `--notebook-id` | No* | Notebook ID from library |
| `--notebook-url` | No* | Direct notebook URL |
| `--show-browser` | No | Show browser window |

---

## Cleanup Manager (`cleanup_manager.py`)

```mermaid
stateDiagram-v2
    [*] --> Preview: cleanup_manager.py

    Preview --> chk1
    state chk1 <<choice>>
    chk1 --> FullClean: --confirm
    chk1 --> SafeClean: --preserve-library

    state FullClean {
        [*] --> RmCache
        RmCache --> RmTranscripts
        RmTranscripts --> RmAuth
        RmAuth --> RmLibrary
        RmLibrary --> [*]
    }

    state SafeClean {
        [*] --> CleanCache
        CleanCache --> CleanTranscripts
        CleanTranscripts --> CleanTemp
        CleanTemp --> [*]
    }

    FullClean --> [*]
    SafeClean --> [*]

    note right of SafeClean
        Keeps: notebooks + auth
        Removes: cache, temp, transcripts
    end note
```

```bash
.\run.bat cleanup_manager.py                    # Preview
.\run.bat cleanup_manager.py --confirm          # Full cleanup
.\run.bat cleanup_manager.py --preserve-library # Keep notebooks + auth
```

---

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | General error |
| 2 | Authentication required |
| 3 | Notebook not found |
| 4 | Network error |
| 5 | Rate limit exceeded |

---

## Data File Structure

### `<skill-dir>/data/library.json`

```json
{
  "notebooks": [
    {
      "id": "nb_abc123",
      "name": "API Documentation",
      "description": "Complete REST API docs for v2.0",
      "topics": ["api", "rest", "documentation"],
      "url": "https://notebooklm.google.com/notebook/...",
      "added_at": "2024-01-10T12:00:00Z",
      "last_accessed": "2024-01-15T08:30:00Z"
    }
  ],
  "active_notebook_id": "nb_abc123",
  "version": "1.0.0"
}
```

### `<skill-dir>/data/auth_info.json`

```json
{
  "authenticated": true,
  "email": "user@example.com",
  "session_expires": "2024-01-20T00:00:00Z",
  "last_verified": "2024-01-15T10:00:00Z"
}
```

---

## Module Classes

```mermaid
stateDiagram-v2
    state NotebookLibrary {
        [*] --> nb_add: add_notebook
        nb_add --> nb_list: list_notebooks
        nb_list --> nb_search: search_notebooks
        nb_search --> nb_get: get_notebook
        nb_get --> nb_activate: activate_notebook
        nb_activate --> nb_remove: remove_notebook
        nb_remove --> [*]
    }

    state AuthMgr {
        [*] --> is_auth: is_authenticated
        is_auth --> setup: setup_auth
        setup --> get_info: get_auth_info
        get_info --> validate: validate_auth
        validate --> clear: clear_auth
        clear --> [*]
    }
```
