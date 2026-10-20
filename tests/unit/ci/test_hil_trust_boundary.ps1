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
#   - every trigger event is evaluated in explicit contexts:
#       pull_request(_target/_review/_review_comment): a FORK PR and a SAME-REPOSITORY PR  -> Reachable;
#       issue_comment                                                                     -> Reachable;
#       workflow_run, by triggering run: push to master of zarfld/IntelAvbFilter that succeeded (Authorized,
#             informational - the only HIL path); fork PR / same-repository PR (Reachable); failed upstream
#             (UpstreamFailed); feature branch / tag (UnauthorizedRef); fork repository (ForeignRepo);
#       workflow_dispatch on any ref                                    -> Dispatch (no manual HIL path);
#       any other event (push, schedule, release, ...) on master and on a feature branch  -> Automatic (a direct
#             push would bypass the CI + deploy-staging gate).
#   - every self-hosted job needs explicit read-only permissions, an environment, serialized matrix legs
#     (strategy.max-parallel 1), no cancelling concurrency group, no continue-on-error, and checkout without
#     persisted credentials (Governance); missing = violation.
#   - REAL-011 simulates the job graph (GitHub needs/skip semantics); REAL-012 executes hil.yml's authorization
#     script against a localhost fixture API and its acceptance script against fixture evidence, so a failed CI, a
#     skipped deploy-staging, a PR, a foreign repository, a wrong ref/SHA or stale evidence cannot pass.
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
          'd=yaml.load(open(sys.argv[1],encoding="utf-8-sig"),Loader=yaml.BaseLoader)' + [Environment]::NewLine +
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
        # Job-graph simulation (REAL-011) supplies the needs outcome; elsewhere: the run is not failing.
        'success'   { if ($null -ne $script:ex_ctx -and $script:ex_ctx.Vars.Contains('__success')) { return $script:ex_ctx.Vars['__success'] }; return $true }
        'always'    { return $true }
        'failure'   { if ($null -ne $script:ex_ctx -and $script:ex_ctx.Vars.Contains('__failure')) { return $script:ex_ctx.Vars['__failure'] }; return $false }
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
$PullRequestEvents = @('pull_request', 'pull_request_target', 'pull_request_review', 'pull_request_review_comment')
$UntrustedEvents = $PullRequestEvents + @('issue_comment', 'workflow_run')
$OwnRepo = 'zarfld/IntelAvbFilter'
$AuthorizedRef = 'refs/heads/master'
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

# Context variants: 'fork' / 'same-repo' (pull requests), or a trusted-looking context with explicit repo + ref.
function New-Ctx([string]$EventName, $Leg, [string]$Variant = 'fork', [string]$Ref = '', [string]$Repository = '') {
    $v = @{
        'github.event_name' = $EventName; 'github.repository' = $OwnRepo; 'github.repository_owner' = 'zarfld'
        'github.actor' = 'untrusted-fork'; 'github.triggering_actor' = 'untrusted-fork'
        'github.event.pull_request.head.repo.full_name' = 'untrusted-fork/IntelAvbFilter'
        'github.event.pull_request.head.repo.fork' = 'true'; 'github.ref' = 'refs/pull/1/merge'
        'github.head_ref' = 'untrusted-branch'; 'github.base_ref' = 'master'
        'runner.os' = 'Windows'
    }
    if ($Variant -eq 'same-repo') {
        $v['github.event.pull_request.head.repo.full_name'] = $OwnRepo; $v['github.event.pull_request.head.repo.fork'] = 'false'
        $v['github.actor'] = 'collaborator'; $v['github.triggering_actor'] = 'collaborator'
    }
    if ($Variant -eq 'trusted') {
        foreach ($k in 'github.event.pull_request.head.repo.full_name', 'github.event.pull_request.head.repo.fork', 'github.head_ref', 'github.base_ref') { $v.Remove($k) }
        $v['github.actor'] = 'zarfld'; $v['github.triggering_actor'] = 'zarfld'
    }
    if ($Ref) { $v['github.ref'] = $Ref }
    if ($Repository) { $v['github.repository'] = $Repository; $v['github.repository_owner'] = ($Repository -split '/')[0] }
    return @{ Vars = $v; Leg = $Leg }
}

# workflow_run context: this repository runs the workflow on its default branch; the TRIGGERING run is described by
# github.event.workflow_run.* (event, head_branch, conclusion, head_repository).  needs.* outputs stay undecidable.
function New-WrCtx($Leg, [string]$UpEvent, [string]$UpBranch, [string]$UpConclusion, [string]$HeadRepo = $OwnRepo, [string]$Repository = $OwnRepo) {
    $x = New-Ctx 'workflow_run' $Leg 'trusted' $AuthorizedRef $Repository
    $x.Vars['github.event.workflow_run.event'] = $UpEvent
    $x.Vars['github.event.workflow_run.head_branch'] = $UpBranch
    $x.Vars['github.event.workflow_run.conclusion'] = $UpConclusion
    $x.Vars['github.event.workflow_run.head_repository.full_name'] = $HeadRepo
    $x.Vars['github.event.workflow_run.repository.full_name'] = $Repository
    return $x
}

# Contexts to evaluate per trigger event; Kind = finding kind if the self-hosted job is reachable in it.
function Get-EventContexts([string]$EventName, $Leg) {
    $c = @()
    $fork = 'untrusted-fork/IntelAvbFilter'
    if ($PullRequestEvents -contains $EventName) {
        $c += @{ Kind = 'Reachable'; Label = 'fork-pr';      Ctx = (New-Ctx $EventName $Leg 'fork') }
        $c += @{ Kind = 'Reachable'; Label = 'same-repo-pr'; Ctx = (New-Ctx $EventName $Leg 'same-repo') }
    } elseif ($EventName -eq 'workflow_run') {
        # The only HIL path: the upstream CI completed successfully for a push to master of this repository (the
        # deploy-staging check lives in the authorize job and is exercised by REAL-012).
        $c += @{ Kind = 'Authorized';      Label = 'upstream-master-push-success'; Ctx = (New-WrCtx $Leg 'push' 'master' 'success') }
        $c += @{ Kind = 'Reachable';       Label = 'upstream-fork-pr';             Ctx = (New-WrCtx $Leg 'pull_request' 'master' 'success' $fork) }
        $c += @{ Kind = 'Reachable';       Label = 'upstream-same-repo-pr';        Ctx = (New-WrCtx $Leg 'pull_request' 'feature/x' 'success') }
        # A same-repository PR FROM master (e.g. master -> develop) has head_branch 'master' and this head repository.
        $c += @{ Kind = 'Reachable';       Label = 'upstream-same-repo-pr-from-master'; Ctx = (New-WrCtx $Leg 'pull_request' 'master' 'success') }
        $c += @{ Kind = 'UpstreamFailed';  Label = 'upstream-master-push-failure'; Ctx = (New-WrCtx $Leg 'push' 'master' 'failure') }
        $c += @{ Kind = 'UnauthorizedRef'; Label = 'upstream-feature-push';        Ctx = (New-WrCtx $Leg 'push' 'feature/x' 'success') }
        $c += @{ Kind = 'UnauthorizedRef'; Label = 'upstream-tag-push';            Ctx = (New-WrCtx $Leg 'push' 'v1.0.0' 'success') }
        $c += @{ Kind = 'ForeignRepo';     Label = 'fork-repository';              Ctx = (New-WrCtx $Leg 'push' 'master' 'success' $fork $fork) }
    } elseif ($UntrustedEvents -contains $EventName) {
        $c += @{ Kind = 'Reachable'; Label = 'untrusted';    Ctx = (New-Ctx $EventName $Leg 'fork') }
    } elseif ($EventName -eq 'workflow_dispatch') {
        # No manual HIL path: a dispatch must never reach the physical runner, on any ref.
        $c += @{ Kind = 'Dispatch'; Label = 'master';          Ctx = (New-Ctx $EventName $Leg 'trusted' $AuthorizedRef) }
        $c += @{ Kind = 'Dispatch'; Label = 'feature-branch';  Ctx = (New-Ctx $EventName $Leg 'trusted' 'refs/heads/feature/x') }
        $c += @{ Kind = 'Dispatch'; Label = 'tag';             Ctx = (New-Ctx $EventName $Leg 'trusted' 'refs/tags/v1.0.0') }
        $c += @{ Kind = 'Dispatch'; Label = 'fork-repository'; Ctx = (New-Ctx $EventName $Leg 'trusted' $AuthorizedRef 'untrusted-fork/IntelAvbFilter') }
    } else {
        # schedule, release, repository_dispatch, merge_group, ...: never a trusted-revision decision
        $c += @{ Kind = 'Automatic'; Label = 'master';         Ctx = (New-Ctx $EventName $Leg 'trusted' $AuthorizedRef) }
        $c += @{ Kind = 'Automatic'; Label = 'feature-branch'; Ctx = (New-Ctx $EventName $Leg 'trusted' 'refs/heads/feature/x') }
    }
    return ,$c
}

