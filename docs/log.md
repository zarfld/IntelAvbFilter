================================================================
  Intel AVB Filter Driver - Test Suite
  Configuration: Debug
================================================================

==> Checking Driver Service Status
[OK] Service found: Running

==> Enumerating Intel Network Adapters
[OK] Found 6 Intel network adapter(s)

  Adapter: Intel(R) Ethernet Controller I226-V #3
    Name: Ethernet 3
    Status: Disconnected
    MAC: A8-B8-E0-0A-1E-8C
    Speed: 0 bps
    Hardware ID: PCI\VEN_8086&DEV_125C&SUBSYS_00008086&REV_04
    VID: 0x8086, DID: 0x125C
    Device: Intel I226-LM 2.5GbE Network Connection
[OK]     [SUPPORTED] This device is supported by the driver

  Adapter: Intel(R) Ethernet Controller I226-V #5
    Name: Ethernet 5
    Status: Up
    MAC: A8-B8-E0-0A-1E-8E
    Speed: 1 Gbps
    Hardware ID: PCI\VEN_8086&DEV_125C&SUBSYS_00008086&REV_04
    VID: 0x8086, DID: 0x125C
    Device: Intel I226-LM 2.5GbE Network Connection
[OK]     [SUPPORTED] This device is supported by the driver

  Adapter: Intel(R) Ethernet Controller I226-V #4
    Name: Ethernet 4
    Status: Up
    MAC: A8-B8-E0-0A-1E-8D
    Speed: 1 Gbps
    Hardware ID: PCI\VEN_8086&DEV_125C&SUBSYS_00008086&REV_04
    VID: 0x8086, DID: 0x125C
    Device: Intel I226-LM 2.5GbE Network Connection
[OK]     [SUPPORTED] This device is supported by the driver

  Adapter: Intel(R) Ethernet Controller I226-V #6
    Name: Ethernet 6
    Status: Disconnected
    MAC: A8-B8-E0-0A-1E-8B
    Speed: 0 bps
    Hardware ID: PCI\VEN_8086&DEV_125C&SUBSYS_00008086&REV_04
    VID: 0x8086, DID: 0x125C
    Device: Intel I226-LM 2.5GbE Network Connection
[OK]     [SUPPORTED] This device is supported by the driver

  Adapter: Intel(R) Ethernet Controller I226-V
    Name: Ethernet
    Status: Up
    MAC: A8-B8-E0-0A-1E-8F
    Speed: 1 Gbps
    Hardware ID: PCI\VEN_8086&DEV_125C&SUBSYS_00008086&REV_04
    VID: 0x8086, DID: 0x125C
    Device: Intel I226-LM 2.5GbE Network Connection
[OK]     [SUPPORTED] This device is supported by the driver

  Adapter: Intel(R) Ethernet Controller I226-V #2
    Name: Ethernet 2
    Status: Disconnected
    MAC: A8-B8-E0-0A-1E-90
    Speed: 0 bps
    Hardware ID: PCI\VEN_8086&DEV_125C&SUBSYS_00008086&REV_04
    VID: 0x8086, DID: 0x125C
    Device: Intel I226-LM 2.5GbE Network Connection
[OK]     [SUPPORTED] This device is supported by the driver

==> Testing Device Node Access
[OK] Device node accessible: \\.\IntelAvbFilter

==> Locating Test Executables
[OK] Found 73 test executable(s)
  [OK] avb_capability_validation_test.exe (46592 bytes)
  [OK] avb_comprehensive_test.exe (664576 bytes)
  [OK] avb_device_separation_test.exe (45568 bytes)
  [OK] avb_diagnostic_test.exe (663040 bytes)
  [OK] avb_diagnostic_test_um.exe (672768 bytes)
  [OK] avb_hw_state_test.exe (663040 bytes)
  [OK] avb_hw_state_test_um.exe (672768 bytes)
  [OK] avb_i226_advanced_test.exe (685056 bytes)
  [OK] avb_i226_test.exe (683520 bytes)
  [OK] avb_multi_adapter_test.exe (691712 bytes)
  [OK] avb_test_i210.exe (664064 bytes)
  [OK] avb_test_i210_um.exe (692736 bytes)
  [OK] avb_test_i219.exe (665600 bytes)
  [OK] avb_test_i226.exe (692736 bytes)
  [OK] chatgpt5_i226_tas_validation.exe (679424 bytes)
  [OK] comprehensive_ioctl_test.exe (685568 bytes)
  [OK] corrected_i226_tas_test.exe (672768 bytes)
  [OK] critical_prerequisites_investigation.exe (674304 bytes)
  [OK] device_open_test.exe (665600 bytes)
  [OK] diagnose_ptp.exe (671232 bytes)
  [OK] enhanced_tas_investigation.exe (673792 bytes)
  [OK] hardware_investigation_tool.exe (674816 bytes)
  [OK] hw_timestamping_control_test.exe (668672 bytes)
  [OK] intel_avb_diagnostics.exe (732160 bytes)
  [OK] ptp_clock_control_production_test.exe (669696 bytes)
  [OK] ptp_clock_control_test.exe (677376 bytes)
  [OK] quick_diagnostics.exe (696320 bytes)
  [OK] rx_timestamping_test.exe (666624 bytes)
  [OK] ssot_register_validation_test.exe (668672 bytes)
  [OK] test_all_adapters.exe (667648 bytes)
  [OK] test_avtp_tu_bit_events.exe (685568 bytes)
  [OK] test_clock_config.exe (669184 bytes)
  [OK] test_clock_working.exe (665088 bytes)
  [OK] test_device_register_access.exe (669184 bytes)
  [OK] test_dev_lifecycle.exe (675328 bytes)
  [OK] test_direct_clock.exe (664576 bytes)
  [OK] test_event_log.exe (696320 bytes)
  [OK] test_extended_diag.exe (665088 bytes)
  [OK] test_hal_errors.exe (675840 bytes)
  [OK] test_hal_performance.exe (671744 bytes)
  [OK] test_hal_unit.exe (677888 bytes)
  [OK] test_hw_state_machine.exe (678400 bytes)
  [OK] test_hw_ts_ctrl.exe (676352 bytes)
  [OK] test_ioctl_fp_ptm.exe (675328 bytes)
  [OK] test_ioctl_offset.exe (675840 bytes)
  [OK] test_ioctl_phc_query.exe (673280 bytes)
  [OK] test_ioctl_routing.exe (667136 bytes)
  [OK] test_ioctl_simple.exe (663552 bytes)
  [OK] test_ioctl_target_time.exe (675840 bytes)
  [OK] test_ioctl_tas.exe (677888 bytes)
  [OK] test_ioctl_trace.exe (668672 bytes)
  [OK] test_ioctl_version.exe (667136 bytes)
  [OK] test_lazy_initialization.exe (674304 bytes)
  [OK] test_mdio_phy.exe (673792 bytes)
  [OK] test_multidev_adapter_enum.exe (673792 bytes)
  [OK] test_ndis_receive_path.exe (669696 bytes)
  [OK] test_ndis_send_path.exe (673280 bytes)
  [OK] test_ptp_freq.exe (677376 bytes)
  [OK] test_ptp_freq_complete.exe (724480 bytes)
  [OK] test_ptp_getset.exe (674816 bytes)
  [OK] test_qav_cbs.exe (671232 bytes)
  [OK] test_registry_diagnostics.exe (674304 bytes)
  [OK] test_rx_timestamp.exe (672768 bytes)
  [OK] test_rx_timestamp_complete.exe (684544 bytes)
  [OK] test_security_validation.exe (683520 bytes)
  [OK] test_statistics_counters.exe (674816 bytes)
  [OK] test_timestamp_latency.exe (682496 bytes)
  [OK] test_tsn_ioctl_handlers.exe (667648 bytes)
  [OK] test_tsn_ioctl_handlers_um.exe (667648 bytes)
  [OK] test_ts_event_sub.exe (691200 bytes)
  [OK] test_tx_timestamp_retrieval.exe (671232 bytes)
  [OK] tsauxc_toggle_test.exe (672256 bytes)
  [OK] tsn_hardware_activation_validation.exe (670720 bytes)

