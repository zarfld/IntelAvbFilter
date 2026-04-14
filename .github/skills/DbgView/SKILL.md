---
name: dbgview-capture
description: "Use when running manual tests on the IntelAvbFilter driver and kernel debug output (DbgPrint) is missing, silent, or unverified. Covers launch, kernel-capture state, Debug Print Filter registry, and log file setup."
---

# DbgView Capture for Driver Testing

## Overview

DebugView (Sysinternals) captures `DbgPrint` / `KdPrint` output from the IntelAvbFilter kernel driver in real time. Without it running elevated with **Capture Kernel** on (and the Windows Debug Print Filter mask set), kernel messages are silently discarded.

## When to Use

- About to run a manual test that depends on seeing driver log output
- Driver is loaded but no messages appear in DebugView
- Setting up a new dev/test machine for the first time
- Test script needs an automatic log file of kernel output

**NOT for:** WPP/ETW tracing (separate workflow); CI pipelines (use structured log collection instead).

## Quick-Start Checklist

Before every manual test session:

- [ ] DebugView is running as **Administrator**
- [ ] **Capture → Capture Kernel** is checked (ticked) in the menu
- [ ] **Capture → Capture Win32** is checked (optional but useful)
- [ ] No messages? → Check Debug Print Filter registry (see below)
- [ ] Want a log file? → Use `-LogFile` with `Start-DbgViewCapture.ps1`

## Launching DebugView

**Binary location** (local only, not in git):
```
.github\skills\DbgView\DebugView\Dbgview.exe     (32-bit, also works for 64-bit kernel)
.github\skills\DbgView\DebugView\dbgview64.exe    (native 64-bit UI)
```

**Manual launch** (from an elevated PowerShell prompt):
```powershell
Start-Process ".github\skills\DbgView\DebugView\Dbgview.exe" -Verb RunAs
```

**Scripted launch with log file** (see `Start-DbgViewCapture.ps1`):
```powershell
# Auto-named log (logs\dbgview_<timestamp>.log):
$proc = .\.github\skills\DbgView\Start-DbgViewCapture.ps1

# Named log stem — produces logs\avb_i210_test_<timestamp>.log:
$proc = .\.github\skills\DbgView\Start-DbgViewCapture.ps1 -LogName "avb_i210_test"

# Explicit full path:
$proc = .\.github\skills\DbgView\Start-DbgViewCapture.ps1 -LogFile "C:\Logs\mytest.log"
```

Always assign the return value — avoids PowerShell printing the process object table to the console.

**Stopping DebugView** (see `Stop-DbgViewCapture.ps1`):
```powershell
# Stop the specific instance (preferred in automated tests):
.\.github\skills\DbgView\Stop-DbgViewCapture.ps1 -ProcessId $proc.Id

# Stop all running DebugView processes (cleanup / no PID available):
.\.github\skills\DbgView\Stop-DbgViewCapture.ps1
```

## Command-Line Options Reference

| Option | Effect |
|--------|--------|
| `/t` | Start minimised to system tray |
| `/f` | Skip filter-save confirmation on exit |
| `/l <path>` | Start logging to `<path>` immediately on launch |
| `/a` | Append to existing log file (default: overwrite) |
| `/m <MB> /w` | Size-limited log with wrap-around |
| `/n` | Daily log rollover |

> Run `Dbgview.exe /?` to confirm options for the installed build.

## Persisted Capture State

DebugView saves its capture settings (including **Capture Kernel**) in the local user profile on exit. On the next launch those settings are restored automatically.

**First-time machine setup:**
1. Launch DebugView elevated.
2. Enable **Capture → Capture Kernel** manually.
3. Exit normally.
4. All subsequent scripted launches inherit kernel-capture state.

You only need to do this once per user account / machine.

## Debug Print Filter Registry

Even when DebugView is running with Capture Kernel on, messages can be silently discarded by Windows if the kernel-mode debug print filter mask is not set.

**Check current state:**
```powershell
Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Debug Print Filter" -ErrorAction SilentlyContinue
```

**Set mask for generic `DbgPrint` (DEFAULT component):**
```powershell
# Requires Administrator. Change takes effect at NEXT BOOT.
reg add "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Debug Print Filter" `
    /v DEFAULT /t REG_DWORD /d 0xFFFFFFFF /f
```

**Set mask for NDIS/network drivers (`IHVNETWORK`):**
```powershell
reg add "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Debug Print Filter" `
    /v IHVNETWORK /t REG_DWORD /d 0xFFFFFFFF /f
```

> ⚠️ Registry changes require a **reboot** before they take effect. Set once during machine setup.

The IntelAvbFilter driver uses plain `DbgPrint` → `DEFAULT` component. If you see nothing, set DEFAULT and reboot.

## Verify DebugView Is Running

```powershell
$dbg = Get-Process -Name "Dbgview*" -ErrorAction SilentlyContinue
if ($dbg) { Write-Host "DbgView running PID=$($dbg.Id)" }
else       { Write-Warning "DbgView is NOT running — driver output will be lost" }
```

`tools\development\Check-System.ps1` runs this check automatically as part of the broader system readiness check.

## Common Mistakes

| Mistake | Fix |
|---------|-----|
| DebugView running but **Capture Kernel** is OFF | Enable via **Capture → Capture Kernel** (or pre-seed the saved state) |
| Launched DebugView without elevation (not as Admin) | Close it; re-launch elevated (`Start-Process … -Verb RunAs`) |
| Debug Print Filter not set on a new machine | Set DEFAULT mask and reboot (one-time setup) |
| Filter mask set but machine not rebooted | Reboot; mask is read at boot only |
| Using 32-bit `Dbgview.exe` on a 64-bit-only kernel config | Try `dbgview64.exe` instead |
| Log file path not created before launch | `Start-DbgViewCapture.ps1` creates the directory automatically |
| Forgetting to stop DebugView → log stays open | Call `Stop-Process` or use the `-StopAfterTest` pattern in the helper script |
