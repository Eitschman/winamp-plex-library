# Arbeitsstand und Wiedereinstieg

Die Entwicklung ist auf Benutzerwunsch für heute angehalten. „Weiter“ bedeutet, den ursprünglichen Plugin-Auftrag einschließlich Bibliotheksansichten, Wiedergabe und Qualitätssicherung fortzuführen. Das PRP unter `prp/prp.md` ist ein veränderbares Arbeitsdokument.

## Nächster konkreter Schritt

Die Phase-1-Wiedergabeprüfung ist offen. `src/plugin/plugin.cpp` verwendet derzeit `WM_COPYDATA` mit `IPC_ENQUEUEFILEW` und der lokalen Unicode-URL. Das Anhängen verursacht damit keinen Strukturabsturz. Der lokale Proxy lehnt die anschließende Winamp-Anfrage jedoch ab. Es gibt noch keinen erfolgreichen echten Audio- oder Seeking-Nachweis. Titel und Länge werden auf diesem Übergabepfad noch nicht transportiert.

1. Den festen Ablehnungsgrund aus `parse_local_request` sicher diagnostizieren. Keine vollständigen Requests, Token, Serveradressen oder Medienpfade protokollieren. Den tatsächlich von Winamp verwendeten HTTP-Request prüfen, bevor Validierungsregeln geändert werden.
2. Die Ursache im regulären Transportpfad beheben und positive sowie negative Sicherheitsfälle ergänzen. TLS-Prüfung, Host-Bindung, Capability und Redirect-Schutz erhalten.
3. Die dokumentierte Übergabe einschließlich Unicode-Titel und Länge lösen, ohne unbekannte SDK-Strukturfelder zu erfinden. Das direkte `WM_WA_IPC` mit alter Metadatenstruktur ist für diese Zielversion nachgewiesen ungeeignet.
4. `scripts/Test-PlexPlayback.ps1` gegen die getrennte Testkopie erneut ausführen. Der Test benötigt fortschreitende Wiedergabe und Seeking auf zehn Sekunden. Das Skript sollte vor weiteren Debugläufen begrenzt auf vorhandene Fenster warten und IPC-Zeitüberschreitungen verständlich behandeln.
5. Erst nach diesem Nachweis die allgemeine API-/Modellschicht und Bibliotheksansichten mit Pagination, Suche, Sortierung, Album-/Mehrfachwiedergabe und Plex-Playlists umsetzen. Danach Cache, Cover und zuverlässig nachweisbare Erweiterungen sowie die übrigen Tests des PRP bearbeiten.

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

Die ignorierten lokalen Dateien `.local/logs/playback-debug-current.log`, `.local/logs/playback-copydata.log`, `.local/logs/playback-copydata-debug.log` und `.local/logs/playback-copydata-probe.log` enthalten die konkreten Diagnoseversuche. Der Strukturabsturz zeigt den Zugriff auf `0x132`, die übergebene Titellänge. Die aktuelle Proxy-Diagnose lieferte keinen festen Ablehnungsgrund. Ein temporärer `OutputDebugStringA`-Aufruf wurde aus dem Quellcode entfernt. Diese Artefakte sind keine Erfolgsmeldung für Wiedergabe.

Die nativen Screenshots liegen nur unter `.local`. Root-Ansicht und Plex-Anmeldedialoge wurden visuell angesehen. Die vollständigen Bibliotheksansichten existieren noch nicht. Das manuelle Token-Skript `scripts/Set-PlexTestAccess.ps1` und der manuelle Testdialog sind noch zu bereinigen, sobald der reguläre Ablauf sie ersetzt.

## Freigaben und Grenzen

Die Installation der Build Tools und Tests in einer getrennten Winamp-Kopie sind freigegeben. Das normale Winamp, Plex-Konfiguration und Routereinstellungen nicht verändern. Kein öffentliches Repository, kein Push und keine automatische Installation in die vorhandene Winamp-Installation. Plexa ist die Verhaltensreferenz; mangels nachgewiesener Lizenz wird dessen Quellcode nicht übernommen. Persönliche Daten und Binärdateien bleiben außerhalb des Repositorys.
