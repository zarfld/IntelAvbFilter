# Build script for TSAUXC toggle test
# Uses Visual Studio C++ compiler

param(
    [string]$BuildCmd = "cl /nologo /W4 /Zi /I include /I external/intel_avb/lib /I intel-ethernet-regs/gen tests/integration/tsn/tsauxc_toggle_test.c /Fe:tsauxc_toggle_test.exe"
)

Write-Host "TSAUXC Toggle Test Build Script" -ForegroundColor Cyan
Write-Host "================================" -ForegroundColor Cyan
Write-Host ""

# Find Visual Studio installation
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (!(Test-Path $vswhere)) {
    Write-Error "vswhere.exe not found - Visual Studio installation may be corrupted"
    exit 1
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsPath) {
    Write-Error "Visual Studio C++ tools not found"
    exit 1
}

$vcvarsPath = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
if (!(Test-Path $vcvarsPath)) {
    Write-Error "vcvars64.bat not found at: $vcvarsPath"
    exit 1
}

Write-Host "? Visual Studio found at: $vsPath" -ForegroundColor Green
Write-Host "? Setting up environment..." -ForegroundColor Yellow
Write-Host ""

# Create temporary batch file to run vcvars and compile
$tempBat = [System.IO.Path]::GetTempFileName() + ".bat"
$batContent = @"
@echo off
call "$vcvarsPath" > nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to initialize Visual Studio environment
    exit /b 1
)
echo Building TSAUXC toggle test...
$BuildCmd
"@

Set-Content -Path $tempBat -Value $batContent

# Execute the build
$output = cmd /c $tempBat 2>&1
$exitCode = $LASTEXITCODE

# Clean up temp file
Remove-Item $tempBat -ErrorAction SilentlyContinue

# Display output
$output | ForEach-Object { Write-Host $_ }

if ($exitCode -eq 0) {
    Write-Host ""
    Write-Host "? BUILD SUCCESSFUL" -ForegroundColor Green
    Write-Host ""
    Write-Host "Run the test with:" -ForegroundColor Cyan
    Write-Host "  .\tsauxc_toggle_test.exe" -ForegroundColor Yellow
    Write-Host ""
} else {
    Write-Host ""
    Write-Host "? BUILD FAILED (exit code: $exitCode)" -ForegroundColor Red
    exit $exitCode
}
