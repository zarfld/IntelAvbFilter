# Convert-Logs-To-JUnit.ps1
#
# Converts local hardware test log files to JUnit XML for CI reporting.
# This allows local test results (requiring hardware + installed driver) to be
# committed as "test evidence" and surfaced in GitHub Actions test result UI.
#
# Usage:
#   .\Convert-Logs-To-JUnit.ps1                       # latest run, output to test-evidence/
#   .\Convert-Logs-To-JUnit.ps1 -RunDate "20260323"   # specific date
#   .\Convert-Logs-To-JUnit.ps1 -OutputFile "out.xml" # custom output path
#
# Implements: #265 (TEST-COVERAGE-001), #244 (TEST-BUILD-CI-001)
# Traces to:  #98 (REQ-NF-BUILD-CI-001)

param(
    [Parameter(Mandatory=$false)]
    [string]$LogsDir = "",

    [Parameter(Mandatory=$false)]
    [string]$RunDate = "",       # YYYYMMDD -- default: most recent date found

    [Parameter(Mandatory=$false)]
    [string]$OutputFile = "",    # default: test-evidence/hardware-results-YYYYMMDD.xml

    # When -OutputFile is explicitly supplied (isolated/sentinel runs) the caller
    # is responsible for its own output destination; skip writing the shared
    # hardware-results-latest.xml and traceability-summary-*.md so that
    # behavioral test runs cannot corrupt production HIL evidence.
    # Set this switch explicitly to force evidence-dir writes even when -OutputFile is set.
    [Parameter(Mandatory=$false)]
    [switch]$ForceEvidenceDir,

    [Parameter(Mandatory=$false)]
    [switch]$ShowDetails,

    # Build configuration / suite the logs were produced under; recorded as <property>
    # elements so Debug and Release results stay distinguishable in the JUnit evidence.
    [Parameter(Mandatory=$false)]
    [string]$Configuration = "",

    [Parameter(Mandatory=$false)]
    # NOTE: not named $Suite - PowerShell variables are case-insensitive and this script
    # uses $suite as a loop variable for parsed log suites.
    [string]$SuiteName = "",

    # Verification is MANDATORY unless this switch is given (fail-closed default):
    # INCONCLUSIVE -> <failure/>.  With the switch it keeps its label as <skipped/>.
    # SKIP_NOT_IMPLEMENTED is a labelled <skipped/> in both modes; the suite runner
    # decides whether it makes a mandatory run incomplete.  Used for HardwareMutation only.
    [Parameter(Mandatory=$false)]
    [switch]$OptionalVerification
)

$ErrorActionPreference = 'Stop'

# Canonical verdict vocabulary shared with Parse-TestResults.ps1 and Run-Tests-CI.ps1.
. (Join-Path $PSScriptRoot 'TestVerdict.ps1')

# ------------------------------------------------------------------
# Paths
# ------------------------------------------------------------------
$scriptDir   = $PSScriptRoot
$repoRoot    = Split-Path (Split-Path $scriptDir -Parent) -Parent
$logsPath    = if ($LogsDir) { $LogsDir } else { Join-Path $repoRoot "logs" }
$evidenceDir = Join-Path $repoRoot "test-evidence"

# Write shared evidence-dir files (hardware-results-latest.xml, traceability MD)
# only when NOT running in isolated/sentinel mode.
# Isolated mode: -OutputFile is explicitly supplied and -ForceEvidenceDir is absent.
$writeEvidenceDir = $ForceEvidenceDir -or (-not $OutputFile)

if (-not (Test-Path $logsPath)) {
    Write-Error "Logs directory not found: $logsPath"
    exit 1
}

# ------------------------------------------------------------------
# Discover available test log files (individual per-test logs)
# Format: <testname>.exe_<YYYYMMDD>_<HHMMSS>.log
# ------------------------------------------------------------------
$allTestLogs = Get-ChildItem $logsPath -Filter "*.exe_*.log" |
    Where-Object { $_.Name -notmatch "^full_test_suite|^test_logs" } |
    Sort-Object Name

if ($allTestLogs.Count -eq 0) {
    Write-Warning "No test log files found in: $logsPath"
    exit 0
}

# Derive dates from filenames: testname.exe_YYYYMMDD_HHMMSS.log
$availableDates = $allTestLogs |
    ForEach-Object { if ($_.Name -match '\.exe_(\d{8})_\d{6}\.log$') { $Matches[1] } } |
    Sort-Object -Unique

