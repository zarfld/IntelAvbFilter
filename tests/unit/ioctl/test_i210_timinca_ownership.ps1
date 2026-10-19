# test_i210_timinca_ownership.ps1
# TEST-TIMINCA-ARCH-001..007: stable ARCHITECTURE contracts for the I210 TIMINCA correction
# (HAL boundary: ADR-HAL-001 #126, ADR-ARCH-003 #92, REQ-F-DEVICE-ABS-003 #40;
#  register SSOT: REQ-NF-REGS-001 #21, ADR-SUBM-001 #124).
#
#  001  generic src/** never includes a device-specific TIMINCA codec or uses its identifiers
#  002  no I210-specific code in the generic frequency-adjust handler or in AvbPlatformInit
#  003  the device registry dispatches to the correct family implementation
#  004  TIMINCA fields come from the register SSOT: canonical YAML -> generated header agree, and
#       the I210 implementation consumes the generated masks and defines no register bits of its own
#  005  only the I210 device implementation programs the I210 TIMINCA register
#  006  the I210 init chain performs exactly ONE TIMINCA programming step            (RED until redesign)
#  007  other families still initialise through their own device init() + platform init
#  008  IOCTL_AVB_ADJUST_FREQUENCY routes through the two logical device ops; ETW order + status mapping  (#343)
#  009  observation IOCTLs cannot reach initialization; the lazy timestamp paths do not use TIMINCA        (#343)
#  010  PTP_READY is established in one helper, only after a successful intel_init                          (#343)
#  011  the HIL test oracle selects exactly the device IDs the driver maps to the I210 family              (#343)
#  012  bring-up reports initialization failure instead of success                                          (#343)
#  013  init() of I210/I219/I226 returns an init_ptp() failure (a swallowed failure would yield a false PTP_READY) (#343)
#
# These tests check structure and contracts, never source-text hashes. Behavioural proof lives in
# test_i210_device_ops.c (independent datasheet vectors and init behavior against the production device implementation).
#
# Implements: I210 TIMINCA correction (datasheet 333016 v3.7, 8.15.11)
# Verifies:   #126, #92, #40, #21, #124, #3, #149
# No hardware, no driver.

$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent

$total = 0; $failed = 0
function tc_pass { param([string]$n) $script:total++; Write-Host "  [PASS] $n" -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++; Write-Host "  [FAIL] ${n}: $why" -ForegroundColor Red }
function Read-Src([string]$rel) { [System.IO.File]::ReadAllText((Join-Path $RepoRoot $rel)) }
# Remove C comments so contracts apply to code, not prose.
function Strip-Comments([string]$s) {
    $s = [regex]::Replace($s, '(?s)/\*.*?\*/', ' ')
    return [regex]::Replace($s, '//[^\r\n]*', ' ')
}
# Function / case body helpers (brace-balanced from the first '{' after the anchor).
function Get-Braced([string]$text, [string]$anchorRx) {
    $m = [regex]::Match($text, $anchorRx)
    if (-not $m.Success) { return $null }
    $i = $text.IndexOf('{', $m.Index + $m.Length - 1); if ($i -lt 0) { return $null }
    $depth = 0
    for ($j = $i; $j -lt $text.Length; $j++) {
        if ($text[$j] -eq '{') { $depth++ } elseif ($text[$j] -eq '}') { $depth--; if ($depth -eq 0) { return $text.Substring($i, $j - $i + 1) } }
    }
    return $null
}

Write-Host "  TEST-TIMINCA-ARCH-001..007 - HAL boundary and register SSOT contracts (no hardware)" -ForegroundColor Cyan

$integ = Read-Src 'src\avb_integration_fixed.c'
$i210  = Read-Src 'devices\intel_i210_impl.c'
$precomp = Read-Src 'src\precomp.h'

