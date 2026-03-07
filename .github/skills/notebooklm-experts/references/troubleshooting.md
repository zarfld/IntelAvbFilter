# NotebookLM Skill Troubleshooting

## Error Decision Tree

```mermaid
stateDiagram-v2
    [*] --> ErrorOccurs

    ErrorOccurs --> Identify
    state Identify <<choice>>
    Identify --> ModuleErr: ModuleNotFoundError
    Identify --> AuthErr: Auth failed
    Identify --> BrowserErr: Browser crash
    Identify --> RateErr: Rate limit
    Identify --> NBNotFound: Notebook not found
    Identify --> ScriptErr: Script not working

    ModuleErr --> UseRunBat: Use .\run.bat wrapper
    UseRunBat --> [*]: Resolved

    AuthErr --> AuthFix
    BrowserErr --> BrowserFix
    RateErr --> RateFix
    NBNotFound --> NBFix
    ScriptErr --> UseRunBat

    AuthFix --> [*]: See Auth Flow below
    BrowserFix --> [*]: See Browser Flow below
    RateFix --> [*]: See Rate Flow below
    NBFix --> [*]: See Notebook Flow below
```

---

## Auth Errors

```mermaid
stateDiagram-v2
    [*] --> AuthError

    AuthError --> chk1
    state chk1 <<choice>>
    chk1 --> NotAuth: Not authenticated error
    chk1 --> ExpiresOften: Auth expires frequently
    chk1 --> Blocked: Google blocks login

    NotAuth --> CheckStatus: auth_manager.py status
    CheckStatus --> chk2
    state chk2 <<choice>>
    chk2 --> DoSetup: Never authenticated
    chk2 --> DoReauth: Session expired

    DoSetup --> [*]: setup (VISIBLE browser!)
    DoReauth --> [*]: reauth

    ExpiresOften --> CleanOld: cleanup --preserve-library
    CleanOld --> FreshSetup: auth_manager.py setup --timeout 15
    FreshSetup --> [*]

    Blocked --> DedicatedAcct: Use dedicated Google account
    DedicatedAcct --> VisibleSetup: auth_manager.py setup
    VisibleSetup --> [*]

    note right of DoSetup
        Browser MUST be visible
        User logs in manually
    end note
```

```bash
.\run.bat auth_manager.py status
.\run.bat auth_manager.py setup
.\run.bat auth_manager.py reauth
```

---

## Browser Errors

```mermaid
stateDiagram-v2
    [*] --> BrowserError

    BrowserError --> chk1
    state chk1 <<choice>>
    chk1 --> CrashHang: Crash or hang
    chk1 --> NotFound: Browser not found

    CrashHang --> KillProcs: Kill chrome processes
    KillProcs --> CleanState: cleanup --confirm --preserve-library
    CleanState --> DoReauth: auth_manager.py reauth
    DoReauth --> [*]: Resolved

    NotFound --> AutoInstall: run.bat auto-installs
    AutoInstall --> chk2
    state chk2 <<choice>>
    chk2 --> [*]: Resolved
    chk2 --> ManualInstall: patchright install chromium
    ManualInstall --> [*]: Resolved
```

---

## Rate Limiting

```mermaid
stateDiagram-v2
    [*] --> RateLimited: 50 queries/day exceeded

    RateLimited --> chk1
    state chk1 <<choice>>
    chk1 --> WaitReset: Option 1: Wait
    chk1 --> SwitchAcct: Option 2: Switch account

    WaitReset --> [*]: Resets at midnight PST

    SwitchAcct --> ClearAuth: auth_manager.py clear
    ClearAuth --> NewSetup: auth_manager.py setup
    NewSetup --> [*]: New account active
```

---

## Notebook Access Errors

