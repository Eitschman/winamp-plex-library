# Winamp Plex Library: Anforderungen und Arbeitsstand

Dieses PRP wird als Arbeitsdokument gepflegt. Der konkrete Prüfstand steht in `docs/testing.md`, Schnittstellen und Quellen in `docs/research.md` und `docs/winamp-sdk.md`.

## Verbindliche Projektentscheidungen

- Die Plex-Anmeldung erfolgt wie bei Plexa im Browser über den Plex-PIN-Anmeldevorgang. Der Benutzer gibt sein Passwort ausschließlich bei Plex ein. Eine manuelle Token-Eingabe gehört nicht zum regulären Ablauf.
- Einstellungen öffnen zunächst ohne Browseranmeldung. Die normale Ansicht enthält Anmeldung, Server und Musikbibliothek; feste Verbindungswege und eine manuelle HTTPS-Adresse stehen unter „Erweitert“. Erreichbare lokale HTTPS-Verbindungen werden vor Fernzugriff und Relay gewählt. Der tatsächliche Zugangsweg wird angezeigt.
- Die Sprache folgt ausschließlich dem aktiven Winamp-Sprachpaket. Es gibt weder in der Bibliotheksansicht noch in den Einstellungen eine eigene Sprachauswahl. Deutsch und Englisch sind enthalten.
- Die Bibliothekskopfzeile benennt die Musikbibliothek; eine Dropdownauswahl erscheint nur bei mehreren Bibliotheken. Hauptansichten wiederholen den Baumtitel nicht. Kontextüberschrift und „Zurück“ gehören nur zu Unteransichten; Seitenwechsel heißen „Vorherige Seite“ und „Nächste Seite“.
- Plexa dient als Referenz für Anmeldung, Serverauswahl und Musikzugriff. Winamp übernimmt die Wiedergabe. Alexa und ein dauerhaft laufender externer Dienst gehören nicht zur Architektur.
- Plex-Tokens bleiben technisch für API-Zugriffe erforderlich. Sie werden nach der Browserfreigabe vom Plugin bezogen, mit Windows DPAPI gespeichert und ausschließlich als HTTP-Header an den ausgewählten, geprüften Server übertragen.
- Winamp erhält ausschließlich zeitlich begrenzte Loopback-URLs ohne Plex-Token. Der lokale Proxy verwendet Direct Play und prüft TLS-Zertifikate für den Plex-Zugriff.
- Laufzeittests erfolgen in einer ausdrücklich freigegebenen, getrennten Winamp-Testkopie. Die vorhandene Installation und ihre Konfiguration werden nicht verändert. Die Installation der VS-2022-Build-Tools ist freigegeben.
- Persönliche Serveradressen, Zugangsdaten und API-Antworten mit privaten Inhalten gehören nicht in dieses Dokument oder das Repository.

## Nachgewiesener Arbeitsstand

