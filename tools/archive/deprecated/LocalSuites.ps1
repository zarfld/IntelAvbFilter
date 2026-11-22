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
#   5. suites, in this order (the only hardware suites, Q 2026-11-03):
#        HardwareUnit, HardwareIntegration
#      via tools\test\Run-Tests-CI.ps1 -ResultsJson (machine-readable; never console-table parsing)
#   6. failure inventory from the results documents (executed failures vs. blocked vs. skipped kept apart)
#   7. reproduction of failures of THIS configuration (its driver is installed) with
#      tools\test\Run-Tests-Elevated.ps1 -CaptureDbgView -Suite <suite> (same runner gates); ReadOnly tests only;
#      per item a 30 s window (N declines, default yes; noninteractive: yes at once); -NoReproduce disables it
#   8. combined report (JSON + text) in -ReportDir
#
# The suite matrix is always attempted completely (Q 2026-10-22; #458 comment 7008463710, 2026-11-19): earlier results
# never stop later suites or configurations.  A missing or inconsistent results document is one RESULTS_UNTRUSTED failure
# row and the next suite runs; a driver/device infrastructure failure is recorded per suite (suites[].infrastructure) and
# the next suite runs.  A configuration whose driver build, installation or pre-flight failed records its hardware suites
# NOT_EXECUTED with that reason (they need this configuration's own built and installed driver, an essential input) and
# the NEXT configuration runs; a missing test build manifest is a recorded build failure and the suites still run (the
# runner reports each missing binary).  Only an explicit operator stop in the bounded window (default continue) ends the
# run early.  A loss of controlled execution in a suite (a timeout whose process tree did not terminate, a lost machine
# lock, a state halt) is kept as evidence; later suites get -PriorResultsDir <ReportDir>, where the runner reports it and
# treats only a process still running NOW as a conflict.  The run stays nonpassing.  Cleanup outcomes (FAILED /
# NOT_VERIFIED) are reported per suite and never halt.
# Never reboots, never retries an installation, never repairs adapters.
#
# Evidence class: LOCAL_DIAGNOSTIC (Run-Tests-CI -LocalDiagnostic: build provenance reported per item, never enforced;
# Q 2026-11-02).  It is never CI evidence.
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

$Suites = @('HardwareUnit', 'HardwareIntegration')

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

# Inventory rows: one per result record, Category = Failure | Blocked | BlockedTestcases | NotExecuted | NotFailure
# (verdict kept verbatim).  NotExecuted = declared coverage gap: a required test not executed (nonpassing).
# BlockedTestcases = executed, no functional FAIL, testcases BLOCKED (runner BLOCKED(TESTCASE); nonpassing).  An unknown
# verdict is a failure.
function Get-AvbFailureInventory {
    param($Doc, [string]$Configuration, [string]$Suite)
    $rows = @()
    foreach ($r in @($Doc.records)) {
        $cat = if ($r.verdict -in @('PASSED', 'SKIPPED')) { 'NotFailure' } elseif ($r.verdict -eq 'COVERAGE-GAP') { 'NotExecuted' }
               elseif ($r.verdict -in @('BLOCKED(PROVENANCE)', 'BLOCKED(SAFETY)')) { 'Blocked' }
               elseif ($r.verdict -eq 'BLOCKED(TESTCASE)') { 'BlockedTestcases' } else { 'Failure' }
        $failing = @($r.testcases | Where-Object { $_.Verdict -notin @('PASS', 'SKIP', 'SKIP_UNSUPPORTED') } | ForEach-Object {
            if ($_.AdapterDid) { "$($_.Id)@$($_.AdapterDid)" } elseif ($null -ne $_.AdapterIndex) { "$($_.Id)/adapter:$($_.AdapterIndex)" } else { $_.Id } })
        $rows += [pscustomobject]@{
            Configuration = $Configuration; Suite = $Suite; Name = $r.name; Entry = $r.entry; Arguments = $r.arguments
            Verdict = $r.verdict; Category = $cat; Executed = [bool]$r.executed; Check = [bool]$r.check; ExitCode = $r.exitCode
            SafetyClass = $r.safetyClass; TargetDid = $r.targetDid; Testcases = ($failing -join ' '); Reason = $r.reason; LogPath = $r.logPath
            Incidents = $(if ($r.PSObject.Properties['incidents']) { (@($r.incidents) | Where-Object { $_ } | ForEach-Object { "$($_.incidentId):$($_.role)" }) -join ' ' } else { '' })
        }
    }
    return ,$rows
}

