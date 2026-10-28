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
# Original SESSION baseline (Q 2026-10-21): one per exclusive HIL session, beside the lock
# (%ProgramData%\IntelAvbFilter-hil-session.json, schema avb-hil-session/1), independent of suite results.
#   session id: ci:<runId>:<attempt> (spans the hil-debug AND hil-release jobs of one workflow run) or
#               local:<host>:<owner pid>:<lock acquiredUtc> (spans every suite/configuration of one LocalSuites run).
# Three distinct baselines - never conflated: the machine's APPROVED baseline (safety record), this SESSION baseline
# (detects state changes that escape per-test cleanup), and the per-suite / per-test snapshots (test cleanup).
# Never overwritten in place; a different, finished session's file is archived (.prev-*) when a new one starts.
# ---------------------------------------------------------------------------------------------------------
function Get-AvbHilSessionPath { return (Join-Path $env:ProgramData 'IntelAvbFilter-hil-session.json') }

function Get-AvbHilSessionId {
    param($LockOwner, $GitHub = $null)
    if ($null -ne $GitHub) { return ("ci:{0}:{1}" -f $GitHub.runId, $GitHub.runAttempt) }
    if ($null -eq $LockOwner) { return '' }
    return ("local:{0}:{1}:{2}" -f $LockOwner.host, $LockOwner.pid, $LockOwner.acquiredUtc)
}

# @{ State = NONE|OK|UNAVAILABLE; Session; Why }
function Read-AvbHilSession {
    param([string]$Path = (Get-AvbHilSessionPath))
    if (-not (Test-Path -LiteralPath $Path)) { return @{ State = 'NONE'; Session = $null; Why = 'no session baseline' } }
    try { $s = Get-Content -LiteralPath $Path -Raw -ErrorAction Stop | ConvertFrom-Json } catch { return @{ State = 'UNAVAILABLE'; Session = $null; Why = "session baseline unreadable or corrupt: $($_.Exception.Message)" } }
    if ($null -eq $s -or $s.schema -ne 'avb-hil-session/1') { return @{ State = 'UNAVAILABLE'; Session = $null; Why = 'session baseline has an unknown schema' } }
    if ($s.host -ne $env:COMPUTERNAME) { return @{ State = 'UNAVAILABLE'; Session = $s; Why = "session baseline belongs to host '$($s.host)', not '$($env:COMPUTERNAME)'" } }
    if ($null -eq $s.baseline) { return @{ State = 'UNAVAILABLE'; Session = $s; Why = 'session baseline has no captured state' } }
    return @{ State = 'OK'; Session = $s; Why = '' }
}

# Creates the session baseline; an existing file (another session) is archived first, never overwritten.  Its OPEN cleanup
# incidents are carried into the new record (carriedFrom = that session): a session boundary never erases unresolved
# cleanup evidence (Q 2026-10-28).  The archive keeps the full history.
function New-AvbHilSession {
    param([string]$Path = (Get-AvbHilSessionPath), [string]$SessionId, $Owner, $Baseline, $Provenance)
    $carried = @()
    if (Test-Path -LiteralPath $Path) {
        $prev = Read-AvbHilSession -Path $Path
        if ($prev.Session -and $prev.Session.PSObject.Properties['cleanupIncidents']) {
            $carried = @(@($prev.Session.cleanupIncidents) | Where-Object { $null -ne $_ -and $_.status -eq 'OPEN' } | ForEach-Object {
                if (-not $_.PSObject.Properties['carriedFrom']) { $_ | Add-Member -NotePropertyName carriedFrom -NotePropertyValue $prev.Session.sessionId }
                $_ })
        }
        $arch = "$Path.prev-$((Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssfff')).json"
        Move-Item -LiteralPath $Path -Destination $arch
    }
    $s = [ordered]@{ schema = 'avb-hil-session/1'; sessionId = $SessionId; host = $env:COMPUTERNAME; owner = $Owner
                     createdUtc = (Get-Date).ToUniversalTime().ToString('o'); provenance = $Provenance; baseline = $Baseline; checks = @()
                     cleanupIncidents = @($carried) }
    Write-AvbHilSafetyFile $Path $s
    return $s
}

