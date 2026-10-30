# Completeness gate - RED evidence 2026-10-21

Command: `powershell -File tests\unit\ci\test_artifact_completeness.ps1` at HEAD 4826c74 plus the uncommitted working tree (gate + empty ``). Preserved before any artifact is classified.

```text
TEST-COMPLETE: build targets / test scripts vs runner registry and classification (offline)
  [PASS] TEST-COMPLETE-000-classification-table-valid
  [FAIL] TEST-COMPLETE-001-every-build-target-is-a-runner-entry-or-classified: 43 of 147 build target(s) Unknown - neither executed, nor a declared gap, nor classified: avb_test_i226, avb_test_i210_um (Build-Tests Name 'avb_test_i210'), avb_test_project, avb_capability_validation, ptp_clock_control_production_test (Build-Tests Name 'ptp_clock_control_production'), tsn_ioctl_test, tsn_hardware_activation, avb_multi_adapter, avb_device_separation, comprehensive_ioctl_test, quick_diagnostics, intel_avb_diagnostics, device_open_test, avb_diagnostic, avb_hw_state_test, hardware_investigation, critical_prerequisites, enhanced_tas_investigation, avb_test_i210 (Build-Tests Name 'avb_test_i210_device'), avb_test_i350, avb_i226_test, avb_i226_advanced, chatgpt5_i226_tas, corrected_i226_tas, test_tsn_ioctl_handlers, hw_timestamping_control_test (Build-Tests Name 'hw_timestamping_control'), rx_timestamping_test (Build-Tests Name 'rx_timestamping'), ptp_bringup_fixed, target_time_test, avb_test_actual, avb_test_app, avb_test_standalone, AvbIntegrationTests (Build-Tests Name 'avb_integration_tests'), diagnose_ptp, test_extended_diag, avb_diagnostic_test_um, avb_hw_state_test_um, avb_comprehensive_test, taef_tests, integration_suite, test_exit_code_propagation, test_control_device_lifecycle, test_device_idle_regression
  [FAIL] TEST-COMPLETE-002-every-registered-executable-is-built-under-its-entry-name: 3 registered entr(y/ies) the runner would report as binary missing: hw_timestamping_control (Build-Tests Name 'hw_timestamping_control' outputs 'hw_timestamping_control_test.exe'), ptp_clock_control_production (Build-Tests Name 'ptp_clock_control_production' outputs 'ptp_clock_control_production_test.exe'), rx_timestamping (Build-Tests Name 'rx_timestamping' outputs 'rx_timestamping_test.exe')
  [FAIL] TEST-COMPLETE-003-every-unit-test-script-is-registered-or-classified: 4 of 21 test script(s) never executed by any runner list: tests/unit/ci/test_hil_dispatch_paths.ps1, tests/unit/ci/test_hil_suite_safety.ps1, tests/unit/ci/test_hil_trust_boundary.ps1, tests/unit/ioctl/test_identity_contract.ps1

  Summary: 1 passed, 3 failed of 4 tests
[FAIL] TEST-COMPLETE: artifacts disappear from verification
```
