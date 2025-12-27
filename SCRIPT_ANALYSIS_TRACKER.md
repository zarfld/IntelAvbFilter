# Script Analysis Tracker - Issue #27 Loop 1 & 2

**Systematisches Vorgehen:**
1. **Loop 1**: Jedes alte Script analysieren → Kanonische Scripts vervollständigen
2. **Loop 2**: Funktionalität testen → Bei positivem Test archivieren
3. **Danach**: Wrapper erstellen

---

## BUILD Scripts (tools/build/)

### 🎯 STATUS: ✅ KATEGORIE ABGESCHLOSSEN (6/6 Scripts archiviert)

### Kanonische Scripts (✅ BEHALTEN):
- ✅ **Build-Tests.ps1** - Canonical test build script (765 lines, builds 53 tests)
- ✅ **Build-Driver.ps1** - Canonical driver build script (353 lines)
- ✅ **Build-And-Sign.ps1** - Canonical CAT generation + signing script (308 lines)
- ✅ **Import-VisualStudioVars.ps1** - Helper für VS environment setup

### Alte Scripts (6/6 ARCHIVIERT ✅):

#### ✅ Test Build Scripts (Funktionalität: Build test executables) - KANONISCH VORHANDEN
- [x] **build_i226_test.bat** (27 lines) 🗂️ **ARCHIVIERT**
  - **Funktion**: Baut avb_test_i226.exe mit cl.exe via vs_compile.ps1
  - **Kanonisch**: ✅ Build-Tests.ps1 -TestName avb_test_i226
  - **Test**: ✅ FUNKTIONIERT (avb_test_i226.exe erfolgreich gebaut)
  - **Loop 2**: ✅ ABGESCHLOSSEN → tools/archive/deprecated/
  
- [x] **Build-AllTests-Honest.ps1** (131 lines) 🗂️ **ARCHIVIERT**
  - **Funktion**: Baut 10 Test-Tools mit nmake via vcvars64.bat, zeigt Erfolg/Fehler
  - **Kanonisch**: ✅ Build-Tests.ps1 (baut 53 Tests, davon 45 erfolgreich)
  - **Test**: ✅ FUNKTIONIERT - Canonical baut MEHR als alte Version
  - **Loop 2**: ✅ ABGESCHLOSSEN → tools/archive/deprecated/
  
- [x] **Build-AllTests-TrulyHonest.ps1** (147 lines) 🗂️ **ARCHIVIERT**
  - **Funktion**: Wie Build-AllTests-Honest.ps1, aber mit vollständiger Ausgabe (verbose)
  - **Kanonisch**: ✅ Build-Tests.ps1 -ShowDetails
  - **Test**: ✅ FUNKTIONIERT - Zeigt Build-Commands und Details
  - **Loop 2**: ✅ ABGESCHLOSSEN → tools/archive/deprecated/

#### ✅ Build & Sign Scripts (Funktionalität: CAT-Datei generieren + signieren)
- [x] **Build-And-Sign-Driver.ps1** (320 lines) 🗂️ **ARCHIVIERT**
  - **Funktion**: CAT-File Generation (makecat.exe) + Zertifikat erstellen (makecert.exe) + Signierung (signtool.exe)
  - **Kanonisch**: ✅ Build-And-Sign.ps1 (verbessert: inf2cat Support, INF+SYS Hashes, build\x64\Debug Pfad)
  - **Test**: ✅ FUNKTIONIERT - Generiert IntelAvbFilter.cat erfolgreich
  - **Loop 2**: ✅ ABGESCHLOSSEN → tools/archive/deprecated/
  - **Bonus**: 🎯 Build-Pfade standardisiert (x64\Debug → build\x64\Debug) in Build-And-Sign.ps1 + Build-Driver.ps1

#### ✅ CAT File Generation (Funktionalität: .cat Datei erzeugen)
- [x] **Generate-CATFile.ps1** (99 lines) 🗂️ **ARCHIVIERT**
  - **Funktion**: Generiert IntelAvbFilter.cat mit makecat.exe aus .cdf Datei
  - **Kanonisch**: ✅ Build-And-Sign.ps1 (enthält CAT-Generierung + mehr Features)
  - **Test**: ✅ FUNKTIONIERT - Build-And-Sign.ps1 generiert CAT erfolgreich (bereits in Script 4 getestet)
  - **Loop 2**: ✅ ABGESCHLOSSEN → tools/archive/deprecated/
  - **Hinweis**: Build-And-Sign.ps1 ist BESSER (inf2cat Support, INF+SYS Hashes)

