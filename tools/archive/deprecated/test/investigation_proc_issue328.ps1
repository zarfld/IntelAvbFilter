

#tools\archive\deprecated\test\investigation_proc_issue328.ps1

.\tools\build\Build-Driver.ps1 -Configuration Debug    
.\tools\build\Build-Tests.ps1 -Configuration Debug    
 
$GoodSuite = @( 
      "avb_capability_validation_test.exe", 
      "avb_comprehensive_test.exe", 
      "avb_device_separation_test.exe", 
      "avb_diagnostic_test.exe", 
      "avb_diagnostic_test_um.exe", 
      "avb_hw_state_test.exe", 
      "avb_hw_state_test_um.exe", 
      "avb_i226_advanced_test.exe", 
      "avb_i226_test.exe", 
      "avb_multi_adapter_test.exe", 
      "avb_test_i210.exe", 
      "avb_test_i210_um.exe", 
      "avb_test_i219.exe", 
      "avb_test_i226.exe", 

      "critical_prerequisites_investigation.exe", 
      "device_open_test.exe", 
      "diagnose_ptp.exe", 
      "enhanced_tas_investigation.exe", 
      "hardware_investigation_tool.exe", 
      "hw_timestamping_control_test.exe", 
      "intel_avb_diagnostics.exe", 
      "ptp_clock_control_production_test.exe", 
      "ptp_clock_control_test.exe", 
      "quick_diagnostics.exe", 
      "rx_timestamping_test.exe", 
      "ssot_register_validation_test.exe", 
      "test_all_adapters.exe", 
      "test_atdecc_aen_protocol.exe", 
      "test_atdecc_event.exe", 
      "test_avtp_tu_bit_events.exe", 
      "test_build_sign.exe", 
      "test_cleanup_archive.exe", 
      "test_clock_config.exe", 
      "test_clock_working.exe", 
      "test_compat_win11.exe", 
      "test_device_register_access.exe", 
      "test_dev_lifecycle.exe", 
      "test_direct_clock.exe", 
      "test_eee_lpi.exe", 
      "test_error_recovery.exe", 
      "test_event_latency_4ch.exe", 
      "test_event_log.exe", 
      "test_extended_diag.exe", 
      "test_gptp_daemon_coexist.exe", 
      "test_gptp_phc_interface.exe", 
      "test_hal_errors.exe", 
      "test_hal_performance.exe", 
      "test_hal_unit.exe", 
      "test_hot_plug.exe", 
      "test_hw_state.exe", 
      "test_hw_state_machine.exe", 
      "test_hw_ts_ctrl.exe", 
      "test_ioctl_abi.exe", 
      "test_ioctl_access_control.exe", 
      "test_ioctl_buffer_fuzz.exe", 
      "test_ioctl_fp_ptm.exe", 
      "test_ioctl_launch_time.exe", 
      "test_ioctl_offset.exe", 
      "test_ioctl_phc_epoch.exe", 
      "test_ioctl_phc_monotonicity.exe", 
      "test_ioctl_phc_query.exe", 
      "test_ioctl_routing.exe", 
      "test_ioctl_simple.exe", 
      "test_ioctl_target_time.exe", 
       
      "test_ioctl_trace.exe", 
      "test_ioctl_version.exe", 
      "test_ioctl_xstamp.exe", 
      "test_lazy_initialization.exe", 
      "test_lifecycle_coverage.exe", 
      "test_magic_numbers.exe", 
      "test_mdio_phy.exe", 
      "test_minimal_ioctl.exe", 
      "test_multidev_adapter_enum.exe", 
      "test_multi_adapter_phc_sync.exe", 
      "test_ndis_fastpath_latency.exe", 
      "test_ndis_receive_path.exe", 
      "test_ndis_send_path.exe", 
      "test_perf_regression.exe", 
      "test_pfc_pause.exe", 
      "test_power_management.exe", 
      "test_ptp_001.exe", 
      "test_ptp_corr.exe", 
      "test_ptp_corr_extended.exe", 
      "test_ptp_crosstimestamp.exe", 
      "test_ptp_event_latency.exe", 
      "test_ptp_freq.exe", 
      "test_ptp_freq_complete.exe", 
      "test_ptp_getset.exe", 
      "test_ptp_phc_stability.exe", 
      "test_qav_cbs.exe", 
      "test_registry_diagnostics.exe", 
      "test_rx_timestamp.exe", 
      "test_rx_timestamp_complete.exe", 
      "test_scripts_consolidate.exe", 
      "test_security_validation.exe", 
      "test_send_ptp_debug.exe", 
      "test_srp_interface.exe", 
      "test_statistics_counters.exe", 
      "test_timestamp_latency.exe", 
      "test_tsn_ioctl_handlers.exe", 
      "test_tsn_ioctl_handlers_um.exe", 
      "test_ts_event_sub.exe", 
      "test_tx_timestamp_retrieval.exe", 
      "test_vlan_pcp_tc_mapping.exe", 
      "test_vlan_tag.exe", 
      "test_vv_corr_003_crossdomain.exe", 
      "test_win7_stub.exe", 
      "test_zero_polling_overhead.exe", 
      "tsauxc_toggle_test.exe", 
      "tsn_hardware_activation_validation.exe", 
      "test_ptp_phc_stability.exe" 
 
  ) 
 
