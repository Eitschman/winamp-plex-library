# Prüfstand und Integrationscheckliste

Prüfdatum: 04.11.2026. Dieser Stand trennt ausgeführte Prüfungen von offenen Produktanforderungen.

## Ausgeführte Prüfungen

- MSVC-Release-Build mit Visual Studio 2022 Build Tools, x86. Die DLL entsteht unter `build/Release/ml_plex.dll`.
- SDK-Testhost: 100 Folgen aus Laden, fehlgeschlagener Knotenregistrierung, erfolgreicher Initialisierung, Ansichtserzeugung, Beenden und Entladen. Fremde Knoten-IDs werden nicht akzeptiert; die Anzahl der USER-Handles bleibt im Test gleich.
- Sicherheitsprüfungen mit gültigen und ungültigen Origins, Headern, Medienpfaden, Query-Encoding, Capabilities und Byte-Ranges.
- Transport gegen einen synthetischen Loopback-PMS: bytegenaue Vollantwort, HEAD, begrenzte und offene Ranges, Suffix-Range, 416, 401/403/404, 302/500, kein Weiterreichen privater Header, ungültige Capabilities, Origin-/Host-Prüfung und Abbruch einer tatsächlich ausstehenden Anfrage.
- DPAPI-Roundtrip mit Unicode, Prüfung auf fehlende Klartext-Zugangsdaten, atomarer Austausch sowie Gegenproben mit beschädigter Konfiguration und unsicherer Origin.
- Authentifizierungs-Parsing mit synthetischen Serverressourcen, getrenntem Server-Token, HTTPS-Verbindungen, leerer Musikbibliotheksliste und fehlerhaften Antworten.
- Tatsächliche Winamp-Testkopie der Version 5.9.2 Build 10042: DLL erkannt, Plex-Knoten und eigene Ansicht sichtbar. Die Darstellung wurde als Screenshot angesehen. Die Instanz wurde regulär beendet und mit ausgetauschter Test-DLL neu gestartet.
- Native Browseranmeldung: starke PIN erstellt, Systembrowser geöffnet, Benutzer hat den Zugriff für die Anwendung bestätigt, Serverressourcen wurden geladen. Die ausgewählte HTTPS-Verbindung und Serveridentität wurden geprüft; eine Musikbibliothek wurde angezeigt und zur Speicherung ausgewählt.
- Katalog-Livetest: Künstler, Alben, Titel und Audio-Playlists, zweite Seiten, Suchabfragen und Unterelemente gegen den echten Server geprüft. Album-Kinder werden ohne zusätzliche Sortierung abgefragt; die reale Disc-/Titelreihenfolge wurde geprüft. Ein leerer erster Playlistinhalt wird korrekt als leer behandelt.
- `scripts/Test-PlexLibrary.ps1`: Künstler → drei Alben → fünf Titel → einen Titel an die bestehende Winamp-Playlist angehängt; Zurück-Navigation und alle vier Baumansichten geprüft. Die reale Testbibliothek meldete 305 Künstler, 1333 Alben und 20770 Titel. Private Inhalte bleiben im lokalen Testordner.
- Classic, Winamp Modern, Bento und Big Bento tatsächlich geladen und Screenshots angesehen. Farben, Tabellenkopf, Scrollbalken und Steuerelemente wechseln mit dem Skin. Die Darstellung wurde außerdem bei 1000 × 450 Pixeln im Bento-Container geprüft. Keine Zusage für sämtliche fremden Skins, DPI-Stufen oder Farbschemata.

Die Screenshots und aktuellen Buildlogs liegen nur im ignorierten lokalen Testordner. Sie enthalten gegebenenfalls private Anzeigeinformationen und werden nicht veröffentlicht. Synthetische Testdaten enthalten keine echten Plex-Zugänge.

## Automatisierte Ausführung

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

CTest enthält `plugin_lifecycle`, `transport`, `config_auth`, `catalog`, `artwork`, `artwork_provider` und `security`. Sieben von sieben Tests sind durchgelaufen. Der Katalogtest prüft unter anderem Unicode, fehlende Titel, geerbte Albumcover, große Playlistlaufzeiten, fehlerhafte Pagination, Pfadinjektion, Reihenfolge und Cache-Ablauf/LRU/Leerung. Transporttests prüfen unabhängige Grants, gezielten Widerruf und Cover-HTTP-Fehler. Bildtests prüfen WIC-Decodierung, Größenlimits, beschädigte Bilder, sichere Pfade, fehlende Bilder und den BMP-Roundtrip. Der Provider-Test prüft Dispatch, Registrierung, fehlende Hostdienste, Allokationsfehler, Schreibschutz und Ressourcenfreigabe. Der SDK-Host prüft Skin-Registrierung, Dienstfreigabe und 100 Ladezyklen. Einzelne Prüfbedingungen sind keine Aussage über die Abdeckung aller Produktfunktionen.

