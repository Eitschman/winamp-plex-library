# Arbeitsstand und Wiedereinstieg

Die Entwicklung läuft am ursprünglichen Plugin-Auftrag einschließlich Bibliotheksansichten, Wiedergabe und Qualitätssicherung weiter. Das PRP unter `prp/prp.md` ist ein veränderbares Arbeitsdokument. Der Benutzer hat den lokalen Proxy als Architektur bestätigt.

## Nächster konkreter Schritt

Die Phase-1-Wiedergabeprüfung ist für einen echten Plex-MP3-Titel bestanden. `src/plugin/plugin.cpp` verwendet `WM_COPYDATA` mit einer temporären M3U8-Datei, `ML_IPC_HOOKTITLEW` und den im SDK dokumentierten Play-Befehl. Wiedergabe, Seeking, 306 Sekunden Länge und Plex-Titel sind nachgewiesen; die exportierte Testplaylist ist tokenfrei. Details und konkrete Messwerte stehen in `docs/testing.md`.

1. Die implementierten Bibliotheksansichten und Mehrfach-Grants weiter im regulären Benutzerablauf prüfen: Album-/Mehrfachwiedergabe, Suche, Sortierung, Paging, Kontextmenüs und Sprache. Die API-/Modellschicht, Hintergrundjobs und der begrenzte Seiten-Cache sind vorhanden.
2. Cover sind in Album-/Titelansicht und beiden Big-Bento-Playerfeldern visuell geprüft. Die Titel übernehmen `parentThumb`; der dateibezogene `EMBEDDED`-Provider liefert vorbereitete PNG-Daten mit SDK-Dateitypkennung `png`. Der letzte stumme Lauf meldet 14 erfolgreiche Abfragen für 13 Zuordnungen. `DATABASE` wird für HTTP-Titel nicht abgefragt, V5-BMP wurde trotz WIC-Erfolg nicht angezeigt. Fehlende Bilder sind synthetisch geprüft; eine reale Gegenprobe fehlt noch.
3. Einstellungen und zuverlässig nachweisbare Erweiterungen sowie die übrigen Tests des PRP bearbeiten. Weitere echte Audioformate und Fehlerzustände prüfen. Die neue Ansicht besitzt entsprechend Benutzerentscheidung keine Player-Steuerleiste. Classic, Modern, Bento und Big Bento sind über die gen_ml-Skin-Engine angebunden und visuell geprüft.
4. Den manuellen Token-Testdialog und das überholte Token-Skript durch den regulären Bibliotheksablauf ersetzen. Die Browseranmeldung bleibt der Benutzerablauf.

## Lokal vorhandene Umgebung

- Visual Studio 2022 Build Tools: `C:\BuildTools2022`, MSVC-x86 und Windows SDK installiert.
- CMake/CTest: `C:\BuildTools2022\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`.
- Debugger: `C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\cdb.exe`.
- Unveränderte SDK-5.55-Header: `.local/sdk`, zusätzlicher veröffentlichter Skin-Header von 2016 unter `.local/skin-sdk`. CMake prüft die dokumentierten SHA-256-Werte einschließlich Wasabi/Agave. Der 48-Byte-Popupvertrag ist in der Zielversion geprüft.
- Getrennte Testkopie: `.local/winamp-test`. Eigene Klasse `WinampPlexTest`, eigene Einstellungen unter `.local/winamp-test/settings`. Nur diese Kopie für Installation und Laufzeittests verwenden.
- Die normale Winamp-Installation ist nicht verändert. Vor weiteren Prozessaktionen den tatsächlichen Pfad erneut prüfen. Die Testkopie enthält auch die vorhandenen Skins und deren Modern-Engine.
- Die Browseranmeldung ist bestätigt. Der Plugin-eigene Zugriff liegt DPAPI-geschützt unter `%LOCALAPPDATA%\WinampPlexLibrary\connection.dpapi`. Ausschließlich den dokumentierten Anwendungszugriff verwenden, keine Zugangsdaten suchen oder ausgeben.
- Die derzeit in der Testkopie liegende DLL kann vom nächsten Quellbuild abweichen. Vor dem nächsten Start bei geschlossener Testkopie den aktuellen Build kopieren.

## Nachweise und Diagnoseartefakte

`docs/testing.md` enthält den fachlichen Prüfstand. Der MSVC-x86-Release-Build und sieben CTest-Suiten sind erfolgreich gelaufen. Katalog-Livetest, native Navigation, Kontextmenü und Albumwiedergabe sind geprüft. Die horizontale Tabellenposition ist korrigiert und visuell geprüft. Die Testkopie verwendet die gespeicherte DPAPI-Verbindung und wird ausschließlich regulär beendet beziehungsweise neu gestartet. Ihre Playlist wird vor Neustart geleert, damit keine abgelaufenen lokalen Links zurückbleiben. Stumme Abspieltests stoppen im `finally`-Pfad und stellen erst danach die vorherige Lautstärke wieder her.

Der aktuelle reale Erfolgsnachweis liegt in `.local/logs/playback-metadata-hook.log`. Die festen lokalen Fehlercodes sind in `ProxyStatus` verfügbar, ohne rohe Requests zu speichern. Eine Winamp-Anfrage ohne Port im Host-Header ist geprüft und wird ausschließlich für `127.0.0.1` zugelassen. Falsche explizite Ports und fremde Hosts bleiben verboten. `IPC_STARTPLAY` setzt in der Zielversion die Position auf 0; `WINAMP_BUTTON2` erhält die gesetzte Position. Alte ignorierte Debuglogs sind keine Nachweise aktueller Testläufe.

Die nativen Screenshots liegen nur unter `.local`. Root-Ansicht, Anmeldedialoge, Bibliotheksansichten in vier Skins und Albumcover wurden visuell angesehen. Das manuelle Token-Skript `scripts/Set-PlexTestAccess.ps1` und der manuelle Testdialog sind noch zu bereinigen, sobald der reguläre Ablauf sie ersetzt. Die vorhandene Testplaylist enthält nur das aktuelle Album einmal; Wiedergabe ist gestoppt. Nicht ungefragt erneut dauerhaft abspielen.

## Freigaben und Grenzen

Die Installation der Build Tools und Tests in einer getrennten Winamp-Kopie sind freigegeben. Das normale Winamp, Plex-Konfiguration und Routereinstellungen nicht verändern. Kein öffentliches Repository, kein Push und keine automatische Installation in die vorhandene Winamp-Installation. Plexa ist die Verhaltensreferenz; mangels nachgewiesener Lizenz wird dessen Quellcode nicht übernommen. Persönliche Daten und Binärdateien bleiben außerhalb des Repositorys.
