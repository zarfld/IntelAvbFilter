@echo off
echo === Intel AVB Filter Driver - NDIS Filter Installation ===
echo Korrekte Installation für NDIS Lightweight Filter Driver
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
echo Step 2: Prüfung der Treiberdateien...
if exist "x64\Debug\IntelAvbFilter.sys" (
    echo ? Treiber gefunden: IntelAvbFilter.sys
) else (
    echo ? Treiber nicht gefunden: x64\Debug\IntelAvbFilter.sys
    pause
    exit /b 1
)

if exist "x64\Debug\IntelAvbFilter.inf" (
    echo ? INF-Datei gefunden: IntelAvbFilter.inf
) else (
    echo ? INF-Datei nicht gefunden: x64\Debug\IntelAvbFilter.inf
    pause
    exit /b 1
)

if exist "x64\Debug\IntelAvbFilter.cat" (
    echo ? Katalogdatei gefunden: IntelAvbFilter.cat
) else (
    echo ? Katalogdatei nicht gefunden: x64\Debug\IntelAvbFilter.cat
    pause
    exit /b 1
)

echo.
echo Step 3: Kopieren der Treiberdateien ins System...
echo Kopiere nach C:\Windows\System32\drivers\
copy "x64\Debug\IntelAvbFilter.sys" "C:\Windows\System32\drivers\" >nul
if %errorLevel% == 0 (
    echo ? IntelAvbFilter.sys erfolgreich kopiert
) else (
    echo ? Fehler beim Kopieren von IntelAvbFilter.sys
    pause
    exit /b 1
)

echo.
echo Step 4: Registrierung des NDIS Filter Driver...
echo Verwende pnputil für Filter Driver Registration...
pnputil /add-driver "x64\Debug\IntelAvbFilter.inf" /install
if %errorLevel% == 0 (
    echo ? Filter Driver erfolgreich registriert
) else (
    echo ??  pnputil Registrierung fehlgeschlagen, versuche alternative Methode...
    goto :ALTERNATIVE_INSTALL
)

goto :TEST_INSTALLATION

:ALTERNATIVE_INSTALL
echo.
echo === Alternative Installation: Manueller Service ===
echo.
echo Step 5: Erstelle Windows Service für Filter Driver...
sc create IntelAvbFilter binPath= "C:\Windows\System32\drivers\IntelAvbFilter.sys" type= kernel start= auto error= normal
if %errorLevel% == 0 (
    echo ? Service erfolgreich erstellt
) else (
    echo ? Service-Erstellung fehlgeschlagen
    pause
    exit /b 1
)

echo.
echo Step 6: Starte den Filter Service...
sc start IntelAvbFilter
if %errorLevel% == 0 (
    echo ? Service erfolgreich gestartet
) else (
    echo ??  Service-Start fehlgeschlagen (normal bei NDIS Filter)
    echo    Filter wird automatisch geladen wenn benötigt
)

:TEST_INSTALLATION
echo.
echo Step 7: Überprüfung der Installation...
echo.
echo Prüfe in Device Manager:
echo 1. Öffne Device Manager (devmgmt.msc)
echo 2. Ansicht ? "Ausgeblendete Geräte anzeigen"
echo 3. Suche nach "Intel AVB Filter Driver" unter Netzwerkadapter oder System
echo.

echo Prüfe Windows Services:
sc query IntelAvbFilter
if %errorLevel% == 0 (
    echo ? Service ist registriert
) else (
    echo ??  Service nicht gefunden (normal bei manchen Installationen)
)

echo.
echo Step 8: Test der Hardware-Zugriffe...
if exist "avb_test_i219.exe" (
    echo Führe Hardware-Test aus...
    avb_test_i219.exe
) else (
    echo ??  Test-Anwendung nicht gefunden
    echo    Kompiliere sie mit: cl avb_test_i219.c /Fe:avb_test_i219.exe
)

echo.
echo === Installation abgeschlossen ===
echo.
echo Wenn der Test erfolgreich ist, ist Ihr I219 Filter Driver funktionsfähig!
echo.
echo Für Debug-Ausgabe:
echo 1. Lade DebugView.exe herunter (Microsoft Sysinternals)
echo 2. Führe als Administrator aus
echo 3. Aktiviere "Capture Kernel"
echo 4. Schaue nach "(REAL HARDWARE)" Meldungen
echo.
pause