if ($RunDate -eq "") {
    $RunDate = $availableDates | Select-Object -Last 1
    Write-Host "[INFO] Using most recent run date: $RunDate" -ForegroundColor Cyan
} elseif ($availableDates -notcontains $RunDate) {
    Write-Error "No test logs found for date: $RunDate. Available: $($availableDates -join ', ')"
    exit 1
}

# Filter to the target date -- take the LAST (most recent) log per test binary
$runLogs = $allTestLogs |
    Where-Object { $_.Name -match "\.exe_${RunDate}_\d{6}\.log$" } |
    Sort-Object LastWriteTime

# When multiple runs exist for same test on same date, keep the latest
$latestByTest = @{}
foreach ($log in $runLogs) {
    if ($log.Name -match '^(.+\.exe)_\d{8}_\d{6}\.log$') {
        $latestByTest[$Matches[1]] = $log
    }
}
$targetLogs = $latestByTest.Values | Sort-Object Name

Write-Host "[INFO] Found $($targetLogs.Count) test log(s) for date $RunDate" -ForegroundColor Cyan

# ------------------------------------------------------------------
# Also find the full suite log for metadata
# ------------------------------------------------------------------
$machineName = $env:COMPUTERNAME
$osVersion   = (Get-CimInstance Win32_OperatingSystem -ErrorAction SilentlyContinue).Version
$timestamp   = Get-Date -Format "yyyy-MM-ddTHH:mm:ss"

$fullSuiteLog = Get-ChildItem $logsPath -Filter "full_test_suite_${RunDate}_*.log" -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime | Select-Object -Last 1

if ($fullSuiteLog) {
    $hdrLines = Get-Content $fullSuiteLog.FullName -TotalCount 20
    foreach ($line in $hdrLines) {
        if ($line -match 'Machine:\s+(.+)') { $machineName = $Matches[1].Trim(); break }
    }
}

# ------------------------------------------------------------------
# XML escape helper (ASCII-safe -- avoids literal & in source)
# ------------------------------------------------------------------
function EscapeXml {
    param([string]$s)
    $s = $s.Replace('&',  'and')
    $s = $s.Replace('<',  '[')
    $s = $s.Replace('>',  ']')
    $s = $s.Replace('"',  "'")
    return $s
}

# ------------------------------------------------------------------
# Extract Verifies/Implements issue refs from log lines.
# Returns hashtable { VerifiesNums, ImplementsNums } — lists of issue-number strings.
# Supports formats:
#   "Verifies: #64 (REQ-F-...)"     "Implements: #273 (...)"
#   "Issue: #273 (...)"              "Requirement: #64 (...)"
#   "║ Verifies: #3   ║"             "Issue #238 | Verifies: #149 ..."
# ------------------------------------------------------------------
function Get-TraceabilityFromLines {
    param([string[]]$Lines)
    $verifies   = [System.Collections.Generic.List[string]]::new()
    $implements = [System.Collections.Generic.List[string]]::new()
    foreach ($line in $Lines) {
        # Strip box-drawing chars from line boundaries
        $stripped = $line.Trim() -replace '^[║│┃=─\-]+\s*', '' -replace '\s*[║│┃=─\-]+$', ''
        # Split on pipe separators to prevent cross-contamination
        foreach ($seg in ($stripped -split '\|')) {
            $s = $seg.Trim()
            # Verifies / Requirement keyword → requirement issue refs
            if ($s -match '\b(?:Verifies?|Requirement)[:\s]') {
                [regex]::Matches($s, '#(\d+)') | ForEach-Object {
                    $n = $_.Groups[1].Value
                    if (-not $verifies.Contains($n)) { $verifies.Add($n) }
                }
            }
            # Implements / Issue keyword → test/implementation issue refs
            if ($s -match '\bImplements?\s*[:#]|\bImplements\s+#|\bIssue\s*[:#]\s*#|\bIssue\s+#\d') {
                [regex]::Matches($s, '#(\d+)') | ForEach-Object {
                    $n = $_.Groups[1].Value
                    if (-not $implements.Contains($n)) { $implements.Add($n) }
                }
            }
        }
    }
    return @{ VerifiesNums = $verifies; ImplementsNums = $implements }
}

