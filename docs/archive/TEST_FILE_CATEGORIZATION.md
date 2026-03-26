# Test File Categorization - Vollständige Einzelanalyse

**Zweck**: Einzelne Kategorisierung aller 51 Test-Quelldateien für Migration nach tests/  
**Status**: Loop 1 - Teil des Build-Tests.ps1 korrekten Mappings  
**Datum**: 2024-12-23

---

## Kategorisierungs-Schema

### Zielstruktur
```
tests/
├── unit/
│   ├── ioctl/          # IOCTL-spezifische Unit-Tests (isoliert, keine Hardware)
│   └── hardware/       # Hardware-Zugriff Unit-Tests (einzelne Register/Features)
├── integration/
│   ├── avb/            # AVB-Protokoll Integration Tests
│   ├── ptp/            # PTP Clock Integration Tests
│   ├── multi_adapter/  # Multi-Adapter Tests
│   └── tsn/            # TSN/TAS Integration Tests
├── e2e/                # End-to-End Comprehensive Tests
├── diagnostic/         # Diagnostic & Investigation Tools
├── device_specific/
│   ├── i210/           # i210-spezifische Tests
│   ├── i219/           # i219-spezifische Tests
│   └── i226/           # i226-spezifische Tests
├── taef/               # TAEF Framework Tests (bereits existiert)
└── user_mode/          # User-Mode Test Applications
```

---

## ROOT (6 Dateien) - Kategorisierung

| # | Datei | Größe | Zielkategorie | Begründung |
|---|-------|-------|---------------|------------|
| 1 | `avb_test.c` | 6.7 KB | `tests/integration/avb/avb_test.c` | Allgemeiner AVB-Test (nicht device-specific) |
| 2 | `avb_test_i210.c` | 564 B | `tests/device_specific/i210/avb_test_i210.c` | i210-spezifischer AVB-Test |
| 3 | `avb_test_i210_um.c` | 1.8 KB | `tests/device_specific/i210/avb_test_i210_um.c` | i210 User-Mode Test |
| 4 | `avb_test_i219.c` | 4.9 KB | `tests/device_specific/i219/avb_test_i219.c` | i219-spezifischer AVB-Test |
| 5 | `intel_avb_diagnostics.c` | 26.5 KB | `tests/diagnostic/intel_avb_diagnostics.c` | Umfassendes Diagnose-Tool |
| 6 | `quick_diagnostics.c` | 4.2 KB | `tests/diagnostic/quick_diagnostics.c` | Schnell-Diagnose-Tool |

**ROOT Zusammenfassung**: 
- 2 generic AVB → `integration/avb/`
- 3 device-specific → `device_specific/i210/`, `device_specific/i219/`
- 2 diagnostics → `diagnostic/`

---

## tools/ (11 .c Dateien) - Kategorisierung

| # | Datei | Zielkategorie | Begründung |
|---|-------|---------------|------------|
| 1 | `test_ioctl_simple.c` | `tests/unit/ioctl/test_ioctl_simple.c` | IOCTL Unit-Test (einfache IOCTLs) |
| 2 | `test_ioctl_routing.c` | `tests/unit/ioctl/test_ioctl_routing.c` | IOCTL Routing Logic Test |
| 3 | `test_ioctl_trace.c` | `tests/unit/ioctl/test_ioctl_trace.c` | IOCTL Call Tracing Test |
| 4 | `test_minimal_ioctl.c` | `tests/unit/ioctl/test_minimal_ioctl.c` | Minimal IOCTL Coverage Test |
| 5 | `test_tsn_ioctl_handlers.c` | `tests/unit/ioctl/test_tsn_ioctl_handlers.c` | TSN IOCTL Handler Unit-Test |
| 6 | `test_clock_config.c` | `tests/unit/hardware/test_clock_config.c` | Clock Configuration Unit-Test |
| 7 | `test_clock_working.c` | `tests/unit/hardware/test_clock_working.c` | Clock Functional Validation |
| 8 | `test_direct_clock.c` | `tests/unit/hardware/test_direct_clock.c` | Direct Clock Access Test |
| 9 | `test_hw_state.c` | `tests/unit/hardware/test_hw_state.c` | Hardware State Unit-Test |
| 10 | `test_extended_diag.c` | `tests/diagnostic/test_extended_diag.c` | Extended Diagnostic Test |
| 11 | `test_all_adapters.c` | `tests/integration/multi_adapter/test_all_adapters.c` | Multi-Adapter Integration Test |

**tools/ Zusammenfassung**:
- 5 IOCTL unit tests → `unit/ioctl/`
- 4 hardware unit tests → `unit/hardware/`
- 1 diagnostic → `diagnostic/`
- 1 multi-adapter → `integration/multi_adapter/`

---

## tools/avb_test/ (51 Dateien total, 34 .c/.cpp) - Kategorisierung

### Unit Tests (IOCTL)

| # | Datei | Zielkategorie | Begründung |
|---|-------|---------------|------------|
| 1 | `test_tsn_ioctl_handlers_um.c` | `tests/unit/ioctl/test_tsn_ioctl_handlers_um.c` | User-Mode TSN IOCTL Handler Test |

