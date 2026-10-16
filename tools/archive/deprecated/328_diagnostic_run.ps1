#Enable the channel
wevtutil sl Microsoft-Windows-NDIS/Diagnostic /ms:67108864
wevtutil sl Microsoft-Windows-NDIS/Diagnostic /e:true

.\tools\test\Run-Tests-Elevated.ps1 -Configuration Debug -TestName test_device_idle_regression -TestArgs 1800


#Export: 
wevtutil epl Microsoft-Windows-NDIS/Diagnostic logs\ndis_diag_<ts>.evtx
#Disable: 
wevtutil sl Microsoft-Windows-NDIS/Diagnostic /e:false
#Restore size: 
wevtutil sl Microsoft-Windows-NDIS/Diagnostic /ms:1052672

#Check the restore: 
wevtutil gl Microsoft-Windows-NDIS/Diagnostic should show enabled: false and maxSize 1052672.

#Evaluate: 
.\tools\test\Evaluate-IdleTransition.ps1 -LogFile logs\test_device_idle_regression.exe_<ts>.log -EventFile logs\ndis_diag_<ts>.evtx -ResultFile logs\idle_eval_<ts>.json
