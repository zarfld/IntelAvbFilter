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
# manual inspection required; no further HIL on this machine until the operator clears it).
#
# All system access goes through the $Sys adapter (New-AvbRealSystem); tests/unit/ci/test_ci_driver_deploy.ps1
# drives the same code with a scripted fake.

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
            $q = (& sc.exe query IntelAvbFilter 2>&1 | Out-String)
            if ($q -match 'FAILED 1060') { return 'NOT_FOUND' }
            if ($q -match 'STATE\s*:\s*\d+\s+(\w+)') { return $Matches[1] }
            return "UNKNOWN:$($q.Trim())"
        }
        # @( @{ Name; Enabled } ) of MS_IntelAvbFilter bindings
        Bindings = { @(Get-NetAdapterBinding -ComponentID 'MS_IntelAvbFilter' -IncludeHidden -ErrorAction SilentlyContinue | ForEach-Object { @{ Name = [string]$_.Name; Enabled = [bool]$_.Enabled } }) }
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

function Invoke-AvbCiDriverDeployment {
    param([hashtable]$Sys, [string]$PackageDir, [string]$ExpectedSysSha256, [int]$StepTimeoutSeconds = 180, [int]$CopyAttempts = 10)
    $res = [ordered]@{ outcome = $null; exitCode = $null; reason = ''; steps = @(); preState = $null; postState = $null; managementRisk = '' }
    $step = { param([string]$Name, [bool]$Ok, [string]$Detail) $res.steps += ,([ordered]@{ step = $Name; ok = $Ok; detail = $Detail }) }
    $finish = { param([string]$Outcome, [string]$Reason)
        $res.outcome = $Outcome; $res.reason = $Reason
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