==> Running Specific Test: test_ts_event_sub.exe
Executing: C:\Users\dzarf\source\repos\IntelAvbFilter\build\tools\avb_test\x64\Debug\test_ts_event_sub.exe


====================================================================
 Timestamp Event Subscription Test Suite
====================================================================
 Test Plan: TEST-PLAN-IOCTL-NEW-2025-12-31.md
 Issue: #314 (TEST-TS-EVENT-SUB-001)
 Requirement: #13 (REQ-F-TS-EVENT-SUB-001)
 IOCTLs: SUBSCRIBE_TS_EVENTS (33), MAP_TS_RING_BUFFER (34)
 Total Tests: 19 per adapter
 Priority: P1
====================================================================

Found 6 Intel AVB adapter(s):
  [0] \\.\IntelAvbFilter
  [1] \\.\IntelAvbFilter
  [2] \\.\IntelAvbFilter
  [3] \\.\IntelAvbFilter
  [4] \\.\IntelAvbFilter
  [5] \\.\IntelAvbFilter

====================================================================
 OPENING ALL ADAPTERS (preventing Windows handle reuse caching)
====================================================================

  [DEBUG] CreateFileA returned handle=00000000000000B4 for Index=0
  [DEBUG] Calling IOCTL_AVB_OPEN_ADAPTER: VID=0x8086 DID=0x125B Index=0
  [DEBUG] DeviceIoControl returned: result=1, bytes=12, GetLastError=0, status=0x00000000
  [DEBUG] IOCTL_AVB_OPEN_ADAPTER returned: bytes=12 status=0x00000000
  [INFO] Bound handle to adapter VID=0x8086 DID=0x125B (Index=0)
  [OK] Adapter 0 opened with handle=00000000000000B4
  [DEBUG] CreateFileA returned handle=00000000000000B8 for Index=1
  [DEBUG] Calling IOCTL_AVB_OPEN_ADAPTER: VID=0x8086 DID=0x125B Index=1
  [DEBUG] DeviceIoControl returned: result=1, bytes=12, GetLastError=0, status=0x00000000
  [DEBUG] IOCTL_AVB_OPEN_ADAPTER returned: bytes=12 status=0x00000000
  [INFO] Bound handle to adapter VID=0x8086 DID=0x125B (Index=1)
  [OK] Adapter 1 opened with handle=00000000000000B8
  [DEBUG] CreateFileA returned handle=00000000000000BC for Index=2
  [DEBUG] Calling IOCTL_AVB_OPEN_ADAPTER: VID=0x8086 DID=0x125B Index=2
  [DEBUG] DeviceIoControl returned: result=1, bytes=12, GetLastError=0, status=0x00000000
  [DEBUG] IOCTL_AVB_OPEN_ADAPTER returned: bytes=12 status=0x00000000
  [INFO] Bound handle to adapter VID=0x8086 DID=0x125B (Index=2)
  [OK] Adapter 2 opened with handle=00000000000000BC
  [DEBUG] CreateFileA returned handle=00000000000000C0 for Index=3
  [DEBUG] Calling IOCTL_AVB_OPEN_ADAPTER: VID=0x8086 DID=0x125B Index=3
  [DEBUG] DeviceIoControl returned: result=1, bytes=12, GetLastError=0, status=0x00000000
  [DEBUG] IOCTL_AVB_OPEN_ADAPTER returned: bytes=12 status=0x00000000
  [INFO] Bound handle to adapter VID=0x8086 DID=0x125B (Index=3)
  [OK] Adapter 3 opened with handle=00000000000000C0
  [DEBUG] CreateFileA returned handle=00000000000000C4 for Index=4
  [DEBUG] Calling IOCTL_AVB_OPEN_ADAPTER: VID=0x8086 DID=0x125B Index=4
  [DEBUG] DeviceIoControl returned: result=1, bytes=12, GetLastError=0, status=0x00000000
  [DEBUG] IOCTL_AVB_OPEN_ADAPTER returned: bytes=12 status=0x00000000
  [INFO] Bound handle to adapter VID=0x8086 DID=0x125B (Index=4)
  [OK] Adapter 4 opened with handle=00000000000000C4
  [DEBUG] CreateFileA returned handle=00000000000000C8 for Index=5
  [DEBUG] Calling IOCTL_AVB_OPEN_ADAPTER: VID=0x8086 DID=0x125B Index=5
  [DEBUG] DeviceIoControl returned: result=1, bytes=12, GetLastError=0, status=0x00000000
  [DEBUG] IOCTL_AVB_OPEN_ADAPTER returned: bytes=12 status=0x00000000
  [INFO] Bound handle to adapter VID=0x8086 DID=0x125B (Index=5)
  [OK] Adapter 5 opened with handle=00000000000000C8

====================================================================
 TESTING ALL ADAPTERS (Multi-Adapter Validation)
====================================================================


********************************************************************
 ADAPTER [1/6]: \\.\IntelAvbFilter (Handle=00000000000000B4)
********************************************************************

Running Timestamp Event Subscription tests...

    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=2
  [PASS] UT-TS-SUB-001: Basic Event Subscription
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=3
  [PASS] UT-TS-SUB-002: Selective Event Type Subscription
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=4
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000002
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=5
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=6
    Created 3 subscriptions: ring_id=4, 5, 6
  [PASS] UT-TS-SUB-003: Multiple Concurrent Subscriptions
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=7
  [PASS] UT-TS-SUB-004: Unsubscribe Operation
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=8
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=8, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46540000
    Ring buffer mapped: 32840 bytes at 0000019B46540000
  [PASS] UT-TS-RING-001: Ring Buffer Mapping
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=9
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=9, length=32768
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46550000
    Requested: 32768, Actual: 32840
  [PASS] UT-TS-RING-002: Ring Buffer Size Negotiation
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=10
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=10, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46560000
    Ring buffer state:
      Producer: 0, Consumer: 0, Mask: 0x3FF, Overflow: 1024, Total: 4294967296
  [PASS] UT-TS-RING-003: Ring Buffer Wraparound
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=11
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=11, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46570000
    Read synchronization test:
      Initial: Producer=0, Consumer=0
      After 10ms: Producer=0, Consumer=0
  [PASS] UT-TS-RING-004: Ring Buffer Read Synchronization
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=12
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=12, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46580000
    [DIAG] Enabling hardware timestamping...
    [DIAG] Enabling hardware timestamping for adapter 0...
    [DIAG] Global RX timestamp enabled (RXPBSIZE: 0x000000A2 -> 0x200000A2)
    [DIAG] Queue 0 timestamp enabled (SRRCTL[0]: 0x02000402 -> 0x42000402)
    [DIAG] HW timestamping features enabled (TSAUXC: 0x78000004 -> 0x78000515)
    [DIAG] Checking hardware timestamping configuration...
    [DIAG] Adapter 0 TSYNCRXCTL: 0x00000000 (RX_ENABLED=0, VALID=0)
    [WARN] RX timestamping NOT enabled in hardware!
    [DIAG] Ring buffer state: producer=0, consumer=0, capacity_mask=0x3FF
    [DIAG] Subscription ring_id=12, subscription address mapped at 0000019B46580000
    Waiting for PTP RX events (30 sec timeout for 802.1AS/Milan traffic)...
    Initial producer index: 0
    Expected: ~30 Announce + Sync + Pdelay messages (1 sec intervals)
    [DIAG] 5 seconds elapsed, total events: 0
    [DIAG] 10 seconds elapsed, total events: 0
    [DIAG] 15 seconds elapsed, total events: 0
    [DIAG] 20 seconds elapsed, total events: 0
    [DIAG] 25 seconds elapsed, total events: 0
    [DIAG] 30 seconds elapsed, total events: 0
    [DIAG] Final ring buffer state: producer=0, consumer=0, overflow=1024
    [WARN] No events written to ring buffer - check driver AvbPostTimestampEvent() calls
  [SKIP] UT-TS-EVENT-001: RX Timestamp Event Delivery: No RX events received (no PTP traffic detected)
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000002
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=13
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=13, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46590000
    Waiting for TX timestamp events (30 sec timeout)...
    Initial producer index: 0
    NOTE: TX events require Task 6b implementation (1ms polling)
  [SKIP] UT-TS-EVENT-002: TX Timestamp Event Delivery: No TX events received (Task 6b not implemented yet)

