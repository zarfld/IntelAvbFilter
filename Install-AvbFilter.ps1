# Install Intel AVB Filter Driver
# Run as Administrator: Right-click PowerShell and select "Run as Administrator"

#Requires -RunAsAdministrator

Write-Host "=== Intel AVB/TSN NDIS Filter Driver Installation ===" -ForegroundColor Cyan
Write-Host ""

# Check if driver files exist
$driverPath = "x64\Debug\IntelAvbFilter"
if (-not (Test-Path "$driverPath\IntelAvbFilter.sys")) {
    Write-Host "? ERROR: Driver file not found: $driverPath\IntelAvbFilter.sys" -ForegroundColor Red
    Write-Host "Please build the driver first using: msbuild /p:Configuration=Debug;Platform=x64" -ForegroundColor Yellow
    exit 1
}

Write-Host "? Driver files found" -ForegroundColor Green

# Step 1: Install driver package
Write-Host ""
Write-Host "Step 1: Installing driver package..." -ForegroundColor Yellow
$result = pnputil /add-driver "$driverPath\IntelAvbFilter.inf" /install 2>&1
Write-Host $result

# Step 2: Create/Register the filter service
Write-Host ""
Write-Host "Step 2: Registering filter service..." -ForegroundColor Yellow

# Check if service already exists
$service = Get-Service -Name "IntelAvbFilter" -ErrorAction SilentlyContinue
if ($service) {
    Write-Host "? Service already exists, stopping it first..." -ForegroundColor Yellow
    Stop-Service -Name "IntelAvbFilter" -Force -ErrorAction SilentlyContinue
}

# Create or update the service
$scResult = sc.exe create IntelAvbFilter type= kernel start= demand error= normal binpath= System32\drivers\IntelAvbFilter.sys DisplayName= "Intel AVB/TSN NDIS Filter Driver" 2>&1

if ($LASTEXITCODE -eq 1073) {
    Write-Host "? Service already exists, that's OK" -ForegroundColor Green
    # Update the service configuration
    sc.exe config IntelAvbFilter start= demand 2>&1 | Out-Null
} elseif ($LASTEXITCODE -eq 0) {
    Write-Host "? Service created successfully" -ForegroundColor Green
} else {
    Write-Host "? Service creation had issues, but may still work" -ForegroundColor Yellow
}

# Step 3: Start the service to create device interface
Write-Host ""
Write-Host "Step 3: Starting filter service..." -ForegroundColor Yellow
$startResult = sc.exe start IntelAvbFilter 2>&1
if ($LASTEXITCODE -eq 0) {
    Write-Host "? Service started successfully" -ForegroundColor Green
} elseif ($LASTEXITCODE -eq 1056) {
    Write-Host "? Service is already running" -ForegroundColor Green
} else {
    Write-Host "? Service start returned code $LASTEXITCODE - this may be normal for NDIS filters" -ForegroundColor Yellow
    Write-Host "   NDIS filters can auto-start when a network adapter is accessed" -ForegroundColor Cyan
}

# Step 4: Check service status
Write-Host ""
Write-Host "Step 4: Checking service status..." -ForegroundColor Yellow
sc.exe query IntelAvbFilter

# Step 5: List available network adapters
Write-Host ""
Write-Host "Step 5: Available Intel network adapters:" -ForegroundColor Yellow
$adapters = Get-NetAdapter | Where-Object { $_.InterfaceDescription -like "*Intel*" }
$adapters | Format-Table Name, InterfaceDescription, Status -AutoSize

# Step 6: Check for device interface
Write-Host ""
Write-Host "Step 6: Testing device interface..." -ForegroundColor Yellow
$devicePath = "\\.\IntelAvbFilter"
Write-Host "Checking if device '$devicePath' is accessible..." -ForegroundColor Cyan

# Try to access the device (this will show if the driver created its device interface)
try {
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
    $handle = [DeviceTest]::CreateFile($devicePath, 0x80000000 -bor 0x40000000, 0, [IntPtr]::Zero, 3, 0, [IntPtr]::Zero)
    if ($handle -ne [IntPtr]::new(-1)) {
        [DeviceTest]::CloseHandle($handle)
        Write-Host "? Device interface is accessible!" -ForegroundColor Green
        Write-Host ""
        Write-Host "=== Installation Successful ===" -ForegroundColor Green
        Write-Host "You can now run tests using: .\avb_test_i210_um.exe selftest" -ForegroundColor Cyan
    } else {
        $error = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
        Write-Host "? Device interface not yet accessible (Error: $error)" -ForegroundColor Yellow
        Write-Host "  This may be normal - the device interface is created when the filter attaches to an adapter" -ForegroundColor Cyan
        Write-Host ""
        Write-Host "Next steps:" -ForegroundColor Yellow
        Write-Host "1. Restart your computer to ensure the filter loads properly" -ForegroundColor Cyan
        Write-Host "2. Or try: Disable and re-enable one of your Intel network adapters" -ForegroundColor Cyan
        Write-Host "3. Then run: .\avb_test_i210_um.exe selftest" -ForegroundColor Cyan
    }
} catch {
    Write-Host "? Could not test device interface: $_" -ForegroundColor Yellow
    Write-Host "  Try running the test after reboot: .\avb_test_i210_um.exe selftest" -ForegroundColor Cyan
}

Write-Host ""
Write-Host "For debugging, you can use DebugView (Microsoft Sysinternals):" -ForegroundColor Yellow
Write-Host "1. Download DebugView from Microsoft Sysinternals" -ForegroundColor Cyan
Write-Host "2. Run as Administrator" -ForegroundColor Cyan
Write-Host "3. Enable 'Capture Kernel'" -ForegroundColor Cyan
Write-Host "4. Look for [IntelAvbFilter] messages" -ForegroundColor Cyan
