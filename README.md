# Winamp Plex Library

Natives Media-Library-Plugin für Winamp Legacy unter Windows 10/11, x86. Zielversion ist **Winamp 5.9.2 Build 10042**. Die DLL heißt `ml_plex.dll`.

## Entwicklungsstand

Der technische Prototyp erzeugt den Plex-Navigationsknoten und eine native Ansicht. Die DLL wurde in einer getrennten Kopie der Zielversion geladen, die Ansicht gerendert und die Testinstanz regulär beendet. Ein SDK-Testhost prüft zusätzlich wiederholtes Laden und Entladen.

Die Anmeldung folgt dem Plexa-Ablauf: **Mit Plex anmelden**, Freigabe im Systembrowser, Server auswählen, HTTPS-Verbindung prüfen und Musikbibliothek auswählen. Das Plugin erhält Tokens intern. Ein Passwort oder eine manuelle Token-Eingabe ist für diesen Ablauf nicht vorgesehen. Der Zugriff wird benutzergebunden mit Windows DPAPI gespeichert.

Der Wiedergabe-Prototyp übergibt ausschließlich befristete Loopback-URLs an Winamp. WinHTTP übernimmt die Plex-Authentifizierung. Der Proxy wurde mit synthetischen Medienbytes und Fehlerantworten getestet. Ein erfolgreicher Transporttest belegt noch keine Decoder-, Audio- oder Seeking-Kompatibilität mit echten Plex-Medien.

Der Auftrag enthält eine verbindliche Integrationsschranke: Vor der allgemeinen Bibliotheksimplementierung muss ein echter Plex-Titel in Winamp geprüft werden. Der Stand und die offenen Nachweise stehen in [docs/testing.md](docs/testing.md). Künstler-, Album-, Titel- und Playlistansichten, Suche, Cover, Cache und Plex-Schreibfunktionen sind noch keine zugesicherten Produktfunktionen.

## Build

Erforderlich sind Visual Studio 2022 oder dessen Build Tools mit C++-x86-Werkzeugen, Windows SDK, CMake ab 3.24 und die unveränderten SDK-5.55-Header. Die einzige zusätzliche C++-Bibliothek ist nlohmann/json 3.12.0, mit SHA-256 festgelegt.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Ergebnis: `build/Release/ml_plex.dll`. SDK-Beschaffung, Debug-Build und Hinweise zu fehlenden Werkzeugen: [docs/build.md](docs/build.md).

## Installation und Konfiguration

Winamp beenden und die DLL manuell in das **tatsächlich von dieser Winamp-Installation verwendete Plugins-Verzeichnis** kopieren. `gen_ml.dll` muss vorhanden sein. Das Plugin gehört neben `gen_ml.dll`, nicht in dessen Datenverzeichnis `Plugins/ml`. Keine automatische Installation in die vorhandene Installation ausführen.

In der Medienbibliothek **Plex → Mit Plex anmelden** öffnen und die Anmeldung bei Plex bestätigen. Die Verbindung wird erst nach einer HTTPS-Anfrage mit normaler Zertifikatsprüfung und einem Vergleich der Serveridentität gespeichert. Bei einer fehlerhaften Verbindung erfolgt kein automatischer Wechsel auf HTTP oder eine andere Verbindung.

Die eigene Konfiguration liegt unter `%LOCALAPPDATA%/WinampPlexLibrary/connection.dpapi`. Sie enthält keine Klartext-Tokens. Beschädigte oder nicht entschlüsselbare Dateien werden als Fehler behandelt. Die Winamp- und Plex-Konfiguration werden nicht verändert.

Zur Deinstallation Winamp beenden und ausschließlich die selbst installierte `ml_plex.dll` entfernen. Die eigene DPAPI-Datei kann bei Bedarf separat entfernt werden. Keine Winamp-Datenbank oder Plex-Dateien löschen.

## Sicherheit und Einschränkungen

- Wiedergabe-URLs enthalten weder Plex-Token noch Serveradresse oder Plex-Medienpfad.
- Der Proxy bindet ausschließlich an `127.0.0.1` auf einem dynamisch vergebenen Port. Jeder Grant besitzt 192 Bit Zufall und eine feste Laufzeit von zwei Stunden.
- Ein Grant endet bei Austausch, Verbindungsende oder Plugin-Beendigung. Gespeicherte Winamp-Playlists können danach nicht wiederverwendbare lokale URLs enthalten.
- Der Prototyp besitzt einen Wiedergabe-Grant. Eine allgemeine Album- oder Mehrtitelliste erfordert eine eigene Grant-Verwaltung.
- Direct Play wird verwendet. Es gibt weder einen eigenen Decoder noch einen automatischen Transcoding-Pfad.
- Externes HTTP, ungültige TLS-Zertifikate, fremde Redirects und nicht unterstützte Medienpfade werden abgewiesen. IPv6-Origin-Adressen sind noch nicht unterstützt.
- Browseranmeldung und Serververzeichnis verwenden Plex-Dienste; Medienbytes kommen direkt vom ausgewählten Plex Media Server.

## Dokumentation

- [Recherche und Referenzen](docs/research.md)
- [Winamp-SDK und ABI](docs/winamp-sdk.md)
- [Plex-API](docs/plex-api.md)
- [Architektur und Sicherheit](docs/architecture.md)
- [Build und Installation](docs/build.md)
- [Prüfungen und offene Integrationsnachweise](docs/testing.md)

## Veröffentlichung

Das Projekt ist lokal versioniert. Es gibt weder ein öffentliches Repository noch einen Push. Der eigene Quellcode steht unter MIT, siehe [LICENSE](LICENSE). SDK-Dateien, Downloads, Binärdateien, private Adressen und lokale Zugänge werden nicht eingecheckt. Fremde Lizenzen sind in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) aufgeführt. Es besteht keine Verbindung zu den Markeninhabern von Winamp oder Plex.
