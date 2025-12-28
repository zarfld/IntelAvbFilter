<#
.SYNOPSIS
    Fix Visual Studio WDK Deployment Configuration

.DESCRIPTION
    One-time configuration script to disable remote deployment testing
    in Visual Studio project files.
    
    When remote deployment is enabled, builds fail with:
    "No connection could be made because the target machine actively refused it"
    
    This script disables deployment to allow local builds.

.PARAMETER ProjectFile
    Path to .vcxproj file. Default: IntelAvbFilter.vcxproj

.EXAMPLE
    .\Fix-Deployment-Config.ps1
    Fix default project file

.EXAMPLE
    .\Fix-Deployment-Config.ps1 -ProjectFile MyProject.vcxproj
    Fix specific project file

.NOTES
    This is a one-time fix - only needed after cloning repository
    or when deployment config gets re-enabled
#>

param(
    [Parameter(Mandatory=$false)]
    [string]$ProjectFile = "IntelAvbFilter.vcxproj"
)

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Fix Visual Studio Deployment Config" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Navigate to repository root
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
if ($ScriptDir -match '\\tools\\setup$') {
    $RepoRoot = Split-Path (Split-Path $ScriptDir -Parent) -Parent
} else {
    $RepoRoot = $ScriptDir
}

$projectPath = Join-Path $RepoRoot $ProjectFile

if (-not (Test-Path $projectPath)) {
    Write-Host "✗ Project file not found: $projectPath" -ForegroundColor Red
    Write-Host ""
    Write-Host "Available project files:" -ForegroundColor Yellow
    Get-ChildItem -Path $RepoRoot -Filter "*.vcxproj" | ForEach-Object {
        Write-Host "  - $($_.Name)" -ForegroundColor Gray
    }
    exit 1
}

Write-Host "✓ Found project file: $projectPath" -ForegroundColor Green
Write-Host ""

# Read project file
$projectContent = Get-Content $projectPath -Raw

# Check if deployment is configured
if ($projectContent -match '<DeploymentEnabled>true</DeploymentEnabled>') {
    Write-Host "⚠ Deployment is currently ENABLED" -ForegroundColor Yellow
    Write-Host "  This causes 'No connection could be made' errors" -ForegroundColor Yellow
    Write-Host ""
    
    $response = Read-Host "Disable deployment? (y/n)"
    
    if ($response -eq 'y' -or $response -eq 'Y') {
        # Disable deployment
        $projectContent = $projectContent -replace '<DeploymentEnabled>true</DeploymentEnabled>', '<DeploymentEnabled>false</DeploymentEnabled>'
        
        # Backup original
        $backupPath = "$projectPath.backup"
        Copy-Item $projectPath $backupPath -Force
        Write-Host "✓ Backup created: $backupPath" -ForegroundColor Gray
        
        # Write modified content
        Set-Content $projectPath $projectContent -NoNewline
        
        Write-Host "✓ Deployment DISABLED" -ForegroundColor Green
        Write-Host ""
        Write-Host "Changes made:" -ForegroundColor Cyan
        Write-Host "  <DeploymentEnabled>true</DeploymentEnabled>" -ForegroundColor Red
        Write-Host "  ↓" -ForegroundColor Yellow
        Write-Host "  <DeploymentEnabled>false</DeploymentEnabled>" -ForegroundColor Green
        Write-Host ""
        Write-Host "✓ Project configuration fixed!" -ForegroundColor Green
        Write-Host "  You can now build without remote deployment errors" -ForegroundColor Gray
    } else {
        Write-Host "Aborted - no changes made" -ForegroundColor Yellow
    }
} elseif ($projectContent -match '<DeploymentEnabled>false</DeploymentEnabled>') {
    Write-Host "✓ Deployment is already DISABLED" -ForegroundColor Green
    Write-Host "  No changes needed" -ForegroundColor Gray
} else {
    Write-Host "ℹ No deployment configuration found in project file" -ForegroundColor Gray
    Write-Host "  This is normal for projects that never had deployment enabled" -ForegroundColor Gray
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
