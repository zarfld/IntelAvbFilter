<#
.SYNOPSIS
    Final #328 verdict for TC-LCY-IDLE-001 (Approach B): correlates the observational
    [EVIDENCE] lines of test_device_idle_regression.exe with exported
    Microsoft-Windows-NDIS/Diagnostic power events.

.DESCRIPTION
    READ-ONLY.  Never enables Event Log channels, never touches the driver, NIC
    power settings or services.  The C executable's own result is retained as a
    separate record (observational_exit); this script never rewrites it.

    Event semantics (verified against the Microsoft-Windows-NDIS provider manifest,
    template fields IfGuid / IfIndex / IfLuid / Status):
        10308  Low power state requested for adapter
        10309  Low power request completed           (Status; 0 = NDIS_STATUS_SUCCESS)
        10306  Working power state requested for adapter
        10307  Working power request completed       (Status; 0 = NDIS_STATUS_SUCCESS)
    10302 (resume, carries NetLuidIndex only) is not used for acceptance.

    PASS requires, for ONE adapter, ALL of:
      1. Phase A and Phase C device checks OK; evidence complete and consistent
      2. Phase timestamps present, UTC, strictly ordered
      3. Lifecycle counters of that adapter increased between Phase A and C
         (none decreased; Phase A/C adapter sets identical)
      4. Adapter identity resolved: its VID/DID maps to exactly ONE physical adapter
         in the inventory (VID/DID is only a precondition, never proof)
      5. Events for that adapter's IfGuid (and IfLuid when both known), all inside
         the Phase B window +/- ToleranceSec, in the order
         10308 -> 10309(Status 0) -> 10307(Status 0)
    Anything else that is not a device failure is INCONCLUSIVE.

    Exit-code contract (same numbering as the C executable):
        0  PASS                      qualifying transition correlated (see above)
        1  FAIL                      DEVICE_FAILURE_OBSERVED (cause may be unconfirmed)
        2  SKIP                      BASELINE_UNAVAILABLE (device not accessible in Phase A)
        3  INCONCLUSIVE              reason code in the [IDLE-EVAL] verdict line
        4  EVALUATOR_ERROR           bad usage / unreadable log; no verdict

.PARAMETER LogFile
    Captured stdout of test_device_idle_regression.exe.
.PARAMETER EventFile
    Exported NDIS Diagnostic .evtx (primary), or a .json fixture:
    [{Id, TimeCreated, IfGuid, IfLuid, Status}] with TimeCreated carrying Z or an offset.
.PARAMETER AdapterInventoryFile
    Optional .json fixture [{Name, InterfaceGuid, InterfaceLuid, PnPDeviceID}].
    Default: live Get-NetAdapter -Physical (read-only).
.PARAMETER ExeExitCode
    Real process exit code of the executable, if known; cross-checked against
    the observational_exit evidence line.
.PARAMETER ToleranceSec
    Clock tolerance applied to both edges of the Phase B window (default 2).
.PARAMETER ResultFile
    Optional JSON output keeping observational_exit and the evaluator verdict
    as separate fields.
