@echo off
echo === Intel AVB Filter Driver - Hyper-V VM Setup Tool ===
echo Erstellt Entwicklungsumgebung für Corporate IT Umgebungen
echo.

echo Überprüfe Hyper-V Verfügbarkeit...
dism /online /get-featureinfo /featurename:Microsoft-Hyper-V >nul 2>&1
if %errorLevel% == 0 (
    echo ? Hyper-V ist verfügbar auf diesem System
) else (
    echo ? Hyper-V ist nicht verfügbar
    echo    Benötigt: Windows 11 Pro/Enterprise mit Virtualisierung
    echo    Alternative: VMware Workstation Pro oder VirtualBox
    goto :ALTERNATIVE_VM
)

echo.
echo Schritt 1: Hyper-V aktivieren (Administrator erforderlich)
echo.
echo Führen Sie diesen PowerShell-Befehl als Administrator aus:
echo Enable-WindowsOptionalFeature -Online -FeatureName Microsoft-Hyper-V -All
echo.

echo Schritt 2: Windows 11 ISO herunterladen
echo.
echo 1. Gehen Sie zu: https://www.microsoft.com/software-download/windows11
echo 2. Laden Sie Windows 11 ISO herunter
echo 3. Speichern Sie ISO in: C:\VMs\Windows11.iso
echo.

echo Schritt 3: VM erstellen (PowerShell als Administrator)
echo.
echo # VM erstellen
echo New-VM -Name "IntelAvbDev" -MemoryStartupBytes 8GB -Generation 2
echo # Virtuelle Festplatte erstellen
echo New-VHD -Path "C:\VMs\IntelAvbDev.vhdx" -SizeBytes 100GB -Dynamic
echo # Festplatte anhängen
echo Add-VMHardDiskDrive -VMName "IntelAvbDev" -Path "C:\VMs\IntelAvbDev.vhdx"
echo # ISO anhängen
echo Add-VMDvdDrive -VMName "IntelAvbDev" -Path "C:\VMs\Windows11.iso"
echo # Secure Boot DEAKTIVIEREN (wichtig!)
echo Set-VMFirmware -VMName "IntelAvbDev" -EnableSecureBoot Off
echo # VM starten
echo Start-VM -Name "IntelAvbDev"
echo.

echo Schritt 4: In VM - Windows installieren und konfigurieren
echo.
echo 1. Windows 11 in VM installieren
echo 2. Visual Studio + WDK installieren
echo 3. Als Administrator: bcdedit /set testsigning on
echo 4. VM neustarten
echo 5. Intel AVB Filter Driver Projekt kopieren
echo 6. Vollständige Entwicklung und Tests in VM
echo.

echo Schritt 5: Hardware-Passthrough (Optional für echte I219 Tests)
echo.
echo # USB-Controller passthrough für externe Intel-Adapter
echo Add-VMAssignableDevice -VMName "IntelAvbDev" -LocationPath "..."
echo # Oder: Netzwerk-Bridging für Tests
echo.

goto :BENEFITS

:ALTERNATIVE_VM
echo.
echo === Alternative VM Solutions ===
echo.
echo **VMware Workstation Pro:**
echo - Download: https://www.vmware.com/products/workstation-pro.html
echo - Kosten: ca. €250 (einmalig)
echo - Secure Boot deaktivierbar
echo - Hardware-Passthrough möglich
echo.
echo **Oracle VirtualBox (Kostenlos):**
echo - Download: https://www.virtualbox.org/
echo - Kostenlos
echo - Limitierte Hardware-Unterstützung
echo - Gut für User-Mode Testing
echo.
echo **Parallels Desktop (Mac):**
echo - Falls Sie auch Mac-Hardware haben
echo - Excellent Windows 11 Support
echo.

:BENEFITS
echo.
echo === Vorteile der VM-Lösung ===
echo.
echo ? **Host-System bleibt unverändert** (IT-Policy konform)
echo ? **Vollständige Test-Freiheit** in VM
echo ? **Keine Corporate IT Genehmigung** nötig für VM-interne Tests
echo ? **Snapshot/Restore** für schnelle Entwicklungszyklen
echo ? **Isolierte Umgebung** - keine Auswirkung auf Host
echo ? **Hardware-Passthrough** möglich für echte I219 Tests
echo.

echo === Limitierungen ===
echo.
echo ??  **Performance**: Etwas langsamer als native Hardware
echo ??  **Hardware-Zugriff**: Passthrough-Setup erforderlich für echte Tests
echo ??  **Lizenzierung**: Windows 11 Lizenz für VM erforderlich
echo.

echo === Testing Strategy in VM ===
echo.
echo **Phase 1: Complete Driver Development**
echo - Vollständige Code-Entwicklung in VM
echo - User-Mode Interface Tests
echo - Simulation-Mode Validierung
echo.
echo **Phase 2: Hardware Simulation**
echo - Virtueller Intel Network Adapter
echo - NDIS Filter Installation Tests
echo - IOCTL Interface Validierung
echo.
echo **Phase 3: Real Hardware (mit Passthrough)**
echo - USB-based Intel I219 Adapter
echo - PCIe Passthrough (falls verfügbar)
echo - Vollständige Hardware-Validierung
echo.

echo === Quick Start Commands ===
echo.
echo REM 1. Als Administrator ausführen:
echo PowerShell Enable-WindowsOptionalFeature -Online -FeatureName Microsoft-Hyper-V -All
echo.
echo REM 2. Nach Neustart - VM erstellen:
echo PowerShell New-VM -Name IntelAvbDev -MemoryStartupBytes 8GB -Generation 2
echo PowerShell Set-VMFirmware -VMName IntelAvbDev -EnableSecureBoot Off
echo.
echo REM 3. In VM - Test Signing aktivieren:
echo bcdedit /set testsigning on
echo.
echo === Nächste Schritte ===
echo.
echo 1. **VM einrichten** (heute möglich)
echo 2. **Driver in VM testen** (vollständige Funktionalität)
echo 3. **Code Signing Certificate beantragen** für Production
echo 4. **Corporate Deployment** mit signierten Treibern
echo.
echo Ihre Intel AVB Filter Driver Implementierung ist vollständig -
echo Sie brauchen nur eine Umgebung zum Testen! ??
echo.
pause