# Appends a start/end comparison of one suite; never changes the captured baseline.
function Add-AvbHilSessionCheck {
    param([string]$Path = (Get-AvbHilSessionPath), $Check)
    $r = Read-AvbHilSession -Path $Path
    if ($r.State -ne 'OK') { return @{ Ok = $false; Why = $r.Why } }
    $r.Session.checks = @(@($r.Session.checks) + @($Check))
    Write-AvbHilSafetyFile $Path $r.Session
    return @{ Ok = $true; Why = '' }
}

# Cleanup incidents (Q 2026-10-28, option b): diagnostic evidence in the SESSION record - never in the machine safety
# record, never a dispatch gate.  An observed difference whose key (adapter | component | expected | observed) matches an
# OPEN incident is another observation of the same unresolved condition ('confirmed'); otherwise a new incident
# ('opened').  RESOLVED / SUPERSEDED incidents stay as history: a recurrence after them opens a NEW incident, so equal
# register values are never merged across a resolution.  @{ Ok; IncidentId; Role; Why }
function Add-AvbHilSessionIncident {
    param([string]$Path = (Get-AvbHilSessionPath), $Item, $Observation, $Origin)
    $r = Read-AvbHilSession -Path $Path
    if ($r.State -ne 'OK') { return @{ Ok = $false; Why = $r.Why } }
    $incs = @(if ($r.Session.PSObject.Properties['cleanupIncidents']) { @($r.Session.cleanupIncidents) | Where-Object { $null -ne $_ } })
    $open = @($incs | Where-Object { $_.status -eq 'OPEN' -and $_.key -eq $Item.Key }) | Select-Object -First 1
    if ($open) {
        $open.observations = @(@($open.observations) + @($Observation)); $id = $open.incidentId; $role = 'confirmed'
    } else {
        $id = [guid]::NewGuid().ToString(); $role = 'opened'
        $incs += ,([ordered]@{ incidentId = $id; key = $Item.Key; adapter = $Item.Adapter; identityVerified = [bool]$Item.IdentityVerified
                               component = $Item.Component; expected = $Item.Expected; observed = $Item.Observed; status = 'OPEN'
                               openedUtc = $Observation.utc; origin = $Origin; observations = @($Observation); resolvedUtc = $null; resolution = $null })
    }
    Set-AvbRecordField $r.Session 'cleanupIncidents' @($incs)
    Write-AvbHilSafetyFile $Path $r.Session
    return @{ Ok = $true; IncidentId = $id; Role = $role; Why = '' }
}

# Closes this session's OPEN incidents that a comparison against the ORIGINAL session baseline no longer shows: RESOLVED
# (the component equals the baseline again) or SUPERSEDED (it now differs in another way - a new incident).  Never when
# the comparison could not read every component, never a carried incident (judged against another session's baseline).
# Per-test comparisons use a moving baseline and must not call this.  @{ Ok; Closed = @(@{ IncidentId; Status; Key }); Why }
function Close-AvbHilSessionIncidents {
    param([string]$Path = (Get-AvbHilSessionPath), [object[]]$Items, $Observation)
    if (@($Items | Where-Object { $_.Kind -eq 'unobservable' }).Count -gt 0) { return @{ Ok = $true; Closed = @(); Why = 'comparison incomplete' } }
    $r = Read-AvbHilSession -Path $Path
    if ($r.State -ne 'OK') { return @{ Ok = $false; Closed = @(); Why = $r.Why } }
    if (-not $r.Session.PSObject.Properties['cleanupIncidents']) { return @{ Ok = $true; Closed = @(); Why = '' } }
    $diffKeys = @($Items | Where-Object { $_.Kind -eq 'diff' } | ForEach-Object { $_.Key })
    $diffComponents = @($diffKeys | ForEach-Object { (($_ -split '\|')[0..1]) -join '|' })
    $closed = @()
    foreach ($inc in @($r.Session.cleanupIncidents | Where-Object { $null -ne $_ -and $_.status -eq 'OPEN' -and -not $_.PSObject.Properties['carriedFrom'] })) {
        if ($diffKeys -contains $inc.key) { continue }
        $st = if ($diffComponents -contains ((($inc.key -split '\|')[0..1]) -join '|')) { 'SUPERSEDED' } else { 'RESOLVED' }
        $inc.status = $st; $inc.resolvedUtc = $Observation.utc; $inc.resolution = $Observation
        $closed += ,@{ IncidentId = $inc.incidentId; Status = $st; Key = $inc.key }
    }
    if ($closed.Count -gt 0) { Write-AvbHilSafetyFile $Path $r.Session }
    return @{ Ok = $true; Closed = $closed; Why = '' }
}