- Implementiert und getestet: MSVC-x86-Release-Build, SDK-basierter Plugin-Export, Plex-Knoten und native Ansicht, reguläres Beenden und Neustarten der Testkopie.
- Implementiert und getestet: Plex-Browseranmeldung mit bestätigter Freigabe, HTTPS-Serverprüfung, Musikbibliotheksabfrage und DPAPI-Speicherung.
- Implementiert und mit synthetischen Daten getestet: HTTP-Transport, Loopback-Proxy, Bereiche für Seeking, Abbruch, URL-Sicherheitsprüfungen und Fehlerstatus.
- Implementiert und in Winamp 5.9.2 Build 10042 getestet: echte Plex-MP3-Wiedergabe über den lokalen Proxy, Seeking auf zehn Sekunden, Länge und Plex-Titel in der tokenfreien Winamp-Playlist. Verwendet werden die dokumentierte `WM_COPYDATA`-Playlistübergabe, der Titel-Hook und der Play-Befehl des SDK. Unbekannte ABI-Felder werden nicht ergänzt.
- Implementiert: typisierte Bibliotheksabfragen, Pagination, Suche, Sortierung, virtuelle Tabellen, Album-/Mehrfachübergabe, Plex-Playlists und begrenzter Metadaten-Cache. Künstler → Alben → Titel → Winamp-Playlist und vier Navigationsansichten sind mit realen Daten geprüft; weitere Nachweise stehen in `docs/testing.md`.
- Die Darstellung verwendet Winamps Skin-Engine und ist in Classic, Winamp Modern, Bento und Big Bento visuell geprüft. Es gibt keine eigene Player-Steuerleiste. Doppelklick und Kontextmenü übergeben die Auswahl an Winamp, dessen vorhandene Bedienelemente Wiedergabe, Pause, Stop und Seeking übernehmen.
- Albumcover sind im Hintergrund geladen und am realen Album „Grand Serpent Rising“ mit 13 Titeln visuell geprüft. Titel übernehmen vorhandene Albumcover über `parentThumb`. Fehlende Bilder werden synthetisch geprüft. Die Titelansicht und beide Coverfelder des Big-Bento-Players zeigen die zugeordneten Bilder; die Darstellung wurde tatsächlich angesehen.
- Geplant beziehungsweise noch nachzuweisen: weitere Audioformate, erweiterte Fehler- und Bedienungstests sowie Plex-Schreibfunktionen. Die Testplaylist enthält keine alten Versuche oder doppelten Testalben. Weitere Abspieltests erfolgen möglichst stumm und werden danach gestoppt.
- Zusätzliches späteres Kompatibilitätsziel ist WACUP. Die Host-Anbindung bleibt von Plex-Client und Transport getrennt. Die laufende Entwicklung und ihre verbindlichen Integrationsnachweise richten sich zunächst gegen Winamp 5.9.2 Build 10042. WACUP ist ohne eigene Laufzeitprüfung keine zugesicherte Zielversion.

## 1. Projektziel

Entwickle ein natives Media-Library-Plugin für den klassischen Winamp unter Windows.

**Projektname:** Winamp Plex Library  
**Plugin-DLL:** `ml_plex.dll`  
**Zielplattform:** Windows 10/11, x86 (32-Bit)  
**Zielversion:** Winamp 5.9.2 Build 10042 (x86), 26.04.2023  
**Programmiersprache:** C++17 oder neuer, sofern mit dem SDK kompatibel  
**Buildsystem:** CMake mit MSVC  
**Ziel:** Vollständig nutzbare Integration eines Plex Media Servers in die klassische Winamp-Medienbibliothek.

Die Anwendung soll zunächst ausschließlich privat eingesetzt werden. Eine spätere Open-Source-Veröffentlichung auf GitHub soll ohne größere Umstrukturierung möglich sein.

Arbeite möglichst eigenständig und führe die Entwicklung über mehrere zusammenhängende Arbeitspakete durch. Stoppe nicht nach der Erstellung eines Projektgerüsts.

## 2. Grundlegende Anforderungen

Das Plugin muss:

- Von Winamp 5.9.2 als Media-Library-Plugin erkannt werden.
- Einen eigenen Eintrag „Plex“ im Navigationsbaum der Medienbibliothek erzeugen.
- Eine Verbindung zu einem vorhandenen Plex Media Server herstellen.
- Plex-Musikbibliotheken erkennen und auswählbar machen.
- Künstler, Alben und Titel anzeigen.
- Musik direkt über Winamp abspielen.
- Plex-Playlists unterstützen, sofern technisch zuverlässig umsetzbar.
- Such- und Filterfunktionen anbieten.
- Fehler und Verbindungsprobleme sauber behandeln.
- Die Winamp-Oberfläche jederzeit reaktionsfähig halten.
- Einstellungen und Zugangsdaten sicher speichern.
- Deutsch und Englisch unterstützen.

Das Plugin darf keine Änderungen am Plex Media Server voraussetzen.

Es soll keinen dauerhaft laufenden externen Dienst benötigen. Ein optionaler lokaler HTTP-Proxy innerhalb des Plugin-Prozesses ist zulässig, falls er für sichere und zuverlässige Wiedergabe notwendig wird.

