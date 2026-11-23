# DriverDeploy.ps1 - unattended (CI) deployment of a verified staged driver package.  Dot-source; no side effects.
#
# Entry point: tools/setup/Install-Driver.ps1 -NonInteractive -Reinstall -PackageDir <dir> -ExpectedSysSha256 <sha>.
# The NDIS lightweight-filter mechanism is the canonical one of Install-Driver.ps1 (netcfg -u / copy .sys / ETW
# manifest registration / netcfg -l ... -c s -i MS_IntelAvbFilter).  This mode differs only where unattended
# operation requires it:
#   - never prompts, never reboots, never schedules a reboot-time file replacement (PendingFileRenameOperations);
#   - never deletes DriverStore directories, never restarts the EventLog service;
#   - administers network adapters ONLY for the #510 quiescing (design A): immediately before netcfg -u it disables the
#     adapters that are administratively enabled AND have an enabled MS_IntelAvbFilter binding (recorded first; adapters
#     disabled beforehand are never touched; no name filter, no "all adapters" fallback), and after netcfg -l - or on
#     ANY earlier exit - it re-enables every adapter a disable was sent for, then confirms each one from a fresh
#     observation.  Every operation is bounded (Invoke-AvbAdapterAdminChange, the only code that may contain the
#     adapter cmdlets; TEST-CIDEPLOY-016/016b).  One of these adapters can be the runner's management connection:
#     the interruption is reported.  An unconfirmed restoration is never DEPLOYED (NEEDS_OPERATOR);
#   - every native step is bounded; a step that does not finish in time is LEFT RUNNING (killing netcfg while it
#     holds the INetCfg write lock can corrupt the network configuration) and the machine is reported as needing
#     the operator (exit 3); STOP_PENDING is never success;
#   - pre-state and post-state are recorded; success requires service RUNNING, the filter bound, the installed .sys
#     AND the service ImagePath file (the image the service loads; netcfg -l may select another DriverStore package,
#     #510) equal to the staged SHA256 and the ETW provider registered.
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
        # File the service actually loads (#510 identity postcondition): netcfg -l can select another DriverStore package
        # (highest DriverVer) than the staged one, so System32\drivers alone is not the identity.  @{ Path; Sha256; Error }
        ServiceImage = {
            try {
                $ip = [string](Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Services\IntelAvbFilter' -Name ImagePath -ErrorAction Stop).ImagePath
                $f = ConvertTo-AvbServiceImageFile -ImagePath $ip
                return @{ Path = $f; Sha256 = (Get-FileHash -LiteralPath $f -Algorithm SHA256 -ErrorAction Stop).Hash; Error = $null }
            } catch { return @{ Path = $null; Sha256 = $null; Error = $_.Exception.Message } }
        }
        # @( @{ Name; Guid; AdminEnabled } ) of the visible network adapters (bounded); throws when not measurable.
        AdapterStates = { Get-AvbAdapterStates -TimeoutSeconds 60 }
        # Bounded administrative change of one adapter (#510).  @{ Ok; Detail; ElapsedMs }
        SetAdapterEnabled = { param([string]$Name, [bool]$Enabled, [int]$TimeoutSeconds) Invoke-AvbAdapterAdminChange -Name $Name -Enabled $Enabled -TimeoutSeconds $TimeoutSeconds }
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
    $img = if ($Sys.ContainsKey('ServiceImage')) { & $Sys.ServiceImage } else { @{ Path = $null; Sha256 = $null; Error = 'service image probe not available' } }
    return [ordered]@{
        service = (& $Sys.ServiceState); installedSysSha256 = (& $Sys.FileHash $sysPath)
        serviceImagePath = $img.Path; serviceImageSha256 = $img.Sha256; serviceImageError = $img.Error
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

# Runs a read-only PowerShell query in a helper process with an ENFORCED time limit that includes output collection
# (-EncodedCommand: no quoting).  On timeout the only process terminated is this helper, through the process object
# returned by its own Start call (TEST-CIDEPLOY-016 enforces that structurally); installers, netcfg and every other
# process are never terminated.  Terminating the helper does not imply that work it requested elsewhere (for example
# from a WMI provider) was cancelled.  Process.Kill() is asynchronous: termination is reported as confirmed only when
# the bounded wait observes the exit, otherwise as unconfirmed.
# @{ status = COMPLETED | TIMEOUT | FAILED; exitCode; stdout; stderr; pid; disposition = exited | terminated |
#    'exited before termination ...' | 'termination unconfirmed ...' | 'not started'; timeoutSeconds; elapsedMs; error }
function Invoke-AvbBoundedPowerShell {
    param([string]$Script, [int]$TimeoutSeconds, [int]$CleanupSeconds = 5)
    $r = [ordered]@{ status = $null; exitCode = $null; stdout = ''; stderr = ''; pid = $null; disposition = 'not started'
                     timeoutSeconds = $TimeoutSeconds; elapsedMs = $null; error = $null }
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $p = $null
    try {
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
        $psi.Arguments = '-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand ' + [Convert]::ToBase64String([System.Text.Encoding]::Unicode.GetBytes($Script))
        $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
        $p = [System.Diagnostics.Process]::Start($psi)
        $r.pid = $p.Id
        $out = $p.StandardOutput.ReadToEndAsync(); $err = $p.StandardError.ReadToEndAsync()
        $remaining = [Math]::Max(0, $TimeoutSeconds * 1000 - [int]$sw.ElapsedMilliseconds)
        if ($p.WaitForExit($remaining)) {
            $r.disposition = 'exited'; $r.exitCode = $p.ExitCode
            # Output collection is inside the limit as well (a grandchild could keep the pipes open).
            $left = [Math]::Max(0, $TimeoutSeconds * 1000 - [int]$sw.ElapsedMilliseconds)
            if ([System.Threading.Tasks.Task]::WaitAll(@($out, $err), $left)) { $r.status = 'COMPLETED'; $r.stdout = $out.Result; $r.stderr = $err.Result }
            else { $r.status = 'TIMEOUT'; $r.error = "helper output not collected within ${TimeoutSeconds}s" }
        } else {
            $r.status = 'TIMEOUT'
            $killError = $null
            try { $p.Kill() } catch { $killError = $_.Exception.Message }
            if ($p.WaitForExit($CleanupSeconds * 1000)) {
                $r.disposition = if ($killError) { "exited before termination (termination request failed: $killError)" } else { 'terminated' }
            } else {
                $r.disposition = "termination unconfirmed after ${CleanupSeconds}s (helper PID $($r.pid) may still be running$(if ($killError) { "; termination request failed: $killError" }))"
            }
        }
    } catch {
        $r.status = 'FAILED'; $r.error = "helper could not be run: $($_.Exception.Message)"
    } finally {
        $r.elapsedMs = [int]$sw.ElapsedMilliseconds
        if ($null -ne $p) { $p.Dispose() }
    }
    return $r
}

# MS_IntelAvbFilter bindings through the bounded helper.  Only "component not found" means no binding (as in
# New-AvbRealSystem.Bindings).  The helper prints one AVBBIND:<json> line.
$AvbHilBindingQueryScript = @'
$ErrorActionPreference = 'Stop'
try { $b = @(Get-NetAdapterBinding -ComponentID 'MS_IntelAvbFilter' -IncludeHidden -ErrorAction Stop | ForEach-Object { [ordered]@{ name = [string]$_.Name; enabled = [bool]$_.Enabled } }) }
catch { if ($_.CategoryInfo.Category -eq 'ObjectNotFound') { $b = @() } else { [Console]::Error.WriteLine($_.Exception.Message); exit 2 } }
'AVBBIND:' + (ConvertTo-Json -InputObject @($b) -Compress -Depth 3)
exit 0
'@
# @{ status = MEASURED | TIMEOUT | FAILED; bindings; error; helper = @{ pid; disposition; timeoutSeconds; elapsedMs; exitCode } }
function Get-AvbHilBindingObservation {
    param([int]$TimeoutSeconds = 60, [int]$CleanupSeconds = 5, [string]$Script = $AvbHilBindingQueryScript)
    $h = Invoke-AvbBoundedPowerShell -Script $Script -TimeoutSeconds $TimeoutSeconds -CleanupSeconds $CleanupSeconds
    $res = [ordered]@{ status = $null; bindings = @(); error = $null
                       helper = [ordered]@{ pid = $h.pid; disposition = $h.disposition; timeoutSeconds = $h.timeoutSeconds; elapsedMs = $h.elapsedMs; exitCode = $h.exitCode } }
    if ($h.status -eq 'TIMEOUT') {
        $res.status = 'TIMEOUT'
        $res.error = "MS_IntelAvbFilter binding query exceeded ${TimeoutSeconds}s (helper PID $($h.pid) $($h.disposition)$(if ($h.error) { "; $($h.error)" }))"
    } elseif ($h.status -ne 'COMPLETED') {
        $res.status = 'FAILED'; $res.error = "MS_IntelAvbFilter binding query failed: $($h.error)"
    } else {
        $line = @(([string]$h.stdout) -split "`r?`n" | Where-Object { $_ -match '^AVBBIND:' }) | Select-Object -Last 1
        if ($h.exitCode -ne 0 -or -not $line) {
            $res.status = 'FAILED'; $res.error = "MS_IntelAvbFilter binding query failed (exit $($h.exitCode)): $(([string]$h.stderr).Trim())"
        } else {
            try {
                $res.bindings = @(ConvertFrom-Json -InputObject $line.Substring(8) | ForEach-Object { $_ } | ForEach-Object { [ordered]@{ name = [string]$_.name; enabled = [bool]$_.enabled } })
                $res.status = 'MEASURED'
            } catch { $res.status = 'FAILED'; $res.error = "MS_IntelAvbFilter binding query output unreadable: $($_.Exception.Message)" }
        }
    }
    return $res
}

# Real, read-only observation of the current machine.  A probe that fails is recorded as unavailable, never as a clean
# value.  $WatchPids: process IDs the incident recorded as left running (evidence; a PID can be reused).
# The binding query (INetCfg) can block while a conflicting netcfg holds the network configuration (PR #494 review):
# it is not attempted when process enumeration failed or found a conflict, and otherwise runs bounded
# (Get-AvbHilBindingObservation).  $ProcessQuery / $BindingObservation: injectable for offline tests.
function Get-AvbHilRecoveryObservation {
    param([int[]]$WatchPids = @(), [int]$SelfPid = $PID,
          [scriptblock]$ProcessQuery = { Get-CimInstance -ClassName Win32_Process -ErrorAction Stop },
          [scriptblock]$BindingObservation = { Get-AvbHilBindingObservation })
    $o = [ordered]@{
        observedUtc = [datetime]::UtcNow.ToString('o')
        processes = @(); processError = $null; watchedPids = @()
        service = $null; imagePath = $null; imageFile = $null; imageSha256 = $null; imageError = $null; system32Sha256 = $null
        pendingRenames = @(); pendingError = $null; bindings = @(); bindingsError = $null; bindingsStatus = $null; bindingsHelper = $null
    }
    try {
        $all = @(& $ProcessQuery)
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
    if ($o.processError) {
        $o.bindingsStatus = 'NOT_ATTEMPTED'
        $o.bindingsError = 'binding query not attempted: process observation failed, so a netcfg holding the network configuration cannot be excluded'
    } elseif (@($o.processes).Count -gt 0) {
        $o.bindingsStatus = 'NOT_ATTEMPTED'
        $o.bindingsError = "binding query not attempted: live conflicting operation(s) PID $((@($o.processes) | ForEach-Object { $_.pid }) -join ', ') may hold the network configuration"
    } else {
        try {
            $b = & $BindingObservation
            $o.bindingsStatus = [string](Get-AvbHilLockField $b 'status'); $o.bindings = @(Get-AvbHilLockField $b 'bindings')
            $o.bindingsError = Get-AvbHilLockField $b 'error'; $o.bindingsHelper = Get-AvbHilLockField $b 'helper'
        } catch { $o.bindingsStatus = 'FAILED'; $o.bindingsError = "MS_IntelAvbFilter binding observation failed: $($_.Exception.Message)" }
    }
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
    $bs = [string](& $f 'bindingsStatus')
    if ($bs -ne 'MEASURED') { $r += "filter binding observation $(if ($bs) { $bs } else { 'status missing' }): $(& $f 'bindingsError')" }
    elseif (& $f 'bindingsError') { $r += "filter binding observation unavailable: $(& $f 'bindingsError')" }
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

# ---------------------------------------------------------------------------------------------------------
# #510 design A: adapter quiescing around the filter replacement (see the header).  Read-only state query and the one
# permitted administrative change both run through Invoke-AvbBoundedPowerShell (bounded, nothing but the helper's own
# process is ever terminated).
# ---------------------------------------------------------------------------------------------------------
$AvbHilAdapterStateScript = @'
$ErrorActionPreference = 'Stop'
try { $a = @(Get-NetAdapter -ErrorAction Stop | ForEach-Object { [ordered]@{ name = [string]$_.Name; guid = [string]$_.InterfaceGuid; adminEnabled = ([string]$_.AdminStatus -eq 'Up') } }) }
catch { [Console]::Error.WriteLine($_.Exception.Message); exit 2 }
'AVBADAPT:' + (ConvertTo-Json -InputObject @($a) -Compress -Depth 3)
exit 0
'@
# @( @{ Name; Guid; AdminEnabled } ); throws when the adapters cannot be measured (never an empty "clean" answer).
function Get-AvbAdapterStates {
    param([int]$TimeoutSeconds = 60)
    $h = Invoke-AvbBoundedPowerShell -Script $AvbHilAdapterStateScript -TimeoutSeconds $TimeoutSeconds
    if ($h.status -ne 'COMPLETED' -or $h.exitCode -ne 0) {
        throw "adapter state query $($h.status) (exit $($h.exitCode), helper PID $($h.pid) $($h.disposition)): $(([string]$h.stderr).Trim()) $($h.error)"
    }
    $line = @(([string]$h.stdout) -split "`r?`n" | Where-Object { $_ -match '^AVBADAPT:' }) | Select-Object -Last 1
    if (-not $line) { throw 'adapter state query produced no AVBADAPT line' }
    return @(ConvertFrom-Json -InputObject $line.Substring(9) | ForEach-Object { $_ } | ForEach-Object { @{ Name = [string]$_.name; Guid = [string]$_.guid; AdminEnabled = [bool]$_.adminEnabled } })
}

# The ONLY code in this library that may contain the adapter cmdlets (TEST-CIDEPLOY-016b).  $AvbName / $AvbEnable are
# prepended by Invoke-AvbAdapterAdminChange.
$AvbHilAdapterAdminScript = @'
$ErrorActionPreference = 'Stop'
try {
    if ($AvbEnable) { Enable-NetAdapter -Name $AvbName -Confirm:$false -ErrorAction Stop } else { Disable-NetAdapter -Name $AvbName -Confirm:$false -ErrorAction Stop }
} catch { [Console]::Error.WriteLine($_.Exception.Message); exit 2 }
'AVBADMIN:done'
exit 0
'@
# @{ Ok; Detail; ElapsedMs }.  Ok = the cmdlet returned without error within the bound; the resulting state is
# confirmed separately by the caller from a fresh Get-AvbAdapterStates observation.
function Invoke-AvbAdapterAdminChange {
    param([string]$Name, [bool]$Enabled, [int]$TimeoutSeconds)
    $prefix = "`$AvbName = '" + $Name.Replace("'", "''") + "'`n`$AvbEnable = `$" + $(if ($Enabled) { 'true' } else { 'false' }) + "`n"
    $h = Invoke-AvbBoundedPowerShell -Script ($prefix + $AvbHilAdapterAdminScript) -TimeoutSeconds $TimeoutSeconds
    $ok = ($h.status -eq 'COMPLETED' -and $h.exitCode -eq 0 -and ([string]$h.stdout) -match 'AVBADMIN:done')
    $detail = "$(if ($Enabled) { 'enable' } else { 'disable' }) status=$($h.status) exit=$($h.exitCode) helper=$($h.disposition)$(if (-not $ok) { " err=$((([string]$h.stderr).Trim() + ' ' + $h.error).Trim())" })"
    return @{ Ok = $ok; Detail = $detail; ElapsedMs = $h.elapsedMs }
}

# Records the targets (enabled MS_IntelAvbFilter binding AND administratively enabled), disables them sequentially and
# confirms the disable.  Returns @{ Ok; Reason; Record; Sent } - Sent: every adapter a disable was sent for (restored
# afterwards by Invoke-AvbAdapterRestore whatever the outcome).
function Invoke-AvbAdapterQuiesce {
    param([hashtable]$Sys, $Bindings, [int]$TimeoutSeconds)
    $rec = [ordered]@{ targets = @(); skipped = @(); disable = @(); enable = @(); final = @(); restored = $null; error = '' }
    $out = @{ Ok = $false; Reason = ''; Record = $rec; Sent = @() }
    try { $states = @(& $Sys.AdapterStates) } catch { $rec.error = "adapter state query failed: $($_.Exception.Message)"; $out.Reason = $rec.error; return $out }
    foreach ($n in @($Bindings | Where-Object { $_.enabled } | ForEach-Object { [string]$_.name } | Select-Object -Unique)) {
        $a = @($states | Where-Object { $_.Name -eq $n }) | Select-Object -First 1
        if ($null -eq $a) { $rec.skipped += ,([ordered]@{ name = $n; reason = 'bound, but no visible adapter of that name: not administered' }); continue }
        if (-not $a.AdminEnabled) { $rec.skipped += ,([ordered]@{ name = $n; reason = 'administratively disabled before the deployment: left untouched' }); continue }
        $rec.targets += ,([ordered]@{ name = $n; guid = $a.Guid; originalAdminEnabled = $true })
    }
    foreach ($t in $rec.targets) {
        $out.Sent += $t.name
        $r = & $Sys.SetAdapterEnabled $t.name $false $TimeoutSeconds
        $rec.disable += ,([ordered]@{ name = $t.name; ok = [bool]$r.Ok; detail = $r.Detail; elapsedMs = $r.ElapsedMs })
        if (-not $r.Ok) { $out.Reason = "disable of adapter '$($t.name)' failed: $($r.Detail)"; return $out }
    }
    if ($rec.targets.Count -gt 0) {
        try { $now = @(& $Sys.AdapterStates) } catch { $out.Reason = "disable not confirmed: adapter state query failed: $($_.Exception.Message)"; return $out }
        $still = @($rec.targets | Where-Object { $n = $_.name; @($now | Where-Object { $_.Name -eq $n -and -not $_.AdminEnabled }).Count -eq 0 } | ForEach-Object { $_.name })
        if ($still.Count -gt 0) { $out.Reason = "disable not confirmed for adapter(s) $($still -join ', ')"; return $out }
    }
    $out.Ok = $true
    return $out
}

# Re-enables every adapter in $Sent (each attempt bounded, all attempted even after a failure) and confirms each one from
# a fresh observation.  Sets $Record.enable/final/restored; returns '' or the restoration problem.
function Invoke-AvbAdapterRestore {
    param([hashtable]$Sys, $Record, [string[]]$Sent, [int]$TimeoutSeconds)
    foreach ($n in @($Sent)) {
        $r = & $Sys.SetAdapterEnabled $n $true $TimeoutSeconds
        $Record.enable += ,([ordered]@{ name = $n; ok = [bool]$r.Ok; detail = $r.Detail; elapsedMs = $r.ElapsedMs })
    }
    try { $now = @(& $Sys.AdapterStates) } catch { $Record.restored = $false; $Record.error = "restoration not confirmed: adapter state query failed: $($_.Exception.Message)"; return $Record.error }
    $Record.final = @(foreach ($n in @($Sent)) { $a = @($now | Where-Object { $_.Name -eq $n }) | Select-Object -First 1; [ordered]@{ name = $n; adminEnabled = $(if ($a) { [bool]$a.AdminEnabled } else { $null }) } })
    $bad = @($Record.final | Where-Object { $_.adminEnabled -ne $true } | ForEach-Object { $_.name })
    $Record.restored = ($bad.Count -eq 0)
    if ($bad.Count -gt 0) { $Record.error = "adapter restoration not confirmed for $($bad -join ', ') (administratively enabled before the deployment)"; return $Record.error }
    return ''
}

function Invoke-AvbCiDriverDeployment {
    param([hashtable]$Sys, [string]$PackageDir, [string]$ExpectedSysSha256, [int]$StepTimeoutSeconds = 180, [int]$CopyAttempts = 10,
          [int]$AdapterTimeoutSeconds = 60)
    $res = [ordered]@{ outcome = $null; exitCode = $null; reason = ''; steps = @(); preState = $null; postState = $null; managementRisk = ''; adapters = $null }
    # sent: adapters a disable was sent for (#510); while any is outstanding the post-failure state is taken only after
    # the restoration (deferPost), so machine_ok judges the restored machine.
    $ctx = @{ sent = @(); deferPost = $false }
    $step = { param([string]$Name, [bool]$Ok, [string]$Detail) $res.steps += ,([ordered]@{ step = $Name; ok = $Ok; detail = $Detail }) }
    $finish = { param([string]$Outcome, [string]$Reason)
        $res.outcome = $Outcome; $res.reason = $Reason
        # The state a failure left behind (read-only queries) decides machine_ok.  Not after NEEDS_OPERATOR: netcfg may
        # still be running and hold the INetCfg lock.
        if ($Outcome -eq 'FAILED' -and $null -eq $res.postState -and -not $ctx.deferPost) {
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
        $res.managementRisk = "management adapter '$($res.preState.managementAdapter)' is bound to MS_IntelAvbFilter: it is disabled before netcfg -u and re-enabled after installation (#510 quiescing), and netcfg -u/-l restart its NDIS stack (connectivity interruption expected; if the runner loses its connection the job is lost and the next run collects the evidence)"
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
    # Steps 3b-6 run as one block so that EVERY exit after an adapter disable reaches the restoration below (#510).
    $core = {
        if ($res.preState.service -ne 'NOT_FOUND' -or @($res.preState.bindings).Count -gt 0) {
            # #510 design A: quiesce the recorded adapters immediately before netcfg -u.
            $ctx.deferPost = $true
            $q = Invoke-AvbAdapterQuiesce -Sys $Sys -Bindings $res.preState.bindings -TimeoutSeconds $AdapterTimeoutSeconds
            $res.adapters = $q.Record; $ctx.sent = @($q.Sent)
            & $step 'adapter-quiesce' $q.Ok ("targets=[{0}] skipped=[{1}]{2}" -f (@($q.Record.targets | ForEach-Object { $_.name }) -join ', '), (@($q.Record.skipped | ForEach-Object { "$($_.name): $($_.reason)" }) -join '; '), $(if ($q.Ok) { '' } else { " | $($q.Reason)" }))
            if (-not $q.Ok) { return (& $finish 'FAILED' "adapter quiescing before netcfg -u failed ($($q.Reason)); nothing uninstalled") }
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
    }
    $null = & $core

    # #510: re-enable every adapter a disable was sent for - after netcfg -l, or after any earlier exit of the block -
    # and confirm each one.  No adapter operation happens after this point.
    $restoreProblem = ''
    if (@($ctx.sent).Count -gt 0) {
        $restoreProblem = Invoke-AvbAdapterRestore -Sys $Sys -Record $res.adapters -Sent $ctx.sent -TimeoutSeconds $AdapterTimeoutSeconds
        & $step 'adapter-restore' ([bool]$res.adapters.restored) $(if ($restoreProblem) { $restoreProblem } else { "re-enabled and confirmed: $(@($ctx.sent) -join ', ')" })
    }
    $ctx.deferPost = $false
    if ($null -ne $res.outcome) {
        # The block already decided a failure; an unconfirmed restoration makes it an operator case.
        if ($res.outcome -eq 'FAILED' -and $null -eq $res.postState) {
            try { $res.postState = Get-AvbDeployState $Sys } catch { $res.postState = [ordered]@{ service = "UNKNOWN:UNRESOLVED (post-state query failed: $($_.Exception.Message))" } }
        }
        if ($restoreProblem) { $res.outcome = 'NEEDS_OPERATOR'; $res.exitCode = 3; $res.reason = "$($res.reason); $restoreProblem" }
        return $res
    }

    # 7. Post-state: running, bound, identity, ETW - and, when adapters were quiesced, their confirmed restoration.
    $w2 = Wait-AvbServiceState -Sys $Sys -Want @('RUNNING') -TimeoutSeconds $StepTimeoutSeconds
    $res.postState = Get-AvbDeployState $Sys
    & $step 'service-running' $w2.Ok "state $($w2.State) after $($w2.Waited)s"
    $verdict = $null
    if (-not $w2.Ok) {
        if ($w2.State -in @('STOP_PENDING', 'START_PENDING')) { $verdict = @('NEEDS_OPERATOR', "IntelAvbFilter $($w2.State) after installation: not usable, reboot/inspection required") }
        else { $verdict = @('FAILED', "IntelAvbFilter is $($w2.State) after installation, not RUNNING") }
    }
    if ($null -eq $verdict) {
        $bound = @($res.postState.bindings | Where-Object { $_.enabled })
        if ($bound.Count -eq 0) { & $step 'bound' $false 'no enabled binding'; $verdict = @('FAILED', 'MS_IntelAvbFilter is not bound to any adapter after installation') }
        elseif ($res.postState.installedSysSha256 -ne $want) { & $step 'identity' $false $res.postState.installedSysSha256; $verdict = @('FAILED', "installed .sys SHA256 $($res.postState.installedSysSha256) is not the staged $want") }
        elseif (-not $res.postState.serviceImageSha256) { & $step 'service-image' $false "not observed: $($res.postState.serviceImageError)"; $verdict = @('FAILED', "service image identity not observed (ImagePath file: $($res.postState.serviceImageError)): the loaded driver cannot be attributed to the staged package") }
        elseif ($res.postState.serviceImageSha256 -ne $want) { & $step 'service-image' $false "$($res.postState.serviceImagePath) $($res.postState.serviceImageSha256)"; $verdict = @('FAILED', "service ImagePath file $($res.postState.serviceImagePath) SHA256 $($res.postState.serviceImageSha256) is not the staged $want (netcfg -l selected another package from the driver store; the System32 copy is not what the service loads)") }
        else {
            & $step 'service-image' $true "$($res.postState.serviceImagePath) = staged"
            $gp = & $Sys.Invoke 'wevtutil.exe' @('gp', 'IntelAvbFilter') $StepTimeoutSeconds ''
            & $step 'etw-provider' ($gp.Completed -and $gp.ExitCode -eq 0) "rc=$($gp.ExitCode)"
            if (-not ($gp.Completed -and $gp.ExitCode -eq 0)) { $verdict = @('FAILED', "ETW provider IntelAvbFilter not registered after installation (wevtutil gp rc $($gp.ExitCode))") }
            else { $verdict = @('DEPLOYED', "service RUNNING, bound to $($bound.Count) adapter(s), installed .sys = service image = staged $want, ETW provider registered$(if (@($ctx.sent).Count -gt 0) { ", adapters re-enabled: $(@($ctx.sent) -join ', ')" })") }
        }
    }
    if ($restoreProblem) { return (& $finish 'NEEDS_OPERATOR' "$($verdict[1]); $restoreProblem") }
    return (& $finish $verdict[0] $verdict[1])
}
