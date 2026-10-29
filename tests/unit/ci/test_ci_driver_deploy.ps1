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

# ---------------------------------------------------------------------------------------------------------
# Serialization (CI case 10)
# ---------------------------------------------------------------------------------------------------------
$lp = Join-Path $Tmp 'hil.lock'
$alive = { param($Id) $true }; $dead = { param($Id) $false }; $noSleep = { param($s) }
$env:GITHUB_RUN_ID = '500'; $env:GITHUB_RUN_ATTEMPT = '1'; $env:GITHUB_JOB = 'hil-debug'
$a = Enter-AvbHilLock -Kind 'ci' -Label 'hil Debug' -Path $lp -IsProcessAlive $alive -Sleep $noSleep
$env:GITHUB_RUN_ID = ''; $env:GITHUB_JOB = ''
$b = Enter-AvbHilLock -Kind 'local' -Label 'LocalSuites' -Path $lp -WaitSeconds 30 -IsProcessAlive $dead -IsCiJobActive { $true } -Sleep $noSleep
tc_check 'TEST-CIDEPLOY-020-operator-run-cannot-enter-while-a-ci-job-holds-the-machine' ($a.Ok -and -not $b.Ok -and $b.Why -match 'is executing on this runner' -and $b.Waited -eq 30) ("ci={0} local={1} why={2} waited={3}" -f $a.Ok, $b.Ok, $b.Why, $b.Waited)
$env:GITHUB_RUN_ID = '500'; $env:GITHUB_JOB = 'hil-release'
$c = Enter-AvbHilLock -Kind 'ci' -Label 'hil Release' -Path $lp -IsProcessAlive $alive -Sleep $noSleep
tc_check 'TEST-CIDEPLOY-021-lock-left-by-a-dead-ci-job-is-taken-over-with-the-previous-owner-recorded' `
    ($c.Ok -and $null -ne $c.PreviousOwner -and $c.PreviousOwner.owner.job -eq 'hil-debug' -and $c.PreviousOwner.why -match 'did not release') ("ok={0} prev={1}" -f $c.Ok, $(if ($c.PreviousOwner) { $c.PreviousOwner.why }))
$x = Exit-AvbHilLock -Owner $a.Owner -Path $lp
$y = Exit-AvbHilLock -Owner $c.Owner -Path $lp
tc_check 'TEST-CIDEPLOY-022-only-the-owner-releases' ((-not $x.Ok) -and $y.Ok -and -not (Test-Path -LiteralPath $lp)) ("foreign={0} own={1}" -f $x.Why, $y.Ok)
$env:GITHUB_RUN_ID = ''; $env:GITHUB_RUN_ATTEMPT = ''; $env:GITHUB_JOB = ''
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
function Invoke-Orch([string]$Name, $Pkg, [string]$InstallRc = '0', [string]$PreRc = '0', [string]$Sha = $null, [string]$LockFile = (Join-Path $Tmp "$Name.lock"), [string]$GitHubActions = '') {
    if (Test-Path $calls) { Remove-Item $calls -Force }
    $evd = Join-Path $Tmp "ev_$Name"
    $env:FAKE_INSTALL_RC = $InstallRc; $env:FAKE_PRE_RC = $PreRc
    $env:GITHUB_RUN_ID = '600'; $env:GITHUB_RUN_ATTEMPT = '1'; $env:GITHUB_JOB = "job-$Name"
    $ghPrev = $env:GITHUB_ACTIONS; $env:GITHUB_ACTIONS = $GitHubActions
    try {
        $out = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $orch -Action Deploy -Configuration Debug -PackageDir $Pkg.Dir -ExpectedRevision $Rev `
                   -ExpectedRepository 'zarfld/IntelAvbFilter' -ExpectedRunId '1234' -ExpectedRunAttempt '1' -ExpectedDriverSha256 $(if ($Sha) { $Sha } else { $Pkg.Sha }) `
                   -EvidenceDir $evd -PinnedCertificate $pinCer -LockWaitSeconds 0 -InstallScript $fakeInstall -PreFlightScript $fakePre -LockPath $LockFile -SkipTrustCheck 2>&1 | Out-String
        $rc = $LASTEXITCODE
    } finally { $env:GITHUB_ACTIONS = $ghPrev }
    $env:GITHUB_RUN_ID = ''; $env:GITHUB_RUN_ATTEMPT = ''; $env:GITHUB_JOB = ''
    $dep = $null; $dj = Join-Path $evd 'deployment.json'; if (Test-Path $dj) { $dep = Get-Content $dj -Raw | ConvertFrom-Json }
    $cl = @(); if (Test-Path $calls) { $cl = @(Get-Content $calls) }
    return @{ Exit = $rc; Dep = $dep; Calls = $cl; Out = $out; Evd = $evd; Lock = $LockFile }
}
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
# on the machine (RUNNER_TEMP does not survive the next job); the next deployment attributes both.
$lostLock = Join-Path $Tmp 'lost.lock'
[System.IO.File]::WriteAllText($lostLock, ([ordered]@{ kind = 'ci'; label = 'hil Debug'; host = $env:COMPUTERNAME; pid = 1; runId = '590'; runAttempt = '1'; job = 'hil-debug'; acquiredUtc = (Get-Date).AddHours(-1).ToUniversalTime().ToString('o') } | ConvertTo-Json))
[System.IO.File]::WriteAllText("$lostLock.inflight.json", ([ordered]@{ schema = 'avb-hil-deployment/1'; configuration = 'Debug'; verdict = 'IN_FLIGHT'; reason = 'installation started' } | ConvertTo-Json))
$oLost = Invoke-Orch 'afterlost' $pOk -LockFile $lostLock
$after = Get-Content -LiteralPath "$lostLock.inflight.json" -Raw | ConvertFrom-Json
tc_check 'TEST-CIDEPLOY-044-lost-job-lock-and-in-flight-record-are-attributed-by-the-next-deployment' `
    (($null -ne $oLost.Dep) -and ($oLost.Dep.previousDeployment.verdict -eq 'IN_FLIGHT') -and ($oLost.Dep.lock.previousOwner.owner.runId -eq '590') -and
     ($after.verdict -eq $oLost.Dep.verdict) -and ($after.verdict -ne 'IN_FLIGHT')) `
    ("prev={0} prevOwner={1} inflight-now={2}" -f $(if ($oLost.Dep) { $oLost.Dep.previousDeployment.verdict }), $(if ($oLost.Dep) { $oLost.Dep.lock.previousOwner.owner.runId }), $after.verdict)
$hilSrc = [System.IO.File]::ReadAllText((Join-Path $RepoRoot '.github\workflows\hil.yml'))
tc_check 'TEST-CIDEPLOY-045-in-flight-or-operator-states-stop-the-next-configuration' `
    ([regex]::Matches($hilSrc, [regex]::Escape("`$v -in 'NEEDS_OPERATOR', 'NOT_EXECUTED_BUSY', 'IN_FLIGHT'")).Count -eq 2) 'machine_ok does not treat IN_FLIGHT/NEEDS_OPERATOR/BUSY as unusable in both physical jobs'

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