#>
[CmdletBinding()]
param(
    [string]$LogFile,
    [string]$EventFile,
    [string]$AdapterInventoryFile,
    [int]$ExeExitCode = -1,
    [double]$ToleranceSec = 2.0,
    [string]$ResultFile
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:Inv = [Globalization.CultureInfo]::InvariantCulture
$script:PowerEventIds = @(10306, 10307, 10308, 10309)
$script:StampNames = @('phaseA_start','phaseA_end','phaseB_start','phaseB_end','phaseC_start','phaseC_end')

# --------------------------------------------------------------------------- helpers

# UTC DateTime from an ISO-8601 string that carries 'Z' or an explicit offset; $null otherwise.
function ConvertTo-UtcDateTime {
    param([string]$Text)
    if ([string]::IsNullOrWhiteSpace($Text)) { return $null }
    if ($Text.Trim() -notmatch '(Z|[+-]\d\d:?\d\d)$') { return $null }   # ambiguous local time
    $o = [DateTimeOffset]::MinValue
    $styles = [Globalization.DateTimeStyles]::AssumeUniversal -bor [Globalization.DateTimeStyles]::AdjustToUniversal
    if (-not [DateTimeOffset]::TryParse($Text.Trim(), $script:Inv, $styles, [ref]$o)) { return $null }
    return $o.UtcDateTime
}

function ConvertTo-UInt64Flexible {
    param($Value)
    if ($null -eq $Value) { return $null }
    $s = ([string]$Value).Trim()
    if ($s -eq '') { return $null }
    try {
        if ($s -match '^0[xX]([0-9a-fA-F]+)$') { return [Convert]::ToUInt64($Matches[1], 16) }
        return [UInt64]::Parse($s, $script:Inv)
    } catch { return $null }
}

function ConvertTo-GuidKey {
    param($Value)
    if ($null -eq $Value) { return $null }
    $s = ([string]$Value).Trim().Trim('{', '}').ToLowerInvariant()
    if ($s -eq '') { return $null }
    return $s
}

# --------------------------------------------------------------------------- evidence parsing

function Read-IdleEvidence {
    param([string[]]$Lines)
    $ev = [ordered]@{
        Stamps  = @{}
        Phases  = @{}
        Adapters = @{ A = @(); C = @() }
        ObsExit = $null
        Errors  = New-Object System.Collections.ArrayList
    }
    foreach ($line in $Lines) {
        if ($line -notmatch '^\s*\[EVIDENCE\]\s+(.*?)\s*$') { continue }
        $body = $Matches[1]
        $kv = @{}
        foreach ($m in [regex]::Matches($body, '(\w+)=(\S+)')) { $kv[$m.Groups[1].Value] = $m.Groups[2].Value }

        if ($body -match '^stamp\s') {
            $name = $kv['event']; $utc = ConvertTo-UtcDateTime $kv['utc']
            if (-not $name -or $null -eq $utc) { [void]$ev.Errors.Add("bad stamp record: $body"); continue }
            if ($ev.Stamps.ContainsKey($name)) { [void]$ev.Errors.Add("duplicate stamp '$name'"); continue }
            $ev.Stamps[$name] = $utc
        }
        elseif ($body -match '^adapter\s') {
            try {
                $rec = [pscustomobject]@{
                    Phase = $kv['phase']; Index = [UInt32]::Parse($kv['idx'], $script:Inv)
                    Vid = [Convert]::ToUInt16($kv['vid'], 16); Did = [Convert]::ToUInt16($kv['did'], 16)
                    Valid = ([int]$kv['valid'] -eq 1)
                    Attach = [UInt64]::Parse($kv['attach'], $script:Inv); Pause = [UInt64]::Parse($kv['pause'], $script:Inv)
                    Restart = [UInt64]::Parse($kv['restart'], $script:Inv); Detach = [UInt64]::Parse($kv['detach'], $script:Inv)
                    Gen = [UInt64]::Parse($kv['gen'], $script:Inv)
                }
                if ($rec.Phase -ne 'A' -and $rec.Phase -ne 'C') { throw "phase '$($rec.Phase)'" }
                $ev.Adapters[$rec.Phase] += $rec
            } catch { [void]$ev.Errors.Add("bad adapter record ($($_.Exception.Message)): $body") }
        }
        elseif ($body -match '^phase=') {
            $label = $kv['phase']
            if ($ev.Phases.ContainsKey($label)) { [void]$ev.Errors.Add("duplicate phase record '$label'"); continue }
            $cnt = 0
            if (-not [int]::TryParse([string]$kv['adapters'], [ref]$cnt) -or -not $kv.ContainsKey('device_ok')) {
                [void]$ev.Errors.Add("bad phase record: $body"); continue
            }
            $ev.Phases[$label] = [pscustomobject]@{
                DeviceOk = ($kv['device_ok'] -eq '1'); Win32 = $kv['win32_err']; Adapters = $cnt }
        }
        elseif ($body -match '^observational_exit=') {
            $v = 0
            if ([int]::TryParse($kv['observational_exit'], [ref]$v)) { $ev.ObsExit = $v }
            else { [void]$ev.Errors.Add("bad observational_exit: $body") }
        }
    }
    return $ev
}

# --------------------------------------------------------------------------- NDIS events

# Template field order of events 10306..10309 (from the provider manifest); used only when
# the rendered XML lacks Data/@Name.
$script:NdisFieldOrder = @('IfGuid', 'IfIndex', 'IfLuid', 'Status')

function ConvertFrom-NdisEventXml {
    param([Parameter(Mandatory)][string]$Xml, [int]$Order = 0)
    $doc = New-Object System.Xml.XmlDocument
    $doc.LoadXml($Xml)
    $ns = New-Object System.Xml.XmlNamespaceManager($doc.NameTable)
    $ns.AddNamespace('e', 'http://schemas.microsoft.com/win/2004/08/events/event')
    $id = [int]$doc.SelectSingleNode('//e:System/e:EventID', $ns).InnerText
    $tc = $doc.SelectSingleNode('//e:System/e:TimeCreated', $ns).GetAttribute('SystemTime')
    $fields = @{}
    $pos = 0
    foreach ($d in $doc.SelectNodes('//e:EventData/e:Data', $ns)) {
        $n = $d.GetAttribute('Name')
        if (-not $n -and $pos -lt $script:NdisFieldOrder.Count) { $n = $script:NdisFieldOrder[$pos] }
        if ($n) { $fields[$n] = $d.InnerText }
        $pos++
    }
    return New-NdisEvent -Id $id -TimeText $tc -Guid $fields['IfGuid'] -Luid $fields['IfLuid'] -Status $fields['Status'] -Order $Order
}

function New-NdisEvent {
    param([int]$Id, [string]$TimeText, $Guid, $Luid, $Status, [int]$Order)
    [pscustomobject]@{
        Id = $Id; TimeUtc = (ConvertTo-UtcDateTime $TimeText); TimeText = $TimeText
        Guid = (ConvertTo-GuidKey $Guid); Luid = (ConvertTo-UInt64Flexible $Luid)
        Status = (ConvertTo-UInt64Flexible $Status); Order = $Order }
}

# Returns @{ Ok; Events; Error }.  Ok=$false means the source was supplied but unreadable.
function Read-NdisEvents {
    param([string]$Path)
    $events = New-Object System.Collections.ArrayList
    try {
        if ($Path -match '\.json$') {
            $i = 0
            $parsed = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json   # PS 5.1: array arrives as ONE object
            foreach ($o in $parsed) {
                $i++
                $opt = { param($name) $p = $o.PSObject.Properties[$name]; if ($p) { $p.Value } else { $null } }
                [void]$events.Add((New-NdisEvent -Id ([int]$o.Id) -TimeText ([string](& $opt 'TimeCreated')) `
                    -Guid (& $opt 'IfGuid') -Luid (& $opt 'IfLuid') -Status (& $opt 'Status') -Order $i))
            }
        } else {
            $i = 0
            $recs = @()
            try {
                $recs = @(Get-WinEvent -FilterHashtable @{ Path = $Path; Id = $script:PowerEventIds } -ErrorAction Stop)
            } catch {
                if ($_.FullyQualifiedErrorId -notmatch 'NoMatchingEventsFound') { throw }
            }
            foreach ($r in ($recs | Sort-Object TimeCreated, RecordId)) {
                $i++
                [void]$events.Add((ConvertFrom-NdisEventXml -Xml $r.ToXml() -Order $i))
            }
        }
    } catch {
        return @{ Ok = $false; Events = @(); Error = $_.Exception.Message }
    }
    return @{ Ok = $true; Events = @($events | Where-Object { $_.Id -in $script:PowerEventIds }); Error = $null }
}

# --------------------------------------------------------------------------- inventory

function Read-AdapterInventory {
    param([string]$Path)
    $rows = @()
    if ($Path) {
        $parsedInv = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
        $rows = @(foreach ($p in $parsedInv) { $p })
    } else {
        $rows = @(Get-NetAdapter -Physical -ErrorAction Stop | ForEach-Object {
            [pscustomobject]@{ Name = $_.Name; InterfaceGuid = $_.InterfaceGuid
                               InterfaceLuid = $_.InterfaceLuid; PnPDeviceID = $_.PnPDeviceID } })
    }
    foreach ($r in $rows) {
        $vid = $null; $did = $null
        if ([string]$r.PnPDeviceID -match 'VEN_([0-9A-Fa-f]{4})&DEV_([0-9A-Fa-f]{4})') {
            $vid = [Convert]::ToUInt16($Matches[1], 16); $did = [Convert]::ToUInt16($Matches[2], 16)
        }
        [pscustomobject]@{ Name = [string]$r.Name; Guid = (ConvertTo-GuidKey $r.InterfaceGuid)
                           Luid = (ConvertTo-UInt64Flexible $r.InterfaceLuid); Vid = $vid; Did = $did }
    }
}

# --------------------------------------------------------------------------- power sequence

# Returns @{ Qualified; Code; Detail }.
function Test-PowerSequence {
    param($Events, [string]$Guid, $Luid, [datetime]$From, [datetime]$To)

    $forGuid = @($Events | Where-Object { $_.Guid -eq $Guid -and ($null -eq $Luid -or $null -eq $_.Luid -or $_.Luid -eq $Luid) })
    if ($forGuid.Count -eq 0) { return @{ Qualified = $false; Code = 'NO_EVENTS_FOR_ADAPTER'; Detail = "no NDIS power events for IfGuid $Guid" } }
    if (@($forGuid | Where-Object { $null -eq $_.TimeUtc }).Count -gt 0) {
        return @{ Qualified = $false; Code = 'EVENT_TIMESTAMP_INVALID'; Detail = 'event without unambiguous UTC timestamp' } }

    $sel = @($forGuid | Where-Object { $_.TimeUtc -ge $From -and $_.TimeUtc -le $To } | Sort-Object TimeUtc, Order)
    if ($sel.Count -eq 0) {
        return @{ Qualified = $false; Code = 'POWER_EVENTS_OUTSIDE_PHASE_B'; Detail = "$($forGuid.Count) event(s) for adapter, none inside Phase B window" } }

    $lows = @(for ($i = 0; $i -lt $sel.Count; $i++) { if ($sel[$i].Id -eq 10308) { $i } })
    if ($lows.Count -eq 0) {
        return @{ Qualified = $false; Code = 'NO_LOW_POWER_REQUEST'; Detail = 'no event 10308 in Phase B window' } }

    $best = @{ Qualified = $false; Code = 'NO_LOW_POWER_COMPLETION'; Detail = '10308 without later 10309'; Rank = 1 }
    foreach ($li in $lows) {
        $ci = -1
        for ($k = $li + 1; $k -lt $sel.Count; $k++) { if ($sel[$k].Id -eq 10309) { $ci = $k; break } }
        if ($ci -lt 0) { continue }
        if ($null -eq $sel[$ci].Status -or $sel[$ci].Status -ne 0) {
            if ($best.Rank -lt 2) { $best = @{ Qualified = $false; Code = 'LOW_POWER_COMPLETION_FAILED'; Rank = 2
                Detail = "10309 Status=$($sel[$ci].Status) (expected 0)" } }
            continue }
        $wi = -1
        for ($k = $ci + 1; $k -lt $sel.Count; $k++) { if ($sel[$k].Id -eq 10307) { $wi = $k; break } }
        if ($wi -lt 0) {
            if ($best.Rank -lt 3) { $best = @{ Qualified = $false; Code = 'NO_WORKING_POWER_RECOVERY'; Rank = 3
                Detail = '10308 -> 10309(ok) without later 10307' } }
            continue }
        if ($null -eq $sel[$wi].Status -or $sel[$wi].Status -ne 0) {
            if ($best.Rank -lt 4) { $best = @{ Qualified = $false; Code = 'WORKING_POWER_COMPLETION_FAILED'; Rank = 4
                Detail = "10307 Status=$($sel[$wi].Status) (expected 0)" } }
            continue }
        return @{ Qualified = $true; Code = 'QUALIFIED'; Rank = 9
                  Detail = ("10308 {0:o} -> 10309 {1:o} -> 10307 {2:o}" -f $sel[$li].TimeUtc, $sel[$ci].TimeUtc, $sel[$wi].TimeUtc) }
    }
    return $best
}

# --------------------------------------------------------------------------- evaluation

function New-Verdict {
    param([string]$Verdict, [int]$Code, [string]$Reason, $Adapters = @())
    [pscustomobject]@{ Verdict = $Verdict; Code = $Code; Reason = $Reason; Adapters = @($Adapters) }
}
function New-Inconclusive { param([string]$Reason, [string]$Text, $Adapters = @()) New-Verdict "INCONCLUSIVE/$Reason" 3 $Text $Adapters }

function Invoke-IdleEvaluation {
    param($Ev, $PowerEvents, [bool]$EventsSupplied, [string]$EventError, $Inventory, [double]$Tol, [int]$ExeExit)

    if ($Ev.Errors.Count -gt 0) { return New-Inconclusive 'EVIDENCE_INVALID' ($Ev.Errors -join '; ') }
    if (-not $Ev.Phases.ContainsKey('A')) { return New-Inconclusive 'EVIDENCE_INCOMPLETE' 'no Phase A record' }

    if (-not $Ev.Phases['A'].DeviceOk) {
        return New-Verdict 'BASELINE_UNAVAILABLE' 2 'device not accessible in Phase A' }
    if (-not $Ev.Phases.ContainsKey('C')) { return New-Inconclusive 'EVIDENCE_INCOMPLETE' 'no Phase C record (executable aborted?)' }

    if (-not $Ev.Phases['C'].DeviceOk) {
        return New-Verdict 'DEVICE_FAILURE_OBSERVED' 1 ("device inaccessible after Phase B (win32_err=$($Ev.Phases['C'].Win32)); power-management cause UNCONFIRMED") }

    # executable-result consistency (executable must never report PASS)
    if ($null -eq $Ev.ObsExit) { return New-Inconclusive 'EVIDENCE_INCOMPLETE' 'observational_exit record missing' }
    if ($Ev.ObsExit -eq 0) { return New-Inconclusive 'EVIDENCE_INCONSISTENT' 'executable reported exit 0, which is reserved' }
    if ($Ev.ObsExit -ne 3) { return New-Inconclusive 'EVIDENCE_INCONSISTENT' "observational_exit=$($Ev.ObsExit) contradicts Phase C device_ok=1" }
    if ($ExeExit -ge 0 -and $ExeExit -ne $Ev.ObsExit) {
        return New-Inconclusive 'EVIDENCE_INCONSISTENT' "process exit $ExeExit != observational_exit $($Ev.ObsExit)" }

    # timestamps
    foreach ($n in $script:StampNames) {
        if (-not $Ev.Stamps.ContainsKey($n)) { return New-Inconclusive 'TIMESTAMP_INVALID' "missing stamp $n" } }
    for ($i = 1; $i -lt $script:StampNames.Count; $i++) {
        if ($Ev.Stamps[$script:StampNames[$i]] -lt $Ev.Stamps[$script:StampNames[$i - 1]]) {
            return New-Inconclusive 'TIMESTAMP_INVALID' "$($script:StampNames[$i]) precedes $($script:StampNames[$i - 1])" } }
    if ($Ev.Stamps['phaseB_end'] -le $Ev.Stamps['phaseB_start']) {
        return New-Inconclusive 'TIMESTAMP_INVALID' 'Phase B has zero duration' }
    $from = $Ev.Stamps['phaseB_start'].AddSeconds(-$Tol)
    $to   = $Ev.Stamps['phaseB_end'].AddSeconds($Tol)

    # adapter sets
    foreach ($p in 'A', 'C') {
        if (@($Ev.Adapters[$p]).Count -ne $Ev.Phases[$p].Adapters) {
            return New-Inconclusive 'EVIDENCE_INCOMPLETE' "Phase $p declares $($Ev.Phases[$p].Adapters) adapter(s), found $(@($Ev.Adapters[$p]).Count) records" } }
    $keyOf = { param($a) '{0:X4}:{1:X4}:{2}' -f $a.Vid, $a.Did, $a.Index }
    $keysA = @($Ev.Adapters['A'] | ForEach-Object { & $keyOf $_ } | Sort-Object)
    $keysC = @($Ev.Adapters['C'] | ForEach-Object { & $keyOf $_ } | Sort-Object)
    if (($keysA -join ',') -ne ($keysC -join ',')) {
        return New-Inconclusive 'ADAPTER_SET_CHANGED' "Phase A adapters [$($keysA -join ',')] != Phase C [$($keysC -join ',')] (enumeration index is not a stable identity)" }

    # per-adapter lifecycle + identity + power sequence
    $rows = @()
    foreach ($a in $Ev.Adapters['A']) {
        $c = @($Ev.Adapters['C'] | Where-Object { (& $keyOf $_) -eq (& $keyOf $a) })[0]
        $row = [ordered]@{ Index = $a.Index; Vid = ('{0:X4}' -f $a.Vid); Did = ('{0:X4}' -f $a.Did)
                           Lifecycle = ''; Identity = ''; Guid = $null; Power = ''; Detail = '' }
        if (-not $a.Valid -or -not $c.Valid) {
            $row.Lifecycle = 'STATS_UNAVAILABLE'
        }
        elseif ($c.Attach -lt $a.Attach -or $c.Pause -lt $a.Pause -or $c.Restart -lt $a.Restart -or
                $c.Detach -lt $a.Detach -or $c.Gen -lt $a.Gen) {
            $row.Lifecycle = 'COUNTER_DECREASED'
        }
        elseif ($c.Attach -gt $a.Attach -or $c.Pause -gt $a.Pause -or $c.Restart -gt $a.Restart -or
                $c.Detach -gt $a.Detach -or $c.Gen -gt $a.Gen) {
            $row.Lifecycle = 'CONFIRMED'
        } else { $row.Lifecycle = 'UNCHANGED' }
        $rows += [pscustomobject]$row
    }

    $candidates = @($rows | Where-Object { $_.Lifecycle -eq 'CONFIRMED' })
    if ($candidates.Count -eq 0) {
        if (@($rows | Where-Object { $_.Lifecycle -eq 'COUNTER_DECREASED' }).Count -gt 0) {
            return New-Inconclusive 'COUNTER_RESET' 'lifecycle counter decreased (context recreated, statistics reset or mismatched identity)' $rows }
        if (@($rows | Where-Object { $_.Lifecycle -eq 'STATS_UNAVAILABLE' }).Count -gt 0) {
            return New-Inconclusive 'STATS_UNAVAILABLE' 'adapter statistics unavailable in Phase A or C' $rows }
        return New-Inconclusive 'TRANSITION_NOT_OBSERVED' 'no adapter lifecycle counter changed between Phase A and Phase C' $rows
    }

    if (-not $EventsSupplied) {
        foreach ($r in $candidates) { $r.Power = 'NO_EVENT_SOURCE' }
        return New-Inconclusive 'NO_POWER_EVIDENCE' 'lifecycle activity observed but no NDIS Diagnostic event export supplied (channel is disabled by default)' $rows }
    if ($EventError) {
        return New-Inconclusive 'EVENT_SOURCE_UNREADABLE' $EventError $rows }
    if (@($PowerEvents).Count -eq 0) {
        foreach ($r in $candidates) { $r.Power = 'NO_EVENTS' }
        return New-Inconclusive 'NO_POWER_EVIDENCE' 'event export contains no NDIS power events 10306-10309' $rows }

    $qualified = @()
    foreach ($r in $candidates) {
        $vid = [Convert]::ToUInt16($r.Vid, 16); $did = [Convert]::ToUInt16($r.Did, 16)
        $dupEv = @($rows | Where-Object { $_.Vid -eq $r.Vid -and $_.Did -eq $r.Did }).Count
        $inv = @($Inventory | Where-Object { $_.Vid -eq $vid -and $_.Did -eq $did })
        if ($dupEv -gt 1 -or $inv.Count -gt 1) {
            $r.Identity = 'AMBIGUOUS'; $r.Power = 'NOT_EVALUATED'
            $r.Detail = "VID/DID $($r.Vid):$($r.Did) maps to $([Math]::Max($dupEv, $inv.Count)) adapters; cannot tell which one the driver context is" ; continue }
        if ($inv.Count -eq 0 -or -not $inv[0].Guid) {
            $r.Identity = 'UNRESOLVED'; $r.Power = 'NOT_EVALUATED'
            $r.Detail = "no inventory adapter for VID/DID $($r.Vid):$($r.Did)"; continue }
        $r.Identity = 'UNIQUE'; $r.Guid = $inv[0].Guid
        $seq = Test-PowerSequence -Events $PowerEvents -Guid $inv[0].Guid -Luid $inv[0].Luid -From $from -To $to
        $r.Power = $seq.Code; $r.Detail = $seq.Detail
        if ($seq.Qualified) { $qualified += $r }
    }

    if ($qualified.Count -gt 0) {
        $names = ($qualified | ForEach-Object { "idx=$($_.Index) $($_.Vid):$($_.Did) guid=$($_.Guid)" }) -join '; '
        return New-Verdict 'PASS' 0 "qualifying NDIS power transition (10308->10309->10307, Status 0) correlated with lifecycle activity on: $names" $rows
    }
    # most informative non-pass reason: identity problems first, then deepest power stage
    $first = @($candidates | Where-Object { $_.Identity -ne 'UNIQUE' })
    if ($first.Count -eq $candidates.Count) {
        return New-Inconclusive ('IDENTITY_' + $first[0].Identity) $first[0].Detail $rows }
    $rank = @{ NO_EVENTS_FOR_ADAPTER = 0; POWER_EVENTS_OUTSIDE_PHASE_B = 1; NO_LOW_POWER_REQUEST = 1; NO_LOW_POWER_COMPLETION = 2
               LOW_POWER_COMPLETION_FAILED = 3; NO_WORKING_POWER_RECOVERY = 4; WORKING_POWER_COMPLETION_FAILED = 5; EVENT_TIMESTAMP_INVALID = 0 }
    $top = @($candidates | Where-Object { $_.Identity -eq 'UNIQUE' } | Sort-Object { $rank[$_.Power] } -Descending)[0]
    return New-Inconclusive $top.Power $top.Detail $rows
}

# --------------------------------------------------------------------------- main
if ($MyInvocation.InvocationName -eq '.') { return }   # dot-sourced by unit tests: functions only

if (-not $LogFile -or -not (Test-Path -LiteralPath $LogFile)) {
    Write-Host "[IDLE-EVAL] EVALUATOR_ERROR: -LogFile is required and must exist (got '$LogFile')"
    exit 4
}

$ev = Read-IdleEvidence -Lines (Get-Content -LiteralPath $LogFile)

$eventsSupplied = [bool]$EventFile
$powerEvents = @(); $eventError = $null
if ($eventsSupplied) {
    $r = Read-NdisEvents -Path $EventFile
    if ($r.Ok) { $powerEvents = $r.Events } else { $eventError = "cannot read event file '$EventFile': $($r.Error)" }
}

$inventory = @()
try { $inventory = @(Read-AdapterInventory -Path $AdapterInventoryFile) }
catch { Write-Host "[IDLE-EVAL] note: adapter inventory unavailable: $($_.Exception.Message)" }

$res = Invoke-IdleEvaluation -Ev $ev -PowerEvents $powerEvents -EventsSupplied $eventsSupplied -EventError $eventError `
        -Inventory $inventory -Tol $ToleranceSec -ExeExit $ExeExitCode

$obs = if ($null -ne $ev.ObsExit) { $ev.ObsExit } else { 'none' }
Write-Host "[IDLE-EVAL] observational_exit=$obs (C executable result, retained unchanged)"
if ($ExeExitCode -ge 0) { Write-Host "[IDLE-EVAL] process_exit=$ExeExitCode" }
foreach ($a in $res.Adapters) {
    Write-Host ("[IDLE-EVAL] adapter idx={0} vid={1} did={2} lifecycle={3} identity={4} guid={5} power={6} {7}" -f
        $a.Index, $a.Vid, $a.Did, $a.Lifecycle, $a.Identity, $a.Guid, $a.Power, $a.Detail)
}
Write-Host "[IDLE-EVAL] verdict=$($res.Verdict) exit=$($res.Code) reason=$($res.Reason)"

if ($ResultFile) {
    [pscustomobject]@{
        observational_exit = $ev.ObsExit; process_exit = $(if ($ExeExitCode -ge 0) { $ExeExitCode } else { $null })
        evaluator_verdict = $res.Verdict; evaluator_exit = $res.Code; reason = $res.Reason; adapters = $res.Adapters
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $ResultFile -Encoding UTF8
}
exit $res.Code