# Cleanup incidents (Decision B): every CLEANUP_FAILED row is an observation; rows referencing the same incident id are
# ONE incident.  Rows without an incident reference count as one incident each.  @{ Incidents; Observations }
function Get-AvbCleanupIncidentSummary {
    param([object[]]$Inventory)
    $rows = @($Inventory | Where-Object { $_.Verdict -eq 'CLEANUP_FAILED' })
    $ids = @($rows | ForEach-Object { if ($_.Incidents) { $_.Incidents -split ' ' | ForEach-Object { ($_ -split ':')[0] } } } | Where-Object { $_ } | Sort-Object -Unique)
    return @{ Incidents = $ids.Count + @($rows | Where-Object { -not $_.Incidents }).Count; Observations = $rows.Count }
}

# '' = continue; otherwise the reason this run must stop after the suite: global driver/device infrastructure loss only
# (Q 2026-10-21: then even ReadOnly tests are reported blocked, not run).
function Get-AvbStopReason {
    param($Doc)
    $bad = @(@($Doc.records) | Where-Object { $_.verdict -eq 'INFRA_FAILURE' })
    if ($bad.Count -gt 0) { return ("driver/device infrastructure unusable: {0}" -f (($bad | ForEach-Object { "$($_.name)=$($_.verdict)" }) -join ', ')) }
    return ''
}

# '' or the safety halt this suite raised (a timeout without evidenced termination / state halt).  It does
# not stop the run: later suites run with -PriorResultsDir <ReportDir>, so Run-Tests-CI blocks every Init/Mutating test
# and still executes eligible ReadOnly tests (same as hil.yml).  The run stays nonpassing.
function Get-AvbSafetyHalt {
    param($Doc)
    # Same rule as Run-Tests-CI Get-AvbPriorHalt (D2, revised Q 2026-10-28): a timeout whose process tree was terminated
    # (timeoutDisposition TERMINATED_STATE_VERIFIED / TERMINATED, any class) stays FAILED in the inventory but is no halt;
    # CLEANUP_FAILED / INCONCLUSIVE_RESTORATION rows are cleanup observations, not loss of control: no halt.
    $bad = @(@($Doc.records) | Where-Object { $_.verdict -eq 'FAILED(TIMEOUT)' -and -not (
                $_.PSObject.Properties['timeoutDisposition'] -and $_.timeoutDisposition -in @('TERMINATED_STATE_VERIFIED', 'TERMINATED')) })
    $why = @()
    if ($bad.Count -gt 0) { $why += ("hardware state not verified: {0}" -f (($bad | ForEach-Object { "$($_.name)=$($_.verdict)" }) -join ', ')) }
    if ($Doc.stateHalt) { $why += "state-integrity halt: $($Doc.stateHalt)" }
    return ($why -join '; ')
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

# A suite whose results document is missing or untrusted: one Failure row, so the suite can never silently drop out of
# the inventory (and the run can never pass without it).  Not executed -> never a reproduction candidate.
function New-AvbUntrustedSuiteRow {
    param([string]$Configuration, [string]$Suite, [string]$Why)
    return [pscustomobject]@{
        Configuration = $Configuration; Suite = $Suite; Name = "$Suite results"; Entry = ''; Arguments = ''
        Verdict = 'RESULTS_UNTRUSTED'; Category = 'Failure'; Executed = $false; Check = $false; ExitCode = $null
        SafetyClass = ''; TargetDid = ''; Testcases = ''; Reason = $Why; LogPath = ''; Incidents = ''
    }
}

# A suite of a configuration whose own driver was not built, installed or verified (#458): one Failure row per suite with
# the reason - never silently absent, never executed against a driver that is not this configuration's.
function New-AvbNotExecutedSuiteRow {
    param([string]$Configuration, [string]$Suite, [string]$Why)
    return [pscustomobject]@{
        Configuration = $Configuration; Suite = $Suite; Name = "$Suite (not executed)"; Entry = ''; Arguments = ''
        Verdict = 'NOT_EXECUTED'; Category = 'Failure'; Executed = $false; Check = $false; ExitCode = $null
        SafetyClass = ''; TargetDid = ''; Testcases = ''; Reason = $Why; LogPath = ''; Incidents = ''
    }
}

# Bounded operator choice (intent Q 2026-10-22: the run never waits indefinitely for an operator).  Returns the value
# of the first key of -Keys pressed within -Seconds, else -Default.  Noninteractive (redirected input, no user session,
# GitHub Actions): -Default at once; the console key buffer is never touched ([Console]::KeyAvailable throws there).
function Read-AvbTimedChoice {
    param([string]$Prompt, [int]$Seconds, [string]$Default, [hashtable]$Keys, [object]$Interactive = $null, [scriptblock]$ReadKey = $null)
    if ($null -eq $Interactive) {
        $Interactive = [Environment]::UserInteractive -and -not [Console]::IsInputRedirected -and $env:GITHUB_ACTIONS -ne 'true'
    }
    Write-Host -NoNewline "$Prompt "
    if (-not $Interactive) { Write-Host "$Default (noninteractive)"; return $Default }
    if (-not $ReadKey) { $ReadKey = { if ([Console]::KeyAvailable) { return [Console]::ReadKey($true).Key.ToString() }; return $null } }
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $k = & $ReadKey
        if ($k -and $Keys.ContainsKey([string]$k)) { Write-Host $Keys[[string]$k]; return $Keys[[string]$k] }
        Start-Sleep -Milliseconds 100
    }
    Write-Host "$Default (timeout)"
    return $Default
}

