Intel AVB Filter Driver - Test and Debug Suite

Overview
- **Verification tests** (PowerShell): Standards compliance and code quality checks (SSOT, traceability).
- Integration tests (standalone EXE): fast smoke and stress checks using the public IOCTL ABI.
- TAEF tests (optional): Windows Test Authoring and Execution Framework tests for HLK-like automation.
- Error-path scenarios: basic link toggle and resilience checks.
- WHQL readiness aids: Driver Verifier profile, SDV/HCK notes.

Prerequisites
- Built and installed IntelAvbFilter driver exposing \\.\IntelAvbFilter device.
- Admin shell for tests that toggle adapter state.
- Optional: Windows SDK with TAEF (te.exe, WEX headers/libs) for TAEF tests.

Integration Tests (standalone)
- Build: nmake -f tests\\integration\\Makefile.mak [CFG=Debug|Release] [PLATFORM=x64|Win32]
- Run: build\\tests\\integration\\<platform>\\<cfg>\\AvbIntegrationTests.exe
- Environment (optional): set AVB_ADAPTER_NAME="<friendly name>" to enable link toggle test.

Covers
- IOCTL handshake: init + device info
- gPTP: set/get roundtrip (if supported)
- Timestamp monotonicity (10k samples)
- IOCTL throughput (1s)
- Error path: disable/enable interface, recovery check

TAEF Tests (optional)
- Source: tests\\taef\\AvbTaefTests.cpp
- Build (requires TAEF env): nmake -f tests\\taef\\Makefile.mak TAEF_INCLUDE="C:\\Program Files (x86)\\Windows Kits\\10\\Include\\<ver>\\Wex" TAEF_LIB="C:\\Program Files (x86)\\Windows Kits\\10\\Lib\\<ver>\\um\\x64"
- Run: te.exe AvbTaefTests.dll

Throughput/Latency and Loopback
- High-rate AVTP on-wire validation is best done externally (e.g., NPCAP/iperf/windump + Wireshark with 802.1AS dissector) while sampling IOCTL timestamps. Add your traffic generator of choice and record pcap concurrently.
- Loopback TX/RX timestamp correlation requires NIC hardware timestamping hooks in your environment; this suite focuses on the driver IOCTL surface. Extend as needed once capture path is plumbed.

Windows-specific scenarios
- Fast user switch: no driver impact expected; validate with the TAEF suite running across user changes.
- Driver reinstall/upgrade: use pnputil /add-driver IntelAvbFilter.inf /install. Validate IOCTLs post-upgrade.
- Boot safety: run Driver Verifier + WPR boot trace; ensure no hangs. See tools\\whql_prep\\DriverVerifier.ps1.

WHQL/HLK Readiness
- Enable Driver Verifier with standard NDIS checks.
- Run Static Driver Verifier (SDV) on the project (Visual Studio Driver menu).
- Clean WPP/trace usage; avoid deprecated APIs. Ensure PASS for NDIS and KM tests in HLK.

Verification Tests (Standards Compliance)
- Location: `tests\verification\ssot\`
- Purpose: Verify ISO/IEC/IEEE standards compliance and architectural decisions
- **TEST-SSOT-001** (#301): Verify no duplicate IOCTL definitions (Issue #24)
- **TEST-SSOT-002** (#300): Verify CI workflow catches SSOT violations
- **TEST-SSOT-003** (#302): Verify all files use SSOT header include
- **TEST-SSOT-004** (#303): Verify SSOT header completeness
- Run all: `.\tests\verification\ssot\Run-All-SSOT-Tests.ps1`
- Integrated: Runs automatically in `Quick` and `Full` test modes
- See: `tests\verification\ssot\README.md` for detailed documentation

Notes
- All tests use the public IOCTL contract in include/avb_ioctl.h.
- Hardware-first: tests exercise real paths; no simulation toggles are used.
- **SSOT tests run first** in Full mode to ensure code quality before hardware tests.
