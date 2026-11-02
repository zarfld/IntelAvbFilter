# PhcRestoration.ps1 - pure PHC/QPC restoration evaluator (dot-source; no I/O, no hardware access).
# Design: docs/testing/phc-restoration-evaluator.md.  Tests: tests/unit/ci/test_phc_restoration_evaluator.ps1.
#
# Two separate questions:
#   1. CONFIGURATION restored?  TIMINCA, TSAUXC, timestamp control - exact equality of what was read back.  A difference
#      is CLEANUP_FAILED whatever any tolerance says (the recorded I226 TIMINCA 0x18000000 -> 0x08000000 case).
#   2. RUNNING CLOCK restored?  Restoring the numeric PHC value does not restore a continuously advancing clock.  The
#      PHC-vs-QPC relationship is fitted per window (least squares PHC = a + b*t over paired reads, t = QPC midpoint of
#      the read bracket / QPC frequency; QPC is monotonic, not wall-clock time); the BEFORE fit is extrapolated to the
#      AFTER window: offset change and rate change, each with an uncertainty U (fit standard errors + read-bracket
#      latency, coverage factor 2).  Verdict against a HARDWARE-DERIVED tolerance only:
#        |x| + U <= tol for offset AND rate -> RESTORATION_VERIFIED
#        |x| - U >  tol for offset OR rate  -> CLEANUP_FAILED
#        otherwise / no valid tolerance     -> INCONCLUSIVE_RESTORATION      (no threshold is ever invented here)
#   PHC going backwards inside a window is a discontinuity: CLEANUP_FAILED (after) / FAILED_SETUP (before).
#   Invalid BEFORE measurement -> FAILED_SETUP; invalid or mismatched AFTER measurement -> INCONCLUSIVE_RESTORATION.
#   Capability 'unsupported' only WITH evidence -> SKIP_UNSUPPORTED (test-verdict-contract.md), else INCONCLUSIVE.
# Numerics: PHC values (~1.7e18 ns) exceed double precision (2^53), so every value is centred with 64-bit integer
# differences before conversion.  The evaluator judges RESTORATION, not clock function: an already malfunctioning PHC
# (e.g. the slow I219 observation) is judged by the functional tests, never accepted because its malfunction persisted.

function Test-AvbPhcWindow {
    param($W, [string]$Name)
    if ($null -eq $W) { return "$Name measurement missing" }
    foreach ($k in 'provenance', 'qpcFrequency', 'samples') { if ($null -eq $W[$k] -and -not ($W.PSObject.Properties[$k])) { return "$Name measurement lacks '$k'" } }
    $p = $W.provenance
    if (-not $p -or -not $p.did -or -not $p.adapterIdentity -or $p.source -notin @('hardware', 'synthetic')) { return "$Name provenance incomplete (did, adapterIdentity, source hardware|synthetic required)" }
    if (-not ([double]$W.qpcFrequency -gt 0)) { return "$Name QPC frequency missing or zero" }
    $s = @($W.samples)
    if ($s.Count -lt 3) { return "$Name has $($s.Count) paired read(s); at least 3 are required for a fit" }
    for ($i = 0; $i -lt $s.Count; $i++) {
        if ([long]$s[$i].qpcAfter -lt [long]$s[$i].qpcBefore) { return "$Name read $i has QPC after < before" }
        if ($i -gt 0 -and [long]$s[$i].qpcBefore -le [long]$s[$i - 1].qpcBefore) { return "$Name QPC not increasing at read $i" }
    }
    return ''
}

# Least-squares fit of one window relative to (QpcRef, PhcRef): @{ N; A; B; S; Sxx; Xbar; SeB; LatNs; Monotonic; BackAt }
function Get-AvbPhcFit {
    param($W, [long]$QpcRef, [long]$PhcRef)
    $f = [double]$W.qpcFrequency; $xs = @(); $ys = @(); $lat = 0.0; $back = -1
    $s = @($W.samples)
    for ($i = 0; $i -lt $s.Count; $i++) {
        $qm = ([double]([long]$s[$i].qpcBefore - $QpcRef) + [double]([long]$s[$i].qpcAfter - $QpcRef)) / 2.0
        $xs += $qm / $f
        $ys += [double]([long]$s[$i].phcNs - $PhcRef)
        $lat += ([double]([long]$s[$i].qpcAfter - [long]$s[$i].qpcBefore) / $f) * 1e9 / 2.0
        if ($i -gt 0 -and $back -lt 0 -and [long]$s[$i].phcNs -le [long]$s[$i - 1].phcNs) { $back = $i }
    }
    $n = $xs.Count; $xb = ($xs | Measure-Object -Average).Average; $yb = ($ys | Measure-Object -Average).Average
    $sxx = 0.0; $sxy = 0.0
    for ($i = 0; $i -lt $n; $i++) { $sxx += ($xs[$i] - $xb) * ($xs[$i] - $xb); $sxy += ($xs[$i] - $xb) * ($ys[$i] - $yb) }
    $b = if ($sxx -gt 0) { $sxy / $sxx } else { 0.0 }
    $a = $yb - $b * $xb
    $ssr = 0.0; for ($i = 0; $i -lt $n; $i++) { $r = $ys[$i] - ($a + $b * $xs[$i]); $ssr += $r * $r }
    $sd = if ($n -gt 2) { [math]::Sqrt($ssr / ($n - 2)) } else { 0.0 }
    return @{ N = $n; A = $a; B = $b; S = $sd; Sxx = $sxx; Xbar = $xb; Ybar = $yb; SeB = $(if ($sxx -gt 0) { $sd / [math]::Sqrt($sxx) } else { [double]::PositiveInfinity })
              LatNs = $lat / $n; Monotonic = ($back -lt 0); BackAt = $back }
}

