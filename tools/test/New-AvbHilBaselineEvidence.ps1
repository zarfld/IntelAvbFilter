# New-AvbHilBaselineEvidence.ps1 - baseline evidence document for the machine safety record (D3, Q 2026-10-22).
#
# Produces the document Approve-AvbHilSafetyBaseline (tools/lib/HilLock.ps1) requires - schema
# avb-hil-baseline-authorization/1 - from REAL observations made with the runner's own observer (Get-AvbHilState of
# tools/test/Run-Tests-CI.ps1: PnP / NetAdapter / filter binding + ENUM_ADAPTERS -> OPEN_ADAPTER -> GET_CLOCK_CONFIG),
# loaded from the runner source, never copied.  PHC behaviour between passes is measured with the existing evaluator
# (tools/lib/PhcRestoration.ps1) WITHOUT a tolerance: characterization data only; no tolerance is derived here.
#
# It NEVER initializes, approves, clears or writes the machine safety record and never changes adapter configuration.
# Approval stays an operator decision (Approve-AvbHilSafetyBaseline with this document as -BaselineEvidence).
#
# Observation side effects (driver source, issue #343, IOCTL_AVB_OPEN_ADAPTER in src/avb_integration_fixed.c) are
# recorded in the document: the observation is the same one every HIL run performs before and after each test.
#
# Usage (elevated PowerShell, repository root, driver installed, no other HIL run active - takes the machine lock):
#   .\tools\test\New-AvbHilBaselineEvidence.ps1 -ExpectDids 0x1533,0x15BB,0x125B
# Exit: 0 = BASELINE_VERIFIED document written; 1 = NOT_VERIFIED document written (findings listed); 2 = not run.
param(
    [string[]]$ExpectDids = @(),
    [int]$IntervalSeconds = 5,
    [string]$OutPath = ''
)

# PHC behaviour of one adapter between two passes (evaluator metrics, tolerance deliberately absent).
function Get-AvbBaselinePhcCharacterization {
    param([object[]]$Passes)
    $out = @()
    for ($p = 1; $p -lt $Passes.Count; $p++) {
        $after = @{}; foreach ($c in @($Passes[$p].Clock)) { $after["$($c.Did)#$($c.Index)"] = $c }
        foreach ($b in @($Passes[$p - 1].Clock | Where-Object { $_.Ok })) {
            $a = $after["$($b.Did)#$($b.Index)"]
            if ($null -eq $a -or -not $a.Ok) { continue }
            $v = Get-AvbPhcRestorationVerdict -Before (ConvertTo-AvbHilPhcWindow $b) -After (ConvertTo-AvbHilPhcWindow $a) -Tolerance $null -Testcase 'TC-INFRA-BASELINE-PHC'
            $out += ,([ordered]@{ did = $b.Did; driverIndex = $b.Index; fromPass = $p - 1; toPass = $p; verdict = $v.Verdict
                                  reasons = @($v.Reasons); metrics = $v.Metrics; hardwareEvidence = $v.HardwareEvidence })
        }
    }
    return ,$out
}