### Integration Tests - AVB

| # | Datei | Zielkategorie | Begründung |
|---|-------|---------------|------------|
| 2 | `avb_test_um.c` | `tests/user_mode/avb_test_um.c` | **WICHTIG**: Main user-mode AVB test (BUILD-TESTS REFERENZ) |
| 3 | `avb_test_actual.c` | `tests/integration/avb/avb_test_actual.c` | Actual AVB Integration Test |
| 4 | `avb_test_app.c` | `tests/user_mode/avb_test_app.c` | AVB Test Application |
| 5 | `avb_test_standalone.c` | `tests/integration/avb/avb_test_standalone.c` | Standalone AVB Test |
| 6 | `avb_test_user.c` | `tests/user_mode/avb_test_user.c` | AVB User-Mode Interface Test |
| 7 | `avb_test_user_main.c` | `tests/user_mode/avb_test_user_main.c` | AVB User-Mode Main Entry |
| 8 | `avb_comprehensive_test.c` | `tests/e2e/avb_comprehensive_test.c` | Comprehensive AVB E2E Test |
| 9 | `avb_capability_validation_test_um.c` | `tests/integration/avb/avb_capability_validation_test_um.c` | AVB Capability Validation |

### Integration Tests - PTP

| # | Datei | Zielkategorie | Begründung |
|---|-------|---------------|------------|
| 10 | `ptp_clock_control_test.c` | `tests/integration/ptp/ptp_clock_control_test.c` | **WICHTIG**: PTP Clock Control (BUILD-TESTS REFERENZ) |
| 11 | `ptp_clock_control_production_test.c` | `tests/integration/ptp/ptp_clock_control_production_test.c` | Production PTP Test |
| 12 | `ptp_bringup_fixed.c` | `tests/integration/ptp/ptp_bringup_fixed.c` | PTP Bringup Test (fixed version) |
| 13 | `diagnose_ptp.c` | `tests/diagnostic/diagnose_ptp.c` | PTP Diagnostic Tool |

### Integration Tests - TSN/TAS

| # | Datei | Zielkategorie | Begründung |
|---|-------|---------------|------------|
| 14 | `tsauxc_toggle_test.c` | `tests/integration/tsn/tsauxc_toggle_test.c` | TSAUXC Register Toggle Test |
| 15 | `chatgpt5_i226_tas_validation.c` | `tests/device_specific/i226/chatgpt5_i226_tas_validation.c` | i226 TAS Validation (specific) |
| 16 | `corrected_i226_tas_test.c` | `tests/device_specific/i226/corrected_i226_tas_test.c` | Corrected i226 TAS Test |
| 17 | `enhanced_tas_investigation.c` | `tests/diagnostic/enhanced_tas_investigation.c` | TAS Investigation Tool |
| 18 | `tsn_hardware_activation_validation.c` | `tests/integration/tsn/tsn_hardware_activation_validation.c` | TSN HW Activation Validation |
| 19 | `ssot_register_validation_test.c` | `tests/integration/tsn/ssot_register_validation_test.c` | SSOT Register Validation |
| 20 | `target_time_test.c` | `tests/integration/tsn/target_time_test.c` | Target Time Feature Test |

### Integration Tests - Multi-Adapter

| # | Datei | Zielkategorie | Begründung |
|---|-------|---------------|------------|
| 21 | `avb_multi_adapter_test.c` | `tests/integration/multi_adapter/avb_multi_adapter_test.c` | Multi-Adapter Integration Test |
| 22 | `avb_device_separation_test_um.c` | `tests/integration/multi_adapter/avb_device_separation_test_um.c` | Device Separation Test |

### E2E (End-to-End) Tests

| # | Datei | Zielkategorie | Begründung |
|---|-------|---------------|------------|
| 23 | `comprehensive_ioctl_test.c` | `tests/e2e/comprehensive_ioctl_test.c` | **WICHTIG**: Comprehensive IOCTL E2E (BUILD-TESTS KANDIDAT) |

### Diagnostic Tools

| # | Datei | Zielkategorie | Begründung |
|---|-------|---------------|------------|
| 24 | `avb_diagnostic_test.c` | `tests/diagnostic/avb_diagnostic_test.c` | AVB Diagnostic Test |
| 25 | `avb_diagnostic_test_um.c` | `tests/diagnostic/avb_diagnostic_test_um.c` | AVB Diagnostic (User-Mode) |
| 26 | `avb_hw_state_test.c` | `tests/diagnostic/avb_hw_state_test.c` | Hardware State Diagnostic |
| 27 | `avb_hw_state_test_um.c` | `tests/diagnostic/avb_hw_state_test_um.c` | HW State Diagnostic (User-Mode) |
| 28 | `hardware_investigation_tool.c` | `tests/diagnostic/hardware_investigation_tool.c` | Hardware Investigation Tool |
| 29 | `critical_prerequisites_investigation.c` | `tests/diagnostic/critical_prerequisites_investigation.c` | Prerequisites Investigation |
| 30 | `device_open_test.c` | `tests/diagnostic/device_open_test.c` | Device Open Diagnostic |