# ------------------------------------------------------------------
# Parse a single test log file
# ------------------------------------------------------------------
function Parse-TestLog {
    param([System.IO.FileInfo]$LogFile)

    $lines = Get-Content $LogFile.FullName -ErrorAction SilentlyContinue
    if (-not $lines) { return $null }

    $testName = if ($LogFile.Name -match '^(.+)\.exe_\d{8}_\d{6}\.log$') { $Matches[1] } else { $LogFile.BaseName }

    # --- Extract individual named test cases ---
    # Patterns observed in logs:
    #   "[PASS] TC-HAL-001: description"
    #   "[FAIL] TC-HAL-001: description"
    #   "[SKIP] TC-HAL-001: description"
    #   "PASS: TC-SCRIPTS-001: description"
    #   "FAIL: TC-MAGIC-001: description"
    #   "[PASS] [Adapter0[VEN_8086:DEV_125B]] TC-LCY-001: description"  (adapter-prefixed)
    #   "[FAIL] [Adapter0[VEN_8086:DEV_125B]] TC-LCY-003: description"  (adapter-prefixed)
    #   "[SKIP] [Adapter0[VEN_8086:DEV_125B]] TC-LCY-006: description"  (adapter-prefixed)
    # The adapter prefix is included in the case name so each adapter×TC combination
    # is unique in the JUnit XML, giving correct per-adapter pass/fail granularity.

    $cases     = [System.Collections.Generic.List[hashtable]]::new()
    $caseNames = [System.Collections.Generic.HashSet[string]]::new()

    # Optional adapter prefix pattern: [AdapterN[VEN_XXXX:DEV_YYYY]]
    $adapterPrefix = '(?:\[Adapter\d+\[[^\]]*\]\]\s+)?'

    $caseBody = "($adapterPrefix(?:TC|IT|VV|UT|REG|TEST)-[A-Z0-9_-]+.*)"
    # Legacy text form used by existing C tests:  [SKIP] TC-X/adapter:0: SKIP_UNSUPPORTED: reason
    $legacyRx = "^\[SKIP\]\s+$adapterPrefix(?:TC|IT|VV|UT|REG|TEST)-[A-Z0-9_-]+(?:/adapter:\d+)?\s*:?\s*(SKIP_UNSUPPORTED|SKIP_NOT_IMPLEMENTED)\b"

    foreach ($line in $lines) {
        $stripped = $line.Trim()

        # Labelled non-pass/fail verdicts (shared vocabulary: tools/test/TestVerdict.ps1).
        # Before this branch existed the converter silently ignored [SKIP_UNSUPPORTED].
        $labelled = $null; $labelledBody = $null
        if ($stripped -match "^\[(SKIP_UNSUPPORTED|SKIP_NOT_IMPLEMENTED|INCONCLUSIVE)\]\s+$caseBody") {
            $labelled = $Matches[1]; $labelledBody = $Matches[2]
        } elseif ($stripped -match $legacyRx) {
            $labelled = $Matches[1]
            [void]($stripped -match "^\[SKIP\]\s+$caseBody")
            $labelledBody = $Matches[1]
        }

        if ($labelled) {
            $caseName = ($labelledBody -replace '\s*\(.*\)\s*$', '').Trim()
            $class    = Get-AvbVerdictClass -Verdict $labelled -Optional:$OptionalVerification
            if ($caseNames.Add($caseName)) {
                # The exact verdict stays in the message; testcase id, adapter and any
                # ntstatus=/did= evidence printed by the test are part of the body text.
                $cases.Add(@{ Name = $caseName; Verdict = $labelled
                              Failed  = ($class -eq 'failure'); Skipped = ($class -eq 'skipped')
                              FailMsg = "[$labelled] $labelledBody" })
            }
        } elseif ($stripped -match "^\[PASS\]\s+($adapterPrefix(?:TC|IT|VV|UT|REG|TEST)-[A-Z0-9_-]+.*)" -or
            $stripped -match '^PASS:\s+((?:TC|IT|VV|UT|REG|TEST)-[A-Z0-9_-]+.*)') {
            $caseName = ($Matches[1] -replace '\s*\(.*\)\s*$', '').Trim()
            if ($caseNames.Add($caseName)) {
                $cases.Add(@{ Name = $caseName; Verdict = 'PASS'; Failed = $false; Skipped = $false; FailMsg = '' })
            }
        } elseif ($stripped -match "^\[FAIL\]\s+($adapterPrefix(?:TC|IT|VV|UT|REG|TEST)-[A-Z0-9_-]+.*)" -or
                  $stripped -match '^FAIL:\s+((?:TC|IT|VV|UT|REG|TEST)-[A-Z0-9_-]+.*)') {
            $caseName = ($Matches[1] -replace '\s*\(.*\)\s*$', '').Trim()
            if ($caseNames.Add($caseName)) {
                $cases.Add(@{ Name = $caseName; Verdict = 'FAIL'; Failed = $true; Skipped = $false; FailMsg = $stripped })
            }
        } elseif ($stripped -match "^\[(INFRA_FAILURE|BLOCKED|CLEANUP_FAILED)\]\s+($adapterPrefix(?:TC|IT|VV|UT|REG|TEST)-[A-Z0-9_-]+.*)") {
            # INFRA_FAILURE, BLOCKED, and CLEANUP_FAILED are all hard failures in JUnit.
            # They must never appear as <skipped/> — doing so masks infrastructure breakage.
            $infStatus = $Matches[1].Trim()
            $caseName  = ($Matches[2] -replace '\s*\(.*\)\s*$', '').Trim()
            if ($caseNames.Add($caseName)) {
                $cases.Add(@{ Name = $caseName; Verdict = $infStatus; Failed = $true; Skipped = $false;
                              FailMsg = "[$infStatus] $($stripped -replace '^\[' + $infStatus + '\]\s*', '')" })
            }
        } elseif ($stripped -match "^\[SKIP\]\s+($adapterPrefix(?:TC|IT|VV|UT|REG|TEST)-[A-Z0-9_-]+.*)") {
            $caseName = ($Matches[1] -replace '\s*\(.*\)\s*$', '').Trim()
            if ($caseNames.Add($caseName)) {
                $cases.Add(@{ Name = $caseName; Verdict = 'SKIP'; Failed = $false; Skipped = $true; FailMsg = '' })
            }
        }
    }

    # --- Extract overall summary totals ---
    $totalTests = 0; $passedTests = 0; $failedTests = 0; $skippedTests = 0
    $allText = $lines -join "`n"

    # Form 1: "Total: 17  Passed: 11  Failed: 0  Skipped: 6"  (all on one line)
    if ($allText -match '(?m)Total:\s*(\d+)\s+Passed:\s*(\d+)\s+Failed:\s*(\d+)') {
        $totalTests   = [int]$Matches[1]
        $passedTests  = [int]$Matches[2]
        $failedTests  = [int]$Matches[3]
        if ($allText -match '(?m)Skipped:\s*(\d+)') { $skippedTests = [int]$Matches[1] }
    }
    # Form 2: multiline "Total:  345\n  Passed: 345\n  Failed: 0"
    if ($totalTests -eq 0 -and $allText -match '(?m)^\s*Total:\s*(\d+)') {
        $totalTests   = [int]$Matches[1]
        if ($allText -match '(?m)^\s*Passed:\s*(\d+)')  { $passedTests  = [int]$Matches[1] }
        if ($allText -match '(?m)^\s*Failed:\s*(\d+)')  { $failedTests  = [int]$Matches[1] }
        if ($allText -match '(?m)^\s*Skipped:\s*(\d+)') { $skippedTests = [int]$Matches[1] }
    }
    # Form 3: "Tests Passed: X" / "Tests Failed: Y"
    if ($totalTests -eq 0 -and $allText -match 'Tests Passed:\s*(\d+)') {
        $passedTests  = [int]$Matches[1]
        if ($allText -match 'Tests Failed:\s*(\d+)') { $failedTests = [int]$Matches[1] }
        $totalTests   = $passedTests + $failedTests
    }

    # --- Build final test case list ---
    $testCases = [System.Collections.Generic.List[hashtable]]::new()

    if ($cases.Count -gt 0) {
        foreach ($c in $cases) { $testCases.Add($c) }
    } else {
        # No named test cases -- emit one synthetic entry from the summary
        $overallFailed  = ($failedTests -gt 0) -or ($allText -match '(?i)TESTS?\s+FAILED(?![:\s]*\d)')
        $overallSkipped = ($totalTests -eq 0 -and $passedTests -eq 0 -and $failedTests -eq 0)
        $msg = if ($overallFailed) { "$failedTests test(s) failed - see $($LogFile.Name)" } else { '' }
        $testCases.Add(@{
            Name    = "${testName} (overall)"
            Verdict = if ($overallFailed) { 'FAIL' } elseif ($overallSkipped) { 'SKIP' } else { 'PASS' }
            Failed  = $overallFailed
            Skipped = $overallSkipped
            FailMsg = $msg
        })
    }

    # Extract Verifies/Implements refs from the log content
    $tracing = Get-TraceabilityFromLines -Lines $lines

    return @{
        Name           = $testName
        Total          = [Math]::Max($totalTests, $testCases.Count)
        Passed         = $passedTests
        Failed         = $failedTests
        Skipped        = $skippedTests
        LogFile        = $LogFile.Name
        TestCases      = $testCases
        RunTime        = [Math]::Max(0, (($LogFile.LastWriteTime) - ($LogFile.CreationTime)).TotalSeconds)
        VerifiesNums   = $tracing.VerifiesNums
        ImplementsNums = $tracing.ImplementsNums
    }
}

