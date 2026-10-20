# HilPreflight.Policy.ps1 -- trusted HIL preflight policy library (REFERENCE COPY; functions only, no side effects)
#
# Installed by the operator, together with Invoke-HilPreflight.ps1, into the runner-local trusted folder
# (C:\hil-trusted).  Dot-sourcing this file only defines functions; every decision is made by the entry script.
# Offline tests (tests/unit/ci/test_hil_preflight.ps1) exercise these functions with synthetic inputs.
#
# Fail-closed rules: every required value must be present and match EXACTLY (case-sensitive); any query error,
# unparseable file or timeout denies; there is no permissive default.

function Get-HilPolicy {
    return @{
        Repository            = 'zarfld/IntelAvbFilter'
        EventName             = 'workflow_dispatch'
        Ref                   = 'refs/heads/master'
        WorkflowRef           = 'zarfld/IntelAvbFilter/.github/workflows/hil.yml@refs/heads/master'
        AdapterDids           = @('125B', '1533', '15BB')
        DriverFile            = "$env:SystemRoot\System32\drivers\IntelAvbFilter.sys"
        ServiceName           = 'IntelAvbFilter'
        TimeoutSeconds        = 90
        RequireNonAdminRunner = $true     # an elevated runner can rewrite this folder: ACLs would not protect it
        TrustedFiles          = @('Invoke-HilPreflight.ps1', 'HilPreflight.Policy.ps1', 'allowed-actors.txt', 'authorized-driver.sha256', 'bugcheck-ack.txt')
    }
}

# GitHub job environment (hook and step see the same default variables).
function Get-HilJobEnv {
    $e = @{}
    foreach ($n in 'GITHUB_ACTIONS', 'GITHUB_REPOSITORY', 'GITHUB_EVENT_NAME', 'GITHUB_REF', 'GITHUB_WORKFLOW_REF', 'GITHUB_SHA',
                   'GITHUB_WORKFLOW_SHA', 'GITHUB_RUN_ATTEMPT', 'GITHUB_ACTOR', 'GITHUB_TRIGGERING_ACTOR', 'GITHUB_HEAD_REF',
                   'GITHUB_BASE_REF', 'GITHUB_WORKSPACE', 'RUNNER_WORKSPACE', 'RUNNER_TEMP', 'RUNNER_TOOL_CACHE') {
        $v = [Environment]::GetEnvironmentVariable($n)
        if ($null -ne $v) { $e[$n] = $v }
    }
    return $e
}

