@echo off
echo === Intel AVB Filter Driver - NDIS Filter Installation ===
echo Korrekte Installation f�r NDIS Lightweight Filter Driver
echo.

echo Step 1: Checking Administrator privileges...
net session >nul 2>&1
if %errorLevel% == 0 (
    echo ? Running as Administrator
) else (
    echo ? FEHLER: Muss als Administrator ausgef�hrt werden
    echo    Rechtsklick auf Eingabeaufforderung -> "Als Administrator ausf�hren"
    pause
    exit /b 1
)

echo.
echo Step 2: Pr�fung der Treiberdateien...
if exist "x64\Debug\IntelAvbFilter\IntelAvbFilter.sys" (
    echo ? Treiber gefunden: IntelAvbFilter.sys
) else (
    echo ? Treiber nicht gefunden: x64\Debug\IntelAvbFilter\IntelAvbFilter.sys
    pause
    exit /b 1
)

if exist "x64\Debug\IntelAvbFilter\IntelAvbFilter.inf" (
    echo ? INF-Datei gefunden: IntelAvbFilter.inf
) else (
    echo ? INF-Datei nicht gefunden: x64\Debug\IntelAvbFilter\IntelAvbFilter.inf
    pause
    exit /b 1
)

if exist "x64\Debug\IntelAvbFilter\intelavbfilter.cat" (
    echo ? Katalogdatei gefunden: intelavbfilter.cat
) else (
    echo ? Katalogdatei nicht gefunden: x64\Debug\IntelAvbFilter\intelavbfilter.cat
    echo ? WARNUNG: Installation kann ohne Katalog fehlschlagen
    REM Don't exit - try to continue anyway
)

echo.
echo Step 3: Kopieren der Treiberdateien ins System...
echo Kopiere nach C:\Windows\System32\drivers\
copy "x64\Debug\IntelAvbFilter\IntelAvbFilter.sys" "C:\Windows\System32\drivers\" >nul
if %errorLevel% == 0 (
    echo ? IntelAvbFilter.sys erfolgreich kopiert
) else (
    echo ? Fehler beim Kopieren von IntelAvbFilter.sys
    pause
    exit /b 1
)

echo.
echo Step 4: Registrierung des NDIS Filter Driver...
echo Verwende pnputil f�r Filter Driver Registration...
pnputil /add-driver "x64\Debug\IntelAvbFilter\IntelAvbFilter.inf" /install
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
echo Step 5: Erstelle Windows Service f�r Filter Driver...
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
    echo    Filter wird automatisch geladen wenn ben�tigt
)

:TEST_INSTALLATION
echo.
echo Step 7: �berpr�fung der Installation...
echo.
echo Pr�fe in Device Manager:
echo 1. �ffne Device Manager (devmgmt.msc)
echo 2. Ansicht ? "Ausgeblendete Ger�te anzeigen"
echo 3. Suche nach "Intel AVB Filter Driver" unter Netzwerkadapter oder System
echo.

echo Pr�fe Windows Services:
sc query IntelAvbFilter
if %errorLevel% == 0 (
    echo ? Service ist registriert
) else (
    echo ??  Service nicht gefunden (normal bei manchen Installationen)
)

echo.
echo Step 7b: Neustart der Netzwerkadapter (aktiviert Device Interface)...
echo Deaktiviere und aktiviere Intel Adapter...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$adapters = Get-NetAdapter | Where-Object { $_.InterfaceDescription -match 'Intel.*226' }; foreach ($a in $adapters) { Write-Host \"  Restarting: $($a.Name)\"; Disable-NetAdapter -Name $a.Name -Confirm:$false -ErrorAction SilentlyContinue; Start-Sleep -Milliseconds 500; Enable-NetAdapter -Name $a.Name -Confirm:$false -ErrorAction SilentlyContinue }"
echo ? Adapter neu gestartet

echo.
echo Step 8: Test der Hardware-Zugriffe...
if exist "comprehensive_ioctl_test.exe" (
    echo F�hre umfassenden Hardware-Test aus ^(device-aware^)...
    comprehensive_ioctl_test.exe
    goto :TEST_DONE
)
if exist "avb_test_i219.exe" (
    echo ??  WARNUNG: Verwende Legacy I219 Test ^(nicht empfohlen^)
    avb_test_i219.exe
    goto :TEST_DONE
)
echo ??  Test-Anwendung nicht gefunden
echo    Baue comprehensive test mit: cl /I include /I external/intel_avb/lib /I intel-ethernet-regs/gen tools/avb_test/comprehensive_ioctl_test.c /Fe:comprehensive_ioctl_test.exe

:TEST_DONE

echo.
echo === Installation abgeschlossen ===
echo.
echo Wenn der Test erfolgreich ist, ist Ihr Intel AVB Filter Driver funktionsf�hig!
echo.
echo F�r Debug-Ausgabe:
echo 1. Lade DebugView.exe herunter (Microsoft Sysinternals)
echo 2. F�hre als Administrator aus
echo 3. Aktiviere "Capture Kernel"
echo 4. Schaue nach "(REAL HARDWARE)" Meldungen
echo.
pause