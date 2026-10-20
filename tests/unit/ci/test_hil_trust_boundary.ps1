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
#       issue_comment, workflow_run                                                       -> Reachable;
#       workflow_dispatch: master (Authorized, informational), feature branch / tag (UnauthorizedRef),
#                          dispatch in a fork repository (ForeignRepo);
#       any other event (push, schedule, release, ...) on master and on a feature branch  -> Automatic.
#   - every self-hosted job needs explicit read-only permissions, an approval environment, a non-cancelling
#     concurrency group and checkout without persisted credentials (Governance); missing = violation.
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

# Contexts to evaluate per trigger event; Kind = finding kind if the self-hosted job is reachable in it.
function Get-EventContexts([string]$EventName, $Leg) {
    $c = @()
    if ($PullRequestEvents -contains $EventName) {
        $c += @{ Kind = 'Reachable'; Label = 'fork-pr';      Ctx = (New-Ctx $EventName $Leg 'fork') }
        $c += @{ Kind = 'Reachable'; Label = 'same-repo-pr'; Ctx = (New-Ctx $EventName $Leg 'same-repo') }
    } elseif ($UntrustedEvents -contains $EventName) {
        $c += @{ Kind = 'Reachable'; Label = 'untrusted';    Ctx = (New-Ctx $EventName $Leg 'fork') }
    } elseif ($EventName -eq 'workflow_dispatch') {
        $c += @{ Kind = 'Authorized';      Label = 'master';          Ctx = (New-Ctx $EventName $Leg 'trusted' $AuthorizedRef) }
        $c += @{ Kind = 'UnauthorizedRef'; Label = 'feature-branch';  Ctx = (New-Ctx $EventName $Leg 'trusted' 'refs/heads/feature/x') }
        $c += @{ Kind = 'UnauthorizedRef'; Label = 'tag';             Ctx = (New-Ctx $EventName $Leg 'trusted' 'refs/tags/v1.0.0') }
        $c += @{ Kind = 'ForeignRepo';     Label = 'fork-repository'; Ctx = (New-Ctx $EventName $Leg 'trusted' $AuthorizedRef 'untrusted-fork/IntelAvbFilter') }
    } else {
        # push, schedule, release, repository_dispatch, merge_group, ...: never an operator decision
        $c += @{ Kind = 'Automatic'; Label = 'master';         Ctx = (New-Ctx $EventName $Leg 'trusted' $AuthorizedRef) }
        $c += @{ Kind = 'Automatic'; Label = 'feature-branch'; Ctx = (New-Ctx $EventName $Leg 'trusted' 'refs/heads/feature/x') }
    }
    return ,$c
}