```mermaid
stateDiagram-v2
    [*] --> NBError

    NBError --> chk1
    state chk1 <<choice>>
    chk1 --> NotFoundNB: Notebook not found
    chk1 --> AccessDenied: Access denied
    chk1 --> WrongNB: Wrong notebook active

    NotFoundNB --> ListNBs: notebook_manager.py list
    ListNBs --> chk2
    state chk2 <<choice>>
    chk2 --> ActivateIt: Found in list
    chk2 --> AddNew: Not in list
    ActivateIt --> [*]: activate --id ID
    AddNew --> [*]: add --url URL --name NAME

    AccessDenied --> CheckSharing: Still shared publicly?
    CheckSharing --> chk3
    state chk3 <<choice>>
    chk3 --> ReaddNB: URL changed
    chk3 --> FixAcct: Wrong Google account
    ReaddNB --> [*]
    FixAcct --> [*]: setup with correct account

    WrongNB --> ListActive: notebook_manager.py list
    ListActive --> ActivateCorrect: activate --id correct-id
    ActivateCorrect --> [*]
```

---

## Recovery Flow

```mermaid
stateDiagram-v2
    [*] --> Diagnose: Multiple things broken

    Diagnose --> BackupLib: Backup library.json
    BackupLib --> CleanAll: cleanup_manager.py --confirm --force
    CleanAll --> RemoveVenv: Remove .venv
    RemoveVenv --> Reinstall: run.bat auth_manager.py setup
    Reinstall --> RestoreLib: Restore library backup
    RestoreLib --> Verify: Test all operations

    Verify --> chk1
    state chk1 <<choice>>
    chk1 --> [*]: All working
    chk1 --> Diagnose: Still broken
```
```

### Partial recovery (keep data)
```bash
# Keep auth and library, fix execution
cd <skill-dir>
rm -rf .venv

# run.py will recreate venv automatically
.\\run.bat auth_manager.py status
```

## Error Messages Reference

### Authentication Errors
| Error | Cause | Solution |
|-------|-------|----------|
| Not authenticated | No valid auth | `run.py auth_manager.py setup` |
| Authentication expired | Session old | `run.py auth_manager.py reauth` |
| Invalid credentials | Wrong account | Check Google account |
| 2FA required | Security challenge | Complete in visible browser |

### Browser Errors
| Error | Cause | Solution |
|-------|-------|----------|
| Browser not found | Chromium missing | Use run.py (auto-installs) |
| Connection refused | Browser crashed | Kill processes, restart |
| Timeout waiting | Page slow | Increase timeout |
| Context closed | Browser terminated | Check logs for crashes |

### Notebook Errors
| Error | Cause | Solution |
|-------|-------|----------|
| Notebook not found | Invalid ID | `run.py notebook_manager.py list` |
| Access denied | Not shared | Re-share in NotebookLM |
| Invalid URL | Wrong format | Use full NotebookLM URL |
| No active notebook | None selected | `run.py notebook_manager.py activate` |

## Prevention Tips

1. **Always use run.py** - Prevents 90% of issues
2. **Regular maintenance** - Clear browser state weekly
3. **Monitor queries** - Track daily count to avoid limits
4. **Backup library** - Export notebook list regularly
5. **Use dedicated account** - Separate Google account for automation

## Getting Help

### Diagnostic information to collect
```bash
# System info
python --version
cd <skill-dir>
ls -la

# Skill status
.\\run.bat auth_manager.py status
.\\run.bat notebook_manager.py list | head -5

# Check data directory
ls -la <skill-dir>/data/
```

### Common questions

**Q: Why doesn't this work without network access?**
A: This skill requires a local environment with browser access. Use GitHub Copilot in VS Code with the skill installed locally.

**Q: Can I use multiple Google accounts?**
A: Yes, use `run.py auth_manager.py reauth` to switch.

**Q: How to increase rate limit?**
A: Use multiple accounts or upgrade to Google Workspace.

**Q: Is this safe for my Google account?**
A: Use dedicated account for automation. Only accesses NotebookLM.
