# Treiber-Signierung f�r Intel AVB Filter

## Schritt 1: Testsignatur erstellen

# Calculate repository root from script location
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
if ($ScriptDir -match '\\tools\\build$') {
    # Script is in tools/build/ subdirectory
    $RepoRoot = Split-Path (Split-Path $ScriptDir -Parent) -Parent
} else {
    # Script is in repository root
    $RepoRoot = $ScriptDir
}

# Test-Zertifikat erstellen (nur f�r Entwicklung!)
$certPath = Join-Path $RepoRoot "IntelAvbTestCert.cer"
$pvkPath = Join-Path $RepoRoot "IntelAvbTestCert.pvk"

Write-Host "Creating test certificate for driver signing..." -ForegroundColor Yellow

# Finde makecert.exe
$WDKPaths = @(
    "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\makecert.exe",
    "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\makecert.exe",
    "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\makecert.exe"
)

$MakecertPath = $null
foreach ($Path in $WDKPaths) {
    if (Test-Path $Path) {
        $MakecertPath = $Path
        break
    }
}

if (-not $MakecertPath) {
    Write-Error "makecert.exe not found. Please ensure Windows Driver Kit is installed."
    exit 1
}

# Erstelle Test-Zertifikat
Write-Host "Using makecert: $MakecertPath" -ForegroundColor Green
& $MakecertPath -r -pe -ss PrivateCertStore -n "CN=Intel AVB Filter Test Certificate" -eku 1.3.6.1.5.5.7.3.3 $certPath

if ($LASTEXITCODE -eq 0) {
    Write-Host "Test certificate created successfully!" -ForegroundColor Green
} else {
    Write-Error "Failed to create test certificate"
    exit 1
}

# Signiere die CAT-Datei
$signtoolPaths = @(
    "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe",
    "C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\signtool.exe",
    "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe"
)

$SigntoolPath = $null
foreach ($Path in $signtoolPaths) {
    if (Test-Path $Path) {
        $SigntoolPath = $Path
        break
    }
}

if (-not $SigntoolPath) {
    Write-Error "signtool.exe not found"
    exit 1
}

Write-Host "Signing CAT file..." -ForegroundColor Yellow
$CATFile = Join-Path $RepoRoot "build\x64\Debug\IntelAvbFilter.cat"

& $SigntoolPath sign /a /s PrivateCertStore /n "Intel AVB Filter Test Certificate" /fd SHA256 /t http://timestamp.verisign.com/scripts/timstamp.dll $CATFile

if ($LASTEXITCODE -eq 0) {
    Write-Host "CAT file signed successfully!" -ForegroundColor Green
} else {
    Write-Error "Failed to sign CAT file"
    exit 1
}

Write-Host "Driver signing completed! Next steps:" -ForegroundColor Green
Write-Host "1. Enable test signing: bcdedit /set testsigning on" -ForegroundColor Yellow
Write-Host "2. Restart computer" -ForegroundColor Yellow  
Write-Host "3. Install certificate to Trusted Root: certmgr.msc" -ForegroundColor Yellow
Write-Host "4. Install driver: pnputil /add-driver IntelAvbFilter.inf /install" -ForegroundColor Yellow