## 3. Verbindliche Recherchephase

Bevor mit der eigentlichen Implementierung begonnen wird, recherchiere die technischen Grundlagen anhand möglichst offizieller Quellen und existierender Open-Source-Projekte.

### 3.1 Winamp SDK

Untersuche:

- Verfügbarkeit und Lizenz des klassischen Winamp SDK.
- Media-Library-Plugin-API.
- Struktur `winampMediaLibraryPlugin`.
- Registrierung und Entfernung eigener Navigationsknoten.
- `ML_MSG_TREE_ONCREATEVIEW` und zugehörige Nachrichten.
- Erstellung eigener Media-Library-Ansichten.
- Winamp IPC und Playlist-Integration.
- Übergabe von URLs und Metadaten.
- Kompatibilität mit Winamp 5.9.2 Build 10042.
- Bekannte ABI- und Plugin-Kompatibilitätsprobleme.
- Plugin-Lifecycle und Thread-Sicherheit.

Suche nach vorhandenen Media-Library-Plugins als Referenzimplementierung.

Verwende keine unbestätigten SDK-Strukturen, Nachrichtenkonstanten oder Funktionssignaturen.

Falls das SDK nicht unmittelbar verfügbar ist, dokumentiere die Bezugsquelle und prüfe die rechtlichen Rahmenbedingungen. Rekonstruiere Schnittstellen nicht durch unbelegte Annahmen.

### 3.2 Plex Media Server API

Untersuche die dokumentierten und tatsächlich verfügbaren Plex-Endpunkte für:

- Serveridentifikation
- Authentifizierung
- Musikbibliotheken
- Künstler
- Alben
- Titel
- Playlists
- Metadaten
- Cover-Art
- Mediendateien
- Direkte Wiedergabe
- Transcoding
- Wiedergabestatus
- Bewertungen
- Bibliotheksänderungen

Bevorzuge die direkte Kommunikation mit dem Plex Media Server.

Nutze JSON-Antworten, soweit der jeweilige Endpunkt sie unterstützt. Falls erforderlich, unterstütze XML als Fallback.

Verifiziere Endpunkte und Antwortstrukturen anhand offizieller Dokumentation oder realer, anonymisierter API-Beispiele.

### 3.3 Audio-Wiedergabe

Untersuche besonders gründlich:

1. Kann Winamp authentifizierte Plex-HTTP-URLs direkt abspielen?
2. Funktioniert HTTPS zuverlässig?
3. Werden MP3, FLAC, AAC, M4A und OGG korrekt unterstützt?
4. Funktioniert Seeking?
5. Werden Dateilänge und Metadaten richtig erkannt?
6. Was passiert beim Ablauf oder Widerruf eines Plex-Tokens?
7. Kann ein lokaler Streaming-Proxy die Authentifizierung übernehmen?
8. Wie können unnötige Transcoding-Vorgänge vermieden werden?

Vergleiche direkte URLs und einen lokalen Proxy hinsichtlich Sicherheit, Zuverlässigkeit und Implementierungsaufwand.

**Wichtig:** Plex-Tokens dürfen nicht ungeschützt in Logdateien, Fehlermeldungen oder exportierten Playlists erscheinen.

## 4. Architektur

Entwickle eine modular aufgebaute Architektur mit klar getrennten Komponenten.

Vorgeschlagene Struktur:

```text
winamp-plex-library/
├── CMakeLists.txt
├── README.md
├── LICENSE
├── .gitignore
├── docs/
│   ├── research.md
│   ├── architecture.md
│   ├── plex-api.md
│   ├── winamp-sdk.md
│   ├── build.md
│   └── testing.md
├── src/
│   ├── plugin/
│   ├── plex/
│   ├── playback/
│   ├── ui/
│   ├── config/
│   ├── cache/
│   └── common/
├── include/
├── resources/
│   ├── icons/
│   └── locales/
├── tests/
└── scripts/
```

Passe die Struktur bei Bedarf begründet an.

### 4.1 Plugin Core

Verantwortlich für:

