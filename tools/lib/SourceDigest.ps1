# SourceDigest.ps1 - deterministic digest of the build inputs of the test executables (dot-source, no side effects)
#
# Local pre-commit hardware TDD: a test binary built from an UNCOMMITTED working tree is attributable only to exactly
# that working-tree content.  Build-Tests.ps1 records this digest in test-build-manifest.json; Run-Tests-CI.ps1
# recomputes it before a Hardware* artifact runs and blocks the artifact when any build input changed after the build
# (edited, created or deleted file, different submodule revision or submodule working tree).
# GitHub Actions keeps the strict clean-commit rule; a local digest match is LOCAL evidence, never CI evidence.
#
# Inputs (relative to the repository root): tracked AND untracked-not-ignored files under $AvbBuildInputRoots, plus
# each submodule's HEAD and its tracked/untracked-not-ignored files.  File contents are hashed with `git hash-object`
# (the same clean/line-ending filters git applies), so the digest does not depend on checkout line endings.

$AvbBuildInputRoots      = @('include', 'src', 'devices', 'tests', 'tools/build/Build-Tests.ps1', 'tools/vs_compile.ps1')
$AvbBuildInputSubmodules = @('external/intel_avb', 'intel-ethernet-regs')

# @{ Ok; Digest (64 hex, upper); Files; Error }.  Any git failure or missing root fails closed (Ok = $false).
function Get-AvbBuildInputDigest {
    param([string]$RepoRoot, [string[]]$Roots = $AvbBuildInputRoots, [string[]]$Submodules = $AvbBuildInputSubmodules)
    $res = @{ Ok = $false; Digest = $null; Files = 0; Error = '' }
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $records = [System.Collections.Generic.List[string]]::new()
        $sets = @(@{ Root = $RepoRoot; Prefix = ''; Paths = @($Roots) })
        foreach ($sm in @($Submodules)) { $sets += @{ Root = (Join-Path $RepoRoot $sm); Prefix = ($sm.TrimEnd('/') + '/'); Paths = @('.') } }
        foreach ($s in $sets) {
            if (-not (Test-Path -LiteralPath $s.Root)) { $res.Error = "build input root missing: '$($s.Prefix)'"; return $res }
            if ($s.Prefix) {
                $global:LASTEXITCODE = $null
                $h = @(& git -C $s.Root rev-parse HEAD 2>$null)
                if ($global:LASTEXITCODE -ne 0 -or $h.Count -ne 1) { $res.Error = "submodule '$($s.Prefix)' revision unavailable"; return $res }
                $records.Add(("submodule`t{0}`t{1}" -f $s.Prefix, ([string]$h[0]).Trim()))
            }
            $global:LASTEXITCODE = $null
            $files = @(& git -C $s.Root -c core.quotepath=off ls-files -co --exclude-standard -- @($s.Paths) 2>$null)
            if ($global:LASTEXITCODE -ne 0) { $res.Error = "git ls-files failed in '$($s.Prefix)'"; return $res }
            $files = @($files | Where-Object { $_ } | Sort-Object -Unique -CaseSensitive)
            $present = [System.Collections.Generic.List[string]]::new()
            foreach ($f in $files) {
                if (Test-Path -LiteralPath (Join-Path $s.Root $f) -PathType Leaf) { $present.Add($f) }
                else { $records.Add(("{0}{1}`tdeleted" -f $s.Prefix, $f)) }
            }
            # Paths go as arguments in chunks, never via stdin: PowerShell may prepend a BOM to piped native input
            # (session $OutputEncoding), which corrupts the first path.
            for ($c = 0; $c -lt $present.Count; $c += 100) {
                $chunk = @($present.GetRange($c, [Math]::Min(100, $present.Count - $c)))
                $global:LASTEXITCODE = $null
                $hashes = @(& git -C $s.Root hash-object -- @chunk 2>$null)
                if ($global:LASTEXITCODE -ne 0 -or $hashes.Count -ne $chunk.Count) {
                    $res.Error = "git hash-object failed in '$($s.Prefix)' ($($hashes.Count) of $($chunk.Count) files)"; return $res
                }
                for ($i = 0; $i -lt $chunk.Count; $i++) { $records.Add(("{0}{1}`t{2}" -f $s.Prefix, $chunk[$i], ([string]$hashes[$i]).Trim())) }
            }
        }
        $sorted = [string[]]$records.ToArray()
        [System.Array]::Sort($sorted, [System.StringComparer]::Ordinal)
        $bytes = [System.Text.Encoding]::UTF8.GetBytes(($sorted -join "`n"))
        $sha = [System.Security.Cryptography.SHA256]::Create()
        try { $res.Digest = ([System.BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '') } finally { $sha.Dispose() }
        $res.Files = $sorted.Count
        $res.Ok = $true
    } catch {
        $res.Error = "build input digest failed: $($_.Exception.Message)"
    } finally {
        $ErrorActionPreference = $prevEap
    }
    return $res
}
