# Prüfstand und Integrationscheckliste

Prüfdatum: 04.11.2026. Dieser Stand trennt ausgeführte Prüfungen von offenen Produktanforderungen.

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

CTest enthält `plugin_lifecycle`, `transport`, `config_auth`, `catalog`, `artwork`, `artwork_provider` und `security`. Sieben von sieben Tests sind durchgelaufen. Der Katalogtest prüft unter anderem Unicode, fehlende Titel, geerbte Albumcover, große Playlistlaufzeiten, fehlerhafte Pagination, Pfadinjektion, Reihenfolge und Cache-Ablauf/LRU/Leerung. Transporttests prüfen unabhängige Grants, gezielten Widerruf und Cover-HTTP-Fehler. Bildtests prüfen WIC-Decodierung, Größenlimits, beschädigte Bilder, sichere Pfade, fehlende Bilder und den PNG-Roundtrip. Der Provider-Test prüft Dispatch, Registrierung, fehlende Hostdienste, Allokationsfehler, Schreibschutz und Ressourcenfreigabe. Konfigurationsprüfungen umfassen DPAPI, Größenlimits, beschädigte Profile, Cache-Optionen und die priorisierte Verbindungsauswahl mit synthetischer Erreichbarkeit. Der SDK-Host prüft beide Hostsprachen, Einstellungen, Diagnose, Skin-Registrierung, Dienstfreigabe und 100 Ladezyklen. Einzelne Prüfbedingungen sind keine Aussage über die Abdeckung aller Produktfunktionen.

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

| Format | Echtes Plex Direct Play | Länge / Titel | Seeking | Abbruch / Neustart |
| --- | --- | --- | --- | --- |
| MP3 | getestet | getestet | getestet | weitere Fehlerfälle offen |
| FLAC | fehlgeschlagen: Winamp verlangt lokale Datei | nicht geprüft | nicht geprüft | Decoderfehlerdialog erkannt und geschlossen |
| AAC | offen | offen | offen | offen |
| M4A | getestet, MP4-Container | 251 Sekunden, Titel über reguläre Bibliotheksübergabe | getestet, 10.550 ms nach Sprung auf 10 Sekunden | weitere Fehlerfälle offen |
| OGG | Fortschritt 551 ms nachgewiesen | Decoder meldet Länge 0 | abgewiesen, Rückgabecode -1 | Test gestoppt; weitere Fälle offen |

`catalog_probe.exe --formats <lokaler-Ausgabepfad>` erfasst die gewählte Musikbibliothek seitenweise und erzeugt optional einen privaten Titel-/Formatbericht ohne URLs oder Zugangsdaten. Der reale Lauf prüfte 20.782 stabile Titel-IDs: 20.332 MP3-, 77 MP4-, 186 OGG-, ein FLAC- und 100 weitere Containerangaben; 86 Titel besitzen keine vom derzeitigen Direct-Play-Selektor unterstützte Variante. Bei 1.434 Titeln fehlt eine Cover-Referenz. Containerangaben sind kein Decoder-Nachweis. Der lokale Bericht und private Titel werden nicht eingecheckt.

`scripts/Test-PlexFormats.ps1` verwendet diesen Bericht ausschließlich zur Auswahl über die regulären nativen Bibliotheksansichten. Mehrdeutige Titel werden über Künstler und Album eingegrenzt und die tatsächliche Dateiendung der sicheren Playlist-URL geprüft. Der reale Gesamtlauf meldet zwei erfolgreiche und zwei fehlgeschlagene Formate; sein Fehlerstatus ist beabsichtigt. Der FLAC-Dialog meldet ausdrücklich „Cannot stream flac file, please download it“. Bei OGG beantwortet derselbe Proxy eine Range-Anfrage korrekt mit HTTP 206; Winamp lehnt den Positionswechsel dennoch ab. Eine ausschließlich auf HTTP-Streaming beruhende Wiedergabearchitektur erfüllt damit die Anforderungen für diese Zielversion noch nicht. Ein eigener Decoder oder automatisches Transcoding wurde nicht ergänzt. Der nächste Implementierungsschritt muss den lokalen Dateizugriff der vorhandenen Decoder berücksichtigen und erneut mit diesen Originalmedien geprüft werden.

- [x] Winamp-Testplaylist gespeichert und jede Medienzeile gegen das vollständige Loopback-URL-Format ohne Plex-Tokens geprüft. M4A-/MP3-Titel außerdem mit Winamps aktuellem Fenstertitel verglichen.
- [ ] Abgelaufene lokale Capability und URL nach Plugin-Beendigung abweisen.
- [ ] Fehlende Mediendatei und Verbindungsunterbrechung während des Abspielens.
- [ ] Prüfen, dass Plex tatsächlich keine Transcoding-Session erzeugt.
- [ ] Keine anderen Titel oder Konfigurationen der vorhandenen Winamp-Instanz verändern.

