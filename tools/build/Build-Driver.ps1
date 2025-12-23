<# 
.SYNOPSIS
    Intel AVB Filter Driver - Canonical Build Script

.DESCRIPTION
    Comprehensive build script for driver and test executables.
    Combines Build-And-Sign-Driver.ps1 with Build-AllTests-Honest.ps1 functionality.
    
    This is THE canonical build script - all build wrappers call this with preset parameters.

.PARAMETER Configuration
    Build configuration (Debug or Release). Default: Debug

.PARAMETER Platform
    Build platform (x64). Default: x64

.PARAMETER Sign
    Sign the driver after building (creates test certificate if needed)

.PARAMETER BuildTests
    Build all test executables after driver build

.PARAMETER SkipDriver
    Skip driver build, only build tests

.PARAMETER Clean
    Clean before building

.EXAMPLE
    .\Build-Driver.ps1 -Configuration Debug
    Build debug driver

.EXAMPLE
    .\Build-Driver.ps1 -Configuration Release -Sign
    Build and sign release driver

.EXAMPLE
    .\Build-Driver.ps1 -BuildTests
    Build driver + all test executables

.EXAMPLE
    .\Build-Driver.ps1 -SkipDriver -BuildTests
    Build only test executables

.NOTES
    Based on Build-And-Sign-Driver.ps1 and Build-AllTests-Honest.ps1
    Finds MSBuild and WDK tools automatically
    
    Author: Intel AVB Filter Team
    Issue: #27 REQ-NF-SCRIPTS-001
#>

param(
    [Parameter(Mandatory=$false)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    
    [Parameter(Mandatory=$false)]
    [ValidateSet("x64")]
    [string]$Platform = "x64",
    
    [Parameter(Mandatory=$false)]
    [switch]$Sign,
    
    [Parameter(Mandatory=$false)]
    [switch]$BuildTests,
    
    [Parameter(Mandatory=$false)]
    [switch]$SkipDriver,
    
    [Parameter(Mandatory=$false)]
    [switch]$Clean
)

# Helper functions
function Write-Step {
    param([string]$Message)
    Write-Host "`n==> $Message" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)
    Write-Host "[OK] $Message" -ForegroundColor Green
}

function Write-Failure {
    param([string]$Message)
    Write-Host "[FAIL] $Message" -ForegroundColor Red
}

function Write-Info {
    param([string]$Message)
    Write-Host "[INFO] $Message" -ForegroundColor Yellow
}

# Banner
Write-Host @"
================================================================
  Intel AVB Filter Driver - Build Script
  Configuration: $Configuration | Platform: $Platform
================================================================
"@ -ForegroundColor Cyan

# Calculate paths
$scriptDir = $PSScriptRoot  # tools/build
$toolsDir = Split-Path $scriptDir -Parent  # tools
$repoRoot = Split-Path $toolsDir -Parent  # repository root
$outputDir = Join-Path $repoRoot "$Platform\$Configuration\IntelAvbFilter"
$solutionFile = Join-Path $repoRoot "IntelAvbFilter.sln"

# ===========================
# STEP 1: Find MSBuild
# ===========================
if (-not $SkipDriver) {
    Write-Step "Locating MSBuild"
    
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        Write-Failure "vswhere.exe not found. Please install Visual Studio 2022."
        exit 1
    }
    
    $vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
    if (-not $vsPath) {
        Write-Failure "Visual Studio not found"
        exit 1
    }
    
    $msbuildPath = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
    if (-not (Test-Path $msbuildPath)) {
        Write-Failure "MSBuild not found at: $msbuildPath"
        exit 1
    }
    
    Write-Success "Found MSBuild: $msbuildPath"
}

# ===========================
# STEP 2: Find WDK Tools (if signing)
# ===========================
if ($Sign) {
    Write-Step "Locating Windows Driver Kit tools"
    
    $wdkVersions = @("10.0.26100.0", "10.0.22621.0", "10.0.19041.0")
    $wdkBasePath = "C:\Program Files (x86)\Windows Kits\10\bin"
    
    $inf2catPath = $null
    $signtoolPath = $null
    
    foreach ($version in $wdkVersions) {
        $inf2cat = Join-Path $wdkBasePath "$version\x64\inf2cat.exe"
        $signtool = Join-Path $wdkBasePath "$version\x64\signtool.exe"
        
        if ((Test-Path $inf2cat) -and (Test-Path $signtool)) {
            $inf2catPath = $inf2cat
            $signtoolPath = $signtool
            Write-Success "Found WDK $version"
            break
        }
    }
    
    if (-not $inf2catPath -or -not $signtoolPath) {
        Write-Failure "WDK tools not found. Please install Windows Driver Kit."
        exit 1
    }
}

