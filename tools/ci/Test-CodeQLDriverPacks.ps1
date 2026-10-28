# Test-CodeQLDriverPacks.ps1
# Real-CodeQL validation of the pinned driver-analysis configuration (.github/codeql/codeql-driver-config.yml).
#
#   -Mode Resolve   (tier 2, runs BEFORE codeql-action/init, independent of it)
#       Fresh, isolated package cache (a warm cache would hide a missing transitive pack) ->
#       `codeql database init --codescanning-config` on an empty source root (the same pack download + suite
#       resolution path the init action uses) -> `codeql resolve queries` on the suite CodeQL generated from the
#       config -> exact comparison (query key + @id) against the reviewed inventory.
#       -ProposedInventoryOut writes the resolved inventory: the reviewed upgrade path (see docs in the test plan).
#
# Exit codes (all failures nonzero; the first fatal CodeQL line is reported as the primary error):
#   0 PASS | 2 CONFIG_INVALID | 3 INFRA_FAILURE (registry/network; not a dependency verdict) |
#   4 DEPENDENCY_FAILURE | 5 INVENTORY_MISMATCH | 6 TOOL_FAILURE (unclassified CodeQL error, timeout, missing output)
#
# Offline contract: tests/unit/ci/test_codeql_driver_config.ps1.  Regression origin: CodeQL runs 38997424025..40782452229.

param(
    [Parameter(Mandatory = $true)][ValidateSet('Resolve')][string]$Mode,
    [Parameter(Mandatory = $true)][string]$CodeQL,
    [string]$ConfigPath = '',
    [string]$InventoryPath = '',
    [string]$WorkDir = '',
    [string]$ReportPath = '',
    [string]$ProposedInventoryOut = '',
    [int]$TimeoutSeconds = 1200
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path (Split-Path (Split-Path -Parent $MyInvocation.MyCommand.Definition) -Parent) -Parent
. (Join-Path $RepoRoot 'tools\lib\NativeExit.ps1')
. (Join-Path $RepoRoot 'tools\lib\CodeQLDriverPacks.ps1')

if (-not $ConfigPath)    { $ConfigPath    = Join-Path $RepoRoot '.github\codeql\codeql-driver-config.yml' }
if (-not $InventoryPath) { $InventoryPath = Join-Path $RepoRoot '.github\codeql\codeql-driver-inventory.txt' }
if (-not $WorkDir)       { $WorkDir = Join-Path $(if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { $env:TEMP }) 'codeql-pack-validation' }

$ExitCodes = @{ PASS = 0; CONFIG_INVALID = 2; INFRA_FAILURE = 3; DEPENDENCY_FAILURE = 4; INVENTORY_MISMATCH = 5; TOOL_FAILURE = 6 }
$report = [ordered]@{ schemaVersion = 1; mode = $Mode; verdict = $null; primaryError = ''; codeqlPath = $CodeQL; codeqlVersion = '';
    config = $ConfigPath; configSha256 = ''; inventory = $InventoryPath; inventorySha256 = ''; packageCache = ''; installedPacks = @();
    expectedQueries = 0; resolvedQueries = 0; removed = @(); added = @(); changed = @(); startedUtc = [DateTime]::UtcNow.ToString('o') }

function Complete([string]$Verdict, [string]$Message) {
    $report.verdict = $Verdict
    if ($Verdict -ne 'PASS' -and -not $report.primaryError) { $report.primaryError = $Message }
    if ($ReportPath) { ($report | ConvertTo-Json -Depth 5) | Set-Content -LiteralPath $ReportPath -Encoding UTF8 }
    $color = $(if ($Verdict -eq 'PASS') { 'Green' } else { 'Red' })
    Write-Host ("[{0}] CodeQL driver packs ({1}): {2}" -f $Verdict, $Mode, $Message) -ForegroundColor $color
    exit $ExitCodes[$Verdict]
}

# Bounded native call; stdout/stderr to files (PS 5.1 would turn native stderr into terminating errors).
function Invoke-CodeQL([string]$Name, [string[]]$Arguments) {
    $out = Join-Path $WorkDir "$Name.out.log"; $err = Join-Path $WorkDir "$Name.err.log"
    $argLine = ($Arguments | ForEach-Object { if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ } }) -join ' '
    Write-Host ("  > codeql {0}" -f $argLine) -ForegroundColor DarkGray
    $p = Start-Process -FilePath $CodeQL -ArgumentList $argLine -NoNewWindow -PassThru -RedirectStandardOutput $out -RedirectStandardError $err
    $null = $p.Handle   # cache the handle so ExitCode is available after exit
    if (-not $p.WaitForExit($TimeoutSeconds * 1000)) {
        $stop = Stop-AvbProcessTree -Process $p
        Complete 'TOOL_FAILURE' ("codeql {0} timed out after {1}s (process tree stopped: {2})" -f $Name, $TimeoutSeconds, $stop.Ok)
    }
    $p.WaitForExit()
    $text = ([System.IO.File]::ReadAllText($err) + "`n" + [System.IO.File]::ReadAllText($out))
    return @{ Code = $p.ExitCode; Text = $text; Out = $out; Err = $err }
}

