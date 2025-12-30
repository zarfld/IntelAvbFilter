# Test-REGS-001-BuildVerification.ps1
# Verifies: #304 (TEST-REGS-001: Build with intel-ethernet-regs headers)
# Implements: #21 (REQ-NF-REGS-001: Eliminate Magic Numbers)

<#
.SYNOPSIS
    Verify driver builds successfully with intel-ethernet-regs submodule headers

.DESCRIPTION
    This test verifies that:
    1. intel-ethernet-regs submodule is initialized
    2. Generated headers exist
    3. Driver builds in Debug and Release configurations
    4. Binary files are created successfully

.PARAMETER Configuration
    Build configuration (Debug or Release). Default: Debug

.EXAMPLE
    .\Test-REGS-001-BuildVerification.ps1 -Configuration Release
#>

param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))

Write-Host "=== TEST-REGS-001: Build Verification ===" -ForegroundColor Cyan
Write-Host "Repository: $RepoRoot" -ForegroundColor Gray
Write-Host "Configuration: $Configuration" -ForegroundColor Gray
Write-Host ""

$TestsPassed = 0
$TestsFailed = 0

# Step 1: Verify submodule initialized
Write-Host "[1/4] Verifying intel-ethernet-regs submodule..." -ForegroundColor Yellow
$SubmodulePath = Join-Path $RepoRoot "intel-ethernet-regs"

if (-not (Test-Path $SubmodulePath)) {
    Write-Host "  ❌ FAIL: Submodule directory not found: $SubmodulePath" -ForegroundColor Red
    $TestsFailed++
} else {
    $GitmodulesPath = Join-Path $RepoRoot ".gitmodules"
    if (Test-Path $GitmodulesPath) {
        $Content = Get-Content $GitmodulesPath -Raw
        if ($Content -match 'intel-ethernet-regs') {
            Write-Host "  ✅ PASS: Submodule directory exists and configured in .gitmodules" -ForegroundColor Green
            $TestsPassed++
        } else {
            Write-Host "  ❌ FAIL: Submodule not configured in .gitmodules" -ForegroundColor Red
            $TestsFailed++
        }
    } else {
        Write-Host "  ⚠️  WARNING: .gitmodules not found, but directory exists" -ForegroundColor Yellow
        $TestsPassed++
    }
}

# Step 2: Verify generated headers exist
Write-Host "[2/4] Verifying generated headers..." -ForegroundColor Yellow
$GenPath = Join-Path $SubmodulePath "gen"
$RequiredHeaders = @('i210_regs.h', 'i225_regs.h', 'i226_regs.h')

$HeadersFound = 0
foreach ($Header in $RequiredHeaders) {
    $HeaderPath = Join-Path $GenPath $Header
    if (Test-Path $HeaderPath) {
        Write-Host "  ✅ Found: $Header" -ForegroundColor Green
        $HeadersFound++
    } else {
        Write-Host "  ❌ Missing: $Header" -ForegroundColor Red
    }
}

if ($HeadersFound -eq $RequiredHeaders.Count) {
    Write-Host "  ✅ PASS: All required headers found ($HeadersFound/$($RequiredHeaders.Count))" -ForegroundColor Green
    $TestsPassed++
} else {
    Write-Host "  ❌ FAIL: Missing headers ($HeadersFound/$($RequiredHeaders.Count))" -ForegroundColor Red
    $TestsFailed++
}

# Step 3: Build driver
Write-Host "[3/4] Building driver ($Configuration)..." -ForegroundColor Yellow
$BuildScript = Join-Path $RepoRoot "tools\build\Build-Driver.ps1"

if (-not (Test-Path $BuildScript)) {
    Write-Host "  ❌ FAIL: Build script not found: $BuildScript" -ForegroundColor Red
    $TestsFailed++
} else {
    try {
        & powershell -NoProfile -ExecutionPolicy Bypass -File $BuildScript -Configuration $Configuration -ErrorAction Stop
        if ($LASTEXITCODE -eq 0) {
            Write-Host "  ✅ PASS: Build succeeded" -ForegroundColor Green
            $TestsPassed++
        } else {
            Write-Host "  ❌ FAIL: Build failed with exit code $LASTEXITCODE" -ForegroundColor Red
            $TestsFailed++
        }
    } catch {
        Write-Host "  ❌ FAIL: Build exception: $_" -ForegroundColor Red
        $TestsFailed++
    }
}

# Step 4: Verify binary exists
Write-Host "[4/4] Verifying binary..." -ForegroundColor Yellow
$BinaryPath = Join-Path $RepoRoot "build\x64\$Configuration\IntelAvbFilter.sys"

if (Test-Path $BinaryPath) {
    $Binary = Get-Item $BinaryPath
    Write-Host "  ✅ PASS: Binary created: $($Binary.Name) ($($Binary.Length) bytes)" -ForegroundColor Green
    $TestsPassed++
} else {
    Write-Host "  ❌ FAIL: Binary not found: $BinaryPath" -ForegroundColor Red
    $TestsFailed++
}

# Summary
Write-Host ""
Write-Host "=== Test Summary ===" -ForegroundColor Cyan
Write-Host "  Passed: $TestsPassed" -ForegroundColor Green
Write-Host "  Failed: $TestsFailed" -ForegroundColor Red
Write-Host ""

if ($TestsFailed -gt 0) {
    Write-Host "❌ TEST-REGS-001 FAILED" -ForegroundColor Red
    exit 1
} else {
    Write-Host "✅ TEST-REGS-001 PASSED" -ForegroundColor Green
    exit 0
}
