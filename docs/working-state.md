# Arbeitsstand und Wiedereinstieg

Prüfstand: 05.11.2026. Der ursprüngliche Plugin-Auftrag bleibt maßgeblich; das PRP unter `prp/prp.md` ist ein veränderbares Arbeitsdokument. Die detaillierten Nachweise stehen in `docs/testing.md`.

## Aktueller Produktstand

- Natives x86-Media-Library-Plugin für Winamp 5.9.2 Build 10042, vier Bibliotheksknoten, native virtuelle Tabellen, Suche, Sortierung, Paging, Mehrfachauswahl, Künstler-/Album-/Titelnavigation und Plex-Playlists.
- Plex-Browseranmeldung erst nach Klick auf „Mit Plex anmelden“. Einstellungen, automatische HTTPS-Verbindungswahl, Server und Musikbibliothek, manuelle Adresse unter „Erweitert“. Die Sprache folgt Winamps Sprachpaket. Zugriff ausschließlich benutzergebunden mit DPAPI; keine Klartext-Tokens.
- Classic, Winamp Modern, Bento und Big Bento sind visuell geprüft. Keine zusätzliche Player-Steuerleiste. Einfügen hinter dem aktuellen Titel ist mit exakter Reihenfolge, Metadaten und erhaltenem Wiedergabe-/Pausenzustand geprüft. Kontextmenüs sind in allen vier Skins sowie auf Deutsch und Englisch geprüft.
- Albumcover und geerbte Trackcover, beide Big-Bento-Playerfelder, reales Referenzalbum „Grand Serpent Rising“ mit 13 Titeln sowie ein echter Titel ohne Bild sind geprüft.
- MP3 und M4A verwenden authentifizierte Loopback-Streams; FLAC und OGG private vorbereitete Dateien. Synthetisches AAC-LC wird ohne Neucodierung als M4A vorbereitet. Echte MP3-/M4A-/FLAC-/OGG-Titel bestehen Fortschritt, Länge, Seeking und Metadaten ohne TranscodeSession. Der aktuelle Wiederholungslauf steht unter `formats-view-reaper-native.log`.
- Timeline für Abspielen, Pause, Fortsetzen, Seeking und Stop, regulärer Verlaufseintrag und Wiedergabezähler sowie persönliche Bewertung mit Rücksetzen sind am echten Server zurückgelesen.
- Gemeinsamer begrenzter Bibliotheks- und Covercache; Leeren, deaktivierter Cache, Revisionen, Ablauf und gezählte Wiederverwendung sind geprüft. Rasche Suchaufträge liefern zuletzt das aktuelle Ergebnis.

## Lebensdauer, lange Playlists und Ressourcen

Volle Decoderpuffer dürfen keinen Medienabbruch verursachen. Der Proxy wartet mit nicht blockierenden Sockets auf Empfangsbereitschaft und prüft dabei das Stop-Event. Bytegenaue Rückstauübertragung, abbrechbarer Stillstand und die separate Drei-Sekunden-Headerfrist besitzen Regressionstests samt erkannten Gegenproben. MP3 und M4A bestehen jeweils 30 Sekunden ohne Seeking und danach die Seeking-Prüfung (`stream-continuous-fixed.log`). Der Titelendetest `Test-PlexFormats.ps1 -ToEnd` besteht ohne Seeking für einen echten M4A-Titel mit 251 Sekunden und einen MP3-Titel mit 209 Sekunden bis zum natürlichen Stop (`stream-natural-end-fixed.log`). Jeweils 13 Testsuiten bestehen im aktuellen Debug- und Release-Build; die Transportsuite enthält 115 Einzelprüfungen. Eine Prüfung aller Bibliothekstitel ist damit nicht behauptet.

Streaming-Capabilities sind für fremde Prozesse zwei Stunden gültig. Wartende Titel bleiben innerhalb derselben Winamp-Sitzung über kernelbestätigte TCP-Client-PID nutzbar; die Frist fremder Links wird nicht verlängert. Der native Test mit zehn Sekunden Laufzeit bestätigt Fortschritt und Seeking nach Ablauf bei gleichzeitigem HTTP 410 für einen fremden Prozess (`host-session-native-short.log`). Die normale zweistündige Laufzeit ist wiederhergestellt. Keinen zweistündigen Echtzeitlauf starten.

Der HTTP-Transport wartet auf die tatsächliche Rückkehr aller WinHTTP-Sitzungs-Callbacks. Ein deterministisch angehaltener Callback samt negativer Gegenprobe bestätigt die Freigabereihenfolge. Kein Callback darf nach Freigabe des Plugin-Codes weiterlaufen.

`ViewReaper` übernimmt bereits abgebrochene Ansichtsworker im Hintergrund. Fenster-, Skin- und Bildlistenressourcen werden vorher auf dem UI-Thread getrennt. Ein eigener COM-initialisierter Thread verbindet die Worker und leert die Warteschlange vor DLL-Ende. Höchstens acht ausstehende Ansichten einschließlich laufendem Abbau sind zugelassen. Der SDK-Testhost besteht 100 Ladezyklen. Fünf gezielte Prüfungen bestätigen nicht blockierende Übergabe, Kapazitätsgrenze, Abschluss, MTA-Apartment und FIFO-Abbau; Gegenproben für jeden dieser Fälle werden erkannt und sind entfernt.