# Pure: the evidence document and its verdict from observations (no I/O).  Any finding = NOT_VERIFIED.
function New-AvbHilBaselineDocument {
    param([object[]]$Passes, [hashtable]$Identity, $Driver, [hashtable]$Supported, [string[]]$ExpectDids = @(), $Safety,
          [int]$IntervalSeconds, [string]$HostName, [string]$CreatedUtc)
    $findings = @()
    if (@($Passes).Count -lt 2) { $findings += 'fewer than two observation passes' }
    for ($p = 0; $p -lt @($Passes).Count; $p++) {
        if (-not $Passes[$p].Ok) { $findings += "pass ${p}: adapter state not observable ($($Passes[$p].Error))" }
        if (-not $Passes[$p].ClockOk) { $findings += "pass ${p}: driver clock state not observable ($($Passes[$p].ClockError))" }
    }
    $first = $Passes[0]
    $sup = @($first.Adapters | Where-Object { $Supported.ContainsKey([string]$_.Did) })
    foreach ($d in @($sup | Group-Object Did | Where-Object { $_.Count -gt 1 })) { $findings += "DID $($d.Name) present $($d.Count) times: PnP <-> driver mapping by DID is ambiguous" }
    foreach ($e in $ExpectDids) {
        $ed = ConvertTo-AvbDid $e
        if (@($sup | Where-Object { $_.Did -eq $ed }).Count -eq 0) { $findings += "expected adapter $ed not present" }
    }
    $inventory = @()
    foreach ($a in $sup) {
        $id = $Identity[[string]$a.InstanceId]
        $mac = if ($id) { [string]$id.Mac } else { '' }
        $loc = if ($id) { [string]$id.PciLocation } else { '' }
        if ($a.PnpStatus -ne 'OK' -or [int]$a.Problem -ne 0) { $findings += "$($a.Did): PnP status $($a.PnpStatus) problem $($a.Problem)" }
        if ([string]$a.FilterBound -ne 'True') { $findings += "$($a.Did): IntelAvbFilter binding '$($a.FilterBound)'" }
        if (-not $mac) { $findings += "$($a.Did): MAC address not observed" }
        if (-not $loc) { $findings += "$($a.Did): PCI location path not observed" }
        $clk = @($first.Clock | Where-Object { $_.Did -eq $a.Did })
        if ($clk.Count -ne 1) { $findings += "$($a.Did): $($clk.Count) driver enumeration entries (exactly one required)" }
        elseif (-not $clk[0].Ok) { $findings += "$($a.Did): clock config not readable ($($clk[0].Error))" }
        $inventory += ,([ordered]@{ did = $a.Did; family = $Supported[[string]$a.Did]; name = $a.Name; instanceId = $a.InstanceId; mac = $mac; pciLocation = $loc
                                    locationInfo = $(if ($id) { [string]$id.LocationInfo } else { '' }); miniportDriverVersion = $(if ($id) { [string]$id.DriverVersion } else { '' })
                                    interfaceDescription = $(if ($id) { [string]$id.Description } else { '' })
                                    pnpStatus = $a.PnpStatus; problem = $a.Problem; adminStatus = $a.AdminStatus; mediaStatus = $a.MediaStatus; filterBound = $a.FilterBound
                                    driverIndex = $(if ($clk.Count -eq 1) { $clk[0].Index } else { $null }) })
    }
    foreach ($c in @($first.Clock)) { if (@($sup | Where-Object { $_.Did -eq $c.Did }).Count -eq 0) { $findings += "driver enumerates $($c.Did) (index $($c.Index)) without a healthy supported PnP adapter" } }
    # Stability: every later pass must equal the first (adapter state and TIMINCA/TSAUXC) - Compare-AvbHilState.
    for ($p = 1; $p -lt @($Passes).Count; $p++) {
        foreach ($d in @((Compare-AvbHilState $first $Passes[$p]).Diffs)) { $findings += "pass 0 -> pass ${p}: $d" }
    }
    $phc = Get-AvbBaselinePhcCharacterization -Passes $Passes
    foreach ($m in $phc) {
        # Without a tolerance the evaluator's verdict is INCONCLUSIVE_RESTORATION by design; anything else (FAILED_SETUP,
        # CLEANUP_FAILED: non-monotonic PHC, configuration change) is a finding.
        if ($m.verdict -ne 'INCONCLUSIVE_RESTORATION') { $findings += ("{0}: PHC pass {1} -> {2}: {3} ({4})" -f $m.did, $m.fromPass, $m.toPass, $m.verdict, ($m.reasons -join '; ')) }
    }
    # A PHC within 10 s of its epoch right after OPEN_ADAPTER may have been (re)initialized by this very observation
    # (AvbEnsureDeviceReady: SYSTIM reset) or by a driver load moments ago: no stable baseline.
    foreach ($c in @($first.Clock | Where-Object { $_.Ok -and @($_.Samples).Count -gt 0 })) {
        if ([long]@($c.Samples)[0].phcNs -lt 10000000000) { $findings += ("{0}: PHC {1} ns is within 10 s of its epoch - possibly initialized by this observation's OPEN_ADAPTER or a driver load moments ago" -f $c.Did, @($c.Samples)[0].phcNs) }
    }
    if (-not $Driver -or -not $Driver.Sha256) { $findings += 'installed driver identity (SHA256) not observed' }

    $measurements = @()
    for ($p = 0; $p -lt @($Passes).Count; $p++) {
        foreach ($c in @($Passes[$p].Clock | Where-Object { $_.Ok })) {
            $src = "IOCTL GET_CLOCK_CONFIG after OPEN_ADAPTER(index $($c.Index)) - Get-AvbHilState, pass $p"
            $measurements += ,([ordered]@{ pass = $p; did = $c.Did; driverIndex = $c.Index; name = 'TIMINCA'; value = ('0x{0:X8}' -f [uint32]$c.Timinca); source = $src })
            $measurements += ,([ordered]@{ pass = $p; did = $c.Did; driverIndex = $c.Index; name = 'TSAUXC'; value = ('0x{0:X8}' -f [uint32]$c.Tsauxc); source = $src })
            $measurements += ,([ordered]@{ pass = $p; did = $c.Did; driverIndex = $c.Index; name = 'PHC_SAMPLES'; value = @($c.Samples); qpcFrequency = [System.Diagnostics.Stopwatch]::Frequency; source = $src })
        }
    }
    return [ordered]@{
        schema = 'avb-hil-baseline-authorization/1'; host = $HostName; createdUtc = $CreatedUtc
        producer = 'tools/test/New-AvbHilBaselineEvidence.ps1'
        verdict = $(if ($findings.Count -eq 0) { 'BASELINE_VERIFIED' } else { 'NOT_VERIFIED' }); findings = @($findings)
        approval = 'NOT APPROVED - Approve-AvbHilSafetyBaseline is an operator decision; this producer never writes the safety record'
        inventory = @($inventory); driver = $Driver; measurements = @($measurements); phcCharacterization = @($phc)
        observation = [ordered]@{ passes = @($Passes).Count; intervalSeconds = $IntervalSeconds; phcReadsPerPass = $(if (@($first.Clock).Count) { @(@($first.Clock)[0].Samples).Count } else { 0 })
                                  method = 'Get-AvbHilState (tools/test/Run-Tests-CI.ps1): Get-PnpDevice, Get-NetAdapter, Get-NetAdapterBinding MS_IntelAvbFilter; ENUM_ADAPTERS -> OPEN_ADAPTER -> GET_CLOCK_CONFIG' }
        observationSideEffects = @(
            'OPEN_ADAPTER sets the driver-global g_AvbContext to the observed adapter; after the observation the last enumerated adapter stays active (src/avb_integration_fixed.c IOCTL_AVB_OPEN_ADAPTER, issue #343)'
            'OPEN_ADAPTER runs AvbBringUpHardware / AvbEnsureDeviceReady (PTP init: SYSTIM reset, TIMINCA rewrite) for an adapter below PTP_READY; detected only indirectly (PHC near its epoch finding)'
            'OPEN_ADAPTER clears I210 TSAUXC bit 31 (DISABLE_SYSTIM) at PTP_READY: the TSAUXC readback is the post-OPEN_ADAPTER value; a set bit 31 before the observation is not observable'
            'GET_CLOCK_CONFIG ignores the get_systime software-fallback indicator: whether a SYSTIM sample is a hardware read is UNVERIFIED per adapter')
        limitations = @('PnP <-> driver enumeration mapping is by DID only (no driver-reported PCI location)', 'no PHC tolerance is derived: phcCharacterization is input for an operator decision')
        safetyRecordAtCapture = $Safety
    }
}

