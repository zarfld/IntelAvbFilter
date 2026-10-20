# test_hil_suite_safety.ps1
# TEST-HILSUITE-*: the hardware suites dispatched automatically (ci-standards-compliance.yml HIL jobs) must not be able to execute TSN
# configuration (SETUP_TAS / SETUP_FP / SETUP_PTM / SETUP_QAV / SET_LAUNCH_TIME / DISARM_TAS) with a hardware
# effect, nor nested executables, through any entry the HIL safety gate would run - regardless of the entry's
# declared class.
#
# Offline, static.  Nothing is built or executed.  "Runnable" is decided by the runner's own
# Get-AvbHilGateDecision (extracted by AST) for a GitHub Actions run with one exact target and no HALT.
# Per runnable entry the scan covers its Build-Tests sources + ExtraSources, local `#include "*.c"` files
# (transitively), tests/common helpers (transitive fixpoint), TSN IOCTLs by macro name, `_NDIS_CONTROL_CODE(n)` /
# `CTL_CODE(..., n, ...)` function number and raw 0x0017xxxx code, process creation, and for ps1 entries
# P/Invoke codes and binary invocation.  Comments are stripped; C string literals are stripped.
#
# Precision rules (each verified from source, never assumed):
#   - capability: an entry can issue an IOCTL only if it (or a helper it calls) calls DeviceIoControl /
#     NtDeviceIoControlFile; mentions without that capability (e.g. ABI code tables) are not findings;
#   - driver effect: a TSN IOCTL is a finding only if its `case IOCTL_AVB_<X>:` handler in
#     src/avb_integration_fixed.c can reach hardware (MMIO / register write / device-ops call); a missing handler
#     counts as hardware-reaching.  A verified no-op stub is reported as INFO and re-flags automatically once
#     the handler gains a hardware effect;
#   - spawn exceptions are evidence-bound: the exact spawned command and the number of spawn sites are verified.
# A whole-binary scan over-approximates `--case` selection: such hits are reported, never silently excused.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent

$total = 0; $passed = 0; $failed = 0
function tc_pass { param([string]$n) $script:total++; $script:passed++; Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red }
function tc_check { param([string]$n, [bool]$ok, [string]$why) if ($ok) { tc_pass $n } else { tc_fail $n $why } }

Write-Host ""
Write-Host "TEST-HILSUITE: TSN configuration and nested execution unreachable through automatic HIL suites (static)" -ForegroundColor Cyan

# IOCTL function numbers from include/avb_ioctl.h (_NDIS_CONTROL_CODE(n) = CTL_CODE(0x17, n, ...) = 0x00170000 + 4n).
$TsnFn = [ordered]@{ SETUP_TAS = 26; SETUP_FP = 27; SETUP_PTM = 28; SETUP_QAV = 35; SET_LAUNCH_TIME = 64; DISARM_TAS = 66 }
$SpawnRx  = '\b(CreateProcess(A|W|AsUser\w*)?|ShellExecute(Ex)?(A|W)?|WinExec|_?w?system|_?w?popen|_w?spawn\w*|_w?exec[lv]p?e?)\s*\('
$IoctlRx  = '\b(DeviceIoControl|NtDeviceIoControlFile|ZwDeviceIoControlFile)\s*\('
$HwEffectRx = '\bmmio_write\b|\bAvbMmioWrite\w*\s*\(|\bwrite_reg\w*\s*\(|\bAvbWriteReg\w*\s*\(|->\s*setup_\w+\s*\(|\bops\s*->|\bintel_\w+\s*\(|\bmdio_write\b'

# Spawn exceptions: entry -> evidence that must ALL match, plus the exact number of spawn sites.
$SpawnExceptions = @{
    'test_zero_polling_overhead' = @{
        Sites = 1
        Evidence = @('GetWindowsDirectoryA\s*\(', '"\\\\System32\\\\wpr\.exe"', 'snprintf\s*\(\s*cmd\s*,\s*sizeof\s*\(\s*cmd\s*\)\s*,\s*"\\"%s\\"\s+-status"\s*,\s*wpr_path\s*\)')
        Why = 'launches only %SystemRoot%\System32\wpr.exe -status (non-destructive query)'
    }
}

