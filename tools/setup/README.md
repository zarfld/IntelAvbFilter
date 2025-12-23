# Setup Scripts

**Which script should I use?**

## Quick Start (Most Common)

```batch
Install-Debug-Driver.bat    # Install debug build + enable test signing
Install-Release-Driver.bat  # Install release build
Uninstall-Driver.bat         # Remove driver completely
```

## Advanced Setup

```powershell
Setup-Driver.ps1             # Full-featured (accepts parameters)
  -Configuration Debug|Release
  -EnableTestSigning
  -InstallDriver
  -UninstallDriver
  -CheckStatus
```

## Reference Implementations (Educational)

- **install_filter_proper.bat** - THE correct NDIS filter install method (42 lines)
  - Comment: "CRITICAL: NDIS Lightweight Filters require netcfg.exe, not pnputil!"
  - Workflow: sc stop → netcfg -u → netcfg -l → sc start
  - Use this to understand NDIS filter installation

## Special Cases

- **install_certificate_method.bat** - Secure Boot compatible (uses pnputil)
- **Enable-TestSigning.bat** - Only enable test signing (no install)
- **troubleshoot_certificates.ps1** - Certificate diagnostics
- **setup_hyperv_*.bat** - Hyper-V VM environment setup

## Deprecated (Don't Use)

Moved to `tools/archive/deprecated/`:
- install_devcon_method.bat (uses deprecated devcon.exe)
- Install-Now.bat (dangerous - no cleanup)
