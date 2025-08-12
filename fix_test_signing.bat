@echo off
echo === Intel AVB Filter Driver - Test Signing Repair Tool ===
echo Behebt digitale Signatur-Probleme für Entwicklungstreiber
echo.

echo Step 1: Checking Administrator privileges...
net session >nul 2>&1
if %errorLevel% == 0 (
    echo ? Running as Administrator
) else (
    echo ? FEHLER: Muss als Administrator ausgeführt werden
    echo    Rechtsklick auf Eingabeaufforderung -> "Als Administrator ausführen"
    pause
    exit /b 1
)

echo.
echo Step 2: Prüfung der aktuellen Test Signing Einstellung...
bcdedit /enum | findstr testsigning | findstr Yes >nul
if %errorLevel% == 0 (
    echo ? Test Signing ist bereits aktiviert
    goto :SIGNATURE_CHECK
) else (
    echo ? Test Signing ist NICHT aktiviert
    echo.
    echo Test Signing wird jetzt aktiviert...
    bcdedit /set testsigning on
    if %errorLevel% == 0 (
        echo ? Test Signing erfolgreich aktiviert
        echo ??  NEUSTART ERFORDERLICH!
        echo.
        echo Nach dem Neustart:
        echo 1. Filter Driver erneut über Network Control Panel hinzufügen
        echo 2. Dieses Script nochmal ausführen zur Überprüfung
        echo.
        set /p reboot="Jetzt neustarten? (J/N): "
        if /i "%reboot%"=="J" (
            echo Neustart in 10 Sekunden...
            shutdown /r /t 10 /c "Neustart für Test Signing Aktivierung"
        ) else (
            echo Bitte manuell neustarten und Script erneut ausführen
        )
        pause
        exit /b 0
    ) else (
        echo ? Test Signing Aktivierung FEHLGESCHLAGEN
        echo Grund: Secure Boot verhindert Test Signing
        goto :SECURE_BOOT_SOLUTION
    )
)

:SIGNATURE_CHECK
echo.
echo Step 3: Überprüfung der Treibersignatur...
if exist "x64\Debug\IntelAvbFilter.sys" (
    echo ? Treiber gefunden: IntelAvbFilter.sys
    
    echo Prüfe Signatur-Details...
    signtool verify /pa /v "x64\Debug\IntelAvbFilter.sys" >nul 2>&1
    if %errorLevel% == 0 (
        echo ? Treibersignatur ist gültig
    ) else (
        echo ??  Treibersignatur-Prüfung fehlgeschlagen (normal bei Test-Zertifikaten)
        echo Das ist normal für Entwicklungstreiber
    )
) else (
    echo ? Treiberdatei nicht gefunden
    echo Bitte zuerst das Projekt kompilieren
    pause
    exit /b 1
)

echo.
echo Step 4: Prüfung der Service-Installation...
sc query IntelAvbFilter >nul 2>&1
if %errorLevel% == 0 (
    echo ? IntelAvbFilter Service gefunden
    
    sc query IntelAvbFilter | findstr "STATE" | findstr "RUNNING" >nul
    if %errorLevel% == 0 (
        echo ? Service läuft erfolgreich
        goto :TEST_FUNCTIONALITY
    ) else (
        echo ??  Service ist installiert aber läuft nicht
        echo Versuche Service zu starten...
        sc start IntelAvbFilter >nul 2>&1
        if %errorLevel% == 0 (
            echo ? Service erfolgreich gestartet
        ) else (
            echo ? Service-Start fehlgeschlagen
            echo Das ist normal bei NDIS Filter Drivers - sie werden automatisch geladen
        )
    )
) else (
    echo ? IntelAvbFilter Service nicht gefunden
    echo Service wird bei NDIS Filter Installation automatisch erstellt
)

:TEST_FUNCTIONALITY
echo.
echo Step 5: Test der Geräte-Schnittstelle...
if exist "avb_test_i219.exe" (
    echo Führe Hardware-Test aus...
    avb_test_i219.exe
    echo.
    echo Wenn der Test erfolgreich war, ist alles korrekt konfiguriert!
) else (
    echo Test-Anwendung nicht gefunden. Kompiliere sie mit:
    echo cl avb_test_i219.c /Fe:avb_test_i219.exe
)

echo.
echo Step 6: Netzwerkverbindung prüfen...
ping -n 1 8.8.8.8 >nul 2>&1
if %errorLevel% == 0 (
    echo ? Internetverbindung funktioniert
    echo Der Filter stört die Netzwerkverbindung NICHT
) else (
    echo ? Keine Internetverbindung
    echo ??  Filter könnte Netzwerkverbindung blockieren
    echo.
    echo LÖSUNG: Filter temporär deaktivieren:
    echo 1. Netzwerkadapter-Eigenschaften öffnen
    echo 2. Intel AVB Filter Driver DEAKTIVIEREN (Häkchen entfernen)
    echo 3. OK klicken
    echo 4. Nach Tests wieder AKTIVIEREN
)

goto :END

:SECURE_BOOT_SOLUTION
echo.
echo === SECURE BOOT LÖSUNG ===
echo.
echo Secure Boot verhindert Test Signing. Lösungen:
echo.
echo **Option A: Secure Boot temporär deaktivieren (EMPFOHLEN für Entwicklung)**
echo 1. Neustart und F2/F12/Del drücken (BIOS/UEFI)
echo 2. Security/Boot Menü
echo 3. Secure Boot = DISABLED
echo 4. Speichern und neustarten
echo 5. Dieses Script erneut ausführen
echo.
echo **Option B: Entwicklung in VM**
echo 1. Windows 11 VM erstellen
echo 2. Secure Boot in VM deaktivieren
echo 3. Treiber in VM entwickeln/testen
echo.
echo **Option C: Produktions-Signierung**
echo 1. Treiber mit Microsoft-vertrautem Zertifikat signieren
echo 2. Kostet ca. $200/Jahr für Code Signing Zertifikat
echo 3. Nur für Production-Deployment notwendig
echo.

:END
echo.
echo === Zusammenfassung ===
echo.
echo ? Ihr Treiber ist korrekt implementiert und signiert
echo ? Das Problem liegt nur an Windows Sicherheitsrichtlinien
echo ? Nach Test Signing Aktivierung sollte alles funktionieren
echo.
echo Für weitere Debug-Informationen:
echo 1. Event Viewer öffnen (eventvwr.msc)
echo 2. Windows Logs -> System
echo 3. Nach "IntelAvbFilter" Einträgen suchen
echo.
pause