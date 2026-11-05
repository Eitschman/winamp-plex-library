# Arbeitsstand und Wiedereinstieg

## Nächster Auftrag bei „weiter“

Der Benutzer hat einen begrenzten Medienpuffer nach dem Vorbild des Plexamp-Caches beauftragt und die Umsetzung ausdrücklich auf morgen verschoben. Dafür wurde bisher ausschließlich vorhandener Code gelesen; es gibt noch keine Pufferimplementierung und keine entsprechende Änderung der installierten DLL. Morgen bedeutet „weiter“: diesen Auftrag aufnehmen, ohne erneut nach dem Arbeitsziel zu fragen.

Ziel ist, den Plex-Abruf von Winamps Lesegeschwindigkeit zu entkoppeln. Zuerst den bestehenden Proxy und den privaten Medienspeicher prüfen, dann einen begrenzten Puffer mit kontrolliertem Ressourcenverbrauch umsetzen. Speicherort und genaue Grenzen sind noch keine festgelegte Architekturentscheidung. Bestehende Authentifizierung, TLS-Prüfung, bytegenaue Fortsetzung und native Decoder bleiben maßgeblich. Langsames Lesen, Pause, Verbindungsunterbrechung, Seeking, Ressourcenbegrenzung und Abbruch beim Plugin-Ende müssen gezielt geprüft werden. Keine automatische weitere Installation in der normalen Winamp-Installation aus der einmaligen Austauschfreigabe ableiten.

Die Recherche zu Plexamp belegt Hintergrundpufferung und Wiederholungsversuche bei Verbindungsabbrüchen sowie einen Festplattencache und Vorausladen. Quellen: `https://forums.plex.tv/t/plexamp-constantly-pausing/675610`, `https://forums.plex.tv/t/plexamp-macos-always-re-caches-tracks-even-when-downloaded/910747`, `https://forums.plex.tv/t/plexamp-pre-caching-speed-limit-causes-playback-to-buffer/897623`. Exakte aktuelle Wiederholungsgrenzen und ETag-Logik von Plexamp sind öffentlich nicht nachgewiesen. Die Recherche ist keine Freigabe für unkontrollierte Ausweichpfade.

Benutzernachweis nach dem ausdrücklich freigegebenen DLL-Austausch: Zwei Titel sind in seiner normalen Winamp-Installation ohne vorzeitigen Abbruch durchgelaufen. Titel und Laufzeiten dieser beiden Durchläufe wurden nicht genannt. Das sind zusätzliche Beobachtungen, keine allgemeine Stabilitätsgarantie. Die eigene Testkopie bleibt beendet. Den tatsächlichen Ausgang des unten verlinkten ersten Release-Laufs beim Wiedereinstieg prüfen; dessen Veröffentlichung wurde vor der Arbeitsgrenze noch nicht bestätigt.

Prüfstand: 05.11.2026. Der ursprüngliche Plugin-Auftrag bleibt maßgeblich; das PRP unter `prp/prp.md` ist ein veränderbares Arbeitsdokument. Die detaillierten Nachweise stehen in `docs/testing.md`.

Feierabend am 05.11.2026: nach ausdrücklicher Verlängerung spätestens gegen 18:10 Uhr. Die eigene Testkopie ist gestoppt, ihre gespeicherte Playlist geleert und der Prozess regulär beendet (`release-day-shutdown.log`). Anschließend wurde auf ausdrücklichen neuen Auftrag ausschließlich die normale `Plugins/ml_plex.dll` bei beendetem Winamp ersetzt. Der installierte SHA-256 entspricht dem getesteten Build `080dd4d80f798e9600be5592c1c07420ecc2529f13d96f79465dc11a77d119ed`. Die vorherige DLL ist unter `.local/installation-rollback-20261105/ml_plex.dll` gesichert; ihre SHA-256 lautet `f37b12279e11fa4eb220ca1c70a47f195a6f9d563a2d7ab26490ec1834230f81`. Windows hat den Austausch nach Administratorfreigabe zugelassen; die Zieldatei wurde anschließend unabhängig erneut geprüft. Winamp wurde nicht gestartet. Diese Freigabe betrifft nur diesen Austausch.

