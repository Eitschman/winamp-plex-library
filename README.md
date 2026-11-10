# Winamp Plex Library

Browse Plex music directly in the classic Winamp Media Library and play it with Winamp. A native C++17 plugin for **Windows 10/11 and Winamp 5.9.2 Build 10042, x86**. The plugin DLL is `ml_plex.dll`.

[![Windows x86](https://github.com/Eitschman/winamp-plex-library/actions/workflows/build.yml/badge.svg)](https://github.com/Eitschman/winamp-plex-library/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

## Features

- Artists, albums, tracks and Plex audio playlists in dedicated Media Library nodes.
- Related artist, album and track lists, single-click selection, sorting and multiple track selection.
- Reset search, artist/album selection and filters together with **Reset** or Escape in the search field.
- Server-side genre, album-year and audio-codec filters, named saved views, and restored positions in the unfiltered Tracks area.
- Bounded next-track prefetch for sequential MP3/M4A playback, with cache usage in Diagnostics.
- Play tracks or albums, append selections, or insert them after the current playlist entry. Winamp provides the player controls.
- Replay saved Winamp playlists after a restart using protected Plex identity references and renewed local playback targets.
- Plex browser sign-in, server and music library selection, and automatic HTTPS connection selection.
- Album artwork in library lists and through Winamp's album-art service. Tracks inherit available album covers.
- Native skin integration and German or English text following Winamp's active language pack.
- Asynchronous networking, bounded caches, Plex playback status and personal track ratings.

## Requirements

Winamp 5.9.2 Build 10042, 32-bit, with Media Library enabled (`gen_ml.dll`), Windows 10/11, a reachable Plex Media Server with a music library, and an authorized Plex account. The DLL requires the current [Microsoft Visual C++ v14 Runtime for x86](https://aka.ms/vc14/vc_redist.x86.exe). No external background service or custom audio decoder is required.

WACUP is a future compatibility target and has not been tested at runtime. Other Winamp versions are not generally supported without separate validation.

## Download

Published versions are available from [GitHub Releases](https://github.com/Eitschman/winamp-plex-library/releases). Each ZIP contains the DLL, installation instructions and license notices, with a separate SHA-256 checksum file. [GitHub Actions](https://github.com/Eitschman/winamp-plex-library/actions/workflows/build.yml) also provides development packages named **`winamp-plex-library-win32-release`** with limited retention; downloading those requires a GitHub account. You can also build from source.

Version **0.3.4.1** shows available album artwork directly beside the music data in Winamp's File Information command (Alt+3). The compact panel uses the active skin's colors and font, borderless selectable music fields and grouped year, track number, duration and format. It reuses the same prepared cover as Winamp's album-art service, with no additional network request. Missing artwork has an explicit empty state. Connection, URL, path and credential fields remain excluded; local files and web radios retain their native information dialogs. See the [release notes](docs/releases/0.3.4.1.md) for upgrade instructions and limitations.

## Installation and setup

Exit Winamp and manually copy `ml_plex.dll` into the **Plugins directory actually used by your Winamp installation**, next to `gen_ml.dll`. The `Plugins/ml` data directory is not the destination. Preserve an existing DLL before replacing it. See [installation instructions](docs/installation.md).

Open **Media Library → Plex → Settings → Sign in with Plex** and approve access in your browser. Select your server and music library, then save. Opening Settings does not start browser sign-in. Passwords are entered only on Plex's website; manually copying a token is not part of setup.

Automatic connection selection prefers a reachable local HTTPS connection, then direct remote access, then Plex Relay. It runs in the background for the first library request after plugin startup and in Settings. Certificates and server identity are checked before authenticated library access. Authentication failures do not trigger searches for other tokens or servers.

**Advanced** provides a fixed connection and a custom HTTPS base address. Changes must pass connection validation before saving; **Automatic** removes the fixed selection. Language follows Winamp's active language pack: German and English are included, and other host languages use English plugin text. There is no separate language selector. Metadata and artwork caches can be disabled or cleared. Diagnostics contain fixed codes and numeric counters.

The entire plugin configuration is encrypted with Windows DPAPI for the current user and stored at `%LOCALAPPDATA%/WinampPlexLibrary/connection.dpapi`. Corrupt or undecryptable files are reported as errors. Normal operation does not alter Winamp or Plex configuration.

To uninstall, exit Winamp and remove only the DLL you installed. The plugin's DPAPI file can be removed separately if desired. Do not remove Winamp databases or Plex files.

## Development and validation status

The plugin is tested in a separate copy of the target Winamp version. The SDK host passes 100 load/unload cycles, and a native 100-view-switch test stays within the defined resource limits. View changes cancel outstanding requests and transfer worker cleanup to a controlled background thread. Debug and Release each pass 20 automated test suites, including track-information routing and rendering, protected playlist references, diagnostic counterchecks and isolation checks for the separate replay probe. Aggregate process-handle counts are diagnostic only, following the Windows-runtime observations and the user's testing-policy decision. GDI/USER, owned-resource cleanup and functional checks remain enforced. Measurements, intermittent Windows IME observations and their limits are documented in [testing](docs/testing.md).

Source builds use related selection panes modeled on Winamp's local Audio library: Artists shows artists and albums above the track table, Albums shows albums above tracks, Tracks is a direct track table, and Playlists shows playlists above their tracks. Single-click a selector to update its dependent lists. Choose **All artists** or **All albums** to remove that selection; double-click a track to play it. Pane dividers can be dragged or adjusted with arrow keys when focused. Each list loads bounded pages in the background and has its own page controls when needed. A bounded metadata cache retains loaded pages. Playback remains under Winamp's control.

Search applies to the current area. Its persistent label reads **Search artists**, **Search albums**, **Search tracks** or **Search playlists**, including while text is entered. A new search removes artist/album selections. **Reset**, or Escape in the search field, clears search, selections and filters and returns to the first page, keeping the music library and navigation area. Switching areas or restarting does not restore implicit search/filter restrictions. Cache clearing is available in Settings instead of the browsing toolbar. These source changes are separate from the published version described under Download.

Narrow or low windows keep title, artist and duration visible without a horizontal scrollbar; wider, taller windows restore the full track columns. With one music library, low windows combine search and actions into two rows instead of repeating the library label. The library remains selectable through Settings. Multiple-library windows retain their chooser.

Each table shows page ranges as **1-250 of 306** (German: **1-250 von 306**). With page buttons, the range is centered horizontally and vertically between them. Without page buttons, it is right-aligned beneath its table. Selector ranges remain visible in small windows.

**Insert after current track** preserves playback or pause and changes the physical playlist order. Winamp shuffle and queue plugins can still choose a different next track. This action has been tested in Classic, Winamp Modern, Bento and Big Bento, in German and English. Queued streaming tracks remain playable within the same Winamp session after their exportable links expire; other processes then receive HTTP 410.

Personal ratings appear on a five-star scale. The context menu for one selected track sets one to five stars or removes the rating. Background writing and readback update the table only after confirmation. Existing fractional ratings are preserved when read. A real test track's rating was changed and restored.

Dialogs, tables, menus and controls use the `gen_ml` skin engine. Classic, Winamp Modern, Bento and Big Bento have been inspected visually. This is not validation of every third-party skin or DPI setting. There is no separate player toolbar; double-click and context actions hand selections to Winamp.

Artwork and missing track counts load for visible rows in the background. The real Dimmu Borgir album “Grand Serpent Rising” was checked with its cover and 13 tracks. Missing images remain blank. Prepared PNG artwork is supplied through Winamp's album-art service for plugin playback paths; both Big Bento player cover fields have been inspected.

MP3 and M4A use authenticated loopback streams. FLAC and OGG are prepared as private local files because the target decoders do not provide the required HTTP behavior. Separate ADTS-AAC files are repackaged into M4A without re-encoding. Real MP3, M4A, FLAC and OGG tracks have been checked for progress, seeking, duration and metadata. AAC-LC uses generated silence and an authenticated test server; real Plex ADTS tracks and additional AAC profiles remain unverified.

The proxy downloads each active response into a bounded private disk buffer independently of Winamp's reading speed. Playback starts after 128 KiB, or earlier if a shorter response finishes. The upstream connection can close after download while Winamp continues reading. Automated tests compare complete content after a receiver temporarily stops reading and check cancellation, quotas and cleanup. Guarded byte-range resumption still requires unchanged content. This does not guarantee playback through an outage longer than the available buffered content.

**Settings → Diagnostics** refreshes once per second and shows received/sent MB since the proxy connection started, active streaming downloads and reserved private media storage against the 2147.4 MB budget. Sizes use decimal MB (1 MB = 1,000,000 bytes), truncated to one decimal place; German uses a decimal comma. Exact byte limits are listed in [Architecture](docs/architecture.md#storage-units-and-limits). It also shows next-track cache size, prefetch activity, cache hits and a numeric prefetch error code. Storage includes response buffers, prefetched files and prepared local FLAC/OGG/AAC files; known-length downloads reserve their whole response size before reception. These are shared counters, not a percentage or remaining playback time for the current track. A completed download can remain buffered while Winamp continues playing.

**Filters / views** opens a skinned native dialog. Genre values come from Plex; year filters use the album release year, and track views also offer audio codecs. A numbered Filters button indicates active filters. Named views retain search text, sorting and filters for the selected server/library and item type, and are restored only when explicitly applied. Enter a name and Save, select a saved view and Apply, or Delete it. The unfiltered Tracks area retains selection, page and the top visible item when revisited and after restarting Winamp. Missing rows are not selected. Saved views and positions use the same DPAPI-protected configuration as connection settings.

Natural-end tests include an MP3 track lasting 3:29 and an M4A track lasting 4:11. A later “The Puppeteer” run reached its natural end after a real upstream interruption and guarded resumption; “Until Dawn” also reached its natural end. With the progressive buffer, muted MP3 and M4A runs again reached natural end at 273 and 251 seconds. These results do not establish uninterrupted playback for every track or network condition.

Plex timeline updates for playback, pause, resume, seeking and stop were read back from the real server. A natural track ending produced a new history entry and increased the play count. Details and unverified cases are recorded in [docs/testing.md](docs/testing.md).

## Build

Use Visual Studio 2022 or its Build Tools with C++ x86 tools, Windows SDK, CMake 3.24 or later, the unchanged SDK 5.55 headers, published 2013 Nullsoft IPC header and 2016 skin interface header. Obtain the headers with `scripts/Fetch-WinampSdk.ps1` first. The only additional C++ library is checksum-pinned nlohmann/json 3.12.0. Image processing uses Windows Imaging Component.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Output: `build/Release/ml_plex.dll`. Prerequisites, exact SDK commands, Debug builds and troubleshooting are covered in [docs/build.md](docs/build.md). SDK headers are checked locally by checksum and are not distributed in this repository.

## Security and limitations

- Playback URLs contain no Plex token, server address or Plex media path.
- The proxy binds only to `127.0.0.1` on an automatically assigned port. Each grant has 192 random bits and expires for other processes after two hours.
- Replacement, connection teardown or plugin shutdown revoke the associated grants. Saved local URLs are not permanent HTTP access; the plugin renews registered entries through the protected reference registry.
- Saved playlists can be replayed after restarting Winamp by the same Windows user with the same plugin profile and selected Plex server. The plugin records server, track and media-part identities in `playlist-references.dpapi` beside its connection profile. It verifies the current server and renews local playback targets in the background before playback. Real MP3 and FLAC/M4A restart checks pass in the regular DLL. Old entries created before reference recording must be added again from the library once; their lost Plex IDs are not guessed from titles. Keep the protected reference file when retaining exported playlists. See [testing](docs/testing.md).
- Restoration supports playlists of up to 10000 entries. It resolves the requested track first, then prepares at most one predicted successor. FLAC/OGG/AAC entries are also queued without downloading the entire selection. Unused restored files are released; returning to an evicted entry downloads it again. The 536.8 MB per-file and 2147.4 MB shared-media limits still apply. The protected registry retains up to 100000 playback aliases and 67.1 MB; quota errors are explicit, and old mappings are not silently evicted. It is not a portable playlist format or an offline music cache.
- The running Winamp instance has additional session access, verified by Windows ownership of the exact TCP client connection. This does not extend exported-link validity or bypass revocation and security checks.
- One listener manages up to 10,000 independent grants; worker count does not grow with playlist length.
- Active MP3/M4A responses use private temporary disk buffers in the same restricted session store. Each active response has at most one download thread, bounded by four proxy workers. Response buffers are deleted after the response ends or is cancelled. Buffers and prepared media share the 536.8 MB per-file and 2147.4 MB total limits. Quota or disk errors fail explicitly. One additional worker can cache the next owned sequential streaming entry, at most 33.5 MB per file and two cached entries (current/next). Only completed files are reused, including for seeking. Streaming byte prefetch remains sequential; the playlist resolver additionally prepares one successor predicted by Winamp, including native shuffle. Foreign playlist URLs/files are never fetched by the plugin. Playback and queue order remain Winamp's responsibility.
- Prepared FLAC/OGG files and repackaged AAC reside in a private session under `%LOCALAPPDATA%/WinampPlexLibrary/playback`. Only the current user and SYSTEM have access. Random filenames contain no server paths or tokens. Limits are 536.8 MB per file and 2147.4 MB total. Preparation finishes before the decoder opens the requested entry. Unused restored files are released; returning to them downloads them again. Marked abandoned sessions are cleaned on subsequent preparation. HTTP grant expiry does not apply to these local files.
- AAC preparation requires Windows Media Foundation. Missing components are reported as errors. Other formats do not require these DLLs. ADTS Program Config Elements and multiple raw data blocks per frame are rejected; HE-AAC has not been tested.
- Direct Play uses Winamp's existing decoders. There is no automatic transcoding path.
- External HTTP, invalid TLS certificates, cross-origin redirects and unsupported media paths are rejected. IPv6 origins are not supported.
- Sign-in and server discovery use Plex services; media bytes come from the selected Plex Media Server.

## Documentation

- [Research and references](docs/research.md)
- [Winamp SDK and ABI](docs/winamp-sdk.md)
- [Plex API](docs/plex-api.md)
- [Architecture and security](docs/architecture.md)
- [Build and installation](docs/build.md)
- [Testing and unverified integration cases](docs/testing.md)
- [Requirements and development plan](prp/prp.md)

## License and contributions

Project code uses the **MIT License**, see [LICENSE](LICENSE). Third-party terms are listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). SDK files, downloads, build artifacts and local access settings are excluded from Git. Binary packages include project and nlohmann/json license texts. This project is not affiliated with the owners of the Winamp or Plex trademarks.

Please report bugs through [GitHub Issues](https://github.com/Eitschman/winamp-plex-library/issues), including Winamp version, skin, Windows version, audio format and reproduction steps. Do not publish tokens, private server addresses or local configuration files. Contributor build and validation guidance is in [CONTRIBUTING.md](CONTRIBUTING.md).
