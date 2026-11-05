# Winamp-Schnittstellen

## Sprache des Hosts

Die Oberfläche übernimmt die aktive Winamp-Sprache über `IPC_GETLANGUAGEPACKINSTANCE` und `MAKEWPARAM(5, LANG_LANG_CODE)`. Der unveränderte SDK-5.55-Header `Winamp/wa_ipc.h`, Zeilen 1648 bis 1665, dokumentiert diesen Vertrag seit Winamp 5.51. Die Zielversion liefert damit `de` beziehungsweise `en`. Deutsch verwendet die deutschen Plugin-Texte; andere Sprachcodes verwenden die englischen Texte. Fehlt die Schnittstelle, schlägt die Initialisierung ausdrücklich fehl. Eine Windows-Sprache oder gespeicherte Plugin-Sprachauswahl wird nicht ersatzweise verwendet. Das SDK-Testprogramm prüft Deutsch, Englisch und eine fehlende Hostantwort; die echte Testkopie wurde mit englischem Winamp und aktiviertem deutschem Sprachpaket geprüft.

## Herkunft und unveränderte Header

Verwendet wird [SDK 5.55](https://download.nullsoft.com/winamp/plugin-dev/WA5.55_SDK.exe). CMake prüft die Header vor dem Build:

| Datei | SHA-256 |
| --- | --- |
| `gen_ml/ml.h` | `afc20588bfcf645c59758fef1ef28b80c55ff9ecc2837e7970f74552998140da` |
| `Winamp/wa_ipc.h` | `38fef5b50b4e028b5f77c0645a0e2a956607d8468c2ecbba3988d700286e60f8` |
| `Winamp/ipc_pe.h` | `af3abc9f930e1d17bce4e08ac35f848eea0345519d184ee4ea844ddd1660405e` |
| `gen_tray/WINAMPCMD.H` | `a6c932f30f418a1bb890eb6e6b1f13b538f2f6aad3632eb24d272e6c175697c6` |

Es werden weder Konstanten noch ABI-Strukturen lokal rekonstruiert. Das CMake-Projekt verweigert x64 und andere Windows-Compiler als MSVC.

## Plugin-ABI

`winampGetMediaLibraryPlugin` ist ein ungeschmückter C-Export mit C-Aufrufkonvention. Eine `.def`-Datei sichert den Exportnamen. Der Rückgabewert zeigt auf eine während der gesamten DLL-Lebensdauer gültige `winampMediaLibraryPlugin`-Instanz.

SDK 5.55 verwendet `MLHDR_VER == 0x15`, eine ANSI-Beschreibung und die Felder `version`, `description`, `init`, `quit`, `MessageProc`, `hwndWinampParent`, `hwndLibraryParent`, `hDllInstance`. Der x86-Vertrag wird mit `static_assert` auf 32 Bytes und den HWND-Offset 20 geprüft. Die Thread- und Pointerbreite darf nicht durch Pack-Pragmas oder einen x64-Build verändert werden.

`init` liefert bei Erfolg 0. Bei einem Fehler darf Winamp `quit` auslassen. Deshalb muss jeder fehlgeschlagene Initialisierungspfad seine Ressourcen selbst freigeben. Keine Ausnahme verlässt Export, Callback, Dialogprozedur oder Thread-Einstieg.

## Navigation und Ansichten

- `ML_IPC_TREEITEM_ADDW`: `MLTREEITEMW` mit `size`, `parentId`, Unicode-Titel und initialer `id == 0`. Die vom Host zugewiesene `id` wird gespeichert.
- `ML_MSG_TREE_ONCREATEVIEW`: `param1` ist die Knoten-ID, `param2` das Elternfenster. Nur die eigene ID darf akzeptiert werden. Der Rückgabewert ist das HWND des Kinddialogs.
- `ML_IPC_TREEITEM_GETHANDLE` löst eine ID in das zugehörige Handle auf. `ML_IPC_TREEITEM_DELETE` erhält dieses Handle, keine ungeprüfte ID.
- Winamp kann eine Ansicht beim Navigieren zerstören. Wiedergaberessourcen gehören deshalb dem Plugin-Core und nicht einem kurzlebigen Ansichtsfenster.

Native Dialoge werden mit `WS_CHILD`, `DS_CONTROL` und `WS_EX_CONTROLPARENT` erstellt. Steuerelemente verwenden Unicode. Die UI arbeitet auf dem Hostthread. Netzwerkcallbacks bearbeiten keine Fenster oder Winamp-Playlist.

## Playlist und Metadaten

SDK 5.55 beschreibt für `IPC_ENQUEUEFILEW` die Struktur `enqueueFileWithMetaStructW`: URL, Unicode-Titel und Länge in **Sekunden**, unbekannte Länge `-1`. Diese Strukturübergabe verursacht in der konkreten Zielversion einen im Debugger reproduzierten Absturz innerhalb von Winamp. Im [offiziellen Fehlerbericht, Beitrag 141](https://forums.winamp.com/forum/winamp/winamp-bug-reports/316139-post-all-winamp-5-9-2-bug-reports-in-this-thread/page10) ist die Regression ebenfalls gemeldet. Der alte Header ist damit kein ausreichender Kompatibilitätsnachweis für diesen Aufruf.

Das Plugin übergibt eine temporäre Unicode-M3U8-Datei mit `WM_COPYDATA` und `IPC_ENQUEUEFILEW` als `dwData`. Dieser SDK-Pfad übernimmt die Einträge einschließlich Länge; nach dem synchronen Import wird nur diese selbst erzeugte temporäre Datei entfernt. `ML_IPC_HOOKTITLEW` verwendet die SDK-Struktur `waHookTitleStructW` und beantwortet ausschließlich eigene lokale URLs mit dem zugehörigen Plex-Titel. Fremde URLs bleiben unverändert.

**Nach aktuellem Titel einfügen** verwendet den originalen Playlisteditor-Vertrag aus `Winamp/ipc_pe.h`: `IPC_GETWND` mit `IPC_GETWND_PE` liefert das Editorfenster desselben Prozesses. Dorthin geht `WM_COPYDATA` mit `IPC_PE_INSERTFILENAMEW` und `fileinfoW`. Diese Struktur enthält `file[MAX_PATH]` und `index` und besitzt unter x86 524 Bytes. Der absolute temporäre M3U8-Pfad muss kürzer als `MAX_PATH` sein. Der Editor importiert dessen Einträge ab dem nullbasierten Index hinter dem aktuellen Titel, bei leerer Liste ab Index null. Die UTF-8-BOM der Datei ist für Unicode-Titel erforderlich. Die gemeinsame Übergabefunktion prüft den tatsächlichen Zuwachs. Fehlende Editorfenster, ungültige Positionen, zu lange Pfade und unvollständige Importe ergeben einen Fehler.

Die Aktion verändert weder den aktuellen Titel noch Wiedergabe, Pause oder Zufallsmodus. Sie bestimmt die physische Reihenfolge der Playlist; eine Zufallswiedergabe oder fremde Warteschlangen können weiterhin eine andere Abspielfolge wählen. `IPC_GET_NEXT_PLITEM` wird nicht zur vermeintlichen Abfrage benutzt: Diese Benachrichtigung kann von Warteschlangen-Plugins verarbeitet werden. Eine transaktionale Rücknahme eines unvollständigen Hostimports ist nicht implementiert.

Nach `IPC_SETPLAYLISTPOS` wird wie im zugehörigen SDK-Kommentar `WM_COMMAND` mit `WINAMP_BUTTON2` gesendet. Die Konstante stammt aus dem unveränderten SDK-Header `gen_tray/WINAMPCMD.H`. `IPC_STARTPLAY` ist für diesen Ablauf ungeeignet: In der Zielversion setzt es die Position auf 0 zurück. MP3-Wiedergabe, Seeking, Länge und Plex-Titel wurden mit dieser Kombination in der Zielversion geprüft. Die bestehende Liste wird nicht gelöscht.

`IPC_GETLISTLENGTH` prüft den tatsächlichen Zuwachs. `IPC_GETOUTPUTTIME` dient zur Laufzeit- und Seek-Prüfung. `IPC_JUMPTOTIME` wird für die manuelle Gegenprüfung verwendet. Diese Aufrufe werden nur gegen das konkret geprüfte Testfenster ausgeführt.

Der SDK-Kommentar zu `IPC_WRITEPLAYLIST` beschreibt die Speicherung in `winamp.m3u` und auf Unicode-Systemen zusätzlich `winamp.m3u8`. Somit müssen bereits die übergebenen URLs tokenfrei sein. Ein nachträgliches Redigieren von Logs löst dieses Problem nicht.

## Skin-Anbindung

Der unveränderte [Skin-Schnittstellenheader von 2016](https://getwacup.com/sdk/gen_ml/ml_ipc_0313.h) enthält `MLSKINWINDOW`, `ML_IPC_SKINWINDOW` und `MLSKINNEDPOPUP`. Seine SHA-256 ist `922e6d21b0b8f2e9fe536456c87e51e05bba82b579ec6eaa77264cbdb25c4e42`. Er liegt getrennt unter `.local/skin-sdk`; der Build verwendet ihn zusammen mit den vier oben genannten SDK-5.55-Headern. Das ist keine Zusage zur WACUP-Laufzeitkompatibilität.

`MLSKINNEDPOPUP` muss im x86-Build 48 Bytes besitzen, einschließlich `customProc` und `customParam`. Ein `static_assert` sichert diese Größe. Die 40-Byte-Fassung im SDK-5.55-Archiv öffnet in der Zielversion kein Menü. Der 48-Byte-Vertrag wurde mit geöffnetem, skinned Kontextmenü und tatsächlicher Albumübergabe geprüft. Es werden keine zusätzlichen Felder lokal nachgebildet.

Dialog, Tabellen und Steuerelemente werden mit `SWS_USESKINCOLORS`, `SWS_USESKINFONT` und `SWS_USESKINCURSORS` registriert. Die Registrierung erfolgt zuerst am Dialog, damit die erforderliche Nachrichtenreflexion vorhanden ist. Laut Header entfernt Winamp die Skin-Unterklasse bei `WM_NCDESTROY` selbst. Kontextmenüs werden über `ML_IPC_TRACKSKINNEDPOPUPEX` angezeigt.

Die getrennte Zielinstallation enthält für die Prüfung auch `gen_ff.dll`, dessen `freeform`-Komponenten und die vorhandenen Skins. Classic, Winamp Modern, Bento und Big Bento wurden tatsächlich geladen und die Ansicht visuell geprüft. Eine Prüfung dieser vier Skins ist keine Aussage über beliebige Drittanbieter-Skins oder alle Schriftgrößen.

## Album-Art-Dienst

Die Album-Art-Anbindung verwendet `svc_albumArtProvider`, `waServiceFactory`, `api_service` und `api_memmgr` aus dem SDK-5.55-Archiv. Die zusätzlich verwendeten Originalheader und ihre Prüfsummen stehen in `cmake/WinampArtworkSdk.cmake`. `IPC_GET_API_SERVICE` liefert den Hostdienst; die Registrierung wird anschließend über dessen GUID-Abfrage geprüft. Daten- und Dateityppuffer stammen aus Winamps Speicherverwaltung. Der Callback liefert nur vorbereitete PNG-Bilder mit Typkennung `png`, führt keine Netzwerkanfrage aus und beantwortet Schreibversuche mit `ALBUMARTPROVIDER_READONLY`. Die Kategorie `ALBUMARTPROVIDER_TYPE_EMBEDDED` ist für HTTP-Titel in der Zielversion erforderlich und durch Gegenprobe zur nicht abgefragten Kategorie `DATABASE` belegt. Es werden keine Bilddaten in Mediendateien geschrieben.

Der private SDK-Typname `int8_t` kollidiert mit dem C++17-Standardheader: MSVC behandelt das alte `__int8` als `char`, der Standardtyp ist `signed char`. `wasabi_sdk.h` trennt ausschließlich diesen SDK-Typnamen per lokal begrenztem Präprozessormakro. Keine Headerdatei oder Dispatch-Signatur wird verändert. Der MSVC-Build und die Dispatch-Tests prüfen die gemeinsame Verwendung.

## Lifecycle-Grenzen

Beim Beenden werden eigene Ansichten zerstört, Hintergrundaufträge abgebrochen, deren Threads beendet und Navigationsknoten entfernt. Es gibt keine Arbeit in `DllMain`, keine globale Winamp-Fensterunterklasse und keinen unbegrenzt weiterlaufenden Dienst. Netzwerk-Callbacks müssen vor dem Entladen der DLL beendet sein.

Der Testhost deckt 100 wiederholte Ladezyklen und fehlgeschlagene Knotenregistrierung ab. Er ersetzt nicht die tatsächliche Winamp-Prüfung von Laden, Plugin-Verwaltung, Ansichtswechseln und Beenden.
