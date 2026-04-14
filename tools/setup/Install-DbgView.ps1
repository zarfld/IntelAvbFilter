# Install-DbgView.ps1
# Downloads DebugView (Sysinternals) and extracts it to .github\skills\DbgView\DebugView\.
# The binaries are listed in .gitignore and must NOT be committed to the repository.
#
# Usage (run once per developer machine):
#   .\tools\setup\Install-DbgView.ps1
#
# The script is idempotent: if the binary is already present it reports success and exits.

$ErrorActionPreference = 'Stop'

# ── Paths ──────────────────────────────────────────────────────────────────────
$repoRoot   = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$destDir    = Join-Path $repoRoot ".github\skills\DbgView\DebugView"
$destExe    = Join-Path $destDir "Dbgview.exe"
$zipPath    = Join-Path $env:TEMP "DebugView.zip"
$downloadUrl = "https://download.sysinternals.com/files/DebugView.zip"

# ── Already installed? ─────────────────────────────────────────────────────────
if (Test-Path $destExe) {
    Write-Host "[Install-DbgView] Already installed at: $destExe" -ForegroundColor Green
    exit 0
}

# ── Verify .gitignore covers the destination ───────────────────────────────────
$gitignorePath = Join-Path $repoRoot ".gitignore"
if (Test-Path $gitignorePath) {
    $content = Get-Content $gitignorePath -Raw
    if ($content -notmatch 'DbgView/DebugView') {
        Write-Warning (".gitignore does not appear to exclude .github/skills/DbgView/DebugView/`n" +
                      "Add the binaries to .gitignore before committing!")
    } else {
        Write-Host "[Install-DbgView] .gitignore already covers DebugView binaries." -ForegroundColor Green
    }
}

# ── Download ───────────────────────────────────────────────────────────────────
Write-Host "[Install-DbgView] Downloading DebugView from Sysinternals..." -ForegroundColor Cyan
Write-Host "  URL : $downloadUrl"
Write-Host "  Dest: $zipPath"

try {
    # Use TLS 1.2 — required for download.sysinternals.com
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $downloadUrl -OutFile $zipPath -UseBasicParsing
} catch {
    Write-Error ("Download failed: $_`n" +
                 "Download manually from https://learn.microsoft.com/sysinternals/downloads/debugview`n" +
                 "and extract to: $destDir")
    exit 1
}

# ── Extract ────────────────────────────────────────────────────────────────────
Write-Host "[Install-DbgView] Extracting to: $destDir"
if (-not (Test-Path $destDir)) {
    New-Item -ItemType Directory -Force -Path $destDir | Out-Null
}

Expand-Archive -Path $zipPath -DestinationPath $destDir -Force
Remove-Item $zipPath -Force

# ── Verify ────────────────────────────────────────────────────────────────────
if (Test-Path $destExe) {
    Write-Host "[Install-DbgView] Installation complete: $destExe" -ForegroundColor Green
    Write-Host ""
    Write-Host "  Next: run DebugView once manually (elevated) and enable" -ForegroundColor Yellow
    Write-Host "        Capture -> Capture Kernel to persist the setting." -ForegroundColor Yellow
    Write-Host "  See: .github\skills\DbgView\SKILL.md for full setup guidance." -ForegroundColor Yellow
} else {
    Write-Error "Extraction succeeded but $destExe not found. Check archive contents in $destDir."
    exit 1
}
