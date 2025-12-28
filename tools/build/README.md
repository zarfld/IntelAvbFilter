# Build Scripts

**Which script should I use?**

## Primary Scripts (Production Use)

### Build-Driver.ps1 (Canonical)
```powershell
# Build debug configuration
.\Build-Driver.ps1 -Configuration Debug

# Build release configuration
.\Build-Driver.ps1 -Configuration Release

# Clean rebuild
.\Build-Driver.ps1 -Configuration Debug -Clean

# Build and sign
.\Build-Driver.ps1 -Configuration Debug -Sign
```

**Features**:
- Locates MSBuild automatically (Visual Studio 2022)
- Platform: x64 (hardcoded)
- Output: `build\x64\{Configuration}\IntelAvbFilter\`
- Timestamp stamping in .inf DriverVer field
- Catalog generation (.cat file)

## VS Code Tasks (Recommended)

Run from VS Code Command Palette (Ctrl+Shift+P → "Tasks: Run Task"):
- **build-driver-debug** - Build debug (incremental)
- **build-driver-release** - Build release (incremental)
- **rebuild-driver-release** - Clean + rebuild release

**Benefits**: Integrated terminal output, error parsing, default build task (Ctrl+Shift+B)

**Note**: Debug builds use incremental compilation - .sys only recompiled when source changes. Use clean rebuild to force .sys regeneration.

## Supporting Scripts

- **Build-Tests.ps1** - Build individual test executables
- **Build-And-Sign.ps1** - Build + sign workflow

## Archived Scripts (Historical Reference)

Moved to `tools/archive/deprecated/`:
- Build-AllTests-TrulyHonest.ps1 (duplicate functionality)
- build_i226_test.bat (replaced by Build-Tests.ps1)

## Build Output Structure

```
build/x64/{Configuration}/IntelAvbFilter/IntelAvbFilter/
├── IntelAvbFilter.sys      # Driver binary
├── IntelAvbFilter.inf      # Installation manifest (DriverVer stamped)
└── intelavbfilter.cat      # Catalog signature file
```

**Timestamp Behavior**:
- `.inf` and `.cat` - Updated every build (DriverVer stamping)
- `.sys` - Only updated when source code changes (incremental build)