# 001 ---------------------------------------------------------------------------------------------
$offenders = @()
foreach ($f in Get-ChildItem (Join-Path $RepoRoot 'src') -Recurse -Include *.c, *.h) {
    $code = Strip-Comments ([System.IO.File]::ReadAllText($f.FullName))
    if ($code -match '#\s*include\s*[<"][^>"]*timinca[^>"]*[>"]' -or $code -match '\bi210_timinca_\w+' -or $code -match '\bAVB_I210_\w+') {
        $offenders += $f.Name
    }
}
if ($offenders.Count -eq 0) { tc_pass 'TEST-TIMINCA-ARCH-001-src-has-no-device-specific-TIMINCA-codec' } else { tc_fail 'TEST-TIMINCA-ARCH-001' ($offenders -join ', ') }

# 002 ---------------------------------------------------------------------------------------------
$adj  = Get-Braced $integ 'case IOCTL_AVB_ADJUST_FREQUENCY:\s*\{'
$plat = Get-Braced $integ 'NTSTATUS AvbPlatformInit\(_In_ device_t \*dev\)\s*\{'
$bad = @()
foreach ($pair in @(@('ADJUST_FREQUENCY handler', $adj), @('AvbPlatformInit', $plat))) {
    if (-not $pair[1]) { $bad += "$($pair[0]): not found"; continue }
    $code = Strip-Comments $pair[1]
    if ($code -match '\bINTEL_DEVICE_I210\b' -or $code -match '\bI210_\w+' -or $code -match 'INTEL_TIMINCA_I210') { $bad += $pair[0] }
}
if ($bad.Count -eq 0) { tc_pass 'TEST-TIMINCA-ARCH-002-no-I210-specific-code-in-generic-handler-or-platform-init' } else { tc_fail 'TEST-TIMINCA-ARCH-002' ($bad -join '; ') }

# 003 ---------------------------------------------------------------------------------------------
$reg = Read-Src 'devices\intel_device_registry.c'
$expect = @{ 'INTEL_DEVICE_I210' = 'i210_ops'; 'INTEL_DEVICE_I217' = 'i217_ops'; 'INTEL_DEVICE_I219' = 'i219_ops'; 'INTEL_DEVICE_I226' = 'i226_ops' }
$wrong = @()
foreach ($k in $expect.Keys) {
    if ($reg -notmatch ('(?m)^\s*device_registry\[' + $k + '\]\s*=\s*&' + $expect[$k] + '\s*;')) { $wrong += "$k -> $($expect[$k])" }
}
$typeMap = [regex]::Matches($integ, '(?m)case\s+(INTEL_DEV_I210_\w+)\s*:\s*return\s+(INTEL_DEVICE_\w+)')
$typeWrong = @($typeMap | Where-Object { $_.Groups[2].Value -ne 'INTEL_DEVICE_I210' } | ForEach-Object { $_.Groups[1].Value })
if ($wrong.Count -eq 0 -and $typeMap.Count -gt 0 -and $typeWrong.Count -eq 0) { tc_pass "TEST-TIMINCA-ARCH-003-registry-dispatches-to-correct-family ($($typeMap.Count) I210 device IDs mapped)" }
else { tc_fail 'TEST-TIMINCA-ARCH-003' ("registry wrong: " + ($wrong -join ', ') + "; I210 id mapped elsewhere: " + ($typeWrong -join ', ')) }

