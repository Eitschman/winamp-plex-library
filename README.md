# Winamp Plex Library

Natives Media-Library-Plugin für Winamp Legacy unter Windows 10/11, x86. Zielversion ist **Winamp 5.9.2 Build 10042**. Die DLL heißt `ml_plex.dll`.

## Entwicklungsstand

Der technische Prototyp erzeugt den Plex-Navigationsknoten und eine native Ansicht. Die DLL wurde in einer getrennten Kopie der Zielversion geladen, die Ansicht gerendert und die Testinstanz regulär beendet. Ein SDK-Testhost prüft zusätzlich wiederholtes Laden und Entladen.

Die Anmeldung folgt dem Plexa-Ablauf: **Einstellungen → Mit Plex anmelden**, Freigabe im Systembrowser, Server und Musikbibliothek auswählen. Das Öffnen der Einstellungen startet keine Browseranmeldung. Das Plugin bezieht Tokens intern und speichert den Zugriff benutzergebunden mit Windows DPAPI. Ein Passwort oder eine manuelle Token-Eingabe ist nicht vorgesehen.

Der Wiedergabe-Prototyp übergibt befristete Loopback-URLs für MP3 und M4A an Winamp. FLAC, OGG und separate ADTS-AAC-Dateien werden wegen der Decoderanforderungen vorab im Hintergrund als private lokale Dateien vorbereitet. AAC erhält dabei ohne Neucodierung einen M4A-Container. WinHTTP übernimmt die Plex-Authentifizierung. MP3, M4A, FLAC und OGG sind mit echten Plex-Titeln auf Fortschritt, Seeking, Länge und Titel geprüft. AAC-LC ist mit selbst erzeugter Stille und einem authentifizierten Testserver geprüft. Exportierte Testplaylists enthalten keine Plex-Zugangsdaten. Weitere Fehlerfälle benötigen eigene Integrationsprüfungen.

Die Medienbibliothek enthält eigene Knoten für Künstler, Alben, Titel und Plex-Playlists. Native Tabellen laden Seiten im Hintergrund, unterstützen Suche, Sortierung, Mehrfachauswahl und Kontextmenüs. Künstler → Alben → Titel → Winamp-Playlist wurde mit realen Daten geprüft. Ein begrenzter Metadaten-Cache hält bereits geladene Seiten vor.

Dialog, Tabellen, Menüs und Steuerelemente verwenden die Skin-Engine von `gen_ml`. Classic, Winamp Modern, Bento und Big Bento wurden in der getrennten Testkopie visuell geprüft. Das ist kein Nachweis für jeden fremden Skin oder jede DPI-Einstellung. Es gibt keine zusätzliche Player-Steuerleiste: Doppelklick und Kontextmenü übergeben Medien an Winamp; der Player steuert die Wiedergabe.

Albumcover und fehlende Titelanzahlen werden für sichtbare Zeilen im Hintergrund geladen. Titel übernehmen das vorhandene Albumcover, sofern sie kein eigenes Bild besitzen. Ein begrenzter Bildcache hält Miniaturen vor. Das reale Dimmu-Borgir-Album „Grand Serpent Rising“ wurde mit Cover und 13 Titeln geprüft. Fehlende Bilder werden ohne Ersatzbild dargestellt. Winamps Album-Art-Dienst erhält vorbereitete PNG-Bilder für eigene Wiedergabe-URLs. Beide Coverfelder des Big-Bento-Players wurden damit visuell geprüft.

Die verbindliche Integrationsschranke für den ersten echten Plex-Titel ist erfüllt. Der lokale Dateizugriff ermöglicht bei den geprüften FLAC-, OGG- und AAC-Dateien Laufzeit und Seeking mit den vorhandenen Winamp-Decodern. Ein echter Plex-ADTS-Titel, weitere AAC-Profile, weitere Fehlerfälle und Plex-Schreibfunktionen bleiben offen, siehe [docs/testing.md](docs/testing.md). WACUP ist ein späteres Kompatibilitätsziel und bisher nicht als Laufzeit geprüft.

## Build

Erforderlich sind Visual Studio 2022 oder dessen Build Tools mit C++-x86-Werkzeugen, Windows SDK, CMake ab 3.24 sowie die unveränderten SDK-5.55-Header und der veröffentlichte Skin-Schnittstellenheader von 2016. Die einzige zusätzliche C++-Bibliothek ist nlohmann/json 3.12.0, mit SHA-256 festgelegt. Die Bildverarbeitung verwendet Windows Imaging Component.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Ergebnis: `build/Release/ml_plex.dll`. SDK-Beschaffung, Debug-Build und Hinweise zu fehlenden Werkzeugen: [docs/build.md](docs/build.md).

