<#
.SYNOPSIS
    Intel AVB Filter Driver - Precondition Checker

.DESCRIPTION
    Verifies that all required preconditions are met before installing or testing
    the Intel AVB Filter Driver.

    Checks:
      - Administrator privileges (always required)
      - Test signing mode (bcdedit)
      - Secure Boot status (blocks test-signed drivers when enabled)
      - WDK installation (required for build; informational for run)
      - Visual Studio / VC++ toolset installation
      - Driver service installed and running
      - Intel Ethernet hardware present

.PARAMETER ForInstall
    Apply checks required before driver installation.
    Critical: Admin, TestSigning.
    Warning:  SecureBoot, Wdk.

.PARAMETER ForBuild
    Apply checks required before building the driver or tests.
    Critical: Admin, Wdk, VS.

.PARAMETER ForTest
    Apply checks required before running tests.
    Critical: Admin, TestSigning, DriverInstalled, IntelHardware.
    Warning:  SecureBoot.

.PARAMETER RequireTestSigning
    Fail (exit 1) if test signing mode is not enabled.

.PARAMETER RequireDriver
    Fail (exit 1) if the IntelAvbFilter service is not found/running.

.PARAMETER RequireWdk
    Fail (exit 1) if WDK is not installed.

.PARAMETER RequireVS
    Fail (exit 1) if Visual Studio VC++ toolset is not installed.

.PARAMETER RequireIntelHardware
    Fail (exit 1) if no Intel Ethernet adapter is found.

.PARAMETER WarnOnSecureBoot
    Emit a warning (but do not fail) if Secure Boot is enabled.

.PARAMETER Quiet
    Suppress informational output; print only failures and warnings.

.EXAMPLE
    .\Test-Preconditions.ps1 -ForInstall
    Run install-mode precondition checks.

.EXAMPLE
    .\Test-Preconditions.ps1 -ForTest
    Run test-mode precondition checks (gates test suite execution).

.EXAMPLE
    .\Test-Preconditions.ps1 -ForBuild
    Run build-mode precondition checks.

.OUTPUTS
    Exit code 0  — all checks passed
    Exit code 1  — one or more CRITICAL checks failed
    Exit code 2  — only WARNING checks failed (non-blocking)

.PARAMETER CheckSetup
    Non-blocking full diagnostic: reports status of ALL checks without failing.
    Use this to see what is and is not installed on a new machine.
    Always exits 0 (pass) or 2 (warnings/gaps found).

.EXAMPLE
    .\Test-Preconditions.ps1 -CheckSetup
    Show full environment status without blocking anything.

