# Import-VisualStudioVars.ps1
# Imports Visual Studio environment variables into current PowerShell session
# Avoids cmd.exe line length limitations

param(
    [Parameter(Mandatory=$true)]
    [string]$VcVarsPath
)

if (-not (Test-Path $VcVarsPath)) {
    throw "vcvars64.bat not found at: $VcVarsPath"
}

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
