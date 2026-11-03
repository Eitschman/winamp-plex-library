# Winamp-Schnittstellen

## Herkunft und unveränderte Header

Verwendet wird [SDK 5.55](https://download.nullsoft.com/winamp/plugin-dev/WA5.55_SDK.exe). CMake prüft beide Header vor dem Build:

| Datei | SHA-256 |
| --- | --- |
| `gen_ml/ml.h` | `afc20588bfcf645c59758fef1ef28b80c55ff9ecc2837e7970f74552998140da` |
| `Winamp/wa_ipc.h` | `38fef5b50b4e028b5f77c0645a0e2a956607d8468c2ecbba3988d700286e60f8` |

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

`IPC_ENQUEUEFILEW` erhält `enqueueFileWithMetaStructW`: URL, Unicode-Titel und Länge in **Sekunden**, unbekannte Länge `-1`. Dieser IPC hängt an; er startet nicht automatisch. Für den Prototyp werden danach `IPC_SETPLAYLISTPOS` und `IPC_STARTPLAY` verwendet. Die bestehende Liste wird dabei nicht gelöscht.

`IPC_GETLISTLENGTH` prüft den tatsächlichen Zuwachs. `IPC_GETOUTPUTTIME` dient zur Laufzeit- und Seek-Prüfung. `IPC_JUMPTOTIME` wird für die manuelle Gegenprüfung verwendet. Diese Aufrufe werden nur gegen das konkret geprüfte Testfenster ausgeführt.

Der SDK-Kommentar zu `IPC_WRITEPLAYLIST` beschreibt die Speicherung in `winamp.m3u` und auf Unicode-Systemen zusätzlich `winamp.m3u8`. Somit müssen bereits die übergebenen URLs tokenfrei sein. Ein nachträgliches Redigieren von Logs löst dieses Problem nicht.

## Lifecycle-Grenzen

Beim Beenden werden eigene Ansichten zerstört, Hintergrundaufträge abgebrochen, deren Threads beendet und Navigationsknoten entfernt. Es gibt keine Arbeit in `DllMain`, keine globale Winamp-Fensterunterklasse und keinen unbegrenzt weiterlaufenden Dienst. Netzwerk-Callbacks müssen vor dem Entladen der DLL beendet sein.

Der Testhost deckt 100 wiederholte Ladezyklen und fehlgeschlagene Knotenregistrierung ab. Er ersetzt nicht die tatsächliche Winamp-Prüfung von Laden, Plugin-Verwaltung, Ansichtswechseln und Beenden.