function Get-Stripped([string]$Text, [bool]$KeepStrings) {
    $t = [regex]::Replace($Text, '(?s)/\*.*?\*/', ' ')
    $t = [regex]::Replace($t, '//[^\r\n]*', ' ')
    if (-not $KeepStrings) { $t = [regex]::Replace($t, '"(?:\\.|[^"\\])*"', '""') }
    return $t
}
# TSN IOCTL names a code text refers to (macro, function number, raw code).
function Get-TsnNames([string]$Code) {
    $n = @()
    foreach ($k in $TsnFn.Keys) {
        $fn = $TsnFn[$k]; $raw = '{0:X3}' -f ($fn * 4)
        if ([regex]::IsMatch($Code, "\bIOCTL_AVB_$k\b") -or
            [regex]::IsMatch($Code, "(?i)\b0x0*170$raw\b") -or
            [regex]::IsMatch($Code, "\b(_NDIS_CONTROL_CODE|CTL_CODE)\s*\(([^()]*,\s*)?$fn\s*,")) { $n += $k }
    }
    return ,$n
}

# Helper facts for common C files: which functions (transitively) issue TSN IOCTLs, call DeviceIoControl, spawn.
function Get-HelperFacts([string[]]$Files) {
    $bodies = @{}
    foreach ($f in $Files) {
        if (-not (Test-Path -LiteralPath $f)) { continue }
        $code = Get-Stripped ([System.IO.File]::ReadAllText($f)) $false
        foreach ($m in [regex]::Matches($code, '(?ms)^[A-Za-z_][\w\s\*]*?\b(\w+)\s*\([^;{]*\)\s*\{(.*?)^\}')) { $bodies[$m.Groups[1].Value] = $m.Groups[2].Value }
    }
    $tsn = @{}; $ioctl = @{}; $spawn = @{}
    foreach ($k in $bodies.Keys) {
        $names = Get-TsnNames $bodies[$k]; if ($names.Count) { $tsn[$k] = $names }
        if ([regex]::IsMatch($bodies[$k], $IoctlRx)) { $ioctl[$k] = $true }
        if ([regex]::IsMatch($bodies[$k], $SpawnRx)) { $spawn[$k] = $true }
    }
    do {
        $changed = $false
        foreach ($k in $bodies.Keys) {
            foreach ($h in @($tsn.Keys))   { if (-not $tsn.ContainsKey($k) -and [regex]::IsMatch($bodies[$k], '\b' + [regex]::Escape($h) + '\s*\(')) { $tsn[$k] = $tsn[$h]; $changed = $true } }
            foreach ($h in @($ioctl.Keys)) { if (-not $ioctl.ContainsKey($k) -and [regex]::IsMatch($bodies[$k], '\b' + [regex]::Escape($h) + '\s*\(')) { $ioctl[$k] = $true; $changed = $true } }
            foreach ($h in @($spawn.Keys)) { if (-not $spawn.ContainsKey($k) -and [regex]::IsMatch($bodies[$k], '\b' + [regex]::Escape($h) + '\s*\(')) { $spawn[$k] = $true; $changed = $true } }
        }
    } while ($changed)
    return @{ Tsn = $tsn; Ioctl = @($ioctl.Keys); Spawn = @($spawn.Keys) }
}

# Driver handler effect per TSN IOCTL: $true = can reach hardware (or handler not found -> fail closed).
function Get-DriverEffects([string]$DriverFile) {
    $text = if (Test-Path -LiteralPath $DriverFile) { Get-Stripped ([System.IO.File]::ReadAllText($DriverFile)) $false } else { '' }
    $eff = [ordered]@{}
    foreach ($k in $TsnFn.Keys) {
        $m = [regex]::Match($text, "(?s)case\s+IOCTL_AVB_$k\s*:(.*?)(?=\bcase\s+IOCTL_AVB_|\bdefault\s*:)")
        $eff[$k] = (-not $m.Success) -or [regex]::IsMatch($m.Groups[1].Value, $HwEffectRx)
    }
    return $eff
}

function Get-CallsAny([string]$Code, [string[]]$Names) { foreach ($h in $Names) { if ([regex]::IsMatch($Code, '\b' + [regex]::Escape($h) + '\s*\(')) { return $true } }; return $false }

