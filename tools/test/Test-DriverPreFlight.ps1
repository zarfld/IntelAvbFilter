# Test-DriverPreFlight.ps1
# Verifies IntelAvbFilter service is Running and the device node is accessible.
# Reports driver SYS version and current checkout SHA for build-SHA traceability.
# Called by CI before each hardware-dependent test suite to surface driver loss
# as a single clear failure instead of cascading into every test in the suite.
#
# Exit 0 = driver healthy; version/SHA reported to GITHUB_ENV.
# Exit 1 = driver down; caller should abort hardware tests and reboot runner.

$ErrorActionPreference = 'Stop'

function Write-Ok   { param([string]$m) Write-Host "[OK]  $m" -ForegroundColor Green }
function Write-Warn { param([string]$m) Write-Host "[WARN] $m" -ForegroundColor Yellow }
function Write-Err  { param([string]$m) Write-Host "[ERROR] $m" -ForegroundColor Red }

Write-Host ""
Write-Host "=== IntelAvbFilter HIL Pre-Flight Check ===" -ForegroundColor Cyan

# --- 0. Driver version + build SHA traceability ---
Write-Host ""
Write-Host "--- Driver / build identity ---"
$drvPath = "$env:SystemRoot\System32\drivers\IntelAvbFilter.sys"
if (Test-Path $drvPath) {
    $vi  = (Get-Item $drvPath).VersionInfo
    $ver = "$($vi.FileMajorPart).$($vi.FileMinorPart).$($vi.FileBuildPart).$($vi.FilePrivatePart)"
    $ts  = (Get-Item $drvPath).LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss")
    Write-Ok "IntelAvbFilter.sys version : $ver  (modified: $ts)"
    if ($env:GITHUB_ACTIONS -eq 'true') {
        "HIL_DRIVER_VERSION=$ver" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
        "HIL_DRIVER_DATE=$ts"     | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
    }
} else {
    Write-Warn "IntelAvbFilter.sys not found at $drvPath"
}

try {
    $sha = & git -C $PSScriptRoot rev-parse HEAD 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Ok "Checkout SHA               : $sha"
        if ($env:GITHUB_ACTIONS -eq 'true') {
            "HIL_CHECKOUT_SHA=$sha" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
        }
    }
} catch {}

if ($env:RUNNER_NAME) { Write-Ok "Runner                     : $env:RUNNER_NAME" }

# NIC type label for per-adapter evidence matrix
$nicLabel = $env:HIL_NIC_TYPE
if (-not $nicLabel) {
    $nicLabel = $env:RUNNER_LABELS -split ',' |
                Where-Object { $_.Trim() -match '^i[0-9]' } |
                Select-Object -First 1
}
if ($nicLabel) {
    Write-Ok "NIC type label             : $nicLabel"
    if ($env:GITHUB_ACTIONS -eq 'true') {
        "HIL_NIC_TYPE=$nicLabel" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
    }
} else {
    Write-Warn "NIC type label not set - set HIL_NIC_TYPE env var or add runner label (i217/i219/i210/i225/i226)"
}

# --- 1. Service check ---
Write-Host ""
Write-Host "--- Service status ---"
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
    // Constants defined in C# to avoid PowerShell 5.1 hex-literal overflow
    public const uint GENERIC_READ_WRITE    = 0xC0000000u;
    public const uint OPEN_EXISTING        = 3u;
    public const uint FILE_ATTRIBUTE_NORMAL = 0x80u;
}
'@ -ErrorAction SilentlyContinue

$h = [AvbPreFlight]::CreateFile(
    '\\.\IntelAvbFilter',
    [AvbPreFlight]::GENERIC_READ_WRITE,
    [uint32]3, [IntPtr]::Zero,    # FILE_SHARE_READ|WRITE (3) for concurrent access
    [AvbPreFlight]::OPEN_EXISTING,
    [AvbPreFlight]::FILE_ATTRIBUTE_NORMAL,
    [IntPtr]::Zero)

if ($h -eq [AvbPreFlight]::INVALID_HANDLE_VALUE) {
    $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
    if ($err -eq 5) {
        # ACCESS_DENIED with a Running service means the runner process is not elevated
        # enough to open the device node directly. The service check above already caught
        # STOP_PENDING, so this is likely a privilege gap â€” warn and let tests determine
        # actual accessibility rather than pre-emptively blocking the suite.
        Write-Warn "Cannot open device node (Win32 error=5 ACCESS_DENIED) -- runner may not be fully elevated."
        Write-Host "  Hardware tests will still run; Run-Tests-CI.ps1 health checks will catch driver loss mid-suite." -ForegroundColor Yellow
    } else {
        Write-Err "Cannot open \\.\IntelAvbFilter (Win32 error=$err) -- #328 sentinel: device node inaccessible."
        exit 1
    }
} else {
    [AvbPreFlight]::CloseHandle($h) | Out-Null
    Write-Ok "IntelAvbFilter service Running and device node accessible."
}

Write-Host ""
Write-Ok "Pre-flight passed on runner: $($env:RUNNER_NAME)"
exit 0