Tag `v0.1.0` ist auf Quellstand `3644c12` veröffentlicht. Der Release-Lauf [42071402195](https://github.com/Eitschman/winamp-plex-library/actions/runs/42071402195) führt Debug, Release und Geheimnisprüfung aus und veröffentlicht anschließend ZIP und SHA-256-Datei. Beim nächsten Einstieg zuerst dessen tatsächliches Ergebnis und das Release unter `https://github.com/Eitschman/winamp-plex-library/releases/tag/v0.1.0` prüfen. Der vorangegangene Lauf 42070363161 besteht bereits. Der Tag selbst ist kein Nachweis einer erfolgreichen Veröffentlichung. Das heruntergeladene Releasepaket und dessen Prüfsumme gesondert nachprüfen, falls dies bis zur Arbeitsgrenze offen bleibt.

## Öffentliches Repository und CI

Aktuelle Priorität ist der wiederholt gemeldete vorzeitige Titelabbruch. „The Puppeteer“ ist nach 129,8 statt 273 Sekunden mit fehlenden Medienbytes reproduziert. Die bytegenaue Fortsetzung derselben Datei mit starkem ETag besteht synthetische Gegenproben und einen echten Titelendetest einschließlich tatsächlich unterbrochener Upstream-Verbindung. „Until Dawn“ besteht den zweiten hörbaren Titelendetest. Künstler und Titel erscheinen zusammen in Winamps Playlist und Anzeige. Debug und Release bestehen jeweils 13 CTest-Suiten mit 137 Transportprüfungen. Ein sporadischer Handle-Anstieg im Lifecycle-Test bleibt als eigener Befund offen.

Die Geheimnisprüfung umfasst Git-Historie, veröffentlichten Quellstand, aktuelle Änderungen, CI-Logs und das vorhandene Buildpaket. Keine Funde; künstliche positive und gültige negative Gegenprobe bestehen. Einzelheiten und Grenzen stehen in `SECURITY.md`. Der Release-Workflow veröffentlicht explizite Versionstags nach bestandenen Prüfungen. Für das erste Release ist `v0.1.0` vorgesehen; den tatsächlichen Workflow- und Veröffentlichungsstatus auf GitHub prüfen. Keine normale Winamp-Installation automatisch ersetzen.

Das Repository liegt unter `https://github.com/Eitschman/winamp-plex-library`; `origin/main` ist mit der lokalen Historie verbunden. Der ursprüngliche GitHub-Lizenzcommit bleibt erhalten. Die Veröffentlichung und GitHub Actions sind freigegeben. Der Workflow baut MSVC-x86-Debug und Release auf Windows 2022 und besteht für Quellstand `d85e25a` jeweils 13 CTest-Suiten. Release besteht zusätzlich sieben Paketprüfungen. Nachweis: [Actions-Lauf 42061725581](https://github.com/Eitschman/winamp-plex-library/actions/runs/42061725581).

Das heruntergeladene Artefakt `winamp-plex-library-win32-release` enthält exakt die x86-DLL, `INSTALL.md`, `LICENSE`, `THIRD-PARTY-NOTICES.md` und die MIT-Lizenz von nlohmann/json. SDK-Dateien und lokale Einstellungen sind nicht enthalten. CI ersetzt keinen nativen Winamp-/Plex-Nachweis. Der Release-Workflow veröffentlicht nach einem expliziten Versionstag und bestandenen Prüfungen; eine automatische Installation in der normalen Winamp-Installation gibt es nicht.

Die heruntergeladene CI-DLL ist zusätzlich in der getrennten Zielversion geprüft: 30 Sekunden MP3 ohne Seeking, Länge, Seeking, Plex-Sitzung ohne TranscodeSession und Titelmetadaten bestehen im Wiederholungslauf. Der erste Lauf findet nach bestandener Wiedergabeprüfung die eigene Plex-Sitzung nicht; dessen Ursache ist offen, siehe `docs/testing.md`. Die Testkopie erhält danach wieder den lokalen Build und bleibt gestoppt, leer und minimiert.

## Aktueller Produktstand

Zusätzlicher Nachweis: „Until Dawn“ besteht den hörbaren Titelendetest mit 369 Sekunden Decoderlänge und natürlichem Stop nach 368,3 Sekunden. Release-Build und Testkopie enthalten dieselbe DLL mit SHA-256 `080dd4d80f798e9600be5592c1c07420ecc2529f13d96f79465dc11a77d119ed`. Der neue GitHub-Lauf 42070363161 besteht Geheimnisprüfung sowie Debug und Release. Dessen Buildlogs wurden ebenfalls ohne Gitleaks-Fund geprüft. Die Einstellungsprüfung aus minimiertem Zustand besteht nach explizitem Wiederherstellen ihres Eigentümerfensters.

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