# ---------------------------------------------------------------------------------------------------------
# Machine-wide HIL safety record (Q 2026-10-21): %ProgramData%\IntelAvbFilter-hil-safety.json, schema avb-hil-safety/1.
# Shared by every process, configuration and run on this machine (Debug, Release, LocalSuites, CI).
#   UNBASELINED record infrastructure exists, no authorized hardware baseline -> Init/Mutating BLOCKED
#   CLEAR       authorized, evidence-backed baseline and no unrecovered event   -> Init/Mutating may run
#   HALTED      at least one safety event with recovery PENDING / ATTEMPTED    -> Init/Mutating BLOCKED, ReadOnly runs
#   UNAVAILABLE missing / unreadable / corrupt / wrong schema / other host /
#               internally inconsistent                                       -> fail closed (as HALTED)
# Events are appended, never removed.  Nothing clears a halt implicitly (reboot, reinstall, new run, new baseline);
# only Clear-AvbHilSafetyHalt (verified recovery), Invoke-AvbHilSafetyRebaseline (operator-approved) or
# Invoke-AvbHilSafetyReclassification (events the current policy does not classify as safety events) do, and only while
# holding the machine lock.  Scope is GLOBAL until physical adapter identity and isolation are
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
    $hasBaseline = ($null -ne $r.PSObject.Properties['baseline'] -and $null -ne $r.baseline)
    if ($r.status -eq 'UNBASELINED') {
        if ($open.Count -gt 0) { return (& $un 'safety record inconsistent: UNBASELINED with unrecovered events') }
        return @{ State = 'UNBASELINED'; Why = 'no authorized hardware baseline (Approve-AvbHilSafetyBaseline)'; Record = $r }
    }
    if ($r.status -eq 'HALTED') {
        if ($open.Count -eq 0) { return (& $un 'safety record inconsistent: HALTED without an unrecovered event') }
        $tr = $open[0].trigger
        $what = if ($null -eq $tr) { '' } else { @(foreach ($n in 'executable', 'record', 'testcase') { if ($tr.PSObject.Properties[$n] -and $tr.$n) { $tr.$n } })[0] }
        return @{ State = 'HALTED'; Why = ("{0} unrecovered safety event(s), first: {1} {2} at {3}" -f $open.Count, $open[0].verdict, $what, $open[0].utc); Record = $r }
    }
    if ($r.status -eq 'CLEAR') {
        if ($open.Count -gt 0) { return (& $un 'safety record inconsistent: CLEAR with unrecovered events') }
        if (-not $hasBaseline) { return (& $un 'safety record inconsistent: CLEAR without an authorized baseline') }
        return @{ State = 'CLEAR'; Why = ''; Record = $r }
    }
    return (& $un "safety record has an unknown status '$($r.status)'")
}

