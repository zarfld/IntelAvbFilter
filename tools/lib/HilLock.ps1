# HilLock.ps1 - machine-wide serialization of physical HIL operations (driver installation + hardware tests).
# Dot-source; no side effects.
#
# One owner at a time across configurations, workflow runs and operator runs (LocalSuites.ps1): a lock FILE created
# with CreateNew (atomic on NTFS) holding the owner identity as JSON.  A CI job spans several step processes, so the
# file (not a handle) is the lock; it is released by an always() step / a finally block.
# Default path: %ProgramData%\IntelAvbFilter-hil.lock (an existing directory; no directory is created).
#
# Stale owners (never silently ignored; returned as evidence):
#   local  : its PID is no longer alive on this host                        -> stale
#   ci     : seen by a CI job: a different job's lock was left behind by a job that died (ONE runner instance on
#            this machine executes one job at a time - precondition) -> stale.  Seen by an operator: stale only when
#            no runner job is executing (no Runner.Worker process); the PID of a CI owner is a finished step process.
# A live local owner is waited for up to -WaitSeconds, then the caller reports HIL_NOT_EXECUTED (busy).
# Taking over a stale lock is recorded (PreviousOwner) so the caller can attribute an interrupted deployment and
# collect its crash evidence; the machine state is then UNKNOWN and must pass the deployment pre-state checks.

function Get-AvbHilLockPath { return (Join-Path $env:ProgramData 'IntelAvbFilter-hil.lock') }

function New-AvbHilLockOwner {
    param([string]$Kind, [string]$Label)
    return [ordered]@{ kind = $Kind; label = $Label; host = $env:COMPUTERNAME; pid = $PID
                       runId = $env:GITHUB_RUN_ID; runAttempt = $env:GITHUB_RUN_ATTEMPT; job = $env:GITHUB_JOB
                       acquiredUtc = (Get-Date).ToUniversalTime().ToString('o') }
}

function Read-AvbHilLock {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    try { return (Get-Content -LiteralPath $Path -Raw -ErrorAction Stop | ConvertFrom-Json) } catch { return [pscustomobject]@{ kind = 'unreadable'; label = "unreadable lock file: $($_.Exception.Message)" } }
}

# $IsProcessAlive: scriptblock(pid) -> bool (injectable for tests).
function Test-AvbHilLockStale {
    param($Owner, $Me, [scriptblock]$IsProcessAlive = { param([int]$Id) [bool](Get-Process -Id $Id -ErrorAction SilentlyContinue) },
          [scriptblock]$IsCiJobActive = { [bool](Get-Process -Name 'Runner.Worker' -ErrorAction SilentlyContinue) })
    if ($null -eq $Owner) { return @{ Stale = $true; Why = 'no owner' } }
    if ($Owner.kind -eq 'unreadable') { return @{ Stale = $false; Why = $Owner.label } }   # never guess: operator decides
    if ($Owner.host -and $Owner.host -ne $Me.host) { return @{ Stale = $false; Why = "owned by another host $($Owner.host)" } }
    if ($Owner.kind -eq 'ci' -and $Me.kind -eq 'ci') {
        if ([string]$Owner.runId -eq [string]$Me.runId -and [string]$Owner.runAttempt -eq [string]$Me.runAttempt -and [string]$Owner.job -eq [string]$Me.job) {
            return @{ Stale = $true; Why = 're-entry by the same CI job' }
        }
        return @{ Stale = $true; Why = "CI job run $($Owner.runId) attempt $($Owner.runAttempt) job $($Owner.job) did not release the lock (one runner executes one job at a time: that job died)" }
    }
    if ($Owner.kind -eq 'ci') {
        if (& $IsCiJobActive) { return @{ Stale = $false; Why = "CI job run $($Owner.runId) job $($Owner.job) is executing on this runner" } }
        return @{ Stale = $true; Why = "CI job run $($Owner.runId) attempt $($Owner.runAttempt) job $($Owner.job) did not release the lock and no runner job is executing" }
    }
    if ($Owner.pid -and -not (& $IsProcessAlive ([int]$Owner.pid))) {
        return @{ Stale = $true; Why = "owner PID $($Owner.pid) ($($Owner.kind) '$($Owner.label)') is no longer running" }
    }
    return @{ Stale = $false; Why = "held by live $($Owner.kind) owner '$($Owner.label)' (PID $($Owner.pid)) since $($Owner.acquiredUtc)" }
}

