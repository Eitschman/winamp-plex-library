# Winamp Plex Library

Plex-Musik direkt in der klassischen Winamp-Medienbibliothek durchsuchen und mit Winamp abspielen. Natives C++17-Plugin für **Windows 10/11 und Winamp 5.9.2 Build 10042, x86**. Die DLL heißt `ml_plex.dll`.

[![Windows x86](https://github.com/Eitschman/winamp-plex-library/actions/workflows/build.yml/badge.svg)](https://github.com/Eitschman/winamp-plex-library/actions/workflows/build.yml)
[![Lizenz: MIT](https://img.shields.io/badge/Lizenz-MIT-blue.svg)](LICENSE)

## Funktionen

- Künstler, Alben, Titel und Plex-Audio-Playlists in eigenen Medienbibliotheksknoten.
- Suche, Sortierung, Mehrfachauswahl und Navigation vom Künstler bis zum Titel.
- Titel und Alben abspielen, anhängen oder hinter dem aktuellen Playlisttitel einfügen. Die Player-Steuerung übernimmt Winamp.
- Plex-Anmeldung im Browser, Server- und Musikbibliotheksauswahl sowie automatische HTTPS-Verbindungsauswahl.
- Albumcover in den Listen und über Winamps Album-Art-Dienst; Titel übernehmen vorhandene Albumcover.
- Native Skin-Anbindung und deutsche beziehungsweise englische Texte entsprechend Winamps aktivem Sprachpaket.
- Asynchrone Netzwerkzugriffe, begrenzte Caches, Plex-Wiedergabestatus und persönliche Titelbewertungen.

## Voraussetzungen

Winamp 5.9.2 Build 10042 als 32-Bit-Version mit aktivierter Medienbibliothek (`gen_ml.dll`), Windows 10/11, ein erreichbarer Plex Media Server mit Musikbibliothek und ein berechtigtes Plex-Konto. Für die DLL wird die aktuelle [Microsoft Visual C++ v14 Runtime für x86](https://aka.ms/vc14/vc_redist.x86.exe) benötigt. Es gibt keinen zusätzlichen Hintergrunddienst und keinen eigenen Audiodecoder.

WACUP ist ein späteres Kompatibilitätsziel und bisher nicht zur Laufzeit geprüft. Andere Winamp-Versionen sind ebenfalls nicht pauschal freigegeben.

## Download

Unter [GitHub Actions](https://github.com/Eitschman/winamp-plex-library/actions/workflows/build.yml) erzeugen erfolgreiche Windows-Builds das Artefakt **`winamp-plex-library-win32-release`** mit DLL, Installationshinweisen und Lizenztexten. Zum Herunterladen von Actions-Artefakten ist eine GitHub-Anmeldung erforderlich. Diese zeitlich aufbewahrten Buildpakete sind keine automatisch veröffentlichten Releases. Alternativ lässt sich das Plugin aus dem Quellcode bauen.

## Entwicklungsstand

Der Streaming-Proxy wartet bei vollen Decoderpuffern ohne kurzen Sendetimeout weiter. Die automatisierten Transportprüfungen vergleichen die gesamte Datei auch bei einem vorübergehend nicht lesenden Empfänger und prüfen das abbrechbare Beenden. Ein echter MP3-Titel mit 3:29 Minuten und ein M4A-Titel mit 4:11 Minuten sind ohne Seeking bis zum natürlichen Ende geprüft. Weitere Nachweise und ihre Grenzen stehen in [docs/testing.md](docs/testing.md).

Das Plugin erzeugt den Plex-Navigationsknoten und native Bibliotheksansichten. Die DLL wird in einer getrennten Kopie der Zielversion geprüft. Der SDK-Testhost bestätigt 100 Lade-/Entladezyklen; eine native Prüfung mit 100 Ansichtswechseln besteht die festgelegten Ressourcengrenzen. Ansichtswechsel brechen laufende Anfragen ab und übergeben deren Abbau an einen kontrollierten Hintergrundthread. 13 automatisierte Testsuiten bestehen in Debug und Release. Einzelne Ergebnisse und offene Integrationsnachweise stehen in [docs/testing.md](docs/testing.md).

Die Anmeldung folgt dem Plexa-Ablauf: **Einstellungen → Mit Plex anmelden**, Freigabe im Systembrowser, Server und Musikbibliothek auswählen. Das Öffnen der Einstellungen startet keine Browseranmeldung. Das Plugin bezieht Tokens intern und speichert den Zugriff benutzergebunden mit Windows DPAPI. Ein Passwort oder eine manuelle Token-Eingabe ist nicht vorgesehen.

Der Wiedergabe-Prototyp übergibt befristete Loopback-URLs für MP3 und M4A an Winamp. FLAC, OGG und separate ADTS-AAC-Dateien werden wegen der Decoderanforderungen vorab im Hintergrund als private lokale Dateien vorbereitet. AAC erhält dabei ohne Neucodierung einen M4A-Container. WinHTTP übernimmt die Plex-Authentifizierung. MP3, M4A, FLAC und OGG sind mit echten Plex-Titeln auf Fortschritt, Seeking, Länge und Titel geprüft. AAC-LC ist mit selbst erzeugter Stille und einem authentifizierten Testserver geprüft. Exportierte Testplaylists enthalten keine Plex-Zugangsdaten. Weitere Fehlerfälle benötigen eigene Integrationsprüfungen.

Die Medienbibliothek enthält eigene Knoten für Künstler, Alben, Titel und Plex-Playlists. Native Tabellen laden Seiten im Hintergrund, unterstützen Suche, Sortierung, Mehrfachauswahl und Kontextmenüs. Künstler → Alben → Titel → Winamp-Playlist wurde mit realen Daten geprüft. Ein begrenzter Metadaten-Cache hält bereits geladene Seiten vor.

Die Kontextaktion „Nach aktuellem Titel einfügen“ fügt ausgewählte Titel hinter dem aktuellen Playlist-Eintrag ein und erhält Wiedergabe beziehungsweise Pause. Die tatsächliche nächste Wiedergabe richtet sich weiterhin nach Winamps Zufallswiedergabe und gegebenenfalls dessen Warteschlangen-Plugins. Die Menüaktion ist in Classic, Winamp Modern, Bento und Big Bento sowie auf Deutsch und Englisch geprüft. Wartende Streaming-Titel bleiben innerhalb derselben Winamp-Sitzung auch nach Ablauf ihrer exportierbaren Links abspielbar; fremde Prozesse erhalten dann HTTP 410. Andere abgelaufene lokale Links werden mit einer eigenen Meldung angezeigt.

Die Titelansicht zeigt persönliche Plex-Bewertungen auf einer Fünf-Sterne-Skala. Das Kontextmenü eines einzelnen Titels setzt ein bis fünf Sterne oder entfernt die Bewertung. Schreiben und Rücklesen laufen im Hintergrund; erst der bestätigte Serverwert aktualisiert die Tabelle. Vorhandene Bruchteile bleiben beim Lesen erhalten. Die Änderung und Wiederherstellung eines Testtitels sind am echten Server nachgewiesen.

Dialog, Tabellen, Menüs und Steuerelemente verwenden die Skin-Engine von `gen_ml`. Classic, Winamp Modern, Bento und Big Bento wurden in der getrennten Testkopie visuell geprüft. Das ist kein Nachweis für jeden fremden Skin oder jede DPI-Einstellung. Es gibt keine zusätzliche Player-Steuerleiste: Doppelklick und Kontextmenü übergeben Medien an Winamp; der Player steuert die Wiedergabe.

Albumcover und fehlende Titelanzahlen werden für sichtbare Zeilen im Hintergrund geladen. Titel übernehmen das vorhandene Albumcover, sofern sie kein eigenes Bild besitzen. Ein begrenzter Bildcache hält Miniaturen vor. Das reale Dimmu-Borgir-Album „Grand Serpent Rising“ wurde mit Cover und 13 Titeln geprüft. Fehlende Bilder werden ohne Ersatzbild dargestellt. Winamps Album-Art-Dienst erhält vorbereitete PNG-Bilder für eigene Wiedergabe-URLs. Beide Coverfelder des Big-Bento-Players wurden damit visuell geprüft.

Die verbindliche Integrationsschranke für den ersten echten Plex-Titel ist erfüllt. Der lokale Dateizugriff ermöglicht bei den geprüften FLAC-, OGG- und AAC-Dateien Laufzeit und Seeking mit den vorhandenen Winamp-Decodern. Automatische Plex-Statusmeldungen für Abspielen, Pause, Fortsetzen, Seeking und Stop sind am echten Winamp geprüft. Ein reguläres Titelende hat einen neuen Plex-Verlaufseintrag und einen erhöhten Wiedergabezähler erzeugt. Ein echter Plex-ADTS-Titel, weitere AAC-Profile und weitere Fehlerfälle bleiben offen, siehe [docs/testing.md](docs/testing.md). WACUP ist ein späteres Kompatibilitätsziel und bisher nicht als Laufzeit geprüft.

## Build

Erforderlich sind Visual Studio 2022 oder dessen Build Tools mit C++-x86-Werkzeugen, Windows SDK, CMake ab 3.24 sowie die unveränderten SDK-5.55-Header und der veröffentlichte Skin-Schnittstellenheader von 2016. Die einzige zusätzliche C++-Bibliothek ist nlohmann/json 3.12.0, mit SHA-256 festgelegt. Die Bildverarbeitung verwendet Windows Imaging Component.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Ergebnis: `build/Release/ml_plex.dll`. Zuvor die SDK-Header mit `scripts/Fetch-WinampSdk.ps1` beschaffen; Voraussetzungen, genaue Befehle, Debug-Build und Fehlerdiagnose stehen in [docs/build.md](docs/build.md). SDK-Dateien werden lokal mit Prüfsummen geprüft und nicht im Repository mitgeliefert.

## Installation und Konfiguration

Winamp beenden und die DLL manuell in das **tatsächlich von dieser Winamp-Installation verwendete Plugins-Verzeichnis** kopieren. `gen_ml.dll` muss vorhanden sein. Das Plugin gehört neben `gen_ml.dll`, nicht in dessen Datenverzeichnis `Plugins/ml`. Keine automatische Installation in die vorhandene Installation ausführen.

In der Medienbibliothek **Plex → Einstellungen → Mit Plex anmelden** öffnen und die Anmeldung bei Plex bestätigen. Danach Server und Musikbibliothek auswählen und speichern. Die automatische Erreichbarkeitsprüfung bevorzugt eine passende lokale HTTPS-Verbindung, danach direkten Fernzugriff und schließlich Plex Relay. Sie läuft beim ersten Bibliotheksauftrag jedes Plugin-Starts im Hintergrund und zusätzlich in den Einstellungen. Vor der authentifizierten Bibliotheksabfrage werden Zertifikat und Serveridentität geprüft. Fehlerhafte Authentifizierung startet keine Suche nach anderen Tokens oder Servern.

Unter **Erweitert** stehen eine feste Verbindung und eine eigene HTTPS-Basisadresse zur Verfügung. Eine Änderung muss vor dem Speichern geprüft werden. **Automatisch** hebt die feste Auswahl auf. Die Sprache folgt dem aktiven Winamp-Sprachpaket; eine eigene Sprachauswahl gibt es nicht. Deutsch und Englisch sind enthalten; weitere Winamp-Sprachen verwenden die englischen Plugin-Texte. Der Metadaten- und Cover-Cache lässt sich abschalten und leeren. Diagnoseinformationen enthalten feste Statuscodes und Zähler.

Die eigene Konfiguration liegt unter `%LOCALAPPDATA%/WinampPlexLibrary/connection.dpapi`. Sie enthält keine Klartext-Tokens. Beschädigte oder nicht entschlüsselbare Dateien werden als Fehler behandelt. Die Winamp- und Plex-Konfiguration werden nicht verändert.

Zur Deinstallation Winamp beenden und ausschließlich die selbst installierte `ml_plex.dll` entfernen. Die eigene DPAPI-Datei kann bei Bedarf separat entfernt werden. Keine Winamp-Datenbank oder Plex-Dateien löschen.

## Sicherheit und Einschränkungen

- Wiedergabe-URLs enthalten weder Plex-Token noch Serveradresse oder Plex-Medienpfad.
- Der Proxy bindet ausschließlich an `127.0.0.1` auf einem dynamisch vergebenen Port. Jeder Grant besitzt 192 Bit Zufall. Für andere Prozesse endet seine Gültigkeit nach zwei Stunden.
- Ein Grant endet bei Austausch, Verbindungsende oder Plugin-Beendigung. Gespeicherte Winamp-Playlists können danach nicht wiederverwendbare lokale URLs enthalten.
- Die laufende Winamp-Instanz besitzt zusätzlich prozessgebundenen Sitzungszugriff für lange Playlists. Windows muss sie als Eigentümer der konkreten TCP-Clientverbindung bestätigen. Dadurch wird die Frist exportierter Links nicht verlängert. Dieser Zugriff endet mit Verbindung oder Plugin; Widerruf und Sicherheitsprüfungen gelten auch für Winamp.
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

Der eigene Quellcode steht unter **MIT**, siehe [LICENSE](LICENSE). Fremde Bedingungen stehen in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). SDK-Dateien, Downloads, Build-Artefakte und lokale Zugänge gehören nicht in Git. Binärpakete enthalten die Lizenztexte des Projekts und von nlohmann/json. Es besteht keine Verbindung zu den Markeninhabern von Winamp oder Plex.

Fehler bitte als [GitHub Issue](https://github.com/Eitschman/winamp-plex-library/issues) mit Winamp-Version, Skin, Windows-Version, Audioformat und reproduzierbaren Schritten melden. Tokens, private Serveradressen und lokale Konfigurationsdateien nicht veröffentlichen. Build- und Testhinweise für Beiträge stehen in [CONTRIBUTING.md](CONTRIBUTING.md).