# ===========================
# STEP 3: Build Driver
# ===========================
if (-not $SkipDriver) {
    Write-Step "Building Driver"
    
    $buildCmd = if ($Clean) { "Clean;Build" } else { "Build" }
    
    Write-Host "  Solution: $solutionFile" -ForegroundColor Gray
    Write-Host "  Output: $outputDir" -ForegroundColor Gray
    
    & $msbuildPath $solutionFile `
        /t:$buildCmd `
        /p:Configuration=$Configuration `
        /p:Platform=$Platform `
        /m `
        /verbosity:minimal
    
    if ($LASTEXITCODE -ne 0) {
        Write-Failure "Driver build failed! Exit code: $LASTEXITCODE"
        exit $LASTEXITCODE
    }
    
    Write-Success "Driver build completed"
    
    # Verify output files
    $sysFile = Join-Path $outputDir "IntelAvbFilter.sys"
    $infFile = Join-Path $outputDir "IntelAvbFilter.inf"
    
    if ((Test-Path $sysFile) -and (Test-Path $infFile)) {
        Write-Success "Build artifacts verified:"
        Write-Host "  SYS: $(Get-Item $sysFile | Select-Object -ExpandProperty Length) bytes" -ForegroundColor Gray
        Write-Host "  INF: $infFile" -ForegroundColor Gray
    } else {
        Write-Failure "Build artifacts missing!"
        exit 1
    }
}

# ===========================
# STEP 4: Generate CAT and Sign
# ===========================
if ($Sign -and -not $SkipDriver) {
    Write-Step "Generating Catalog and Signing"
    
    $catFile = Join-Path $outputDir "IntelAvbFilter.cat"
    
    # Generate CAT using inf2cat
    Write-Host "Generating CAT file..." -ForegroundColor Gray
    & $inf2catPath /driver:"$outputDir" /os:10_X64
    
    if ($LASTEXITCODE -ne 0) {
        Write-Failure "CAT generation failed! Exit code: $LASTEXITCODE"
        exit $LASTEXITCODE
    }
    
    if (-not (Test-Path $catFile)) {
        Write-Failure "CAT file not created: $catFile"
        exit 1
    }
    
    Write-Success "CAT file generated: $(Get-Item $catFile | Select-Object -ExpandProperty Length) bytes"
    
    # Check for test certificate
    $certName = "Intel AVB Filter Test Certificate"
    $cert = Get-ChildItem -Path "Cert:\CurrentUser\My" | Where-Object {$_.Subject -like "*$certName*"} | Select-Object -First 1
    
    if (-not $cert) {
        Write-Info "Test certificate not found, creating..."
        $cert = New-SelfSignedCertificate -Type CodeSigningCert `
            -Subject "CN=$certName" `
            -KeyUsage DigitalSignature `
            -FriendlyName $certName `
            -CertStoreLocation "Cert:\CurrentUser\My" `
            -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")
        
        Write-Success "Created test certificate: $($cert.Thumbprint)"
    } else {
        Write-Success "Using existing certificate: $($cert.Thumbprint)"
    }
    
    # Sign CAT file
    Write-Host "Signing CAT file..." -ForegroundColor Gray
    & $signtoolPath sign /v /s My /n $certName /t http://timestamp.digicert.com $catFile
    
    if ($LASTEXITCODE -ne 0) {
        Write-Failure "Signing failed! Exit code: $LASTEXITCODE"
        exit $LASTEXITCODE
    }
    
    Write-Success "Driver signed successfully"
}

# ===========================
# STEP 5: Build Tests
# ===========================
if ($BuildTests) {
    Write-Step "Building Test Executables"
    
    # Find vcvars64.bat
    if (-not $vsPath) {
        $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    
    $vcvarsPath = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
    if (-not (Test-Path $vcvarsPath)) {
        Write-Failure "vcvars64.bat not found: $vcvarsPath"
        exit 1
    }
    
    # Build test executables
    $testMakefiles = @(
        "tools\avb_test\avb_diagnostic.mak",
        "tools\avb_test\avb_hw_state_test.mak",
        "tools\avb_test\tsn_ioctl_test.mak",
        "tools\avb_test\avb_test.mak",
        "tools\avb_test\avb_i226_test.mak"
    )
    
    $successCount = 0
    $failCount = 0
    
    foreach ($makefile in $testMakefiles) {
        $testName = [System.IO.Path]::GetFileNameWithoutExtension($makefile)
        Write-Host "`nBuilding $testName..." -ForegroundColor Yellow
        
        $buildCmd = "cmd /c `"`"$vcvarsPath`" && nmake -f $makefile`""
        
        try {
            Invoke-Expression $buildCmd | Out-Null
            
            if ($LASTEXITCODE -eq 0) {
                Write-Success "$testName build succeeded"
                $successCount++
            } else {
                Write-Failure "$testName build failed (Exit code: $LASTEXITCODE)"
                $failCount++
            }
        } catch {
            Write-Failure "$testName build failed: $($_.Exception.Message)"
            $failCount++
        }
    }
    
    Write-Host "`nTest Build Summary:" -ForegroundColor Cyan
    Write-Host "  Successful: $successCount" -ForegroundColor Green
    Write-Host "  Failed: $failCount" -ForegroundColor $(if ($failCount -gt 0) { "Red" } else { "Green" })
    
    # List built executables
    $testExeDir = Join-Path $repoRoot "build\tools\avb_test\$Platform\$Configuration"
    if (Test-Path $testExeDir) {
        $exes = Get-ChildItem -Path $testExeDir -Filter "*.exe"
        if ($exes) {
            Write-Host "`nBuilt executables:" -ForegroundColor Cyan
            foreach ($exe in $exes) {
                Write-Host "  [OK] $($exe.Name) ($($exe.Length) bytes)" -ForegroundColor Green
            }
        }
    }
}

# ===========================
# STEP 6: Summary
# ===========================
Write-Host "`n================================================================" -ForegroundColor Cyan
Write-Host "  Build Complete!" -ForegroundColor Green
Write-Host "================================================================" -ForegroundColor Cyan

if (-not $SkipDriver) {
    Write-Host "Driver output: $outputDir" -ForegroundColor White
}

if ($BuildTests) {
    $testExeDir = Join-Path $repoRoot "build\tools\avb_test\$Platform\$Configuration"
    Write-Host "Test executables: $testExeDir" -ForegroundColor White
}

if (-not $SkipDriver) {
    Write-Host "`nNext steps:" -ForegroundColor Yellow
    Write-Host "  1. Install driver: .\tools\Setup-Driver.ps1 -Configuration $Configuration -InstallDriver" -ForegroundColor Cyan
    Write-Host "  2. Run tests: .\tools\Test-Driver.ps1 -Configuration $Configuration" -ForegroundColor Cyan
}
