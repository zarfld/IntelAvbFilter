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
# Named predicates: on failure every false predicate is listed with its actual value (CI logs are the only evidence).
# $p: ordered @{ 'predicate' = @(<bool>, <actual value>) }
function tc_preds {
    param([string]$n, [System.Collections.Specialized.OrderedDictionary]$p)
    $bad = @(foreach ($k in $p.Keys) { if (-not [bool]$p[$k][0]) { "$k (actual: $($p[$k][1]))" } })
    tc_check $n ($bad.Count -eq 0) ("failed predicates: " + ($bad -join '; '))
}

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
    #     Adapters (@(@{Name;Guid;AdminEnabled}); default: one administratively enabled adapter per initial binding),
    #     DisableFails / EnableFails (names whose administrative change is reported failed),
    #     EnableNoEffect (names whose enable is reported done but leaves the adapter disabled: unconfirmed restoration)
    $st = @{ Service = $S.Service; Bindings = @($S.Bindings); InstalledSha = $S.InstalledSha; Copies = 0; ImageSha = $S.InstalledSha }
    $st.Adapters = @(if ($S['Adapters']) { $S['Adapters'] | ForEach-Object { @{ Name = $_.Name; Guid = $_.Guid; AdminEnabled = [bool]$_.AdminEnabled } } }
                     else { $S.Bindings | ForEach-Object { @{ Name = $_; Guid = "{$_}"; AdminEnabled = $true } } })
    $log = $S.Log; $drivers = Join-Path $Tmp ('drv_' + (Get-Random)); New-Item -ItemType Directory -Path $drivers -Force | Out-Null
    return @{
        DriversDir = $drivers
        Invoke = { param($File, $Arguments, $TimeoutSeconds, $WorkDir)
            $log.Add("$File $($Arguments -join ' ')")
            if ($File -eq 'netcfg.exe' -and $Arguments[0] -eq '-u') { $r = $S.UninstallResult; if ($r.Completed) { $st.Service = $r.Service; if ($r.ExitCode -eq 0) { $st.Bindings = @() } }; return @{ Completed = $r.Completed; ExitCode = $r.ExitCode; Output = '' } }
            if ($File -eq 'netcfg.exe' -and $Arguments[0] -eq '-l') { $r = $S.InstallResult; if ($r.Completed -and $r.ExitCode -eq 0) { $st.Service = $r.Service; $st.Bindings = @('Ethernet I226', 'Ethernet Mgmt'); $st.ImageSha = $(if ($S['SelectedImageSha']) { $S['SelectedImageSha'] } else { $st.InstalledSha }) }; return @{ Completed = $r.Completed; ExitCode = $r.ExitCode; Output = '' } }
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
        AdapterStates = { @($st.Adapters | ForEach-Object { @{ Name = $_.Name; Guid = $_.Guid; AdminEnabled = $_.AdminEnabled } }) }.GetNewClosure()
        # Service ImagePath file (#510 identity postcondition): the staged image after netcfg -l, unless SelectedImageSha
        # scripts another store package being selected, or ImageUnobservable makes it unmeasurable.
        ServiceImage = { if ($S['ImageUnobservable']) { return @{ Path = $null; Sha256 = $null; Error = 'ImagePath not readable (scripted)' } }
            return @{ Path = 'C:\Windows\System32\DriverStore\FileRepository\intelavbfilter.inf_amd64_fake\IntelAvbFilter.sys'; Sha256 = $st.ImageSha; Error = $null } }.GetNewClosure()
        SetAdapterEnabled = { param($Name, $Enabled, $TimeoutSeconds)
            $log.Add(('{0} {1}' -f $(if ($Enabled) { 'enable' } else { 'disable' }), $Name))
            $fails = if ($Enabled) { @($S['EnableFails']) } else { @($S['DisableFails']) }
            if ($fails -contains $Name) { return @{ Ok = $false; Detail = 'scripted failure'; ElapsedMs = 1 } }
            if (-not ($Enabled -and @($S['EnableNoEffect']) -contains $Name)) { foreach ($a in $st.Adapters) { if ($a.Name -eq $Name) { $a.AdminEnabled = [bool]$Enabled } } }
            return @{ Ok = $true; Detail = 'done'; ElapsedMs = 1 } }.GetNewClosure()
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

# #510 design A: adapter quiescing before netcfg -u (offline orchestration only; hardware evidence is separate).
function Get-LogIndex($d, [string]$Pattern) { for ($i = 0; $i -lt $d.Log.Count; $i++) { if ($d.Log[$i] -match $Pattern) { return $i } }; return -1 }
function Get-LogLastIndex($d, [string]$Pattern) { $k = -1; for ($i = 0; $i -lt $d.Log.Count; $i++) { if ($d.Log[$i] -match $Pattern) { $k = $i } }; return $k }
$twoUp = @(@{ Name = 'Ethernet I226'; Guid = '{a}'; AdminEnabled = $true }, @{ Name = 'Ethernet Mgmt'; Guid = '{m}'; AdminEnabled = $true })
$dQ = Invoke-Deploy @{ Bindings = @('Ethernet I226', 'Ethernet Mgmt'); Adapters = $twoUp }
$iU = Get-LogIndex $dQ '^netcfg\.exe -u'; $iL = Get-LogIndex $dQ '^netcfg\.exe -l'
$dis = @($dQ.Log | Where-Object { $_ -match '^disable ' }); $ena = @($dQ.Log | Where-Object { $_ -match '^enable ' })
tc_check 'TEST-CIDEPLOY-070-bound-enabled-adapters-are-disabled-before-uninstall-and-re-enabled-after-install-with-confirmed-restoration' `
    (($dQ.R.outcome -eq 'DEPLOYED') -and (($dis -join ',') -eq 'disable Ethernet I226,disable Ethernet Mgmt') -and (($ena -join ',') -eq 'enable Ethernet I226,enable Ethernet Mgmt') -and
     ((Get-LogLastIndex $dQ '^disable ') -lt $iU) -and ((Get-LogIndex $dQ '^enable ') -gt $iL) -and ($null -ne $dQ.R.adapters) -and ($dQ.R.adapters.restored -eq $true) -and
     (@($dQ.R.adapters.targets).Count -eq 2) -and (@($dQ.R.adapters.targets | Where-Object { $_.originalAdminEnabled -ne $true }).Count -eq 0) -and
     (@($dQ.R.steps | Where-Object { $_.step -eq 'adapter-quiesce' -and $_.ok }).Count -eq 1) -and (@($dQ.R.steps | Where-Object { $_.step -eq 'adapter-restore' -and $_.ok }).Count -eq 1)) (Show-D $dQ)

$mixed = @(@{ Name = 'Ethernet I226'; Guid = '{a}'; AdminEnabled = $true }, @{ Name = 'Ethernet Off'; Guid = '{o}'; AdminEnabled = $false }, @{ Name = 'Ethernet Unbound'; Guid = '{u}'; AdminEnabled = $true })
$dMix = Invoke-Deploy @{ Bindings = @('Ethernet I226', 'Ethernet Off'); Adapters = $mixed }
tc_check 'TEST-CIDEPLOY-071-previously-disabled-and-unbound-adapters-are-never-touched' `
    (($dMix.R.outcome -eq 'DEPLOYED') -and (@($dMix.Log | Where-Object { $_ -match '^(disable|enable) Ethernet (Off|Unbound)$' }).Count -eq 0) -and
     (@($dMix.Log | Where-Object { $_ -match '^(disable|enable) Ethernet I226$' }).Count -eq 2) -and (@($dMix.R.adapters.targets).Count -eq 1)) (Show-D $dMix)

$dNone = Invoke-Deploy @{ Service = 'NOT_FOUND'; Bindings = @(); InstalledSha = $null }
tc_check 'TEST-CIDEPLOY-072-no-adapter-change-when-there-is-nothing-to-uninstall' `
    (($dNone.R.outcome -eq 'DEPLOYED') -and (@($dNone.Log | Where-Object { $_ -match '^(disable|enable) ' }).Count -eq 0)) (Show-D $dNone)

$dDisF = Invoke-Deploy @{ Bindings = @('Ethernet I226', 'Ethernet Mgmt'); Adapters = $twoUp; DisableFails = @('Ethernet Mgmt') }
$dDisF2 = Invoke-Deploy @{ Bindings = @('Ethernet I226', 'Ethernet Mgmt'); Adapters = $twoUp; DisableFails = @('Ethernet Mgmt'); EnableFails = @('Ethernet I226') }
tc_check 'TEST-CIDEPLOY-073-failed-disable-starts-no-uninstall-and-re-enables-what-was-disabled' `
    (($dDisF.R.outcome -eq 'FAILED') -and (@($dDisF.Log | Where-Object { $_ -match '^netcfg' }).Count -eq 0) -and ($dDisF.Log -contains 'enable Ethernet I226') -and
     ($dDisF.R.adapters.restored -eq $true) -and ($dDisF.R.reason -match 'Ethernet Mgmt') -and
     ($dDisF2.R.outcome -eq 'NEEDS_OPERATOR') -and (@($dDisF2.Log | Where-Object { $_ -match '^netcfg' }).Count -eq 0) -and ($dDisF2.R.adapters.restored -eq $false) -and ($dDisF2.R.reason -match 'Ethernet I226')) `
    ("disable-fail={0} | plus enable-fail={1}" -f (Show-D $dDisF), (Show-D $dDisF2))

$dQHang = Invoke-Deploy @{ Bindings = @('Ethernet I226', 'Ethernet Mgmt'); Adapters = $twoUp; UninstallResult = @{ Completed = $false; ExitCode = $null; Service = 'RUNNING' } }
tc_check 'TEST-CIDEPLOY-074-uninstall-left-running-still-attempts-every-re-enable-and-stays-needs-operator' `
    (($dQHang.R.outcome -eq 'NEEDS_OPERATOR') -and ($dQHang.R.reason -match 'left running') -and (@($dQHang.Log | Where-Object { $_ -match '^copy|^netcfg\.exe -l' }).Count -eq 0) -and
     ((@($dQHang.Log | Where-Object { $_ -match '^enable ' }) -join ',') -eq 'enable Ethernet I226,enable Ethernet Mgmt') -and ((Get-LogIndex $dQHang '^enable ') -gt (Get-LogIndex $dQHang '^netcfg\.exe -u'))) (Show-D $dQHang)

$dQInst = Invoke-Deploy @{ Bindings = @('Ethernet I226'); InstallResult = @{ Completed = $true; ExitCode = 1; Service = 'NOT_FOUND' } }
tc_check 'TEST-CIDEPLOY-075-failed-installation-re-enables-adapters-and-records-the-post-restore-state' `
    (($dQInst.R.outcome -eq 'FAILED') -and ($dQInst.Log -contains 'enable Ethernet I226') -and ($dQInst.R.adapters.restored -eq $true) -and ($null -ne $dQInst.R.postState) -and
     ((Get-LogIndex $dQInst '^enable ') -gt (Get-LogIndex $dQInst '^netcfg\.exe -l'))) (Show-D $dQInst)

$dNoRest = Invoke-Deploy @{ Bindings = @('Ethernet I226', 'Ethernet Mgmt'); Adapters = $twoUp; EnableNoEffect = @('Ethernet Mgmt') }
tc_check 'TEST-CIDEPLOY-076-unconfirmed-restoration-is-never-deployed' `
    (($dNoRest.R.outcome -eq 'NEEDS_OPERATOR') -and ($dNoRest.R.exitCode -eq 3) -and ($dNoRest.R.adapters.restored -eq $false) -and ($dNoRest.R.reason -match 'Ethernet Mgmt') -and
     (@($dNoRest.R.steps | Where-Object { $_.step -eq 'adapter-restore' -and -not $_.ok }).Count -eq 1)) (Show-D $dNoRest)

$dSel = Invoke-Deploy @{ SelectedImageSha = ('9' * 64) }
tc_check 'TEST-CIDEPLOY-078-a-service-image-other-than-the-staged-package-is-never-deployed' `
    (($dSel.R.outcome -eq 'FAILED') -and ($dSel.R.reason -match 'ImagePath') -and ($dSel.R.reason -match ('9' * 64)) -and ($dSel.R.postState.serviceImageSha256 -eq ('9' * 64)) -and
     (@($dSel.R.steps | Where-Object { $_.step -eq 'service-image' -and -not $_.ok }).Count -eq 1) -and ($dQ.R.postState.serviceImageSha256 -eq $pOk.Sha)) (Show-D $dSel)
$dImgU = Invoke-Deploy @{ ImageUnobservable = $true }
tc_check 'TEST-CIDEPLOY-079-an-unobservable-service-image-is-never-deployed' `
    (($dImgU.R.outcome -eq 'FAILED') -and ($dImgU.R.reason -match '(?i)service image') -and (@($dImgU.R.steps | Where-Object { $_.step -eq 'service-image' -and -not $_.ok }).Count -eq 1)) (Show-D $dImgU)

tc_check 'TEST-CIDEPLOY-077-management-adapter-is-quiesced-and-the-risk-names-it' `
    ((@($dQ.R.adapters.targets | Where-Object { $_.name -eq 'Ethernet Mgmt' }).Count -eq 1) -and ($dQ.R.managementRisk -match "management adapter 'Ethernet Mgmt'") -and ($dQ.R.managementRisk -match '(?i)disabled')) $dQ.R.managementRisk

# Process-termination policy for the unattended deployment library (AST, not text).  Violations:
#   - Stop-Process / kill / spps, taskkill, tskill, wmic, and Invoke-CimMethod/Invoke-WmiMethod with Terminate;
#   - any .Kill() / .Terminate() member call, EXCEPT inside Invoke-AvbBoundedPowerShell on a variable that this function
#     assigns exactly once from [System.Diagnostics.Process]::Start(...) (an initial $null is allowed) before the call;
#   - a member call whose name is not a constant (cannot be verified).
# Returns violation strings (empty = compliant).
function Test-AvbTerminationPolicy {
    param([string]$Source)
    $tok = $null; $perr = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseInput($Source, [ref]$tok, [ref]$perr)
    if (@($perr).Count -gt 0) { return "unparseable source: $(@($perr)[0].Message)" }
    $v = @()
    foreach ($c in $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.CommandAst] }, $true)) {
        $n = [string]$c.GetCommandName()
        if ($n -match '^(?i)(Stop-Process|kill|spps|taskkill(\.exe)?|tskill(\.exe)?|wmic(\.exe)?)$') { $v += "line $($c.Extent.StartLineNumber): process termination command '$n'" }
        elseif ($n -match '^(?i)Invoke-(Cim|Wmi)Method$' -and $c.Extent.Text -match '(?i)Terminate') { $v += "line $($c.Extent.StartLineNumber): $n Terminate" }
    }
    foreach ($m in $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.InvokeMemberExpressionAst] }, $true)) {
        if ($m.Member -isnot [System.Management.Automation.Language.StringConstantExpressionAst]) { $v += "line $($m.Extent.StartLineNumber): dynamic member call '$($m.Extent.Text)' cannot be verified"; continue }
        $name = [string]$m.Member.Value
        if ($name -notmatch '^(?i)(Kill|Terminate)$') { continue }
        $fn = $m.Parent; while ($null -ne $fn -and $fn -isnot [System.Management.Automation.Language.FunctionDefinitionAst]) { $fn = $fn.Parent }
        if ($null -eq $fn -or $fn.Name -ne 'Invoke-AvbBoundedPowerShell') { $v += "line $($m.Extent.StartLineNumber): .$name() outside Invoke-AvbBoundedPowerShell"; continue }
        if ($m.Static -or $m.Expression -isnot [System.Management.Automation.Language.VariableExpressionAst]) { $v += "line $($m.Extent.StartLineNumber): .$name() target is not the helper's process variable"; continue }
        $var = $m.Expression.VariablePath.UserPath
        $assigns = @($fn.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and
                                   $x.Left -is [System.Management.Automation.Language.VariableExpressionAst] -and $x.Left.VariablePath.UserPath -ieq $var }, $true))
        $isStart = {
            param($a)
            # An expression right-hand side is a CommandExpressionAst; a pipeline form is accepted only with one element.
            $ce = $a.Right
            if ($ce -is [System.Management.Automation.Language.PipelineAst]) { if ($ce.PipelineElements.Count -ne 1) { return $false }; $ce = $ce.PipelineElements[0] }
            if ($ce -isnot [System.Management.Automation.Language.CommandExpressionAst]) { return $false }
            $e = $ce.Expression
            return ($e -is [System.Management.Automation.Language.InvokeMemberExpressionAst] -and $e.Static -and
                    $e.Member -is [System.Management.Automation.Language.StringConstantExpressionAst] -and [string]$e.Member.Value -ieq 'Start' -and
                    $e.Expression -is [System.Management.Automation.Language.TypeExpressionAst] -and
                    $e.Expression.TypeName.FullName -match '^(?i)(System\.)?Diagnostics\.Process$')
        }
        $starts = @($assigns | Where-Object { & $isStart $_ })
        $nulls = @($assigns | Where-Object { $_.Right.Extent.Text -eq '$null' })
        $paramUse = @($fn.FindAll({ param($x) $x -is [System.Management.Automation.Language.ParameterAst] -and $x.Name.VariablePath.UserPath -ieq $var }, $true))
        if ($starts.Count -ne 1 -or ($assigns.Count - $starts.Count - $nulls.Count) -ne 0 -or $paramUse.Count -gt 0) {
            $v += "line $($m.Extent.StartLineNumber): .$name() target `$$var is not solely the process started by this helper (starts=$($starts.Count) other assignments=$($assigns.Count - $starts.Count - $nulls.Count) parameter=$($paramUse.Count))"
        } elseif ($starts[0].Extent.EndOffset -gt $m.Extent.StartOffset) {
            $v += "line $($m.Extent.StartLineNumber): .$name() precedes the helper's process start"
        }
    }
    return $v   # callers collect with @(...)
}

