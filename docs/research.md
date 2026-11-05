# Technische Recherche

## Separate ADTS-AAC-Dateien

Die [Media-Foundation-Formatübersicht](https://learn.microsoft.com/en-us/windows/win32/medfound/supported-media-formats-in-media-foundation) führt ADTS als AAC-Quelle und MPEG-4 als Ausgabemedium. Der [MPEG-4-Dateisink](https://learn.microsoft.com/en-us/windows/win32/medfound/mpeg-4-file-sink) unterstützt `MEDIASUBTYPE_RAW_AAC1`; seine Nutzdaten enthalten die AudioSpecificConfig. Die [AAC-Medientypen](https://learn.microsoft.com/en-us/windows/win32/medfound/aac-media-types) und `MF_READWRITE_DISABLE_CONVERTERS` bilden den SDK-Vertrag. Das Plugin reicht komprimierte AAC-Pakete ohne Encoder oder Decoder durch. Die ADTS-Feldbelegung ist mit den primären FFmpeg-Referenzen `libavcodec/adts_header.c` und `libavcodec/bsf/aac_adtstoasc.c` abgeglichen; daraus wird kein Quellcode übernommen. Der [ID3v2.4-Vertrag](https://id3.org/id3v2.4.0-structure) beschreibt die separat berücksichtigten Metadatenlängen. Native Messwerte und Grenzen stehen in [testing.md](testing.md).

Recherche am 03.11.2026. Dokumentationsnachweise, Quellcodebefunde und tatsächliche Laufzeitprüfungen werden getrennt behandelt. Laufzeitbefunde stehen in [testing.md](testing.md).

## Winamp-SDK

Das historische [Nullsoft-SDK 5.55](https://download.nullsoft.com/winamp/plugin-dev/WA5.55_SDK.exe) ließ sich über HTTPS herunterladen und ohne Ausführung mit 7-Zip extrahieren. Es enthält `gen_ml/ml.h`, `Winamp/wa_ipc.h` sowie unter anderem die Media-Library-Beispiele `ml_xmlex`, `ml_http` und `mlExplorer`. Die verwendeten Header enthalten die permissiven Nullsoft-Bedingungen. Keine proprietären SDK-Binärdateien werden eingecheckt.

Zusätzlich wurden die vom früheren Winamp-Entwickler bereitgestellten [WACUP-SDK-Header](https://getwacup.com/sdk/) untersucht. Deren `ml.h` unterscheidet die ABI-Versionen `0x15`, `0x16` und `0x17`. Die Fassung ab Winamp 5.66 ergänzt einen Service-Zeiger und verwendet eine Unicode-Beschreibung. Der ältere `0x15`-Vertrag mit ANSI-Beschreibung wird dort ausdrücklich weiter unterstützt. Das Projekt bindet das prüfsummengesicherte SDK 5.55 ein und verwendet dessen Definitionen, keine nachgebauten Strukturen und keine WACUP-spezifischen Funktionen.

Das Beispiel `ml_xmlex/main.cpp` registriert einen Unicode-Baumknoten über `MLTREEITEMW` und `ML_IPC_TREEITEM_ADDW`. `xmlview.cpp` behandelt `ML_MSG_TREE_ONCREATEVIEW` ausschließlich für seine eigene Knoten-ID und erzeugt einen nativen Kinddialog. Diese Abläufe bilden die Referenz für den Plugin-Prototyp. Das Beispiel enthält keine ausreichende Thread- oder Ressourcenverwaltung für den gesamten Projektumfang; diese wird eigenständig umgesetzt.

Die lokal vorgefundene `winamp.exe` meldet Dateiversion `5,9,2,10042`. `gen_ml.dll`, `in_mp3.dll`, `in_flac.dll`, `in_mp4.dll` und `in_vorbis.dll` sind vorhanden. Die verwendeten Schnittstellen wurden zusätzlich mit der tatsächlichen Winamp-Testkopie geprüft. Die Aussage gilt nicht automatisch für andere Winamp- oder WACUP-Versionen.

Für skinned Kontextmenüs wird der unveränderte veröffentlichte Header `gen_ml/ml_ipc_0313.h` von 2016 verwendet. Der Zielhost benötigt dessen 48-Byte-Struktur einschließlich der Anpassungsfelder. Die 40-Byte-Fassung im SDK-5.55-Archiv öffnet dort kein Popup. Hashes, Quellen und positive Laufzeitprüfung stehen in `winamp-sdk.md`. Das SDK-Beispiel `coverdirectory` belegt den Album-Art-Providervertrag und die Freigabe über `api_memmgr`. Es wird kein Beispielcode übernommen. Für die getesteten HTTP-Titel wird der dateibezogene Provider mit Kategorie `EMBEDDED` und PNG-Daten verwendet; Big Bento zeigt damit beide Player-Coverfelder. Der Provider verändert keine Mediendateien. Details und Gegenproben stehen in `testing.md`.

## Plex und Plexa

Die [offizielle PMS-API](https://developer.plex.tv/pms/) beschreibt JSON und XML, Token-Header, Bibliotheken, Metadaten, Pagination und Authentifizierung. JSON wird ausdrücklich über `Accept` angefordert. Die alte [Plex-Supportbeschreibung](https://support.plex.tv/articles/201638786-plex-media-server-url-commands/) enthält ergänzende XML-Beispiele für Server und Bibliotheken.

[Plexa](https://github.com/rupisaini123/plexa/blob/main/src/plex/auth.ts) verwendet einen stabilen Client-Identifier, einen starken PIN und eine Plex-seitige Browseranmeldung. Anschließend werden Serverressourcen abgefragt und der Token des ausgewählten Servers gespeichert. Die Anmeldung unterstützt damit die von Plex angebotenen Anmeldeverfahren, ohne das Kontopasswort in der Anwendung zu verarbeiten. Diese Vorgehensweise ist die verbindliche Anmeldereferenz für dieses Projekt. Das manuelle Kopieren eines Tokens ist kein vorgesehener Benutzerablauf.

Plexas [Adapter](https://github.com/rupisaini123/plexa/blob/main/src/plex/adapter.ts) und Mediengateway dienen als Vergleich für Pagination, Albumtitelauflösung, Playlistinhalte und Direct Play. Alexa, öffentliche Gateway-Endpunkte, Node-Dienst, Weboberfläche und dessen Verschlüsselungsschlüsselverwaltung werden nicht übernommen. Das Plugin läuft im Winamp-Prozess und verwendet Windows DPAPI. Medien werden lokal an Winamp weitergereicht.

Die gezielte URL-Prüfung am 04.11.2026 bestätigt: `getMediaParts` liest `Media`/`Part` aus den Metadaten, `buildDirectStreamUrl` kombiniert Server-Origin, Part-Schlüssel und Query-Token. Der [Gateway-Code](https://github.com/rupisaini123/plexa/blob/main/src/media/gateway.ts) erzeugt davon getrennte signierte, befristete Medienpfade und streamt intern vom PMS einschließlich Byte-Ranges. Die [Alexa-Übergabe](https://github.com/rupisaini123/plexa/blob/main/src/alexa/playback.ts) setzt diese Gateway-URL in `AudioPlayer.Play`, zusammen mit separaten Titelmetadaten und einem Queue-Token. Dieser Queue-Token ist kein Plex-Zugriffstoken. Plexa arbeitet damit ebenfalls mit einem Proxy. Für Winamp ist der prozessinterne Loopback-Proxy ausdrücklich gewählt; interne Plex-Zugriffe verwenden Header statt Query-Tokens. Plexas Redirect- und Transcoding-Verhalten wird nicht übernommen.

Die offizielle API unterscheidet Konto- und Serverzugriff. Der Account-Token wird für die Serverressourcen verwendet; der Ressourcen-Token wird ausschließlich an die ausgewählte Server-Origin gesendet. Die Serveridentität wird vor dem Speichern mit `machineIdentifier` verglichen. Sichere Verbindungen werden von Plex als Ressourcen-URIs angeboten. [Plex dokumentiert](https://support.plex.tv/articles/206225077-how-to-use-secure-server-connections/), weshalb ein bloßer Wechsel von HTTP auf HTTPS bei einer privaten Domain zu einem Zertifikats-Namensfehler führen kann.

## Wiedergabevergleich

| Weg | Nachweis / Grenze | Entscheidung |
| --- | --- | --- |
| Plex-URL mit Token im Query direkt an Winamp | SDK dokumentiert das Speichern von Playlist-URLs. Token könnten exportiert werden. | Kein Produktpfad. |
| Direkte HTTPS-URL ohne Token | `IPC_ENQUEUEFILEW` enthält URL, Titel und Länge, aber kein Feld für beliebige Authentifizierungsheader. Decoderabhängige Speziallösungen sind nicht belegt. | Keine verlässliche Authentifizierungsarchitektur. |
| Prozessinterner Loopback-Proxy | Authentifizierung im WinHTTP-Header; Winamp erhält nur einen zufälligen lokalen Grant. Echter MP3-Titel mit Seeking und Plex-Metadaten geprüft. | Gewählte Architektur; weitere Formate getrennt prüfen. |
| Audio-Transcoding | Plexa verwendet unter anderem `/music/:/transcode/universal/start.mp3`. Das ist ein Referenzbefund, kein eigener Laufzeitnachweis. | Nicht automatisch aktivieren; Direct Play zuerst nachweisen. |

MP3 und M4A bestehen den tatsächlichen Winamp-Test mit Fortschritt, Laufzeit und Seeking. FLAC wird vom Zieldecoder mit der Aufforderung zum lokalen Download abgewiesen. OGG spielt an, meldet aber Länge 0 und lehnt Seeking ab, obwohl der Proxy die zusätzliche Range-Anfrage korrekt beantwortet. AAC ist noch nicht tatsächlich geprüft. Das Vorhandensein eines Decoders beweist weder HTTP- noch HTTPS-, Range- oder Containerkompatibilität. Ein Proxy allein ergänzt keine fehlende URL-Unterstützung eines Input-Plugins.

Ein widerrufenes Token muss als HTTP 401/403 sichtbar werden. Der Proxy reicht keine fremden Redirects weiter und liefert keine authentifizierten URLs oder Antworttexte an Winamp. Beim Widerruf erfolgt weder Transcoding noch eine automatische Anmeldung mit anderen Zugangsdaten.

## Threading und offene technische Fragen

[Microsofts WinHTTP-Dokumentation](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpclosehandle) verlangt, dass ein Callback-Kontext bis zur letzten `HANDLE_CLOSING`-Benachrichtigung lebt. Der Transport wartet beim Schließen auf diesen Abschluss. Ein synchron blockierter Request wird nicht aus einem anderen Thread gewaltsam geschlossen. Netzwerkoperationen verwenden den asynchronen Modus und finden außerhalb des Winamp-UI-Threads statt.

Noch nachzuweisen sind echte Plex-Audioformate, Seek-Verhalten, Decoder-Metadaten, Auswirkungen langer Streams und wiederholter Netzunterbrechungen. Wiedergabestatus, Verlauf und Bewertungen werden erst nach einer API-Gegenprüfung als Produktfunktionen geführt. Eine bloß dokumentierte API oder eine HTTP-200-Antwort ohne Rückleseprüfung reicht dafür nicht.
