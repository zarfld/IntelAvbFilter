param(
    [Parameter(Mandatory=$false)]
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [Parameter(Mandatory=$false)]
    [switch]$Full,

    [Parameter(Mandatory=$false)]
    [string]$TestName,

    [Parameter(Mandatory=$false)]
    [string]$TestExecutable,

    [Parameter(Mandatory=$false)]
    [string]$TestArgs = "",

    [Parameter(Mandatory=$false)]
    [string]$LogFile,

    # Start DebugView kernel capture around the test run and save to a log file.
    # Requires .github\skills\DbgView\DebugView\Dbgview.exe (run tools\setup\Install-DbgView.ps1 once).
    # The test is NOT executed when the capture cannot be confirmed as started (process alive + log file created).
    [Parameter(Mandatory=$false)]
    [switch]$CaptureDbgView,

    # Run -TestName through tools\test\Run-Tests-CI.ps1 -LocalDiagnostic in this Hardware* suite (safety class, adapter
    # baseline and post-test state check apply; build provenance is reported, not enforced) instead of the legacy
    # dispatcher Run-Tests.ps1.
    # -TestName is then the runner entry, e.g. 'test_ptp_phc_stability --case UT-CORR-005' or 'ps1:tests/...'.
    # -Suite without -TestName runs every registered entry of the suite (tools\archive\deprecated\FullSuite.ps1).
    [Parameter(Mandatory=$false)]
    [ValidateSet('', 'HardwareUnit', 'HardwareIntegration')]
    [string]$Suite = '',

    # With -Suite: machine-readable results document of the run (Run-Tests-CI.ps1 -ResultsJson).
    [Parameter(Mandatory=$false)]
    [string]$ResultsJson = '',

    # With -Suite: results documents of earlier suites of the same run (Run-Tests-CI.ps1 -PriorResultsDir), so a halt
    # keeps later Init/Mutating tests BLOCKED as in LocalSuites.ps1 and hil.yml.
    [Parameter(Mandatory=$false)]
    [string]$PriorResultsDir = '',

    # Bound for the elevated child; on expiry it is terminated and the run reported as TIMEOUT.  0 (default): the budget of
    # what will run (entries, per-adapter scaling, runner overhead; Get-AvbElevatedBudget, #480).  An explicit shorter value
    # is used as given and announced.
    [Parameter(Mandatory=$false)]
    [int]$TimeoutSeconds = 0
)

$scriptPath = Join-Path $PSScriptRoot 'Run-Tests.ps1'
$runCiPath  = Join-Path $PSScriptRoot 'Run-Tests-CI.ps1'
$repoRoot   = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
. (Join-Path $PSScriptRoot '..\lib\NativeExit.ps1')   # Stop-AvbProcessTree: timed-out child + descendants, verified

function ConvertTo-AvbPsLiteral { param([string]$Text) return "'" + $Text.Replace("'", "''") + "'" }

