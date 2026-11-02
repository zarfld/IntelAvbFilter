# test_agent_testing_guidance.ps1
# TEST-AGENTGUIDE-*: static integrity of the agent testing guidance (offline; no driver, no hardware, no agent).
#
# Checks the wiring of the canonical testing contract, not agent behaviour:
#   - skill frontmatter (strict subset parse: key: value, quoted value, folded >- block), name == directory,
#     description present and <= 1024 chars, model invocation not disabled, no added tool permissions;
#   - Claude entry point (.claude/skills) is a thin loader of the canonical file (.github/skills), no duplicated contract;
#   - root claude.md imports AGENTS.md and requires the skill; Copilot routing files point at the canonical contract;
#   - every relative link and backticked repository path in the guidance exists;
#   - every -Parameter used with a runner script in the guidance exists in that script's param() block (AST);
#   - superseded statements ("CI has no hardware") are not presented as current; reference.md is marked historical.
# A pass means the guidance files are consistent with the repository; it is not evidence that any agent follows them.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent

$total = 0; $passed = 0; $failed = 0
function tc_check { param([string]$n, [bool]$ok, [string]$why)
    $script:total++; if ($ok) { $script:passed++; Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green } else { $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red } }
Write-Host ""
Write-Host "TEST-AGENTGUIDE: agent testing guidance wiring (offline)" -ForegroundColor Cyan

$Canonical  = '.github/skills/intelavbfilter-testing/SKILL.md'
$Reference  = '.github/skills/intelavbfilter-testing/reference.md'
$Scenarios  = '.github/skills/intelavbfilter-testing/scenarios.md'
$ClaudeSkill = '.claude/skills/intelavbfilter-testing/SKILL.md'
$RunTests   = '.github/skills/run-tests/SKILL.md'
$TestsInstr = '.github/instructions/tests.instructions.md'

