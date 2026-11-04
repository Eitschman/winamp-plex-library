# Build, SDK und Installation

## Voraussetzungen

- Windows 10 oder 11, x64-Host mit Unterstützung für x86-Anwendungen.
- Visual Studio 2022 oder Build Tools 2022, C++-Werkzeuge für x86/x64.
- Windows 10/11 SDK und CMake ab 3.24. Die VS-Komponente für CMake genügt.
- 7-Zip zum Extrahieren des SDK-Archivs, ohne dessen Installer auszuführen.
- Netzwerkzugriff auf den historischen Nullsoft-Download, das veröffentlichte WACUP-SDK-Verzeichnis und GitHub für die prüfsummengesicherten Abhängigkeiten.

Im aktuellen Lauf wurden mit ausdrücklicher Freigabe Build Tools 2022 installiert. Der verwendete Compiler meldet MSVC 19.44.35229. Der CMake-Lauf wählte Windows SDK 10.0.19041.0. Diese Versionsangaben sind Testbefunde und keine zusätzlichen Mindestanforderungen an spätere Entwicklerinstallationen.

## SDK beschaffen

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Fetch-WinampSdk.ps1 `
  -SevenZip "C:\Program Files\7-Zip\7z.exe"
```

Der angegebene 7-Zip-Pfad ist vom Entwickler zu prüfen. Das Skript lädt `WA5.55_SDK.exe` von der historischen HTTPS-Quelle, prüft die SHA-256 und extrahiert `gen_ml/ml.h`, `Winamp/wa_ipc.h`, `gen_tray/WINAMPCMD.H` sowie die Wasabi-/Agave-Header für Album-Art nach `.local/sdk`. Zusätzlich lädt es den unveränderten Skin-Schnittstellenheader von 2016 nach `.local/skin-sdk/gen_ml/ml_ipc_0313.h`. CMake prüft die verwendeten Header einzeln, für Album-Art gemäß `cmake/WinampArtworkSdk.cmake`. Vorhandene Zielordner werden nicht überschrieben. Quellen, Hashes und ABI-Vertrag stehen in [winamp-sdk.md](winamp-sdk.md).

Ein manuell entpacktes SDK kann mit `-DWINAMP_SDK_ROOT=C:/Pfad/zum/SDK` angegeben werden. Der getrennte Skin-Header wird mit `-DWINAMP_SKIN_SDK_ROOT=C:/Pfad/zum/SkinSDK` angegeben. Die Prüfsummen müssen zu den untersuchten Fassungen passen. Andere Fassungen erfordern eine gesonderte ABI-Prüfung und bewusste Änderung des Buildvertrags.

## Konfiguration und Build

In einer Entwickler-PowerShell oder mit verfügbarem `cmake`:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Der Konfigurationsschritt lädt nlohmann/json 3.12.0 über CMake FetchContent. Die Archiv-Prüfsumme ist im Projekt festgelegt. Der Build benötigt keine Plex-Zugangsdaten und keinen Plex-Server. Alle automatisierten Fixtures verwenden erfundene Tokens und Loopback.

Die DLL liegt im Release-Build unter `build/Release/ml_plex.dll`, im Debug-Build unter `build/Debug/ml_plex.dll`. Keine nachträgliche Umbenennung ist erforderlich. Testprogramme und `.lib`-Dateien werden nicht in Winamp installiert. CMake enthält keinen Installationsschritt in eine bestehende Winamp-Installation.

Unter Linux lässt sich der plattformunabhängige Sicherheitscode mit CMake und CTest prüfen. Die Windows-DLL wird dort nicht mit MSVC gebaut. Ein Linux-Test ersetzt weder den x86-Build noch einen Winamp-Lauf.

## Isolierte Laufzeitprüfung

Das Skript `scripts/Prepare-WinampTest.ps1` erstellt ausschließlich nach ausdrücklicher Freigabe eine getrennte Testkopie. Die Quellinstallation muss explizit angegeben werden; Version und Dateien werden geprüft. Ein bereits vorhandenes Ziel wird abgewiesen.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Prepare-WinampTest.ps1 `
  -WinampDirectory "C:\tatsächlich-geprüfter-Pfad\Winamp"
```

Die Kopie liegt unter `.local/winamp-test`. Ihre `paths.ini` verwendet ein eigenes `settings`-Verzeichnis und die Fensterklasse `WinampPlexTest`. Programmdateien, Systemkomponenten, Audio-Plugins sowie `gen_ff.dll`, `freeform` und vorhandene Skins werden für die Classic-/Modern-Prüfung übernommen. Die vorhandene Benutzerkonfiguration wird nicht kopiert. Keine Dateizuordnungen einrichten und keinen Test als Administrationsbedarf interpretieren.

Der Testhost setzt ausschließlich für seinen Prozess `WINAMP_PLEX_CONFIG_DIRECTORY` auf einen absoluten temporären Pfad. Dadurch liest er keine echte Plugin-Verbindung und sendet keine echten Plex-Anfragen. Relative Umleitungen werden abgewiesen. Im regulären Betrieb bleibt die DPAPI-Datei unter `%LOCALAPPDATA%/WinampPlexLibrary`.

## Manuelle Installation

1. Die tatsächliche `winamp.exe` lokalisieren und deren Dateiversion prüfen.
2. Das tatsächlich verwendete Plugin-Verzeichnis prüfen, zum Beispiel anhand der geladenen `gen_ml.dll`. Nicht allein aus einem vermuteten Standardpfad ableiten.
3. Winamp regulär beenden.
4. Nur `ml_plex.dll` neben `gen_ml.dll` kopieren. Eine vorhandene gleichnamige Datei nicht ungeprüft überschreiben.
5. Winamp starten, unter Einstellungen → Plugins → Media Library den Eintrag prüfen und den Plex-Knoten öffnen.

Im aktuellen Rechner wurde die vorhandene Installation unter `C:\Program Files (x86)\Winamp` mit entsprechendem `Plugins`-Verzeichnis tatsächlich gefunden. Sie wurde nicht verändert. Diese Beobachtung ist keine universelle Installationsvorgabe.

## Deinstallation und Fehlerdiagnose

Winamp regulär beenden und die konkret installierte `ml_plex.dll` entfernen. Die eigene Datei `%LOCALAPPDATA%/WinampPlexLibrary/connection.dpapi` ist vom Benutzer getrennt löschbar. Keine pauschalen Löschungen in `Plugins`, `AppData` oder Plex-Verzeichnissen ausführen.

- DLL wird nicht angezeigt: x86 prüfen, `gen_ml.dll` prüfen, Exportnamen prüfen, erforderliche VC-Runtime prüfen.
- Plugin wird nicht geladen: SDK-Prüfsummen und eigene DPAPI-Konfiguration prüfen. Keine fremde Konfigurationsdatei als Zugang übernehmen.
- HTTPS schlägt fehl: angebotene Plex-Verbindung und Zertifikatsnamen prüfen. Zertifikatsprüfung nicht abschalten.
- Browseranmeldung läuft ab: Dialog schließen und neue Anmeldung starten.
- Wiedergabe-URL funktioniert nach Neustart nicht: Grants sind pro Prozess befristet. Eine gespeicherte Loopback-URL ist kein dauerhafter Plex-Link.
- Streaming antwortet, aber kein Audio: Format und tatsächliches Winamp-Input-Plugin prüfen; keine Decoderkompatibilität aus HTTP 200 ableiten.