# 004 ---------------------------------------------------------------------------------------------
# Canonical YAML = the I210 YAML that the regs repo's own generator loop processes LAST (CI loops
# devices/*.yaml in glob order and every I210 YAML writes gen/i210_regs.h).
$yamlDir = Join-Path $RepoRoot 'intel-ethernet-regs\devices'
$i210Yamls = @(Get-ChildItem $yamlDir -Filter *.yaml | Where-Object { (Get-Content $_.FullName -TotalCount 1) -match '^device:\s*I210\s*$' } | Sort-Object Name)
$canonical = $i210Yamls | Select-Object -Last 1
$yaml = [System.IO.File]::ReadAllText($canonical.FullName)
$tim = [regex]::Match($yaml, '(?s)- name: TIMINCA\r?\n.*?(?=\r?\n      - name: )').Value
$f1 = [regex]::Match($tim, '(?s)- name: INCVALUE\s+lsb:\s*(\d+)\s+width:\s*(\d+)')
$f2 = [regex]::Match($tim, '(?s)- name: ISGN\s+lsb:\s*(\d+)\s+width:\s*(\d+)')
$gen = Read-Src 'intel-ethernet-regs\gen\i210_regs.h'
$ok = $f1.Success -and $f2.Success
if ($ok) {
    $incSh = [int]$f1.Groups[1].Value; $incW = [int]$f1.Groups[2].Value; $sgSh = [int]$f2.Groups[1].Value; $sgW = [int]$f2.Groups[2].Value
    $ok = ($incSh -eq 0 -and $incW -eq 31 -and $sgSh -eq 31 -and $sgW -eq 1) -and   # datasheet 8.15.11
          ($gen -match ('I210_TIMINCA_INCVALUE_SHIFT\s+' + $incSh + '\b')) -and ($gen -match ('I210_TIMINCA_INCVALUE_MASK\s+\(\(\(1ULL<<' + $incW + '\)')) -and
          ($gen -match ('I210_TIMINCA_ISGN_SHIFT\s+' + $sgSh + '\b')) -and ($gen -match ('I210_TIMINCA_ISGN_MASK\s+\(\(\(1ULL<<' + $sgW + '\)'))
}
$i210Code = Strip-Comments $i210
$redef = @([regex]::Matches($i210Code, '(?m)^\s*#\s*define\s+I210_TIMINCA\w*'))
$consumes = ($i210Code -match '\bI210_TIMINCA_INCVALUE_MASK\b') -and ($i210Code -match '\bI210_TIMINCA_ISGN_MASK\b')
$ssotIncluded = $precomp -match 'intel-ethernet-regs/gen/i210_regs\.h'
if ($ok -and $redef.Count -eq 0 -and $consumes -and $ssotIncluded) { tc_pass "TEST-TIMINCA-ARCH-004-fields-originate-from-register-SSOT (canonical YAML: $($canonical.Name))" }
else { tc_fail 'TEST-TIMINCA-ARCH-004' "yaml/header agree=$ok impl-redefinitions=$($redef.Count) impl-consumes-SSOT-masks=$consumes ssot-header-included=$ssotIncluded canonical=$($canonical.Name)" }

# 005 ---------------------------------------------------------------------------------------------
$users = @()
foreach ($f in Get-ChildItem $RepoRoot -Recurse -Include *.c, *.h -File -ErrorAction SilentlyContinue |
         Where-Object { $_.FullName -match '\\(src|devices)\\' -and $_.FullName -notmatch '\\(build|external|intel-ethernet-regs)\\' }) {
    if ((Strip-Comments ([System.IO.File]::ReadAllText($f.FullName))) -match '\bI210_TIMINCA\b') { $users += $f.Name }
}
if ($users.Count -eq 1 -and $users[0] -eq 'intel_i210_impl.c') { tc_pass 'TEST-TIMINCA-ARCH-005-only-the-I210-device-implementation-addresses-the-register' }
else { tc_fail 'TEST-TIMINCA-ARCH-005' ("files using I210_TIMINCA: " + ($users -join ', ')) }

