# test_evaluate_idle_transition.ps1
# Deterministic tests for tools/test/Evaluate-IdleTransition.ps1 (#328, Approach B).
# Synthetic [EVIDENCE] logs + synthetic NDIS event / adapter-inventory fixtures only.
# No hardware, no driver, no Event Log access.  Runs the real script in a child
# powershell.exe so the exit-code contract itself is under test.
#
# TEST-IDLEEVAL-001  Positive: 10308 -> 10309(0) -> 10307(0) in Phase B for the lifecycle adapter -> PASS (0)
# TEST-IDLEEVAL-002  No event export supplied -> INCONCLUSIVE/NO_POWER_EVIDENCE (3)
# TEST-IDLEEVAL-003  Export without 10307 recovery (partial transition) -> INCONCLUSIVE (3)
# TEST-IDLEEVAL-004  10309 Status != 0 -> INCONCLUSIVE/LOW_POWER_COMPLETION_FAILED (3)
# TEST-IDLEEVAL-005  Events belong to a different adapter than the lifecycle one -> INCONCLUSIVE (3)
# TEST-IDLEEVAL-006  Two physical adapters share VID/DID -> INCONCLUSIVE/IDENTITY_AMBIGUOUS (3)
# TEST-IDLEEVAL-007  Counter decreased -> INCONCLUSIVE/COUNTER_RESET (3), never PASS
# TEST-IDLEEVAL-008  Events outside Phase B window -> INCONCLUSIVE/POWER_EVENTS_OUTSIDE_PHASE_B (3)
# TEST-IDLEEVAL-009  UTC offsets: +02:00 event times equal to Phase B UTC -> PASS (0)
# TEST-IDLEEVAL-010  Event time without Z/offset -> INCONCLUSIVE (3)
# TEST-IDLEEVAL-011  Phase C device_ok=0 -> FAIL / DEVICE_FAILURE_OBSERVED (1), even with perfect events
# TEST-IDLEEVAL-012  Phase A device_ok=0 -> BASELINE_UNAVAILABLE (2)
# TEST-IDLEEVAL-013  Exe exit 0 / process exit mismatch -> INCONCLUSIVE/EVIDENCE_INCONSISTENT (3)
# TEST-IDLEEVAL-014  Adapter enumeration index shifted A->C -> INCONCLUSIVE/ADAPTER_SET_CHANGED (3)
# TEST-IDLEEVAL-015  Unreadable event file -> INCONCLUSIVE/EVENT_SOURCE_UNREADABLE (3)
# TEST-IDLEEVAL-016  ResultFile keeps observational_exit and evaluator verdict separate
# TEST-IDLEEVAL-017  ConvertFrom-NdisEventXml parses manifest-shaped event XML (named + positional)
# TEST-IDLEEVAL-018  win32_err=unknown accepted for failed Phase C -> FAIL (1)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
$Eval      = Join-Path $RepoRoot 'tools\test\Evaluate-IdleTransition.ps1'
$Tmp       = Join-Path ([IO.Path]::GetTempPath()) ("idleeval_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $Tmp | Out-Null

$total = 0; $passed = 0; $failed = 0
function tc_pass { param([string]$n) $script:total++; $script:passed++; Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red }

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  TEST-IDLEEVAL-001..018: Evaluate-IdleTransition.ps1" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

if (-not (Test-Path $Eval)) { tc_fail 'TEST-IDLEEVAL-SETUP' "evaluator not found at $Eval"; exit 1 }

# ---- fixtures -------------------------------------------------------------------
$G226 = '5989ba55-d568-47a4-afbf-b47f27e1b435'
$G219 = 'd44a7e27-72ee-480d-958c-558facf2b105'
$G210 = '9f90590f-e0f0-46b7-a69e-bf0a45dedb6c'
$Inv3 = @(
    @{ Name = 'Ethernet';   InterfaceGuid = "{$G219}"; InterfaceLuid = 1; PnPDeviceID = 'PCI\VEN_8086&DEV_15BB&SUBSYS_085A1028&REV_10\3&1' },
    @{ Name = 'Ethernet 2'; InterfaceGuid = "{$G210}"; InterfaceLuid = 2; PnPDeviceID = 'PCI\VEN_8086&DEV_1533&SUBSYS_00001D1A&REV_03\4&1' },
    @{ Name = 'Ethernet 3'; InterfaceGuid = "{$G226}"; InterfaceLuid = 3; PnPDeviceID = 'PCI\VEN_8086&DEV_125B&SUBSYS_8C5C103C&REV_04\4&2' })

function New-Log {
    param([string]$AOk = '1', [string]$COk = '1', [string]$CErr = '0',
          [string[]]$AAdapters, [string[]]$CAdapters, [string]$Obs = '3',
          [string]$BStart = '2026-10-15T18:00:10.000Z', [string]$BEnd = '2026-10-15T18:05:10.000Z')
    if (-not $AAdapters) { $AAdapters = @('idx=0 vid=8086 did=125B valid=1 attach=3 pause=2 restart=2 detach=1 gen=2') }
    if (-not $CAdapters) { $CAdapters = @('idx=0 vid=8086 did=125B valid=1 attach=3 pause=3 restart=3 detach=2 gen=3') }
    $l = @('TC-LCY-IDLE-001: synthetic',
        '[EVIDENCE] stamp event=phaseA_start utc=2026-10-15T18:00:00.000Z',
        '[EVIDENCE] stamp event=phaseA_end utc=2026-10-15T18:00:05.000Z',
        "[EVIDENCE] phase=A device_ok=$AOk win32_err=0 adapters=$($AAdapters.Count)")
    foreach ($a in $AAdapters) { $l += "[EVIDENCE] adapter phase=A $a" }
    if ($AOk -eq '1') {
        $l += "[EVIDENCE] stamp event=phaseB_start utc=$BStart"
        $l += "[EVIDENCE] stamp event=phaseB_end utc=$BEnd"
        $l += '[EVIDENCE] stamp event=phaseC_start utc=2026-10-15T18:05:11.000Z'
        $l += '[EVIDENCE] stamp event=phaseC_end utc=2026-10-15T18:05:15.000Z'
        $l += "[EVIDENCE] phase=C device_ok=$COk win32_err=$CErr adapters=$($CAdapters.Count)"
        foreach ($a in $CAdapters) { $l += "[EVIDENCE] adapter phase=C $a" }
    }
    $l += "[EVIDENCE] observational_exit=$Obs"
    return $l
}

function New-Ev { param([int]$Id, [string]$T, [string]$Guid = $G226, $Status = $null)
    $o = @{ Id = $Id; TimeCreated = $T; IfGuid = "{$Guid}"; IfLuid = 3 }
    if ($null -ne $Status) { $o.Status = $Status }
    return $o }
$GoodSeq = @(
    (New-Ev 10308 '2026-10-15T18:02:00.000Z'),
    (New-Ev 10309 '2026-10-15T18:02:01.000Z' $G226 '0x0'),
    (New-Ev 10306 '2026-10-15T18:03:00.000Z'),
    (New-Ev 10307 '2026-10-15T18:03:01.000Z' $G226 '0x0'))

$n = 0
function Run-Eval {
    param([string[]]$LogLines, $Events = $null, $Inventory = $Inv3, [string[]]$Extra = @(), [switch]$RawEventFile)
    $script:n++
    $log = Join-Path $Tmp "log$($script:n).txt"; $LogLines | Set-Content -LiteralPath $log -Encoding ASCII
    $inv = Join-Path $Tmp "inv$($script:n).json"; ConvertTo-Json -InputObject @($Inventory) -Depth 4 | Set-Content -LiteralPath $inv -Encoding ASCII
    $psArgs = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $Eval,
              '-LogFile', $log, '-AdapterInventoryFile', $inv)
    if ($null -ne $Events) {
        $ef = Join-Path $Tmp "ev$($script:n).json"
        if ($RawEventFile) { $Events | Set-Content -LiteralPath $ef -Encoding ASCII }
        else { ConvertTo-Json -InputObject @($Events) -Depth 4 | Set-Content -LiteralPath $ef -Encoding ASCII }
        $psArgs += @('-EventFile', $ef)
    }
    $psArgs += $Extra
    $out = & powershell.exe @psArgs 2>&1 | Out-String
    return [pscustomobject]@{ Exit = $LASTEXITCODE; Out = $out }
}
function Check { param([string]$Id, $R, [int]$Code, [string]$Pattern)
    if ($R.Exit -eq $Code -and $R.Out -match $Pattern) { tc_pass "$Id exit=$Code /$Pattern/" }
    else { tc_fail $Id ("expected exit $Code matching /$Pattern/, got exit $($R.Exit):`n" + $R.Out) } }