function Initialize-AvbHilSafetyRecord {
    param([string]$Path = (Get-AvbHilSafetyPath), [string]$Operator, [string]$Reason)
    if (Test-Path -LiteralPath $Path) { return @{ Ok = $false; Why = "a safety record already exists at $Path (never overwritten; inspect it)" } }
    if (-not $Operator -or -not $Reason) { return @{ Ok = $false; Why = 'operator and reason are required' } }
    $now = (Get-Date).ToUniversalTime().ToString('o')
    # Infrastructure only: UNBASELINED, asserts nothing about the hardware (Approve-AvbHilSafetyBaseline makes it CLEAR).
    Write-AvbHilSafetyFile $Path ([ordered]@{ schema = 'avb-hil-safety/1'; host = $env:COMPUTERNAME; status = 'UNBASELINED'; baseline = $null; events = @()
        history = @([ordered]@{ utc = $now; operation = 'INITIALIZE'; operator = $Operator; reason = $Reason }) })
    return @{ Ok = $true; Why = '' }
}

# $SafetyEvent (-Event): verdict, scope, hardware, trigger, initialState, lastKnownState, evidence (eventId/utc/host/recovery are added).
function Add-AvbHilSafetyEvent {
    # -IncidentKey: a hardware discrepancy (adapter|component|expected|observed).  An OPEN event with the same key gets an
    # appended observation (one incident, many observations; history append-only); otherwise a new event whose id is
    # -EventId when given (the runner's incident id).
    param([string]$Path = (Get-AvbHilSafetyPath), [Alias('Event')]$SafetyEvent, [string]$IncidentKey = '', [string]$EventId = '')
    $now = (Get-Date).ToUniversalTime().ToString('o')
    # StrictMode-safe: callers may omit optional fields.
    $f = { param($n) if ($SafetyEvent -is [System.Collections.IDictionary]) { if ($SafetyEvent.Contains($n)) { $SafetyEvent[$n] } } elseif ($null -ne $SafetyEvent -and $SafetyEvent.PSObject.Properties[$n]) { $SafetyEvent.$n } }
    $scope = & $f 'scope'
    $obs = [ordered]@{ utc = $now; trigger = (& $f 'trigger'); lastKnownState = (& $f 'lastKnownState') }
    $e = [ordered]@{ eventId = $(if ($EventId) { $EventId } else { [guid]::NewGuid().ToString() }); utc = $now; host = $env:COMPUTERNAME
                     incidentKey = $(if ($IncidentKey) { $IncidentKey } else { $null }); observations = @($obs)
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
        [pscustomobject]@{ schema = 'avb-hil-safety/1'; host = $env:COMPUTERNAME; status = 'HALTED'; baseline = $null; events = @()
                           history = @([ordered]@{ utc = $now; operation = 'CREATED_BY_EVENT'; operator = ''; reason = 'no record existed when a safety event occurred' }) } }
    if ($IncidentKey) {
        $same = @(@($rec.events) | Where-Object { $null -ne $_ -and $_.PSObject.Properties['incidentKey'] -and $_.incidentKey -eq $IncidentKey -and $_.recovery.status -in @('PENDING', 'ATTEMPTED') })
        if ($same.Count -gt 0) {   # same open incident: one more observation, never a second event
            $ev = $same[0]
            if ($ev.PSObject.Properties['observations']) { $ev.observations = @(@($ev.observations) + @($obs)) } else { $ev | Add-Member -NotePropertyName observations -NotePropertyValue @($obs) }
            Write-AvbHilSafetyFile $Path $rec
            return @{ Ok = $true; Why = ''; EventId = $ev.eventId; Merged = $true }
        }
    }
    $rec.events = @(@($rec.events) + @($e)); $rec.status = 'HALTED'
    Write-AvbHilSafetyFile $Path $rec
    return @{ Ok = $true; Why = ''; EventId = $e.eventId; Merged = $false }
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
    # Recovery restores the AUTHORIZED baseline; a machine that never had one returns to UNBASELINED, never CLEAR.
    $st.Record.status = $(if ($null -ne $st.Record.PSObject.Properties['baseline'] -and $null -ne $st.Record.baseline) { 'CLEAR' } else { 'UNBASELINED' })
    $st.Record.history = @(@($st.Record.history) + @([ordered]@{ utc = $now; operation = 'CLEAR'; operator = $Operator; evidence = $VerificationEvidence; events = @($open | ForEach-Object { $_.eventId }) }))
    Write-AvbHilSafetyFile $Path $st.Record
    return @{ Ok = $true; Why = '' }
}

