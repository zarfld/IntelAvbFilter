# DriverDeploy.ps1 - unattended (CI) deployment of a verified staged driver package.  Dot-source; no side effects.
#
# Entry point: tools/setup/Install-Driver.ps1 -NonInteractive -Reinstall -PackageDir <dir> -ExpectedSysSha256 <sha>.
# The NDIS lightweight-filter mechanism is the canonical one of Install-Driver.ps1 (netcfg -u / copy .sys / ETW
# manifest registration / netcfg -l ... -c s -i MS_IntelAvbFilter).  This mode differs only where unattended
# operation requires it:
#   - never prompts, never reboots, never schedules a reboot-time file replacement (PendingFileRenameOperations);
#   - never disables/enables network adapters (one of them can be the runner's management connection), never
#     deletes DriverStore directories, never restarts the EventLog service;
#   - every native step is bounded; a step that does not finish in time is LEFT RUNNING (killing netcfg while it
#     holds the INetCfg write lock can corrupt the network configuration) and the machine is reported as needing
#     the operator (exit 3); STOP_PENDING is never success;
#   - pre-state and post-state are recorded; success requires service RUNNING, the filter bound, the installed .sys
#     equal to the staged SHA256 and the ETW provider registered.
# Outcomes / exit codes: DEPLOYED 0 | FAILED 1 (infrastructure failure, machine usable) | NEEDS_OPERATOR 3 (reboot or
# manual inspection required; the record stays as the incident, and a later deployment proceeds only after
# Invoke-AvbHilReconciliation establishes from CURRENT observations that the machine has recovered, #493).
#
# All system access goes through the $Sys adapter (New-AvbRealSystem); tests/unit/ci/test_ci_driver_deploy.ps1
# drives the same code with a scripted fake.

# Service state from Service Control Manager codes only, never from command output text: German Windows prints
# "OpenService FEHLER 1060" where the former sc.exe parser expected "FAILED 1060" and reported UNKNOWN instead of
# NOT_FOUND (CI run 41500818916).  $Probe: @{ Win32Error = <int|$null>; Status = <SERVICE_* state 1..7|$null> }.
# NOT_FOUND only for ERROR_SERVICE_DOES_NOT_EXIST (1060); anything else unresolved is UNKNOWN:<cause>.
function ConvertTo-AvbServiceState {
    param([hashtable]$Probe)
    $err = $Probe['Win32Error']; $st = $Probe['Status']; $detail = $Probe['Detail']   # indexer: no throw under StrictMode
    if ($null -ne $err -and $null -ne $st) { return "UNKNOWN:MALFORMED (Win32 error $err together with status $st)" }
    if ($null -ne $err) {
        if ($err -eq 1060) { return 'NOT_FOUND' }
        if ($err -eq 5) { return 'UNKNOWN:ACCESS_DENIED (Win32 error 5)' }
        return "UNKNOWN:WIN32_ERROR_$err"
    }
    if ($null -eq $st) { return "UNKNOWN:UNRESOLVED (no status and no Win32 error$(if ($detail) { ": $detail" }))" }
    $names = @{ 1 = 'STOPPED'; 2 = 'START_PENDING'; 3 = 'STOP_PENDING'; 4 = 'RUNNING'; 5 = 'CONTINUE_PENDING'; 6 = 'PAUSE_PENDING'; 7 = 'PAUSED' }
    if ($st -is [int] -and $names.ContainsKey($st)) { return $names[$st] }
    return "UNKNOWN:MALFORMED (status $st)"
}

# Real probe.  get_Status() as a METHOD: reading the .Status property of an absent service returns $null in PowerShell
# (property-getter exceptions are swallowed) instead of surfacing Win32 error 1060.
function Get-AvbServiceState {
    param([string]$Name)
    $p = @{ Win32Error = $null; Status = $null; Detail = '' }
    Add-Type -AssemblyName System.ServiceProcess
    $sc = New-Object System.ServiceProcess.ServiceController($Name)
    try { $p.Status = [int]$sc.get_Status() }
    catch {
        $w = $_.Exception; while ($w -and -not ($w -is [System.ComponentModel.Win32Exception])) { $w = $w.InnerException }
        if ($w) { $p.Win32Error = $w.NativeErrorCode } else { $p.Detail = $_.Exception.Message }
    } finally { $sc.Dispose() }
    return (ConvertTo-AvbServiceState -Probe $p)
}