# ------------------------------------------------------------------------------------------------------------------
# Collector (real hardware).  The offline test (tests/unit/ci/test_hil_baseline_evidence.ps1) loads only the functions
# above from the AST and never executes this part.
# ------------------------------------------------------------------------------------------------------------------
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
. (Join-Path $repoRoot 'tools\lib\HilLock.ps1')
. (Join-Path $repoRoot 'tools\lib\PhcRestoration.ps1')
$runner = Join-Path $repoRoot 'tools\test\Run-Tests-CI.ps1'
$ast = [System.Management.Automation.Language.Parser]::ParseFile($runner, [ref]$null, [ref]$null)
foreach ($n in 'Format-AvbDid', 'ConvertTo-AvbDid', 'Get-AvbHilSupportedDids', 'New-AvbHilAdapter', 'Get-AvbHilClockState', 'Get-AvbHilSandboxState',
               'Get-AvbHilState', 'Compare-AvbHilState', 'ConvertTo-AvbHilPhcWindow') {
    $fn = $ast.Find({ param($x) $x -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $x.Name -eq $n }, $true)
    if (-not $fn) { Write-Host "[FAIL] $n not found in $runner" -ForegroundColor Red; exit 2 }
    . ([scriptblock]::Create($fn.Extent.Text))
}
$script:IsSandbox = $false