# 006 (RED until redesign) -----------------------------------------------------------------------
# Programming steps reachable from i210_ops.init: the generic platform step (when init() calls the
# platform init and AvbPlatformInit writes TIMINCA) + each I210 register write in init_ptp.
# The anchor must include the opening brace: a bare signature also matches the forward declaration.
$initFn  = Get-Braced $i210 '(?m)^static int init\(device_t \*dev\)\s*\{'
$initPtp = Get-Braced $i210 '(?m)^static int init_ptp\(device_t \*dev\)\s*\{'
$platformWrites   = if ($initFn -and (Strip-Comments $initFn) -match 'ndis_platform_ops\.init' -and $plat -and (Strip-Comments $plat) -match 'write_timinca\(') { 1 } else { 0 }
$initPtpWrites    = if ($initPtp) { @([regex]::Matches((Strip-Comments $initPtp), 'mmio_write\(dev,\s*I210_TIMINCA\b')).Count } else { -1 }
$steps = $platformWrites + $initPtpWrites
if ($initFn -and $initPtp -and $steps -eq 1) { tc_pass 'TEST-TIMINCA-ARCH-006-I210-init-chain-programs-TIMINCA-exactly-once' }
else { tc_fail 'TEST-TIMINCA-ARCH-006' "TIMINCA programming steps in the I210 init chain = $steps (platform step=$platformWrites, init_ptp=$initPtpWrites); expected exactly 1 (single device-owned initialisation)" }

# 007 ---------------------------------------------------------------------------------------------
$unchanged = @()
foreach ($pair in @(@('devices\intel_i217_impl.c'), @('devices\intel_i219_impl.c'), @('devices\intel_i226_impl.c'))) {
    $txt = Strip-Comments (Read-Src $pair[0])
    if ($txt -notmatch 'ndis_platform_ops\.init\b') { $unchanged += $pair[0] }
}
if ($unchanged.Count -eq 0) { tc_pass 'TEST-TIMINCA-ARCH-007-other-families-still-use-platform-init-in-their-own-init' }
else { tc_fail 'TEST-TIMINCA-ARCH-007' ("family init no longer reaches platform init: " + ($unchanged -join ', ')) }

# ---- generic-layer contracts (issue #343). The kernel handler cannot be executed offline, so these are
# ---- STRUCTURAL contracts on comment-stripped function bodies (token presence/ordering, call reachability),
# ---- not behavior tests; the behavior of the device side is proven in test_i210_device_ops.c.
function Get-FunctionBodies([string]$code) {
    $map = @{}
    foreach ($m in [regex]::Matches($code, '(?m)^[A-Za-z_][\w \t\*]*?\b([A-Za-z_]\w*)\s*\(([^;{}]*?)\)\s*\{')) {
        $name = $m.Groups[1].Value
        if ($name -in 'if', 'for', 'while', 'switch', 'sizeof', 'return') { continue }
        $body = Get-Braced $code ([regex]::Escape($m.Value.Substring(0, $m.Value.Length - 1)) + '\{')
        if ($body -and -not $map.ContainsKey($name)) { $map[$name] = $body }
    }
    return $map
}
$integCode = Strip-Comments $integ
$fnBodies  = Get-FunctionBodies $integCode
$dispatcher = 'AvbHandleDeviceIoControl'

# 008 ---------------------------------------------------------------------------------------------
# IOCTL_AVB_ADJUST_FREQUENCY routes through the two logical device operations, keeps the ETW/status order
# (reject -> ETW 300 before the hardware gate; accepted request -> ETW 200 before the gate) and translates
# the device return codes: -EINVAL -> STATUS_INVALID_PARAMETER, -ENOTSUP -> STATUS_NOT_SUPPORTED.
$adjc = if ($adj) { Strip-Comments $adj } else { '' }
$iVal  = $adjc.IndexOf('validate_frequency_request')
$iWarn = $adjc.IndexOf('EventWriteEVT_PHC_FORCESET_WARN')
$iGate = [regex]::Match($adjc, 'hw_state\s*<\s*AVB_HW_BAR_MAPPED').Index
$iAdj  = $adjc.IndexOf('adjust_frequency(')
$ordered = ($iVal -ge 0) -and ($iWarn -gt $iVal) -and ($iGate -gt $iWarn) -and ($iAdj -gt $iGate)
# The NTSTATUS ASSIGNMENT is required (a bare "STATUS_..." match would also hit "NDIS_STATUS_...").
$mapInv  = ($adjc -match '-EINVAL[\s\S]{0,500}?\bstatus\s*=\s*STATUS_INVALID_PARAMETER') -and
           ($adjc -match 'frc\s*!=\s*0[\s\S]{0,800}?\bstatus\s*=\s*STATUS_INVALID_PARAMETER')           # adjust AND validate-reject paths