# Governance of a self-hosted job: explicit read-only token, an environment (deployment-branch policy), serialized
# legs, failures never masked, no persisted git credentials.  Missing or non-map values fail closed.
# Serialization: a job-level concurrency group would CANCEL pending matrix legs (GitHub keeps one pending entry
# per group), so matrix legs must be serialized with strategy.max-parallel 1; a concurrency group, if present,
# must not cancel.
function Get-GovernanceIssues($Wf, $Job) {
    $issues = @()
    $perm = if ($Job.Contains('permissions')) { $Job['permissions'] } elseif ($Wf.Contains('permissions')) { $Wf['permissions'] } else { $null }
    if ($null -eq $perm) { $issues += 'no explicit token permissions (repository default applies)' }
    elseif (-not ($perm -is [System.Collections.IDictionary])) { $issues += "permissions '$perm' is not an explicit least-privilege map" }
    else { foreach ($k in $perm.Keys) { if (@('read', 'none') -notcontains "$($perm[$k])") { $issues += "permission ${k}: $($perm[$k])" } } }
    if (-not $Job.Contains('environment')) { $issues += 'no environment (no deployment-branch policy)' }
    $conc = if ($Job.Contains('concurrency')) { $Job['concurrency'] } elseif ($Wf.Contains('concurrency')) { $Wf['concurrency'] } else { $null }
    if ($null -ne $conc -and -not ($conc -is [System.Collections.IDictionary] -and "$($conc['cancel-in-progress'])" -eq 'false')) { $issues += "concurrency cancel-in-progress is not 'false' (must never cancel a HIL job)" }
    $strat = if ($Job.Contains('strategy') -and $Job['strategy'] -is [System.Collections.IDictionary]) { $Job['strategy'] } else { $null }
    $legCount = 1
    if ($null -ne $strat -and $strat.Contains('matrix')) { $mLegs = Expand-Matrix $strat['matrix']; $legCount = @($mLegs).Count }   # Expand-Matrix returns ,$legs
    if ($legCount -gt 1 -and -not ($null -ne $strat -and "$($strat['max-parallel'])" -eq '1')) { $issues += "matrix legs not serialized ($legCount legs; strategy.max-parallel must be 1)" }
    if ($Job.Contains('continue-on-error') -and "$($Job['continue-on-error'])" -ne 'false') { $issues += "job continue-on-error '$($Job['continue-on-error'])' masks HIL failures" }
    if ($Job.Contains('steps')) {
        foreach ($s in @($Job['steps'])) {
            if ($s -is [System.Collections.IDictionary] -and $s.Contains('continue-on-error') -and "$($s['continue-on-error'])" -ne 'false') { $issues += "step continue-on-error masks a HIL failure ($(if ($s.Contains('name')) { $s['name'] } elseif ($s.Contains('uses')) { $s['uses'] } else { "$($s['run'])".Trim() }))" }
            if ($s -is [System.Collections.IDictionary] -and $s.Contains('uses') -and "$($s['uses'])" -match '^actions/checkout@') {
                $w = if ($s.Contains('with')) { $s['with'] } else { $null }
                if (-not ($w -is [System.Collections.IDictionary] -and "$($w['persist-credentials'])" -eq 'false')) { $issues += "checkout persists git credentials ($($s['uses']))" }
            }
        }
    }
    return ,$issues
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

# Returns findings.  Kind: Reachable (untrusted/PR event or PR-triggered upstream) | Automatic (push/schedule/...) |
# UpstreamFailed | UnauthorizedRef (non-master upstream) | ForeignRepo | Dispatch (manual dispatch) | Order |
# Governance | External | Authorized (informational: workflow_run after a successful master push; not a violation).
function Get-WorkflowFindings([string]$Root, [string]$Rel, [string[]]$InheritedEvents = $null, [string]$Via = '', $CallerJob = $null) {
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
                $anyCtx = @(Get-EventContexts $ev ([ordered]@{}) | Where-Object {
                    if (-not $job.Contains('if')) { return $true }
                    $g = Invoke-Expr "$($job['if'])" $_.Ctx; return ((Test-Unk $g) -or (Test-Truthy $g)) })
                if ($anyCtx.Count -eq 0) { continue }
                if ($u -match '^\./(.+)$') {
                    $findings += @(Get-WorkflowFindings $Root ($Matches[1]) @($ev) $jobName $job)
                } elseif ($ev -ne 'workflow_dispatch') {
                    $findings += [pscustomobject]@{ Kind = 'External'; Job = $jobName; Leg = ''; Event = $ev; Detail = "external reusable workflow $u cannot be verified" }
                }
            }
            continue
        }
        $legs = Expand-Matrix $(if ($job.Contains('strategy') -and $job['strategy'] -is [System.Collections.IDictionary] -and $job['strategy'].Contains('matrix')) { $job['strategy']['matrix'] } else { $null })
        $jobSelfHosted = $false
        foreach ($leg in $legs) {
            $legDesc = if (Test-Unk $leg) { '<unevaluable matrix>' } else { (@($leg.Keys | ForEach-Object { "$_=$($leg[$_])" }) -join ',') }
            $selfHostedAny = $false
            foreach ($ev in $events) {
                $byKind = [ordered]@{}
                $lbl = ''
                foreach ($c in (Get-EventContexts $ev $leg)) {
                    $labels = Get-LegLabels $job $c.Ctx
                    if (-not (Test-SelfHostedLabels $labels)) { continue }
                    $selfHostedAny = $true
                    $lbl = if (Test-Unk $labels) { '<unevaluable runs-on>' } else { ($labels -join ',') }
                    $reach = $true
                    foreach ($g0 in @($CallerJob, $job)) {
                        if ($null -ne $g0 -and $g0.Contains('if')) { $g = Invoke-Expr "$($g0['if'])" $c.Ctx; if (-not ((Test-Unk $g) -or (Test-Truthy $g))) { $reach = $false } }
                    }
                    if ($reach) { if (-not $byKind.Contains($c.Kind)) { $byKind[$c.Kind] = @() }; $byKind[$c.Kind] += $c.Label }
                }
                foreach ($k in $byKind.Keys) {
                    $findings += [pscustomobject]@{ Kind = $k; Job = $jobName; Leg = $legDesc; Event = ("{0}[{1}]" -f $ev, ($byKind[$k] -join ',')); Detail = "runs-on [$lbl]" }
                }
            }
            if ($selfHostedAny) { $jobSelfHosted = $true }
            if ($selfHostedAny -and $job.Contains('steps')) {
                $o = Get-StepOrderFinding $job['steps'] (New-Ctx 'push' $leg)
                if ((-not $o.HasPreflight) -or $o.RepoBefore.Count -gt 0) {
                    $pf = if ($o.HasPreflight) { 'before hil-trusted-preflight' } else { 'no hil-trusted-preflight step; all repo steps unguarded' }
                    $findings += [pscustomobject]@{ Kind = 'Order'; Job = $jobName; Leg = $legDesc; Event = '*'; Detail = ("{0}: {1}" -f $pf, ($o.RepoBefore -join ' | ')) }
                }
            }
        }
        if ($jobSelfHosted) {
            $gi = Get-GovernanceIssues $wf $job
            if ($gi.Count -gt 0) { $findings += [pscustomobject]@{ Kind = 'Governance'; Job = $jobName; Leg = ''; Event = '*'; Detail = ($gi -join ' | ') } }
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

$wrGuard = "github.repository == 'zarfld/IntelAvbFilter' && github.event_name == 'workflow_run' && github.event.workflow_run.event == 'push' && github.event.workflow_run.head_branch == 'master' && github.event.workflow_run.conclusion == 'success' && github.event.workflow_run.head_repository.full_name == 'zarfld/IntelAvbFilter'"
$fx01 = New-Fx 'fx01_safe.yml' @"
on:
  workflow_run:
    workflows: [CI]
    types: [completed]
permissions:
  contents: read
jobs:
  hil:
    if: $wrGuard && needs.authorize.outputs.authorized == 'true'
    runs-on: [self-hosted, Windows, i226]
    environment: hil
    steps:
$preflightStep
      - uses: actions/checkout@v5
        with:
          persist-credentials: false
      - run: .\tools\build\Build-Tests.ps1
"@
$fx20 = New-Fx 'fx20_matrix_unserialized_masked.yml' @"
on: [push]
permissions:
  contents: read
concurrency:
  group: hil
  cancel-in-progress: false
jobs:
  hil:
    if: github.repository == 'zarfld/IntelAvbFilter' && github.event_name == 'push' && github.ref == 'refs/heads/master'
    runs-on: [self-hosted, Windows, i226]
    environment: hil
    continue-on-error: true
    strategy:
      matrix:
        config: [Debug, Release]
    steps:
$preflightStep
      - uses: actions/checkout@v5
        with:
          persist-credentials: false
      - run: .\tools\test\Run-Tests-CI.ps1
        continue-on-error: true
"@
$fx22 = New-Fx 'fx22_workflow_run_branch_guard_without_event_guard.yml' @"
on:
  workflow_run:
    workflows: [CI]
    types: [completed]
jobs:
  hil:
    if: github.repository == 'zarfld/IntelAvbFilter' && github.event.workflow_run.head_branch == 'master' && github.event.workflow_run.conclusion == 'success'
    runs-on: [self-hosted, Windows, i226]
    steps:
$preflightStep
"@
$fx23 = New-Fx 'fx23_workflow_run_without_conclusion_guard.yml' @"
on:
  workflow_run:
    workflows: [CI]
    types: [completed]
jobs:
  hil:
    if: github.repository == 'zarfld/IntelAvbFilter' && github.event.workflow_run.event == 'push' && github.event.workflow_run.head_branch == 'master' && github.event.workflow_run.head_repository.full_name == 'zarfld/IntelAvbFilter'
    runs-on: [self-hosted, Windows, i226]
    steps:
$preflightStep
"@
$fx21 = New-Fx 'fx21_matrix_serialized.yml' @"
on:
  workflow_run:
    workflows: [CI]
    types: [completed]
jobs:
  hil:
    if: $wrGuard
    runs-on: [self-hosted, Windows, i226]
    environment: hil
    permissions:
      contents: read
    strategy:
      fail-fast: false
      max-parallel: 1
      matrix:
        config: [Debug, Release]
    steps:
$preflightStep
      - uses: actions/checkout@v5
        with:
          persist-credentials: false
"@
$fx15 = New-Fx 'fx15_push.yml' @"
on:
  push:
    branches: [master]
jobs:
  hil:
    runs-on: [self-hosted, Windows, i226]
    steps:
$preflightStep
"@
$fx16 = New-Fx 'fx16_schedule_guarded_by_repo_only.yml' @"
on:
  schedule:
    - cron: '0 2 * * 0'
jobs:
  hil:
    if: github.repository == 'zarfld/IntelAvbFilter'
    runs-on: [self-hosted, Windows, i226]
    steps:
$preflightStep
"@
$fx17 = New-Fx 'fx17_dispatch_without_ref_guard.yml' @"
on: [workflow_dispatch]
jobs:
  hil:
    if: github.event_name == 'workflow_dispatch'
    runs-on: [self-hosted, Windows, i226]
    steps:
$preflightStep
"@
$fx18 = New-Fx 'fx18_governance_missing.yml' @"
on: [workflow_dispatch]
jobs:
  hil:
    if: github.repository == 'zarfld/IntelAvbFilter' && github.ref == 'refs/heads/master'
    runs-on: [self-hosted, Windows, i226]
    steps:
$preflightStep
      - uses: actions/checkout@v5
"@
$fx19 = New-Fx 'fx19_governance_weak.yml' @"
on: [workflow_dispatch]
permissions: write-all
concurrency:
  group: hil
  cancel-in-progress: true
jobs:
  hil:
    if: github.repository == 'zarfld/IntelAvbFilter' && github.ref == 'refs/heads/master'
    runs-on: [self-hosted, Windows, i226]
    environment: hil
    permissions:
      contents: write
    steps:
$preflightStep
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

function Get-Violations($F) { return ,@(@($F) | Where-Object { $null -ne $_ -and $_.Kind -ne 'Authorized' }) }

$f = Get-WorkflowFindings $Fx $fx01
tc_check 'TEST-HILTRUST-FX-001-guarded-governed-workflow-run-with-trusted-preflight-first-has-no-violations' `
    (((Get-Violations $f).Count -eq 0) -and ((Get-Kind $f 'Authorized').Count -eq 1) -and ((Get-Kind $f 'Authorized')[0].Event -eq 'workflow_run[upstream-master-push-success]')) (Show-Findings $f)
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
$r6 = Get-Kind $f 'Reachable'
tc_check 'TEST-HILTRUST-FX-006-same-repo-head-guard-excludes-fork-pr-but-not-same-repository-pr' `
    (($r6.Count -eq 1) -and ($r6[0].Event -match 'same-repo-pr') -and ($r6[0].Event -notmatch 'fork-pr')) (Show-Findings $f)
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
tc_check 'TEST-HILTRUST-FX-014-steps-excluded-by-step-if-and-system-tools-are-not-repo-code' ((Get-Kind $f 'Order').Count -eq 0) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx15
tc_check 'TEST-HILTRUST-FX-015-push-to-self-hosted-is-an-automatic-execution-bypassing-ci-and-deploy-staging' ((Get-Kind $f 'Automatic').Count -eq 1) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx22
$r22 = Get-Kind $f 'Reachable'
tc_check 'TEST-HILTRUST-FX-022-workflow-run-branch-guard-without-event-guard-is-reachable-from-a-fork-pr-of-its-master' `
    (($r22.Count -eq 1) -and ($r22[0].Event -eq 'workflow_run[upstream-fork-pr,upstream-same-repo-pr-from-master]') -and ((Get-Kind $f 'ForeignRepo').Count -eq 0)) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx23
tc_check 'TEST-HILTRUST-FX-023-workflow-run-without-conclusion-guard-runs-after-failed-ci' `
    (((Get-Kind $f 'UpstreamFailed').Count -eq 1) -and ((Get-Kind $f 'Reachable').Count -eq 0)) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx16
tc_check 'TEST-HILTRUST-FX-016-schedule-guarded-only-by-repository-is-still-automatic' ((Get-Kind $f 'Automatic').Count -eq 1) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx17
$d17 = Get-Kind $f 'Dispatch'
tc_check 'TEST-HILTRUST-FX-017-manual-dispatch-to-self-hosted-is-a-violation-on-every-ref' `
    (($d17.Count -eq 1) -and ($d17[0].Event -match 'master') -and ($d17[0].Event -match 'feature-branch') -and ($d17[0].Event -match 'tag') -and
     ($d17[0].Event -match 'fork-repository') -and ((Get-Kind $f 'Authorized').Count -eq 0)) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx18
$g18 = Get-Kind $f 'Governance'
tc_check 'TEST-HILTRUST-FX-018-missing-permissions-environment-and-persisted-credentials-fail-closed' `
    (($g18.Count -eq 1) -and ($g18[0].Detail -match 'no explicit token permissions') -and ($g18[0].Detail -match 'no environment') -and
     ($g18[0].Detail -match 'persists git credentials') -and ((Get-Kind $f 'UnauthorizedRef').Count -eq 0)) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx19
$g19 = Get-Kind $f 'Governance'
tc_check 'TEST-HILTRUST-FX-019-write-permission-and-cancelling-concurrency-are-violations' `
    (($g19.Count -eq 1) -and ($g19[0].Detail -match 'permission contents: write') -and ($g19[0].Detail -match 'cancel-in-progress')) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx20
$g20 = Get-Kind $f 'Governance'
tc_check 'TEST-HILTRUST-FX-020-unserialized-matrix-and-continue-on-error-are-violations' `
    (($g20.Count -eq 1) -and ($g20[0].Detail -match 'max-parallel must be 1') -and ($g20[0].Detail -match 'job continue-on-error') -and
     ($g20[0].Detail -match 'step continue-on-error')) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx21
$a21 = Get-Kind $f 'Authorized'
tc_check 'TEST-HILTRUST-FX-021-serialized-guarded-workflow-run-matrix-is-authorized-per-leg-without-violations' `
    (((Get-Violations $f).Count -eq 0) -and ($a21.Count -eq 2) -and (@($a21 | Where-Object { $_.Event -ne 'workflow_run[upstream-master-push-success]' }).Count -eq 0)) (Show-Findings $f)

# ---------------------------------------------------------------------------------------------------------
# REAL: this repository's workflows (must be GREEN: physical HIL only in hil.yml, after CI + deploy-staging)
# ---------------------------------------------------------------------------------------------------------
$realFindings = @()
$wfDir = Join-Path $RepoRoot '.github\workflows'
foreach ($wfFile in @(Get-ChildItem $wfDir -File | Where-Object { $_.Extension -in '.yml', '.yaml' } | Sort-Object Name)) {
    $wfData = Read-Yaml $wfFile.FullName
    $evs = @(Get-Events $wfData)
    if ($evs -contains 'workflow_call' -and $evs.Count -eq 1) { continue }   # analyzed through its callers
    $realFindings += @(Get-WorkflowFindings $RepoRoot ".github/workflows/$($wfFile.Name)")
}
$realReach  = Get-Kind $realFindings 'Reachable'
$realOrder  = Get-Kind $realFindings 'Order'
$realExt    = Get-Kind $realFindings 'External'
$realAuto   = Get-Kind $realFindings 'Automatic'
$realRef    = @((Get-Kind $realFindings 'UnauthorizedRef') + (Get-Kind $realFindings 'ForeignRepo'))
$realGov    = Get-Kind $realFindings 'Governance'
$realAuth   = Get-Kind $realFindings 'Authorized'
$realDisp   = Get-Kind $realFindings 'Dispatch'
$realUpFail = Get-Kind $realFindings 'UpstreamFailed'

Write-Host ""
Write-Host "  REAL findings (repository .github/workflows):" -ForegroundColor Yellow
foreach ($x in $realFindings) { Write-Host ("    [{0}] {1} {{{2}}} @{3}: {4}" -f $x.Kind, $x.Job, $x.Leg, $x.Event, $x.Detail) -ForegroundColor Yellow }

tc_check 'TEST-HILTRUST-REAL-001-no-self-hosted-leg-reachable-from-untrusted-events' ($realReach.Count -eq 0) `
    ("{0} reachable self-hosted leg(s): {1}" -f $realReach.Count, (Show-Findings $realReach))
tc_check 'TEST-HILTRUST-REAL-002-no-repository-code-before-trusted-hil-preflight' ($realOrder.Count -eq 0) `
    ("{0} self-hosted leg(s) run repository code without/before a trusted preflight: {1}" -f $realOrder.Count, (Show-Findings $realOrder))
tc_check 'TEST-HILTRUST-REAL-003-no-unverifiable-external-workflow-on-untrusted-events' ($realExt.Count -eq 0) `
    ("{0} external reusable workflow reference(s)" -f $realExt.Count)
tc_check 'TEST-HILTRUST-REAL-004-no-direct-push-schedule-or-other-automatic-event-reaches-the-hil-runner' ($realAuto.Count -eq 0) (Show-Findings $realAuto)
tc_check 'TEST-HILTRUST-REAL-005-non-master-upstreams-and-foreign-repositories-cannot-reach-hil' ($realRef.Count -eq 0) (Show-Findings $realRef)
tc_check 'TEST-HILTRUST-REAL-006-self-hosted-jobs-least-privilege-environment-serialized-never-masked' ($realGov.Count -eq 0) (Show-Findings $realGov)
tc_check 'TEST-HILTRUST-REAL-014-no-manual-dispatch-reaches-the-hil-runner' ($realDisp.Count -eq 0) (Show-Findings $realDisp)
tc_check 'TEST-HILTRUST-REAL-015-failed-upstream-ci-cannot-reach-the-hil-runner' ($realUpFail.Count -eq 0) (Show-Findings $realUpFail)

# Only hil.yml's two physical stages reach a self-hosted runner, each leg only after a successful master push CI.
$ciRel  = '.github/workflows/ci-standards-compliance.yml'
$hilRel = '.github/workflows/hil.yml'
$hilJobIds = @('hil-unit', 'hil-integration')
$authIssues = @()
foreach ($jid in $hilJobIds) {
    $a = @($realAuth | Where-Object { $_.Job -eq "$hilRel/$jid" })
    $legsA = @($a | ForEach-Object { $_.Leg } | Sort-Object)
    if (($legsA -join ',') -ne 'config=Debug,config=Release') { $authIssues += "${jid}: authorized legs [$($legsA -join ',')] (expected config=Debug,config=Release)" }
    if (@($a | Where-Object { $_.Event -ne 'workflow_run[upstream-master-push-success]' }).Count -gt 0) { $authIssues += "${jid}: authorized on $(@($a | ForEach-Object { $_.Event }) -join ',')" }
}
$selfHostedJobs = @($realFindings | Where-Object { $_.Kind -in 'Authorized', 'Reachable', 'Automatic', 'UpstreamFailed', 'UnauthorizedRef', 'ForeignRepo', 'Dispatch', 'Order', 'Governance' } | ForEach-Object { ($_.Job -split ' -> ')[0] } | Sort-Object -Unique)
$expectedSelfHosted = @($hilJobIds | ForEach-Object { "$hilRel/$_" } | Sort-Object)
if (($selfHostedJobs -join ',') -ne ($expectedSelfHosted -join ',')) { $authIssues += "self-hosted jobs: [$($selfHostedJobs -join ',')] (expected [$($expectedSelfHosted -join ',')])" }
tc_check 'TEST-HILTRUST-REAL-007-only-hil-yml-stages-reach-the-hil-runner-and-only-after-successful-master-push-ci' ($authIssues.Count -eq 0) `
    ("{0} || authorized: {1}" -f ($authIssues -join '; '), (Show-Findings $realAuth))

# Hosted CI lanes, triggers and deploy-staging preserved; the upstream workflow knows nothing about HIL.
$ci = Read-Yaml (Join-Path $RepoRoot '.github\workflows\ci-standards-compliance.yml')
$ciEvents = @(Get-Events $ci)
$ciJobs = $ci['jobs']
$laneIssues = @()
foreach ($j in 'unit-tests', 'integration-tests') {
    if (-not $ciJobs.Contains($j)) { $laneIssues += "$j missing"; continue }
    $legs = Expand-Matrix $ciJobs[$j]['strategy']['matrix']
    $hosted = @($legs | Where-Object { -not (Test-Unk $_) -and -not (Test-SelfHostedLabels (Get-LegLabels $ciJobs[$j] (New-Ctx 'push' $_ 'trusted' $AuthorizedRef))) })
    $combos = @($hosted | ForEach-Object { "$($_['os'])/$($_['config'])" } | Sort-Object)
    if (($combos -join ',') -ne 'windows-2022/Debug,windows-2022/Release,windows-latest/Debug,windows-latest/Release') { $laneIssues += "$j hosted legs: $($combos -join ',')" }
    if (@($legs).Count -ne $hosted.Count) { $laneIssues += "$j still has non-hosted legs" }
}
foreach ($ev in 'push', 'pull_request', 'workflow_dispatch') { if ($ciEvents -notcontains $ev) { $laneIssues += "ci-standards-compliance lost trigger $ev" } }
$dep = $ciJobs['deploy-staging']
$DeployName = 'Deploy to Staging (Phase 08 - Transition)'
if ($null -eq $dep) { $laneIssues += 'deploy-staging missing' }
else {
    if ("$($dep['name'])" -cne $DeployName) { $laneIssues += "deploy-staging name '$($dep['name'])' (hil.yml authorize looks up '$DeployName')" }
    if (@($dep['needs']) -notcontains 'acceptance-tests' -or @($dep['needs']) -notcontains 'unit-tests' -or @($dep['needs']) -notcontains 'integration-tests') { $laneIssues += "deploy-staging needs [$(@($dep['needs']) -join ',')]" }
    if ("$($dep['if'])" -notmatch "github\.event_name == 'push'" -or "$($dep['if'])" -notmatch "refs/heads/master") { $laneIssues += "deploy-staging condition changed: $($dep['if'])" }
}
foreach ($jid in $ciJobs.Keys) { if (@(if ($ciJobs[$jid].Contains('needs')) { @($ciJobs[$jid]['needs']) }) | Where-Object { $_ -like 'hil*' -or $_ -like '*-hil' }) { $laneIssues += "ci-standards job $jid depends on a HIL job" } }
tc_check 'TEST-HILTRUST-REAL-008-hosted-ci-lanes-triggers-and-deploy-staging-preserved' ($laneIssues.Count -eq 0) ($laneIssues -join '; ')

# hil.yml authorization structure.
$hil = Read-Yaml (Join-Path $RepoRoot '.github\workflows\hil.yml')
$hilJobs = $hil['jobs']
$wrGuardReal = "github.repository == 'zarfld/IntelAvbFilter' && github.event_name == 'workflow_run' && github.event.workflow_run.event == 'push' && github.event.workflow_run.head_branch == 'master' && github.event.workflow_run.conclusion == 'success' && github.event.workflow_run.head_repository.full_name == 'zarfld/IntelAvbFilter'"
$azIssues = @()
$hilEvents = @(Get-Events $hil)
if (($hilEvents -join ',') -ne 'workflow_run') { $azIssues += "hil.yml triggers [$($hilEvents -join ',')] (exactly workflow_run; no dispatch, push or workflow_call)" }
else {
    $wrOn = $hil['on']['workflow_run']
    if ((@($wrOn['workflows']) -join '|') -cne "$($ci['name'])") { $azIssues += "workflow_run.workflows [$(@($wrOn['workflows']) -join '|')] is not exactly '$($ci['name'])'" }
    if ((@($wrOn['types']) -join ',') -ne 'completed') { $azIssues += "workflow_run.types [$(@($wrOn['types']) -join ',')]" }
    if ($wrOn.Contains('branches') -or $wrOn.Contains('branches-ignore')) { $azIssues += 'workflow_run branch filter present (the authorize job decides)' }
}
$az = $hilJobs['authorize']
if ($null -eq $az) { $azIssues += 'authorize job missing' }
else {
    if ("$($az['runs-on'])" -ne 'ubuntu-latest') { $azIssues += "authorize runs on '$($az['runs-on'])' (must be GitHub-hosted)" }
    $p = $az['permissions']
    if (-not ($p -is [System.Collections.IDictionary]) -or (@($p.Keys | Sort-Object) -join ',') -ne 'actions,contents' -or "$($p['actions'])" -ne 'read' -or "$($p['contents'])" -ne 'read') { $azIssues += 'authorize permissions must be exactly actions: read, contents: read' }
    if ("$($az['if'])".Trim() -cne $wrGuardReal) { $azIssues += "authorize if is not the workflow_run guard: $($az['if'])" }
    foreach ($o in 'authorized', 'head_sha', 'upstream_run_id', 'upstream_run_attempt') { if ("$($az['outputs'][$o])" -ne "`${{ steps.authz.outputs.$o }}") { $azIssues += "authorize output $o" } }
    $azRun = "$(@($az['steps'])[0]['run'])"
    if ($azRun -notmatch [regex]::Escape("DEPLOY_JOB = '$DeployName'")) { $azIssues += 'authorize script does not look up the deploy-staging job by its exact name' }
    if ($azRun -notmatch [regex]::Escape("CI_PATH = '$ciRel'")) { $azIssues += 'authorize script does not pin the CI workflow path' }
}
foreach ($jid in $hilJobIds) {
    $j = $hilJobs[$jid]
    if ($null -eq $j) { $azIssues += "$jid missing"; continue }
    if (@($j['needs']) -notcontains 'authorize') { $azIssues += "$jid does not need authorize" }
    if ("$($j['if'])".Trim() -cne "$wrGuardReal && needs.authorize.outputs.authorized == 'true'") { $azIssues += "$jid if is not guard && authorized: $($j['if'])" }
    $first = @($j['steps'])[0]
    $fr = "$($first['run'])"
    if ("$($first['id'])" -ne 'hil-trusted-preflight' -or $first.Contains('if') -or $first.Contains('continue-on-error')) { $azIssues += "${jid}: first step is not an unconditional hil-trusted-preflight" }
    foreach ($m in "GITHUB_WORKFLOW_REF -cne 'zarfld/IntelAvbFilter/.github/workflows/hil.yml@refs/heads/master'", "HIL_AUTHORIZED -cne 'true'",
                   'WindowsBuiltInRole]::Administrator', 'HIL_AUTHORIZED_DRIVER_SHA256', 'Get-Service -Name IntelAvbFilter', 'HIL_BUGCHECK_ACK', 'HIL_HALT',
                   'Get-PnpDevice', 'Get-NetAdapter', 'exit 1') {
        if ($fr.IndexOf($m) -lt 0) { $azIssues += "${jid}: preflight lacks '$m'" }
    }
    if ("$($first['env']['HIL_AUTHORIZED'])" -ne '${{ needs.authorize.outputs.authorized }}') { $azIssues += "${jid}: preflight HIL_AUTHORIZED is not the authorize output" }
}
if ($hilJobs.Contains('hil-unit') -and $hilJobs.Contains('hil-integration') -and
    "$(@($hilJobs['hil-unit']['steps'])[0]['run'])" -cne "$(@($hilJobs['hil-integration']['steps'])[0]['run'])") { $azIssues += 'hil-unit and hil-integration preflights differ' }