$id = [System.Security.Principal.WindowsIdentity]::GetCurrent()
if (-not (New-Object System.Security.Principal.WindowsPrincipal($id)).IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host '[FAIL] run elevated (the driver control device and PnP properties need administrator rights)' -ForegroundColor Red; exit 2
}
$lock = Enter-AvbHilLock -Kind 'local' -Label 'baseline evidence (New-AvbHilBaselineEvidence.ps1)'
if (-not $lock.Ok) { Write-Host "[FAIL] machine lock not acquired: $($lock.Why) - another HIL run is active" -ForegroundColor Red; exit 2 }
try {
    $supported = Get-AvbHilSupportedDids
    $passes = @()
    for ($p = 0; $p -lt 3; $p++) {
        if ($p -gt 0) { Start-Sleep -Seconds $IntervalSeconds }
        $passes += ,(Get-AvbHilState -PhcReads 5)
        Write-Host ("[INFO] pass {0}: {1} PnP adapter(s), {2} driver clock entr(y/ies)" -f $p, @($passes[-1].Adapters).Count, @($passes[-1].Clock).Count)
    }
    $identity = @{}
    $na = @{}; foreach ($n in @(Get-NetAdapter -IncludeHidden -ErrorAction SilentlyContinue)) { if ($n.PnPDeviceID) { $na[([string]$n.PnPDeviceID).ToUpperInvariant()] = $n } }
    foreach ($a in @($passes[0].Adapters)) {
        $n = $na[([string]$a.InstanceId).ToUpperInvariant()]
        $lp = (Get-PnpDeviceProperty -InstanceId $a.InstanceId -KeyName 'DEVPKEY_Device_LocationPaths' -ErrorAction SilentlyContinue).Data
        $li = (Get-PnpDeviceProperty -InstanceId $a.InstanceId -KeyName 'DEVPKEY_Device_LocationInfo' -ErrorAction SilentlyContinue).Data
        $identity[[string]$a.InstanceId] = @{ Mac = $(if ($n) { [string]$n.MacAddress } else { '' }); PciLocation = $(if ($lp) { [string]@($lp)[0] } else { '' })
                                              LocationInfo = [string]$li; DriverVersion = $(if ($n) { [string]$n.DriverVersion } else { '' })
                                              Description = $(if ($n) { [string]$n.InterfaceDescription } else { '' }) }
    }
    $sys = Join-Path $env:SystemRoot 'System32\drivers\IntelAvbFilter.sys'
    $svc = Get-Service -Name 'IntelAvbFilter' -ErrorAction SilentlyContinue
    $driver = [ordered]@{ path = $sys; Sha256 = $(if (Test-Path -LiteralPath $sys) { (Get-FileHash -LiteralPath $sys -Algorithm SHA256).Hash } else { '' })
                          fileVersion = $(if (Test-Path -LiteralPath $sys) { [System.Diagnostics.FileVersionInfo]::GetVersionInfo($sys).FileVersion } else { '' })
                          serviceStatus = $(if ($svc) { [string]$svc.Status } else { 'not found' }) }
    $st = Read-AvbHilSafetyState
    $doc = New-AvbHilBaselineDocument -Passes $passes -Identity $identity -Driver $driver -Supported $supported -ExpectDids $ExpectDids `
               -Safety ([ordered]@{ state = $st.State; why = $st.Why; path = (Get-AvbHilSafetyPath) }) -IntervalSeconds $IntervalSeconds `
               -HostName $env:COMPUTERNAME -CreatedUtc ((Get-Date).ToUniversalTime().ToString('o'))
} finally {
    $null = Exit-AvbHilLock -Owner $lock.Owner
}
if (-not $OutPath) {
    $dir = Join-Path $repoRoot 'logs\hil-baseline'; New-Item -ItemType Directory -Path $dir -Force | Out-Null
    $OutPath = Join-Path $dir ("hil-baseline-evidence-{0}.json" -f (Get-Date -Format 'yyyyMMdd_HHmmss'))
}
($doc | ConvertTo-Json -Depth 10) | Set-Content -LiteralPath $OutPath -Encoding UTF8
Write-Host ("[{0}] {1} - {2}" -f $doc.verdict, $OutPath, $(if ($doc.findings.Count) { ($doc.findings -join ' | ') } else { 'no findings' }))
Write-Host '[INFO] the machine safety record was only read, never modified; approval is an operator decision (Approve-AvbHilSafetyBaseline)'
if ($doc.verdict -eq 'BASELINE_VERIFIED') { exit 0 } else { exit 1 }
