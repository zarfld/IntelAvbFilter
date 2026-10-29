# test_hil_dispatch_paths.ps1
# TEST-HILPATH-*: every route that executes test binaries on the HIL machine must pass through the authoritative
# dispatcher (tools/test/Run-Tests-CI.ps1: HIL safety classification + build provenance), or be retired.
#
# Characterization test (offline, static).  NONE of the analyzed scripts is executed.  Comments are stripped
# (PowerShell tokenizer; `rem` / `::` lines in .bat) before matching.  RED results document bypass paths that
# require migration.  Registered in Unit (Q 2026-10-29): it stays RED until every route is repaired.
#
# Discovery (TEST-HILPATH-010, Q decision 4 2026-10-29): every tracked PowerShell / batch script, makefile, VS Code
# task / launch file and GitHub workflow or action - not only tools/.  A launch is recognised from the PowerShell
# syntax tree (& / . call operator, Start-Process, Invoke-Expression, cmd, a bare *.exe command, .NET Process.Start /
# ProcessStartInfo.FileName) with variables resolved through their assignments and foreach sources, from batch
# lines (with set variables), makefile recipe lines (with macros) and task / launch string arguments.  A target is a
# test program when it names a Build-Tests artifact (.exe), the shared test directory (build\tools\avb_test, $testExeDir)
# or enumerates *.exe; artifacts classified Diagnostic in $TestArtifactClassification are not test programs.
# Delegation is a graph over all dispatch-capable files; a route is gated only if every path through it is.
#
# Residual blind spots (a static scan cannot prove runtime safety; Run-Tests-CI.ps1 enforces it at runtime):
#   - commands assembled at runtime from data the scan cannot resolve (files, registry, network, user input);
#   - copies or renames of test binaries outside build\tools\avb_test and outside the Build-Tests names;
#   - programs that start other programs (compiled tests: TEST-HILSUITE spawn checks), untracked or external files
#     (external/ submodules, user profiles, scheduled tasks), and commands typed by hand in a console.
#
# Classification of a dispatch-capable file:
#   supported       - the authoritative dispatcher (Run-Tests-CI.ps1)
#   gated           - active route: must execute test programs only through gated routes, on every path
#   archived-gated  - tools/archive/** kept for audit/reproduction; delegates only through gated routes
#   offline-test    - tests/unit/** registered Unit test; the launch text is fixture data or a sandboxed runner copy

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent

$total = 0; $passed = 0; $failed = 0
function tc_pass { param([string]$n) $script:total++; $script:passed++; Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green }
function tc_fail { param([string]$n, [string]$why) $script:total++; $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red }
function tc_check { param([string]$n, [bool]$ok, [string]$why) if ($ok) { tc_pass $n } else { tc_fail $n $why } }

Write-Host ""
Write-Host "TEST-HILPATH: test-binary dispatch paths vs. the authoritative HIL dispatcher (static)" -ForegroundColor Cyan

# Per-invocation caches (performance, 2026-10-29): each file is read, tokenized and parsed once per test run.  Files do
# not change during the run, so every check sees exactly the text / syntax tree it saw before the caches.
$script:CodeTextCache = @{}
$script:AstCache = @{}
function Get-ParsedAst([string]$Path) {
    if (-not $script:AstCache.ContainsKey($Path)) { $script:AstCache[$Path] = [System.Management.Automation.Language.Parser]::ParseFile($Path, [ref]$null, [ref]$null) }
    return $script:AstCache[$Path]
}
function Get-CodeText([string]$Path) {
    if ($script:CodeTextCache.ContainsKey($Path)) { return $script:CodeTextCache[$Path] }
    $raw = [System.IO.File]::ReadAllText($Path)
    if ($Path -match '\.(bat|cmd)$') {
        $text = ((($raw -split "`r?`n") | Where-Object { $_ -notmatch '^\s*(@?rem\b|::)' }) -join "`n")
    } else {
        $errs = $null
        $toks = [System.Management.Automation.PSParser]::Tokenize($raw, [ref]$errs)
        $sb = New-Object System.Text.StringBuilder
        foreach ($t in $toks) {
            if ($t.Type -eq 'Comment') { continue }
            if ($t.Type -eq 'Variable') { [void]$sb.Append('$') }   # tokenizer drops the sigil from variable tokens
            [void]$sb.Append($t.Content).Append(' ')
        }
        $text = $sb.ToString()
    }
    $script:CodeTextCache[$Path] = $text
    return $text
}

# Execution evidence (direct) and delegation (to other dispatch scripts)
# (Tokenized code text separates member access: "$test . FullName".)
$ExecRx = '&\s*\$testPath\b|&\s*\$testExe\b|&\s*\$\w+\s*\.\s*FullName\b|"%TEST_EXE%"|&\s*\$exePath\b|Start-Process\s+[^\r\n]*\$exePath'
# Enumerating every *.exe counts only together with executing enumerated items (listing them is harmless).
$EnumAllRx = '(?s)Get-ChildItem\s+[^\r\n]*-Filter\s+"?\*\.exe"?(?=[\s)]|$).*&\s*\$\w+\s*\.\s*FullName\b'
$DispatchScripts = @('Run-Tests.ps1', 'Run-Tests-Elevated.ps1', 'Run-Tests-CI.ps1')
# Delegation = a dispatch-script name inside an actual command (invocation, Join-Path, Start-Process, ...),
# not inside a message (Write-*/Out-*) or a throw statement.  .bat: any non-comment mention.
function Get-DelegationTargets([string]$Path) {
    if ($Path -match '\.(bat|cmd)$') {
        $c = Get-CodeText $Path
        return @($DispatchScripts | Where-Object { [regex]::IsMatch($c, '(?<![-\w])' + [regex]::Escape($_)) })
    }
    $ast = Get-ParsedAst $Path
    $cmds = $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.CommandAst] }, $true)
    $texts = @($cmds | Where-Object { $_.GetCommandName() -notmatch '^(Write-|Out-)' } | ForEach-Object { $_.Extent.Text })
    return @($DispatchScripts | Where-Object { $n = $_; @($texts | Where-Object { [regex]::IsMatch($_, '(?<![-\w])' + [regex]::Escape($n)) }).Count -gt 0 })
}

