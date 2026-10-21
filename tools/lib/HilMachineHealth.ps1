# HilMachineHealth.ps1 - automatic crash evidence for physical HIL (replaces the operator variable HIL_BUGCHECK_ACK).
# Dot-source; no side effects.
#
# Sources (read-only): System event log - BugCheck (Microsoft-Windows-WER-SystemErrorReporting 1001), unexpected
# shutdown (Microsoft-Windows-Kernel-Power 41, EventLog 6008); %SystemRoot%\Minidump\*.dmp and MEMORY.DMP (metadata;
# minidumps of the window are copied into the evidence directory); last boot time.
# Classification relative to the HIL serialization lock (tools/lib/HilLock.ps1):
#   HISTORICAL  before the previous HIL owner started (or no previous owner known)  -> recorded, never blocks
#   PREVIOUS_RUN_CRASHED  after a previous owner acquired the lock it never released -> that run is attributed
#               CRASHED (its results are void); evidence retained; does not block this run
#   CURRENT_RUN_INTERRUPTED  boot time after this run started (machine rebooted/crashed during this run) -> this run
#               is quarantined: its results are void and acceptance fails
# Nothing here requires or accepts an operator acknowledgement.

function New-AvbRealHealthSource {
    return @{
        Events = { param([datetime]$SinceUtc)
            $f = @{ LogName = 'System'; StartTime = $SinceUtc.ToLocalTime() }
            @(Get-WinEvent -FilterHashtable $f -ErrorAction SilentlyContinue | Where-Object {
                ($_.ProviderName -eq 'Microsoft-Windows-WER-SystemErrorReporting' -and $_.Id -eq 1001) -or
                ($_.ProviderName -eq 'Microsoft-Windows-Kernel-Power' -and $_.Id -eq 41) -or ($_.ProviderName -eq 'EventLog' -and $_.Id -eq 6008)
            } | ForEach-Object { @{ TimeUtc = $_.TimeCreated.ToUniversalTime(); Provider = $_.ProviderName; Id = $_.Id; Message = ([string]$_.Message).Split("`n")[0].Trim() } })
        }
        Dumps = {
            $d = @()
            $mini = Join-Path $env:SystemRoot 'Minidump'
            if (Test-Path -LiteralPath $mini) { $d += @(Get-ChildItem -LiteralPath $mini -Filter '*.dmp' -File | ForEach-Object { @{ Path = $_.FullName; TimeUtc = $_.LastWriteTimeUtc; Bytes = $_.Length; Mini = $true } }) }
            $full = Join-Path $env:SystemRoot 'MEMORY.DMP'
            if (Test-Path -LiteralPath $full) { $i = Get-Item -LiteralPath $full; $d += @{ Path = $i.FullName; TimeUtc = $i.LastWriteTimeUtc; Bytes = $i.Length; Mini = $false } }
            return $d
        }
        LastBootUtc = { (Get-CimInstance -ClassName Win32_OperatingSystem).LastBootUpTime.ToUniversalTime() }
        CopyFile = { param([string]$Source, [string]$Destination) Copy-Item -LiteralPath $Source -Destination $Destination -Force }
    }
}

# @{ Classification; Events; Dumps; LastBootUtc; Summary }   $PreviousOwner: HilLock PreviousOwner record or $null.
function Get-AvbCrashEvidence {
    param([hashtable]$Source, $PreviousOwner, [datetime]$RunStartUtc, [int]$HistoryDays = 30, [string]$EvidenceDir = '')
    $since = $RunStartUtc.AddDays(-$HistoryDays)
    $events = @(& $Source.Events $since | Sort-Object { $_.TimeUtc })
    $dumps = @(& $Source.Dumps | Where-Object { $_.TimeUtc -ge $since })
    $boot = & $Source.LastBootUtc
    $prevStart = $null
    if ($PreviousOwner -and $PreviousOwner.owner -and $PreviousOwner.owner.acquiredUtc) {
        $prevStart = ([datetime]::Parse([string]$PreviousOwner.owner.acquiredUtc, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind)).ToUniversalTime()
    }
    $classification = 'NONE'
    $summary = @()
    if ($boot -gt $RunStartUtc) {
        $classification = 'CURRENT_RUN_INTERRUPTED'
        $summary += "machine booted at $($boot.ToString('o')), after this run started at $($RunStartUtc.ToString('o'))"
    } elseif ($prevStart -and (@($events | Where-Object { $_.TimeUtc -ge $prevStart }).Count -gt 0 -or $boot -gt $prevStart)) {
        $classification = 'PREVIOUS_RUN_CRASHED'
        $summary += "previous HIL owner (run $($PreviousOwner.owner.runId) job $($PreviousOwner.owner.job), started $($prevStart.ToString('o'))) never released the lock; crash/reboot evidence after its start"
    } elseif ($events.Count -gt 0) {
        $classification = 'HISTORICAL'
        $summary += "$($events.Count) crash/unexpected-shutdown event(s) in the last $HistoryDays days, none attributable to an unfinished HIL run"
    }
    if ($prevStart -and $classification -ne 'PREVIOUS_RUN_CRASHED' -and $classification -ne 'CURRENT_RUN_INTERRUPTED') {
        $summary += "previous HIL owner left the lock without crash evidence (job cancelled or runner connection lost): its results are void"
    }
    $copied = @()
    if ($EvidenceDir) {
        $cut = if ($prevStart) { $prevStart } else { $RunStartUtc.AddDays(-$HistoryDays) }
        foreach ($d in @($dumps | Where-Object { $_.Mini -and $_.TimeUtc -ge $cut })) {
            $dst = Join-Path $EvidenceDir ([System.IO.Path]::GetFileName($d.Path))
            & $Source.CopyFile $d.Path $dst
            $copied += $dst
        }
    }
    return [ordered]@{
        classification = $classification; summary = $summary; lastBootUtc = $boot.ToString('o'); runStartUtc = $RunStartUtc.ToString('o')
        previousOwner = $PreviousOwner
        events = @($events | ForEach-Object { [ordered]@{ timeUtc = $_.TimeUtc.ToString('o'); provider = $_.Provider; id = $_.Id; message = $_.Message } })
        dumps = @($dumps | ForEach-Object { [ordered]@{ path = $_.Path; timeUtc = $_.TimeUtc.ToString('o'); bytes = $_.Bytes } })
        copiedMinidumps = $copied
    }
}
