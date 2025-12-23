# Test Source Code Reorganization Plan - Issue #27

**Problem**: Test-Quellen sind über 3 Locations verstreut
**Ziel**: Saubere Struktur unter `tests/` nach Art des Tests

---

## CURRENT STATE (Chaos!) - DETAILLIERTE KATEGORISIERUNG

**Gesamt**: 51 Test-Quelldateien über 3 Locations verstreut

### 📍 ROOT/ (6 Dateien - FALSCH!)

| # | Datei | Größe | Zielkategorie | Begründung |
|---|-------|-------|---------------|------------|
| 1 | avb_test.c | 6.7 KB | integration/avb/ | Allgemeiner AVB-Test |
| 2 | avb_test_i210.c | 564 B | device_specific/i210/ | i210-spezifisch |
| 3 | avb_test_i210_um.c | 1.8 KB | device_specific/i210/ | i210 User-Mode |
| 4 | avb_test_i219.c | 4.9 KB | device_specific/i219/ | i219-spezifisch |
| 5 | intel_avb_diagnostics.c | 26.5 KB | diagnostic/ | Diagnose-Tool |
| 6 | quick_diagnostics.c | 4.2 KB | diagnostic/ | Schnell-Diagnose |

### 📍 tools/ (11 Dateien - FALSCH!)

| # | Datei | Zielkategorie | Typ |
|---|-------|---------------|-----|
| 1 | test_ioctl_simple.c | unit/ioctl/ | IOCTL Unit-Test |
| 2 | test_ioctl_routing.c | unit/ioctl/ | IOCTL Routing |
| 3 | test_ioctl_trace.c | unit/ioctl/ | IOCTL Tracing |
| 4 | test_minimal_ioctl.c | unit/ioctl/ | Minimal IOCTL |
| 5 | test_tsn_ioctl_handlers.c | unit/ioctl/ | TSN IOCTL Handler |
| 6 | test_clock_config.c | unit/hardware/ | Clock Config |
| 7 | test_clock_working.c | unit/hardware/ | Clock Validation |
| 8 | test_direct_clock.c | unit/hardware/ | Direct Clock |
| 9 | test_hw_state.c | unit/hardware/ | HW State |
| 10 | test_extended_diag.c | diagnostic/ | Extended Diag |
| 11 | test_all_adapters.c | integration/multi_adapter/ | Multi-Adapter |

### 📍 tools/avb_test/ - TEST-PROJEKTE (.mak + .c + _um.c)

**WICHTIG**: Test-Projekte = .mak + zugehörige .c/.cpp + _um.c ZUSAMMEN verschieben!

#### Integration/AVB Test-Projekte
| Projekt | Dateien | Ziel |
|---------|---------|------|
| avb_test | avb_test.mak, avb_test.c | integration/avb/ |
| avb_capability_validation | avb_capability_validation.mak, avb_capability_validation_test_um.c | integration/avb/ |

#### Integration/PTP Test-Projekte  
| Projekt | Dateien | Ziel |
|---------|---------|------|
| (keine .mak) | ptp_clock_control_test.c ⚠️ BUILD-TESTS | integration/ptp/ |
| (keine .mak) | ptp_clock_control_production_test.c | integration/ptp/ |
| (keine .mak) | ptp_bringup_fixed.c | integration/ptp/ |

#### Integration/TSN Test-Projekte
| Projekt | Dateien | Ziel |
|---------|---------|------|
| tsn_ioctl_test | tsn_ioctl_test.mak, test_tsn_ioctl_handlers_um.c | integration/tsn/ |
| tsn_hardware_activation_validation | tsn_hardware_activation_validation.mak, tsn_hardware_activation_validation.c | integration/tsn/ |
| (keine .mak) | tsauxc_toggle_test.c | integration/tsn/ |
| (keine .mak) | ssot_register_validation_test.c | integration/tsn/ |
| (keine .mak) | target_time_test.c | integration/tsn/ |

#### Integration/Multi-Adapter Test-Projekte
| Projekt | Dateien | Ziel |
|---------|---------|------|
| avb_multi_adapter | avb_multi_adapter.mak, avb_multi_adapter_test.c | integration/multi_adapter/ |
| avb_device_separation_validation | avb_device_separation_validation.mak, avb_device_separation_test_um.c | integration/multi_adapter/ |