Die aktuelle native Ressourcenprüfung besteht nach 100 Wechseln und gleichen 60-Sekunden-Ruhephasen in 319,4 Sekunden: Handles 584 → 586, GDI 1479 → 1479, USER 232 → 232, Threads 19 → 19, private Bytes 87932928 → 86704128, TCP/UDP null. Unveränderte Grenzen; kein allgemeiner Speicherlecknachweis. Nachweise: `view-resources-reaper-100.log`, `view-resources-reaper-100-summary.json`. Die anderen protokollierten Ressourcenläufe erklären keine vollständigen Allokationsursachen. Eine reine Cookie-Lösung mit `CoIncrementMTAUsage` ist verworfen und aus dem Produktcode entfernt.

Die native Prüfung einer hängenden TLS-Anfrage bestätigt drei Millisekunden UI-Abfrage, zwei Millisekunden Ansichtswechsel und 849 Millisekunden reguläres Beenden nach dem Wechsel. Die ursprüngliche DPAPI-Datei bleibt hashgleich (`connection-navigation-reaper-native.log`). Eigenständige Prüfungen bestätigen außerdem Beenden während echter FLAC-Vorbereitung und ausstehender Browserfreigabe sowie das Ende lokaler Streaming-URLs nach Plugin-Beendigung.

## Build und aktuelle Prüfung

MSVC-x86-Debug und Release bestehen jeweils 13 CTest-Suiten (`ctest-view-reaper-debug.log`, `ctest-view-reaper-release.log`). Das separate Buildverzeichnis `.local/repro-build` besteht ebenfalls 13 Suiten (`repro-view-reaper-build.log`, `repro-view-reaper-ctest.log`). Compilerwerkzeuge und Befehle stehen in `docs/build.md`.

Der Coverlauf `artwork-view-reaper-native.log` besteht; die tatsächliche Albumaufnahme wurde mit Cover und 13 Titeln angesehen. Die PE-Prüfung bestätigt x86 und den ungeschmückten SDK-Export (`plugin-view-reaper-pe.log`). Die regulär neu gestartete Testkopie verwendet die hashgleiche Release-DLL. Wiedergabe gestoppt, gespeicherte Playlist leer, Testkopie minimiert; beide geänderten Prüfscripts bestehen die Syntaxprüfung (`view-reaper-handoff-verification.log`). Sämtliche Gegenproben und Cookie-Diagnosen sind entfernt. Die verbleibenden Aufgaben sind die unten benannten externen Integrationsnachweise; nicht mit ungetesteten Behauptungen schließen.

## Verbleibende externe Integrationsnachweise

- Echtes widerrufenes Token und erneute Anmeldung, echtes Konto ohne Server sowie leere Musikbibliothek.
- Positive lokale HTTPS-Auswahl im Heimnetz; derzeit wird vom Büro über Fernzugriff geprüft.
- Echter Ausfall des Heimservers während Wiedergabe. Synthetische Netzwerk-, Authentifizierungs-, Redirect-, TLS- und Decoderfehler sind getrennt geprüft.
- Echte separate Plex-ADTS-Datei und weitere AAC-Profile. Die Inventur fand keinen passenden ADTS-Titel; synthetisches AAC-LC ist nachgewiesen.
- WACUP ist ein späteres Ziel ohne Laufzeitnachweis. Weitere fremde Skins und DPI-Stufen besitzen keine pauschale Freigabe.

Diese offenen Nachweise erfordern passende externe Zustände beziehungsweise eine zusätzliche Laufzeitumgebung. Keine Plex-Konfiguration, Routereinstellungen, echten Zugangsdaten oder normale Winamp-Installation eigenständig dafür verändern.

## Lokale Umgebung und Freigaben

- Workspace: `C:\Users\andreasht\Documents\Winamp`.
- Build Tools: `C:\BuildTools2022`; CMake/CTest unter `Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`.
- Unveränderte SDK-Header unter `.local/sdk` und `.local/skin-sdk`; Beschaffung und Hashprüfung dokumentiert.
- Getrennte Testkopie: `.local/winamp-test`, Klasse `WinampPlexTest`, eigene Einstellungen unter `settings`. Nur ihren tatsächlichen Prozesspfad verwenden.
- Die Testkopie startet und bleibt minimiert. Nur notwendige Sicht-/Maustests blenden kurz ein und minimieren danach wieder. `scripts/Restart-PlexTest.ps1` prüft den regulären Neustart. Wiedergabetests möglichst stumm; danach stoppen und gespeicherte Playlist leeren.
- Browserfreigabe ist bestätigt. Zugriff ausschließlich über den dokumentierten Anwendungsweg auf `%LOCALAPPDATA%\WinampPlexLibrary\connection.dpapi`. Keine Zugangsdaten suchen, ausgeben oder in Logs übernehmen.
- Build-Tools-Installation und getrennte Testkopie sind freigegeben. Ein existierender Titel darf für eine zurückgesetzte Bewertung und einen verbleibenden Testeintrag im Verlauf verwendet werden; diese Prüfung ist erfolgt.
- Keine automatische Installation im normalen Winamp und keine fremden Konfigurationsänderungen. Das öffentliche Repository `https://github.com/Eitschman/winamp-plex-library`, dessen Befüllung und GitHub Actions sind ausdrücklich freigegeben. AndreasHT besitzt laut GitHub Schreibzugriff; administrative Einstellungen bleiben beim Eigentümer. Lokale Git-Checkpoints mit expliziten Dateipfaden.
- Die feste Arbeitsgrenze um 13 Uhr galt für den 04.11.2026. Sie ist keine tägliche Stopbedingung.
