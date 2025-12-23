# Build Scripts

**Which script should I use?**

## Quick Start (Most Common)

```batch
Build-Debug.bat              # Build debug configuration
Build-Release.bat            # Build release configuration
Build-And-Sign.bat           # Build + sign with test certificate
```

## Advanced Build

```powershell
Build-Driver.ps1             # Full-featured (accepts parameters)
  -Configuration Debug|Release
  -Platform x64
  -Sign
  -BuildTests
```

## Special Cases

- **Sign-Driver.ps1** - Separate signing tool (German)
  - Creates test certificate
  - Signs CAT file
  - Searches multiple WDK SDK versions

- **Build-AllTests-Honest.ps1** - Build all 10 test executables
  - Tracks success/failure
  - Verifies .exe exists after build

## Deprecated (Don't Use)

Moved to `tools/archive/deprecated/`:
- Build-AllTests-TrulyHonest.ps1 (duplicate of Build-AllTests-Honest)
- build_i226_test.bat (use Build-Driver.ps1 -BuildTests)