# Whether the next configuration may deploy on this machine after this deployment record (hil.yml machine_ok).
# A failed deployment leaves the machine usable only when the state recorded after the failure is settled and known.
# $Record: avb-hil-deployment/1 (deployment.json).  @{ Ok; Reason }
function Get-AvbMachineOk {
    param($Record)
    if ($null -eq $Record) { return @{ Ok = $false; Reason = 'no deployment record: machine state unknown' } }
    switch ([string]$Record.verdict) {
        'DEPLOYED'         { return @{ Ok = $true; Reason = 'staged driver deployed and verified' } }
        'REJECTED_PACKAGE' { return @{ Ok = $true; Reason = 'package rejected before the installer ran: installed driver untouched' } }
        'DEPLOY_FAILED'    { }
        default            { return @{ Ok = $false; Reason = "deployment verdict '$($Record.verdict)' needs the operator or left the machine in an unknown state" } }
    }
    $post = if ($Record.install) { $Record.install.postState } else { $null }
    if ($null -eq $post) { return @{ Ok = $false; Reason = 'deployment failed without a recorded post-failure state: machine state unknown' } }
    if ([string]$post.service -notin @('NOT_FOUND', 'STOPPED', 'RUNNING')) { return @{ Ok = $false; Reason = "deployment failed leaving service state '$($post.service)' (not settled/known)" } }
    if (@($post.pendingRenames).Count -gt 0) { return @{ Ok = $false; Reason = 'deployment failed with a reboot-time replacement of IntelAvbFilter pending' } }
    return @{ Ok = $true; Reason = "deployment failed; recorded post-failure state is settled (service $($post.service), no pending replacement): the next deployment replaces it" }
}

function New-AvbRealSystem {
    $drivers = Join-Path $env:SystemRoot 'System32\drivers'
    return @{
        DriversDir = $drivers
        # Bounded native call.  @{ Completed; ExitCode; Output }.  On timeout the process is NOT killed.
        Invoke = {
            param([string]$File, [string[]]$Arguments, [int]$TimeoutSeconds, [string]$WorkDir)
            $psi = New-Object System.Diagnostics.ProcessStartInfo
            $psi.FileName = $File
            $psi.Arguments = (($Arguments | ForEach-Object { if ($_ -match '[\s"]') { '"' + $_.Replace('"', '\"') + '"' } else { $_ } }) -join ' ')
            $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
            if ($WorkDir) { $psi.WorkingDirectory = $WorkDir }
            $p = [System.Diagnostics.Process]::Start($psi)
            $o = $p.StandardOutput.ReadToEndAsync(); $e = $p.StandardError.ReadToEndAsync()
            if (-not $p.WaitForExit($TimeoutSeconds * 1000)) {
                return @{ Completed = $false; ExitCode = $null; Output = "still running after ${TimeoutSeconds}s (PID $($p.Id), left running)" }
            }
            return @{ Completed = $true; ExitCode = $p.ExitCode; Output = ($o.Result + $e.Result).Trim() }
        }
        # NOT_FOUND | RUNNING | STOPPED | STOP_PENDING | START_PENDING | UNKNOWN:<text>
        ServiceState = {
            Get-AvbServiceState -Name 'IntelAvbFilter'
        }
        # @( @{ Name; Enabled } ) of MS_IntelAvbFilter bindings.  Only "component not found" means no binding; any other
        # query error throws instead of looking like a completed removal.
        Bindings = {
            try { @(Get-NetAdapterBinding -ComponentID 'MS_IntelAvbFilter' -IncludeHidden -ErrorAction Stop | ForEach-Object { @{ Name = [string]$_.Name; Enabled = [bool]$_.Enabled } }) }
            catch { if ($_.CategoryInfo.Category -eq 'ObjectNotFound') { @() } else { throw } }
        }
        # Adapter carrying the IPv4/IPv6 default route (the runner's likely management connection) or $null
        ManagementAdapter = {
            $r = @(Get-NetRoute -DestinationPrefix '0.0.0.0/0', '::/0' -ErrorAction SilentlyContinue | Sort-Object RouteMetric | Select-Object -First 1)
            if ($r.Count -eq 0) { return $null }
            return [string]$r[0].InterfaceAlias
        }
        PendingRenames = {
            $v = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager' -Name PendingFileRenameOperations -ErrorAction SilentlyContinue).PendingFileRenameOperations
            return @($v | Where-Object { $_ -match '(?i)IntelAvbFilter' })
        }
        FileHash = { param([string]$Path) if (Test-Path -LiteralPath $Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash } else { $null } }
        CopyFile = { param([string]$Source, [string]$Destination) Copy-Item -LiteralPath $Source -Destination $Destination -Force -ErrorAction Stop }
        TestPath = { param([string]$Path) Test-Path -LiteralPath $Path }
        Sleep    = { param([int]$Seconds) Start-Sleep -Seconds $Seconds }
    }
}