.NOTES
    Implements: REQ-NF-SCRIPTS-001 (#27)
    Called by:  Install-Driver.ps1, Run-Tests.ps1
#>

[CmdletBinding(DefaultParameterSetName = 'Explicit')]
param(
    # Convenience presets
    [Parameter(ParameterSetName='ForInstall', Mandatory=$true)]
    [switch]$ForInstall,

    [Parameter(ParameterSetName='ForBuild', Mandatory=$true)]
    [switch]$ForBuild,

    [Parameter(ParameterSetName='ForTest', Mandatory=$true)]
    [switch]$ForTest,

    [Parameter(ParameterSetName='CheckSetup', Mandatory=$true)]
    [switch]$CheckSetup,

    # Fine-grained controls (ParameterSetName='Explicit')
    [Parameter(ParameterSetName='Explicit')]
    [switch]$RequireTestSigning,

    [Parameter(ParameterSetName='Explicit')]
    [switch]$RequireDriver,

    [Parameter(ParameterSetName='Explicit')]
    [switch]$RequireWdk,

    [Parameter(ParameterSetName='Explicit')]
    [switch]$RequireVS,

    [Parameter(ParameterSetName='Explicit')]
    [switch]$RequireIntelHardware,

    # Available in all sets
    [switch]$WarnOnSecureBoot,

    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'

# ───────────────────────────────────────────────────
# Apply presets
# ───────────────────────────────────────────────────
if ($ForInstall) {
    $RequireTestSigning  = $true
    $WarnOnSecureBoot    = $true
    $RequireWdk          = $false   # build already done before install
    $RequireVS           = $false
    $RequireDriver       = $false   # we ARE about to install
    $RequireIntelHardware = $false
}

if ($ForBuild) {
    $RequireTestSigning  = $false
    $WarnOnSecureBoot    = $false
    $RequireWdk          = $true
    $RequireVS           = $true
    $RequireDriver       = $false
    $RequireIntelHardware = $false
}

if ($CheckSetup) {
    # Non-blocking: check everything, report status, never exit 1
    $RequireTestSigning  = $false
    $WarnOnSecureBoot    = $true
    $RequireWdk          = $false
    $RequireVS           = $false
    $RequireDriver       = $false
    $RequireIntelHardware = $false
}

if ($ForTest) {
    $RequireTestSigning  = $true
    $WarnOnSecureBoot    = $true
    $RequireDriver       = $true
    $RequireIntelHardware = $true
    $RequireWdk          = $false
    $RequireVS           = $false
}

# ───────────────────────────────────────────────────
# Output helpers
# ───────────────────────────────────────────────────
$checkOnly = $CheckSetup.IsPresent   # never block; downgrade FAIL -> MISS

function Write-Pass   { param([string]$m) if (-not $Quiet) { Write-Host "[PASS] $m" -ForegroundColor Green } }
function Write-Fail   { param([string]$m) Write-Host "[FAIL] $m" -ForegroundColor Red }
function Write-Miss   { param([string]$m) Write-Host "[MISS] $m" -ForegroundColor Yellow }  # not installed / not configured
function Write-Warn   { param([string]$m) Write-Host "[WARN] $m" -ForegroundColor Yellow }
function Write-Info   { param([string]$m) if (-not $Quiet) { Write-Host "[INFO] $m" -ForegroundColor Cyan } }

# In CheckSetup mode every "required" check is demoted: missing = [MISS] (warning) not [FAIL] (critical)
function Report-Missing {
    param([string]$Message)
    if ($checkOnly) {
        Write-Miss $Message
        $script:warnings++
    } else {
        Write-Fail $Message
        $script:criticalFailures++
    }
}

$criticalFailures = 0
$warnings         = 0

# ───────────────────────────────────────────────────
# CHECK 0 — PowerShell execution policy
# ───────────────────────────────────────────────────
$effectivePolicy = Get-ExecutionPolicy -Scope Process
if ($effectivePolicy -in @('Restricted', 'AllSigned')) {
    Write-Warn "Execution policy for this process is '$effectivePolicy'."
    Write-Warn "  Scripts in this repo require at least RemoteSigned or Bypass."
    Write-Warn "  To run directly: powershell -ExecutionPolicy Bypass -File $($MyInvocation.MyCommand.Path) $($PSBoundParameters.Keys | ForEach-Object { "-$_" })"
    Write-Warn "  To persist:      Set-ExecutionPolicy RemoteSigned -Scope CurrentUser"
    $warnings++
} else {
    Write-Pass "Execution policy: $effectivePolicy"
}

# ───────────────────────────────────────────────────
# CHECK 1 — Administrator privileges (always required)
# ───────────────────────────────────────────────────
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)

if ($isAdmin) {
    Write-Pass "Running as Administrator"
} else {
    if ($checkOnly) {
        Write-Miss "Not running as Administrator (required for install/test operations)."
        $warnings++
    } else {
        Write-Fail "Administrator privileges required. Re-run from an elevated PowerShell prompt."
        exit 1   # Hard stop
    }
}

# ───────────────────────────────────────────────────
# CHECK 2 — Test signing mode
# ───────────────────────────────────────────────────
try {
    $bcdedit = bcdedit /enum "{current}" 2>&1
    $testSigningOn = ($bcdedit -join "`n") -match 'testsigning\s+Yes'
} catch {
    $testSigningOn = $false
}

if ($testSigningOn) {
    Write-Pass "Test signing: ENABLED"
} elseif ($RequireTestSigning) {
    Report-Missing "Test signing is DISABLED. The test-signed driver will not load."
    Write-Info "  Fix: Run (as Administrator) -- bcdedit /set testsigning on -- then reboot."
    Write-Info "  Or:  .\tools\setup\Install-Driver.ps1 -EnableTestSigning"
} else {
    Write-Warn "Test signing is DISABLED (not required for this operation)."
    $warnings++
}

# ───────────────────────────────────────────────────
# CHECK 3 — Secure Boot
# ───────────────────────────────────────────────────
$secureBootEnabled = $false
try {
    $secureBootEnabled = Confirm-SecureBootUEFI -ErrorAction SilentlyContinue
} catch {
    # Cmdlet may throw on legacy BIOS or non-UEFI systems — treat as disabled
    $secureBootEnabled = $false
}

if ($secureBootEnabled) {
    if ($WarnOnSecureBoot) {
        Write-Warn "Secure Boot is ENABLED. Test-signed drivers are blocked by Secure Boot."
        Write-Warn "  Remediation options:"
        Write-Warn "    1. Disable Secure Boot in BIOS/UEFI (recommended for development)"
        Write-Warn "    2. Use an EV (Extended Validation) code-signing certificate"
        Write-Warn "    3. Test in a VM with Secure Boot disabled"
        $warnings++
    } else {
        Write-Info "Secure Boot: ENABLED (Secure Boot check not requested for this operation)"
    }
} else {
    Write-Pass "Secure Boot: disabled or not applicable"
}

# ───────────────────────────────────────────────────
# CHECK 4 — WDK installation
# ───────────────────────────────────────────────────
$wdkFound = $false
$wdkVersion = $null
try {
    # Check both native and WOW6432Node registry hives (WDK installer writes to either)
    $wdkRoot = $null
    foreach ($regPath in @(
        'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots'
    )) {
        try {
            $candidate = (Get-ItemProperty -Path $regPath -Name 'KitsRoot10' -ErrorAction Stop).KitsRoot10
            if ($candidate -and (Test-Path $candidate)) { $wdkRoot = $candidate; break }
        } catch { }
    }

    # Fallback: WDK installed as VS extension may omit KitsRoot10 — probe default dir
    if (-not $wdkRoot) {
        $defaultWdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
        if (Test-Path $defaultWdk) { $wdkRoot = $defaultWdk }
    }

    if ($wdkRoot) {
        # Determine WDK version from the highest Include subfolder
        $wdkIncludes = Get-ChildItem -Path (Join-Path $wdkRoot 'Include') -Directory -ErrorAction SilentlyContinue |
                       Sort-Object Name -Descending | Select-Object -First 1
        # Confirm this is actually a WDK (not just SDK) by checking for km/ subfolder
        $isWdk = $wdkIncludes -and (Test-Path (Join-Path $wdkIncludes.FullName 'km'))
        $wdkVersion = if ($wdkIncludes) { $wdkIncludes.Name } else { 'unknown' }
        $wdkFound = $true
        if (-not $isWdk) {
            Write-Warn "Windows Kits found at '$wdkRoot' but no km/ headers detected -- SDK only, WDK may not be installed."
            $warnings++
        }
    }
} catch {
    $wdkFound = $false
}

if ($wdkFound) {
    Write-Pass "WDK installed: $wdkRoot (version $wdkVersion)"
} elseif ($RequireWdk -or $checkOnly) {
    Report-Missing "WDK not found. Searched registry (both hives) and '$( Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10' )'."
    Write-Info "  Download: https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk"
    Write-Info "  Recommended: WDK 10.0.22621 (Windows 11 22H2)"
} else {
    Write-Info "WDK: not installed (not required for this operation)"
}

# ───────────────────────────────────────────────────
# CHECK 5 — Visual Studio / VC++ toolset
# ───────────────────────────────────────────────────
$vsFound = $false
$vsPath  = $null
try {
    # vswhere.exe ships with the VS Installer; try both 32-bit and 64-bit Program Files paths
    $vswhere = $null
    foreach ($candidate in @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
        (Join-Path $env:ProgramFiles        'Microsoft Visual Studio\Installer\vswhere.exe')
    )) {
        if (Test-Path $candidate) { $vswhere = $candidate; break }
    }

    if ($vswhere) {
        # Accept any VS or Build Tools installation that has the VC++ toolset
        $vsPath = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath 2>$null
        $vsFound = ($null -ne $vsPath -and $vsPath.Trim() -ne '')

        # If the VC++ component check fails, fall back to any VS installation
        if (-not $vsFound) {
            $vsPath = & $vswhere -latest -products * -property installationPath 2>$null
            if ($null -ne $vsPath -and $vsPath.Trim() -ne '') {
                Write-Warn "Visual Studio found at '$vsPath' but VC++ toolset (Microsoft.VisualStudio.Component.VC.Tools.x86.x64) is missing."
                Write-Info "  Install workload: 'Desktop development with C++'"
                $vsFound = $true   # VS present but incomplete; let downstream build fail with a clear error
                $warnings++
            }
        }
    }
} catch {
    $vsFound = $false
}