function Test-AvbPhcTolerance {
    param($Tol, [string]$Did, [bool]$HardwareMeasurements)
    if ($null -eq $Tol) { return 'no hardware-derived tolerance available' }
    if ($Tol.source -ne 'hardware-characterization') { return "tolerance source '$($Tol.source)' is not hardware characterization" }
    if ($Tol.did -ne $Did) { return "tolerance is for DID $($Tol.did), not $Did" }
    if (-not ([double]$Tol.offsetNs -gt 0) -or -not ([double]$Tol.rateChangePpb -gt 0)) { return 'tolerance lacks positive offsetNs / rateChangePpb' }
    if (-not $Tol.evidence) { return 'tolerance has no evidence reference' }
    if ($HardwareMeasurements -and ([string]$Tol.evidence) -match '(?i)synthetic') { return 'a synthetic tolerance cannot judge hardware measurements' }
    return ''
}

# Result object; non-hardware measurements are always labelled as such.
function New-AvbPhcResult {
    param([object[]]$Reasons, $Metrics, $Adapter, [string]$Testcase, [bool]$Hw, [string]$Verdict)
    $r = @($Reasons)
    if (-not $Hw) { $r += 'synthetic or non-hardware measurements: not hardware evidence' }
    return @{ Verdict = $Verdict; Reasons = $r; Metrics = $Metrics; Adapter = $Adapter; Testcase = $Testcase; HardwareEvidence = $Hw }
}

