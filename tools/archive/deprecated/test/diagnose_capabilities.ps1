# Diagnostic script to trace capability values through kernel to userspace
# Shows EXACT data flow with comprehensive logging

Write-Host "=== Capability Flow Diagnostic ===" -ForegroundColor Cyan
Write-Host ""

# Step 1: Stop driver to clear old state
Write-Host "[1] Stopping driver..." -ForegroundColor Yellow
sc.exe stop IntelAvbFilter | Out-Null
Start-Sleep -Seconds 1

# Step 2: Install new driver with diagnostic logging
Write-Host "[2] Installing driver with diagnostic logging..." -ForegroundColor Yellow
.\install_ndis_filter.bat | Out-Null

# Step 3: Check driver status
Write-Host "[3] Checking driver status..." -ForegroundColor Yellow
$status = sc.exe query IntelAvbFilter
if ($status -match "RUNNING") {
    Write-Host "    ✓ Driver is RUNNING" -ForegroundColor Green
} else {
    Write-Host "    ✗ Driver is NOT running" -ForegroundColor Red
    Write-Host $status
    exit 1
}

# Step 4: Run test and capture output
Write-Host ""
Write-Host "[4] Running ENUM_ADAPTERS test..." -ForegroundColor Yellow
Write-Host "    Check DebugView for diagnostic messages starting with '!!! DIAG:'" -ForegroundColor Cyan
Write-Host ""

# Run the test
.\comprehensive_ioctl_test.exe

Write-Host ""
Write-Host "=== IMPORTANT: Check DebugView Output ===" -ForegroundColor Cyan
Write-Host "Look for these diagnostic messages:" -ForegroundColor Yellow
Write-Host "  !!! DIAG: AvbCreateMinimalContext - Shows initial device_type and capabilities" -ForegroundColor White
Write-Host "  !!! DIAG: AvbBringUpHardware - Shows baseline_caps calculation and assignment" -ForegroundColor White  
Write-Host "  !!! DIAG: ENUM_ADAPTERS - Shows what's read from context and written to response" -ForegroundColor White
Write-Host ""
Write-Host "This will reveal WHERE capabilities get lost: context creation, assignment, or IOCTL transfer" -ForegroundColor Yellow
