Write-Host "=== Intel AVB Driver Debug Analysis ===" -ForegroundColor Cyan
Write-Host ""

$ErrorActionPreference = 'Continue'  # Allow script to continue on errors for diagnostics

try {

    # Check which driver is currently loaded
    Write-Host "[1] Currently Installed Driver:" -ForegroundColor Yellow
    $driverFiles = Get-ChildItem "C:\Windows\System32\DriverStore\FileRepository\intelavbfilter*\IntelAvbFilter.sys" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending
if ($driverFiles) {
    $latest = $driverFiles[0]
    Write-Host "  Path: $($latest.FullName)"
    Write-Host "  Size: $($latest.Length) bytes"
    Write-Host "  Modified: $($latest.LastWriteTime)"
    Write-Host "  Build Time: $(($latest.LastWriteTime).ToString('HH:mm:ss'))"
} else {
    Write-Host "  ERROR: No driver found in DriverStore" -ForegroundColor Red
}

# Check our local build
Write-Host ""
Write-Host "[2] Local Build:" -ForegroundColor Yellow
# Try multiple possible build locations (Debug first, then Release)
$buildPaths = @(
    "build\x64\Debug\IntelAvbFilter\IntelAvbFilter\IntelAvbFilter.sys",
    "build\x64\Release\IntelAvbFilter\IntelAvbFilter\IntelAvbFilter.sys"
)
$localBuild = $null
$buildConfig = "Unknown"
foreach ($path in $buildPaths) {
    $localBuild = Get-Item $path -ErrorAction SilentlyContinue
    if ($localBuild) {
        if ($path -match "Debug") { $buildConfig = "Debug" }
        elseif ($path -match "Release") { $buildConfig = "Release" }
        break
    }
}
if ($localBuild) {
    Write-Host "  Configuration: $buildConfig"
    Write-Host "  Size: $($localBuild.Length) bytes"
    Write-Host "  Modified: $($localBuild.LastWriteTime)"
    Write-Host "  Build Time: $(($localBuild.LastWriteTime).ToString('HH:mm:ss'))"
} else {
    Write-Host "  WARNING: Local build not found (build driver first)" -ForegroundColor Yellow
}

# Compare
Write-Host ""
if ($driverFiles -and $localBuild) {
    if ($latest.LastWriteTime -lt $localBuild.LastWriteTime) {
        Write-Host "[!] WARNING: Installed driver is OLDER than local build!" -ForegroundColor Red
        Write-Host "    Installed: $($latest.LastWriteTime.ToString('HH:mm:ss'))"
        Write-Host "    Local:     $($localBuild.LastWriteTime.ToString('HH:mm:ss'))"
        Write-Host "    You need to reinstall the driver!"
    } elseif ($latest.LastWriteTime -eq $localBuild.LastWriteTime) {
        Write-Host "[OK] Installed driver matches local build" -ForegroundColor Green
    } else {
        Write-Host "[?] Installed driver is NEWER than local build" -ForegroundColor Yellow
    }
}

# Check service status
Write-Host ""
Write-Host "[3] Service Status:" -ForegroundColor Yellow
$service = Get-Service -Name "IntelAvbFilter" -ErrorAction SilentlyContinue
if ($service) {
    Write-Host "  Status: $($service.Status)"
    if ($service.Status -eq "Running") {
        Write-Host "  [OK] Driver is loaded" -ForegroundColor Green
    } else {
        Write-Host "  [!] Driver is NOT running" -ForegroundColor Red
    }
} else {
    Write-Host "  [!] Service not found" -ForegroundColor Red
}

# Test device interface
Write-Host ""
Write-Host "[4] Device Interface Test:" -ForegroundColor Yellow
# Check if running as administrator (required for device access)
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "  [SKIP] Not running as Administrator" -ForegroundColor Yellow
    Write-Host "         Device interface test requires admin rights" -ForegroundColor Gray
    Write-Host "         Rerun this script as Administrator to test device access" -ForegroundColor Gray
} else {
    # Try to find test executable in common locations
    $testPaths = @(
        "build\tools\avb_test\x64\Debug\avb_test_i226.exe",
        "build\tools\avb_test\x64\Release\avb_test_i226.exe",
        "tools\avb_test\x64\Debug\avb_test.exe",
        ".\avb_test_i226.exe"
    )
    $testExe = $null
    foreach ($path in $testPaths) {
        if (Test-Path $path) { $testExe = $path; break }
    }
    if ($testExe) {
        $testResult = & $testExe 2>&1
        if ($testResult -match "Capabilities") {
            Write-Host "  [OK] Device interface working" -ForegroundColor Green
            $testResult | Select-String -Pattern "Capabilities" | ForEach-Object { Write-Host "  $_" }
        } elseif ($testResult -match "Open.*failed.*5") {
            Write-Host "  [!] Access Denied (Error 5)" -ForegroundColor Red
            Write-Host "      This script must run as Administrator" -ForegroundColor Yellow
        } elseif ($testResult -match "Open.*failed") {
            Write-Host "  [!] Device interface test failed" -ForegroundColor Red
            $testResult | Select-String -Pattern "Open.*failed" | ForEach-Object { Write-Host "  $_" }
        } else {
            Write-Host "  [?] Test result unclear" -ForegroundColor Yellow
            Write-Host "  $($testResult | Select-Object -First 3)"
        }
    } else {
        Write-Host "  [SKIP] Test executable not found" -ForegroundColor Yellow
        Write-Host "         Build tests with: .\tools\build\Build-Tests.ps1 -TestName avb_test_i226" -ForegroundColor Gray
    }
}

