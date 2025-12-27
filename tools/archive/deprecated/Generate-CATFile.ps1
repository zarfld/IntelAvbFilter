# Intel AVB Filter CAT File Generation Script
# This script automatically generates the catalog file required for driver signing

param(
    [Parameter(Mandatory=$false)]
    [string]$Configuration = "Debug",
    
    [Parameter(Mandatory=$false)]
    [string]$Platform = "x64"
)

Write-Host "Generating CAT file for Intel AVB Filter Driver..." -ForegroundColor Green

# Calculate repository root from script location
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
if ($ScriptDir -match '\\tools\\build$') {
    # Script is in tools/build/ subdirectory
    $ProjectRoot = Split-Path (Split-Path $ScriptDir -Parent) -Parent
} else {
    # Script is in repository root
    $ProjectRoot = $ScriptDir
}

# Define paths - CORRECTED: Use the correct output directory structure
$CDFFile = Join-Path $ProjectRoot "IntelAvbFilter.cdf"
# FIXED: The correct path is x64\Debug, not IntelAvbFilter\x64\Debug
$OutputDir = Join-Path $ProjectRoot "$Platform\$Configuration"

# Ensure output directory exists
if (!(Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

Write-Host "Target directory: $OutputDir" -ForegroundColor Yellow

# Find makecat.exe - try multiple possible locations
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
    Write-Error "makecat.exe not found in common WDK locations. Please ensure Windows Driver Kit is installed."
    Write-Host "Searched paths:"
    $WDKPaths | ForEach-Object { Write-Host "  $_" }
    exit 1
}

Write-Host "Using makecat: $MakecatPath" -ForegroundColor Yellow

# Update CDF file with correct paths
$CDFContent = @"
[CatalogHeader]
Name=IntelAvbFilter.cat
ResultDir=$OutputDir
PublicVersion=0x0000001
EncodingType=0x00010001
CATATTR1=0x10010001:OSAttr:2:10.0

[CatalogFiles]
<hash>IntelAvbFilter.inf=IntelAvbFilter.inf
"@

Set-Content -Path $CDFFile -Value $CDFContent -Encoding ASCII

# Generate CAT file
Write-Host "Generating catalog file..." -ForegroundColor Yellow
try {
    & $MakecatPath $CDFFile
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Successfully generated IntelAvbFilter.cat" -ForegroundColor Green
        
        # Verify the file was created
        $CATFile = Join-Path $OutputDir "IntelAvbFilter.cat"
        if (Test-Path $CATFile) {
            $FileInfo = Get-Item $CATFile
            Write-Host "CAT file created: $($FileInfo.FullName) ($($FileInfo.Length) bytes)" -ForegroundColor Green
        } else {
            Write-Warning "CAT file was not found at expected location: $CATFile"
        }
    } else {
        Write-Error "makecat.exe failed with exit code: $LASTEXITCODE"
        exit $LASTEXITCODE
    }
} catch {
    Write-Error "Failed to run makecat.exe: $($_.Exception.Message)"
    exit 1
}

Write-Host "CAT file generation completed successfully!" -ForegroundColor Green