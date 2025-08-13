# CAT-Datei Verwaltung f�r Intel AVB Filter

## �bersicht

Windows-Treiber ben�tigen eine Catalog-Datei (.cat) f�r die digitale Signierung und Installation. Diese Datei wird mit dem `makecat.exe` Tool aus dem Windows Driver Kit (WDK) generiert.

## Problem behoben ?

**Das CAT-Datei Problem wurde erfolgreich gel�st:**

1. **CAT-Datei wird automatisch generiert** mit dem `Generate-CATFile.ps1` Skript
2. **Korrekte Pfade konfiguriert** - CAT-Datei wird im richtigen Ausgabeverzeichnis erstellt
3. **Build funktioniert** - keine fehlenden Dateifehler mehr

## Automatische Generierung

### PowerShell Skript verwenden:
```powershell
# F�r Debug x64 (Standard)
.\Generate-CATFile.ps1

# F�r andere Konfigurationen
.\Generate-CATFile.ps1 -Configuration Release -Platform ARM64
```

### Manuell mit makecat.exe:
```cmd
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\makecat.exe" IntelAvbFilter.cdf
```

## Verzeichnisstruktur

```
IntelAvbFilter/
??? IntelAvbFilter.inf              # Treiber-Installationsdatei
??? IntelAvbFilter.cdf              # Catalog Definition File
??? Generate-CATFile.ps1            # Automatisches Generierungsskript
??? IntelAvbFilter/x64/Debug/
    ??? IntelAvbFilter.cat          # Generierte Catalog-Datei (440 bytes)
```

## Fehlerbehebung

### CAT-Datei nicht gefunden
```
Fehler: "IntelAvbFilter.cat" konnte nicht kopiert werden, da die Datei nicht gefunden wurde.
```

**L�sung:**
1. Skript ausf�hren: `.\Generate-CATFile.ps1`
2. Pr�fen ob CAT-Datei existiert: `Get-ChildItem IntelAvbFilter\x64\Debug\IntelAvbFilter.cat`
3. Falls nicht, makecat manuell ausf�hren

### makecat.exe nicht gefunden
**L�sung:** Windows Driver Kit (WDK) installieren oder Pfad in Generate-CATFile.ps1 anpassen.

### CDF-Datei Syntax-Fehler
**L�sung:** Die `IntelAvbFilter.cdf` Datei wird automatisch mit korrekter Syntax generiert.

## CAT-Datei Details

- **Gr��e:** ~440 bytes (normale Gr��e f�r einfache CAT-Dateien)
- **Format:** Microsoft Authenticode Catalog Binary Format
- **Zweck:** Digitale Signatur-Validierung f�r Windows-Treiberinstallation
- **G�ltigkeitsdauer:** Unbegrenzt (bis zur n�chsten Treiber�nderung)

## Integration in Build-Prozess

Die CAT-Datei wird automatisch in den Visual Studio Build integriert:

1. **Pre-Build Event:** CAT-Datei wird generiert (optional)
2. **Build:** Treiber wird kompiliert
3. **Post-Build Event:** CAT-Datei wird ins korrekte Verzeichnis kopiert
4. **Package:** Treiber-Paket wird mit CAT-Datei erstellt

## Treiber-Installation

Mit korrekter CAT-Datei:
```cmd
# Installation
pnputil /add-driver IntelAvbFilter.inf /install

# Deinstallation  
pnputil /delete-driver oem123.inf /uninstall
```

## Entwicklungshinweise

- **CAT-Datei bei jeder INF-�nderung neu generieren**
- **Keine manuelle Bearbeitung der CAT-Datei** 
- **CAT-Datei in Versionskontrolle einschlie�en** f�r Reproduzierbarkeit
- **Test-Signierung verwenden** w�hrend der Entwicklung

## Status: ? GEL�ST

Das CAT-Datei Problem ist vollst�ndig behoben:
- ? Automatische Generierung funktioniert
- ? Korrekte Pfade konfiguriert
- ? Build erfolgreich ohne Fehler
- ? Installationsbereit

**Letztes Update:** 13. August 2025
**N�chste Schritte:** Treiber-Test und Hardware-Validierung