# Report text of a step result: 'NOT_RUN' = never reached (the run stopped earlier); $null = ran, no exit code (failure).
function Format-AvbStepResult {
    param($Result)
    if ($Result -is [string] -and $Result -eq 'NOT_RUN') { return 'NOT_RUN' }
    return (Format-AvbExitCode $Result)
}

# Failed entries of a test build ("name: reason"); none when Build-Tests exited 0.  A nonzero (or missing) exit code
# without a failed manifest entry is still a failure, never a silent pass (TEST-LOCALSUITES-021).
function Get-AvbTestBuildFailures {
    param($ExitCode, $Manifest)
    if (Test-AvbExitSuccess $ExitCode) { return @() }
    $failed = @(@($Manifest.entries) | Where-Object { $_.status -eq 'failed' } | ForEach-Object { "$($_.name): $($_.reason)" })
    if ($failed.Count -eq 0) { $failed = @("Build-Tests exit $(Format-AvbExitCode $ExitCode) without a failed manifest entry") }
    return $failed
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
    schema = 'avb-local-suites/1'; evidenceClass = 'LOCAL_DIAGNOSTIC (never CI evidence)'; host = $env:COMPUTERNAME
    startedUtc = (Get-Date).ToUniversalTime().ToString('o'); finishedUtc = $null; stopped = $null; configurations = @()
}
$AllInventory = @()
$Stop = ''
$SafetyHalt = ''   # first loss-of-control evidence of this run (reported; the run is nonpassing; never a veto, #458)
$NotExecuted = @()   # configurations whose hardware suites could not run against their own driver (reported, nonpassing)
function Skip-AvbConfiguration {
    param($Cfg, [string]$Why)
    foreach ($s in $Suites) {
        $Cfg.suites += [ordered]@{ suite = $s; exitCode = $null; resultsJson = $null; console = $null; durationSeconds = 0; overall = 'NOT_EXECUTED'
                                   executed = 0; counts = $null; error = $Why }
        $script:AllInventory += @(New-AvbNotExecutedSuiteRow -Configuration $Cfg.configuration -Suite $s -Why $Why)
    }
    $script:NotExecuted += "$($Cfg.configuration): $Why"
    Write-Host "[NOT_EXECUTED] $($Cfg.configuration) hardware suites: $Why --- they need this configuration's own built and installed driver; recorded, the next configuration runs (#458)" -ForegroundColor Red
}
$BuildFailures = @()   # failed test builds: never a stop (the runner BLOCKs those entries by provenance), always nonpassing

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
                           provenanceMode = $null; buildInputDigest = $null; testBuild = 'NOT_RUN'; testBuildFailed = @()
                           install = 'NOT_RUN'; preflight = 'NOT_RUN'; suites = @(); reproductions = @() }
        $Report.configurations += $cfg
        $cfg.head = ([string](& git -C $RepoRoot rev-parse HEAD)).Trim()

        # 1-2. Build (artifacts are verified independently of the exit code)
        $driverBuild = Invoke-AvbStep "Build-Driver $Configuration" { & $BuildDriver -Configuration $Configuration }
        $sys = Join-Path $RepoRoot "build\x64\$Configuration\IntelAvbFilter\IntelAvbFilter.sys"
        $driverWhy = if (-not (Test-AvbExitSuccess $driverBuild)) { "driver build failed (exit $(Format-AvbExitCode $driverBuild))" }
                     elseif (-not (Test-Path -LiteralPath $sys)) { "driver build reported success but '$sys' is missing" } else { '' }
        if (-not $driverWhy) { $cfg.builtDriverSha256 = (Get-FileHash -LiteralPath $sys -Algorithm SHA256).Hash }

        # A failed individual test build does not stop the matrix (LocalSuites_20261024_133502: one LNK1168 skipped all of
        # Release).  Build-Tests builds every other entry and records the failure in the manifest.  This is a manual local
        # diagnostic run (Run-Tests-CI -LocalDiagnostic, Q 2026-11-02): an older executable of a failed entry still runs,
        # its record says provenance NOT verified.  The build failure keeps the run nonpassing.
        $cfg.testBuild = Invoke-AvbStep "Build-Tests $Configuration" { & $BuildTests -Configuration $Configuration }
        $manPath = Join-Path $RepoRoot "build\tools\avb_test\x64\$Configuration\test-build-manifest.json"
        $man = if (Test-Path -LiteralPath $manPath) { Get-Content -LiteralPath $manPath -Raw | ConvertFrom-Json } else { $null }
        if ($null -eq $man) {
            # Recorded, never a stop (#458): the suites still run; the runner reports each binary it cannot find.
            $BuildFailures += "$Configuration test build manifest missing: $manPath"
            Write-Host "[BUILD FAILED] $Configuration test build manifest missing: $manPath --- recorded; the suites still run and report each missing binary" -ForegroundColor Red
            $man = [pscustomobject]@{ provenanceMode = 'missing'; sourceDigest = $null; entries = @() }
        }
        $cfg.testBuildFailed = @(Get-AvbTestBuildFailures $cfg.testBuild $man)
        if ($cfg.testBuildFailed.Count -gt 0) {
            $BuildFailures += "$Configuration test build exit $(Format-AvbExitCode $cfg.testBuild): $(@($cfg.testBuildFailed).Count) failed entr(y/ies)"
            Write-Host "[BUILD FAILED] $Configuration test build exit $(Format-AvbExitCode $cfg.testBuild) --- continuing; an existing older executable of a failed entry runs as LOCAL_DIAGNOSTIC with provenance NOT verified:" -ForegroundColor Red
            $cfg.testBuildFailed | ForEach-Object { Write-Host "    $_" -ForegroundColor Red }
        }
        $cfg.testManifest = $manPath; $cfg.provenanceMode = $man.provenanceMode; $cfg.buildInputDigest = $man.sourceDigest
        if ($man.provenanceMode -notin @('clean-commit', 'local-worktree')) {
            Write-Host "[LOCAL_DIAGNOSTIC] test build is not attributable (provenanceMode '$($man.provenanceMode)') --- tests still run; their provenance is reported NOT verified" -ForegroundColor Yellow
        }

        # The hardware suites of this configuration need ITS driver: without a build there is nothing to install or test.
        if ($driverWhy) { Skip-AvbConfiguration -Cfg $cfg -Why $driverWhy; continue }

        # 3. Install (existing operator script; UAC prompt)
        $cfg.install = Invoke-AvbStep "Install-Driver-Elevated $Configuration -Action Reinstall" { & $InstallDriver -Configuration $Configuration -Action Reinstall -CaptureDbgView }
        if (-not (Test-AvbExitSuccess $cfg.install)) { Skip-AvbConfiguration -Cfg $cfg -Why "driver installation failed (exit $(Format-AvbExitCode $cfg.install)); no test executed against a driver that is not this configuration's"; continue }

        # 4. Verify the installed driver is the one built in step 1
        $cfg.preflight = Invoke-AvbPreFlight $cfg.builtDriverSha256
        if (-not (Test-AvbExitSuccess $cfg.preflight)) { Skip-AvbConfiguration -Cfg $cfg -Why "pre-flight failed (exit $(Format-AvbExitCode $cfg.preflight)): installed driver is not the locally built $($cfg.builtDriverSha256), or service/device node unhealthy"; continue }
        Write-Host "[INFO] Installed driver file == locally built driver (on-disk SHA256 $($cfg.builtDriverSha256)). The loaded image is not hashed; after a reinstall without reboot it is UNPROVEN that the running image is this file." -ForegroundColor Yellow

        # 5-6. Suites
        # LocalSuites.ps1 must always attempt the complete configured test matrix, regardless of preceding test or suite failures. Failures affect verdicts and final exit status, not scheduling. Only an explicit operator stop may terminate an otherwise executable test sequence.
        #After a failed suite, provide an optional 30-second stop window. Default action is CONTINUE. In noninteractive CI, continue automatically.
        #Every test must establish its prerequisites and enforce hardware-state restoration. Failed restoration is an explicit test failure and must never be hidden or reclassified as success. A subsequent test with unmet prerequisites must be reported as BLOCKED, while other tests continue.
        #Preserve complete Debug/Release execution, the failure inventory, JSON evidence, DbgView capture, and nonzero final exit status if any mandatory test failed or was blocked.
        $cfgInventory = @()
        foreach ($Suite in $Suites) {
            $json = Join-Path $ReportDir ("results-{0}-{1}.json" -f $Configuration, $Suite)
            $transcript = Join-Path $ReportDir ("console-{0}-{1}.log" -f $Configuration, $Suite)
            $t0 = Get-Date
            Start-Transcript -Path $transcript -Force | Out-Null
            try { $rc = Invoke-AvbStep "Run-Tests-CI $Suite $Configuration" { & $RunCI -Suite $Suite -Configuration $Configuration -ResultsJson $json -PriorResultsDir $ReportDir -LocalDiagnostic } }
            finally { Stop-Transcript | Out-Null }
            $res = Read-AvbSuiteResults -Path $json -Suite $Suite -Configuration $Configuration -ObservedExit $rc
            $s = [ordered]@{ suite = $Suite; exitCode = $rc; resultsJson = $json; console = $transcript; durationSeconds = [Math]::Round(((Get-Date) - $t0).TotalSeconds, 1)
                             overall = $null; executed = $null; counts = $null; error = $res.Error }
            $cfg.suites += $s
            if (-not $res.Ok) {
                # No trusted document: the suite is one Failure row (never silently absent); the matrix continues.
                Write-Host "[FAIL] $Configuration/${Suite}: $($res.Error) --- recorded as RESULTS_UNTRUSTED; the next suite runs" -ForegroundColor Red
                $inv = @(New-AvbUntrustedSuiteRow -Configuration $Configuration -Suite $Suite -Why $res.Error)
                $cfgInventory += $inv
            } else {
                $s.overall = $res.Doc.overall; $s.executed = $res.Doc.executedCount; $s.counts = $res.Doc.counts
                # Cleanup results are reported separately from the functional inventory (Q 2026-10-28): visible, never a failure row.
                $s.cleanupCounts = $(if ($res.Doc.PSObject.Properties['cleanupCounts']) { $res.Doc.cleanupCounts } else { $null })
                $inv = Get-AvbFailureInventory -Doc $res.Doc -Configuration $Configuration -Suite $Suite
                $cfgInventory += $inv
                $nf = @($inv | Where-Object { $_.Category -eq 'Failure' }).Count; $nb = @($inv | Where-Object { $_.Category -eq 'Blocked' }).Count
                $nt = @($inv | Where-Object { $_.Category -eq 'BlockedTestcases' }).Count
                Write-Host ("`n{0} / {1}: exit {2}, overall {3}, executed {4}, failures {5}, blocked {6}, executed with blocked testcases {7}" -f $Configuration, $Suite, (Format-AvbExitCode $rc), $res.Doc.overall, $res.Doc.executedCount, $nf, $nb, $nt)
                if ($s.cleanupCounts) {
                    $cc = $s.cleanupCounts
                    Write-Host ("{0} / {1}: cleanup (separate from functional results): FAILED {2}, NOT_VERIFIED {3}, SUCCESS {4}, NOT_REQUIRED {5} --- details: {6} 'cleanup'" -f $Configuration, $Suite, $cc.FAILED, $cc.NOT_VERIFIED, $cc.SUCCESS, $cc.NOT_REQUIRED, $json) -ForegroundColor $(if ($cc.FAILED -or $cc.NOT_VERIFIED) { 'Yellow' } else { 'Gray' })
                }
                $h = Get-AvbSafetyHalt $res.Doc
                if ($h) {
                    Write-Host "[EVIDENCE] $Configuration/${Suite}: $h --- kept; later suites and configurations still run, and the runner treats only a process still running NOW as a conflict (#458)" -ForegroundColor Red
                    if (-not $SafetyHalt) { $SafetyHalt = "$Configuration/${Suite}: $h" }
                }
                # Infrastructure loss is recorded, not a stop (861e318: only an explicit operator stop ends the matrix).
                $why = Get-AvbStopReason $res.Doc
                if ($why) { $s.infrastructure = $why; Write-Host "[INFRA] $Configuration/${Suite}: $why --- recorded; the next suite runs" -ForegroundColor Red }
            }
            if (@($inv | Where-Object { $_.Category -in @('Failure', 'Blocked') }).Count -gt 0 -and
                (Read-AvbTimedChoice -Prompt "$Configuration/${Suite} has failures. Press S to stop the run, Enter to continue (3 s, default continue):" -Seconds 3 -Default 'continue' -Keys @{ S = 'stop'; C = 'continue'; Enter = 'continue' }) -eq 'stop') { $Stop = "operator stop after $Configuration/$Suite"; break }
        }
        $AllInventory += $cfgInventory
        if ($Stop) { break }

        # 7. Reproduction (this configuration's driver is installed; operator selects each item)
        if (-not $NoReproduce) {
            foreach ($item in (Get-AvbReproductionQueue -Inventory $cfgInventory -Configuration $Configuration)) {
                Write-Host "`n$Configuration / $($item.Suite) / $($item.Entry) $($item.Arguments)  [$($item.Verdict)] $($item.Testcases)"
                if ((Read-AvbTimedChoice -Prompt "Reproduce with Run-Tests-Elevated.ps1 -CaptureDbgView? (Y/n, 3 s, default Y):" -Seconds 3 -Default 'y' -Keys @{ Y = 'y'; Enter = 'y'; N = 'n' }) -ne 'y') { continue }

                if (-not (Test-AvbExitSuccess (Invoke-AvbPreFlight $cfg.builtDriverSha256))) {
                    # Recorded; the remaining reproductions of THIS configuration need its verified driver, the next
                    # configuration still runs (#458; was: the whole run stopped).
                    $cfg.reproductionsNotExecuted = "pre-flight failed before reproduction ($Configuration)"
                    $NotExecuted += "$Configuration reproductions: pre-flight failed before reproduction"
                    Write-Host "[NOT_EXECUTED] $Configuration reproductions: pre-flight failed --- recorded; the next configuration runs" -ForegroundColor Red
                    break
                }
                $repJson = Join-Path $ReportDir ("repro-{0}-{1}-{2}.json" -f $Configuration, ($item.Entry -replace '[^\w.-]', '_'), (Get-Date -Format 'HHmmss'))
                $rrc = Invoke-AvbStep "reproduce $($item.Entry)" {
                    & $RunElevated -TestName $item.Entry -Suite $item.Suite -Configuration $Configuration -CaptureDbgView -ResultsJson $repJson }
                $cfg.reproductions += [ordered]@{ suite = $item.Suite; entry = $item.Entry; arguments = $item.Arguments; originalVerdict = $item.Verdict
                                                  exitCode = $rrc; resultsJson = $repJson }
            }
        }
    }
} finally {
    $rel = Exit-AvbHilLock -Owner $HilLock.Owner
    if (-not $rel.Ok) { Write-Host "[WARN] HIL lock not released: $($rel.Why)" -ForegroundColor Yellow }
    $Report.finishedUtc = (Get-Date).ToUniversalTime().ToString('o')
    $Report.stopped = $(if ($Stop) { $Stop } else { $null })
    $Report.safetyHalt = $(if ($SafetyHalt) { $SafetyHalt } else { $null })
    $Report.inventory = @($AllInventory)
    ($Report | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath (Join-Path $ReportDir 'local-suites-report.json') -Encoding UTF8

    Write-Host "`n================ LOCAL SUITES REPORT (LOCAL_DIAGNOSTIC evidence, never CI evidence) ================" -ForegroundColor Cyan
    foreach ($c in $Report.configurations) {
        Write-Host ("{0}: HEAD {1}, provenance {2}, digest {3}, built driver {4}, test build {5}, install {6}, pre-flight {7}" -f $c.configuration, $c.head, $c.provenanceMode,
                    $c.buildInputDigest, $c.builtDriverSha256, (Format-AvbStepResult $c.testBuild), (Format-AvbStepResult $c.install), (Format-AvbStepResult $c.preflight))
        foreach ($f in @($c.testBuildFailed)) { Write-Host "  build FAILED: $f" -ForegroundColor Red }
        foreach ($s in $c.suites) { Write-Host ("  {0,-20} exit {1,-4} overall {2,-13} executed {3}{4}" -f $s.suite, (Format-AvbExitCode $s.exitCode), $s.overall, $s.executed, $(if ($s.error) { "  ERROR: $($s.error)" } else { '' })) }
        foreach ($r in $c.reproductions) { Write-Host ("  reproduction {0} {1}: exit {2} ({3})" -f $r.entry, $r.arguments, (Format-AvbExitCode $r.exitCode), $r.resultsJson) }
    }
    $fail = @($AllInventory | Where-Object { $_.Category -eq 'Failure' }); $blk = @($AllInventory | Where-Object { $_.Category -eq 'Blocked' })
    Write-Host "`nExecuted failures ($($fail.Count)):"
    $fail | Sort-Object Configuration, Suite, Name | Format-Table Configuration, Suite, Name, Verdict, ExitCode, Testcases, Incidents -AutoSize | Out-String -Width 250 | Write-Host
    $ci = Get-AvbCleanupIncidentSummary -Inventory $AllInventory
    if ($ci.Observations -gt 0) { Write-Host ("Cleanup incidents: {0} unique hardware discrepancy incident(s), {1} verification observation(s)" -f $ci.Incidents, $ci.Observations) -ForegroundColor Red }
    $btc = @($AllInventory | Where-Object { $_.Category -eq 'BlockedTestcases' })
    Write-Host "Executed, no functional failure, testcases BLOCKED - nonpassing ($($btc.Count)):"
    $btc | Sort-Object Configuration, Suite, Name | Format-Table Configuration, Suite, Name, Verdict, ExitCode, Testcases -AutoSize | Out-String -Width 250 | Write-Host
    Write-Host "Not executed - blocked ($($blk.Count)):"
    $blk | Sort-Object Configuration, Suite, Name | Format-Table Configuration, Suite, Name, Verdict, Reason -AutoSize | Out-String -Width 250 | Write-Host
    $gap = @($AllInventory | Where-Object { $_.Category -eq 'NotExecuted' })
    Write-Host "Required but NOT executed - coverage gaps, run is INCOMPLETE ($($gap.Count)):"
    $gap | Sort-Object Configuration, Suite, Name | Format-Table Configuration, Suite, Name, Reason -AutoSize | Out-String -Width 250 | Write-Host
    foreach ($b in $BuildFailures) { Write-Host "[BUILD FAILED] $b --- the run is nonpassing" -ForegroundColor Red }
    if ($SafetyHalt) { Write-Host "[EVIDENCE] $SafetyHalt --- loss of controlled execution in that suite; later tests still ran; the run is nonpassing" -ForegroundColor Red }
    foreach ($n in $NotExecuted) { Write-Host "[NOT_EXECUTED] $n --- the run is nonpassing" -ForegroundColor Red }
    if ($Stop) { Write-Host "[STOPPED] $Stop" -ForegroundColor Red }
    Write-Host "Report: $(Join-Path $ReportDir 'local-suites-report.json')"
}

$executed = @($AllInventory | Where-Object { $_.Executed }).Count
if ($Stop -or $SafetyHalt -or $NotExecuted.Count -gt 0 -or $BuildFailures.Count -gt 0 -or @($AllInventory | Where-Object { $_.Category -ne 'NotFailure' }).Count -gt 0 -or $executed -eq 0) { exit 1 }
exit 0
