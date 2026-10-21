# test_junit_verdict_normalization.ps1
# Offline tests: the CI converter (Convert-Logs-To-JUnit.ps1) and the local parser
# (Parse-TestResults.ps1) normalise the verdict contract identically, the traceability
# matrix keeps exact classification, and the suite runner gates mandatory verification
# on SKIP_NOT_IMPLEMENTED / INCONCLUSIVE.
#
# TEST-VERDICT-001  converter: SKIP_UNSUPPORTED is recognised (was silently dropped)
# TEST-VERDICT-002  converter: SKIP_NOT_IMPLEMENTED -> labelled <skipped>, never a pass
# TEST-VERDICT-003  converter: INCONCLUSIVE -> <failure> mandatory / labelled <skipped> optional
# TEST-VERDICT-004  converter and parser agree on the JUnit class of every verdict (both modes)
# TEST-VERDICT-005  traceability row: exact verdict columns, old 3-count row regex still matches
# TEST-VERDICT-006  configuration/suite recorded in the converter output
# TEST-VERDICT-007  runner (sandboxed): exit 0 + SKIP_NOT_IMPLEMENTED -> not PASSED, exit 1
# TEST-VERDICT-008  runner (sandboxed): clean PASS + SKIP_UNSUPPORTED stays green, exit 0
# TEST-VERDICT-009  runner (sandboxed): exit 0 + INCONCLUSIVE -> exit 1
# TEST-VERDICT-010  suite verification class: only HardwareMutation is optional
# TEST-VERDICT-011  unknown verdict throws (no silent pass/skip)
#
# Implements: test verdict taxonomy (docs/testing/test-verdict-contract.md)
# Verifies:   #199, #317, #340 (no false-pass; fail-closed mandatory verification)
#
# No hardware and NO driver access: the runner is exercised from a temp copy of
# tools/test whose lifecycle snapshot (which would open \\.\IntelAvbFilter) is stubbed.

$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
$ToolsTest = Join-Path $RepoRoot 'tools\test'
$Convert   = Join-Path $ToolsTest 'Convert-Logs-To-JUnit.ps1'
$Parse     = Join-Path $ToolsTest 'Parse-TestResults.ps1'
$VerdictLib = Join-Path $RepoRoot 'tools\lib\TestVerdict.ps1'

$total = 0; $passed = 0; $failed = 0
function tc_pass { param([string]$n) $script:total++; $script:passed++; Write-Host "  [PASS] $n" -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red }

Write-Host ""
Write-Host "  TEST-VERDICT-001..011 - verdict normalisation, no hardware, no driver access" -ForegroundColor Cyan
Write-Host "  Verifies: #199, #317, #340" -ForegroundColor Cyan

$Tmp = Join-Path $env:TEMP ("verdict_norm_{0}" -f (Get-Random))
New-Item -ItemType Directory -Path $Tmp -Force | Out-Null

function Strip-Comments([string]$xml) { [regex]::Replace($xml, '(?s)<!--.*?-->', '') }

# Run the converter in isolated mode over one synthetic log. Returns xml, md, error.
function Invoke-Converter {
    param([string]$Name, [string]$LogText, [switch]$Optional, [string]$Configuration = '', [string]$Suite = '')
    $dir = Join-Path $Tmp $Name
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    Set-Content -Path (Join-Path $dir 'x_test.exe_20260101_000000.log') -Value $LogText -Encoding UTF8
    $out = Join-Path $dir 'out.xml'
    $err = ''
    try {
        $a = @{ LogsDir = $dir; RunDate = '20260101'; OutputFile = $out }
        if ($Optional)      { $a.OptionalVerification = $true }
        if ($Configuration) { $a.Configuration = $Configuration }
        if ($Suite)         { $a.SuiteName = $Suite }
        & $Convert @a *>$null
    } catch { $err = $_.Exception.Message }
    $xml = if (Test-Path $out) { Strip-Comments (Get-Content $out -Raw) } else { '' }
    $mdFile = Get-ChildItem $dir -Filter 'traceability-summary-*.md' -ErrorAction SilentlyContinue | Select-Object -First 1
    $md = if ($mdFile) { Get-Content $mdFile.FullName -Raw } else { '' }
    return @{ Xml = $xml; Md = $md; Error = $err }
}

function Assert-Xml {
    param([string]$Name, [hashtable]$R, [string[]]$Match = @(), [string[]]$NoMatch = @())
    $why = @()
    if ($R.Error) { $why += "converter error: $($R.Error)" }
    foreach ($m in $Match)   { if ($R.Xml -notmatch $m) { $why += "missing /$m/" } }
    foreach ($m in $NoMatch) { if ($R.Xml -match $m)    { $why += "unexpected /$m/" } }
    if ($why.Count -eq 0) { tc_pass $Name } else { tc_fail $Name (($why -join '; ') + "`nXML:`n$($R.Xml)") }
}