# @{ Ok; Owner; PreviousOwner (stale lock taken over, or $null); Why; Waited }
function Enter-AvbHilLock {
    param([string]$Kind, [string]$Label, [string]$Path = (Get-AvbHilLockPath), [int]$WaitSeconds = 0,
          [scriptblock]$IsProcessAlive = { param([int]$Id) [bool](Get-Process -Id $Id -ErrorAction SilentlyContinue) },
          [scriptblock]$IsCiJobActive = { [bool](Get-Process -Name 'Runner.Worker' -ErrorAction SilentlyContinue) },
          [scriptblock]$Sleep = { param([int]$s) Start-Sleep -Seconds $s })
    $me = New-AvbHilLockOwner -Kind $Kind -Label $Label
    $json = ($me | ConvertTo-Json -Compress)
    $waited = 0; $previous = $null
    while ($true) {
        try {
            $fs = [System.IO.File]::Open($Path, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
            try { $b = [System.Text.Encoding]::UTF8.GetBytes($json); $fs.Write($b, 0, $b.Length) } finally { $fs.Dispose() }
            return @{ Ok = $true; Owner = $me; PreviousOwner = $previous; Why = ''; Waited = $waited }
        } catch [System.IO.IOException] {
            $owner = Read-AvbHilLock -Path $Path
            $st = Test-AvbHilLockStale -Owner $owner -Me $me -IsProcessAlive $IsProcessAlive -IsCiJobActive $IsCiJobActive
            if ($st.Stale) {
                $previous = [ordered]@{ owner = $owner; why = $st.Why; detectedUtc = (Get-Date).ToUniversalTime().ToString('o') }
                Remove-Item -LiteralPath $Path -Force -ErrorAction Stop
                continue
            }
            if ($waited -ge $WaitSeconds) { return @{ Ok = $false; Owner = $owner; PreviousOwner = $previous; Why = $st.Why; Waited = $waited } }
            & $Sleep 10; $waited += 10
        }
    }
}

# Releases only a lock this owner holds.  @{ Ok; Why }
# Is the lock file currently held by $Owner?  @{ Ok; Why }.
function Test-AvbHilLockHeld {
    param($Owner, [string]$Path = (Get-AvbHilLockPath))
    if ($null -eq $Owner) { return @{ Ok = $false; Why = 'no lock owner given (exclusive HIL control required)' } }
    $cur = Read-AvbHilLock -Path $Path
    if ($null -eq $cur) { return @{ Ok = $false; Why = 'lock already absent' } }
    $same = ($cur.kind -eq $Owner.kind -and [string]$cur.pid -eq [string]$Owner.pid -and [string]$cur.runId -eq [string]$Owner.runId -and
             [string]$cur.runAttempt -eq [string]$Owner.runAttempt -and [string]$cur.job -eq [string]$Owner.job -and $cur.acquiredUtc -eq $Owner.acquiredUtc)
    # A CI job releases from another step process: same run/attempt/job identifies the owner, the PID differs.
    if (-not $same -and $Owner.kind -eq 'ci') {
        $same = ($cur.kind -eq 'ci' -and [string]$cur.runId -eq [string]$Owner.runId -and [string]$cur.runAttempt -eq [string]$Owner.runAttempt -and [string]$cur.job -eq [string]$Owner.job)
    }
    if (-not $same) { return @{ Ok = $false; Why = "lock is held by another owner ($($cur.kind) '$($cur.label)')" } }
    return @{ Ok = $true; Why = '' }
}

function Exit-AvbHilLock {
    param($Owner, [string]$Path = (Get-AvbHilLockPath))
    $held = Test-AvbHilLockHeld -Owner $Owner -Path $Path
    if (-not $held.Ok) { return $held }
    Remove-Item -LiteralPath $Path -Force
    return @{ Ok = $true; Why = '' }
}

# Process ancestry from the OS (parent chain, nearest first); bounded, cycle-safe.
function Get-AvbProcessAncestors {
    param([int]$ProcessId = $PID)
    $out = @(); $seen = @{}; $id = $ProcessId
    for ($i = 0; $i -lt 32; $i++) {
        $p = Get-CimInstance Win32_Process -Filter "ProcessId=$id" -ErrorAction SilentlyContinue
        if ($null -eq $p -or $p.ParentProcessId -eq 0 -or $seen.ContainsKey([int]$p.ParentProcessId)) { break }
        $id = [int]$p.ParentProcessId; $seen[$id] = $true; $out += $id
    }
    return ,$out
}

# Does the CURRENT execution context own the machine lock?  @{ Owned; How = self|ancestor|ci-job; Owner; Why }.
# Never inferred from the lock's existence: CI = this process's own run/attempt/job (GitHub Actions environment);
# local = the owner's PID is this process (in-process call, e.g. LocalSuites -> Run-Tests-CI) or one of its OS
# ancestors (child launched by the owner, e.g. Run-Tests-Elevated).  $GitHub: @{ runId; runAttempt; job } or $null.
function Resolve-AvbHilLockContext {
    param([string]$Path = (Get-AvbHilLockPath), [int]$ProcessId = $PID, [int[]]$Ancestors = $null, $GitHub = $null)
    $owner = Read-AvbHilLock -Path $Path
    if ($null -eq $owner) { return @{ Owned = $false; How = ''; Owner = $null; Why = 'machine lock absent' } }
    if ($owner.kind -eq 'unreadable' -or -not $owner.kind) { return @{ Owned = $false; How = ''; Owner = $owner; Why = "lock ownership cannot be verified ($($owner.label))" } }
    if ($owner.host -and $owner.host -ne $env:COMPUTERNAME) { return @{ Owned = $false; How = ''; Owner = $owner; Why = "lock written by host '$($owner.host)'" } }
    if ($owner.kind -eq 'ci') {
        if ($null -ne $GitHub -and [string]$owner.runId -eq [string]$GitHub.runId -and [string]$owner.runAttempt -eq [string]$GitHub.runAttempt -and [string]$owner.job -eq [string]$GitHub.job) {
            return @{ Owned = $true; How = 'ci-job'; Owner = $owner; Why = '' } }
        return @{ Owned = $false; How = ''; Owner = $owner; Why = "lock held by CI job $($owner.runId)/$($owner.runAttempt)/$($owner.job), not by this execution" }
    }
    if ($null -ne $GitHub) { return @{ Owned = $false; How = ''; Owner = $owner; Why = "lock held by a $($owner.kind) owner '$($owner.label)', not by this CI job" } }
    if ([string]$owner.pid -eq [string]$ProcessId) { return @{ Owned = $true; How = 'self'; Owner = $owner; Why = '' } }
    if ($null -eq $Ancestors) { $Ancestors = Get-AvbProcessAncestors -ProcessId $ProcessId }
    if (@($Ancestors) -contains [int]$owner.pid) { return @{ Owned = $true; How = 'ancestor'; Owner = $owner; Why = '' } }
    return @{ Owned = $false; How = ''; Owner = $owner; Why = "lock held by $($owner.kind) '$($owner.label)' (pid $($owner.pid)), which is not this process or one of its ancestors" }
}

# ---------------------------------------------------------------------------------------------------------
# Machine-wide HIL safety record (Q 2026-10-21): %ProgramData%\IntelAvbFilter-hil-safety.json, schema avb-hil-safety/1.
# Shared by every process, configuration and run on this machine (Debug, Release, LocalSuites, CI).
#   CLEAR       explicitly initialized or cleared, no unrecovered event       -> Init/Mutating may run
#   HALTED      at least one safety event with recovery PENDING / ATTEMPTED    -> Init/Mutating BLOCKED, ReadOnly runs
#   UNAVAILABLE missing / unreadable / corrupt / wrong schema / other host /
#               internally inconsistent                                       -> fail closed (as HALTED)
# Events are appended, never removed.  Nothing clears a halt implicitly (reboot, reinstall, new run, new baseline);
# only Clear-AvbHilSafetyHalt (verified recovery) or Invoke-AvbHilSafetyRebaseline (operator-approved) do, and only on a
# HALTED record while holding the machine lock.  Scope is GLOBAL until physical adapter identity and isolation are
# hardware-verified.  An accidental-damage safeguard, not a security boundary (the file can be edited).
# ---------------------------------------------------------------------------------------------------------
function Get-AvbHilSafetyPath { return (Join-Path $env:ProgramData 'IntelAvbFilter-hil-safety.json') }

function Write-AvbHilSafetyFile([string]$Path, $Record) {
    $tmp = "$Path.tmp-$PID"
    ($Record | ConvertTo-Json -Depth 10) | Set-Content -LiteralPath $tmp -Encoding UTF8
    Move-Item -LiteralPath $tmp -Destination $Path -Force
}

# @{ State = CLEAR|HALTED|UNAVAILABLE; Why; Record }
function Read-AvbHilSafetyState {
    param([string]$Path = (Get-AvbHilSafetyPath))
    $un = { param($w) return @{ State = 'UNAVAILABLE'; Why = $w; Record = $null } }
    if (-not (Test-Path -LiteralPath $Path)) { return (& $un "safety record missing at $Path (initialize it explicitly)") }
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return (& $un "safety record path is not a file: $Path") }
    try { $r = Get-Content -LiteralPath $Path -Raw -ErrorAction Stop | ConvertFrom-Json } catch { return (& $un "safety record unreadable or corrupt: $($_.Exception.Message)") }
    if ($null -eq $r -or $r.schema -ne 'avb-hil-safety/1') { return (& $un "safety record has an unknown schema") }
    if ($r.host -ne $env:COMPUTERNAME) { return (& $un "safety record belongs to host '$($r.host)', not '$($env:COMPUTERNAME)' (stale or copied)") }
    $open = @(@($r.events) | Where-Object { $null -ne $_ -and $_.recovery.status -in @('PENDING', 'ATTEMPTED') })
    if ($r.status -eq 'HALTED') {
        if ($open.Count -eq 0) { return (& $un 'safety record inconsistent: HALTED without an unrecovered event') }
        return @{ State = 'HALTED'; Why = ("{0} unrecovered safety event(s), first: {1} {2} at {3}" -f $open.Count, $open[0].verdict, $open[0].trigger.executable, $open[0].utc); Record = $r }
    }
    if ($r.status -eq 'CLEAR') {
        if ($open.Count -gt 0) { return (& $un 'safety record inconsistent: CLEAR with unrecovered events') }
        return @{ State = 'CLEAR'; Why = ''; Record = $r }
    }
    return (& $un "safety record has an unknown status '$($r.status)'")
}