$acc = $hilJobs['hil-acceptance']
if ($null -eq $acc) { $azIssues += 'hil-acceptance missing' }
else {
    if ("$($acc['runs-on'])" -ne 'ubuntu-latest') { $azIssues += 'hil-acceptance must be GitHub-hosted' }
    if ((@($acc['needs']) -join ',') -ne 'authorize,hil-unit,hil-integration') { $azIssues += "hil-acceptance needs [$(@($acc['needs']) -join ',')]" }
    if ("$($acc['if'])" -notmatch '!cancelled\(\)' -or "$($acc['if'])" -notmatch "needs\.authorize\.outputs\.authorized == 'true'") { $azIssues += 'hil-acceptance condition' }
}
foreach ($jid in $hilJobs.Keys) {
    foreach ($s in @($hilJobs[$jid]['steps'])) {
        if ($s -is [System.Collections.IDictionary] -and $s.Contains('run') -and "$($s['run'])" -match '\$\{\{\s*github\.(event|head_ref)') { $azIssues += "${jid}: a run script interpolates $($Matches[0])... (script injection; use env)" }
    }
}
tc_check 'TEST-HILTRUST-REAL-009-hil-starts-only-via-workflow-run-and-hosted-authorization-before-any-physical-job' ($azIssues.Count -eq 0) ($azIssues -join ' || ')

