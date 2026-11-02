# test_hil_safety_record.ps1
# TEST-SAFETYREC-*: machine-wide persistent HIL safety record (tools/lib/HilLock.ps1; Q 2026-10-21).
#
# Contract: one record per machine (%ProgramData%\IntelAvbFilter-hil-safety.json) shared by every process,
# configuration and run (Debug, Release, LocalSuites, CI).  State CLEAR | HALTED | UNAVAILABLE.  Missing, corrupt,
# foreign-host, wrong-schema, inaccessible or internally inconsistent = UNAVAILABLE = fail closed for Init/Mutating.
# Safety events are appended, never removed.  Only an explicit operation clears a halt: machine lock held, record
# HALTED (never UNAVAILABLE), operator named, verification document (RESTORATION_VERIFIED, this host, every pending
# event, measured checks).  Rebaselining needs explicit approval text and marks events REBASELINED - original verdicts
# stay.  A reboot, reinstall, new run or new baseline never clears anything.  Reclassification (Q 2026-10-28) ends only
# events the current policy no longer counts as safety events (cleanup-only verdicts).  Offline: temp files only.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
$total = 0; $passed = 0; $failed = 0
function tc_check { param([string]$n, [bool]$ok, [string]$why)
    $script:total++; if ($ok) { $script:passed++; Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green } else { $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red } }
Write-Host ""; Write-Host "TEST-SAFETYREC: machine-wide HIL safety record (offline, temp files)" -ForegroundColor Cyan

$Tmp = Join-Path $env:TEMP ("safetyrec_{0}" -f (Get-Random)); New-Item -ItemType Directory -Path $Tmp -Force | Out-Null
$missing = ''
try { . (Join-Path $RepoRoot 'tools\lib\HilLock.ps1')
      foreach ($f in 'Get-AvbHilSafetyPath', 'Read-AvbHilSafetyState', 'Initialize-AvbHilSafetyRecord', 'Approve-AvbHilSafetyBaseline', 'Add-AvbHilSafetyEvent', 'Clear-AvbHilSafetyHalt', 'Invoke-AvbHilSafetyRebaseline') {
          if (-not (Get-Command $f -ErrorAction SilentlyContinue)) { throw "$f missing in tools/lib/HilLock.ps1" } } } catch { $missing = $_.Exception.Message }
tc_check 'TEST-SAFETYREC-000-library-functions-present' ($missing -eq '') $missing
if ($missing) { Remove-Item $Tmp -Recurse -Force -ErrorAction SilentlyContinue; exit 1 }