function Initialize-AvbHilSafetyRecord {
    param([string]$Path = (Get-AvbHilSafetyPath), [string]$Operator, [string]$Reason)
    if (Test-Path -LiteralPath $Path) { return @{ Ok = $false; Why = "a safety record already exists at $Path (never overwritten; inspect it)" } }
    if (-not $Operator -or -not $Reason) { return @{ Ok = $false; Why = 'operator and reason are required' } }
    $now = (Get-Date).ToUniversalTime().ToString('o')
    Write-AvbHilSafetyFile $Path ([ordered]@{ schema = 'avb-hil-safety/1'; host = $env:COMPUTERNAME; status = 'CLEAR'; events = @()
        history = @([ordered]@{ utc = $now; operation = 'INITIALIZE'; operator = $Operator; reason = $Reason }) })
    return @{ Ok = $true; Why = '' }
}

# $SafetyEvent (-Event): verdict, scope, hardware, trigger, initialState, lastKnownState, evidence (eventId/utc/host/recovery are added).
function Add-AvbHilSafetyEvent {
    param([string]$Path = (Get-AvbHilSafetyPath), [Alias('Event')]$SafetyEvent)
    $now = (Get-Date).ToUniversalTime().ToString('o')
    # StrictMode-safe: callers may omit optional fields.
    $f = { param($n) if ($SafetyEvent -is [System.Collections.IDictionary]) { if ($SafetyEvent.Contains($n)) { $SafetyEvent[$n] } } elseif ($null -ne $SafetyEvent -and $SafetyEvent.PSObject.Properties[$n]) { $SafetyEvent.$n } }
    $scope = & $f 'scope'
    $e = [ordered]@{ eventId = [guid]::NewGuid().ToString(); utc = $now; host = $env:COMPUTERNAME
                     scope = $(if ($scope) { $scope } else { 'GLOBAL' }); hardware = (& $f 'hardware'); trigger = (& $f 'trigger')
                     verdict = (& $f 'verdict'); initialState = (& $f 'initialState'); lastKnownState = (& $f 'lastKnownState')
                     evidence = @(& $f 'evidence'); recovery = [ordered]@{ status = 'PENDING' } }
    $st = Read-AvbHilSafetyState -Path $Path
    if ($st.State -eq 'UNAVAILABLE' -and (Test-Path -LiteralPath $Path)) {
        # Never overwrite an unreadable / foreign record (it is evidence): keep the event beside it.
        $side = "$Path.event-$((Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssfff')).json"
        ($e | ConvertTo-Json -Depth 10) | Set-Content -LiteralPath $side -Encoding UTF8
        return @{ Ok = $false; Why = "safety record unavailable ($($st.Why)); event written to $side"; EventId = $e.eventId }
    }
    $rec = if ($st.Record) { $st.Record } else {
        [pscustomobject]@{ schema = 'avb-hil-safety/1'; host = $env:COMPUTERNAME; status = 'HALTED'; events = @()
                           history = @([ordered]@{ utc = $now; operation = 'CREATED_BY_EVENT'; operator = ''; reason = 'no record existed when a safety event occurred' }) } }
    $rec.events = @(@($rec.events) + @($e)); $rec.status = 'HALTED'
    Write-AvbHilSafetyFile $Path $rec
    return @{ Ok = $true; Why = ''; EventId = $e.eventId }
}