# Each physical stage builds and tests exactly the authorized upstream commit, in order, with isolated evidence.
$expectedSuites = @{ 'hil-unit' = 'HardwareMandatory,HardwareUnit'; 'hil-integration' = 'HardwareIntegration' }
$idIssues = @()
foreach ($jid in $hilJobIds) {
    if (-not $hilJobs.Contains($jid)) { $idIssues += "$jid missing"; continue }
    $hSteps = @($hilJobs[$jid]['steps'])
    if ("$($hilJobs[$jid]['env']['AVB_HIL_EXPECTED_REVISION'])" -ne '${{ needs.authorize.outputs.head_sha }}') { $idIssues += "${jid}: AVB_HIL_EXPECTED_REVISION is not the authorized head_sha" }
    $co = @($hSteps | Where-Object { $_ -is [System.Collections.IDictionary] -and $_.Contains('uses') -and "$($_['uses'])" -match '^actions/checkout@' })
    if ($co.Count -ne 1) { $idIssues += "${jid}: expected exactly one checkout step, found $($co.Count)" }
    else {
        $w = $co[0]['with']
        if ("$($w['ref'])" -ne '${{ needs.authorize.outputs.head_sha }}') { $idIssues += "${jid}: checkout ref is '$($w['ref'])', not the authorized upstream head_sha" }
        if ("$($w['clean'])" -ne 'true') { $idIssues += "${jid}: checkout does not clean the workspace" }
    }
    $thirdParty = @($hSteps | Where-Object { $_ -is [System.Collections.IDictionary] -and $_.Contains('uses') -and "$($_['uses'])" -notmatch '^actions/(checkout|upload-artifact)@' } | ForEach-Object { "$($_['uses'])" })
    if ($thirdParty.Count -gt 0) { $idIssues += "${jid}: actions other than checkout/upload-artifact run on the HIL machine: $($thirdParty -join ', ')" }
    $runs = @($hSteps | ForEach-Object { if ($_ -is [System.Collections.IDictionary] -and $_.Contains('run')) { "$($_['run'])" } else { '' } })
    $idx = [ordered]@{}
    foreach ($k in 'git rev-parse HEAD\)\)\.Trim\(\)\s*\r?\n\s*if \(\$head -cne \$env:AVB_HIL_EXPECTED_REVISION\)', 'Build-Tests\.ps1', 'Test-DriverPreFlight\.ps1', 'hil-run-identity\.json', 'Run-Tests-CI\.ps1') { $idx[$k] = [array]::FindIndex([string[]]$runs, [Predicate[string]]{ param($x) $x -match $k }) }
    $iCo = if ($co.Count -eq 1) { [array]::IndexOf($hSteps, $co[0]) } else { -1 }
    $seq = @($iCo) + @($idx.Values)
    $ordered = ($iCo -ge 1); for ($q = 1; $q -lt $seq.Count; $q++) { if ($seq[$q] -le $seq[$q - 1]) { $ordered = $false } }
    if (-not $ordered) { $idIssues += "${jid}: order must be preflight -> checkout -> HEAD check -> Build-Tests -> Test-DriverPreFlight -> run identity -> Run-Tests-CI (found $($seq -join ','))" }
    $suiteRuns = @($runs | Where-Object { $_ -match 'Run-Tests-CI\.ps1' })
    $suites = @($suiteRuns | ForEach-Object { if ($_ -match '-Suite\s+(\S+)') { $Matches[1] } else { '<none>' } })
    if (($suites -join ',') -cne $expectedSuites[$jid]) { $idIssues += "${jid}: suites [$($suites -join ',')] (expected [$($expectedSuites[$jid])])" }
    foreach ($r in $suiteRuns) {
        if ($r -notmatch "-LogsDir \(Join-Path \`$env:RUNNER_TEMP 'hil-evidence\\(\w+)'\)" -or $r -notmatch "-Suite $($Matches[1])\b") { $idIssues += "${jid}: Run-Tests-CI without a per-suite -LogsDir under RUNNER_TEMP: $($r.Trim())" }
        if ($r -notmatch 'exit \$LASTEXITCODE') { $idIssues += "${jid}: Run-Tests-CI exit code not propagated" }
    }
    $perm = $hilJobs[$jid]['permissions']
    if (-not ($perm -is [System.Collections.IDictionary]) -or (@($perm.Keys) -join ',') -ne 'contents' -or "$($perm['contents'])" -ne 'read') { $idIssues += "${jid}: permissions must be exactly contents: read" }
    if ("$($hilJobs[$jid]['environment'])" -ne 'hil') { $idIssues += "${jid}: environment is '$($hilJobs[$jid]['environment'])', not hil" }
}
tc_check 'TEST-HILTRUST-REAL-010-physical-stages-test-exactly-the-authorized-upstream-commit-in-order-with-isolated-evidence' ($idIssues.Count -eq 0) ($idIssues -join ' || ')