# ---- tests ----------------------------------------------------------------------
Check 'TEST-IDLEEVAL-001' (Run-Eval (New-Log) $GoodSeq) 0 'verdict=PASS exit=0'
Check 'TEST-IDLEEVAL-002' (Run-Eval (New-Log)) 3 'INCONCLUSIVE/NO_POWER_EVIDENCE'
Check 'TEST-IDLEEVAL-003' (Run-Eval (New-Log) @($GoodSeq[0], $GoodSeq[1])) 3 'INCONCLUSIVE/NO_WORKING_POWER_RECOVERY'
$badLow = @((New-Ev 10308 '2026-10-15T18:02:00Z'), (New-Ev 10309 '2026-10-15T18:02:01Z' $G226 '0xC0000001'),
            (New-Ev 10307 '2026-10-15T18:03:01Z' $G226 '0x0'))
Check 'TEST-IDLEEVAL-004' (Run-Eval (New-Log) $badLow) 3 'INCONCLUSIVE/LOW_POWER_COMPLETION_FAILED'
$badWork = @($GoodSeq[0], $GoodSeq[1], (New-Ev 10307 '2026-10-15T18:03:01Z' $G226 '0x1'))
Check 'TEST-IDLEEVAL-004b' (Run-Eval (New-Log) $badWork) 3 'INCONCLUSIVE/WORKING_POWER_COMPLETION_FAILED'

