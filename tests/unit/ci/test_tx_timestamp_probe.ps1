# test_tx_timestamp_probe.ps1
# TEST-TXPROBE-*: TC-CORR-TX-RX-001 phase-1 probe tools/test/Invoke-AvbTxTimestampProbe.ps1 (D4, Q 2026-10-22) - offline.
# Interpretation rules with SYNTHETIC register values (not hardware evidence) + static non-destructiveness guard.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
$total = 0; $passed = 0; $failed = 0
function tc_check { param([string]$n, [bool]$ok, [string]$why)
    $script:total++; if ($ok) { $script:passed++; Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green } else { $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red } }
Write-Host ""; Write-Host "TEST-TXPROBE: TX timestamp probe (offline, synthetic register values only)" -ForegroundColor Cyan

$Probe = Join-Path $RepoRoot 'tools\test\Invoke-AvbTxTimestampProbe.ps1'
$ast = [System.Management.Automation.Language.Parser]::ParseFile($Probe, [ref]$null, [ref]$null)
$fn = $ast.Find({ param($x) $x -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $x.Name -eq 'Get-AvbTxProbeInterpretation' }, $true)
tc_check 'TEST-TXPROBE-000-interpretation-function-present' ($null -ne $fn) $Probe
if (-not $fn) { exit 1 }
Set-Item -Path 'function:script:Get-AvbTxProbeInterpretation' -Value $fn.Body.GetScriptBlock()

function S($ms) { return [ordered]@{ bit0SetAfterMs = $ms } }
$cases = [ordered]@{
    '001-en-clear-means-no-latch-possible'      = @{ Did = '0x1533'; Before = @([uint32]0x00, [uint32]0x00, [uint32]0x00); Sends = @((S $null)); Rx = 'EN = 0'; Latched = 0 }
    '002-bit0-set-before-send-is-a-held-latch'  = @{ Did = '0x125B'; Before = @([uint32]0x11, [uint32]0x11, [uint32]0x11); Sends = @((S 0.5)); Rx = 'already SET before any probe frame'; Latched = 1 }
    '003-no-latch-after-sends'                  = @{ Did = '0x125B'; Before = @([uint32]0x10, [uint32]0x10, [uint32]0x10); Sends = @((S $null), (S $null)); Rx = 'did not latch the injected frames'; Latched = 0 }
    '004-latch-after-send-association-unverified' = @{ Did = '0x125B'; Before = @([uint32]0x10, [uint32]0x10, [uint32]0x10); Sends = @((S 0.8), (S $null)); Rx = 'association with the probe frame is UNVERIFIED'; Latched = 1 }
}
foreach ($k in $cases.Keys) {
    $c = $cases[$k]; $i = Get-AvbTxProbeInterpretation -Did $c.Did -Before $c.Before -SendResults $c.Sends
    tc_check "TEST-TXPROBE-$k" ((($i.notes -join ' | ') -match [regex]::Escape($c.Rx)) -and $i.framesLatched -eq $c.Latched) ("notes={0} latched={1}" -f ($i.notes -join ' | '), $i.framesLatched)
}
$i210 = Get-AvbTxProbeInterpretation -Did '0x1533' -Before @([uint32]0x10) -SendResults @((S $null))
$i226 = Get-AvbTxProbeInterpretation -Did '0x125B' -Before @([uint32]0x10) -SendResults @((S $null))
tc_check 'TEST-TXPROBE-005-bit0-decoding-verified-only-where-the-register-ssot-defines-it' `
    (-not $i210.bit0DecodingVerified -and ($i210.notes -join ' ') -match 'UNVERIFIED \(not defined in the repository register SSOT' -and $i226.bit0DecodingVerified) ("i210={0} i226={1}" -f $i210.bit0DecodingVerified, $i226.bit0DecodingVerified)

# 006 static: only ENUM/OPEN/READ_REGISTER/TEST_SEND_PTP; never WRITE_REGISTER, INIT_DEVICE, GET_TX_TIMESTAMP,
#     SET_HW_TIMESTAMPING; never TXSTMPL/TXSTMPH; the only register read is TSYNCTXCTL.
$code = (([System.IO.File]::ReadAllText($Probe) -split "`r?`n") | Where-Object { $_ -notmatch '^\s*#' }) -join "`n"
$forbidden = @('0x0017005C', '0x00170050', '0x001700C4', '0x001700A0', '0x0B618', '0xB618', '0x0B61C', '0xB61C')
$hits = @($forbidden | Where-Object { $code -match [regex]::Escape($_) })
$codes = @([regex]::Matches($code, '0x0017[0-9A-Fa-f]{4}') | ForEach-Object { $_.Value.ToUpperInvariant() } | Sort-Object -Unique)
$regs  = @([regex]::Matches($code, '\$TSYNCTXCTL\s*=\s*(0x[0-9A-Fa-f]+)') | ForEach-Object { $_.Groups[1].Value })
tc_check 'TEST-TXPROBE-006-probe-is-non-destructive-by-construction' `
    ($hits.Count -eq 0 -and (@($codes | Where-Object { $_ -notin @('0X0017007C', '0X00170080', '0X00170058', '0X001700CC') }).Count -eq 0) -and $regs.Count -eq 1 -and $regs[0] -eq '0x0B614') `
    ("forbidden={0} ioctl codes={1} register={2}" -f ($hits -join ','), ($codes -join ','), ($regs -join ','))

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-TXPROBE: probe contract violated" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-TXPROBE: probe interpretation + non-destructive construction verified (offline)" -ForegroundColor Green
exit 0
