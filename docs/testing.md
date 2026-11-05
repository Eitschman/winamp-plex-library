# Prüfstand und Integrationscheckliste

Prüfdatum: 05.11.2026. Dieser Stand trennt ausgeführte Prüfungen von offenen Produktanforderungen.

## Ausgeführte Prüfungen

- MSVC-Release-Build mit Visual Studio 2022 Build Tools, x86. Die DLL entsteht unter `build/Release/ml_plex.dll`.
- SDK-Testhost: 100 Folgen aus Laden, fehlgeschlagener Knotenregistrierung, erfolgreicher Initialisierung, Ansichtserzeugung, Einstellungen, Diagnose, Beenden und Entladen. Fremde Knoten-IDs werden nicht akzeptiert; USER-Handles bleiben gleich und GDI-Handles wachsen nach dem ersten Dialogzyklus nicht weiter.
- Sicherheitsprüfungen mit gültigen und ungültigen Origins, Headern, Medienpfaden, Query-Encoding, Capabilities und Byte-Ranges.
- Transport gegen einen synthetischen Loopback-PMS: bytegenaue Vollantwort, HEAD, begrenzte und offene Ranges, Suffix-Range, 416, 401/403/404, 302/500, kein Weiterreichen privater Header, ungültige Capabilities, Origin-/Host-Prüfung und Abbruch einer tatsächlich ausstehenden Anfrage.
- DPAPI-Roundtrip mit Unicode, Prüfung auf fehlende Klartext-Zugangsdaten, atomarer Austausch sowie Gegenproben mit beschädigter Konfiguration und unsicherer Origin.
- Authentifizierungs-Parsing mit synthetischen Serverressourcen, getrenntem Server-Token, HTTPS-Verbindungen, leerer Musikbibliotheksliste und fehlerhaften Antworten.
- Tatsächliche Winamp-Testkopie der Version 5.9.2 Build 10042: DLL erkannt, Plex-Knoten und eigene Ansicht sichtbar. Die Darstellung wurde als Screenshot angesehen. Die Instanz wurde regulär beendet und mit ausgetauschter Test-DLL neu gestartet.
- Native Browseranmeldung: starke PIN erstellt, Systembrowser geöffnet, Benutzer hat den Zugriff für die Anwendung bestätigt, Serverressourcen wurden geladen. Die ausgewählte HTTPS-Verbindung und Serveridentität wurden geprüft; eine Musikbibliothek wurde angezeigt und zur Speicherung ausgewählt.
- Katalog-Livetest: Künstler, Alben, Titel und Audio-Playlists, zweite Seiten, Suchabfragen und Unterelemente gegen den echten Server geprüft. Album-Kinder werden ohne zusätzliche Sortierung abgefragt; die reale Disc-/Titelreihenfolge wurde geprüft. Ein leerer erster Playlistinhalt wird korrekt als leer behandelt.
- `scripts/Test-PlexLibrary.ps1`: Künstler → drei Alben → fünf Titel → einen Titel an die bestehende Winamp-Playlist angehängt; Zurück-Navigation und alle vier Baumansichten geprüft. Die reale Testbibliothek meldete 305 Künstler, 1333 Alben und 20770 Titel. Private Inhalte bleiben im lokalen Testordner.
- `scripts/Test-PlexLibraryUi.ps1`: beschriftete einzelne Musikbibliothek ohne Dropdown, Root ohne redundante Überschrift oder Zurück-Button, zwei Künstlerseiten mit 250 und 56 Einträgen, Künstler-Alben-Navigation, Suche und Rückkehr geprüft. Die native Zeilenkapazität bleibt nach einer Albumansicht gleich; die Gegenprobe erkannte zuvor 42 statt anschließend 16 sichtbare Künstlerzeilen. `scripts/Test-PlexViewLayout.ps1` prüft Ansichten mit 1024 × 600, 600 × 400 und 420 × 330 Pixeln. Screenshots wurden angesehen; echte zweite Musikbibliothek noch nicht geprüft.
- Classic, Winamp Modern, Bento und Big Bento tatsächlich geladen und Screenshots angesehen. Farben, Tabellenkopf, Scrollbalken und Steuerelemente wechseln mit dem Skin. Die Darstellung wurde außerdem bei 1000 × 450 Pixeln im Bento-Container geprüft. Keine Zusage für sämtliche fremden Skins, DPI-Stufen oder Farbschemata.