# Adapter-administration policy for the unattended deployment library (#510 design A, AST/tokens, not text).  The only
# permitted Disable-NetAdapter / Enable-NetAdapter text is inside the ONE single-quoted here-string assigned to
# $AvbHilAdapterAdminScript, and that variable is read only inside Invoke-AvbAdapterAdminChange (the bounded helper the
# deployment calls for the recorded adapters).  Violations:
#   - a non-comment token containing Disable-NetAdapter / Enable-NetAdapter outside that here-string;
#   - none or more than one assignment of $AvbHilAdapterAdminScript, or one whose value is not a single-quoted here-string;
#   - a read of $AvbHilAdapterAdminScript outside Invoke-AvbAdapterAdminChange.
# Returns @{ Violations; AllowedExtent } (AllowedExtent: the here-string assignment's text, excluded from the text scan).
function Test-AvbAdapterAdminPolicy {
    param([string]$Source)
    $tok = $null; $perr = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseInput($Source, [ref]$tok, [ref]$perr)
    if (@($perr).Count -gt 0) { return @{ Violations = @("unparseable source: $(@($perr)[0].Message)"); AllowedExtent = $null } }
    $v = @(); $allowed = $null
    $assigns = @($ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and
                                 $x.Left -is [System.Management.Automation.Language.VariableExpressionAst] -and $x.Left.VariablePath.UserPath -ieq 'AvbHilAdapterAdminScript' }, $true))
    if ($assigns.Count -ne 1) { $v += "expected exactly one assignment of `$AvbHilAdapterAdminScript, found $($assigns.Count)" }
    else {
        $rhs = $assigns[0].Right
        if ($rhs -is [System.Management.Automation.Language.CommandExpressionAst]) { $rhs = $rhs.Expression }
        if ($rhs -isnot [System.Management.Automation.Language.StringConstantExpressionAst] -or $rhs.StringConstantType -ne 'SingleQuotedHereString') {
            $v += "line $($assigns[0].Extent.StartLineNumber): `$AvbHilAdapterAdminScript is not a single-quoted here-string"
        } else { $allowed = $assigns[0].Extent }
    }
    foreach ($t in @($tok)) {
        if ($t.Kind -eq 'Comment') { continue }
        if ($t.Text -notmatch '(?i)(Disable|Enable)-NetAdapter') { continue }
        if ($null -ne $allowed -and $t.Extent.StartOffset -ge $allowed.StartOffset -and $t.Extent.EndOffset -le $allowed.EndOffset) { continue }
        $v += "line $($t.Extent.StartLineNumber): adapter administration outside `$AvbHilAdapterAdminScript"
    }
    foreach ($r in $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.VariableExpressionAst] -and $x.VariablePath.UserPath -ieq 'AvbHilAdapterAdminScript' }, $true)) {
        if ($assigns.Count -ge 1 -and $r.Parent -eq $assigns[0]) { continue }
        $fn = $r.Parent; while ($null -ne $fn -and $fn -isnot [System.Management.Automation.Language.FunctionDefinitionAst]) { $fn = $fn.Parent }
        if ($null -eq $fn -or $fn.Name -ne 'Invoke-AvbAdapterAdminChange') { $v += "line $($r.Extent.StartLineNumber): `$AvbHilAdapterAdminScript read outside Invoke-AvbAdapterAdminChange" }
    }
    return @{ Violations = $v; AllowedExtent = $allowed }
}