#### Diagnostic Test-Projekte
| Projekt | Dateien | Ziel |
|---------|---------|------|
| avb_diagnostic | avb_diagnostic.mak, avb_diagnostic_test.c, avb_diagnostic_test_um.c | diagnostic/ |
| avb_hw_state_test | avb_hw_state_test.mak, avb_hw_state_test.c, avb_hw_state_test_um.c | diagnostic/ |
| hardware_investigation | hardware_investigation.mak, hardware_investigation_tool.c | diagnostic/ |
| critical_prerequisites_investigation | critical_prerequisites_investigation.mak, critical_prerequisites_investigation.c | diagnostic/ |
| enhanced_tas_investigation | enhanced_tas_investigation.mak, enhanced_tas_investigation.c | diagnostic/ |
| (keine .mak) | diagnose_ptp.c | diagnostic/ |
| (keine .mak) | device_open_test.c | diagnostic/ |

#### Device-Specific/i226 Test-Projekte
| Projekt | Dateien | Ziel |
|---------|---------|------|
| avb_i226_test | avb_i226_test.mak, avb_i226_test.c | device_specific/i226/ |
| avb_i226_advanced | avb_i226_advanced.mak, avb_i226_advanced_test.c | device_specific/i226/ |
| chatgpt5_i226_tas_validation | chatgpt5_i226_tas_validation.mak, chatgpt5_i226_tas_validation.c | device_specific/i226/ |
| corrected_i226_tas_test | corrected_i226_tas_test.mak, corrected_i226_tas_test.c | device_specific/i226/ |

#### Standalone Dateien (OHNE .mak - Legacy/Experimental)
| Datei | Ziel | Typ |
|-------|------|-----|
| **avb_test_um.c** ⚠️ | integration/avb/ | BUILD-TESTS REFERENZ |
| avb_test_actual.c | integration/avb/ | Legacy |
| avb_test_app.c | integration/avb/ | Legacy |
| avb_test_standalone.c | integration/avb/ | Legacy |
| avb_test_user.c | integration/avb/ | Legacy |
| avb_test_user_main.c | integration/avb/ | Legacy |
| avb_comprehensive_test.c | e2e/ | Comprehensive |
| comprehensive_ioctl_test.c | e2e/ | Comprehensive |
| rx_timestamping_test.c | integration/ptp/ | Feature Test |
| hw_timestamping_control_test.c | integration/ptp/ | Feature Test |

### 📍 tests/ (4 Dateien - RICHTIG, aber fast leer!)
- tests/integration/AvbIntegrationTests.c ✅ Bleibt
- tests/integration/AvbIntegrationTests.cpp ✅ Bleibt
- tests/taef/AvbTaefTests.c ✅ Bleibt
- tests/taef/AvbTaefTests.cpp ✅ Bleibt

---

## GESAMT-ZUSAMMENFASSUNG (51 Dateien)

