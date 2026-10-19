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
    [string]$ClassNameOverride = "",

    # Verification is MANDATORY unless this switch is given (fail-closed default).
    #   mandatory: SKIP_NOT_IMPLEMENTED is incomplete -> exit 1; INCONCLUSIVE -> <failure/>
    #   optional : both keep their label as <skipped/> and do not fail the run
    # Used for the experimental HardwareMutation lane only.
    [Parameter(Mandatory=$false)]
    [switch]$OptionalVerification
)

. (Join-Path $PSScriptRoot 'TestVerdict.ps1')

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
#   [FAIL]           TC-PERF-TS-006/adapter:0: Some threads exceeded thresholds
#   [SKIP]           TC-STAT-009: requires driver restart (user-mode limitation)
#   [BLOCKED]        TC-TAS-001: restore capability absent
#   [CLEANUP_FAILED] TC-TAS-001: hardware state unknown
#   [PASS]           UT-CORR-007 Jitter: ...   (PHC stability tests use UT-CORR-NNN IDs)
#   [FAIL]           UT-CORR-009 Driver Reload: ...
#   [PASS]           TEST-PROV-L01: ...        (provenance regression tests)
#   [SKIP]           TEST-PROV-H03: ...
#   [SKIP_UNSUPPORTED] TEST-PROV-H03: adapter lacks INTEL_CAP_BASIC_1588
#   [SKIP_NOT_IMPLEMENTED] TC-TAS-001/adapter:0: ntstatus=0xC0000002 (hardware ok, driver op missing)
#   [INCONCLUSIVE]   TC-X-001/adapter:0: capability unknown
#   [BLOCKED]        TC-TAS-001: prerequisite not met
#   [INFRA_FAILURE]  TC-PROV-H01: driver absent
# Legacy text form (existing C tests):  [SKIP] ID: SKIP_UNSUPPORTED: reason
# ---------------------------------------------------------------------------
# Pattern captures: status (tags in tools/test/TestVerdict.ps1)
#                   test ID (TC-*, UT-CORR-*, or TEST-PROV-* with optional /adapter:N)
#                   reason text
$pattern = '^\s*\[(' + $script:AvbVerdictTagPattern + ')\]\s+((?:TC-|UT-CORR-|TEST-PROV-)[\w-]+(?:/adapter:\d+)?)\s*(.*)'
$entries = [System.Collections.Generic.List[hashtable]]::new()

$content = Get-Content $LogFile -ErrorAction SilentlyContinue
if (-not $content) {
    Write-Warning "Parse-TestResults: log file is empty or unreadable: $LogFile"
    exit 0
}