# wrong adapter: perfect sequence, but for the I219 GUID while the I226 changed
$wrong = $GoodSeq | ForEach-Object { New-Ev $_.Id $_.TimeCreated $G219 $(if ($_.ContainsKey('Status')) { $_.Status } else { $null }) }
Check 'TEST-IDLEEVAL-005' (Run-Eval (New-Log) $wrong) 3 'INCONCLUSIVE/NO_EVENTS_FOR_ADAPTER'

# ambiguous identity: two inventory adapters with VID/DID 8086:125B
$InvDup = $Inv3 + @(@{ Name = 'Ethernet 4'; InterfaceGuid = '{11111111-2222-3333-4444-555555555555}'; InterfaceLuid = 9; PnPDeviceID = 'PCI\VEN_8086&DEV_125B&SUBSYS_8C5C103C&REV_04\4&9' })
Check 'TEST-IDLEEVAL-006' (Run-Eval (New-Log) $GoodSeq $InvDup) 3 'INCONCLUSIVE/IDENTITY_AMBIGUOUS'

# counter reset
$dec = @('idx=0 vid=8086 did=125B valid=1 attach=1 pause=0 restart=0 detach=0 gen=0')
Check 'TEST-IDLEEVAL-007' (Run-Eval (New-Log -CAdapters $dec) $GoodSeq) 3 'INCONCLUSIVE/COUNTER_RESET'

# window
$late = @((New-Ev 10308 '2026-10-15T19:02:00Z'), (New-Ev 10309 '2026-10-15T19:02:01Z' $G226 '0x0'),
          (New-Ev 10307 '2026-10-15T19:03:01Z' $G226 '0x0'))
Check 'TEST-IDLEEVAL-008' (Run-Eval (New-Log) $late) 3 'INCONCLUSIVE/POWER_EVENTS_OUTSIDE_PHASE_B'

# UTC offset: same instants expressed as +02:00 local
$off = @((New-Ev 10308 '2026-10-15T20:02:00.000+02:00'), (New-Ev 10309 '2026-10-15T20:02:01.000+02:00' $G226 '0x0'),
         (New-Ev 10307 '2026-10-15T20:03:01.000+02:00' $G226 '0x0'))
Check 'TEST-IDLEEVAL-009' (Run-Eval (New-Log) $off) 0 'verdict=PASS exit=0'
# same wall clock but WITHOUT offset must not be guessed
$naive = @((New-Ev 10308 '2026-10-15T18:02:00'), (New-Ev 10309 '2026-10-15T18:02:01' $G226 '0x0'), (New-Ev 10307 '2026-10-15T18:03:01' $G226 '0x0'))
Check 'TEST-IDLEEVAL-010' (Run-Eval (New-Log) $naive) 3 'INCONCLUSIVE/EVENT_TIMESTAMP_INVALID'