=== UT-TS-EVENT-003: Target Time Reached Event (Task 7) ===
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000004
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=14
Ô£ô Subscribed (ring_id=14)
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=14, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B465A0000
Ô£ô Ring buffer mapped (32840 bytes)
Current SYSTIM: 292456007891 ns
    DEBUG: Before DeviceIoControl:
      Structure size: 40 bytes
      IOCTL code: 0x0017009C (expecting 0x001700AC)
      Handle: 00000000000000B4 (same as subscription: 00000000000000B4)
      timer_index: 0
      target_time: 294456007891 ns
      enable_interrupt: 1
      status (sentinel): 0xFFFFFFFF
      previous_target (sentinel): 0xDEADBEEF
    DEBUG: About to call DeviceIoControl...
    DEBUG: DeviceIoControl RETURNED
      API result: 1
      GetLastError(): 0 (0x00000000)
      bytes_returned: 40 (expected ~36-40)
      status: 0x00000000 <-- ZERO (success or Windows zeroed it?)
      previous_target: 0xDEADBEEF <-- SENTINEL UNCHANGED!
Ô£ô Target time set to 294456007891 ns (+ 2.0s)
  Polling ring buffer for 3 seconds...
  [FAIL] UT-TS-EVENT-003: Target Time Reached Event: No event received within 3 seconds - Task 7 not working!
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000008
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=15
    Aux timestamp event subscription created (ring_id=15)
    NOTE: Requires GPIO or external signal trigger
  [PASS] UT-TS-EVENT-004: Aux Timestamp Event
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=16
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=16, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B465B0000
    Event sequence state:
      Total events: 12884901888
      Producer index: 0, Consumer index: 0
  [PASS] UT-TS-EVENT-005: Event Sequence Numbering
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=17
  [PASS] UT-TS-EVENT-006: Event Filtering by Criteria
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=18
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=18, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B465C0000
  [PASS] UT-TS-RING-005: Ring Buffer Unmap Operation
  [SKIP] UT-TS-PERF-001: High Event Rate Performance: Requires sustained traffic generation (10K events/sec) - benchmark test
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=4294967295, length=65536
    DEBUG: DeviceIoControl result=0, bytes_returned=0, status=0x00000000, shm_token=0x0
    DEBUG: DeviceIoControl failed with error 87 (0x00000057)
  [PASS] UT-TS-ERROR-001: Invalid Subscription Handle
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=19
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=19, length=10485760
    DEBUG: DeviceIoControl result=0, bytes_returned=0, status=0x00000000, shm_token=0x0
    DEBUG: DeviceIoControl failed with error 1450 (0x000005AA)
  [PASS] UT-TS-ERROR-002: Ring Buffer Mapping Failure
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=20
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=20, length=4096
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B467B0000
    Small buffer overflow test:
      Buffer size: 32840 bytes (capacity_mask=0x3FF)
      Overflow count: 1024
      NOTE: Overflow requires high event rate (Task 6b TX polling)
  [PASS] UT-TS-ERROR-003: Event Overflow Notification

--------------------------------------------------------------------
 Adapter [1/6] Summary: \\.\IntelAvbFilter
--------------------------------------------------------------------
 Total:   19 tests
 Passed:  15 tests
 Failed:  1 tests
 Skipped: 3 tests
--------------------------------------------------------------------


********************************************************************
 ADAPTER [2/6]: \\.\IntelAvbFilter (Handle=00000000000000B8)
********************************************************************

Running Timestamp Event Subscription tests...

    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=2
  [PASS] UT-TS-SUB-001: Basic Event Subscription
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=3
  [PASS] UT-TS-SUB-002: Selective Event Type Subscription
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=4
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000002
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=5
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=6
    Created 3 subscriptions: ring_id=4, 5, 6
  [PASS] UT-TS-SUB-003: Multiple Concurrent Subscriptions
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=7
  [PASS] UT-TS-SUB-004: Unsubscribe Operation
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=8
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=8, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B467C0000
    Ring buffer mapped: 32840 bytes at 0000019B467C0000
  [PASS] UT-TS-RING-001: Ring Buffer Mapping
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=9
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=9, length=32768
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B467D0000
    Requested: 32768, Actual: 32840
  [PASS] UT-TS-RING-002: Ring Buffer Size Negotiation
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=10
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=10, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B467E0000
    Ring buffer state:
      Producer: 0, Consumer: 0, Mask: 0x3FF, Overflow: 1024, Total: 4294967296
  [PASS] UT-TS-RING-003: Ring Buffer Wraparound
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=11
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=11, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B467F0000
    Read synchronization test:
      Initial: Producer=0, Consumer=0
      After 10ms: Producer=0, Consumer=0
  [PASS] UT-TS-RING-004: Ring Buffer Read Synchronization
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=12
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=12, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46800000
    [DIAG] Enabling hardware timestamping...
    [DIAG] Enabling hardware timestamping for adapter 1...
    [DIAG] Global RX timestamp enabled (RXPBSIZE: 0x000000A2 -> 0x200000A2)
    [DIAG] Queue 0 timestamp enabled (SRRCTL[0]: 0x42000402 -> 0x42000402)
    [DIAG] HW timestamping features enabled (TSAUXC: 0x78000515 -> 0x78000515)
    [DIAG] Checking hardware timestamping configuration...
    [DIAG] Adapter 1 TSYNCRXCTL: 0x00000000 (RX_ENABLED=0, VALID=0)
    [WARN] RX timestamping NOT enabled in hardware!
    [DIAG] Ring buffer state: producer=0, consumer=0, capacity_mask=0x3FF
    [DIAG] Subscription ring_id=12, subscription address mapped at 0000019B46800000
    Waiting for PTP RX events (30 sec timeout for 802.1AS/Milan traffic)...
    Initial producer index: 0
    Expected: ~30 Announce + Sync + Pdelay messages (1 sec intervals)
    [DIAG] 0 seconds: 1 events received (+1 new)
    [DIAG] 1 seconds: 2 events received (+1 new)
    [DIAG] 2 seconds: 3 events received (+1 new)
    [DIAG] 3 seconds: 4 events received (+1 new)
    [DIAG] 4 seconds: 5 events received (+1 new)
    [DIAG] 5 seconds elapsed, total events: 5
    [DIAG] 5 seconds: 6 events received (+1 new)
    [DIAG] 6 seconds: 7 events received (+1 new)
    [DIAG] 7 seconds: 8 events received (+1 new)
    [DIAG] 8 seconds: 9 events received (+1 new)
    [DIAG] 8 seconds: 10 events received (+1 new)
    [DIAG] 9 seconds: 11 events received (+1 new)
    [DIAG] 10 seconds elapsed, total events: 11
    [DIAG] 10 seconds: 12 events received (+1 new)
    [DIAG] 11 seconds: 13 events received (+1 new)
    [DIAG] 12 seconds: 14 events received (+1 new)
    [DIAG] 13 seconds: 15 events received (+1 new)
    [DIAG] 14 seconds: 16 events received (+1 new)
    [DIAG] 15 seconds elapsed, total events: 16
    [DIAG] 15 seconds: 17 events received (+1 new)
    [DIAG] 16 seconds: 18 events received (+1 new)
    [DIAG] 17 seconds: 19 events received (+1 new)
    [DIAG] 17 seconds: 20 events received (+1 new)
    [DIAG] 18 seconds: 21 events received (+1 new)
    [DIAG] 19 seconds: 22 events received (+1 new)
    [DIAG] 20 seconds elapsed, total events: 22
    [DIAG] 20 seconds: 23 events received (+1 new)
    [DIAG] 21 seconds: 24 events received (+1 new)
    [DIAG] 22 seconds: 25 events received (+1 new)
    [DIAG] 23 seconds: 26 events received (+1 new)
    [DIAG] 24 seconds: 27 events received (+1 new)
    [DIAG] 25 seconds elapsed, total events: 27
    [DIAG] 25 seconds: 28 events received (+1 new)
    [DIAG] 26 seconds: 29 events received (+1 new)
    [DIAG] 27 seconds: 30 events received (+1 new)
    [DIAG] 27 seconds: 31 events received (+1 new)
    [DIAG] 28 seconds: 32 events received (+1 new)
    [DIAG] 29 seconds: 33 events received (+1 new)
    [DIAG] 30 seconds elapsed, total events: 33

    === PTP Message Analysis (33 events) ===
      Event[0]: ts=0x0000000000000000, type=0x2999BAE, seq=223, trigger=0x1 (PTP msgType=0x1:Delay_Req)
      Event[1]: ts=0x0000000000000000, type=0x4595505, seq=224, trigger=0x2 (PTP msgType=0x2:Pdelay_Req)
      Event[2]: ts=0x0000000000000000, type=0x6190F77, seq=225, trigger=0x3 (PTP msgType=0x3:Pdelay_Resp)

    PTP Message Type Distribution:
      0x0 Sync                :   2 events [CRITICAL]
      0x1 Delay_Req           :   3 events
      0x2 Pdelay_Req          :   2 events [CRITICAL]
      0x3 Pdelay_Resp         :   2 events [CRITICAL]
      0x4 Reserved4           :   2 events
      0x5 Reserved5           :   2 events
      0x6 Reserved6           :   2 events
      0x7 Reserved7           :   2 events
      0x8 Follow_Up           :   2 events [CRITICAL]
      0x9 Delay_Resp          :   2 events
      0xA Pdelay_Resp_FU      :   2 events [CRITICAL]
      0xB Announce            :   2 events [CRITICAL]
      0xC Signaling           :   2 events
      0xD Management          :   2 events
      0xE Reserved14          :   2 events
      0xF Reserved15          :   2 events
    Total critical PTP messages: 12/33 (36.4%)

    [PASS] Received 33 events (>= 5 required for 802.1AS traffic)
  [PASS] UT-TS-EVENT-001: RX Timestamp Event Delivery
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000002
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=13
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=13, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46810000
    Waiting for TX timestamp events (30 sec timeout)...
    Initial producer index: 0
    NOTE: TX events require Task 6b implementation (1ms polling)
  [SKIP] UT-TS-EVENT-002: TX Timestamp Event Delivery: No TX events received (Task 6b not implemented yet)