# ---------------------------------------------------------------------------------------------------------
# REAL-011: job graphs under GitHub needs/skip semantics (a job runs iff its `if` holds; without a status function
# the implicit success() requires every need to be 'success'; a skipped or failed need skips the job).
# ---------------------------------------------------------------------------------------------------------
function Get-SimulatedResults($Jobs, [hashtable]$Vars, [hashtable]$Outcome) {
    $res = [ordered]@{}
    $pending = [System.Collections.Generic.List[string]]::new(); foreach ($k in $Jobs.Keys) { $pending.Add($k) }
    while ($pending.Count -gt 0) {
        $progress = $false
        foreach ($jid in @($pending)) {
            $job = $Jobs[$jid]
            $needs = @(if ($job.Contains('needs')) { @($job['needs']) })
            if (@($needs | Where-Object { -not $res.Contains($_) }).Count -gt 0) { continue }
            $v = @{}; foreach ($k in $Vars.Keys) { $v[$k] = $Vars[$k] }
            foreach ($n in $needs) { $v["needs.$n.result"] = $res[$n] }
            $allOk = (@($needs | Where-Object { $res[$_] -ne 'success' }).Count -eq 0)
            $v['__success'] = $allOk; $v['__failure'] = (@($needs | Where-Object { $res[$_] -eq 'failure' }).Count -gt 0)
            $run = $allOk
            if ($job.Contains('if')) {
                $txt = "$($job['if'])"
                $g = Invoke-Expr $txt @{ Vars = $v; Leg = [ordered]@{} }
                if (Test-Unk $g) { throw "undecidable if of ${jid}: $txt" }
                $run = if ($txt -match '\b(success|always|failure|cancelled)\s*\(') { Test-Truthy $g } else { $allOk -and (Test-Truthy $g) }
            }
            $res[$jid] = if ($run) { if ($Outcome.ContainsKey($jid)) { $Outcome[$jid] } else { 'success' } } else { 'skipped' }
            [void]$pending.Remove($jid); $progress = $true
        }
        if (-not $progress) { throw "needs cycle or unknown need among: $($pending -join ',')" }
    }
    return $res
}
function New-RunVars([string]$EventName, [string]$Ref, [string]$Repo = 'zarfld/IntelAvbFilter') {
    return @{ 'github.event_name' = $EventName; 'github.ref' = $Ref; 'github.repository' = $Repo; 'github.repository_owner' = ($Repo -split '/')[0]; 'runner.os' = 'Linux' }
}
function New-WrVars([string]$UpEvent, [string]$UpBranch, [string]$UpConclusion, [string]$Authorized, [string]$HeadRepo = 'zarfld/IntelAvbFilter', [string]$Repo = 'zarfld/IntelAvbFilter') {
    $v = New-RunVars 'workflow_run' 'refs/heads/master' $Repo
    $v['github.event.workflow_run.event'] = $UpEvent; $v['github.event.workflow_run.head_branch'] = $UpBranch
    $v['github.event.workflow_run.conclusion'] = $UpConclusion; $v['github.event.workflow_run.head_repository.full_name'] = $HeadRepo
    $v['needs.authorize.outputs.authorized'] = $Authorized
    return $v
}
$forkR = 'untrusted-fork/IntelAvbFilter'
$allSkipped = @{ 'authorize' = 'skipped'; 'hil-unit' = 'skipped'; 'hil-integration' = 'skipped'; 'hil-acceptance' = 'skipped' }
$hilScen = [ordered]@{
    'authorized master push CI' = @{ Vars = (New-WrVars 'push' 'master' 'success' 'true'); Out = @{}
        Expect = @{ 'authorize' = 'success'; 'hil-unit' = 'success'; 'hil-integration' = 'success'; 'hil-acceptance' = 'success' } }
    'eligible, authorization denied (deploy-staging skipped, wrong SHA, stale, unprotected)' = @{ Vars = (New-WrVars 'push' 'master' 'success' ''); Out = @{ 'authorize' = 'failure' }
        Expect = @{ 'authorize' = 'failure'; 'hil-unit' = 'skipped'; 'hil-integration' = 'skipped'; 'hil-acceptance' = 'skipped' } }
    'upstream fork PR from its master' = @{ Vars = (New-WrVars 'pull_request' 'master' 'success' '' $forkR); Out = @{}; Expect = $allSkipped }
    'upstream same-repository PR'      = @{ Vars = (New-WrVars 'pull_request' 'feature/x' 'success' ''); Out = @{}; Expect = $allSkipped }
    'upstream CI failed on master'     = @{ Vars = (New-WrVars 'push' 'master' 'failure' ''); Out = @{}; Expect = $allSkipped }
    'upstream CI cancelled on master'  = @{ Vars = (New-WrVars 'push' 'master' 'cancelled' ''); Out = @{}; Expect = $allSkipped }
    'upstream feature push'            = @{ Vars = (New-WrVars 'push' 'feature/x' 'success' ''); Out = @{}; Expect = $allSkipped }
    'upstream tag push'                = @{ Vars = (New-WrVars 'push' 'v1.0.0' 'success' ''); Out = @{}; Expect = $allSkipped }
    'foreign repository'               = @{ Vars = (New-WrVars 'push' 'master' 'success' 'true' $forkR $forkR); Out = @{}; Expect = $allSkipped }
    'HIL unit fails' = @{ Vars = (New-WrVars 'push' 'master' 'success' 'true'); Out = @{ 'hil-unit' = 'failure'; 'hil-acceptance' = 'failure' }
        Expect = @{ 'hil-integration' = 'skipped'; 'hil-acceptance' = 'failure' } }
    'HIL integration fails' = @{ Vars = (New-WrVars 'push' 'master' 'success' 'true'); Out = @{ 'hil-integration' = 'failure'; 'hil-acceptance' = 'failure' }
        Expect = @{ 'hil-unit' = 'success'; 'hil-acceptance' = 'failure' } }
}
$simIssues = @()
foreach ($sn in $hilScen.Keys) {
    try { $r = Get-SimulatedResults $hilJobs $hilScen[$sn].Vars $hilScen[$sn].Out }
    catch { $simIssues += "hil.yml ${sn}: $($_.Exception.Message)"; continue }
    foreach ($j in $hilScen[$sn].Expect.Keys) { if ($r[$j] -ne $hilScen[$sn].Expect[$j]) { $simIssues += "hil.yml ${sn}: $j=$($r[$j]) (expected $($hilScen[$sn].Expect[$j]))" } }
}
$ciScen = [ordered]@{
    'master push, all pass'      = @{ Vars = (New-RunVars 'push' 'refs/heads/master'); Out = @{}; Expect = @{ 'acceptance-tests' = 'success'; 'deploy-staging' = 'success' } }
    'master push, unit fails'    = @{ Vars = (New-RunVars 'push' 'refs/heads/master'); Out = @{ 'unit-tests' = 'failure' }; Expect = @{ 'deploy-staging' = 'skipped' } }
    'master push, accept. fails' = @{ Vars = (New-RunVars 'push' 'refs/heads/master'); Out = @{ 'acceptance-tests' = 'failure' }; Expect = @{ 'deploy-staging' = 'skipped' } }
    'pull request'               = @{ Vars = (New-RunVars 'pull_request' 'refs/pull/1/merge'); Out = @{}; Expect = @{ 'acceptance-tests' = 'success'; 'deploy-staging' = 'skipped' } }
    'feature push'               = @{ Vars = (New-RunVars 'push' 'refs/heads/feature/x'); Out = @{}; Expect = @{ 'deploy-staging' = 'skipped' } }
}
foreach ($sn in $ciScen.Keys) {
    try { $r = Get-SimulatedResults $ciJobs $ciScen[$sn].Vars $ciScen[$sn].Out }
    catch { $simIssues += "ci ${sn}: $($_.Exception.Message)"; continue }
    foreach ($j in $ciScen[$sn].Expect.Keys) { if ($r[$j] -ne $ciScen[$sn].Expect[$j]) { $simIssues += "ci ${sn}: $j=$($r[$j]) (expected $($ciScen[$sn].Expect[$j]))" } }
}
tc_check 'TEST-HILTRUST-REAL-011-unit-integration-acceptance-chain-starts-only-after-authorized-ci-with-deploy-staging' ($simIssues.Count -eq 0) ($simIssues -join ' || ')