$dd = [System.IO.File]::ReadAllText((Join-Path $RepoRoot 'tools\lib\DriverDeploy.ps1'))
$adminPol = if (Get-Command Test-AvbAdapterAdminPolicy -ErrorAction SilentlyContinue) { Test-AvbAdapterAdminPolicy -Source $dd } else { @{ Violations = @('Test-AvbAdapterAdminPolicy missing'); AllowedExtent = $null } }
# The text scan covers everything except the one permitted here-string (whose content Test-AvbAdapterAdminPolicy pins).
$ddScan = if ($null -ne $adminPol.AllowedExtent) { $dd.Remove($adminPol.AllowedExtent.StartOffset, $adminPol.AllowedExtent.EndOffset - $adminPol.AllowedExtent.StartOffset) } else { $dd }
$code = ($ddScan -split "`n" | Where-Object { $_ -notmatch '^\s*#' }) -join "`n"
# Process termination is checked STRUCTURALLY (Test-AvbTerminationPolicy below): netcfg, installers and unrelated
# processes are never terminated; the only permitted termination is the read-only query helper that
# Invoke-AvbBoundedPowerShell itself started (PR #494 review r4641317470, Q 2026-11-20).
$forbiddenText = [regex]::Matches($code, '(?i)Read-Host|Restart-Computer|shutdown|Disable-NetAdapter|Enable-NetAdapter|DriverStore|Restart-Service|Stop-Service|PendingFileRenameOperations"\s*-Value|Set-ItemProperty') | ForEach-Object { $_.Value }
$termViol = @(if (Get-Command Test-AvbTerminationPolicy -ErrorAction SilentlyContinue) { Test-AvbTerminationPolicy -Source $dd } else { 'Test-AvbTerminationPolicy missing' })
tc_check 'TEST-CIDEPLOY-016-unattended-path-never-prompts-reboots-deletes-driverstore-or-restarts-eventlog-and-administers-adapters-only-in-the-510-helper' `
    (@($forbiddenText).Count -eq 0 -and $termViol.Count -eq 0 -and @($adminPol.Violations).Count -eq 0) `
    ("forbidden operation found in tools/lib/DriverDeploy.ps1: text=[{0}] termination=[{1}] adapter-admin=[{2}]" -f (@($forbiddenText) -join ', '), ($termViol -join '; '), (@($adminPol.Violations) -join '; '))

# Negative and positive controls for the adapter-administration policy itself (#510).
$adminOk = "`$AvbHilAdapterAdminScript = @'`nparam(`$n, `$e) if (`$e) { Enable-NetAdapter -Name `$n -Confirm:`$false } else { Disable-NetAdapter -Name `$n -Confirm:`$false }`n'@`n" +
           "function Invoke-AvbAdapterAdminChange { `$s = `$AvbHilAdapterAdminScript; `$s }"