## Phase-1-Schranke

Der echte Wiedergabenachweis läuft separat über `scripts/Test-PlexPlayback.ps1` gegen die isolierte Instanz. Das Skript entschlüsselt ausschließlich die vom Plugin gespeicherte eigene DPAPI-Datei, fragt eine begrenzte Titelseite ab und verwendet den nativen Testdialog. Es gibt weder Tokens noch private Medienpfade aus. Eine Prüfung ist erst erfolgreich, wenn Winamps Wiedergabezeiger fortschreitet und ein Seek auf zehn Sekunden nachgewiesen ist.

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
- [ ] Widerrufenes echtes Token und erneute Anmeldung.
- [ ] Nicht erreichbarer Server, DNS-Fehler und Zertifikatsfehler im nativen Dialog.
- [ ] Leere Musikbibliothek und Konto ohne Server.
- [ ] Deutsche und englische Dialoge, Tastatur und Fokus visuell prüfen.

### Audio

| Format | Echtes Plex Direct Play | Länge / Titel | Seeking | Abbruch / Neustart |
| --- | --- | --- | --- | --- |
| MP3 | getestet | getestet | getestet | weitere Fehlerfälle offen |
| FLAC | offen | offen | offen | offen |
| AAC | offen | offen | offen | offen |
| M4A | offen | offen | offen | offen |
| OGG | offen | offen | offen | offen |

- [ ] Winamp-Playlist speichern und kontrollieren, dass keine Plex-Tokens enthalten sind.
- [ ] Abgelaufene lokale Capability und URL nach Plugin-Beendigung abweisen.
- [ ] Fehlende Mediendatei und Verbindungsunterbrechung während des Abspielens.
- [ ] Prüfen, dass Plex tatsächlich keine Transcoding-Session erzeugt.
- [ ] Keine anderen Titel oder Konfigurationen der vorhandenen Winamp-Instanz verändern.

### Bibliotheken und Erweiterungen

Künstler → Alben → Titel, einzelne Playlistübergabe und die vier Baumansichten sind real geprüft. Das native, skinned Kontextmenü hat das Testalbum mit 13 Titeln an Winamp übergeben und abgespielt. Seeking lieferte Rückgabewert 0 und anschließend Position 10429 ms. Die gespeicherte Playlist enthält ausschließlich lokale Capability-URLs. Alte Testeinträge wurden auf Benutzerwunsch entfernt; weitere Testläufe dürfen keine doppelten Alben hinterlassen. Wiedergabetests werden stumm ausgeführt und danach gestoppt; die vorherige Lautstärke wird erst nach Stop wiederhergestellt.

„Grand Serpent Rising“ von Dimmu Borgir wurde in der Albumansicht mit Cover, Jahr 2026 und 13 Titeln visuell geprüft. Der reale Katalog-Livetest bestätigt Album-ID und vorhandenen Coverpfad für alle 13 Titel. Ein Album ohne Bild ist synthetisch einschließlich HTTP 404 geprüft, noch nicht als reale fehlende Coveranzeige. Die horizontale Tabellenposition wird beim Spaltenwechsel zurückgesetzt und wurde visuell geprüft.

Die zusätzliche Player-Coveranzeige ist offen: Der `DATABASE`-Album-Art-Dienst ist im echten Host registriert und besitzt 13 Bildzuordnungen. Big Bento fragt diesen Dienst während der HTTP-Wiedergabe nicht ab; dessen Playerfelder zeigen weiterhin das Standardbild. Artist-/Album-/Titelmetadaten werden dagegen über `ML_IPC_HOOKEXTINFOW` übernommen und sind sichtbar. Die Bibliotheks-Cover sind davon getrennt. Die Titelansicht mit geerbten Miniaturen benötigt ihre eigene Sichtprüfung.

Weitere Mehrfachauswahl, Sortierung, Paging und Sprache brauchen zusätzliche Bedienungstests. Serverseitige Pagination und Suche sind im Katalog-Livetest geprüft; der Cache besitzt synthetische Tests. Wiedergabestatus, Verlauf und Bewertungen benötigen eine tatsächliche API-Rückleseprüfung. „Als Nächstes abspielen“ wird nur bei belegter Winamp-Schnittstelle ergänzt.

## Grenzen

Der SDK-Testhost simuliert Winamp-Nachrichten, nicht den gesamten Player. Eine positive HTTP-Antwort beweist keine Audioausgabe. Ein Fortschrittszeiger belegt die Verarbeitung durch Winamp, ersetzt aber kein menschliches Hörurteil. Die zwei Stunden Grant-Lebensdauer wurden noch nicht durch einen entsprechend langen Lauf geprüft. Die aktuellen Prüfungen belegen keinen allgemeinen Ausschluss von Speicherlecks.