# device failure dominates
Check 'TEST-IDLEEVAL-011' (Run-Eval (New-Log -COk '0' -CErr '2') $GoodSeq) 1 'verdict=DEVICE_FAILURE_OBSERVED exit=1'
Check 'TEST-IDLEEVAL-012' (Run-Eval (New-Log -AOk '0' -Obs '2')) 2 'verdict=BASELINE_UNAVAILABLE exit=2'

# consistency
Check 'TEST-IDLEEVAL-013a' (Run-Eval (New-Log -Obs '0') $GoodSeq) 3 'INCONCLUSIVE/EVIDENCE_INCONSISTENT'
Check 'TEST-IDLEEVAL-013b' (Run-Eval (New-Log) $GoodSeq -Extra @('-ExeExitCode', '1')) 3 'INCONCLUSIVE/EVIDENCE_INCONSISTENT'

# index shift
$shift = @('idx=1 vid=8086 did=125B valid=1 attach=3 pause=3 restart=3 detach=2 gen=3')
Check 'TEST-IDLEEVAL-014' (Run-Eval (New-Log -CAdapters $shift) $GoodSeq) 3 'INCONCLUSIVE/ADAPTER_SET_CHANGED'

# unreadable events
Check 'TEST-IDLEEVAL-015' (Run-Eval (New-Log) @('{ not json') -RawEventFile) 3 'INCONCLUSIVE/EVENT_SOURCE_UNREADABLE'

# result file keeps both records
$rf = Join-Path $Tmp 'result.json'
$r16 = Run-Eval (New-Log) $GoodSeq -Extra @('-ResultFile', $rf)
$j = if (Test-Path $rf) { Get-Content $rf -Raw | ConvertFrom-Json } else { $null }
if ($r16.Exit -eq 0 -and $j -and $j.observational_exit -eq 3 -and $j.evaluator_verdict -eq 'PASS' -and $j.evaluator_exit -eq 0) {
    tc_pass 'TEST-IDLEEVAL-016 observational_exit=3 and evaluator_verdict=PASS recorded separately'
} else { tc_fail 'TEST-IDLEEVAL-016' "result file missing or merged: $($r16.Out)" }

# XML converter (dot-sourced: functions only)
. $Eval
$xmlNamed = '<Event xmlns="http://schemas.microsoft.com/win/2004/08/events/event"><System><EventID>10309</EventID><TimeCreated SystemTime="2026-10-15T18:02:01.5000000Z"/></System><EventData><Data Name="IfGuid">{5989BA55-D568-47A4-AFBF-B47F27E1B435}</Data><Data Name="IfIndex">7</Data><Data Name="IfLuid">0x3</Data><Data Name="Status">0x0</Data></EventData></Event>'
$xmlPos = $xmlNamed -replace ' Name="[A-Za-z]+"', ''
$e1 = ConvertFrom-NdisEventXml -Xml $xmlNamed
$e2 = ConvertFrom-NdisEventXml -Xml $xmlPos
$okX = ($e1.Id -eq 10309 -and $e1.Guid -eq $G226 -and $e1.Luid -eq 3 -and $e1.Status -eq 0 -and $e1.TimeUtc.Kind -eq 'Utc' -and $e1.TimeUtc.Millisecond -eq 500) -and
       ($e2.Guid -eq $G226 -and $e2.Luid -eq 3 -and $e2.Status -eq 0)
if ($okX) { tc_pass 'TEST-IDLEEVAL-017 event XML parsed (named and positional fields)' } else { tc_fail 'TEST-IDLEEVAL-017' ($e1 | Out-String) }

# unknown win32 error on failed Phase C
Check 'TEST-IDLEEVAL-018' (Run-Eval (New-Log -COk '0' -CErr 'unknown')) 1 'win32_err=unknown'

Remove-Item -LiteralPath $Tmp -Recurse -Force -ErrorAction SilentlyContinue
Write-Host ""
Write-Host ("Results: {0} passed, {1} failed (of {2})" -f $passed, $failed, $total)
if ($failed -gt 0) { exit 1 }
exit 0