# Polls ServiceState until it is in $Want or the bound expires.  @{ Ok; State; Waited }
function Wait-AvbServiceState {
    param([hashtable]$Sys, [string[]]$Want, [int]$TimeoutSeconds)
    $waited = 0
    while ($true) {
        $s = & $Sys.ServiceState
        if ($Want -contains $s) { return @{ Ok = $true; State = $s; Waited = $waited } }
        if ($waited -ge $TimeoutSeconds) { return @{ Ok = $false; State = $s; Waited = $waited } }
        & $Sys.Sleep 2; $waited += 2
    }
}

function Get-AvbDeployState {
    param([hashtable]$Sys)
    $sysPath = Join-Path $Sys.DriversDir 'IntelAvbFilter.sys'
    $mgmt = & $Sys.ManagementAdapter
    $b = @(& $Sys.Bindings)
    return [ordered]@{
        service = (& $Sys.ServiceState); installedSysSha256 = (& $Sys.FileHash $sysPath)
        bindings = @($b | ForEach-Object { [ordered]@{ name = $_.Name; enabled = $_.Enabled } })
        pendingRenames = @(& $Sys.PendingRenames); managementAdapter = $mgmt
        managementAdapterBound = [bool](@($b | Where-Object { $_.Name -eq $mgmt -and $_.Enabled }).Count)
    }
}

# ---------------------------------------------------------------------------------------------------------
# Deployment-incident reconciliation (#493).  A persisted unsafe deployment record (NEEDS_OPERATOR, IN_FLIGHT,
# unreadable) is a historical failure: it stays recorded, but it does not veto every later deployment.  The CALLER
# HOLDS THE MACHINE LOCK (tools/lib/HilLock.ps1; Get-AvbHilLockField is used from there).  Readiness is established
# only from CURRENT observations - never from the record's age, a free lock, a runner configuration or an
# acknowledgement.  The incident file is never rewritten: before a deployment may replace it, it is archived
# byte-for-byte (<lock>.incident-<utc>.json, SHA256 verified), and every attempt writes its own recovery record
# (<lock>.recovery-<utc>.json, schema avb-hil-recovery/1).  Recovery never yields DEPLOYED: the staged package is still
# verified and deployed through the normal path.
# ---------------------------------------------------------------------------------------------------------

# A live process that is (or may be) a deployment operation: the native tools the installer drives and the PowerShell
# entry points that drive them.  Returns the reason, or $null.  Attended installs are additionally excluded by the
# machine lock (Install-Driver.ps1 holds it), so a console that started one is covered while it runs.
function Test-AvbHilConflictProcess {
    param([string]$Name, $CommandLine)
    $n = ([string]$Name).ToLowerInvariant()
    if ($n -in @('netcfg.exe', 'pnputil.exe', 'wevtutil.exe')) { return 'driver-installation tool' }
    if ($n -in @('powershell.exe', 'pwsh.exe')) {
        if ([string]::IsNullOrEmpty([string]$CommandLine)) { return 'PowerShell process whose command line cannot be read (a deployment script cannot be excluded)' }
        if ([string]$CommandLine -match '(?i)(Install-Driver(-Elevated)?|Invoke-HilDeployment)\.ps1') { return 'deployment script' }
    }
    return $null
}

# Service ImagePath (registry form) -> file path.
function ConvertTo-AvbServiceImageFile {
    param([string]$ImagePath)
    $p = $ImagePath.Trim().Trim('"')
    if ($p -match '^(?i)\\SystemRoot\\(.+)$') { return (Join-Path $env:SystemRoot $Matches[1]) }
    if ($p -match '^\\\?\?\\(.+)$') { return $Matches[1] }
    if ($p -match '^(?i)System32\\') { return (Join-Path $env:SystemRoot $p) }
    return $p
}

