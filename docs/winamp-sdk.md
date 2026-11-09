# Winamp interfaces

## Host language

The Plex track-information panel follows the host language and uses the existing skinned child-dialog frame. Its borderless native read-only EDIT fields inherit the dialog's skin font and use the adjacent skinned STATIC's `WM_CTLCOLORSTATIC` colors. Their compact height and alignment are defined in the dialog resource. Text selection remains native. The cover's owner-drawn STATIC uses the same skin-provided brush and text color; `AlphaBlend` preserves transparency without a fixed background. No connection or decoder state is passed to this panel.

The original command header defines `WINAMP_EDIT_ID3` (40188). The pinned IPC header defines `IPC_INFOBOX` (293), `IPC_INFOBOXW` (1293), their parameter layouts and OK/Cancel results. The playlist editor's `ID_PE_ID3` value (40208) was checked against [Winamp core resource definitions](https://www.unix.dog/~agris/winamp/Src/Winamp/resource.h), with selection enumeration from the original `ipc_pe.h`. The factual command value is retained locally; no implementation from that mirror is copied or bundled. [FileInfo.cpp](https://www.unix.dog/~agris/winamp/Src/Winamp/FileInfo.cpp) explains the unified information dialog's decoder-specific stream queries. Native target-version tests cover both command entry points; other hosts, including WACUP, remain unverified.

The single-line search EDIT uses the verified Skin SDK `SWES_VCENTER` style through `MLSkinWindow`. The skin engine handles text/caret alignment and colors; the field keeps its ordinary row height and native editing behavior. No multiline control or custom text renderer is used.

The UI inherits the active Winamp language through `IPC_GETLANGUAGEPACKINSTANCE` with `MAKEWPARAM(5, LANG_LANG_CODE)`. Original SDK 5.55 `Winamp/wa_ipc.h`, lines 1648 through 1665, documents this contract since Winamp 5.51. The target returns `de` or `en`; German selects German text, and other supported host-language responses use English plugin text. A missing interface fails initialization explicitly. Windows locale and a saved plugin language are not substituted. The SDK host covers German, English and missing responses; the isolated real host was tested in English and with its German language pack.

## Original headers and provenance

