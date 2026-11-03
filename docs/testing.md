# Prüfstand und Integrationscheckliste

Prüfdatum: 03.11.2026. Dieser Stand trennt ausgeführte Prüfungen von offenen Produktanforderungen.

## Ausgeführte Prüfungen

- MSVC-Release-Build mit Visual Studio 2022 Build Tools, x86. Die DLL entsteht unter `build/Release/ml_plex.dll`.
- SDK-Testhost: 100 Folgen aus Laden, fehlgeschlagener Knotenregistrierung, erfolgreicher Initialisierung, Ansichtserzeugung, Beenden und Entladen. Fremde Knoten-IDs werden nicht akzeptiert; die Anzahl der USER-Handles bleibt im Test gleich.
- Sicherheitsprüfungen mit gültigen und ungültigen Origins, Headern, Medienpfaden, Query-Encoding, Capabilities und Byte-Ranges.
- Transport gegen einen synthetischen Loopback-PMS: bytegenaue Vollantwort, HEAD, begrenzte und offene Ranges, Suffix-Range, 416, 401/403/404, 302/500, kein Weiterreichen privater Header, ungültige Capabilities, Origin-/Host-Prüfung und Abbruch einer tatsächlich ausstehenden Anfrage.
- DPAPI-Roundtrip mit Unicode, Prüfung auf fehlende Klartext-Zugangsdaten, atomarer Austausch sowie Gegenproben mit beschädigter Konfiguration und unsicherer Origin.
- Authentifizierungs-Parsing mit synthetischen Serverressourcen, getrenntem Server-Token, HTTPS-Verbindungen, leerer Musikbibliotheksliste und fehlerhaften Antworten.
- Tatsächliche Winamp-Testkopie der Version 5.9.2 Build 10042: DLL erkannt, Plex-Knoten und eigene Ansicht sichtbar. Die Darstellung wurde als Screenshot angesehen. Die Instanz wurde regulär beendet und mit ausgetauschter Test-DLL neu gestartet.
- Native Browseranmeldung: starke PIN erstellt, Systembrowser geöffnet, Benutzer hat den Zugriff für die Anwendung bestätigt, Serverressourcen wurden geladen. Die ausgewählte HTTPS-Verbindung und Serveridentität wurden geprüft; eine Musikbibliothek wurde angezeigt und zur Speicherung ausgewählt.

Die Screenshots und aktuellen Buildlogs liegen nur im ignorierten lokalen Testordner. Sie enthalten gegebenenfalls private Anzeigeinformationen und werden nicht veröffentlicht. Synthetische Testdaten enthalten keine echten Plex-Zugänge.

## Automatisierte Ausführung

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

CTest enthält `plugin_lifecycle`, `transport`, `config_auth` und `security`. Einzelprogramme melden ihre Anzahl von Prüfbedingungen. Eine gezählte Bedingung ist keine Aussage über die Abdeckung aller Produktfunktionen. Die allgemeine Bibliotheks-, Pagination- und Cache-Schicht ist noch nicht implementiert und hat deshalb noch keine entsprechende Testsuite.

## Phase-1-Schranke

Der echte Wiedergabenachweis läuft separat über `scripts/Test-PlexPlayback.ps1` gegen die isolierte Instanz. Das Skript entschlüsselt ausschließlich die vom Plugin gespeicherte eigene DPAPI-Datei, fragt eine begrenzte Titelseite ab und verwendet den nativen Testdialog. Es gibt weder Tokens noch private Medienpfade aus. Eine Prüfung ist erst erfolgreich, wenn Winamps Wiedergabezeiger fortschreitet und ein Seek auf zehn Sekunden nachgewiesen ist.

Ein technischer Proxytest allein erfüllt diese Schranke nicht. Der aktuelle echte Wiedergabe- und Seeking-Nachweis ist noch offen. Vor dessen Ergebnis wird die allgemeine Bibliotheksimplementierung nicht als nutzbar beschrieben.

Der echte MP3-Test mit `enqueueFileWithMetaStructW` stürzt innerhalb von Winamp bei `IPC_ENQUEUEFILEW` ab. Der Debugger zeigt einen Lesezugriff auf `0x132`, den übergebenen Wert der Titellänge. Die entsprechende API-Regression ist auch im offiziellen Winamp-Forum für 5.9.1/5.9.2 gemeldet. Es wird keine alternative Struktur geraten. Mit `WM_COPYDATA` lässt sich ein Titel anhängen, ohne diesen Absturz auszulösen. Dieser Test scheitert anschließend: Winamp meldet `access denied`, der Proxy hat noch keinen Plex-Upstream-Status. Der genaue Ablehnungsgrund der lokalen Anfrage ist offen. Die aktuelle Übergabe transportiert außerdem noch keinen Plex-Titel und keine vorgegebene Länge.

Die lokale Übergabe ist somit ein untersuchter Implementierungsstand, kein erfolgreicher Audio-Nachweis. Der nächste Schritt ist die sichere Diagnose des lokalen Request-Validators mit Winamps tatsächlicher Anfrage. Anschließend müssen Metadaten, fortschreitende Wiedergabe und Seeking gemeinsam geprüft werden.

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
- [ ] Neustart mit gespeicherter DPAPI-Verbindung.
- [ ] Widerrufenes echtes Token und erneute Anmeldung.
- [ ] Nicht erreichbarer Server, DNS-Fehler und Zertifikatsfehler im nativen Dialog.
- [ ] Leere Musikbibliothek und Konto ohne Server.
- [ ] Deutsche und englische Dialoge, Tastatur und Fokus visuell prüfen.

### Audio

| Format | Echtes Plex Direct Play | Länge / Titel | Seeking | Abbruch / Neustart |
| --- | --- | --- | --- | --- |
| MP3 | offen | offen | offen | offen |
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

Noch umzusetzen und danach zu testen: Künstler → Alben → Titel, Album-/Mehrfachwiedergabe, Playlists, Sortierung, Suche, serverseitige Pagination, große Ergebnismengen, Unicode-Zeilendarstellung, Cover, begrenzter Metadaten-Cache und Cache-Leerung. Wiedergabestatus, Verlauf und Bewertungen benötigen zusätzlich eine tatsächliche API-Rückleseprüfung. „Als Nächstes abspielen“ wird nur bei belegter Winamp-Schnittstelle ergänzt.

## Grenzen

Der SDK-Testhost simuliert Winamp-Nachrichten, nicht den gesamten Player. Eine positive HTTP-Antwort beweist keine Audioausgabe. Ein Fortschrittszeiger belegt die Verarbeitung durch Winamp, ersetzt aber kein menschliches Hörurteil. Die zwei Stunden Grant-Lebensdauer wurden noch nicht durch einen entsprechend langen Lauf geprüft. Die aktuellen Prüfungen belegen keinen allgemeinen Ausschluss von Speicherlecks.