# #458 (Q 2026-11-19): Unsafe execution requires neither attendance nor per-entry permission, so naming an entry creates
# no authorization.  AVB_HIL_AUTHORIZE_UNSAFE is a compatibility variable only: a value the operator set is forwarded
# unchanged (normalized list) and has no dispatch effect; Run-Tests-CI.ps1 reports that when it sees it.  -TestName and
# -TestArgs are accepted for call compatibility and ignored.
function Get-AvbElevatedAuthorization {
    param([string]$Existing, [string]$TestName, [string]$TestArgs)
    $list = @($Existing -split '[;,]' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    return ($list -join ',')
}

# Command for the elevated child.  The exit code of the dispatcher is captured BEFORE Stop-Transcript: PowerShell
# -Command exits with the value of the last expression, and Stop-Transcript's return value would overwrite it.
# Captured via $global:LASTEXITCODE (reset first; no exit code = failure), then exit explicitly.
function New-AvbElevatedCommand {
    param([string]$Configuration, [string]$TestName, [string]$TestArgs, [string]$TestExecutable, [bool]$Full,
          [string]$LogFile, [string]$Suite, [string]$ResultsJson, [string]$PriorResultsDir = '', [string]$LegacyScript, [string]$RunCiScript,
          [string]$AuthorizeUnsafe = '', [string]$InventoryFile = '')
    $command = if ($LogFile) { "Start-Transcript -Path $(ConvertTo-AvbPsLiteral $LogFile) -Force | Out-Null; " } else { '' }
    $command += "`$_innerCode = 1; `$global:LASTEXITCODE = `$null; "
    # -Verb RunAs builds a new environment.  An operator-set compatibility value is carried across it unchanged; it has no
    # dispatch effect (#458).
    if ($AuthorizeUnsafe) { $command += "`$env:AVB_HIL_AUTHORIZE_UNSAFE = $(ConvertTo-AvbPsLiteral $AuthorizeUnsafe); " }
    # Where the runner reports the adapter count it budgets with (#480).
    if ($InventoryFile) { $command += "`$env:AVB_ELEVATED_INVENTORY_FILE = $(ConvertTo-AvbPsLiteral $InventoryFile); " }
    if ($Suite) {
        $command += "try { & $(ConvertTo-AvbPsLiteral $RunCiScript) -Suite $Suite -Configuration $Configuration"
        if ($TestName) {
            $entry = if ($TestArgs) { "$TestName $TestArgs" } else { $TestName }
            $command += " -Tests @($(ConvertTo-AvbPsLiteral $entry))"
        }
        if ($ResultsJson) { $command += " -ResultsJson $(ConvertTo-AvbPsLiteral $ResultsJson)" }
        if ($PriorResultsDir) { $command += " -PriorResultsDir $(ConvertTo-AvbPsLiteral $PriorResultsDir)" }
        $command += " -LocalDiagnostic"   # manual run: provenance reported, not enforced (the legacy path gets it from Run-Tests.ps1)
    } else {
        $command += "try { & $(ConvertTo-AvbPsLiteral $LegacyScript) -Configuration $Configuration"
        if ($Full) { $command += " -Full" }
        if ($TestName) {
            # PS1 test scripts (e.g. Test-DebugLevelVerbosity) are dispatched directly; all other names get .exe appended if missing.
            $testExe = if ($TestName -match '\.ps1$') { $TestName } elseif ($TestName -notmatch '\.exe$') { "$TestName.exe" } else { $TestName }
            $command += " -TestExecutable $(ConvertTo-AvbPsLiteral $testExe)"
        }
        if ($TestArgs) { $command += " -TestArgs $(ConvertTo-AvbPsLiteral $TestArgs)" }
        if ($TestExecutable) { $command += " -TestExecutable $(ConvertTo-AvbPsLiteral $TestExecutable)" }
        if ($LogFile) { $command += " -CollectLogs" }   # always collect logs when logging to file
    }
    $command += "; `$_innerCode = `$global:LASTEXITCODE; if (`$null -eq `$_innerCode) { `$_innerCode = 1 } } catch { `$_innerCode = 1 } finally { "
    $command += $(if ($LogFile) { "Stop-Transcript | Out-Null" } else { "`$null" })
    $command += " }; exit `$_innerCode"
    return $command
}

# Capture is confirmed only when the DbgView process is alive AND its log file exists.  @{ Ok; Error }
function Wait-AvbDbgViewStarted {
    param($Process, [string]$LogFile, [int]$TimeoutSeconds = 10)
    if ($null -eq $Process) { return @{ Ok = $false; Error = 'DbgView was not started (start script returned no process; Dbgview.exe missing or launch refused)' } }
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ($true) {
        if ($Process.HasExited) { return @{ Ok = $false; Error = "DbgView PID $($Process.Id) exited during start-up (another kernel capture active?)" } }
        if (Test-Path -LiteralPath $LogFile) { return @{ Ok = $true; Error = '' } }
        if ((Get-Date) -ge $deadline) { return @{ Ok = $false; Error = "DbgView PID $($Process.Id) is running but created no log file '$LogFile' within ${TimeoutSeconds}s" } }
        Start-Sleep -Milliseconds 250
    }
}

# After the stop: CAPTURED (non-empty log) | EMPTY | MISSING.  Never claims a capture without a log.
function Get-AvbDbgViewCaptureResult {
    param([string]$LogFile)
    if (-not (Test-Path -LiteralPath $LogFile)) { return @{ Status = 'MISSING'; Bytes = 0 } }
    $bytes = (Get-Item -LiteralPath $LogFile).Length
    return @{ Status = $(if ($bytes -gt 0) { 'CAPTURED' } else { 'EMPTY' }); Bytes = $bytes }
}

# --- Outer deadline (#480) -------------------------------------------------------------------------------------------
# The elevated child runs the runner's own per-item budgets one after another, so its deadline is their sum plus the runner's
# work around them: 300 s once (lock wait up to 60 s, preconditions, baseline, results) and 60 s per item (state checks between
# two items: at most 21.6 s in the archived suite runs 2026-11-18..24).  Tables, entry resolution and the per-item budget are the
# runners' own code, taken from their syntax trees (no second table); nothing of them is executed but these statements.
# AdapterCount: an upper bound of the adapters the driver enumerates; -1 = unknown, budgeted like the runner does, and reported.
# @{ Seconds; Entries ("suite: entry = n s"); Note }
function Get-AvbElevatedBudget {
    param([string]$RunCiScript, [string]$FrontEndScript, [string]$Suite, [string]$TestName, [string]$TestArgs, [bool]$Full, [int]$AdapterCount = -1,
          [int]$FixedSeconds = 300, [int]$PerItemSeconds = 60)
    $ci = [System.Management.Automation.Language.Parser]::ParseFile($RunCiScript, [ref]$null, [ref]$null)
    # Top-level statements only; what is not found is an error, never an empty table.
    $pick = { param($ast, [string]$what, [scriptblock]$match) $x = @($ast.EndBlock.Statements | Where-Object $match) | Select-Object -First 1; if (-not $x) { throw "$what not found in $($ast.Extent.File)" }; $x.Extent.Text }
    $asg = { param($ast, [string]$name) & $pick $ast "`$$name" { $_ -is [System.Management.Automation.Language.AssignmentStatementAst] -and $_.Left.Extent.Text -eq "`$$name" }.GetNewClosure() }
    $fun = { param($ast, [string]$name) & $pick $ast "function $name" { $_ -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $_.Name -eq $name }.GetNewClosure() }
    $ifs = { param($ast, [string]$cond, [string]$needs) & $pick $ast "if ($cond)" { $_ -is [System.Management.Automation.Language.IfStatementAst] -and $_.Clauses[0].Item1.Extent.Text -eq $cond -and $_.Extent.Text -match $needs }.GetNewClosure() }
    $text = @('param([string]$Suite, [string[]]$Tests, [int]$AdapterCount)', '$script:IsSandbox = $false; $TestTimeoutSeconds = 0')
    foreach ($n in 'UnitTests', 'IntegrationTests', 'HardwareUnitTests', 'HardwareIntegrationTests', 'HardwareIntegrationAttendedOnly', 'HardwareUnitCoverageGaps',
                   'HardwareIntegrationCoverageGaps', 'HilTestSafety', 'HilTestTimeoutSeconds', 'HilTestTimeoutPerAdapter') { $text += (& $asg $ci $n) }
    foreach ($n in 'Expand-AvbBareProgram', 'Get-AvbTestTimeoutSeconds') { $text += (& $fun $ci $n) }
    $text += (& $ifs $ci '$Tests.Count -gt 0' '\$TestList'), (& $ifs $ci '$TestTimeoutSeconds -eq 0' 'switch')
    # A whole HardwareIntegration suite also runs its attended-only entries; a Mutating:Target entry runs once per exact target.
    $text += 'if ($Tests.Count -eq 0 -and $Suite -eq ''HardwareIntegration'') { $TestList = @($TestList) + @($HardwareIntegrationAttendedOnly.Keys) }'
    $text += 'foreach ($t in @($TestList)) { $s = Get-AvbTestTimeoutSeconds -Entry $t -SuiteSeconds $TestTimeoutSeconds -AdapterCount $AdapterCount'
    $text += '  $k = if ([string]$HilTestSafety[$t] -eq ''Mutating:Target'') { [Math]::Max(1, $AdapterCount) } else { 1 }; [pscustomobject]@{ Entry = $t; Seconds = $s; Items = $k } }'
    $items = [scriptblock]::Create($text -join "`n")
    $notes = @(); $groups = [ordered]@{}
    if ($Suite) { $groups[$Suite] = @(if ($TestName) { $(if ($TestArgs) { "$TestName $TestArgs" } else { $TestName }) }) }
    elseif ($TestName) {
        # The front end's own resolution (Run-Tests.ps1 -TestExecutable).
        $fe = [System.Management.Automation.Language.Parser]::ParseFile($FrontEndScript, [ref]$null, [ref]$null)
        $feText = @('param([string]$Name, [string]$Arguments, [string]$runCi, [string]$repoRoot)') + @(foreach ($n in 'ConvertTo-AvbEntryKey', 'Get-AvbRunnerRegistry', 'Resolve-AvbRunnerEntry') { & $fun $fe $n }) +
                  @('Resolve-AvbRunnerEntry -Name $Name -Arguments $Arguments -Registry (Get-AvbRunnerRegistry)')
        $r = & ([scriptblock]::Create($feText -join "`n")) $TestName $TestArgs $RunCiScript (Split-Path (Split-Path (Split-Path $RunCiScript -Parent) -Parent) -Parent)
        foreach ($s in @($r.Groups.Keys)) { $groups[$s] = @($r.Groups[$s]) }
        if ($groups.Count -eq 0) { $groups['HardwareIntegration'] = @([string]$r.Entry); $notes += "'$($r.Entry)' is no registered entry: budgeted as one HardwareIntegration item" }
    } else { foreach ($s in 'Unit', 'HardwareUnit', 'HardwareIntegration') { $groups[$s] = @() } }   # the full run
    if ($AdapterCount -lt 0) { $notes += 'number of adapters unknown here: budgeted like the runner without an observed enumeration until the runner reports its own' }
    $sum = [long]$FixedSeconds; $entries = @()
    foreach ($s in @($groups.Keys)) {
        foreach ($i in @(& $items $s @($groups[$s]) $AdapterCount)) { $sum += [long]$i.Items * ([long]$i.Seconds + $PerItemSeconds); $entries += "${s}: $($i.Entry) = $($i.Seconds) s$(if ($i.Items -gt 1) { " x $($i.Items) targets" })" }
    }
    return @{ Seconds = $sum; Entries = $entries; Note = ($notes -join '; ') }
}

# Upper bound of the adapters the driver can enumerate: every MS_IntelAvbFilter binding (read-only); -1 when not readable.
function Get-AvbFilterAdapterBound {
    try { return [int]@(Get-NetAdapterBinding -ComponentID 'MS_IntelAvbFilter' -IncludeHidden -ErrorAction Stop).Count } catch { return [int]-1 }
}

# 0 = the computed budget.  An explicit value is used as given; one shorter than the budget is announced, never silent.
function Resolve-AvbElevatedDeadline {
    param([int]$Requested, $Budget)
    if ($Requested -le 0) { return @{ Seconds = [long]$Budget.Seconds; Warning = '' } }
    $w = if ($Requested -lt $Budget.Seconds) { "explicit -TimeoutSeconds $Requested is shorter than the $($Budget.Seconds) s budget of what will run: the child can be ended during a test or its cleanup" } else { '' }
    return @{ Seconds = [long]$Requested; Warning = $w }
}
# The count the elevated runner reported (Run-Tests-CI.ps1 Write-AvbObservedInventory); -1 = none (yet) or not readable.
function Read-AvbObservedInventory {
    param([string]$Path)
    try {
        if (-not $Path -or -not (Test-Path -LiteralPath $Path)) { return -1 }
        $m = [regex]::Match([System.IO.File]::ReadAllText($Path), '^\s*(\d{1,4})\s*$')
        if ($m.Success) { return [int]$m.Groups[1].Value }
    } catch { }
    return -1
}
# Reconciles a DEFAULT deadline with the runner's enumeration: more adapters than budgeted (or an unknown count) recompute it, never
# shorter.  An explicit deadline, a smaller count and no report leave it as it is.  @{ Deadline; BudgetedCount }
function Update-AvbElevatedDeadline {
    param($Deadline, [int]$Requested, [int]$BudgetedCount, [int]$Observed, [scriptblock]$BudgetFor)
    if ($Requested -gt 0 -or $Observed -lt 1 -or $Observed -le $BudgetedCount) { return @{ Deadline = $Deadline; BudgetedCount = $BudgetedCount } }
    $b = & $BudgetFor $Observed
    return @{ Deadline = @{ Seconds = [Math]::Max([long]$Deadline.Seconds, [long]$b.Seconds); Warning = $Deadline.Warning }; BudgetedCount = $Observed }
}
# Bounded wait for the elevated child; the deadline is reconciled while waiting and once more before it is enforced.
# @{ Exited; Deadline; BudgetedCount }
function Wait-AvbElevatedChild {
    param([System.Diagnostics.Process]$Process, $Deadline, [int]$Requested, [int]$BudgetedCount, [string]$InventoryFile, [scriptblock]$BudgetFor)
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($true) {
        $left = [double]$Deadline.Seconds * 1000 - $sw.ElapsedMilliseconds
        $slice = [int][Math]::Min(5000, [Math]::Max(1, $left))
        if ($Process.WaitForExit($slice)) { return @{ Exited = $true; Deadline = $Deadline; BudgetedCount = $BudgetedCount } }
        $u = Update-AvbElevatedDeadline -Deadline $Deadline -Requested $Requested -BudgetedCount $BudgetedCount -Observed (Read-AvbObservedInventory $InventoryFile) -BudgetFor $BudgetFor
        if ($u.Deadline.Seconds -ne $Deadline.Seconds) { Write-Host "Deadline: $($u.Deadline.Seconds) s (the runner enumerated $($u.BudgetedCount) adapter(s), was $($Deadline.Seconds) s)" -ForegroundColor Cyan }
        $Deadline = $u.Deadline; $BudgetedCount = $u.BudgetedCount
        if ($sw.ElapsedMilliseconds -ge [double]$Deadline.Seconds * 1000) { return @{ Exited = $false; Deadline = $Deadline; BudgetedCount = $BudgetedCount } }
    }
}

# Default log file if not specified - use logs directory
$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$logsDir = Join-Path $repoRoot "logs"
if (-not $LogFile) {
    if ($TestName -or $Full -or $Suite) {
        if (-not (Test-Path $logsDir)) { New-Item -ItemType Directory -Path $logsDir -Force | Out-Null }
        if ($TestName) {
            $LogFile = Join-Path $logsDir (("{0}_{1}.log" -f $TestName, $timestamp) -replace '[^\w.\-\\:]', '_')
        } elseif ($Suite) {
            $LogFile = Join-Path $logsDir ("{0}_{1}_{2}.log" -f $Suite, $Configuration, $timestamp)
        } elseif ($Full) {
            $LogFile = Join-Path $logsDir "full_test_suite_$timestamp.log"
        }
    }
}
# Only a value the operator set is forwarded (compatibility, no dispatch effect, #458); nothing is added for the named entry.
$inventoryFile = if ($LogFile) { "$LogFile.adapters.txt" } else { Join-Path $env:TEMP "avb_elevated_adapters_$PID.txt" }
if (Test-Path -LiteralPath $inventoryFile) { Remove-Item -LiteralPath $inventoryFile -Force }   # never a count of an earlier run
$authorizeUnsafe = Get-AvbElevatedAuthorization -Existing $env:AVB_HIL_AUTHORIZE_UNSAFE -TestName $TestName -TestArgs $TestArgs
$command = New-AvbElevatedCommand -Configuration $Configuration -TestName $TestName -TestArgs $TestArgs -TestExecutable $TestExecutable `
               -Full ([bool]$Full) -LogFile $LogFile -Suite $Suite -ResultsJson $ResultsJson -PriorResultsDir $PriorResultsDir -AuthorizeUnsafe $authorizeUnsafe -InventoryFile $inventoryFile -LegacyScript $scriptPath -RunCiScript $runCiPath
$arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', $command)
if ($authorizeUnsafe) { Write-Host "Forwarding AVB_HIL_AUTHORIZE_UNSAFE=$authorizeUnsafe to the elevated runner (compatibility only: no dispatch effect, #458)" -ForegroundColor Cyan }

if ($LogFile) {
    Write-Host "Output will be logged to: $LogFile" -ForegroundColor Cyan
}
$adapterBound = Get-AvbFilterAdapterBound
$budgetFor = { param([int]$n) Get-AvbElevatedBudget -RunCiScript $runCiPath -FrontEndScript $scriptPath -Suite $Suite -TestName $(if ($TestName) { $TestName } else { $TestExecutable }) `
                                  -TestArgs $TestArgs -Full ([bool]$Full) -AdapterCount $n }
