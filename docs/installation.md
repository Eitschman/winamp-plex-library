# Installation

Target: Winamp 5.9.2 Build 10042, 32-bit, on Windows 10/11. WACUP and other Winamp versions have not been validated at runtime.

1. Install the current [Microsoft Visual C++ v14 Runtime for x86](https://aka.ms/vc14/vc_redist.x86.exe) if needed.
2. Find the Plugins directory actually used by this Winamp installation. It must contain `gen_ml.dll`. The `Plugins/ml` data directory is not the destination.
3. Exit Winamp. Preserve an existing `ml_plex.dll` before replacing it manually.
4. Copy only `ml_plex.dll` next to `gen_ml.dll`. Do not install SDK files, test executables or additional decoders.
5. Start Winamp and open **Media Library → Plex → Settings → Sign in with Plex**.
6. Approve access in the Plex browser page, select a server and music library, and save.

The plugin follows Winamp's active language pack. Additional connection options are under **Advanced**. The server must be reachable over HTTPS with a valid certificate. Automatic selection checks advertised local connections, direct remote access and Plex Relay, in that order.

MP3 and M4A use the local streaming proxy. Active responses are progressively buffered in private temporary files. FLAC, OGG and ADTS-AAC selections enter the playlist as protected deferred aliases without downloading the entire selection. The requested file is prepared before its decoder opens; at most one predicted successor is prepared ahead. Unused restored targets are released and downloaded again when revisited. Limits are 536.8 MB per file and 2147.4 MB overall, shared with response buffers. AAC is repackaged into M4A without re-encoding and requires Windows Media Foundation. Windows N editions may need additional media components. There is no automatic transcoding.

Access settings are encrypted for the current Windows user at `%LOCALAPPDATA%/WinampPlexLibrary/connection.dpapi`. Winamp playlists contain no Plex tokens. Keep `playlist-references.dpapi` beside that file when retaining saved playlists: registered tracks restore on demand after a restart with current authentication to the selected server. Only the requested stale track must resolve before playback resumes. Native shuffle is preserved, and local files and web radios keep their original addresses and playback behavior. Entries created before reference recording must be added again from the Plex library once. Playlists are not portable to other users or PCs and need an online server. Local audio preparation still uses bounded temporary storage.

Open **Settings → Diagnostics** to inspect cumulative received/sent MB, active downloads, reserved storage and next-track prefetch. Values use decimal MB, truncated to one decimal place; the displayed shared budget is 2147.4 MB and the streaming-prefetch budget is 67.1 MB. A completed download may remain buffered while playback continues. See [Architecture](architecture.md#storage-units-and-limits) for exact byte limits.

To uninstall, exit Winamp and remove only the installed `ml_plex.dll`. The plugin's DPAPI file may be removed separately if desired. Do not delete Winamp databases or Plex files.

Source, build instructions, limitations and issue reporting: https://github.com/Eitschman/winamp-plex-library
