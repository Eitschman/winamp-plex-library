# Winamp-Schnittstellen

## Herkunft und unveränderte Header

Verwendet wird [SDK 5.55](https://download.nullsoft.com/winamp/plugin-dev/WA5.55_SDK.exe). CMake prüft die Header vor dem Build:

| Datei | SHA-256 |
| --- | --- |
| `gen_ml/ml.h` | `afc20588bfcf645c59758fef1ef28b80c55ff9ecc2837e7970f74552998140da` |
| `Winamp/wa_ipc.h` | `38fef5b50b4e028b5f77c0645a0e2a956607d8468c2ecbba3988d700286e60f8` |
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

Nach `IPC_SETPLAYLISTPOS` wird wie im zugehörigen SDK-Kommentar `WM_COMMAND` mit `WINAMP_BUTTON2` gesendet. Die Konstante stammt aus dem unveränderten SDK-Header `gen_tray/WINAMPCMD.H`. `IPC_STARTPLAY` ist für diesen Ablauf ungeeignet: In der Zielversion setzt es die Position auf 0 zurück. MP3-Wiedergabe, Seeking, Länge und Plex-Titel wurden mit dieser Kombination in der Zielversion geprüft. Die bestehende Liste wird nicht gelöscht.

`IPC_GETLISTLENGTH` prüft den tatsächlichen Zuwachs. `IPC_GETOUTPUTTIME` dient zur Laufzeit- und Seek-Prüfung. `IPC_JUMPTOTIME` wird für die manuelle Gegenprüfung verwendet. Diese Aufrufe werden nur gegen das konkret geprüfte Testfenster ausgeführt.

Der SDK-Kommentar zu `IPC_WRITEPLAYLIST` beschreibt die Speicherung in `winamp.m3u` und auf Unicode-Systemen zusätzlich `winamp.m3u8`. Somit müssen bereits die übergebenen URLs tokenfrei sein. Ein nachträgliches Redigieren von Logs löst dieses Problem nicht.

## Skin-Anbindung

Die geprüfte SDK-Erweiterung `gen_ml/ml_ipc_0313.h` enthält `MLSKINWINDOW`, `ML_IPC_SKINWINDOW` und `MLSKINNEDPOPUP`. Ihre SHA-256 ist `3a1cb197ba4e32ea856af2e3d83b74c12924371863eb38286829551ed0fbfb96`. Dialog, Tabellen und Steuerelemente werden mit `SWS_USESKINCOLORS`, `SWS_USESKINFONT` und `SWS_USESKINCURSORS` registriert. Die Registrierung erfolgt zuerst am Dialog, damit die erforderliche Nachrichtenreflexion vorhanden ist. Laut Header entfernt Winamp die Skin-Unterklasse bei `WM_NCDESTROY` selbst. Kontextmenüs werden über `ML_IPC_TRACKSKINNEDPOPUPEX` angezeigt.

Die getrennte Zielinstallation enthält für die Prüfung auch `gen_ff.dll`, dessen `freeform`-Komponenten und die vorhandenen Skins. Classic, Winamp Modern, Bento und Big Bento wurden tatsächlich geladen und die Ansicht visuell geprüft. Eine Prüfung dieser vier Skins ist keine Aussage über beliebige Drittanbieter-Skins oder alle Schriftgrößen.

## Lifecycle-Grenzen

Beim Beenden werden eigene Ansichten zerstört, Hintergrundaufträge abgebrochen, deren Threads beendet und Navigationsknoten entfernt. Es gibt keine Arbeit in `DllMain`, keine globale Winamp-Fensterunterklasse und keinen unbegrenzt weiterlaufenden Dienst. Netzwerk-Callbacks müssen vor dem Entladen der DLL beendet sein.

Der Testhost deckt 100 wiederholte Ladezyklen und fehlgeschlagene Knotenregistrierung ab. Er ersetzt nicht die tatsächliche Winamp-Prüfung von Laden, Plugin-Verwaltung, Ansichtswechseln und Beenden.
