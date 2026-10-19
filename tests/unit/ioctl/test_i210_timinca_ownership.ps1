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
#
# These tests check structure and contracts, never source-text hashes. Behavioural proof lives in
# test_i210_frequency_ops.c (independent datasheet vectors against the production device implementation).
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

Write-Host "  Summary: $($total - $failed)/$total passed, $failed failed"
if ($failed -gt 0) { exit 1 }
exit 0