# ------------------------------------------------------------------
# Parse all target logs
# ------------------------------------------------------------------
Write-Host "[INFO] Parsing $($targetLogs.Count) test logs..." -ForegroundColor Cyan

$suites    = [System.Collections.Generic.List[hashtable]]::new()
$totalPass = 0; $totalFail = 0; $totalSkip = 0

foreach ($log in $targetLogs) {
    $suite = Parse-TestLog -LogFile $log
    if ($suite) {
        $suites.Add($suite)
        $totalPass += $suite.Passed
        $totalFail += $suite.Failed
        $totalSkip += $suite.Skipped
        if ($ShowDetails) {
            # Use actual case counts (not summary-line counts, which fail for TC-* style logs)
            $tcTotal = $suite.TestCases.Count
            $tcPass  = @($suite.TestCases | Where-Object { -not $_.Failed -and -not $_.Skipped }).Count
            $tcFail  = @($suite.TestCases | Where-Object { $_.Failed }).Count
            $st  = if ($tcFail -gt 0) { "[FAIL]" } else { "[PASS]" }
            $clr = if ($tcFail -gt 0) { "Red" } else { "Green" }
            Write-Host "  $st $($suite.Name): $tcPass/$tcTotal passed  [$tcTotal cases]" -ForegroundColor $clr
        }
    }
}