# Real, read-only observation of the current machine.  A probe that fails is recorded as unavailable, never as a clean
# value.  $WatchPids: process IDs the incident recorded as left running (evidence; a PID can be reused).
function Get-AvbHilRecoveryObservation {
    param([int[]]$WatchPids = @(), [int]$SelfPid = $PID)
    $o = [ordered]@{
        observedUtc = [datetime]::UtcNow.ToString('o')
        processes = @(); processError = $null; watchedPids = @()
        service = $null; imagePath = $null; imageFile = $null; imageSha256 = $null; imageError = $null; system32Sha256 = $null
        pendingRenames = @(); pendingError = $null; bindings = @(); bindingsError = $null
    }
    try {
        $all = @(Get-CimInstance -ClassName Win32_Process -ErrorAction Stop)
        $byId = @{}; foreach ($p in $all) { $byId[[int]$p.ProcessId] = $p }
        # This process and the chain that started it (CI step shell, operator console) are not survivors.  A parent
        # created AFTER its child is a reused PID, not an ancestor: the walk stops there.
        $skip = @($SelfPid); $cur = $byId[$SelfPid]
        for ($i = 0; $i -lt 32 -and $null -ne $cur; $i++) {
            $par = $byId[[int]$cur.ParentProcessId]
            if ($null -eq $par -or $skip -contains [int]$par.ProcessId -or ($par.CreationDate -and $cur.CreationDate -and $par.CreationDate -gt $cur.CreationDate)) { break }
            $skip += [int]$par.ProcessId; $cur = $par
        }
        $o.processes = @(foreach ($p in $all) {
            if ($skip -contains [int]$p.ProcessId) { continue }
            $why = Test-AvbHilConflictProcess -Name $p.Name -CommandLine $p.CommandLine
            if ($why) {
                [ordered]@{ pid = [int]$p.ProcessId; name = [string]$p.Name
                            startedUtc = $(if ($p.CreationDate) { ([datetime]$p.CreationDate).ToUniversalTime().ToString('o') } else { $null })
                            commandLine = [string]$p.CommandLine; why = $why }
            }
        })
        $o.watchedPids = @(foreach ($w in @($WatchPids)) { $p = $byId[[int]$w]; [ordered]@{ pid = [int]$w; alive = ($null -ne $p); name = $(if ($p) { [string]$p.Name } else { $null }) } })
    } catch { $o.processError = "process enumeration failed: $($_.Exception.Message)" }
    try { $o.service = Get-AvbServiceState -Name 'IntelAvbFilter' } catch { $o.service = "UNKNOWN:PROBE_FAILED ($($_.Exception.Message))" }
    $s32 = Join-Path $env:SystemRoot 'System32\drivers\IntelAvbFilter.sys'
    try { if (Test-Path -LiteralPath $s32 -PathType Leaf) { $o.system32Sha256 = (Get-FileHash -LiteralPath $s32 -Algorithm SHA256 -ErrorAction Stop).Hash } } catch { $o.system32Sha256 = $null }
    if ($o.service -ne 'NOT_FOUND') {
        try {
            $o.imagePath = [string](Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Services\IntelAvbFilter' -Name ImagePath -ErrorAction Stop).ImagePath
            $o.imageFile = ConvertTo-AvbServiceImageFile -ImagePath $o.imagePath
            if (-not (Test-Path -LiteralPath $o.imageFile -PathType Leaf)) { $o.imageError = "service image '$($o.imageFile)' not found" }
            else { $o.imageSha256 = (Get-FileHash -LiteralPath $o.imageFile -Algorithm SHA256 -ErrorAction Stop).Hash }
        } catch { $o.imageError = "service image identity unavailable: $($_.Exception.Message)" }
    }
    try {
        $k = Get-Item -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager' -ErrorAction Stop
        $o.pendingRenames = @(@($k.GetValue('PendingFileRenameOperations')) | Where-Object { $_ -and $_ -match '(?i)IntelAvbFilter' } | ForEach-Object { [string]$_ })
    } catch { $o.pendingError = "PendingFileRenameOperations unreadable: $($_.Exception.Message)" }
    try { $o.bindings = @(& (New-AvbRealSystem).Bindings | ForEach-Object { [ordered]@{ name = $_.Name; enabled = $_.Enabled } }) }
    catch { $o.bindingsError = "MS_IntelAvbFilter binding query failed: $($_.Exception.Message)" }
    return $o
}

# Readiness for a NEW deployment from one observation document (real probe or the offline seam).  Each reason names
# the current condition.  The installed driver need not match the next staged package (the deployment replaces and
# verifies it), but its identity must be known.  @{ Decision = READY | NOT_READY; Reasons }
function Get-AvbHilReadiness {
    param($Observation)
    if ($null -eq $Observation) { return @{ Decision = 'NOT_READY'; Reasons = @('current machine observation unavailable') } }
    $f = { param([string]$n) Get-AvbHilLockField $Observation $n }
    $r = @()
    if (& $f 'processError') { $r += "process observation unavailable: $(& $f 'processError')" }
    foreach ($p in @(& $f 'processes')) {
        if ($null -eq $p) { continue }
        $r += ("live conflicting operation: {0} PID {1} (started {2}; {3}): {4}" -f (Get-AvbHilLockField $p 'name'), (Get-AvbHilLockField $p 'pid'),
               (Get-AvbHilLockField $p 'startedUtc'), (Get-AvbHilLockField $p 'why'), (Get-AvbHilLockField $p 'commandLine'))
    }
    $svc = [string](& $f 'service')
    if (-not $svc) { $r += 'IntelAvbFilter service state observation unavailable' }
    elseif ($svc -notin @('NOT_FOUND', 'STOPPED', 'RUNNING')) { $r += "IntelAvbFilter service state '$svc' is not settled/known" }
    if ($svc -ne 'NOT_FOUND') {
        if (& $f 'imageError') { $r += "installed driver identity unavailable: $(& $f 'imageError')" }
        elseif (-not (& $f 'imageSha256')) { $r += "installed driver identity incomplete: service registered (state '$svc') but no service image SHA256 observed" }
    }
    if (& $f 'pendingError') { $r += "pending driver replacement observation unavailable: $(& $f 'pendingError')" }
    foreach ($x in @(& $f 'pendingRenames')) { if ($x) { $r += "reboot-time replacement of IntelAvbFilter pending: $x" } }
    if (& $f 'bindingsError') { $r += "filter binding observation unavailable: $(& $f 'bindingsError')" }
    return @{ Decision = $(if ($r.Count -eq 0) { 'READY' } else { 'NOT_READY' }); Reasons = $r }
}

# The persisted deployment record as evidence: identity, exact bytes (SHA256) and the PIDs it recorded as left running.
function Get-AvbHilIncident {
    param([string]$Path)
    $i = [ordered]@{ path = $Path; present = $false; readable = $false; bytes = $null; sha256 = $null; schema = $null; verdict = $null
                     configuration = $null; runId = $null; runAttempt = $null; revision = $null; reason = $null; finishedUtc = $null
                     machineOk = $null; hungPids = @(); archivePath = $null }
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $i }
    $i.present = $true
    $i.bytes = (Get-Item -LiteralPath $Path).Length
    $i.sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    $rec = $null
    try { $rec = Get-Content -LiteralPath $Path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop } catch { return $i }
    if ($null -eq $rec -or $rec -is [string] -or [string](Get-AvbHilLockField $rec 'schema') -cne 'avb-hil-deployment/1') { return $i }
    $auth = Get-AvbHilLockField $rec 'authorized'; $inst = Get-AvbHilLockField $rec 'install'
    $i.readable = $true; $i.schema = [string]$rec.schema
    foreach ($n in 'verdict', 'configuration', 'reason', 'finishedUtc', 'machineOk') { $i[$n] = Get-AvbHilLockField $rec $n }
    $i.runId = Get-AvbHilLockField $auth 'runId'; $i.runAttempt = Get-AvbHilLockField $auth 'runAttempt'; $i.revision = Get-AvbHilLockField $auth 'revision'
    $i.hungPids = @(foreach ($s in @(Get-AvbHilLockField $inst 'steps')) {
        if ($null -ne $s -and (Get-AvbHilLockField $s 'ok') -eq $false -and [string](Get-AvbHilLockField $s 'detail') -match 'PID (\d+)') { [int]$Matches[1] }
    })
    return $i
}