## Installation und Konfiguration

Winamp beenden und die DLL manuell in das **tatsächlich von dieser Winamp-Installation verwendete Plugins-Verzeichnis** kopieren. `gen_ml.dll` muss vorhanden sein. Das Plugin gehört neben `gen_ml.dll`, nicht in dessen Datenverzeichnis `Plugins/ml`. Keine automatische Installation in die vorhandene Installation ausführen.

In der Medienbibliothek **Plex → Einstellungen → Mit Plex anmelden** öffnen und die Anmeldung bei Plex bestätigen. Danach Server und Musikbibliothek auswählen und speichern. Die automatische Erreichbarkeitsprüfung bevorzugt eine passende lokale HTTPS-Verbindung, danach direkten Fernzugriff und schließlich Plex Relay. Vor der authentifizierten Bibliotheksabfrage werden Zertifikat und Serveridentität geprüft. Fehlerhafte Authentifizierung startet keine Suche nach anderen Tokens oder Servern.

Unter **Erweitert** stehen eine feste Verbindung und eine eigene HTTPS-Basisadresse zur Verfügung. Eine Änderung muss vor dem Speichern geprüft werden. **Automatisch** hebt die feste Auswahl auf. Die Sprache folgt dem aktiven Winamp-Sprachpaket; eine eigene Sprachauswahl gibt es nicht. Deutsch und Englisch sind enthalten; weitere Winamp-Sprachen verwenden die englischen Plugin-Texte. Der Metadaten- und Cover-Cache lässt sich abschalten und leeren. Diagnoseinformationen enthalten feste Statuscodes und Zähler.

Die eigene Konfiguration liegt unter `%LOCALAPPDATA%/WinampPlexLibrary/connection.dpapi`. Sie enthält keine Klartext-Tokens. Beschädigte oder nicht entschlüsselbare Dateien werden als Fehler behandelt. Die Winamp- und Plex-Konfiguration werden nicht verändert.

Zur Deinstallation Winamp beenden und ausschließlich die selbst installierte `ml_plex.dll` entfernen. Die eigene DPAPI-Datei kann bei Bedarf separat entfernt werden. Keine Winamp-Datenbank oder Plex-Dateien löschen.

## Sicherheit und Einschränkungen

- Wiedergabe-URLs enthalten weder Plex-Token noch Serveradresse oder Plex-Medienpfad.
- Der Proxy bindet ausschließlich an `127.0.0.1` auf einem dynamisch vergebenen Port. Jeder Grant besitzt 192 Bit Zufall und eine feste Laufzeit von zwei Stunden.
- Ein Grant endet bei Austausch, Verbindungsende oder Plugin-Beendigung. Gespeicherte Winamp-Playlists können danach nicht wiederverwendbare lokale URLs enthalten.
- Ein gemeinsamer Listener verwaltet bis zu 10000 unabhängige Wiedergabe-Grants; die Workerzahl steigt nicht mit der Playlistlänge.
- Vorbereitete FLAC-/OGG-Dateien und aus ADTS erzeugte M4A-Container liegen in einem privaten Sitzungsordner unter `%LOCALAPPDATA%/WinampPlexLibrary/playback`. Nur der aktuelle Benutzer und SYSTEM erhalten Zugriff. Zufällige Namen enthalten weder Serverpfade noch Tokens. Grenzen: 512 MiB je Datei und 2 GiB insgesamt. Der Download muss vor der Playlistübergabe beendet sein; die Dateien bleiben bis zum Verbindungswechsel oder Plugin-Ende erhalten. Verwaiste, eindeutig markierte Sitzungen werden beim nächsten Vorbereiten bereinigt. Diese lokalen Dateipfade besitzen keine HTTP-Grant-Laufzeit.
- AAC-Vorbereitung benötigt Windows Media Foundation. Fehlende Medienkomponenten werden als Fehler gemeldet; andere Formate benötigen diese DLLs nicht. AAC mit Program Config Element oder mehreren Rohdatenblöcken pro ADTS-Frame wird abgewiesen; HE-AAC ist nicht geprüft.
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
