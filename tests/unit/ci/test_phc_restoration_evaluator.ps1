# test_phc_restoration_evaluator.ps1
# TEST-PHCEVAL-*: pure PHC/QPC restoration evaluator (tools/lib/PhcRestoration.ps1; design docs/testing/phc-restoration-evaluator.md).
#
# ALL INPUTS HERE ARE SYNTHETIC (deterministic generators): they characterize the evaluator's mathematics and verdict
# rules only.  They are NOT hardware evidence, carry provenance source 'synthetic', and can never satisfy a HIL
# verification requirement.  No numeric tolerance is invented: tolerances in these tests are labelled synthetic test
# fixtures with source 'hardware-characterization' only where the rule under test needs a valid one.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
$total = 0; $passed = 0; $failed = 0
function tc_check { param([string]$n, [bool]$ok, [string]$why)
    $script:total++; if ($ok) { $script:passed++; Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green } else { $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red } }
Write-Host ""; Write-Host "TEST-PHCEVAL: PHC/QPC restoration evaluator (offline, synthetic inputs only)" -ForegroundColor Cyan

$lib = Join-Path $RepoRoot 'tools\lib\PhcRestoration.ps1'
$missing = ''
try { . $lib; if (-not (Get-Command Get-AvbPhcRestorationVerdict -ErrorAction SilentlyContinue)) { throw 'Get-AvbPhcRestorationVerdict missing' } } catch { $missing = $_.Exception.Message }
tc_check 'TEST-PHCEVAL-000-evaluator-present' ($missing -eq '') $missing
if ($missing) { exit 1 }

# Synthetic window: n paired reads 10 ms apart starting at QPC second t0; PHC = phc0 + ratePpbOff-adjusted 1e9 ns/s.
# Each read is bracketed by QPC before/after (latencyTicks apart, 10 MHz QPC); deterministic +/-jitterNs pattern.
function New-Window([double]$T0s, [long]$Phc0, [double]$RatePpb = 0, [int]$N = 50, [int]$LatencyTicks = 300, [double]$JitterNs = 40,
                    [string]$Did = '0x125B', [string]$Identity = '00-11-22-33-44-55', [uint32]$Timinca = 0x18000000, [uint32]$Tsauxc = 0, [string]$Source = 'synthetic') {
    $f = 10000000; $s = @()
    for ($i = 0; $i -lt $N; $i++) {
        $t = $T0s + $i * 0.010
        $qb = [long]([math]::Round($t * $f)); $qa = $qb + $LatencyTicks
        $tm = ($qb + $qa) / 2.0 / $f
        $phc = $Phc0 + [long][math]::Round(($tm - $T0s) * 1e9 * (1 + $RatePpb * 1e-9) + $JitterNs * [math]::Sin($i * 1.7))
        $s += ,([ordered]@{ qpcBefore = $qb; qpcAfter = $qa; phcNs = $phc })
    }
    return [ordered]@{ provenance = [ordered]@{ source = $Source; host = 'SYNTH'; did = $Did; adapterIdentity = $Identity; capturedUtc = '2026-10-21T00:00:00Z'; driverSha256 = ('A' * 64) }
                       qpcFrequency = $f; timinca = $Timinca; tsauxc = $Tsauxc; tsControl = [ordered]@{ rx = 'all'; tx = 'on' }; samples = $s }
}
# The after-window of an undisturbed clock: same phc line continued (offset 0) at a later QPC time.
$phc0 = [long]1729500000000000000          # ~2024 in ns: exceeds 2^53 on purpose (centring required)
function Continue-Phc([double]$T0s, [double]$T1s, [long]$Phc0, [double]$OffsetNs = 0, [double]$RatePpb = 0) { return ($Phc0 + [long][math]::Round(($T1s - $T0s) * 1e9 + $OffsetNs)) }
$tolOk = [ordered]@{ source = 'hardware-characterization'; did = '0x125B'; offsetNs = 5000; rateChangePpb = 200; evidence = 'SYNTHETIC TEST FIXTURE - not a measured tolerance' }

$b   = New-Window 100.0 $phc0
$aOk = New-Window 160.0 (Continue-Phc 100.0 160.0 $phc0)