# --- TEST-VERDICT-001: SKIP_UNSUPPORTED was dropped by the converter ---------------------------------
$r = Invoke-Converter 'v001' '  [SKIP_UNSUPPORTED] TC-V-001/adapter:0: ntstatus=0xC00000BB did=0x1533 lacks capability'
Assert-Xml 'TEST-VERDICT-001-converter-recognises-skip-unsupported' $r `
    -Match @('<skipped message="[^"]*\[SKIP_UNSUPPORTED\][^"]*ntstatus=0xC00000BB', 'tests="1"', 'skipped="1"') `
    -NoMatch @('\(overall\)')

# --- TEST-VERDICT-002: NOT_IMPLEMENTED is a labelled skip, never a pass ------------------------------
$r = Invoke-Converter 'v002' @"
  [PASS] TC-V-002/adapter:0: ok
  [SKIP_NOT_IMPLEMENTED] TC-V-002/adapter:1: ntstatus=0xC0000002 did=0x125B op=DISARM_TAS
"@
Assert-Xml 'TEST-VERDICT-002-converter-not-implemented-labelled-skip' $r `
    -Match @('<skipped message="\[SKIP_NOT_IMPLEMENTED\][^"]*ntstatus=0xC0000002', 'failures="0"', 'skipped="1"')

# --- TEST-VERDICT-003: INCONCLUSIVE depends on verification class -------------------------------------
$line = '  [INCONCLUSIVE] TC-V-003/adapter:0: capability bit not defined; ntstatus=0xC00000BB'
$r = Invoke-Converter 'v003a' $line
Assert-Xml 'TEST-VERDICT-003a-converter-inconclusive-mandatory-failure' $r `
    -Match @('<failure[^>]*type="INCONCLUSIVE"', 'failures="1"')
$r = Invoke-Converter 'v003b' $line -Optional
Assert-Xml 'TEST-VERDICT-003b-converter-inconclusive-optional-labelled-skip' $r `
    -Match @('<skipped message="\[INCONCLUSIVE\]', 'failures="0"') -NoMatch @('<failure')

# --- TEST-VERDICT-004: converter and parser agree for every verdict, both modes -----------------------
$verdicts = 'PASS','FAIL','SKIP','SKIP_UNSUPPORTED','SKIP_NOT_IMPLEMENTED','BLOCKED','INCONCLUSIVE','INFRA_FAILURE','CLEANUP_FAILED'
$logText = ($verdicts | ForEach-Object { "  [$_] TC-AGREE-$($_ -replace '_','-')/adapter:0: r" }) -join "`n"
function Class-Of([string]$xml, [string]$tc) {
    $m = [regex]::Match($xml, '(?s)<testcase name="' + [regex]::Escape($tc) + '[^"]*"[^>]*?(/>|>(.*?)</testcase>)')
    if (-not $m.Success) { return 'MISSING' }
    $body = $m.Value
    if ($body -match '<failure') { return 'failure' }
    if ($body -match '<skipped') { return 'skipped' }
    return 'pass'
}
foreach ($mode in 'mandatory','optional') {
    $opt = ($mode -eq 'optional')
    $cr = Invoke-Converter ("v004c-$mode") $logText -Optional:$opt
    $pf = Join-Path $Tmp "v004p-$mode.log"; $px = Join-Path $Tmp "v004p-$mode.xml"
    Set-Content $pf -Value $logText -Encoding UTF8
    try { if ($opt) { & $Parse -LogFile $pf -OutputFile $px -OptionalVerification *>$null } else { & $Parse -LogFile $pf -OutputFile $px *>$null } } catch {}
    $pxml = if (Test-Path $px) { Strip-Comments (Get-Content $px -Raw) } else { '' }
    $bad = @()
    foreach ($v in $verdicts) {
        $tc = "TC-AGREE-$($v -replace '_','-')"
        $a = Class-Of $cr.Xml $tc; $b = Class-Of $pxml $tc
        if ($a -ne $b -or $a -eq 'MISSING') { $bad += "$v converter=$a parser=$b" }
    }
    if ($bad.Count -eq 0) { tc_pass "TEST-VERDICT-004-$mode-converter-parser-agree" }
    else { tc_fail "TEST-VERDICT-004-$mode-converter-parser-agree" ($bad -join '; ') }
}

