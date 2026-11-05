# Beiträge

Zielplattform ist Windows 10/11 mit Winamp 5.9.2 Build 10042, x86. Technische Bezeichner sind Englisch, Dokumentation und fachliche Kommentare Deutsch. Bitte Änderungen auf ein nachvollziehbares Problem begrenzen und vorhandene Kommentare erhalten.

## Bauen und prüfen

Die unveränderten SDK-Header gemäß [Build-Anleitung](docs/build.md) beschaffen. CMake mit Visual Studio 2022 und `-A Win32` konfigurieren, Debug und Release bauen und jeweils CTest ausführen. GitHub Actions führt diese Prüfungen mit synthetischen Daten aus. Ein grüner CI-Lauf ersetzt keine Prüfung in Winamp und an einem echten Plex Media Server. Manuelle Nachweise und offene Fälle stehen in [docs/testing.md](docs/testing.md).

Änderungen an sichtbaren Steuerelementen in Classic und einem Modern-Skin tatsächlich ansehen. Änderungen an Netzwerk, Playback oder Lifecycle brauchen passende Regressionstests. Native Tests ausschließlich in einer getrennten Winamp-Kopie ausführen und danach stoppen; die Skripte sind keine Freigabe zum Ändern einer normalen Installation.

## Pull Requests und Fehlerberichte

Problem, geändertes Verhalten und ausgeführte Prüfungen knapp beschreiben. Nicht geprüfte Varianten ausdrücklich nennen. Bei Fehlern Winamp-Version, Windows-Version, Skin, Format und reproduzierbare Schritte angeben. Keine Tokens, privaten Serveradressen, DPAPI-Dateien, privaten API-Antworten oder Bildschirmfotos mit persönlichen Inhalten hochladen.

SDK-Header und Winamp-Binärdateien bleiben lokal. Keine Abhängigkeit ohne Prüfung ihrer Lizenz übernehmen. Für Projektcode gilt [MIT](LICENSE), für Fremdkomponenten [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