if ($vsFound) {
    Write-Pass "Visual Studio (VC++ toolset): $($vsPath.Trim())"
} elseif ($RequireVS -or $checkOnly) {
    Report-Missing "Visual Studio with VC++ toolset not found (vswhere.exe not detected)."
    Write-Info "  Install Visual Studio 2019 or 2022 with the 'Desktop development with C++' workload."
    Write-Info "  Minimum component: Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
} else {
    Write-Info "Visual Studio: not detected (not required for this operation)"
}

# ───────────────────────────────────────────────────
# CHECK 6 — Driver service installed and running
# ───────────────────────────────────────────────────
if ($RequireDriver) {
    $svc = Get-Service -Name 'IntelAvbFilter' -ErrorAction SilentlyContinue
    if ($null -eq $svc) {
        Report-Missing "IntelAvbFilter service not found - driver is not installed."
        Write-Info "  Fix: .\tools\setup\Install-Driver.ps1 -Configuration Debug -InstallDriver"
    } elseif ($svc.Status -ne 'Running') {
        Write-Warn "IntelAvbFilter service found but status is '$($svc.Status)' (expected Running)."
        Write-Info "  Fix: sc.exe start IntelAvbFilter"
        $warnings++
    } else {
        Write-Pass "Driver service: Running"
    }
} else {
    $svc = Get-Service -Name 'IntelAvbFilter' -ErrorAction SilentlyContinue
    if ($svc) {
        Write-Pass "Driver service: $($svc.Status)"
    } else {
        Write-Info "Driver service: not installed (informational)"
    }
}