=== UT-TS-EVENT-003: Target Time Reached Event (Task 7) ===
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000004
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=14
Ô£ô Subscribed (ring_id=14)
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=14, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46820000
Ô£ô Ring buffer mapped (32840 bytes)
Current SYSTIM: 597760967625 ns
    DEBUG: Before DeviceIoControl:
      Structure size: 40 bytes
      IOCTL code: 0x0017009C (expecting 0x001700AC)
      Handle: 00000000000000B8 (same as subscription: 00000000000000B8)
      timer_index: 0
      target_time: 599760967625 ns
      enable_interrupt: 1
      status (sentinel): 0xFFFFFFFF
      previous_target (sentinel): 0xDEADBEEF
    DEBUG: About to call DeviceIoControl...
    DEBUG: DeviceIoControl RETURNED
      API result: 1
      GetLastError(): 1450 (0x000005AA)
      bytes_returned: 40 (expected ~36-40)
      status: 0x00000000 <-- ZERO (success or Windows zeroed it?)
      previous_target: 0xDEADBEEF <-- SENTINEL UNCHANGED!
Ô£ô Target time set to 599760967625 ns (+ 2.0s)
  Polling ring buffer for 3 seconds...
  [FAIL] UT-TS-EVENT-003: Target Time Reached Event: No event received within 3 seconds - Task 7 not working!
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000008
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=15
    Aux timestamp event subscription created (ring_id=15)
    NOTE: Requires GPIO or external signal trigger
  [PASS] UT-TS-EVENT-004: Aux Timestamp Event
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=16
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=16, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46830000
    Event sequence state:
      Total events: 12884901888
      Producer index: 0, Consumer index: 0
  [PASS] UT-TS-EVENT-005: Event Sequence Numbering
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=17
  [PASS] UT-TS-EVENT-006: Event Filtering by Criteria
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=18
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=18, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46840000
  [PASS] UT-TS-RING-005: Ring Buffer Unmap Operation
  [SKIP] UT-TS-PERF-001: High Event Rate Performance: Requires sustained traffic generation (10K events/sec) - benchmark test
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=4294967295, length=65536
    DEBUG: DeviceIoControl result=0, bytes_returned=0, status=0x00000000, shm_token=0x0
    DEBUG: DeviceIoControl failed with error 87 (0x00000057)
  [PASS] UT-TS-ERROR-001: Invalid Subscription Handle
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=19
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=19, length=10485760
    DEBUG: DeviceIoControl result=0, bytes_returned=0, status=0x00000000, shm_token=0x0
    DEBUG: DeviceIoControl failed with error 1450 (0x000005AA)
  [PASS] UT-TS-ERROR-002: Ring Buffer Mapping Failure
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=20
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=20, length=4096
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46850000
    Small buffer overflow test:
      Buffer size: 32840 bytes (capacity_mask=0x3FF)
      Overflow count: 1024
      NOTE: Overflow requires high event rate (Task 6b TX polling)
  [PASS] UT-TS-ERROR-003: Event Overflow Notification

--------------------------------------------------------------------
 Adapter [2/6] Summary: \\.\IntelAvbFilter
--------------------------------------------------------------------
 Total:   19 tests
 Passed:  16 tests
 Failed:  1 tests
 Skipped: 2 tests
--------------------------------------------------------------------


********************************************************************
 ADAPTER [3/6]: \\.\IntelAvbFilter (Handle=00000000000000BC)
********************************************************************

