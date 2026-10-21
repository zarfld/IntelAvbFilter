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
#
# Verdicts: DEPLOYED | NOT_EXECUTED_BUSY | REJECTED_PACKAGE | DEPLOY_FAILED | NEEDS_OPERATOR.  A deployment is
# "pre-deployment absent driver" tolerant: an absent or different installed driver is replaced, never a failure.
# The running image is not hashed: after deployment "loaded image == installed file" rests on the observed
# unload (service released) and reload (RUNNING) during this deployment - disclosed in the record.

param(
    [ValidateSet('Deploy', 'Release')]
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
    [switch]$SkipTrustCheck   # offline tests only: synthetic packages are not signed
)

$ErrorActionPreference = 'Stop'
if ($SkipTrustCheck -and $env:GITHUB_ACTIONS -eq 'true') { Write-Host "[HIL-DEPLOY] -SkipTrustCheck is an offline-test seam and is refused under GitHub Actions" -ForegroundColor Red; exit 1 }
$RepoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
foreach ($lib in 'NativeExit.ps1', 'HilLock.ps1', 'HilMachineHealth.ps1', 'DriverPackage.ps1') { . (Join-Path $RepoRoot "tools\lib\$lib") }
if (-not $InstallScript)     { $InstallScript = Join-Path $RepoRoot 'tools\setup\Install-Driver.ps1' }
if (-not $PreFlightScript)   { $PreFlightScript = Join-Path $RepoRoot 'tools\test\Test-DriverPreFlight.ps1' }
if (-not $LockPath)          { $LockPath = Get-AvbHilLockPath }
if (-not $PinnedCertificate) { $PinnedCertificate = Join-Path $RepoRoot 'tools\ci\signing\hil-test-signer.cer' }
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

$record = [ordered]@{
    schema = 'avb-hil-deployment/1'; configuration = $Configuration; verdict = $null; reason = ''
    authorized = [ordered]@{ revision = $ExpectedRevision; repository = $ExpectedRepository; runId = $ExpectedRunId; runAttempt = $ExpectedRunAttempt; driverSha256 = $ExpectedDriverSha256 }
    runStartUtc = (Get-Date).ToUniversalTime().ToString('o'); lock = $null; crashEvidence = $null; package = $null
    install = $null; installExitCode = $null; preflightExitCode = $null; finishedUtc = $null
    loadedImageEvidence = 'not hashed; DEPLOYED requires the service to be released (unloaded) and to reach RUNNING again during this deployment'
}
function Complete-Deployment([string]$Verdict, [string]$Reason, [int]$Code) {
    $record.verdict = $Verdict; $record.reason = $Reason; $record.finishedUtc = (Get-Date).ToUniversalTime().ToString('o')
    ($record | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath (Join-Path $EvidenceDir 'deployment.json') -Encoding UTF8
    $c = if ($Code -eq 0) { 'Green' } else { 'Red' }
    Write-Host "[HIL-DEPLOY] $Configuration $Verdict - $Reason" -ForegroundColor $c
    exit $Code
}

# 1. Serialization
$lock = Enter-AvbHilLock -Kind 'ci' -Label "hil $Configuration" -Path $LockPath -WaitSeconds $LockWaitSeconds
$record.lock = [ordered]@{ path = $LockPath; ok = $lock.Ok; why = $lock.Why; waitedSeconds = $lock.Waited; previousOwner = $lock.PreviousOwner }
if (-not $lock.Ok) { Complete-Deployment 'NOT_EXECUTED_BUSY' "HIL machine busy: $($lock.Why)" 1 }
($lock.Owner | ConvertTo-Json) | Set-Content -LiteralPath $ownerFile -Encoding UTF8

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

# 4. Unattended installation through the canonical installer
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