# --- TEST-VERDICT-005: traceability row keeps exact classification, old consumers still parse -------
$r = Invoke-Converter 'v005' @"
  Verifies: #9 (REQ-F-TAS-001)
  Implements: #206 (TEST-TAS-001)
  [PASS] TC-V-005A/adapter:0: ok
  [SKIP_NOT_IMPLEMENTED] TC-V-005B/adapter:0: ntstatus=0xC0000002
  [SKIP_UNSUPPORTED] TC-V-005C/adapter:0: ntstatus=0xC00000BB
  [INCONCLUSIVE] TC-V-005D/adapter:0: unknown
"@
# The CI coverage step (ci-standards-compliance.yml) reads rows with this exact regex.
$oldRowRe = '(?m)^\| \[#(\d+)\]\([^)]+\) \| ([^|]+) \| ([^|]*) \| (\d+) \| (\d+) \| (\d+) \|'
$m = [regex]::Match($r.Md, $oldRowRe)
if ($m.Success) {
    # PASS=1, FAIL=0 (genuine failures only), SKIP=3 (NOT_IMPL + UNSUPPORTED + INCONCLUSIVE)
    $okOld = ($m.Groups[4].Value -eq '1' -and $m.Groups[5].Value -eq '0' -and $m.Groups[6].Value -eq '3')
    $newRowRe = '\| 1 \| 0 \| 3 \| 1 \| 1 \|'   # ... | NOT_IMPL=1 | INCONCL=1 |
    if ($okOld -and $r.Md -match $newRowRe -and $r.Md -match 'NOT_IMPL' -and $r.Md -match 'INCONCL') {
        tc_pass 'TEST-VERDICT-005-traceability-exact-columns-and-legacy-regex'
    } else {
        tc_fail 'TEST-VERDICT-005-traceability-exact-columns-and-legacy-regex' "counts wrong. old=$($m.Groups[4].Value)/$($m.Groups[5].Value)/$($m.Groups[6].Value)`n$($r.Md)"
    }
} else {
    tc_fail 'TEST-VERDICT-005-traceability-exact-columns-and-legacy-regex' "legacy row regex does not match.`n$($r.Md)"
}

# --- TEST-VERDICT-006: configuration and suite are recorded -------------------------------------------
$r = Invoke-Converter 'v006' '  [PASS] TC-V-006/adapter:0: ok' -Configuration 'Release' -Suite 'HardwareUnit'
Assert-Xml 'TEST-VERDICT-006-configuration-and-suite-recorded' $r `
    -Match @('<property name="configuration" value="Release"', '<property name="suite" value="HardwareUnit"')

# --- TEST-VERDICT-007..009: suite runner from a sandbox copy (lifecycle snapshot stubbed) -------------
$Sandbox = Join-Path $Tmp 'sandbox'
$SbTools = Join-Path $Sandbox 'tools\test'
New-Item -ItemType Directory -Path $SbTools -Force | Out-Null
foreach ($f in 'Run-Tests-CI.ps1','Convert-Logs-To-JUnit.ps1','Lib-AvbLifecycle.ps1') {
    $src = Join-Path $ToolsTest $f
    if (Test-Path $src) { Copy-Item $src (Join-Path $SbTools $f) }
}
New-Item -ItemType Directory -Path (Join-Path $Sandbox 'tools\lib') -Force | Out-Null
Copy-Item $VerdictLib (Join-Path $Sandbox 'tools\lib\TestVerdict.ps1')
Copy-Item (Join-Path $RepoRoot 'tools\lib\NativeExit.ps1') (Join-Path $Sandbox 'tools\lib\NativeExit.ps1')
# Replace the lifecycle library (it opens \\.\IntelAvbFilter) with inert stubs.
@'
function Get-AvbLifecycleSnapshot { return $null }
function Write-LifecycleDiff { param($Before, $After, $Label) return $false }
'@ | Set-Content (Join-Path $SbTools 'Lib-AvbLifecycle.ps1') -Encoding UTF8

function Invoke-SandboxRunner {
    param([string]$Name, [string]$FakeBody)
    $dir = Join-Path $Tmp $Name
    $logs = Join-Path $dir 'logs'
    New-Item -ItemType Directory -Path $logs -Force | Out-Null
    $fake = Join-Path $dir 'test_fake.ps1'
    Set-Content $fake -Value $FakeBody -Encoding UTF8
    $wrapper = Join-Path $dir 'wrapper.ps1'
    $runner = Join-Path $SbTools 'Run-Tests-CI.ps1'
    $fakeFwd = $fake.Replace('\', '/')
    @"
& '$runner' -Suite Unit -Tests @('ps1:$fakeFwd') -TestTimeoutSeconds 60 -Configuration Debug -LogsDir '$logs'
exit `$LASTEXITCODE
"@ | Set-Content $wrapper -Encoding UTF8
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = 'powershell.exe'
    $psi.Arguments = "-NoProfile -ExecutionPolicy Bypass -NonInteractive -File `"$wrapper`""
    $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true; $psi.UseShellExecute = $false
    $psi.WorkingDirectory = $dir
    $p = [System.Diagnostics.Process]::Start($psi)
    $o = $p.StandardOutput.ReadToEndAsync(); $e = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit(120000)) { $p.Kill(); return @{ Exit = -999; Out = 'TIMEOUT'; Xml = '' } }
    $xmlFile = Get-ChildItem $logs -Filter 'junit-*.xml' -ErrorAction SilentlyContinue | Select-Object -First 1
    $xml = if ($xmlFile) { Strip-Comments (Get-Content $xmlFile.FullName -Raw) } else { '' }
    return @{ Exit = $p.ExitCode; Out = ($o.Result + "`n" + $e.Result); Xml = $xml }
}