$content | ForEach-Object {
    if ($_ -match $pattern) {
        $reason = $Matches[3].Trim()
        $status = Resolve-AvbVerdict -Tag $Matches[1].Trim() -Reason $reason
        $fullId = $Matches[2].Trim()

        # ── JUnit normalisation (shared table: tools/test/TestVerdict.ps1) ──
        # FAIL, BLOCKED, CLEANUP_FAILED, INFRA_FAILURE → JUnit failure
        # INCONCLUSIVE → failure (mandatory) / skipped (-OptionalVerification)
        # SKIP, SKIP_UNSUPPORTED, SKIP_NOT_IMPLEMENTED → JUnit skipped
        # PASS                                         → JUnit passed
        #
        # NOTE: INFRA_FAILURE must NEVER produce a JUnit passing test case.
        # A test that reports INFRA_FAILURE is evidence that the HIL runner,
        # driver, or test binary is broken — treat it as hard failure.
        $junitStatus = switch (Get-AvbVerdictClass -Verdict $status -Optional:$OptionalVerification) {
            'pass'    { 'PASS' }
            'failure' { 'FAIL' }
            'skipped' { 'SKIP' }
        }
        # Everything except the three plain tags keeps its exact verdict label so the
        # classification survives normalisation (testcase id/adapter/ntstatus stay in the text).
        $reasonWithStatus = if ($status -in @('PASS','FAIL','SKIP')) {
            $reason
        } else {
            "[$status] " + ($reason -replace '^:\s*', '')
        }

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
            Status     = $junitStatus        # PASS/FAIL/SKIP (JUnit-normalised)
            OrigStatus = $status             # exact verdict preserved (tag, or promoted legacy text)
            TcBase     = $tcBase
            Adapter    = $adapter
            Reason     = $reasonWithStatus
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
$incompleteCases   = 0 # SKIP_NOT_IMPLEMENTED present (no failure): required work missing
$inconclusiveCases = 0 # INCONCLUSIVE kept as skipped (-OptionalVerification only)

$sb = [System.Text.StringBuilder]::new()

# One adapter row rendered as "adapter:N: [TAG] reason" for messages.
function Format-Row($r) { if ($r.Adapter) { "$($r.Adapter): $($r.Reason)" } else { $r.Reason } }
function Esc($s) { [System.Security.SecurityElement]::Escape($s) }

foreach ($tcBase in $cases.Keys) {
    $rows      = $cases[$tcBase]
    $passCount = @($rows | Where-Object { $_.Status -eq 'PASS' }).Count
    $failRows  = @($rows | Where-Object { $_.Status -eq 'FAIL' })
    $failCount = $failRows.Count
    $skipCount = @($rows | Where-Object { $_.Status -eq 'SKIP' }).Count
    $total     = $rows.Count
    # Skipped rows that are NOT plain "N/A" skips: required work missing or not verified.
    $nimplRows  = @($rows | Where-Object { $_.OrigStatus -eq 'SKIP_NOT_IMPLEMENTED' })
    $inconRows  = @($rows | Where-Object { $_.Status -eq 'SKIP' -and $_.OrigStatus -eq 'INCONCLUSIVE' })

    $tcAttr = "name=`"$(Esc $tcBase)`" classname=`"$(Esc $className)`""

    if ($failCount -gt 0) {
        # ── FAIL: at least one adapter reported failure ──────────────────
        $failedCases++
        $failMsg = (($failRows | ForEach-Object { Format-Row $_ }) -join ';  ')

        # Keep every other non-passing adapter visible, with its exact verdict label.
        $others = @($rows | Where-Object { $_.Status -eq 'SKIP' })
        if ($others.Count -gt 0) {
            $skipAdapters = ($others | ForEach-Object { $_.Adapter }) -join ', '
            $firstSkipReason = $others[0].Reason
            $failMsg += "  [also not passing on ($skipAdapters): $firstSkipReason]"
        }
        # Exact class of the (first) failing row: FAIL / BLOCKED / CLEANUP_FAILED / INFRA_FAILURE / INCONCLUSIVE
        $failType = $failRows[0].OrigStatus

        [void]$sb.AppendLine("    <testcase $tcAttr>")
        [void]$sb.AppendLine("      <failure type=`"$(Esc $failType)`" message=`"$(Esc $failMsg)`"/>")
        [void]$sb.AppendLine("    </testcase>")

    } elseif ($nimplRows.Count -gt 0 -or $inconRows.Count -gt 0) {
        # ── INCOMPLETE: hardware supports it but the driver does not implement it
        #    (SKIP_NOT_IMPLEMENTED), or evidence is insufficient (optional INCONCLUSIVE).
        #    Stays a JUnit <skipped/> with an explicit gap message; it is NEVER folded
        #    into PASS / PARTIAL COVERAGE.  Mandatory runs exit non-zero (see tail). ──
        if ($nimplRows.Count -gt 0) { $incompleteCases++ } else { $inconclusiveCases++ }
        $label    = if ($nimplRows.Count -gt 0) { 'SKIP_NOT_IMPLEMENTED' } else { 'INCONCLUSIVE' }
        $gapRows  = @($rows | Where-Object { $_.Status -eq 'SKIP' -and $_.OrigStatus -in @('SKIP_NOT_IMPLEMENTED','INCONCLUSIVE') })
        $gapMsg   = (($gapRows | ForEach-Object { Format-Row $_ }) -join ';  ')
        $detail   = "incomplete ($label)"
        if ($passCount -gt 0) {
            $passAdapters = (($rows | Where-Object { $_.Status -eq 'PASS' } |
                             ForEach-Object { if ($_.Adapter) { $_.Adapter } else { 'N/A' } }) -join ', ')
            $detail += "; passed on [$passAdapters]"
        }
        $skipMsg = "$detail`: $gapMsg"

        [void]$sb.AppendLine("    <testcase $tcAttr>")
        [void]$sb.AppendLine("      <skipped message=`"$(Esc $skipMsg)`"/>")
        [void]$sb.AppendLine("    </testcase>")

    } elseif ($skipCount -eq $total) {
        # ── ALL-SKIP: genuine skip — feature N/A (SKIP_UNSUPPORTED), link down on
        #    all adapters, requires restart, etc. ─────────────────────────
        $skippedCases++
        $firstReason = $rows[0].Reason
        $adapterCtx  = if ($rows[0].Adapter) { "all $total adapter(s) skipped" } else { "skipped" }
        $skipMsg     = "$adapterCtx`: $firstReason"

        [void]$sb.AppendLine("    <testcase $tcAttr>")
        [void]$sb.AppendLine("      <skipped message=`"$(Esc $skipMsg)`"/>")
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
       <skipped>  = all adapters skipped (feature N/A), OR labelled
                    "incomplete (SKIP_NOT_IMPLEMENTED)": required work missing
       <failure>  = at least one adapter failed (type= keeps the exact verdict)
       PASS + PARTIAL COVERAGE annotation = some adapters skipped (e.g. link-down),
                                            rest passed; no failures
-->
<testsuite name="$className" tests="$totalCases" failures="$failedCases" skipped="$($skippedCases + $incompleteCases + $inconclusiveCases)" time="0">
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
$passedCases = $totalCases - $failedCases - $skippedCases - $incompleteCases - $inconclusiveCases

$mandatory      = -not $OptionalVerification
$incompleteFail = $mandatory -and ($incompleteCases -gt 0)
$color = if ($failedCases -gt 0 -or $incompleteFail) { 'Red' }
         elseif ($partialCases -gt 0 -or $incompleteCases -gt 0 -or $inconclusiveCases -gt 0) { 'Yellow' }
         else { 'Green' }

$detail = "  [JUnit] $className`: $passedCases pass"
if ($partialCases -gt 0)      { $detail += ", $partialCases partial" }
if ($skippedCases -gt 0)      { $detail += ", $skippedCases skip" }
if ($incompleteCases -gt 0)   { $detail += ", $incompleteCases INCOMPLETE (SKIP_NOT_IMPLEMENTED)" }
if ($inconclusiveCases -gt 0) { $detail += ", $inconclusiveCases INCONCLUSIVE (optional)" }
if ($failedCases  -gt 0)      { $detail += ", $failedCases FAIL" }
$detail += " of $totalCases cases - $([System.IO.Path]::GetFileName($OutputFile))"

Write-Host $detail -ForegroundColor $color

# If partial cases exist, show which ones for immediate visibility
if ($partialCases -gt 0) {
    foreach ($tcBase in $cases.Keys) {
        $rows     = $cases[$tcBase]
        $passed   = ($rows | Where-Object { $_.Status -eq 'PASS'}).Count
        $skipped  = ($rows | Where-Object { $_.Status -eq 'SKIP' -and $_.OrigStatus -notin @('SKIP_NOT_IMPLEMENTED','INCONCLUSIVE') }).Count
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

# Exit non-zero on TC-level failures.  Partial coverage and genuine SKIP/SKIP_UNSUPPORTED do not
# block.  SKIP_NOT_IMPLEMENTED is a JUnit skip but incomplete compliance: for mandatory
# verification it must not read as green, so the run exits 1 (use -OptionalVerification
# for experimental lanes).
if ($incompleteFail) {
    Write-Host "  [INCOMPLETE] $incompleteCases testcase(s) carry SKIP_NOT_IMPLEMENTED: mandatory verification is not complete" -ForegroundColor Red
}
if ($failedCases -gt 0 -or $incompleteFail) { exit 1 } else { exit 0 }
