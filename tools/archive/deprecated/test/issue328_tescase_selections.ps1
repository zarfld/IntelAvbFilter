.\tools\setup\Install-Driver-Elevated.ps1 -Configuration Debug -Action Reinstall -CaptureDbgView

$cases = @(
  'TC-TAS-001',
  'TC-TAS-002',
  'TC-TAS-003',
  'TC-TAS-004',
  'TC-TAS-005',
  'TC-TAS-006',
  'TC-TAS-007',
  'TC-TAS-008',
  'TC-TAS-009',
  'TC-TAS-010'
)

foreach ($case in $cases) {
  Write-Host "==== Running $case ====" -ForegroundColor Cyan
  .\tools\test\Run-Tests-Elevated.ps1 -Configuration Debug `
    -TestName 'test_ioctl_tas.exe' `
    -TestArgs "--case $case" `
    -CaptureDbgView

  Write-Host "==== Running PHC stability after $case ====" -ForegroundColor Yellow
  .\tools\test\Run-Tests-Elevated.ps1 -Configuration Debug `
    -TestName 'test_ptp_phc_stability.exe' `
    -CaptureDbgView
}