Running Timestamp Event Subscription tests...

    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=2
  [PASS] UT-TS-SUB-001: Basic Event Subscription
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=3
  [PASS] UT-TS-SUB-002: Selective Event Type Subscription
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=4
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000002
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=5
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=6
    Created 3 subscriptions: ring_id=4, 5, 6
  [PASS] UT-TS-SUB-003: Multiple Concurrent Subscriptions
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=7
  [PASS] UT-TS-SUB-004: Unsubscribe Operation
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=8
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=8, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46860000
    Ring buffer mapped: 32840 bytes at 0000019B46860000
  [PASS] UT-TS-RING-001: Ring Buffer Mapping
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=9
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=9, length=32768
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46870000
    Requested: 32768, Actual: 32840
  [PASS] UT-TS-RING-002: Ring Buffer Size Negotiation
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=10
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=10, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46880000
    Ring buffer state:
      Producer: 0, Consumer: 0, Mask: 0x3FF, Overflow: 1024, Total: 4294967296
  [PASS] UT-TS-RING-003: Ring Buffer Wraparound
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=11
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=11, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46890000
    Read synchronization test:
      Initial: Producer=0, Consumer=0
      After 10ms: Producer=0, Consumer=0
  [PASS] UT-TS-RING-004: Ring Buffer Read Synchronization
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=12
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=12, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B468A0000
    [DIAG] Enabling hardware timestamping...
    [DIAG] Enabling hardware timestamping for adapter 2...
    [DIAG] Global RX timestamp enabled (RXPBSIZE: 0x000000A2 -> 0x200000A2)
    [DIAG] Queue 0 timestamp enabled (SRRCTL[0]: 0x42000402 -> 0x42000402)
    [DIAG] HW timestamping features enabled (TSAUXC: 0x78000515 -> 0x78000515)
    [DIAG] Checking hardware timestamping configuration...
    [DIAG] Adapter 2 TSYNCRXCTL: 0x00000000 (RX_ENABLED=0, VALID=0)
    [WARN] RX timestamping NOT enabled in hardware!
    [DIAG] Ring buffer state: producer=0, consumer=0, capacity_mask=0x3FF
    [DIAG] Subscription ring_id=12, subscription address mapped at 0000019B468A0000
    Waiting for PTP RX events (30 sec timeout for 802.1AS/Milan traffic)...
    Initial producer index: 0
    Expected: ~30 Announce + Sync + Pdelay messages (1 sec intervals)
    [DIAG] 5 seconds elapsed, total events: 0
    [DIAG] 10 seconds elapsed, total events: 0
    [DIAG] 15 seconds elapsed, total events: 0
    [DIAG] 20 seconds elapsed, total events: 0
    [DIAG] 25 seconds elapsed, total events: 0
    [DIAG] 30 seconds elapsed, total events: 0
    [DIAG] Final ring buffer state: producer=0, consumer=0, overflow=1024
    [WARN] No events written to ring buffer - check driver AvbPostTimestampEvent() calls
  [SKIP] UT-TS-EVENT-001: RX Timestamp Event Delivery: No RX events received (no PTP traffic detected)
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000002
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=13
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=13, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B468B0000
    Waiting for TX timestamp events (30 sec timeout)...
    Initial producer index: 0
    NOTE: TX events require Task 6b implementation (1ms polling)
  [SKIP] UT-TS-EVENT-002: TX Timestamp Event Delivery: No TX events received (Task 6b not implemented yet)

=== UT-TS-EVENT-003: Target Time Reached Event (Task 7) ===
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000004
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=14
Ô£ô Subscribed (ring_id=14)
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=14, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B468C0000
Ô£ô Ring buffer mapped (32840 bytes)
Current SYSTIM: 906437534460 ns
    DEBUG: Before DeviceIoControl:
      Structure size: 40 bytes
      IOCTL code: 0x0017009C (expecting 0x001700AC)
      Handle: 00000000000000BC (same as subscription: 00000000000000BC)
      timer_index: 0
      target_time: 908437534460 ns
      enable_interrupt: 1
      status (sentinel): 0xFFFFFFFF
      previous_target (sentinel): 0xDEADBEEF
    DEBUG: About to call DeviceIoControl...
    DEBUG: DeviceIoControl RETURNED
      API result: 1
      GetLastError(): 1450 (0x000005AA)
      bytes_returned: 40 (expected ~36-40)
      status: 0x00000000 <-- ZERO (success or Windows zeroed it?)
      previous_target: 0xDEADBEEF <-- SENTINEL UNCHANGED!
Ô£ô Target time set to 908437534460 ns (+ 2.0s)
  Polling ring buffer for 3 seconds...
  [FAIL] UT-TS-EVENT-003: Target Time Reached Event: No event received within 3 seconds - Task 7 not working!
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000008
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=15
    Aux timestamp event subscription created (ring_id=15)
    NOTE: Requires GPIO or external signal trigger
  [PASS] UT-TS-EVENT-004: Aux Timestamp Event
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=16
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=16, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B468D0000
    Event sequence state:
      Total events: 12884901888
      Producer index: 0, Consumer index: 0
  [PASS] UT-TS-EVENT-005: Event Sequence Numbering
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=17
  [PASS] UT-TS-EVENT-006: Event Filtering by Criteria
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=18
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=18, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B468E0000
  [PASS] UT-TS-RING-005: Ring Buffer Unmap Operation
  [SKIP] UT-TS-PERF-001: High Event Rate Performance: Requires sustained traffic generation (10K events/sec) - benchmark test
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=4294967295, length=65536
    DEBUG: DeviceIoControl result=0, bytes_returned=0, status=0x00000000, shm_token=0x0
    DEBUG: DeviceIoControl failed with error 87 (0x00000057)
  [PASS] UT-TS-ERROR-001: Invalid Subscription Handle
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=19
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=19, length=10485760
    DEBUG: DeviceIoControl result=0, bytes_returned=0, status=0x00000000, shm_token=0x0
    DEBUG: DeviceIoControl failed with error 1450 (0x000005AA)
  [PASS] UT-TS-ERROR-002: Ring Buffer Mapping Failure
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=20
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=20, length=4096
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B468F0000
    Small buffer overflow test:
      Buffer size: 32840 bytes (capacity_mask=0x3FF)
      Overflow count: 1024
      NOTE: Overflow requires high event rate (Task 6b TX polling)
  [PASS] UT-TS-ERROR-003: Event Overflow Notification

--------------------------------------------------------------------
 Adapter [3/6] Summary: \\.\IntelAvbFilter
--------------------------------------------------------------------
 Total:   19 tests
 Passed:  15 tests
 Failed:  1 tests
 Skipped: 3 tests
--------------------------------------------------------------------


********************************************************************
 ADAPTER [4/6]: \\.\IntelAvbFilter (Handle=00000000000000C0)
********************************************************************