$budget = & $budgetFor $adapterBound
$deadline = Resolve-AvbElevatedDeadline -Requested $TimeoutSeconds -Budget $budget
Write-Host "Deadline: $($deadline.Seconds) s (budget $($budget.Seconds) s: $(@($budget.Entries).Count) entry/entries, adapter bound $adapterBound)" -ForegroundColor Cyan
if ($budget.Note) { Write-Host "[WARN] $($budget.Note)" -ForegroundColor Yellow }
if ($deadline.Warning) { Write-Host "[WARN] $($deadline.Warning)" -ForegroundColor Yellow }

# -- Optional: DebugView kernel capture, confirmed BEFORE the test starts --------
$dbgViewProc = $null
$dbgLog = $null
$capture = $null
if ($CaptureDbgView) {
    $dbgViewScript = Join-Path $repoRoot '.github\skills\DbgView\Start-DbgViewCapture.ps1'
    $stem = if ($TestName) { "dbgview_$TestName" } elseif ($Suite) { "dbgview_${Suite}_$Configuration" } elseif ($Full) { 'dbgview_full_test_suite' } else { 'dbgview_tests' }
    $dbgLog = Join-Path $logsDir ((("{0}_{1}" -f $stem, $timestamp) -replace '[^\w.\-]', '_') + '.log')
    $started = @{ Ok = $false; Error = "start script not found at '$dbgViewScript'" }
    if (Test-Path $dbgViewScript) {
        Write-Host "[DbgView] Starting kernel capture -> $dbgLog" -ForegroundColor Cyan
        try { $dbgViewProc = & $dbgViewScript -LogFile $dbgLog } catch { $dbgViewProc = $null; Write-Host "[DbgView] start script threw: $($_.Exception.Message)" -ForegroundColor Red }
        $started = Wait-AvbDbgViewStarted -Process $dbgViewProc -LogFile $dbgLog
    }
    if (-not $started.Ok) {
        Write-Host "[DbgView] CAPTURE NOT STARTED: $($started.Error) --- test NOT executed (a reproduction without its kernel log is not the requested evidence)" -ForegroundColor Red
        if ($dbgViewProc -and -not $dbgViewProc.HasExited) { Stop-Process -Id $dbgViewProc.Id -Force -ErrorAction SilentlyContinue }
        exit 1
    }
    Write-Host "[DbgView] Capture confirmed: PID=$($dbgViewProc.Id), log $dbgLog" -ForegroundColor Green
    $capture = [ordered]@{ testName = $TestName; testArgs = $TestArgs; configuration = $Configuration; suite = $Suite; dbgViewPid = $dbgViewProc.Id
                           dbgViewLog = $dbgLog; startedUtc = (Get-Date).ToUniversalTime().ToString('o'); transcript = $LogFile; resultsJson = $ResultsJson
                           installedDriverSha256 = $null; outcome = $null; testExitCode = $null; stoppedUtc = $null; stop = $null; logStatus = $null; logBytes = 0
                           terminationVerified = $null; termination = $null; timeoutSeconds = $deadline.Seconds; timeoutBudgetSeconds = $budget.Seconds; budgetedAdapters = $adapterBound }
    $drv = Join-Path $env:SystemRoot 'System32\drivers\IntelAvbFilter.sys'
    if (Test-Path -LiteralPath $drv) { $capture.installedDriverSha256 = (Get-FileHash -LiteralPath $drv -Algorithm SHA256).Hash }
}

