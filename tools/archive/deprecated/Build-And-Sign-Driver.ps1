<#
.SYNOPSIS
  Legacy build+sign script kept for compatibility.

.DESCRIPTION
  Thin wrapper around the canonical build+sign script:
    tools/build/Build-And-Sign.ps1

  Issue: #27
#>

param(
    [Parameter(Mandatory=$false)]
    [string]$Configuration = "Debug",
    
    [Parameter(Mandatory=$false)]
    [string]$Platform = "x64",
    
    [Parameter(Mandatory=$false)]
    [string]$CertificateName = "Intel AVB Filter Test Certificate",
    
    [Parameter(Mandatory=$false)]
    [switch]$SkipSigning = $false
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$canonical = Join-Path $scriptDir "Build-And-Sign.ps1"

Write-Host "Build-And-Sign-Driver.ps1 -> Build-And-Sign.ps1" -ForegroundColor DarkGray

& $canonical -Configuration $Configuration -Platform $Platform -CertificateName $CertificateName -SkipSigning:$SkipSigning
exit $LASTEXITCODE

# ===========================
# STEP 1: Find WDK Tools
# ===========================
Write-Host "STEP 1: Locating Windows Driver Kit tools..." -ForegroundColor Yellow

# Find makecat.exe
$WDKPaths = @(
    "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\makecat.exe",
    "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\makecat.exe",
    "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\makecat.exe"
)

$MakecatPath = $null
foreach ($Path in $WDKPaths) {
    if (Test-Path $Path) {
        $MakecatPath = $Path
        break
    }
}

if (-not $MakecatPath) {
    Write-Error "makecat.exe not found. Please ensure Windows Driver Kit is installed."
    Write-Host "Searched paths:"
    $WDKPaths | ForEach-Object { Write-Host "  $_" }
    exit 1
}

Write-Host "? Found makecat: $MakecatPath" -ForegroundColor Green

# Find inf2cat.exe
$Inf2CatPath = $null
$Inf2CatPaths = @(
    "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\inf2cat.exe",
    "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\inf2cat.exe",
    "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\inf2cat.exe"
)
foreach ($p in $Inf2CatPaths) {
    if (Test-Path $p) { $Inf2CatPath = $p; break }
}
if ($Inf2CatPath) {
    Write-Host "? Found inf2cat: $Inf2CatPath" -ForegroundColor Green
} else {
    Write-Host "! inf2cat.exe not found - will fallback to makecat with CDF" -ForegroundColor Yellow
}

# Find signtool.exe (nur wenn nicht �bersprungen)
$SigntoolPath = $null
if (-not $SkipSigning) {
    $SigntoolPaths = @(
        "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe",
        "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\signtool.exe",
        "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe"
    )

    foreach ($Path in $SigntoolPaths) {
        if (Test-Path $Path) {
            $SigntoolPath = $Path
            break
        }
    }

    if (-not $SigntoolPath) {
        Write-Error "signtool.exe not found. Please ensure Windows Driver Kit is installed."
        exit 1
    }

    Write-Host "? Found signtool: $SigntoolPath" -ForegroundColor Green
}

# ===========================
# STEP 2: Setup Paths
# ===========================
Write-Host ""
Write-Host "STEP 2: Setting up paths..." -ForegroundColor Yellow

# Calculate repository root from script location
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
if ($ScriptDir -match '\\tools\\build$') {
    # Script is in tools/build/ subdirectory
    $ProjectRoot = Split-Path (Split-Path $ScriptDir -Parent) -Parent
} else {
    # Script is in repository root
    $ProjectRoot = $ScriptDir
}
$OutputDir = Join-Path $ProjectRoot "$Platform\$Configuration"
$CDFFile = Join-Path $OutputDir "IntelAvbFilter.cdf"   # Place CDF in output dir so member files resolve
$CATFile = Join-Path $OutputDir "IntelAvbFilter.cat"
$INFFile = Join-Path $OutputDir "IntelAvbFilter.inf"
$SYSFile = Join-Path $OutputDir "IntelAvbFilter.sys"

# Ensure output directory exists
if (!(Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
    Write-Host "? Created output directory: $OutputDir" -ForegroundColor Green
} else {
    Write-Host "? Output directory exists: $OutputDir" -ForegroundColor Green
}

# Sanity check for INF/SYS presence
if (!(Test-Path $INFFile)) {
    Write-Error "INF file not found: $INFFile"
    exit 1
}
if (!(Test-Path $SYSFile)) {
    Write-Error "SYS file not found: $SYSFile"
    exit 1
}

# ===========================
# STEP 3/4: Generate CAT File
# ===========================
Write-Host ""
Write-Host "STEP 3-4: Generating Catalog File (CAT)..." -ForegroundColor Yellow

$catGenerated = $false

if ($Inf2CatPath) {
    try {
        # Use inf2cat to include INF + SYS hashes properly
        & "$Inf2CatPath" /driver:"$OutputDir" /os:10_X64 | Write-Host
        if ($LASTEXITCODE -eq 0) {
            $catGenerated = $true
            Write-Host "? Successfully generated CAT file with inf2cat" -ForegroundColor Green
        } else {
            Write-Host "! inf2cat failed with exit code $LASTEXITCODE - will fallback to makecat" -ForegroundColor Yellow
        }
    } catch {
        Write-Host "! inf2cat threw exception - will fallback to makecat: $($_.Exception.Message)" -ForegroundColor Yellow
    }
}

if (-not $catGenerated) {
    Write-Host "Using makecat with CDF fallback..." -ForegroundColor Yellow
    # Create CDF including both INF and SYS; use ABSOLUTE paths to avoid any resolution issues
    $CDFContent = @"
[CatalogHeader]
Name=IntelAvbFilter.cat
ResultDir=$OutputDir
PublicVersion=0x00000001
EncodingType=0x00010001
CATATTR1=0x10010001:OSAttr:2:10.0

[CatalogFiles]
<hash>IntelAvbFilter.inf=$INFFile
<hash>IntelAvbFilter.sys=$SYSFile
"@
    Set-Content -Path $CDFFile -Value $CDFContent -Encoding ASCII
    Write-Host "? Created CDF file: $CDFFile" -ForegroundColor Green

    try {
        & "$MakecatPath" "$CDFFile"
        if ($LASTEXITCODE -eq 0) {
            $catGenerated = $true
            Write-Host "? Successfully generated CAT file with makecat" -ForegroundColor Green
        } else {
            Write-Error "makecat.exe failed with exit code: $LASTEXITCODE"
            exit $LASTEXITCODE
        }
    } catch {
        Write-Error "Failed to run makecat.exe: $($_.Exception.Message)"
        exit 1
    }
}

# Verify the file was created
if (Test-Path $CATFile) {
    $FileInfo = Get-Item $CATFile
    Write-Host "  ?? CAT file: $($FileInfo.FullName)" -ForegroundColor Cyan
    Write-Host "  ?? Size: $($FileInfo.Length) bytes" -ForegroundColor Cyan
} else {
    Write-Error "CAT file was not created at expected location: $CATFile"
    exit 1
}

# ===========================
# STEP 5: Create/Use Test Certificate
# ===========================
if (-not $SkipSigning) {
    Write-Host ""
    Write-Host "STEP 5: Checking for test certificate..." -ForegroundColor Yellow

    # Check if certificate exists
    $existingCerts = Get-ChildItem -Path "Cert:\CurrentUser\PrivateCertStore" -ErrorAction SilentlyContinue | 
                    Where-Object { $_.Subject -match $CertificateName }

    if ($existingCerts.Count -eq 0) {
        Write-Host "Creating new test certificate..." -ForegroundColor Yellow
        
        # Find makecert.exe
        $MakecertPaths = @(
            "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\makecert.exe",
            "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\makecert.exe",
            "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\makecert.exe"
        )

        $MakecertPath = $null
        foreach ($Path in $MakecertPaths) {
            if (Test-Path $Path) {
                $MakecertPath = $Path
                break
            }
        }

        if (-not $MakecertPath) {
            Write-Error "makecert.exe not found for certificate creation"
            exit 1
        }

        try {
            & $MakecertPath -r -pe -ss PrivateCertStore -n "CN=$CertificateName" -eku 1.3.6.1.5.5.7.3.3
            if ($LASTEXITCODE -eq 0) {
                Write-Host "? Test certificate created successfully" -ForegroundColor Green
            } else {
                Write-Error "Failed to create test certificate"
                exit 1
            }
        } catch {
            Write-Error "Failed to run makecert.exe: $($_.Exception.Message)"
            exit 1
        }
    } else {
        $newestCert = $existingCerts | Sort-Object NotAfter -Descending | Select-Object -First 1
        Write-Host "? Using existing certificate:" -ForegroundColor Green
        Write-Host "  ?? Subject: $($newestCert.Subject)" -ForegroundColor Cyan
        Write-Host "  ?? Thumbprint: $($newestCert.Thumbprint)" -ForegroundColor Cyan
        Write-Host "  ?? Expires: $($newestCert.NotAfter)" -ForegroundColor Cyan
    }

    # ===========================
    # STEP 6: Sign CAT File
    # ===========================
    Write-Host ""
    Write-Host "STEP 6: Signing CAT file..." -ForegroundColor Yellow

    try {
        & $SigntoolPath sign /a /s PrivateCertStore /n $CertificateName /fd SHA256 $CATFile
        if ($LASTEXITCODE -eq 0) {
            Write-Host "? CAT file signed successfully!" -ForegroundColor Green
        } else {
            Write-Error "Failed to sign CAT file with exit code: $LASTEXITCODE"
            exit $LASTEXITCODE
        }
    } catch {
        Write-Error "Failed to run signtool.exe: $($_.Exception.Message)"
        exit 1
    }
} else {
    Write-Host ""
    Write-Host "STEP 5-6: Skipping certificate creation and signing (SkipSigning = $true)" -ForegroundColor Yellow
}

# ===========================
# STEP 7: Summary & Next Steps
# ===========================
Write-Host ""
Write-Host "=== BUILD & SIGN COMPLETED SUCCESSFULLY! ===" -ForegroundColor Green
Write-Host ""
Write-Host "?? Summary:" -ForegroundColor Cyan
Write-Host "  ? CAT file generated: $CATFile" -ForegroundColor White
if (-not $SkipSigning) {
    Write-Host "  ? CAT file digitally signed" -ForegroundColor White
    Write-Host "  ? Certificate: $CertificateName" -ForegroundColor White
} else {
    Write-Host "  ?? CAT file NOT signed (SkipSigning enabled)" -ForegroundColor Yellow
}
Write-Host "  ? Ready for driver installation" -ForegroundColor White

Write-Host ""
Write-Host "?? Next Steps:" -ForegroundColor Cyan
if (-not $SkipSigning) {
    Write-Host "  1. Enable test signing (as Administrator):" -ForegroundColor Yellow
    Write-Host "     bcdedit /set testsigning on" -ForegroundColor White
    Write-Host "  2. Restart computer" -ForegroundColor Yellow
    Write-Host "  3. Install certificate (as Administrator):" -ForegroundColor Yellow
    Write-Host "     .\Manage-Certificate.ps1" -ForegroundColor White
    Write-Host "  4. Install driver:" -ForegroundColor Yellow
    Write-Host "     pnputil /add-driver IntelAvbFilter.inf /install" -ForegroundColor White
} else {
    Write-Host "  1. Sign the CAT file before installation" -ForegroundColor Yellow
    Write-Host "  2. Enable test signing and install certificate" -ForegroundColor Yellow
    Write-Host "  3. Install driver" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "?? Intel AVB Filter Driver is ready for deployment!" -ForegroundColor Green