$totalRun = $totalPass + $totalFail

# ------------------------------------------------------------------
# Build JUnit XML
# ------------------------------------------------------------------
$sb = [System.Text.StringBuilder]::new()
[void]$sb.AppendLine('<?xml version="1.0" encoding="UTF-8"?>')
[void]$sb.AppendLine("<!-- Generated: $timestamp -->")
[void]$sb.AppendLine("<!-- Machine: $machineName | Run date: $RunDate | OS: $osVersion -->")
[void]$sb.AppendLine("<!-- NOTE: Tests require Intel NIC hardware and installed driver -->")

# Traceability source of truth lives in the test files themselves.
# Each test file prints "Verifies: #N" / "Implements: #N" lines which are
# captured in the log and parsed by Get-TraceabilityFromLines (above).
# No external mapping file is used or needed.
$ghBase = 'https://github.com/zarfld/IntelAvbFilter/issues/'

# Compute accurate root-level totals from parsed test cases (summary-line counts
# are unreliable when logs use TC-* style markers without a 'Total:' summary line)
$total   = ($suites | ForEach-Object { $_.TestCases.Count } | Measure-Object -Sum).Sum
$fail    = ($suites | ForEach-Object { @($_.TestCases | Where-Object { $_.Failed  }).Count } | Measure-Object -Sum).Sum
$skipped = ($suites | ForEach-Object { @($_.TestCases | Where-Object { $_.Skipped }).Count } | Measure-Object -Sum).Sum
[void]$sb.AppendLine('<testsuites name="IntelAvbFilter Hardware Tests ' + $RunDate + '" tests="' + $total + '" failures="' + $fail + '" skipped="' + $skipped + '" time="0">')