# 001 no hardware-derived tolerance -> INCONCLUSIVE_RESTORATION, metrics still reported
$r1 = Get-AvbPhcRestorationVerdict -Before $b -After $aOk -Tolerance $null -Testcase 'TC-SYNTH-001'
tc_check 'TEST-PHCEVAL-001-no-hardware-tolerance-is-inconclusive-never-accepted' `
    ($r1.Verdict -eq 'INCONCLUSIVE_RESTORATION' -and ($r1.Reasons -join ' ') -match 'no hardware-derived tolerance' -and [math]::Abs($r1.Metrics.offsetNs) -lt 200) ("{0}: {1} offset={2}" -f $r1.Verdict, ($r1.Reasons -join '; '), $r1.Metrics.offsetNs)

# 002 configuration not restored (the recorded I226 discrepancy, test_ptp_freq_complete 2026-10-20) -> CLEANUP_FAILED
#     regardless of any tolerance; adapter and testcase evidence preserved in the result
$aTim = New-Window 160.0 (Continue-Phc 100.0 160.0 $phc0) -Timinca 0x08000000
$r2a = Get-AvbPhcRestorationVerdict -Before $b -After $aTim -Tolerance $null -Testcase 'test_ptp_freq_complete'
$r2b = Get-AvbPhcRestorationVerdict -Before $b -After $aTim -Tolerance $tolOk -Testcase 'test_ptp_freq_complete'
tc_check 'TEST-PHCEVAL-002-i226-timinca-0x18000000-to-0x08000000-is-cleanup-failed-with-evidence' `
    ($r2a.Verdict -eq 'CLEANUP_FAILED' -and $r2b.Verdict -eq 'CLEANUP_FAILED' -and ($r2a.Reasons -join ' ') -match 'TIMINCA 0x18000000 -> 0x08000000' -and
     $r2a.Adapter.did -eq '0x125B' -and $r2a.Testcase -eq 'test_ptp_freq_complete') ("{0}/{1}: {2}" -f $r2a.Verdict, $r2b.Verdict, ($r2a.Reasons -join '; '))

# 003 valid (labelled) tolerance, offset and rate inside it including uncertainty -> RESTORATION_VERIFIED.
#     Short read bracket (10 ticks = 1 us): the half-bracket latency is counted as an uncertainty BOUND (conservative;
#     with the 30 us bracket of the other windows a 5 us tolerance is honestly not verifiable - see 001/005).
$b3 = New-Window 100.0 $phc0 -LatencyTicks 10; $a3 = New-Window 160.0 (Continue-Phc 100.0 160.0 $phc0) -LatencyTicks 10
$tol3 = [ordered]@{ source = 'hardware-characterization'; did = '0x125B'; offsetNs = 10000; rateChangePpb = 200; evidence = 'SYNTHETIC TEST FIXTURE - not a measured tolerance' }
$r3 = Get-AvbPhcRestorationVerdict -Before $b3 -After $a3 -Tolerance $tol3 -Testcase 'TC-SYNTH-003'
tc_check 'TEST-PHCEVAL-003-inside-tolerance-including-uncertainty-is-verified' ($r3.Verdict -eq 'RESTORATION_VERIFIED') ("{0}: {1}" -f $r3.Verdict, ($r3.Reasons -join '; '))

