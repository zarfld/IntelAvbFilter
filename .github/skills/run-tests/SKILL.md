````skill
---
name: run-tests
description: Use when about to execute any .exe test binary, run any test command, investigate test failures, or claim any test result in this repo. STOP before running build/tools/avb_test .exe files directly - only Run-Tests-Elevated.ps1 is the correct entry point.
---

# Running Tests in the IntelAvbFilter Repo

## HARD STOP — Read This Before Touching Any Test

**If you are about to type any of these, STOP:**
- `.\build\tools\avb_test\x64\Debug\<anything>.exe`
- `& ".\build\...\<test>.exe"`
- `cd build\...; .\test_something.exe`

**The correct invocations are:**

| Context | Correct command |
|---------|----------------|
| Local dev (interactive) | `.\tools\test\Run-Tests-Elevated.ps1 -TestName <test>.exe` |
| CI workflow (GitHub Actions) | `.\tools\test\Run-Tests-CI.ps1 -Configuration Debug -Suite Unit` |

Running the `.exe` directly bypasses elevation, device enumeration, logging, and transcript capture. The terminal output shows nothing useful. **You will get wrong or no results and not know it.**

---

## The Iron Law

```
NEVER claim a test passed or failed without reading the log file output.

NEVER run the .exe directly from the build output directory.
ALWAYS use the correct entry point for the context:
  - Local dev:  Run-Tests-Elevated.ps1
  - CI/Headless: Run-Tests-CI.ps1
```

---

## Script Architecture

```
tools/test/Run-Tests-Elevated.ps1   ← LOCAL DEV entry point — elevation via Start-Process -Verb RunAs -Wait
                                      Interactive UAC required; caller terminal shows NOTHING useful.
                                      Transcript captured to logs/ — you MUST read the log file.
                                      NOT suitable for headless CI.

tools/test/Run-Tests.ps1            ← CANONICAL RUNNER — requires Admin + driver + Intel hardware.
                                      Checks driver service, Intel NIC, device node \\.\IntelAvbFilter.
                                      Use for full integration/hardware testing from an elevated terminal.

tools/test/Run-Tests-CI.ps1         ← CI ENTRY POINT — GitHub Actions / headless CI only.
                                      Runner is already Administrator; NO hardware/driver expected.
                                      Hardware-independent tests only; no interactive prompts.
                                      Correct invocation from CI workflow:
                                        .\tools\test\Run-Tests-CI.ps1 -Configuration Debug -Suite Unit
                                        .\tools\test\Run-Tests-CI.ps1 -Configuration Debug -Suite Integration

tools/build/Build-Tests.ps1         ← BUILD (compile test executables)
build/tools/avb_test/x64/Debug/     ← TEST BINARIES (.exe files)
logs/                               ← LOG FILES (written by all three runners)
```

### Which runner to use?

| Context                              | Correct entry point           |
|--------------------------------------|-------------------------------|
| Local dev, interactive terminal      | `Run-Tests-Elevated.ps1`      |
| Local dev, already in elevated shell | `Run-Tests.ps1`               |
| GitHub Actions / headless CI         | `Run-Tests-CI.ps1`            |
| CI workflow YAML step                | `Run-Tests-CI.ps1`            |

`Run-Tests-Elevated.ps1` spawns a new elevated PowerShell via `Start-Process
powershell -Verb RunAs -Wait`. The test output is captured in a transcript log
under `logs/`. The caller's stdout only shows `Output will be logged to:` and
`Log file created:`.  **The caller terminal shows nothing about test results.**
You MUST read the log file.

---

## Step-by-Step: Run a Specific Test

### Step 1 — Build if needed

```powershell
# Only needed when source has changed since last build.
# Use the test name as it appears in Build-Tests.ps1 -TestName listing:
powershell -NoProfile -ExecutionPolicy Bypass -File "tools/build/Build-Tests.ps1" -TestName "test_ptp_getset"
```

Known test names (pass to `-TestName`):
`test_ptp_getset`, `test_ptp_freq`, `test_hw_ts_ctrl`, `test_rx_timestamp`,
`test_statistics_counters`, `test_timestamp_latency`, `test_qav_cbs`,
`test_ioctl_target_time`, `test_ioctl_phc_query`, `test_ioctl_offset`, …

### Step 2 — Run via the elevated wrapper

```powershell
# From the repo root. Note: -TestName, NOT -TestExecutable
.\tools\test\Run-Tests-Elevated.ps1 -TestName test_ptp_getset.exe
```

Parameter notes:
- `-TestName <name.exe>` — Run one specific test; `.exe` suffix is optional
- `-Full`                — Run the full test suite (all registered tests)
- `-Configuration Release` — Default is `Debug`

