# Fix Visual Studio WDK Deployment Configuration
# Run this script to disable remote deployment testing

param(
    [Parameter(Mandatory=$false)]
    [string]$ProjectFile = "IntelAvbFilter.vcxproj"
)

Write-Host "Intel AVB Filter Driver - Fix Deployment Configuration" -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan
Write-Host ""

# Calculate repository root from script location
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
if ($ScriptDir -match '\\tools\\build$') {
    # Script is in tools/build/ subdirectory
    $RepoRoot = Split-Path (Split-Path $ScriptDir -Parent) -Parent
} else {
    # Script is in repository root
    $RepoRoot = $ScriptDir
}

$projectPath = Join-Path $RepoRoot $ProjectFile

if (-not (Test-Path $projectPath)) {
    Write-Host "? Project file not found: $projectPath" -ForegroundColor Red
    Write-Host ""
    Write-Host "Available project files:" -ForegroundColor Yellow
    Get-ChildItem -Path $RepoRoot -Filter "*.vcxproj" | ForEach-Object {
        Write-Host "  - $($_.Name)" -ForegroundColor Gray
    }
    exit 1
}

Write-Host "? Found project file: $projectPath" -ForegroundColor Green
Write-Host ""

# Read project file
$projectContent = Get-Content $projectPath -Raw

# Check if deployment is configured
if ($projectContent -match '<DeploymentEnabled>true</DeploymentEnabled>') {
    Write-Host "? Deployment is currently ENABLED" -ForegroundColor Yellow
    Write-Host "  This causes 'No connection could be made' errors" -ForegroundColor Yellow
    Write-Host ""
    
    $response = Read-Host "Disable deployment? (y/n)"
    
    if ($response -eq 'y' -or $response -eq 'Y') {
        # Disable deployment
        $projectContent = $projectContent -replace '<DeploymentEnabled>true</DeploymentEnabled>', '<DeploymentEnabled>false</DeploymentEnabled>'
        
        # Save changes
        $projectContent | Set-Content $projectPath -NoNewline
        
        Write-Host "? Deployment DISABLED in project file" -ForegroundColor Green
        Write-Host ""
        Write-Host "Next steps:" -ForegroundColor Cyan
        Write-Host "  1. Reload project in Visual Studio (if open)" -ForegroundColor White
        Write-Host "  2. Rebuild solution" -ForegroundColor White
        Write-Host "  3. Test errors should be gone!" -ForegroundColor White
    } else {
        Write-Host "Cancelled" -ForegroundColor Yellow
    }
} else {
    Write-Host "? Deployment is already DISABLED or not configured" -ForegroundColor Green
    Write-Host "  The deployment errors might be coming from:" -ForegroundColor Yellow
    Write-Host "    � Test configuration files (.xml)" -ForegroundColor Gray
    Write-Host "    � Visual Studio cache" -ForegroundColor Gray
    Write-Host ""
    Write-Host "Try these steps:" -ForegroundColor Cyan
    Write-Host "  1. Close Visual Studio" -ForegroundColor White
    Write-Host "  2. Delete .vs folder in solution directory" -ForegroundColor White
    Write-Host "  3. Reopen solution" -ForegroundColor White
    Write-Host "  4. Rebuild" -ForegroundColor White
}

Write-Host ""
Write-Host "Manual fix (if script doesn't work):" -ForegroundColor Cyan
Write-Host "  1. Open Visual Studio" -ForegroundColor White
Write-Host "  2. Right-click project ? Properties" -ForegroundColor White
Write-Host "  3. Configuration Properties ? Driver Install ? Deployment" -ForegroundColor White
Write-Host "  4. UNCHECK 'Enable deployment'" -ForegroundColor White
Write-Host "  5. Apply ? OK ? Rebuild" -ForegroundColor White
Write-Host ""
