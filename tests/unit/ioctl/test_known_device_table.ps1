# test_known_device_table.ps1
# TEST-DEVTABLE-001..003: the device-ID recognition table in test_hw_state_machine.c is built from
# the INTEL_DEV_* macros in include/intel_pci_ids.h (the SSOT) and cannot silently drift.
#
# Defect fixed: the test carried a hand-written raw-hex whitelist that omitted I219-LM4 (0x15BB,
# present on the HIL host) and several I225/I226 IDs, so a correctly bound adapter FAILED
# "Device ID recognition".  A generic "caps != 0" test was rejected as device recognition.
#
# Implements: REQ-NF-REGS-001 (SSOT, no magic numbers)
# Verifies:   #18 (hardware state machine)
# No hardware, no driver.

$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
$Ids  = Join-Path $RepoRoot 'include\intel_pci_ids.h'
$Test = Join-Path $RepoRoot 'tests\integration\hw_state\test_hw_state_machine.c'

$total = 0; $failed = 0
function tc_pass { param([string]$n) $script:total++; Write-Host "  [PASS] $n" -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++; Write-Host "  [FAIL] ${n}: $why" -ForegroundColor Red }

Write-Host "  TEST-DEVTABLE-001..003 - device recognition table vs intel_pci_ids.h (no hardware)" -ForegroundColor Cyan

$macros = @(Select-String -Path $Ids -Pattern '^#define\s+(INTEL_DEV_\w+)\s+0x' | ForEach-Object { $_.Matches[0].Groups[1].Value })
$src    = Get-Content $Test -Raw

# 001: the SSOT header actually defines device macros (guards the scan itself)
if ($macros.Count -ge 40) { tc_pass "TEST-DEVTABLE-001-ssot-header-defines-$($macros.Count)-device-macros" }
else { tc_fail 'TEST-DEVTABLE-001' "only $($macros.Count) INTEL_DEV_* macros found - scan broken?" }

# 002: every SSOT device macro is referenced by the test's recognition table
$missing = @($macros | Where-Object { $src -notmatch ('\b' + [regex]::Escape($_) + '\b') })
if ($missing.Count -eq 0) { tc_pass 'TEST-DEVTABLE-002-every-ssot-device-id-is-recognised' }
else { tc_fail 'TEST-DEVTABLE-002' ("not in test_hw_state_machine.c: " + ($missing -join ', ')) }

# 003: no hand-written raw device-ID comparison remains
$raw = @([regex]::Matches($src, 'query\.device_id\s*==\s*0x[0-9A-Fa-f]{4}'))
if ($raw.Count -eq 0) { tc_pass 'TEST-DEVTABLE-003-no-raw-hex-device-id-comparisons' }
else { tc_fail 'TEST-DEVTABLE-003' "$($raw.Count) raw 'query.device_id == 0x....' comparison(s) remain" }

Write-Host "  Summary: $($total - $failed)/$total passed, $failed failed"
if ($failed -gt 0) { exit 1 }
exit 0