function New-Event([string]$Verdict = 'CLEANUP_FAILED') {
    return [ordered]@{ verdict = $Verdict; scope = 'GLOBAL'
        hardware = [ordered]@{ did = '0x125B'; driverIndex = 0; identityVerified = $false }
        trigger  = [ordered]@{ testcase = 'TC-X-001'; executable = 'test_x.exe'; arguments = '--case X'; suite = 'HardwareMutation'; configuration = 'Debug'; runId = '42'; pid = $PID }
        initialState = [ordered]@{ timinca = '0x18000000' }; lastKnownState = [ordered]@{ timinca = '0x08000000' }; evidence = @('C:\logs\x.log') }
}
function Write-Verification([string]$P, [string[]]$Ids, [string]$Verdict = 'RESTORATION_VERIFIED', [string]$HostName = $env:COMPUTERNAME) {
    ([ordered]@{ schema = 'avb-hil-recovery-verification/1'; host = $HostName; verdict = $Verdict; eventIds = @($Ids)
                 identity = @([ordered]@{ did = '0x125B'; mac = '00-11-22-33-44-55' }); checks = @([ordered]@{ name = 'TIMINCA'; expected = '0x18000000'; observed = '0x18000000'; ok = $true })
                 utc = (Get-Date).ToUniversalTime().AddSeconds(1).ToString('o') } | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $P -Encoding UTF8
}
# Baseline authorization evidence (verified physical inventory + measurements; produced by the read-only
# characterization once it exists - synthetic here).
function Write-Baseline([string]$P, [string]$Verdict = 'BASELINE_VERIFIED', [object[]]$Inventory = @([ordered]@{ did = '0x125B'; mac = '00-11-22-33-44-55'; pciLocation = 'PCIROOT(0)#PCI(1C00)#PCI(0000)' })) {
    ([ordered]@{ schema = 'avb-hil-baseline-authorization/1'; host = $env:COMPUTERNAME; verdict = $Verdict; inventory = @($Inventory)
                 measurements = @([ordered]@{ name = 'TIMINCA'; did = '0x125B'; value = '0x18000000' }); utc = (Get-Date).ToUniversalTime().ToString('o') } | ConvertTo-Json -Depth 6) | Set-Content -LiteralPath $P -Encoding UTF8
}
# Holding the lock is part of the clearing contract.
$lockPath = Join-Path $Tmp 'hil.lock'
$lock = Enter-AvbHilLock -Kind 'local' -Label 'safetyrec test' -Path $lockPath -WaitSeconds 1

# 001-003, 010: unavailable record = fail closed
$p1 = Join-Path $Tmp 'missing.json'
$s1 = Read-AvbHilSafetyState -Path $p1
$p2 = Join-Path $Tmp 'corrupt.json'; '{ not json' | Set-Content -LiteralPath $p2
$s2 = Read-AvbHilSafetyState -Path $p2
$p3 = Join-Path $Tmp 'foreign.json'; $null = Initialize-AvbHilSafetyRecord -Path $p3 -Operator 'Q' -Reason 'test'
$j3 = Get-Content $p3 -Raw | ConvertFrom-Json; $j3.host = 'OTHER-HOST'; ($j3 | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $p3 -Encoding UTF8
$s3 = Read-AvbHilSafetyState -Path $p3
$p10 = Join-Path $Tmp 'adir.json'; New-Item -ItemType Directory -Path $p10 | Out-Null
$s10 = Read-AvbHilSafetyState -Path $p10
tc_check 'TEST-SAFETYREC-001-missing-corrupt-foreign-or-inaccessible-record-is-unavailable-fail-closed' `
    ($s1.State -eq 'UNAVAILABLE' -and $s2.State -eq 'UNAVAILABLE' -and $s3.State -eq 'UNAVAILABLE' -and $s10.State -eq 'UNAVAILABLE' -and
     $s1.Why -match 'missing' -and $s3.Why -match 'OTHER-HOST') ("missing={0}:{1} corrupt={2} foreign={3}:{4} dir={5}" -f $s1.State, $s1.Why, $s2.State, $s3.State, $s3.Why, $s10.State)

# 004: initialization creates the record infrastructure only: UNBASELINED (blocks mutation, asserts nothing about the
# hardware); it never overwrites an existing record.  Q 2026-10-21: initialization must not be a bypass.
$p = Join-Path $Tmp 'record.json'
$i1 = Initialize-AvbHilSafetyRecord -Path $p -Operator 'Q' -Reason 'first HIL safety record on this machine'
$i2 = Initialize-AvbHilSafetyRecord -Path $p -Operator 'Q' -Reason 'again'
$s4 = Read-AvbHilSafetyState -Path $p
$i3 = Initialize-AvbHilSafetyRecord -Path $p2 -Operator 'Q' -Reason 'replace corrupt'
tc_check 'TEST-SAFETYREC-004-initialization-is-unbaselined-never-clear-and-never-overwrites' `
    ($i1.Ok -and -not $i2.Ok -and $s4.State -eq 'UNBASELINED' -and -not $i3.Ok -and ((Get-Content $p2 -Raw) -match 'not json')) ("init1={0} init2={1} state={2} overwriteCorrupt={3}" -f $i1.Ok, $i2.Ok, $s4.State, $i3.Ok)

# 016: only an authorized, evidence-backed initial baseline makes the record CLEAR
$bOk = Join-Path $Tmp 'baseline.json'; Write-Baseline $bOk
$bBad = Join-Path $Tmp 'baseline_bad.json'; Write-Baseline $bBad 'INCONCLUSIVE'
$bNoId = Join-Path $Tmp 'baseline_noid.json'; Write-Baseline $bNoId 'BASELINE_VERIFIED' @([ordered]@{ did = '0x125B' })
$a16 = @(
    (Approve-AvbHilSafetyBaseline -Path $p -Operator 'Q' -Authorization 'x' -LockOwner $null -LockPath $lockPath -BaselineEvidence $bOk),
    (Approve-AvbHilSafetyBaseline -Path $p -Operator 'Q' -Authorization '' -LockOwner $lock.Owner -LockPath $lockPath -BaselineEvidence $bOk),
    (Approve-AvbHilSafetyBaseline -Path $p -Operator 'Q' -Authorization 'x' -LockOwner $lock.Owner -LockPath $lockPath -BaselineEvidence $bBad),
    (Approve-AvbHilSafetyBaseline -Path $p -Operator 'Q' -Authorization 'x' -LockOwner $lock.Owner -LockPath $lockPath -BaselineEvidence $bNoId))
$sRej = (Read-AvbHilSafetyState -Path $p).State
$a16ok = Approve-AvbHilSafetyBaseline -Path $p -Operator 'Q' -Authorization 'initial baseline from read-only characterization, approved by Q' -LockOwner $lock.Owner -LockPath $lockPath -BaselineEvidence $bOk
$s16 = Read-AvbHilSafetyState -Path $p
tc_check 'TEST-SAFETYREC-016-only-an-authorized-verified-baseline-makes-the-record-clear' `
    (@($a16 | Where-Object { $_.Ok }).Count -eq 0 -and $sRej -eq 'UNBASELINED' -and $a16ok.Ok -and $s16.State -eq 'CLEAR') `
    ("rejected-all={0} stateAfterRejects={1} approve={2}:{3} state={4}; reasons: {5}" -f (@($a16 | Where-Object { $_.Ok }).Count -eq 0), $sRej, $a16ok.Ok, $a16ok.Why, $s16.State, (($a16 | ForEach-Object { $_.Why }) -join ' | '))

# 017: clearing a halt on a machine that was never baselined returns it to UNBASELINED, never CLEAR
$pN = Join-Path $Tmp 'never.json'; $null = Initialize-AvbHilSafetyRecord -Path $pN -Operator 'Q' -Reason 't'
$aN = Add-AvbHilSafetyEvent -Path $pN -Event (New-Event 'CLEANUP_FAILED')
$vN = Join-Path $Tmp 'verify_never.json'; Write-Verification $vN @($aN.EventId)
$cN = Clear-AvbHilSafetyHalt -Path $pN -Operator 'Q' -LockOwner $lock.Owner -LockPath $lockPath -VerificationEvidence $vN
tc_check 'TEST-SAFETYREC-017-clear-without-authorized-baseline-returns-to-unbaselined' ($cN.Ok -and (Read-AvbHilSafetyState -Path $pN).State -eq 'UNBASELINED') ("clear={0}:{1} state={2}" -f $cN.Ok, $cN.Why, (Read-AvbHilSafetyState -Path $pN).State)

# 005: a safety event halts with every required field
$a5 = Add-AvbHilSafetyEvent -Path $p -Event (New-Event 'CLEANUP_FAILED')
$s5 = Read-AvbHilSafetyState -Path $p
$e5 = if ($s5.Record) { @($s5.Record.events)[0] } else { $null }
$need = 'eventId', 'utc', 'host', 'scope', 'hardware', 'trigger', 'verdict', 'initialState', 'lastKnownState', 'evidence', 'recovery'
$lack = @(if ($e5) { $need | Where-Object { $null -eq $e5.$_ } } else { $need })
tc_check 'TEST-SAFETYREC-005-safety-event-halts-and-records-every-required-field' `
    ($a5.Ok -and $s5.State -eq 'HALTED' -and $lack.Count -eq 0 -and $e5.recovery.status -eq 'PENDING' -and $e5.scope -eq 'GLOBAL' -and $e5.host -eq $env:COMPUTERNAME) `
    ("state={0} missing fields: {1}" -f $s5.State, ($lack -join ','))

# 006: persistence across processes - a fresh process (new run, new baseline) still sees HALTED
$lib = Join-Path $RepoRoot 'tools\lib\HilLock.ps1'
$o6 = powershell -NoProfile -ExecutionPolicy Bypass -Command ". '$lib'; (Read-AvbHilSafetyState -Path '$p').State" 2>&1 | Out-String
tc_check 'TEST-SAFETYREC-006-halt-persists-across-processes-and-runs' ($o6.Trim() -eq 'HALTED') ("fresh process read: {0}" -f $o6.Trim())

# 007: clearing refused without lock, operator, or valid verification; record stays HALTED
$id = [string]$e5.eventId
$v = Join-Path $Tmp 'verify.json'; Write-Verification $v @($id)
$vBad = Join-Path $Tmp 'verify_bad.json'; Write-Verification $vBad @($id) 'INCONCLUSIVE'
$vNoEv = Join-Path $Tmp 'verify_noev.json'; Write-Verification $vNoEv @('other-event')
$c7 = @(
    (Clear-AvbHilSafetyHalt -Path $p -Operator 'Q' -LockOwner $null -LockPath $lockPath -VerificationEvidence $v),
    (Clear-AvbHilSafetyHalt -Path $p -Operator '' -LockOwner $lock.Owner -LockPath $lockPath -VerificationEvidence $v),
    (Clear-AvbHilSafetyHalt -Path $p -Operator 'Q' -LockOwner $lock.Owner -LockPath $lockPath -VerificationEvidence (Join-Path $Tmp 'absent.json')),
    (Clear-AvbHilSafetyHalt -Path $p -Operator 'Q' -LockOwner $lock.Owner -LockPath $lockPath -VerificationEvidence $vBad),
    (Clear-AvbHilSafetyHalt -Path $p -Operator 'Q' -LockOwner $lock.Owner -LockPath $lockPath -VerificationEvidence $vNoEv),
    (Clear-AvbHilSafetyHalt -Path $p2 -Operator 'Q' -LockOwner $lock.Owner -LockPath $lockPath -VerificationEvidence $v))
$s7 = Read-AvbHilSafetyState -Path $p
tc_check 'TEST-SAFETYREC-007-clear-refused-without-lock-operator-or-verified-evidence-or-on-unavailable-record' `
    (@($c7 | Where-Object { $_.Ok }).Count -eq 0 -and $s7.State -eq 'HALTED') ("accepted: {0}; state={1}; reasons: {2}" -f @($c7 | Where-Object { $_.Ok }).Count, $s7.State, (($c7 | ForEach-Object { $_.Why }) -join ' | '))

# 008: verified clearing -> CLEAR; the event is kept with recovery VERIFIED, operator and evidence
$c8 = Clear-AvbHilSafetyHalt -Path $p -Operator 'Q' -LockOwner $lock.Owner -LockPath $lockPath -VerificationEvidence $v
$s8 = Read-AvbHilSafetyState -Path $p
$e8 = if ($s8.Record) { @($s8.Record.events) | Where-Object { $_.eventId -eq $id } } else { $null }
tc_check 'TEST-SAFETYREC-008-verified-clear-keeps-event-with-operator-and-evidence' `
    ($c8.Ok -and $s8.State -eq 'CLEAR' -and $e8 -and $e8.verdict -eq 'CLEANUP_FAILED' -and $e8.recovery.status -eq 'VERIFIED' -and $e8.recovery.operator -eq 'Q' -and
     $e8.recovery.evidence -eq $v -and @($s8.Record.history | Where-Object { $_.operation -eq 'CLEAR' }).Count -eq 1) ("clear={0}:{1} state={2}" -f $c8.Ok, $c8.Why, $s8.State)

# 009: rebaselining needs explicit approval; events REBASELINED, original verdict unchanged
$null = Add-AvbHilSafetyEvent -Path $p -Event (New-Event 'INCONCLUSIVE_RESTORATION')
$nb = Join-Path $Tmp 'newbaseline.json'; Write-Baseline $nb
$r9a = Invoke-AvbHilSafetyRebaseline -Path $p -Operator 'Q' -Approval '' -LockOwner $lock.Owner -LockPath $lockPath -BaselineEvidence $nb
$r9 = Invoke-AvbHilSafetyRebaseline -Path $p -Operator 'Q' -Approval 'original PHC epoch not reconstructible; approved by Q 2026-10-21' -LockOwner $lock.Owner -LockPath $lockPath -BaselineEvidence $nb
$s9 = Read-AvbHilSafetyState -Path $p
$e9 = @($s9.Record.events) | Where-Object { $_.verdict -eq 'INCONCLUSIVE_RESTORATION' }
tc_check 'TEST-SAFETYREC-009-rebaseline-needs-approval-and-never-rewrites-the-failure' `
    (-not $r9a.Ok -and $r9.Ok -and $s9.State -eq 'CLEAR' -and $e9 -and $e9.recovery.status -eq 'REBASELINED' -and $e9.verdict -eq 'INCONCLUSIVE_RESTORATION' -and
     $e9.recovery.approval -match 'approved by Q') ("noApproval={0} approved={1}:{2} state={3}" -f $r9a.Ok, $r9.Ok, $r9.Why, $s9.State)

# 011: an event on a corrupt record never overwrites it (sidecar) and the state stays UNAVAILABLE
$a11 = Add-AvbHilSafetyEvent -Path $p2 -Event (New-Event 'CLEANUP_FAILED')
$side = @(Get-ChildItem $Tmp -Filter 'corrupt.json.event-*.json').Count
tc_check 'TEST-SAFETYREC-011-event-on-corrupt-record-goes-to-sidecar-record-untouched' `
    (-not $a11.Ok -and $side -eq 1 -and ((Get-Content $p2 -Raw) -match 'not json') -and (Read-AvbHilSafetyState -Path $p2).State -eq 'UNAVAILABLE') ("ok={0} sidecars={1}" -f $a11.Ok, $side)

# 012: an event when the record is missing creates a HALTED record (fail closed, nothing lost)
$p12 = Join-Path $Tmp 'none.json'
$a12 = Add-AvbHilSafetyEvent -Path $p12 -Event (New-Event 'FAILED(TIMEOUT)')
tc_check 'TEST-SAFETYREC-012-event-without-record-creates-halted-record' ($a12.Ok -and (Read-AvbHilSafetyState -Path $p12).State -eq 'HALTED') ("ok={0}" -f $a12.Ok)

# 018-021: policy reconciliation (Q 2026-10-28).  Cleanup outcomes are never machine-safety events; events persisted by
# the obsolete rule (CLEANUP_FAILED, INCONCLUSIVE_RESTORATION) are reclassified as non-blocking cleanup diagnostics,
# chosen by their recorded verdict, never by id.  Original event kept, cleanup outcome stays truthful (never "restored"),
# loss-of-control events (FAILED(TIMEOUT), LOCK_LOST) keep the halt, state recomputed, idempotent, under the machine lock.
if (-not (Get-Command Invoke-AvbHilSafetyReclassification -ErrorAction SilentlyContinue)) {
    tc_check 'TEST-SAFETYREC-018-reclassification-function-present' $false 'Invoke-AvbHilSafetyReclassification missing in tools/lib/HilLock.ps1'
} else {
    $rev = '0123456789abcdef0123456789abcdef01234567'
    $pC = Join-Path $Tmp 'reclass_cleanup.json'
    $null = Initialize-AvbHilSafetyRecord -Path $pC -Operator 'Q' -Reason 't'
    $null = Approve-AvbHilSafetyBaseline -Path $pC -Operator 'Q' -Authorization 'baseline' -LockOwner $lock.Owner -LockPath $lockPath -BaselineEvidence $bOk
    $eInc = Add-AvbHilSafetyEvent -Path $pC -Event (New-Event 'INCONCLUSIVE_RESTORATION')
    $eCln = Add-AvbHilSafetyEvent -Path $pC -Event (New-Event 'CLEANUP_FAILED')
    $before = Get-Content -LiteralPath $pC -Raw
    $orig = @((Get-Content -LiteralPath $pC -Raw | ConvertFrom-Json).events)

    # 018: refused without lock, initiator or source revision, and on an unavailable record; nothing written
    $r18 = @(
        (Invoke-AvbHilSafetyReclassification -Path $pC -LockOwner $null -LockPath $lockPath -SourceRevision $rev -InitiatedBy 'Q'),
        (Invoke-AvbHilSafetyReclassification -Path $pC -LockOwner $lock.Owner -LockPath $lockPath -SourceRevision '' -InitiatedBy 'Q'),
        (Invoke-AvbHilSafetyReclassification -Path $pC -LockOwner $lock.Owner -LockPath $lockPath -SourceRevision $rev -InitiatedBy ''),
        (Invoke-AvbHilSafetyReclassification -Path $p2 -LockOwner $lock.Owner -LockPath $lockPath -SourceRevision $rev -InitiatedBy 'Q'))
    tc_check 'TEST-SAFETYREC-018-reclassification-refused-without-lock-initiator-revision-or-on-unavailable-record' `
        (@($r18 | Where-Object { $_.Ok }).Count -eq 0 -and (Get-Content -LiteralPath $pC -Raw) -eq $before -and ((Get-Content $p2 -Raw) -match 'not json') -and
         (Read-AvbHilSafetyState -Path $pC).State -eq 'HALTED') ("accepted={0} reasons: {1}" -f @($r18 | Where-Object { $_.Ok }).Count, (($r18 | ForEach-Object { $_.Why }) -join ' | '))

    # 019: cleanup-only events -> CLEAR; every original field kept, cleanup outcome truthful, migration audited, pre-image kept
    $r19 = Invoke-AvbHilSafetyReclassification -Path $pC -LockOwner $lock.Owner -LockPath $lockPath -SourceRevision $rev -InitiatedBy 'Q'
    $s19 = Read-AvbHilSafetyState -Path $pC
    $ev19 = @($s19.Record.events)
    $kept = @(foreach ($o in $orig) { $n = $ev19 | Where-Object { $_.eventId -eq $o.eventId }
        if ($n -and $n.utc -eq $o.utc -and $n.verdict -eq $o.verdict -and $n.host -eq $o.host -and $n.scope -eq $o.scope -and
            ($n.trigger | ConvertTo-Json -Depth 6 -Compress) -eq ($o.trigger | ConvertTo-Json -Depth 6 -Compress) -and
            ($n.evidence | ConvertTo-Json -Compress) -eq ($o.evidence | ConvertTo-Json -Compress) -and
            ($n.lastKnownState | ConvertTo-Json -Depth 6 -Compress) -eq ($o.lastKnownState | ConvertTo-Json -Depth 6 -Compress)) { $n } })
    $nInc = $ev19 | Where-Object { $_.eventId -eq $eInc.EventId }; $nCln = $ev19 | Where-Object { $_.eventId -eq $eCln.EventId }
    $h19 = @($s19.Record.history | Where-Object { $_.operation -eq 'RECLASSIFY_CLEANUP_ONLY' })
    $bak = @(Get-ChildItem $Tmp -Filter 'reclass_cleanup.json.pre-reclassify-*.json')
    tc_check 'TEST-SAFETYREC-019-cleanup-only-events-reclassified-not-restored-record-clear-original-and-audit-kept' `
        ($r19.Ok -and $r19.Changed -and $s19.State -eq 'CLEAR' -and $ev19.Count -eq 2 -and $kept.Count -eq 2 -and
         $nInc.recovery.status -eq 'RECLASSIFIED' -and $nInc.recovery.cleanupOutcome -eq 'NOT_VERIFIED' -and $nInc.recovery.restorationVerified -eq $false -and $nInc.recovery.previous.status -eq 'PENDING' -and
         $nCln.recovery.status -eq 'RECLASSIFIED' -and $nCln.recovery.cleanupOutcome -eq 'FAILED' -and $nCln.recovery.restorationVerified -eq $false -and
         $h19.Count -eq 1 -and $h19[0].sourceRevision -eq $rev -and $h19[0].initiatedBy -eq 'Q' -and $h19[0].rationale -and @($h19[0].events).Count -eq 2 -and
         $h19[0].resultingStatus -eq 'CLEAR' -and $bak.Count -eq 1 -and (Get-Content -LiteralPath $bak[0].FullName -Raw) -eq $before) `
        ("ok={0}:{1} changed={2} state={3} kept={4}/2 inc={5}/{6} cln={7}/{8} history={9} backups={10}" -f $r19.Ok, $r19.Why, $r19.Changed, $s19.State, $kept.Count,
         $nInc.recovery.status, $nInc.recovery.cleanupOutcome, $nCln.recovery.status, $nCln.recovery.cleanupOutcome, $h19.Count, $bak.Count)

    # 020: idempotent - a repeated run changes nothing: same bytes, no second history entry, no second pre-image
    $after19 = Get-Content -LiteralPath $pC -Raw
    $r20 = Invoke-AvbHilSafetyReclassification -Path $pC -LockOwner $lock.Owner -LockPath $lockPath -SourceRevision $rev -InitiatedBy 'Q'
    tc_check 'TEST-SAFETYREC-020-reclassification-is-idempotent' `
        ($r20.Ok -and -not $r20.Changed -and (Get-Content -LiteralPath $pC -Raw) -eq $after19 -and @(Get-ChildItem $Tmp -Filter 'reclass_cleanup.json.pre-reclassify-*.json').Count -eq 1) `
        ("ok={0}:{1} changed={2} bytesEqual={3}" -f $r20.Ok, $r20.Why, $r20.Changed, ((Get-Content -LiteralPath $pC -Raw) -eq $after19))

    # 021: loss of controlled execution keeps the halt; only the cleanup-only event is reclassified.  A never-baselined
    # record with only cleanup-only events returns to UNBASELINED, never CLEAR.
    $pM = Join-Path $Tmp 'reclass_mixed.json'
    $null = Initialize-AvbHilSafetyRecord -Path $pM -Operator 'Q' -Reason 't'
    $null = Approve-AvbHilSafetyBaseline -Path $pM -Operator 'Q' -Authorization 'baseline' -LockOwner $lock.Owner -LockPath $lockPath -BaselineEvidence $bOk
    $mC = Add-AvbHilSafetyEvent -Path $pM -Event (New-Event 'INCONCLUSIVE_RESTORATION')
    $mL = Add-AvbHilSafetyEvent -Path $pM -Event (New-Event 'LOCK_LOST')
    $mT = Add-AvbHilSafetyEvent -Path $pM -Event (New-Event 'FAILED(TIMEOUT)')
    $r21 = Invoke-AvbHilSafetyReclassification -Path $pM -LockOwner $lock.Owner -LockPath $lockPath -SourceRevision $rev -InitiatedBy 'Q'
    $s21 = Read-AvbHilSafetyState -Path $pM
    $st21 = @{}; foreach ($e in @($s21.Record.events)) { $st21[$e.eventId] = $e.recovery.status }
    $pU = Join-Path $Tmp 'reclass_unbaselined.json'
    $null = Initialize-AvbHilSafetyRecord -Path $pU -Operator 'Q' -Reason 't'
    $null = Add-AvbHilSafetyEvent -Path $pU -Event (New-Event 'CLEANUP_FAILED')
    $r21u = Invoke-AvbHilSafetyReclassification -Path $pU -LockOwner $lock.Owner -LockPath $lockPath -SourceRevision $rev -InitiatedBy 'Q'
    tc_check 'TEST-SAFETYREC-021-loss-of-control-events-keep-the-halt-state-recomputed-from-remaining-events' `
        ($r21.Ok -and $r21.Changed -and $s21.State -eq 'HALTED' -and $st21[$mC.EventId] -eq 'RECLASSIFIED' -and $st21[$mL.EventId] -eq 'PENDING' -and $st21[$mT.EventId] -eq 'PENDING' -and
         $r21u.Ok -and (Read-AvbHilSafetyState -Path $pU).State -eq 'UNBASELINED') `
        ("mixed={0}:{1} state={2} cleanup={3} lock={4} timeout={5}; unbaselined={6}:{7}" -f $r21.Ok, $r21.Why, $s21.State, $st21[$mC.EventId], $st21[$mL.EventId], $st21[$mT.EventId], $r21u.Ok, (Read-AvbHilSafetyState -Path $pU).State)
}

# 013-016: lock OWNERSHIP of the current execution context (Q 2026-10-21) - never "a lock exists".
# Resolve-AvbHilLockContext: @{ Owned; How; Owner; Why }.  Ancestry injected (OS parent chain in production).
$have = [bool](Get-Command Resolve-AvbHilLockContext -ErrorAction SilentlyContinue)
if (-not $have) { tc_check 'TEST-SAFETYREC-013-lock-context-function-present' $false 'Resolve-AvbHilLockContext missing in tools/lib/HilLock.ps1' }
else {
    $cur = Read-AvbHilLock -Path $lockPath                                   # held by this test process (local, our PID)
    $self   = Resolve-AvbHilLockContext -Path $lockPath -ProcessId $PID -Ancestors @() -GitHub $null
    $child  = Resolve-AvbHilLockContext -Path $lockPath -ProcessId 999999 -Ancestors @(4242, [int]$cur.pid, 1) -GitHub $null
    $alien  = Resolve-AvbHilLockContext -Path $lockPath -ProcessId 999999 -Ancestors @(4242, 1) -GitHub $null
    $none   = Resolve-AvbHilLockContext -Path (Join-Path $Tmp 'nolock.lock') -ProcessId $PID -Ancestors @() -GitHub $null
    tc_check 'TEST-SAFETYREC-013-local-lock-owned-by-self-or-ancestor-only-never-by-discovery' `
        ($self.Owned -and $self.How -eq 'self' -and $child.Owned -and $child.How -eq 'ancestor' -and -not $alien.Owned -and -not $none.Owned -and $none.Why -match 'absent') `
        ("self={0}/{1} child={2}/{3} alien={4}:{5} none={6}:{7}" -f $self.Owned, $self.How, $child.Owned, $child.How, $alien.Owned, $alien.Why, $none.Owned, $none.Why)
    $ciLock = Join-Path $Tmp 'ci.lock'
    ([ordered]@{ kind = 'ci'; label = 'hil Debug'; host = $env:COMPUTERNAME; pid = 1; runId = '77'; runAttempt = '1'; job = 'hil-debug'; acquiredUtc = '2026-10-21T00:00:00Z' } | ConvertTo-Json) | Set-Content -LiteralPath $ciLock
    $ciSame  = Resolve-AvbHilLockContext -Path $ciLock -ProcessId 5 -Ancestors @() -GitHub @{ runId = '77'; runAttempt = '1'; job = 'hil-debug' }
    $ciOther = Resolve-AvbHilLockContext -Path $ciLock -ProcessId 5 -Ancestors @() -GitHub @{ runId = '77'; runAttempt = '1'; job = 'hil-release' }
    $ciLocal = Resolve-AvbHilLockContext -Path $ciLock -ProcessId 1 -Ancestors @() -GitHub $null
    tc_check 'TEST-SAFETYREC-014-ci-lock-owned-only-by-the-same-run-attempt-and-job' `
        ($ciSame.Owned -and $ciSame.How -eq 'ci-job' -and -not $ciOther.Owned -and -not $ciLocal.Owned) ("same={0} otherJob={1} localWithSamePid={2}:{3}" -f $ciSame.Owned, $ciOther.Owned, $ciLocal.Owned, $ciLocal.Why)
    $badLock = Join-Path $Tmp 'bad.lock'; '{ garbage' | Set-Content -LiteralPath $badLock
    $bad = Resolve-AvbHilLockContext -Path $badLock -ProcessId $PID -Ancestors @() -GitHub $null
    tc_check 'TEST-SAFETYREC-015-unverifiable-lock-is-not-owned' (-not $bad.Owned -and $bad.Why -match 'unreadable|cannot be verified') ("owned={0} why={1}" -f $bad.Owned, $bad.Why)
}

$null = Exit-AvbHilLock -Owner $lock.Owner -Path $lockPath
Remove-Item $Tmp -Recurse -Force -ErrorAction SilentlyContinue
Write-Host ""; Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-SAFETYREC: machine safety record contract violated" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-SAFETYREC: machine safety record contract holds" -ForegroundColor Green
exit 0
