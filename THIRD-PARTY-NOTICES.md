# Fremde Komponenten und Quellen

## Winamp SDK 5.55

Bezugsquelle: https://download.nullsoft.com/winamp/plugin-dev/WA5.55_SDK.exe

SHA-256 des geprüften Installationsarchivs:
`d88abbb9882add625e6286dc68fb7598645862a191d63577be84b72ef0870370`

Verwendet werden die unveränderten Dateien `gen_ml/ml.h` und `Winamp/wa_ipc.h`. Beide enthalten die Nullsoft-Lizenz mit freier Nutzung, Änderung und Weitergabe unter Erhalt des Lizenztextes, korrekter Herkunftsangabe und Kennzeichnung veränderter Fassungen. Die Lizenz wird als zlib-artige Lizenz eingeordnet. Maßgeblich ist der jeweilige Originaltext im Header. Die Header werden lokal beschafft und nicht im Repository mitgeliefert. Die MIT-Lizenz des Projekts ersetzt diese Lizenz nicht.

Das gesamte SDK enthält weitere Beispiele und Komponenten mit eigenen Bedingungen. Aus der Header-Lizenz wird keine pauschale Freigabe aller SDK-Dateien abgeleitet. Insbesondere wird kein Material aus dem 2024 veröffentlichten Winamp-Quellbaum verwendet.

`gen_tray/WINAMPCMD.H` liefert die ursprünglichen numerischen Befehlsdefinitionen, insbesondere `WINAMP_BUTTON2`. Dieser reine Definitionsheader enthält keinen eigenen Lizenztext und wird ebenfalls ausschließlich lokal aus dem originalen SDK beschafft. Der Header und die SDK-Beispiele werden nicht mit dem Projekt weitergegeben; es wird kein Beispielprogramm übernommen.

## nlohmann/json 3.12.0

Quelle: https://github.com/nlohmann/json/releases/tag/v3.12.0

Lizenz: MIT, siehe https://json.nlohmann.me/home/license/ und den Lizenztext im heruntergeladenen Paket. CMake prüft die SHA-256 des Release-Archivs. Bei einer Binärweitergabe ist der Lizenztext aus dem Paket beizulegen. Es wird keine zusätzliche JSON-Laufzeit-DLL benötigt.

## Windows und Buildwerkzeuge

WinHTTP, Winsock, BCrypt und DPAPI sind Windows-Systemschnittstellen. Die Microsoft-Werkzeuge und Windows-SDK-Dateien werden nicht mit dem Quellprojekt weitergegeben. Eine Binärweitergabe muss die gegebenenfalls erforderlichen Microsoft-VC-Runtime-Bedingungen berücksichtigen.

## Referenzprojekte

- SDK-Beispiel `ml_xmlex`: Untersuchung von Initialisierung, Unicode-Baumknoten und `CreateDialog` im Lifecycle. Keine Beispielimplementierung wurde als Produktcode kopiert.
- [Plexa](https://github.com/rupisaini123/plexa): Verhaltensreferenz für Browseranmeldung, Serverauswahl, Bibliotheksnavigation und Mediengateway. Im untersuchten Dateibaum wurde keine projektweite LICENSE-Datei gefunden. Deshalb kein Quellcode übernehmen oder als MIT-Code ausgeben. Die Schnittstellen werden anhand der offiziellen Plex-Dokumentation eigenständig implementiert.
- [Python PlexAPI](https://github.com/pushingkarmaorg/python-plexapi): Gegenprüfung von API-Pfaden und Medienpart-Schlüsseln. Keine Laufzeitabhängigkeit und kein übernommener Quellcode.
