# test_native_exit_codes.ps1
# TEST-NATIVE-EXIT-*: native and child-script exit codes survive PowerShell scope boundaries (offline; no driver).
#
# Root cause reproduced (FullSuite_20261021_095005.log): LocalSuites.ps1 assigned an unqualified `$LASTEXITCODE = $null`.
# Invoked with `& LocalSuites.ps1 *>&1 | Tee-Object` (its documented usage), that creates a script-scope variable which
# shadows the automatic variable for every script it calls (dynamic scoping); PowerShell writes native exit codes only
# to $global:LASTEXITCODE.  Build-Driver.ps1 then read $null after a successful MSBuild ("Exit code: " blank), vs_compile
# reported every compile as failed with a blank code, Build-Tests.ps1 lost the source revision, and `exit $null` could
# end a failed step with 0.  Contract enforced here: 0 = verified success, nonzero = failure, no observed exit code =
# failure; native exit codes are read from $global:LASTEXITCODE (tools/lib/NativeExit.ps1) everywhere in the chain.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
$LibPath   = Join-Path $RepoRoot 'tools\lib\NativeExit.ps1'
$VsCompile = Join-Path $RepoRoot 'tools\vs_compile.ps1'

$total = 0; $passed = 0; $failed = 0
function tc_pass { param([string]$n) $script:total++; $script:passed++; Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red }
function tc_check { param([string]$n, [bool]$ok, [string]$why) if ($ok) { tc_pass $n } else { tc_fail $n $why } }

Write-Host ""
Write-Host "TEST-NATIVE-EXIT: exit codes across nested PowerShell / native execution (offline)" -ForegroundColor Cyan

$Tmp = Join-Path $env:TEMP ("nativeexit_{0}" -f (Get-Random))
New-Item -ItemType Directory -Path $Tmp -Force | Out-Null

# Runs $Body as a script invoked exactly like LocalSuites.ps1 (`& script *>&1 | Tee-Object`) in a fresh powershell.exe.
function Invoke-Shadowed([string]$Name, [string]$Body, [hashtable]$Env = @{}) {
    $caller = Join-Path $Tmp "$Name.ps1"
    Set-Content -LiteralPath $caller -Value $Body -Encoding UTF8
    $teeLog = Join-Path $Tmp "$Name.tee.log"
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = 'powershell.exe'
    $psi.Arguments = "-NoProfile -NonInteractive -ExecutionPolicy Bypass -Command `"& '$caller' *>&1 | Tee-Object -FilePath '$teeLog'; exit `$global:LASTEXITCODE`""
    $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    foreach ($k in $Env.Keys) { $psi.EnvironmentVariables[$k] = [string]$Env[$k] }
    $p = [System.Diagnostics.Process]::Start($psi)
    $o = $p.StandardOutput.ReadToEndAsync(); $e = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit(180000)) { $p.Kill(); return @{ Exit = -999; Out = 'TIMEOUT' } }
    return @{ Exit = $p.ExitCode; Out = ($o.Result + "`n" + $e.Result) }
}
function Get-Result($r) { if ($r.Out -match 'NEC-RESULT=\[(-?\d*)\]') { return $Matches[1] } return '<missing>' }