Die Screenshots und aktuellen Buildlogs liegen nur im ignorierten lokalen Testordner. Sie enthalten gegebenenfalls private Anzeigeinformationen und werden nicht veröffentlicht. Synthetische Testdaten enthalten keine echten Plex-Zugänge.

## Automatisierte Ausführung

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

CTest enthält `plugin_lifecycle`, `aac_remux`, `playback_reporter`, `transport`, `config_auth`, `catalog`, `artwork`, `artwork_provider`, `feedback` und `security`. Zehn von zehn Tests sind durchgelaufen. Der Katalogtest prüft unter anderem Unicode, fehlende Titel, geerbte Albumcover, große Playlistlaufzeiten, persönliche Bruchteilbewertungen, fehlerhafte Pagination, Pfadinjektion, Reihenfolge und Cache-Ablauf/LRU/Leerung. Transporttests prüfen unabhängige Grants, gezielten Widerruf, POST-/PUT-Methoden und Cover-HTTP-Fehler. Bildtests prüfen WIC-Decodierung, Größenlimits, beschädigte Bilder, sichere Pfade, fehlende Bilder und den PNG-Roundtrip. Der Provider-Test prüft Dispatch, Registrierung, fehlende Hostdienste, Allokationsfehler, Schreibschutz und Ressourcenfreigabe. Konfigurationsprüfungen umfassen DPAPI, Größenlimits, beschädigte Profile, Cache-Optionen und die priorisierte Verbindungsauswahl mit synthetischer Erreichbarkeit. Feedback-Tests prüfen Zeitbasis, Zustände, Bewertungsskala, Pfadinjektion, ungültige Zahlen und Ablehnung externen HTTP. Der Reporter prüft Wechsel, Pause, Zehnsekundenintervall, Seeking, Stop bei fremden Dateien, FIFO-Reihenfolge, Warteschlangenlimit, Tokenwiderruf, Erholung nach Timeout und Abbruch blockierter Anfragen. Der SDK-Host prüft beide Hostsprachen, Einstellungen, Diagnose, Skin-Registrierung, Dienstfreigabe und 100 Ladezyklen. Einzelne Prüfbedingungen sind keine Aussage über die Abdeckung aller Produktfunktionen.

## Phase-1-Schranke

Der echte Wiedergabenachweis läuft separat über `scripts/Test-PlexPlayback.ps1` gegen die isolierte Instanz und deren reguläre Playlist. Das Skript benötigt keine Zugangsdaten und entschlüsselt keine Konfiguration. Es prüft ausschließlich lokale Capability-URLs, Wiedergabefortschritt, Länge und Seeking. Der Test startet stumm, stoppt im `finally`-Pfad und stellt erst danach die vorherige Lautstärke wieder her.

Die Phase-1-Wiedergabeprüfung ist für einen echten Plex-MP3-Titel bestanden. Der aktuelle Lauf in `.local/logs/playback-metadata-hook.log` meldet 551 ms fortschreitende Wiedergabe, HTTP 200 und 262144 übertragene Bytes. Seeking liefert Ergebnis 0 und danach 10569 ms. Die Länge beträgt 306 Sekunden. Der Plex-Titel wurde in der exportierten Winamp-Playlist verglichen; diese enthält weder Plex-Token noch Plex-Serveradresse. Die allgemeinen Bibliotheksansichten können damit auf dieser geprüften Transportbasis entwickelt werden.

