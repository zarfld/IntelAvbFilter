# test_ci_driver_deploy.ps1
# TEST-CIDEPLOY-*: unattended staged-driver deployment for automatic HIL (offline; orchestration correctness only).
#
# Under test (real code, scripted fakes for every system effect):
#   tools/lib/DriverDeploy.ps1       Invoke-AvbCiDriverDeployment (Install-Driver.ps1 -NonInteractive)
#   tools/lib/DriverPackage.ps1      staged package manifest + verification (signer injected)
#   tools/lib/HilLock.ps1            machine-wide serialization
#   tools/lib/HilMachineHealth.ps1   automatic crash evidence (replaces HIL_BUGCHECK_ACK)
#   tools/ci/Invoke-HilDeployment.ps1  order: lock -> crash evidence -> package -> install -> pre-flight
# These fakes prove ORCHESTRATION only.  Driver installation, NDIS binding, unload/reload and #328 behaviour on the
# real machine are UNVERIFIED until the first controlled physical deployment.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent

$total = 0; $passed = 0; $failed = 0
function tc_pass { param([string]$n) $script:total++; $script:passed++; Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red }
function tc_check { param([string]$n, [bool]$ok, [string]$why) if ($ok) { tc_pass $n } else { tc_fail $n $why } }

Write-Host ""
Write-Host "TEST-CIDEPLOY: unattended staged-driver deployment (offline orchestration)" -ForegroundColor Cyan
$Tmp = Join-Path $env:TEMP ("cideploy_{0}" -f (Get-Random))
New-Item -ItemType Directory -Path $Tmp -Force | Out-Null

$missing = ''
try {
    foreach ($lib in 'NativeExit.ps1', 'DriverDeploy.ps1', 'DriverPackage.ps1', 'HilLock.ps1', 'HilMachineHealth.ps1') { . (Join-Path $RepoRoot "tools\lib\$lib") }
} catch { $missing = $_.Exception.Message }
tc_check 'TEST-CIDEPLOY-000-libraries-present' ($missing -eq '') $missing
if ($missing) { Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total); exit 1 }

