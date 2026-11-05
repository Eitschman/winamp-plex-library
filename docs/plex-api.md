# Plex-API-Vertrag

Primärquelle ist die [offizielle PMS-API](https://developer.plex.tv/pms/). Die folgenden Pfade sind dokumentierte Verträge beziehungsweise ausdrücklich markierte Referenzbefunde. Ein Eintrag in dieser Tabelle ist keine Behauptung, dass die zugehörige Produktfunktion schon implementiert oder am realen Server geprüft wurde.

## Authentifizierung

1. Pro Installation einen zufälligen Client-Identifier anlegen.
2. `POST https://plex.tv/api/v2/pins?strong=true` mit `Accept: application/json`, `X-Plex-Product` und `X-Plex-Client-Identifier`. Die Antwort enthält unter anderem `id`, `code`, `expiresIn` und zunächst kein Zugriffstoken.
3. Die Plex-Anmeldeseite `https://app.plex.tv/auth#?...` im Systembrowser öffnen. Die Parameter enthalten Client-ID, PIN-Code und Produktname. Kein Kontopasswort wird vom Plugin verarbeitet.
4. `GET /api/v2/pins/{id}` bei `plex.tv` etwa einmal pro Sekunde abfragen, bis `authToken` gesetzt ist, die Anforderung abläuft oder der Benutzer abbricht.
5. Mit dem Account-Token `GET https://clients.plex.tv/api/v2/resources?includeHttps=1&includeRelay=1&includeIPv6=0` abfragen.
6. Nur Ressourcen mit `provides=server` anbieten. Server besitzen `clientIdentifier`, `name`, `accessToken` und `connections[]`. Der Server-Token kann sich vom Account-Token unterscheiden.
7. Die von Plex gelieferten HTTPS-Verbindungen im Hintergrund auf Erreichbarkeit prüfen, lokal vor direktem Fernzugriff und Relay. `/identity` wird ohne Token abgefragt; `machineIdentifier` muss zur ausgewählten Serverressource passen. Erst danach wird der Ressourcen-Token für `/library/sections` verwendet. Eine manuelle Origin wird mit demselben Verfahren geprüft.
8. Serverprofile, Verbindungen, ausgewählten Zugang und Musikbibliothek mit Windows DPAPI speichern. Der Account-Token wird nach der Ressourcenermittlung nicht dauerhaft benötigt. Eine erneute Browseranmeldung erfolgt ausschließlich über den Anmeldebutton.

Plex dokumentiert zusätzlich JWT-basierte Verfahren mit Geräteschlüsseln und Token-Erneuerung. Der Prototyp verwendet den ebenfalls offiziell beschriebenen starken PIN-Ablauf. Abgelaufene oder widerrufene Serverzugänge erfordern eine sichtbare erneute Anmeldung, keinen stillen Wechsel auf andere Tokens.

## Server und Musik

| Zweck | Pfad / Struktur | Umsetzung |
| --- | --- | --- |
| Serveridentifikation | `GET /identity`, `MediaContainer.machineIdentifier` | Anmeldung vergleicht die Identität. |
| Fähigkeiten und Versionsdaten | `GET /` | Recherchiert; kein umfassendes Capability-Modell. |
| Bibliotheken | `GET /library/sections`, `MediaContainer.Directory[]` mit `key`, `title`, `type` | Musikbibliotheken haben `type=artist`. |
| Künstler | `/library/sections/{id}/all?type=8` | Native paginierte Ansicht, Suche und Sortierung. |
| Alben | `/library/sections/{id}/all?type=9` | Native paginierte Ansicht, Suche und Sortierung. |
| Titel | `/library/sections/{id}/all?type=10` | Native paginierte Ansicht und Playlistübergabe. |
| Metadaten | `/library/metadata/{ratingKey}`, `MediaContainer.Metadata[]` | Grundlage für die gezielte Medienauflösung. |
| Kinder | `/library/metadata/{ratingKey}/children` | Künstler → Alben, Album → Titel. |
| Audio-Playlists | `/playlists?playlistType=audio`, `/playlists/{id}` und `/playlists/{id}/items` | Übersicht, aktuelle Detailwerte und Titel in Plex-Reihenfolge; leere sowie gefüllte Playlist nativ geprüft. |
| Bilder | Relativer `thumb`-Schlüssel unter `/library/metadata/{id}/thumb`, optional numerischer Versionspfad. | Authentifizierter Originalbildtransport und WIC-Miniatur. Titel verweisen über `parentThumb` auf das Albumcover. Kein Foto-Transcoding erforderlich. |
| Direkte Medienbytes | `Media[].Part[].key`, zum Beispiel `/library/parts/46618/1389985872/file.flac` | WinHTTP für Proxy oder private FLAC-/OGG-/ADTS-AAC-Vorbereitung; nie das lokale Serverdateifeld `file` verwenden. |
| Suche und Sortierung | Typisierte `all`-Abfrage mit dokumentierten Bibliotheksfiltern und `sort` | Serverfähigkeiten prüfen; keine SQL-ähnliche Syntax erfinden. |

Ein Titel kann mehrere `Media`-Varianten und mehrere `Part`-Einträge besitzen. Eine allgemeine Implementierung muss diese Auswahl sichtbar und reproduzierbar behandeln. Der Prototyp darf nicht beliebige zusätzliche Parts durch Zusammenhängen von URLs simulieren. Laufzeiten sind API-seitig Millisekunden; Winamp erwartet im Playlist-IPC Sekunden.

## Pagination

`X-Plex-Container-Start` und `X-Plex-Container-Size` werden gemeinsam gesendet. Die Antwort kann eine andere Seitengröße als angefordert besitzen. Maßgeblich sind tatsächliches `offset`, `size`, optional `totalSize` sowie gegebenenfalls die entsprechenden Antwortheader. Ein leeres Ergebnis darf keine Endlosschleife erzeugen. Eine nicht paginierte Antwort darf nicht wiederholt als neue Seite angehängt werden. Eine allgemeine Sammlung aller Datensätze ist kein Ersatz für bedarfsgesteuerte Ansichten.

## JSON und XML

Der Client fordert JSON explizit an. Fehlende oder falsch typisierte Felder sind Fehler. Der Prototyp enthält noch keinen XML-Verarbeitungspfad. Ein zusätzlicher XML-Pfad darf nur bei einem nachgewiesenen Serverbedarf ergänzt werden und muss mit DTD-/Entitätsauflösung abgeschaltet getestet werden. XML darf keinen HTTP-, Authentifizierungs- oder Parserfehler verdecken. Die [Plex-Supportseite](https://support.plex.tv/articles/201638786-plex-media-server-url-commands/) liefert offizielle XML-Beispiele für diesen späteren Kompatibilitätstest.

## Wiedergabestatus und schreibende Funktionen

`POST /:/timeline` meldet `key=/library/metadata/{id}`, `ratingKey`, Zustand und Position sowie Dauer in Millisekunden. Die offizielle API verlangt Meldungen bei Zustandswechseln und regelmäßig etwa alle zehn Sekunden. `X-Plex-Client-Identifier` identifiziert den Client. Neben der synthetischen API-Probe sind tatsächliches Abspielen, Pause, Fortsetzen, Seeking und Stop in Winamp über `GET /status/sessions` zurückgelesen. Eine erste Meldung kann Position null enthalten, während Winamp noch puffert; der Fortschrittsnachweis erfolgt separat am Player.

`GET /status/sessions/history/all?metadataItemID={id}&sort=viewedAt%3Adesc` liefert die gefilterte Historie. Die native Probe vergleicht vor und nach einem regulären Titelende den neuesten `historyKey` und zusätzlich `viewCount` aus `GET /library/metadata/{id}`. Beide haben sich am freigegebenen Testtitel geändert. Der Test springt vor das Titelende und lässt Winamp danach regulär auslaufen. Das bestätigt diesen Ablauf am vorhandenen Server, keine allgemeine Zählregel für übersprungene oder kurz angespielte Titel. Es wird kein zusätzlicher Scrobble-Request gesendet. Parameter und Antwortfelder sind in der [offiziellen PMS-API](https://developer.plex.tv/pms/) beschrieben.

`PUT /:/rate?identifier=com.plexapp.plugins.library&key={ratingKey}&rating={0..10}` setzt die persönliche Bewertung. Ein anschließendes `GET /library/metadata/{ratingKey}` liefert `userRating`. Der echte Schreib-/Rücklesetest hat eine Testbewertung gesetzt und die ursprüngliche Bewertung wiederhergestellt. Der Wert 0 entspricht in der Plugin-Oberfläche keiner Bewertung. Ganze Winamp-Sterne werden mit Faktor zwei auf die Plex-Skala abgebildet; vorhandene Bruchteile werden ohne Rundung im Modell gehalten. Die native Titelansicht verwendet diesen Ablauf für genau einen ausgewählten Titel. Fehler oder ausbleibende Bestätigung werden sichtbar gemeldet. Playlists und Bibliotheksinhalte werden darüber nicht bearbeitet.

Plexas Audio-Transcoding unter `/music/:/transcode/universal/start.mp3` ist zusätzlich im Referenzadapter belegt. Noch nicht belegt ist die erforderliche Parameterkombination für diesen Winamp-Client. Es gibt deshalb keinen automatischen Transcoding-Versuch.

`GET /status/sessions` ist der offizielle Endpunkt für aktive Wiedergaben. Die [PlexAPI-Referenzimplementierung](https://github.com/pkkid/python-plexapi/blob/master/plexapi/base.py) liest `TranscodeSession` unmittelbar unter dem jeweiligen Medienobjekt; fehlt dieses Element, meldet die Sitzung keine Transcodierung. Der Integrationsprüfer ordnet die Sitzung zuerst anhand von `Player.machineIdentifier` dem eigenen Client zu und verlangt einen aktiven Player. Ein fehlender Player ist kein Nachweis für Direct Play. Eine vorhandene `TranscodeSession` wird als Fehler behandelt. Der echte MP3-Lauf prüft dies während Abspielen, Pause, Fortsetzen und nach Seeking; eine synthetische Gegenprobe erkennt die eingefügte Transcoding-Sitzung. Nachweis: `.local/logs/timeline-no-transcode-native.log`. Diese Beobachtung ist auf die geprüfte Sitzung und die Abfragezeitpunkte begrenzt.

## Fehler und Sicherheitsgrenzen

401/403: Zugriff verweigert; 404: Datei oder Metadaten fehlen; 416: Byte-Range nicht erfüllbar. Redirects werden nicht gefolgt. Andere Upstream-Fehler ergeben am lokalen Proxy einen leeren HTTP-502-Fehler. Antworttexte, Cookies und Location-Header gehen nicht an Winamp. Die UI zeigt nur klassifizierte Codes, nie Tokens oder rohe API-Antworten.

Der Client bindet jeden Request an eine vorab ausgewählte Origin. Medienpfade müssen relative, geprüfte Plex-Part-Pfade sein. Absolute URLs, Netzwerkpfade, eingebettete Zugangsdaten, Query-Tokens und Header-Injection werden abgewiesen. Der Proxy sendet keine vom lokalen HTTP-Client gelieferten Authentifizierungsheader an Plex.
