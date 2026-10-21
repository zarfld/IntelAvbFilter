# LocalSuites.ps1 - operator-driven local pre-commit HIL run, Debug and Release.
#
# Usage (elevated PowerShell, repository root; no GitHub variables, artifacts, SHA input, prior CI or push needed):
#   .\tools\archive\deprecated\LocalSuites.ps1 *>&1 | Tee-Object -FilePath ".\logs\FullSuite_$(Get-Date -Format 'yyyyMMdd_HHmmss').log"
#
# Per configuration, strictly sequential (the next configuration is installed only after this one is finished):
#   1. build the driver            tools\build\Build-Driver.ps1
#   2. build the tests             tools\build\Build-Tests.ps1 (manifest with local build-input digest)
#   3. reinstall the driver        tools\setup\Install-Driver-Elevated.ps1 -Action Reinstall (operator script, UAC)
#   4. verify driver + hardware    tools\test\Test-DriverPreFlight.ps1 with the SHA256 of the driver built in step 1
#                                  (installed file != built file -> stop; STOP_PENDING / not Running -> stop)
#   5. suites, in this order (HardwareMandatory intentionally last - do not reorder without approval):
#        HardwareUnit, HardwareIntegration, HardwareMutation, HardwareMandatory
#      via tools\test\Run-Tests-CI.ps1 -ResultsJson (machine-readable; never console-table parsing)
#   6. failure inventory from the results documents (executed failures vs. blocked vs. skipped kept apart)
#   7. operator-confirmed reproduction of selected failures of THIS configuration (its driver is installed) with
#      tools\test\Run-Tests-Elevated.ps1 -CaptureDbgView -Suite <suite> (same runner gates); ReadOnly tests only
#   8. combined report (JSON + text) in -ReportDir
#
# Stops (no further suite, no reproduction, no next configuration) after a timeout, an infrastructure failure, a
# cleanup failure / state-integrity halt, a missing or inconsistent results document, or any build/install/verify
# failure.  Never reboots, never retries an installation, never repairs adapters.
#
# Evidence class: LOCAL (pre-commit when the working tree is dirty).  It is never clean-commit CI evidence.
# Exit codes: tools\lib\NativeExit.ps1 ($global:LASTEXITCODE only; a missing exit code is a failure).