$mapNot  = ([regex]::Matches($adjc, '-ENOTSUP[\s\S]{0,500}?\bstatus\s*=\s*STATUS_NOT_SUPPORTED').Count -ge 2)   # validate AND adjust paths
$etw300  = $adjc -match 'EventWriteEVT_HARDWARE_FAULT'
$cur     = $adjc -match 'current_increment\s*='
if ($ordered -and $mapInv -and $mapNot -and $etw300 -and $cur) { tc_pass 'TEST-TIMINCA-ARCH-008-ADJUST_FREQUENCY-routes-through-device-ops-with-ETW-order-and-status-mapping' }
else { tc_fail 'TEST-TIMINCA-ARCH-008' "validate<ETW200<hwgate<adjust ordered=$ordered (idx $iVal,$iWarn,$iGate,$iAdj); -EINVAL->INVALID_PARAMETER=$mapInv; -ENOTSUP->NOT_SUPPORTED=$mapNot; ETW300=$etw300; current_increment assigned=$cur" }

# 009 ---------------------------------------------------------------------------------------------
# Observation / adjustment IOCTLs must not be able to reach any initialization function, and the two lazy
# timestamp paths must not use TIMINCA as a readiness signal.  Reachability is computed over the file's own
# helper functions (the dispatcher itself is excluded: INIT_DEVICE legitimately initializes inside it).
$initFns = New-Object System.Collections.Generic.HashSet[string]
[void]$initFns.Add('intel_init')
do {
    $grew = $false
    foreach ($kv in $fnBodies.GetEnumerator()) {
        if ($kv.Key -eq $dispatcher -or $initFns.Contains($kv.Key)) { continue }
        foreach ($f in @($initFns)) { if ($kv.Value -match ('\b' + [regex]::Escape($f) + '\s*\(')) { [void]$initFns.Add($kv.Key); $grew = $true; break } }
    }
} while ($grew)
$cases = @(
    @('GET/SET_TIMESTAMP',  'case IOCTL_AVB_SET_TIMESTAMP:\s*\{',        $true),
    @('GET_AUX_TIMESTAMP',  'case IOCTL_AVB_GET_AUX_TIMESTAMP:\s*\{',    $true),
    @('GET_CLOCK_CONFIG',   'case IOCTL_AVB_GET_CLOCK_CONFIG:\s*\{',     $false),
    @('ADJUST_FREQUENCY',   'case IOCTL_AVB_ADJUST_FREQUENCY:\s*\{',     $false),
    @('PHC_OFFSET_ADJUST',  'case IOCTL_AVB_PHC_OFFSET_ADJUST:\s*\{',    $false)
)
$viol = @()
foreach ($c in $cases) {
    $b = Get-Braced $integ $c[1]
    if (-not $b) { $viol += "$($c[0]): case body not found"; continue }
    $code = Strip-Comments $b
    foreach ($f in $initFns) { if ($code -match ('\b' + [regex]::Escape($f) + '\s*\(')) { $viol += "$($c[0]) calls $f (initialization reachable)" } }
    if ($c[2]) {
        if ($code -match '\bread_timinca\b|TIMINCA|timinca') { $viol += "$($c[0]) uses TIMINCA as a readiness signal" }
        if ($code -notmatch 'hw_state\s*<\s*AVB_HW_PTP_READY' -or $code -notmatch 'STATUS_DEVICE_NOT_READY') { $viol += "$($c[0]) does not return STATUS_DEVICE_NOT_READY below PTP_READY" }
    }
}
if ($viol.Count -eq 0) { tc_pass "TEST-TIMINCA-ARCH-009-observation-IOCTLs-cannot-initialize-and-lazy-paths-do-not-use-TIMINCA (init-reaching helpers: $(@($initFns | Sort-Object) -join ','))" }
else { tc_fail 'TEST-TIMINCA-ARCH-009' ($viol -join '; ') }