# Pure decision on the job environment: @{ Allowed; Reasons[] }.  AllowedActors is mandatory (empty = deny).
function Test-HilJobAuthorization {
    param([hashtable]$JobEnv, [hashtable]$Policy, [string[]]$AllowedActors = @())
    $r = @()
    function V([string]$n) { if ($JobEnv.ContainsKey($n) -and $null -ne $JobEnv[$n]) { return [string]$JobEnv[$n] } return '' }
    foreach ($n in 'GITHUB_ACTIONS', 'GITHUB_REPOSITORY', 'GITHUB_EVENT_NAME', 'GITHUB_REF', 'GITHUB_WORKFLOW_REF', 'GITHUB_SHA',
                   'GITHUB_WORKFLOW_SHA', 'GITHUB_RUN_ATTEMPT', 'GITHUB_ACTOR') {
        if ((V $n) -eq '') { $r += "required variable $n is missing or empty" }
    }
    if ((V 'GITHUB_ACTIONS') -cne 'true')                    { $r += "GITHUB_ACTIONS is '$(V 'GITHUB_ACTIONS')', not 'true'" }
    if ((V 'GITHUB_REPOSITORY') -cne $Policy.Repository)     { $r += "repository '$(V 'GITHUB_REPOSITORY')' is not exactly '$($Policy.Repository)'" }
    if ((V 'GITHUB_EVENT_NAME') -cne $Policy.EventName)      { $r += "event '$(V 'GITHUB_EVENT_NAME')' is not exactly '$($Policy.EventName)'" }
    if ((V 'GITHUB_REF') -cne $Policy.Ref)                   { $r += "ref '$(V 'GITHUB_REF')' is not exactly '$($Policy.Ref)'" }
    if ((V 'GITHUB_WORKFLOW_REF') -cne $Policy.WorkflowRef)  { $r += "workflow '$(V 'GITHUB_WORKFLOW_REF')' is not exactly '$($Policy.WorkflowRef)'" }
    if ((V 'GITHUB_SHA') -cnotmatch '^[0-9a-f]{40}$')        { $r += "GITHUB_SHA '$(V 'GITHUB_SHA')' is not a 40-hex commit id" }
    if ((V 'GITHUB_WORKFLOW_SHA') -cne (V 'GITHUB_SHA'))     { $r += "workflow revision '$(V 'GITHUB_WORKFLOW_SHA')' differs from the code revision '$(V 'GITHUB_SHA')'" }
    if ((V 'GITHUB_RUN_ATTEMPT') -cne '1')                   { $r += "re-run (attempt '$(V 'GITHUB_RUN_ATTEMPT')') is not authorized; dispatch a new run" }
    if ((V 'GITHUB_HEAD_REF') -ne '' -or (V 'GITHUB_BASE_REF') -ne '') { $r += 'pull-request context (GITHUB_HEAD_REF/GITHUB_BASE_REF set)' }
    if ($AllowedActors.Count -eq 0) { $r += 'operator actor allow-list is missing or empty' }
    else {
        if ($AllowedActors -cnotcontains (V 'GITHUB_ACTOR')) { $r += "actor '$(V 'GITHUB_ACTOR')' is not on the operator allow-list" }
        $ta = V 'GITHUB_TRIGGERING_ACTOR'
        if ($ta -ne '' -and $AllowedActors -cnotcontains $ta) { $r += "triggering actor '$ta' is not on the operator allow-list" }
    }
    return @{ Allowed = ($r.Count -eq 0); Reasons = $r }
}