foreach ($suite in $suites) {
    $tcCount = $suite.TestCases.Count
    $tcFail  = @($suite.TestCases | Where-Object { $_.Failed }).Count
    $tcSkip  = @($suite.TestCases | Where-Object { $_.Skipped }).Count
    $rt      = [Math]::Round($suite.RunTime, 2)
    $sn      = EscapeXml $suite.Name

    [void]$sb.AppendLine('  <testsuite name="' + $sn + '" tests="' + $tcCount + '" failures="' + $tcFail + '" skipped="' + $tcSkip + '" errors="0" time="' + $rt + '" hostname="' + $machineName + '">')
    [void]$sb.AppendLine('    <properties>')
    [void]$sb.AppendLine('      <property name="log_file" value="' + (EscapeXml $suite.LogFile) + '"/>')
    [void]$sb.AppendLine('      <property name="run_date" value="' + $RunDate + '"/>')
    if ($Configuration) { [void]$sb.AppendLine('      <property name="configuration" value="' + (EscapeXml $Configuration) + '"/>') }
    if ($SuiteName)     { [void]$sb.AppendLine('      <property name="suite" value="' + (EscapeXml $SuiteName) + '"/>') }
    [void]$sb.AppendLine('      <property name="verification" value="' + $(if ($OptionalVerification) { 'optional' } else { 'mandatory' }) + '"/>')
    foreach ($n in $suite.VerifiesNums) {
        [void]$sb.AppendLine('      <property name="verifies" value="' + $ghBase + $n + '"/>')
    }
    foreach ($n in $suite.ImplementsNums) {
        [void]$sb.AppendLine('      <property name="implements" value="' + $ghBase + $n + '"/>')
    }
    [void]$sb.AppendLine('    </properties>')

    foreach ($tc in $suite.TestCases) {
        $cn = EscapeXml $tc.Name
        $cl = EscapeXml $suite.Name
        # Inherit requirement/test issue refs from the suite's parsed Verifies:/Implements: log lines.
        # The test file is the SSOT — no external mapping file.
        $hasProps = ($suite.VerifiesNums.Count -gt 0) -or ($suite.ImplementsNums.Count -gt 0)
        $open = '    <testcase name="' + $cn + '" classname="' + $cl + '" time="0"'

        # Labelled skips (SKIP_UNSUPPORTED / SKIP_NOT_IMPLEMENTED / optional INCONCLUSIVE) keep
        # their exact verdict + evidence text; plain SKIP stays a bare <skipped/>.
        $skipEl = if ($tc.Skipped -and $tc.FailMsg) { '<skipped message="' + (EscapeXml $tc.FailMsg) + '"/>' } else { '<skipped/>' }
        $failTy = if ($tc.ContainsKey('Verdict') -and $tc.Verdict) { ' type="' + (EscapeXml $tc.Verdict) + '"' } else { '' }

        if (-not $hasProps) {
            if ($tc.Skipped) {
                [void]$sb.AppendLine($open + '>' + $skipEl + '</testcase>')
            } elseif ($tc.Failed) {
                $msg = EscapeXml $tc.FailMsg
                [void]$sb.AppendLine($open + '><failure' + $failTy + ' message="' + $msg + '">' + $msg + '</failure></testcase>')
            } else {
                [void]$sb.AppendLine($open + '/>')
            }
        } else {
            [void]$sb.AppendLine($open + '>')
            [void]$sb.AppendLine('      <properties>')
            foreach ($n in $suite.VerifiesNums)   { [void]$sb.AppendLine('        <property name="verifies"   value="' + $ghBase + $n + '"/>') }
            foreach ($n in $suite.ImplementsNums) { [void]$sb.AppendLine('        <property name="implements" value="' + $ghBase + $n + '"/>') }
            [void]$sb.AppendLine('      </properties>')
            if ($tc.Skipped) {
                [void]$sb.AppendLine('      ' + $skipEl)
            } elseif ($tc.Failed) {
                $msg = EscapeXml $tc.FailMsg
                [void]$sb.AppendLine('      <failure' + $failTy + ' message="' + $msg + '">' + $msg + '</failure>')
            }
            [void]$sb.AppendLine('    </testcase>')
        }
    }
    [void]$sb.AppendLine('  </testsuite>')
}
[void]$sb.AppendLine('</testsuites>')

# ------------------------------------------------------------------
# Write output files
# ------------------------------------------------------------------

$outPath = if ($OutputFile) {
    $OutputFile
} else {
    if (-not (Test-Path $evidenceDir)) { New-Item -ItemType Directory -Path $evidenceDir -Force | Out-Null }
    Join-Path $evidenceDir "hardware-results-${RunDate}.xml"
}

$outDir = Split-Path $outPath -Parent
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }
$sb.ToString() | Set-Content -Path $outPath -Encoding UTF8

# Shared evidence-dir writes: hardware-results-latest.xml + traceability MD.
# Skipped when running in isolated/sentinel mode (-OutputFile supplied without -ForceEvidenceDir)
# to prevent behavioral test runs from corrupting production HIL evidence.
if ($writeEvidenceDir) {
    if (-not (Test-Path $evidenceDir)) { New-Item -ItemType Directory -Path $evidenceDir -Force | Out-Null }
    $latestPath = Join-Path $evidenceDir "hardware-results-latest.xml"
    Copy-Item $outPath $latestPath -Force
} else {
    $latestPath = $outPath   # point to isolated file for summary line
}

