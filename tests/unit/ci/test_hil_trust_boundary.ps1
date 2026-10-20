# test_hil_trust_boundary.ps1
# TEST-HILTRUST-*: the physical HIL workstation (self-hosted runner) must not be reachable from untrusted
# GitHub events, and no repository-controlled code may run on it before a trusted HIL preflight.
#
# Characterization test (iteration 1, offline).  It does NOT change CI behaviour and is not listed in any
# Run-Tests-CI suite.  It runs in two layers:
#   FX-*   synthetic workflow fixtures in an isolated temp directory prove the analyzer itself (safe and
#          unsafe cases, matrix expansion, job-level conditions, local reusable-workflow references);
#   REAL-* the same analyzer applied to .github/workflows of this repository.
#
# Model (fail closed):
#   - untrusted events: pull_request, pull_request_target, pull_request_review, pull_request_review_comment,
#     issue_comment, workflow_run; evaluated as a FORK pull request (head repo != base repo, actor = fork owner).
#   - a job leg is "self-hosted" if any runs-on label is 'self-hosted' or is not a GitHub-hosted image label,
#     or if runs-on cannot be evaluated.
#   - a job is reachable unless its job-level `if:` evaluates to false for that context; an expression the
#     evaluator cannot decide counts as reachable.  Step-level `if:` never protects the runner (the job is
#     still scheduled on it).
#   - the trusted HIL preflight is the step with `id: hil-trusted-preflight`.  Every `uses: ./...` step and
#     every `run:` step that references workspace files (scripts, build tools, relative paths) before it is
#     repository-controlled.
#   - YAML is parsed by PyYAML BaseLoader (all scalars are strings; `on:` stays 'on') via `python -I`; no
#     network, no repository execution.
#
# Limitation (documented, not testable here): for pull_request events GitHub takes the workflow file from the
# PR merge ref, so a malicious fork PR can rewrite these files.  This test enforces declared intent only; the
# fork-PR approval setting and runner restrictions must be enforced in GitHub settings.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent

$total = 0; $passed = 0; $failed = 0
function tc_pass { param([string]$n) $script:total++; $script:passed++; Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red }
function tc_check { param([string]$n, [bool]$ok, [string]$why) if ($ok) { tc_pass $n } else { tc_fail $n $why } }

Write-Host ""
Write-Host "TEST-HILTRUST: self-hosted HIL runner trust boundary (offline characterization)" -ForegroundColor Cyan

# ---------------------------------------------------------------------------------------------------------
# YAML loading (PyYAML BaseLoader through an isolated Python)
# ---------------------------------------------------------------------------------------------------------
$Python = $null
foreach ($c in 'python', 'python3', 'py') { $cmd = Get-Command $c -ErrorAction SilentlyContinue; if ($cmd) { $Python = $cmd.Source; break } }
$PyCode = 'import json,sys,yaml' + [Environment]::NewLine +
          'd=yaml.load(open(sys.argv[1],encoding="utf-8"),Loader=yaml.BaseLoader)' + [Environment]::NewLine +
          'sys.stdout.write(json.dumps(d))'

function ConvertTo-Plain($o) {
    if ($null -eq $o) { return $null }
    if ($o -is [System.Management.Automation.PSCustomObject]) {
        $h = [ordered]@{}
        foreach ($p in $o.PSObject.Properties) { $h[$p.Name] = ConvertTo-Plain $p.Value }
        return $h
    }
    if ($o -is [System.Collections.IEnumerable] -and -not ($o -is [string])) { return ,@($o | ForEach-Object { ConvertTo-Plain $_ }) }
    return $o
}

$script:PyScriptFile = $null
function Read-Yaml([string]$Path) {
    if (-not $script:PyScriptFile) {
        $script:PyScriptFile = Join-Path $script:Tmp 'yaml2json.py'
        Set-Content -LiteralPath $script:PyScriptFile -Value $PyCode -Encoding ASCII
    }
    $json = & $Python -I $script:PyScriptFile $Path
    if ($LASTEXITCODE -ne 0) { throw "YAML parse failed for $Path" }
    return ConvertTo-Plain ((($json -join "`n")) | ConvertFrom-Json)
}