function Clear-AvbHilSafetyHalt {
    param([string]$Path = (Get-AvbHilSafetyPath), [string]$Operator, $LockOwner, [string]$LockPath = (Get-AvbHilLockPath), [string]$VerificationEvidence)
    $held = Test-AvbHilLockHeld -Owner $LockOwner -Path $LockPath
    if (-not $held.Ok) { return @{ Ok = $false; Why = "exclusive HIL control required: $($held.Why)" } }
    if (-not $Operator) { return @{ Ok = $false; Why = 'responsible operator required' } }
    $st = Read-AvbHilSafetyState -Path $Path
    if ($st.State -ne 'HALTED') { return @{ Ok = $false; Why = "record is $($st.State) $($st.Why) - only a HALTED record can be cleared; an unavailable record is never replaced by clearing" } }
    if (-not $VerificationEvidence -or -not (Test-Path -LiteralPath $VerificationEvidence -PathType Leaf)) { return @{ Ok = $false; Why = 'verification evidence document missing' } }
    try { $v = Get-Content -LiteralPath $VerificationEvidence -Raw | ConvertFrom-Json } catch { return @{ Ok = $false; Why = "verification evidence unreadable: $($_.Exception.Message)" } }
    if ($v.schema -ne 'avb-hil-recovery-verification/1' -or $v.host -ne $env:COMPUTERNAME) { return @{ Ok = $false; Why = 'verification evidence has a wrong schema or host' } }
    if ($v.verdict -ne 'RESTORATION_VERIFIED') { return @{ Ok = $false; Why = "verification verdict is '$($v.verdict)', not RESTORATION_VERIFIED" } }
    if (@($v.identity).Count -eq 0 -or @($v.checks).Count -eq 0 -or @(@($v.checks) | Where-Object { -not $_.ok }).Count -gt 0) { return @{ Ok = $false; Why = 'verification evidence lacks a confirmed identity or has a failing / missing check' } }
    $open = @(@($st.Record.events) | Where-Object { $_.recovery.status -in @('PENDING', 'ATTEMPTED') })
    $uncovered = @($open | Where-Object { @($v.eventIds) -notcontains $_.eventId })
    if ($uncovered.Count -gt 0) { return @{ Ok = $false; Why = ("verification does not cover event(s): {0}" -f (($uncovered | ForEach-Object { $_.eventId }) -join ', ')) } }
    $now = (Get-Date).ToUniversalTime().ToString('o')
    foreach ($e in $open) { $e.recovery = [ordered]@{ status = 'VERIFIED'; operator = $Operator; evidence = $VerificationEvidence; utc = $now } }
    $st.Record.status = 'CLEAR'
    $st.Record.history = @(@($st.Record.history) + @([ordered]@{ utc = $now; operation = 'CLEAR'; operator = $Operator; evidence = $VerificationEvidence; events = @($open | ForEach-Object { $_.eventId }) }))
    Write-AvbHilSafetyFile $Path $st.Record
    return @{ Ok = $true; Why = '' }
}