# ---------------------------------------------------------------------------------------------------------
# Test-program identity (Build-Tests names and outputs; Diagnostic artifacts are not test programs)
# ---------------------------------------------------------------------------------------------------------
$runnerAst = [System.Management.Automation.Language.Parser]::ParseFile((Join-Path $RepoRoot 'tools\test\Run-Tests-CI.ps1'), [ref]$null, [ref]$null)
function Get-RunnerTable([string]$n) {
    $a = $runnerAst.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and $x.Left.Extent.Text -eq "`$$n" }, $true) | Select-Object -First 1
    $t = [ordered]@{}
    if ($a) { $h = $a.Right.Find({ param($x) $x -is [System.Management.Automation.Language.HashtableAst] }, $true)
        if ($h) { foreach ($kv in $h.KeyValuePairs) { $v = $kv.Item2.Find({ param($x) $x -is [System.Management.Automation.Language.StringConstantExpressionAst] }, $true); $t[$kv.Item1.Value] = if ($v) { $v.Value } else { '' } } } }
    return $t
}
function Get-RunnerStrings([string]$n) {
    $a = $runnerAst.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and $x.Left.Extent.Text -eq "`$$n" }, $true) | Select-Object -First 1
    if (-not $a) { return @() }
    return @($a.Right.FindAll({ param($x) $x -is [System.Management.Automation.Language.StringConstantExpressionAst] }, $true) | ForEach-Object { $_.Value })
}
$cls = Get-RunnerTable 'TestArtifactClassification'
$Diagnostic = @($cls.Keys | Where-Object { [string]$cls[$_] -match '^Diagnostic:' })
$TestBins = @()
foreach ($l in (Get-Content -LiteralPath (Join-Path $RepoRoot 'tools\build\Build-Tests.ps1'))) {
    if ($l -match '^\s*Name\s*=\s*"([^"]+)"') { $TestBins += $Matches[1] }
    if ($l -match '^\s*Output\s*=\s*"([^"]+)"') { $TestBins += [System.IO.Path]::GetFileNameWithoutExtension($Matches[1]) }
}
$TestBins = @($TestBins | Sort-Object -Unique | Where-Object { $Diagnostic -notcontains $_ })
$BinExeRx  = '(?i)(?<![\w-])(' + (($TestBins | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')\.exe\b'
$TestDirRx = '(?i)avb_test[\\/]|\$testExeDir\b|-Filter\s+["'']?\*\.exe'
# Built once (same patterns, same options as the static [regex]::IsMatch calls they replace - performance only).
$BinExeRegex  = New-Object System.Text.RegularExpressions.Regex $BinExeRx
$TestDirRegex = New-Object System.Text.RegularExpressions.Regex $TestDirRx
function Test-TestTarget([string]$Text) { return ($BinExeRegex.IsMatch($Text) -or $TestDirRegex.IsMatch($Text)) }

# ---------------------------------------------------------------------------------------------------------
# Launch detection per file type -> @{ Direct = [string[]] target texts; Refs = [string[]] command texts }
# ---------------------------------------------------------------------------------------------------------
function Get-PsLaunches([string]$Path) {
    $ast = Get-ParsedAst $Path
    # One walk of the tree, split by node type afterwards: each list keeps the traversal order of the per-type FindAll
    # it replaces, and assignments are still recorded before foreach sources (performance only).
    $all = $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -or $x -is [System.Management.Automation.Language.ForEachStatementAst] -or
                                   $x -is [System.Management.Automation.Language.CommandAst] -or $x -is [System.Management.Automation.Language.InvokeMemberExpressionAst] }, $true)
    $asgNodes = @(); $forNodes = @(); $cmdNodes = @(); $invNodes = @()
    foreach ($x in $all) {
        if ($x -is [System.Management.Automation.Language.AssignmentStatementAst]) { $asgNodes += $x }
        if ($x -is [System.Management.Automation.Language.ForEachStatementAst]) { $forNodes += $x }
        if ($x -is [System.Management.Automation.Language.CommandAst]) { $cmdNodes += $x }
        if ($x -is [System.Management.Automation.Language.InvokeMemberExpressionAst]) { $invNodes += $x }
    }
    $defs = @{}
    foreach ($a in $asgNodes) {
        $k = $a.Left.Extent.Text.ToLowerInvariant() -replace '^\[[^\]]+\]\s*', ''
        if (-not $defs.ContainsKey($k)) { $defs[$k] = @() }; $defs[$k] += $a.Right.Extent.Text
    }
    foreach ($f in $forNodes) {
        $k = ('$' + $f.Variable.VariablePath.UserPath).ToLowerInvariant()
        if (-not $defs.ContainsKey($k)) { $defs[$k] = @() }; $defs[$k] += $f.Condition.Extent.Text
    }
    $expand = $null
    $expMemo = @{}   # (depth, text) -> expansion: a pure function of this file's assignments (performance, same result)
    $expand = { param([string]$t, [int]$d)
        if ($d -le 0) { return $t }
        $mk = "$d|$t"
        if ($expMemo.ContainsKey($mk)) { return $expMemo[$mk] }
        $out = $t
        foreach ($m in [regex]::Matches($t, '\$\{?(\w+)')) {
            $k = ('$' + $m.Groups[1].Value).ToLowerInvariant()
            if ($defs.ContainsKey($k)) { foreach ($v in $defs[$k]) { $out += ' ' + (& $expand $v ($d - 1)) } }
        }
        $expMemo[$mk] = $out
        return $out }
    $direct = @(); $refs = @()
    foreach ($c in $cmdNodes) {
        $n = [string]$c.GetCommandName()
        if ($n -match '^(Write-|Out-)') { continue }
        $refs += $c.Extent.Text
        $t = $null
        if ($c.InvocationOperator -ne [System.Management.Automation.Language.TokenKind]::Unknown) { $t = $c.CommandElements[0].Extent.Text }
        elseif ($n -match '^(?i)(Start-Process|saps|start|Invoke-Expression|iex|Invoke-Item|ii|cmd|cmd\.exe)$') { $t = (@($c.CommandElements | Select-Object -Skip 1 | ForEach-Object { $_.Extent.Text }) -join ' ') }
        elseif ($n -match '(?i)\.exe$') { $t = $n }
        if ($null -ne $t) { $x = & $expand $t 4; if (Test-TestTarget $x) { $direct += $c.Extent.Text } }
    }
    foreach ($k in @($defs.Keys)) {   # .NET: $psi.FileName = ...; [Diagnostics.Process]::Start(...)
        if ($k -match '\.filename$') { foreach ($v in $defs[$k]) { if (Test-TestTarget (& $expand $v 4)) { $direct += "$k = $v" } } }
    }
    foreach ($m in $invNodes) {
        if ($m.Member.Extent.Text -eq 'Start' -and $m.Expression.Extent.Text -match '(?i)Diagnostics\.Process') {
            $refs += $m.Extent.Text
            if (Test-TestTarget (& $expand $m.Extent.Text 4)) { $direct += $m.Extent.Text }
        }
    }
    return @{ Direct = @($direct); Refs = @($refs) }
}
function Get-BatLaunches([string]$Path) {
    $vars = @{}; $direct = @(); $refs = @()
    foreach ($l in ((Get-CodeText $Path) -split "`n")) {
        if ($l -match '^\s*@?set\s+"?(\w+)=([^"]*)') { $vars[$Matches[1].ToLowerInvariant()] = $Matches[2]; continue }
        if ($l -match '^\s*@?(echo|if\s+(not\s+)?exist|goto|:)') { continue }
        $x = [regex]::Replace($l, '%(\w+)%', { param($m) $k = $m.Groups[1].Value.ToLowerInvariant(); if ($vars.ContainsKey($k)) { $vars[$k] } else { $m.Value } })
        if (-not $l.Trim()) { continue }
        $refs += $l
        # Build / file tools as the command (compiling or listing a test program is not running it); a script started
        # with -File is a delegation, resolved through the delegation graph.
        $first = (($x.Trim() -replace '^@', '' -replace '^(?i)call\s+', '') -split '\s+', 2)[0].Trim('"')
        if ($first.StartsWith('#')) { continue }   # not a command (cmd reports it as unknown; never starts a program)
        if ($first -match '^(?i)(.*[\\/])?(cl|link|lib|nmake|msbuild|copy|xcopy|move|ren|rename|del|erase|dir|md|mkdir|rd|rmdir|type|cd|pushd|popd|findstr|where|signtool|certutil|pnputil|bcdedit)(\.exe)?$') { continue }
        if ($first -match '^(?i)(powershell|pwsh)(\.exe)?$' -and $x -match '(?i)\s-File\s') { continue }
        if (Test-TestTarget $x) { $direct += $l.Trim() }
    }
    return @{ Direct = @($direct); Refs = @($refs) }
}
function Get-MakLaunches([string]$Path) {
    $raw = (([System.IO.File]::ReadAllText($Path)) -split "`r?`n")
    $macros = @{}; foreach ($l in $raw) { if ($l -match '^\s*(\w+)\s*=\s*(.*)$') { $macros[$Matches[1].ToUpperInvariant()] = $Matches[2].Trim() } }
    $direct = @(); $refs = @()
    foreach ($l in $raw) {
        if ($l -notmatch '^\t') { continue }
        $cmd = $l.Trim() -replace '^[@-]+', ''
        $refs += $cmd
        $first = ($cmd -split '\s+', 2)[0]
        if ($first -match '^(?i)(\$\((CC|LD|LINK|RC|MT|CPP|CXX)\)|cl|link|lib|del|erase|copy|xcopy|move|md|mkdir|rd|rmdir|if|echo|echo\.|nmake|rc|mt|signtool|type|cd|set)$') { continue }
        $x = $first; for ($i = 0; $i -lt 3; $i++) { $x = [regex]::Replace($x, '\$\((\w+)\)', { param($m) $k = $m.Groups[1].Value.ToUpperInvariant(); if ($macros.ContainsKey($k)) { $macros[$k] } else { $m.Value } }) }
        if (Test-TestTarget $x) { $direct += $cmd }
    }
    return @{ Direct = @($direct); Refs = @($refs) }
}
function Get-JsonLaunches([string]$Path) {
    $raw = [regex]::Replace([System.IO.File]::ReadAllText($Path), '(?m)^\s*//.*$', '')
    $strs = @([regex]::Matches($raw, '"((?:\\.|[^"\\])*)"') | ForEach-Object { $_.Groups[1].Value })
    return @{ Direct = @($strs | Where-Object { Test-TestTarget $_ }); Refs = @($strs) }
}
function Get-YmlLaunches([string]$Path) {
    $direct = @(); $refs = @()
    foreach ($l in (([System.IO.File]::ReadAllText($Path)) -split "`r?`n")) {
        $t = $l.Trim()
        if (-not $t -or $t.StartsWith('#') -or $t -match '^(-\s*)?(name|description|echo|Write-Host|Write-Output)\b' -or $t -match '^echo\s') { continue }
        $refs += $t
        if ([regex]::IsMatch($t, $BinExeRx) -and $t -match '^(&|\.\\|\./|Start-Process|cmd\s+/c|(run:\s*)?[\w.\\/-]+\.exe\b)') { $direct += $t }
    }
    return @{ Direct = @($direct); Refs = @($refs) }
}
function Get-FileLaunches([string]$Path) {
    switch -Regex ($Path) {
        '\.(ps1|psm1)$'                         { return Get-PsLaunches $Path }
        '\.(bat|cmd)$'                          { return Get-BatLaunches $Path }
        '(\.mak|[\\/]Makefile[^\\/]*)$'         { return Get-MakLaunches $Path }
        '\.json$'                               { return Get-JsonLaunches $Path }
        '\.ya?ml$'                              { return Get-YmlLaunches $Path }
    }
    return @{ Direct = @(); Refs = @() }
}

