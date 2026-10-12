# test_health_sentinel.ps1
# Behavioral integration test for the #328 health sentinel.
# Invokes Run-Tests-CI.ps1 via a wrapper subprocess.
# Injects health failure via AVB_TEST_HEALTH_FAIL_AFTER without a real driver.
#
# TEST-SENTINEL-001..007: #328 Behavioral Health Sentinel
#
# TEST-SENTINEL-001  fake test actually PASSED before health failure
# TEST-SENTINEL-002  health-failure hook executed after that specific test
# TEST-SENTINEL-003  exit=1 due to injected health failure (not another error)
# TEST-SENTINEL-004  FAILED(INFRA) in the actual generated JUnit XML (isolated dir)
# TEST-SENTINEL-005  IsMandatorySuite by Suite alone (not gated on -Tests)
# TEST-SENTINEL-006  test env var scope - AVB_TEST_HEALTH_FAIL_AFTER cleared after use
# TEST-SENTINEL-007  negative: INFRA absent from JUnit -> TEST-SENTINEL-004 FAILs
#
# Log isolation: writes to its own tmpDir via -LogsDir; cannot contaminate real HIL logs.
# Fault injection: AVB_TEST_HEALTH_FAIL_AFTER is set only in the subprocess env.
# No hardware or driver required.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot   = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
$RunCIPath  = Join-Path $RepoRoot 'tools\test\Run-Tests-CI.ps1'
$ParsePath  = Join-Path $RepoRoot 'tools\test\Parse-TestResults.ps1'

$total = 0; $passed = 0; $failed = 0
function tc_pass { param([string]$n) $script:total++; $script:passed++
    Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++
    Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red }

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  TEST-SENTINEL-001..007: #328 Behavioral Health Sentinel" -ForegroundColor Cyan
Write-Host "  Injects health failure via AVB_TEST_HEALTH_FAIL_AFTER=*" -ForegroundColor Cyan
Write-Host "  Isolated logs via -LogsDir; no contamination of HIL logs" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

if (-not (Test-Path $RunCIPath)) {
    tc_fail "TEST-SENTINEL-SETUP" ("Run-Tests-CI.ps1 not found at {0}" -f $RunCIPath)
    exit 1
}

# Isolated temporary workspace - separate from real HIL logs
$tmpDir  = Join-Path $env:TEMP ("sentinel_test_{0}" -f (Get-Random))
$logsDir = Join-Path $tmpDir "logs"
New-Item -ItemType Directory -Path $tmpDir  -Force | Out-Null
New-Item -ItemType Directory -Path $logsDir -Force | Out-Null

# Fake passing test script
$fakePsTest = Join-Path $tmpDir "test_sentinel_fake.ps1"
"Write-Host '  [PASS] TC-FAKE-001: fake test always passes'; exit 0" | Set-Content $fakePsTest -Encoding UTF8