Die Zielversion benötigt die dokumentierte `WM_COPYDATA`-Playlistübergabe anstelle der dort abstürzenden alten Metadatenstruktur. Eine temporäre M3U8-Datei überträgt Länge und Unicode-Titel; sie wird nach der synchronen Verarbeitung entfernt. `ML_IPC_HOOKTITLEW` erhält den Plex-Titel auch dann, wenn die Dateitags abweichen. `IPC_SETPLAYLISTPOS` wird gemäß SDK mit `WINAMP_BUTTON2` kombiniert; `IPC_STARTPLAY` setzt die Position in der Zielversion auf 0 zurück. Der Proxy akzeptiert Winamps nachgewiesenen Host-Header ohne Port ausschließlich für die konkrete Loopback-Adresse. Fremde Hosts, falsche explizite Ports, doppelte Hosts, Origins und falsche Capabilities werden weiterhin abgewiesen.

## Manuelle Checkliste für die Zielversion

### Plugin und Lifecycle

- [x] x86-DLL bauen.
- [x] Plex-Knoten in der echten Zielversion öffnen.
- [x] Eigene Ansicht erzeugen und visuell prüfen.
- [x] Testinstanz regulär beenden und neu starten.
- [ ] Eintrag in Einstellungen → Plugins → Media Library visuell prüfen.
- [ ] Mehrfach zwischen fremden Ansichten und Plex wechseln.
- [ ] Beenden bei aktiver Anmeldung, Medienanfrage und unterbrochener Verbindung prüfen.
- [ ] Über einen längeren Lauf Prozess-, GDI-, USER- und Socket-Ressourcen beobachten. Die bisherigen Tests sind kein allgemeiner Speicherlecknachweis.

### Verbindung und Konfiguration

- [x] Plex-Browseranmeldung mit Benutzerfreigabe.
- [x] Server- und Musikbibliotheksabfrage über eine gültige HTTPS-Verbindung.
- [x] Neustart mit gespeicherter DPAPI-Verbindung.
- [x] Öffnen der Einstellungen ohne Browseranmeldung, manuelle Adresse nur unter „Erweitert“, keine zusätzliche Sprachauswahl.
- [x] Explizite erneute Browseranmeldung mit Benutzerfreigabe, automatische HTTPS-Auswahl, Speicherung und Wiederöffnung eines realen Servers mit 13 Verbindungen.
- [x] Cache-Option speichern, wieder öffnen und ursprünglichen Wert wiederherstellen; ausgeschalteten Cache unabhängig testen.
- [x] HTTP-Adresse abweisen; DNS-Fehler verständlich anzeigen; während einer Anfrage abbrechen; Abbrechen verändert die DPAPI-Datei nicht.
- [x] Sprache aus dem aktiven deutschen Winamp-Sprachpaket und englischem Winamp übernehmen. Der SDK-Testhost prüft beide Sprachen ohne eigenen Sprachschalter.
- [ ] Widerrufenes echtes Token und erneute Anmeldung.
- [x] Nicht erreichbarer Server, DNS-Fehler und Zertifikatsfehler im nativen Dialog. Der TLS-Test verwendet einen kurzlebigen selbstsignierten Loopback-Server ohne Änderung des Windows-Zertifikatsspeichers; eine gepinnte Gegenprobe bestätigt zuerst dessen Erreichbarkeit. WinHTTP weist das Zertifikat ab und sendet keine Anwendungsdaten.
- [ ] Leere Musikbibliothek und Konto ohne Server.
- [x] Deutsche und englische Einstellungen sowie Classic, Modern und Big Bento visuell prüfen. Tab-Reihenfolge im normalen Dialog und Escape zum Abbrechen sind in der Zielversion geprüft; verborgene erweiterte Eingaben bleiben außerhalb der Tab-Reihenfolge. Diagnose ohne abgeschnittene letzte Zeile geprüft.

### Audio

Die native Probe `.local/logs/startup-route-native.log` lädt die Bibliothek nach einem Plugin-Neustart ohne Einstellungsdialog. Sie prüft Seitenwechsel von 250 auf 56 Künstler, Navigation, Suche und Rückkehr ohne falsche Zeilenhöhe. Die Root-Ansicht wurde in Big Bento angesehen; Wiedergabe wurde nicht gestartet. Die Startauswahl verwendet den zentralen Identitätshelfer. Dessen Gegenproben prüfen lokale Priorität, nicht erreichbare lokale Route, fremde lokale Identität ohne Tokenweitergabe, feste Route, Authentifizierungsfehler ohne weitere Routensuche und Abbruch. Eine tatsächlich lokale Verbindung zuhause ist aus der Büro-Testumgebung nicht nachgewiesen.