# ---------------------------------------------------------------------------------------------------------
# Synthetic package
# ---------------------------------------------------------------------------------------------------------
function New-Package([string]$Dir, [string]$SysContent = 'MZ staged driver') {
    New-Item -ItemType Directory -Path $Dir -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $Dir 'IntelAvbFilter.inf') -Value '[Version]' -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $Dir 'IntelAvbFilter.sys') -Value $SysContent -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $Dir 'intelavbfilter.cat') -Value 'catalog' -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $Dir 'IntelAvbFilter.man') -Value '<manifest/>' -Encoding ASCII
    return (Get-FileHash -LiteralPath (Join-Path $Dir 'IntelAvbFilter.sys')).Hash
}
$Pin = ('AB' * 20)
$sigPinned  = { param($p) @{ Status = 'Valid'; Thumbprint = ('AB' * 20); Subject = 'CN=IntelAvbFilter HIL Test Signing' } }
$sigOther   = { param($p) @{ Status = 'Valid'; Thumbprint = ('CD' * 20); Subject = 'CN="WDKTestCert runneradmin,1"' } }
$sigUntrust = { param($p) @{ Status = 'UnknownError'; Thumbprint = ('AB' * 20); Subject = 'CN=IntelAvbFilter HIL Test Signing' } }
$sigNone    = { param($p) @{ Status = 'NotSigned'; Thumbprint = $null; Subject = $null } }
$Rev = ('a' * 40)
function New-StagedPackage([string]$Name, [string]$Cfg = 'Debug', [string]$SysContent = 'MZ staged driver') {
    $d = Join-Path $Tmp $Name
    $sha = New-Package $d $SysContent
    $null = New-AvbDriverPackageManifest -PackageDir $d -Configuration $Cfg -SourceRevision $Rev -Repository 'zarfld/IntelAvbFilter' `
                -Workflow '.github/workflows/ci-standards-compliance.yml' -RunId '1234' -RunAttempt '1' -GetSignature $sigPinned
    return @{ Dir = $d; Sha = $sha }
}
function Test-Pkg($p, [hashtable]$Over = @{}, [scriptblock]$Sig = $sigPinned) {
    $a = @{ PackageDir = $p.Dir; Configuration = 'Debug'; ExpectedRevision = $Rev; ExpectedRepository = 'zarfld/IntelAvbFilter'; ExpectedRunId = '1234'
            ExpectedRunAttempt = '1'; ExpectedDriverSha256 = $p.Sha; PinnedThumbprint = $Pin; RequireTrusted = $true; GetSignature = $Sig }
    foreach ($k in $Over.Keys) { $a[$k] = $Over[$k] }
    return (Test-AvbDriverPackage @a)
}

# --- CI case 2/9/15: package identity ------------------------------------------------------------------------
$pOk = New-StagedPackage 'pkg_ok'
$v0 = Test-Pkg $pOk
tc_check 'TEST-CIDEPLOY-001-authorized-signed-package-of-the-exact-run-verifies' ($v0.Ok -and $v0.DriverSha256 -eq $pOk.Sha) ($v0.Reasons -join '; ')
$cases = [ordered]@{
    'other run id'        = @{ ExpectedRunId = '9999' }
    'other attempt'       = @{ ExpectedRunAttempt = '2' }
    'other revision'      = @{ ExpectedRevision = ('b' * 40) }
    'other configuration' = @{ Configuration = 'Release' }
    'other staged sha'    = @{ ExpectedDriverSha256 = ('0' * 64) }
    'no pinned signer'    = @{ PinnedThumbprint = '' }
}
$notRejected = @(foreach ($k in $cases.Keys) { $v = Test-Pkg $pOk $cases[$k]; if ($v.Ok) { $k } })
tc_check 'TEST-CIDEPLOY-002-package-from-another-run-attempt-revision-configuration-or-sha-is-rejected' ($notRejected.Count -eq 0) "accepted: $($notRejected -join ', ')"
$vSigOther = Test-Pkg $pOk @{} $sigOther; $vUntrust = Test-Pkg $pOk @{} $sigUntrust; $vNone = Test-Pkg $pOk @{} $sigNone
tc_check 'TEST-CIDEPLOY-003-wrong-signer-untrusted-or-unsigned-package-is-rejected' `
    ((-not $vSigOther.Ok) -and (($vSigOther.Reasons -join ' ') -match 'not the pinned HIL signer') -and (-not $vUntrust.Ok) -and (($vUntrust.Reasons -join ' ') -match 'never imported') -and
     (-not $vNone.Ok) -and (($vNone.Reasons -join ' ') -match 'not signed')) ("other={0} | untrusted={1} | none={2}" -f ($vSigOther.Reasons -join ';'), ($vUntrust.Reasons -join ';'), ($vNone.Reasons -join ';'))
$pTamper = New-StagedPackage 'pkg_tamper'; Add-Content -LiteralPath (Join-Path $pTamper.Dir 'IntelAvbFilter.inf') -Value '; tampered'
$pExtra = New-StagedPackage 'pkg_extra'; Set-Content -LiteralPath (Join-Path $pExtra.Dir 'evil.dll') -Value 'x'
$pNoMan = New-StagedPackage 'pkg_noman'; Remove-Item -LiteralPath (Join-Path $pNoMan.Dir 'avb-driver-package.json')
$vT = Test-Pkg $pTamper; $vE = Test-Pkg $pExtra; $vN = Test-Pkg $pNoMan
tc_check 'TEST-CIDEPLOY-004-tampered-extra-file-or-missing-manifest-package-is-rejected' ((-not $vT.Ok) -and (-not $vE.Ok) -and (-not $vN.Ok) -and (($vE.Reasons -join ' ') -match 'evil\.dll')) `
    ("tamper={0} | extra={1} | noman={2}" -f ($vT.Reasons -join ';'), ($vE.Reasons -join ';'), ($vN.Reasons -join ';'))
$cerDir = Join-Path $Tmp 'cer'; New-Item -ItemType Directory -Path $cerDir -Force | Out-Null
$pinMissing = Get-AvbPinnedSigner -CertificatePath (Join-Path $cerDir 'absent.cer')
tc_check 'TEST-CIDEPLOY-005-missing-pinned-certificate-fails-closed' ((-not $pinMissing.Ok) -and ($pinMissing.Error -match 'one-time setup')) $pinMissing.Error

# ---------------------------------------------------------------------------------------------------------
# Deployment engine with a scripted fake system
# ---------------------------------------------------------------------------------------------------------
function New-FakeSys([hashtable]$S) {
    # $S: Service, Bindings (names), InstalledSha, Pending, Mgmt, UninstallResult(@{Completed;ExitCode;Service}),
    #     InstallResult(@{Completed;ExitCode;Service}), LockedCopies, ImRc, GpRc, Log (List)
    $st = @{ Service = $S.Service; Bindings = @($S.Bindings); InstalledSha = $S.InstalledSha; Copies = 0 }
    $log = $S.Log; $drivers = Join-Path $Tmp ('drv_' + (Get-Random)); New-Item -ItemType Directory -Path $drivers -Force | Out-Null
    return @{
        DriversDir = $drivers
        Invoke = { param($File, $Arguments, $TimeoutSeconds, $WorkDir)
            $log.Add("$File $($Arguments -join ' ')")
            if ($File -eq 'netcfg.exe' -and $Arguments[0] -eq '-u') { $r = $S.UninstallResult; if ($r.Completed) { $st.Service = $r.Service; if ($r.ExitCode -eq 0) { $st.Bindings = @() } }; return @{ Completed = $r.Completed; ExitCode = $r.ExitCode; Output = '' } }
            if ($File -eq 'netcfg.exe' -and $Arguments[0] -eq '-l') { $r = $S.InstallResult; if ($r.Completed -and $r.ExitCode -eq 0) { $st.Service = $r.Service; $st.Bindings = @('Ethernet I226', 'Ethernet Mgmt') }; return @{ Completed = $r.Completed; ExitCode = $r.ExitCode; Output = '' } }
            if ($File -eq 'wevtutil.exe' -and $Arguments[0] -eq 'im') { return @{ Completed = $true; ExitCode = $S.ImRc; Output = '' } }
            if ($File -eq 'wevtutil.exe' -and $Arguments[0] -eq 'gp') { return @{ Completed = $true; ExitCode = $S.GpRc; Output = '' } }
            return @{ Completed = $true; ExitCode = 0; Output = '' } }.GetNewClosure()
        ServiceState = { $st.Service }.GetNewClosure()
        Bindings = { @($st.Bindings | ForEach-Object { @{ Name = $_; Enabled = $true } }) }.GetNewClosure()
        ManagementAdapter = { $S.Mgmt }.GetNewClosure()
        PendingRenames = { @($S.Pending) }.GetNewClosure()
        FileHash = { param($Path) if ($Path -like '*\drv_*\IntelAvbFilter.sys') { $st.InstalledSha } elseif (Test-Path -LiteralPath $Path) { (Get-FileHash -LiteralPath $Path).Hash } else { $null } }.GetNewClosure()
        CopyFile = { param($Source, $Destination) $log.Add("copy $([IO.Path]::GetFileName($Source))")
            if ($Destination -like '*IntelAvbFilter.sys') { $st.Copies++; if ($st.Copies -le $S.LockedCopies) { throw 'file in use' }; $st.InstalledSha = (Get-FileHash -LiteralPath $Source).Hash } }.GetNewClosure()
        TestPath = { param($Path) if ($Path -like '*\drv_*\IntelAvbFilter.man') { $st.Service -ne 'NOT_FOUND' } else { Test-Path -LiteralPath $Path } }.GetNewClosure()
        Sleep = { param($s) }
    }
}
function Invoke-Deploy([hashtable]$Over = @{}, $Pkg = $pOk) {
    $log = [System.Collections.Generic.List[string]]::new()
    $S = @{ Service = 'RUNNING'; Bindings = @('Ethernet I226'); InstalledSha = ('1' * 64); Pending = @(); Mgmt = 'Ethernet Mgmt'
            UninstallResult = @{ Completed = $true; ExitCode = 0; Service = 'NOT_FOUND' }; InstallResult = @{ Completed = $true; ExitCode = 0; Service = 'RUNNING' }
            LockedCopies = 0; ImRc = 0; GpRc = 0; Log = $log }
    foreach ($k in $Over.Keys) { $S[$k] = $Over[$k] }
    $r = Invoke-AvbCiDriverDeployment -Sys (New-FakeSys $S) -PackageDir $Pkg.Dir -ExpectedSysSha256 $Pkg.Sha -StepTimeoutSeconds 4 -CopyAttempts 3
    return @{ R = $r; Log = @($log) }
}
function Show-D($d) { return ("{0} exit={1} reason={2} | calls: {3}" -f $d.R.outcome, $d.R.exitCode, $d.R.reason, ($d.Log -join ' ; ')) }

$dAbsent = Invoke-Deploy @{ Service = 'NOT_FOUND'; Bindings = @(); InstalledSha = $null }
tc_check 'TEST-CIDEPLOY-010-absent-driver-is-installed-not-a-failure' `
    (($dAbsent.R.outcome -eq 'DEPLOYED') -and ($dAbsent.R.exitCode -eq 0) -and (@($dAbsent.Log | Where-Object { $_ -match '^netcfg\.exe -u' }).Count -eq 0) -and
     (@($dAbsent.Log | Where-Object { $_ -match '^netcfg\.exe -l IntelAvbFilter\.inf -c s -i MS_IntelAvbFilter' }).Count -eq 1)) (Show-D $dAbsent)
$dWrong = Invoke-Deploy @{ InstalledSha = ('2' * 64) }
tc_check 'TEST-CIDEPLOY-011-wrong-installed-driver-is-replaced-by-the-staged-build' `
    (($dWrong.R.outcome -eq 'DEPLOYED') -and ($dWrong.R.preState.installedSysSha256 -eq ('2' * 64)) -and ($dWrong.R.postState.installedSysSha256 -eq $pOk.Sha)) (Show-D $dWrong)
$dFail = Invoke-Deploy @{ InstallResult = @{ Completed = $true; ExitCode = 1; Service = 'NOT_FOUND' } }
tc_check 'TEST-CIDEPLOY-012-failed-installation-is-an-infrastructure-failure-with-evidence' `
    (($dFail.R.outcome -eq 'FAILED') -and ($dFail.R.exitCode -eq 1) -and ($dFail.R.reason -match 'netcfg -l failed with rc 1') -and ($null -ne $dFail.R.preState) -and
     (@($dFail.R.steps | Where-Object { $_.step -eq 'netcfg-install' -and -not $_.ok }).Count -eq 1)) (Show-D $dFail)
$dSP = Invoke-Deploy @{ Service = 'STOP_PENDING' }
$dSP2 = Invoke-Deploy @{ UninstallResult = @{ Completed = $true; ExitCode = 0; Service = 'STOP_PENDING' } }
$dHang = Invoke-Deploy @{ UninstallResult = @{ Completed = $false; ExitCode = $null; Service = 'RUNNING' } }
$dLock = Invoke-Deploy @{ LockedCopies = 99 }
$dPend = Invoke-Deploy @{ Pending = @('\??\C:\Windows\System32\drivers\IntelAvbFilter.sys.new') }
tc_check 'TEST-CIDEPLOY-013-stop-pending-hang-locked-image-or-pending-reboot-fail-closed-without-reboot' `
    (($dSP.R.outcome -eq 'NEEDS_OPERATOR') -and (@($dSP.Log | Where-Object { $_ -match '^netcfg' }).Count -eq 0) -and
     ($dSP2.R.outcome -eq 'NEEDS_OPERATOR') -and ($dSP2.R.reason -match 'STOP_PENDING') -and (@($dSP2.Log | Where-Object { $_ -match '^netcfg\.exe -l' }).Count -eq 0) -and
     ($dHang.R.outcome -eq 'NEEDS_OPERATOR') -and ($dHang.R.reason -match 'left running') -and (@($dHang.Log | Where-Object { $_ -match '^copy|^netcfg\.exe -l' }).Count -eq 0) -and
     ($dLock.R.outcome -eq 'NEEDS_OPERATOR') -and ($dLock.R.reason -match 'no reboot-time replacement') -and (@($dLock.Log | Where-Object { $_ -eq 'copy IntelAvbFilter.sys' }).Count -eq 3) -and
     ($dPend.R.outcome -eq 'NEEDS_OPERATOR') -and (@($dSP.R.exitCode, $dSP2.R.exitCode, $dHang.R.exitCode, $dLock.R.exitCode, $dPend.R.exitCode) -join ',') -eq '3,3,3,3,3') `
    ("pre={0} | post-u={1} | hang={2} | locked={3} | pending={4}" -f (Show-D $dSP), (Show-D $dSP2), (Show-D $dHang), (Show-D $dLock), (Show-D $dPend))
$dNoRun = Invoke-Deploy @{ InstallResult = @{ Completed = $true; ExitCode = 0; Service = 'STOPPED' } }
$dEtw = Invoke-Deploy @{ GpRc = 15007 }
$pBad = @{ Dir = $pOk.Dir; Sha = ('3' * 64) }
$dBadPkg = Invoke-Deploy @{} $pBad
tc_check 'TEST-CIDEPLOY-014-success-requires-running-identity-and-etw-and-a-bad-package-touches-nothing' `
    (($dNoRun.R.outcome -eq 'FAILED') -and ($dNoRun.R.reason -match 'not RUNNING') -and ($dEtw.R.outcome -eq 'FAILED') -and ($dEtw.R.reason -match 'ETW provider') -and
     ($dBadPkg.R.outcome -eq 'FAILED') -and ($dBadPkg.Log.Count -eq 0)) ("stopped={0} | etw={1} | badpkg={2}" -f (Show-D $dNoRun), (Show-D $dEtw), (Show-D $dBadPkg))
$dMgmt = Invoke-Deploy @{ Bindings = @('Ethernet I226', 'Ethernet Mgmt') }
tc_check 'TEST-CIDEPLOY-015-management-connection-risk-is-reported' (($dMgmt.R.managementRisk -match "management adapter 'Ethernet Mgmt' is bound") -and ($dMgmt.R.outcome -eq 'DEPLOYED')) $dMgmt.R.managementRisk

$dd = [System.IO.File]::ReadAllText((Join-Path $RepoRoot 'tools\lib\DriverDeploy.ps1'))
$code = ($dd -split "`n" | Where-Object { $_ -notmatch '^\s*#' }) -join "`n"
tc_check 'TEST-CIDEPLOY-016-unattended-path-never-prompts-reboots-disables-adapters-deletes-driverstore-or-restarts-eventlog' `
    ($code -notmatch '(?i)Read-Host|Restart-Computer|shutdown|Disable-NetAdapter|Enable-NetAdapter|DriverStore|Restart-Service|Stop-Service|PendingFileRenameOperations"\s*-Value|Set-ItemProperty|\.Kill\(') 'forbidden operation found in tools/lib/DriverDeploy.ps1'

# --- Service state from codes, never from localized text (CI run 41500818916: German Windows prints
#     "OpenService FEHLER 1060", the old parser expected "FAILED 1060" and reported UNKNOWN instead of NOT_FOUND) ---
$haveSvc = [bool](Get-Command ConvertTo-AvbServiceState -ErrorAction SilentlyContinue) -and [bool](Get-Command Get-AvbServiceState -ErrorAction SilentlyContinue)
if ($haveSvc) {
    $map = [ordered]@{
        'absent (Win32 1060)'        = @(@{ Win32Error = 1060; Status = $null }, '^NOT_FOUND$')
        'RUNNING (4)'                = @(@{ Win32Error = $null; Status = 4 }, '^RUNNING$')
        'STOPPED (1)'                = @(@{ Win32Error = $null; Status = 1 }, '^STOPPED$')
        'STOP_PENDING (3)'           = @(@{ Win32Error = $null; Status = 3 }, '^STOP_PENDING$')
        'START_PENDING (2)'          = @(@{ Win32Error = $null; Status = 2 }, '^START_PENDING$')
        'access denied (Win32 5)'    = @(@{ Win32Error = 5; Status = $null }, '^UNKNOWN:ACCESS_DENIED')
        'other Win32 error'          = @(@{ Win32Error = 1062; Status = $null }, '^UNKNOWN:WIN32_ERROR_1062')
        'malformed status code'      = @(@{ Win32Error = $null; Status = 99 }, '^UNKNOWN:MALFORMED')
        'error and status together'  = @(@{ Win32Error = 1060; Status = 4 }, '^UNKNOWN:MALFORMED')
        'unresolved (no code at all)'= @(@{ Win32Error = $null; Status = $null }, '^UNKNOWN:UNRESOLVED')
    }
    $bad = @(foreach ($k in $map.Keys) { $s = ConvertTo-AvbServiceState -Probe $map[$k][0]; if ($s -notmatch $map[$k][1]) { "$k -> '$s'" } })
    tc_check 'TEST-CIDEPLOY-017-service-state-is-derived-from-scm-codes-for-every-case' ($bad.Count -eq 0) ($bad -join '; ')
    # Real, read-only probe on this machine (any UI language; the HIL runner is German).
    $absent = Get-AvbServiceState -Name ('AvbNoSuchService_' + (Get-Random))
    $evlog = Get-AvbServiceState -Name 'EventLog'
    $realSs = ([regex]::Match($dd, '(?s)ServiceState\s*=\s*\{.*?\n\s{8}\}')).Value
    tc_check 'TEST-CIDEPLOY-018-real-probe-reports-an-absent-service-as-not-found-without-parsing-localized-text' `
        (($absent -eq 'NOT_FOUND') -and ($evlog -eq 'RUNNING') -and ($realSs -match 'Get-AvbServiceState') -and ($code -notmatch 'FAILED 1060') -and ($code -notmatch 'sc\.exe')) `
        ("absent={0} EventLog={1} realServiceState={2}" -f $absent, $evlog, ($realSs -replace '\s+', ' '))
} else {
    tc_fail 'TEST-CIDEPLOY-017-service-state-is-derived-from-scm-codes-for-every-case' 'ConvertTo-AvbServiceState / Get-AvbServiceState missing in tools/lib/DriverDeploy.ps1'
    tc_fail 'TEST-CIDEPLOY-018-real-probe-reports-an-absent-service-as-not-found-without-parsing-localized-text' 'Get-AvbServiceState missing in tools/lib/DriverDeploy.ps1'
}

# --- netcfg -u must be confirmed (exit 0, service released, no binding) before the image is replaced; the state after a
#     failed deployment is recorded for the machine_ok decision ---
$dRc = Invoke-Deploy @{ Bindings = @(); UninstallResult = @{ Completed = $true; ExitCode = 1; Service = 'NOT_FOUND' } }
$dUnk = Invoke-Deploy @{ UninstallResult = @{ Completed = $true; ExitCode = 0; Service = 'UNKNOWN:UNRESOLVED (probe)' } }
$noTouch = { param($d) @($d.Log | Where-Object { $_ -match '^copy|^netcfg\.exe -l' }).Count -eq 0 }
tc_check 'TEST-CIDEPLOY-019-nonzero-netcfg-exit-or-unresolved-state-after-uninstall-never-reaches-installation-and-records-post-state' `
    (($dRc.R.outcome -eq 'FAILED') -and ($dRc.R.reason -match 'netcfg -u returned rc 1') -and (& $noTouch $dRc) -and ($null -ne $dRc.R.postState) -and ($dRc.R.postState.service -eq 'NOT_FOUND') -and
     ($dUnk.R.outcome -eq 'FAILED') -and ($dUnk.R.reason -match 'UNKNOWN') -and (& $noTouch $dUnk) -and ($null -ne $dUnk.R.postState) -and ($dUnk.R.postState.service -match '^UNKNOWN')) `
    ("rc1={0} post={1} | unknown={2} post={3}" -f (Show-D $dRc), $(if ($dRc.R.postState) { $dRc.R.postState.service }), (Show-D $dUnk), $(if ($dUnk.R.postState) { $dUnk.R.postState.service }))

# ---------------------------------------------------------------------------------------------------------
# Serialization (CI case 10)
# ---------------------------------------------------------------------------------------------------------
$lp = Join-Path $Tmp 'hil.lock'
$alive = { param($Id) $true }; $dead = { param($Id) $false }; $noSleep = { param($s) }
$env:GITHUB_RUN_ID = '500'; $env:GITHUB_RUN_ATTEMPT = '1'; $env:GITHUB_JOB = 'hil'; $env:HIL_CONFIGURATION = 'Debug'
$a = Enter-AvbHilLock -Kind 'ci' -Label 'hil Debug' -Path $lp -IsProcessAlive $alive -Sleep $noSleep
$env:GITHUB_RUN_ID = ''; $env:GITHUB_JOB = ''
$b = Enter-AvbHilLock -Kind 'local' -Label 'LocalSuites' -Path $lp -WaitSeconds 30 -IsProcessAlive $dead -IsCiJobActive { $true } -Sleep $noSleep
tc_check 'TEST-CIDEPLOY-020-operator-run-cannot-enter-while-a-ci-job-holds-the-machine' ($a.Ok -and -not $b.Ok -and $b.Why -match 'may still own this runner|is executing on this runner' -and $b.Waited -eq 30) ("ci={0} local={1} why={2} waited={3}" -f $a.Ok, $b.Ok, $b.Why, $b.Waited)

# RED regression: GitHub's matrix legs have the SAME GITHUB_JOB but must never share lock ownership.
# A live worker on a second runner instance is not a stale CI owner, even on the same physical host.
$env:GITHUB_RUN_ID = '500'; $env:GITHUB_JOB = 'hil'; $env:HIL_CONFIGURATION = 'Release'
$originalBytes = [IO.File]::ReadAllBytes($lp)
$live = Enter-AvbHilLock -Kind 'ci' -Label 'hil Release' -Path $lp -WaitSeconds 0 -IsProcessAlive $alive -IsCiJobActive { param($owner) $true } -Sleep $noSleep
$stillOwned = (Test-Path -LiteralPath $lp) -and
    ([Convert]::ToBase64String([IO.File]::ReadAllBytes($lp)) -eq [Convert]::ToBase64String($originalBytes))
tc_check 'TEST-CIDEPLOY-021a-live-matrix-leg-lock-cannot-be-stolen-by-another-leg' `
    ($a.Ok -and -not $live.Ok -and $stillOwned -and $null -eq $live.PreviousOwner) `
    ("acquired={0} stolen={1} unchanged={2} reason={3}" -f $a.Ok, $live.Ok, $stillOwned, $live.Why)
$alienCtx = Resolve-AvbHilLockContext -Path $lp -GitHub @{ runId = '500'; runAttempt = '1'; job = 'hil'; configuration = 'Release' } -Ancestors @()
tc_check 'TEST-CIDEPLOY-021b-matrix-legs-with-the-same-job-name-have-distinct-lock-ownership' `
    (-not $alienCtx.Owned) ("foreign lock owned={0} how={1}" -f $alienCtx.Owned, $alienCtx.How)

# Different workflow runs targeting the same physical host must also keep a live owner intact.
$env:GITHUB_RUN_ID = '501'
$crossRun = Enter-AvbHilLock -Kind 'ci' -Label 'hil Release in other run' -Path $lp -WaitSeconds 0 -IsProcessAlive $alive -IsCiJobActive { param($owner) $true } -Sleep $noSleep
tc_check 'TEST-CIDEPLOY-021d-a-second-workflow-run-cannot-steal-a-live-ci-lock' `
    (-not $crossRun.Ok -and (Test-Path -LiteralPath $lp) -and
     ([Convert]::ToBase64String([IO.File]::ReadAllBytes($lp)) -eq [Convert]::ToBase64String($originalBytes))) `
    ("stolen={0} reason={1}" -f $crossRun.Ok, $crossRun.Why)
$env:GITHUB_RUN_ID = '500'

# Proven interrupted owner: only a conclusive worker-exited check permits recorded stale takeover.
$c = Enter-AvbHilLock -Kind 'ci' -Label 'hil Release' -Path $lp -WaitSeconds 0 -IsProcessAlive $dead -IsCiJobActive { param($owner) $false } -Sleep $noSleep
tc_check 'TEST-CIDEPLOY-021c-confirmed-dead-ci-owner-can-be-replaced-with-incident-evidence' `
    ($c.Ok -and $null -ne $c.PreviousOwner -and $c.PreviousOwner.owner.job -eq 'hil' -and
     $c.PreviousOwner.why -match 'did not release|no longer running|confirmed') `
    ("ok={0} prev={1}" -f $c.Ok, $(if ($c.PreviousOwner) { $c.PreviousOwner.why }))
$x = Exit-AvbHilLock -Owner $a.Owner -Path $lp
$y = Exit-AvbHilLock -Owner $c.Owner -Path $lp
tc_check 'TEST-CIDEPLOY-022-only-the-owner-releases' ((-not $x.Ok) -and $y.Ok -and -not (Test-Path -LiteralPath $lp)) ("foreign={0} own={1}" -f $x.Why, $y.Ok)
$env:GITHUB_RUN_ID = ''; $env:GITHUB_RUN_ATTEMPT = ''; $env:GITHUB_JOB = ''; $env:HIL_CONFIGURATION = ''
$l1 = Enter-AvbHilLock -Kind 'local' -Label 'LocalSuites A' -Path $lp -IsProcessAlive $alive -Sleep $noSleep
$l2 = Enter-AvbHilLock -Kind 'local' -Label 'LocalSuites B' -Path $lp -WaitSeconds 0 -IsProcessAlive $alive -Sleep $noSleep
$l3 = Enter-AvbHilLock -Kind 'local' -Label 'LocalSuites C' -Path $lp -WaitSeconds 0 -IsProcessAlive $dead -Sleep $noSleep
tc_check 'TEST-CIDEPLOY-023-live-owner-blocks-dead-local-owner-is-stale' ($l1.Ok -and -not $l2.Ok -and $l3.Ok -and $l3.PreviousOwner.why -match 'no longer running') ("A={0} B={1} C={2}" -f $l1.Ok, $l2.Ok, $l3.Ok)
$null = Exit-AvbHilLock -Owner $l3.Owner -Path $lp
# Concurrency: two real processes race for the same lock; exactly one wins.
$race = Join-Path $Tmp 'race.lock'
$raceScript = Join-Path $Tmp 'race.ps1'
@"
. '$((Join-Path $RepoRoot 'tools\lib\HilLock.ps1').Replace("'", "''"))'
`$r = Enter-AvbHilLock -Kind 'local' -Label `$args[0] -Path '$($race.Replace("'", "''"))' -WaitSeconds 0
if (`$r.Ok) { Start-Sleep -Seconds 4; exit 0 } else { exit 7 }
"@ | Set-Content -LiteralPath $raceScript -Encoding UTF8
$procs = @(1..4 | ForEach-Object { Start-Process powershell.exe -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $raceScript, "racer$_" -PassThru -WindowStyle Hidden })
foreach ($p in $procs) { $null = $p.Handle }
foreach ($p in $procs) { $p.WaitForExit(60000) | Out-Null }
$codes = @($procs | ForEach-Object { $_.ExitCode } | Sort-Object)
tc_check 'TEST-CIDEPLOY-024-concurrent-processes-exactly-one-acquires' (($codes -join ',') -eq '0,7,7,7') "exit codes: $($codes -join ',')"


