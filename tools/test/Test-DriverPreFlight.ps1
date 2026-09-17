# Test-DriverPreFlight.ps1
# Verifies IntelAvbFilter service is Running and the device node is accessible.
# Called by CI before each hardware-dependent test suite to surface driver loss
# as a single clear failure instead of cascading into every test in the suite.
#
# Exit 0 = driver healthy.
# Exit 1 = driver down; caller should abort hardware tests and reboot runner.

$ErrorActionPreference = 'Stop'

function Write-Ok   { param([string]$m) Write-Host "[OK]  $m" -ForegroundColor Green }
function Write-Warn { param([string]$m) Write-Host "[WARN] $m" -ForegroundColor Yellow }
function Write-Err  { param([string]$m) Write-Host "[ERROR] $m" -ForegroundColor Red }

# --- 1. Service check ---
$svc = Get-Service -Name 'IntelAvbFilter' -ErrorAction SilentlyContinue
if ($null -eq $svc) {
    Write-Err "IntelAvbFilter service not found -- driver not installed on this runner."
    Write-Host "  Install the driver manually on the self-hosted machine before enabling hardware CI jobs." -ForegroundColor Yellow
    exit 1
}

Write-Host "IntelAvbFilter service status: $($svc.Status)"

if ($svc.Status -eq 'StopPending') {
    Write-Err "IntelAvbFilter is stuck in STOP_PENDING -- reboot the runner machine to recover."
    exit 1
}

if ($svc.Status -ne 'Running') {
    Write-Warn "IntelAvbFilter is $($svc.Status) -- attempting Start-Service..."
    try {
        Start-Service -Name 'IntelAvbFilter' -ErrorAction Stop
        Start-Sleep -Seconds 8
        $svc.Refresh()
        if ($svc.Status -ne 'Running') {
            Write-Err "IntelAvbFilter failed to reach Running state (status: $($svc.Status))."
            exit 1
        }
        Write-Ok "IntelAvbFilter restarted successfully."
    } catch {
        Write-Err "Start-Service IntelAvbFilter failed: $_"
        exit 1
    }
}

# --- 2. Device node accessibility check ---
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class AvbPreFlight {
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    public static extern IntPtr CreateFile(
        string lpFileName, uint dwDesiredAccess, uint dwShareMode,
        IntPtr lpSecurityAttributes, uint dwCreationDisposition,
        uint dwFlagsAndAttributes, IntPtr hTemplateFile);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool CloseHandle(IntPtr hObject);
    public static readonly IntPtr INVALID_HANDLE_VALUE = new IntPtr(-1);
}
'@ -ErrorAction SilentlyContinue

$h = [AvbPreFlight]::CreateFile(
    '\\.\IntelAvbFilter',
    [uint32]0xC0000000,   # GENERIC_READ | GENERIC_WRITE
    [uint32]0, [IntPtr]::Zero,
    [uint32]3,            # OPEN_EXISTING
    [uint32]0x80,         # FILE_ATTRIBUTE_NORMAL
    [IntPtr]::Zero)

if ($h -eq [AvbPreFlight]::INVALID_HANDLE_VALUE) {
    $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
    Write-Err "Cannot open \\.\IntelAvbFilter (Win32 error=$err)."
    if ($err -eq 5) {
        Write-Host "  error=5 (ACCESS_DENIED): runner process may not be elevated." -ForegroundColor Yellow
        Write-Host "  Ensure the self-hosted runner service runs as a member of the Administrators group." -ForegroundColor Yellow
    }
    exit 1
}

[AvbPreFlight]::CloseHandle($h) | Out-Null
Write-Ok "IntelAvbFilter service Running and device node accessible."
exit 0