- Winamp-Plugin-Initialisierung
- Plugin-Lifecycle
- Media-Library-Integration
- Navigation
- Event-Verarbeitung
- Fehlerbehandlung
- Ressourcenfreigabe

### 4.2 Plex API Client

Implementiere einen eigenständigen Plex-Client mit:

- WinHTTP oder einer begründeten Alternative
- TLS-Zertifikatsprüfung
- Token-Authentifizierung
- Timeouts
- Abbruch laufender Anfragen
- Fehlerklassifizierung
- JSON/XML-Verarbeitung
- Pagination
- URL-Encoding
- Unterstützung großer Bibliotheken

Keine blockierenden Netzwerkoperationen auf dem Winamp-UI-Thread.

### 4.3 Datenmodelle

Definiere typisierte Modelle für:

- PlexServer
- PlexLibrary
- PlexArtist
- PlexAlbum
- PlexTrack
- PlexPlaylist
- PlexMediaPart
- PlexConnectionSettings

Nutze stabile Plex-IDs zur Zuordnung.

Trenne API-Modelle von UI-Modellen.

### 4.4 Playback Controller

Verantwortlich für:

- Wiedergabe einzelner Titel
- Übergabe an die Winamp-Playlist
- Anhängen mehrerer Titel
- Albumwiedergabe
- Playlistwiedergabe
- Auflösung von Plex-Media-URLs
- Authentifizierung
- Fehlerbehandlung

Verwende nach Möglichkeit Winamps vorhandene Audio-Decoder.

Implementiere keinen eigenen Decoder, solange keine technische Notwendigkeit besteht.

### 4.5 Konfiguration

Speichere:

- Serveradresse
- Port
- HTTPS-Einstellung
- ausgewählte Musikbibliothek
- UI-Präferenzen
- Cache-Einstellungen
- Die Sprache aus dem aktiven Winamp-Sprachpaket übernehmen; keine eigene Spracheinstellung speichern.

Speichere Zugangsdaten mit Windows DPAPI oder einem gleichwertigen Windows-Mechanismus.

Keine fest eingebauten Tokens, Passwörter, Serveradressen oder persönlichen Daten.

## 5. Benutzeroberfläche

Integriere das Plugin möglichst natürlich in die klassische Winamp-Medienbibliothek.

Die Oberfläche soll optisch zu Winamp Legacy passen.

### 5.1 Navigation

Geplante Baumstruktur:

```text
Medienbibliothek
├── Lokale Medien
├── ...
└── Plex
    ├── Künstler
    ├── Alben
    ├── Titel
    └── Playlists
```

Falls mehrere Musikbibliotheken existieren, muss eine sinnvolle Auswahl möglich sein.

### 5.2 Künstleransicht

Zeige Künstler alphabetisch sortiert.

Beim Auswählen eines Künstlers sollen die zugehörigen Alben angezeigt werden.

### 5.3 Albumansicht

Zeige:

- Albumtitel
- Künstler
- Erscheinungsjahr
- Anzahl der Titel
- Cover, falls verfügbar

Doppelklick auf ein Album soll dessen Titel zur Wiedergabe bereitstellen.

### 5.4 Titelansicht

Zeige mindestens:

- Titel
- Künstler
- Album
- Laufzeit
- Tracknummer
- Jahr
- Audioformat, sofern verfügbar

Unterstütze Sortierung, Mehrfachauswahl und Kontextmenüs.

### 5.5 Wiedergabefunktionen

Unterstütze:

- Jetzt abspielen
- Als Nächstes abspielen, sofern zuverlässig implementierbar
- Zur Playlist hinzufügen
- Ganzes Album abspielen
- Mehrere ausgewählte Titel abspielen

### 5.6 Suche

Implementiere eine Suche nach:

- Künstler
- Album
- Titel

Die Suche darf die Benutzeroberfläche nicht blockieren.

### 5.7 Einstellungen

Stelle einen Einstellungsdialog bereit mit:

