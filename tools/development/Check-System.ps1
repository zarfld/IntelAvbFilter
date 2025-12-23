Write-Host "=== Intel AVB Driver Debug Analysis ===" -ForegroundColor Cyan
Write-Host ""

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
Write-Host "[2] Local Release Build:" -ForegroundColor Yellow
$localBuild = Get-Item "x64\Release\IntelAvbFilter\IntelAvbFilter.sys" -ErrorAction SilentlyContinue
if ($localBuild) {
    Write-Host "  Size: $($localBuild.Length) bytes"
    Write-Host "  Modified: $($localBuild.LastWriteTime)"
    Write-Host "  Build Time: $(($localBuild.LastWriteTime).ToString('HH:mm:ss'))"
} else {
    Write-Host "  ERROR: Local build not found" -ForegroundColor Red
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
$testResult = & ".\avb_test_i226.exe" 2>&1 | Select-String -Pattern "Open.*failed|Capabilities"
if ($testResult -match "Capabilities") {
    Write-Host "  [OK] Device interface working" -ForegroundColor Green
    Write-Host "  $testResult"
} else {
    Write-Host "  [!] Device interface failed" -ForegroundColor Red
    Write-Host "  $testResult"
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

# Determine the issue
if ($driverFiles -and $localBuild -and $latest.LastWriteTime -lt $localBuild.LastWriteTime) {
    Write-Host ""
    Write-Host "ACTION REQUIRED: Your installed driver is outdated!" -ForegroundColor Red
    Write-Host "1. Run: .\Smart-Update-Driver.bat (as Administrator)"
    Write-Host "2. This will handle the stuck service and install the new driver"
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
