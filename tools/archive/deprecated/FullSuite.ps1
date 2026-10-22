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



    $CIFailed = @(
    # CI Test Summary -- HardwareMandatory suite (Debug)
        #test_ioctl_version.exe               PASSED    0
        #test_ioctl_routing.exe               PASSED    0
        #test_ioctl_simple.exe                PASSED    0
        #test_minimal_ioctl.exe               PASSED    0
        #test_ioctl_abi.exe                   PASSED    0
        #test_tx_timestamp_provenance.exe     PASSED    0
        "test_tx_timestamp_retrieval.exe",#      FAILED    1
        #test_ioctl_phc_query.exe             PASSED    0
        #test_ioctl_xstamp.exe                PASSED    0
        "test_ioctl_phc_crosstimestamp_v2.exe",# FAILED    1
        "ptp_clock_control_test.exe",#           PASSED    0
        #test_all_adapters.exe                PASSED    0
        #test_multidev_adapter_enum.exe       PASSED    0
        #test_multi_adapter_phc_sync.exe      PASSED    0
        #test_send_ptp_debug.exe              PASSED    0
        #test_rx_timestamp.exe                PASSED    0
        #test_rx_timestamp_complete.exe       PASSED    0

        #================================================================
        #  CI Test Summary -- HardwareUnit suite (Debug)
        #================================================================
        #Name                             Status          Exit
        #----                             ------          ----
        #test_cleanup_archive.exe         PASSED             0
        #test_ioctl_simple.exe            PASSED             0
        #test_ioctl_routing.exe           PASSED             0
        #test_ioctl_trace.exe             PASSED             0
        #test_minimal_ioctl.exe           PASSED             0
        #test_clock_config.exe            PASSED             0
        #test_clock_working.exe           PASSED             0
        #test_direct_clock.exe            PASSED             0
        #test_hal_performance.exe         PASSED             0
        #test_atdecc_event.exe            PASSED             0
        "test_atdecc_aen_protocol.exe",#     FAILED(TIMEOUT) -999
        #test_ioctl_version.exe           PASSED             0
        #test_mdio_phy.exe                PASSED             0
        #test_dev_lifecycle.exe           PASSED             0
        #test_ts_event_sub.exe            PASSED             0
        #test_ptp_getset.exe              PASSED             0
        #test_power_management.exe        PASSED             0
        #test_tx_timestamp_provenance.exe PASSED             0
        #test_ptp_freq.exe                PASSED             0
        #test_ptp_freq_complete.exe       PASSED             0
        #test_hw_ts_ctrl.exe              PASSED             0
        #test_rx_timestamp.exe            PASSED             0
        #test_rx_timestamp_complete.exe   PASSED             0
        #test_ioctl_target_time.exe       PASSED             0
        #test_qav_cbs.exe                 PASSED             0
        "test_ioctl_tas.exe", #               FAILED             3
        #test_ioctl_fp_ptm.exe            PASSED             0
        "test_tx_timestamp_retrieval.exe", #  FAILED             1
        #test_ioctl_phc_query.exe         PASSED             0
        #test_ioctl_offset.exe            PASSED             0
        #test_ioctl_phc_epoch.exe         PASSED             0
        #test_ioctl_phc_monotonicity.exe  PASSED             0
        #test_ioctl_xstamp.exe            PASSED             0
        #test_ioctl_xstamp_v2.exe         PASSED             0
        #test_ioctl_abi.exe               PASSED             0
        #test_ioctl_access_control.exe    PASSED             0
        #test_ioctl_buffer_fuzz.exe       PASSED             0
        #"test_tsn_ioctl_handlers.exe", #      SKIPPED          N/A
        #ptp_clock_control_test.exe       PASSED             0
        #test_ptp_crosstimestamp.exe      PASSED             0
        #test_zero_polling_overhead.exe   PASSED             0
        #"test_lifecycle_coverage.exe", #      FAILED             1
        #test_hw_state.exe                PASSED             0
        #test_statistics_counters.exe     PASSED             0
        "test_event_log.exe", #               FAILED             1
        #test_build_sign.exe              PASSED             0
        #test_compat_win11.exe            PASSED             0
        #test_win7_stub.exe               PASSED             0
        #test_eee_lpi.exe                 PASSED             0
        #test_pfc_pause.exe               PASSED             0
        #test_srp_interface.exe           PASSED             0
        #test_vlan_tag.exe                PASSED             0
        #test_vlan_pcp_tc_mapping.exe     PASSED             0
        #test_security_validation.exe     PASSED             0
        #test_ioctl_launch_time.exe       PASSED             0
        #test_avtp_tu_bit_events.exe      PASSED             0
        "test_ptp_event_latency.exe" #       FAILED             1
        #test_event_latency_4ch.exe       PASSED             0
        #test_event_nf_zero_polling.exe   PASSED             0
        #test_event_nf_latency.exe        PASSED             0
        #test_gptp_phc_interface.exe      PASSED             0
        #test_send_ptp_debug.exe          PASSED             0
        #test_timestamp_latency.exe       PASSED             0
        #test_perf_regression.exe         PASSED             0
        #test_ndis_fastpath_latency.exe   PASSED             0
        #test_hot_plug.exe                PASSED             0
        #test_error_recovery.exe          PASSED             0
        #test_multi_adapter_phc_sync.exe  PASSED             0
        #test_tx_timestamp_provenance.exe PASSED             0

        #================================================================
        #  CI Test Summary -- HardwareUnit suite (Release)
        #================================================================
        #Name                             Status          Exit
        #----                             ------          ----
        #test_cleanup_archive.exe         PASSED             0
        #test_ioctl_simple.exe            PASSED             0
        #test_ioctl_routing.exe           PASSED             0
        #test_ioctl_trace.exe             PASSED             0
        #test_minimal_ioctl.exe           PASSED             0
        #test_clock_config.exe            PASSED             0
        #test_clock_working.exe           PASSED             0
        #test_direct_clock.exe            PASSED             0
        #test_hal_performance.exe         PASSED             0
        #test_atdecc_event.exe            PASSED             0
        # dupl test_atdecc_aen_protocol.exe     FAILED(TIMEOUT) -999
        #test_ioctl_version.exe           PASSED             0
        #test_mdio_phy.exe                PASSED             0
        #test_dev_lifecycle.exe           PASSED             0
        #test_ts_event_sub.exe            PASSED             0
        #test_ptp_getset.exe              PASSED             0
        #test_power_management.exe        PASSED             0
        #test_tx_timestamp_provenance.exe PASSED             0
        #test_ptp_freq.exe                PASSED             0
        #test_ptp_freq_complete.exe       PASSED             0
        #test_hw_ts_ctrl.exe              PASSED             0
        #test_rx_timestamp.exe            PASSED             0
        #test_rx_timestamp_complete.exe   PASSED             0
        #test_ioctl_target_time.exe       PASSED             0
        #test_qav_cbs.exe                 PASSED             0
        #test_ioctl_tas.exe               FAILED             3
        #test_ioctl_fp_ptm.exe            PASSED             0
       # "test_tx_timestamp_retrieval.exe", #  FAILED             1
        #test_ioctl_phc_query.exe         PASSED             0
        #test_ioctl_offset.exe            PASSED             0
        #test_ioctl_phc_epoch.exe         PASSED             0
        #test_ioctl_phc_monotonicity.exe  PASSED             0
        #test_ioctl_xstamp.exe            PASSED             0
        #test_ioctl_xstamp_v2.exe         PASSED             0
        #test_ioctl_abi.exe               PASSED             0
        #test_ioctl_access_control.exe    PASSED             0
        #test_ioctl_buffer_fuzz.exe       PASSED             0
        "test_tsn_ioctl_handlers.exe", #      SKIPPED          N/A
        #ptp_clock_control_test.exe       PASSED             0
        #test_ptp_crosstimestamp.exe      PASSED             0
        #test_zero_polling_overhead.exe   PASSED             0
        "test_lifecycle_coverage.exe", #      FAILED             1
        #test_hw_state.exe                PASSED             0
        #test_statistics_counters.exe     PASSED             0
        #test_event_log.exe               FAILED             1
        #test_build_sign.exe              PASSED             0
        #test_compat_win11.exe            PASSED             0
        #test_win7_stub.exe               PASSED             0
        #test_eee_lpi.exe                 PASSED             0
        #test_pfc_pause.exe               PASSED             0
        #test_srp_interface.exe           PASSED             0
        #test_vlan_tag.exe                PASSED             0
        #test_vlan_pcp_tc_mapping.exe     PASSED             0
        #test_security_validation.exe     PASSED             0
        #test_ioctl_launch_time.exe       PASSED             0
        #test_avtp_tu_bit_events.exe      PASSED             0
        #test_ptp_event_latency.exe       FAILED             1
        #test_event_latency_4ch.exe       PASSED             0
        #test_event_nf_zero_polling.exe   PASSED             0
        #test_event_nf_latency.exe        PASSED             0
        #test_gptp_phc_interface.exe      PASSED             0
        #test_send_ptp_debug.exe          PASSED             0
        #test_timestamp_latency.exe       PASSED             0
        #test_perf_regression.exe         PASSED             0
        #test_ndis_fastpath_latency.exe   PASSED             0
        #test_hot_plug.exe                PASSED             0
        #test_error_recovery.exe          PASSED             0
        #test_multi_adapter_phc_sync.exe  PASSED             0
        #test_tx_timestamp_provenance.exe PASSED             0


        #================================================================
        #  CI Test Summary -- HardwareIntegration suite (Debug)
        #================================================================
        #
        #Name                              Status  Exit
        #----                              ------  ----
        #ssot_register_validation_test.exe PASSED     0
        #test_tsn_ioctl_handlers_um.exe    PASSED     0
        #test_all_adapters.exe             PASSED     0
        #test_multidev_adapter_enum.exe    PASSED     0
        #test_device_register_access.exe   PASSED     0
        #test_ndis_send_path.exe           PASSED     0
        #test_ndis_receive_path.exe        PASSED     0
        "test_hw_state_machine.exe", #         FAILED     1
        #test_lazy_initialization.exe      PASSED     0
        #test_registry_diagnostics.exe     PASSED     0
        #test_ptp_corr_extended.exe        PASSED     0
        #test_vv_corr_003_crossdomain.exe  PASSED     0
        #test_ptp_001.exe                  PASSED     0
        #test_ptp_corr.exe                 PASSED     0
        "test_ptp_phc_stability.exe", #        FAILED     1
        "ptp_clock_control_production.exe", #  SKIPPED  N/A
        #ptp_ioctl_latency_test.exe        PASSED     0
        "hw_timestamping_control.exe", #       SKIPPED  N/A
        "rx_timestamping.exe" #               SKIPPED  N/A
        #tsauxc_toggle_test.exe            PASSED     0
        #test_gptp_daemon_coexist.exe      PASSED     0
        #avb_test_i219.exe                 PASSED     0
        #avb_test_portability.exe          PASSED     0
        #avb_test_i217.exe                 PASSED     0
        #avb_test_i225.exe                 PASSED     0
        #avb_test_i226_um.exe              PASSED     0
        #test_hardware_detection.exe       PASSED     0


        #================================================================
        #  CI Test Summary -- HardwareIntegration suite (Release)
        #================================================================
        #
        #Name                              Status  Exit
        #----                              ------  ----
        #ssot_register_validation_test.exe PASSED     0
        #test_tsn_ioctl_handlers_um.exe    PASSED     0
        #test_all_adapters.exe             PASSED     0
        #test_multidev_adapter_enum.exe    PASSED     0
        #test_device_register_access.exe   PASSED     0
        #test_ndis_send_path.exe           PASSED     0
        #test_ndis_receive_path.exe        PASSED     0
        #test_hw_state_machine.exe         FAILED     1
        #test_lazy_initialization.exe      PASSED     0
        #test_registry_diagnostics.exe     PASSED     0
        #test_ptp_corr_extended.exe        PASSED     0
        #test_vv_corr_003_crossdomain.exe  PASSED     0
        #test_ptp_001.exe                  PASSED     0
        #test_ptp_corr.exe                 PASSED     0
        #test_ptp_phc_stability.exe        FAILED     1
        #ptp_clock_control_production.exe  SKIPPED  N/A
        #ptp_ioctl_latency_test.exe        PASSED     0
        #hw_timestamping_control.exe       SKIPPED  N/A
        #rx_timestamping.exe               SKIPPED  N/A
        #tsauxc_toggle_test.exe            PASSED     0
        #test_gptp_daemon_coexist.exe      PASSED     0
        #avb_test_i219.exe                 PASSED     0
        #avb_test_portability.exe          PASSED     0
        #avb_test_i217.exe                 PASSED     0
        #avb_test_i225.exe                 PASSED     0
        #avb_test_i226_um.exe              PASSED     0
        #test_hardware_detection.exe       PASSED     0
    )
    foreach ($t in $CIFailed) { 
   # .\tools\test\Run-Tests-Elevated.ps1 -TestName $t -CaptureDbgView -Configuration $Configuration 
    } 