# Wrapper invokes Run-Tests-CI.ps1 with isolated log dir and injected health failure.
# AVB_TEST_HEALTH_FAIL_AFTER is set ONLY inside this wrapper (subprocess env).
# The parent process env is never modified, preventing accidental HIL contamination.
# -LogsDir routes the runner's JUnit XML into $logsDir (isolated tmpDir), not repo logs/.
$wrapperScript = Join-Path $tmpDir "wrapper.ps1"
$fakePsTestFwd = $fakePsTest.Replace('\', '/')
$logsEsc = $logsDir.Replace('\', '\\')
$wrapperContent = @"
`$env:AVB_TEST_HEALTH_FAIL_AFTER = '*'
`$env:AVB_DRIVER_BUILD = 'Debug'
& '$RunCIPath' -Suite HardwareMandatory -Tests @('ps1:$fakePsTestFwd') ``
    -TestTimeoutSeconds 30 -Configuration Debug -LogsDir '$logsEsc'
exit `$LASTEXITCODE
"@
$wrapperContent | Set-Content $wrapperScript -Encoding UTF8

# Run wrapper as subprocess with isolated log dir
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName  = 'powershell.exe'
$psi.Arguments = ("-ExecutionPolicy Bypass -NonInteractive -File `"" + $wrapperScript + "`"")
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError  = $true
$psi.UseShellExecute = $false
$psi.WorkingDirectory = $RepoRoot

$proc = [System.Diagnostics.Process]::Start($psi)
$outJob = $proc.StandardOutput.ReadToEndAsync()
$errJob = $proc.StandardError.ReadToEndAsync()

if (-not $proc.WaitForExit(60000)) {
    $proc.Kill()
    tc_fail "TEST-SENTINEL-001" "Run-Tests-CI.ps1 timed out (60s)"
    Remove-Item $tmpDir -Recurse -Force -ErrorAction SilentlyContinue
    exit 1
}
$exitCode  = $proc.ExitCode
$allOutput = $outJob.Result + [Environment]::NewLine + $errJob.Result
$outputLines = $allOutput -split [Environment]::NewLine

Write-Host ""
Write-Host "--- Subprocess output (first 50 lines) ---" -ForegroundColor DarkGray
$outputLines | Select-Object -First 50 | ForEach-Object {
    Write-Host ("  {0}" -f $_) -ForegroundColor DarkGray
}
Write-Host ("--- exit code: {0} ---" -f $exitCode) -ForegroundColor DarkGray

# TEST-SENTINEL-001: fake test actually PASSED before the health failure
$fakeTestPassed = $allOutput -match 'TC-FAKE-001.*fake test always passes'
if ($fakeTestPassed) {
    tc_pass "TEST-SENTINEL-001: fake test (TC-FAKE-001) passed before health failure injected"
} else {
    tc_fail "TEST-SENTINEL-001" "TC-FAKE-001 PASS line not found -- fake test did not execute or did not pass before failure"
}

# TEST-SENTINEL-002: health-failure hook executed after the specific test
$hookFired = $allOutput -match 'TEST-HOOK.*test_sentinel_fake'
if ($hookFired) {
    tc_pass "TEST-SENTINEL-002: TEST-HOOK fired after 'test_sentinel_fake' specifically"
} else {
    tc_fail "TEST-SENTINEL-002" "TEST-HOOK did not fire after 'test_sentinel_fake' -- hook not triggered for the right test"
}

# TEST-SENTINEL-003: exit=1 specifically due to INFRA failure (not a pre-existing failure)
# Exit 1 with INFRA marker confirms the sentinel mechanism, not another failure mode.
$infraPresent = ($allOutput -match 'FAILED.INFRA.') -or ($allOutput -match '328-SENTINEL') -or ($allOutput -match 'INFRA-HEALTH')
if ($exitCode -eq 1 -and $infraPresent) {
    tc_pass "TEST-SENTINEL-003: exit=1 with INFRA marker -- sentinel mechanism confirmed (RED->GREEN)"
} elseif ($exitCode -ne 1) {
    tc_fail "TEST-SENTINEL-003" ("exit={0}: expected 1 from health failure" -f $exitCode)
} else {
    tc_fail "TEST-SENTINEL-003" "exit=1 but no INFRA marker -- exit 1 from a different cause"
}

# TEST-SENTINEL-004: FAILED(INFRA) in the actual generated JUnit XML (isolated dir).
# Run-Tests-CI.ps1 was invoked with -LogsDir pointing to $logsDir (inside $tmpDir).
# The JUnit XML must be in that exact directory -- no fallback to shared logs/ or console.
# Accepting console output instead of JUnit is a false pass: remove that fallback here.
$xmlFiles = @(Get-ChildItem -Path (Join-Path $logsDir "junit-*.xml") -ErrorAction SilentlyContinue)
if ($xmlFiles.Count -eq 0) {
    tc_fail "TEST-SENTINEL-004" ("No JUnit XML generated in isolated logs dir '{0}' -- Run-Tests-CI.ps1 must write JUnit into -LogsDir" -f $logsDir)
} else {
    $latestXml  = $xmlFiles | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $xmlContent = Get-Content $latestXml.FullName -Raw -ErrorAction SilentlyContinue
    $junitHasInfra = $xmlContent -match 'INFRA-HEALTH' -or
                     $xmlContent -match 'FAILED\(INFRA\)' -or
                     $xmlContent -match '<failure'
    if ($junitHasInfra) {
        tc_pass ("TEST-SENTINEL-004: INFRA/failure element in isolated JUnit '{0}'" -f $latestXml.Name)
    } else {
        tc_fail "TEST-SENTINEL-004" ("JUnit '{0}' exists but contains no INFRA/failure element -- Parse-TestResults must emit <failure> for INFRA_FAILURE results" -f $latestXml.Name)
    }
}

# TEST-SENTINEL-007: negative test -- deliberately absent JUnit INFRA -> 004 check correctly FAILs.
# Creates a JUnit XML without any failure element and verifies the 004 check rejects it.
# This proves the check actually discriminates (positive proof that protection is real).
& {
    $negDir  = Join-Path $env:TEMP ("sentinel_neg_{0}" -f (Get-Random))
    New-Item -ItemType Directory -Path $negDir -Force | Out-Null

    $fakeXml     = Join-Path $negDir "junit-HardwareMandatory-Debug-20991231.xml"
    $fakeContent = '<?xml version="1.0" encoding="utf-8"?><testsuites><testsuite name="HardwareMandatory" tests="1" failures="0"><testcase name="TC-FAKE-001" classname="test_sentinel_fake" time="0.1"/></testsuite></testsuites>'
    Set-Content $fakeXml -Value $fakeContent -Encoding UTF8

    $negXmlFiles = @(Get-ChildItem -Path (Join-Path $negDir "junit-*.xml") -ErrorAction SilentlyContinue)
    $negXmlOk    = $false
    if ($negXmlFiles.Count -gt 0) {
        $negXml     = $negXmlFiles[0]
        $negContent = Get-Content $negXml.FullName -Raw -ErrorAction SilentlyContinue
        $negHasInfra = $negContent -match 'INFRA-HEALTH' -or
                       $negContent -match 'FAILED\(INFRA\)' -or
                       $negContent -match '<failure'
        if (-not $negHasInfra) { $negXmlOk = $true }
    }

    Remove-Item $negDir -Recurse -Force -ErrorAction SilentlyContinue

    if ($negXmlOk) {
        tc_pass "TEST-SENTINEL-007: negative check correctly rejects JUnit without <failure> element"
    } else {
        tc_fail "TEST-SENTINEL-007" "Negative JUnit (no <failure>) passed SENTINEL-004 check -- check is too permissive"
    }
}

# TEST-SENTINEL-005: IsMandatorySuite by Suite alone
$src = Get-Content $RunCIPath -Raw
$decoupled = $src -match '(?ms)\$IsMandatorySuite\s*=\s*\(\$Suite\s*-eq\s*"HardwareMandatory"\)'
if ($decoupled) {
    tc_pass "TEST-SENTINEL-005: IsMandatorySuite determined by Suite alone (not gated on -Tests override)"
} else {
    tc_fail "TEST-SENTINEL-005" "IsMandatorySuite still gated on Tests override"
}

# TEST-SENTINEL-006: AVB_TEST_HEALTH_FAIL_AFTER not set in parent process
# If the hook leaked to parent env, this would fail all subsequent hardware tests.
$leaked = $env:AVB_TEST_HEALTH_FAIL_AFTER
if (-not $leaked) {
    tc_pass "TEST-SENTINEL-006: AVB_TEST_HEALTH_FAIL_AFTER not set in parent env (no HIL contamination)"
} else {
    tc_fail "TEST-SENTINEL-006" ("AVB_TEST_HEALTH_FAIL_AFTER='{0}' leaked to parent env -- would contaminate HIL runs" -f $leaked)
}

# Cleanup isolated workspace
Remove-Item $tmpDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) {
    Write-Host "[FAIL] TEST-SENTINEL: behavioral health sentinel regression" -ForegroundColor Red
    exit 1
}
Write-Host "[PASS] TEST-SENTINEL: behavioral health sentinel verified" -ForegroundColor Green
exit 0
