# DriverIdentity.ps1 - identity of the IntelAvbFilter driver for the HIL preflight (#428).
# Dot-source; no side effects. Read-only: registry read, kernel module list, file hashes. Never changes a service,
# driver package or device.
#
# Three identities, which must be the same file content before any HIL test runs:
#   System32 copy    %SystemRoot%\System32\drivers\IntelAvbFilter.sys (the file the preflight hashed before #428)
#   selected image   the service's registry ImagePath, read without environment expansion and resolved below
#   running module   the kernel module list (EnumDeviceDrivers, GetDeviceDriverBaseNameW, GetDeviceDriverFileNameW; the
#                    method of driver_provenance() in tests/hardware/test_tas_activate.c), resolved by the same rules
# Limitation: the running module's SHA256 is the hash of the FILE at the module's path at check time. User mode cannot hash
# the in-memory image, so a file replaced after the module was loaded is not detected.
# Fail closed: a missing, unsupported or unresolved path, a missing file, an absent or ambiguous module, an unavailable
# module list, a running path different from the selected path, or any hash difference is a failure, never a warning.

# Resolves a kernel image path (service ImagePath or module file name). Supported forms:
#   \SystemRoot\<rest>   %SystemRoot%\<rest>   System32\<rest> (relative to SystemRoot)   \??\<X>:\<rest>   <X>:\<rest>
# Anything else, and a missing or empty value, is not resolved (the Windows default for an absent ImagePath is not assumed).
function Resolve-AvbKernelImagePath {
    param([AllowNull()][AllowEmptyString()][string]$Path, [Parameter(Mandatory = $true)][string]$SystemRoot)
    $r = [pscustomobject]@{ Ok = $false; Raw = $Path; Path = $null; Reason = $null }
    if ([string]::IsNullOrWhiteSpace($Path)) { $r.Reason = 'missing or empty'; return $r }
    $full = $null
    if ($Path -match '^\\SystemRoot\\(.+)$')        { $full = [System.IO.Path]::Combine($SystemRoot, $Matches[1]) }
    elseif ($Path -match '^%SystemRoot%\\(.+)$')    { $full = [System.IO.Path]::Combine($SystemRoot, $Matches[1]) }
    elseif ($Path -match '^System32\\(.+)$')        { $full = [System.IO.Path]::Combine($SystemRoot, 'System32\' + $Matches[1]) }
    elseif ($Path -match '^\\\?\?\\([A-Za-z]:\\.+)$') { $full = $Matches[1] }
    elseif ($Path -match '^[A-Za-z]:\\.+$')         { $full = $Path }
    else { $r.Reason = 'unsupported form'; return $r }
    try { $r.Path = [System.IO.Path]::GetFullPath($full); $r.Ok = $true }
    catch { $r.Reason = ('not a valid path: {0}' -f $_.Exception.Message) }
    return $r
}

# Picks the one loaded module with the given base name. $ModuleList: @{ Available; Modules = @(@{ BaseName; FileName }); Error }.
function Select-AvbRunningModule {
    param($ModuleList, [string]$BaseName = 'IntelAvbFilter.sys')
    $r = [pscustomobject]@{ Ok = $false; FileName = $null; Reason = $null }
    if ($null -eq $ModuleList -or -not $ModuleList.Available) {
        $why = if ($null -ne $ModuleList -and $ModuleList.Error) { $ModuleList.Error } else { 'no module list' }
        $r.Reason = ('running-module evidence unavailable: {0}' -f $why)
        return $r
    }
    $hits = @($ModuleList.Modules | Where-Object { $null -ne $_ -and $_.BaseName -and $_.BaseName -ieq $BaseName })
    if ($hits.Count -eq 0) { $r.Reason = ('{0} is not in the kernel module list (not loaded)' -f $BaseName); return $r }
    if ($hits.Count -gt 1) {
        $r.Reason = ('ambiguous: {0} loaded modules named {1} ({2})' -f $hits.Count, $BaseName, (($hits | ForEach-Object { $_.FileName }) -join '; '))
        return $r
    }
    $r.FileName = $hits[0].FileName
    $r.Ok = $true
    return $r
}

function Get-AvbFileSha256Or {
    param([string]$Path, [System.Collections.Generic.List[string]]$Failures, [string]$What)
    try { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256 -ErrorAction Stop).Hash.ToUpperInvariant() }
    catch { $Failures.Add(('{0}: cannot hash {1}: {2}' -f $What, $Path, $_.Exception.Message)); return $null }
}

# Decision: the System32 copy, the service-selected image and the running module are the same file content, and the
# running module is the selected file. Returns Ok, Failures and the three identities (path and file SHA256 each).
function Test-AvbDriverIdentity {
    param([string]$System32Path, [AllowNull()][AllowEmptyString()][string]$ImagePath, $ModuleList,
          [Parameter(Mandatory = $true)][string]$SystemRoot, [string]$BaseName = 'IntelAvbFilter.sys')
    $failures = New-Object 'System.Collections.Generic.List[string]'
    $sys = [pscustomobject]@{ Path = $System32Path; Sha256 = $null }
    $sel = [pscustomobject]@{ Raw = $ImagePath; Path = $null; Sha256 = $null }
    $run = [pscustomobject]@{ Raw = $null; Path = $null; Sha256 = $null }

    if (Test-Path -LiteralPath $System32Path -PathType Leaf) { $sys.Sha256 = Get-AvbFileSha256Or $System32Path $failures 'System32 copy' }
    else { $failures.Add(('System32 copy not found: {0}' -f $System32Path)) }

    $s = Resolve-AvbKernelImagePath -Path $ImagePath -SystemRoot $SystemRoot
    if (-not $s.Ok) { $failures.Add(("service ImagePath '{0}' not resolved: {1}" -f $ImagePath, $s.Reason)) }
    elseif (-not (Test-Path -LiteralPath $s.Path -PathType Leaf)) { $sel.Path = $s.Path; $failures.Add(('service-selected image not found: {0}' -f $s.Path)) }
    else { $sel.Path = $s.Path; $sel.Sha256 = Get-AvbFileSha256Or $s.Path $failures 'service-selected image' }

    $m = Select-AvbRunningModule -ModuleList $ModuleList -BaseName $BaseName
    if (-not $m.Ok) { $failures.Add($m.Reason) }
    else {
        $run.Raw = $m.FileName
        $p = Resolve-AvbKernelImagePath -Path $m.FileName -SystemRoot $SystemRoot
        if (-not $p.Ok) { $failures.Add(("running module path '{0}' not resolved: {1}" -f $m.FileName, $p.Reason)) }
        elseif (-not (Test-Path -LiteralPath $p.Path -PathType Leaf)) { $run.Path = $p.Path; $failures.Add(("file at the running module's path not found: {0}" -f $p.Path)) }
        else { $run.Path = $p.Path; $run.Sha256 = Get-AvbFileSha256Or $p.Path $failures 'running module file' }
    }

    if ($sel.Path -and $run.Path -and $sel.Path -ine $run.Path) {
        $failures.Add(('running module path {0} differs from the service-selected path {1}' -f $run.Path, $sel.Path))
    }
    foreach ($pair in @(@($sys, 'System32 copy', $sel, 'service-selected image'), @($sel, 'service-selected image', $run, 'running module file'),
                        @($sys, 'System32 copy', $run, 'running module file'))) {
        if ($pair[0].Sha256 -and $pair[2].Sha256 -and $pair[0].Sha256 -ne $pair[2].Sha256) {
            $failures.Add(('{0} SHA256 {1} differs from {2} SHA256 {3}' -f $pair[1], $pair[0].Sha256, $pair[3], $pair[2].Sha256))
        }
    }
    return [pscustomobject]@{ Ok = ($failures.Count -eq 0); Failures = $failures.ToArray(); System32 = $sys; Selected = $sel; Running = $run }
}

# Raw registry ImagePath of a service (REG_EXPAND_SZ not expanded), or $null when the key or value is absent.
function Get-AvbServiceImagePath {
    param([string]$ServiceName)
    $key = Get-Item -LiteralPath ('HKLM:\SYSTEM\CurrentControlSet\Services\{0}' -f $ServiceName) -ErrorAction SilentlyContinue
    if ($null -eq $key) { return $null }
    return $key.GetValue('ImagePath', $null, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
}

# Kernel module list: @{ Available; Modules = @(@{ BaseName; FileName }); Error }. Unavailable when the list cannot be read
# or when no module name is readable (a token may be denied kernel addresses).
function Get-AvbLoadedDriverModules {
    if (-not ('AvbDriverModules' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class AvbDriverModules {
    [DllImport("psapi.dll", SetLastError = true)]
    static extern bool EnumDeviceDrivers([Out] IntPtr[] lpImageBase, uint cb, out uint lpcbNeeded);
    [DllImport("psapi.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern uint GetDeviceDriverBaseNameW(IntPtr ImageBase, StringBuilder lpBaseName, uint nSize);
    [DllImport("psapi.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern uint GetDeviceDriverFileNameW(IntPtr ImageBase, StringBuilder lpFilename, uint nSize);
    public static int LastError;
    /* Each entry: { base name or null, file name or null }. Returns null when the list cannot be read. */
    public static string[][] List() {
        IntPtr[] bases = new IntPtr[1024];
        uint needed = 0;
        for (int attempt = 0; attempt < 3; attempt++) {
            if (!EnumDeviceDrivers(bases, (uint)(bases.Length * IntPtr.Size), out needed)) { LastError = Marshal.GetLastWin32Error(); return null; }
            if (needed <= (uint)(bases.Length * IntPtr.Size)) { break; }
            bases = new IntPtr[needed / IntPtr.Size + 64];
        }
        if (needed > (uint)(bases.Length * IntPtr.Size)) { LastError = -1; return null; }
        List<string[]> list = new List<string[]>();
        for (int i = 0; i < (int)(needed / IntPtr.Size); i++) {
            StringBuilder b = new StringBuilder(260), f = new StringBuilder(1024);
            uint lb = GetDeviceDriverBaseNameW(bases[i], b, (uint)b.Capacity);
            uint lf = lb == 0 ? 0 : GetDeviceDriverFileNameW(bases[i], f, (uint)f.Capacity);
            list.Add(new string[] { lb == 0 ? null : b.ToString(), lf == 0 ? null : f.ToString() });
        }
        return list.ToArray();
    }
}
'@
    }
    $raw = [AvbDriverModules]::List()
    if ($null -eq $raw) {
        return [pscustomobject]@{ Available = $false; Modules = @(); Error = ('EnumDeviceDrivers failed (Win32 {0})' -f [AvbDriverModules]::LastError) }
    }
    $mods = @($raw | Where-Object { $null -ne $_[0] } | ForEach-Object { [pscustomobject]@{ BaseName = $_[0]; FileName = $_[1] } })
    if ($mods.Count -eq 0) {
        return [pscustomobject]@{ Available = $false; Modules = @(); Error = ('no module name readable ({0} entries; kernel addresses may be withheld from this token)' -f @($raw).Count) }
    }
    return [pscustomobject]@{ Available = $true; Modules = $mods; Error = $null }
}
