<#
.SYNOPSIS
    Parse Intel AVB Filter test log output into per-test-case JUnit XML.

.DESCRIPTION
    Reads a .log file produced by Run-Tests.ps1 / an AVB test executable and
    extracts [PASS]/[FAIL]/[SKIP] lines.  Results are aggregated across
    adapter iterations (format: TC-ID/adapter:N) so that skip semantics
    become visible at CI level:

        All adapters SKIP  → <skipped/>        (feature genuinely N/A)
        Any FAIL           → <failure/>         (blocks CI)
        Some SKIP + PASS   → PASS + PARTIAL     (partial adapter coverage)
        All PASS           → clean PASS

    This removes the ambiguity identified in CI: previously a test FILE was
    only reported as PASS/FAIL based on exit code, hiding per-test-case skip
    reasons.  With this parser, CI distinguishes:

        "skipped because link down on adapter 2, but passed on adapters 0,1,3,4,5"
    from
        "skipped everywhere because the feature is not yet implemented"

    Output: JUnit XML (compatible with GitHub Actions dorny/test-reporter@v1
    and the actions/upload-artifact + workflow summary reporters).

.PARAMETER LogFile
    Path to the .log file written by the test executable.

.PARAMETER OutputFile
    Path for the JUnit XML output.  Defaults to $LogFile with '.xml' extension.

.PARAMETER ClassNameOverride
    Override the classname attribute in <testcase>.  Defaults to the log
    filename stem (timestamp suffix stripped).

.EXAMPLE
    # Called automatically by Run-Tests.ps1 after each Invoke-Test call.
    .\Parse-TestResults.ps1 -LogFile logs\test_timestamp_latency.exe_20260402.log

