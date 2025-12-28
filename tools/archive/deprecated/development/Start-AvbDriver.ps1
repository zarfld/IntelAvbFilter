# Start Intel AVB Filter Driver and Test
# Run as Administrator

#Requires -RunAsAdministrator

Write-Host "=== Starting Intel AVB Filter Driver ===" -ForegroundColor Cyan
Write-Host ""

# Try to start the service
Write-Host "Attempting to start IntelAvbFilter service..." -ForegroundColor Yellow
$result = sc.exe start IntelAvbFilter 2>&1
$exitCode = $LASTEXITCODE

if ($exitCode -eq 0) {
    Write-Host "? Service started successfully" -ForegroundColor Green
} elseif ($exitCode -eq 1056) {
    Write-Host "? Service is already running" -ForegroundColor Green
} elseif ($exitCode -eq 1058) {
    Write-Host "? Service is disabled - enabling it..." -ForegroundColor Yellow
    sc.exe config IntelAvbFilter start= demand
    $result = sc.exe start IntelAvbFilter 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "? Service started successfully" -ForegroundColor Green
    } else {
        Write-Host "? Service start failed with code: $LASTEXITCODE" -ForegroundColor Red
        Write-Host $result
    }
} else {
    Write-Host "? Service start returned code: $exitCode" -ForegroundColor Yellow
    Write-Host $result
    Write-Host ""
    Write-Host "This may be normal for NDIS filters. Trying alternative activation..." -ForegroundColor Cyan
}

# Wait a moment for device to initialize
Start-Sleep -Milliseconds 500

# Check service status
Write-Host ""
Write-Host "Checking service status..." -ForegroundColor Yellow
sc.exe query IntelAvbFilter

# Try to trigger filter attachment by cycling a network adapter
Write-Host ""
Write-Host "Triggering filter activation via network adapter..." -ForegroundColor Yellow
$adapter = Get-NetAdapter | Where-Object { $_.InterfaceDescription -like "*Intel*I226*" -and $_.Status -eq "Up" } | Select-Object -First 1

if ($adapter) {
    Write-Host "Found active adapter: $($adapter.Name) - $($adapter.InterfaceDescription)" -ForegroundColor Cyan
    Write-Host "Restarting adapter to trigger filter attachment..." -ForegroundColor Yellow
    
    try {
        Restart-NetAdapter -Name $adapter.Name -Confirm:$false
        Write-Host "? Adapter restarted" -ForegroundColor Green
        Start-Sleep -Seconds 2
    } catch {
        Write-Host "? Could not restart adapter: $_" -ForegroundColor Yellow
    }
} else {
    Write-Host "? No active Intel I226 adapter found" -ForegroundColor Yellow
}

# Test device interface
Write-Host ""
Write-Host "Testing device interface..." -ForegroundColor Yellow
if (Test-Path ".\avb_test_i210_um.exe") {
    Write-Host "Running test application..." -ForegroundColor Cyan
    & ".\avb_test_i210_um.exe" caps
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host ""
        Write-Host "? Driver is working! Running full selftest..." -ForegroundColor Green
        & ".\avb_test_i210_um.exe" selftest
    } else {
        Write-Host ""
        Write-Host "? Device interface not accessible" -ForegroundColor Red
        Write-Host "This usually means:" -ForegroundColor Yellow
        Write-Host "1. Driver hasn't loaded yet - try rebooting" -ForegroundColor Cyan
        Write-Host "2. Or run Event Viewer to check for driver errors:" -ForegroundColor Cyan
        Write-Host "   eventvwr -> Windows Logs -> System" -ForegroundColor Cyan
    }
} else {
    Write-Host "? Test application not found. Build it first:" -ForegroundColor Yellow
    Write-Host "   .\tools\vs_compile.ps1 -BuildCmd 'cl /nologo /W4 /Zi /I include /I external/intel_avb/lib /I . tools/avb_test/avb_test_um.c /Fe:avb_test_i210_um.exe'" -ForegroundColor Cyan
}

Write-Host ""
Write-Host "=== Done ===" -ForegroundColor Cyan