# Every lock mutation (create, stale reclaim, release) must respect the same named mutex.
# These tests use a separate real process holding that mutex, not a simulated boolean.
$mutexWorker = Join-Path $Tmp 'hold-mutex.ps1'
@'
param([string]$Ready)
$gate = [System.Threading.Mutex]::new($false, 'Global\IntelAvbFilter-HIL-Lock-Recovery')
$held = $false
try {
    $held = $gate.WaitOne(10000)
    if (-not $held) { exit 2 }
    [IO.File]::WriteAllText($Ready, 'held')
    Start-Sleep -Milliseconds 2200
} finally {
    if ($held) { $gate.ReleaseMutex() }
    $gate.Dispose()
}
'@ | Set-Content -LiteralPath $mutexWorker -Encoding UTF8
$mutexFailures = @()
foreach ($operation in @('create', 'release')) {
    $mp = Join-Path $Tmp "mutex-$operation.lock"
    $ready = Join-Path $Tmp "mutex-$operation.ready"
    $owned = $null
    if ($operation -eq 'release') {
        $owned = Enter-AvbHilLock -Kind 'local' -Label 'release target' -Path $mp -WaitSeconds 0
        if (-not $owned.Ok) { $mutexFailures += 'could not establish release target' }
    }
    $child = Start-Process powershell.exe -ArgumentList '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $mutexWorker, $ready -PassThru -WindowStyle Hidden
    try {
        $readySeen = $false
        for ($i = 0; $i -lt 200; $i++) {
            if (Test-Path -LiteralPath $ready) { $readySeen = $true; break }
            Start-Sleep -Milliseconds 50
        }
        if (-not $readySeen) { $mutexFailures += "${operation}: mutex not acquired by child" }
        else {
            $sw = [Diagnostics.Stopwatch]::StartNew()
            $result = if ($operation -eq 'create') {
                Enter-AvbHilLock -Kind 'local' -Label 'create target' -Path $mp -WaitSeconds 0
            } else {
                Exit-AvbHilLock -Owner $owned.Owner -Path $mp
            }
            $sw.Stop()
            if (-not $result.Ok -or $sw.ElapsedMilliseconds -lt 1100) {
                $mutexFailures += "${operation}: result=$($result.Ok) elapsed=$($sw.ElapsedMilliseconds)ms; did not wait for mutex"
            }
            if ($operation -eq 'create' -and $result.Ok) { $null = Exit-AvbHilLock -Owner $result.Owner -Path $mp }
        }
    } finally {
        $null = $child.WaitForExit(20000)
        if (-not $child.HasExited) { $child.Kill() }
        $child.Dispose()
        if (Test-Path -LiteralPath $mp) { Remove-Item -LiteralPath $mp -Force }
    }
}
tc_check 'TEST-CIDEPLOY-024a-create-and-release-share-the-stale-recovery-mutex' ($mutexFailures.Count -eq 0) ($mutexFailures -join '; ')
# Build exclusion: a build writes the artifacts a HIL run verifies and executes (LocalSuites_20261029_005219: a build
# outside the run overwrote them mid-run - 12 SHA256 mismatches, a failed relink of a running test).
$bl = Join-Path $Tmp 'build.lock'
$haveBx = [bool](Get-Command Enter-AvbHilBuildExclusion -ErrorAction SilentlyContinue)
tc_check 'TEST-CIDEPLOY-025a-build-exclusion-function-present' $haveBx 'Enter-AvbHilBuildExclusion missing in tools/lib/HilLock.ps1'
if ($haveBx) {
    $env:GITHUB_RUN_ID = ''; $env:GITHUB_RUN_ATTEMPT = ''; $env:GITHUB_JOB = ''
    $bx = Enter-AvbHilBuildExclusion -Label 'Build-Tests Debug' -Path $bl -Ancestors @()
    $held = Read-AvbHilLock -Path $bl
    $rel = Exit-AvbHilLock -Owner $bx.Owner -Path $bl
    tc_check 'TEST-CIDEPLOY-025b-attended-build-without-owner-context-holds-the-lock-for-its-duration' `
        ($bx.Ok -and $bx.How -eq 'acquired' -and $null -ne $held -and $held.kind -eq 'local' -and $held.label -eq 'Build-Tests Debug' -and $rel.Ok -and -not (Test-Path -LiteralPath $bl)) `
        ("ok={0} how={1} held={2} released={3}" -f $bx.Ok, $bx.How, $(if ($held) { $held.label }), $rel.Ok)

    # Foreign live owner (a LocalSuites run), stale owner and unreadable lock: refused, lock bytes untouched, no takeover.
    $refused = @()
    foreach ($case in @(
            @{ Name = 'foreign-live-local'; Json = '{"kind":"local","label":"LocalSuites Debug+Release","host":"' + $env:COMPUTERNAME + '","pid":4,"acquiredUtc":"2026-10-28T23:52:19Z"}' },
            @{ Name = 'stale-dead-local';   Json = '{"kind":"local","label":"LocalSuites Debug","host":"' + $env:COMPUTERNAME + '","pid":999999,"acquiredUtc":"2026-10-28T23:52:19Z"}' },
            @{ Name = 'foreign-ci-job';     Json = '{"kind":"ci","label":"hil Debug","host":"' + $env:COMPUTERNAME + '","pid":4,"runId":"77","runAttempt":"1","job":"hil-debug","acquiredUtc":"2026-10-28T23:52:19Z"}' },
            @{ Name = 'unreadable';         Json = '{not json' })) {
        Set-Content -LiteralPath $bl -Value $case.Json -Encoding UTF8 -NoNewline
        $before = [IO.File]::ReadAllBytes($bl)
        $r = Enter-AvbHilBuildExclusion -Label 'Build-Tests Debug' -Path $bl -Ancestors @(1234)
        $after = [IO.File]::ReadAllBytes($bl)
        if ($r.Ok -or $null -ne $r.Owner -or -not $r.Why -or ([Convert]::ToBase64String($before) -ne [Convert]::ToBase64String($after))) { $refused += "$($case.Name): ok=$($r.Ok) why=$($r.Why)" }
        Remove-Item -LiteralPath $bl -Force
    }
    tc_check 'TEST-CIDEPLOY-025c-build-refused-while-another-execution-or-a-stale-or-unreadable-lock-holds-the-machine-lock-untouched' ($refused.Count -eq 0) ($refused -join '; ')

    # Inside the owner's context (LocalSuites in-process = self, child of the owner = ancestor, the hil.yml job = ci-job):
    # the build proceeds and acquires nothing (the owner releases).
    $ownJson = '{"kind":"local","label":"LocalSuites Debug+Release","host":"' + $env:COMPUTERNAME + '","pid":4321,"acquiredUtc":"2026-10-28T23:52:19Z"}'
    Set-Content -LiteralPath $bl -Value $ownJson -Encoding UTF8 -NoNewline
    $self = Enter-AvbHilBuildExclusion -Label 'b' -Path $bl -ProcessId 4321 -Ancestors @()
    $anc  = Enter-AvbHilBuildExclusion -Label 'b' -Path $bl -ProcessId 99 -Ancestors @(55, 4321)
    $ownKept = ((Get-Content -LiteralPath $bl -Raw) -eq $ownJson)
    Set-Content -LiteralPath $bl -Value ('{"kind":"ci","label":"hil Debug","host":"' + $env:COMPUTERNAME + '","pid":4,"runId":"77","runAttempt":"1","job":"hil-debug","acquiredUtc":"2026-10-28T23:52:19Z"}') -Encoding UTF8 -NoNewline
    $job   = Enter-AvbHilBuildExclusion -Label 'b' -Path $bl -GitHub @{ runId = '77'; runAttempt = '1'; job = 'hil-debug' }
    $other = Enter-AvbHilBuildExclusion -Label 'b' -Path $bl -GitHub @{ runId = '77'; runAttempt = '1'; job = 'hil-release' }
    Remove-Item -LiteralPath $bl -Force
    $hosted = Enter-AvbHilBuildExclusion -Label 'b' -Path $bl -GitHub @{ runId = '78'; runAttempt = '1'; job = 'unit-tests' }
    tc_check 'TEST-CIDEPLOY-025d-build-inside-the-owner-context-proceeds-without-acquiring-hosted-ci-writes-no-lock' `
        ($self.Ok -and $self.How -eq 'self' -and $null -eq $self.Owner -and $anc.Ok -and $anc.How -eq 'ancestor' -and $ownKept -and
         $job.Ok -and $job.How -eq 'ci-job' -and -not $other.Ok -and $hosted.Ok -and $null -eq $hosted.Owner -and -not (Test-Path -LiteralPath $bl)) `
        ("self={0}/{1} anc={2}/{3} kept={4} job={5}/{6} other={7} hosted={8} lockfile={9}" -f $self.Ok, $self.How, $anc.Ok, $anc.How, $ownKept, $job.Ok, $job.How, $other.Ok, $hosted.Ok, (Test-Path -LiteralPath $bl))
}
# STATIC (source order, not runtime evidence): both build scripts take the exclusion before any build step and release
# a lock they acquired in a finally block (every exit path, including exit inside the body).
$staticBx = @()
foreach ($bs in 'Build-Tests.ps1', 'Build-Driver.ps1') {
    $src = Get-Content -LiteralPath (Join-Path $RepoRoot "tools\build\$bs") -Raw
    $iBx = $src.IndexOf('Enter-AvbHilBuildExclusion'); $iTry = $src.IndexOf("`ntry {", [Math]::Max(0, $iBx))
    $iWork = [regex]::Match($src, '(?m)^# =+\s*\r?\n# (Build Diagnostic Tools|STEP 1: Find MSBuild)').Index
    $iFin = $src.LastIndexOf('} finally {'); $iExit = $src.IndexOf('Exit-AvbHilLock', [Math]::Max(0, $iFin))
    if ($iBx -lt 0 -or $iTry -lt 0 -or $iWork -le 0 -or $iBx -gt $iWork -or $iTry -gt $iWork -or $iFin -lt $iWork -or $iExit -lt 0) { $staticBx += "${bs}: exclusion=$iBx try=$iTry firstStep=$iWork finally=$iFin release=$iExit" }
}
tc_check 'TEST-CIDEPLOY-025e-STATIC-build-scripts-take-the-exclusion-before-building-and-release-in-finally' ($staticBx.Count -eq 0) ($staticBx -join '; ')