| Format | Echtes Plex Direct Play | Länge / Titel | Seeking | Abbruch / Neustart |
| --- | --- | --- | --- | --- |
| MP3 | getestet | getestet | getestet | weitere Fehlerfälle offen |
| FLAC | getestet, private lokale Datei | 564 Sekunden, Plex-Titel geprüft | getestet, 10.404 ms nach Sprung auf 10 Sekunden | Sitzungsdatei nach regulärem Beenden entfernt |
| AAC | synthetische AAC-LC-Stille, lokal als M4A-Container | 45 Sekunden, 1939 Frames bei 44,1 kHz | getestet, 10.580 ms nach Sprung auf 10 Sekunden | echte Plex-ADTS-Datei und weitere AAC-Profile offen |
| M4A | getestet, MP4-Container | 251 Sekunden, Titel über reguläre Bibliotheksübergabe | getestet, 10.550 ms nach Sprung auf 10 Sekunden | weitere Fehlerfälle offen |
| OGG | getestet, private lokale Datei | 263 Sekunden, Plex-Titel geprüft | getestet, 10.541 ms nach Sprung auf 10 Sekunden | Sitzungsdatei nach regulärem Beenden entfernt |

`catalog_probe.exe --formats <lokaler-Ausgabepfad>` erfasst die gewählte Musikbibliothek seitenweise und erzeugt optional einen privaten Titel-/Formatbericht ohne URLs oder Zugangsdaten. Der reale Lauf prüfte 20.782 stabile Titel-IDs: 20.332 MP3-, 77 MP4-, 186 OGG-, ein FLAC- und 100 weitere Containerangaben; 86 Titel besitzen keine vom derzeitigen Direct-Play-Selektor unterstützte Variante. Bei 1.434 Titeln fehlt eine Cover-Referenz. Containerangaben sind kein Decoder-Nachweis. Der lokale Bericht und private Titel werden nicht eingecheckt.

`scripts/Test-PlexFormats.ps1` verwendet diesen Bericht ausschließlich zur Auswahl über die regulären nativen Bibliotheksansichten. Mehrdeutige Titel werden über Künstler und Album eingegrenzt und die tatsächliche Dateiendung der sicheren Playlistquelle geprüft. Der reale Gesamtlauf in `.local/logs/formats-local-media.log` besteht für MP3, M4A, FLAC und OGG einschließlich Fortschritt, Laufzeit, Seeking und Plex-Titelvergleich. FLAC und OGG verwenden dafür vorbereitete private Dateien. Der FLAC-Decoder akzeptiert keine HTTP-Wiedergabe; der OGG-Decoder meldet dort keine Länge und lehnt Seeking trotz korrekter HTTP-206-Bereiche ab. Ein eigener Decoder oder automatisches Transcoding wurde nicht ergänzt.

- [x] Winamp-Testplaylist gespeichert und jede Medienzeile gegen das vollständige eigene Loopback-URL- oder private Sitzungsdateiformat ohne Plex-Tokens geprüft. Alle vier Formatproben mit Winamps aktuellem Fenstertitel verglichen.
- [x] Nach den FLAC-/OGG-Proben zwei vorbereitete Dateien festgestellt, Testkopie regulär beendet und Entfernung beider Dateien sowie des Sitzungsordners geprüft (`.local/logs/local-media-shutdown.log`).
- [x] Grant-Ablauf synthetisch mit verkürzter Laufzeit prüfen: zunächst Medienbytes, danach HTTP 410 ohne Upstream-Zugriff. Die Plugin-Standardeinstellung bleibt zwei Stunden; kein zweistündiger Winamp-Dauertest.
- [x] Frischen Proxy-Link in der echten Testkopie mit HTTP 206 und 16 Medienbytes bestätigen, regulär beenden, denselben Link mit eindeutigem Verbindungsfehler prüfen (`scripts/Test-PlexProxyShutdown.ps1`).
- [ ] Fehlende Mediendatei und Verbindungsunterbrechung während des Abspielens.
- [ ] Prüfen, dass Plex tatsächlich keine Transcoding-Session erzeugt.
- [ ] Keine anderen Titel oder Konfigurationen der vorhandenen Winamp-Instanz verändern.