Die Formatprüfung besitzt eine positive Gegenprobe mit M4A und MP3 und eine synthetische negative Gegenprobe mit einer unzulässigen Dateiendung. Die gültigen Formate bestehen; die ungültige Probe liefert einen Fehlerstatus. Der Gesamtlauf bleibt wegen der nachgewiesenen FLAC-/OGG-Grenzen absichtlich rot. Die sieben unabhängigen CTest-Suiten bestehen weiterhin; diese Ergebnisse dürfen nicht miteinander verwechselt werden.

### Bibliotheken und Erweiterungen

Künstler → Alben → Titel, einzelne Playlistübergabe und die vier Baumansichten sind real geprüft. Das native, skinned Kontextmenü hat das Testalbum mit 13 Titeln an Winamp übergeben und abgespielt. Seeking lieferte Rückgabewert 0 und anschließend Position 10429 ms. Die gespeicherte Playlist enthält ausschließlich lokale Capability-URLs. Alte Testeinträge wurden auf Benutzerwunsch entfernt; weitere Testläufe dürfen keine doppelten Alben hinterlassen. Wiedergabetests werden stumm ausgeführt und danach gestoppt; die vorherige Lautstärke wird erst nach Stop wiederhergestellt.

„Grand Serpent Rising“ von Dimmu Borgir wurde in der Albumansicht mit Cover, Jahr 2026 und 13 Titeln visuell geprüft. Der reale Katalog-Livetest bestätigt Album-ID und vorhandenen Coverpfad für alle 13 Titel. Ein Album ohne Bild ist synthetisch einschließlich HTTP 404 geprüft, noch nicht als reale fehlende Coveranzeige. Die horizontale Tabellenposition wird beim Spaltenwechsel zurückgesetzt und wurde visuell geprüft.

Die Player-Coveranzeige ist in Big Bento visuell geprüft: Beide Player-Coverfelder zeigen das Testalbum. Der Dienst besitzt 13 Bildzuordnungen; der aktuelle stumme Lauf meldet 14 erfolgreiche Bildabfragen. Die Wiedergabe ist anschließend gestoppt. Die Gegenproben zeigen: Der Host fragt die Kategorie `DATABASE` für diese HTTP-Titel nicht ab; die Kategorie `EMBEDDED` wird abgefragt. Das angebotene V5-BMP wurde trotz positiver WIC-Decodierung nicht angezeigt. Mit PNG und der SDK-Dateitypkennung `png` erscheinen die Bilder. Eine positive Decodierung in WIC allein reicht damit nicht als Hostnachweis. Artist-/Album-/Titelmetadaten werden über `ML_IPC_HOOKEXTINFOW` übernommen und sind sichtbar. Auch die Titelansicht mit geerbten Miniaturen ist visuell geprüft.

Weitere Mehrfachauswahl, Sortierung, Paging und Sprache brauchen zusätzliche Bedienungstests. Serverseitige Pagination und Suche sind im Katalog-Livetest geprüft; der Cache besitzt synthetische Tests. Wiedergabestatus, Verlauf und Bewertungen benötigen eine tatsächliche API-Rückleseprüfung. „Als Nächstes abspielen“ wird nur bei belegter Winamp-Schnittstelle ergänzt.

## Regulärer Wiedergabetest

`scripts/Test-PlexPlayback.ps1` arbeitet mit der bereits über die Bibliothek gefüllten Testplaylist. Es liest keine DPAPI-Datei und übernimmt keine manuell eingetragenen Tokens. Der reale positive Lauf prüft Fortschritt, bekannte Länge, Seeking auf zehn Sekunden und ausschließlich lokale Capability-URLs. Die Gegenproben mit leerer Playlist sowie mit einer Seek-Zeit hinter dem Titelende scheitern sichtbar. Nach dem stummen Test wird Stop geprüft, bevor die vorherige Lautstärke wiederhergestellt wird. Das entfernte manuelle Testskript und der Testdialog gehören nicht zum Produktpfad; ihre Quellen bleiben im lokalen Git-Verlauf rekonstruierbar.

## Grenzen

Der SDK-Testhost simuliert Winamp-Nachrichten, nicht den gesamten Player. Eine positive HTTP-Antwort beweist keine Audioausgabe. Ein Fortschrittszeiger belegt die Verarbeitung durch Winamp, ersetzt aber kein menschliches Hörurteil. Die zwei Stunden Grant-Lebensdauer wurden noch nicht durch einen entsprechend langen Lauf geprüft. Die aktuellen Prüfungen belegen keinen allgemeinen Ausschluss von Speicherlecks.
