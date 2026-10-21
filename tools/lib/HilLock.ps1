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
function Exit-AvbHilLock {
    param($Owner, [string]$Path = (Get-AvbHilLockPath))
    $cur = Read-AvbHilLock -Path $Path
    if ($null -eq $cur) { return @{ Ok = $false; Why = 'lock already absent' } }
    $same = ($cur.kind -eq $Owner.kind -and [string]$cur.pid -eq [string]$Owner.pid -and [string]$cur.runId -eq [string]$Owner.runId -and
             [string]$cur.runAttempt -eq [string]$Owner.runAttempt -and [string]$cur.job -eq [string]$Owner.job -and $cur.acquiredUtc -eq $Owner.acquiredUtc)
    # A CI job releases from another step process: same run/attempt/job identifies the owner, the PID differs.
    if (-not $same -and $Owner.kind -eq 'ci') {
        $same = ($cur.kind -eq 'ci' -and [string]$cur.runId -eq [string]$Owner.runId -and [string]$cur.runAttempt -eq [string]$Owner.runAttempt -and [string]$cur.job -eq [string]$Owner.job)
    }
    if (-not $same) { return @{ Ok = $false; Why = "lock is held by another owner ($($cur.kind) '$($cur.label)')" } }
    Remove-Item -LiteralPath $Path -Force
    return @{ Ok = $true; Why = '' }
}