# Operator-approved rebaselining when the original state cannot be reconstructed: the failure evidence and verdicts stay.
function Invoke-AvbHilSafetyRebaseline {
    param([string]$Path = (Get-AvbHilSafetyPath), [string]$Operator, [string]$Approval, $LockOwner, [string]$LockPath = (Get-AvbHilLockPath), [string]$BaselineEvidence)
    $held = Test-AvbHilLockHeld -Owner $LockOwner -Path $LockPath
    if (-not $held.Ok) { return @{ Ok = $false; Why = "exclusive HIL control required: $($held.Why)" } }
    if (-not $Operator -or -not $Approval) { return @{ Ok = $false; Why = 'operator and explicit approval text required' } }
    if (-not $BaselineEvidence -or -not (Test-Path -LiteralPath $BaselineEvidence -PathType Leaf)) { return @{ Ok = $false; Why = 'new baseline evidence document missing' } }
    $st = Read-AvbHilSafetyState -Path $Path
    if ($st.State -ne 'HALTED') { return @{ Ok = $false; Why = "record is $($st.State) $($st.Why) - only a HALTED record can be rebaselined" } }
    $now = (Get-Date).ToUniversalTime().ToString('o')
    $open = @(@($st.Record.events) | Where-Object { $_.recovery.status -in @('PENDING', 'ATTEMPTED') })
    foreach ($e in $open) { $e.recovery = [ordered]@{ status = 'REBASELINED'; operator = $Operator; approval = $Approval; evidence = $BaselineEvidence; utc = $now } }
    $st.Record.status = 'CLEAR'
    $st.Record.history = @(@($st.Record.history) + @([ordered]@{ utc = $now; operation = 'REBASELINE'; operator = $Operator; approval = $Approval; evidence = $BaselineEvidence; events = @($open | ForEach-Object { $_.eventId }) }))
    Write-AvbHilSafetyFile $Path $st.Record
    return @{ Ok = $true; Why = '' }
}
