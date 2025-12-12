# Force complete driver reload to load new binary with diagnostic logging
# This is required because sc.exe start/stop doesn't reload the driver binary

Write-Host "=== Force Driver Reload ===" -ForegroundColor Cyan
Write-Host ""

Write-Host "[1] Uninstalling old driver..." -ForegroundColor Yellow
netcfg.exe -u MS_IntelAvbFilter

Start-Sleep -Seconds 2

Write-Host "[2] Installing new driver with diagnostic logging..." -ForegroundColor Yellow
.\install_ndis_filter.bat
Write-Host ""
Write-Host "    start driver with diagnostic logging..." -ForegroundColor Yellow
sc.exe start IntelAvbFilter

Write-Host ""
Write-Host "[3] Verify driver is loaded and running..." -ForegroundColor Yellow
$status = sc.exe query IntelAvbFilter
Write-Host $status

Write-Host ""
Write-Host "=== Driver Reload Complete ===" -ForegroundColor Green
Write-Host "Now run: .\comprehensive_ioctl_test.exe" -ForegroundColor Yellow
.\comprehensive_ioctl_test.exe

Write-Host "And check DebugView for '!!! DIAG:' messages" -ForegroundColor Cyan
Write-Host ""