| Zielkategorie | Anzahl | Quelle |
|---------------|--------|--------|
| **unit/ioctl/** | 6 | tools/ (5) + tools/avb_test/ (1) |
| **unit/hardware/** | 6 | tools/ (4) + tools/avb_test/ (2) |
| **integration/avb/** | 3 | ROOT (1) + tools/avb_test/ (2) |
| **integration/ptp/** | 3 | tools/avb_test/ (3 - diagnose_ptp → diagnostic) |
| **integration/tsn/** | 4 | tools/avb_test/ (4 - 2 i226 → device_specific) |
| **integration/multi_adapter/** | 3 | tools/ (1) + tools/avb_test/ (2) |
| **e2e/** | 2 | ROOT (1 avb_test) + tools/avb_test/ (1) |
| **diagnostic/** | 10 | ROOT (2) + tools/ (1) + tools/avb_test/ (7) |
| **device_specific/i210/** | 2 | ROOT (2) |
| **device_specific/i219/** | 1 | ROOT (1) |
| **device_specific/i226/** | 4 | tools/avb_test/ (4) |
| **user_mode/** | 3 | tools/avb_test/ (3 - avb_test_um.c wichtig!) |
| **taef/** | 4 | tests/ (4 - bereits korrekt) |
| **GESAMT** | **51** | ROOT (6) + tools/ (11) + tools/avb_test/ (34) |

---

## TARGET STRUCTURE (Clean!)

```
tests/
├── unit/                    # Unit-Tests (isolierte Funktionen)
│   ├── ioctl/
│   │   ├── test_ioctl_simple.c
│   │   ├── test_ioctl_routing.c
│   │   ├── test_ioctl_trace.c
│   │   ├── test_minimal_ioctl.c
│   │   └── test_tsn_ioctl_handlers.c
│   └── hardware/
│       ├── test_hw_state.c
│       ├── test_clock_config.c
│       └── test_clock_working.c
│
├── integration/             # Integrations-Tests (mehrere Komponenten)
│   ├── avb/
│   │   ├── AvbIntegrationTests.c    (EXISTIERT)
│   │   ├── AvbIntegrationTests.cpp  (EXISTIERT)
│   │   ├── avb_test.c               (von ROOT)
│   │   └── avb_hw_state_test.c      (von tools/avb_test)
│   ├── ptp/
│   │   ├── ptp_clock_control_test.c
│   │   ├── diagnose_ptp.c
│   │   └── ptp_bringup_fixes.c
│   └── multi_adapter/
│       ├── test_all_adapters.c
│       └── avb_multi_adapter_test.c
│
├── e2e/                     # End-to-End Tests (vollständige Szenarien)
│   ├── comprehensive_ioctl_test.c
│   ├── comprehensive_ioctl_test_7624.c
│   └── avb_i226_advanced_test.c
│
├── diagnostic/              # Diagnose-Tools (nicht eigentliche Tests)
│   ├── intel_avb_diagnostics.c     (von ROOT)
│   ├── quick_diagnostics.c         (von ROOT)
│   ├── avb_diagnostic.c
│   ├── avb_diagnostic_enhanced.c
│   └── test_extended_diag.c
│
├── device_specific/         # Geräte-spezifische Tests
│   ├── i210/
│   │   ├── avb_test_i210.c         (von ROOT)
│   │   └── avb_test_i210_um.c      (von ROOT)
│   ├── i219/
│   │   └── avb_test_i219.c         (von ROOT)
│   └── i226/
│       ├── avb_i226_test.c
│       ├── avb_i226_advanced_test.c
│       └── corrected_i226_test.c
│
├── taef/                    # TAEF Framework Tests
│   ├── AvbTaefTests.c              (EXISTIERT)
│   └── AvbTaefTests.cpp            (EXISTIERT)
│
└── user_mode/               # User-Mode Test Tools
    ├── avb_test_um.c               (von tools/avb_test)
    ├── avb_test_user.c
    ├── avb_test_user_mode.c
    └── device_open_test.c
```

---

## MIGRATION PLAN (Loop 2)

### Phase 1: Verzeichnisse erstellen
```powershell
New-Item -ItemType Directory -Path tests/unit/ioctl
New-Item -ItemType Directory -Path tests/unit/hardware
New-Item -ItemType Directory -Path tests/integration/avb
New-Item -ItemType Directory -Path tests/integration/ptp
New-Item -ItemType Directory -Path tests/integration/multi_adapter
New-Item -ItemType Directory -Path tests/e2e
New-Item -ItemType Directory -Path tests/diagnostic
New-Item -ItemType Directory -Path tests/device_specific/i210
New-Item -ItemType Directory -Path tests/device_specific/i219
New-Item -ItemType Directory -Path tests/device_specific/i226
New-Item -ItemType Directory -Path tests/user_mode
```

### Phase 2: Dateien verschieben (Script für Script)

**UNIT TESTS (5 IOCTL + 3 Hardware = 8 Dateien)**:
```powershell
# IOCTL Unit Tests
Move-Item tools/test_ioctl_simple.c tests/unit/ioctl/
Move-Item tools/test_ioctl_routing.c tests/unit/ioctl/
Move-Item tools/test_ioctl_trace.c tests/unit/ioctl/
Move-Item tools/test_minimal_ioctl.c tests/unit/ioctl/
Move-Item tools/test_tsn_ioctl_handlers.c tests/unit/ioctl/

# Hardware Unit Tests  
Move-Item tools/test_hw_state.c tests/unit/hardware/
Move-Item tools/test_clock_config.c tests/unit/hardware/
Move-Item tools/test_clock_working.c tests/unit/hardware/
```

**INTEGRATION TESTS (AVB + PTP + Multi-Adapter = ~10 Dateien)**:
```powershell
# AVB Integration
Move-Item avb_test.c tests/integration/avb/
Move-Item tools/avb_test/avb_hw_state_test.c tests/integration/avb/

# PTP Integration
Move-Item tools/avb_test/ptp_clock_control_test.c tests/integration/ptp/
Move-Item tools/avb_test/diagnose_ptp.c tests/integration/ptp/
Move-Item tools/avb_test/ptp_bringup_fixes.c tests/integration/ptp/

# Multi-Adapter
Move-Item tools/test_all_adapters.c tests/integration/multi_adapter/
Move-Item tools/avb_test/avb_multi_adapter_test.c tests/integration/multi_adapter/
```

**DIAGNOSTIC TOOLS (5 Dateien)**:
```powershell
Move-Item intel_avb_diagnostics.c tests/diagnostic/
Move-Item quick_diagnostics.c tests/diagnostic/
Move-Item tools/avb_test/avb_diagnostic.c tests/diagnostic/
Move-Item tools/avb_test/avb_diagnostic_enhanced.c tests/diagnostic/
Move-Item tools/test_extended_diag.c tests/diagnostic/
```

**DEVICE-SPECIFIC (3 Geräte, ~6 Dateien)**:
```powershell
# I210
Move-Item avb_test_i210.c tests/device_specific/i210/
Move-Item avb_test_i210_um.c tests/device_specific/i210/

# I219
Move-Item avb_test_i219.c tests/device_specific/i219/

# I226
Move-Item tools/avb_test/avb_i226_test.c tests/device_specific/i226/
Move-Item tools/avb_test/avb_i226_advanced_test.c tests/device_specific/i226/
Move-Item tools/avb_test/corrected_i226_test.c tests/device_specific/i226/
```

**E2E TESTS (Comprehensive Tests, ~3 Dateien)**:
```powershell
Move-Item tools/avb_test/comprehensive_ioctl_test.c tests/e2e/
Move-Item tools/avb_test/comprehensive_ioctl_test_7624.c tests/e2e/
```

**USER-MODE TOOLS (4 Dateien)**:
```powershell
Move-Item tools/avb_test/avb_test_um.c tests/user_mode/
Move-Item tools/avb_test/avb_test_user.c tests/user_mode/
Move-Item tools/avb_test/avb_test_user_mode.c tests/user_mode/
Move-Item tools/avb_test/device_open_test.c tests/user_mode/
```

### Phase 3: Build-Tests.ps1 aktualisieren

Nach Migration: Pfade in Build-Tests.ps1 anpassen:
- `tools/avb_test/avb_test_um.c` → `tests/user_mode/avb_test_um.c`
- `tools/avb_test/ptp_clock_control_test.c` → `tests/integration/ptp/ptp_clock_control_test.c`

### Phase 4: Alte Verzeichnisse aufräumen

```powershell
# tools/avb_test/ sollte leer sein (oder nur spezielle Dateien behalten)
# tools/*.c Tests sollten weg sein  
# ROOT/*.c Tests sollten weg sein
```

---

## BENEFITS

✅ **Klare Struktur** - Jeder Test hat logische Location basierend auf Art
✅ **Einfacher zu finden** - tests/unit/, tests/integration/, tests/e2e/
✅ **Besseres Build-System** - Kann Tests kategorieweise bauen
✅ **Standards-Konform** - Folgt üblicher Projekt-Organisation
✅ **Skalierbar** - Neue Tests haben klare Platzierung

---

## ACTION ITEMS

1. [ ] Alle 34 Dateien in tools/avb_test/ einzeln analysieren → Kategorisierung
2. [ ] Migration Script erstellen (Move-Item Batch)
3. [ ] Build-Tests.ps1 Pfade aktualisieren
4. [ ] Test ob alle Tests noch bauen
5. [ ] Alte Verzeichnisse säubern
6. [ ] Dokumentation aktualisieren (README.md, Build-Tests.ps1 Kommentare)

---

**NEXT**: Soll ich die vollständige Kategorisierung aller 34 tools/avb_test/ Dateien machen?
