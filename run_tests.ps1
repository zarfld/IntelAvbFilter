# execute all tests
.\build\tools\avb_test\x64\Debug\avb_diagnostic_test.exe
.\build\tools\avb_test\x64\Debug\avb_hw_state_test.exe
.\build\tools\avb_test\x64\Debug\avb_i226_test.exe
Commands:
.\build\tools\avb_test\x64\Debug\avb_i226_test.exeinfo      
#    - Show I226 device information
.\build\tools\avb_test\x64\Debug\avb_i226_test.exe ptp          
# - Test I226 PTP timing verification
.\build\tools\avb_test\x64\Debug\avb_i226_test.exe tsn
#           - Test I226 TSN register access
.\build\tools\avb_test\x64\Debug\avb_i226_test.exe advanced
#      - Test I226 advanced TSN features
.\build\tools\avb_test\x64\Debug\avb_i226_test.exe all
#           - Run all tests (default)

#.\build\tools\avb_test\x64\Debug\avb_i226_test.exe reg-read <offset>
#       - Read specific I226 register
#.build\tools\avb_test\x64\Debug\avb_i226_test.exe reg-write <offset> <val>
#       - Write specific I226 register
.\build\tools\avb_test\x64\Debug\avb_i226_advanced_test.exe
.\build\tools\avb_test\x64\Debug\avb_multi_adapter_test.exe
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe ptp-unlock
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe ptp-bringup
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe ptp-probe
selftest
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe selftest
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe snapshot
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe snapshot-ssot
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe ptp-enable-ssot
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe ptp-probe
#.\build\tools\avb_test\x64\Debug\avb_test_i210.exe ptp-timinca <hex>
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe 
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe ptp-bringup
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe info
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe caps
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe ts-get
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe ts-set-now
#.\build\tools\avb_test\x64\Debug\avb_test_i210.exe reg-read <hexOff>
#.\build\tools\avb_test\x64\Debug\avb_test_i210.exe reg-write <hexOff> <hexVal>

.\build\tools\avb_test\x64\Debug\test_tsn_ioctl_handlers.exe