# 010 ---------------------------------------------------------------------------------------------
# PTP_READY is established in ONE place (AvbMarkPtpReady), only after a successful intel_init, by the explicit
# lifecycle operations (AvbBringUpHardware, AvbEnsureDeviceReady) - never on a failed initialization.
$mark   = $fnBodies['AvbMarkPtpReady']
$bring  = $fnBodies['AvbBringUpHardware']
$ensure = $fnBodies['AvbEnsureDeviceReady']
$bad = @()
if (-not $mark -or $mark -notmatch 'AVB_SET_HW_STATE' -or $mark -notmatch 'AVB_HW_PTP_READY') { $bad += 'AvbMarkPtpReady missing or does not set PTP_READY' }
if (-not $bring) { $bad += 'AvbBringUpHardware not found' }
else {
    $okBlock = Get-Braced $bring 'if\s*\(\s*init_result\s*==\s*0\s*\)\s*\{'
    if (-not $okBlock -or $okBlock -notmatch 'AvbMarkPtpReady\s*\(') { $bad += 'AvbBringUpHardware: success branch does not call AvbMarkPtpReady' }
    $rest = if ($okBlock) { $bring.Replace($okBlock, ' ') } else { $bring }
    if ($rest -match 'AvbMarkPtpReady\s*\(' -or $rest -match 'AVB_SET_HW_STATE\([^)]*AVB_HW_PTP_READY') { $bad += 'AvbBringUpHardware: PTP_READY reachable outside the intel_init success branch' }
}
if (-not $ensure) { $bad += 'AvbEnsureDeviceReady not found' }
else {
    $iFail = [regex]::Match($ensure, 'init_result\s*!=\s*0').Index
    $iMark = $ensure.IndexOf('AvbMarkPtpReady(')
    if ($iMark -lt 0 -or $iFail -lt 0 -or $iMark -lt $iFail) { $bad += 'AvbEnsureDeviceReady: AvbMarkPtpReady must follow the intel_init failure return' }
    if ($ensure -match 'AVB_SET_HW_STATE\([^)]*AVB_HW_PTP_READY') { $bad += 'AvbEnsureDeviceReady: duplicate PTP_READY assignment (use the shared helper)' }
}
if ($bad.Count -eq 0) { tc_pass 'TEST-TIMINCA-ARCH-010-PTP_READY-established-in-one-helper-only-after-successful-intel_init' }
else { tc_fail 'TEST-TIMINCA-ARCH-010' ($bad -join '; ') }

# 012 ---------------------------------------------------------------------------------------------
# Initialization failures must be reported, not converted to success: AvbBringUpHardware returns the failing status when
# the basic initialization fails and a failure status when intel_init fails.  Adapter ENUMERATION stays possible because its
# callers (AvbInitializeDevice -> FilterAttach/IOCTL context creation, FilterRestart) deliberately discard that status.
$bringCode = if ($bring) { $bring } else { '' }
$basicFail = Get-Braced $bringCode 'if\s*\(\s*!NT_SUCCESS\(\s*status\s*\)\s*\)\s*\{'
$initElse  = [regex]::Match($bringCode, 'if\s*\(\s*init_result\s*==\s*0\s*\)\s*\{[\s\S]*?\}\s*else\s*\{')
$elseBody  = if ($initElse.Success) { Get-Braced $bringCode ([regex]::Escape($initElse.Value.Substring(0, $initElse.Value.LastIndexOf('{'))) + '\{') } else { $null }
$bad12 = @()
if (-not $basicFail)                                                        { $bad12 += 'basic-init failure branch not found' }
elseif ($basicFail -notmatch 'return\s+status\s*;' -or $basicFail -match 'STATUS_SUCCESS') { $bad12 += 'basic-init failure does not propagate its status (or returns STATUS_SUCCESS)' }
if (-not $initElse.Success)                                                 { $bad12 += 'intel_init failure branch not found' }
else {
    $failBlock = [regex]::Match($bringCode.Substring($initElse.Index), 'else\s*\{[\s\S]*?\n        \}').Value
    if ($failBlock -notmatch 'return\s+[^;]*(STATUS_DEVICE_HARDWARE_ERROR|init_status|status)\s*;' -or $failBlock -match 'return\s+STATUS_SUCCESS') { $bad12 += 'intel_init failure does not return a failure status' }
}
$unconditionalOk = [regex]::Matches($bringCode, 'return\s+STATUS_SUCCESS\s*;').Count
if ($bad12.Count -eq 0) { tc_pass "TEST-TIMINCA-ARCH-012-bring-up-reports-initialization-failure (remaining STATUS_SUCCESS returns: $unconditionalOk = normal completion, which also covers the documented MDIO software-fallback mode)" }
else { tc_fail 'TEST-TIMINCA-ARCH-012' ($bad12 -join '; ') }