# ---------------------------------------------------------------------------------------------------------
# 001: the shadowing mechanism itself (proves the harness reproduces the LocalSuites invocation shape)
# ---------------------------------------------------------------------------------------------------------
$inner = Join-Path $Tmp 'inner.ps1'
Set-Content -LiteralPath $inner -Encoding UTF8 -Value "cmd /c exit 7`r`n`"NEC-INNER=[`$LASTEXITCODE] GLOBAL=[`$global:LASTEXITCODE]`""
$r = Invoke-Shadowed 'mech' "`$LASTEXITCODE = `$null`r`n& '$inner'"
tc_check 'TEST-NATIVE-EXIT-001-harness-reproduces-unqualified-lastexitcode-shadowing' ($r.Out -match 'NEC-INNER=\[\] GLOBAL=\[7\]') ($r.Out -replace '\s+', ' ')

# ---------------------------------------------------------------------------------------------------------
# 002-005: the shared library contract
# ---------------------------------------------------------------------------------------------------------
if (-not (Test-Path -LiteralPath $LibPath)) {
    foreach ($id in '002-library-success-only-for-observed-zero', '003-library-failure-exit-code-never-zero', '004-library-reads-global-despite-caller-shadow', '005-library-reset-prevents-stale-codes') {
        tc_fail "TEST-NATIVE-EXIT-$id" "tools/lib/NativeExit.ps1 missing"
    }
} else {
    . $LibPath
    tc_check 'TEST-NATIVE-EXIT-002-library-success-only-for-observed-zero' `
        ((Test-AvbExitSuccess 0) -and -not (Test-AvbExitSuccess $null) -and -not (Test-AvbExitSuccess 3) -and -not (Test-AvbExitSuccess -1)) 'success must require an observed 0'
    tc_check 'TEST-NATIVE-EXIT-003-library-failure-exit-code-never-zero' `
        (((Get-AvbFailureExitCode $null) -eq 1) -and ((Get-AvbFailureExitCode 0) -eq 1) -and ((Get-AvbFailureExitCode 7) -eq 7) -and ((Get-AvbFailureExitCode -5) -eq -5) -and
         ((Format-AvbExitCode $null) -match 'none')) 'failure exit code must be nonzero; unknown must be printed as such'
    $r = Invoke-Shadowed 'lib' ("`$LASTEXITCODE = `$null`r`n. '$LibPath'`r`n& { Reset-AvbExitCode; cmd /c exit 9; `"NEC-RESULT=[`$(Get-AvbExitCode)]`" }")
    tc_check 'TEST-NATIVE-EXIT-004-library-reads-global-despite-caller-shadow' ((Get-Result $r) -eq '9') ($r.Out -replace '\s+', ' ')
    $r = Invoke-Shadowed 'reset' ("cmd /c exit 4`r`n. '$LibPath'`r`nReset-AvbExitCode`r`n`$x = Get-Process -Id `$PID`r`n`"NEC-RESULT=[`$(Get-AvbExitCode)]`"")
    tc_check 'TEST-NATIVE-EXIT-005-library-reset-prevents-stale-codes' ((Get-Result $r) -eq '') ("stale code read after reset: " + ($r.Out -replace '\s+', ' '))
}