param(
    [ValidateSet('Debug', 'Release')]
    [string[]]$Configurations = @('Debug', 'Release'),

    # Never offer DbgView reproductions (unattended local run).
    [switch]$NoReproduce,

    # Report directory; default logs\LocalSuites_<timestamp>.
    [string]$ReportDir = ''
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent
. (Join-Path $RepoRoot 'tools\lib\NativeExit.ps1')
. (Join-Path $RepoRoot 'tools\lib\HilLock.ps1')   # machine-wide serialization shared with automatic HIL (hil.yml)

$Suites = @('HardwareUnit', 'HardwareIntegration', 'HardwareMutation', 'HardwareMandatory')

# ---------------------------------------------------------------------------------------------------------
# Decision functions (pure; tests/unit/ci/test_local_suites.ps1)
# ---------------------------------------------------------------------------------------------------------
# Results document of one suite run: it must exist, be schema avb-runner-results/1 for this suite/configuration and
# agree with the exit code the operator observed.  Anything else is untrustworthy -> @{ Ok = $false }.
function Read-AvbSuiteResults {
    param([string]$Path, [string]$Suite, [string]$Configuration, $ObservedExit)
    if (-not (Test-Path -LiteralPath $Path)) {
        return @{ Ok = $false; Doc = $null; Error = "no results document at '$Path' (runner did not complete; exit $(Format-AvbExitCode $ObservedExit))" }
    }
    try { $doc = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json } catch { return @{ Ok = $false; Doc = $null; Error = "results document unreadable: $($_.Exception.Message)" } }
    if ($doc.schema -ne 'avb-runner-results/1') { return @{ Ok = $false; Doc = $doc; Error = "unexpected schema '$($doc.schema)'" } }
    if ($doc.suite -ne $Suite -or $doc.configuration -ne $Configuration) {
        return @{ Ok = $false; Doc = $doc; Error = "document is for $($doc.suite)/$($doc.configuration), expected $Suite/$Configuration" }
    }
    if ($null -eq $ObservedExit -or [int]$doc.exitCode -ne [int]$ObservedExit) {
        return @{ Ok = $false; Doc = $doc; Error = "document exit code $($doc.exitCode) disagrees with the observed exit code $(Format-AvbExitCode $ObservedExit)" }
    }
    return @{ Ok = $true; Doc = $doc; Error = '' }
}

# Inventory rows: one per result record, Category = Failure | Blocked | NotFailure (verdict kept verbatim).
# An unknown verdict is a failure (never silently dropped).
function Get-AvbFailureInventory {
    param($Doc, [string]$Configuration, [string]$Suite)
    $rows = @()
    foreach ($r in @($Doc.records)) {
        $cat = if ($r.verdict -in @('PASSED', 'SKIPPED', 'COVERAGE-GAP')) { 'NotFailure' } elseif ($r.verdict -in @('BLOCKED(PROVENANCE)', 'BLOCKED(SAFETY)')) { 'Blocked' } else { 'Failure' }
        $failing = @($r.testcases | Where-Object { $_.Verdict -notin @('PASS', 'SKIP', 'SKIP_UNSUPPORTED') } | ForEach-Object {
            if ($_.AdapterDid) { "$($_.Id)@$($_.AdapterDid)" } elseif ($null -ne $_.AdapterIndex) { "$($_.Id)/adapter:$($_.AdapterIndex)" } else { $_.Id } })
        $rows += [pscustomobject]@{
            Configuration = $Configuration; Suite = $Suite; Name = $r.name; Entry = $r.entry; Arguments = $r.arguments
            Verdict = $r.verdict; Category = $cat; Executed = [bool]$r.executed; Check = [bool]$r.check; ExitCode = $r.exitCode
            SafetyClass = $r.safetyClass; TargetDid = $r.targetDid; Testcases = ($failing -join ' '); Reason = $r.reason; LogPath = $r.logPath
        }
    }
    return ,$rows
}

# '' = continue; otherwise the reason this run must stop after the suite (hardware state uncertain or evidence missing).
function Get-AvbStopReason {
    param($Doc)
    $bad = @(@($Doc.records) | Where-Object { $_.verdict -in @('FAILED(TIMEOUT)', 'INFRA_FAILURE', 'CLEANUP_FAILED') })
    if ($bad.Count -gt 0) { return ("hardware state requires inspection: {0}" -f (($bad | ForEach-Object { "$($_.name)=$($_.verdict)" }) -join ', ')) }
    if ($Doc.stateHalt) { return "state-integrity halt: $($Doc.stateHalt)" }
    return ''
}

# Reproduction candidates of ONE configuration: executed ReadOnly failures without an exact target, deduplicated by
# suite-independent identity (entry + arguments); never another configuration's rows.
function Get-AvbReproductionQueue {
    param([object[]]$Inventory, [string]$Configuration)
    $seen = @{}; $q = @()
    foreach ($row in @($Inventory)) {
        if ($row.Configuration -ne $Configuration) { continue }
        if ($row.Category -ne 'Failure' -or -not $row.Executed -or $row.Check) { continue }
        if ($row.Verdict -notin @('FAILED', 'INCOMPLETE', 'INCONCLUSIVE')) { continue }
        if ($row.SafetyClass -ne 'ReadOnly' -or $row.TargetDid) { continue }
        $key = "$($row.Entry)|$($row.Arguments)"
        if ($seen.ContainsKey($key)) { continue }
        $seen[$key] = $true
        $q += $row
    }
    return ,$q
}

# Report text of a step result: 'NOT_RUN' = never reached (the run stopped earlier); $null = ran, no exit code (failure).
function Format-AvbStepResult {
    param($Result)
    if ($Result -is [string] -and $Result -eq 'NOT_RUN') { return 'NOT_RUN' }
    return (Format-AvbExitCode $Result)
}

# ---------------------------------------------------------------------------------------------------------
# Orchestration
# ---------------------------------------------------------------------------------------------------------
if (-not $ReportDir) { $ReportDir = Join-Path $RepoRoot ("logs\LocalSuites_{0}" -f (Get-Date -Format 'yyyyMMdd_HHmmss')) }
New-Item -ItemType Directory -Path $ReportDir -Force | Out-Null

$BuildDriver   = Join-Path $RepoRoot 'tools\build\Build-Driver.ps1'
$BuildTests    = Join-Path $RepoRoot 'tools\build\Build-Tests.ps1'
$InstallDriver = Join-Path $RepoRoot 'tools\setup\Install-Driver-Elevated.ps1'
$PreFlight     = Join-Path $RepoRoot 'tools\test\Test-DriverPreFlight.ps1'
$RunCI         = Join-Path $RepoRoot 'tools\test\Run-Tests-CI.ps1'
$RunElevated   = Join-Path $RepoRoot 'tools\test\Run-Tests-Elevated.ps1'

$Report = [ordered]@{
    schema = 'avb-local-suites/1'; evidenceClass = 'LOCAL (never clean-commit CI evidence)'; host = $env:COMPUTERNAME
    startedUtc = (Get-Date).ToUniversalTime().ToString('o'); finishedUtc = $null; stopped = $null; configurations = @()
}
$AllInventory = @()
$Stop = ''

function Invoke-AvbStep {
    param([string]$Label, [scriptblock]$Call)
    Write-Host "`n==> $Label" -ForegroundColor Cyan
    Reset-AvbExitCode
    try { & $Call | Out-Host } catch { Write-Host "[FAIL] $Label threw: $($_.Exception.Message)" -ForegroundColor Red; Reset-AvbExitCode; return $null }
    $rc = Get-AvbExitCode
    if (-not (Test-AvbExitSuccess $rc)) { Write-Host "[FAIL] $Label exit code $(Format-AvbExitCode $rc)" -ForegroundColor Red }
    return $rc
}

function Invoke-AvbPreFlight {
    param([string]$BuiltSha256)
    $saveB = $env:BUILD_DRIVER_SHA256; $saveR = $env:HIL_REQUIRE_DEPLOYED_SHA
    try {
        $env:BUILD_DRIVER_SHA256 = $BuiltSha256; $env:HIL_REQUIRE_DEPLOYED_SHA = '1'
        return (Invoke-AvbStep 'verify installed driver == locally built driver, service, device node' { & $PreFlight })
    } finally { $env:BUILD_DRIVER_SHA256 = $saveB; $env:HIL_REQUIRE_DEPLOYED_SHA = $saveR }
}

$HilLock = Enter-AvbHilLock -Kind 'local' -Label "LocalSuites $($Configurations -join '+')"
if (-not $HilLock.Ok) {
    Write-Host "[STOPPED] HIL machine busy: $($HilLock.Why) --- nothing built, installed or executed" -ForegroundColor Red
    exit 1
}
if ($HilLock.PreviousOwner) { Write-Host "[INFO] took over a stale HIL lock: $($HilLock.PreviousOwner.why)" -ForegroundColor Yellow }
$Report.lock = [ordered]@{ owner = $HilLock.Owner; previousOwner = $HilLock.PreviousOwner }

try {
    foreach ($Configuration in $Configurations) {
        $cfg = [ordered]@{ configuration = $Configuration; head = $null; builtDriverSha256 = $null; testManifest = $null
                           provenanceMode = $null; buildInputDigest = $null; install = 'NOT_RUN'; preflight = 'NOT_RUN'; suites = @(); reproductions = @() }
        $Report.configurations += $cfg
        $cfg.head = ([string](& git -C $RepoRoot rev-parse HEAD)).Trim()

        # 1-2. Build (artifacts are verified independently of the exit code)
        if (-not (Test-AvbExitSuccess (Invoke-AvbStep "Build-Driver $Configuration" { & $BuildDriver -Configuration $Configuration }))) { $Stop = "driver build failed ($Configuration)"; break }
        $sys = Join-Path $RepoRoot "build\x64\$Configuration\IntelAvbFilter\IntelAvbFilter.sys"
        if (-not (Test-Path -LiteralPath $sys)) { $Stop = "driver build reported success but '$sys' is missing ($Configuration)"; break }
        $cfg.builtDriverSha256 = (Get-FileHash -LiteralPath $sys -Algorithm SHA256).Hash

        if (-not (Test-AvbExitSuccess (Invoke-AvbStep "Build-Tests $Configuration" { & $BuildTests -Configuration $Configuration }))) { $Stop = "test build failed ($Configuration)"; break }
        $manPath = Join-Path $RepoRoot "build\tools\avb_test\x64\$Configuration\test-build-manifest.json"
        if (-not (Test-Path -LiteralPath $manPath)) { $Stop = "test build manifest missing: $manPath"; break }
        $man = Get-Content -LiteralPath $manPath -Raw | ConvertFrom-Json
        $cfg.testManifest = $manPath; $cfg.provenanceMode = $man.provenanceMode; $cfg.buildInputDigest = $man.sourceDigest
        if ($man.provenanceMode -notin @('clean-commit', 'local-worktree')) {
            $Stop = "test build is not attributable (provenanceMode '$($man.provenanceMode)'); every Hardware* artifact would be BLOCKED"; break
        }

        # 3. Install (existing operator script; UAC prompt)
        $cfg.install = Invoke-AvbStep "Install-Driver-Elevated $Configuration -Action Reinstall" { & $InstallDriver -Configuration $Configuration -Action Reinstall -CaptureDbgView }
        if (-not (Test-AvbExitSuccess $cfg.install)) { $Stop = "driver installation failed ($Configuration); no test executed against this configuration"; break }

        # 4. Verify the installed driver is the one built in step 1
        $cfg.preflight = Invoke-AvbPreFlight $cfg.builtDriverSha256
        if (-not (Test-AvbExitSuccess $cfg.preflight)) { $Stop = "pre-flight failed ($Configuration): installed driver is not the locally built $($cfg.builtDriverSha256), or service/device node unhealthy"; break }
        Write-Host "[INFO] Installed driver file == locally built driver (on-disk SHA256 $($cfg.builtDriverSha256)). The loaded image is not hashed; after a reinstall without reboot it is UNPROVEN that the running image is this file." -ForegroundColor Yellow

        # 5-6. Suites
        $cfgInventory = @()
        foreach ($Suite in $Suites) {
            $json = Join-Path $ReportDir ("results-{0}-{1}.json" -f $Configuration, $Suite)
            $transcript = Join-Path $ReportDir ("console-{0}-{1}.log" -f $Configuration, $Suite)
            $t0 = Get-Date
            Start-Transcript -Path $transcript -Force | Out-Null
            try { $rc = Invoke-AvbStep "Run-Tests-CI $Suite $Configuration" { & $RunCI -Suite $Suite -Configuration $Configuration -ResultsJson $json } }
            finally { Stop-Transcript | Out-Null }
            $res = Read-AvbSuiteResults -Path $json -Suite $Suite -Configuration $Configuration -ObservedExit $rc
            $s = [ordered]@{ suite = $Suite; exitCode = $rc; resultsJson = $json; console = $transcript; durationSeconds = [Math]::Round(((Get-Date) - $t0).TotalSeconds, 1)
                             overall = $null; executed = $null; counts = $null; error = $res.Error }
            $cfg.suites += $s
            if (-not $res.Ok) { $Stop = "$Configuration/${Suite}: $($res.Error)"; break }
            $s.overall = $res.Doc.overall; $s.executed = $res.Doc.executedCount; $s.counts = $res.Doc.counts
            $inv = Get-AvbFailureInventory -Doc $res.Doc -Configuration $Configuration -Suite $Suite
            $cfgInventory += $inv
            $nf = @($inv | Where-Object { $_.Category -eq 'Failure' }).Count; $nb = @($inv | Where-Object { $_.Category -eq 'Blocked' }).Count
            Write-Host ("`n{0} / {1}: exit {2}, overall {3}, executed {4}, failures {5}, blocked {6}" -f $Configuration, $Suite, (Format-AvbExitCode $rc), $res.Doc.overall, $res.Doc.executedCount, $nf, $nb)
            $why = Get-AvbStopReason $res.Doc
            if ($why) { $Stop = "$Configuration/${Suite}: $why"; break }
        }
        $AllInventory += $cfgInventory
        if ($Stop) { break }

        # 7. Reproduction (this configuration's driver is installed; operator selects each item)
        if (-not $NoReproduce) {
            foreach ($item in (Get-AvbReproductionQueue -Inventory $cfgInventory -Configuration $Configuration)) {
                Write-Host "`n$Configuration / $($item.Suite) / $($item.Entry) $($item.Arguments)  [$($item.Verdict)] $($item.Testcases)"
                if ((Read-Host "Reproduce with Run-Tests-Elevated.ps1 -CaptureDbgView? (y/N)") -ne 'y') { continue }
                if (-not (Test-AvbExitSuccess (Invoke-AvbPreFlight $cfg.builtDriverSha256))) { $Stop = "pre-flight failed before reproduction ($Configuration)"; break }
                $repJson = Join-Path $ReportDir ("repro-{0}-{1}-{2}.json" -f $Configuration, ($item.Entry -replace '[^\w.-]', '_'), (Get-Date -Format 'HHmmss'))
                $rrc = Invoke-AvbStep "reproduce $($item.Entry)" {
                    & $RunElevated -TestName $item.Entry -Suite $item.Suite -Configuration $Configuration -CaptureDbgView -ResultsJson $repJson }
                $cfg.reproductions += [ordered]@{ suite = $item.Suite; entry = $item.Entry; arguments = $item.Arguments; originalVerdict = $item.Verdict
                                                  exitCode = $rrc; resultsJson = $repJson }
            }
            if ($Stop) { break }
        }
    }
} finally {
    $rel = Exit-AvbHilLock -Owner $HilLock.Owner
    if (-not $rel.Ok) { Write-Host "[WARN] HIL lock not released: $($rel.Why)" -ForegroundColor Yellow }
    $Report.finishedUtc = (Get-Date).ToUniversalTime().ToString('o')
    $Report.stopped = $(if ($Stop) { $Stop } else { $null })
    $Report.inventory = @($AllInventory)
    ($Report | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath (Join-Path $ReportDir 'local-suites-report.json') -Encoding UTF8

    Write-Host "`n================ LOCAL SUITES REPORT (LOCAL evidence, never CI evidence) ================" -ForegroundColor Cyan
    foreach ($c in $Report.configurations) {
        Write-Host ("{0}: HEAD {1}, provenance {2}, digest {3}, built driver {4}, install {5}, pre-flight {6}" -f $c.configuration, $c.head, $c.provenanceMode,
                    $c.buildInputDigest, $c.builtDriverSha256, (Format-AvbStepResult $c.install), (Format-AvbStepResult $c.preflight))
        foreach ($s in $c.suites) { Write-Host ("  {0,-20} exit {1,-4} overall {2,-13} executed {3}{4}" -f $s.suite, (Format-AvbExitCode $s.exitCode), $s.overall, $s.executed, $(if ($s.error) { "  ERROR: $($s.error)" } else { '' })) }
        foreach ($r in $c.reproductions) { Write-Host ("  reproduction {0} {1}: exit {2} ({3})" -f $r.entry, $r.arguments, (Format-AvbExitCode $r.exitCode), $r.resultsJson) }
    }
    $fail = @($AllInventory | Where-Object { $_.Category -eq 'Failure' }); $blk = @($AllInventory | Where-Object { $_.Category -eq 'Blocked' })
    Write-Host "`nExecuted failures ($($fail.Count)):"
    $fail | Sort-Object Configuration, Suite, Name | Format-Table Configuration, Suite, Name, Verdict, ExitCode, Testcases -AutoSize | Out-String -Width 250 | Write-Host
    Write-Host "Not executed - blocked ($($blk.Count)):"
    $blk | Sort-Object Configuration, Suite, Name | Format-Table Configuration, Suite, Name, Verdict, Reason -AutoSize | Out-String -Width 250 | Write-Host
    if ($Stop) { Write-Host "[STOPPED] $Stop" -ForegroundColor Red }
    Write-Host "Report: $(Join-Path $ReportDir 'local-suites-report.json')"
}

$executed = @($AllInventory | Where-Object { $_.Executed }).Count
if ($Stop -or @($AllInventory | Where-Object { $_.Category -ne 'NotFailure' }).Count -gt 0 -or $executed -eq 0) { exit 1 }
exit 0