# Check for DebugView
Write-Host ""
Write-Host "[5] Debug Output Monitoring:" -ForegroundColor Yellow
$dbgview = Get-Process -Name "Dbgview*" -ErrorAction SilentlyContinue
if ($dbgview) {
    Write-Host "  [OK] DebugView is running (PID: $($dbgview.Id))" -ForegroundColor Green
} else {
    Write-Host "  [!] DebugView is NOT running" -ForegroundColor Red
    Write-Host "  Without DebugView, you cannot see driver debug output!"
    Write-Host "  Download: https://learn.microsoft.com/en-us/sysinternals/downloads/debugview"
}

Write-Host ""
Write-Host "=== Analysis Summary ===" -ForegroundColor Cyan

# Check device test status
$deviceTestPassed = $false
if ($isAdmin -and $testExe -and $testResult) {
    $deviceTestPassed = $testResult -match "Capabilities"
}

# Determine the issue
if ($driverFiles -and $localBuild -and $latest.LastWriteTime -lt $localBuild.LastWriteTime) {
    Write-Host ""
    Write-Host "ACTION REQUIRED: Your installed driver is outdated!" -ForegroundColor Red
    Write-Host "1. Run: .\Smart-Update-Driver.bat (as Administrator)"
    Write-Host "2. This will handle the stuck service and install the new driver"
} elseif (-not $isAdmin) {
    Write-Host ""
    Write-Host "RECOMMENDATION: Rerun as Administrator for full diagnostics" -ForegroundColor Yellow
    Write-Host "Device interface test was skipped (requires admin rights)"
} elseif (-not $deviceTestPassed -and $testExe) {
    Write-Host ""
    Write-Host "ACTION REQUIRED: Device interface test failed!" -ForegroundColor Red
    Write-Host "Check that the driver is properly installed and bound to your network adapter"
} elseif (-not $dbgview) {
    Write-Host ""
    Write-Host "ACTION REQUIRED: Start DebugView to see what's happening!" -ForegroundColor Yellow
    Write-Host "1. Download DebugView from Sysinternals"
    Write-Host "2. Run as Administrator"
    Write-Host "3. Enable: Capture > Capture Kernel"
    Write-Host "4. Run test again: .\avb_test_i226.exe selftest"
    Write-Host "5. Look for messages about SYSTIM, TIMINCA, and PTP clock"
} else {
    Write-Host ""
    Write-Host "System appears configured correctly." -ForegroundColor Green
    Write-Host "Check DebugView output for PTP initialization messages"
}

Write-Host ""

} catch {
    Write-Host "ERROR: An error occurred during system check" -ForegroundColor Red
    Write-Host "  $_" -ForegroundColor Yellow
    exit 1
}