Die Formatprüfung besitzt positive Gegenproben mit M4A und MP3 und eine synthetische negative Gegenprobe mit einer unzulässigen Dateiendung. Der Pfadprüfer akzeptiert eigene FLAC-Sitzungsdateien und weist fremde Pfade, Pfadwechsel und angehängte Query-Parameter ab. Die Transporttests prüfen vollständige lokale Bytes, Dateifreigabe, Schreibschutz, private DACL, Größen- und Platzlimits, HTTP-Fehler, abgebrochene Downloads, Bereinigung verwaister markierter Sitzungen und Erhalt aktiver beziehungsweise fremder Sitzungen. Für AAC als eigene Dateiendung fehlt eine reale Probe.

Die gezielte AAC-Suche hat alle 20.782 Titel und sämtliche 20.782 gelieferten Medienparts der ausgewählten Bibliothek geprüft. Keine Part-Datei besitzt die Endung `.aac`; 77 Parts melden AAC als Audiocodec. Der Bericht `.local/logs/catalog-aac-search.log` enthält ausschließlich Zähler. Zusätzliche Varianten werden unabhängig vom automatisch ausgewählten Direct-Play-Medium erfasst. Der Bestand liefert damit keinen separaten AAC-Dateinachweis.

`scripts/Test-PlexAac.ps1` verwendet selbst erzeugte AAC-LC-Stille mit 44,1 kHz und zwei Kanälen. Ein authentifizierter synthetischer Loopback-Server liefert die ADTS-Datei an denselben Vorbereitungsweg wie Plex-Medien. Die Probe `.local/logs/aac-current-native.log` bestätigt in Winamp 45 Sekunden Länge und Seeking auf 10.580 ms. Wiedergabe und Playlist werden im `finally`-Pfad gestoppt beziehungsweise geleert. Ein echter Plex-ADTS-Titel wird dadurch nicht nachgewiesen. Die Referenzlänge ergibt sich aus 1939 Frames mit je 1024 Samples, nicht aus einer Bitratenschätzung. Direkte HTTP-Wiedergabe verweigert Seeking; die rohe lokale ADTS-Datei meldet 50 statt 45 Sekunden, auch mit TLEN-Tag. Die verschärfte Laufzeitprüfung erkennt diesen Fehler.

Die AAC-Vorbereitung ändert nur den Container. Der normalisierte AAC-Bitstream der 45-Sekunden-Probe besitzt vor und nach dem Containerwechsel denselben SHA-256-Wert. `aac_remux` vergleicht zusätzlich alle AAC-Nutzdaten über die MP4-Sampletabellen und prüft bestehende Ausgabedateien, Größenlimits, Abbruch, ungültige Daten, abgeschnittene Frames und unerwartete Restbytes. Die Suite ist Teil des aktuellen Gesamtlaufs mit neun Tests. Das ersetzt keine Prüfung von HE-AAC oder Windows-N-Systemen ohne Medienkomponenten.

### Bibliotheken und Erweiterungen

Künstler → Alben → Titel, einzelne Playlistübergabe und die vier Baumansichten sind real geprüft. Das native, skinned Kontextmenü hat das Testalbum mit 13 Titeln an Winamp übergeben und abgespielt. Seeking lieferte Rückgabewert 0 und anschließend Position 10429 ms. Die gespeicherte Playlist enthält ausschließlich lokale Capability-URLs. Alte Testeinträge wurden auf Benutzerwunsch entfernt; weitere Testläufe dürfen keine doppelten Alben hinterlassen. Wiedergabetests werden stumm ausgeführt und danach gestoppt; die vorherige Lautstärke wird erst nach Stop wiederhergestellt.