$BenchmarkTests = @(
#    'test_ioctl_phc_crosstimestamp_v2.exe',
    'test_ioctl_xstamp_v2.exe',
    'ptp_clock_control_test.exe'      # TC-5b PHC/QPC coherence; TC-5b runs first in main()
#    'test_timestamp_latency.exe',      # P50/P95/P99 latency; no --case filter
#    'test_tx_timestamp_retrieval.exe', # TC-CORR-TX-RX-001 per adapter; no --case filter
#    'test_ptp_phc_stability.exe',      # UT-CORR-007+008 (PRE_SEND_PHC overhead); no --case needed
#    'test_ptp_001.exe'                # TC-PTP-001-CORR rate stability; no --case filter
)
    foreach ($t in $BenchmarkTests) { 
#    .\tools\test\Run-Tests-Elevated.ps1 -TestName $t -CaptureDbgView -Configuration $Configuration 
    }


    $CISuiteFailed = @()
    # tools\test\Run-Tests-CI.ps1
    #$CISuite = @("Unit", "Integration",  "HardwareUnit", "HardwareIntegration", "HardwareMandatory", "HardwareMutation")
    #
    $CISuite = @("HardwareUnit", "HardwareIntegration", "HardwareMutation", "HardwareMandatory")
    #$CISuite =@("All")
   # foreach ($Suite in $CISuite) { 
   #     $SuiteStartTime = Get-Date
   #     .\tools\test\Run-Tests-CI.ps1 -Suite $Suite -CaptureDbgView -Configuration $Configuration 
   #     $SuiteEndTime = Get-Date

   #     find failed tests in .\logs for this suite and add names of tests that failed to $CISuiteFailed
   # } 

    #foreach ($t in $CISuiteFailed) { 
    #    .\tools\test\Run-Tests-Elevated.ps1 -TestName $t -CaptureDbgView -Configuration $Configuration 
    #} 

    $FullSuite = Get-ChildItem -Path $testExeDir -Filter "*.exe" -ErrorAction SilentlyContinue
    $BlackList = @("test_s3_sleep_wake.exe","test_vv_corr_001_stability.exe")
    
    foreach ($t in $FullSuite) { 
        if ($BlackList -contains $t.Name) { continue }
        .\tools\test\Run-Tests-Elevated.ps1 -TestName $t -CaptureDbgView -Configuration $Configuration 
    } 

}