# ---------------------------------------------------------------------------------------------------------
# 011: the detector itself, against deliberate fixtures (RED fixtures: every launch form must be detected; benign
#      forms must not be).  Fixtures are written to %TEMP% and never executed.
# ---------------------------------------------------------------------------------------------------------
$FxDir = Join-Path $env:TEMP ("hilpath_fx_{0}" -f (Get-Random))
New-Item -ItemType Directory -Path $FxDir -Force | Out-Null
$fixtures = [ordered]@{
    'amp_var.ps1'      = @('direct', '$exe = Join-Path ''build\tools\avb_test\x64\Debug'' ''x.bin''; & $exe caps')
    'amp_literal.ps1'  = @('direct', "& '.\build\tools\avb_test\x64\Debug\avb_test_i226.exe' caps")
    'amp_nested.ps1'   = @('direct', '$d = Join-Path $PSScriptRoot ''..\build\tools\avb_test\x64''; $exe = Join-Path $d ''Debug\test_ptp_freq.exe''; & $exe')
    'bare_exe.ps1'     = @('direct', '.\avb_test_i226.exe selftest')
    'start_proc.ps1'   = @('direct', '$p = "C:\r\test_ioctl_tas.exe"; Start-Process -FilePath $p -Wait')
    'dotnet.ps1'       = @('direct', '$psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = "$dir\test_ioctl_tas.exe"; [System.Diagnostics.Process]::Start($psi)')
    'iex.ps1'          = @('direct', 'Invoke-Expression ".\build\tools\avb_test\x64\Debug\test_ioctl_tas.exe"')
    'loop_all.ps1'     = @('direct', 'foreach ($t in (Get-ChildItem ''C:\x'' -Filter *.exe)) { & $t.FullName }')
    'cmd_c.ps1'        = @('direct', 'cmd /c "avb_test_i226.exe caps"')
    'launch.bat'       = @('direct', "@echo off`r`n""%~dp0build\tools\avb_test\x64\Debug\avb_test_i226.exe"" caps")
    'setvar.cmd'       = @('direct', "set T=build\tools\avb_test\x64\Debug\x.bin`r`n%T% caps")
    'run.mak'          = @('direct', "TARGET = test_ptp_freq.exe`r`nrun: `$(TARGET)`r`n`t.\`$(TARGET)")
    'tasks.json'       = @('direct', '{ "tasks": [ { "command": "powershell", "args": [ "-Command", "./avb_test_i226.exe" ] } ] }')
    'wf.yml'           = @('direct', "jobs:`r`n  t:`r`n    steps:`r`n      - run: |`r`n          & build\tools\avb_test\x64\Debug\test_ioctl_tas.exe")
    'deleg_frontend.ps1' = @('delegates', '& (Join-Path $PSScriptRoot ''Run-Tests.ps1'') -Full')
    'benign_msg.ps1'   = @('none', 'Write-Host "run avb_test_i226.exe manually"; $x = ''avb_test_i226.exe''')
    'benign_link.mak'  = @('none', "TARGET = test_ptp_freq.exe`r`nall: `$(TARGET)`r`n`tlink /OUT:`$(OUTDIR)\`$(TARGET) a.obj`r`n`t@if exist `$(TARGET) del `$(TARGET)")
    'benign_diag.json' = @('none', '{ "tasks": [ { "args": [ "-Command", "./quick_diagnostics.exe" ] } ] }')
    'benign_build.cmd' = @('none', "cl avb_test_i219.c /Fe:avb_test_i219.exe`r`nnmake -f tools\avb_test\avb_diagnostic.mak`r`ndir ""build\tools\avb_test\x64\Debug\*.exe""")
    'bat_after_build.bat' = @('direct', "cl avb_test_i219.c /Fe:avb_test_i219.exe`r`navb_test_i219.exe")
    'benign_ci.ps1'    = @('none', '& (Join-Path $PSScriptRoot ''Run-Tests-CI.ps1'') -Suite HardwareUnit -Tests @(''test_ptp_freq'')')
}
$fxBad = @()
foreach ($k in $fixtures.Keys) {
    $p = Join-Path $FxDir $k
    [System.IO.File]::WriteAllText($p, $fixtures[$k][1])
    $f = Get-FileLaunches $p
    $dl = if ($p -match '\.(ps1|bat|cmd)$') { @(Get-DelegationTargets $p) } else { @() }
    $want = $fixtures[$k][0]
    if ($want -eq 'direct' -and @($f.Direct).Count -eq 0) { $fxBad += "${k}: launch not detected" }
    if ($want -ne 'direct' -and @($f.Direct).Count -gt 0) { $fxBad += "${k}: false launch: $(@($f.Direct) -join ' | ')" }
    if ($want -eq 'delegates' -and @($dl | Where-Object { $_ -ne 'Run-Tests-CI.ps1' }).Count -eq 0) { $fxBad += "${k}: delegation not detected" }
}
Remove-Item -LiteralPath $FxDir -Recurse -Force -ErrorAction SilentlyContinue
tc_check 'TEST-HILPATH-011-launch-detector-recognises-every-fixture-form' ($fxBad.Count -eq 0) ($fxBad -join '; ')