„Grand Serpent Rising“ von Dimmu Borgir wurde in der Albumansicht mit Cover, Jahr 2026 und 13 Titeln visuell geprüft. Der reale Katalog-Livetest bestätigt Album-ID und vorhandenen Coverpfad für alle 13 Titel. Ein Titel ohne eigene oder geerbte Cover-Referenz wurde anhand echter API-Daten ausgewählt: Die native Bildabfrage liefert den leeren Bildplatz, eine positive Gegenprobe einen tatsächlich geladenen Coverindex. Die entsprechende Titelzeile wurde in Big Bento ohne Ersatzbild angesehen. Ein Album ohne Bild ist synthetisch einschließlich HTTP 404 geprüft, noch nicht als eigene reale Albumprobe. Die horizontale Tabellenposition wird beim Spaltenwechsel zurückgesetzt und wurde visuell geprüft.

Die Player-Coveranzeige ist in Big Bento visuell geprüft: Beide Player-Coverfelder zeigen das Testalbum. Der Dienst besitzt 13 Bildzuordnungen; der aktuelle stumme Lauf meldet 14 erfolgreiche Bildabfragen. Die Wiedergabe ist anschließend gestoppt. Die Gegenproben zeigen: Der Host fragt die Kategorie `DATABASE` für diese HTTP-Titel nicht ab; die Kategorie `EMBEDDED` wird abgefragt. Das angebotene V5-BMP wurde trotz positiver WIC-Decodierung nicht angezeigt. Mit PNG und der SDK-Dateitypkennung `png` erscheinen die Bilder. Eine positive Decodierung in WIC allein reicht damit nicht als Hostnachweis. Artist-/Album-/Titelmetadaten werden über `ML_IPC_HOOKEXTINFOW` übernommen und sind sichtbar. Auch die Titelansicht mit geerbten Miniaturen ist visuell geprüft.

`scripts/Test-PlexLibraryActions.ps1` prüft alle 250 sichtbaren Laufzeiten auf aufsteigende und absteigende Reihenfolge sowie den Übergang zur nächsten Seite. Der Rückweg stellt die Titelreihenfolge wieder her. Drei ausgewählte MP3-Titel und danach zwei weitere werden ohne Wiedergabestart angehängt; Länge, Reihenfolge, Unicode-Titel und fünf unabhängige Loopback-URLs werden aus der gespeicherten Playlist verglichen. Die gespeicherte Playlist wird danach geleert. Negative Zeilen-, Bild- und Spaltenindizes werden abgewiesen. Der erfolgreiche Lauf einschließlich positiver Cover-Gegenprobe steht in `.local/logs/library-actions-countercheck-native.log`. Weitere Sortierspalten, größere Mehrfachübergaben und Sprache brauchen zusätzliche Bedienungstests. Serverseitige Pagination und Suche sind im Katalog-Livetest geprüft; der Cache besitzt synthetische Tests. Automatische Player-Statusmeldungen und Verlauf sind für den unten beschriebenen nativen Ablauf geprüft. „Als Nächstes abspielen“ wird nur bei belegter Winamp-Schnittstelle ergänzt.

### Persönliche Bewertungen und Timeline-API

Der Benutzer hat einen vorhandenen Testtitel, das anschließende Zurücksetzen seiner Bewertung und einen verbleibenden Testeintrag im Verlauf freigegeben. `feedback_probe --rate <lokale-Sicherungsdatei>` sichert den Ausgangswert vor dem Schreiben. Beide Werte werden über Metadaten zurückgelesen. Die erfolgreiche Gegenprobe steht in `.local/logs/feedback-rating-live.log`. Eine erhaltene Sicherungsdatei kann mit `feedback_probe --restore <Datei>` wieder angewendet werden; sie enthält keine Zugangsdaten und gehört nicht ins Repository.