function Set-AvbRecordField($Record, [string]$Name, $Value) {
    if ($null -ne $Record.PSObject.Properties[$Name]) { $Record.$Name = $Value } else { $Record | Add-Member -NotePropertyName $Name -NotePropertyValue $Value }
}

# Baseline authorization document (avb-hil-baseline-authorization/1): this host, BASELINE_VERIFIED, a physical inventory
# in which every adapter has a DID and a stable identity (MAC or PCI location), and non-empty measurements.
function Test-AvbHilBaselineEvidence {
    param([string]$Path)
    if (-not $Path -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) { return @{ Ok = $false; Why = 'baseline evidence document missing' } }
    try { $b = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json } catch { return @{ Ok = $false; Why = "baseline evidence unreadable: $($_.Exception.Message)" } }
    if ($b.schema -ne 'avb-hil-baseline-authorization/1' -or $b.host -ne $env:COMPUTERNAME) { return @{ Ok = $false; Why = 'baseline evidence has a wrong schema or host' } }
    if ($b.verdict -ne 'BASELINE_VERIFIED') { return @{ Ok = $false; Why = "baseline verdict is '$($b.verdict)', not BASELINE_VERIFIED" } }
    $inv = @($b.inventory)
    $noId = @($inv | Where-Object { -not $_.did -or (-not ($_.PSObject.Properties['mac'] -and $_.mac) -and -not ($_.PSObject.Properties['pciLocation'] -and $_.pciLocation)) })
    if ($inv.Count -eq 0 -or $noId.Count -gt 0) { return @{ Ok = $false; Why = 'baseline inventory empty or an adapter lacks a stable physical identity (MAC or PCI location)' } }
    if (@($b.measurements).Count -eq 0) { return @{ Ok = $false; Why = 'baseline evidence has no measurements' } }
    return @{ Ok = $true; Why = '' }
}

# Authorize the initial hardware baseline: UNBASELINED -> CLEAR (machine lock, operator, authorization text, evidence).
function Approve-AvbHilSafetyBaseline {
    param([string]$Path = (Get-AvbHilSafetyPath), [string]$Operator, [string]$Authorization, $LockOwner, [string]$LockPath = (Get-AvbHilLockPath), [string]$BaselineEvidence)
    $held = Test-AvbHilLockHeld -Owner $LockOwner -Path $LockPath
    if (-not $held.Ok) { return @{ Ok = $false; Why = "exclusive HIL control required: $($held.Why)" } }
    if (-not $Operator -or -not $Authorization) { return @{ Ok = $false; Why = 'operator and explicit authorization text required' } }
    $bv = Test-AvbHilBaselineEvidence -Path $BaselineEvidence
    if (-not $bv.Ok) { return $bv }
    $st = Read-AvbHilSafetyState -Path $Path
    if ($st.State -ne 'UNBASELINED') { return @{ Ok = $false; Why = "record is $($st.State) $($st.Why) - only an UNBASELINED record takes an initial baseline" } }
    $now = (Get-Date).ToUniversalTime().ToString('o')
    Set-AvbRecordField $st.Record 'baseline' ([ordered]@{ evidence = $BaselineEvidence; operator = $Operator; authorization = $Authorization; utc = $now; via = 'APPROVE' })
    $st.Record.status = 'CLEAR'
    $st.Record.history = @(@($st.Record.history) + @([ordered]@{ utc = $now; operation = 'APPROVE_BASELINE'; operator = $Operator; authorization = $Authorization; evidence = $BaselineEvidence }))
    Write-AvbHilSafetyFile $Path $st.Record
    return @{ Ok = $true; Why = '' }
}