- Server auswählen; feste Verbindung und Plex-HTTPS-Adresse unter „Erweitert“.
- Anmeldung mit Plex im Browser sowie Verbindungsstatus
- Verbindung testen
- Musikbibliothek auswählen
- Cache leeren
- Diagnoseinformationen

Nutze native Windows-Steuerelemente oder die vorhandenen Winamp-Schnittstellen.

## 6. Performance und Stabilität

Berücksichtige Bibliotheken mit mehreren Tausend Alben und Zehntausenden Titeln.

Anforderungen:

- Asynchrone Netzwerkkommunikation
- Keine blockierenden UI-Aufrufe
- Pagination
- Bedarfsgesteuertes Laden
- Vermeidung unnötiger API-Abfragen
- Thread-sichere Datenstrukturen
- Kontrollierte Hintergrundthreads
- Saubere Freigabe aller Ressourcen
- Keine Speicherlecks
- Keine unkontrollierten Exceptions über Plugin-ABI-Grenzen

Ein nicht erreichbarer Plex-Server darf Winamp weder blockieren noch zum Absturz bringen.

Beim Beenden von Winamp müssen alle Hintergrundaktivitäten zuverlässig beendet werden.

## 7. Sicherheit

Beachte insbesondere:

- Keine Zugangsdaten im Quellcode.
- Keine Tokens im Repository.
- Keine Tokens in normalen Logdateien.
- Sichere lokale Speicherung.
- TLS-Zertifikatsprüfung nicht deaktivieren.
- Keine unkontrollierte Weiterleitung authentifizierter URLs.
- Keine unnötig geöffneten Netzwerkports.
- Bei lokalem Proxy ausschließlich Loopback-Bindung.
- Keine Offenlegung von Tokens über lokale Proxy-URLs.
- Begrenzte Lebensdauer temporärer Wiedergabe-URLs.
- Keine Weitergabe von Zugangsdaten an fremde Hosts.

Beachte, dass Winamp möglicherweise Wiedergabe-URLs intern speichert. Untersuche dies ausdrücklich.

## 8. Entwicklung in Phasen

### Phase 1 – Recherche und Proof of Concept

Erstelle ein minimales, lauffähiges Media-Library-Plugin.

Akzeptanzkriterien:

- DLL wird erfolgreich als x86 gebaut.
- Winamp erkennt das Plugin.
- Plugin erscheint in der Plugin-Verwaltung.
- Eintrag „Plex“ erscheint im Navigationsbaum.
- Anklicken erzeugt eine einfache Plugin-Ansicht.
- Winamp beendet sich ohne Absturz.

Teste anschließend die Wiedergabe eines Plex-Titels.

Erst nach erfolgreicher technischer Validierung mit Phase 2 fortfahren.

Falls kein echter Winamp oder Plex-Server in der Entwicklungsumgebung verfügbar ist, dokumentiere die noch offenen Integrationstests ausdrücklich. Behaupte keine erfolgreiche Laufzeitvalidierung.

### Phase 2 – Plex-Verbindung

Implementiere:

- Konfiguration
- Authentifizierung
- Verbindungstest
- Bibliotheksabfrage
- Fehlerbehandlung

Akzeptanzkriterien:

- Gültiger Server wird erkannt.
- Musikbibliotheken werden angezeigt.
- Ungültige Zugangsdaten werden verständlich gemeldet.
- Netzwerkfehler führen nicht zu einem Absturz.

### Phase 3 – Bibliotheksansichten

Implementiere:

- Künstler
- Alben
- Titel
- Sortierung
- Suche
- Navigation
- Kontextmenüs

Akzeptanzkriterien:

- Bibliotheksdaten werden korrekt angezeigt.
- Große Ergebnismengen bleiben bedienbar.
- Navigation funktioniert ohne vollständiges Neuladen aller Daten.

### Phase 4 – Wiedergabe

Implementiere:

- Einzelne Titel abspielen
- Album abspielen
- Mehrere Titel zur Playlist hinzufügen
- Korrekte Metadaten
- Seeking
- Fehlerbehandlung bei Verbindungsabbruch

Teste verschiedene Audioformate.

Bevorzuge Direct Play gegenüber Transcoding.