# -- Launch elevated child (bounded) and capture its exit code -------------------
# -PassThru returns a Process object; touching .Handle keeps it so ExitCode is readable after the wait (P0 fix #328).
$outcome = 'COMPLETED'
$childProc = Start-Process powershell -Verb RunAs -ArgumentList $arguments -PassThru
$childExitCode = -1
if ($null -ne $childProc) {
    $null = $childProc.Handle
    $waited = Wait-AvbElevatedChild -Process $childProc -Deadline $deadline -Requested $TimeoutSeconds -BudgetedCount $adapterBound -InventoryFile $inventoryFile -BudgetFor $budgetFor
    $deadline = $waited.Deadline
    if ($capture) { $capture.timeoutSeconds = $deadline.Seconds; $capture.budgetedAdapters = $waited.BudgetedCount }
    if ($waited.Exited) {
        $childExitCode = $childProc.ExitCode
        if ($null -eq $childExitCode) { $childExitCode = 1; $outcome = 'NO_EXIT_CODE' }
    } else {
        $outcome = 'TIMEOUT'
        # The whole tree (the test and everything it started, e.g. an installer), then verified: killing only this root
        # left the 2026-10-24 UT-CORR-009 installer chain running unnoticed for two days (#328 investigation).
        $termination = Stop-AvbProcessTree -Process $childProc
        if ($capture) { $capture.terminationVerified = [bool]$termination.Ok; $capture.termination = $termination.Detail }
        if ($termination.Ok) {
            Write-Host "[TIMEOUT] elevated test process PID=$($childProc.Id) still running after $($deadline.Seconds)s --- process tree terminated and verified ($($termination.Detail)); hardware state must be inspected" -ForegroundColor Red
        } else {
            Write-Host "[TIMEOUT] elevated test process PID=$($childProc.Id) still running after $($deadline.Seconds)s --- TERMINATION NOT VERIFIED ($($termination.Detail)); these processes may still change the machine; hardware state must be inspected" -ForegroundColor Red
        }
        $childExitCode = 1
    }
} else {
    $outcome = 'NOT_STARTED'
}