# Scan one entry -> @{ Tsn = names[]; CanIoctl; Spawn = reasons[]; SpawnSites; Raw }
function Get-EntryFacts([string]$Root, [string[]]$Sources, $Helpers, [bool]$IsPs1) {
    $res = @{ Tsn = @(); CanIoctl = $false; Spawn = @(); SpawnSites = 0; Raw = ''; Missing = @() }
    $queue = [System.Collections.Generic.List[string]]::new()
    foreach ($s in $Sources) { $queue.Add((Join-Path $Root ($s -replace '/', '\'))) }
    $seen = @{}
    while ($queue.Count -gt 0) {
        $f = [System.IO.Path]::GetFullPath($queue[0]); $queue.RemoveAt(0)
        if ($seen.ContainsKey($f)) { continue }
        $seen[$f] = $true
        if (-not (Test-Path -LiteralPath $f)) { $res.Missing += $f; continue }
        $raw = [System.IO.File]::ReadAllText($f)
        $res.Raw += "`n" + $raw
        $leaf = Split-Path $f -Leaf
        if ($IsPs1) {
            $code = [regex]::Replace($raw, '(?m)^\s*#.*$', ' ')
            $res.Tsn += (Get-TsnNames $code)
            if ([regex]::IsMatch($code, '\bDeviceIoControl\b')) { $res.CanIoctl = $true }
            if ([regex]::IsMatch($code, '&\s*\$\w+|Start-Process|Invoke-Expression|\bcmd(\.exe)?\s+/c')) { $res.Spawn += "$leaf invokes executables/commands"; $res.SpawnSites++ }
            continue
        }
        foreach ($m in [regex]::Matches($raw, '#include\s+"([^"]+\.c)"')) { $queue.Add((Join-Path (Split-Path $f) $m.Groups[1].Value)) }
        $code = Get-Stripped $raw $false
        $res.Tsn += (Get-TsnNames $code)
        foreach ($h in $Helpers.Tsn.Keys) { if ([regex]::IsMatch($code, '\b' + [regex]::Escape($h) + '\s*\(')) { $res.Tsn += $Helpers.Tsn[$h]; $res.CanIoctl = $true } }
        if ([regex]::IsMatch($code, $IoctlRx) -or (Get-CallsAny $code $Helpers.Ioctl)) { $res.CanIoctl = $true }
        $sites = [regex]::Matches($code, $SpawnRx).Count
        if ($sites -gt 0) { $res.Spawn += "$leaf creates processes ($sites site(s))"; $res.SpawnSites += $sites }
        foreach ($h in $Helpers.Spawn) { if ([regex]::IsMatch($code, '\b' + [regex]::Escape($h) + '\s*\(')) { $res.Spawn += "$leaf calls helper $h() which creates processes"; $res.SpawnSites++ } }
    }
    $res.Tsn = @($res.Tsn | Sort-Object -Unique)
    return $res
}

# ---------------------------------------------------------------------------------------------------------
# Real gate decision (extracted from Run-Tests-CI.ps1)
# ---------------------------------------------------------------------------------------------------------
$RunCIPath = Join-Path $RepoRoot 'tools\test\Run-Tests-CI.ps1'
$ast = [System.Management.Automation.Language.Parser]::ParseFile($RunCIPath, [ref]$null, [ref]$null)
$gateFn = $ast.Find({ param($x) $x -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $x.Name -eq 'Get-AvbHilGateDecision' }, $false)
if ($null -eq $gateFn) { tc_fail 'TEST-HILSUITE-000-gate-decision-available' 'Get-AvbHilGateDecision not found'; exit 1 }
. ([scriptblock]::Create($gateFn.Extent.Text))
function Test-Runnable([string]$Entry, [string]$Class) {
    $d = Get-AvbHilGateDecision -Entry $Entry -Class $Class -Target '0x125B' -Halt '' -TargetCount 1 -AuthorizedUnsafe @() -UnderGitHubActions $true
    return ($d.Action -eq 'Run')
}
tc_pass 'TEST-HILSUITE-000-gate-decision-available'

# Evaluate a suite -> findings (Kind Tsn | Spawn | Info) for gate-runnable entries only.
function Get-SuiteFindings([string]$Root, [string[]]$Entries, [hashtable]$Registry, [hashtable]$BuildMap, $Helpers, $DriverEffects) {
    $out = @()
    foreach ($e in $Entries) {
        $cls = if ($Registry.ContainsKey($e)) { $Registry[$e] } else { '' }
        if (-not (Test-Runnable $e $cls)) { continue }
        $bin = if ($e.StartsWith('ps1:')) { [System.IO.Path]::GetFileNameWithoutExtension($e.Substring(4)) } else { ($e -split '\s+', 2)[0] -replace '\.exe$', '' }
        if ($e.StartsWith('ps1:')) { $f = Get-EntryFacts $Root @($e.Substring(4)) $Helpers $true }
        else {
            $srcs = @(if ($BuildMap.ContainsKey($bin)) { $BuildMap[$bin] })
            if ($srcs.Count -eq 0) { $out += [pscustomobject]@{ Entry = $e; Class = $cls; Kind = 'Tsn'; Why = 'no build sources (unverifiable)' }; continue }
            $f = Get-EntryFacts $Root $srcs $Helpers $false
        }
        foreach ($x in $f.Missing) { $out += [pscustomobject]@{ Entry = $e; Class = $cls; Kind = 'Tsn'; Why = "source missing (unverifiable): $x" } }
        foreach ($n in $f.Tsn) {
            if (-not $f.CanIoctl) { $out += [pscustomobject]@{ Entry = $e; Class = $cls; Kind = 'Info'; Why = "mentions IOCTL_AVB_$n but cannot issue IOCTLs (no DeviceIoControl)" }; continue }
            if (-not $DriverEffects[$n]) { $out += [pscustomobject]@{ Entry = $e; Class = $cls; Kind = 'Info'; Why = "issues IOCTL_AVB_$n; driver handler verified side-effect free (stub)" }; continue }
            $out += [pscustomobject]@{ Entry = $e; Class = $cls; Kind = 'Tsn'; Why = "can issue IOCTL_AVB_$n (driver handler reaches hardware)" }
        }
        if ($f.Spawn.Count -gt 0) {
            $ex = if ($SpawnExceptions.ContainsKey($bin)) { $SpawnExceptions[$bin] } else { $null }
            $ok = $false
            if ($null -ne $ex -and $f.SpawnSites -eq $ex.Sites) { $ok = (@($ex.Evidence | Where-Object { -not [regex]::IsMatch($f.Raw, $_) }).Count -eq 0) }
            if ($ok) { $out += [pscustomobject]@{ Entry = $e; Class = $cls; Kind = 'Info'; Why = "spawn exception verified: $($ex.Why)" } }
            else { foreach ($w in $f.Spawn) { $out += [pscustomobject]@{ Entry = $e; Class = $cls; Kind = 'Spawn'; Why = $w } } }
        }
    }
    return $out
}
function Show($F) { return (@(@($F) | Where-Object { $null -ne $_ } | ForEach-Object { "{0} [{1}] {2}: {3}" -f $_.Entry, $_.Class, $_.Kind, $_.Why }) -join ' || ') }
function Get-KindCount($F, [string]$K) { return @(@($F) | Where-Object { $null -ne $_ -and $_.Kind -eq $K }).Count }

# ---------------------------------------------------------------------------------------------------------
# FX: synthetic fixtures prove each detector and each precision rule (both directions)
# ---------------------------------------------------------------------------------------------------------
$Tmp = Join-Path $env:TEMP ("hilsuite_{0}" -f (Get-Random))
New-Item -ItemType Directory -Path (Join-Path $Tmp 'src'), (Join-Path $Tmp 'common'), (Join-Path $Tmp 'drv') -Force | Out-Null
function New-Src([string]$Rel, [string]$Text) { Set-Content -LiteralPath (Join-Path $Tmp $Rel) -Value $Text -Encoding ASCII; return $Rel }
$commonC = New-Src 'common\fx_common.c' @"
int FxWriteTas(HANDLE h) {
    return DeviceIoControl(h, IOCTL_AVB_SETUP_TAS, 0, 0, 0, 0, 0, 0);
}
int FxIndirect(HANDLE h) {
    return FxWriteTas(h);
}
int FxRun(void) {
    return system("x.exe");
}
int FxRead(HANDLE h) {
    return DeviceIoControl(h, IOCTL_AVB_GET_TIMESTAMP, 0, 0, 0, 0, 0, 0);
}
"@
$drvC = New-Src 'drv\fx_driver.c' @"
switch (code) {
    case IOCTL_AVB_SETUP_TAS: { rc = ops->setup_tas(dev, cfg); } break;
    case IOCTL_AVB_SETUP_FP:  { rc = intel_setup_frame_preemption(dev, cfg); } break;
    case IOCTL_AVB_SETUP_QAV: { /* validated; no-op stub */ status = STATUS_SUCCESS; } break;
    default: break;
}
"@
$fxHelpers = Get-HelperFacts @((Join-Path $Tmp $commonC))
$fxEff = Get-DriverEffects (Join-Path $Tmp $drvC)
$fxSrc = [ordered]@{
    'fx_direct'   = New-Src 'src\fx_direct.c'   'int main(){ DeviceIoControl(h, IOCTL_AVB_SETUP_TAS, 0,0,0,0,0,0); }'
    'fx_helper'   = New-Src 'src\fx_helper.c'   'int main(){ return FxIndirect(h); }'
    'fx_fnnum'    = New-Src 'src\fx_fnnum.c'    '#define MY_PTM _NDIS_CONTROL_CODE(28, METHOD_BUFFERED)
int main(){ DeviceIoControl(h, MY_PTM, 0,0,0,0,0,0); }'
    'fx_rawcode'  = New-Src 'src\fx_rawcode.c'  'int main(){ DeviceIoControl(h, 0x0017006C, 0,0,0,0,0,0); }'
    'fx_include'  = New-Src 'src\fx_include.c'  '#include "fx_included.c"
int main(){ return 0; }'
    'fx_spawn'    = New-Src 'src\fx_spawn.c'    'int main(){ CreateProcessA(0,"other.exe",0,0,0,0,0,0,0,0); }'
    'fx_spawnhlp' = New-Src 'src\fx_spawnhlp.c' 'int main(){ return FxRun(); }'
    'fx_mutblk'   = New-Src 'src\fx_mutblk.c'   'int main(){ DeviceIoControl(h, IOCTL_AVB_SETUP_TAS, 0,0,0,0,0,0); }'
    'fx_uncl'     = New-Src 'src\fx_uncl.c'     'int main(){ DeviceIoControl(h, IOCTL_AVB_SETUP_FP, 0,0,0,0,0,0); }'
    'fx_clean'    = New-Src 'src\fx_clean.c'    '/* IOCTL_AVB_SETUP_TAS in a comment only */ int main(){ printf("IOCTL_AVB_SETUP_TAS"); return FxRead(h); }'
    'fx_abi'      = New-Src 'src\fx_abi.c'      'static const unsigned codes[] = { IOCTL_AVB_SETUP_TAS, IOCTL_AVB_SETUP_FP }; int main(){ return codes[0] != 0; }'
    'fx_qavstub'  = New-Src 'src\fx_qavstub.c'  'int main(){ DeviceIoControl(h, IOCTL_AVB_SETUP_QAV, 0,0,0,0,0,0); }'
    'fx_ltmiss'   = New-Src 'src\fx_ltmiss.c'   'int main(){ DeviceIoControl(h, IOCTL_AVB_SET_LAUNCH_TIME, 0,0,0,0,0,0); }'
    'fx_wprok'    = New-Src 'src\fx_wprok.c'    @'
int main(){ char wpr_path[260]; char cmd[300]; GetWindowsDirectoryA(wpr_path, 260);
  strncat_s(wpr_path, sizeof(wpr_path), "\\System32\\wpr.exe", _TRUNCATE);
  snprintf(cmd, sizeof(cmd), "\"%s\" -status", wpr_path); CreateProcessA(NULL, cmd, 0,0,0,0,0,0,0,0); }
'@
    'fx_wprbad'   = New-Src 'src\fx_wprbad.c'   @'
int main(){ char wpr_path[260]; char cmd[300]; GetWindowsDirectoryA(wpr_path, 260);
  strncat_s(wpr_path, sizeof(wpr_path), "\\System32\\wpr.exe", _TRUNCATE);
  snprintf(cmd, sizeof(cmd), "\"%s\" -status", wpr_path); CreateProcessA(NULL, cmd, 0,0,0,0,0,0,0,0);
  CreateProcessA(NULL, "pnputil /disable-device x", 0,0,0,0,0,0,0,0); }
'@
}
New-Src 'src\fx_included.c' 'static void x(){ DeviceIoControl(h, IOCTL_AVB_SETUP_FP, 0,0,0,0,0,0); }' | Out-Null
$ps1Tsn  = New-Src 'src\fx_tsn.ps1'  '$code = 0x00170068; [Native]::DeviceIoControl($h, $code, $null, 0, $null, 0, [ref]$r, [IntPtr]::Zero)'
$ps1Exec = New-Src 'src\fx_exec.ps1' '& $exePath --all'
$ps1Ok   = New-Src 'src\fx_ok.ps1'   '# & $exe in a comment only
Write-Host ok'
$fxMap = @{}; foreach ($k in $fxSrc.Keys) { $fxMap[$k] = @($fxSrc[$k]) }
$fxReg = @{ fx_direct = 'ReadOnly'; fx_helper = 'Init'; fx_fnnum = 'Mutating:Target'; fx_rawcode = 'ReadOnly'; fx_include = 'ReadOnly'
            fx_spawn = 'ReadOnly'; fx_spawnhlp = 'ReadOnly'; fx_mutblk = 'Mutating'; fx_clean = 'ReadOnly'; fx_abi = 'ReadOnly'
            fx_qavstub = 'Mutating:Target'; fx_ltmiss = 'Mutating:Target'; fx_wprok = 'ReadOnly'; fx_wprbad = 'ReadOnly'
            "ps1:$ps1Tsn" = 'ReadOnly'; "ps1:$ps1Exec" = 'ReadOnly'; "ps1:$ps1Ok" = 'ReadOnly' }   # fx_uncl: unclassified
$SpawnExceptions['fx_wprok']  = $SpawnExceptions['test_zero_polling_overhead']
$SpawnExceptions['fx_wprbad'] = $SpawnExceptions['test_zero_polling_overhead']
function FxRun1([string]$e) { return @(Get-SuiteFindings $Tmp @($e) $fxReg $fxMap $fxHelpers $fxEff) }

tc_check 'TEST-HILSUITE-FX-001-direct-tsn-ioctl-in-runnable-entry' ((Get-KindCount (FxRun1 'fx_direct') 'Tsn') -ge 1) (Show (FxRun1 'fx_direct'))
tc_check 'TEST-HILSUITE-FX-002-tsn-through-transitive-common-helper' (((Get-KindCount (FxRun1 'fx_helper') 'Tsn') -ge 1) -and $fxHelpers.Tsn.ContainsKey('FxIndirect') -and -not $fxHelpers.Tsn.ContainsKey('FxRead')) (Show (FxRun1 'fx_helper'))
tc_check 'TEST-HILSUITE-FX-003-tsn-via-control-code-function-number' ((Get-KindCount (FxRun1 'fx_fnnum') 'Tsn') -ge 1) (Show (FxRun1 'fx_fnnum'))
tc_check 'TEST-HILSUITE-FX-004-tsn-via-raw-ioctl-code' ((Get-KindCount (FxRun1 'fx_rawcode') 'Tsn') -ge 1) (Show (FxRun1 'fx_rawcode'))
tc_check 'TEST-HILSUITE-FX-005-tsn-in-locally-included-c-file' ((Get-KindCount (FxRun1 'fx_include') 'Tsn') -ge 1) (Show (FxRun1 'fx_include'))
tc_check 'TEST-HILSUITE-FX-006-process-creation-is-nested-execution' ((Get-KindCount (FxRun1 'fx_spawn') 'Spawn') -ge 1) (Show (FxRun1 'fx_spawn'))
tc_check 'TEST-HILSUITE-FX-007-process-creation-through-helper' ((Get-KindCount (FxRun1 'fx_spawnhlp') 'Spawn') -ge 1) (Show (FxRun1 'fx_spawnhlp'))
tc_check 'TEST-HILSUITE-FX-008-ps1-entry-tsn-code-and-nested-binary' `
    (((Get-KindCount (FxRun1 "ps1:$ps1Tsn") 'Tsn') -ge 1) -and ((Get-KindCount (FxRun1 "ps1:$ps1Exec") 'Spawn') -ge 1) -and (@(FxRun1 "ps1:$ps1Ok").Count -eq 0)) `
    ("tsn: {0} | exec: {1} | ok: {2}" -f (Show (FxRun1 "ps1:$ps1Tsn")), (Show (FxRun1 "ps1:$ps1Exec")), (Show (FxRun1 "ps1:$ps1Ok")))
tc_check 'TEST-HILSUITE-FX-009-gate-blocked-mutating-and-unclassified-entries-are-not-reachable' ((@(FxRun1 'fx_mutblk').Count -eq 0) -and (@(FxRun1 'fx_uncl').Count -eq 0)) ("{0} | {1}" -f (Show (FxRun1 'fx_mutblk')), (Show (FxRun1 'fx_uncl')))
tc_check 'TEST-HILSUITE-FX-010-comments-strings-and-read-only-helpers-are-clean' (@(FxRun1 'fx_clean').Count -eq 0) (Show (FxRun1 'fx_clean'))
tc_check 'TEST-HILSUITE-FX-011-code-table-without-deviceiocontrol-is-info-only' (((Get-KindCount (FxRun1 'fx_abi') 'Tsn') -eq 0) -and ((Get-KindCount (FxRun1 'fx_abi') 'Info') -ge 1)) (Show (FxRun1 'fx_abi'))
tc_check 'TEST-HILSUITE-FX-012-verified-driver-stub-is-info-and-missing-handler-fails-closed' `
    (((Get-KindCount (FxRun1 'fx_qavstub') 'Tsn') -eq 0) -and ((Get-KindCount (FxRun1 'fx_qavstub') 'Info') -ge 1) -and ((Get-KindCount (FxRun1 'fx_ltmiss') 'Tsn') -ge 1) -and
     $fxEff['SETUP_TAS'] -and $fxEff['SETUP_FP'] -and -not $fxEff['SETUP_QAV'] -and $fxEff['SET_LAUNCH_TIME']) ("qav: {0} | lt: {1}" -f (Show (FxRun1 'fx_qavstub')), (Show (FxRun1 'fx_ltmiss')))
tc_check 'TEST-HILSUITE-FX-013-spawn-exception-requires-exact-evidence-and-site-count' `
    (((Get-KindCount (FxRun1 'fx_wprok') 'Spawn') -eq 0) -and ((Get-KindCount (FxRun1 'fx_wprbad') 'Spawn') -ge 1)) ("ok: {0} | extra spawn: {1}" -f (Show (FxRun1 'fx_wprok')), (Show (FxRun1 'fx_wprbad')))
$SpawnExceptions.Remove('fx_wprok'); $SpawnExceptions.Remove('fx_wprbad')
Remove-Item $Tmp -Recurse -Force -ErrorAction SilentlyContinue

# ---------------------------------------------------------------------------------------------------------
# REAL: hardware suites dispatched by any workflow (the automatic HIL jobs of ci-standards-compliance.yml)
# ---------------------------------------------------------------------------------------------------------
$offered = @()
foreach ($wf in @(Get-ChildItem (Join-Path $RepoRoot '.github\workflows') -File | Where-Object { $_.Extension -in '.yml', '.yaml' })) {
    foreach ($mm in [regex]::Matches([System.IO.File]::ReadAllText($wf.FullName), 'Run-Tests-CI\.ps1[^\r\n]*?-Suite\s+(Hardware\w+)')) { $offered += $mm.Groups[1].Value }
}
$offered = @($offered | Sort-Object -Unique)
$approved = @('HardwareMandatory', 'HardwareUnit', 'HardwareIntegration')
tc_check 'TEST-HILSUITE-REAL-001-workflows-dispatch-only-reviewed-hardware-suites' (($offered.Count -gt 0) -and (@($offered | Where-Object { $approved -notcontains $_ }).Count -eq 0)) `
    ("dispatched: [{0}]; approved: [{1}]" -f ($offered -join ','), ($approved -join ','))

function Get-RunnerList([string]$name) {
    $as = $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and
                         $x.Left -is [System.Management.Automation.Language.VariableExpressionAst] -and
                         $x.Left.VariablePath.UserPath -eq $name }, $true) | Select-Object -First 1
    if ($null -eq $as) { return @() }
    return @($as.Right.FindAll({ param($x) $x -is [System.Management.Automation.Language.StringConstantExpressionAst] }, $true) | ForEach-Object { $_.Value })
}
$registry = @{}
$regAst = $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and
                         $x.Left -is [System.Management.Automation.Language.VariableExpressionAst] -and
                         $x.Left.VariablePath.UserPath -eq 'HilTestSafety' }, $true) | Select-Object -First 1
$ht = $regAst.Right.Find({ param($x) $x -is [System.Management.Automation.Language.HashtableAst] }, $true)
foreach ($kv in $ht.KeyValuePairs) { $registry[$kv.Item1.Value] = ($kv.Item2.Extent.Text.Trim() -replace "^'|'$", '') }

$buildLines = Get-Content (Join-Path $RepoRoot 'tools\build\Build-Tests.ps1')
$buildMap = @{}
for ($i = 0; $i -lt $buildLines.Count; $i++) {
    if ($buildLines[$i] -match '^\s*Name\s*=\s*"([^"]+)"') {
        $nm = $Matches[1]; $srcs = @()
        for ($j = $i + 1; $j -lt [Math]::Min($i + 16, $buildLines.Count); $j++) {
            if ($buildLines[$j] -match '^\s*Name\s*=') { break }
            if ($buildLines[$j] -match '^\s*(Source|ExtraSources)\s*=\s*"([^"]+)"') { $srcs += ($Matches[2] -split '\s+') }
        }
        if (-not $buildMap.ContainsKey($nm)) { $buildMap[$nm] = @($srcs | Where-Object { $_ -notmatch 'avb_test_common\.c$' }) }
    }
}
$helpers = Get-HelperFacts @((Join-Path $RepoRoot 'tests\common\avb_test_common.c'))
$drvEff  = Get-DriverEffects (Join-Path $RepoRoot 'src\avb_integration_fixed.c')
Write-Host ("  [INFO] tests/common helpers: TSN=[{0}] DeviceIoControl=[{1}] spawn=[{2}]" -f (@($helpers.Tsn.Keys) -join ','), ($helpers.Ioctl -join ','), ($helpers.Spawn -join ',')) -ForegroundColor Yellow
Write-Host ("  [INFO] driver TSN handlers reaching hardware: {0}" -f ((@($drvEff.Keys) | ForEach-Object { "$_=$($drvEff[$_])" }) -join ', ')) -ForegroundColor Yellow

$realTsn = @(); $realSpawn = @(); $realInfo = @()
foreach ($s in $offered) {
    $entries = @(Get-RunnerList ("{0}Tests" -f $s))
    $runnable = @($entries | Where-Object { Test-Runnable $_ $(if ($registry.ContainsKey($_)) { $registry[$_] } else { '' }) })
    Write-Host ("  [INFO] {0}: {1} entries, {2} gate-runnable" -f $s, $entries.Count, $runnable.Count) -ForegroundColor Yellow
    $f = @(Get-SuiteFindings $RepoRoot $entries $registry $buildMap $helpers $drvEff)
    $realTsn   += @($f | Where-Object { $_.Kind -eq 'Tsn' }   | ForEach-Object { "[$s] " + (Show $_) })
    $realSpawn += @($f | Where-Object { $_.Kind -eq 'Spawn' } | ForEach-Object { "[$s] " + (Show $_) })
    $realInfo  += @($f | Where-Object { $_.Kind -eq 'Info' }  | ForEach-Object { "[$s] " + (Show $_) })
}
foreach ($x in $realInfo) { Write-Host "  [INFO] $x" -ForegroundColor Yellow }
tc_check 'TEST-HILSUITE-REAL-002-no-gate-runnable-entry-can-issue-hardware-tsn-configuration' ($realTsn.Count -eq 0) ($realTsn -join ' || ')
tc_check 'TEST-HILSUITE-REAL-003-no-gate-runnable-entry-executes-unverified-nested-processes' ($realSpawn.Count -eq 0) ($realSpawn -join ' || ')
tc_check 'TEST-HILSUITE-REAL-004-tas-fp-ptm-handlers-are-recognized-as-hardware-reaching' ($drvEff['SETUP_TAS'] -and $drvEff['SETUP_FP'] -and $drvEff['SETUP_PTM']) `
    ("driver effects: {0}" -f ((@($drvEff.Keys) | ForEach-Object { "$_=$($drvEff[$_])" }) -join ', '))

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-HILSUITE: automatic HIL suites can reach TSN configuration or nested execution" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-HILSUITE: automatic HIL suites cannot reach TSN configuration or nested execution" -ForegroundColor Green
exit 0
