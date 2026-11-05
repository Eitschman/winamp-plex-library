# Installation

Zielversion: Winamp 5.9.2 Build 10042, 32 Bit, unter Windows 10/11. WACUP und andere Winamp-Versionen sind nicht zur Laufzeit nachgewiesen.

1. Die aktuelle Microsoft Visual C++ v14 Runtime für x86 installieren, falls sie fehlt: https://aka.ms/vc14/vc_redist.x86.exe
2. Das tatsächlich verwendete Plugins-Verzeichnis dieser Winamp-Installation ermitteln. Dort muss `gen_ml.dll` liegen. Das Datenverzeichnis `Plugins/ml` ist nicht das Ziel.
3. Winamp beenden. Eine vorhandene `ml_plex.dll` vor dem manuellen Ersetzen separat sichern.
4. Ausschließlich `ml_plex.dll` neben `gen_ml.dll` kopieren. Keine SDK-Dateien, Testprogramme oder fremden Decoder installieren.
5. Winamp starten, Medienbibliothek öffnen und unter Plex → Einstellungen auf „Mit Plex anmelden“ klicken.
6. Den Zugriff im Browser bei Plex bestätigen, Server und Musikbibliothek wählen und speichern.

Die Sprache folgt dem aktiven Winamp-Sprachpaket. Zusätzliche Verbindungsoptionen stehen unter „Erweitert“. Der Plex-Server muss über HTTPS mit gültigem Zertifikat erreichbar sein; die automatische Auswahl prüft angebotene lokale Verbindungen, direkten Fernzugriff und Plex Relay.

MP3 und M4A werden über den lokalen Proxy abgespielt. FLAC, OGG und ADTS-AAC werden vor dem Hinzufügen im Hintergrund als private Dateien vorbereitet. Dabei gelten 512 MiB je Datei und 2 GiB insgesamt. AAC wird ohne Neucodierung in M4A verpackt und benötigt Windows Media Foundation; Windows-N-Ausgaben können zusätzliche Medienkomponenten benötigen. Es gibt kein automatisches Transcoding.

Zugriffsdaten liegen benutzergebunden verschlüsselt unter `%LOCALAPPDATA%/WinampPlexLibrary/connection.dpapi`. Winamp-Playlists enthalten keine Plex-Tokens. Gespeicherte lokale Wiedergabe-URLs sind nach Plugin-Neustart nicht wiederverwendbar; Titel erneut aus der Plex-Bibliothek hinzufügen.

Zur Deinstallation Winamp beenden und die selbst installierte `ml_plex.dll` entfernen. Die eigene DPAPI-Datei ist bei Bedarf separat löschbar. Keine Winamp-Datenbank oder Plex-Dateien entfernen.

Quellcode, Build-Anleitung, bekannte Grenzen und Fehlerberichte: https://github.com/Eitschman/winamp-plex-library