# place test where issue was reproduced below 
$BadSuite = @( 
    "chatgpt5_i226_tas_validation.exe", 
    "corrected_i226_tas_test.exe", 
    "test_ioctl_tas.exe", 
    "comprehensive_ioctl_test.exe" 
  ) 
 
.\tools\setup\Install-Driver-Elevated.ps1 -Configuration Debug -Action Reinstall -CaptureDbgView 
  
foreach ($t in $GoodSuite) { 
 .\tools\test\Run-Tests-Elevated.ps1 -TestName $t -CaptureDbgView -Configuration Debug 
# exclude here as GoodSuite contains "test_ptp_phc_stability.exe" as last test anyway - only acivate to determine if there is BadTest causing issues
 # // .\tools\test\Run-Tests-Elevated.ps1 -TestName "test_ptp_phc_stability.exe" -CaptureDbgView -Configuration Debug 
} 

.\tools\setup\Install-Driver-Elevated.ps1 -Configuration Debug -Action Reinstall -CaptureDbgView 
$identifyBadCase = @(
    "chatgpt5_i226_tas_validation.exe", 
    "corrected_i226_tas_test.exe", 
    "test_ioctl_tas.exe", 
    "comprehensive_ioctl_test.exe" 
)
foreach ($t in $identifyBadCase) { 
  .\tools\test\Run-Tests-Elevated.ps1 -TestName $t -CaptureDbgView -Configuration Debug 
 .\tools\test\Run-Tests-Elevated.ps1 -TestName "test_ptp_phc_stability.exe" -CaptureDbgView -Configuration Debug 
}

foreach ($t in $BadSuite) { 
 .\tools\setup\Install-Driver-Elevated.ps1 -Configuration Debug -Action Reinstall -CaptureDbgView 
 .\tools\test\Run-Tests-Elevated.ps1 -TestName $t -CaptureDbgView -Configuration Debug 
 .\tools\test\Run-Tests-Elevated.ps1 -TestName "test_ptp_phc_stability.exe" -CaptureDbgView -Configuration Debug 
}

# Start DbgView manually BEFORE running the script
# DO NOT use -CaptureDbgView — DbgView must stay running across both runs
.\tools\test\Run-Tests-Elevated.ps1 -TestName 'test_ioctl_tas.exe' -TestArgs '--case TC-TAS-001' -CaptureDbgView -Configuration Debug 
.\tools\test\Run-Tests-Elevated.ps1 -TestName 'test_ptp_phc_stability.exe' -CaptureDbgView -Configuration Debug 
# Only stop DbgView after everything (including the hang) completes or is killed