# Governance of a self-hosted job: explicit read-only token, approval environment, no overlap, no persisted git
# credentials.  Missing or non-map values fail closed.
function Get-GovernanceIssues($Wf, $Job) {
    $issues = @()
    $perm = if ($Job.Contains('permissions')) { $Job['permissions'] } elseif ($Wf.Contains('permissions')) { $Wf['permissions'] } else { $null }
    if ($null -eq $perm) { $issues += 'no explicit token permissions (repository default applies)' }
    elseif (-not ($perm -is [System.Collections.IDictionary])) { $issues += "permissions '$perm' is not an explicit least-privilege map" }
    else { foreach ($k in $perm.Keys) { if (@('read', 'none') -notcontains "$($perm[$k])") { $issues += "permission ${k}: $($perm[$k])" } } }
    if (-not $Job.Contains('environment')) { $issues += 'no environment (no operator approval gate)' }
    $conc = if ($Job.Contains('concurrency')) { $Job['concurrency'] } elseif ($Wf.Contains('concurrency')) { $Wf['concurrency'] } else { $null }
    if ($null -eq $conc) { $issues += 'no concurrency group (overlapping HIL jobs possible)' }
    elseif ($conc -is [System.Collections.IDictionary] -and "$($conc['cancel-in-progress'])" -ne 'false') { $issues += "concurrency cancel-in-progress is '$($conc['cancel-in-progress'])' (must be false)" }
    if ($Job.Contains('steps')) {
        foreach ($s in @($Job['steps'])) {
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

# Returns findings.  Kind: Reachable (untrusted/PR event) | Automatic (push/schedule/...) | UnauthorizedRef
# (dispatch from a non-authorized ref) | ForeignRepo (dispatch in another repository) | Order | Governance |
# External | Authorized (informational: the intended operator path; not a violation).
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

$fx01 = New-Fx 'fx01_safe.yml' @"
on:
  workflow_dispatch:
permissions:
  contents: read
concurrency:
  group: hil
  cancel-in-progress: false
jobs:
  hil:
    if: github.repository == 'zarfld/IntelAvbFilter' && github.event_name == 'workflow_dispatch' && github.ref == 'refs/heads/master'
    runs-on: [self-hosted, Windows, i226]
    environment: hil
    steps:
$preflightStep
      - uses: actions/checkout@v5
        with:
          persist-credentials: false
      - run: .\tools\build\Build-Tests.ps1
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
tc_check 'TEST-HILTRUST-FX-001-guarded-governed-dispatch-with-trusted-preflight-first-has-no-violations' `
    (((Get-Violations $f).Count -eq 0) -and ((Get-Kind $f 'Authorized').Count -eq 1)) (Show-Findings $f)
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
tc_check 'TEST-HILTRUST-FX-015-push-to-self-hosted-is-an-automatic-execution' ((Get-Kind $f 'Automatic').Count -eq 1) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx16
tc_check 'TEST-HILTRUST-FX-016-schedule-guarded-only-by-repository-is-still-automatic' ((Get-Kind $f 'Automatic').Count -eq 1) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx17
$u17 = Get-Kind $f 'UnauthorizedRef'
tc_check 'TEST-HILTRUST-FX-017-dispatch-without-ref-and-repo-guard-is-unauthorized' `
    (($u17.Count -eq 1) -and ($u17[0].Event -match 'feature-branch') -and ($u17[0].Event -match 'tag') -and ((Get-Kind $f 'ForeignRepo').Count -eq 1)) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx18
$g18 = Get-Kind $f 'Governance'
tc_check 'TEST-HILTRUST-FX-018-missing-permissions-environment-concurrency-and-persisted-credentials-fail-closed' `
    (($g18.Count -eq 1) -and ($g18[0].Detail -match 'no explicit token permissions') -and ($g18[0].Detail -match 'no environment') -and
     ($g18[0].Detail -match 'no concurrency') -and ($g18[0].Detail -match 'persists git credentials') -and ((Get-Kind $f 'UnauthorizedRef').Count -eq 0)) (Show-Findings $f)
$f = Get-WorkflowFindings $Fx $fx19
$g19 = Get-Kind $f 'Governance'
tc_check 'TEST-HILTRUST-FX-019-write-permission-and-cancelling-concurrency-are-violations' `
    (($g19.Count -eq 1) -and ($g19[0].Detail -match 'permission contents: write') -and ($g19[0].Detail -match 'cancel-in-progress')) (Show-Findings $f)

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
$realAuto  = Get-Kind $realFindings 'Automatic'
$realRef   = @((Get-Kind $realFindings 'UnauthorizedRef') + (Get-Kind $realFindings 'ForeignRepo'))
$realGov   = Get-Kind $realFindings 'Governance'
$realAuth  = Get-Kind $realFindings 'Authorized'

Write-Host ""
Write-Host "  REAL findings (repository .github/workflows):" -ForegroundColor Yellow
foreach ($x in $realFindings) { Write-Host ("    [{0}] {1} {{{2}}} @{3}: {4}" -f $x.Kind, $x.Job, $x.Leg, $x.Event, $x.Detail) -ForegroundColor Yellow }

tc_check 'TEST-HILTRUST-REAL-001-no-self-hosted-leg-reachable-from-untrusted-events' ($realReach.Count -eq 0) `
    ("{0} reachable self-hosted leg(s): {1}" -f $realReach.Count, (Show-Findings $realReach))
tc_check 'TEST-HILTRUST-REAL-002-no-repository-code-before-trusted-hil-preflight' ($realOrder.Count -eq 0) `
    ("{0} self-hosted leg(s) run repository code without/before a trusted preflight" -f $realOrder.Count)
tc_check 'TEST-HILTRUST-REAL-003-no-unverifiable-external-workflow-on-untrusted-events' ($realExt.Count -eq 0) `
    ("{0} external reusable workflow reference(s)" -f $realExt.Count)
tc_check 'TEST-HILTRUST-REAL-004-no-automatic-event-reaches-the-hil-runner' ($realAuto.Count -eq 0) (Show-Findings $realAuto)
tc_check 'TEST-HILTRUST-REAL-005-unauthorized-refs-and-foreign-repositories-cannot-dispatch-hil' ($realRef.Count -eq 0) (Show-Findings $realRef)
tc_check 'TEST-HILTRUST-REAL-006-self-hosted-jobs-least-privilege-approval-and-no-overlap' ($realGov.Count -eq 0) (Show-Findings $realGov)
$hilAuth = @($realAuth | Where-Object { $_.Job -eq '.github/workflows/hil.yml/hil' })
$selfHostedJobs = @($realFindings | Where-Object { $_.Kind -in 'Authorized', 'Reachable', 'Automatic', 'UnauthorizedRef', 'ForeignRepo', 'Order', 'Governance' } | ForEach-Object { ($_.Job -split ' -> ')[0] } | Sort-Object -Unique)
tc_check 'TEST-HILTRUST-REAL-007-hil-yml-is-the-only-self-hosted-workflow-and-is-reachable-by-the-operator' `
    (($hilAuth.Count -eq 1) -and ($hilAuth[0].Event -eq 'workflow_dispatch[master]') -and (($selfHostedJobs -join ',') -eq '.github/workflows/hil.yml/hil')) `
    ("authorized: {0}; self-hosted jobs: {1}" -f (Show-Findings $hilAuth), ($selfHostedJobs -join ','))

# Hosted CI lanes preserved: ci-standards-compliance unit/integration matrices and their triggers.
$ci = Read-Yaml (Join-Path $RepoRoot '.github\workflows\ci-standards-compliance.yml')
$ciEvents = @(Get-Events $ci)
$laneIssues = @()
foreach ($j in 'unit-tests', 'integration-tests') {
    if (-not $ci['jobs'].Contains($j)) { $laneIssues += "$j missing"; continue }
    $legs = Expand-Matrix $ci['jobs'][$j]['strategy']['matrix']
    $hosted = @($legs | Where-Object { -not (Test-Unk $_) -and -not (Test-SelfHostedLabels (Get-LegLabels $ci['jobs'][$j] (New-Ctx 'push' $_ 'trusted' $AuthorizedRef))) })
    $combos = @($hosted | ForEach-Object { "$($_['os'])/$($_['config'])" } | Sort-Object)
    if (($combos -join ',') -ne 'windows-2022/Debug,windows-2022/Release,windows-latest/Debug,windows-latest/Release') { $laneIssues += "$j hosted legs: $($combos -join ',')" }
    if (@($legs).Count -ne $hosted.Count) { $laneIssues += "$j still has non-hosted legs" }
}
foreach ($ev in 'push', 'pull_request', 'workflow_dispatch') { if ($ciEvents -notcontains $ev) { $laneIssues += "ci-standards-compliance lost trigger $ev" } }
tc_check 'TEST-HILTRUST-REAL-008-github-hosted-ci-lanes-and-triggers-preserved' ($laneIssues.Count -eq 0) ($laneIssues -join '; ')

# Trusted preflight (runner-local copy of tools/hil-trusted/Invoke-HilPreflight.ps1): authorization policy only.
$pfRef = Join-Path $RepoRoot 'tools\hil-trusted\Invoke-HilPreflight.ps1'
$pfIssues = @()
$hilPath = Join-Path $RepoRoot '.github\workflows\hil.yml'
if (-not (Test-Path -LiteralPath $hilPath)) { $pfIssues += '.github/workflows/hil.yml missing' }
else {
    $hilWf = Read-Yaml $hilPath
    $firstStep = if ($hilWf['jobs'].Contains('hil')) { @($hilWf['jobs']['hil']['steps'])[0] } else { @{} }
    if (-not ($firstStep -is [System.Collections.IDictionary]) -or $firstStep['id'] -ne 'hil-trusted-preflight' -or
        "$($firstStep['run'])" -notmatch [regex]::Escape("C:\hil-trusted\Invoke-HilPreflight.ps1")) { $pfIssues += 'hil.yml first step is not the runner-local trusted preflight' }
}
$pfLib = Join-Path $RepoRoot 'tools\hil-trusted\HilPreflight.Policy.ps1'
if (-not (Test-Path -LiteralPath $pfRef)) { $pfIssues += 'reference preflight entry missing' }
if (-not (Test-Path -LiteralPath $pfLib)) { $pfIssues += 'reference preflight policy library missing' }
else {
    # Policy smoke check only (the entry script always evaluates and exits; never dot-source it here).
    # Full hook coverage: tests/unit/ci/test_hil_preflight.ps1.
    . $pfLib
    $sha = 'a' * 40
    $ok = @{ GITHUB_ACTIONS = 'true'; GITHUB_REPOSITORY = 'zarfld/IntelAvbFilter'; GITHUB_EVENT_NAME = 'workflow_dispatch'; GITHUB_REF = 'refs/heads/master'
             GITHUB_WORKFLOW_REF = 'zarfld/IntelAvbFilter/.github/workflows/hil.yml@refs/heads/master'; GITHUB_ACTOR = 'zarfld'
             GITHUB_SHA = $sha; GITHUB_WORKFLOW_SHA = $sha; GITHUB_RUN_ATTEMPT = '1' }
    $fork = @{}; foreach ($k in $ok.Keys) { $fork[$k] = $ok[$k] }
    $fork['GITHUB_EVENT_NAME'] = 'pull_request'; $fork['GITHUB_HEAD_REF'] = 'evil'; $fork['GITHUB_REF'] = 'refs/pull/7/merge'
    if (-not (Test-HilJobAuthorization -JobEnv $ok -Policy (Get-HilPolicy) -AllowedActors @('zarfld')).Allowed) { $pfIssues += 'authorized master dispatch was denied' }
    if ((Test-HilJobAuthorization -JobEnv $fork -Policy (Get-HilPolicy) -AllowedActors @('zarfld')).Allowed) { $pfIssues += 'fork pull request was authorized' }
}
tc_check 'TEST-HILTRUST-REAL-009-trusted-preflight-policy-authorizes-only-the-operator-dispatch-of-hil-yml-on-master' ($pfIssues.Count -eq 0) ($pfIssues -join ' || ')

# hil.yml cannot be invoked as a reusable workflow, and it builds/tests exactly the dispatched commit.
$idIssues = @()
if (Test-Path -LiteralPath $hilPath) {
    $hilEvents = @(Get-Events (Read-Yaml $hilPath))
    if (($hilEvents -join ',') -ne 'workflow_dispatch') { $idIssues += "hil.yml triggers: [$($hilEvents -join ',')] (must be exactly workflow_dispatch; no workflow_call)" }
    $hSteps = @((Read-Yaml $hilPath)['jobs']['hil']['steps'])
    $co = @($hSteps | Where-Object { $_ -is [System.Collections.IDictionary] -and $_.Contains('uses') -and "$($_['uses'])" -match '^actions/checkout@' })
    if ($co.Count -ne 1) { $idIssues += "expected exactly one checkout step, found $($co.Count)" }
    else {
        $w = $co[0]['with']
        if ("$($w['ref'])" -ne '${{ github.sha }}') { $idIssues += "checkout ref is '$($w['ref'])', not the dispatched commit (github.sha)" }
        if ("$($w['clean'])" -ne 'true') { $idIssues += 'checkout does not clean the workspace (stale build outputs could survive)' }
    }
    $runs = @($hSteps | ForEach-Object { if ($_ -is [System.Collections.IDictionary] -and $_.Contains('run')) { "$($_['run'])" } else { '' } })
    $iBuild = [array]::FindIndex([string[]]$runs, [Predicate[string]]{ param($x) $x -match 'Build-Tests\.ps1' })
    $iRun   = [array]::FindIndex([string[]]$runs, [Predicate[string]]{ param($x) $x -match 'Run-Tests-CI\.ps1' })
    if ($iBuild -lt 0 -or $iRun -lt 0 -or $iBuild -gt $iRun) { $idIssues += 'Build-Tests.ps1 must run before Run-Tests-CI.ps1 in the same job (manifest from this checkout)' }
} else { $idIssues += 'hil.yml missing' }
tc_check 'TEST-HILTRUST-REAL-010-hil-yml-not-reusable-and-builds-exactly-the-dispatched-commit' ($idIssues.Count -eq 0) ($idIssues -join ' || ')

Remove-Item $script:Tmp -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-HILTRUST: HIL trust boundary violations (characterization)" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-HILTRUST: HIL trust boundary holds" -ForegroundColor Green
exit 0
