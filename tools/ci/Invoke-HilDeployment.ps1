# Invoke-HilDeployment.ps1 - deploy one authorized staged driver package on the HIL runner (.github/workflows/hil.yml).
#
#   -Action Deploy   1 machine lock (tools/lib/HilLock.ps1; held until -Action Release)
#                    2 crash evidence (tools/lib/HilMachineHealth.ps1; attributes an interrupted previous run)
#                    3 package verification (tools/lib/DriverPackage.ps1): manifest, exact files + SHA256, authorized
#                      revision / repository / upstream run / attempt / configuration / staged SHA256, pinned signer,
#                      signature trusted on this machine - BEFORE anything touches the installed driver
#                    4 tools/setup/Install-Driver.ps1 -NonInteractive -Reinstall (bounded, no reboot, no prompts)
#                    5 tools/test/Test-DriverPreFlight.ps1 with HIL_REQUIRE_DEPLOYED_SHA=1 (installed == staged,
#                      service RUNNING, device node)
#                    -> <EvidenceDir>\deployment.json; exit 0 only for DEPLOYED.
#   -Action Release  releases the lock taken by this CI job (always() step).
#   -Action Recover  OPERATOR interface (refused under GitHub Actions), elevated:
#                      .\tools\ci\Invoke-HilDeployment.ps1 -Action Recover -EvidenceDir <new-evidence-directory>
#                    takes the machine lock (no wait unless -LockWaitSeconds), observes the CURRENT machine state and
#                    records the readiness decision (<lock>.recovery-<utc>.json + a NEW attempt directory
#                    <EvidenceDir>\recover-<utc>-pid<PID>\ with recovery.json, lock-release.json and SHA256SUMS, each
#                    written once; the directory is printed); it never rewrites the incident or earlier evidence and
#                    never installs anything.  Exit 0 READY | 3 NOT_READY | 1 busy/refused.
#
# Local evidence that survives a lost job: before the installer runs (netcfg can interrupt the runner's network
# connection, #328), the record so far is written next to the lock (<lock>.inflight.json) and replaced by the final
# record afterwards.  RUNNER_TEMP is emptied when the next job starts; this file is not.  The next deployment attaches
# it as previousDeployment, together with the stale-lock owner and the crash evidence.
# A previous record that is not machineOk (NEEDS_OPERATOR, IN_FLIGHT, unreadable) is history, not the current state
# (#493): Deploy reconciles it under its OWN lock - held on through installation and verification, so there is no
# unlocked gap - from current observations (tools/lib/DriverDeploy.ps1 Invoke-AvbHilReconciliation: no surviving
# netcfg/pnputil/wevtutil or deployment script, service settled, installed driver identity known, no pending
# replacement, bindings readable).  READY: the incident is archived byte-for-byte (<lock>.incident-<utc>.json) and the
# staged package goes through the normal verification and installation.  Otherwise NEEDS_OPERATOR names the current
# condition and the incident stays untouched.
# Verdicts: DEPLOYED | NOT_EXECUTED_BUSY | REJECTED_PACKAGE | DEPLOY_FAILED | NEEDS_OPERATOR.  A deployment is
# "pre-deployment absent driver" tolerant: an absent or different installed driver is replaced, never a failure.
# The running image is not hashed: after deployment "loaded image == installed file" rests on the observed
# unload (service released) and reload (RUNNING) during this deployment - disclosed in the record.

param(
    [ValidateSet('Deploy', 'Release', 'Recover')]
    [string]$Action = 'Deploy',
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration,
    [string]$PackageDir,
    [string]$ExpectedRevision,
    [string]$ExpectedRepository,
    [string]$ExpectedRunId,
    [string]$ExpectedRunAttempt,
    [string]$ExpectedDriverSha256,
    [string]$EvidenceDir,
    [string]$PinnedCertificate = '',
    [int]$LockWaitSeconds = 3600,
    [int]$StepTimeoutSeconds = 180,
    # Test seams (defaults are the real scripts / lock path).
    [string]$InstallScript = '',
    [string]$PreFlightScript = '',
    [string]$LockPath = '',
    [string]$ProbeScript = '', # offline tests only: replaces the machine observation of the recovery reconciliation
    [switch]$SkipTrustCheck   # offline tests only: synthetic packages are not signed
)