Running Timestamp Event Subscription tests...

    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=2
  [PASS] UT-TS-SUB-001: Basic Event Subscription
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=3
  [PASS] UT-TS-SUB-002: Selective Event Type Subscription
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=4
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000002
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=5
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=6
    Created 3 subscriptions: ring_id=4, 5, 6
  [PASS] UT-TS-SUB-003: Multiple Concurrent Subscriptions
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=7
  [PASS] UT-TS-SUB-004: Unsubscribe Operation
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=8
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=8, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46900000
    Ring buffer mapped: 32840 bytes at 0000019B46900000
  [PASS] UT-TS-RING-001: Ring Buffer Mapping
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=9
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=9, length=32768
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46910000
    Requested: 32768, Actual: 32840
  [PASS] UT-TS-RING-002: Ring Buffer Size Negotiation
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=10
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=10, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46920000
    Ring buffer state:
      Producer: 0, Consumer: 0, Mask: 0x3FF, Overflow: 1024, Total: 4294967296
  [PASS] UT-TS-RING-003: Ring Buffer Wraparound
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=11
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=11, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46930000
    Read synchronization test:
      Initial: Producer=0, Consumer=0
      After 10ms: Producer=0, Consumer=0
  [PASS] UT-TS-RING-004: Ring Buffer Read Synchronization
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=12
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=12, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46940000
    [DIAG] Enabling hardware timestamping...
    [DIAG] Enabling hardware timestamping for adapter 3...
    [DIAG] Global RX timestamp enabled (RXPBSIZE: 0x000000A2 -> 0x200000A2)
    [DIAG] Queue 0 timestamp enabled (SRRCTL[0]: 0x42000402 -> 0x42000402)
    [DIAG] HW timestamping features enabled (TSAUXC: 0x78000515 -> 0x78000515)
    [DIAG] Checking hardware timestamping configuration...
    [DIAG] Adapter 3 TSYNCRXCTL: 0x00000000 (RX_ENABLED=0, VALID=0)
    [WARN] RX timestamping NOT enabled in hardware!
    [DIAG] Ring buffer state: producer=0, consumer=0, capacity_mask=0x3FF
    [DIAG] Subscription ring_id=12, subscription address mapped at 0000019B46940000
    Waiting for PTP RX events (30 sec timeout for 802.1AS/Milan traffic)...
    Initial producer index: 0
    Expected: ~30 Announce + Sync + Pdelay messages (1 sec intervals)
    [DIAG] 0 seconds: 1 events received (+1 new)
    [DIAG] 1 seconds: 2 events received (+1 new)
    [DIAG] 2 seconds: 3 events received (+1 new)
    [DIAG] 3 seconds: 4 events received (+1 new)
    [DIAG] 3 seconds: 5 events received (+1 new)
    [DIAG] 4 seconds: 6 events received (+1 new)
    [DIAG] 5 seconds elapsed, total events: 6
    [DIAG] 5 seconds: 7 events received (+1 new)
    [DIAG] 6 seconds: 8 events received (+1 new)
    [DIAG] 7 seconds: 9 events received (+1 new)
    [DIAG] 8 seconds: 10 events received (+1 new)
    [DIAG] 9 seconds: 11 events received (+1 new)
    [DIAG] 10 seconds elapsed, total events: 11
    [DIAG] 10 seconds: 12 events received (+1 new)
    [DIAG] 11 seconds: 13 events received (+1 new)
    [DIAG] 12 seconds: 14 events received (+1 new)
    [DIAG] 12 seconds: 15 events received (+1 new)
    [DIAG] 13 seconds: 16 events received (+1 new)
    [DIAG] 14 seconds: 17 events received (+1 new)
    [DIAG] 15 seconds elapsed, total events: 17
    [DIAG] 15 seconds: 18 events received (+1 new)
    [DIAG] 16 seconds: 19 events received (+1 new)
    [DIAG] 17 seconds: 20 events received (+1 new)
    [DIAG] 18 seconds: 21 events received (+1 new)
    [DIAG] 19 seconds: 22 events received (+1 new)
    [DIAG] 20 seconds elapsed, total events: 22
    [DIAG] 20 seconds: 23 events received (+1 new)
    [DIAG] 21 seconds: 24 events received (+1 new)
    [DIAG] 22 seconds: 25 events received (+1 new)
    [DIAG] 22 seconds: 26 events received (+1 new)
    [DIAG] 23 seconds: 27 events received (+1 new)
    [DIAG] 24 seconds: 28 events received (+1 new)
    [DIAG] 25 seconds elapsed, total events: 28
    [DIAG] 25 seconds: 29 events received (+1 new)
    [DIAG] 26 seconds: 30 events received (+1 new)
    [DIAG] 27 seconds: 31 events received (+1 new)
    [DIAG] 28 seconds: 32 events received (+1 new)
    [DIAG] 29 seconds: 33 events received (+1 new)
    [DIAG] 30 seconds elapsed, total events: 33

    === PTP Message Analysis (33 events) ===
      Event[0]: ts=0x0000000000000000, type=0x1E5739A0, seq=367, trigger=0x1 (PTP msgType=0x1:Delay_Req)
      Event[1]: ts=0x0000000000000000, type=0x201681BE, seq=368, trigger=0x2 (PTP msgType=0x2:Pdelay_Req)
      Event[2]: ts=0x0000000000000000, type=0x21E5E8ED, seq=369, trigger=0x3 (PTP msgType=0x3:Pdelay_Resp)

    PTP Message Type Distribution:
      0x0 Sync                :   2 events [CRITICAL]
      0x1 Delay_Req           :   3 events
      0x2 Pdelay_Req          :   2 events [CRITICAL]
      0x3 Pdelay_Resp         :   2 events [CRITICAL]
      0x4 Reserved4           :   2 events
      0x5 Reserved5           :   2 events
      0x6 Reserved6           :   2 events
      0x7 Reserved7           :   2 events
      0x8 Follow_Up           :   2 events [CRITICAL]
      0x9 Delay_Resp          :   2 events
      0xA Pdelay_Resp_FU      :   2 events [CRITICAL]
      0xB Announce            :   2 events [CRITICAL]
      0xC Signaling           :   2 events
      0xD Management          :   2 events
      0xE Reserved14          :   2 events
      0xF Reserved15          :   2 events
    Total critical PTP messages: 12/33 (36.4%)

    [PASS] Received 33 events (>= 5 required for 802.1AS traffic)
  [PASS] UT-TS-EVENT-001: RX Timestamp Event Delivery
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000002
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=13
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=13, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46950000
    Waiting for TX timestamp events (30 sec timeout)...
    Initial producer index: 0
    NOTE: TX events require Task 6b implementation (1ms polling)
  [SKIP] UT-TS-EVENT-002: TX Timestamp Event Delivery: No TX events received (Task 6b not implemented yet)

=== UT-TS-EVENT-003: Target Time Reached Event (Task 7) ===
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000004
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=14
Ô£ô Subscribed (ring_id=14)
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=14, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46960000
Ô£ô Ring buffer mapped (32840 bytes)
Current SYSTIM: 1211707381066 ns
    DEBUG: Before DeviceIoControl:
      Structure size: 40 bytes
      IOCTL code: 0x0017009C (expecting 0x001700AC)
      Handle: 00000000000000C0 (same as subscription: 00000000000000C0)
      timer_index: 0
      target_time: 1213707381066 ns
      enable_interrupt: 1
      status (sentinel): 0xFFFFFFFF
      previous_target (sentinel): 0xDEADBEEF
    DEBUG: About to call DeviceIoControl...
    DEBUG: DeviceIoControl RETURNED
      API result: 1
      GetLastError(): 1450 (0x000005AA)
      bytes_returned: 40 (expected ~36-40)
      status: 0x00000000 <-- ZERO (success or Windows zeroed it?)
      previous_target: 0xDEADBEEF <-- SENTINEL UNCHANGED!
Ô£ô Target time set to 1213707381066 ns (+ 2.0s)
  Polling ring buffer for 3 seconds...
  [FAIL] UT-TS-EVENT-003: Target Time Reached Event: No event received within 3 seconds - Task 7 not working!
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000008
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=15
    Aux timestamp event subscription created (ring_id=15)
    NOTE: Requires GPIO or external signal trigger
  [PASS] UT-TS-EVENT-004: Aux Timestamp Event
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=16
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=16, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46970000
    Event sequence state:
      Total events: 12884901888
      Producer index: 0, Consumer index: 0
  [PASS] UT-TS-EVENT-005: Event Sequence Numbering
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=17
  [PASS] UT-TS-EVENT-006: Event Filtering by Criteria
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=18
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=18, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46980000
  [PASS] UT-TS-RING-005: Ring Buffer Unmap Operation
  [SKIP] UT-TS-PERF-001: High Event Rate Performance: Requires sustained traffic generation (10K events/sec) - benchmark test
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=4294967295, length=65536
    DEBUG: DeviceIoControl result=0, bytes_returned=0, status=0x00000000, shm_token=0x0
    DEBUG: DeviceIoControl failed with error 87 (0x00000057)
  [PASS] UT-TS-ERROR-001: Invalid Subscription Handle
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=19
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=19, length=10485760
    DEBUG: DeviceIoControl result=0, bytes_returned=0, status=0x00000000, shm_token=0x0
    DEBUG: DeviceIoControl failed with error 1450 (0x000005AA)
  [PASS] UT-TS-ERROR-002: Ring Buffer Mapping Failure
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=20
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=20, length=4096
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46990000
    Small buffer overflow test:
      Buffer size: 32840 bytes (capacity_mask=0x3FF)
      Overflow count: 1024
      NOTE: Overflow requires high event rate (Task 6b TX polling)
  [PASS] UT-TS-ERROR-003: Event Overflow Notification