The build uses [SDK 5.55](https://download.nullsoft.com/winamp/plugin-dev/WA5.55_SDK.exe). CMake verifies these files:

| File | SHA-256 |
| --- | --- |
| `gen_ml/ml.h` | `afc20588bfcf645c59758fef1ef28b80c55ff9ecc2837e7970f74552998140da` |
| `Winamp/wa_ipc.h` | `38fef5b50b4e028b5f77c0645a0e2a956607d8468c2ecbba3988d700286e60f8` |
| `Winamp/ipc_pe.h` | `af3abc9f930e1d17bce4e08ac35f848eea0345519d184ee4ea844ddd1660405e` |
| `gen_tray/WINAMPCMD.H` | `a6c932f30f418a1bb890eb6e6b1f13b538f2f6aad3632eb24d272e6c175697c6` |

Constants and ABI structures are not reconstructed locally. CMake rejects x64 and Windows compilers other than MSVC.

The plugin additionally uses the unchanged [Nullsoft IPC header published in 2013](https://getwacup.com/sdk/Winamp/wa_ipc.h), downloaded to `.local/playback-sdk/Winamp/wa_ipc.h` and checked by CMake against SHA-256 `cb86f3409cfdd4c578c9ab8cf4b5d37339c2a4ab052f5490f46eb899574171b8`. It supplies `IPC_GETNEXTLISTPOS` (Winamp 5.61+) and `IPC_GET_STOP_AFTER_CURRENT` (5.58+, inverted result). The original Media Library and playlist-editor structures still come from SDK 5.55. No SDK header is redistributed.

## Plugin ABI

`winampGetMediaLibraryPlugin` is an undecorated C export using the C calling convention; a module-definition file fixes its name. Its returned `winampMediaLibraryPlugin` remains valid for the DLL lifetime.

SDK 5.55 uses `MLHDR_VER == 0x15`, an ANSI description, and fields `version`, `description`, `init`, `quit`, `MessageProc`, `hwndWinampParent`, `hwndLibraryParent`, `hDllInstance`. Static assertions enforce the x86 size of 32 bytes and HWND offset of 20. Packing or pointer width must not change this contract.

`init` returns zero on success. Winamp may omit `quit` after initialization failure, so failure paths clean up their own resources. Exceptions never cross exports, callbacks, dialog procedures or thread entry points.

## Navigation and views

- `ML_IPC_TREEITEM_ADDW` takes `MLTREEITEMW` with size, parent ID, Unicode title and initial ID zero. Save the host-assigned ID.
- `ML_MSG_TREE_ONCREATEVIEW` supplies the node ID in `param1` and parent window in `param2`. Accept only owned IDs and return the child dialog HWND.
- `ML_IPC_TREEITEM_GETHANDLE` resolves an ID; `ML_IPC_TREEITEM_DELETE` takes that handle, not an unchecked ID.
- Views may be destroyed during navigation. Playback belongs to the plugin core rather than a short-lived view.

Native Unicode dialogs use `WS_CHILD`, `DS_CONTROL` and `WS_EX_CONTROLPARENT`. Only the host UI thread modifies controls and playlists. Network callbacks do neither.

## Playlist and metadata

SDK 5.55 documents `enqueueFileWithMetaStructW` for `IPC_ENQUEUEFILEW`: URL, Unicode title and duration in **seconds**, or -1 if unknown. Passing this structure causes a debugger-reproduced crash in the target host. The regression is also reported in [official bug-thread post 141](https://forums.winamp.com/forum/winamp/winamp-bug-reports/316139-post-all-winamp-5-9-2-bug-reports-in-this-thread/page10). Header availability alone does not establish runtime compatibility.

The plugin passes a temporary Unicode M3U8 using `WM_COPYDATA` with `IPC_ENQUEUEFILEW` as `dwData`. This documented path imports titles and durations; the plugin removes only its own temporary file after synchronous import. `ML_IPC_HOOKTITLEW` uses original `waHookTitleStructW` and responds only for owned playback paths.

**Insert after current track** uses original `Winamp/ipc_pe.h`: `IPC_GETWND` with `IPC_GETWND_PE` locates the same-process playlist editor, which receives `WM_COPYDATA` with `IPC_PE_INSERTFILENAMEW` and `fileinfoW`. This structure contains `file[MAX_PATH]` and `index` and is 524 bytes on x86. The absolute temporary M3U8 path must be shorter than MAX_PATH. Entries are imported at the zero-based position after the current item, or zero for an empty playlist. A UTF-8 BOM is required for Unicode titles. Actual list growth is checked. Missing editor windows, invalid positions, excessive paths and partial imports are errors.

Insertion preserves the current item, playback, pause and shuffle state. It controls physical order; shuffle or queue plugins may choose another playback sequence. `IPC_GET_NEXT_PLITEM` is not used as a query because queue plugins may handle that notification. Partial host imports do not have transactional rollback.

After `IPC_SETPLAYLISTPOS`, the plugin sends `WM_COMMAND` with original `WINAMP_BUTTON2` from `gen_tray/WINAMPCMD.H`, as documented by the SDK. `IPC_STARTPLAY` resets position to zero in this target and is unsuitable. Real MP3 playback, seeking, duration and title were checked using the selected combination. Existing playlist content is preserved.

`IPC_GETLISTLENGTH` checks actual import growth. `IPC_GETOUTPUTTIME` measures playback and seeking; `IPC_JUMPTOTIME` performs explicit seek checks. Tests address only the verified isolated window.

Restored-file prefetch uses documented `IPC_GETNEXTLISTPOS` on the UI thread, including native shuffle. The streaming byte buffer retains its existing sequential selection. Queue overrides are queried only for an actual navigation/EOF action, once, and that answer is retained through deferred playback. Prefetch never consumes queue notifications. Native stopped Previous/Next selects without opening a decoder; `IPC_CHANGECURRENTFILEW` replaces a resolved filename in place without rebuilding the shuffle table. `scripts/Test-PlexNativeSelection.ps1` checks these contracts against the isolated 5.9.2 host with generated PCM silence. Third-party queue plugins are not verified.

The `IPC_WRITEPLAYLIST` SDK comment documents persistence to `winamp.m3u` and additionally `winamp.m3u8` on Unicode systems. Playback URLs must therefore already be token-free; log redaction after handoff is insufficient.

## Skin integration

The unchanged [2016 skin interface header](https://getwacup.com/sdk/gen_ml/ml_ipc_0313.h) supplies `MLSKINWINDOW`, `ML_IPC_SKINWINDOW` and `MLSKINNEDPOPUP`. SHA-256: `922e6d21b0b8f2e9fe536456c87e51e05bba82b579ec6eaa77264cbdb25c4e42`. It resides separately in `.local/skin-sdk`. Using this header does not establish WACUP runtime compatibility.

The x86 popup structure must be 48 bytes, including `customProc` and `customParam`, enforced by static assertion. The SDK 5.55 40-byte variant does not open a menu in the target. An actual skinned menu and album handoff verified the 48-byte contract without locally inventing fields.

Dialogs and controls register `SWS_USESKINCOLORS`, `SWS_USESKINFONT` and `SWS_USESKINCURSORS`. Register the dialog first for message reflection. The header documents host removal of subclasses at `WM_NCDESTROY`. Context menus use `ML_IPC_TRACKSKINNEDPOPUPEX`.

The test copy includes `gen_ff.dll`, freeform components and installed skins. Classic, Winamp Modern, Bento and Big Bento were actually loaded and inspected. This does not validate every third-party skin or font/DPI setting.

## Album-art service

Integration uses SDK 5.55 `svc_albumArtProvider`, `waServiceFactory`, `api_service` and `api_memmgr`. Additional original headers and hashes are listed in `cmake/WinampArtworkSdk.cmake`. `IPC_GET_API_SERVICE` obtains host services; registration is confirmed through GUID lookup. Image and type buffers use host memory allocation.

The callback returns prepared PNG data with type `png`, performs no networking, and rejects writes with `ALBUMARTPROVIDER_READONLY`. `ALBUMARTPROVIDER_TYPE_EMBEDDED` is required for tested HTTP tracks; a negative comparison showed that the host did not query the `DATABASE` category. No image data is written into media files.

The SDK's private `int8_t` conflicts with C++17: MSVC treats legacy `__int8` as `char`, while the standard type is `signed char`. `wasabi_sdk.h` isolates only that SDK type name with a narrowly scoped preprocessor macro. It changes neither original headers nor dispatch signatures. MSVC compilation and dispatch tests cover joint usage.

## Lifecycle boundaries

Shutdown destroys owned views, cancels work, joins threads and removes nodes. There is no work in DllMain, global Winamp window subclass or persistent external service. Network callbacks must have returned before DLL unload.

The SDK host covers 100 load cycles and node-registration failure. It does not replace real-host checks of loading, plugin management, navigation and exit.
