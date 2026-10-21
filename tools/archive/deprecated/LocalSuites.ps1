
# .\tools\archive\deprecated\LocalSuites.ps1 *>&1 | Tee-Object -FilePath ".\logs\FullSuite_$(Get-Date -Format 'yyyyMMdd_HHmmss').log"


$Configurations = @("Debug", "Release")
$CISuiteFailed = @()

foreach ($Configuration in $Configurations) {

    # 1. Build the selected configuration
    $LASTEXITCODE = $null
    .\tools\build\Build-Driver.ps1 -Configuration $Configuration
    if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) {
        throw "Driver build failed: $Configuration"
    }

    $LASTEXITCODE = $null
    .\tools\build\Build-Tests.ps1 -Configuration $Configuration
    if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) {
        throw "Test build failed: $Configuration"
    }

    # 2. Install the driver for this configuration
    $LASTEXITCODE = $null
    .\tools\setup\Install-Driver-Elevated.ps1 `
        -Configuration $Configuration `
        -Action Reinstall `
        -CaptureDbgView

    if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) {
        throw "Driver installation failed: $Configuration"
    }

    if ((Get-Service IntelAvbFilter -ErrorAction Stop).Status -ne "Running") {
        throw "IntelAvbFilter is not running after $Configuration installation"
    }

    # 3. Execute the existing CI suites
    $CISuite = @(
        "HardwareUnit",
        "HardwareIntegration",
        "HardwareMutation",
        "HardwareMandatory"
    )

    foreach ($Suite in $CISuite) {

        $SuiteStartTime = Get-Date
        $SuiteLog = ".\logs\FullSuite_${Configuration}_${Suite}_$($SuiteStartTime.ToString('yyyyMMdd_HHmmss')).log"

        # Capture the existing console summary, without creating folders
        Start-Transcript -Path $SuiteLog -Force | Out-Null
        try {
            $LASTEXITCODE = $null
            .\tools\test\Run-Tests-CI.ps1 `
                -Suite $Suite `
                -CaptureDbgView `
                -Configuration $Configuration

            $SuiteExit = $LASTEXITCODE
        }
        finally {
            Stop-Transcript | Out-Null
        }

        $SuiteEndTime = Get-Date

        # 4. Extract failures from THIS suite's summary
        $Lines = @(Get-Content -LiteralPath $SuiteLog)

        $Header = "CI Test Summary -- $Suite suite ($Configuration)"
        $Start = -1

        for ($i = 0; $i -lt $Lines.Count; $i++) {
            if ($Lines[$i].Contains($Header)) {
                $Start = $i
            }
        }

        $NewFailures = @()

        if ($Start -ge 0) {
            for ($i = $Start + 1; $i -lt $Lines.Count; $i++) {

                if ($Lines[$i] -match '^\s*Passed\s*:') {
                    break
                }

                # Existing Format-Table columns: Name, Status, Exit
                if ($Lines[$i] -match '^\s*(?<Test>.+?)\s{2,}(?<Status>FAILED(?:\([^)]*\))?|BLOCKED(?:\([^)]*\))?|INCOMPLETE(?:\([^)]*\))?|CLEANUP_FAILED|INFRA_FAILURE)\s+(?<Exit>-?\d+|N/A)\s*$') {

                    $NewFailures += [PSCustomObject]@{
                        Configuration = $Configuration
                        Suite         = $Suite
                        TestName      = $Matches.Test.Trim()
                        Status        = $Matches.Status
                        ExitCode      = $Matches.Exit
                        LogFile       = $SuiteLog
                    }
                }
            }
        }

        $CISuiteFailed += $NewFailures

        Write-Host "`n$Configuration / $Suite"
        Write-Host "Exit: $SuiteExit"
        Write-Host "Duration: $(($SuiteEndTime - $SuiteStartTime).ToString())"
        Write-Host "Failures: $($NewFailures.Count)"

        # Do not silently continue if output could not be interpreted
        if ($Start -lt 0 -or
            ($SuiteExit -ne 0 -and $NewFailures.Count -eq 0)) {
            throw "Suite summary incomplete or unparseable: $SuiteLog"
        }

        # Do not start another suite after uncertain hardware state
        if (@($NewFailures | Where-Object {
            $_.Status -match 'TIMEOUT|INFRA|CLEANUP_FAILED'
        }).Count -gt 0) {
            throw "Hardware state requires inspection after $Suite"
        }
    }
    # 5. Final combined failure inventory
    $CISuiteFailed |
        Sort-Object Configuration, Suite, TestName |
        Format-Table Configuration, Suite, TestName, Status, ExitCode -AutoSize

    foreach ($t in $CISuiteFailed) {

        # Only ordinary executable failures can be rerun this way.
        # Target-specific arguments must be handled separately.
        if ($t.Status -ne "FAILED") { continue }
        if ($t.TestName -notmatch '^[\w.-]+\.exe$') { continue }

        Write-Host "`n$($t.Configuration) / $($t.Suite) / $($t.TestName)"

        $Answer = Read-Host "Rerun using Run-Tests-Elevated.ps1? (y/N)"

        if ($Answer -eq "y") {
            .\tools\test\Run-Tests-Elevated.ps1 `
                -TestName $t.TestName `
                -CaptureDbgView `
                -Configuration $t.Configuration
        }
    }

}