function Get-RepoFile([string]$Rel) { return (Join-Path $RepoRoot ($Rel -replace '/', '\')) }
function Read-RepoText([string]$Rel) {
    $p = Get-RepoFile $Rel
    if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { return $null }
    return [System.IO.File]::ReadAllText($p)
}

# Strict subset of YAML frontmatter.  Returns @{ Ok; Why; Keys }.  Anything outside the subset is a failure, never ignored.
function Read-Frontmatter([string]$Text) {
    if ($null -eq $Text) { return @{ Ok = $false; Why = 'file missing'; Keys = @{} } }
    $lines = $Text -split "`r?`n"
    if ($lines[0].TrimStart([char]0xFEFF) -ne '---') { return @{ Ok = $false; Why = "line 1 is '$($lines[0])', expected '---' (fenced or missing frontmatter)"; Keys = @{} } }
    $end = -1
    for ($i = 1; $i -lt $lines.Count; $i++) { if ($lines[$i] -eq '---') { $end = $i; break } }
    if ($end -lt 0) { return @{ Ok = $false; Why = 'no closing ---'; Keys = @{} } }
    $keys = @{}; $cur = $null
    for ($i = 1; $i -lt $end; $i++) {
        $l = $lines[$i]
        if ($cur -and $l -match '^\s+\S') { $keys[$cur] = ($keys[$cur] + ' ' + $l.Trim()).Trim(); continue }
        $cur = $null
        if ($l -match '^\s*$') { continue }
        if ($l -match '^([A-Za-z][\w-]*):\s*>-?\s*$') { $cur = $Matches[1]; $keys[$cur] = ''; continue }
        if ($l -match '^([A-Za-z][\w-]*):\s*"((?:[^"\\]|\\.)*)"\s*$') { $keys[$Matches[1]] = $Matches[2]; continue }
        if ($l -match "^([A-Za-z][\w-]*):\s*'([^']*)'\s*$") { $keys[$Matches[1]] = $Matches[2]; continue }
        if ($l -match '^([A-Za-z][\w-]*):\s*([^\s"''>|][^#]*?)\s*$') { $keys[$Matches[1]] = $Matches[2]; continue }
        return @{ Ok = $false; Why = "frontmatter line $($i + 1) outside the supported subset: '$l'"; Keys = $keys }
    }
    return @{ Ok = $true; Why = ''; Keys = $keys }
}

function Test-SkillFrontmatter([string]$Id, [string]$Rel, [string]$ExpectName) {
    $fm = Read-Frontmatter (Read-RepoText $Rel)
    tc_check "$Id-frontmatter-parses ($Rel)" $fm.Ok $fm.Why
    if (-not $fm.Ok) { return }
    $k = $fm.Keys
    tc_check "$Id-name-matches-directory" ($k.ContainsKey('name') -and $k['name'] -eq $ExpectName) ("name '{0}', expected '{1}'" -f $(if ($k.ContainsKey('name')) { $k['name'] } else { '<none>' }), $ExpectName)
    $d = $(if ($k.ContainsKey('description')) { [string]$k['description'] } else { '' })
    tc_check "$Id-description-present-and-bounded" ($d.Length -ge 40 -and $d.Length -le 1024) ("description length {0} (need 40..1024)" -f $d.Length)
    tc_check "$Id-model-invocation-enabled" (-not ($k.ContainsKey('disable-model-invocation') -and $k['disable-model-invocation'] -eq 'true')) 'disable-model-invocation: true'
    tc_check "$Id-user-invocable" (-not ($k.ContainsKey('user-invocable') -and $k['user-invocable'] -eq 'false')) 'user-invocable: false'
    tc_check "$Id-no-added-tool-permissions" (-not $k.ContainsKey('allowed-tools')) 'allowed-tools grants extra permissions'
}

# 001 / 002 / 003: frontmatter of the canonical contract, the Claude entry point and run-tests.
Test-SkillFrontmatter 'TEST-AGENTGUIDE-001' $Canonical   'intelavbfilter-testing'
Test-SkillFrontmatter 'TEST-AGENTGUIDE-002' $ClaudeSkill 'intelavbfilter-testing'
Test-SkillFrontmatter 'TEST-AGENTGUIDE-003' $RunTests    'run-tests'

# 004: canonical contract carries every mandatory section; the Claude entry duplicates none and loads the canonical file.
$canon  = Read-RepoText $Canonical
$claude = Read-RepoText $ClaudeSkill
$sections = @('A. Dynamic adapter discovery', 'B. Verdict correctness', 'C. Complete test inventory', 'D. TDD with real hardware',
              'E. Hardware-state establishment and restoration', 'F. Timeouts and unattended execution', 'G. Provenance and evidence',
              'H. Existing infrastructure and authority', 'I. Elevated execution and hardware authorization')
$missing = @(if ($canon) { $sections | Where-Object { $canon -notmatch ('(?m)^## ' + [regex]::Escape($_)) } } else { $sections })
tc_check 'TEST-AGENTGUIDE-004a-canonical-has-all-contract-sections' ($missing.Count -eq 0) ("missing: {0}" -f ($missing -join '; '))
$dup = @(if ($claude) { $sections | Where-Object { $claude -match ('(?m)^#+ ' + [regex]::Escape($_)) } })
$claudeLines = $(if ($claude) { @($claude -split "`r?`n").Count } else { 0 })
tc_check 'TEST-AGENTGUIDE-004b-claude-entry-is-thin-loader' ($null -ne $claude -and $dup.Count -eq 0 -and $claudeLines -le 40 -and $claude.Contains($Canonical)) `
    ("lines={0}, duplicated sections={1}, references canonical={2}" -f $claudeLines, ($dup -join ';'), $(if ($claude) { $claude.Contains($Canonical) } else { $false }))

# 005: root claude.md imports AGENTS.md and requires the skill (file name is case-insensitive on Windows; git tracks claude.md).
$rootClaude = @(Get-ChildItem -LiteralPath $RepoRoot -File | Where-Object { $_.Name -ieq 'claude.md' })
$rc = $(if ($rootClaude.Count -eq 1) { [System.IO.File]::ReadAllText($rootClaude[0].FullName) } else { '' })
tc_check 'TEST-AGENTGUIDE-005a-root-claude-md-imports-agents-md' ($rc -match '(?m)^@AGENTS\.md\s*$' -and (Test-Path -LiteralPath (Get-RepoFile 'AGENTS.md'))) ("root claude.md files: {0}; @AGENTS.md line present: {1}" -f $rootClaude.Count, ($rc -match '(?m)^@AGENTS\.md\s*$'))
tc_check 'TEST-AGENTGUIDE-005b-root-claude-md-requires-testing-skill' ($rc.Contains('intelavbfilter-testing') -and $rc.Contains($ClaudeSkill)) 'claude.md does not name the testing skill entry point'

# 006: Copilot routing reaches the canonical contract.
$agents = Read-RepoText 'AGENTS.md'; $copilot = Read-RepoText '.github/copilot-instructions.md'; $instr = Read-RepoText $TestsInstr
tc_check 'TEST-AGENTGUIDE-006-copilot-routing-reaches-canonical' (($agents -match 'intelavbfilter-testing') -and ($copilot -match 'intelavbfilter-testing') -and $instr -and $instr.Contains($Canonical)) `
    'AGENTS.md / copilot-instructions.md must route to intelavbfilter-testing and tests.instructions.md must name the canonical file'

# 007: every relative markdown link and backticked repository path in the guidance exists.
$guidance = @($Canonical, $Scenarios, $ClaudeSkill, $RunTests, $TestsInstr)
$broken = @()
foreach ($g in $guidance) {
    $t = Read-RepoText $g
    if ($null -eq $t) { $broken += "$g (file missing)"; continue }
    $dir = Split-Path -Parent (Get-RepoFile $g)
    foreach ($m in [regex]::Matches($t, '\]\((\.{1,2}/[^)#\s]+)')) {
        $target = [System.IO.Path]::GetFullPath((Join-Path $dir ($m.Groups[1].Value -replace '/', '\')))
        if (-not (Test-Path -LiteralPath $target)) { $broken += "$g -> $($m.Groups[1].Value)" }
    }
    foreach ($m in [regex]::Matches($t, '`\.?\\?((?:\.github|\.claude|tools|tests|docs|include|devices|src)[\\/][^`\s]+?)`')) {
        $p = $m.Groups[1].Value
        if ($p -match '[<>*]') { continue }                                  # placeholder, not a path
        $p = $p.TrimEnd('\', '/')
        if (-not (Test-Path -LiteralPath (Get-RepoFile ($p -replace '\\', '/')))) { $broken += "$g -> $p" }
    }
}
tc_check 'TEST-AGENTGUIDE-007-referenced-files-exist' ($broken.Count -eq 0) ("{0} broken: {1}" -f $broken.Count, ($broken -join ' | '))

# 008: every -Parameter used with a runner script in the guidance exists in that script's param() block.
$scriptPaths = @{
    'Run-Tests-CI.ps1'       = 'tools/test/Run-Tests-CI.ps1'
    'Run-Tests-Elevated.ps1' = 'tools/test/Run-Tests-Elevated.ps1'
    'Run-Tests.ps1'          = 'tools/test/Run-Tests.ps1'
    'Build-Tests.ps1'        = 'tools/build/Build-Tests.ps1'
    'LocalSuites.ps1'        = 'tools/archive/deprecated/LocalSuites.ps1'
}
$params = @{}
foreach ($s in $scriptPaths.Keys) {
    $ast = [System.Management.Automation.Language.Parser]::ParseFile((Get-RepoFile $scriptPaths[$s]), [ref]$null, [ref]$null)
    $params[$s] = @(if ($ast.ParamBlock) { $ast.ParamBlock.Parameters | ForEach-Object { $_.Name.VariablePath.UserPath.ToLowerInvariant() } })
}
$unknown = @()
foreach ($g in @($Canonical, $Scenarios, $ClaudeSkill, $RunTests)) {
    $t = Read-RepoText $g
    if ($null -eq $t) { continue }
    $section = $null
    foreach ($line in ($t -split "`r?`n")) {
        foreach ($s in $scriptPaths.Keys) {
            if ($line -match ('^`?(?:tools/(?:test|build|archive/deprecated)/)?' + [regex]::Escape($s) + '`?\s+[—-]')) {
                $section = $s
                # Script header line listing parameters inline: `tools/x/Script.ps1` — `-A`, `-B <v>` ...
                foreach ($pm in [regex]::Matches($line, '`-([A-Za-z]\w*)')) {
                    if ($params[$s] -notcontains $pm.Groups[1].Value.ToLowerInvariant()) { $unknown += "$g : $s header -$($pm.Groups[1].Value)" }
                }
            }
        }
        if ($line -match '^#') { $section = $null }
        # Command form: <script>.ps1 -A x -B ...  A value token is accepted only directly after a -Parameter; any other
        # token (prose, arrow, pipe, redirection) ends the command, so "X.ps1 <- entry ... Start-Process -Verb" is not X's.
        foreach ($m in [regex]::Matches($line, '([\w-]+\.ps1)((?:\s+-\w+(?:\s+(?:''[^'']*''|"[^"]*"|(?!-)[^\s|`''"]+))?)*)')) {
            $s = $m.Groups[1].Value
            if (-not $scriptPaths.ContainsKey($s)) { continue }
            foreach ($pm in [regex]::Matches($m.Groups[2].Value, '(?<![\w''"])-([A-Za-z]\w*)')) {
                if ($params[$s] -notcontains $pm.Groups[1].Value.ToLowerInvariant()) { $unknown += "$g : $s -$($pm.Groups[1].Value)" }
            }
        }
        # Parameter-table row under a script heading line: | `-Name ...` |
        if ($section -and $line -match '^\|\s*`-([A-Za-z]\w*)') {
            if ($params[$section] -notcontains $Matches[1].ToLowerInvariant()) { $unknown += "$g : $section table -$($Matches[1])" }
        }
    }
}
tc_check 'TEST-AGENTGUIDE-008-runner-parameters-exist' ($unknown.Count -eq 0) ("{0} unknown: {1}" -f $unknown.Count, ($unknown -join ' | '))

# 009: superseded statements are not presented as current; historical reference is marked; fences are balanced.
$rt = Read-RepoText $RunTests
$stale = @()
foreach ($g in @($Canonical, $ClaudeSkill, $RunTests)) {
    $t = Read-RepoText $g
    if ($null -eq $t) { continue }
    foreach ($rx in @('NO hardware/driver expected', 'Hardware tests are auto-skipped', 'hw-independent tests only')) { if ($t -match [regex]::Escape($rx)) { $stale += "$g : '$rx'" } }
    if ($t -match '(?m)^````') { $stale += "$g : four-backtick fence (skill wrapped in a code block)" }
    if ((@([regex]::Matches($t, '(?m)^```')).Count % 2) -ne 0) { $stale += "$g : unbalanced code fences" }
}
tc_check 'TEST-AGENTGUIDE-009a-no-superseded-runner-guidance' ($stale.Count -eq 0) ($stale -join ' | ')
$ref = Read-RepoText $Reference
tc_check 'TEST-AGENTGUIDE-009b-reference-marked-historical' ($null -ne $ref -and $ref.StartsWith('# HISTORICAL REFERENCE')) 'reference.md must start with the HISTORICAL REFERENCE banner'

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-AGENTGUIDE: testing guidance wiring is inconsistent" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-AGENTGUIDE: testing guidance wiring is consistent (static check only)" -ForegroundColor Green
exit 0
