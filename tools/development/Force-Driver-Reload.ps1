# Force reload Intel AVB Filter Driver
# Handles the case where service won't stop cleanly

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Intel AVB Filter - Force Reload" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check admin
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "ERROR: Administrator privileges required" -ForegroundColor Red
    Write-Host "Right-click PowerShell and select 'Run as administrator'" -ForegroundColor Yellow
    Read-Host "Press Enter to exit"
    exit 1
}

# Step 1: Unbind filter from all adapters first (this allows clean stop)
Write-Host "[1] Unbinding filter from network adapters..." -ForegroundColor Yellow
try {
    $output = netcfg -u MS_IntelAvbFilter 2>&1
    Write-Host "    Filter unbound" -ForegroundColor Green
} catch {
    Write-Host "    Warning: Unbind may have failed (continuing...)" -ForegroundColor Yellow
}

Start-Sleep -Seconds 2

# Step 2: Force stop service
Write-Host "[2] Stopping service (forced)..." -ForegroundColor Yellow
$service = Get-Service -Name IntelAvbFilter -ErrorAction SilentlyContinue
if ($service) {
    if ($service.Status -eq 'Running') {
        # Try graceful stop first
        Stop-Service -Name IntelAvbFilter -Force -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 2
        
        # Check if still running
        $service = Get-Service -Name IntelAvbFilter -ErrorAction SilentlyContinue
        if ($service.Status -eq 'Running') {
            Write-Host "    Service still running, killing process..." -ForegroundColor Yellow
            # Find and kill the driver host process if needed
            # Note: Kernel drivers don't have a process to kill, but this might help with stuck states
            sc.exe stop IntelAvbFilter | Out-Null
            Start-Sleep -Seconds 3
        }
        Write-Host "    Service stopped" -ForegroundColor Green
    } else {
        Write-Host "    Service already stopped" -ForegroundColor Green
    }
}

# Step 3: Remove old installation
Write-Host "[3] Removing old driver files..." -ForegroundColor Yellow
try {
    # Remove from driver store
    $driverStore = Get-ChildItem "C:\Windows\System32\DriverStore\FileRepository" -Filter "intelavbfilter*" -Directory -ErrorAction SilentlyContinue
    foreach ($dir in $driverStore) {
        Write-Host "    Removing: $($dir.FullName)" -ForegroundColor Gray
        pnputil /delete-driver oem*.inf /uninstall /force 2>&1 | Out-Null
    }
    Write-Host "    Old drivers removed" -ForegroundColor Green
} catch {
    Write-Host "    Warning: Cleanup may be incomplete" -ForegroundColor Yellow
}

Start-Sleep -Seconds 2

# Step 4: Install new driver
Write-Host "[4] Installing new driver ($(Get-Date -Format 'HH:mm:ss'))..." -ForegroundColor Yellow
$infPath = "x64\Release\IntelAvbFilter\IntelAvbFilter.inf"
if (-not (Test-Path $infPath)) {
    Write-Host "ERROR: Driver not found at $infPath" -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}

$driverFile = Get-Item "x64\Release\IntelAvbFilter\IntelAvbFilter.sys"
Write-Host "    Driver: $($driverFile.LastWriteTime.ToString('MM/dd/yyyy HH:mm:ss')) ($($driverFile.Length) bytes)" -ForegroundColor Gray

try {
    $result = netcfg -v -l $infPath -c s -i MS_IntelAvbFilter 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "    SUCCESS: Driver installed and bound" -ForegroundColor Green
    } else {
        Write-Host "    Warning: netcfg returned code $LASTEXITCODE" -ForegroundColor Yellow
        Write-Host "    $result" -ForegroundColor Gray
    }
} catch {
    Write-Host "    ERROR: Installation failed: $_" -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}

Start-Sleep -Seconds 2

# Step 5: Verify service started
Write-Host "[5] Verifying service status..." -ForegroundColor Yellow
$service = Get-Service -Name IntelAvbFilter -ErrorAction SilentlyContinue
if ($service) {
    if ($service.Status -eq 'Running') {
        Write-Host "    ? Service RUNNING" -ForegroundColor Green
    } else {
        Write-Host "    ? Service status: $($service.Status)" -ForegroundColor Yellow
        Write-Host "    Attempting to start..." -ForegroundColor Yellow
        Start-Service -Name IntelAvbFilter -ErrorAction SilentlyContinue
        Start-Sleep -Seconds 2
        $service = Get-Service -Name IntelAvbFilter
        Write-Host "    ? Service now: $($service.Status)" -ForegroundColor $(if ($service.Status -eq 'Running') { 'Green' } else { 'Red' })
    }
} else {
    Write-Host "    ERROR: Service not found!" -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}

# Step 6: Quick test
Write-Host "[6] Testing device interface..." -ForegroundColor Yellow
if (Test-Path ".\avb_test_i226.exe") {
    $testOutput = .\avb_test_i226.exe 2>&1 | Select-Object -First 5
    Write-Host "    Test output:" -ForegroundColor Gray
    $testOutput | ForEach-Object { Write-Host "    $_" -ForegroundColor Gray }
    
    if ($testOutput -match "Capabilities") {
        Write-Host "    ? Device interface working!" -ForegroundColor Green
    } else {
        Write-Host "    ? Device interface may not be ready" -ForegroundColor Yellow
    }
} else {
    Write-Host "    (Test executable not found - skipping)" -ForegroundColor Gray
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Driver reload complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Check DebugView for initialization messages:" -ForegroundColor Yellow
Write-Host "  ? AvbPlatformInit: Starting PTP clock initialization" -ForegroundColor Gray
Write-Host "  ? TSAUXC before: 0x..." -ForegroundColor Gray
Write-Host "  ? PTP clock enabled" -ForegroundColor Gray
Write-Host "  ? TIMINCA programmed: 0x18000000" -ForegroundColor Gray
Write-Host "  ?? PTP clock is RUNNING!" -ForegroundColor Gray
Write-Host ""
Write-Host "Run full test:" -ForegroundColor Yellow
Write-Host "  .\avb_test_i226.exe selftest" -ForegroundColor Cyan
Write-Host ""