# ---------------------------------------------------------------------------------------------------------
# Three-valued GitHub expression evaluator (subset).  $UNK = cannot decide.
# ---------------------------------------------------------------------------------------------------------
$script:UNK = New-Object psobject
function Test-Unk($v) { return [object]::ReferenceEquals($v, $script:UNK) }
function Test-Truthy($v) {
    if ($null -eq $v) { return $false }
    if ($v -is [bool]) { return $v }
    if ($v -is [string]) { return ($v -ne '') }
    if ($v -is [double] -or $v -is [int]) { return ($v -ne 0) }
    return $true
}

function Get-Tokens([string]$Text) {
    $rx = '\G\s*(?:(?<s>''(?:[^'']|'''')*'')|(?<op>==|!=|&&|\|\||<=|>=|[!()<>,\[\]])|(?<n>-?\d+(?:\.\d+)?)|(?<id>[A-Za-z_][A-Za-z0-9_\-]*(?:\.[A-Za-z_*][A-Za-z0-9_\-]*)*))'
    $toks = New-Object System.Collections.Generic.List[object]
    $pos = 0
    while ($pos -lt $Text.Length) {
        if ($Text.Substring($pos).Trim() -eq '') { break }
        $m = [regex]::Match($Text.Substring($pos), $rx)
        if (-not $m.Success -or $m.Length -eq 0) { return $null }
        if ($m.Groups['s'].Success)  { $toks.Add(@{ T = 'str'; V = $m.Groups['s'].Value.Substring(1, $m.Groups['s'].Value.Length - 2).Replace("''", "'") }) }
        elseif ($m.Groups['op'].Success) { $toks.Add(@{ T = 'op'; V = $m.Groups['op'].Value }) }
        elseif ($m.Groups['n'].Success)  { $toks.Add(@{ T = 'num'; V = [double]$m.Groups['n'].Value }) }
        else { $toks.Add(@{ T = 'id'; V = $m.Groups['id'].Value }) }
        $pos += $m.Length
    }
    return ,$toks
}

function Resolve-Id([string]$Id, $Ctx) {
    switch -Regex ($Id) {
        '^(true)$'  { return $true }
        '^(false)$' { return $false }
        '^(null)$'  { return $null }
    }
    if ($Id.StartsWith('matrix.')) {
        if (Test-Unk $Ctx.Leg) { return $script:UNK }
        $k = $Id.Substring(7)
        if ($Ctx.Leg.Contains($k)) { return $Ctx.Leg[$k] }
        return $null
    }
    if ($Ctx.Vars.Contains($Id)) { return $Ctx.Vars[$Id] }
    return $script:UNK
}

function Invoke-Func([string]$Name, [object[]]$A) {
    switch ($Name.ToLowerInvariant()) {
        'success'   { return $true }
        'always'    { return $true }
        'failure'   { return $false }
        'cancelled' { return $false }
        { $_ -in 'startswith', 'endswith', 'contains' } {
            if ($A.Count -ne 2 -or (Test-Unk $A[0]) -or (Test-Unk $A[1])) { return $script:UNK }
            if ($_ -eq 'contains' -and $A[0] -is [System.Collections.IList] -and -not ($A[0] -is [string])) {
                return [bool](@($A[0] | Where-Object { "$_" -ieq "$($A[1])" }).Count)
            }
            $s = "$($A[0])"; $t = "$($A[1])"
            if ($_ -eq 'startswith') { return $s.StartsWith($t, [System.StringComparison]::OrdinalIgnoreCase) }
            if ($_ -eq 'endswith')   { return $s.EndsWith($t, [System.StringComparison]::OrdinalIgnoreCase) }
            return ($s.IndexOf($t, [System.StringComparison]::OrdinalIgnoreCase) -ge 0)
        }
        default { return $script:UNK }
    }
}

function Invoke-Expr([string]$Text, $Ctx) {
    $t = $Text.Trim()
    if ($t -match '^\$\{\{(.*)\}\}$') { $t = $Matches[1] }
    $toks = Get-Tokens $t
    if ($null -eq $toks -or $toks.Count -eq 0) { return $script:UNK }
    $script:ex_t = $toks; $script:ex_i = 0; $script:ex_ctx = $Ctx; $script:ex_bad = $false
    $v = Read-Or
    if ($script:ex_bad -or $script:ex_i -ne $toks.Count) { return $script:UNK }
    return $v
}
function Peek([string]$op) { return ($script:ex_i -lt $script:ex_t.Count -and $script:ex_t[$script:ex_i].T -eq 'op' -and $script:ex_t[$script:ex_i].V -eq $op) }
function Read-Or {
    $l = Read-And
    while (Peek '||') { $script:ex_i++; $r = Read-And
        if (Test-Unk $l) { $l = $script:UNK } elseif (Test-Truthy $l) { } else { $l = $r } }
    return $l
}
function Read-And {
    $l = Read-Cmp
    while (Peek '&&') { $script:ex_i++; $r = Read-Cmp
        if (Test-Unk $l) { if ((-not (Test-Unk $r)) -and -not (Test-Truthy $r)) { $l = $r } else { $l = $script:UNK } }
        elseif (-not (Test-Truthy $l)) { } else { $l = $r } }
    return $l
}
function Read-Cmp {
    $l = Read-Unary
    foreach ($op in '==', '!=') {
        if (Peek $op) {
            $script:ex_i++; $r = Read-Unary
            if ((Test-Unk $l) -or (Test-Unk $r)) { return $script:UNK }
            $eq = ("$l" -ieq "$r")
            if ($op -eq '==') { return $eq } else { return (-not $eq) }
        }
    }
    foreach ($op in '<', '>', '<=', '>=') { if (Peek $op) { $script:ex_i++; [void](Read-Unary); return $script:UNK } }
    return $l
}
function Read-Unary {
    if (Peek '!') { $script:ex_i++; $v = Read-Unary; if (Test-Unk $v) { return $script:UNK }; return (-not (Test-Truthy $v)) }
    return (Read-Primary)
}
function Read-Primary {
    if ($script:ex_i -ge $script:ex_t.Count) { $script:ex_bad = $true; return $script:UNK }
    $tk = $script:ex_t[$script:ex_i]
    if ($tk.T -eq 'op' -and $tk.V -eq '(') {
        $script:ex_i++; $v = Read-Or
        if (Peek ')') { $script:ex_i++ } else { $script:ex_bad = $true }
        return $v
    }
    $script:ex_i++
    if ($tk.T -eq 'str' -or $tk.T -eq 'num') { return $tk.V }
    if ($tk.T -eq 'id') {
        if (Peek '(') {
            $script:ex_i++; $args2 = @()
            if (-not (Peek ')')) { $args2 += ,(Read-Or); while (Peek ',') { $script:ex_i++; $args2 += ,(Read-Or) } }
            if (Peek ')') { $script:ex_i++ } else { $script:ex_bad = $true }
            return (Invoke-Func $tk.V $args2)
        }
        if (Peek '[') { $script:ex_bad = $true; return $script:UNK }
        return (Resolve-Id $tk.V $script:ex_ctx)
    }
    $script:ex_bad = $true; return $script:UNK
}

# Interpolated string value ('${{ x }}' or 'a-${{ x }}' or literal)
function Resolve-Value($Val, $Ctx) {
    if ($null -eq $Val) { return $null }
    if ($Val -is [string]) {
        if ($Val -match '^\s*\$\{\{.*\}\}\s*$' -and ([regex]::Matches($Val, '\$\{\{')).Count -eq 1) { return (Invoke-Expr $Val $Ctx) }
        if ($Val -notmatch '\$\{\{') { return $Val }
        $out = $Val; $unk = $false
        foreach ($m in [regex]::Matches($Val, '\$\{\{(.*?)\}\}')) {
            $r = Invoke-Expr $m.Groups[1].Value $Ctx
            if (Test-Unk $r) { $unk = $true; break }
            $out = $out.Replace($m.Value, "$r")
        }
        if ($unk) { return $script:UNK }
        return $out
    }
    if ($Val -is [System.Collections.IList]) {
        $items = @()
        foreach ($x in $Val) { $r = Resolve-Value $x $Ctx; if (Test-Unk $r) { return $script:UNK }; $items += ,$r }
        return ,$items
    }
    return $Val
}

# ---------------------------------------------------------------------------------------------------------
# Matrix expansion (GitHub semantics: product of base keys, exclude, then include)
# ---------------------------------------------------------------------------------------------------------
function Expand-Matrix($Matrix) {
    if ($null -eq $Matrix) { return ,@([ordered]@{}) }
    if ($Matrix -is [string]) { return ,@($script:UNK) }
    $base = @($Matrix.Keys | Where-Object { $_ -ne 'include' -and $_ -ne 'exclude' })
    $legs = @([ordered]@{})
    foreach ($k in $base) {
        $vals = $Matrix[$k]
        if ($vals -is [string]) { if ($vals -match '\$\{\{') { return ,@($script:UNK) }; $vals = @($vals) }
        $next = @()
        foreach ($leg in $legs) { foreach ($v in @($vals)) { $n = [ordered]@{}; foreach ($kk in $leg.Keys) { $n[$kk] = $leg[$kk] }; $n[$k] = $v; $next += ,$n } }
        $legs = $next
    }
    if ($base.Count -eq 0) { $legs = @() }
    if ($Matrix.Contains('exclude')) {
        foreach ($ex in @($Matrix['exclude'])) {
            $legs = @($legs | Where-Object { $l = $_; @($ex.Keys | Where-Object { "$($l[$_])" -ne "$($ex[$_])" }).Count -gt 0 })
        }
    }
    $origCount = $legs.Count
    if ($Matrix.Contains('include')) {
        foreach ($inc in @($Matrix['include'])) {
            if ($inc -is [string]) { return ,@($script:UNK) }
            $merged = $false
            for ($i = 0; $i -lt $origCount; $i++) {
                $conflict = @($inc.Keys | Where-Object { ($base -contains $_) -and ("$($legs[$i][$_])" -ne "$($inc[$_])") }).Count -gt 0
                if (-not $conflict) { foreach ($k in $inc.Keys) { $legs[$i][$k] = $inc[$k] }; $merged = $true }
            }
            if (-not $merged) { $n = [ordered]@{}; foreach ($k in $inc.Keys) { $n[$k] = $inc[$k] }; $legs += ,$n }
        }
    }
    if ($legs.Count -eq 0) { $legs = @([ordered]@{}) }
    return ,$legs
}

# ---------------------------------------------------------------------------------------------------------
# Workflow analysis
# ---------------------------------------------------------------------------------------------------------
$UntrustedEvents = @('pull_request', 'pull_request_target', 'pull_request_review', 'pull_request_review_comment', 'issue_comment', 'workflow_run')
$HostedLabelRx = '^(ubuntu|windows|macos)-[A-Za-z0-9.\-]+$'
# Repository-controlled: relative/workspace script paths, repository directories, or build tools run on the tree.
$RepoCodeRx = '(^|[\s''"(&;|])\.{1,2}[\\/]|(^|[\s''"(&;|])(tools|tests|scripts|src|build)[\\/]|\b(msbuild|nmake|cmake|make|cl|devenv|vstest\.console)(\.exe)?\s'

function Get-Events($Wf) {
    $on = $Wf['on']
    if ($null -eq $on) { return @() }
    if ($on -is [string]) { return @($on) }
    if ($on -is [System.Collections.IList]) { return @($on) }
    return @($on.Keys)
}

function New-Ctx([string]$EventName, $Leg) {
    $v = @{
        'github.event_name' = $EventName; 'github.repository' = 'zarfld/IntelAvbFilter'; 'github.repository_owner' = 'zarfld'
        'github.actor' = 'untrusted-fork'; 'github.triggering_actor' = 'untrusted-fork'
        'github.event.pull_request.head.repo.full_name' = 'untrusted-fork/IntelAvbFilter'
        'github.event.pull_request.head.repo.fork' = 'true'; 'github.ref' = 'refs/pull/1/merge'
        'github.head_ref' = 'untrusted-branch'; 'github.base_ref' = 'master'
        'runner.os' = 'Windows'
    }
    return @{ Vars = $v; Leg = $Leg }
}

function Get-LegLabels($Job, $Ctx) {
    if (-not $Job.Contains('runs-on')) { return $script:UNK }
    $ro = $Job['runs-on']
    if ($ro -is [System.Collections.IDictionary]) {
        if ($ro.Contains('group')) { return ,@('runner-group:' + "$($ro['group'])") }
        if ($ro.Contains('labels')) { $ro = $ro['labels'] } else { return $script:UNK }
    }
    $r = Resolve-Value $ro $Ctx
    if (Test-Unk $r) { return $script:UNK }
    $labels = @(@($r) | Where-Object { $null -ne $_ -and "$_" -ne '' } | ForEach-Object { "$_" })
    if ($labels.Count -eq 0) { return $script:UNK }
    return ,$labels
}
function Test-SelfHostedLabels($Labels) {
    if (Test-Unk $Labels) { return $true }
    foreach ($l in $Labels) { if ($l -ieq 'self-hosted' -or $l -notmatch $HostedLabelRx) { return $true } }
    return $false
}

function Get-StepOrderFinding($Steps, $Ctx) {
    # Steps whose step-level `if:` is false on this leg never run there; undecidable counts as running.
    $steps = @(@($Steps) | Where-Object {
        if (-not ($_ -is [System.Collections.IDictionary]) -or -not $_.Contains('if')) { return $true }
        $g = Invoke-Expr "$($_['if'])" $Ctx
        return ((Test-Unk $g) -or (Test-Truthy $g)) })
    $pre = -1
    for ($i = 0; $i -lt $steps.Count; $i++) { if ($steps[$i] -is [System.Collections.IDictionary] -and $steps[$i].Contains('id') -and $steps[$i]['id'] -eq 'hil-trusted-preflight') { $pre = $i; break } }
    $limit = if ($pre -ge 0) { $pre } else { $steps.Count }
    $repo = @()
    for ($i = 0; $i -lt $limit; $i++) {
        $s = $steps[$i]
        $nm = if ($s.Contains('name')) { $s['name'] } elseif ($s.Contains('uses')) { $s['uses'] } else { "step[$i]" }
        if ($s.Contains('uses') -and "$($s['uses'])" -match '^\./') { $repo += "$nm (local action)"; continue }
        if ($s.Contains('run') -and [regex]::IsMatch("$($s['run'])", $RepoCodeRx)) { $repo += "$nm" }
    }
    return @{ HasPreflight = ($pre -ge 0); RepoBefore = $repo }
}

# Returns a list of findings: Kind = Reachable | Order | External
function Get-WorkflowFindings([string]$Root, [string]$Rel, [string[]]$InheritedEvents = $null, [string]$Via = '') {
    $wf = Read-Yaml (Join-Path $Root $Rel)
    $events = if ($InheritedEvents) { $InheritedEvents } else { @(Get-Events $wf) }
    $findings = @()
    if (-not $wf.Contains('jobs')) { return $findings }
    foreach ($jobId in $wf['jobs'].Keys) {
        $job = $wf['jobs'][$jobId]
        $jobName = if ($Via) { "$Via -> $Rel/$jobId" } else { "$Rel/$jobId" }
        if ($job.Contains('uses')) {
            $u = "$($job['uses'])"
            foreach ($ev in $events) {
                $ctx = New-Ctx $ev ([ordered]@{})
                $gate = $true; if ($job.Contains('if')) { $g = Invoke-Expr "$($job['if'])" $ctx; $gate = (Test-Unk $g) -or (Test-Truthy $g) }
                if (-not $gate) { continue }
                if ($u -match '^\./(.+)$') {
                    $findings += @(Get-WorkflowFindings $Root ($Matches[1]) @($ev) $jobName)
                } elseif ($UntrustedEvents -contains $ev) {
                    $findings += [pscustomobject]@{ Kind = 'External'; Job = $jobName; Leg = ''; Event = $ev; Detail = "external reusable workflow $u cannot be verified" }
                }
            }
            continue
        }
        $legs = Expand-Matrix $(if ($job.Contains('strategy') -and $job['strategy'] -is [System.Collections.IDictionary] -and $job['strategy'].Contains('matrix')) { $job['strategy']['matrix'] } else { $null })
        foreach ($leg in $legs) {
            $legDesc = if (Test-Unk $leg) { '<unevaluable matrix>' } else { (@($leg.Keys | ForEach-Object { "$_=$($leg[$_])" }) -join ',') }
            $selfHostedAny = $false
            foreach ($ev in $events) {
                $ctx = New-Ctx $ev $leg
                $labels = Get-LegLabels $job $ctx
                if (-not (Test-SelfHostedLabels $labels)) { continue }
                $selfHostedAny = $true
                if ($UntrustedEvents -notcontains $ev) { continue }
                $reach = $true
                if ($job.Contains('if')) { $g = Invoke-Expr "$($job['if'])" $ctx; $reach = (Test-Unk $g) -or (Test-Truthy $g) }
                if ($reach) {
                    $lbl = if (Test-Unk $labels) { '<unevaluable runs-on>' } else { ($labels -join ',') }
                    $findings += [pscustomobject]@{ Kind = 'Reachable'; Job = $jobName; Leg = $legDesc; Event = $ev; Detail = "runs-on [$lbl]" }
                }
            }
            if ($selfHostedAny -and $job.Contains('steps')) {
                $o = Get-StepOrderFinding $job['steps'] (New-Ctx 'push' $leg)
                if ((-not $o.HasPreflight) -or $o.RepoBefore.Count -gt 0) {
                    $pf = if ($o.HasPreflight) { 'before hil-trusted-preflight' } else { 'no hil-trusted-preflight step; all repo steps unguarded' }
                    $findings += [pscustomobject]@{ Kind = 'Order'; Job = $jobName; Leg = $legDesc; Event = '*'; Detail = ("{0}: {1}" -f $pf, ($o.RepoBefore -join ' | ')) }
                }
            }
        }
    }
    return $findings
}

function Show-Findings($F) { return (@(@($F) | Where-Object { $null -ne $_ } | ForEach-Object { "[{0}] {1} {{{2}}} @{3}: {4}" -f $_.Kind, $_.Job, $_.Leg, $_.Event, $_.Detail }) -join ' || ') }

# ---------------------------------------------------------------------------------------------------------
# Preconditions
# ---------------------------------------------------------------------------------------------------------
$script:Tmp = Join-Path $env:TEMP ("hiltrust_{0}" -f (Get-Random))
New-Item -ItemType Directory -Path $script:Tmp -Force | Out-Null
$pyOk = $false
if ($Python) { & $Python -I -c 'import yaml' 2>$null; $pyOk = ($LASTEXITCODE -eq 0) }
if (-not $pyOk) {
    tc_fail 'TEST-HILTRUST-000-yaml-parser-available' 'python with PyYAML is required (no silent pass)'
    Remove-Item $script:Tmp -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
    exit 1
}
tc_pass 'TEST-HILTRUST-000-yaml-parser-available'

# ---------------------------------------------------------------------------------------------------------
# FX: synthetic fixtures (prove the analyzer)
# ---------------------------------------------------------------------------------------------------------
$Fx = Join-Path $script:Tmp 'fx'
$FxWf = Join-Path $Fx '.github\workflows'
New-Item -ItemType Directory -Path $FxWf -Force | Out-Null
function New-Fx([string]$Name, [string]$Yaml) { Set-Content -LiteralPath (Join-Path $FxWf $Name) -Value $Yaml -Encoding UTF8; return ".github/workflows/$Name" }

$preflightStep = @"
      - id: hil-trusted-preflight
        run: C:\hil-trusted\Invoke-HilPreflight.ps1
"@

$fx01 = New-Fx 'fx01_safe.yml' @"
on:
  workflow_dispatch:
jobs:
  hil:
    runs-on: [self-hosted, Windows, i226]
    steps:
$preflightStep
      - uses: actions/checkout@v5
      - run: .\tools\build\Build-Tests.ps1
"@
$fx02 = New-Fx 'fx02_pr_direct.yml' @"
on: [pull_request]
jobs:
  hil:
    runs-on: [self-hosted, Windows, i226]
    steps:
$preflightStep
"@
$fx03 = New-Fx 'fx03_pr_matrix.yml' @"
on:
  pull_request:
    branches: [master]
jobs:
  unit:
    runs-on: `${{ matrix.runner || matrix.os }}
    strategy:
      matrix:
        os: [windows-latest, windows-2022]
        config: [Debug, Release]
        include:
          - os: self-hosted-hardware
            runner: [self-hosted, Windows, i226]
            config: Debug
    steps:
$preflightStep
"@
$fx04 = New-Fx 'fx04_pr_guarded.yml' @"
on: [pull_request, workflow_dispatch]
jobs:
  hil:
    if: github.event_name == 'workflow_dispatch'
    runs-on: [self-hosted, Windows, i226]
    steps:
$preflightStep
"@
$fx05 = New-Fx 'fx05_pr_weak_guard.yml' @"
on: [pull_request, schedule]
jobs:
  hil:
    if: `${{ github.event_name != 'schedule' }}
    runs-on: [self-hosted, Windows, i226]
    steps:
$preflightStep
"@
$fx06 = New-Fx 'fx06_pr_same_repo_guard.yml' @"
on: [pull_request]
jobs:
  hil:
    if: github.event.pull_request.head.repo.full_name == github.repository
    runs-on: [self-hosted, Windows, i226]
    steps:
$preflightStep
"@
$fx07 = New-Fx 'fx07_pr_unknown_guard.yml' @"
on: [pull_request]
jobs:
  hil:
    if: contains(fromJSON(vars.HIL_ACTORS), github.actor)
    runs-on: [self-hosted, Windows, i226]
    steps:
$preflightStep
"@
New-Fx 'fx08_callee.yml' @"
on:
  workflow_call:
jobs:
  inner:
    runs-on: [self-hosted, Windows, i226]
    steps:
$preflightStep
"@ | Out-Null
$fx08 = New-Fx 'fx08_caller.yml' @"
on: [pull_request]
jobs:
  call:
    uses: ./.github/workflows/fx08_callee.yml
"@
$fx09 = New-Fx 'fx09_order.yml' @"
on: [workflow_dispatch]
jobs:
  hil:
    runs-on: [self-hosted, Windows, i226]
    steps:
      - uses: actions/checkout@v5
      - name: Build tests
        run: .\tools\build\Build-Tests.ps1 -Configuration Debug
$preflightStep
"@
$fx10 = New-Fx 'fx10_unevaluable.yml' @"
on: [pull_request]
jobs:
  hil:
    runs-on: `${{ fromJSON(inputs.labels) }}
    steps:
$preflightStep
"@
$fx11 = New-Fx 'fx11_pr_target.yml' @"
on:
  pull_request_target:
jobs:
  hil:
    runs-on: [self-hosted, Windows]
    steps:
$preflightStep
"@
$fx12 = New-Fx 'fx12_custom_label.yml' @"
on: [pull_request]
jobs:
  hil:
    runs-on: hil-box
    steps:
$preflightStep
"@
$fx13 = New-Fx 'fx13_hosted_only_pr.yml' @"
on: [pull_request]
jobs:
  build:
    runs-on: `${{ matrix.os }}
    strategy:
      matrix:
        os: [windows-latest, ubuntu-latest]
    steps:
      - run: .\tools\build\Build-Tests.ps1
"@

$fx14 = New-Fx 'fx14_step_if_and_system_tool.yml' @"
on: [workflow_dispatch]
jobs:
  hil:
    runs-on: `${{ matrix.runner }}
    strategy:
      matrix:
        include:
          - runner: [self-hosted, Windows, i226]
            hardware: true
    steps:
      - uses: actions/checkout@v5
      - name: Linux-only build
        if: runner.os == 'Linux'
        run: ./tools/build/build.sh
      - name: Hosted-only build
        if: matrix.hardware != true
        run: .\tools\build\Build-Tests.ps1
      - name: System tool probe
        run: '& "`${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest'
$preflightStep
"@

function Get-Kind($F, [string]$Kind) { return ,@(@($F) | Where-Object { $null -ne $_ -and $_.Kind -eq $Kind }) }

$f = Get-WorkflowFindings $Fx $fx01
tc_check 'TEST-HILTRUST-FX-001-dispatch-only-with-trusted-preflight-first-has-no-findings' (@($f).Count -eq 0) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx02
tc_check 'TEST-HILTRUST-FX-002-pull-request-to-self-hosted-is-reachable' ((Get-Kind $f 'Reachable').Count -eq 1) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx03
$r3 = Get-Kind $f 'Reachable'
tc_check 'TEST-HILTRUST-FX-003-matrix-include-self-hosted-leg-is-reachable-hosted-legs-are-not' `
    (($r3.Count -eq 1) -and ($r3[0].Leg -match 'os=self-hosted-hardware') -and ($r3[0].Detail -match 'self-hosted,Windows,i226')) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx04
tc_check 'TEST-HILTRUST-FX-004-job-if-event-equals-dispatch-excludes-pull-request' ((Get-Kind $f 'Reachable').Count -eq 0) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx05
tc_check 'TEST-HILTRUST-FX-005-job-if-not-equal-schedule-does-not-exclude-pull-request' ((Get-Kind $f 'Reachable').Count -eq 1) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx06
tc_check 'TEST-HILTRUST-FX-006-same-repo-head-guard-excludes-fork-pull-request' ((Get-Kind $f 'Reachable').Count -eq 0) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx07
tc_check 'TEST-HILTRUST-FX-007-undecidable-guard-fails-closed-as-reachable' ((Get-Kind $f 'Reachable').Count -eq 1) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx08
$r8 = Get-Kind $f 'Reachable'
tc_check 'TEST-HILTRUST-FX-008-local-reusable-workflow-reference-inherits-pull-request' `
    (($r8.Count -eq 1) -and ($r8[0].Job -match 'fx08_caller\.yml/call -> .*fx08_callee\.yml/inner')) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx09
$o9 = Get-Kind $f 'Order'
tc_check 'TEST-HILTRUST-FX-009-repo-script-before-trusted-preflight-is-an-order-finding' `
    (($o9.Count -eq 1) -and ($o9[0].Detail -match 'Build tests') -and (Get-Kind $f 'Reachable').Count -eq 0) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx10
$r10 = Get-Kind $f 'Reachable'
tc_check 'TEST-HILTRUST-FX-010-unevaluable-runs-on-fails-closed' (($r10.Count -eq 1) -and ($r10[0].Detail -match 'unevaluable')) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx11
tc_check 'TEST-HILTRUST-FX-011-pull-request-target-to-self-hosted-is-reachable' ((Get-Kind $f 'Reachable').Count -eq 1) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx12
tc_check 'TEST-HILTRUST-FX-012-non-hosted-custom-label-treated-as-self-hosted' ((Get-Kind $f 'Reachable').Count -eq 1) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx13
tc_check 'TEST-HILTRUST-FX-013-github-hosted-only-pull-request-job-has-no-findings' (@($f).Count -eq 0) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx14
tc_check 'TEST-HILTRUST-FX-014-steps-excluded-by-step-if-and-system-tools-are-not-repo-code' (@($f).Count -eq 0) (Show-Findings $f)

# ---------------------------------------------------------------------------------------------------------
# REAL: this repository's workflows (characterization; RED is the expected current result)
# ---------------------------------------------------------------------------------------------------------
$realFindings = @()
$wfDir = Join-Path $RepoRoot '.github\workflows'
foreach ($wfFile in @(Get-ChildItem $wfDir -File | Where-Object { $_.Extension -in '.yml', '.yaml' } | Sort-Object Name)) {
    $wfData = Read-Yaml $wfFile.FullName
    $evs = @(Get-Events $wfData)
    if ($evs -contains 'workflow_call' -and $evs.Count -eq 1) { continue }   # analyzed through its callers
    $realFindings += @(Get-WorkflowFindings $RepoRoot ".github/workflows/$($wfFile.Name)")
}
$realReach = Get-Kind $realFindings 'Reachable'
$realOrder = Get-Kind $realFindings 'Order'
$realExt   = Get-Kind $realFindings 'External'

Write-Host ""
Write-Host "  REAL findings (repository .github/workflows):" -ForegroundColor Yellow
foreach ($x in $realFindings) { Write-Host ("    [{0}] {1} {{{2}}} @{3}: {4}" -f $x.Kind, $x.Job, $x.Leg, $x.Event, $x.Detail) -ForegroundColor Yellow }

tc_check 'TEST-HILTRUST-REAL-001-no-self-hosted-leg-reachable-from-untrusted-events' ($realReach.Count -eq 0) `
    ("{0} reachable self-hosted leg(s)" -f $realReach.Count)
tc_check 'TEST-HILTRUST-REAL-002-no-repository-code-before-trusted-hil-preflight' ($realOrder.Count -eq 0) `
    ("{0} self-hosted leg(s) run repository code without/before a trusted preflight" -f $realOrder.Count)
tc_check 'TEST-HILTRUST-REAL-003-no-unverifiable-external-workflow-on-untrusted-events' ($realExt.Count -eq 0) `
    ("{0} external reusable workflow reference(s)" -f $realExt.Count)

Remove-Item $script:Tmp -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-HILTRUST: HIL trust boundary violations (characterization)" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-HILTRUST: HIL trust boundary holds" -ForegroundColor Green
exit 0