# ───────────────────────────────────────────────────
# CHECK 7 — Intel Ethernet hardware present
# ───────────────────────────────────────────────────
if ($RequireIntelHardware) {
    $intelAdapters = @(Get-NetAdapter -ErrorAction SilentlyContinue |
        Where-Object { $_.InterfaceDescription -like '*Intel*' -and
                       $_.MediaType -eq '802.3' })
    if ($intelAdapters.Count -eq 0) {
        Report-Missing "No Intel Ethernet adapters found. Hardware tests cannot run."
        Write-Info "  Supported controllers: I210, I217, I219, I225, I226."
    } else {
        Write-Pass "Intel Ethernet adapters found: $($intelAdapters.Count)"
        foreach ($a in $intelAdapters) {
            Write-Info "  $($a.InterfaceDescription) - $($a.Status)"
        }
    }
} else {
    $intelAdapters = @(Get-NetAdapter -ErrorAction SilentlyContinue |
        Where-Object { $_.InterfaceDescription -like '*Intel*' -and
                       $_.MediaType -eq '802.3' })
    if ($intelAdapters.Count -gt 0) {
        Write-Pass "Intel Ethernet adapters found: $($intelAdapters.Count)"
    } else {
        Write-Info "Intel Ethernet adapters: none detected (informational)"
    }
}

# ───────────────────────────────────────────────────
# Summary
# ───────────────────────────────────────────────────
Write-Host ""

if ($checkOnly) {
    Write-Host "=== Development Environment Setup Checklist ==" -ForegroundColor Cyan
    Write-Host "  [PASS] = installed/configured" -ForegroundColor Green
    Write-Host "  [MISS] = not installed (action needed for that scenario)" -ForegroundColor Yellow
    Write-Host "  [WARN] = installed but needs attention" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  To build the driver:   WDK + Visual Studio required" -ForegroundColor Gray
    Write-Host "  To install the driver: test signing required + WDK/VS not needed" -ForegroundColor Gray
    Write-Host "  To run tests:          test signing + driver installed + Intel hardware" -ForegroundColor Gray
    Write-Host ""
    if ($warnings -eq 0) {
        Write-Host "[SETUP] Machine is fully ready for all operations." -ForegroundColor Green
    } else {
        Write-Host "[SETUP] $warnings item(s) need attention (see [MISS]/[WARN] above)." -ForegroundColor Yellow
    }
    exit $(if ($warnings -gt 0) { 2 } else { 0 })
}

if ($criticalFailures -eq 0 -and $warnings -eq 0) {
    Write-Host "[PRECONDITIONS] All checks passed." -ForegroundColor Green
    exit 0
} elseif ($criticalFailures -eq 0) {
    Write-Host "[PRECONDITIONS] Passed with $warnings warning(s). Review [WARN] items above." -ForegroundColor Yellow
    exit 2
} else {
    Write-Host "[PRECONDITIONS] FAILED - $criticalFailures critical issue(s), $warnings warning(s). Resolve [FAIL] items before proceeding." -ForegroundColor Red
    exit 1
}
