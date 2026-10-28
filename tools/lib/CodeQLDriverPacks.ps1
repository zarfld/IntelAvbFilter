# CodeQLDriverPacks.ps1
# Pure functions for the pinned CodeQL driver-analysis configuration (.github/codeql/).
# Consumers: tools/ci/Test-CodeQLDriverPacks.ps1 (real CodeQL CLI, tier 2/3) and
#            tests/unit/ci/test_codeql_driver_config.ps1 (offline contract, tier 1).
# No I/O, no CodeQL invocation here.

# Strict subset of a CodeQL code-scanning config: 'name', 'disable-default-queries' and a 'packs:' list of
# '<scope>/<pack>@<version>:<path>' strings.  Any other key or shape fails (it could add or drop queries outside the
# reviewed inventory).  Returns @{ Ok; Why; DisableDefault; Packs = @(@{ Spec; Pack; Version; Path; Exact }) }.
function Read-AvbCodeQLConfigPacks([string]$Text) {
    $res = @{ Ok = $false; Why = ''; DisableDefault = $false; Packs = @() }
    $packs = @(); $inPacks = $false; $n = 0
    foreach ($raw in ($Text -split "`r?`n")) {
        $n++
        $l = ($raw -replace '\s+#.*$', '').TrimEnd()
        if ($l -match '^\s*(#.*)?$') { continue }
        if ($l -match '^name:\s*\S') { $inPacks = $false; continue }
        if ($l -match '^disable-default-queries:\s*(true|false)$') { $res.DisableDefault = ($Matches[1] -eq 'true'); $inPacks = $false; continue }
        if ($l -match '^packs:$') { $inPacks = $true; continue }
        if ($inPacks -and $l -match '^\s+-\s+"?([^"\s]+)"?$') {
            $spec = $Matches[1]
            if ($spec -notmatch '^([a-z0-9-]+/[a-z0-9-]+)(?:@([^:]+))?:(.+)$') { $res.Why = "line ${n}: pack reference '$spec' is not <scope>/<pack>@<version>:<path>"; return $res }
            $ver = $(if ($Matches[2]) { $Matches[2] } else { '' })
            $packs += @{ Spec = $spec; Pack = $Matches[1]; Version = $ver; Path = ($Matches[3] -replace '\\', '/'); Exact = ($ver -match '^\d+\.\d+\.\d+$') }
            continue
        }
        $res.Why = "line ${n} outside the supported config subset: '$raw'"; return $res
    }
    $res.Ok = $true; $res.Packs = $packs
    return $res
}

# Reviewed inventory: '<pack>@<version>:<path><TAB><@id>' per line, '#' comments.  Returns @{ Ok; Why; Entries }.
function Read-AvbCodeQLInventory([string]$Text) {
    $entries = @(); $n = 0
    foreach ($l in ($Text -split "`r?`n")) {
        $n++
        if ($l -match '^\s*(#.*)?$') { continue }
        if ($l -notmatch '^([a-z0-9-]+/[a-z0-9-]+)@(\d+\.\d+\.\d+):([^\t]+\.ql)\t(\S*)$') { return @{ Ok = $false; Why = "line ${n}: '$l' is not <pack>@<version>:<path.ql><TAB><id>"; Entries = @() } }
        $entries += @{ Key = "$($Matches[1])@$($Matches[2]):$($Matches[3])"; Pack = $Matches[1]; Version = $Matches[2]; Path = $Matches[3]; Id = $Matches[4] }
    }
    return @{ Ok = $true; Why = ''; Entries = $entries }
}

function Format-AvbCodeQLInventory($Entries) {
    return (@($Entries | Sort-Object { $_.Key } -Culture ([cultureinfo]::InvariantCulture) | ForEach-Object { "$($_.Key)`t$($_.Id)" }) -join "`n")
}

# Absolute resolved query path inside a CodeQL package cache -> @{ Key; Pack; Version; Path }, or $null.
function ConvertTo-AvbQueryKey([string]$QueryPath) {
    $p = $QueryPath -replace '\\', '/'
    if ($p -notmatch '/packages/([^/]+)/([^/]+)/(\d+\.\d+\.\d+)/(.+\.ql)$') { return $null }
    return @{ Key = "$($Matches[1])/$($Matches[2])@$($Matches[3]):$($Matches[4])"; Pack = "$($Matches[1])/$($Matches[2])"; Version = $Matches[3]; Path = $Matches[4] }
}

# @id from a query's QLDoc header ('' when absent).
function Get-AvbQueryMetadataId([string]$QueryText) {
    $m = [regex]::Match($QueryText, '(?s)/\*\*(.*?)\*/')
    if (-not $m.Success) { return '' }
    $id = [regex]::Match($m.Groups[1].Value, '(?m)^\s*\*?\s*@id\s+(\S+)')
    return $(if ($id.Success) { $id.Groups[1].Value } else { '' })
}

# Exact comparison by query key; Changed = same key, different @id.
function Compare-AvbCodeQLInventory($Expected, $Actual) {
    $e = @{}; foreach ($x in @($Expected)) { $e[$x.Key] = $x.Id }
    $a = @{}; foreach ($x in @($Actual))   { $a[$x.Key] = $x.Id }
    $removed = @($e.Keys | Where-Object { -not $a.ContainsKey($_) } | Sort-Object)
    $added   = @($a.Keys | Where-Object { -not $e.ContainsKey($_) } | Sort-Object)
    $changed = @($e.Keys | Where-Object { $a.ContainsKey($_) -and $a[$_] -ne $e[$_] } | Sort-Object)
    return @{ Removed = $removed; Added = $added; Changed = $changed; Equal = (($removed.Count + $added.Count + $changed.Count) -eq 0) }
}

# Classify CodeQL CLI output.  Primary = the first 'A fatal error occurred' line (+ its 'eventual cause' line);
# anything after it is consequential.  Registry/network problems are INFRA_FAILURE, never a dependency verdict.
function Get-AvbCodeQLFailure([string]$Output) {
    $lines = @($Output -split "`r?`n")
    $i = -1
    for ($k = 0; $k -lt $lines.Count; $k++) { if ($lines[$k] -match 'A fatal error occurred') { $i = $k; break } }
    if ($i -lt 0) { return @{ Category = 'TOOL_FAILURE'; Primary = '' } }
    $primary = $lines[$i].Trim()
    if ($i + 1 -lt $lines.Count -and $lines[$i + 1] -match '^\s*\(eventual cause:') { $primary += ' ' + $lines[$i + 1].Trim() }
    $cat = 'TOOL_FAILURE'
    if ($primary -match 'Error downloading|SocketException|Connection reset|timed out|UnknownHost|HTTP (4|5)\d\d|status code (4|5)\d\d|rate limit|SSL|certificate') { $cat = 'INFRA_FAILURE' }
    elseif ($primary -match "(QL pack|Query pack|pack) '?[a-z0-9-]+/[a-z0-9-]+.*cannot be found|No version of .* satisfies|version range|could not resolve|is not compatible") { $cat = 'DEPENDENCY_FAILURE' }
    return @{ Category = $cat; Primary = $primary }
}
