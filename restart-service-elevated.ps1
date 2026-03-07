# Restart IntelAvbFilter service (requires elevation)
# Run as: Start-Process powershell -Verb RunAs -ArgumentList "-File .\restart-service-elevated.ps1"

Write-Host "==> Stopping IntelAvbFilter service..." -ForegroundColor Cyan
net stop IntelAvbFilter

Start-Sleep -Milliseconds 1000

Write-Host "==> Starting IntelAvbFilter service..." -ForegroundColor Cyan
net start IntelAvbFilter

Start-Sleep -Milliseconds 500

Write-Host "==> Service status:" -ForegroundColor Cyan
sc.exe query IntelAvbFilter

Write-Host "`n[OK] Service restarted - Build 76 now active" -ForegroundColor Green
Read-Host "Press Enter to close"