# Get-AvbPhcRestorationVerdict -Before <window> -After <window> -Tolerance <tol|$null> -Testcase <id> [-Capability @{ phc; evidence }]
# window: @{ provenance = @{ source; host; did; adapterIdentity; capturedUtc; driverSha256 }; qpcFrequency; timinca; tsauxc;
#            tsControl = @{...}; samples = @( @{ qpcBefore; qpcAfter; phcNs } ) }
# -> @{ Verdict; Reasons; Metrics; Adapter; Testcase; HardwareEvidence }
function Get-AvbPhcRestorationVerdict {
    param($Before, $After, $Tolerance, [string]$Testcase, $Capability = $null)
    $reasons = @(); $metrics = [ordered]@{}
    $adapter = if ($Before -and $Before.provenance) { [ordered]@{ did = $Before.provenance.did; identity = $Before.provenance.adapterIdentity } } else { $null }
    $hw = [bool]($Before -and $After -and $Before.provenance.source -eq 'hardware' -and $After.provenance.source -eq 'hardware')

    if ($Capability -and $Capability.phc -eq 'unsupported') {
        if ($Capability.evidence) { $reasons += "PHC capability absent: $($Capability.evidence)"; return (New-AvbPhcResult $reasons $metrics $adapter $Testcase $hw 'SKIP_UNSUPPORTED') }
        $reasons += 'PHC capability claimed absent without evidence'; return (New-AvbPhcResult $reasons $metrics $adapter $Testcase $hw 'INCONCLUSIVE_RESTORATION')
    }
    $eb = Test-AvbPhcWindow $Before 'before'
    if ($eb) { $reasons += $eb; return (New-AvbPhcResult $reasons $metrics $adapter $Testcase $hw 'FAILED_SETUP') }
    $ea = Test-AvbPhcWindow $After 'after'
    if ($ea) { $reasons += $ea; return (New-AvbPhcResult $reasons $metrics $adapter $Testcase $hw 'INCONCLUSIVE_RESTORATION') }
    if ($After.provenance.did -ne $Before.provenance.did -or $After.provenance.adapterIdentity -ne $Before.provenance.adapterIdentity) {
        $reasons += ("after measurement is from another adapter ({0}/{1}, before {2}/{3})" -f $After.provenance.did, $After.provenance.adapterIdentity, $Before.provenance.did, $Before.provenance.adapterIdentity)
        return (New-AvbPhcResult $reasons $metrics $adapter $Testcase $hw 'INCONCLUSIVE_RESTORATION')
    }

    # 1. configuration (exact)
    $cfgFail = $false
    foreach ($k in 'timinca', 'tsauxc') {
        if ([uint32]$Before[$k] -ne [uint32]$After[$k]) { $cfgFail = $true; $reasons += ("{0} 0x{1:X8} -> 0x{2:X8} (configuration not restored)" -f $k.ToUpperInvariant(), [uint32]$Before[$k], [uint32]$After[$k]) }
    }
    if ($Before.tsControl -or $After.tsControl) {
        $keys = @(@($Before.tsControl.Keys) + @($After.tsControl.Keys) | Sort-Object -Unique)
        foreach ($k in $keys) { if ([string]$Before.tsControl[$k] -ne [string]$After.tsControl[$k]) { $cfgFail = $true; $reasons += ("tsControl.{0} '{1}' -> '{2}' (timestamp control not restored)" -f $k, $Before.tsControl[$k], $After.tsControl[$k]) } }
    }

    # 2. running clock (centred fits)
    $qRef = [long]@($Before.samples)[0].qpcBefore; $pRef = [long]@($Before.samples)[0].phcNs
    $fb = Get-AvbPhcFit $Before $qRef $pRef; $fa = Get-AvbPhcFit $After $qRef $pRef
    if (-not $fb.Monotonic) { $reasons += "before window: PHC not monotonic at read $($fb.BackAt) (baseline clock discontinuous)"; return (New-AvbPhcResult $reasons $metrics $adapter $Testcase $hw 'FAILED_SETUP') }
    $tStar = $fa.Xbar
    $pred = $fb.A + $fb.B * $tStar
    $offset = $fa.Ybar - $pred
    $sePred = $fb.S * [math]::Sqrt(1.0 / $fb.N + [math]::Pow($tStar - $fb.Xbar, 2) / $fb.Sxx)
    $seMean = $fa.S / [math]::Sqrt($fa.N)
    $uOff = 2.0 * [math]::Sqrt($sePred * $sePred + $seMean * $seMean) + ($fb.LatNs + $fa.LatNs) / 2.0
    $rate = ($fa.B / $fb.B - 1.0) * 1e9
    $uRate = 2.0 * 1e9 * [math]::Sqrt([math]::Pow($fa.SeB / $fb.B, 2) + [math]::Pow($fa.B * $fb.SeB / ($fb.B * $fb.B), 2))
    $metrics.offsetNs = [math]::Round($offset, 1); $metrics.offsetUncertaintyNs = [math]::Round($uOff, 1)
    $metrics.rateChangePpb = [math]::Round($rate, 3); $metrics.rateUncertaintyPpb = [math]::Round($uRate, 3)
    $metrics.rateBeforeNsPerS = [math]::Round($fb.B, 3); $metrics.rateAfterNsPerS = [math]::Round($fa.B, 3)
    $metrics.residualSigmaBeforeNs = [math]::Round($fb.S, 1); $metrics.residualSigmaAfterNs = [math]::Round($fa.S, 1)
    $metrics.readLatencyNs = [math]::Round(($fb.LatNs + $fa.LatNs) / 2.0, 1); $metrics.extrapolationSeconds = [math]::Round($tStar - $fb.Xbar, 3)
    if (-not $fa.Monotonic) { $cfgFail = $true; $reasons += "after window: PHC not monotonic at read $($fa.BackAt) (clock discontinuity)" }
    if ($cfgFail) { return (New-AvbPhcResult $reasons $metrics $adapter $Testcase $hw 'CLEANUP_FAILED') }

    $te = Test-AvbPhcTolerance $Tolerance $Before.provenance.did $hw
    if ($te) { $reasons += $te; return (New-AvbPhcResult $reasons $metrics $adapter $Testcase $hw 'INCONCLUSIVE_RESTORATION') }
    $tO = [double]$Tolerance.offsetNs; $tR = [double]$Tolerance.rateChangePpb
    if (([math]::Abs($offset) - $uOff) -gt $tO -or ([math]::Abs($rate) - $uRate) -gt $tR) {
        $reasons += ("outside tolerance: offset {0:N1} +/- {1:N1} ns (tol {2}), rate {3:N3} +/- {4:N3} ppb (tol {5})" -f $offset, $uOff, $tO, $rate, $uRate, $tR)
        return (New-AvbPhcResult $reasons $metrics $adapter $Testcase $hw 'CLEANUP_FAILED')
    }
    if (([math]::Abs($offset) + $uOff) -le $tO -and ([math]::Abs($rate) + $uRate) -le $tR) {
        $reasons += ("inside tolerance including uncertainty: offset {0:N1} +/- {1:N1} ns, rate {2:N3} +/- {3:N3} ppb" -f $offset, $uOff, $rate, $uRate)
        return (New-AvbPhcResult $reasons $metrics $adapter $Testcase $hw 'RESTORATION_VERIFIED')
    }
    $reasons += ("uncertainty straddles the tolerance: offset {0:N1} +/- {1:N1} ns (tol {2}), rate {3:N3} +/- {4:N3} ppb (tol {5})" -f $offset, $uOff, $tO, $rate, $uRate, $tR)
    return (New-AvbPhcResult $reasons $metrics $adapter $Testcase $hw 'INCONCLUSIVE_RESTORATION')
}
