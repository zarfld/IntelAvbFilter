# test_health_sentinel.ps1
# Behavioral integration test for the #328 health sentinel.
# Invokes Run-Tests-CI.ps1 via Start-Process with AVB_TEST_HEALTH_FAIL_AFTER=*
# injecting a health failure without requiring a real driver or NIC.
# 
# TEST-SENTINEL-001  health failure after last passing test -> exit 1 (not 0)
# TEST-SENTINEL-002  INFRA failure marker in Run-Tests-CI.ps1 output
# TEST-SENTINEL-003  IsMandatorySuite set by Suite alone (not gated on -Tests override)
# No hardware or driver required.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
$RunCIPath = Join-Path $RepoRoot 'tools\test\Run-Tests-CI.ps1'

$total = 0; $passed = 0; $failed = 0
function tc_pass { param([string]$n) $script:total++; $script:passed++
    Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++
    Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red }

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  TEST-SENTINEL-001..003: #328 Behavioral Health Sentinel" -ForegroundColor Cyan
Write-Host "  Injects health failure via AVB_TEST_HEALTH_FAIL_AFTER=*" -ForegroundColor Cyan
Write-Host "  Executes actual Run-Tests-CI.ps1 control flow (subprocess)" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

if (-not (Test-Path $RunCIPath)) {
    tc_fail "TEST-SENTINEL-SETUP" ("Run-Tests-CI.ps1 not found at {0}" -f $RunCIPath)
    exit 1
}

# Create a temporary fake passing test script
$tmpDir = Join-Path $env:TEMP ("sentinel_test_{0}" -f (Get-Random))
New-Item -ItemType Directory -Path $tmpDir -Force | Out-Null
$fakePsTest = Join-Path $tmpDir "test_sentinel_fake.ps1"
"Write-Host '  [PASS] TC-FAKE-001: fake test always passes'; exit 0" | Set-Content $fakePsTest -Encoding UTF8

# Build a temporary wrapper script that calls Run-Tests-CI.ps1 with the correct -Tests parameter.
# This avoids shell quoting issues when passing arrays through psi.Arguments.
$wrapperScript = Join-Path $tmpDir "wrapper.ps1"
$fakePsTestFwd = $fakePsTest.Replace('\', '/')
$wrapperContent = @"
`$env:AVB_TEST_HEALTH_FAIL_AFTER = '*'
`$env:AVB_DRIVER_BUILD = 'Debug'
& '$RunCIPath' -Suite HardwareMandatory -Tests @('ps1:$fakePsTestFwd') -TestTimeoutSeconds 30 -Configuration Debug
exit `$LASTEXITCODE
"@
$wrapperContent | Set-Content $wrapperScript -Encoding UTF8

# Run the wrapper via Start-Process
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

Write-Host ""
Write-Host "--- Run-Tests-CI.ps1 output (first 40 lines) ---" -ForegroundColor DarkGray
($allOutput -split [Environment]::NewLine) | Select-Object -First 40 | ForEach-Object {
    Write-Host ("  {0}" -f $_) -ForegroundColor DarkGray
}
Write-Host ("--- exit code: {0} ---" -f $exitCode) -ForegroundColor DarkGray

# TEST-SENTINEL-001: exit must be 1
if ($exitCode -eq 1) {
    tc_pass "TEST-SENTINEL-001: exit=1 after injected health failure (RED->GREEN verified)"
} else {
    tc_fail "TEST-SENTINEL-001" ("exit={0}: expected 1; health failure did not produce non-zero exit" -f $exitCode)
}

# TEST-SENTINEL-002: INFRA marker must appear in output
$hasInfra = ($allOutput -match 'FAILED.INFRA.') -or
            ($allOutput -match 'INFRA-HEALTH') -or
            ($allOutput -match '328-SENTINEL') -or
            ($allOutput -match 'TEST-HOOK')
if ($hasInfra) {
    tc_pass "TEST-SENTINEL-002: INFRA failure marker in Run-Tests-CI.ps1 output"
} else {
    tc_fail "TEST-SENTINEL-002" "No FAILED(INFRA)/INFRA-HEALTH/#328-SENTINEL/TEST-HOOK in output"
}

# TEST-SENTINEL-003: source verification that IsMandatorySuite is by Suite alone
$src = Get-Content $RunCIPath -Raw
$decoupled = $src -match '(?ms)\$IsMandatorySuite\s*=\s*\(\$Suite\s*-eq\s*"HardwareMandatory"\)'
if ($decoupled) {
    tc_pass "TEST-SENTINEL-003: IsMandatorySuite determined by Suite alone (not gated on -Tests override)"
} else {
    tc_fail "TEST-SENTINEL-003" "IsMandatorySuite still gated on Tests override - decouple not applied"
}

Remove-Item $tmpDir -Recurse -Force -ErrorAction SilentlyContinue
[System.Environment]::SetEnvironmentVariable('AVB_TEST_HEALTH_FAIL_AFTER', '', 'Process')

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) {
    Write-Host "[FAIL] TEST-SENTINEL: behavioral health sentinel regression" -ForegroundColor Red
    exit 1
}
Write-Host "[PASS] TEST-SENTINEL: behavioral health sentinel verified" -ForegroundColor Green
exit 0
