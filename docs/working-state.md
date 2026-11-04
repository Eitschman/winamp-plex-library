# Arbeitsstand und Wiedereinstieg

Die Entwicklung läuft am ursprünglichen Plugin-Auftrag einschließlich Bibliotheksansichten, Wiedergabe und Qualitätssicherung weiter. Das PRP unter `prp/prp.md` ist ein veränderbares Arbeitsdokument. Der Benutzer hat den lokalen Proxy als Architektur bestätigt.

## Nächster konkreter Schritt

Die Phase-1-Wiedergabeprüfung ist für einen echten Plex-MP3-Titel bestanden. `src/plugin/plugin.cpp` verwendet `WM_COPYDATA` mit einer temporären M3U8-Datei, `ML_IPC_HOOKTITLEW` und den im SDK dokumentierten Play-Befehl. Wiedergabe, Seeking, 306 Sekunden Länge und Plex-Titel sind nachgewiesen; die exportierte Testplaylist ist tokenfrei. Details und konkrete Messwerte stehen in `docs/testing.md`.

1. Die allgemeine API-/Modellschicht und Bibliotheksansichten mit Pagination, Suche und Sortierung umsetzen. UI- und API-Modelle trennen, Anfragen abbrechen und alte Ergebnisse verwerfen.
2. Album-/Mehrfachwiedergabe und Plex-Playlists ergänzen. Dafür einen begrenzten Listener mit mehreren unabhängigen Grants verwenden, keine vier Threads pro Titel anlegen.
3. Cache, Cover und zuverlässig nachweisbare Erweiterungen sowie die übrigen Tests des PRP bearbeiten. Weitere echte Audioformate und Fehlerzustände prüfen.
4. Den manuellen Token-Testdialog und das überholte Token-Skript durch den regulären Bibliotheksablauf ersetzen. Die Browseranmeldung bleibt der Benutzerablauf.

## Lokal vorhandene Umgebung

- Visual Studio 2022 Build Tools: `C:\BuildTools2022`, MSVC-x86 und Windows SDK installiert.
- CMake/CTest: `C:\BuildTools2022\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`.
- Debugger: `C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\cdb.exe`.
- Unveränderte SDK-5.55-Header: `.local/sdk`. CMake prüft die dokumentierten SHA-256-Werte.
- Getrennte Testkopie: `.local/winamp-test`. Eigene Klasse `WinampPlexTest`, eigene Einstellungen unter `.local/winamp-test/settings`. Nur diese Kopie für Installation und Laufzeittests verwenden.
- Die normale Winamp-Installation ist nicht verändert. Beim Anhalten lief ausschließlich deren Prozess mit ID 28052 weiter; vor weiteren Tests den tatsächlichen Pfad erneut prüfen.
- Die Browseranmeldung ist bestätigt. Der Plugin-eigene Zugriff liegt DPAPI-geschützt unter `%LOCALAPPDATA%\WinampPlexLibrary\connection.dpapi`. Ausschließlich den dokumentierten Anwendungszugriff verwenden, keine Zugangsdaten suchen oder ausgeben.
- Die derzeit in der Testkopie liegende DLL kann vom nächsten Quellbuild abweichen. Vor dem nächsten Start bei geschlossener Testkopie den aktuellen Build kopieren.

## Nachweise und Diagnoseartefakte

`docs/testing.md` enthält den fachlichen Prüfstand. Beim Anhalten wurde der aktuelle MSVC-x86-Release-Build erfolgreich gebaut. Der anschließende CTest-Lauf bestand alle vier Tests `plugin_lifecycle`, `transport`, `config_auth` und `security` in 5,97 Sekunden. Allgemeine Bibliotheks- und Cachetests fehlen noch. Es waren danach keine Plugin-Test- oder Debuggerprozesse aktiv; die Testkopie ließ sich regulär über `WM_CLOSE` beenden.

Der aktuelle reale Erfolgsnachweis liegt in `.local/logs/playback-metadata-hook.log`. Die festen lokalen Fehlercodes sind in `ProxyStatus` verfügbar, ohne rohe Requests zu speichern. Eine Winamp-Anfrage ohne Port im Host-Header ist geprüft und wird ausschließlich für `127.0.0.1` zugelassen. Falsche explizite Ports und fremde Hosts bleiben verboten. `IPC_STARTPLAY` setzt in der Zielversion die Position auf 0; `WINAMP_BUTTON2` erhält die gesetzte Position. Alte ignorierte Debuglogs sind keine Nachweise aktueller Testläufe.

Die nativen Screenshots liegen nur unter `.local`. Root-Ansicht und Plex-Anmeldedialoge wurden visuell angesehen. Die vollständigen Bibliotheksansichten existieren noch nicht. Das manuelle Token-Skript `scripts/Set-PlexTestAccess.ps1` und der manuelle Testdialog sind noch zu bereinigen, sobald der reguläre Ablauf sie ersetzt.

## Freigaben und Grenzen

Die Installation der Build Tools und Tests in einer getrennten Winamp-Kopie sind freigegeben. Das normale Winamp, Plex-Konfiguration und Routereinstellungen nicht verändern. Kein öffentliches Repository, kein Push und keine automatische Installation in die vorhandene Winamp-Installation. Plexa ist die Verhaltensreferenz; mangels nachgewiesener Lizenz wird dessen Quellcode nicht übernommen. Persönliche Daten und Binärdateien bleiben außerhalb des Repositorys.