### Device-Specific Tests - i226

| # | Datei | Zielkategorie | Begründung |
|---|-------|---------------|------------|
| 31 | `avb_i226_test.c` | `tests/device_specific/i226/avb_i226_test.c` | i226 AVB Test |
| 32 | `avb_i226_advanced_test.c` | `tests/device_specific/i226/avb_i226_advanced_test.c` | i226 Advanced Test |

### Hardware Feature Tests

| # | Datei | Zielkategorie | Begründung |
|---|-------|---------------|------------|
| 33 | `rx_timestamping_test.c` | `tests/unit/hardware/rx_timestamping_test.c` | RX Timestamping Unit Test |
| 34 | `hw_timestamping_control_test.c` | `tests/unit/hardware/hw_timestamping_control_test.c` | HW Timestamping Control Test |

**tools/avb_test/ Zusammenfassung** (34 .c/.cpp):
- 1 IOCTL unit test → `unit/ioctl/`
- 2 hardware feature tests → `unit/hardware/`
- 9 AVB integration → `integration/avb/` + `user_mode/`
- 4 PTP integration → `integration/ptp/`
- 6 TSN/TAS integration → `integration/tsn/`
- 2 multi-adapter → `integration/multi_adapter/`
- 1 E2E → `e2e/`
- 7 diagnostics → `diagnostic/`
- 2 i226-specific → `device_specific/i226/`

---

## Gesamt-Zusammenfassung (51 Dateien)

| Zielkategorie | Anzahl | Quelle |
|---------------|--------|--------|
| **unit/ioctl/** | 6 | tools/ (5) + tools/avb_test/ (1) |
| **unit/hardware/** | 6 | tools/ (4) + tools/avb_test/ (2) |
| **integration/avb/** | 11 | ROOT (1) + tools/avb_test/ (10 inkl. user_mode) |
| **integration/ptp/** | 4 | tools/avb_test/ (4) |
| **integration/tsn/** | 6 | tools/avb_test/ (6) |
| **integration/multi_adapter/** | 3 | tools/ (1) + tools/avb_test/ (2) |
| **e2e/** | 2 | tools/avb_test/ (1) + ROOT (1 comprehensive) |
| **diagnostic/** | 10 | ROOT (2) + tools/ (1) + tools/avb_test/ (7) |
| **device_specific/i210/** | 2 | ROOT (2) |
| **device_specific/i219/** | 1 | ROOT (1) |
| **device_specific/i226/** | 4 | tools/avb_test/ (4) |
| **user_mode/** | 3 | tools/avb_test/ (3 - avb_test_um.c wichtig!) |
| **GESAMT** | **51** | ROOT (6) + tools/ (11) + tools/avb_test/ (34) |

---

## Kritische Build-Tests.ps1 Referenzen

Diese Dateien sind AKTUELL in Build-Tests.ps1 referenziert:

| Build-Tests Name | Aktuelle Source | Neue Source (nach Migration) |
|------------------|-----------------|------------------------------|
| `avb_test_i226` | `tools/avb_test/avb_test_um.c` | `tests/user_mode/avb_test_um.c` |
| `avb_test_i210` | `tools/avb_test/avb_test_um.c` | `tests/user_mode/avb_test_um.c` |
| `ptp_clock_control_test` | `tools/avb_test/ptp_clock_control_test.c` | `tests/integration/ptp/ptp_clock_control_test.c` |

**WICHTIG**: Build-Tests.ps1 muss nach Migration aktualisiert werden!

---

## Nächste Schritte (Teil von Loop 1)

1. ✅ **Kategorisierung abgeschlossen** (diese Datei)
2. ⏳ **Migration ausführen** (Phase 2 - Move-Item für 51 Dateien)
3. ⏳ **Build-Tests.ps1 aktualisieren** (3 Source-Pfade ändern)
4. ⏳ **Testen** (Build-Tests.ps1 nach Migration verifizieren)
5. ⏳ **Old directories cleanup** (tools/avb_test/ → archive?)

---

## Makefile-Dateien (.mak) - Nicht migrieren

tools/avb_test/ enthält auch 17 .mak Dateien (nmake Makefiles):
- `avb_capability_validation.mak`
- `avb_device_separation_validation.mak`
- `avb_diagnostic.mak`
- `avb_hw_state_test.mak`
- `avb_i226_advanced.mak`
- `avb_i226_test.mak`
- `avb_multi_adapter.mak`
- `avb_test.mak`
- `chatgpt5_i226_tas_validation.mak`
- `corrected_i226_tas_test.mak`
- `critical_prerequisites_investigation.mak`
- `enhanced_tas_investigation.mak`
- `hardware_investigation.mak`
- `tsn_hardware_activation_validation.mak`
- `tsn_ioctl_test.mak`

**Entscheidung**: Makefiles bleiben in tools/avb_test/ (Build-System, nicht Test-Source)

---

**Status**: ✅ Kategorisierung komplett - Ready für Migration (Loop 1 Phase 2)