# 011 ---------------------------------------------------------------------------------------------
# The HIL-test oracle (tests/common/avb_test_common.h, AvbTestIsI210Did) must select exactly the device IDs the driver
# maps to the I210 family (AvbGetIntelDeviceType), so test expectations and driver behavior cannot drift apart.
$drv    = @([regex]::Matches($integ, 'case\s+(INTEL_DEV_I210_\w+)\s*:\s*return\s+INTEL_DEVICE_I210') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
$common = Read-Src 'tests\common\avb_test_common.h'
$fn     = Get-Braced $common 'AvbTestIsI210Did\(unsigned short did\)\s*\{'
$orc    = if ($fn) { @([regex]::Matches((Strip-Comments $fn), 'case\s+(INTEL_DEV_\w+)\s*:') | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique) } else { @() }
if ($drv.Count -gt 0 -and $orc.Count -gt 0 -and (($drv -join ',') -eq ($orc -join ','))) { tc_pass "TEST-TIMINCA-ARCH-011-HIL-oracle-selects-exactly-the-driver's-I210-device-IDs ($($drv.Count) IDs)" }
else { tc_fail 'TEST-TIMINCA-ARCH-011' ("driver: " + ($drv -join ',') + " | oracle: " + ($orc -join ',')) }

# 013 ---------------------------------------------------------------------------------------------
# init() == 0 is what lets the generic layer establish AVB_HW_PTP_READY (AvbMarkPtpReady), so no family's init() may swallow
# an init_ptp() failure.  Each family that initialises its PTP clock from init() must return the failure.
$bad13 = @()
foreach ($fam in 'i210', 'i219', 'i226') {
    $src  = Strip-Comments (Read-Src "devices\intel_${fam}_impl.c")
    $body = Get-Braced $src 'static\s+int\s+init\s*\(\s*device_t\s*\*\s*dev\s*\)\s*\{'
    if (-not $body) { $bad13 += "${fam}: init() not found"; continue }
    if ($body -notmatch 'init_ptp\s*\(') { $bad13 += "${fam}: init() does not call init_ptp()"; continue }
    $returned = $body -match 'return\s+init_ptp\s*\(' -or
                $body -match '(\w+)\s*=\s*init_ptp\s*\([^)]*\)\s*;\s*if\s*\(\s*\1\s*!=\s*0\s*\)\s*\{[^}]*\breturn\s+(?:-\s*\w+|\1)\s*;'
    if (-not $returned) { $bad13 += "${fam}: init() does not return an init_ptp() failure (would let a failed PTP init look like success)" }
}
if ($bad13.Count -eq 0) { tc_pass 'TEST-TIMINCA-ARCH-013-init()-of-I210/I219/I226-returns-init_ptp-failure (no false PTP_READY from a swallowed failure)' }
else { tc_fail 'TEST-TIMINCA-ARCH-013' ($bad13 -join '; ') }

Write-Host "  Summary: $($total - $failed)/$total passed, $failed failed"
if ($failed -gt 0) { exit 1 }
exit 0