function Test-HilPathUnder([string]$Path, [string]$Parent) {
    if (-not $Parent) { return $false }
    $p = [System.IO.Path]::GetFullPath($Path).TrimEnd('\') + '\'
    $q = [System.IO.Path]::GetFullPath($Parent).TrimEnd('\') + '\'
    return $p.StartsWith($q, [System.StringComparison]::OrdinalIgnoreCase)
}

# The installed copy must not run from any job-writable or runner-application location.
function Test-HilScriptLocation {
    param([string]$ScriptRoot, [hashtable]$JobEnv)
    $r = @()
    if (-not $ScriptRoot) { return @{ Ok = $false; Reasons = @('script location unknown') } }
    foreach ($n in 'GITHUB_WORKSPACE', 'RUNNER_WORKSPACE', 'RUNNER_TEMP', 'RUNNER_TOOL_CACHE') {
        if ($JobEnv.ContainsKey($n) -and (Test-HilPathUnder $ScriptRoot $JobEnv[$n])) { $r += "preflight runs from job-writable location ($n): $ScriptRoot" }
    }
    if ($ScriptRoot -match '(?i)\\actions-runner(\\|$)') { $r += "preflight runs from the runner application directory: $ScriptRoot" }
    return @{ Ok = ($r.Count -eq 0); Reasons = $r }
}

# ACL rules as plain objects: @{ Sid; Rights; Type } (SIDs, locale-independent).
function Get-HilAclRules([string]$Path) {
    $acl = Get-Acl -LiteralPath $Path
    return @($acl.Access | ForEach-Object {
        $sid = try { $_.IdentityReference.Translate([System.Security.Principal.SecurityIdentifier]).Value } catch { [string]$_.IdentityReference }
        @{ Sid = $sid; Rights = [string]$_.FileSystemRights; Type = [string]$_.AccessControlType }
    })
}
# Only SYSTEM, Administrators, TrustedInstaller and CREATOR OWNER may hold any write-class right.
function Test-HilAclRules {
    param([object[]]$Rules, [string]$Label)
    $trusted = @('S-1-5-18', 'S-1-5-32-544', 'S-1-3-0', 'S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464')
    $r = @()
    foreach ($x in $Rules) {
        if ($x.Type -ne 'Allow') { continue }
        # Numeric renderings (generic-rights masks, possibly negative) are treated as write-capable.
        if ($x.Rights -match 'FullControl|Modify|Write|CreateFiles|AppendData|Delete|ChangePermissions|TakeOwnership|^-?\d+$' -and $trusted -notcontains $x.Sid) {
            $r += "$Label grants '$($x.Rights)' to $($x.Sid)"
        }
    }
    return @{ Ok = ($r.Count -eq 0); Reasons = $r }
}

# An elevated (administrator) runner can rewrite the trusted folder regardless of its ACL.
function Test-HilRunnerContext {
    param([hashtable]$Policy, [bool]$IsElevatedAdmin)
    if ($Policy.RequireNonAdminRunner -and $IsElevatedAdmin) { return @{ Ok = $false; Reasons = @('runner runs elevated as administrator: job code could modify the trusted preflight; use a non-admin runner account') } }
    return @{ Ok = $true; Reasons = @() }
}

function Read-HilAllowedActors([string]$Root) {
    $f = Join-Path $Root 'allowed-actors.txt'
    if (-not (Test-Path -LiteralPath $f)) { return @() }
    return @(Get-Content -LiteralPath $f | ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') })
}

# Default read-only probes.  Each returns @{ Ok; ...; Error } and never throws.
function Get-HilDefaultProbes {
    return @{
        Bugchecks = {
            param([datetime]$Since)
            try {
                $ev = @(Get-WinEvent -FilterHashtable @{ LogName = 'System'; Id = 1001; ProviderName = 'Microsoft-Windows-WER-SystemErrorReporting'; StartTime = $Since } -ErrorAction Stop)
                return @{ Ok = $true; Count = $ev.Count; Latest = $(if ($ev.Count) { $ev[0].TimeCreated } else { $null }); Error = '' }
            } catch {
                if ($_.FullyQualifiedErrorId -like 'NoMatchingEventsFound*') { return @{ Ok = $true; Count = 0; Latest = $null; Error = '' } }
                return @{ Ok = $false; Count = -1; Latest = $null; Error = $_.Exception.Message }
            }
        }
        Service = {
            param([string]$Name)
            try { $s = Get-Service -Name $Name -ErrorAction Stop; return @{ Ok = $true; Status = [string]$s.Status; Error = '' } }
            catch { return @{ Ok = $false; Status = ''; Error = $_.Exception.Message } }
        }
        FileHash = {
            param([string]$Path)
            try { return @{ Ok = $true; Hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash; Error = '' } }
            catch { return @{ Ok = $false; Hash = ''; Error = $_.Exception.Message } }
        }
        PnpDevices = {
            param([string]$Did)
            try { return @{ Ok = $true; Items = @(Get-PnpDevice -Class Net -ErrorAction Stop | Where-Object { $_.InstanceId -match "VEN_8086&DEV_$Did" } | ForEach-Object { [string]$_.Status }); Error = '' } }
            catch { return @{ Ok = $false; Items = @(); Error = $_.Exception.Message } }
        }
        NetAdapters = {
            param([string]$Did)
            try { return @{ Ok = $true; Items = @(Get-NetAdapter -ErrorAction Stop | Where-Object { $_.PnPDeviceID -match "VEN_8086&DEV_$Did" } | ForEach-Object { [string]$_.Status }); Error = '' } }
            catch { return @{ Ok = $false; Items = @(); Error = $_.Exception.Message } }
        }
    }
}

# Read-only machine baseline: @{ Ok; Reasons[] }.  Every query error or unparseable input denies.
function Test-HilMachineBaseline {
    param([hashtable]$Policy, [string]$Root, [hashtable]$Probes = (Get-HilDefaultProbes))
    $r = @()
    if (Test-Path -LiteralPath (Join-Path $Root 'HALT')) { $r += "operator HALT file present: $(Join-Path $Root 'HALT')" }
    $ackFile = Join-Path $Root 'bugcheck-ack.txt'
    $since = [datetime]::MinValue
    if (Test-Path -LiteralPath $ackFile) {
        $txt = (Get-Content -LiteralPath $ackFile -Raw).Trim()
        $parsed = [datetime]::MinValue
        if (-not [datetime]::TryParseExact($txt, 'o', [System.Globalization.CultureInfo]::InvariantCulture, [System.Globalization.DateTimeStyles]::RoundtripKind, [ref]$parsed)) {
            $r += "bugcheck acknowledgement '$txt' is not an ISO 8601 round-trip timestamp ($ackFile)"
        } else { $since = $parsed.AddSeconds(1) }
    }
    $bc = & $Probes.Bugchecks $since
    if (-not $bc.Ok) { $r += "bugcheck history unreadable: $($bc.Error)" }
    elseif ($bc.Count -gt 0) { $r += ("{0} unacknowledged bugcheck(s) (latest {1:o}); record an acknowledgement in {2}" -f $bc.Count, $bc.Latest, $ackFile) }
    $svc = & $Probes.Service $Policy.ServiceName
    if (-not $svc.Ok) { $r += "service $($Policy.ServiceName) unreadable: $($svc.Error)" }
    elseif ($svc.Status -ne 'Running') { $r += "service $($Policy.ServiceName) is $($svc.Status), not Running" }
    $authFile = Join-Path $Root 'authorized-driver.sha256'
    $want = ''
    if (Test-Path -LiteralPath $authFile) { $want = (((Get-Content -LiteralPath $authFile -Raw).Trim()) -split '\s+')[0] }
    if ($want -notmatch '^[0-9A-Fa-f]{64}$') { $r += "no valid operator-authorized driver SHA256 in $authFile" }
    else {
        $h = & $Probes.FileHash $Policy.DriverFile
        if (-not $h.Ok) { $r += "installed driver unreadable: $($h.Error)" }
        elseif ($h.Hash -ne $want.ToUpperInvariant()) { $r += "installed driver SHA256 $($h.Hash) is not the authorized $($want.ToUpperInvariant())" }
    }
    foreach ($did in $Policy.AdapterDids) {
        $p = & $Probes.PnpDevices $did
        if (-not $p.Ok) { $r += "PnP query for DEV_$did failed: $($p.Error)" }
        elseif (@($p.Items).Count -eq 0) { $r += "adapter DEV_$did not present (PnP)" }
        elseif (@($p.Items | Where-Object { $_ -ne 'OK' }).Count -gt 0) { $r += "adapter DEV_$did PnP status: $(@($p.Items) -join ',')" }
        $n = & $Probes.NetAdapters $did
        if (-not $n.Ok) { $r += "network adapter query for DEV_$did failed: $($n.Error)" }
        elseif (@($n.Items).Count -eq 0) { $r += "adapter DEV_$did has no network adapter" }
        elseif (@($n.Items | Where-Object { $_ -ne 'Up' }).Count -gt 0) { $r += "adapter DEV_$did link status: $(@($n.Items) -join ',')" }
    }
    return @{ Ok = ($r.Count -eq 0); Reasons = $r }
}

# Runs a script block in a separate runspace with a hard deadline (runner hooks have no timeout of their own).
function Invoke-HilWithTimeout {
    param([scriptblock]$ScriptBlock, [object[]]$ArgumentList = @(), [int]$Seconds)
    $ps = [powershell]::Create()
    $timedOut = $false
    try {
        [void]$ps.AddScript($ScriptBlock.ToString())
        foreach ($a in $ArgumentList) { [void]$ps.AddArgument($a) }
        $h = $ps.BeginInvoke()
        if (-not $h.AsyncWaitHandle.WaitOne([TimeSpan]::FromSeconds($Seconds))) {
            $timedOut = $true
            [void]$ps.BeginStop($null, $null)
            return @{ TimedOut = $true; Result = $null; Error = "timed out after $Seconds s" }
        }
        $res = $ps.EndInvoke($h)
        if ($ps.Streams.Error.Count -gt 0) { return @{ TimedOut = $false; Result = $res; Error = [string]$ps.Streams.Error[0] } }
        return @{ TimedOut = $false; Result = $res; Error = '' }
    } catch {
        return @{ TimedOut = $false; Result = $null; Error = $_.Exception.Message }
    } finally {
        if (-not $timedOut) { $ps.Dispose() }
    }
}