--------------------------------------------------------------------
 Adapter [4/6] Summary: \\.\IntelAvbFilter
--------------------------------------------------------------------
 Total:   19 tests
 Passed:  16 tests
 Failed:  1 tests
 Skipped: 2 tests
--------------------------------------------------------------------


********************************************************************
 ADAPTER [5/6]: \\.\IntelAvbFilter (Handle=00000000000000C4)
********************************************************************

Running Timestamp Event Subscription tests...

    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=2
  [PASS] UT-TS-SUB-001: Basic Event Subscription
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=3
  [PASS] UT-TS-SUB-002: Selective Event Type Subscription
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=4
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000002
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=5
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=6
    Created 3 subscriptions: ring_id=4, 5, 6
  [PASS] UT-TS-SUB-003: Multiple Concurrent Subscriptions
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=7
  [PASS] UT-TS-SUB-004: Unsubscribe Operation
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=8
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=8, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B469A0000
    Ring buffer mapped: 32840 bytes at 0000019B469A0000
  [PASS] UT-TS-RING-001: Ring Buffer Mapping
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=9
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=9, length=32768
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B469B0000
    Requested: 32768, Actual: 32840
  [PASS] UT-TS-RING-002: Ring Buffer Size Negotiation
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=10
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=10, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B469C0000
    Ring buffer state:
      Producer: 0, Consumer: 0, Mask: 0x3FF, Overflow: 1024, Total: 4294967296
  [PASS] UT-TS-RING-003: Ring Buffer Wraparound
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=11
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=11, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B469D0000
    Read synchronization test:
      Initial: Producer=0, Consumer=0
      After 10ms: Producer=0, Consumer=0
  [PASS] UT-TS-RING-004: Ring Buffer Read Synchronization
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=12
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=12, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B469E0000
    [DIAG] Enabling hardware timestamping...
    [DIAG] Enabling hardware timestamping for adapter 4...
    [DIAG] Global RX timestamp enabled (RXPBSIZE: 0x000000A2 -> 0x200000A2)
    [DIAG] Queue 0 timestamp enabled (SRRCTL[0]: 0x42000402 -> 0x42000402)
    [DIAG] HW timestamping features enabled (TSAUXC: 0x78000515 -> 0x78000515)
    [DIAG] Checking hardware timestamping configuration...
    [DIAG] Adapter 4 TSYNCRXCTL: 0x00000000 (RX_ENABLED=0, VALID=0)
    [WARN] RX timestamping NOT enabled in hardware!
    [DIAG] Ring buffer state: producer=0, consumer=0, capacity_mask=0x3FF
    [DIAG] Subscription ring_id=12, subscription address mapped at 0000019B469E0000
    Waiting for PTP RX events (30 sec timeout for 802.1AS/Milan traffic)...
    Initial producer index: 0
    Expected: ~30 Announce + Sync + Pdelay messages (1 sec intervals)
    [DIAG] 5 seconds elapsed, total events: 0
    [DIAG] 10 seconds elapsed, total events: 0
    [DIAG] 15 seconds elapsed, total events: 0
    [DIAG] 20 seconds elapsed, total events: 0
    [DIAG] 25 seconds elapsed, total events: 0
    [DIAG] 30 seconds elapsed, total events: 0
    [DIAG] Final ring buffer state: producer=0, consumer=0, overflow=1024
    [WARN] No events written to ring buffer - check driver AvbPostTimestampEvent() calls
  [SKIP] UT-TS-EVENT-001: RX Timestamp Event Delivery: No RX events received (no PTP traffic detected)
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000002
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=13
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=13, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B469F0000
    Waiting for TX timestamp events (30 sec timeout)...
    Initial producer index: 0
    NOTE: TX events require Task 6b implementation (1ms polling)
  [SKIP] UT-TS-EVENT-002: TX Timestamp Event Delivery: No TX events received (Task 6b not implemented yet)

=== UT-TS-EVENT-003: Target Time Reached Event (Task 7) ===
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000004
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=14
Ô£ô Subscribed (ring_id=14)
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=14, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46A00000
Ô£ô Ring buffer mapped (32840 bytes)
Current SYSTIM: 1517092936827 ns
    DEBUG: Before DeviceIoControl:
      Structure size: 40 bytes
      IOCTL code: 0x0017009C (expecting 0x001700AC)
      Handle: 00000000000000C4 (same as subscription: 00000000000000C4)
      timer_index: 0
      target_time: 1519092936827 ns
      enable_interrupt: 1
      status (sentinel): 0xFFFFFFFF
      previous_target (sentinel): 0xDEADBEEF
    DEBUG: About to call DeviceIoControl...
    DEBUG: DeviceIoControl RETURNED
      API result: 1
      GetLastError(): 1450 (0x000005AA)
      bytes_returned: 40 (expected ~36-40)
      status: 0x00000000 <-- ZERO (success or Windows zeroed it?)
      previous_target: 0xDEADBEEF <-- SENTINEL UNCHANGED!
Ô£ô Target time set to 1519092936827 ns (+ 2.0s)
  Polling ring buffer for 3 seconds...
  [FAIL] UT-TS-EVENT-003: Target Time Reached Event: No event received within 3 seconds - Task 7 not working!
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000008
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=15
    Aux timestamp event subscription created (ring_id=15)
    NOTE: Requires GPIO or external signal trigger
  [PASS] UT-TS-EVENT-004: Aux Timestamp Event
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=16
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=16, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46A10000
    Event sequence state:
      Total events: 12884901888
      Producer index: 0, Consumer index: 0
  [PASS] UT-TS-EVENT-005: Event Sequence Numbering
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=17
  [PASS] UT-TS-EVENT-006: Event Filtering by Criteria
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=18
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=18, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46A20000
  [PASS] UT-TS-RING-005: Ring Buffer Unmap Operation
  [SKIP] UT-TS-PERF-001: High Event Rate Performance: Requires sustained traffic generation (10K events/sec) - benchmark test
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=4294967295, length=65536
    DEBUG: DeviceIoControl result=0, bytes_returned=0, status=0x00000000, shm_token=0x0
    DEBUG: DeviceIoControl failed with error 87 (0x00000057)
  [PASS] UT-TS-ERROR-001: Invalid Subscription Handle
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=19
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=19, length=10485760
    DEBUG: DeviceIoControl result=0, bytes_returned=0, status=0x00000000, shm_token=0x0
    DEBUG: DeviceIoControl failed with error 1450 (0x000005AA)
  [PASS] UT-TS-ERROR-002: Ring Buffer Mapping Failure
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=20
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=20, length=4096
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46A30000
    Small buffer overflow test:
      Buffer size: 32840 bytes (capacity_mask=0x3FF)
      Overflow count: 1024
      NOTE: Overflow requires high event rate (Task 6b TX polling)
  [PASS] UT-TS-ERROR-003: Event Overflow Notification

--------------------------------------------------------------------
 Adapter [5/6] Summary: \\.\IntelAvbFilter
--------------------------------------------------------------------
 Total:   19 tests
 Passed:  15 tests
 Failed:  1 tests
 Skipped: 3 tests
--------------------------------------------------------------------


********************************************************************
 ADAPTER [6/6]: \\.\IntelAvbFilter (Handle=00000000000000C8)
********************************************************************