# ---------------------------------------------------------------------------------------------------------
# REAL-012: execute hil.yml's own scripts.  (a) authorize: python -I against a localhost fixture GitHub API (no
# network); (b) hil-acceptance verdict: python -I against fixture evidence directories.
# ---------------------------------------------------------------------------------------------------------
function Invoke-Py([string]$Script, [hashtable]$Env) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Python; $psi.Arguments = "-I `"$Script`""
    $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    foreach ($k in @($psi.EnvironmentVariables.Keys)) { if ($k -like 'GITHUB_*' -or $k -like 'HIL_*' -or $k -match '^(?i)(https?|all|no)_proxy$') { $psi.EnvironmentVariables.Remove($k) } }
    $psi.EnvironmentVariables['NO_PROXY'] = '*'
    foreach ($k in $Env.Keys) { $psi.EnvironmentVariables[$k] = [string]$Env[$k] }
    $p = [System.Diagnostics.Process]::Start($psi)
    $o = $p.StandardOutput.ReadToEndAsync(); $e = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit(60000)) { $p.Kill(); return @{ Exit = -999; Out = 'TIMEOUT' } }
    return @{ Exit = $p.ExitCode; Out = ($o.Result + "`n" + $e.Result) }
}
$azRunText = if ($null -ne $hilJobs['authorize']) { "$(@($hilJobs['authorize']['steps'])[0]['run'])" } else { '' }
$accStep = @(if ($null -ne $hilJobs['hil-acceptance']) { @($hilJobs['hil-acceptance']['steps']) | Where-Object { "$($_['name'])" -like 'Hardware acceptance verdict*' } })
$x12 = @()
$srv = $null
try {
    # (a) authorization
    $azPy = Join-Path $script:Tmp 'authz.py'; [System.IO.File]::WriteAllText($azPy, ($azRunText -replace "`r`n", "`n"))
    $srvPy = Join-Path $script:Tmp 'fixture_api.py'
    $routesFile = Join-Path $script:Tmp 'routes.json'
    [System.IO.File]::WriteAllText($routesFile, '{}')
    [System.IO.File]::WriteAllText($srvPy, @'
import json, sys, http.server
ROUTES = sys.argv[1]
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        with open(ROUTES, encoding='utf-8') as fh:
            routes = json.load(fh)
        r = routes.get(self.path)
        if r is None:
            r = routes.get(self.path.split('?')[0])
        code, body = (404, {'message': 'Not Found'}) if r is None else (r.get('status', 200), r.get('body'))
        data = json.dumps(body).encode('utf-8')
        self.send_response(code)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)
    def log_message(self, *a):
        pass
srv = http.server.HTTPServer(('127.0.0.1', 0), H)
print(srv.server_address[1], flush=True)
srv.serve_forever()
'@)
    $spsi = New-Object System.Diagnostics.ProcessStartInfo
    $spsi.FileName = $Python; $spsi.Arguments = "-I `"$srvPy`" `"$routesFile`""
    $spsi.UseShellExecute = $false; $spsi.RedirectStandardOutput = $true; $spsi.RedirectStandardError = $true
    $srv = [System.Diagnostics.Process]::Start($spsi)
    $port = $srv.StandardOutput.ReadLine()
    if ($port -notmatch '^\d+$') { throw "fixture API did not start: $port" }
    $api = "http://127.0.0.1:$port"
    $sha = '0123456789abcdef0123456789abcdef01234567'; $rid = 4242; $RepoName = 'zarfld/IntelAvbFilter'
    function New-AzState {
        $wr = [ordered]@{ id = $rid; run_attempt = 1; head_sha = $sha; event = 'push'; head_branch = 'master'; status = 'completed'; conclusion = 'success'
                          path = $ciRel; name = 'CI - Standards Compliance & Quality Gates'; repository = [ordered]@{ full_name = $RepoName }; head_repository = [ordered]@{ full_name = $RepoName } }
        $run = [ordered]@{}; foreach ($k in $wr.Keys) { $run[$k] = $wr[$k] }
        $run['repository'] = [ordered]@{ full_name = $RepoName }; $run['head_repository'] = [ordered]@{ full_name = $RepoName }
        return @{ Action = 'completed'; Wr = $wr; Run = $run; RunStatus = 200; Protected = $true; Cmp = 'ahead'; Paged = $false
                  Jobs = @([ordered]@{ name = 'Unit Tests (windows-latest, Debug)'; status = 'completed'; conclusion = 'success'; head_sha = $sha; run_id = $rid },
                           [ordered]@{ name = $DeployName; status = 'completed'; conclusion = 'success'; head_sha = $sha; run_id = $rid })
                  Env = @{ GITHUB_REPOSITORY = $RepoName; GITHUB_EVENT_NAME = 'workflow_run' } }
    }
    $both = { param($s, $k, $v) $s.Wr[$k] = $v; $s.Run[$k] = $v }
    $azCases = [ordered]@{
        'all checks pass'                 = @{ Mod = {}; Allow = $true }
        'deploy-staging on jobs page 2'   = @{ Mod = { param($s) $s.Paged = $true }; Allow = $true }
        'upstream CI failed'              = @{ Mod = { param($s) & $both $s 'conclusion' 'failure' }; Why = "conclusion is 'failure'" }
        'deploy-staging skipped'          = @{ Mod = { param($s) $s.Jobs[1].conclusion = 'skipped' }; Why = 'skipped is not enough' }
        'deploy-staging failed'           = @{ Mod = { param($s) $s.Jobs[1].conclusion = 'failure' }; Why = "conclusion='failure'" }
        'deploy-staging still running'    = @{ Mod = { param($s) $s.Jobs[1].status = 'in_progress'; $s.Jobs[1].conclusion = $null }; Why = "status='in_progress'" }
        'deploy-staging absent'           = @{ Mod = { param($s) $s.Jobs = @($s.Jobs[0]) }; Why = '0 jobs named' }
        'deploy-staging of another SHA'   = @{ Mod = { param($s) $s.Jobs[1].head_sha = ('e' * 40) }; Why = 'deploy-staging belongs to' }
        'upstream pull request'           = @{ Mod = { param($s) & $both $s 'event' 'pull_request' }; Why = "event is 'pull_request'" }
        'fork PR from its master'         = @{ Mod = { param($s) & $both $s 'event' 'pull_request'; $s.Wr.head_repository.full_name = 'untrusted-fork/IntelAvbFilter' }; Why = 'head_repository.full_name' }
        'feature branch push'             = @{ Mod = { param($s) & $both $s 'head_branch' 'feature/x' }; Why = "head_branch is 'feature/x'" }
        'tag push'                        = @{ Mod = { param($s) & $both $s 'head_branch' 'v1.0.0' }; Why = "head_branch is 'v1.0.0'" }
        'foreign repository'              = @{ Mod = { param($s) $s.Env.GITHUB_REPOSITORY = 'untrusted-fork/IntelAvbFilter'; $s.Wr.repository.full_name = 'untrusted-fork/IntelAvbFilter' }; Why = "repository 'untrusted-fork/IntelAvbFilter'" }
        'other workflow with the CI name' = @{ Mod = { param($s) & $both $s 'path' '.github/workflows/evil.yml' }; Why = "path is '.github/workflows/evil.yml'" }
        'API head_sha differs (wrong SHA)' = @{ Mod = { param($s) $s.Run.head_sha = ('f' * 40) }; Why = 'API run head_sha' }
        'stale attempt (CI re-run)'       = @{ Mod = { param($s) $s.Run.run_attempt = 2 }; Why = 'API run run_attempt' }
        'master not protected'            = @{ Mod = { param($s) $s.Protected = $false }; Why = 'not a protected branch' }
        'SHA not on master'               = @{ Mod = { param($s) $s.Cmp = 'diverged' }; Why = 'not reachable from master' }
        'API error'                       = @{ Mod = { param($s) $s.RunStatus = 500 }; Why = 'authorization error' }
        'malformed head_sha'              = @{ Mod = { param($s) & $both $s 'head_sha' 'master' }; Why = '40-hex' }
        'workflow_run requested'          = @{ Mod = { param($s) $s.Action = 'requested' }; Why = "action 'requested'" }
        'not a workflow_run event'        = @{ Mod = { param($s) $s.Env.GITHUB_EVENT_NAME = 'workflow_dispatch' }; Why = 'is not workflow_run' }
    }
    $i = 0
    foreach ($n in $azCases.Keys) {
        $i++
        $s = New-AzState
        & $azCases[$n].Mod $s
        $base = "/repos/$RepoName/actions/runs/$rid"
        $routes = [ordered]@{}
        $routes[$base] = @{ status = $s.RunStatus; body = $s.Run }
        if ($s.Paged) {
            $filler = @(1..100 | ForEach-Object { [ordered]@{ name = "filler $_"; status = 'completed'; conclusion = 'success'; head_sha = $sha; run_id = $rid } })
            $routes["$base/attempts/1/jobs?per_page=100&page=1"] = @{ body = [ordered]@{ total_count = 101; jobs = $filler } }
            $routes["$base/attempts/1/jobs?per_page=100&page=2"] = @{ body = [ordered]@{ total_count = 101; jobs = @($s.Jobs[1]) } }
        } else {
            $routes["$base/attempts/1/jobs?per_page=100&page=1"] = @{ body = [ordered]@{ total_count = @($s.Jobs).Count; jobs = @($s.Jobs) } }
        }
        $routes["/repos/$RepoName/branches/master"] = @{ body = @{ protected = $s.Protected } }
        $routes["/repos/$RepoName/compare/$sha...master"] = @{ body = @{ status = $s.Cmp } }
        [System.IO.File]::WriteAllText($routesFile, ($routes | ConvertTo-Json -Depth 12))
        $evFile = Join-Path $script:Tmp "event_$i.json"; $outFile = Join-Path $script:Tmp "out_$i.txt"; $sumFile = Join-Path $script:Tmp "sum_$i.md"
        [System.IO.File]::WriteAllText($evFile, ([ordered]@{ action = $s.Action; workflow_run = $s.Wr } | ConvertTo-Json -Depth 12))
        $envs = @{ GITHUB_API_URL = $api; GITHUB_EVENT_PATH = $evFile; GITHUB_OUTPUT = $outFile; GITHUB_STEP_SUMMARY = $sumFile; GH_TOKEN = 'offline-test-token' }
        foreach ($k in $s.Env.Keys) { $envs[$k] = $s.Env[$k] }
        $r = Invoke-Py $azPy $envs
        $outTxt = if (Test-Path -LiteralPath $outFile) { Get-Content -LiteralPath $outFile -Raw } else { '' }
        if ($azCases[$n].ContainsKey('Allow')) {
            if ($r.Exit -ne 0 -or $outTxt -notmatch 'authorized=true' -or $outTxt -notmatch "head_sha=$sha" -or $outTxt -notmatch "upstream_run_id=$rid") { $x12 += "authorize '$n': expected GRANTED, exit $($r.Exit): $(($r.Out + $outTxt) -replace '\s+', ' ')" }
        } else {
            if ($r.Exit -ne 1 -or $outTxt -notmatch 'authorized=false' -or $outTxt -match 'head_sha=[0-9a-f]' -or $r.Out.IndexOf($azCases[$n].Why) -lt 0) { $x12 += "authorize '$n': expected DENIED with '$($azCases[$n].Why)', exit $($r.Exit): $(($r.Out + $outTxt) -replace '\s+', ' ')" }
        }
    }

    # (b) acceptance verdict over fixture evidence
    if ($accStep.Count -ne 1) { $x12 += "hil-acceptance has $($accStep.Count) verdict step(s)" }
    else {
        $accPy = Join-Path $script:Tmp 'accept.py'; [System.IO.File]::WriteAllText($accPy, ("$($accStep[0]['run'])" -replace "`r`n", "`n"))
        $junitPass = '<?xml version="1.0" encoding="utf-8"?><testsuites><testsuite name="s"><testcase name="TC-A-001: ok"/><testcase name="TC-HIL-COVERAGE-GAP-001: x"><skipped/></testcase></testsuite></testsuites>'
        $junitFail = '<?xml version="1.0" encoding="utf-8"?><testsuites><testsuite name="s"><testcase name="TC-A-001: ok"/><testcase name="TC-B-001: bad"><failure message="x"/></testcase></testsuite></testsuites>'
        $junitSkip = '<?xml version="1.0" encoding="utf-8"?><testsuites><testsuite name="s"><testcase name="TC-HIL-COVERAGE-GAP-001: x"><skipped/></testcase></testsuite></testsuites>'
        $bom = New-Object System.Text.UTF8Encoding($true)
        function New-Evidence([string]$Root) {
            foreach ($st in @(@{ S = 'unit'; J = 'hil-unit'; Su = @('HardwareMandatory', 'HardwareUnit') }, @{ S = 'integration'; J = 'hil-integration'; Su = @('HardwareIntegration') })) {
                foreach ($cfg in 'Debug', 'Release') {
                    $b = Join-Path $Root "hil-evidence-$($st.S)-$cfg"
                    New-Item -ItemType Directory -Path $b -Force | Out-Null
                    $id = [ordered]@{ runId = '777'; runAttempt = '1'; sha = $sha; headRevision = $sha; upstreamRunId = "$rid"; upstreamRunAttempt = '1'; job = $st.J; configuration = $cfg }
                    [System.IO.File]::WriteAllText((Join-Path $b 'hil-run-identity.json'), ($id | ConvertTo-Json), $bom)
                    foreach ($su in $st.Su) { New-Item -ItemType Directory -Path (Join-Path $b $su) -Force | Out-Null; [System.IO.File]::WriteAllText((Join-Path $b "$su\junit-$su-$cfg-20261020.xml"), $junitPass) }
                }
            }
        }
        function Set-Identity([string]$Root, [string]$Bundle, [string]$Key, [string]$Value) {
            $f = Join-Path $Root "$Bundle\hil-run-identity.json"; $o = Get-Content -LiteralPath $f -Raw | ConvertFrom-Json; $o.$Key = $Value
            [System.IO.File]::WriteAllText($f, ($o | ConvertTo-Json), $bom)
        }
        $acCases = [ordered]@{
            'complete current-run evidence'   = @{ Mod = {}; Exit = 0 }
            'hil-unit failed'                 = @{ Mod = {}; Unit = 'failure'; Exit = 1; Why = "hil-unit result is 'failure'" }
            'hil-integration skipped'         = @{ Mod = {}; Integ = 'skipped'; Exit = 1; Why = "hil-integration result is 'skipped'" }
            'bundle missing'                  = @{ Mod = { param($r) Remove-Item (Join-Path $r 'hil-evidence-integration-Release') -Recurse -Force }; Exit = 1; Why = 'no evidence bundle' }
            'leftover evidence of another run' = @{ Mod = { param($r) Set-Identity $r 'hil-evidence-unit-Debug' 'runId' '776' }; Exit = 1; Why = "runId='776'" }
            'evidence of another revision'    = @{ Mod = { param($r) Set-Identity $r 'hil-evidence-unit-Release' 'sha' ('b' * 40) }; Exit = 1; Why = 'identity sha=' }
            'checkout differed from sha'      = @{ Mod = { param($r) Set-Identity $r 'hil-evidence-integration-Debug' 'headRevision' ('c' * 40) }; Exit = 1; Why = 'identity headRevision=' }
            'evidence of another CI run'      = @{ Mod = { param($r) Set-Identity $r 'hil-evidence-unit-Debug' 'upstreamRunId' '4241' }; Exit = 1; Why = "upstreamRunId='4241'" }
            'failing JUnit'                   = @{ Mod = { param($r) [System.IO.File]::WriteAllText((Join-Path $r 'hil-evidence-unit-Release\HardwareUnit\junit-HardwareUnit-Release-20261020.xml'), $junitFail) }; Exit = 1; Why = '1 failed' }
            'skip-only JUnit'                 = @{ Mod = { param($r) [System.IO.File]::WriteAllText((Join-Path $r 'hil-evidence-integration-Debug\HardwareIntegration\junit-HardwareIntegration-Debug-20261020.xml'), $junitSkip) }; Exit = 1; Why = '0 passed' }
            'suite JUnit missing'             = @{ Mod = { param($r) Remove-Item (Join-Path $r 'hil-evidence-unit-Debug\HardwareUnit\junit-HardwareUnit-Debug-20261020.xml') -Force }; Exit = 1; Why = '0 JUnit file(s) for HardwareUnit' }
            'stray JUnit outside a bundle'    = @{ Mod = { param($r) New-Item -ItemType Directory -Path (Join-Path $r 'stale\HardwareUnit') -Force | Out-Null; [System.IO.File]::WriteAllText((Join-Path $r 'stale\HardwareUnit\junit-HardwareUnit-Debug-20250101.xml'), $junitPass) }; Exit = 1; Why = 'outside an identified bundle' }
        }
        $k = 0
        foreach ($n in $acCases.Keys) {
            $k++
            $root = Join-Path $script:Tmp "evidence_$k"
            New-Evidence $root
            & $acCases[$n].Mod $root
            $c = $acCases[$n]
            $unit = if ($c.ContainsKey('Unit')) { $c.Unit } else { 'success' }; $integ = if ($c.ContainsKey('Integ')) { $c.Integ } else { 'success' }
            $r = Invoke-Py $accPy @{ GITHUB_RUN_ID = '777'; HIL_HEAD_SHA = $sha; HIL_UPSTREAM_RUN_ID = "$rid"; HIL_UNIT = $unit; HIL_INTEGRATION = $integ
                                    HIL_EVIDENCE_DIR = $root; GITHUB_STEP_SUMMARY = (Join-Path $script:Tmp "acc_$k.md") }
            if ($r.Exit -ne $c.Exit -or ($c.Exit -ne 0 -and $r.Out.IndexOf($c.Why) -lt 0) -or ($c.Exit -eq 0 -and $r.Out -notmatch 'HIL acceptance: PASSED')) {
                $x12 += "acceptance '$n': exit $($r.Exit) (expected $($c.Exit)$(if ($c.Exit) { ", '$($c.Why)'" })): $($r.Out -replace '\s+', ' ')"
            }
        }
    }
} catch {
    $x12 += "REAL-012 harness error: $($_.Exception.Message)"
} finally {
    if ($null -ne $srv -and -not $srv.HasExited) { $srv.Kill() }
}
tc_check 'TEST-HILTRUST-REAL-012-authorization-requires-successful-master-ci-with-deploy-staging-and-acceptance-requires-current-run-evidence' ($x12.Count -eq 0) ($x12 -join ' || ')

