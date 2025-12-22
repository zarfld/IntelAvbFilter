# Complete Setup for Intel AVB Filter Driver
# Run as Administrator

#Requires -RunAsAdministrator

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "Intel AVB Filter Driver - Complete Setup" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host ""

# Step 1: Check test signing mode
Write-Host "[Step 1] Checking test signing mode..." -ForegroundColor Yellow
$testSigning = bcdedit /enum | Select-String "testsigning.*Yes"

if ($testSigning) {
    Write-Host "? Test signing is enabled" -ForegroundColor Green
} else {
    Write-Host "? Test signing is NOT enabled" -ForegroundColor Red
    Write-Host ""
    Write-Host "Your driver is self-signed for development." -ForegroundColor Yellow
    Write-Host "Windows requires test signing mode for development drivers." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "To enable test signing mode:" -ForegroundColor Cyan
    Write-Host "1. Run: bcdedit /set testsigning on" -ForegroundColor White
    Write-Host "2. Reboot your computer" -ForegroundColor White
    Write-Host "3. Re-run this script" -ForegroundColor White
    Write-Host ""
    $response = Read-Host "Do you want to enable test signing now? (y/n)"
    
    if ($response -eq 'y' -or $response -eq 'Y') {
        Write-Host ""
        Write-Host "Enabling test signing..." -ForegroundColor Yellow
        bcdedit /set testsigning on
        
        if ($LASTEXITCODE -eq 0) {
            Write-Host "? Test signing enabled" -ForegroundColor Green
            Write-Host ""
            Write-Host "IMPORTANT: You must REBOOT for this to take effect!" -ForegroundColor Red
            Write-Host "After reboot, run this script again." -ForegroundColor Yellow
            Write-Host ""
            $reboot = Read-Host "Reboot now? (y/n)"
            if ($reboot -eq 'y' -or $reboot -eq 'Y') {
                Restart-Computer -Confirm:$false
            }
            exit 0
        } else {
            Write-Host "? Failed to enable test signing" -ForegroundColor Red
            Write-Host "You may need to disable Secure Boot in BIOS first" -ForegroundColor Yellow
            exit 1
        }
    } else {
        Write-Host ""
        Write-Host "Cannot proceed without test signing mode." -ForegroundColor Red
        Write-Host "Alternative: Get an EV code signing certificate (~$300/year)" -ForegroundColor Yellow
        exit 1
    }
}

Write-Host ""

# Step 2: Install driver
Write-Host "[Step 2] Installing driver package..." -ForegroundColor Yellow
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$driverPath = Join-Path $repoRoot "x64\Debug\IntelAvbFilter"

if (-not (Test-Path "$driverPath\IntelAvbFilter.sys")) {
    Write-Host "? Driver not found at $driverPath" -ForegroundColor Red
    Write-Host "Build the driver first: msbuild /p:Configuration=Debug;Platform=x64" -ForegroundColor Yellow
    exit 1
}

pnputil /add-driver "$driverPath\IntelAvbFilter.inf" /install | Out-Host
Write-Host ""

# Step 3: Create/update service
Write-Host "[Step 3] Configuring service..." -ForegroundColor Yellow

$service = Get-Service -Name "IntelAvbFilter" -ErrorAction SilentlyContinue
if ($service) {
    Write-Host "Service exists, updating configuration..." -ForegroundColor Cyan
    sc.exe config IntelAvbFilter start= system | Out-Null
} else {
    Write-Host "Creating service..." -ForegroundColor Cyan
    sc.exe create IntelAvbFilter type= kernel start= system error= normal binpath= System32\drivers\IntelAvbFilter.sys DisplayName= "Intel AVB/TSN NDIS Filter Driver" | Out-Host
}

Write-Host ""

# Step 4: Start service
Write-Host "[Step 4] Starting service..." -ForegroundColor Yellow
$result = sc.exe start IntelAvbFilter 2>&1
$exitCode = $LASTEXITCODE

if ($exitCode -eq 0) {
    Write-Host "? Service started successfully!" -ForegroundColor Green
} elseif ($exitCode -eq 1056) {
    Write-Host "? Service is already running" -ForegroundColor Green
} else {
    Write-Host "? Service start failed with code: $exitCode" -ForegroundColor Yellow
    Write-Host $result
    Write-Host ""
    Write-Host "This is normal for NDIS filters - they load automatically when needed" -ForegroundColor Cyan
}

Write-Host ""
sc.exe query IntelAvbFilter | Out-Host
Write-Host ""

# Step 5: Test device interface
Write-Host "[Step 5] Testing device interface..." -ForegroundColor Yellow

Start-Sleep -Milliseconds 1000

$testExe = Join-Path $repoRoot "avb_test_i210_um.exe"
if (Test-Path $testExe) {
    Write-Host "Running quick test..." -ForegroundColor Cyan
    & $testExe caps
    
    if ($LASTEXITCODE -eq 0 -or $LASTEXITCODE -eq $null) {
        Write-Host ""
        Write-Host "=========================================" -ForegroundColor Green
        Write-Host "?  SETUP SUCCESSFUL!" -ForegroundColor Green
        Write-Host "=========================================" -ForegroundColor Green
        Write-Host ""
        Write-Host "Your driver is installed and working!" -ForegroundColor Green
        Write-Host ""
        Write-Host "Run full test suite:" -ForegroundColor Cyan
        Write-Host "  avb_test_i210_um.exe selftest" -ForegroundColor White
        Write-Host ""
        Write-Host "Available test commands:" -ForegroundColor Cyan
        Write-Host "  avb_test_i210_um.exe caps           - Show capabilities" -ForegroundColor White
        Write-Host "  avb_test_i210_um.exe info           - Show device info" -ForegroundColor White
        Write-Host "  avb_test_i210_um.exe snapshot       - Register snapshot" -ForegroundColor White
        Write-Host "  avb_test_i210_um.exe ptp-probe      - Test PTP timer" -ForegroundColor White
        Write-Host "  avb_test_i210_um.exe ptp-bringup    - Initialize PTP" -ForegroundColor White
    } else {
        Write-Host ""
        Write-Host "? Device interface still not accessible" -ForegroundColor Red
        Write-Host ""
        Write-Host "Troubleshooting:" -ForegroundColor Yellow
        Write-Host "1. Check Event Viewer: eventvwr -> Windows Logs -> System" -ForegroundColor Cyan
        Write-Host "2. Use DebugView to see kernel debug messages" -ForegroundColor Cyan
        Write-Host "3. Verify test signing watermark appears on desktop" -ForegroundColor Cyan
        Write-Host "4. Try rebooting the system" -ForegroundColor Cyan
    }
} else {
    Write-Host "? Test application not found" -ForegroundColor Yellow
    Write-Host "Build it with: tools\vs_compile.ps1" -ForegroundColor Cyan
}

Write-Host ""
