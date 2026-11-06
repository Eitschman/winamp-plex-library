# Installation

Target: Winamp 5.9.2 Build 10042, 32-bit, on Windows 10/11. WACUP and other Winamp versions have not been validated at runtime.

1. Install the current [Microsoft Visual C++ v14 Runtime for x86](https://aka.ms/vc14/vc_redist.x86.exe) if needed.
2. Find the Plugins directory actually used by this Winamp installation. It must contain `gen_ml.dll`. The `Plugins/ml` data directory is not the destination.
3. Exit Winamp. Preserve an existing `ml_plex.dll` before replacing it manually.
4. Copy only `ml_plex.dll` next to `gen_ml.dll`. Do not install SDK files, test executables or additional decoders.
5. Start Winamp and open **Media Library → Plex → Settings → Sign in with Plex**.
6. Approve access in the Plex browser page, select a server and music library, and save.

The plugin follows Winamp's active language pack. Additional connection options are under **Advanced**. The server must be reachable over HTTPS with a valid certificate. Automatic selection checks advertised local connections, direct remote access and Plex Relay, in that order.

MP3 and M4A use the local streaming proxy. Current development builds progressively buffer active responses in private temporary files; v0.1.0 does not include this buffer. FLAC, OGG and ADTS-AAC are prepared as private local files before being added to Winamp. Limits are 512 MiB per file and 2 GiB overall, shared with response buffers. AAC is repackaged into M4A without re-encoding and requires Windows Media Foundation. Windows N editions may need additional media components. There is no automatic transcoding.

Access settings are encrypted for the current Windows user at `%LOCALAPPDATA%/WinampPlexLibrary/connection.dpapi`. Winamp playlists contain no Plex tokens. Keep `playlist-references.dpapi` beside that file when retaining saved playlists: it lets the regular development DLL restore registered tracks after a restart with current authentication to the selected server. Entries created before reference recording must be added again from the Plex library once. Playlists are not portable to other users or PCs and need an online server. Local audio preparation still uses bounded temporary storage. This feature is not included in v0.1.0.

To uninstall, exit Winamp and remove only the installed `ml_plex.dll`. The plugin's DPAPI file may be removed separately if desired. Do not delete Winamp databases or Plex files.

Source, build instructions, limitations and issue reporting: https://github.com/Eitschman/winamp-plex-library