$r = Invoke-SandboxRunner 'v007' @'
Write-Host '  [SKIP_NOT_IMPLEMENTED] TC-RUN-007/adapter:0: ntstatus=0xC0000002 did=0x125B op=DISARM_TAS'
exit 0
'@
if ($r.Exit -eq 1 -and $r.Out -match 'INCOMPLETE' -and $r.Out -notmatch 'test_fake\.ps1 PASSED' -and $r.Xml -match '<skipped message="\[SKIP_NOT_IMPLEMENTED\]') {
    tc_pass 'TEST-VERDICT-007-runner-exit0-plus-not-implemented-is-incomplete'
} else {
    tc_fail 'TEST-VERDICT-007-runner-exit0-plus-not-implemented-is-incomplete' "exit=$($r.Exit) (expected 1); out tail: $(($r.Out -split "`n" | Select-Object -Last 14) -join ' | ')"
}

$r = Invoke-SandboxRunner 'v008' @'
Write-Host '  [PASS] TC-RUN-008/adapter:0: ok'
Write-Host '  [SKIP_UNSUPPORTED] TC-RUN-008B/adapter:1: ntstatus=0xC00000BB did=0x1533 lacks capability'
exit 0
'@
if ($r.Exit -eq 0 -and $r.Out -match 'test_fake\.ps1 PASSED') {
    tc_pass 'TEST-VERDICT-008-runner-pass-plus-unsupported-stays-green'
} else {
    tc_fail 'TEST-VERDICT-008-runner-pass-plus-unsupported-stays-green' "exit=$($r.Exit) (expected 0)"
}

$r = Invoke-SandboxRunner 'v009' @'
Write-Host '  [INCONCLUSIVE] TC-RUN-009/adapter:0: capability unknown'
exit 0
'@
if ($r.Exit -eq 1 -and $r.Out -notmatch 'test_fake\.ps1 PASSED') {
    tc_pass 'TEST-VERDICT-009-runner-exit0-plus-inconclusive-is-nonpassing'
} else {
    tc_fail 'TEST-VERDICT-009-runner-exit0-plus-inconclusive-is-nonpassing' "exit=$($r.Exit) (expected 1)"
}

# --- TEST-VERDICT-010 / 011: shared helper contract -------------------------------------------------------
. $VerdictLib
$cls = @{}
foreach ($s in 'Unit','Integration','All','HardwareUnit','HardwareIntegration','HardwareMandatory','HardwareMutation') {
    $cls[$s] = Get-AvbSuiteVerificationClass -Suite $s
}
$optionals = @($cls.GetEnumerator() | Where-Object { $_.Value -eq 'optional' } | ForEach-Object { $_.Key })
if ($optionals.Count -eq 1 -and $optionals[0] -eq 'HardwareMutation') { tc_pass 'TEST-VERDICT-010-only-hardwaremutation-is-optional' }
else { tc_fail 'TEST-VERDICT-010-only-hardwaremutation-is-optional' ("optional suites: " + ($optionals -join ',')) }

$threw = $false
try { Get-AvbVerdictClass -Verdict 'SKIP_NOT_A_THING' | Out-Null } catch { $threw = $true }
if ($threw) { tc_pass 'TEST-VERDICT-011-unknown-verdict-throws' } else { tc_fail 'TEST-VERDICT-011-unknown-verdict-throws' 'unknown verdict was accepted silently' }

Remove-Item $Tmp -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host "  Summary: $passed/$total passed, $failed failed" -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-VERDICT: $failed normalisation regression(s)" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-VERDICT: verdict normalisation invariants hold" -ForegroundColor Green
exit 0
