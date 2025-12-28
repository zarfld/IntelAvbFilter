Set-Location "C:\Users\dzarf\source\repos\IntelAvbFilter"
.\tools\development\Check-System.ps1 | Tee-Object -FilePath "check-system-output.txt"
Write-Host ""
Write-Host "Press any key to close..." -ForegroundColor Yellow
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
