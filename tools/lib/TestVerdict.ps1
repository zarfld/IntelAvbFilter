# TestVerdict.ps1 - canonical testcase verdict vocabulary (dot-source, no side effects)
#
# Single definition of the verdict tags printed by test executables and of how
# each tag normalises to a JUnit element.  Shared by:
#   tools/test/Parse-TestResults.ps1      (local per-test JUnit)
#   tools/test/Convert-Logs-To-JUnit.ps1  (CI JUnit + traceability matrix)
#   tools/test/Run-Tests-CI.ps1           (suite exit status)
# so the three can no longer drift apart.  Contract: docs/testing/test-verdict-contract.md
#
#   Verdict               JUnit      Meaning
#   PASS                  passed     verified
#   FAIL                  failure    implemented but incorrect (incl. success with invalid payload)
#   SKIP_UNSUPPORTED      skipped    hardware genuinely lacks the capability (STATUS_NOT_SUPPORTED)
#   SKIP_NOT_IMPLEMENTED  skipped    hardware supports it, driver does not (STATUS_NOT_IMPLEMENTED)
#                                    -> INCOMPLETE: nonpassing for mandatory verification
#   BLOCKED               failure    unsafe / unmet prerequisite
#   INCONCLUSIVE          failure    insufficient evidence; mandatory verification
#                         skipped    ... or label-preserving skip when -Optional
#   SKIP                  skipped    runtime N/A (link down, requires restart); not a capability claim
#   CLEANUP_FAILED, INFRA_FAILURE    failure
#
# Implements: test verdict taxonomy (see docs/testing/test-verdict-contract.md)

# Longest alternatives first so SKIP does not shadow SKIP_*.
$script:AvbVerdictTagPattern = 'PASS|FAIL|SKIP_UNSUPPORTED|SKIP_NOT_IMPLEMENTED|SKIP|BLOCKED|INCONCLUSIVE|INFRA_FAILURE|CLEANUP_FAILED'

# Some existing C tests print the verdict as text after a plain [SKIP] tag:
#   [SKIP] TEST-PROV-H04: SKIP_UNSUPPORTED: adapter lacks INTEL_CAP_BASIC_1588
# Promote those so their exact classification is not lost.
function Resolve-AvbVerdict {
    param([string]$Tag, [string]$Reason)
    if ($Tag -eq 'SKIP') {
        $text = $Reason -replace '^[\s:]+', ''
        if ($text -match '^(SKIP_UNSUPPORTED|SKIP_NOT_IMPLEMENTED)\b') { return $Matches[1] }
    }
    return $Tag
}

# Normalise a verdict to a JUnit element class: 'pass' | 'failure' | 'skipped'.
# Unknown verdicts throw: a silent default would convert a new verdict into a pass or skip.
function Get-AvbVerdictClass {
    param([Parameter(Mandatory=$true)][string]$Verdict, [switch]$Optional)
    switch ($Verdict) {
        'PASS'                 { return 'pass' }
        'FAIL'                 { return 'failure' }
        'BLOCKED'              { return 'failure' }
        'CLEANUP_FAILED'       { return 'failure' }
        'INFRA_FAILURE'        { return 'failure' }
        'INCONCLUSIVE'         { if ($Optional) { return 'skipped' } else { return 'failure' } }
        'SKIP'                 { return 'skipped' }
        'SKIP_UNSUPPORTED'     { return 'skipped' }
        'SKIP_NOT_IMPLEMENTED' { return 'skipped' }
        default                { throw "Unknown test verdict '$Verdict' - add it to tools/lib/TestVerdict.ps1" }
    }
}

# Verdicts that mean "required work is missing": nonpassing for mandatory verification.
function Test-AvbVerdictIncomplete {
    param([Parameter(Mandatory=$true)][string]$Verdict)
    return ($Verdict -eq 'SKIP_NOT_IMPLEMENTED')
}

# HardwareMutation is the only explicitly optional/experimental lane; every other
# suite is mandatory verification (fail-closed default).  No new lane is introduced.
function Get-AvbSuiteVerificationClass {
    param([string]$Suite)
    if ($Suite -eq 'HardwareMutation') { return 'optional' }
    return 'mandatory'
}

# Count verdict lines in raw test output.  Used by the suite runner, which only
# sees executable exit codes and would otherwise count "exit 0 + NOT_IMPLEMENTED" as PASSED.
function Get-AvbVerdictCounts {
    param([string[]]$Lines)
    $counts = @{ NotImplemented = 0; Inconclusive = 0 }
    $rx = '^\s*\[(' + $script:AvbVerdictTagPattern + ')\]\s+(?:\[Adapter\d+\[[^\]]*\]\]\s+)?(?:TC|IT|VV|UT|REG|TEST)-[\w-]+(?:/adapter:\d+)?\s*(.*)$'
    foreach ($line in $Lines) {
        if ($line -match $rx) {
            $v = Resolve-AvbVerdict -Tag $Matches[1] -Reason $Matches[2]
            if ($v -eq 'SKIP_NOT_IMPLEMENTED') { $counts.NotImplemented++ }
            elseif ($v -eq 'INCONCLUSIVE')     { $counts.Inconclusive++ }
        }
    }
    return $counts
}
