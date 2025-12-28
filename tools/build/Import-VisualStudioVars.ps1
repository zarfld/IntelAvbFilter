# Import-VisualStudioVars.ps1
# Imports Visual Studio environment variables into current PowerShell session
# Auto-detects Visual Studio installation using vswhere

param(
    [Parameter(Mandatory=$false)]
    [ValidateSet('x64', 'x86')]
    [string]$Architecture = 'x64'
)

$ErrorActionPreference = 'Stop'

try {
    # Auto-detect Visual Studio using vswhere
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    
    if (-not (Test-Path $vswhere)) {
        throw "Visual Studio not found. Install Visual Studio 2019 or later with C++ workload."
    }
    
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    
    if (-not $vsPath) {
        throw "Visual Studio C++ tools not found. Install Visual Studio with C++ workload."
    }
    
    $VcVarsPath = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
    
    if (-not (Test-Path $VcVarsPath)) {
        throw "vcvars64.bat not found at: $VcVarsPath"
    }
    
    Write-Host "Detected Visual Studio at: $vsPath" -ForegroundColor Cyan

    # Create temp batch file that outputs environment variables
    $tempBat = [System.IO.Path]::GetTempFileName() + ".bat"
    $tempTxt = [System.IO.Path]::GetTempFileName()

@"
@echo off
call "$VcVarsPath" >nul 2>&1
if errorlevel 1 exit /b 1
set > "$tempTxt"
"@ | Set-Content -Path $tempBat -Encoding ASCII

# Execute batch file
$result = & cmd /c $tempBat
$exitCode = $LASTEXITCODE

# Cleanup batch file
Remove-Item $tempBat -Force -ErrorAction SilentlyContinue

if ($exitCode -ne 0) {
    if (Test-Path $tempTxt) { Remove-Item $tempTxt -Force -ErrorAction SilentlyContinue }
    throw "Failed to initialize Visual Studio environment"
}

# Read environment variables from temp file
if (Test-Path $tempTxt) {
    Get-Content $tempTxt | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') {
            $name = $matches[1]
            $value = $matches[2]
            
            # Set environment variable in current session
            [System.Environment]::SetEnvironmentVariable($name, $value, 'Process')
        }
    }
    Remove-Item $tempTxt -Force -ErrorAction SilentlyContinue
}

Write-Host "Visual Studio environment variables imported successfully" -ForegroundColor Green

} catch {
    Write-Host "ERROR: Failed to import Visual Studio environment" -ForegroundColor Red
    Write-Host "  $_" -ForegroundColor Yellow
    exit 1
}
