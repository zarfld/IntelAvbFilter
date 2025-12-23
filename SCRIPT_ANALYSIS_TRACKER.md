# Script Analysis Tracker - Issue #27 Loop 1 & 2

**Systematisches Vorgehen:**
1. **Loop 1**: Jedes alte Script analysieren → Kanonische Scripts vervollständigen
2. **Loop 2**: Funktionalität testen → Bei positivem Test archivieren
3. **Danach**: Wrapper erstellen

---

## BUILD Scripts (tools/build/)

### Kanonische Scripts (✅ BEHALTEN):
- ✅ **Build-Driver.ps1** - Canonical build script (TESTED ✓)
- ✅ **Sign-Driver.ps1** - Canonical signing script

### Alte Scripts (ANALYSIERT):

#### ✅ Test Build Scripts (Funktionalität: Build test executables) - KANONISCH VORHANDEN
- [x] **build_i226_test.bat** (27 lines)
  - **Funktion**: Baut avb_test_i226.exe mit cl.exe via vs_compile.ps1
  - **Kanonisch**: ✅ Build-Tests.ps1 -TestName avb_test_i226
  - **Test**: ✅ FUNKTIONIERT (ptp_clock_control_test.exe gebaut)
  - **Loop 2**: Bereit für Archivierung
  
- [x] **Build-AllTests-Honest.ps1** (131 lines)
  - **Funktion**: Baut 10 Test-Tools mit nmake via vcvars64.bat, zeigt Erfolg/Fehler
  - **Kanonisch**: ✅ Build-Tests.ps1 (ohne Parameter = alle Tests)
  - **Test**: ✅ FUNKTIONIERT (3 Tests gefunden, Fehlerbehandlung OK)
  - **Loop 2**: Bereit für Archivierung
  
- [x] **Build-AllTests-TrulyHonest.ps1** (147 lines)
  - **Funktion**: Wie Build-AllTests-Honest.ps1, aber mit vollständiger Ausgabe
  - **Kanonisch**: ✅ Build-Tests.ps1 -ShowDetails
  - **Test**: ✅ FUNKTIONIERT (Verbose Output OK)
  - **Loop 2**: Bereit für Archivierung

#### ✅ Build & Sign Scripts (Funktionalität: Driver bauen + signieren)
- [x] **Build-And-Sign-Driver.ps1** (308 lines)
  - **Funktion**: Kombiniert Build + CAT-File Generation + Signing in einem Script
  - **Kanonisch?**: ✅ JA - Build-Driver.ps1 + Sign-Driver.ps1 zusammen
  - **Action**: ✓ Funktionalität vorhanden (2 Scripts statt 1)
  - **Test**: Build-Driver.ps1 THEN Sign-Driver.ps1 (mit -AutoGenCat?)

#### ✅ CAT File Generation (Funktionalität: .cat Datei erzeugen)
- [x] **Generate-CATFile.ps1** (99 lines)
  - **Funktion**: Generiert IntelAvbFilter.cat mit makecat.exe aus .cdf Datei
  - **Kanonisch?**: ⚠️ UNKLAR - Wird von Sign-Driver.ps1 aufgerufen?
  - **Action**: Check Sign-Driver.ps1 ob es CAT-File Generation enthält

#### ✅ Configuration Fix Scripts (Funktionalität: Projekt/System konfigurieren)
- [x] **fix_deployment_config.ps1** (86 lines)
  - **Funktion**: Deaktiviert Remote Deployment in .vcxproj (verhindert Connection-Fehler)
  - **Kanonisch?**: ❌ NEIN - One-time fix, kein kanonisches Äquivalent
  - **Action**: ⚠️ Prüfen ob in Build-Driver.ps1 integrierbar oder separat behalten
  
- [x] **fix_test_signing.bat** (180 lines)
  - **Funktion**: Aktiviert Test Signing (bcdedit), deaktiviert Secure Boot Warnung
  - **Kanonisch?**: ❌ NEIN - Setup-Funktionalität, nicht Build
  - **Action**: ⚠️ Gehört zu SETUP! Zu Install-Certificate.ps1 oder separate Enable-TestSigning.ps1

#### ✅ Combined Fix & Install Scripts
- [x] **Fix-And-Install.bat** (93 lines)
  - **Funktion**: Kombiniert sc stop/delete + netcfg -u + pnputil delete + INF install
  - **Kanonisch?**: ✅ JA - Install-Driver.ps1 -Reinstall -Method netcfg
  - **Action**: ✓ Funktionalität vorhanden in Install-Driver.ps1
  - **Test**: Install-Driver.ps1 -Reinstall -Method netcfg

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