`scripts/Test-PlexRatings.ps1` prüft vor dem Schreiben die sichtbare Tabellenzeile anhand von Titel, Künstler und Album. Die native Zeilenauswahl, der Bewertungsauftrag, die aktualisierte Spalte und der tatsächliche Plex-Rücklesewert sind geprüft. Die ursprüngliche Bewertung wird im `finally`-Pfad zurückgesetzt und zurückgelesen. Der Test startet keine Wiedergabe. Das Untermenü und die Titelspalte sind in Big Bento angesehen; das Untermenü zusätzlich in Classic und Winamp Modern. Negative Zeilenindizes werden vom Tabellenprüfer abgewiesen. Das ist kein Nachweis für fremde Skins oder Winamps eigene Bewertungsbuttons.

Die API-Probe `.local/logs/feedback-timeline-live.log` sendet ausdrücklich synthetische Zustände für einen vorhandenen Titel. Plex bestätigt `playing` und `paused` einschließlich Position und Client-Identifier über `/status/sessions`; nach `stopped` verschwindet diese Sitzung.

`scripts/Test-PlexTimeline.ps1` steuert dagegen den tatsächlichen Player: reguläre Bibliotheksauswahl, fortschreitende MP3-Wiedergabe, Pause, Fortsetzen, Sprung auf zehn Sekunden und Stop. `.local/logs/timeline-native-counters.log` bestätigt alle Zustände durch Rücklesen vom Server. `-CheckHistory` springt anschließend vier Sekunden vor das Titelende und wartet auf dessen reguläres Ende. `.local/logs/timeline-history-native.log` bestätigt sowohl einen neuen `historyKey` als auch den erhöhten `viewCount`. Ein zusätzlicher Scrobble-Request wird nicht benötigt und nicht gesendet. Lautstärke und Wiederholung werden nach geprüftem Stop wiederhergestellt; die gespeicherte Playlist wird geleert. Diese Schreibprüfung setzt die ausdrückliche Freigabe eines Testtitels voraus.

Eine frische Plugin-Sitzung prüft zugleich MP3 ohne vorherige lokale Dateivorbereitung. Die Gegenprobe `.local/logs/timeline-native-ordered.log` erkennt den ungültigen Umgang mit einem noch leeren Vorbereitungspfad; der korrigierte Pfad startet im nativen Wiederholungstest. Die Tabellenhilfe aktualisiert synchron vor ihrer Bereitschaftsprüfung, damit eine alte Statusanzeige nicht als neuer Ladeabschluss gilt. Der Gesamtlauf mit zehn CTest-Suiten steht in `.local/logs/ctest-timeline-native.log`. Der native Bewertungstest besteht mit derselben DLL und setzt die Bewertung zurück, siehe `.local/logs/ratings-reporter-native.log`; die Titelansicht in Big Bento wurde angesehen.

## Regulärer Wiedergabetest

`scripts/Test-PlexPlayback.ps1` arbeitet mit der bereits über die Bibliothek gefüllten Testplaylist. Es liest keine DPAPI-Datei und übernimmt keine manuell eingetragenen Tokens. Der reale positive Lauf prüft Fortschritt, bekannte Länge, Seeking auf zehn Sekunden und ausschließlich eigene Loopback-URLs oder private vorbereitete FLAC-/OGG-Dateien. Die Gegenproben mit leerer Playlist sowie mit einer Seek-Zeit hinter dem Titelende scheitern sichtbar. Nach dem stummen Test wird Stop geprüft, bevor die vorherige Lautstärke wiederhergestellt wird.

## Grenzen

Der SDK-Testhost simuliert Winamp-Nachrichten, nicht den gesamten Player. Eine positive HTTP-Antwort beweist keine Audioausgabe. Ein Fortschrittszeiger belegt die Verarbeitung durch Winamp, ersetzt aber kein menschliches Hörurteil. Die zwei Stunden Grant-Lebensdauer wurden noch nicht durch einen entsprechend langen Lauf geprüft. Die aktuellen Prüfungen belegen keinen allgemeinen Ausschluss von Speicherlecks.
