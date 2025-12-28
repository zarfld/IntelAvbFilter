# Force Start Intel AVB Filter Driver
# This script attempts to force-load the NDIS filter driver
# Run as Administrator

Write-Host "=== Intel AVB Filter Driver - Force Start ===" -ForegroundColor Cyan
Write-Host ""

# Check admin
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "? ERROR: Must run as Administrator" -ForegroundColor Red
    Write-Host "Right-click PowerShell and select 'Run as Administrator'" -ForegroundColor Yellow
    exit 1
}

Write-Host "? Running as Administrator" -ForegroundColor Green
Write-Host ""

# Check if service exists
$service = Get-Service -Name "IntelAvbFilter" -ErrorAction SilentlyContinue
if (-not $service) {
    Write-Host "? Service not found - creating it..." -ForegroundColor Yellow
    sc.exe create IntelAvbFilter type= kernel start= system error= normal binpath= System32\drivers\IntelAvbFilter.sys DisplayName= "Intel AVB/TSN NDIS Filter Driver"
    Start-Sleep -Milliseconds 500
}

Write-Host "Service status before start:" -ForegroundColor Yellow
sc.exe query IntelAvbFilter
Write-Host ""

# Try to start the service
Write-Host "Attempting to start service..." -ForegroundColor Yellow
$result = sc.exe start IntelAvbFilter 2>&1
$exitCode = $LASTEXITCODE

Write-Host "Start result: Exit code $exitCode" -ForegroundColor Cyan
Write-Host $result
Write-Host ""

# Wait for driver to initialize
Start-Sleep -Milliseconds 1000

# Check status after start
Write-Host "Service status after start:" -ForegroundColor Yellow
sc.exe query IntelAvbFilter
Write-Host ""

# Try to open device
Write-Host "Testing device interface..." -ForegroundColor Yellow

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class DeviceTest {
    [DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
    public static extern IntPtr CreateFile(
        string lpFileName, uint dwDesiredAccess, uint dwShareMode,
        IntPtr lpSecurityAttributes, uint dwCreationDisposition,
        uint dwFlagsAndAttributes, IntPtr hTemplateFile);
    
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr hObject);
    
    public const uint GENERIC_READ = 0x80000000;
    public const uint GENERIC_WRITE = 0x40000000;
    public const uint OPEN_EXISTING = 3;
}
"@

$devicePath = "\\.\IntelAvbFilter"
$handle = [DeviceTest]::CreateFile($devicePath, 0x80000000 -bor 0x40000000, 0, [IntPtr]::Zero, 3, 0, [IntPtr]::Zero)

if ($handle -ne [IntPtr]::new(-1)) {
    [DeviceTest]::CloseHandle($handle)
    Write-Host "? SUCCESS! Device interface is accessible!" -ForegroundColor Green
    Write-Host ""
    Write-Host "Running test suite..." -ForegroundColor Cyan
    if (Test-Path ".\avb_test_i210_um.exe") {
        & ".\avb_test_i210_um.exe" selftest
    } else {
        Write-Host "? Test application not found" -ForegroundColor Yellow
    }
} else {
    $error = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
    Write-Host "? Device interface NOT accessible (Error: $error)" -ForegroundColor Red
    Write-Host ""
    
    if ($error -eq 2) {
        Write-Host "Error 2 = File not found - Device interface was not created" -ForegroundColor Yellow
        Write-Host ""
        Write-Host "This means:" -ForegroundColor Yellow
        Write-Host "1. DriverEntry did not execute" -ForegroundColor Cyan
        Write-Host "2. Or IntelAvbFilterRegisterDevice() failed" -ForegroundColor Cyan
        Write-Host ""
        Write-Host "Checking Event Viewer for driver errors..." -ForegroundColor Yellow
        $events = Get-EventLog -LogName System -Source "IntelAvbFilter" -Newest 5 -ErrorAction SilentlyContinue
        if ($events) {
            $events | Format-Table -AutoSize
        } else {
            Write-Host "No IntelAvbFilter events found in System log" -ForegroundColor Yellow
        }
        Write-Host ""
        Write-Host "To debug:" -ForegroundColor Yellow
        Write-Host "1. Download DebugView from Microsoft Sysinternals" -ForegroundColor Cyan
        Write-Host "2. Run as Administrator" -ForegroundColor Cyan
        Write-Host "3. Enable 'Capture Kernel'" -ForegroundColor Cyan
        Write-Host "4. Re-run this script and watch for [IntelAvbFilter] messages" -ForegroundColor Cyan
    }
}

Write-Host ""
Write-Host "=== Done ===" -ForegroundColor Cyan