Running Timestamp Event Subscription tests...

    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=2
  [PASS] UT-TS-SUB-001: Basic Event Subscription
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=3
  [PASS] UT-TS-SUB-002: Selective Event Type Subscription
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=4
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000002
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=5
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=6
    Created 3 subscriptions: ring_id=4, 5, 6
  [PASS] UT-TS-SUB-003: Multiple Concurrent Subscriptions
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=7
  [PASS] UT-TS-SUB-004: Unsubscribe Operation
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=8
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=8, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46A40000
    Ring buffer mapped: 32840 bytes at 0000019B46A40000
  [PASS] UT-TS-RING-001: Ring Buffer Mapping
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=9
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=9, length=32768
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46A50000
    Requested: 32768, Actual: 32840
  [PASS] UT-TS-RING-002: Ring Buffer Size Negotiation
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=10
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=10, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46A60000
    Ring buffer state:
      Producer: 0, Consumer: 0, Mask: 0x3FF, Overflow: 1024, Total: 4294967296
  [PASS] UT-TS-RING-003: Ring Buffer Wraparound
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=11
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=11, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46A70000
    Read synchronization test:
      Initial: Producer=0, Consumer=0
      After 10ms: Producer=0, Consumer=0
  [PASS] UT-TS-RING-004: Ring Buffer Read Synchronization
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=12
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=12, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46A80000
    [DIAG] Enabling hardware timestamping...
    [DIAG] Enabling hardware timestamping for adapter 5...
    [DIAG] Global RX timestamp enabled (RXPBSIZE: 0x000000A2 -> 0x200000A2)
    [DIAG] Queue 0 timestamp enabled (SRRCTL[0]: 0x42000402 -> 0x42000402)
    [DIAG] HW timestamping features enabled (TSAUXC: 0x78000515 -> 0x78000515)
    [DIAG] Checking hardware timestamping configuration...
    [DIAG] Adapter 5 TSYNCRXCTL: 0x00000000 (RX_ENABLED=0, VALID=0)
    [WARN] RX timestamping NOT enabled in hardware!
    [DIAG] Ring buffer state: producer=0, consumer=0, capacity_mask=0x3FF
    [DIAG] Subscription ring_id=12, subscription address mapped at 0000019B46A80000
    Waiting for PTP RX events (30 sec timeout for 802.1AS/Milan traffic)...
    Initial producer index: 0
    Expected: ~30 Announce + Sync + Pdelay messages (1 sec intervals)
    [DIAG] 5 seconds elapsed, total events: 0
    [DIAG] 10 seconds elapsed, total events: 0
    [DIAG] 15 seconds elapsed, total events: 0
    [DIAG] 20 seconds elapsed, total events: 0
    [DIAG] 25 seconds elapsed, total events: 0
    [DIAG] 30 seconds elapsed, total events: 0
    [DIAG] Final ring buffer state: producer=0, consumer=0, overflow=1024
    [WARN] No events written to ring buffer - check driver AvbPostTimestampEvent() calls
  [SKIP] UT-TS-EVENT-001: RX Timestamp Event Delivery: No RX events received (no PTP traffic detected)
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000002
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=13
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=13, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46A90000
    Waiting for TX timestamp events (30 sec timeout)...
    Initial producer index: 0
    NOTE: TX events require Task 6b implementation (1ms polling)
  [SKIP] UT-TS-EVENT-002: TX Timestamp Event Delivery: No TX events received (Task 6b not implemented yet)

=== UT-TS-EVENT-003: Target Time Reached Event (Task 7) ===
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000004
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=14
Ô£ô Subscribed (ring_id=14)
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=14, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46AA0000
Ô£ô Ring buffer mapped (32840 bytes)
Current SYSTIM: 1825796198076 ns
    DEBUG: Before DeviceIoControl:
      Structure size: 40 bytes
      IOCTL code: 0x0017009C (expecting 0x001700AC)
      Handle: 00000000000000C8 (same as subscription: 00000000000000C8)
      timer_index: 0
      target_time: 1827796198076 ns
      enable_interrupt: 1
      status (sentinel): 0xFFFFFFFF
      previous_target (sentinel): 0xDEADBEEF
    DEBUG: About to call DeviceIoControl...
    DEBUG: DeviceIoControl RETURNED
      API result: 1
      GetLastError(): 1450 (0x000005AA)
      bytes_returned: 40 (expected ~36-40)
      status: 0x00000000 <-- ZERO (success or Windows zeroed it?)
      previous_target: 0xDEADBEEF <-- SENTINEL UNCHANGED!
Ô£ô Target time set to 1827796198076 ns (+ 2.0s)
  Polling ring buffer for 3 seconds...
  [FAIL] UT-TS-EVENT-003: Target Time Reached Event: No event received within 3 seconds - Task 7 not working!
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000008
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=15
    Aux timestamp event subscription created (ring_id=15)
    NOTE: Requires GPIO or external signal trigger
  [PASS] UT-TS-EVENT-004: Aux Timestamp Event
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=16
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=16, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46AB0000
    Event sequence state:
      Total events: 12884901888
      Producer index: 0, Consumer index: 0
  [PASS] UT-TS-EVENT-005: Event Sequence Numbering
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=17
  [PASS] UT-TS-EVENT-006: Event Filtering by Criteria
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=18
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=18, length=65536
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46AC0000
  [PASS] UT-TS-RING-005: Ring Buffer Unmap Operation
  [SKIP] UT-TS-PERF-001: High Event Rate Performance: Requires sustained traffic generation (10K events/sec) - benchmark test
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=4294967295, length=65536
    DEBUG: DeviceIoControl result=0, bytes_returned=0, status=0x00000000, shm_token=0x0
    DEBUG: DeviceIoControl failed with error 87 (0x00000057)
  [PASS] UT-TS-ERROR-001: Invalid Subscription Handle
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000001
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=19
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=19, length=10485760
    DEBUG: DeviceIoControl result=0, bytes_returned=0, status=0x00000000, shm_token=0x0
    DEBUG: DeviceIoControl failed with error 1450 (0x000005AA)
  [PASS] UT-TS-ERROR-002: Ring Buffer Mapping Failure
    DEBUG: Calling IOCTL_AVB_TS_SUBSCRIBE with types_mask=0x00000003
    DEBUG: DeviceIoControl result=1, bytes_returned=16, status=0x00000000, ring_id=20
    DEBUG: Calling IOCTL_AVB_TS_RING_MAP with ring_id=20, length=4096
    DEBUG: DeviceIoControl result=1, bytes_returned=32, status=0x00000000, shm_token=0x19B46AD0000
    Small buffer overflow test:
      Buffer size: 32840 bytes (capacity_mask=0x3FF)
      Overflow count: 1024
      NOTE: Overflow requires high event rate (Task 6b TX polling)
  [PASS] UT-TS-ERROR-003: Event Overflow Notification

--------------------------------------------------------------------
 Adapter [6/6] Summary: \\.\IntelAvbFilter
--------------------------------------------------------------------
 Total:   19 tests
 Passed:  15 tests
 Failed:  1 tests
 Skipped: 3 tests
--------------------------------------------------------------------


====================================================================
 CLEANUP: Closing all adapter handles
====================================================================
  Closing adapter 0 (handle=00000000000000B4)
  Closing adapter 1 (handle=00000000000000B8)
  Closing adapter 2 (handle=00000000000000BC)
  Closing adapter 3 (handle=00000000000000C0)
  Closing adapter 4 (handle=00000000000000C4)
  Closing adapter 5 (handle=00000000000000C8)


====================================================================
 OVERALL TEST SUMMARY (All Adapters)
====================================================================
 Adapters Tested: 6
 Total Tests:     114
 Passed:          92 tests
 Failed:          6 tests
 Skipped:         16 tests
====================================================================


================================================================
                       TEST SUMMARY
================================================================

Infrastructure Status:
  [OK] Driver service: Running
  [OK] Intel adapters: 6 detected
  [OK] Device node: Accessible
  [OK] Test executables: 73 available

Overall Assessment:
  => ALL SYSTEMS OPERATIONAL!
     Infrastructure validated, ready for testing.


For detailed kernel debug output, use DebugView:
  https://docs.microsoft.com/en-us/sysinternals/downloads/debugview

================================================================
  Test Suite Complete!
================================================================

Press any key to close...