# Operator-approved rebaselining when the original state cannot be reconstructed: the failure evidence and verdicts stay.
function Invoke-AvbHilSafetyRebaseline {
    param([string]$Path = (Get-AvbHilSafetyPath), [string]$Operator, [string]$Approval, $LockOwner, [string]$LockPath = (Get-AvbHilLockPath), [string]$BaselineEvidence)
    $held = Test-AvbHilLockHeld -Owner $LockOwner -Path $LockPath
    if (-not $held.Ok) { return @{ Ok = $false; Why = "exclusive HIL control required: $($held.Why)" } }
    if (-not $Operator -or -not $Approval) { return @{ Ok = $false; Why = 'operator and explicit approval text required' } }
    $bv = Test-AvbHilBaselineEvidence -Path $BaselineEvidence
    if (-not $bv.Ok) { return $bv }
    $st = Read-AvbHilSafetyState -Path $Path
    if ($st.State -ne 'HALTED') { return @{ Ok = $false; Why = "record is $($st.State) $($st.Why) - only a HALTED record can be rebaselined" } }
    $now = (Get-Date).ToUniversalTime().ToString('o')
    $open = @(@($st.Record.events) | Where-Object { $_.recovery.status -in @('PENDING', 'ATTEMPTED') })
    foreach ($e in $open) { $e.recovery = [ordered]@{ status = 'REBASELINED'; operator = $Operator; approval = $Approval; evidence = $BaselineEvidence; utc = $now } }
    Set-AvbRecordField $st.Record 'baseline' ([ordered]@{ evidence = $BaselineEvidence; operator = $Operator; authorization = $Approval; utc = $now; via = 'REBASELINE' })
    $st.Record.status = 'CLEAR'
    $st.Record.history = @(@($st.Record.history) + @([ordered]@{ utc = $now; operation = 'REBASELINE'; operator = $Operator; approval = $Approval; evidence = $BaselineEvidence; events = @($open | ForEach-Object { $_.eventId }) }))
    Write-AvbHilSafetyFile $Path $st.Record
    return @{ Ok = $true; Why = '' }
}

