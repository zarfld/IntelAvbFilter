# Force complete driver reload to load new binary with diagnostic logging
# This is required because sc.exe start/stop doesn't reload the driver binary

Write-Host "=== Force Driver Reload ===" -ForegroundColor Cyan
Write-Host ""

Write-Host "[1] Uninstalling old driver..." -ForegroundColor Yellow
netcfg.exe -u MS_IntelAvbFilter
Write-Host "Exit Code: $LASTEXITCODE"

Start-Sleep -Seconds 2

Write-Host ""
Write-Host "[2] Installing new driver..." -ForegroundColor Yellow
Set-Location "$PSScriptRoot\..\setup"
& ".\install_ndis_filter.bat"

Write-Host ""
Write-Host "[3] Verify driver is loaded and running..." -ForegroundColor Yellow
sc.exe query IntelAvbFilter

Write-Host ""
Write-Host "[4] Check driver device interface..." -ForegroundColor Yellow
$testExe = "..\..\avb_test_i226.exe"
if (Test-Path $testExe) {
    Write-Host "Running quick test..." -ForegroundColor Gray
    & $testExe
} else {
    Write-Host "Test executable not found: $testExe" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "=== Driver Reload Complete ===" -ForegroundColor Green
Write-Host "Check DebugView for driver messages" -ForegroundColor Cyan
Write-Host ""