# ---------------------------------------------------------------------------------------------------------
# Crash evidence (CI cases 12, 13)
# ---------------------------------------------------------------------------------------------------------
$now = [datetime]::UtcNow
function New-Health([object[]]$Events, [datetime]$Boot, [object[]]$Dumps = @()) {
    return @{ Events = { param($s) $Events }.GetNewClosure(); Dumps = { $Dumps }.GetNewClosure(); LastBootUtc = { $Boot }.GetNewClosure(); CopyFile = { param($a, $b) Set-Content -LiteralPath $b -Value 'dump' } }
}
$old = @{ TimeUtc = $now.AddDays(-5); Provider = 'Microsoft-Windows-WER-SystemErrorReporting'; Id = 1001; Message = 'bugcheck 0x0000009f' }
$h1 = Get-AvbCrashEvidence -Source (New-Health @($old) $now.AddDays(-1)) -PreviousOwner $null -RunStartUtc $now
tc_check 'TEST-CIDEPLOY-030-historical-bugcheck-is-recorded-and-needs-no-acknowledgement' (($h1.classification -eq 'HISTORICAL') -and (@($h1.events).Count -eq 1)) $h1.classification
$prev = [ordered]@{ owner = [pscustomobject]@{ kind = 'ci'; runId = '499'; job = 'hil-release'; acquiredUtc = $now.AddHours(-3).ToString('o') }; why = 'did not release' }
$crash = @{ TimeUtc = $now.AddHours(-2); Provider = 'Microsoft-Windows-WER-SystemErrorReporting'; Id = 1001; Message = 'bugcheck 0x000000d1' }
$ev = Join-Path $Tmp 'evidence'; New-Item -ItemType Directory -Path $ev -Force | Out-Null
$h2 = Get-AvbCrashEvidence -Source (New-Health @($old, $crash) $now.AddHours(-1) @(@{ Path = 'C:\Windows\Minidump\102126-01.dmp'; TimeUtc = $now.AddHours(-2); Bytes = 1024; Mini = $true })) -PreviousOwner $prev -RunStartUtc $now -EvidenceDir $ev
tc_check 'TEST-CIDEPLOY-031-crash-during-an-unfinished-hil-run-is-attributed-and-its-minidump-retained' `
    (($h2.classification -eq 'PREVIOUS_RUN_CRASHED') -and (Test-Path -LiteralPath (Join-Path $ev '102126-01.dmp')) -and (($h2.summary -join ' ') -match 'run 499')) ("{0}: {1}" -f $h2.classification, ($h2.summary -join ' '))
$h3 = Get-AvbCrashEvidence -Source (New-Health @() $now.AddMinutes(5)) -PreviousOwner $null -RunStartUtc $now
tc_check 'TEST-CIDEPLOY-032-reboot-during-the-current-run-quarantines-it' ($h3.classification -eq 'CURRENT_RUN_INTERRUPTED') $h3.classification

# ---------------------------------------------------------------------------------------------------------
# Orchestrator order (real Invoke-HilDeployment.ps1, recording fake installer / pre-flight)
# ---------------------------------------------------------------------------------------------------------
$calls = Join-Path $Tmp 'calls.log'
$fakeInstall = Join-Path $Tmp 'fake_install.ps1'; $fakePre = Join-Path $Tmp 'fake_preflight.ps1'
@"
param([string]`$Configuration, [switch]`$Reinstall, [switch]`$NonInteractive, [string]`$PackageDir, [string]`$ExpectedSysSha256, [int]`$StepTimeoutSeconds, [string]`$ResultJson)
Add-Content -LiteralPath '$calls' -Value "install `$Configuration `$ExpectedSysSha256 nonint=`$NonInteractive"
`$o = if (`$env:FAKE_INSTALL_RC -eq '0') { 'DEPLOYED' } elseif (`$env:FAKE_INSTALL_RC -eq '3') { 'NEEDS_OPERATOR' } else { 'FAILED' }
(@{ outcome = `$o; exitCode = [int]`$env:FAKE_INSTALL_RC; reason = 'fake' } | ConvertTo-Json) | Set-Content -LiteralPath `$ResultJson
exit ([int]`$env:FAKE_INSTALL_RC)
"@ | Set-Content -LiteralPath $fakeInstall -Encoding UTF8
@"
Add-Content -LiteralPath '$calls' -Value "preflight `$env:BUILD_DRIVER_SHA256 require=`$env:HIL_REQUIRE_DEPLOYED_SHA"
exit ([int]`$env:FAKE_PRE_RC)
"@ | Set-Content -LiteralPath $fakePre -Encoding UTF8
$orch = Join-Path $RepoRoot 'tools\ci\Invoke-HilDeployment.ps1'
$pinCer = Join-Path $Tmp 'pin.cer'
# The orchestrator runs as an OFFLINE child (-SkipTrustCheck seam) whatever environment this test runs in: under GitHub
# Actions the child would otherwise inherit GITHUB_ACTIONS=true and be refused by the seam guard before any scenario
# (CI run 40824241990: abort at CIDEPLOY-040).  -GitHubActions 'true' exercises that guard itself (CIDEPLOY-046).
# Recovery observations (#493): -ProbeJson replaces ONLY the machine observation (tools/lib/DriverDeploy.ps1
# Get-AvbHilRecoveryObservation) through the -ProbeScript seam; the readiness decision, incident preservation and the
# deployment gate are the real code.  Without -ProbeJson no seam is passed.
$probeCalls = Join-Path $Tmp 'probe_calls.log'
$fakeProbe = Join-Path $Tmp 'fake_probe.ps1'
@"
param([int[]]`$WatchPids)
Add-Content -LiteralPath '$probeCalls' -Value "probe watch=`$(@(`$WatchPids) -join ',')"
Get-Content -LiteralPath `$env:FAKE_PROBE_JSON -Raw -ErrorAction Stop | ConvertFrom-Json
"@ | Set-Content -LiteralPath $fakeProbe -Encoding UTF8
function Invoke-Orch([string]$Name, $Pkg, [string]$InstallRc = '0', [string]$PreRc = '0', [string]$Sha = $null, [string]$LockFile = (Join-Path $Tmp "$Name.lock"), [string]$GitHubActions = '',
                     [string]$ProbeJson = '', [string]$Action = 'Deploy') {
    if (Test-Path $calls) { Remove-Item $calls -Force }
    if (Test-Path $probeCalls) { Remove-Item $probeCalls -Force }
    $evd = Join-Path $Tmp "ev_$Name"
    $env:FAKE_INSTALL_RC = $InstallRc; $env:FAKE_PRE_RC = $PreRc; $env:FAKE_PROBE_JSON = $ProbeJson
    $env:GITHUB_RUN_ID = '600'; $env:GITHUB_RUN_ATTEMPT = '1'; $env:GITHUB_JOB = "job-$Name"
    $ghPrev = $env:GITHUB_ACTIONS; $env:GITHUB_ACTIONS = $GitHubActions
    $a = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $orch, '-Action', $Action, '-EvidenceDir', $evd, '-LockWaitSeconds', '0', '-LockPath', $LockFile)
    if ($Action -eq 'Deploy') {
        $a += @('-Configuration', 'Debug', '-PackageDir', $Pkg.Dir, '-ExpectedRevision', $Rev, '-ExpectedRepository', 'zarfld/IntelAvbFilter', '-ExpectedRunId', '1234',
                '-ExpectedRunAttempt', '1', '-ExpectedDriverSha256', $(if ($Sha) { $Sha } else { $Pkg.Sha }), '-PinnedCertificate', $pinCer,
                '-InstallScript', $fakeInstall, '-PreFlightScript', $fakePre, '-SkipTrustCheck')
    }
    if ($ProbeJson) { $a += @('-ProbeScript', $fakeProbe) }
    # PS 5.1 wraps redirected native stderr in ErrorRecords; under this script's 'Stop' preference a child's error text
    # would abort the whole test instead of becoming that scenario's verdict.
    $eap = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    try {
        $out = & powershell.exe @a 2>&1 | Out-String
        $rc = $LASTEXITCODE
    } finally { $env:GITHUB_ACTIONS = $ghPrev; $ErrorActionPreference = $eap }
    $env:GITHUB_RUN_ID = ''; $env:GITHUB_RUN_ATTEMPT = ''; $env:GITHUB_JOB = ''; $env:FAKE_PROBE_JSON = ''
    $dep = $null; $dj = Join-Path $evd 'deployment.json'; if (Test-Path $dj) { $dep = Get-Content $dj -Raw | ConvertFrom-Json }
    $rcv = $null; $rj = Join-Path $evd 'recovery.json'; if (Test-Path $rj) { $rcv = Get-Content $rj -Raw | ConvertFrom-Json }
    $cl = @(); if (Test-Path $calls) { $cl = @(Get-Content $calls) }
    $pc = @(); if (Test-Path $probeCalls) { $pc = @(Get-Content $probeCalls) }
    return @{ Exit = $rc; Dep = $dep; Recovery = $rcv; Calls = $cl; ProbeCalls = $pc; Out = $out; Evd = $evd; Lock = $LockFile }
}
# Observation document as the real probe returns it; $Over replaces individual fields.
function New-Obs([hashtable]$Over = @{}) {
    $o = [ordered]@{
        observedUtc = '2026-11-20T10:00:00.0000000Z'
        processes = @(); processError = $null; watchedPids = @()
        service = 'RUNNING'
        imagePath = '\SystemRoot\System32\DriverStore\FileRepository\intelavbfilter.inf_amd64_x\IntelAvbFilter.sys'
        imageFile = 'C:\Windows\System32\DriverStore\FileRepository\intelavbfilter.inf_amd64_x\IntelAvbFilter.sys'
        imageSha256 = ('C0' * 32); imageError = $null; system32Sha256 = ('C0' * 32)
        pendingRenames = @(); pendingError = $null
        bindings = @([ordered]@{ name = 'Ethernet'; enabled = $true }); bindingsError = $null
    }
    foreach ($k in $Over.Keys) { $o[$k] = $Over[$k] }
    $p = Join-Path $Tmp ("obs_{0}.json" -f (Get-Random))
    [IO.File]::WriteAllText($p, ($o | ConvertTo-Json -Depth 6))
    return $p
}
# The retained 2026-11-17 incident shape (run 44052954003): netcfg -u left running, no post-state.
$incidentBody = [ordered]@{
    schema = 'avb-hil-deployment/1'; configuration = 'Release'; verdict = 'NEEDS_OPERATOR'
    reason = 'installation stopped in a state only a reboot or the operator can resolve (no reboot performed): netcfg -u did not finish in 180s (left running; #328 hang signature)'
    authorized = [ordered]@{ revision = ('2' * 40); repository = 'zarfld/IntelAvbFilter'; runId = '44052954003'; runAttempt = '1'; driverSha256 = ('A9' * 32) }
    install = [ordered]@{ outcome = 'NEEDS_OPERATOR'; exitCode = 3; reason = 'netcfg -u did not finish in 180s (left running; #328 hang signature)'
                          steps = @([ordered]@{ step = 'etw-unregister'; ok = $true; detail = 'completed=True rc=0' },
                                    [ordered]@{ step = 'netcfg-uninstall'; ok = $false; detail = 'completed=False rc= still running after 180s (PID 16216, left running)' })
                          postState = $null }
    machineOk = $false; machineOkReason = "deployment verdict 'NEEDS_OPERATOR' needs the operator or left the machine in an unknown state"
} | ConvertTo-Json -Depth 6
$liveNetcfg = @([ordered]@{ pid = 16216; name = 'netcfg.exe'; startedUtc = '2026-11-17T10:31:40.0000000Z'; commandLine = 'netcfg.exe -u MS_IntelAvbFilter'; why = 'driver-installation tool' })
# StrictMode-safe property read: $null for an absent property or object (never a throw that hides the real verdict).
function Get-P($o, [string]$n) { if ($null -ne $o -and $o.PSObject.Properties[$n]) { $o.PSObject.Properties[$n].Value } else { $null } }
# Unary comma: an empty result stays an empty array (StrictMode: $null.Count would throw).
function Get-Archives([string]$Lock) { return ,@(Get-ChildItem -Path (Split-Path $Lock) -Filter ((Split-Path $Lock -Leaf) + '.incident-*.json') -ErrorAction SilentlyContinue) }
function Get-RecoveryRecords([string]$Lock) { return ,@(Get-ChildItem -Path (Split-Path $Lock) -Filter ((Split-Path $Lock -Leaf) + '.recovery-*.json') -ErrorAction SilentlyContinue) }
# The pinned certificate: a throw-away self-signed certificate created in memory (never in a store) and exported
# public-only; the synthetic package is "signed" by its thumbprint through the manifest signer injection above, so
# the orchestrator (real signature reader) is exercised with -SkipTrustCheck plus a matching-thumbprint check below.
$rsa = [System.Security.Cryptography.RSA]::Create(2048)
$req = New-Object System.Security.Cryptography.X509Certificates.CertificateRequest -ArgumentList 'CN=avb-test-pin', $rsa, ([System.Security.Cryptography.HashAlgorithmName]::SHA256), ([System.Security.Cryptography.RSASignaturePadding]::Pkcs1)
$cert = $req.CreateSelfSigned([datetimeoffset]::UtcNow.AddDays(-1), [datetimeoffset]::UtcNow.AddDays(1))
[System.IO.File]::WriteAllBytes($pinCer, $cert.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert))
$oUnsigned = Invoke-Orch 'unsigned' $pOk
tc_check 'TEST-CIDEPLOY-040-unsigned-package-is-rejected-before-the-installer-runs' `
    (($oUnsigned.Exit -eq 1) -and ($oUnsigned.Dep.verdict -eq 'REJECTED_PACKAGE') -and ($oUnsigned.Calls.Count -eq 0) -and ($oUnsigned.Dep.reason -match 'not signed')) `
    ("exit={0} verdict={1} calls={2} reason={3}" -f $oUnsigned.Exit, $(if ($oUnsigned.Dep) { $oUnsigned.Dep.verdict }), ($oUnsigned.Calls -join '|'), $(if ($oUnsigned.Dep) { $oUnsigned.Dep.reason }))
$oWrongSha = Invoke-Orch 'wrongsha' $pOk -Sha ('4' * 64)
tc_check 'TEST-CIDEPLOY-041-package-not-matching-the-authorized-sha-never-reaches-the-installer' `
    (($oWrongSha.Dep.verdict -eq 'REJECTED_PACKAGE') -and ($oWrongSha.Calls.Count -eq 0)) ("verdict={0} calls={1}" -f $(if ($oWrongSha.Dep) { $oWrongSha.Dep.verdict }), ($oWrongSha.Calls -join '|'))
$busy = Join-Path $Tmp 'busy.lock'
$env:GITHUB_RUN_ID = ''; $env:GITHUB_JOB = ''
$hold = Enter-AvbHilLock -Kind 'local' -Label 'operator session' -Path $busy -IsProcessAlive $alive -Sleep $noSleep
$oBusy = Invoke-Orch 'busy' $pOk -LockFile $busy
$null = Exit-AvbHilLock -Owner $hold.Owner -Path $busy
tc_check 'TEST-CIDEPLOY-042-busy-machine-is-hil-not-executed-and-touches-nothing' `
    (($oBusy.Exit -eq 1) -and ($oBusy.Dep.verdict -eq 'NOT_EXECUTED_BUSY') -and ($oBusy.Calls.Count -eq 0)) ("verdict={0} calls={1}" -f $(if ($oBusy.Dep) { $oBusy.Dep.verdict }), ($oBusy.Calls -join '|'))
$oSeam = Invoke-Orch 'seam' $pOk -GitHubActions 'true'
tc_check 'TEST-CIDEPLOY-046-skiptrustcheck-seam-is-refused-under-github-actions-before-anything-runs' `
    (($oSeam.Exit -eq 1) -and ($null -eq $oSeam.Dep) -and ($oSeam.Calls.Count -eq 0) -and ($oSeam.Out -match 'SkipTrustCheck is an offline-test seam and is refused under GitHub Actions')) `
    ("exit={0} deployment.json={1} calls={2} out={3}" -f $oSeam.Exit, ($null -ne $oSeam.Dep), ($oSeam.Calls -join '|'), (($oSeam.Out -split "`n" | Select-Object -Last 3) -join ' | '))

# A job lost during installation (runner connection interrupted by netcfg, #328): its lock and IN_FLIGHT record stay
# on the machine (RUNNER_TEMP does not survive the next job); the next deployment attributes both.  #493: the record
# alone no longer decides - the CURRENT machine state does.  Here the lost job's netcfg is still running (genuinely
# unrecovered), so the deployment must still block and preserve everything.
$lostLock = Join-Path $Tmp 'lost.lock'
[System.IO.File]::WriteAllText($lostLock, ([ordered]@{ kind = 'ci'; label = 'hil Debug'; host = $env:COMPUTERNAME; pid = 1; runId = '590'; runAttempt = '1'; job = 'hil'; configuration = 'Debug'; runnerWorkerPid = 999999; runnerWorkerStartedUtc = '2026-10-28T23:52:19.0000000Z'; acquiredUtc = (Get-Date).AddHours(-1).ToUniversalTime().ToString('o') } | ConvertTo-Json))
[System.IO.File]::WriteAllText("$lostLock.inflight.json", ([ordered]@{ schema = 'avb-hil-deployment/1'; configuration = 'Debug'; verdict = 'IN_FLIGHT'; reason = 'installation started' } | ConvertTo-Json))
$oLost = Invoke-Orch 'afterlost' $pOk -LockFile $lostLock -ProbeJson (New-Obs @{ processes = $liveNetcfg })
$after = Get-Content -LiteralPath "$lostLock.inflight.json" -Raw | ConvertFrom-Json
tc_check 'TEST-CIDEPLOY-044-lost-job-in-flight-record-blocks-new-install-and-is-preserved' (
    $null -ne $oLost.Dep -and $oLost.Exit -eq 3 -and $oLost.Dep.verdict -eq 'NEEDS_OPERATOR' -and
    $oLost.Dep.previousDeployment.verdict -eq 'IN_FLIGHT' -and $oLost.Dep.lock.previousOwner.owner.runId -eq '590' -and
    $oLost.Dep.lock.previousOwner.owner.runnerWorkerPid -eq 999999 -and $after.verdict -eq 'IN_FLIGHT' -and
    $oLost.Calls.Count -eq 0 -and $oLost.Dep.machineOk -eq $false -and $oLost.ProbeCalls.Count -eq 1 -and
    $oLost.Dep.reason -match 'netcfg\.exe' -and (Get-Archives $lostLock).Count -eq 0
) ("exit={0} current={1} prev={2} persisted={3} calls={4} probe={5} archives={6} reason={7}" -f $oLost.Exit, $(if ($oLost.Dep) { $oLost.Dep.verdict }), (Get-P (Get-P $oLost.Dep 'previousDeployment') 'verdict'), $after.verdict, $oLost.Calls.Count, $oLost.ProbeCalls.Count, (Get-Archives $lostLock).Count, $(if ($oLost.Dep) { $oLost.Dep.reason }))

# A previous unsafe/unreadable record remains untouched and blocks deployment before package verification while the
# machine is genuinely unrecovered (#493: a live netcfg); the recovery attempt is recorded separately.
$unsafePrevious = @(
    @{ Name = 'prior_needs_operator'; Body = '{"schema":"avb-hil-deployment/1","configuration":"Debug","verdict":"NEEDS_OPERATOR","machineOk":false,"reason":"native installer still running"}' },
    @{ Name = 'prior_unreadable'; Body = '{ invalid json' }
)
$unsafeFailures = @()
foreach ($case in $unsafePrevious) {
    $lpPrevious = Join-Path $Tmp "$($case.Name).lock"
    [IO.File]::WriteAllText("$lpPrevious.inflight.json", $case.Body)
    $o = Invoke-Orch $case.Name $pOk -LockFile $lpPrevious -ProbeJson (New-Obs @{ processes = $liveNetcfg })
    $persisted = [IO.File]::ReadAllText("$lpPrevious.inflight.json")
    $recs = Get-RecoveryRecords $lpPrevious
    if ($o.Exit -ne 3 -or $null -eq $o.Dep -or $o.Dep.verdict -ne 'NEEDS_OPERATOR' -or $o.Calls.Count -ne 0 -or
        $persisted -cne $case.Body -or $o.Dep.machineOk -ne $false -or (Get-Archives $lpPrevious).Count -ne 0 -or
        $recs.Count -ne 1 -or (Get-Content $recs[0].FullName -Raw | ConvertFrom-Json).decision -ne 'NOT_READY') {
        $unsafeFailures += "$($case.Name): exit=$($o.Exit) verdict=$(if ($o.Dep) { $o.Dep.verdict }) calls=$($o.Calls.Count) preserved=$($persisted -ceq $case.Body) archives=$((Get-Archives $lpPrevious).Count) recoveryRecords=$($recs.Count)"
    }
}
tc_check 'TEST-CIDEPLOY-044a-needs-operator-and-unreadable-predecessors-block-without-erasing-evidence' ($unsafeFailures.Count -eq 0) ($unsafeFailures -join '; ')

# #493: the retained incident (netcfg -u left running, run 44052954003) no longer vetoes forever.  The deployment
# re-establishes readiness under ITS OWN lock from current observations; when ready it preserves the incident
# byte-for-byte, records the recovery separately and continues to the normal package verification (here: the
# synthetic package is unsigned, so the normal path rejects it - recovery never produces DEPLOYED by itself).
$recLock = Join-Path $Tmp 'recovered.lock'
[IO.File]::WriteAllText("$recLock.inflight.json", $incidentBody)
$incSha = (Get-FileHash -LiteralPath "$recLock.inflight.json" -Algorithm SHA256).Hash
$oRec = Invoke-Orch 'recovered' $pOk -LockFile $recLock -ProbeJson (New-Obs)
$recArch = Get-Archives $recLock; $recRecs = Get-RecoveryRecords $recLock
$recDoc = if ($recRecs.Count -eq 1) { Get-Content $recRecs[0].FullName -Raw | ConvertFrom-Json } else { $null }
$depRecovery = Get-P $oRec.Dep 'recovery'
tc_check 'TEST-CIDEPLOY-049-verified-recovery-preserves-the-incident-and-reaches-normal-package-verification' (
    $null -ne $oRec.Dep -and $oRec.Exit -eq 1 -and $oRec.Dep.verdict -eq 'REJECTED_PACKAGE' -and $oRec.Calls.Count -eq 0 -and
    $oRec.ProbeCalls.Count -eq 1 -and $oRec.ProbeCalls[0] -eq 'probe watch=16216' -and
    (Get-P (Get-P $oRec.Dep 'previousDeployment') 'verdict') -eq 'NEEDS_OPERATOR' -and
    $null -ne $depRecovery -and $depRecovery.decision -eq 'READY' -and $depRecovery.mode -eq 'automatic' -and
    $recArch.Count -eq 1 -and (Get-FileHash -LiteralPath $recArch[0].FullName -Algorithm SHA256).Hash -eq $incSha -and
    $null -ne $recDoc -and $recDoc.schema -eq 'avb-hil-recovery/1' -and $recDoc.decision -eq 'READY' -and
    $recDoc.incident.sha256 -eq $incSha -and $recDoc.incident.verdict -eq 'NEEDS_OPERATOR' -and $recDoc.incident.runId -eq '44052954003' -and
    $recDoc.incident.archivePath -eq $recArch[0].FullName -and $null -ne $oRec.Recovery -and $oRec.Recovery.decision -eq 'READY'
) ("exit={0} verdict={1} calls={2} probe={3} recovery={4} archives={5} records={6} archiveSha={7}" -f $oRec.Exit, $(if ($oRec.Dep) { $oRec.Dep.verdict }), $oRec.Calls.Count, ($oRec.ProbeCalls -join '|'),
    $(if ($depRecovery) { $depRecovery.decision }), $recArch.Count, $recRecs.Count, $(if ($recArch.Count -eq 1) { (Get-FileHash -LiteralPath $recArch[0].FullName).Hash -eq $incSha }))

# #493 negative cases: a CURRENT condition keeps the machine unrecovered.  Each must block before package verification,
# leave the incident byte-for-byte in place (no archive), record a NOT_READY recovery attempt and name the condition.
$notReadyCases = [ordered]@{
    'TEST-CIDEPLOY-051-recorded-hung-netcfg-still-alive-blocks-and-is-named' = @{
        Obs = @{ processes = $liveNetcfg; watchedPids = @([ordered]@{ pid = 16216; alive = $true; name = 'netcfg.exe' }) }; Want = 'netcfg\.exe.*16216' }
    'TEST-CIDEPLOY-052-surviving-installer-process-blocks-and-is-named' = @{
        Obs = @{ processes = @([ordered]@{ pid = 5150; name = 'powershell.exe'; startedUtc = '2026-11-20T09:45:36.0000000Z'; commandLine = 'powershell.exe -NoProfile -File C:\r\tools\setup\Install-Driver.ps1 -Reinstall -NonInteractive'; why = 'deployment script' }) }; Want = 'powershell\.exe.*5150.*Install-Driver\.ps1' }
    'TEST-CIDEPLOY-053-pending-driver-replacement-blocks-and-is-named' = @{
        Obs = @{ pendingRenames = @('\??\C:\Windows\System32\drivers\IntelAvbFilter.sys') }; Want = 'replacement.*IntelAvbFilter\.sys' }
}
foreach ($tc in $notReadyCases.Keys) {
    $c = $notReadyCases[$tc]
    $lk = Join-Path $Tmp ("nr_{0}.lock" -f $tc.Substring(14, 3))
    [IO.File]::WriteAllText("$lk.inflight.json", $incidentBody)
    $o = Invoke-Orch ("nr_{0}" -f $tc.Substring(14, 3)) $pOk -LockFile $lk -ProbeJson (New-Obs $c.Obs)
    $persisted = [IO.File]::ReadAllText("$lk.inflight.json")
    $recs = Get-RecoveryRecords $lk
    $doc = if ($recs.Count -eq 1) { Get-Content $recs[0].FullName -Raw | ConvertFrom-Json } else { $null }
    $reasons = if ($doc) { (@($doc.reasons) -join ' | ') } else { '' }
    tc_check $tc (
        $o.Exit -eq 3 -and $null -ne $o.Dep -and $o.Dep.verdict -eq 'NEEDS_OPERATOR' -and $o.Calls.Count -eq 0 -and $o.Dep.machineOk -eq $false -and
        $persisted -ceq $incidentBody -and (Get-Archives $lk).Count -eq 0 -and $null -ne $doc -and $doc.decision -eq 'NOT_READY' -and
        $reasons -match $c.Want -and $o.Dep.reason -match $c.Want -and $null -ne $o.Recovery -and $o.Recovery.decision -eq 'NOT_READY'
    ) ("exit={0} verdict={1} calls={2} preserved={3} archives={4} records={5} decision={6} reasons={7}" -f $o.Exit, $(if ($o.Dep) { $o.Dep.verdict }), $o.Calls.Count,
        ($persisted -ceq $incidentBody), (Get-Archives $lk).Count, $recs.Count, $(if ($doc) { $doc.decision }), $reasons)
}

# Unavailable observations, incomplete installed-driver identity and failed verification never establish readiness
# (function level: the real decision over the observation document).  A reused PID of the incident is not a conflict.
if (Get-Command Get-AvbHilReadiness -ErrorAction SilentlyContinue) {
    $rd = [ordered]@{
        'ready baseline'                          = @(@{}, 'READY', '')
        'driver absent (service NOT_FOUND)'       = @(@{ service = 'NOT_FOUND'; imagePath = $null; imageFile = $null; imageSha256 = $null }, 'READY', '')
        'incident PID reused by another image'    = @(@{ watchedPids = @([ordered]@{ pid = 16216; alive = $true; name = 'svchost.exe' }) }, 'READY', '')
        'process list unavailable'                = @(@{ processError = 'Zugriff verweigert' }, 'NOT_READY', 'process')
        'service state unknown'                   = @(@{ service = 'UNKNOWN:ACCESS_DENIED (Win32 error 5)' }, 'NOT_READY', 'service')
        'service STOP_PENDING'                    = @(@{ service = 'STOP_PENDING' }, 'NOT_READY', 'service')
        'image identity unavailable'              = @(@{ imageSha256 = $null; imageError = "image file 'C:\x\IntelAvbFilter.sys' not found" }, 'NOT_READY', 'identity')
        'image identity missing without error'    = @(@{ service = 'STOPPED'; imageSha256 = $null }, 'NOT_READY', 'identity')
        'pending-rename query failed'             = @(@{ pendingError = 'registry read failed' }, 'NOT_READY', 'replacement')
        'binding query failed'                    = @(@{ bindingsError = 'INetCfg busy' }, 'NOT_READY', 'binding')
    }
    $bad = @(foreach ($k in $rd.Keys) {
        $obs = Get-Content -LiteralPath (New-Obs $rd[$k][0]) -Raw | ConvertFrom-Json
        $r = Get-AvbHilReadiness -Observation $obs
        $txt = (@($r.Reasons) -join ' | ')
        if ($r.Decision -ne $rd[$k][1] -or ($rd[$k][1] -eq 'NOT_READY' -and $txt -notmatch $rd[$k][2]) -or ($rd[$k][1] -eq 'READY' -and @($r.Reasons).Count -ne 0)) { "$k -> $($r.Decision) [$txt]" }
    })
    $rNull = Get-AvbHilReadiness -Observation $null
    if ($rNull.Decision -ne 'NOT_READY') { $bad += "no observation -> $($rNull.Decision)" }
    tc_check 'TEST-CIDEPLOY-054-unavailable-observation-or-incomplete-identity-never-establishes-readiness' ($bad.Count -eq 0) ($bad -join '; ')
} else {
    tc_fail 'TEST-CIDEPLOY-054-unavailable-observation-or-incomplete-identity-never-establishes-readiness' 'Get-AvbHilReadiness missing in tools/lib/DriverDeploy.ps1'
}
# Failed verification at orchestration level: the observation itself fails (probe throws) -> blocked, preserved, named.
$failLock = Join-Path $Tmp 'probefail.lock'
[IO.File]::WriteAllText("$failLock.inflight.json", $incidentBody)
$oPF = Invoke-Orch 'probefail' $pOk -LockFile $failLock -ProbeJson (Join-Path $Tmp 'no-such-observation.json')
$pfRecs = Get-RecoveryRecords $failLock
$pfDoc = if ($pfRecs.Count -eq 1) { Get-Content $pfRecs[0].FullName -Raw | ConvertFrom-Json } else { $null }
tc_check 'TEST-CIDEPLOY-054a-failed-observation-blocks-and-preserves-the-incident' (
    $oPF.Exit -eq 3 -and $null -ne $oPF.Dep -and $oPF.Dep.verdict -eq 'NEEDS_OPERATOR' -and $oPF.Calls.Count -eq 0 -and
    [IO.File]::ReadAllText("$failLock.inflight.json") -ceq $incidentBody -and (Get-Archives $failLock).Count -eq 0 -and
    $null -ne $pfDoc -and $pfDoc.decision -eq 'NOT_READY' -and ((@($pfDoc.reasons) -join ' ') -match 'observation')
) ("exit={0} verdict={1} archives={2} records={3} reasons={4}" -f $oPF.Exit, $(if ($oPF.Dep) { $oPF.Dep.verdict }), (Get-Archives $failLock).Count, $pfRecs.Count, $(if ($pfDoc) { @($pfDoc.reasons) -join ' | ' }))

# Repeated recovery: the operator interface (-Action Recover) observes and records under the lock but never rewrites the
# incident; each later deployment re-establishes readiness under its own lock.  A second, later incident is preserved
# separately and the first archive stays byte-for-byte unchanged.
$repLock = Join-Path $Tmp 'repeat.lock'
[IO.File]::WriteAllText("$repLock.inflight.json", $incidentBody)
$incSha1 = (Get-FileHash -LiteralPath "$repLock.inflight.json").Hash
$r1 = Invoke-Orch 'rep_op1' $null -LockFile $repLock -ProbeJson (New-Obs) -Action Recover
$r2 = Invoke-Orch 'rep_op2' $null -LockFile $repLock -ProbeJson (New-Obs) -Action Recover
$afterOps = [IO.File]::ReadAllText("$repLock.inflight.json")
$opRecs = (Get-RecoveryRecords $repLock).Count; $opArch = (Get-Archives $repLock).Count; $lockAfterOps = Test-Path -LiteralPath $repLock
$d1 = Invoke-Orch 'rep_dep1' $pOk -LockFile $repLock -ProbeJson (New-Obs)
$arch1 = Get-Archives $repLock
$incident2 = ([ordered]@{ schema = 'avb-hil-deployment/1'; configuration = 'Debug'; verdict = 'IN_FLIGHT'; reason = 'installation started; second incident'; machineOk = $false } | ConvertTo-Json)
Start-Sleep -Milliseconds 50
[IO.File]::WriteAllText("$repLock.inflight.json", $incident2)
$incSha2 = (Get-FileHash -LiteralPath "$repLock.inflight.json").Hash
$d2 = Invoke-Orch 'rep_dep2' $pOk -LockFile $repLock -ProbeJson (New-Obs)
$arch2 = Get-Archives $repLock; $arch2 = @($arch2 | Sort-Object Name)
$archShas = @($arch2 | ForEach-Object { (Get-FileHash -LiteralPath $_.FullName).Hash })
tc_check 'TEST-CIDEPLOY-050-repeated-recovery-keeps-every-incident-and-never-rewrites-an-archive' (
    $r1.Exit -eq 0 -and $r2.Exit -eq 0 -and $null -ne $r1.Recovery -and $r1.Recovery.decision -eq 'READY' -and $r1.Recovery.mode -eq 'operator' -and
    $afterOps -ceq $incidentBody -and $opRecs -eq 2 -and $opArch -eq 0 -and -not $lockAfterOps -and
    $null -ne $d1.Dep -and $d1.Dep.verdict -eq 'REJECTED_PACKAGE' -and $arch1.Count -eq 1 -and
    $null -ne $d2.Dep -and $d2.Dep.verdict -eq 'REJECTED_PACKAGE' -and $arch2.Count -eq 2 -and
    (Get-FileHash -LiteralPath $arch1[0].FullName).Hash -eq $incSha1 -and ($archShas -contains $incSha1) -and ($archShas -contains $incSha2) -and
    (Get-RecoveryRecords $repLock).Count -eq 4
) ("op exits={0},{1} opDecision={2} inflightKept={3} opRecords={4} opArchives={5} lockLeft={6} dep1={7} dep2={8} archives={9} records={10}" -f $r1.Exit, $r2.Exit,
    $(if ($r1.Recovery) { $r1.Recovery.decision }), ($afterOps -ceq $incidentBody), $opRecs, $opArch, $lockAfterOps, $(if ($d1.Dep) { $d1.Dep.verdict }), $(if ($d2.Dep) { $d2.Dep.verdict }), $arch2.Count, (Get-RecoveryRecords $repLock).Count)

# The operator interface: NOT_READY exits 3 and names the condition; a busy machine is not observed and writes nothing
# machine-local; it is refused under GitHub Actions (CI reconciles inside Deploy), and the observation seam is refused
# there before anything runs.
$opFail = @()
$opLock = Join-Path $Tmp 'op.lock'
[IO.File]::WriteAllText("$opLock.inflight.json", $incidentBody)
$oNr = Invoke-Orch 'op_notready' $null -LockFile $opLock -ProbeJson (New-Obs @{ processes = $liveNetcfg }) -Action Recover
if ($oNr.Exit -ne 3 -or $null -eq $oNr.Recovery -or $oNr.Recovery.decision -ne 'NOT_READY' -or ((@($oNr.Recovery.reasons) -join ' ') -notmatch 'netcfg\.exe') -or
    [IO.File]::ReadAllText("$opLock.inflight.json") -cne $incidentBody -or (Test-Path -LiteralPath $opLock)) {
    $opFail += "not-ready: exit=$($oNr.Exit) decision=$(if ($oNr.Recovery) { $oNr.Recovery.decision }) lockLeft=$(Test-Path -LiteralPath $opLock)"
}
$opBusy = Join-Path $Tmp 'opbusy.lock'
[IO.File]::WriteAllText("$opBusy.inflight.json", $incidentBody)
$holdOp = Enter-AvbHilLock -Kind 'local' -Label 'operator session' -Path $opBusy -IsProcessAlive $alive -Sleep $noSleep
$oOb = Invoke-Orch 'op_busy' $null -LockFile $opBusy -ProbeJson (New-Obs) -Action Recover
$null = Exit-AvbHilLock -Owner $holdOp.Owner -Path $opBusy
if ($oOb.Exit -ne 1 -or $oOb.Out -notmatch 'NOT_EXECUTED_BUSY' -or $oOb.ProbeCalls.Count -ne 0 -or (Get-RecoveryRecords $opBusy).Count -ne 0 -or
    [IO.File]::ReadAllText("$opBusy.inflight.json") -cne $incidentBody -or $null -eq $oOb.Recovery -or $oOb.Recovery.decision -ne 'NOT_EXECUTED_BUSY') {
    $opFail += "busy: exit=$($oOb.Exit) probe=$($oOb.ProbeCalls.Count) records=$((Get-RecoveryRecords $opBusy).Count) evidence=$(if ($oOb.Recovery) { $oOb.Recovery.decision })"
}
$oGh = Invoke-Orch 'op_gha' $null -LockFile $opLock -Action Recover -GitHubActions 'true'
if ($oGh.Exit -ne 1 -or $oGh.Out -notmatch 'operator interface' -or (Get-RecoveryRecords $opLock).Count -ne 1) {
    $opFail += "gha: exit=$($oGh.Exit) records=$((Get-RecoveryRecords $opLock).Count) out=$(($oGh.Out -replace '\s+', ' ').Trim())"
}
$oGhSeam = Invoke-Orch 'op_gha_seam' $null -LockFile $opLock -ProbeJson (New-Obs) -Action Recover -GitHubActions 'true'
if ($oGhSeam.Exit -ne 1 -or $oGhSeam.ProbeCalls.Count -ne 0 -or $oGhSeam.Out -notmatch 'ProbeScript is an offline-test seam') {
    $opFail += "gha-seam: exit=$($oGhSeam.Exit) probe=$($oGhSeam.ProbeCalls.Count) out=$(($oGhSeam.Out -replace '\s+', ' ').Trim())"
}
tc_check 'TEST-CIDEPLOY-055-operator-recover-reports-conditions-respects-the-lock-and-is-refused-in-ci' ($opFail.Count -eq 0) ($opFail -join '; ')

# What counts as a surviving deployment operation (real classifier) and what the incident record yields (real parser).
if ((Get-Command Test-AvbHilConflictProcess -ErrorAction SilentlyContinue) -and (Get-Command Get-AvbHilIncident -ErrorAction SilentlyContinue)) {
    $cls = @(
        @('netcfg.exe', 'netcfg.exe -u MS_IntelAvbFilter', $true), @('NETCFG.EXE', $null, $true), @('pnputil.exe', 'pnputil /enum-drivers', $true),
        @('wevtutil.exe', 'wevtutil um x.man', $true),
        @('powershell.exe', 'powershell.exe -NoProfile -File C:\r\tools\setup\Install-Driver.ps1 -Reinstall', $true),
        @('powershell.exe', '"powershell.exe" -File C:\r\tools\setup\Install-Driver-Elevated.ps1 -Configuration Debug', $true),
        @('pwsh.exe', 'pwsh -File C:\r\tools\ci\Invoke-HilDeployment.ps1 -Action Deploy', $true),
        @('powershell.exe', $null, $true),
        @('powershell.exe', 'powershell.exe -File C:\r\tools\test\Run-Tests-CI.ps1', $false),
        @('Code.exe', 'Code.exe C:\r\tools\setup\Install-Driver.ps1', $false), @('notepad.exe', 'notepad.exe', $false)
    )
    $clsBad = @(foreach ($c in $cls) { $w = Test-AvbHilConflictProcess -Name $c[0] -CommandLine $c[1]; if ([bool]$w -ne $c[2]) { "$($c[0]) '$($c[1])' -> '$w'" } })
    $incP = Join-Path $Tmp 'parse.lock.inflight.json'; [IO.File]::WriteAllText($incP, $incidentBody)
    $i1 = Get-AvbHilIncident -Path $incP
    if (-not $i1.present -or -not $i1.readable -or $i1.verdict -ne 'NEEDS_OPERATOR' -or $i1.runId -ne '44052954003' -or (@($i1.hungPids) -join ',') -ne '16216' -or
        $i1.sha256 -ne (Get-FileHash -LiteralPath $incP).Hash -or $i1.configuration -ne 'Release') { $clsBad += "incident parse: $(($i1 | ConvertTo-Json -Compress))" }
    [IO.File]::WriteAllText($incP, '{ invalid json'); $i2 = Get-AvbHilIncident -Path $incP
    if (-not $i2.present -or $i2.readable -or -not $i2.sha256 -or @($i2.hungPids).Count -ne 0) { $clsBad += "unreadable parse: $(($i2 | ConvertTo-Json -Compress))" }
    $i3 = Get-AvbHilIncident -Path (Join-Path $Tmp 'absent.inflight.json')
    if ($i3.present) { $clsBad += 'absent record reported present' }
    tc_check 'TEST-CIDEPLOY-056-conflicting-operations-and-incident-identity-are-classified' ($clsBad.Count -eq 0) ($clsBad -join '; ')
} else {
    tc_fail 'TEST-CIDEPLOY-056-conflicting-operations-and-incident-identity-are-classified' 'Test-AvbHilConflictProcess / Get-AvbHilIncident missing in tools/lib/DriverDeploy.ps1'
}

# LOCAL SMOKE (structure only, read-only): the real probe returns every observation field on this host and never lists
# itself.  Its values are host-dependent and are NOT asserted; hardware readiness is verified only on the HIL runner.
if (Get-Command Get-AvbHilRecoveryObservation -ErrorAction SilentlyContinue) {
    $real = Get-AvbHilRecoveryObservation -WatchPids @($PID)
    $keys = 'observedUtc', 'processes', 'processError', 'watchedPids', 'service', 'imagePath', 'imageFile', 'imageSha256', 'imageError', 'system32Sha256', 'pendingRenames', 'pendingError', 'bindings', 'bindingsError'
    $missingKeys = @($keys | Where-Object { -not $real.Contains($_) })
    $selfListed = @(@($real.processes) | Where-Object { $_ -and [int]$_.pid -eq $PID }).Count
    $watchedSelf = @(@($real.watchedPids) | Where-Object { $_ -and [int]$_.pid -eq $PID -and $_.alive }).Count
    tc_check 'TEST-CIDEPLOY-057-LOCALSMOKE-real-observation-probe-returns-every-field-and-excludes-itself' (
        $missingKeys.Count -eq 0 -and $selfListed -eq 0 -and $watchedSelf -eq 1
    ) ("missing={0} selfListed={1} watchedSelf={2}" -f ($missingKeys -join ','), $selfListed, $watchedSelf)
} else {
    tc_fail 'TEST-CIDEPLOY-057-LOCALSMOKE-real-observation-probe-returns-every-field-and-excludes-itself' 'Get-AvbHilRecoveryObservation missing in tools/lib/DriverDeploy.ps1'
}

# STATIC: reconciliation runs under the deployment's own lock, before crash evidence and package verification (no
# unlocked gap), and recovery can never produce DEPLOYED (only the verified installation path does).
$orchSrc = [IO.File]::ReadAllText($orch)
$iL = $orchSrc.IndexOf('Enter-AvbHilLock -Kind ''ci'''); $iR = $orchSrc.IndexOf('Invoke-AvbHilReconciliation -Mode ''automatic'''); $iC = $orchSrc.IndexOf('Get-AvbCrashEvidence -Source'); $iP = $orchSrc.IndexOf('Test-AvbDriverPackage -PackageDir')
$ddAst = [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $RepoRoot 'tools\lib\DriverDeploy.ps1'), [ref]$null, [ref]$null)
$recFns = @($ddAst.FindAll({ param($x) $x -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $x.Name -match '^(Invoke-AvbHilReconciliation|Get-AvbHilReadiness|Get-AvbHilRecoveryObservation|Get-AvbHilIncident)$' }, $true))
$recDeployed = @($recFns | Where-Object { $_.Extent.Text -match 'DEPLOYED' })
tc_check 'TEST-CIDEPLOY-058-STATIC-reconciliation-is-locked-precedes-package-checks-and-never-yields-deployed' (
    $iL -gt 0 -and $iR -gt $iL -and $iC -gt $iR -and $iP -gt $iC -and $recFns.Count -eq 4 -and $recDeployed.Count -eq 0 -and
    ([regex]::Matches($orchSrc, "Complete-Deployment 'DEPLOYED'")).Count -eq 1
) ("lock@{0} reconcile@{1} crash@{2} pkg@{3} recoveryFunctions={4} mentionDEPLOYED={5}" -f $iL, $iR, $iC, $iP, $recFns.Count, (($recDeployed | ForEach-Object Name) -join ','))

# Do not impose a permanent hold after a completed previous deployment with affirmative machineOk.
$priorSafeLock = Join-Path $Tmp 'prior_safe.lock'
[IO.File]::WriteAllText("$priorSafeLock.inflight.json", '{"schema":"avb-hil-deployment/1","configuration":"Debug","verdict":"DEPLOYED","machineOk":true,"reason":"verified"}')
$oPriorSafe = Invoke-Orch 'prior_safe' $pOk -LockFile $priorSafeLock
tc_check 'TEST-CIDEPLOY-044b-previous-verified-deployment-still-reaches-package-checks' (
    $oPriorSafe.Dep.previousDeployment.verdict -eq 'DEPLOYED' -and $oPriorSafe.Dep.verdict -eq 'REJECTED_PACKAGE' -and $oPriorSafe.Exit -eq 1
) ("previous={0} current={1} exit={2}" -f $oPriorSafe.Dep.previousDeployment.verdict, $oPriorSafe.Dep.verdict, $oPriorSafe.Exit)
$hilSrc = [System.IO.File]::ReadAllText((Join-Path $RepoRoot '.github\workflows\hil.yml'))
# Each Debug/Release matrix leg reads the machine state of ITS OWN assigned runner.
# One physical job definition expands into two legs; a remote leg's machine_ok must never govern this leg.
# IN_FLIGHT / NEEDS_OPERATOR / NOT_EXECUTED_BUSY must not become machineOk=true from verdict names alone.
$hilLf = $hilSrc -replace "`r`n", "`n"
$hilStart = $hilLf.IndexOf("`n  hil:`n")
$hilEnd = $hilLf.IndexOf("`n  hil-acceptance:`n")
$hilPhysical = if ($hilStart -ge 0 -and $hilEnd -gt $hilStart) { $hilLf.Substring($hilStart, $hilEnd - $hilStart) } else { '' }
$mokLine = "elseif (`$d.PSObject.Properties['machineOk'] -and `$d.machineOk -eq `$true) { 'true' } else { 'false' }"
$mokStates = @(if (Get-Command Get-AvbMachineOk -ErrorAction SilentlyContinue) { foreach ($v in 'NEEDS_OPERATOR', 'NOT_EXECUTED_BUSY', 'IN_FLIGHT') { if ((Get-AvbMachineOk -Record ([pscustomobject]@{ verdict = $v })).Ok) { $v } } } else { 'Get-AvbMachineOk missing' })
$matrixLocalMachine = (
    $hilPhysical -match '(?m)^      matrix:\s*\r?\n        configuration: \[Debug, Release\]' -and
    $hilPhysical -match '(?m)^    needs: \[authorize\]$' -and
    $hilPhysical -match '(?m)^        id: machine\s*\r?\n        if: always\(\)' -and
    $hilPhysical.Contains("Join-Path `$env:RUNNER_TEMP 'hil-evidence\deployment.json'") -and
    $hilPhysical.Contains("`$d.PSObject.Properties['machineOk']") -and
    $hilPhysical.Contains('Invoke-HilDeployment.ps1 -Action Deploy') -and
    $hilPhysical.Contains('Invoke-HilDeployment.ps1 -Action Release') -and
    $hilPhysical -notmatch 'needs\.hil-(debug|release)\.' -and
    $hilPhysical -notmatch 'needs\.hil\.outputs\.machine_ok'
)
tc_check 'TEST-CIDEPLOY-045-in-flight-or-operator-states-remain-unsafe-per-independent-matrix-leg' `
    (($hilPhysical.Length -gt 0) -and ([regex]::Matches($hilPhysical, [regex]::Escape($mokLine)).Count -eq 1) -and
     ([regex]::Matches($hilLf, [regex]::Escape($mokLine)).Count -eq 1) -and
     $matrixLocalMachine -and ($hilLf -notmatch [regex]::Escape("`$v -in 'NEEDS_OPERATOR'")) -and ($mokStates.Count -eq 0)) `
    "matrix-local machine_ok contract missing, bypassed, or unsafe record accepted: $($mokStates -join ', ')"

# machine_ok after a partially completed deployment: a failed deployment leaves the machine usable for the next
# configuration only when the state recorded after the failure is settled and known (CI run 41500818916 recorded
# DEPLOY_FAILED -> machine_ok=true without looking at the state netcfg -u had left).
if (Get-Command Get-AvbMachineOk -ErrorAction SilentlyContinue) {
    $st = { param([string]$Svc, [object[]]$Pend = @()) [pscustomobject]@{ verdict = 'DEPLOY_FAILED'; install = [pscustomobject]@{ outcome = 'FAILED'; postState = [pscustomobject]@{ service = $Svc; bindings = @(); pendingRenames = $Pend } } } }
    $mo = [ordered]@{
        'DEPLOYED'                           = @([pscustomobject]@{ verdict = 'DEPLOYED' }, $true)
        'REJECTED_PACKAGE (untouched)'       = @([pscustomobject]@{ verdict = 'REJECTED_PACKAGE' }, $true)
        'NOT_EXECUTED_BUSY'                  = @([pscustomobject]@{ verdict = 'NOT_EXECUTED_BUSY' }, $false)
        'NEEDS_OPERATOR'                     = @([pscustomobject]@{ verdict = 'NEEDS_OPERATOR' }, $false)
        'IN_FLIGHT (job lost)'               = @([pscustomobject]@{ verdict = 'IN_FLIGHT' }, $false)
        'no record'                          = @($null, $false)
        'DEPLOY_FAILED, no install record'   = @([pscustomobject]@{ verdict = 'DEPLOY_FAILED'; install = $null }, $false)
        'DEPLOY_FAILED, post NOT_FOUND'      = @((& $st 'NOT_FOUND'), $true)
        'DEPLOY_FAILED, post RUNNING'        = @((& $st 'RUNNING'), $true)
        'DEPLOY_FAILED, post UNKNOWN'        = @((& $st 'UNKNOWN:UNRESOLVED (probe)'), $false)
        'DEPLOY_FAILED, post STOP_PENDING'   = @((& $st 'STOP_PENDING'), $false)
        'DEPLOY_FAILED, reboot rename'       = @((& $st 'NOT_FOUND' @('\??\C:\x\IntelAvbFilter.sys')), $false)
    }
    $wrong = @(foreach ($k in $mo.Keys) { $m = Get-AvbMachineOk -Record $mo[$k][0]; if ([bool]$m.Ok -ne $mo[$k][1] -or -not $m.Reason) { "$k -> ok=$($m.Ok) reason='$($m.Reason)'" } })
    tc_check 'TEST-CIDEPLOY-047-machine-ok-after-a-failed-deployment-requires-a-recorded-settled-known-state' ($wrong.Count -eq 0) ($wrong -join '; ')
} else {
    tc_fail 'TEST-CIDEPLOY-047-machine-ok-after-a-failed-deployment-requires-a-recorded-settled-known-state' 'Get-AvbMachineOk missing in tools/lib/DriverDeploy.ps1'
}
$pv = { param($o, [string]$n) if ($o -and $o.PSObject.Properties[$n]) { $o.PSObject.Properties[$n].Value } else { '<absent>' } }
tc_check 'TEST-CIDEPLOY-048-deployment-record-carries-the-machine-ok-decision' `
    (((& $pv $oUnsigned.Dep 'machineOk') -eq $true) -and [bool](& $pv $oUnsigned.Dep 'machineOkReason') -and ((& $pv $oBusy.Dep 'machineOk') -eq $false)) `
    ("rejected: machineOk={0} ({1}) | busy: machineOk={2}" -f (& $pv $oUnsigned.Dep 'machineOk'), (& $pv $oUnsigned.Dep 'machineOkReason'), (& $pv $oBusy.Dep 'machineOk'))

# Signed-path orchestration: replace the real signature reader through a package signed by the pinned thumbprint is
# not possible offline without a trusted test root; the deployment/pre-flight ordering is exercised with a package
# whose manifest and files are valid and a pinned certificate whose thumbprint the orchestrator reads - signature
# checks are covered by TEST-CIDEPLOY-003 (function level) and rejected here (TEST-CIDEPLOY-040) as they must be.
$ordSrc = [System.IO.File]::ReadAllText($orch)
$iLock = $ordSrc.IndexOf('Enter-AvbHilLock'); $iCrash = $ordSrc.IndexOf('Get-AvbCrashEvidence -Source'); $iPkg = $ordSrc.IndexOf('Test-AvbDriverPackage -PackageDir')
$iInst = $ordSrc.IndexOf('-File $InstallScript'); $iPre = $ordSrc.IndexOf('-File $PreFlightScript')
tc_check 'TEST-CIDEPLOY-043-order-lock-crash-evidence-package-install-preflight' `
    (($iLock -gt 0) -and ($iCrash -gt $iLock) -and ($iPkg -gt $iCrash) -and ($iInst -gt $iPkg) -and ($iPre -gt $iInst) -and ($ordSrc -match "HIL_REQUIRE_DEPLOYED_SHA = '1'") -and
     ($ordSrc -match '-RequireTrusted:\(-not \$SkipTrustCheck\)')) ("lock@{0} crash@{1} pkg@{2} install@{3} preflight@{4}" -f $iLock, $iCrash, $iPkg, $iInst, $iPre)

Remove-Item Env:FAKE_INSTALL_RC, Env:FAKE_PRE_RC -ErrorAction SilentlyContinue
Remove-Item $Tmp -Recurse -Force -ErrorAction SilentlyContinue
Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-CIDEPLOY: unattended deployment contract violated" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-CIDEPLOY: unattended deployment orchestration holds (offline; hardware UNVERIFIED)" -ForegroundColor Green
exit 0