# ------------------------------------------------------------------
# Generate Markdown traceability summary
# Requirement-centric: one row per (requirement × test-suite) pair.
# PASS / FAIL / SKIP are explicit — skipped is never assumed pass or fail.
# Source of truth: Verifies:/Implements: lines printed by each test file.
# ------------------------------------------------------------------

$reqMap = [System.Collections.Generic.Dictionary[string, System.Collections.Generic.List[hashtable]]]::new()
foreach ($suite in $suites) {
    # Matrix tallies: FAIL = genuine failure classes only; SKIP = everything that is neither a
    # pass nor a failure, which includes SKIP_NOT_IMPLEMENTED and INCONCLUSIVE (whatever JUnit
    # class INCONCLUSIVE got), so no reader can mistake them for a pure pass.
    $tcPass = @($suite.TestCases | Where-Object { -not $_.Failed -and -not $_.Skipped }).Count
    $tcFail = @($suite.TestCases | Where-Object { $_.Failed -and $_.Verdict -ne 'INCONCLUSIVE' }).Count
    $tcSkip = @($suite.TestCases | Where-Object { $_.Skipped -or $_.Verdict -eq 'INCONCLUSIVE' }).Count
    # Exact-verdict subsets of SKIP.
    $tcNimpl = @($suite.TestCases | Where-Object { $_.Verdict -eq 'SKIP_NOT_IMPLEMENTED' }).Count
    $tcIncon = @($suite.TestCases | Where-Object { $_.Verdict -eq 'INCONCLUSIVE' }).Count
    foreach ($n in $suite.VerifiesNums) {
        if (-not $reqMap.ContainsKey($n)) {
            $reqMap[$n] = [System.Collections.Generic.List[hashtable]]::new()
        }
        $reqMap[$n].Add(@{ Suite = $suite.Name; Implements = ($suite.ImplementsNums -join ', '); Pass = $tcPass; Fail = $tcFail; Skip = $tcSkip; NotImpl = $tcNimpl; Inconcl = $tcIncon })
    }
}

$mdLines = [System.Collections.Generic.List[string]]::new()
$mdLines.Add("## Test Traceability Matrix - $RunDate")
$mdLines.Add("")
$mdLines.Add('> **Source of truth**: `Verifies: #N` / `Implements: #N` statements printed by each test file.')
$mdLines.Add('> SKIP is shown explicitly - skipped test cases are never assumed to pass or fail.')
$mdLines.Add('> FAIL = genuine failures. SKIP = neither pass nor failure, and includes NOT_IMPL and INCONCL. NOT_IMPL (SKIP_NOT_IMPLEMENTED)')
$mdLines.Add('> marks required work that is missing: the requirement is **incomplete**, not verified. INCONCL (INCONCLUSIVE) means the')
$mdLines.Add('> evidence was insufficient (a JUnit failure when mandatory); it is likewise not verified.')
$mdLines.Add("> A requirement may be covered by multiple test suites; each suite appears as a separate row.")
$mdLines.Add("")

if ($reqMap.Count -gt 0) {
    $mdLines.Add("| Requirement | Test Suite | Test Issue | :white_check_mark: PASS | :x: FAIL | :next_track_button: SKIP | NOT_IMPL | INCONCL |")
    $mdLines.Add("|-------------|------------|:----------:|:-----------------------:|:--------:|:------------------------:|:--------:|:-------:|")
    foreach ($reqNum in ($reqMap.Keys | Sort-Object { [int]$_ })) {
        $reqLink = "[#$reqNum]($ghBase$reqNum)"
        foreach ($entry in $reqMap[$reqNum]) {
            $implLinks = if ($entry.Implements) {
                ($entry.Implements -split ',\s*' | ForEach-Object { "[#$_]($ghBase$_)" }) -join ' '
            } else { '—' }
            $mdLines.Add("| $reqLink | $($entry.Suite) | $implLinks | $($entry.Pass) | $($entry.Fail) | $($entry.Skip) | $($entry.NotImpl) | $($entry.Inconcl) |")
        }
    }
} else {
    $mdLines.Add('*No `Verifies: #N` lines found in any test log.*')
    $mdLines.Add("")
    $mdLines.Add('Add `printf("  Verifies: #N (REQ-...)")` to your test `main()` to enable this table.')
}

