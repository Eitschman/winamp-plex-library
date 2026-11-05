# Sicherheit und Veröffentlichung

Zugangsdaten gehören weder in Issues noch in Screenshots, Playlists oder Diagnoseberichte. Bitte einen möglichen Geheimnisfund zunächst ohne den Wert melden. Bereits öffentlich zugängliche Zugangsdaten müssen beim jeweiligen Anbieter widerrufen werden; das Entfernen aus dem aktuellen Quelltext allein genügt nicht.

Das Plugin speichert den Plex-Zugriff benutzergebunden mit Windows DPAPI. Wiedergabe-URLs enthalten ausschließlich zufällige lokale Berechtigungen. HTTP-Header mit Plex-Tokens werden nicht protokolliert. Diagnoseanzeigen verwenden feste Fehlercodes und numerische Zähler. Lokale Prüfberichte, SDK-Dateien, Konfigurationen und Buildordner sind vom Repository ausgeschlossen.

## Geprüfter Umfang

Am 05.11.2026 wurden mit Gitleaks 8.30.1 die gesamte vorhandene Git-Historie mit 57 Commits, der veröffentlichte Quellbaum, die anstehenden Änderungen und die Debug-/Release-Logs des Actions-Laufs 42061725581 geprüft. Die Scans melden keine Funde. Eine künstliche, nicht ausgegebene GitHub-Tokenprobe wird erkannt; eine gültige Vergleichsdatei bleibt ohne Fund. Zusätzlich wurden historische Dateien auf die bekannten privaten Serveradressen und individuelle Plex-Verbindungshosts geprüft, ohne Treffer.

Das veröffentlichte CI-Buildpaket enthält ausschließlich DLL, Installationshinweise und Lizenztexte. Dessen Texte sowie aus der DLL extrahierte ANSI-/UTF-16-Zeichenfolgen wurden ebenfalls ohne Fund geprüft. Die Konfigurationsimplementierung schreibt ausschließlich das DPAPI-Ergebnis auf den Datenträger; ihre JSON-Darstellung bleibt intern im Speicher.

Ein Scanner erkennt nicht jede denkbare vertrauliche Information. Diese Ergebnisse sind keine absolute Zusicherung, dass beliebige Geheimnisformate ausgeschlossen sind. Die GitHub-Prüfung ergänzt die Prüfung vor jedem Push: Der CI-Workflow lädt einen auf SHA-256 festgelegten Scanner und prüft die gesamte Historie mit redigierter Ausgabe. Ein Fund lässt die Prüfung scheitern. Es werden keine Fundberichte als Artefakte veröffentlicht.