.EXAMPLE
    # Manual per-test-case analysis
    .\Parse-TestResults.ps1 -LogFile logs\test_statistics_counters.exe_20260402.log `
                            -OutputFile artifacts\test_statistics_counters.xml
#>
param(
    [Parameter(Mandatory=$true)]
    [string]$LogFile,

    [Parameter(Mandatory=$false)]
    [string]$OutputFile = "",

    [Parameter(Mandatory=$false)]
    [string]$ClassNameOverride = ""
)

if (-not (Test-Path $LogFile)) {
    Write-Warning "Parse-TestResults: log file not found: $LogFile"
    exit 0   # Non-fatal — log may not exist for non-exe tests (SSOT, REGS, etc.)
}

if (-not $OutputFile) {
    $OutputFile = [System.IO.Path]::ChangeExtension($LogFile, ".xml")
}

# Derive class name from log file stem, stripping the timestamp suffix
$className = if ($ClassNameOverride) {
    $ClassNameOverride
} else {
    [System.IO.Path]::GetFileNameWithoutExtension($LogFile) -replace '_\d{8}_\d{6}$',''
}

# ---------------------------------------------------------------------------
# Parse [PASS]/[FAIL]/[SKIP] lines.
#
# Supported formats — both with and without /adapter:N suffix.
# The canonical summary section (emitted at end of each test binary) takes
# precedence over inline per-measurement prints.
#
# Format:
#   [PASS] TC-STAT-001: reason
#   [FAIL] TC-PERF-TS-006/adapter:0: Some threads exceeded thresholds
#   [SKIP] TC-STAT-009: requires driver restart (user-mode limitation)
# ---------------------------------------------------------------------------
$pattern = '^\s*\[(PASS|FAIL|SKIP)\]\s+(TC-[\w-]+(?:/adapter:\d+)?)\s*:(.*)'
$entries = [System.Collections.Generic.List[hashtable]]::new()

$content = Get-Content $LogFile -ErrorAction SilentlyContinue
if (-not $content) {
    Write-Warning "Parse-TestResults: log file is empty or unreadable: $LogFile"
    exit 0
}

$content | ForEach-Object {
    if ($_ -match $pattern) {
        $status = $Matches[1].Trim()
        $fullId = $Matches[2].Trim()
        $reason = $Matches[3].Trim()

        # Split "TC-PERF-TS-001/adapter:0" → tcBase="TC-PERF-TS-001" adapter="adapter:0"
        $slash = $fullId.IndexOf('/')
        if ($slash -ge 0) {
            $tcBase  = $fullId.Substring(0, $slash)
            $adapter = $fullId.Substring($slash + 1)
        } else {
            $tcBase  = $fullId
            $adapter = ""
        }

        $entries.Add(@{
            Status  = $status
            TcBase  = $tcBase
            Adapter = $adapter
            Reason  = $reason
        })
    }
}

if ($entries.Count -eq 0) {
    Write-Verbose "Parse-TestResults: no [PASS]/[FAIL]/[SKIP] summary lines in $LogFile - skipping XML"
    exit 0
}

# ---------------------------------------------------------------------------
# Aggregate by TC base ID.
# Key insertion order matters for deterministic output (OrderedDictionary).
# ---------------------------------------------------------------------------
$cases = [System.Collections.Specialized.OrderedDictionary]::new()

foreach ($e in $entries) {
    if (-not $cases.Contains($e.TcBase)) {
        $cases[$e.TcBase] = [System.Collections.Generic.List[hashtable]]::new()
    }
    $cases[$e.TcBase].Add($e)
}

# ---------------------------------------------------------------------------
# Build JUnit XML.
# ---------------------------------------------------------------------------
$totalCases   = $cases.Count
$failedCases  = 0
$skippedCases = 0      # counts only all-adapters-skip (real skip)
$partialCases = 0      # some passed, some skipped — tracked for summary only

$sb = [System.Text.StringBuilder]::new()

foreach ($tcBase in $cases.Keys) {
    $rows      = $cases[$tcBase]
    $passCount = ($rows | Where-Object { $_.Status -eq 'PASS' }).Count
    $failCount = ($rows | Where-Object { $_.Status -eq 'FAIL' }).Count
    $skipCount = ($rows | Where-Object { $_.Status -eq 'SKIP' }).Count
    $total     = $rows.Count

    $tcAttr = "name=`"$([System.Security.SecurityElement]::Escape($tcBase))`" classname=`"$([System.Security.SecurityElement]::Escape($className))`""

    if ($failCount -gt 0) {
        # ── FAIL: at least one adapter reported failure ──────────────────
        $failedCases++
        $failParts = $rows | Where-Object { $_.Status -eq 'FAIL' } | ForEach-Object {
            if ($_.Adapter) { "$($_.Adapter): $($_.Reason)" } else { $_.Reason }
        }
        $failMsg = ($failParts -join ';  ')

        # Add skip context if some adapters also skipped (e.g. link down)
        if ($skipCount -gt 0) {
            $skipAdapters = ($rows | Where-Object { $_.Status -eq 'SKIP' } |
                            ForEach-Object { $_.Adapter }) -join ', '
            $firstSkipReason = ($rows | Where-Object { $_.Status -eq 'SKIP' } |
                               Select-Object -First 1).Reason
            $failMsg += "  [also skipped on ($skipAdapters): $firstSkipReason]"
        }

        [void]$sb.AppendLine("    <testcase $tcAttr>")
        [void]$sb.AppendLine("      <failure message=`"$([System.Security.SecurityElement]::Escape($failMsg))`"/>")
        [void]$sb.AppendLine("    </testcase>")

    } elseif ($skipCount -eq $total) {
        # ── ALL-SKIP: genuine skip — feature N/A, link down on all adapters,
        #    requires restart, not implemented, etc. ──────────────────────
        $skippedCases++
        $firstReason = $rows[0].Reason
        $adapterCtx  = if ($rows[0].Adapter) { "all $total adapter(s) skipped" } else { "skipped" }
        $skipMsg     = "$adapterCtx`: $firstReason"

        [void]$sb.AppendLine("    <testcase $tcAttr>")
        [void]$sb.AppendLine("      <skipped message=`"$([System.Security.SecurityElement]::Escape($skipMsg))`"/>")
        [void]$sb.AppendLine("    </testcase>")

    } elseif ($skipCount -gt 0 -and $passCount -gt 0) {
        # ── PARTIAL COVERAGE: passed on some adapters, skipped on others,
        #    no failures.  Emit as PASS so CI stays green, but annotate
        #    clearly.  This is the intentional "link down adapter" scenario. ──
        $partialCases++
        $skipAdapters = ($rows | Where-Object { $_.Status -eq 'SKIP' } |
                        ForEach-Object { if ($_.Adapter) { $_.Adapter } else { "N/A" } }) -join ', '
        $firstSkipReason = ($rows | Where-Object { $_.Status -eq 'SKIP' } |
                           Select-Object -First 1).Reason
        $annotation = "PARTIAL COVERAGE: passed $passCount/$total adapter(s); " +
                      "skipped on [$skipAdapters]: $firstSkipReason"

        [void]$sb.AppendLine("    <testcase $tcAttr>")
        [void]$sb.AppendLine("      <system-out>$([System.Security.SecurityElement]::Escape($annotation))</system-out>")
        [void]$sb.AppendLine("    </testcase>")

    } else {
        # ── PASS: all adapters passed ─────────────────────────────────────
        [void]$sb.AppendLine("    <testcase $tcAttr/>")
    }
}

# ---------------------------------------------------------------------------
# Write JUnit XML file.
# ---------------------------------------------------------------------------
$xmlContent = @"
<?xml version="1.0" encoding="UTF-8"?>
<!-- Generated by tools/test/Parse-TestResults.ps1
     Skip semantics:
       <skipped>  = all adapters skipped (feature N/A)
       <failure>  = at least one adapter failed
       PASS + PARTIAL COVERAGE annotation = some adapters skipped (e.g. link-down),
                                            rest passed; no failures
-->
<testsuite name="$className" tests="$totalCases" failures="$failedCases" skipped="$skippedCases" time="0">
$($sb.ToString().TrimEnd())
</testsuite>
"@

# Ensure output directory exists
$outDir = Split-Path $OutputFile -Parent
if ($outDir -and -not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

$xmlContent | Set-Content -Path $OutputFile -Encoding UTF8

# ---------------------------------------------------------------------------
# Report to console (visible in Run-Tests.ps1 / CI transcript)
# ---------------------------------------------------------------------------
$passedCases = $totalCases - $failedCases - $skippedCases

$color = if ($failedCases -gt 0) { 'Red' } elseif ($partialCases -gt 0) { 'Yellow' } else { 'Green' }

$detail = "  [JUnit] $className`: $passedCases pass"
if ($partialCases -gt 0) { $detail += ", $partialCases partial" }
if ($skippedCases -gt 0) { $detail += ", $skippedCases skip" }
if ($failedCases  -gt 0) { $detail += ", $failedCases FAIL" }
$detail += " of $totalCases cases - $([System.IO.Path]::GetFileName($OutputFile))"

Write-Host $detail -ForegroundColor $color

# If partial cases exist, show which ones for immediate visibility
if ($partialCases -gt 0) {
    foreach ($tcBase in $cases.Keys) {
        $rows     = $cases[$tcBase]
        $passed   = ($rows | Where-Object { $_.Status -eq 'PASS'}).Count
        $skipped  = ($rows | Where-Object { $_.Status -eq 'SKIP'}).Count
        $failCount = ($rows | Where-Object { $_.Status -eq 'FAIL'}).Count
        if ($skipped -gt 0 -and $passed -gt 0 -and $failCount -eq 0) {
            $skipAdapters = ($rows | Where-Object { $_.Status -eq 'SKIP'} |
                            ForEach-Object { $_.Adapter }) -join ', '
            $reason = ($rows | Where-Object { $_.Status -eq 'SKIP'} |
                      Select-Object -First 1).Reason
            Write-Host "    [PARTIAL] $tcBase`: skipped on [$skipAdapters] - $reason" -ForegroundColor Yellow
        }
    }
}

# Exit non-zero only on TC-level failures; partial/skip do not block CI
if ($failedCases -gt 0) { exit 1 } else { exit 0 }