$unlinked = @($suites | Where-Object { $_.VerifiesNums.Count -eq 0 })
if ($unlinked.Count -gt 0) {
    $mdLines.Add("")
    $mdLines.Add("### Test Suites Without Requirement Links")
    $mdLines.Add("")
    $mdLines.Add('These logs contain no `Verifies: #N` lines and are **not** in the matrix above:')
    $mdLines.Add("")
    $mdLines.Add("| Test Suite | :white_check_mark: PASS | :x: FAIL | :next_track_button: SKIP | NOT_IMPL | INCONCL |")
    $mdLines.Add("|------------|:-----------------------:|:--------:|:------------------------:|:--------:|:-------:|")
    foreach ($suite in $unlinked) {
        $p = @($suite.TestCases | Where-Object { -not $_.Failed -and -not $_.Skipped }).Count
        $f = @($suite.TestCases | Where-Object { $_.Failed -and $_.Verdict -ne 'INCONCLUSIVE' }).Count
        $s = @($suite.TestCases | Where-Object { $_.Skipped -or $_.Verdict -eq 'INCONCLUSIVE' }).Count
        $ni =@($suite.TestCases | Where-Object { $_.Verdict -eq 'SKIP_NOT_IMPLEMENTED' }).Count
        $ic = @($suite.TestCases | Where-Object { $_.Verdict -eq 'INCONCLUSIVE' }).Count
        $mdLines.Add("| $($suite.Name) | $p | $f | $s | $ni | $ic |")
    }
}

$mdLines.Add("")
$mdLines.Add("> Run date: $RunDate | Machine: $machineName | Generated by ``Convert-Logs-To-JUnit.ps1``.")

if ($writeEvidenceDir) {
    if (-not (Test-Path $evidenceDir)) { New-Item -ItemType Directory -Path $evidenceDir -Force | Out-Null }
    $mdOut    = Join-Path $evidenceDir "traceability-summary-${RunDate}.md"
    $mdLatest = Join-Path $evidenceDir "traceability-summary-latest.md"
    $mdLines -join "`n" | Set-Content -Path $mdOut    -Encoding UTF8
    Copy-Item $mdOut $mdLatest -Force
    # Also write alongside the JUnit XML when the output dir differs from evidence dir
    $junitDir = Split-Path $outPath -Parent
    if ($junitDir -ne $evidenceDir) {
        $mdLines -join "`n" | Set-Content -Path (Join-Path $junitDir "traceability-summary-${RunDate}.md") -Encoding UTF8
    }
    Write-Host "[OK] Latest XML:      $latestPath" -ForegroundColor Green
    Write-Host "[OK] Traceability MD: $mdLatest" -ForegroundColor Green
} else {
    # Isolated run: write the traceability MD next to the JUnit only
    $junitDir = Split-Path $outPath -Parent
    $mdNear   = Join-Path $junitDir "traceability-summary-${RunDate}.md"
    $mdLines -join "`n" | Set-Content -Path $mdNear -Encoding UTF8
    Write-Host "[OK] Isolated JUnit:  $outPath" -ForegroundColor Green
    Write-Host "[NOTE] Evidence-dir writes skipped (isolated/sentinel run)" -ForegroundColor Yellow
}

Write-Host "[OK] JUnit XML:       $outPath" -ForegroundColor Green
$passed = $total - $fail - $skipped
Write-Host "[OK] Summary:   $($suites.Count) test suites | $total tests | $passed passed | $fail failed | $skipped skipped" -ForegroundColor Cyan
$nimplTotal = ($suites | ForEach-Object { @($_.TestCases | Where-Object { $_.Verdict -eq 'SKIP_NOT_IMPLEMENTED' }).Count } | Measure-Object -Sum).Sum
$inconTotal = ($suites | ForEach-Object { @($_.TestCases | Where-Object { $_.Verdict -eq 'INCONCLUSIVE' }).Count } | Measure-Object -Sum).Sum
if ($nimplTotal -gt 0 -or $inconTotal -gt 0) {
    Write-Host "[NOT VERIFIED] $nimplTotal SKIP_NOT_IMPLEMENTED (incomplete) | $inconTotal INCONCLUSIVE (insufficient evidence) - counted inside the skipped/failed totals above" -ForegroundColor Yellow
}

exit $(if ($fail -gt 0) { 1 } else { 0 })