The wrapper prints:
```
Output will be logged to: C:\...\logs\test_ptp_getset.exe_20260306_092040.log
Log file created: C:\...\logs\test_ptp_getset.exe_20260306_092040.log
```

### Step 3 — Read the log file (MANDATORY)

```powershell
# Get the most recent log for this test
$log = Get-ChildItem logs -Filter "test_ptp_getset*" |
       Sort-Object LastWriteTime -Descending |
       Select-Object -First 1

Get-Content $log.FullName
```

Or read a specific file from the path printed in Step 2.

### Step 4 — Extract pass/fail counts

Look for the `Test Summary` block near the end of the log:

```
====================================================================
 Test Summary
====================================================================
 Passed:  N tests
 Failed:  M tests
 Skipped: K tests
====================================================================
```

And individual test lines:
```
  [PASS] UT-PTP-GETSET-001: ...
  [FAIL] UT-PTP-GETSET-002: ... (reason)
  [SKIP] UT-PTP-GETSET-009: ...
```

**Only after reading these lines can you claim anything about test results.**

---

## Step-by-Step: Run the Full Test Suite

```powershell
.\tools\test\Run-Tests-Elevated.ps1 -Full

# Then read the log:
$log = Get-ChildItem logs -Filter "full_test_suite*" |
       Sort-Object LastWriteTime -Descending |
       Select-Object -First 1
Get-Content $log.FullName
```

---

## Common Mistakes (Never Do)

| Wrong | Right |
|-------|-------|
| `cd build\tools\avb_test\x64\Debug ; .\test_ptp_getset.exe` | `.\tools\test\Run-Tests-Elevated.ps1 -TestName test_ptp_getset.exe` |
| Reading only the terminal output (it shows nothing useful) | Read the log file from `logs/` |
| Claiming pass/fail from build success alone | Run the test, read the log |
| Using `-TestExecutable` on `Run-Tests-Elevated.ps1` | Use `-TestName` on the elevated wrapper |
| Saying "tests should pass now" | Run → read log → state actual counts |

---

## Quick Reference

```powershell
# Build one test
powershell -File tools/build/Build-Tests.ps1 -TestName test_ptp_getset

# Run one test and capture log path
.\tools\test\Run-Tests-Elevated.ps1 -TestName test_ptp_getset.exe

# Read the log (most recent)
Get-ChildItem logs -Filter "test_ptp_getset*" | Sort-Object LastWriteTime -Desc | Select-Object -First 1 | Get-Content

# Full suite
.\tools\test\Run-Tests-Elevated.ps1 -Full
Get-ChildItem logs -Filter "full_test_suite*" | Sort-Object LastWriteTime -Desc | Select-Object -First 1 | Get-Content

# Check if terminal is already elevated (Run-Tests.ps1 direct option)  
([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole('Administrator')
# If $true → can call: powershell -File tools/test/Run-Tests.ps1 -TestExecutable test_ptp_getset.exe
```

---

## Reading a Summarized Log

When the tool returns a summarized/truncated log, the critical lines are:
1. Each `[PASS]`, `[FAIL]`, `[SKIP]` line — individual test verdict
2. The `Test Summary` block — aggregate counts
3. Any `[ERROR]` or exception lines before the summary

If these are omitted by summarization, re-read the raw file with targeted grep:

```powershell
$log = Get-ChildItem logs -Filter "test_ptp_getset*" | Sort-Object LastWriteTime -Desc | Select-Object -First 1
Select-String -Path $log.FullName -Pattern "\[PASS\]|\[FAIL\]|\[SKIP\]|Test Summary|Passed:|Failed:|Skipped:"
```

---

## Driver Install Before Tests

If testing driver-side changes, install the new driver first:

```powershell
# Install / reinstall (requires elevation — wrapper handles it)
.\tools\setup\Install-Driver-Elevated.ps1 -Configuration Debug -Action Reinstall

# Verify running
Get-Service IntelAvbFilter | Select-Object Name, Status

# Verify it is the new binary
Get-Item "C:\Windows\System32\drivers\IntelAvbFilter.sys" | Select-Object LastWriteTime, Length
```

Then run the tests.

---

## Failure Triage

After reading the log, for each `[FAIL]` line:
1. Note the exact failure message (diff values, "rejected", "accepted", etc.)
2. Map to the test case (`UT-PTP-*`, `UT-FREQ-*`, `UT-HW-TS-*`, etc.)
3. Identify whether it is a **driver bug**, **test tolerance bug**, or **env issue**
4. State actual counts: "N passed, M failed: [list the failing test IDs]"

Never say "most tests pass" — cite the numbers.
````