### Phase 5 – Erweiterungen

Implementiere, soweit technisch zuverlässig möglich:

- Plex-Playlists
- Albumcover
- Metadaten-Cache
- Aktualisierung der Bibliothek
- Wiedergabestatus
- Plex-Wiedergabeverlauf
- Bewertungen

Plex-spezifische Funktionen dürfen nur dann als implementiert gelten, wenn ihre tatsächliche Wirkung über die API nachgewiesen ist.

### Phase 6 – Qualitätssicherung

Prüfe:

- Start und Beenden von Winamp
- Wiederholtes Laden des Plugins
- Netzwerkunterbrechungen
- Ungültige Tokens
- Leere Bibliotheken
- Große Bibliotheken
- Unicode und Sonderzeichen
- Deutsche und englische Oberfläche
- Fehlende Mediendateien
- HTTPS-Verbindungen
- Fehlerhafte API-Antworten
- Speicher- und Ressourcenverwaltung

## 9. Tests

Implementiere automatisierte Tests für alle unabhängig von Winamp testbaren Komponenten.

Mindestens:

- Plex-API-Parsing
- Datenmodelle
- URL-Verarbeitung
- Pagination
- Fehlerbehandlung
- Konfigurationsverwaltung
- Cache-Verhalten
- Sicherheitsrelevante URL-Verarbeitung

Verwende Mock-Responses für API-Tests.

Echte Plex-Zugangsdaten dürfen niemals Bestandteil automatisierter Tests sein.

Erstelle eine manuelle Testcheckliste für Winamp 5.9.2 Build 10042.

## 10. Build und Installation

Das Projekt muss mit Visual Studio 2022 und CMake für x86 baubar sein.

Erstelle einen reproduzierbaren Buildprozess.

Ergebnis:

```text
build/Release/ml_plex.dll
```

Dokumentiere:

- Voraussetzungen
- SDK-Beschaffung
- Abhängigkeiten
- CMake-Konfiguration
- Debug-Build
- Release-Build
- Installation
- Deinstallation
- Troubleshooting

Das Plugin soll manuell in das Media-Library-Plugin-Verzeichnis von Winamp kopiert werden können.

Prüfe den tatsächlichen Plugin-Pfad der Zielinstallation und verwende keine ungeprüften Standardpfade.

Führe keine automatische Installation in eine vorhandene Winamp-Installation durch, solange diese nicht ausdrücklich freigegeben wurde.

## 11. Dokumentation

Erstelle eine ausführliche Projektdokumentation.

### README.md

Enthält:

- Projektbeschreibung
- Unterstützte Winamp-Versionen
- Funktionen
- Voraussetzungen
- Installation
- Konfiguration
- Bekannte Einschränkungen
- Build-Anleitung
- Entwicklungsstatus

### docs/research.md

Dokumentiere:

- Gefundene SDK-Versionen
- Offizielle Quellen
- Referenzprojekte
- Kompatibilitätsprobleme
- Plex-API-Erkenntnisse
- Offene technische Fragen

### docs/architecture.md

Dokumentiere:

- Komponenten
- Verantwortlichkeiten
- Datenfluss
- Threading-Modell
- Fehlerbehandlung
- Sicherheitskonzept
- Playback-Architektur

### docs/testing.md

Dokumentiere:

- Automatisierte Tests
- Manuelle Integrationstests
- Bekannte Probleme
- Noch nicht verifizierte Funktionen

## 12. Git und spätere Veröffentlichung

Initialisiere das Projekt als Git-Repository, sofern noch keines vorhanden ist.

Erstelle eine sinnvolle `.gitignore` für C++, Visual Studio und CMake.

Committe keine:

- Zugangsdaten
- Plex-Tokens
- persönlichen Serveradressen
- lokalen Konfigurationsdateien
- Binärdateien ohne ausdrückliche Freigabe
- proprietären SDK-Dateien ohne entsprechende Lizenz

Bereite das Projekt auf eine spätere GitHub-Veröffentlichung vor.