# ---------------------------------------------------------------------------------------------------------
# 006-008: the real tools\vs_compile.ps1 called through the LocalSuites shape (3 nested PowerShell levels + native)
# VSCMD_VER skips the VS dev-shell initialisation; the commands are plain cmd.exe, so no compiler is needed.
# ---------------------------------------------------------------------------------------------------------
$vsEnv = @{ VSCMD_VER = 'native-exit-test' }
$r0 = Invoke-Shadowed 'vs_ok' ("`$LASTEXITCODE = `$null`r`n& '$VsCompile' -BuildCmd 'cmd /c exit 0'`r`n`"NEC-RESULT=[`$global:LASTEXITCODE]`"") $vsEnv
tc_check 'TEST-NATIVE-EXIT-006-successful-native-build-reports-zero-through-nested-scripts' `
    (((Get-Result $r0) -eq '0') -and ($r0.Out -notmatch 'Command failed')) ($r0.Out -replace '\s+', ' ')
$r7 = Invoke-Shadowed 'vs_fail' ("`$LASTEXITCODE = `$null`r`n& '$VsCompile' -BuildCmd 'cmd /c exit 7'`r`n`"NEC-RESULT=[`$global:LASTEXITCODE]`"") $vsEnv
tc_check 'TEST-NATIVE-EXIT-007-failing-native-build-preserves-exact-nonzero-code' `
    (((Get-Result $r7) -eq '7') -and ($r7.Out -match 'Command failed with exit code 7\b')) ($r7.Out -replace '\s+', ' ')
$rNone = Invoke-Shadowed 'vs_none' ("`$LASTEXITCODE = `$null`r`n& '$VsCompile' -BuildCmd 'Write-Output no-native-command-ran'`r`n`"NEC-RESULT=[`$global:LASTEXITCODE]`"") $vsEnv
tc_check 'TEST-NATIVE-EXIT-008-missing-native-exit-code-is-failure-not-success' `
    (((Get-Result $rNone) -notin '0', '', '<missing>') -and ($rNone.Out -match 'no exit code observed')) ($rNone.Out -replace '\s+', ' ')

# ---------------------------------------------------------------------------------------------------------
# 009: vs_compile temp files are unique per invocation (no collision between concurrent / stale compiles)
# ---------------------------------------------------------------------------------------------------------
$vsText = [System.IO.File]::ReadAllText($VsCompile)
tc_check 'TEST-NATIVE-EXIT-009-vs-compile-temp-files-unique-per-invocation' `
    (($vsText -notmatch "Join-Path \`$env:TEMP 'vc\.(ps1|cmd)'") -and ($vsText -match 'GetRandomFileName|NewGuid')) 'fixed %TEMP%\vc.ps1 / vc.cmd paths'

# ---------------------------------------------------------------------------------------------------------
# 010-011: static -- no script in the build/install/test chain reads the unqualified, shadowable $LASTEXITCODE,
# assigns it unqualified, or ends with an exit code that may be $null.
# ---------------------------------------------------------------------------------------------------------
$chain = @('tools\archive\deprecated\LocalSuites.ps1', 'tools\build\Build-Driver.ps1', 'tools\build\Build-Tests.ps1', 'tools\vs_compile.ps1',
           'tools\setup\Install-Driver.ps1', 'tools\setup\Install-Driver-Elevated.ps1', 'tools\test\Run-Tests-CI.ps1', 'tools\test\Run-Tests-Elevated.ps1')
$unq = @(); $nullExit = @()
foreach ($rel in $chain) {
    $path = Join-Path $RepoRoot $rel
    if (-not (Test-Path -LiteralPath $path)) { $unq += "${rel}: missing"; continue }
    $tk = $null; $pe = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile($path, [ref]$tk, [ref]$pe)
    foreach ($v in $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.VariableExpressionAst] -and $x.VariablePath.UserPath -ieq 'LASTEXITCODE' }, $true)) {
        $unq += ("{0}:{1}" -f $rel, $v.Extent.StartLineNumber)
    }
    foreach ($x in $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.ExitStatementAst] -and $null -ne $x.Pipeline }, $true)) {
        $t = $x.Pipeline.Extent.Text
        if ($t -match '^\$(global:)?LASTEXITCODE$|^\$ec$|^\$null$') { $nullExit += ("{0}:{1} exit {2}" -f $rel, $x.Extent.StartLineNumber, $t) }
    }
    # Generated child scripts (here-strings, single-quoted lines, escaped `$) must read the global variable too.
    foreach ($s in $ast.FindAll({ param($x) ($x -is [System.Management.Automation.Language.ExpandableStringExpressionAst] -or $x -is [System.Management.Automation.Language.StringConstantExpressionAst]) -and
                                  $x.Parent -isnot [System.Management.Automation.Language.CommandAst] -or $x -is [System.Management.Automation.Language.StringConstantExpressionAst] }, $true)) {
        if ($s.Extent.Text -match '(?<![\w:])`?\$LASTEXITCODE\b') { $unq += ("{0}:{1} (generated script)" -f $rel, $s.Extent.StartLineNumber) }
    }
}
tc_check 'TEST-NATIVE-EXIT-010-no-unqualified-lastexitcode-in-the-build-install-test-chain' ($unq.Count -eq 0) ("{0} site(s): {1}" -f $unq.Count, (($unq | Select-Object -First 25) -join ', '))
tc_check 'TEST-NATIVE-EXIT-011-no-exit-with-a-possibly-null-code' ($nullExit.Count -eq 0) ($nullExit -join ', ')

Remove-Item $Tmp -Recurse -Force -ErrorAction SilentlyContinue
Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-NATIVE-EXIT: exit codes are lost or misreported across script boundaries" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-NATIVE-EXIT: exit codes survive nested PowerShell / native execution" -ForegroundColor Green
exit 0
