# .\tools\archive\deprecated\FullSuite.ps1 *>&1 | Tee-Object -FilePath ".\logs\FullSuite_$(Get-Date -Format 'yyyyMMdd_HHmmss').log"


$Configurations = @("Debug", "Release")
$Configuration = "Debug"
foreach ($Configuration in $Configurations){
    .\tools\build\Build-Driver.ps1 -Configuration $Configuration    
    .\tools\build\Build-Tests.ps1 -Configuration $Configuration    
    .\tools\setup\Install-Driver-Elevated.ps1 -Configuration $Configuration -Action Reinstall -CaptureDbgView 

    $scriptDir = $PSScriptRoot  #  .\tools\archive\deprecated\
    $ArchiveDir = Split-Path $scriptDir -Parent  # .\tools\archive\
    $toolsDir = Split-Path $ArchiveDir -Parent  # .\tools\
    $repoRoot = Split-Path $toolsDir -Parent  # repository root
    $testExeDir = Join-Path $repoRoot "build\tools\avb_test\x64\$Configuration"



    $CIFailed = @("test_tx_timestamp_provenance.exe",
                    "test_tx_timestamp_retrieval.exe",
                    "test_ioctl_xstamp.exe", 
                    "test_multidev_adapter_enum.exe",
                    "test_ioctl_tas.exe",
                    #"test_atdecc_aen_protocol.exe",
                    "test_atdecc_aen_protocol.exe",
                    "test_ioctl_tas.exe",
                   # "test_tx_timestamp_retrieval.exe",
                    "test_lifecycle_coverage.exe",
                    "test_event_log.exe",
                    "test_ptp_event_latency.exe",
                    "test_control_device_lifecycle.exe",
                    "test_hw_state_machine.exe",
                    "test_ptp_corr_extended.exe",
                    "test_vv_corr_003_crossdomain.exe",
                    "test_ptp_phc_stability.exe")
    foreach ($t in $CIFailed) { 
 #   .\tools\test\Run-Tests-Elevated.ps1 -TestName $t -CaptureDbgView -Configuration $Configuration 
    } 


$BenchmarkTests = @(
    'test_ioctl_phc_crosstimestamp_v2.exe',
    'test_ioctl_xstamp_v2.exe',
    'ptp_clock_control_test.exe',      # TC-5b PHC/QPC coherence; TC-5b runs first in main()
    'test_timestamp_latency.exe',      # P50/P95/P99 latency; no --case filter
    'test_tx_timestamp_retrieval.exe', # TC-CORR-TX-RX-001 per adapter; no --case filter
    'test_ptp_phc_stability.exe',      # UT-CORR-007+008 (PRE_SEND_PHC overhead); no --case needed
    'test_ptp_001.exe'                # TC-PTP-001-CORR rate stability; no --case filter
)
    foreach ($t in $BenchmarkTests) { 
    .\tools\test\Run-Tests-Elevated.ps1 -TestName $t -CaptureDbgView -Configuration $Configuration 
    }
    # tools\test\Run-Tests-CI.ps1
    $CISuite = @("Unit", "Integration",  "HardwareUnit", "HardwareIntegration", "HardwareMandatory", "HardwareMutation")
    #$CISuite =@("All")
    foreach ($Suite in $CISuite) { 
    #.\tools\test\Run-Tests-CI.ps1 -Suite $Suite -CaptureDbgView -Configuration $Configuration 
    } 

    #$FullSuite = Get-ChildItem -Path $testExeDir -Filter "*.exe" -ErrorAction SilentlyContinue
    #foreach ($t in $FullSuite) { 
    # .\tools\test\Run-Tests-Elevated.ps1 -TestName $t -CaptureDbgView -Configuration $Configuration 
    #} 

}