# Verify IntelAvbFilter Driver Version
# Checks if the new driver with NULL checks is loaded

Write-Host "================================================================"
Write-Host "  Intel AVB Filter Driver - Version Verification"
Write-Host "================================================================"

# Get driver file path
$driver = Get-WmiObject Win32_SystemDriver | Where-Object {$_.Name -eq "IntelAvbFilter"}
if (-not $driver) {
    Write-Host "[ERROR] IntelAvbFilter driver not found!" -ForegroundColor Red
    exit 1
}

Write-Host "`n[INFO] Driver Status:" -ForegroundColor Cyan
Write-Host "  Name: $($driver.Name)"
Write-Host "  State: $($driver.State)"
Write-Host "  Status: $($driver.Status)"
Write-Host "  Path: $($driver.PathName)"

# Get file details
$driverPath = $driver.PathName
if (Test-Path $driverPath) {
    $fileInfo = Get-Item $driverPath
    Write-Host "`n[INFO] Driver File Details:" -ForegroundColor Cyan
    Write-Host "  Size: $($fileInfo.Length) bytes"
    Write-Host "  Last Modified: $($fileInfo.LastWriteTime)"
    Write-Host "  Created: $($fileInfo.CreationTime)"
    
    # Get version info
    $versionInfo = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($driverPath)
    Write-Host "`n[INFO] Version Info:" -ForegroundColor Cyan
    Write-Host "  File Version: $($versionInfo.FileVersion)"
    Write-Host "  Product Version: $($versionInfo.ProductVersion)"
} else {
    Write-Host "[ERROR] Driver file not found at: $driverPath" -ForegroundColor Red
}

# Get our build file details
$buildPath = "C:\Users\dzarf\source\repos\IntelAvbFilter\build\x64\Debug\IntelAvbFilter\IntelAvbFilter.sys"
if (Test-Path $buildPath) {
    $buildInfo = Get-Item $buildPath
    Write-Host "`n[INFO] Build File Details:" -ForegroundColor Cyan
    Write-Host "  Size: $($buildInfo.Length) bytes"
    Write-Host "  Last Modified: $($buildInfo.LastWriteTime)"
    Write-Host "  Created: $($buildInfo.CreationTime)"
    
    $buildVersionInfo = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($buildPath)
    Write-Host "`n[INFO] Build Version Info:" -ForegroundColor Cyan
    Write-Host "  File Version: $($buildVersionInfo.FileVersion)"
    Write-Host "  Product Version: $($buildVersionInfo.ProductVersion)"
    
    # Compare timestamps
    Write-Host "`n[VERIFICATION]" -ForegroundColor Yellow
    if ($fileInfo.LastWriteTime -eq $buildInfo.LastWriteTime) {
        Write-Host "  ✅ TIMESTAMPS MATCH - Correct driver is loaded!" -ForegroundColor Green
    } else {
        Write-Host "  ⚠️ TIMESTAMPS DIFFER - May be old driver!" -ForegroundColor Red
        Write-Host "     Loaded: $($fileInfo.LastWriteTime)"
        Write-Host "     Build:  $($buildInfo.LastWriteTime)"
    }
    
    if ($fileInfo.Length -eq $buildInfo.Length) {
        Write-Host "  ✅ FILE SIZES MATCH" -ForegroundColor Green
    } else {
        Write-Host "  ⚠️ FILE SIZES DIFFER" -ForegroundColor Red
        Write-Host "     Loaded: $($fileInfo.Length) bytes"
        Write-Host "     Build:  $($buildInfo.Length) bytes"
    }
} else {
    Write-Host "`n[WARNING] Build file not found at: $buildPath" -ForegroundColor Yellow
}

Write-Host "`n================================================================"
Write-Host "Verification Complete"
Write-Host "================================================================"