# ---------------------------------------------------------------------------------------------------------
# 010: discovery over every tracked launcher file (fail closed without git)
# ---------------------------------------------------------------------------------------------------------
$tracked = @()
try { $tracked = @(& git -C $RepoRoot ls-files 2>$null) } catch { $tracked = @() }
$LauncherRx = '(?i)(\.(ps1|psm1|bat|cmd|mak)$|(^|/)Makefile[^/]*$|^\.vscode/[^/]+\.json$|^\.github/(workflows|actions)/.+\.ya?ml$)'
$Files = @($tracked | Where-Object { $_ -match $LauncherRx -and $_ -notmatch '^external/' })
tc_check 'TEST-HILPATH-010-discovery-covers-every-tracked-launcher-file' ($tracked.Count -gt 0 -and $Files.Count -gt 0 -and
    @($Files | Where-Object { $_ -notlike 'tools/*' }).Count -gt 0) ("git ls-files returned {0} file(s); launcher files={1} (discovery must not depend on tools/ only)" -f $tracked.Count, $Files.Count)

# Facts per discovered file.
$Facts = [ordered]@{}
foreach ($rel in $Files) {
    $p = Join-Path $RepoRoot ($rel -replace '/', '\')
    if (-not (Test-Path -LiteralPath $p)) { continue }
    $f = @{ Exists = $true; Direct = @(); Refs = @(); DelegatesTo = @(); DirectExec = $false; EnumeratesAll = $false; Provenance = $false; Classification = $false; Error = '' }
    try {
        $l = Get-FileLaunches $p; $f.Direct = @($l.Direct); $f.Refs = @($l.Refs)
        if ($rel -match '\.(ps1|psm1|bat|cmd)$') {
            $c = Get-CodeText $p
            $f.DirectExec = [regex]::IsMatch($c, $ExecRx); $f.EnumeratesAll = [regex]::IsMatch($c, $EnumAllRx)
            $f.DelegatesTo = @(Get-DelegationTargets $p)
            $f.Provenance = [regex]::IsMatch($c, '\bGet-AvbHilProvenanceDecision\b'); $f.Classification = [regex]::IsMatch($c, '\bGet-AvbHilGateDecision\b')
        } else {
            $f.DelegatesTo = @($DispatchScripts | Where-Object { $n = $_; @($f.Refs | Where-Object { [regex]::IsMatch($_, '(?<![-\w])' + [regex]::Escape($n)) }).Count -gt 0 })
        }
    } catch { $f.Error = $_.Exception.Message }
    $Facts[$rel] = $f
}
# Delegation graph: a file delegating to any dispatch-capable file (by leaf name inside a command) is dispatch-capable.
$leaf = @{}; foreach ($k in $Facts.Keys) { $n = [System.IO.Path]::GetFileName($k).ToLowerInvariant(); if (-not $leaf.ContainsKey($n)) { $leaf[$n] = @() }; $leaf[$n] += $k }
function Test-Capable([string]$k) { $f = $Facts[$k]; return ($f.Error -or @($f.Direct).Count -gt 0 -or $f.DirectExec -or $f.EnumeratesAll -or @($f.DelegatesTo).Count -gt 0 -or @($f['Edges']).Count -gt 0) }
foreach ($k in @($Facts.Keys)) { $Facts[$k]['Edges'] = @() }
# Performance (2026-10-29; 124 s standalone -> see the evidence): each file's command texts are joined once with "\n",
# which acts as a string boundary for both look-arounds and occurs in no leaf name, so "the joined text matches" is
# exactly "one of the texts matches"; one compiled regex per leaf behind an ordinal (case-sensitive, like the regex)
# Contains prefilter; plain loops instead of nested pipelines.  A pass whose capable set equals the previous one
# recomputes identical edges, so stopping there gives the same result as running all 6 passes.
$refText = @{}; foreach ($k in @($Facts.Keys)) { $refText[$k] = (@($Facts[$k].Refs) -join "`n") }
$leafRx = New-Object 'System.Collections.Generic.Dictionary[string,System.Text.RegularExpressions.Regex]' ([System.StringComparer]::Ordinal); $prevCap = $null
for ($iter = 0; $iter -lt 6; $iter++) {
    $capLeaves = @($Facts.Keys | Where-Object { Test-Capable $_ } | ForEach-Object { [System.IO.Path]::GetFileName($_) } | Sort-Object -Unique)
    $capKey = $capLeaves -join '|'
    if ($null -ne $prevCap -and $capKey -ceq $prevCap) { break }
    $prevCap = $capKey
    foreach ($n in $capLeaves) { if (-not $leafRx.ContainsKey($n)) { $leafRx[$n] = New-Object System.Text.RegularExpressions.Regex ('(?<![-\w])' + [regex]::Escape($n) + '(?![\w.])') } }
    foreach ($k in @($Facts.Keys)) {
        $self = [System.IO.Path]::GetFileName($k); $txt = $refText[$k]
        $e = New-Object System.Collections.Generic.List[string]
        foreach ($n in $capLeaves) {
            if ($n -ne $self -and $txt.Contains($n) -and $leafRx[$n].IsMatch($txt)) { $e.Add($n) }
        }
        $Facts[$k]['Edges'] = @($e)
    }
}
$Gated = @{}
function Test-Gated([string]$Rel, [string[]]$Stack = @()) {
    if ($Gated.ContainsKey($Rel)) { return $Gated[$Rel] }
    if ($Stack -contains $Rel) { return $false }
    if (-not $Facts.Contains($Rel)) { $Gated[$Rel] = $true; return $true }   # absent: nothing to execute
    $f = $Facts[$Rel]
    if ($f.Error) { $Gated[$Rel] = $false; return $false }
    if ($f.Provenance -and $f.Classification) { $Gated[$Rel] = $true; return $true }
    if ($f.DirectExec -or $f.EnumeratesAll -or @($f.Direct).Count -gt 0) { $Gated[$Rel] = $false; return $false }
    $ok = $true
    $targets = @(@($f.DelegatesTo) + @($f.Edges) | Sort-Object -Unique)
    foreach ($d in $targets) {
        $dks = @(if ($leaf.ContainsKey($d.ToLowerInvariant())) { $leaf[$d.ToLowerInvariant()] })
        if ($dks.Count -eq 0) { $ok = $false; continue }
        foreach ($dk in $dks) { if (-not (Test-Gated $dk ($Stack + $Rel))) { $ok = $false } }
    }
    $Gated[$Rel] = $ok
    return $ok
}
function Show-Facts([string]$Rel) {
    if (-not $Facts.Contains($Rel)) { return "$Rel absent" }
    $f = $Facts[$Rel]
    return ("{0} [{1}]: launches=[{2}] direct-exec={3} enumerates-all-exe={4} delegates=[{5}] provenance={6} classification={7}{8}" -f $Rel,
            $(if ($Inventory.Contains($Rel)) { $Inventory[$Rel] } else { 'unclassified' }), ((@($f.Direct) | Select-Object -First 3) -join ' | '),
            $f.DirectExec, $f.EnumeratesAll, ((@(@($f.DelegatesTo) + @($f.Edges)) | Sort-Object -Unique) -join ','), $f.Provenance, $f.Classification,
            $(if ($f.Error) { " ERROR=$($f.Error)" } else { '' }))
}

# ---------------------------------------------------------------------------------------------------------
# Classification of every dispatch-capable file
# ---------------------------------------------------------------------------------------------------------
$Inventory = [ordered]@{
    'tools/test/Run-Tests-CI.ps1'                                     = 'supported'
    'tools/test/Run-Tests.ps1'                                        = 'gated'
    'tools/test/Run-Tests-Elevated.ps1'                               = 'gated'
    'tools/benchmark/Run-DriverBenchmark.ps1'                         = 'gated'
    'tools/development/Check-System.ps1'                              = 'gated'
    'scripts/build_and_test.ps1'                                      = 'gated'
    'tests/integration/ioctl/Test-ISSUE328-TAS-CaseSelection.ps1'     = 'gated'
    '.vscode/tasks.json'                                              = 'gated'
    '.vscode/settings.json'                                           = 'gated'   # terminal auto-approval patterns name Run-Tests-Elevated.ps1
    '.github/workflows/ci-standards-compliance.yml'                   = 'gated'
    '.github/workflows/hil.yml'                                       = 'gated'
    'tests/integration/ndis_receive/Makefile.mak'                     = 'gated'
    'tests/integration/tx_timestamp/Makefile.mak'                     = 'gated'
    'tools/test/Invoke-AvbTxTimestampProbe.ps1'                       = 'gated'
    'tools/test/New-AvbHilBaselineEvidence.ps1'                       = 'gated'
    'tools/archive/deprecated/LocalSuites.ps1'                        = 'archived-gated'   # actively used manual orchestrator
    'tools/archive/deprecated/FullSuite.ps1'                          = 'archived-gated'
    'tools/archive/deprecated/328_diagnostic_run.ps1'                 = 'archived-gated'
    'tools/archive/deprecated/issue_328repro.ps1'                     = 'archived-gated'
    'tools/archive/deprecated/run-check-elevated.ps1'                 = 'archived-gated'
    'tools/archive/deprecated/test/investigation_proc_issue328.ps1'   = 'archived-gated'
    'tools/archive/deprecated/test/issue328_tescase_selections.ps1'   = 'archived-gated'
}
$unitEntries = @(Get-RunnerStrings 'UnitTests' | Where-Object { $_ -match '^ps1:' } | ForEach-Object { $_.Substring(4) })
foreach ($k in @($Facts.Keys)) {
    if ($Inventory.Contains($k)) { continue }
    if ($k -like 'tests/unit/*' -and (Test-Capable $k)) { $Inventory[$k] = 'offline-test' }
}

# Reachability (Q 2026-10-29): tools/archive holds historical artifacts kept for audit - never rewritten, redirected,
# renamed, quarantined, executed or deleted.  An archived script is a bypass only when an ACTIVE entry point reaches it,
# directly or through other scripts; an archived script without an active caller is historical and listed for audit.
# Active entry points: every tracked launcher outside tools/archive except offline Unit tests (their launch text is
# fixture data), plus archived files that are documented supported operator entry points.
$ArchivedEntryPoints = @('tools/archive/deprecated/LocalSuites.ps1')   # .github/skills/run-tests/SKILL.md: the current operator-driven complete local run
function Get-AvbReachable([string[]]$Start, $FactTable, $LeafMap) {
    $seen = @{}; $queue = New-Object System.Collections.Generic.Queue[string]
    foreach ($s in $Start) { if ($FactTable.Contains($s) -and -not $seen.ContainsKey($s)) { $seen[$s] = $true; $queue.Enqueue($s) } }
    while ($queue.Count -gt 0) {
        $k = $queue.Dequeue(); $f = $FactTable[$k]
        foreach ($d in @(@($f.DelegatesTo) + @($f['Edges']) | Where-Object { $_ })) {
            $ln = ([string]$d).ToLowerInvariant()
            foreach ($t in @(if ($LeafMap.ContainsKey($ln)) { $LeafMap[$ln] })) {   # every file with that name: over-approximation, safe
                if (-not $seen.ContainsKey($t)) { $seen[$t] = $true; $queue.Enqueue($t) }
            }
        }
    }
    return @($seen.Keys)
}
$ActiveEntryPoints = @($Facts.Keys | Where-Object { ($_ -notlike 'tools/archive/*' -and -not ($Inventory.Contains($_) -and $Inventory[$_] -eq 'offline-test')) -or $ArchivedEntryPoints -contains $_ })
$Reachable = Get-AvbReachable $ActiveEntryPoints $Facts $leaf
$ArchivedUnreachable = @($Facts.Keys | Where-Object { $_ -like 'tools/archive/*' -and (Test-Capable $_) -and $Reachable -notcontains $_ })
foreach ($k in $ArchivedUnreachable) { Write-Host ("  [INFO] historical, no active caller (not a dispatch route): {0}" -f $k) -ForegroundColor DarkGray }

Write-Host ""
foreach ($k in $Inventory.Keys) { Write-Host ("  [INFO] {0}" -f (Show-Facts $k)) -ForegroundColor Yellow }
Write-Host ""

tc_check 'TEST-HILPATH-000-authoritative-dispatcher-enforces-provenance-and-classification' (Test-Gated 'tools/test/Run-Tests-CI.ps1') (Show-Facts 'tools/test/Run-Tests-CI.ps1')
tc_check 'TEST-HILPATH-001-run-tests-ps1-routes-through-the-gates' (Test-Gated 'tools/test/Run-Tests.ps1') (Show-Facts 'tools/test/Run-Tests.ps1')
tc_check 'TEST-HILPATH-002-run-tests-elevated-routes-through-the-gates' (Test-Gated 'tools/test/Run-Tests-Elevated.ps1') (Show-Facts 'tools/test/Run-Tests-Elevated.ps1')
tc_check 'TEST-HILPATH-003-driver-benchmark-routes-through-the-gates' (Test-Gated 'tools/benchmark/Run-DriverBenchmark.ps1') (Show-Facts 'tools/benchmark/Run-DriverBenchmark.ps1')
tc_check 'TEST-HILPATH-009-check-system-does-not-execute-test-binaries' (Test-Gated 'tools/development/Check-System.ps1') (Show-Facts 'tools/development/Check-System.ps1')
# 004: the deprecated quick-test launchers (archived, historical) are no dispatch route: no active entry point reaches
#      them, or every path through them is gated (Q 2026-10-29: reachability, never modification of the archive).
$bats = @('tools/archive/deprecated/test/Quick-Test-Debug.bat', 'tools/archive/deprecated/test/Quick-Test-Release.bat')
$batBad = @(foreach ($b in $bats) { if ($Reachable -contains $b -and -not (Test-Gated $b)) { "reachable from an active entry point and not gated: $(Show-Facts $b)" } })
tc_check 'TEST-HILPATH-004-deprecated-quick-test-launchers-unreachable-or-gated' ($batBad.Count -eq 0) ($batBad -join ' || ')
tc_check 'TEST-HILPATH-005-deprecated-fullsuite-routes-through-the-gates' (Test-Gated 'tools/archive/deprecated/FullSuite.ps1') (Show-Facts 'tools/archive/deprecated/FullSuite.ps1')
tc_check 'TEST-HILPATH-006-no-full-suite-launcher-bat-reintroduced' (-not (Test-Path -LiteralPath (Join-Path $RepoRoot 'Run-Full-Tests.bat'))) 'Run-Full-Tests.bat exists at the repository root'

# 007: every dispatch-capable tracked launcher file that is an active entry point or reachable from one is classified
#      (a new bypass cannot appear silently); unreachable archived scripts are listed above as historical.
$unknown = @($Facts.Keys | Where-Object { (Test-Capable $_) -and -not $Inventory.Contains($_) -and $ArchivedUnreachable -notcontains $_ } | ForEach-Object { Show-Facts $_ })
tc_check 'TEST-HILPATH-007-every-active-test-binary-dispatch-path-is-classified' ($unknown.Count -eq 0) ("unclassified dispatch-capable files: {0}" -f ($unknown -join ' || '))

# 008 (refined, Q decision 2 2026-10-29): an archived dispatch-capable script is acceptable only if every path through
#      it is gated (delegation chain proven); direct ungated execution stays a violation.  The archive is scanned like every
#      other directory and never modified.
#      Refined 2026-10-29 (Q): only an archived script REACHABLE from an active entry point is a route; it must then be
#      gated on every path.  Unreachable ones are historical (listed above), never modified.
$archBad = @($Facts.Keys | Where-Object { $_ -like 'tools/archive/*' -and (Test-Capable $_) -and $Reachable -contains $_ -and -not (Test-Gated $_) } | ForEach-Object { Show-Facts $_ })
tc_check 'TEST-HILPATH-008-no-reachable-archived-script-dispatches-outside-the-gates' ($archBad.Count -eq 0) ("archived, reachable from an active entry point and not gated: {0}" -f ($archBad -join ' || '))

# 020: negative controls for the reachability rule (synthetic graph, nothing read from disk): an active entry point
#      calling an archived direct launcher - directly or through another archived script - is a violation; an archived
#      launcher nobody active calls is not; a gated archived delegator stays gated.
$fxF = [ordered]@{
    'tools/x/active.ps1'               = @{ DelegatesTo = @(); Edges = @('direct.bat') }
    'tools/x/active2.ps1'              = @{ DelegatesTo = @(); Edges = @('hop.ps1') }
    'tools/archive/deprecated/direct.bat' = @{ DelegatesTo = @(); Edges = @() }
    'tools/archive/deprecated/hop.ps1'    = @{ DelegatesTo = @(); Edges = @('deep.bat') }
    'tools/archive/deprecated/deep.bat'   = @{ DelegatesTo = @(); Edges = @() }
    'tools/archive/deprecated/lonely.bat' = @{ DelegatesTo = @(); Edges = @() }
}
$fxLeaf = @{}; foreach ($k in $fxF.Keys) { $n = [System.IO.Path]::GetFileName($k).ToLowerInvariant(); if (-not $fxLeaf.ContainsKey($n)) { $fxLeaf[$n] = @() }; $fxLeaf[$n] += $k }
$fxR = Get-AvbReachable @('tools/x/active.ps1', 'tools/x/active2.ps1') $fxF $fxLeaf
$rcBad = @()
foreach ($k in 'tools/archive/deprecated/direct.bat', 'tools/archive/deprecated/hop.ps1', 'tools/archive/deprecated/deep.bat') { if ($fxR -notcontains $k) { $rcBad += "$k not detected as reachable" } }
if ($fxR -contains 'tools/archive/deprecated/lonely.bat') { $rcBad += 'unreachable archived script reported as reachable' }
tc_check 'TEST-HILPATH-020-active-calls-into-archived-scripts-are-detected-directly-and-transitively' ($rcBad.Count -eq 0) ($rcBad -join '; ')

# 012-014: routes found by the repository-wide discovery (2026-10-29).
tc_check 'TEST-HILPATH-012-build-and-test-script-routes-through-the-gates' (Test-Gated 'scripts/build_and_test.ps1') (Show-Facts 'scripts/build_and_test.ps1')
tc_check 'TEST-HILPATH-013-vscode-tasks-launch-tests-only-through-the-gates' (Test-Gated '.vscode/tasks.json') (Show-Facts '.vscode/tasks.json')
$mk = @('tests/integration/ndis_receive/Makefile.mak', 'tests/integration/tx_timestamp/Makefile.mak')
tc_check 'TEST-HILPATH-014-makefile-run-targets-route-through-the-gates' (@($mk | Where-Object { -not (Test-Gated $_) }).Count -eq 0) (($mk | ForEach-Object { Show-Facts $_ }) -join ' || ')
# 015: archived files are never renamed or quarantined (Q 2026-10-29: tools/archive is a historical record, kept as is).
$qTracked = @($tracked | Where-Object { $_ -match '\.quarantined$' })
tc_check 'TEST-HILPATH-015-archive-never-quarantined' ($qTracked.Count -eq 0) ("tracked *.quarantined files (archive files are never renamed): {0}" -f ($qTracked -join ', '))
# 016: every active (non-archive) route classified gated is gated on every path - including callers of archived scripts.
$actBad = @($Inventory.Keys | Where-Object { $Inventory[$_] -in 'gated', 'supported' -and -not (Test-Gated $_) } | ForEach-Object { Show-Facts $_ })
tc_check 'TEST-HILPATH-016-every-active-route-is-gated-on-every-path' ($actBad.Count -eq 0) ($actBad -join ' || ')
# 017: offline tests are registered Unit tests under tests/unit (their launch text is fixture data or a sandboxed
#      runner copy - accepted by registration and their own sandbox contracts, e.g. TEST-HILGATE-028, not proven here).
$offBad = @($Inventory.Keys | Where-Object { $Inventory[$_] -eq 'offline-test' -and $unitEntries -notcontains $_ })
tc_check 'TEST-HILPATH-017-offline-test-launch-text-only-in-registered-unit-tests' ($offBad.Count -eq 0) ("not registered in UnitTests: {0}" -f ($offBad -join ', '))

# 018 (STATIC source contract, not runtime evidence - the installer requires Administrator): an attended driver
#      installation (benchmark, LocalSuites, manual) changes what a HIL execution verifies, so it never runs beside an
#      execution it does not belong to.  The attended main path takes the build exclusion before any test-signing,
#      uninstall or install step, refuses when it is not granted, and releases what it acquired in a finally block.
#      (The unattended -NonInteractive path runs only inside tools\ci\Invoke-HilDeployment.ps1, which holds the lock.)
$instPath = Join-Path $RepoRoot 'tools\setup\Install-Driver.ps1'
$instBad = @()
$instAst = [System.Management.Automation.Language.Parser]::ParseFile($instPath, [ref]$null, [ref]$null)
$mainTry = @($instAst.EndBlock.Statements | Where-Object { $_ -is [System.Management.Automation.Language.TryStatementAst] }) | Select-Object -Last 1
if (-not $mainTry) { $instBad += 'no main try statement' }
else {
    $body = $mainTry.Body.Extent.Text
    $iEx  = $body.IndexOf('Enter-AvbHilBuildExclusion')
    $iAct = @(@('Enable-TestSigningMode', 'Uninstall-Driver', '= Install-Driver') | ForEach-Object { $body.IndexOf($_) } | Where-Object { $_ -ge 0 } | Sort-Object)[0]
    if ($iEx -lt 0) { $instBad += 'Enter-AvbHilBuildExclusion not called in the attended main path' }
    elseif ($iEx -gt $iAct) { $instBad += 'build exclusion taken only after a state-changing step' }
    if ($body -notmatch '(?s)Enter-AvbHilBuildExclusion.*?if\s*\(\s*-not\s+\$\w+\.Ok\s*\)\s*\{[^}]*exit\s+1') { $instBad += 'no refusal (exit 1) when the exclusion is not granted' }
    if (-not $mainTry.Finally -or $mainTry.Finally.Extent.Text -notmatch 'Exit-AvbHilLock') { $instBad += 'acquired lock not released in a finally block' }
}
tc_check 'TEST-HILPATH-018-attended-driver-installation-takes-the-machine-lock' ($instBad.Count -eq 0) ("STATIC tools/setup/Install-Driver.ps1: {0}" -f ($instBad -join '; '))

# 019: the Run-Tests.ps1 frontend (Q decision 1 2026-10-29) resolves a typed program to its registered runner entry
#      and suite, fails closed for anything unregistered, and its full run is Unit + all four hardware suites with
#      HardwareMandatory last.  Its own functions are loaded from its syntax tree and called offline (nothing executed
#      besides them; the dispatch itself is Run-Tests-CI.ps1, covered by TEST-HILGATE).
$feBad = @()
$fePath = Join-Path $RepoRoot 'tools\test\Run-Tests.ps1'
$feAst = [System.Management.Automation.Language.Parser]::ParseFile($fePath, [ref]$null, [ref]$null)
$runCi = Join-Path $RepoRoot 'tools\test\Run-Tests-CI.ps1'; $repoRoot = $RepoRoot
foreach ($fn in 'ConvertTo-AvbEntryKey', 'Get-AvbRunnerRegistry', 'Resolve-AvbRunnerEntry') {
    $d = $feAst.Find({ param($x) $x -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $x.Name -eq $fn }, $true)
    if ($d) { . ([scriptblock]::Create($d.Extent.Text)) } else { $feBad += "$fn missing" }
}
if ($feBad.Count -eq 0) {
    $feReg = Get-AvbRunnerRegistry
    $cases = @(
        @('avb_test_i210_um.exe', '', 'avb_test_i210', 'HardwareIntegration'),
        @('ptp_clock_control_production_test.exe', '', 'ptp_clock_control_production', 'HardwareIntegration'),
        @('ptp_clock_control_test.exe', '', 'ptp_clock_control_test', 'HardwareUnit'),
        @('test_ptp_phc_stability.exe', '--case UT-CORR-005', 'test_ptp_phc_stability --case UT-CORR-005', 'HardwareMutation'),
        @('test_hot_plug', '', 'test_hot_plug', 'HardwareUnit'),   # listed there (Unsafe, gate-blocked) besides its Integration gap
        @('test_device_idle_regression', '', 'test_device_idle_regression', 'HardwareIntegration'),   # declared gap only
        @('test_magic_numbers.exe', '', 'test_magic_numbers', 'Unit'),
        @('quick_diagnostics.exe', '', 'quick_diagnostics', $null),
        @('not_a_registered_program.exe', '', 'not_a_registered_program', $null),
        @('Test-DebugLevelVerbosity.ps1', '', 'ps1:tests/integration/registry_diag/Test-DebugLevelVerbosity.ps1', $null)
    )
    foreach ($c in $cases) {
        $r = Resolve-AvbRunnerEntry -Name $c[0] -Arguments $c[1] -Registry $feReg
        if ($r.Entry -ne $c[2] -or $r.Suite -ne $c[3]) { $feBad += "$($c[0]) $($c[1]) -> '$($r.Entry)'/'$($r.Suite)', expected '$($c[2])'/'$($c[3])'" }
    }
}
$orderAsg = $feAst.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and $x.Left.Extent.Text -eq '$suiteOrder' }, $true) | Select-Object -First 1
$order = if ($orderAsg) { @($orderAsg.Right.FindAll({ param($x) $x -is [System.Management.Automation.Language.StringConstantExpressionAst] }, $true) | ForEach-Object { $_.Value }) } else { @() }
if (($order -join ',') -ne 'Unit,HardwareUnit,HardwareIntegration,HardwareMutation,HardwareMandatory') { $feBad += "full-run order [$($order -join ',')]" }
$feText = [System.IO.File]::ReadAllText($fePath)
if ($feText -notmatch 'PARTIAL / SMOKE') { $feBad += '-Quick not labelled as a partial run' }
if ($feText -match 'Read-Host') { $feBad += 'interactive prompt still present' }
tc_check 'TEST-HILPATH-019-run-tests-frontend-resolves-registered-entries-fails-closed-and-runs-every-suite' ($feBad.Count -eq 0) ($feBad -join '; ')

Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-HILPATH: test binaries can be dispatched outside the authoritative HIL gates (characterization)" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-HILPATH: all test-binary dispatch routes pass the authoritative HIL gates" -ForegroundColor Green
exit 0
