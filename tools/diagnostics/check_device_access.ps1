# check_device_access.ps1 - Classify device presence vs. access-denied for #328 triage
# Run elevated for full access; run non-elevated to observe caller token effects.

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public class DeviceCheck {
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    public static extern uint QueryDosDevice(string lpDeviceName, StringBuilder lpTargetPath, int ucchMax);

    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    public static extern IntPtr CreateFile(string lpFileName, uint dwDesiredAccess, uint dwShareMode,
        IntPtr lpSecurityAttributes, uint dwCreationDisposition, uint dwFlagsAndAttributes, IntPtr hTemplateFile);

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool CloseHandle(IntPtr hObject);
}
'@

$INVALID_HANDLE = [IntPtr]::new(-1)
$OPEN_EXISTING  = [uint32]3
$FILE_ATTR_NORMAL = [uint32]0x80

function Get-Win32Error {
    $code = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
    $name = switch ($code) {
        2   { 'ERROR_FILE_NOT_FOUND' }
        3   { 'ERROR_PATH_NOT_FOUND' }
        5   { 'ERROR_ACCESS_DENIED'  }
        21  { 'ERROR_NOT_READY'      }
        default { "WIN32_$code"      }
    }
    return [pscustomobject]@{ Code = $code; Name = $name }
}

Write-Host '=== Caller identity ==='
Write-Host "whoami: $env:USERDOMAIN\$env:USERNAME"
$wid = [System.Security.Principal.WindowsIdentity]::GetCurrent()
$wp  = [System.Security.Principal.WindowsPrincipal]::new($wid)
Write-Host ("IsAdmin (elevated): {0}" -f $wp.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator))
Write-Host ("IntegrityLevel: {0}" -f $wid.Owner)
Write-Host ''

Write-Host '=== QueryDosDevice("IntelAvbFilter") ==='
$sb = [System.Text.StringBuilder]::new(1024)
$r  = [DeviceCheck]::QueryDosDevice('IntelAvbFilter', $sb, 1024)
if ($r -gt 0) {
    Write-Host "SUCCESS -> $($sb.ToString())"
} else {
    $e = Get-Win32Error
    Write-Host "FAILED  -> code=$($e.Code) ($($e.Name))"
}

Write-Host ''
Write-Host '=== CreateFile access matrix ==='

$accessMap = [ordered]@{
    'access=0 (no-access probe)'   = [uint32]0x00000000
    'GENERIC_READ'                 = [uint32]0x80000000
    'GENERIC_WRITE'                = [uint32]0x40000000
    'GENERIC_READ|GENERIC_WRITE'   = [uint32]0xC0000000
}

foreach ($entry in $accessMap.GetEnumerator()) {
    $label  = $entry.Key
    $access = $entry.Value
    $h = [DeviceCheck]::CreateFile('\\.\IntelAvbFilter', $access, [uint32]0, [IntPtr]::Zero,
                                    $OPEN_EXISTING, $FILE_ATTR_NORMAL, [IntPtr]::Zero)
    if ($h -ne $INVALID_HANDLE) {
        Write-Host ("  {0,-38}: SUCCESS (handle=0x{1:X})" -f $label, $h.ToInt64())
        [DeviceCheck]::CloseHandle($h) | Out-Null
    } else {
        $e = Get-Win32Error
        Write-Host ("  {0,-38}: FAILED  code={1} ({2})" -f $label, $e.Code, $e.Name)
    }
}

Write-Host ''
Write-Host '=== CLASSIFICATION ==='
$sb2 = [System.Text.StringBuilder]::new(1024)
$qr  = [DeviceCheck]::QueryDosDevice('IntelAvbFilter', $sb2, 1024)
$h0  = [DeviceCheck]::CreateFile('\\.\IntelAvbFilter', [uint32]0, [uint32]0, [IntPtr]::Zero, $OPEN_EXISTING, $FILE_ATTR_NORMAL, [IntPtr]::Zero)
if ($qr -eq 0) {
    $e = Get-Win32Error
    Write-Host "VERDICT: DEVICE ABSENT (QueryDosDevice failed, code=$($e.Code)) -- potential #328 territory"
} elseif ($h0 -ne $INVALID_HANDLE) {
    [DeviceCheck]::CloseHandle($h0) | Out-Null
    Write-Host "VERDICT: DEVICE PRESENT and accessible (access=0 probe succeeded)"
} else {
    $e = Get-Win32Error
    if ($e.Code -eq 5) {
        Write-Host "VERDICT: DEVICE PRESENT but ACCESS DENIED (code=5) -- NOT #328; separate ACL/security issue"
    } else {
        Write-Host ("VERDICT: DEVICE PRESENT but open failed for other reason (code={0} / {1})" -f $e.Code, $e.Name)
    }
}