$adminCases = [ordered]@{
    'cmdlet called directly in a function'      = "$adminOk`nfunction Invoke-Other { Disable-NetAdapter -Name x -Confirm:`$false }"
    'cmdlet hidden in another string'           = "$adminOk`n`$other = 'Enable-NetAdapter -Name x'"
    'second assignment of the script variable'  = "$adminOk`n`$AvbHilAdapterAdminScript = 'Disable-NetAdapter'"
    'script variable read outside the helper'   = "$adminOk`nfunction Invoke-Other { & ([scriptblock]::Create(`$AvbHilAdapterAdminScript)) }"
    'script variable is a double-quoted string' = "`$AvbHilAdapterAdminScript = `"Disable-NetAdapter`"`nfunction Invoke-AvbAdapterAdminChange { `$AvbHilAdapterAdminScript }"
}
$adminBad = @()
$okRes = Test-AvbAdapterAdminPolicy -Source $adminOk
if (@($okRes.Violations).Count -ne 0) { $adminBad += "compliant source rejected: $(@($okRes.Violations) -join '; ')" }
foreach ($k in $adminCases.Keys) { if (@((Test-AvbAdapterAdminPolicy -Source $adminCases[$k]).Violations).Count -eq 0) { $adminBad += "not rejected: $k" } }
tc_check 'TEST-CIDEPLOY-016b-adapter-administration-policy-accepts-only-the-single-helper-script' ($adminBad.Count -eq 0) ($adminBad -join '; ')

# Negative and positive controls for the termination policy itself: it must reject termination outside the helper,
# of a different process, of a reassigned variable, through an expression, by name, or dynamically - and accept the
# one permitted shape.
$helperOk = @'
function Invoke-AvbBoundedPowerShell {
    $p = $null
    $p = [System.Diagnostics.Process]::Start($psi)
    if (-not $p.WaitForExit(1000)) { $p.Kill() }
}
'@
$termCases = [ordered]@{
    'kill outside the helper function'      = @('function Invoke-Other { $p = [System.Diagnostics.Process]::Start($psi); $p.Kill() }', $true)
    'kill at script level'                  = @('$p = [System.Diagnostics.Process]::Start($psi); $p.Kill()', $true)
    'kill of a different process'           = @('function Invoke-AvbBoundedPowerShell { $p = [System.Diagnostics.Process]::Start($psi); $q = Get-Process -Name netcfg; $q.Kill() }', $true)
    'target reassigned to another process'  = @('function Invoke-AvbBoundedPowerShell { $p = [System.Diagnostics.Process]::Start($psi); $p = Get-Process -Name netcfg; $p.Kill() }', $true)
    'target is a parameter'                 = @('function Invoke-AvbBoundedPowerShell { param($p) $p.Kill() }', $true)
    'kill before the start'                 = @('function Invoke-AvbBoundedPowerShell { $p = $null; $p.Kill(); $p = [System.Diagnostics.Process]::Start($psi) }', $true)
    'kill through an expression'            = @('function Invoke-AvbBoundedPowerShell { [System.Diagnostics.Process]::GetProcessById(4242).Kill() }', $true)
    'Stop-Process netcfg'                   = @('Stop-Process -Name netcfg -Force', $true)
    'kill alias'                            = @('kill -Id 4242', $true)
    'taskkill'                              = @('taskkill.exe /F /IM netcfg.exe', $true)
    'CIM Terminate'                         = @('Get-CimInstance Win32_Process -Filter "Name=''netcfg.exe''" | Invoke-CimMethod -MethodName Terminate', $true)
    'WMI Terminate member'                  = @('function Invoke-AvbBoundedPowerShell { $w = Get-WmiObject Win32_Process; $w.Terminate() }', $true)
    'dynamic member call'                   = @('function Invoke-AvbBoundedPowerShell { $p = [System.Diagnostics.Process]::Start($psi); $m = ''Kill''; $p.$m() }', $true)
    'permitted helper shape'                = @($helperOk, $false)
    'no termination at all'                 = @('function Get-X { Get-Process -Name netcfg }', $false)
}
$termBad = @()
if (Get-Command Test-AvbTerminationPolicy -ErrorAction SilentlyContinue) {
    foreach ($k in $termCases.Keys) {
        $vv = @(Test-AvbTerminationPolicy -Source $termCases[$k][0])
        if (($vv.Count -gt 0) -ne $termCases[$k][1]) { $termBad += "$k -> violations=$($vv.Count) [$($vv -join ' | ')]" }
    }
} else { $termBad += 'Test-AvbTerminationPolicy missing' }
tc_check 'TEST-CIDEPLOY-016a-termination-policy-rejects-every-termination-except-the-helper-own-started-process' ($termBad.Count -eq 0) ($termBad -join '; ')

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
                     [string]$ProbeJson = '', [string]$Action = 'Deploy', [string]$EvdName = '') {
    if (Test-Path $calls) { Remove-Item $calls -Force }
    if (Test-Path $probeCalls) { Remove-Item $probeCalls -Force }
    # -Action Release reuses the Deploy's evidence directory (its hil-lock-owner.json), as the hil.yml always() step does.
    $evd = Join-Path $Tmp ("ev_{0}" -f $(if ($EvdName) { $EvdName } else { $Name }))
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
    # Deploy: <EvidenceDir>\recovery.json.  Recover: the attempt directory THIS run reports ("[HIL-RECOVER] evidence: <dir>").
    $attempt = $null; $lockRel = $null
    if ($Action -eq 'Recover' -and $out -match '\[HIL-RECOVER\] evidence: ([^\r\n]+)') { $attempt = $Matches[1].Trim() }
    $rj = if ($attempt) { Join-Path $attempt 'recovery.json' } else { Join-Path $evd 'recovery.json' }
    $rcv = $null; if (Test-Path $rj) { $rcv = Get-Content $rj -Raw | ConvertFrom-Json }
    if ($attempt -and (Test-Path (Join-Path $attempt 'lock-release.json'))) { $lockRel = Get-Content (Join-Path $attempt 'lock-release.json') -Raw | ConvertFrom-Json }
    $cl = @(); if (Test-Path $calls) { $cl = @(Get-Content $calls) }
    $pc = @(); if (Test-Path $probeCalls) { $pc = @(Get-Content $probeCalls) }
    return @{ Exit = $rc; Dep = $dep; Recovery = $rcv; LockRelease = $lockRel; Attempt = $attempt; Calls = $cl; ProbeCalls = $pc; Out = $out; Evd = $evd; Lock = $LockFile }
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
        bindingsStatus = 'MEASURED'; bindingsHelper = [ordered]@{ pid = 4711; disposition = 'exited'; timeoutSeconds = 60; elapsedMs = 900; exitCode = 0 }
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
function Get-P($o, [string]$n) {
    if ($null -eq $o) { return $null }
    if ($o -is [System.Collections.IDictionary]) { if ($o.Contains($n)) { return $o[$n] } else { return $null } }   # in-process results
    if ($o.PSObject.Properties[$n]) { return $o.PSObject.Properties[$n].Value }                                       # deserialized JSON
    return $null
}
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
$recArch0 = if ($recArch.Count -ge 1) { $recArch[0].FullName } else { $null }
$recArchSha = if ($recArch0) { (Get-FileHash -LiteralPath $recArch0 -Algorithm SHA256).Hash } else { $null }
$recInc = Get-P $recDoc 'incident'
tc_preds 'TEST-CIDEPLOY-049-verified-recovery-preserves-the-incident-and-reaches-normal-package-verification' ([ordered]@{
    'deployment.json present'                  = @(($null -ne $oRec.Dep), ($null -ne $oRec.Dep))
    'exit == 1'                                = @(($oRec.Exit -eq 1), $oRec.Exit)
    'verdict == REJECTED_PACKAGE'              = @(((Get-P $oRec.Dep 'verdict') -eq 'REJECTED_PACKAGE'), (Get-P $oRec.Dep 'verdict'))
    'installer not called'                     = @(($oRec.Calls.Count -eq 0), ($oRec.Calls -join '|'))
    'probe called once with watch=16216'       = @(($oRec.ProbeCalls.Count -eq 1 -and $oRec.ProbeCalls[0] -eq 'probe watch=16216'), ($oRec.ProbeCalls -join '|'))
    'previousDeployment.verdict NEEDS_OPERATOR' = @(((Get-P (Get-P $oRec.Dep 'previousDeployment') 'verdict') -eq 'NEEDS_OPERATOR'), (Get-P (Get-P $oRec.Dep 'previousDeployment') 'verdict'))
    'deployment.recovery READY/automatic'      = @(((Get-P $depRecovery 'decision') -eq 'READY' -and (Get-P $depRecovery 'mode') -eq 'automatic'), ("{0}/{1}" -f (Get-P $depRecovery 'decision'), (Get-P $depRecovery 'mode')))
    'exactly one archive'                      = @(($recArch.Count -eq 1), $recArch.Count)
    'archive SHA256 == incident'               = @(($recArchSha -eq $incSha), $recArchSha)
    'exactly one recovery record'              = @(($recRecs.Count -eq 1), $recRecs.Count)
    'record schema avb-hil-recovery/1'         = @(((Get-P $recDoc 'schema') -eq 'avb-hil-recovery/1'), (Get-P $recDoc 'schema'))
    'record decision READY'                    = @(((Get-P $recDoc 'decision') -eq 'READY'), (Get-P $recDoc 'decision'))
    'record incident.sha256 == incident'       = @(((Get-P $recInc 'sha256') -eq $incSha), (Get-P $recInc 'sha256'))
    'record incident verdict/runId'            = @(((Get-P $recInc 'verdict') -eq 'NEEDS_OPERATOR' -and (Get-P $recInc 'runId') -eq '44052954003'), ("{0}/{1}" -f (Get-P $recInc 'verdict'), (Get-P $recInc 'runId')))
    # Path IDENTITY, not string equality: on GitHub runners TEMP is an 8.3 short path (C:\Users\RUNNER~1\...), so the
    # record holds the short form while Get-ChildItem lists the long form (CI run 44718917485, reproduced offline).
    'record incident.archivePath is the archive' = @(([string](Get-P $recInc 'archivePath') -ne '' -and (Test-Path -LiteralPath ([string](Get-P $recInc 'archivePath'))) -and $recArch0 -and
                                                    (Split-Path ([string](Get-P $recInc 'archivePath')) -Leaf) -eq (Split-Path $recArch0 -Leaf) -and
                                                    (Get-FileHash -LiteralPath ([string](Get-P $recInc 'archivePath'))).Hash -eq $recArchSha), ("record='{0}' listed='{1}'" -f (Get-P $recInc 'archivePath'), $recArch0))
    'evidence recovery.json READY'             = @(((Get-P $oRec.Recovery 'decision') -eq 'READY'), (Get-P $oRec.Recovery 'decision'))
})

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
        'binding query timed out'                 = @(@{ bindingsStatus = 'TIMEOUT'; bindings = @(); bindingsError = 'MS_IntelAvbFilter binding query exceeded 60s' }, 'NOT_READY', 'binding.*TIMEOUT')
        'binding query not attempted'             = @(@{ bindingsStatus = 'NOT_ATTEMPTED'; bindings = @(); bindingsError = 'not attempted: conflict' }, 'NOT_READY', 'binding.*NOT_ATTEMPTED')
        'binding status missing'                  = @(@{ bindingsStatus = $null }, 'NOT_READY', 'binding')
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
# Real lifecycle: every Deploy keeps the lock until hil.yml's always() step runs -Action Release with that job's owner
# evidence.  Under GitHub Actions the recorded Runner.Worker is alive for the whole test, so an unreleased CI lock is
# (correctly) live and the next Deploy is NOT_EXECUTED_BUSY (CI run 44718917485, reproduced offline).
$d1 = Invoke-Orch 'rep_dep1' $pOk -LockFile $repLock -ProbeJson (New-Obs)
$rel1 = Invoke-Orch 'rep_rel1' $null -LockFile $repLock -Action Release -EvdName 'rep_dep1'
$lockAfterRel1 = Test-Path -LiteralPath $repLock
$arch1 = Get-Archives $repLock
$incident2 = ([ordered]@{ schema = 'avb-hil-deployment/1'; configuration = 'Debug'; verdict = 'IN_FLIGHT'; reason = 'installation started; second incident'; machineOk = $false } | ConvertTo-Json)
Start-Sleep -Milliseconds 50
[IO.File]::WriteAllText("$repLock.inflight.json", $incident2)
$incSha2 = (Get-FileHash -LiteralPath "$repLock.inflight.json").Hash
$d2 = Invoke-Orch 'rep_dep2' $pOk -LockFile $repLock -ProbeJson (New-Obs)
$rel2 = Invoke-Orch 'rep_rel2' $null -LockFile $repLock -Action Release -EvdName 'rep_dep2'
$lockAfterRel2 = Test-Path -LiteralPath $repLock
$arch2 = Get-Archives $repLock; $arch2 = @($arch2 | Sort-Object Name)
$archShas = @($arch2 | ForEach-Object { (Get-FileHash -LiteralPath $_.FullName).Hash })
$arch1Sha = if ($arch1.Count -ge 1) { (Get-FileHash -LiteralPath $arch1[0].FullName).Hash } else { $null }
tc_preds 'TEST-CIDEPLOY-050-repeated-recovery-keeps-every-incident-and-never-rewrites-an-archive' ([ordered]@{
    'recover exits 0,0'                     = @(($r1.Exit -eq 0 -and $r2.Exit -eq 0), "$($r1.Exit),$($r2.Exit)")
    'recover READY/operator'                = @(((Get-P $r1.Recovery 'decision') -eq 'READY' -and (Get-P $r1.Recovery 'mode') -eq 'operator'), ("{0}/{1}" -f (Get-P $r1.Recovery 'decision'), (Get-P $r1.Recovery 'mode')))
    'incident untouched by Recover'         = @(($afterOps -ceq $incidentBody), ($afterOps -ceq $incidentBody))
    'two operator records, no archive'      = @(($opRecs -eq 2 -and $opArch -eq 0), "records=$opRecs archives=$opArch")
    'lock released after Recover'           = @((-not $lockAfterOps), $lockAfterOps)
    'deploy 1 REJECTED_PACKAGE'             = @(((Get-P $d1.Dep 'verdict') -eq 'REJECTED_PACKAGE'), ("{0}: {1}" -f (Get-P $d1.Dep 'verdict'), (Get-P $d1.Dep 'reason')))
    'release 1 with deploy 1 owner'         = @(($rel1.Exit -eq 0 -and -not $lockAfterRel1), "exit=$($rel1.Exit) lockLeft=$lockAfterRel1 out=$(($rel1.Out -replace '\s+', ' ').Trim())")
    'one archive after deploy 1'            = @(($arch1.Count -eq 1), $arch1.Count)
    'deploy 2 REJECTED_PACKAGE'             = @(((Get-P $d2.Dep 'verdict') -eq 'REJECTED_PACKAGE'), ("{0}: {1}" -f (Get-P $d2.Dep 'verdict'), (Get-P $d2.Dep 'reason')))
    'release 2 with deploy 2 owner'         = @(($rel2.Exit -eq 0 -and -not $lockAfterRel2), "exit=$($rel2.Exit) lockLeft=$lockAfterRel2 out=$(($rel2.Out -replace '\s+', ' ').Trim())")
    'two archives after deploy 2'           = @(($arch2.Count -eq 2), $arch2.Count)
    'first archive unchanged'               = @(($arch1Sha -eq $incSha1), $arch1Sha)
    'archives hold incident 1 and 2'        = @((($archShas -contains $incSha1) -and ($archShas -contains $incSha2)), ($archShas -join ','))
    'four recovery records'                 = @(((Get-RecoveryRecords $repLock).Count -eq 4), (Get-RecoveryRecords $repLock).Count)
})

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
    $keys = 'observedUtc', 'processes', 'processError', 'watchedPids', 'service', 'imagePath', 'imageFile', 'imageSha256', 'imageError', 'system32Sha256', 'pendingRenames', 'pendingError', 'bindings', 'bindingsError', 'bindingsStatus', 'bindingsHelper'
    $missingKeys = @($keys | Where-Object { -not $real.Contains($_) })
    $selfListed = @(@($real.processes) | Where-Object { $_ -and [int]$_.pid -eq $PID }).Count
    $watchedSelf = @(@($real.watchedPids) | Where-Object { $_ -and [int]$_.pid -eq $PID -and $_.alive }).Count
    # The real binding query itself (the probe above may legitimately skip it when this host shows a conflict).
    $realBind = if (Get-Command Get-AvbHilBindingObservation -ErrorAction SilentlyContinue) { Get-AvbHilBindingObservation } else { $null }
    tc_check 'TEST-CIDEPLOY-057-LOCALSMOKE-real-observation-probe-returns-every-field-and-excludes-itself' (
        $missingKeys.Count -eq 0 -and $selfListed -eq 0 -and $watchedSelf -eq 1 -and $null -ne $realBind -and $realBind.status -eq 'MEASURED' -and
        $realBind.helper.disposition -eq 'exited'
    ) ("missing={0} selfListed={1} watchedSelf={2} realBinding={3} helper={4} error={5}" -f ($missingKeys -join ','), $selfListed, $watchedSelf,
        $(if ($realBind) { $realBind.status }), $(if ($realBind) { $realBind.helper.disposition }), $(if ($realBind) { $realBind.error }))
} else {
    tc_fail 'TEST-CIDEPLOY-057-LOCALSMOKE-real-observation-probe-returns-every-field-and-excludes-itself' 'Get-AvbHilRecoveryObservation missing in tools/lib/DriverDeploy.ps1'
}

# STATIC: reconciliation runs under the deployment's own lock, before crash evidence and package verification (no
# unlocked gap), and recovery can never produce DEPLOYED (only the verified installation path does).
$orchSrc = [IO.File]::ReadAllText($orch)
$iL = $orchSrc.IndexOf('Enter-AvbHilLock -Kind ''ci'''); $iR = $orchSrc.IndexOf('Invoke-AvbHilReconciliation -Mode ''automatic'''); $iC = $orchSrc.IndexOf('Get-AvbCrashEvidence -Source'); $iP = $orchSrc.IndexOf('Test-AvbDriverPackage -PackageDir')
$ddAst = [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $RepoRoot 'tools\lib\DriverDeploy.ps1'), [ref]$null, [ref]$null)
$recFns = @($ddAst.FindAll({ param($x) $x -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $x.Name -match '^(Invoke-AvbHilReconciliation|Get-AvbHilReadiness|Get-AvbHilRecoveryObservation|Get-AvbHilIncident|Get-AvbHilBindingObservation|Invoke-AvbBoundedPowerShell)$' }, $true))
$recDeployed = @($recFns | Where-Object { $_.Extent.Text -match 'DEPLOYED' })
tc_check 'TEST-CIDEPLOY-058-STATIC-reconciliation-is-locked-precedes-package-checks-and-never-yields-deployed' (
    $iL -gt 0 -and $iR -gt $iL -and $iC -gt $iR -and $iP -gt $iC -and $recFns.Count -eq 6 -and $recDeployed.Count -eq 0 -and
    ([regex]::Matches($orchSrc, "Complete-Deployment 'DEPLOYED'")).Count -eq 1
) ("lock@{0} reconcile@{1} crash@{2} pkg@{3} recoveryFunctions={4} mentionDEPLOYED={5}" -f $iL, $iR, $iC, $iP, $recFns.Count, (($recDeployed | ForEach-Object Name) -join ','))

# ---------------------------------------------------------------------------------------------------------
# PR #494 review r4641317470: the binding query (Get-NetAdapterBinding -> INetCfg) can block while a conflicting
# netcfg holds the network configuration.  It is never attempted after a conflict or a failed process enumeration,
# and an attempted query runs in a helper process with an enforced time limit (including output collection).  Only
# that helper is terminated on timeout; installer / netcfg processes are never touched.
# ---------------------------------------------------------------------------------------------------------
$haveBound = [bool]((Get-Command Get-AvbHilBindingObservation -ErrorAction SilentlyContinue) -and (Get-Command Invoke-AvbBoundedPowerShell -ErrorAction SilentlyContinue) -and
                    (Get-Command Get-AvbHilRecoveryObservation -ErrorAction SilentlyContinue) -and (Get-Command Get-AvbHilRecoveryObservation).Parameters.ContainsKey('ProcessQuery') -and
                    (Get-Command Get-AvbHilRecoveryObservation).Parameters.ContainsKey('BindingObservation'))
$script:bindCalls = 0
$countingBinding = { $script:bindCalls++; [ordered]@{ status = 'MEASURED'; bindings = @(); error = $null; helper = $null } }
$fakeNetcfg = { @([pscustomobject]@{ ProcessId = 16216; ParentProcessId = 4; Name = 'netcfg.exe'; CommandLine = 'netcfg.exe -u MS_IntelAvbFilter'; CreationDate = [datetime]'2026-11-17T11:31:40' }) }
if ($haveBound) {
    $script:bindCalls = 0
    $oc = Get-AvbHilRecoveryObservation -WatchPids @(16216) -ProcessQuery $fakeNetcfg -BindingObservation $countingBinding
    $rc059 = Get-AvbHilReadiness -Observation $oc
    tc_check 'TEST-CIDEPLOY-059-detected-conflict-prevents-the-binding-query' (
        $script:bindCalls -eq 0 -and $oc.bindingsStatus -eq 'NOT_ATTEMPTED' -and [string]$oc.bindingsError -match '16216' -and @($oc.processes).Count -eq 1 -and
        $rc059.Decision -eq 'NOT_READY' -and ((@($rc059.Reasons) -join ' | ') -match 'netcfg\.exe PID 16216') -and ((@($rc059.Reasons) -join ' | ') -match 'binding.*NOT_ATTEMPTED')
    ) ("calls={0} status={1} error={2} decision={3} reasons={4}" -f $script:bindCalls, $oc.bindingsStatus, $oc.bindingsError, $rc059.Decision, (@($rc059.Reasons) -join ' | '))

    $script:bindCalls = 0
    $of = Get-AvbHilRecoveryObservation -ProcessQuery { throw 'Win32_Process query denied (fault injection)' } -BindingObservation $countingBinding
    $rc060 = Get-AvbHilReadiness -Observation $of
    tc_check 'TEST-CIDEPLOY-060-failed-process-enumeration-prevents-the-binding-query' (
        $script:bindCalls -eq 0 -and $of.bindingsStatus -eq 'NOT_ATTEMPTED' -and [string]$of.processError -match 'fault injection' -and
        [string]$of.bindingsError -match 'process' -and $rc060.Decision -eq 'NOT_READY'
    ) ("calls={0} status={1} processError={2} bindingsError={3} decision={4}" -f $script:bindCalls, $of.bindingsStatus, $of.processError, $of.bindingsError, $rc060.Decision)

    # Controlled offline stall: the helper sleeps far beyond the limit.  Declared bound = timeout + cleanup.
    $tT = 2; $tC = 5
    # An unrelated sentinel process (started by this test) must survive the helper's termination.
    $sentinel = Start-Process -FilePath (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe') -ArgumentList '-NoProfile', '-NonInteractive', '-Command', 'Start-Sleep -Seconds 120' -WindowStyle Hidden -PassThru
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $stall = Get-AvbHilBindingObservation -TimeoutSeconds $tT -CleanupSeconds $tC -Script 'Start-Sleep -Seconds 120'
    $sw.Stop()
    $stallPid = if ($stall.helper) { [int]$stall.helper.pid } else { 0 }
    $stallAlive = ($stallPid -gt 0) -and [bool](Get-Process -Id $stallPid -ErrorAction SilentlyContinue)
    $sentinel.Refresh(); $sentinelAlive = -not $sentinel.HasExited
    tc_preds 'TEST-CIDEPLOY-061-stalled-binding-query-returns-within-the-declared-bound-and-terminates-only-its-helper' ([ordered]@{
        'status TIMEOUT'                         = @(($stall.status -eq 'TIMEOUT'), $stall.status)
        'returned within timeout + cleanup'      = @(($sw.ElapsedMilliseconds -lt (($tT + $tC) * 1000)), "$($sw.ElapsedMilliseconds) ms (bound $(($tT + $tC) * 1000) ms)")
        'helper termination confirmed'           = @(((Get-P $stall.helper 'disposition') -eq 'terminated' -and -not $stallAlive), "disposition='$(Get-P $stall.helper 'disposition')' pid=$stallPid alive=$stallAlive")
        'declared limit recorded'                = @(([int](Get-P $stall.helper 'timeoutSeconds') -eq $tT -and [string]$stall.error -match "exceeded ${tT}s"), "timeoutSeconds=$(Get-P $stall.helper 'timeoutSeconds') error=$($stall.error)")
        'unrelated sentinel still alive'         = @($sentinelAlive, "sentinel PID $($sentinel.Id) alive=$sentinelAlive")
    })
    if (-not $sentinel.HasExited) { Stop-Process -Id $sentinel.Id -Force -ErrorAction SilentlyContinue }   # the test's own sentinel

    # The same stall inside the real reconciliation: evidence persisted, NOT_READY, incident untouched, no archive.
    $toLock = Join-Path $Tmp 'bindtimeout.lock'; $toEv = Join-Path $Tmp 'ev_bindtimeout'; New-Item -ItemType Directory -Path $toEv -Force | Out-Null
    [IO.File]::WriteAllText("$toLock.inflight.json", $incidentBody)
    $obsStall = { param([int[]]$WatchPids) Get-AvbHilRecoveryObservation -WatchPids $WatchPids -ProcessQuery { @() } -BindingObservation { Get-AvbHilBindingObservation -TimeoutSeconds 2 -CleanupSeconds 5 -Script 'Start-Sleep -Seconds 120' } }
    $sw2 = [System.Diagnostics.Stopwatch]::StartNew()
    $rcT = Invoke-AvbHilReconciliation -LockPath $toLock -Mode 'automatic' -LockOwner ([ordered]@{ kind = 'ci'; pid = $PID }) -PreviousLockOwner $null -Observe $obsStall -EvidenceDir $toEv -ArchiveIncident
    $sw2.Stop()
    $toRecs = Get-RecoveryRecords $toLock
    $toDoc = if ($toRecs.Count -eq 1) { Get-Content $toRecs[0].FullName -Raw | ConvertFrom-Json } else { $null }
    tc_check 'TEST-CIDEPLOY-061a-binding-timeout-persists-evidence-returns-not-ready-and-preserves-the-incident' (
        $rcT.decision -eq 'NOT_READY' -and $sw2.ElapsedMilliseconds -lt 30000 -and ((@($rcT.reasons) -join ' ') -match 'binding.*TIMEOUT') -and
        $null -ne $toDoc -and $toDoc.observations.bindingsStatus -eq 'TIMEOUT' -and $toDoc.observations.bindingsHelper.disposition -eq 'terminated' -and
        [IO.File]::ReadAllText("$toLock.inflight.json") -ceq $incidentBody -and (Get-Archives $toLock).Count -eq 0 -and (Test-Path -LiteralPath (Join-Path $toEv 'recovery.json'))
    ) ("decision={0} elapsedMs={1} records={2} status={3} helper={4} archives={5} reasons={6}" -f $rcT.decision, $sw2.ElapsedMilliseconds, $toRecs.Count,
        $(if ($toDoc) { $toDoc.observations.bindingsStatus }), $(if ($toDoc) { $toDoc.observations.bindingsHelper.disposition }), (Get-Archives $toLock).Count, (@($rcT.reasons) -join ' | '))

    # A completing query keeps the normal readiness behaviour (controlled helper output, real parser and decision).
    $okB = Get-AvbHilBindingObservation -TimeoutSeconds 30 -CleanupSeconds 5 -Script "'AVBBIND:[{""name"":""Ethernet"",""enabled"":true},{""name"":""Ethernet 2"",""enabled"":true}]'"
    $okDoc = Get-Content -LiteralPath (New-Obs @{ bindings = @($okB.bindings); bindingsStatus = $okB.status; bindingsError = $okB.error; bindingsHelper = $okB.helper }) -Raw | ConvertFrom-Json
    $rc062 = Get-AvbHilReadiness -Observation $okDoc
    $script:bindCalls = 0
    $ocClean = Get-AvbHilRecoveryObservation -ProcessQuery { @() } -BindingObservation $countingBinding
    tc_check 'TEST-CIDEPLOY-062-successful-binding-query-keeps-normal-readiness' (
        $okB.status -eq 'MEASURED' -and @($okB.bindings).Count -eq 2 -and $okB.helper.disposition -eq 'exited' -and [int]$okB.helper.exitCode -eq 0 -and
        $rc062.Decision -eq 'READY' -and $script:bindCalls -eq 1 -and $ocClean.bindingsStatus -eq 'MEASURED'
    ) ("status={0} bindings={1} helper={2} exit={3} decision={4} [{5}] cleanProbeCalls={6} cleanStatus={7}" -f $okB.status, @($okB.bindings).Count, $(if ($okB.helper) { $okB.helper.disposition }),
        $(if ($okB.helper) { $okB.helper.exitCode }), $rc062.Decision, (@($rc062.Reasons) -join ' | '), $script:bindCalls, $ocClean.bindingsStatus)
} else {
    foreach ($n in 'TEST-CIDEPLOY-059-detected-conflict-prevents-the-binding-query', 'TEST-CIDEPLOY-060-failed-process-enumeration-prevents-the-binding-query',
                   'TEST-CIDEPLOY-061-stalled-binding-query-returns-within-the-declared-bound-and-terminates-only-its-helper',
                   'TEST-CIDEPLOY-061a-binding-timeout-persists-evidence-returns-not-ready-and-preserves-the-incident', 'TEST-CIDEPLOY-062-successful-binding-query-keeps-normal-readiness') {
        tc_fail $n 'bounded binding observation missing (Get-AvbHilBindingObservation / Invoke-AvbBoundedPowerShell / -ProcessQuery / -BindingObservation)'
    }
}

# Helper and lock disposition reach the records: Recover (lock taken and released, release recorded in the evidence
# copy) and Deploy (lock held for the deployment, released only by -Action Release) after a binding timeout.
$dispFail = @()
$tmoObs = @{ bindingsStatus = 'TIMEOUT'; bindings = @(); bindingsError = 'MS_IntelAvbFilter binding query exceeded 60s (helper PID 5555 terminated)'
             bindingsHelper = [ordered]@{ pid = 5555; disposition = 'terminated'; timeoutSeconds = 60; elapsedMs = 60012; exitCode = $null } }
$dLock = Join-Path $Tmp 'disp.lock'
[IO.File]::WriteAllText("$dLock.inflight.json", $incidentBody)
$oRt = Invoke-Orch 'disp_recover' $null -LockFile $dLock -ProbeJson (New-Obs $tmoObs) -Action Recover
$dRecs = Get-RecoveryRecords $dLock
$dDoc = if ($dRecs.Count -eq 1) { Get-Content $dRecs[0].FullName -Raw | ConvertFrom-Json } else { $null }
$rel = $oRt.LockRelease
if ($oRt.Exit -ne 3 -or $null -eq $dDoc -or $dDoc.decision -ne 'NOT_READY' -or $dDoc.observations.bindingsHelper.disposition -ne 'terminated' -or
    $null -eq $rel -or $rel.ok -ne $true -or (Test-Path -LiteralPath $dLock) -or [IO.File]::ReadAllText("$dLock.inflight.json") -cne $incidentBody) {
    $dispFail += "recover: exit=$($oRt.Exit) records=$($dRecs.Count) helper=$(if ($dDoc) { $dDoc.observations.bindingsHelper.disposition }) lockRelease=$(if ($rel) { $rel | ConvertTo-Json -Compress }) lockLeft=$(Test-Path -LiteralPath $dLock)"
}
$rOk = Invoke-Orch 'disp_recover_ok' $null -LockFile $dLock -ProbeJson (New-Obs) -Action Recover
$relOk = $rOk.LockRelease
if ($rOk.Exit -ne 0 -or $null -eq $relOk -or $relOk.ok -ne $true -or (Test-Path -LiteralPath $dLock)) { $dispFail += "recover-ready: exit=$($rOk.Exit) lockRelease=$(if ($relOk) { $relOk | ConvertTo-Json -Compress })" }
$oDt = Invoke-Orch 'disp_deploy' $pOk -LockFile $dLock -ProbeJson (New-Obs $tmoObs)
$depRec = Get-P $oDt.Dep 'recovery'
if ($oDt.Exit -ne 3 -or $null -eq $oDt.Dep -or $oDt.Dep.verdict -ne 'NEEDS_OPERATOR' -or $oDt.Dep.reason -notmatch 'binding.*TIMEOUT' -or $null -eq $depRec -or
    $depRec.observations.bindingsHelper.disposition -ne 'terminated' -or -not $oDt.Dep.lock.ok -or -not (Test-Path -LiteralPath (Join-Path $oDt.Evd 'hil-lock-owner.json')) -or
    [IO.File]::ReadAllText("$dLock.inflight.json") -cne $incidentBody -or (Get-Archives $dLock).Count -ne 0) {
    $dispFail += "deploy: exit=$($oDt.Exit) verdict=$(if ($oDt.Dep) { $oDt.Dep.verdict }) helper=$(if ($depRec) { $depRec.observations.bindingsHelper.disposition }) lockOk=$(if ($oDt.Dep) { $oDt.Dep.lock.ok }) ownerFile=$(Test-Path -LiteralPath (Join-Path $oDt.Evd 'hil-lock-owner.json'))"
}
# The Deploy kept the lock (held for installation and pre-flight); the always() step releases it with the job's owner.
$dRel = Invoke-Orch 'disp_release' $null -LockFile $dLock -Action Release -EvdName 'disp_deploy'
if ($dRel.Exit -ne 0 -or (Test-Path -LiteralPath $dLock) -or $dRel.Out -notmatch '\[LOCK\] released') { $dispFail += "deploy-release: exit=$($dRel.Exit) lockLeft=$(Test-Path -LiteralPath $dLock)" }
tc_check 'TEST-CIDEPLOY-063-helper-and-lock-disposition-are-recorded' ($dispFail.Count -eq 0) ($dispFail -join '; ')

# Repeated -Action Recover into the SAME requested evidence directory (2026-11-20: a later run overwrote the 11:43Z
# attempt's recovery.json).  Every attempt writes its own directory (recovery.json, lock-release.json, SHA256SUMS -
# each written once), reports where, and never replaces an earlier attempt's files or a pre-existing file.
$shLock = Join-Path $Tmp 'shared.lock'
[IO.File]::WriteAllText("$shLock.inflight.json", $incidentBody)
$shEvd = Join-Path $Tmp 'ev_shared'; New-Item -ItemType Directory -Path $shEvd -Force | Out-Null
$legacy = Join-Path $shEvd 'recovery.json'; [IO.File]::WriteAllText($legacy, '{"legacy":"evidence from an earlier layout"}'); $legacySha = (Get-FileHash -LiteralPath $legacy).Hash
function Get-AttemptHashes([string]$Dir) { $h = [ordered]@{}; if ($Dir -and (Test-Path -LiteralPath $Dir)) { foreach ($f in @(Get-ChildItem -LiteralPath $Dir -File | Sort-Object Name)) { $h[$f.Name] = (Get-FileHash -LiteralPath $f.FullName).Hash } }; return $h }
function Test-AttemptManifest([string]$Dir) {
    if (-not $Dir) { return 'no attempt directory reported' }
    $m = Join-Path $Dir 'SHA256SUMS'; if (-not (Test-Path -LiteralPath $m)) { return 'manifest missing' }
    $bad = @(); $listed = @()
    foreach ($l in @(Get-Content -LiteralPath $m)) { if ($l -match '^([0-9A-F]{64})\s+(\S+)') { $listed += $Matches[2]; $f = Join-Path $Dir $Matches[2]; if (-not (Test-Path -LiteralPath $f) -or (Get-FileHash -LiteralPath $f).Hash -ne $Matches[1]) { $bad += "$($Matches[2]) mismatch" } } }
    foreach ($f in @(Get-ChildItem -LiteralPath $Dir -File | Where-Object Name -ne 'SHA256SUMS')) { if ($listed -notcontains $f.Name) { $bad += "$($f.Name) not listed" } }
    return ($bad -join ', ')
}
$a1 = Invoke-Orch 'sh1' $null -LockFile $shLock -ProbeJson (New-Obs) -Action Recover -EvdName 'shared'
$a1Hashes = Get-AttemptHashes $a1.Attempt
$a2 = Invoke-Orch 'sh2' $null -LockFile $shLock -ProbeJson (New-Obs) -Action Recover -EvdName 'shared'
$holdSh = Enter-AvbHilLock -Kind 'local' -Label 'operator session' -Path $shLock -IsProcessAlive $alive -Sleep $noSleep
$a3 = Invoke-Orch 'sh3' $null -LockFile $shLock -ProbeJson (New-Obs) -Action Recover -EvdName 'shared'
$null = Exit-AvbHilLock -Owner $holdSh.Owner -Path $shLock
$a1After = Get-AttemptHashes $a1.Attempt
$attemptDirs = @(Get-ChildItem -LiteralPath $shEvd -Directory -Filter 'recover-*' -ErrorAction SilentlyContinue)
$rp1 = [string](Get-P $a1.Recovery 'recordPath'); $rp2 = [string](Get-P $a2.Recovery 'recordPath')
tc_preds 'TEST-CIDEPLOY-064-repeated-recover-keeps-every-attempt-and-reports-its-own-evidence' ([ordered]@{
    'exits 0 / 0 / 1 (READY, READY, busy)'      = @(($a1.Exit -eq 0 -and $a2.Exit -eq 0 -and $a3.Exit -eq 1), "$($a1.Exit)/$($a2.Exit)/$($a3.Exit)")
    'three distinct reported attempts'          = @(($a1.Attempt -and $a2.Attempt -and $a3.Attempt -and $a1.Attempt -ne $a2.Attempt -and $a2.Attempt -ne $a3.Attempt -and $a1.Attempt -ne $a3.Attempt), "'$($a1.Attempt)' '$($a2.Attempt)' '$($a3.Attempt)'")
    'attempts inside the requested directory'   = @(($attemptDirs.Count -eq 3 -and @($a1.Attempt, $a2.Attempt, $a3.Attempt | Where-Object { $_ -and (Split-Path $_ -Parent) -eq $shEvd }).Count -eq 3), "dirs=$($attemptDirs.Count)")
    'first attempt files unchanged'             = @(($a1Hashes.Count -eq 3 -and (($a1Hashes | ConvertTo-Json -Compress) -ceq ($a1After | ConvertTo-Json -Compress))), "before=$($a1Hashes | ConvertTo-Json -Compress) after=$($a1After | ConvertTo-Json -Compress)")
    'first attempt manifest verifies'           = @(((Test-AttemptManifest $a1.Attempt) -eq ''), (Test-AttemptManifest $a1.Attempt))
    'second attempt manifest verifies'          = @(((Test-AttemptManifest $a2.Attempt) -eq ''), (Test-AttemptManifest $a2.Attempt))
    'busy attempt manifest verifies'            = @(((Test-AttemptManifest $a3.Attempt) -eq ''), (Test-AttemptManifest $a3.Attempt))
    'each READY record names its own record'    = @(($rp1 -and $rp2 -and $rp1 -ne $rp2 -and (Test-Path -LiteralPath $rp1) -and (Test-Path -LiteralPath $rp2)), "'$rp1' '$rp2'")
    'lock release recorded per READY attempt'   = @(((Get-P $a1.LockRelease 'ok') -eq $true -and (Get-P $a2.LockRelease 'ok') -eq $true -and $null -eq $a3.LockRelease), "$((Get-P $a1.LockRelease 'ok'))/$((Get-P $a2.LockRelease 'ok'))/$($null -ne $a3.LockRelease)")
    'busy attempt recorded as NOT_EXECUTED_BUSY' = @(((Get-P $a3.Recovery 'decision') -eq 'NOT_EXECUTED_BUSY'), (Get-P $a3.Recovery 'decision'))
    'pre-existing evidence file untouched'      = @(((Get-FileHash -LiteralPath $legacy).Hash -eq $legacySha), (Get-FileHash -LiteralPath $legacy).Hash)
    'incident untouched'                        = @(([IO.File]::ReadAllText("$shLock.inflight.json") -ceq $incidentBody), ([IO.File]::ReadAllText("$shLock.inflight.json") -ceq $incidentBody))
})

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
