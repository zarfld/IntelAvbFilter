# Test-DriverPreFlight.ps1
# Verifies IntelAvbFilter service is Running, the device node is accessible,
# and verifies the installed driver SHA256 against an authorized identity.
#
# Driver identity contract (see docs for full details):
#   HIL_EXPECTED_DRIVER_SHA256 = SHA256 of the AUTHORIZED preinstalled driver artifact.
#   This is typically a runner secret pointing to a known-good deployed build.
#   It is NOT the same as BUILD_DRIVER_SHA256 (the current commit build) unless the
#   current build has been explicitly deployed via the authorized deployment process.
#
#   Mismatch between installed SHA and current-commit BUILD_DRIVER_SHA256 is LOGGED
#   (as a warning showing test coverage gap) but does NOT fail the gate unless
#   HIL_REQUIRE_DEPLOYED_SHA=1 (set only in the dedicated deployment-verify lane).
#
#   This prevents: tests labeled as validating commit X when they actually ran
#   against a driver built from a different commit.
#
# HIL_REQUIRED_SHA256_GATE=1: fail if HIL_EXPECTED_DRIVER_SHA256 is absent/empty.
# Exit 0 = driver healthy; identity reported to GITHUB_ENV.
# Exit 1 = driver down, device-node loss, or SHA256 gate failure.

$ErrorActionPreference = "Stop"

function Write-Ok   { param([string]$m) Write-Host "[OK]  $m" -ForegroundColor Green }
function Write-Warn { param([string]$m) Write-Host "[WARN] $m" -ForegroundColor Yellow }
function Write-Err  { param([string]$m) Write-Host "[ERROR] $m" -ForegroundColor Red }

Write-Host ""
Write-Host "=== IntelAvbFilter HIL Pre-Flight Check ===" -ForegroundColor Cyan

# --- 0. Driver identity (version + SHA256 + checkout SHA) ---
Write-Host ""
Write-Host "--- Driver / build identity ---"
$drvPath = "$env:SystemRoot\System32\drivers\IntelAvbFilter.sys"
if (Test-Path $drvPath) {
    $vi  = (Get-Item $drvPath).VersionInfo
    $ver = "$($vi.FileMajorPart).$($vi.FileMinorPart).$($vi.FileBuildPart).$($vi.FilePrivatePart)"
    $ts  = (Get-Item $drvPath).LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss")
    $actualSha256 = (Get-FileHash $drvPath -Algorithm SHA256).Hash
    Write-Ok "IntelAvbFilter.sys version  : $ver  (modified: $ts)"
    Write-Ok "IntelAvbFilter.sys SHA256   : $actualSha256"

    # If the current commit produced a build artifact, compare its SHA to the installed one.
    # A mismatch is expected on shared runners where the preinstalled driver is not rebuilt
    # per-commit. It is LOGGED for traceability but does NOT fail the gate here.
    # (Contract A: preinstalled driver, mismatch is informational)
    $buildSha256 = $env:BUILD_DRIVER_SHA256
    if ($buildSha256) {
        if ($actualSha256.ToLower() -eq $buildSha256.ToLower()) {
            Write-Ok "Build SHA256 match         : installed driver matches current commit build"
        } else {
            Write-Warn ("BUILD SHA256 MISMATCH: installed={0}  current-build={1}" -f $actualSha256, $buildSha256)
            Write-Warn "  Tests run against the preinstalled driver, NOT the current commit build."
            Write-Warn "  Results are valid only for the INSTALLED driver version."
            Write-Warn "  To test the current commit: deploy it via the authorized deployment lane."
            if ($env:GITHUB_ACTIONS -eq 'true') {
                "HIL_BUILD_DRIVER_SHA256=$buildSha256" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
                "HIL_DRIVER_MISMATCH=true"             | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
            }
            # Contract B: HIL_REQUIRE_DEPLOYED_SHA=1 makes this a hard failure
            if ($env:HIL_REQUIRE_DEPLOYED_SHA -eq '1') {
                Write-Err "HIL_REQUIRE_DEPLOYED_SHA=1: installed driver must match current commit build"
                Write-Host "  [INFRA_FAILURE] Installed driver SHA does not match current commit." -ForegroundColor Red
                exit 1
            }
        }
    }

    # HIL_EXPECTED_DRIVER_SHA256: authorization check against the preinstalled artifact identity.
    $expectedSha256   = $env:HIL_EXPECTED_DRIVER_SHA256
    $sha256GateStrict = ($env:HIL_REQUIRED_SHA256_GATE -eq '1')

    if ($expectedSha256) {
        if ($actualSha256.ToLower() -eq $expectedSha256.ToLower()) {
            Write-Ok "Authorized SHA256 match    : installed driver is the authorized artifact"
        } else {
            Write-Err ("UNAUTHORIZED DRIVER: installed={0}  authorized={1}" -f $actualSha256, $expectedSha256)
            Write-Host "  [INFRA_FAILURE] TEST-PROV-SHA256: installed driver is not the authorized artifact" -ForegroundColor Red
            Write-Host "  Deploy the authorized artifact or update HIL_EXPECTED_DRIVER_SHA256." -ForegroundColor Yellow
            exit 1
        }
    } elseif ($sha256GateStrict) {
        Write-Err "HIL_EXPECTED_DRIVER_SHA256 is empty but HIL_REQUIRED_SHA256_GATE=1"
        Write-Host "  [INFRA_FAILURE] TEST-PROV-SHA256: SHA256 identity gate required but no expected hash provided" -ForegroundColor Red
        Write-Host "  Set HIL_EXPECTED_DRIVER_SHA256 (runner secret) to the SHA256 of the authorized preinstalled driver." -ForegroundColor Yellow
        exit 1
    } else {
        Write-Warn "HIL_EXPECTED_DRIVER_SHA256 not set - preinstalled identity not verified (set HIL_REQUIRED_SHA256_GATE=1 to enforce)"
    }

    if ($env:GITHUB_ACTIONS -eq 'true') {
        "HIL_DRIVER_VERSION=$ver"         | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
        "HIL_DRIVER_DATE=$ts"             | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
        "HIL_DRIVER_SHA256=$actualSha256" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8
    }
} else {
    Write-Err "IntelAvbFilter.sys not found at $drvPath -- driver not installed"
    exit 1
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
    Write-Host "  [INFRA_FAILURE] Service not present." -ForegroundColor Red
    exit 1
}