Erstelle aber zunächst kein öffentliches Repository und führe keinen Push ohne ausdrückliche Freigabe durch.

Prüfe die Lizenzbedingungen des Winamp-SDK und sämtlicher Abhängigkeiten, bevor eine Open-Source-Lizenz festgelegt wird.

## 13. Arbeitsweise

Arbeite eigenständig, systematisch und nachvollziehbar.

Wichtige Regeln:

1. Recherchiere zuerst, implementiere anschließend.
2. Verwende echte SDK-Definitionen und verifizierte API-Endpunkte.
3. Erfinde keine Funktionen, Konstanten oder SDK-Strukturen.
4. Baue regelmäßig und behebe Compilerfehler unmittelbar.
5. Verwende keine Platzhalterimplementierungen, die als fertig ausgegeben werden.
6. Trenne getestete Funktionen von ungetesteten Annahmen.
7. Dokumentiere technische Entscheidungen.
8. Bevorzuge robuste, wartbare Lösungen gegenüber schnellen Hacks.
9. Halte Abhängigkeiten möglichst gering.
10. Ändere keine vorhandenen Winamp- oder Plex-Konfigurationen.
11. Führe keine destruktiven Aktionen ohne Freigabe durch.
12. Arbeite nach Möglichkeit bis zu einem funktionsfähigen MVP weiter.
13. Halte die Dokumentation während der Entwicklung aktuell.
14. Führe keine automatischen Commits durch, sofern dies nicht ausdrücklich vereinbart wurde.

Bei technischen Problemen sollst du zunächst eigenständig nach Alternativen suchen und diese bewerten.

Frage nur nach, wenn eine Entscheidung nicht sicher oder sinnvoll selbst getroffen werden kann.

## 14. Ergebnisbericht

Liefere nach jedem größeren Arbeitspaket einen kompakten Statusbericht mit:

- Implementierten Funktionen
- Geänderten Dateien
- Build-Ergebnis
- Testergebnissen
- Bekannten Einschränkungen
- Offenen Fragen
- Nächsten Arbeitsschritten

Kennzeichne ausdrücklich, welche Funktionen tatsächlich getestet wurden.

Unterscheide zwischen:

- Implementiert und getestet
- Implementiert, aber noch nicht getestet
- Geplant
- Technisch blockiert

## 15. Definition of Done

Die erste produktiv nutzbare Version gilt als abgeschlossen, wenn:

- `ml_plex.dll` erfolgreich als x86-DLL gebaut wird.
- Winamp 5.9.2 Build 10042 das Plugin stabil lädt.
- „Plex“ in der Medienbibliothek erscheint.
- Eine Plex-Verbindung konfiguriert werden kann.
- Musikbibliotheken erkannt werden.
- Künstler, Alben und Titel angezeigt werden.
- Titel direkt in Winamp abgespielt werden können.
- Alben vollständig abgespielt werden können.
- Die Winamp-Playlist korrekt befüllt wird.
- Netzwerkfehler zuverlässig behandelt werden.
- Zugangsdaten sicher gespeichert werden.
- Winamp ohne Absturz beendet werden kann.
- Die Dokumentation vollständig ist.
- Ein reproduzierbarer Release-Build existiert.

Funktionen, die mangels realer Testumgebung nicht verifiziert werden können, bleiben ausdrücklich als offene Integrationstests gekennzeichnet.

## 16. Startanweisung

Beginne jetzt mit der technischen Recherche.

Prüfe zunächst das Winamp-SDK, die Media-Library-Plugin-Schnittstellen und die Plex-API.

Erstelle anschließend die Architektur und implementiere einen minimalen Proof of Concept.

Setze die Entwicklung danach eigenständig in möglichst großen, zusammenhängenden und testbaren Arbeitspaketen fort.

Priorisiere einen tatsächlich funktionierenden Plex-Musikplayer innerhalb von Winamp gegenüber optionalen Komfortfunktionen.

**Das Ziel ist ein echtes, benutzbares Winamp-Plex-Plugin – keine reine Machbarkeitsstudie und kein bloßes Projektgerüst.**