function Assert-CodeQL($r, [string]$Name) {
    if ($r.Code -eq 0) { return }
    $f = Get-AvbCodeQLFailure $r.Text
    $report.primaryError = $(if ($f.Primary) { $f.Primary } else { "codeql $Name exit $($r.Code), no fatal line (see $($r.Err))" })
    Write-Host ("  primary error: {0}" -f $report.primaryError) -ForegroundColor Red
    Write-Host ("  full output  : {0} / {1}" -f $r.Err, $r.Out) -ForegroundColor Red
    Complete $f.Category ("codeql {0} exited {1}" -f $Name, $r.Code)
}

Write-Host ""
Write-Host "=== CodeQL driver pack validation ($Mode) ===" -ForegroundColor Cyan
if (Test-Path -LiteralPath $WorkDir) { Remove-Item -LiteralPath $WorkDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null

# Offline preconditions (same contract as tier 1).
foreach ($f in @($ConfigPath, $InventoryPath)) { if (-not (Test-Path -LiteralPath $f -PathType Leaf)) { Complete 'CONFIG_INVALID' "missing $f" } }
if (-not (Test-Path -LiteralPath $CodeQL -PathType Leaf)) { Complete 'TOOL_FAILURE' "CodeQL CLI not found: $CodeQL" }
$cfg = Read-AvbCodeQLConfigPacks ([System.IO.File]::ReadAllText($ConfigPath))
if (-not $cfg.Ok) { Complete 'CONFIG_INVALID' $cfg.Why }
$loose = @($cfg.Packs | Where-Object { -not $_.Exact } | ForEach-Object { $_.Spec })
if ($loose.Count -gt 0) { Complete 'CONFIG_INVALID' ("non-exact pack references: {0}" -f ($loose -join ' | ')) }
$inv = Read-AvbCodeQLInventory ([System.IO.File]::ReadAllText($InventoryPath))
if (-not $inv.Ok) { Complete 'CONFIG_INVALID' $inv.Why }
$report.configSha256    = (Get-FileHash -LiteralPath $ConfigPath -Algorithm SHA256).Hash.ToLowerInvariant()
$report.inventorySha256 = (Get-FileHash -LiteralPath $InventoryPath -Algorithm SHA256).Hash.ToLowerInvariant()
$report.expectedQueries = $inv.Entries.Count

$v = Invoke-CodeQL 'version' @('version', '--format=terse')
Assert-CodeQL $v 'version'
$report.codeqlVersion = ([System.IO.File]::ReadAllText($v.Out)).Trim()
Write-Host ("  CodeQL CLI {0} ({1})" -f $report.codeqlVersion, $CodeQL)

# Isolated, new package cache: nothing pre-installed can satisfy a dependency the config does not provide.
$cache = Join-Path $WorkDir 'packages-cache'
$src   = Join-Path $WorkDir 'empty-source'
$db    = Join-Path $WorkDir 'db'
New-Item -ItemType Directory -Force -Path $src | Out-Null
Set-Content -LiteralPath (Join-Path $src 'probe.c') -Value 'int probe(void) { return 0; }' -Encoding ASCII
$report.packageCache = $cache

$init = Invoke-CodeQL 'database-init' @('database', 'init', '--force-overwrite', '--db-cluster', $db, "--source-root=$src",
    '--language=cpp', '--build-mode=manual', "--codescanning-config=$ConfigPath", "--common-caches=$cache")
Assert-CodeQL $init 'database init'
$pkgRoot = Join-Path $cache 'packages'
$report.installedPacks = @(Get-ChildItem -LiteralPath $pkgRoot -Directory -ErrorAction SilentlyContinue | ForEach-Object {
    $scope = $_.Name; Get-ChildItem -LiteralPath $_.FullName -Directory | ForEach-Object {
        $pack = $_.Name; Get-ChildItem -LiteralPath $_.FullName -Directory | ForEach-Object { "$scope/$pack@$($_.Name)" } } })
Write-Host ("  installed: {0}" -f ($report.installedPacks -join ', '))

# The suite CodeQL generated from the config (what `database analyze` runs).  Internal path of the pinned CLI;
# its absence is a TOOL_FAILURE, never a pass.
$suite = Join-Path $db 'cpp\temp\config-queries.qls'
if (-not (Test-Path -LiteralPath $suite -PathType Leaf)) { Complete 'TOOL_FAILURE' "CodeQL did not produce $suite (CLI layout changed?)" }
$res = Invoke-CodeQL 'resolve-queries' @('resolve', 'queries', $suite, "--common-caches=$cache", '--format=json')
Assert-CodeQL $res 'resolve queries'

$actual = @(); $foreign = @()
$resolved = ConvertFrom-Json ([System.IO.File]::ReadAllText($res.Out))   # argument form: PS 5.1 pipes a JSON array as ONE object
foreach ($q in $resolved) {
    $k = ConvertTo-AvbQueryKey ([string]$q)
    if ($null -eq $k) { $foreign += [string]$q; continue }
    $k.Id = Get-AvbQueryMetadataId ([System.IO.File]::ReadAllText([string]$q))
    $actual += $k
}
if ($foreign.Count -gt 0) { Complete 'TOOL_FAILURE' ("resolved queries outside the package cache: {0}" -f ($foreign -join ' | ')) }
$report.resolvedQueries = $actual.Count
if ($ProposedInventoryOut) {
    $hdr = "# Proposed by tools/ci/Test-CodeQLDriverPacks.ps1 -Mode Resolve; CodeQL $($report.codeqlVersion); config sha256 $($report.configSha256)`n"
    [System.IO.File]::WriteAllText($ProposedInventoryOut, $hdr + (Format-AvbCodeQLInventory $actual) + "`n")
    Write-Host "  proposed inventory: $ProposedInventoryOut"
}

$d = Compare-AvbCodeQLInventory $inv.Entries $actual
$report.removed = $d.Removed; $report.added = $d.Added; $report.changed = $d.Changed
Write-Host ("  queries: expected {0}, resolved {1}; removed {2}, added {3}, changed id {4}" -f $inv.Entries.Count, $actual.Count, $d.Removed.Count, $d.Added.Count, $d.Changed.Count)
foreach ($x in $d.Removed) { Write-Host "    REMOVED $x" -ForegroundColor Red }
foreach ($x in $d.Added)   { Write-Host "    ADDED   $x" -ForegroundColor Yellow }
foreach ($x in $d.Changed) { Write-Host "    CHANGED $x" -ForegroundColor Yellow }
if (-not $d.Equal) { Complete 'INVENTORY_MISMATCH' 'selected queries differ from the reviewed inventory (re-baseline only through a reviewed change)' }
Complete 'PASS' ("{0} queries resolved, identical to the reviewed inventory" -f $actual.Count)