$ErrorActionPreference = 'Stop'
if ($SkipTrustCheck -and $env:GITHUB_ACTIONS -eq 'true') { Write-Host "[HIL-DEPLOY] -SkipTrustCheck is an offline-test seam and is refused under GitHub Actions" -ForegroundColor Red; exit 1 }
if ($ProbeScript -and $env:GITHUB_ACTIONS -eq 'true') { Write-Host "[HIL-DEPLOY] -ProbeScript is an offline-test seam and is refused under GitHub Actions" -ForegroundColor Red; exit 1 }
$RepoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
foreach ($lib in 'NativeExit.ps1', 'HilLock.ps1', 'HilMachineHealth.ps1', 'DriverPackage.ps1', 'DriverDeploy.ps1') { . (Join-Path $RepoRoot "tools\lib\$lib") }
if (-not $InstallScript)     { $InstallScript = Join-Path $RepoRoot 'tools\setup\Install-Driver.ps1' }
if (-not $PreFlightScript)   { $PreFlightScript = Join-Path $RepoRoot 'tools\test\Test-DriverPreFlight.ps1' }
if (-not $LockPath)          { $LockPath = Get-AvbHilLockPath }
if (-not $PinnedCertificate) { $PinnedCertificate = Join-Path $RepoRoot 'tools\ci\signing\hil-test-signer.cer' }
$inflightFile = "$LockPath.inflight.json"
New-Item -ItemType Directory -Path $EvidenceDir -Force | Out-Null
$ownerFile = Join-Path $EvidenceDir 'hil-lock-owner.json'

if ($Action -eq 'Release') {
    if (-not (Test-Path -LiteralPath $ownerFile)) { Write-Host "[LOCK] no lock taken by this job (nothing to release)"; exit 0 }
    $owner = Get-Content -LiteralPath $ownerFile -Raw | ConvertFrom-Json
    $r = Exit-AvbHilLock -Owner $owner -Path $LockPath
    if ($r.Ok) { Write-Host "[LOCK] released"; exit 0 }
    Write-Host "[LOCK] NOT released: $($r.Why)" -ForegroundColor Red
    exit 1
}

# Machine observation for the recovery reconciliation: the real read-only probe, or the offline seam.  No
# GetNewClosure(): it re-validates every captured variable and fails on the unbound ValidateSet $Configuration (Recover).
$observe = if ($ProbeScript) { { param([int[]]$WatchPids) & $script:ProbeScript -WatchPids $WatchPids } }
           else { { param([int[]]$WatchPids) Get-AvbHilRecoveryObservation -WatchPids $WatchPids } }