# ---------------------------------------------------------------------------------------------------------
# REAL-013: evidence is this run's only (no committed test-evidence/ upload, HIL from RUNNER_TEMP) and each
# Hardware* suite has exactly one dispatch site, inside hil.yml's physical stages.
# ---------------------------------------------------------------------------------------------------------
$evIssues = @()
foreach ($pair in @(@{ Rel = $ciRel; Jobs = $ciJobs }, @{ Rel = $hilRel; Jobs = $hilJobs })) {
    foreach ($jid in $pair.Jobs.Keys) {
        foreach ($s in @(if ($pair.Jobs[$jid].Contains('steps')) { @($pair.Jobs[$jid]['steps']) })) {
            if (-not ($s -is [System.Collections.IDictionary]) -or -not $s.Contains('uses') -or "$($s['uses'])" -notmatch '^actions/upload-artifact@') { continue }
            $path = "$($s['with']['path'])"
            if ($path -match 'test-evidence') { $evIssues += "$($pair.Rel)/${jid}: uploads committed evidence ($path)" }
            if ($pair.Rel -eq $hilRel -and $path -notmatch '^\$\{\{ runner\.temp \}\}/hil-evidence/') { $evIssues += "hil.yml/${jid}: uploads from outside RUNNER_TEMP ($path)" }
            if ($pair.Rel -eq $ciRel -and @('unit-tests', 'integration-tests') -contains $jid -and "$($s['with']['name'])" -like 'traceability-*' -and $path -ne 'logs/traceability-summary-*.md') { $evIssues += "${jid}: traceability upload path $path" }
        }
    }
}
$hte = $ciJobs['hardware-test-evidence']
if ($null -ne $hte -and "$($hte['name'])" -notmatch 'HISTORICAL') { $evIssues += 'hardware-test-evidence is not labelled HISTORICAL (committed evidence could read as this run)' }
$sites = @{}
foreach ($wfFile in @(Get-ChildItem $wfDir -File | Where-Object { $_.Extension -in '.yml', '.yaml' })) {
    $wfd = Read-Yaml $wfFile.FullName
    if (-not $wfd.Contains('jobs')) { continue }
    foreach ($jid in $wfd['jobs'].Keys) {
        foreach ($s in @(if ($wfd['jobs'][$jid].Contains('steps')) { @($wfd['jobs'][$jid]['steps']) })) {
            if ($s -is [System.Collections.IDictionary] -and $s.Contains('run') -and "$($s['run'])" -match 'Run-Tests-CI\.ps1[^\r\n]*-Suite\s+(Hardware\w+)') {
                $kk = $Matches[1]; if (-not $sites.ContainsKey($kk)) { $sites[$kk] = @() }; $sites[$kk] += "$($wfFile.Name)/$jid"
            }
        }
    }
}
foreach ($kk in $sites.Keys) {
    if ($sites[$kk].Count -ne 1) { $evIssues += "suite $kk dispatched from $($sites[$kk].Count) sites: $($sites[$kk] -join ', ')" }
    elseif (@($hilJobIds | ForEach-Object { "hil.yml/$_" }) -notcontains $sites[$kk][0]) { $evIssues += "suite $kk dispatched outside hil.yml's physical stages: $($sites[$kk][0])" }
}
foreach ($kk in 'HardwareMandatory', 'HardwareUnit', 'HardwareIntegration') { if (-not $sites.ContainsKey($kk)) { $evIssues += "suite $kk is not dispatched by any workflow" } }
if ($sites.ContainsKey('HardwareMutation')) { $evIssues += 'HardwareMutation is dispatched automatically' }
tc_check 'TEST-HILTRUST-REAL-013-current-run-evidence-only-and-one-dispatch-site-per-hardware-suite' ($evIssues.Count -eq 0) ($evIssues -join ' || ')

Remove-Item $script:Tmp -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-HILTRUST: HIL trust boundary violations" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-HILTRUST: HIL trust boundary holds" -ForegroundColor Green
exit 0