# -- Stop exactly the DebugView instance started above, verify the log -----------
$captureFailed = $false
if ($capture) {
    $stopScript = Join-Path $repoRoot '.github\skills\DbgView\Stop-DbgViewCapture.ps1'
    try {
        if (Test-Path $stopScript) { & $stopScript -ProcessId $dbgViewProc.Id } else { Stop-Process -Id $dbgViewProc.Id -Force }
    } catch { Write-Host "[DbgView] stop threw: $($_.Exception.Message)" -ForegroundColor Red }
    $capture.stop = if ($dbgViewProc.WaitForExit(5000)) { 'STOPPED' } else { 'STOP_FAILED' }
    $res = Get-AvbDbgViewCaptureResult -LogFile $dbgLog
    $capture.logStatus = $res.Status; $capture.logBytes = $res.Bytes
    $capture.outcome = $outcome; $capture.testExitCode = $childExitCode; $capture.stoppedUtc = (Get-Date).ToUniversalTime().ToString('o')
    ($capture | ConvertTo-Json -Depth 4) | Set-Content -LiteralPath "$dbgLog.capture.json" -Encoding UTF8
    if ($capture.stop -ne 'STOPPED') { Write-Host "[DbgView] CAPTURE STOP FAILED: PID=$($dbgViewProc.Id) still running (cleanup failure, distinct from the test result)" -ForegroundColor Red; $captureFailed = $true }
    if ($res.Status -ne 'CAPTURED') { Write-Host "[DbgView] CAPTURE $($res.Status): '$dbgLog' --- no kernel log for this run" -ForegroundColor Red; $captureFailed = $true }
    else { Write-Host "[DbgView] Captured $($res.Bytes) bytes -> $dbgLog (metadata: $dbgLog.capture.json)" -ForegroundColor Green }
}

if ($LogFile -and (Test-Path $LogFile)) {
    Write-Host "`nLog file created: $LogFile" -ForegroundColor Green
}

# -- Propagate child exit code -------------------------------------------------
# Exit codes from the dispatcher (and native test binaries):
#   0 = PASS          -- all tests passed
#   1 = FAIL/BLOCKED  -- at least one test failed, blocked, or CLEANUP_FAILED; also TIMEOUT / no exit code here
#   2 = SKIP_ONLY     -- tests ran but no hardware found; all SKIP_NO_TESTS
# A requested capture that failed never turns into success.
if ($outcome -eq 'NOT_STARTED') { Write-Host "[FAIL] elevated test process was not started" -ForegroundColor Red; exit 1 }
if ($captureFailed -and $childExitCode -eq 0) { exit 1 }
exit $childExitCode