$svcStatus = $svc.Status
Write-Host "IntelAvbFilter service status: $svcStatus"

if ($svcStatus -eq 'StopPending') {
    Write-Err "IntelAvbFilter is STOP_PENDING -- reboot the runner machine to recover."
    Write-Host "  [INFRA_FAILURE] Service stuck in STOP_PENDING; original state preserved." -ForegroundColor Red
    exit 1
}

if ($svcStatus -ne 'Running') {
    # Record original state; do NOT attempt recovery here.
    # Service not Running is an infrastructure problem, not a transient condition.
    # Recovery belongs in a separately authorized workflow; masking failures here
    # would allow a broken environment to silently PASS the preflight check.
    Write-Err ("IntelAvbFilter service is '{0}' -- NOT Running." -f $svcStatus)
    Write-Host "  [INFRA_FAILURE] Service state preserved as '$svcStatus' for diagnosis." -ForegroundColor Red
    Write-Host "  Do not auto-restart in preflight; use the recovery workflow." -ForegroundColor Yellow
    exit 1
}

Write-Ok "IntelAvbFilter service is Running."

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
    $desc = switch ($err) {
        2   { "ERROR_FILE_NOT_FOUND (2): device node does not exist" }
        5   { "ERROR_ACCESS_DENIED (5): process is not elevated; runner must run as Administrator" }
        default { "Win32 error $err" }
    }
    Write-Err ("Cannot open \\.\IntelAvbFilter: {0} -- #328 sentinel" -f $desc)
    Write-Host "  [INFRA_FAILURE] Device node inaccessible." -ForegroundColor Red
    exit 1
} else {
    [AvbPreFlight]::CloseHandle($h) | Out-Null
    Write-Ok "IntelAvbFilter service Running and device node accessible."
}

Write-Host ""
Write-Ok "Pre-flight passed on runner: $($env:RUNNER_NAME)"
exit 0