# Policy reconciliation (Q 2026-10-28): cleanup outcomes (SUCCESS / FAILED / NOT_VERIFIED) are never machine-safety
# events (b225bb4); only loss of controlled execution is (FAILED(TIMEOUT) with a surviving process tree, LOCK_LOST).
# The former rule also persisted CLEANUP_FAILED and INCONCLUSIVE_RESTORATION; such unrecovered events are reclassified,
# chosen by their recorded verdict, as non-blocking cleanup diagnostics.  Not operator acceptance and not a restoration
# claim: recovery RECLASSIFIED keeps the cleanup outcome (FAILED / NOT_VERIFIED, restorationVerified false) and the
# previous recovery.  Ids, timestamps, verdict, trigger and evidence stay; the record before the change is copied beside
# it.  FAILED(TIMEOUT), LOCK_LOST and any unknown verdict keep the halt; the status is recomputed from them.  Machine lock
# required; idempotent (nothing to reclassify = no write).  @{ Ok; Why; Changed; Events; Remaining; State; Backup }
$script:AvbHilCleanupOnlyVerdicts = @{ 'CLEANUP_FAILED' = 'FAILED'; 'INCONCLUSIVE_RESTORATION' = 'NOT_VERIFIED' }
function Invoke-AvbHilSafetyReclassification {
    param([string]$Path = (Get-AvbHilSafetyPath), $LockOwner, [string]$LockPath = (Get-AvbHilLockPath), [string]$SourceRevision, [string]$InitiatedBy)
    $held = Test-AvbHilLockHeld -Owner $LockOwner -Path $LockPath
    if (-not $held.Ok) { return @{ Ok = $false; Changed = $false; Why = "exclusive HIL control required: $($held.Why)" } }
    if (-not $SourceRevision -or -not $InitiatedBy) { return @{ Ok = $false; Changed = $false; Why = 'source revision of the classification policy and initiator required' } }
    $st = Read-AvbHilSafetyState -Path $Path
    if ($st.State -eq 'UNAVAILABLE') { return @{ Ok = $false; Changed = $false; Why = "record is UNAVAILABLE ($($st.Why)) - an unavailable record is never rewritten" } }
    $open = @(@($st.Record.events) | Where-Object { $null -ne $_ -and $_.recovery.status -in @('PENDING', 'ATTEMPTED') })
    $targets = @($open | Where-Object { $script:AvbHilCleanupOnlyVerdicts.ContainsKey([string]$_.verdict) })
    if ($targets.Count -eq 0) { return @{ Ok = $true; Changed = $false; Why = 'no unrecovered cleanup-only event'; Events = @(); Remaining = @($open | ForEach-Object { $_.eventId }); State = $st.State; Backup = $null } }
    $now = (Get-Date).ToUniversalTime()
    $backup = "$Path.pre-reclassify-$($now.ToString('yyyyMMddTHHmmssfff')).json"
    try { Copy-Item -LiteralPath $Path -Destination $backup -ErrorAction Stop } catch { return @{ Ok = $false; Changed = $false; Why = "pre-image not preserved ($($_.Exception.Message)); record unchanged" } }
    $utc = $now.ToString('o')
    $policy = 'cleanup outcomes (SUCCESS / FAILED / NOT_VERIFIED) are not machine-safety events; only loss of controlled execution is'
    $rationale = 'persisted by the former rule that escalated a cleanup outcome to a GLOBAL safety halt; no loss of controlled execution is recorded for it; the cleanup outcome stays as recorded and restoration is not claimed'
    foreach ($e in $targets) {
        $ev = @(foreach ($f in @($e.evidence) | Where-Object { $_ }) {
            if (Test-Path -LiteralPath $f -PathType Leaf) { [ordered]@{ path = $f; sha256 = (Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash } }
            else { [ordered]@{ path = $f; sha256 = $null; note = 'not present at reclassification' } } })
        $e.recovery = [ordered]@{ status = 'RECLASSIFIED'; classification = 'CLEANUP_DIAGNOSTIC'; cleanupOutcome = $script:AvbHilCleanupOnlyVerdicts[[string]$e.verdict]
                                  restorationVerified = $false; previous = $e.recovery; policy = $policy; sourceRevision = $SourceRevision
                                  initiatedBy = $InitiatedBy; utc = $utc; evidence = $ev }
    }
    $ids = @($targets | ForEach-Object { $_.eventId })
    $remaining = @($open | Where-Object { $ids -notcontains $_.eventId } | ForEach-Object { $_.eventId })
    $hasBaseline = ($null -ne $st.Record.PSObject.Properties['baseline'] -and $null -ne $st.Record.baseline)
    $st.Record.status = $(if ($remaining.Count -gt 0) { 'HALTED' } elseif ($hasBaseline) { 'CLEAR' } else { 'UNBASELINED' })
    $st.Record.history = @(@($st.Record.history) + @([ordered]@{ utc = $utc; operation = 'RECLASSIFY_CLEANUP_ONLY'; initiatedBy = $InitiatedBy; sourceRevision = $SourceRevision
        policy = $policy; rationale = $rationale; events = $ids; remaining = $remaining; resultingStatus = $st.Record.status; preImage = $backup }))
    try { Write-AvbHilSafetyFile $Path $st.Record }
    catch {   # the atomic move did not happen: the record is unchanged
        Remove-Item -LiteralPath "$Path.tmp-$PID" -Force -ErrorAction SilentlyContinue
        return @{ Ok = $false; Changed = $false; Why = "record not written ($($_.Exception.Message)); unchanged, pre-image $backup kept" }
    }
    return @{ Ok = $true; Changed = $true; Why = ''; Events = $ids; Remaining = $remaining; State = $st.Record.status; Backup = $backup }
}
