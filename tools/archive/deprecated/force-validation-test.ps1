# Force validation by creating a scenario that should trigger the INVALID message
# This will help confirm if the enhanced validation is actually executing

Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "  Testing Enhanced Pointer Validation" -ForegroundColor Cyan  
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host ""

Write-Host "[INFO] Current driver status:" -ForegroundColor Yellow
Get-WmiObject Win32_SystemDriver | Where-Object { $_.Name -eq "IntelAvbFilter" } | 
    Select-Object Name, State, Status, PathName | Format-List

Write-Host ""
Write-Host "[INFO] Checking driver file timestamp:" -ForegroundColor Yellow  
$driverPath = Get-ChildItem "C:\WINDOWS\system32\DriverStore\FileRepository\intelavbfilter*\IntelAvbFilter.sys" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($driverPath) {
    Write-Host "  File: $($driverPath.FullName)" -ForegroundColor White
    Write-Host "  Size: $($driverPath.Length) bytes" -ForegroundColor White
    Write-Host "  Modified: $($driverPath.LastWriteTime)" -ForegroundColor White
} else {
    Write-Host "  ERROR: Driver file not found!" -ForegroundColor Red
}

Write-Host ""
Write-Host "[INFO] Build file:" -ForegroundColor Yellow
$buildFile = Get-Item "C:\Users\dzarf\source\repos\IntelAvbFilter\build\x64\Debug\IntelAvbFilter\IntelAvbFilter.sys"
Write-Host "  Size: $($buildFile.Length) bytes" -ForegroundColor White
Write-Host "  Modified: $($buildFile.LastWriteTime)" -ForegroundColor White

Write-Host ""
if ($driverPath -and ($driverPath.Length -eq $buildFile. Length) -and 
    ($driverPath.LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss") -eq $buildFile.LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss"))) {
    Write-Host "[OK] Driver matches build!" -ForegroundColor Green
} else {
    Write-Host "[WARNING] Driver DOES NOT match build!" -ForegroundColor Red
    Write-Host "  This could explain why crashes continue despite the fix." -ForegroundColor Red
}

Write-Host ""
Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "  1. Completely uninstall driver" -ForegroundColor White
Write-Host "  2. Clean DriverStore" -ForegroundColor White
Write-Host "  3. Reinstall Build #38" -ForegroundColor White
Write-Host "  4. Verify with DebugView capture" -ForegroundColor White