- [x] **build_all_tests.cmd** 🗂️ **ARCHIVIERT**
  - **Funktion**: Batch-Wrapper für Test-Builds
  - **Kanonisch**: ✅ Build-Tests.ps1
  - **Status**: Bereits archiviert (Redundant zu Build-Tests.ps1)
  - **Loop 2**: ✅ ABGESCHLOSSEN → tools/archive/deprecated/

#### ⚠️ NICHT-BUILD Scripts (Falsch kategorisiert - gehören zu anderen Kategorien)
- [x] **fix_deployment_config.ps1** (86 lines) ✅ **BEHALTEN in tools/development/**
  - **Kategorie**: 🛠️ DEVELOPMENT-Tool (NICHT Build!)
  - **Funktion**: One-time fix für VS WDK Deployment-Fehler (deaktiviert Remote Deployment in .vcxproj)
  - **Kanonisch**: ❌ NEIN - Spezialisiertes Development-Utility
  - **Entscheidung**: BEHALTEN in tools/development/ (korrekte Kategorie)
  - **Keine Archivierung**: Legitimes Tool für Development-Workflow
  
- [x] **fix_test_signing.bat** (180 lines)
  - **Kategorie**: 🔧 SETUP-Tool (NICHT Build!)
  - **Funktion**: Aktiviert Test Signing (bcdedit), deaktiviert Secure Boot Warnung
  - **Action**: → Verschieben zu SETUP-Kategorie (Analyse dort)

- [x] **Fix-And-Install.bat** (93 lines)
  - **Kategorie**: 🔧 SETUP-Tool (NICHT Build!)
  - **Funktion**: Kombiniert sc stop/delete + netcfg -u + pnputil delete + INF install
  - **Action**: → Verschieben zu SETUP-Kategorie (Analyse dort)

---

## 🎯 BUILD-Kategorie ABGESCHLOSSEN!

**Zusammenfassung BUILD Scripts:**
- ✅ 6/6 echte BUILD-Scripts analysiert und archiviert
- ✅ 3 kanonische Scripts bleiben: Build-Tests.ps1, Build-Driver.ps1, Build-And-Sign.ps1
- ✅ Bonus: Build-Pfade standardisiert (build\x64\Debug)
- ⚠️ 3 Scripts waren falsch kategorisiert (gehören zu SETUP/DEVELOPMENT)

**BUILD-Kategorie: 100% ABGESCHLOSSEN** ✅

---

## SETUP Scripts (tools/setup/)

### Kanonische Scripts (✅ BEHALTEN):
- ✅ **Install-Driver.ps1** - Canonical installation (ENHANCED with -Method, ⚠️ NEEDS TEST)
- ✅ **Install-Certificate.ps1** - Canonical certificate installation (⚠️ NOT TESTED)

### Alte Scripts (⏳ ANALYSE PENDING):
- [ ] **Complete-Driver-Setup.bat** - ❓ Was tut es?
- [ ] **Enable-TestSigning.bat** - ❓ Was tut es?
- [ ] **install_certificate_method.bat** - ❓ Was tut es?
- [ ] **install_devcon_method.bat** - ❓ Was tut es?
- [ ] **install_filter_proper.bat** - 🌟 REFERENCE! netcfg method
- [ ] **install_fixed_driver.bat** - ❓ Was tut es?
- [ ] **install_ndis_filter.bat** - ❓ Was tut es?
- [ ] **install_smart_test.bat** - ❓ Was tut es?
- [ ] **Install-AvbFilter.ps1** - ❓ Was tut es?
- [ ] **Install-Debug-Driver.bat** - ❓ Was tut es?
- [ ] **Install-NewDriver.bat** - ❓ Was tut es?
- [ ] **Install-Now.bat** - ❓ Was tut es?
- [ ] **setup_driver.ps1** - ❓ Was tut es?
- [ ] **setup_hyperv_development.bat** - ❓ Was tut es?
- [ ] **setup_hyperv_vm_complete.bat** - ❓ Was tut es?
- [ ] **Setup-Driver.bat** - ❓ Was tut es?
- [ ] **Setup-Driver.ps1** - ❓ Was tut es?
- [ ] **troubleshoot_certificates.ps1** - ❓ Was tut es?

---

## DEVELOPMENT Scripts (tools/development/)

### Kanonische Scripts (✅ BEHALTEN):
- ✅ **Check-System.ps1** - Canonical diagnostics (TESTED ✓)
- ⚠️ **Force-Driver-Reload.ps1** - REDUNDANT (Funktionalität in Install-Driver.ps1 -Reinstall)

### Alte Scripts (⏳ ANALYSE PENDING):
- [ ] **diagnose_capabilities.ps1** - ❓ Was tut es?
- [ ] **enhanced_investigation_suite.ps1** - ❓ Was tut es?
- [ ] **force_driver_reload.ps1** - ✓ ANALYSIERT: netcfg uninstall → install → test
- [ ] **Force-StartDriver.ps1** - ❓ Was tut es?
- [ ] **IntelAvbFilter-Cleanup.ps1** - ❓ Was tut es?
- [ ] **quick_start.ps1** - ❓ Was tut es?
- [ ] **reinstall_debug_quick.bat** - ✓ ANALYSIERT: pnputil disable → delete → add → enable
- [ ] **reinstall-and-test.bat** - ✓ ANALYSIERT: sc stop → delete → Install-Debug-Driver.bat
- [ ] **Smart-Update-Driver.bat** - ❓ Was tut es?
- [ ] **Start-AvbDriver.ps1** - ❓ Was tut es?
- [ ] **Update-Driver-Quick.bat** - ❓ Was tut es?

---

## TEST Scripts (tools/test/)

### Kanonische Scripts (✅ BEHALTEN):
- ✅ **Run-Tests.ps1** - Canonical test execution (TESTED ✓)

### Alte Scripts (⏳ ANALYSE PENDING):
- [ ] **Quick-Test-Debug.bat** - ❓ Was tut es?
- [ ] **Quick-Test-Release.bat** - ❓ Was tut es?
- [ ] **Reboot-And-Test.ps1** - ❓ Was tut es?
- [ ] **run_complete_diagnostics.bat** - ❓ Was tut es?
- [ ] **run_test_admin.ps1** - ❓ Was tut es?
- [ ] **run_tests.ps1** - ❓ Was tut es? (vs Run-Tests.ps1)
- [ ] **test_driver.ps1** - ❓ Was tut es?
- [ ] **test_hardware_only.bat** - ❓ Was tut es?
- [ ] **test_local_i219.bat** - ❓ Was tut es?
- [ ] **test_secure_boot_compatible.bat** - ❓ Was tut es?
- [ ] **Test-Real-Release.bat** - ❓ Was tut es?
- [ ] **Test-Release.bat** - ❓ Was tut es?

---

## UTILITIES (tools/ root level)

### Scripts (⏳ ANALYSE PENDING):
- [ ] **build_tsauxc_test.ps1** - ❓ Was tut es?
- [ ] **vs_compile.ps1** - ❓ Hilfsskript? Kanonisch?

---

## STATISTICS

**Kanonische Scripts:**
- Build-Driver.ps1 ✅ TESTED
- Sign-Driver.ps1 ⚠️ NOT TESTED
- Install-Driver.ps1 ✅ ENHANCED (⚠️ NEW FEATURES NOT TESTED)
- Install-Certificate.ps1 ⚠️ NOT TESTED
- Check-System.ps1 ✅ TESTED
- Run-Tests.ps1 ✅ TESTED
- Force-Driver-Reload.ps1 ⚠️ REDUNDANT (wird archiviert)

**Alte Scripts zu analysieren:** 51 Scripts
**Bereits analysiert:** 3 Scripts (force_driver_reload.ps1, reinstall_debug_quick.bat, reinstall-and-test.bat)
**Verbleibend:** 48 Scripts

---

## NÄCHSTER SCHRITT (Loop 1)

Starte mit **BUILD** Kategorie - Script für Script:
1. build_i226_test.bat
2. Build-AllTests-Honest.ps1
3. Build-AllTests-TrulyHonest.ps1
4. ... etc.