# Per-attempt evidence for -Action Recover (2026-11-20: a later run with the same -EvidenceDir overwrote an earlier
# attempt's recovery.json).  Every invocation gets its own directory under the requested one; each file in it is written
# once; nothing that already exists there is replaced.  Reusing a requested directory never blocks recovery.
function New-AvbRecoverAttemptDir([string]$Root) {
    $base = Join-Path $Root ('recover-{0}-pid{1}' -f [datetime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'), $PID)
    $dir = $base; $n = 1
    while (Test-Path -LiteralPath $dir) { $n++; $dir = "$base-$n" }
    New-Item -ItemType Directory -Path $dir -ErrorAction Stop | Out-Null
    return $dir
}
function Write-AvbNewEvidenceFile([string]$Path, [string]$Text) {
    $fs = [System.IO.File]::Open($Path, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    try { $b = [System.Text.Encoding]::UTF8.GetBytes($Text); $fs.Write($b, 0, $b.Length) } finally { $fs.Dispose() }
}
function Write-AvbAttemptManifest([string]$Dir) {
    $lines = @(Get-ChildItem -LiteralPath $Dir -File | Where-Object Name -ne 'SHA256SUMS' | Sort-Object Name |
               ForEach-Object { '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash, $_.Name })
    Write-AvbNewEvidenceFile -Path (Join-Path $Dir 'SHA256SUMS') -Text (($lines -join "`r`n") + "`r`n")
}

if ($Action -eq 'Recover') {
    if ($env:GITHUB_ACTIONS -eq 'true') { Write-Host "[HIL-RECOVER] -Action Recover is the operator interface and is refused under GitHub Actions; CI reconciles inside -Action Deploy under its own lock" -ForegroundColor Red; exit 1 }
    $attemptDir = New-AvbRecoverAttemptDir -Root $EvidenceDir
    Write-Host "[HIL-RECOVER] evidence: $attemptDir"
    $wait = if ($PSBoundParameters.ContainsKey('LockWaitSeconds')) { $LockWaitSeconds } else { 0 }
    $rl = Enter-AvbHilLock -Kind 'local' -Label 'hil recover' -Path $LockPath -WaitSeconds $wait
    if (-not $rl.Ok) {
        Write-AvbNewEvidenceFile -Path (Join-Path $attemptDir 'recovery.json') -Text ([ordered]@{ schema = 'avb-hil-recovery/1'; mode = 'operator'; host = $env:COMPUTERNAME
            decision = 'NOT_EXECUTED_BUSY'; reasons = @("HIL machine busy: $($rl.Why)"); recordPath = $null } | ConvertTo-Json -Depth 6)
        Write-AvbAttemptManifest -Dir $attemptDir
        Write-Host "[HIL-RECOVER] NOT_EXECUTED_BUSY - HIL machine busy: $($rl.Why) (nothing observed, nothing written next to the lock; evidence: $attemptDir)" -ForegroundColor Red
        exit 1
    }
    $code = 1; $rc = $null
    try {
        $rc = Invoke-AvbHilReconciliation -LockPath $LockPath -Mode 'operator' -LockOwner $rl.Owner -PreviousLockOwner $rl.PreviousOwner -Observe $observe -EvidenceDir $attemptDir
        $inc = $rc.incident
        Write-Host ("[HIL-RECOVER] incident: {0}" -f $(if (-not $inc.present) { 'none' } elseif (-not $inc.readable) { "unreadable record $($inc.path) (SHA256 $($inc.sha256))" }
                                                     else { "$($inc.verdict) $($inc.configuration) run $($inc.runId)/$($inc.runAttempt) revision $($inc.revision) (SHA256 $($inc.sha256), left unchanged)" }))
        foreach ($why in @($rc.reasons)) { Write-Host "[HIL-RECOVER]   current condition: $why" -ForegroundColor Red }
        Write-Host "[HIL-RECOVER] recovery record: $($rc.recordPath); evidence copy: $(Join-Path $attemptDir 'recovery.json')"
        if ($rc.decision -eq 'READY') {
            Write-Host "[HIL-RECOVER] READY - the machine is ready for a new deployment now; the next Deploy re-establishes readiness under its own lock, archives the incident and verifies the staged package through the normal path" -ForegroundColor Green
            $code = 0
        } else {
            Write-Host "[HIL-RECOVER] NOT_READY - resolve the condition(s) above, then run -Action Recover again" -ForegroundColor Red
            $code = 3
        }
    } finally {
        $rel = Exit-AvbHilLock -Owner $rl.Owner -Path $LockPath
        if ($rel.Ok) { Write-Host "[LOCK] released" } else { Write-Host "[LOCK] NOT released: $($rel.Why)" -ForegroundColor Red; $code = 1 }
        # Both records stay as written while the lock was held; the release outcome is its own file, then the manifest.
        Write-AvbNewEvidenceFile -Path (Join-Path $attemptDir 'lock-release.json') -Text ([ordered]@{ ok = [bool]$rel.Ok; why = $rel.Why; utc = [datetime]::UtcNow.ToString('o')
            recordPath = $(if ($rc) { $rc.recordPath } else { $null }) } | ConvertTo-Json -Depth 4)
        Write-AvbAttemptManifest -Dir $attemptDir
        Write-Host "[HIL-RECOVER] evidence written: $attemptDir (recovery.json, lock-release.json, SHA256SUMS)"
    }
    exit $code
}

$record = [ordered]@{
    schema = 'avb-hil-deployment/1'; configuration = $Configuration; verdict = $null; reason = ''
    authorized = [ordered]@{ revision = $ExpectedRevision; repository = $ExpectedRepository; runId = $ExpectedRunId; runAttempt = $ExpectedRunAttempt; driverSha256 = $ExpectedDriverSha256 }
    runStartUtc = (Get-Date).ToUniversalTime().ToString('o'); lock = $null; crashEvidence = $null; package = $null
    install = $null; installExitCode = $null; preflightExitCode = $null; finishedUtc = $null
    loadedImageEvidence = 'not hashed; DEPLOYED requires the service to be released (unloaded) and to reach RUNNING again during this deployment'
    previousDeployment = $null; recovery = $null; machineOk = $null; machineOkReason = ''
}
# Never replace an unsafe predecessor's machine-local evidence with a new nonmutating attempt.
$preservePriorInflight = $false
function Complete-Deployment([string]$Verdict, [string]$Reason, [int]$Code) {
    $record.verdict = $Verdict; $record.reason = $Reason; $record.finishedUtc = (Get-Date).ToUniversalTime().ToString('o')
    # Re-evaluated from the record (state after a partial deployment), not from the verdict name alone.
    $m = Get-AvbMachineOk -Record $record; $record.machineOk = [bool]$m.Ok; $record.machineOkReason = $m.Reason
    ($record | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath (Join-Path $EvidenceDir 'deployment.json') -Encoding UTF8
    if ($record.lock -and $record.lock.ok -and -not $preservePriorInflight) { ($record | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $inflightFile -Encoding UTF8 }
    $c = if ($Code -eq 0) { 'Green' } else { 'Red' }
    Write-Host "[HIL-DEPLOY] $Configuration $Verdict - $Reason" -ForegroundColor $c
    exit $Code
}

# 1. Serialization
$lock = Enter-AvbHilLock -Kind 'ci' -Label "hil $Configuration" -Path $LockPath -WaitSeconds $LockWaitSeconds
$record.lock = [ordered]@{ path = $LockPath; ok = $lock.Ok; why = $lock.Why; waitedSeconds = $lock.Waited; previousOwner = $lock.PreviousOwner }
if (-not $lock.Ok) { Complete-Deployment 'NOT_EXECUTED_BUSY' "HIL machine busy: $($lock.Why)" 1 }
($lock.Owner | ConvertTo-Json) | Set-Content -LiteralPath $ownerFile -Encoding UTF8
if (Test-Path -LiteralPath $inflightFile) {
    try { $record.previousDeployment = Get-Content -LiteralPath $inflightFile -Raw | ConvertFrom-Json }
    catch { $record.previousDeployment = "unreadable: $($_.Exception.Message)" }

    # The persisted record is machine-wide and outlives RUNNER_TEMP/CI jobs.
    # In particular, a timed-out netcfg/wevtutil child may still be running after its CI worker and lock exit.
    # A new Debug/Release leg must NOT infer recovery merely from owning a newly acquired lock - nor stay vetoed by a
    # historical verdict (#493): it re-establishes readiness from current observations under this lock.
    $prior = $record.previousDeployment
    $priorValid = ($null -ne $prior -and $prior -isnot [string] -and
                   $null -ne $prior.PSObject.Properties['schema'] -and $prior.schema -ceq 'avb-hil-deployment/1' -and
                   $null -ne $prior.PSObject.Properties['machineOk'])
    $priorState = if ($priorValid) { Get-AvbMachineOk -Record $prior } else { @{ Ok = $false; Reason = 'unreadable or incomplete previous deployment record' } }
    if (-not $priorValid -or $prior.machineOk -ne $true -or -not $priorState.Ok) {
        $oldVerdict = if ($priorValid) { [string]$prior.verdict } else { 'UNVERIFIABLE' }
        $rc = Invoke-AvbHilReconciliation -Mode 'automatic' -LockPath $LockPath -LockOwner $lock.Owner -PreviousLockOwner $lock.PreviousOwner -Observe $observe -EvidenceDir $EvidenceDir -ArchiveIncident
        $record.recovery = $rc
        if ($rc.decision -ne 'READY') {
            $preservePriorInflight = $true
            Complete-Deployment 'NEEDS_OPERATOR' ("previous HIL deployment $oldVerdict is not recovered ($($priorState.Reason)); current readiness not established: " +
                                                  ((@($rc.reasons)) -join '; ') + "; incident preserved unchanged at $inflightFile; recovery attempt recorded at $($rc.recordPath)") 3
        }
        Write-Host "[HIL-DEPLOY] previous deployment $oldVerdict reconciled: machine ready now (incident archived at $($rc.incident.archivePath); recovery record $($rc.recordPath)); the staged package is verified and deployed through the normal path" -ForegroundColor Yellow
    }
}

# 2. Crash evidence (automatic; never an operator acknowledgement)
try {
    $record.crashEvidence = Get-AvbCrashEvidence -Source (New-AvbRealHealthSource) -PreviousOwner $lock.PreviousOwner `
                                -RunStartUtc ([datetime]::UtcNow) -EvidenceDir $EvidenceDir
} catch {
    $record.crashEvidence = [ordered]@{ classification = 'UNAVAILABLE'; summary = @("crash evidence collection failed: $($_.Exception.Message)") }
}

# 3. Package verification (installed driver untouched on any failure)
$pin = Get-AvbPinnedSigner -CertificatePath $PinnedCertificate
$pv = Test-AvbDriverPackage -PackageDir $PackageDir -Configuration $Configuration -ExpectedRevision $ExpectedRevision -ExpectedRepository $ExpectedRepository `
          -ExpectedRunId $ExpectedRunId -ExpectedRunAttempt $ExpectedRunAttempt -ExpectedDriverSha256 $ExpectedDriverSha256 `
          -PinnedThumbprint $pin.Thumbprint -RequireTrusted:(-not $SkipTrustCheck)
$reasons = @($pv.Reasons); if (-not $pin.Ok) { $reasons = @($pin.Error) + $reasons }
$record.package = [ordered]@{ dir = $PackageDir; ok = ($pv.Ok -and $pin.Ok); reasons = $reasons; driverSha256 = $pv.DriverSha256; pinnedThumbprint = $pin.Thumbprint }
if (-not ($pv.Ok -and $pin.Ok)) { Complete-Deployment 'REJECTED_PACKAGE' ("staged package rejected (installed driver untouched): " + ($reasons -join '; ')) 1 }

# 4. Unattended installation through the canonical installer (evidence persisted locally first)
$record.verdict = 'IN_FLIGHT'; $record.reason = 'installation started; if this is the last record, the job was lost during installation'
($record | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $inflightFile -Encoding UTF8
($record | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath (Join-Path $EvidenceDir 'deployment.json') -Encoding UTF8
$installJson = Join-Path $EvidenceDir 'install.json'
Reset-AvbExitCode
& powershell.exe -NoProfile -ExecutionPolicy Bypass -NonInteractive -File $InstallScript -Configuration $Configuration -Reinstall -NonInteractive `
    -PackageDir $PackageDir -ExpectedSysSha256 $ExpectedDriverSha256 -StepTimeoutSeconds $StepTimeoutSeconds -ResultJson $installJson
$rc = Get-AvbExitCode
$record.installExitCode = $rc
if (Test-Path -LiteralPath $installJson) { $record.install = Get-Content -LiteralPath $installJson -Raw | ConvertFrom-Json }
if ($rc -eq 3) { Complete-Deployment 'NEEDS_OPERATOR' "installation stopped in a state only a reboot or the operator can resolve (no reboot performed): $(if ($record.install) { $record.install.reason })" 3 }
if (-not (Test-AvbExitSuccess $rc) -or $null -eq $record.install -or $record.install.outcome -ne 'DEPLOYED') {
    Complete-Deployment 'DEPLOY_FAILED' "installation failed (exit $(Format-AvbExitCode $rc)): $(if ($record.install) { $record.install.reason } else { 'no installation record' })" 1
}

# 5. Post-deployment verification through the existing pre-flight
$saveB = $env:BUILD_DRIVER_SHA256; $saveR = $env:HIL_REQUIRE_DEPLOYED_SHA
try {
    $env:BUILD_DRIVER_SHA256 = $ExpectedDriverSha256; $env:HIL_REQUIRE_DEPLOYED_SHA = '1'
    Reset-AvbExitCode
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -NonInteractive -File $PreFlightScript
    $pf = Get-AvbExitCode
} finally { $env:BUILD_DRIVER_SHA256 = $saveB; $env:HIL_REQUIRE_DEPLOYED_SHA = $saveR }
$record.preflightExitCode = $pf
if (-not (Test-AvbExitSuccess $pf)) { Complete-Deployment 'DEPLOY_FAILED' "post-deployment pre-flight failed (exit $(Format-AvbExitCode $pf)): installed driver, service or device node not as deployed" 1 }

Complete-Deployment 'DEPLOYED' "staged $Configuration driver $ExpectedDriverSha256 installed and verified" 0