# 004 offset clearly outside the tolerance -> CLEANUP_FAILED
$aOff = New-Window 160.0 (Continue-Phc 100.0 160.0 $phc0 50000000)       # +50 ms (UT-CORR-005-like restore, synthetic model)
$r4 = Get-AvbPhcRestorationVerdict -Before $b -After $aOff -Tolerance $tolOk -Testcase 'UT-CORR-005'
$r4n = Get-AvbPhcRestorationVerdict -Before $b -After $aOff -Tolerance $null -Testcase 'UT-CORR-005'
tc_check 'TEST-PHCEVAL-004-offset-outside-tolerance-is-cleanup-failed-and-without-tolerance-inconclusive' `
    ($r4.Verdict -eq 'CLEANUP_FAILED' -and [math]::Abs($r4.Metrics.offsetNs - 50000000) -lt 1000 -and $r4n.Verdict -eq 'INCONCLUSIVE_RESTORATION') ("{0} offset={1} / {2}" -f $r4.Verdict, $r4.Metrics.offsetNs, $r4n.Verdict)

# 005 |offset| +/- uncertainty straddles the tolerance -> INCONCLUSIVE_RESTORATION (never a coin toss)
$aEdge = New-Window 160.0 (Continue-Phc 100.0 160.0 $phc0 5000) -JitterNs 4000
$r5 = Get-AvbPhcRestorationVerdict -Before $b -After $aEdge -Tolerance $tolOk -Testcase 'TC-SYNTH-005'
tc_check 'TEST-PHCEVAL-005-uncertainty-straddling-tolerance-is-inconclusive' ($r5.Verdict -eq 'INCONCLUSIVE_RESTORATION' -and $r5.Metrics.offsetUncertaintyNs -gt 0) ("{0} offset={1} U={2}" -f $r5.Verdict, $r5.Metrics.offsetNs, $r5.Metrics.offsetUncertaintyNs)

# 006 a tolerance that is not hardware-derived (or for another adapter) is not a tolerance
$tolGuess = [ordered]@{ source = 'datasheet'; did = '0x125B'; offsetNs = 5000; rateChangePpb = 200 }
$tolOther = [ordered]@{ source = 'hardware-characterization'; did = '0x1533'; offsetNs = 5000; rateChangePpb = 200; evidence = 'x' }
$r6a = Get-AvbPhcRestorationVerdict -Before $b -After $aOk -Tolerance $tolGuess -Testcase 'TC-SYNTH-006'
$r6b = Get-AvbPhcRestorationVerdict -Before $b -After $aOk -Tolerance $tolOther -Testcase 'TC-SYNTH-006'
tc_check 'TEST-PHCEVAL-006-non-hardware-or-foreign-tolerance-is-inconclusive' ($r6a.Verdict -eq 'INCONCLUSIVE_RESTORATION' -and $r6b.Verdict -eq 'INCONCLUSIVE_RESTORATION') ("{0} / {1}" -f $r6a.Verdict, $r6b.Verdict)

# 007 clock discontinuity: PHC goes backwards inside a window -> CLEANUP_FAILED (no tolerance needed)
$aBack = New-Window 160.0 (Continue-Phc 100.0 160.0 $phc0); $aBack.samples[20].phcNs = $aBack.samples[19].phcNs - 1000
$r7 = Get-AvbPhcRestorationVerdict -Before $b -After $aBack -Tolerance $null -Testcase 'TC-SYNTH-007'
tc_check 'TEST-PHCEVAL-007-backward-phc-step-is-cleanup-failed' ($r7.Verdict -eq 'CLEANUP_FAILED' -and ($r7.Reasons -join ' ') -match 'not monotonic') ("{0}: {1}" -f $r7.Verdict, ($r7.Reasons -join '; '))

# 008 provenance: invalid BEFORE = FAILED_SETUP; invalid / mismatched AFTER = INCONCLUSIVE_RESTORATION
$bFew = New-Window 100.0 $phc0 -N 2
$aOtherAdapter = New-Window 160.0 (Continue-Phc 100.0 160.0 $phc0) -Identity '66-77-88-99-AA-BB'
$aNoQpc = New-Window 160.0 (Continue-Phc 100.0 160.0 $phc0); $aNoQpc.qpcFrequency = 0
$r8a = Get-AvbPhcRestorationVerdict -Before $bFew -After $aOk -Tolerance $tolOk -Testcase 'TC-SYNTH-008'
$r8b = Get-AvbPhcRestorationVerdict -Before $b -After $aOtherAdapter -Tolerance $tolOk -Testcase 'TC-SYNTH-008'
$r8c = Get-AvbPhcRestorationVerdict -Before $b -After $aNoQpc -Tolerance $tolOk -Testcase 'TC-SYNTH-008'
$r8d = Get-AvbPhcRestorationVerdict -Before $b -After $null -Tolerance $tolOk -Testcase 'TC-SYNTH-008'
tc_check 'TEST-PHCEVAL-008-invalid-or-missing-measurement-provenance-is-never-passing' `
    ($r8a.Verdict -eq 'FAILED_SETUP' -and $r8b.Verdict -eq 'INCONCLUSIVE_RESTORATION' -and $r8c.Verdict -eq 'INCONCLUSIVE_RESTORATION' -and $r8d.Verdict -eq 'INCONCLUSIVE_RESTORATION') `
    ("{0} / {1} / {2} / {3}" -f $r8a.Verdict, $r8b.Verdict, $r8c.Verdict, $r8d.Verdict)

# 009 frequency not restored: rate change outside the tolerance -> CLEANUP_FAILED
$aRate = New-Window 160.0 (Continue-Phc 100.0 160.0 $phc0) -RatePpb 5000
$r9 = Get-AvbPhcRestorationVerdict -Before $b -After $aRate -Tolerance $tolOk -Testcase 'UT-CORR-006'
tc_check 'TEST-PHCEVAL-009-rate-change-outside-tolerance-is-cleanup-failed' ($r9.Verdict -eq 'CLEANUP_FAILED' -and [math]::Abs($r9.Metrics.rateChangePpb - 5000) -lt 50) ("{0} rate={1}" -f $r9.Verdict, $r9.Metrics.rateChangePpb)

# 010 timestamp-control state not restored -> CLEANUP_FAILED
$aTs = New-Window 160.0 (Continue-Phc 100.0 160.0 $phc0); $aTs.tsControl.rx = 'none'
$r10 = Get-AvbPhcRestorationVerdict -Before $b -After $aTs -Tolerance $tolOk -Testcase 'TC-SYNTH-010'
tc_check 'TEST-PHCEVAL-010-timestamp-control-not-restored-is-cleanup-failed' ($r10.Verdict -eq 'CLEANUP_FAILED' -and ($r10.Reasons -join ' ') -match 'tsControl\.rx') ("{0}: {1}" -f $r10.Verdict, ($r10.Reasons -join '; '))

# 011 capability: absent WITH evidence -> SKIP_UNSUPPORTED; claimed absent without evidence -> INCONCLUSIVE_RESTORATION
$r11a = Get-AvbPhcRestorationVerdict -Before $b -After $aOk -Tolerance $tolOk -Testcase 'TC-SYNTH-011' -Capability ([ordered]@{ phc = 'unsupported'; evidence = 'GET_CLOCK_CONFIG STATUS_NOT_SUPPORTED on DID 0x1539 (synthetic)' })
$r11b = Get-AvbPhcRestorationVerdict -Before $b -After $aOk -Tolerance $tolOk -Testcase 'TC-SYNTH-011' -Capability ([ordered]@{ phc = 'unsupported'; evidence = '' })
tc_check 'TEST-PHCEVAL-011-unsupported-only-with-evidence' ($r11a.Verdict -eq 'SKIP_UNSUPPORTED' -and $r11b.Verdict -eq 'INCONCLUSIVE_RESTORATION') ("{0} / {1}" -f $r11a.Verdict, $r11b.Verdict)

# 012 numerics: PHC ~1.7e18 ns (beyond 2^53, ~256 ns double resolution).  Jitter-free windows 1 h apart: any offset or
#     residual left is arithmetic error - centring keeps it below 10 ns (uncentred doubles would show hundreds of ns).
#     (With jitter, the 1 h extrapolation error grows with the slope uncertainty and is covered by U - see 001/005.)
$b0 = New-Window 100.0 $phc0 -JitterNs 0
$aLate = New-Window 3700.0 (Continue-Phc 100.0 3700.0 $phc0) -JitterNs 0
$r12 = Get-AvbPhcRestorationVerdict -Before $b0 -After $aLate -Tolerance $null -Testcase 'TC-SYNTH-012'
tc_check 'TEST-PHCEVAL-012-large-phc-values-centred-no-precision-loss' ([math]::Abs($r12.Metrics.offsetNs) -lt 10 -and $r12.Metrics.residualSigmaAfterNs -lt 10) ("offset after 1h = {0} ns, residual sigma {1} ns" -f $r12.Metrics.offsetNs, $r12.Metrics.residualSigmaAfterNs)

# 013 a synthetic tolerance or synthetic measurements can never produce hardware-verified evidence
tc_check 'TEST-PHCEVAL-013-synthetic-inputs-are-marked-not-hardware-evidence' ($r3.HardwareEvidence -eq $false -and ($r3.Reasons -join ' ') -match 'synthetic') ("hardwareEvidence={0}; {1}" -f $r3.HardwareEvidence, ($r3.Reasons -join '; '))

Write-Host ""; Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-PHCEVAL: restoration evaluator contract violated" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-PHCEVAL: restoration evaluator contract holds (synthetic characterization; not hardware evidence)" -ForegroundColor Green
exit 0