# Shared reconciliation (Deploy: -Mode automatic -ArchiveIncident; Recover: -Mode operator).  Caller holds the lock.
# $Observe: scriptblock(int[] watchPids) -> observation document.  Returns the recovery record (decision READY |
# NOT_READY); the machine-local record path is in .recordPath, a copy goes to <EvidenceDir>\recovery.json.
function Invoke-AvbHilReconciliation {
    param([string]$LockPath, [ValidateSet('automatic', 'operator')][string]$Mode, $LockOwner, $PreviousLockOwner,
          [scriptblock]$Observe, [string]$EvidenceDir, [switch]$ArchiveIncident)
    $inflight = "$LockPath.inflight.json"
    $stamp = [datetime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
    $incident = Get-AvbHilIncident -Path $inflight
    $rec = [ordered]@{
        schema = 'avb-hil-recovery/1'; mode = $Mode; host = $env:COMPUTERNAME; startedUtc = [datetime]::UtcNow.ToString('o')
        lockOwner = $LockOwner; previousLockOwner = $PreviousLockOwner; incident = $incident
        observations = $null; decision = $null; reasons = @(); recordPath = $null; finishedUtc = $null
    }
    $reasons = @()
    try { $rec.observations = & $Observe @($incident.hungPids) }
    catch { $reasons += "current machine observation failed: $($_.Exception.Message)" }
    if ($reasons.Count -eq 0) { $reasons += @((Get-AvbHilReadiness -Observation $rec.observations).Reasons) }
    if ($reasons.Count -eq 0 -and $ArchiveIncident -and $incident.present) {
        $arch = "$LockPath.incident-$stamp.json"
        try {
            [System.IO.File]::Copy($inflight, $arch, $false)   # never overwrites an earlier archive
            $h = (Get-FileHash -LiteralPath $arch -Algorithm SHA256).Hash
            if ($h -ne $incident.sha256) { throw "archive SHA256 $h differs from the incident SHA256 $($incident.sha256)" }
            $incident.archivePath = $arch
        } catch { $reasons += "incident could not be preserved before deployment: $($_.Exception.Message)" }
    }
    $rp = "$LockPath.recovery-$stamp.json"
    $rec.recordPath = $rp
    $rec.reasons = $reasons
    $rec.decision = $(if ($reasons.Count -eq 0) { 'READY' } else { 'NOT_READY' })
    $rec.finishedUtc = [datetime]::UtcNow.ToString('o')
    try {
        $fs = [System.IO.File]::Open($rp, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
        try { $b = [System.Text.Encoding]::UTF8.GetBytes(($rec | ConvertTo-Json -Depth 8)); $fs.Write($b, 0, $b.Length) } finally { $fs.Dispose() }
    } catch {
        $rec.recordPath = $null
        $rec.reasons = @($rec.reasons) + "recovery record could not be written to ${rp}: $($_.Exception.Message)"
        $rec.decision = 'NOT_READY'
    }
    if ($EvidenceDir) { ($rec | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath (Join-Path $EvidenceDir 'recovery.json') -Encoding UTF8 }
    return $rec
}

function Invoke-AvbCiDriverDeployment {
    param([hashtable]$Sys, [string]$PackageDir, [string]$ExpectedSysSha256, [int]$StepTimeoutSeconds = 180, [int]$CopyAttempts = 10)
    $res = [ordered]@{ outcome = $null; exitCode = $null; reason = ''; steps = @(); preState = $null; postState = $null; managementRisk = '' }
    $step = { param([string]$Name, [bool]$Ok, [string]$Detail) $res.steps += ,([ordered]@{ step = $Name; ok = $Ok; detail = $Detail }) }
    $finish = { param([string]$Outcome, [string]$Reason)
        $res.outcome = $Outcome; $res.reason = $Reason
        # The state a failure left behind (read-only queries) decides machine_ok.  Not after NEEDS_OPERATOR: netcfg may
        # still be running and hold the INetCfg lock.
        if ($Outcome -eq 'FAILED' -and $null -eq $res.postState) {
            try { $res.postState = Get-AvbDeployState $Sys } catch { $res.postState = [ordered]@{ service = "UNKNOWN:UNRESOLVED (post-state query failed: $($_.Exception.Message))" } }
        }
        $res.exitCode = switch ($Outcome) { 'DEPLOYED' { 0 } 'NEEDS_OPERATOR' { 3 } default { 1 } }
        return $res }
    $drivers = $Sys.DriversDir
    $installedSys = Join-Path $drivers 'IntelAvbFilter.sys'
    $installedMan = Join-Path $drivers 'IntelAvbFilter.man'
    $want = ([string]$ExpectedSysSha256).ToUpperInvariant()

    # 1. Package (re-checked here, immediately before use: the caller verified signature and manifest).
    $inf = Join-Path $PackageDir 'IntelAvbFilter.inf'; $pkgSys = Join-Path $PackageDir 'IntelAvbFilter.sys'; $pkgMan = Join-Path $PackageDir 'IntelAvbFilter.man'
    if ($want -notmatch '^[0-9A-F]{64}$') { & $step 'package' $false 'no expected SHA256'; return (& $finish 'FAILED' 'no expected driver SHA256 (-ExpectedSysSha256)') }
    foreach ($f in $inf, $pkgSys, $pkgMan) { if (-not (& $Sys.TestPath $f)) { & $step 'package' $false "missing $f"; return (& $finish 'FAILED' "package file missing: $f (installed driver untouched)") } }
    $h = & $Sys.FileHash $pkgSys
    if ($h -ne $want) { & $step 'package' $false "sys $h"; return (& $finish 'FAILED' "package .sys SHA256 $h is not the expected $want (installed driver untouched)") }
    & $step 'package' $true $pkgSys

    # 2. Pre-state; refuse what only a reboot or the operator can resolve.
    $res.preState = Get-AvbDeployState $Sys
    if ($res.preState.managementAdapterBound) {
        $res.managementRisk = "management adapter '$($res.preState.managementAdapter)' is bound to MS_IntelAvbFilter: netcfg -u/-l restart its NDIS stack (connectivity interruption expected; if the runner loses its connection the job is lost and the next run collects the evidence)"
    }
    if ($res.preState.service -eq 'STOP_PENDING') { & $step 'pre-state' $false 'STOP_PENDING'; return (& $finish 'NEEDS_OPERATOR' 'IntelAvbFilter is STOP_PENDING before deployment: reboot required (never automatic)') }
    if (@($res.preState.pendingRenames).Count -gt 0) { & $step 'pre-state' $false 'pending rename'; return (& $finish 'NEEDS_OPERATOR' "reboot-time file replacement pending for IntelAvbFilter: $(@($res.preState.pendingRenames) -join ' | ')") }
    & $step 'pre-state' $true "service $($res.preState.service)"

    # 3. Remove the current filter component (absent driver = nothing to remove, not a failure).
    if (& $Sys.TestPath $installedMan) {
        $um = & $Sys.Invoke 'wevtutil.exe' @('um', $installedMan) $StepTimeoutSeconds ''
        & $step 'etw-unregister' $true "completed=$($um.Completed) rc=$($um.ExitCode)"
        if (-not $um.Completed) { return (& $finish 'NEEDS_OPERATOR' "wevtutil um did not finish in ${StepTimeoutSeconds}s (left running)") }
    }
    if ($res.preState.service -ne 'NOT_FOUND' -or @($res.preState.bindings).Count -gt 0) {
        $u = & $Sys.Invoke 'netcfg.exe' @('-u', 'MS_IntelAvbFilter') $StepTimeoutSeconds ''
        & $step 'netcfg-uninstall' ($u.Completed -and $u.ExitCode -eq 0) "completed=$($u.Completed) rc=$($u.ExitCode) $($u.Output)"
        if (-not $u.Completed) { return (& $finish 'NEEDS_OPERATOR' "netcfg -u did not finish in ${StepTimeoutSeconds}s (left running; #328 hang signature)") }
        if ($u.ExitCode -ne 0) { return (& $finish 'FAILED' ("netcfg -u returned rc {0} (0x{1:X8}): removal of MS_IntelAvbFilter not confirmed, nothing installed: {2}" -f $u.ExitCode, $u.ExitCode, $u.Output)) }
        $w = Wait-AvbServiceState -Sys $Sys -Want @('NOT_FOUND', 'STOPPED') -TimeoutSeconds $StepTimeoutSeconds
        & $step 'service-released' $w.Ok "state $($w.State) after $($w.Waited)s"
        if (-not $w.Ok) {
            if ($w.State -eq 'STOP_PENDING') { return (& $finish 'NEEDS_OPERATOR' "IntelAvbFilter stuck in STOP_PENDING after netcfg -u: reboot required (never automatic)") }
            return (& $finish 'FAILED' "IntelAvbFilter did not stop after netcfg -u (state $($w.State))")
        }
        $left = @(& $Sys.Bindings)
        if ($left.Count -gt 0) { & $step 'unbound' $false "$($left.Count) binding(s)"; return (& $finish 'FAILED' "MS_IntelAvbFilter still bound to $($left.Count) adapter(s) after netcfg -u (rc $($u.ExitCode))") }
    } else {
        & $step 'netcfg-uninstall' $true 'driver absent before deployment (nothing to remove)'
    }

    # 4. Driver image: bounded retry; a locked image is a reboot case, never a scheduled replacement.
    $copied = $false
    for ($i = 1; $i -le $CopyAttempts; $i++) {
        try { & $Sys.CopyFile $pkgSys $installedSys; $copied = $true; break } catch { if ($i -lt $CopyAttempts) { & $Sys.Sleep 2 } }
    }
    if (-not $copied) { & $step 'copy-sys' $false "locked after $CopyAttempts attempts"; return (& $finish 'NEEDS_OPERATOR' "installed IntelAvbFilter.sys still locked after $CopyAttempts attempts: the old image is loaded; reboot required (no reboot-time replacement is scheduled)") }
    $hi = & $Sys.FileHash $installedSys
    if ($hi -ne $want) { & $step 'copy-sys' $false "installed $hi"; return (& $finish 'FAILED' "installed .sys SHA256 $hi after copy is not $want") }
    & $step 'copy-sys' $true $installedSys

    # 5. ETW manifest (same commands as the attended path, without the EventLog restart).
    & $Sys.CopyFile $pkgMan $installedMan
    $null = & $Sys.Invoke 'wevtutil.exe' @('um', $installedMan) $StepTimeoutSeconds ''
    $null = & $Sys.Invoke 'icacls.exe' @($installedSys, '/grant', 'NT SERVICE\EventLog:(R)') $StepTimeoutSeconds ''
    $im = & $Sys.Invoke 'wevtutil.exe' @('im', $installedMan, "/mf:$installedSys", "/rf:$installedSys") $StepTimeoutSeconds ''
    & $step 'etw-register' ($im.Completed -and $im.ExitCode -eq 0) "completed=$($im.Completed) rc=$($im.ExitCode) $($im.Output)"
    if (-not $im.Completed) { return (& $finish 'NEEDS_OPERATOR' "wevtutil im did not finish in ${StepTimeoutSeconds}s (left running)") }
    if ($im.ExitCode -ne 0) { return (& $finish 'FAILED' "ETW manifest registration failed (wevtutil im rc $($im.ExitCode)): $($im.Output)") }

    # 6. Install the filter component.
    $l = & $Sys.Invoke 'netcfg.exe' @('-l', 'IntelAvbFilter.inf', '-c', 's', '-i', 'MS_IntelAvbFilter') $StepTimeoutSeconds $PackageDir
    & $step 'netcfg-install' ($l.Completed -and $l.ExitCode -eq 0) "completed=$($l.Completed) rc=$($l.ExitCode) $($l.Output)"
    if (-not $l.Completed) { return (& $finish 'NEEDS_OPERATOR' "netcfg -l did not finish in ${StepTimeoutSeconds}s (left running; #328 hang signature)") }
    if ($l.ExitCode -ne 0) { return (& $finish 'FAILED' "netcfg -l failed with rc $($l.ExitCode): $($l.Output)") }

    # 7. Post-state: running, bound, identity, ETW.
    $w2 = Wait-AvbServiceState -Sys $Sys -Want @('RUNNING') -TimeoutSeconds $StepTimeoutSeconds
    $res.postState = Get-AvbDeployState $Sys
    & $step 'service-running' $w2.Ok "state $($w2.State) after $($w2.Waited)s"
    if (-not $w2.Ok) {
        if ($w2.State -in @('STOP_PENDING', 'START_PENDING')) { return (& $finish 'NEEDS_OPERATOR' "IntelAvbFilter $($w2.State) after installation: not usable, reboot/inspection required") }
        return (& $finish 'FAILED' "IntelAvbFilter is $($w2.State) after installation, not RUNNING")
    }
    $bound = @($res.postState.bindings | Where-Object { $_.enabled })
    if ($bound.Count -eq 0) { & $step 'bound' $false 'no enabled binding'; return (& $finish 'FAILED' 'MS_IntelAvbFilter is not bound to any adapter after installation') }
    if ($res.postState.installedSysSha256 -ne $want) { & $step 'identity' $false $res.postState.installedSysSha256; return (& $finish 'FAILED' "installed .sys SHA256 $($res.postState.installedSysSha256) is not the staged $want") }
    $gp = & $Sys.Invoke 'wevtutil.exe' @('gp', 'IntelAvbFilter') $StepTimeoutSeconds ''
    & $step 'etw-provider' ($gp.Completed -and $gp.ExitCode -eq 0) "rc=$($gp.ExitCode)"
    if (-not ($gp.Completed -and $gp.ExitCode -eq 0)) { return (& $finish 'FAILED' "ETW provider IntelAvbFilter not registered after installation (wevtutil gp rc $($gp.ExitCode))") }
    return (& $finish 'DEPLOYED' "service RUNNING, bound to $($bound.Count) adapter(s), installed .sys = staged $want, ETW provider registered")
}
