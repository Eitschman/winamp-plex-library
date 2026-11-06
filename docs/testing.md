# Validation and integration checklist

Baseline evidence date: November 5, 2026. This document distinguishes executed checks from requirements still awaiting a suitable environment. Local log and screenshot names refer to the ignored `.local` directory; those files may contain private display information and are not published.

## Interrupted media and artist display

A native “The Puppeteer” run stopped after 129,788 ms despite a decoder duration of 273 seconds. Diagnostics recorded HTTP 200, Windows error 38 and 4,521,984 of 9,330,407 expected bytes. This reproduced an incomplete transfer in the common transport path. The exact cause of the upstream termination remains unresolved. A later healthy request's HTTP 200/Windows 0 cannot reconstruct that earlier failure, so numeric failure history is retained.

Interrupted full GETs can resume the same file at the next byte not yet downloaded, at most three times. Resumption requires known length, a strong ETag, the same origin/token/path, HTTP 206, identical ETag, exact Content-Range and remaining Content-Length. Changed content and inconsistent responses fail. Initial HTTP 206 seeking responses have no equivalent resumption proof. No alternate server, format or decoder is selected.

Synthetic counterchecks without resumption detect missing content. With resumption all bytes match without duplication; changed ETags are rejected. Removing retained failure history also fails its countercheck. Media receive timeout is 15 seconds while API deadlines remain shorter. A six-second media pause passes; the five-second timeout countercheck fails.

Artist and title are jointly formatted for playlists/current display while metadata fields remain separate. A native Classic screenshot and M3U8 entry confirm “Elvellon - The Puppeteer”. Automated coverage includes Unicode and absent artist.

Debug and Release each pass 13 suites with 137 transport checks for the guarded-resumption baseline (`ctest-resume-guarded.log`, `ctest-resume-guarded-debug.log`). An intermediate lifecycle run detected handle growth; an unchanged repeat passed. That sporadic observation remains unresolved.

## Progressive response buffer validation

The local development implementation adds a private progressive disk buffer between WinHTTP and the decoder. These changes are not included in v0.1.0. Focused transport tests pass 178 checks, including:

- A decoder with a small receive buffer stops reading for 1200 ms. All four MiB are downloaded from the source while downstream delivery is still incomplete; the producer releases its network resources while buffered data remains available. The resumed reader receives every byte unchanged, then the reservation is released.
- A paused reader of an HTTP 206 seeking response does not hold back its source download. Its final status, offset and every remaining byte match the requested range.
- A decoder disconnect during a stalled download cancels the producer and releases its buffer within 1500 ms, without recording a source failure. Plugin shutdown while waiting below the startup threshold also completes within that bound.
- Small deterministic quotas check known-length reservation, unknown-length growth, per-file limits, concurrent buffers, shared prepared-media quota and released reservations. Oversized proxy responses return an explicit empty HTTP 502 rather than starting unbuffered playback.
- Private session DACL, exact published offsets, partial-file deletion and marked abandoned-buffer cleanup are checked. Buffered bytes are drained before a terminal producer error remains visible.

The countercheck temporarily recouples the producer to downstream consumption. It fails with exit code 1 at complete_upstream_is_buffered_while_decoder_does_not_read (`transport-buffer-coupled-negative.log`). The change is removed; the regular implementation passes (`transport-buffer-range.log`).

With the buffer, a muted native “The Puppeteer” run reaches natural stop at 273 seconds, last position 272,039 ms and elapsed playback 272.6 seconds (`buffer-puppeteer-natural-end.log`). Its active session has no TranscodeSession. The received/expected diagnostic label was inspected in the native skinned dialog, with no clipping (`buffer-settings-diagnostics.png`, `buffer-settings-current.log`).

A muted native M4A run also reaches natural stop at 251 seconds, last position 250,470 ms and elapsed playback 249.8 seconds. Metadata and the active session without TranscodeSession pass (`buffer-m4a-natural-end.log`). Its loaded DLL matches the current Release build by SHA-256.

Debug and Release each pass 13 suites with all 178 transport checks, including the large-range case (`ctest-buffer-range-debug.log`, `ctest-buffer-range-release.log`). Seven packaging checks pass with the current Release DLL (`buffer-packaging-current.log`).

Additional muted native checks pass for MP3 and M4A: 30 seconds uninterrupted playback, duration, seeking, metadata and own active Plex session without TranscodeSession. After seeking, observed positions are 10,380 and 10,560 ms. The decoder fault series then passes healthy, HTTP 401, 403, 404, redirect, truncated, stalled and healthy-again cases. Truncation reports Windows 38; stalled reception reports 12002 after actual progress. Maximum UI-query latency is 9 ms and stop latency 51 ms in this run (`buffer-seek-faults-native.log`). These fixtures do not establish behavior during every real home-network outage.

### Natural endings in the validated playback baseline

| Track/format | Decoder duration | Last observed position | Observed elapsed playback | Evidence |
| --- | --- | --- | --- | --- |
| Real M4A | 251 s | 250,570 ms | 250.2 s | `stream-natural-end-fixed.log` |
| Real MP3 | 209 s | 208,259 ms | 208.6 s | `stream-natural-end-fixed.log` |
| The Puppeteer, MP3 | 273 s | 271,939 ms | 272.5 s | `elvellon-resume-natural-end-repeat.log` |
| Until Dawn, MP3 | 369 s | 367,350 ms | 368.3 s | `until-dawn-natural-end.log` |

These runs play from the beginning without seeking and reach natural stop. The Puppeteer diagnostics confirm an actual interruption after 2,424,832 of 9,330,407 bytes followed by successful recovery (`diagnostics-resume-playing.log`). A preceding run failed a test-host minimization assertion and is not playback evidence. The user separately reported two uninterrupted tracks after an explicitly authorized DLL replacement; their identities/durations were not supplied.

The natural-end checker rejects an intentional early stop at 35,088 ms, with last position 34,839 ms and exit code 1 (`stream-end-early-stop-negative.log`). Short progress/seek tests do not establish whole-track playback.

## Automated execution

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

The current validated baseline contains 13 suites: plugin lifecycle, view status, view reaper, WinHTTP callbacks, AAC remux, playback reporter, transport, config/auth, catalog, artwork, artwork provider, feedback and security. Earlier referenced runs had fewer suites; their counts are not current totals.

Coverage includes:

- Original SDK descriptor/export, foreign node rejection, skin/service lifecycle, both host languages, settings/diagnostics and 100 load/unload cycles.
- Original dialog logic for classified HTTP/network failures, status priority, separate views, catalog-message preservation and exception containment.
- Bounded background teardown, FIFO order, COM MTA initialization and queue draining.
- WinHTTP callback-context lifetime through actual callback return.
- Byte-exact full/HEAD/range/suffix responses, 416, 401/403/404, redirects/server errors, private-header exclusion, cancellation, independent grants and targeted revocation.
- Origin/Host/capability/path/query validation, injected headers and invalid byte ranges.
- DPAPI Unicode roundtrip, absent plaintext access data, atomic replacement, corruption, size/profile limits, cache options and connection ranking.
- Typed parsing, inherited artwork, large playlist durations, fractional ratings, invalid pagination, injection, ordering and cache TTL/LRU/clear behavior.
- WIC decoding, damaged/oversized images, missing images, safe paths and PNG roundtrip.
- Album-art dispatch/registration, missing services, allocator failures, read-only behavior and cleanup.
- Timeline state/time validation, rating scale, malformed values, queue limits/order, token denial and cancellation.
- ADTS/M4A payload identity, existing-output rejection, quotas, cancellation, malformed/truncated frames and unexpected trailing bytes.

Each check covers its stated condition, not every product feature.

### Test counterchecks

Controlled negative cases were removed after proving their checkers detect faults:

| Fault | Detected by / local evidence |
| --- | --- |
| Short proxy send timeout | Byte mismatch, `backpressure-before.log` |
| Missing stop check under backpressure | `backpressure-no-stop.log` |
| Extended incomplete-header deadline | `backpressure-long-header.log` |
| Missing full-response continuation / changed ETag | Transport resumption checks |
| Cleared transfer-failure history / short media timeout | Guarded transport checks |
| Synchronous view destruction | `view-reaper-blocking-negative.log` |
| Missing drain, wrong apartment, absent limit, reordered cleanup | `view-reaper-drain-negative.log`, `view-reaper-com-negative.log`, `view-reaper-limit-negative.log`, `view-reaper-order-negative.log` |
| Returning before WinHTTP callbacks finish | Exit 1, `http-callback-deterministic-negative.log` |
| Missing PID comparison / expired queued grant removal | `host-session-identity-negative.log`, `host-retention-negative.log` |
| Missing expiry status / translated expiry message | `expired-status-negative.log`, `expired-view-negative.log` |
| Missing shared artwork lifetime / pixel return | `artwork-shared-cache-negative.log`, `artwork-pixels-negative.log` |
| Missing shared library cache | `library-cache-negative.log` |
| Wrong playlist insertion position | `insert-position-negative.log`, `album-context-wrong-placement-negative.log` |
| Stale catalog status after recovery | `view-status-negative.log` |
| Invalid layout width of 80 px | `view-layout-bounds-negative.log` |
| Leaked cancellation event | 159 versus 158 handles in cycle two, `lifecycle-handles-negative.log` |
| Omitted table skin release | Native GDI growth, `view-resources-negative.log` |
| Invalid CDB output despite exit 0 | `handle-summary-negative.log` |
| Injected 96-event resource leak | `catalog-resources-negative.log` |
| Wrong track-artist sorting / column selection | `track-artist-sort-negative.log`, `sort-columns-negative.log` |
| Stale final rapid-search request | `rapid-search-negative.log` |
| TranscodeSession or missing active player | Feedback/session tests |
| Wrong audio extension, foreign private path or query suffix | Format/path tests |

### CI, packages and release

[Actions run 42061725581](https://github.com/Eitschman/winamp-plex-library/actions/runs/42061725581), source d85e25a, passed Debug and Release 13/13 suites on fresh Windows 2022 runners. Release additionally passed seven package checks. The downloaded artifact had exactly five files, x86 PE machine 0x014c and the DLL flag. Installation/license text matched source. Its DLL SHA-256 was `246d2754b71be06afcda5e7303f026c45563b218bf47406fbadd20003440cca9`.

That CI DLL passed a native repeat with 30-second uninterrupted MP3 playback, 209-second duration, seeking, metadata and its own Plex session without TranscodeSession (`ci-artifact-native-repeat.log`). The first run passed playback but failed to find the session; the cause remains unresolved (`ci-artifact-native.log`). The regular local DLL was restored to the isolated copy afterwards.

`tests/packaging_tests.ps1` passes seven positive/negative cases: missing DLL, invalid PE, wrong architecture, absent DLL flag, missing dependency license, exact valid content/hash and refusal to overwrite a directory. It uses isolated copies and does not modify the build DLL. Evidence: `packaging-seven-checks.log`, `package-github-check.log`. Actionlint 1.7.12 detects a deliberately invalid timeout. Fresh SDK acquisition/hash validation also passes (`ci-sdk-fetch-fresh.log`). Native tests are not run in Actions.

Release v0.1.0 is published at [GitHub Releases](https://github.com/Eitschman/winamp-plex-library/releases/tag/v0.1.0). The downloaded 276,607-byte ZIP matches SHA256SUMS:
`b0e92f4eb86a00474871603e39c6152d714c7c85061f7fe09d97fb3d66ed6fe7`.
Its exact five files are the x86 DLL, INSTALL.md, LICENSE, THIRD-PARTY-NOTICES.md and the nlohmann/json MIT notice. This download verification is separate from local playback evidence.

[Actions run 42189390597](https://github.com/Eitschman/winamp-plex-library/actions/runs/42189390597), source cf1f4f9, also passes Debug and Release 13/13 suites, Release 7/7 package checks and the secret-exposure job. Both downloaded build logs pass redacted scans. The downloaded development package has exactly five expected files; English INSTALL.md and THIRD-PARTY-NOTICES.md, plus LICENSE, match the current source byte for byte. Its x86 DLL SHA-256 is `328c6c155883d2fc3c858b253d3bbdc6c5118d4fba230c92f57f636e1be05dad`. Package text and extracted ANSI/UTF-16 DLL strings have no scanner findings. Native buffer tests used the local DLL, not this independently built CI DLL.

## Runtime test discipline

Use only the explicitly authorized isolated target copy, identified by exact process path and WinampPlexTest window class. Today's tests must be muted and minimized, except briefly for necessary visual/mouse checks. Stop playback before restoring volume, clear/save the test playlist and re-minimize afterwards. Never alter the normal instance or infer another DLL-install permission from the prior one-time authorization.

The minimized-start checker observes stable minimization for at least 300 ms within a five-second startup window. Screenshot capture restores prior minimization even after output failure. Invalid/foreign window targets and absent output directories are rejected (`restart-minimized-native.log`, `minimized-start-verification.log`, `minimized-capture-native.log`). A settings test explicitly restores its minimized owner before required visual interaction and minimizes afterwards (`settings-minimized-fixed.log`).

## Native host and resource checks

### Lifecycle and navigation

- [x] x86 build, Plex node, native view and plugin-management entry inspected.
- [x] Configuration invoked from plugin management; normal stop/restart.
- [x] Switching among all Plex views and an actual foreign Media Library plugin.
- [x] Exit during started sign-in, a 12-second pending browser approval, active media preparation and stalled TLS.
- [x] Resource observations with unchanged failure limits; unresolved observations remain below.

The phase-one gate passed with a real MP3: forward progress, HTTP 200, 262,144 transferred bytes, 306-second duration, seeking result zero and subsequent 10,569 ms, plus Plex title in a token/server-free M3U8 (`playback-metadata-hook.log`). Target-version compatibility requires WM_COPYDATA/M3U8 rather than the crashing old metadata structure, and IPC_SETPLAYLISTPOS plus WINAMP_BUTTON2 rather than IPC_STARTPLAY.

The latest ViewReaper 100-switch native run passed with equal 60-second settling periods in 319.4 seconds: handles 584 → 586, GDI 1479 → 1479, USER 232 → 232, threads 19 → 19, private bytes 87,932,928 → 86,704,128, TCP/UDP zero (`view-resources-reaper-100.log`, matching summary JSON). This is a bounded comparison, not a general leak proof. Debug/Release and independent Release each passed 13 suites.

The stalled-TLS navigation check recorded a 3 ms UI query, 2 ms view switch and regular exit in 849 ms; original DPAPI content remained hash-identical (`connection-navigation-reaper-native.log`). WinHTTP callback counterchecks confirm waiting through final callback return rather than merely receiving HANDLE_CLOSING.

Five reaper tests verify nonblocking transfer, bounded pending views, awaited completion, MTA and FIFO. No detached worker or abandoned asynchronous apartment cookie remains.

### Resource evidence and unresolved observations

The checker uses three warmup cycles, equal before/after settling periods and explicit socket/handle/window-class counts. It logs counts rather than window titles. Invalid process IDs and missing idle intervals are rejected. Noninvasive CDB uses `-pvr` and requires an actual type summary; `-pd` can return zero without useful output and is not accepted as evidence.

| Run | Observation | Evidence |
| --- | --- | --- |
| 20 switches, 60 s settling | Handles 562 → 564; GDI 1490 → 1491; USER 229 unchanged; threads 17 unchanged; private bytes 93,323,264 → 93,474,816 | `view-resources-regression.log` |
| Two 50-switch runs | +21 handles, then +17 handles/+4 USER; GDI/sockets stable; causes unassigned | `view-resources-long-sockets.log`, `view-resources-long-handles.log` |
| 10-switch follow-up | 637 handles, 1491 GDI, 236 USER, 26 threads, one TCP unchanged | `view-resources-trace.log` |
| Another 50-switch run | Handles 690 → 694; GDI 1519 and USER 237 unchanged; TCP 2 unchanged | `view-resources-long-inventory.log` |
| 100 switches without shared library cache | Handles 635 → 639; GDI 1509 → 1510; USER 233 unchanged; TCP 1 unchanged | `view-resources-100-idle.log` |
| 100 switches with shared library cache | Handles 560 → 574 exceeds limit; GDI 1490 → 1491; USER 231 → 233 | `view-resources-library-cache-100.log` |
| 20 switches with shared artwork cache | Handles 574 unchanged; GDI 1490 → 1492; USER 233 → 235, within limits | `view-resources-shared-artwork-20.log` |
| 100 minimized switches with shared caches | Handles 597 → 613 and USER 231 → 235 fail unchanged limits; GDI 1498 unchanged; threads 19 → 22 | `view-resources-shared-cache-100-minimized.log` |
| Idle control | Handles 582 → 570; threads 17 → 14; GDI 1484/USER 233 unchanged; TCP/UDP zero | `view-resources-idle-control.log` |
| Synthetic UI-only diagnostic DLL, 100 switches | Handles 484, GDI 1479, USER 231 and threads 12 unchanged | `view-resources-ui-only-100.log` |
| Latest reaper, 100 switches | Within limits, as detailed above | `view-resources-reaper-100.log` |

Passing comparisons do not explain every earlier growth observation. Handle tracing in x86/x64 did not yield usable creation stacks and was disabled afterwards. Synthetic UI-only code was removed and the real DLL rebuilt/restored. Catalog-only transport with WIC passed 20 cycles, handles 333 → 305, GDI zero, USER one (`catalog-resources-native.log`); this does not isolate host/concurrent-cancellation effects.

Tables release gen_ml skin resources at WM_DESTROY before control WM_NCDESTROY. Removing that release reproduces per-table GDI growth. SDK lifecycle measurement also checks process handles after every unload against the warmed baseline.

Foreign-plugin integration used a temporary, hash-checked ml_local.dll only in the closed test copy, removing it afterwards. Five mixed-view cycles passed functionally but added three GDI objects per cycle. Foreign-only audio/video switching added four per cycle independently of Plex views. This remains a target-host observation (`host-integration-fixed.log`, `host-resources-foreign-only.log`, `host-resources-mixed-fixed.log`). RequireStableResources turns global growth into an explicit failure.

## Connection and settings checklist

- [x] Browser authorization, verified HTTPS server/library discovery and DPAPI restart.
- [x] Settings open without browser sign-in; manual address only under Advanced; no language selector.
- [x] Explicit reauthorization, automatic routing, save/reopen with 13 advertised connections.
- [x] Cache option persistence/restoration and disabled-cache behavior.
- [x] HTTP rejection, DNS errors, cancellation and Cancel without DPAPI changes.
- [x] German and English inherited from actual host language packs.
- [x] Native unavailable-server and invalid-certificate checks.
- [x] Settings visually inspected in Classic, Modern and Big Bento; tab order, hidden advanced controls, Escape and unclipped diagnostics.
- [ ] Real revoked token followed by reauthorization.
- [ ] Empty real music library and account with no servers.
- [ ] Positive local HTTPS route selection in the home network.

Connection fault fixtures use process-local DPAPI profiles and preserve the original hash. A loopback peer confirms TLS ClientHello, then stalls; UI queries remain responsive and shutdown completes in the observed 615–845 ms runs. A self-signed TLS fixture is reachable through an independent pinned check, then rejected by WinHTTP without application data. The Windows trust store and network configuration remain unchanged (`connection-faults-native-guards.log`, `connection-callback-native.log`).

Sign-in shutdown recorded a 3 ms UI trigger and 992 ms regular exit. A pending-browser variant made 110 UI queries, at most 3 ms, then exited in 981 ms after twelve seconds. Both preserved the DPAPI hash (`sign-in-shutdown-native.log`, `sign-in-browser-shutdown-native.log`).

Startup routing works without opening Settings. Native paging, navigation and search pass (`startup-route-native.log`). Synthetic route checks verify local priority, unavailable local route, wrong identity without token forwarding, fixed route, auth denial without further search and cancellation.

## Audio and private-file validation

| Format | Real Plex Direct Play | Duration and metadata | Seeking | Remaining scope |
| --- | --- | --- | --- | --- |
| MP3 | Tested | Tested, including natural endings | Tested | Additional real outage conditions |
| M4A | Tested | 251 s fixture and Plex title | 10,550 ms after 10 s seek | Additional interruption cases |
| FLAC | Tested through private file | 564 s and Plex title | 10,404 ms | Other decoder/environment variants |
| OGG | Tested through private file | 263 s and Plex title | 10,541 ms | Other decoder/environment variants |
| AAC-LC | Synthetic authenticated ADTS, repackaged M4A | 45 s, 1939 frames at 44.1 kHz | 10,580 ms | Real Plex ADTS, HE-AAC and other profiles |

`Test-PlexFormats.ps1` selects through ordinary library views, narrowing ambiguous titles by artist/album and checking actual safe-path extensions. MP3/M4A/FLAC/OGG pass progress, duration, seeking and metadata, with their own active Plex session lacking TranscodeSession (`formats-no-transcode-native.log`, `formats-view-reaper-native.log`). A missing own session is an error, not Direct Play evidence.

The inventory checked 20,782 stable tracks and 20,782 returned Parts: 20,332 MP3, 77 MP4, 186 OGG, one FLAC and 100 other container values. Eighty-six tracks lack a supported Direct Play variant; 1434 lack an artwork reference. No Part ends in .aac, although 77 report AAC codec (`catalog-aac-search.log`). Container labels do not establish decoder compatibility. Private reports are not committed.

`Test-PlexAac.ps1` serves generated AAC-LC silence from an authenticated loopback fixture through the actual preparation path (`aac-current-native.log`). The raw local ADTS file incorrectly reports 50 instead of 45 seconds, even with TLEN; the duration checker detects this. M4A repackaging gives the frame-derived duration. Before/after normalized AAC payload SHA-256 values match; sample-table checks compare every payload. No real Plex ADTS or Windows N compatibility claim follows.

Private-file tests cover exact bytes, write protection, DACL, quotas, cancelled downloads, HTTP errors, safe stale-session cleanup and preservation of active/foreign sessions. FLAC/OGG files and their session directory disappear after regular exit (`local-media-shutdown.log`).

`Test-PlexMediaShutdown.ps1` confirms an actual active FLAC write handle after HTTP 200 using an exclusive-lock countercheck, not file size. While the UI shows loading and the playlist is still empty, a query takes 0 ms; shutdown takes 959 ms and removes partial files/session (`media-shutdown-native-verified.log`, `media-lock-counterchecks.log`).

### Capabilities and shutdown

- [x] Full playlist media lines match only owned loopback URLs or private-session paths, without tokens/server origins.
- [x] Shortened expiry produces HTTP 410 without upstream requests and a distinct translated request_expired message; a fresh grant clears it.
- [x] Native ten-second host-session test preserves decoder progress/seeking after expiry while foreign access stays HTTP 410.
- [x] Original two-hour defaults restored after shortened tests; no two-hour real-time run.
- [x] Fresh proxy range request returns 16 bytes/HTTP 206; normal plugin exit makes the same link fail (`Test-PlexProxyShutdown.ps1`).
- [ ] Real missing media file and real home-server interruption during decoder playback.
- [ ] Real ADTS-AAC and additional profiles.

Host-PID tests use a separate child process: fresh access succeeds, expired foreign access fails, owned full/HEAD/range access remains byte-exact, and owned use does not extend foreign validity. Revocation and Origin protection remain effective (`host-session-transport-current.log`, `host-session-native-short.log`).

## Playback test procedure and fault injection

`Test-PlexPlayback.ps1` observes 30 seconds before seeking, with unchanged playlist index and monotonic position. ToEnd requires exactly one track, temporarily disables repeat, and watches without seeking to natural stop. Duration, elapsed time and last position must agree. Volume/repeat restore only after checked stop.

The default checker reads no DPAPI data. Explicit CheckPlexSession uses the application's normal user-bound access for session comparison; it does not accept manually supplied tokens. Empty playlists and seek positions beyond duration are negative cases.

Backpressure tests stop reading for 1200 ms using a small receive buffer, then compare all four MiB. Proxy shutdown must remain below 1500 ms with a test watchdog. Incomplete headers close after three seconds without contacting PMS. With the former short send timeout, a real MP3 stopped at 19.8 seconds (`mp3-continuous-before.log`); nonblocking transfer passes continuous and natural-end tests.

`Test-PlexPlaybackFaults.ps1` uses generated 120-second MP3 silence and an authenticated loopback fixture. Its separate proxy_probe process uses the same transport code; this does not test the plugin view's failure overlay. Healthy, 401, 403, 404, 302, truncated and stalled responses were checked (`playback-faults-progress-native.log`). Truncation yields Windows 38 and stalled reception 12002 after real player progress; UI queries were at most 10 ms and stop at most 41 ms. A subsequent healthy run passes.

After increasing the media receive timeout, the stalled observation window is 25 seconds. The 15-second countercheck fails; the corrected run reports 12002 with UI queries at most 7 ms and stop 18 ms (`resume-native-faults-fixed.log`). These are observations, not universal latency guarantees.

Interactive proxy_probe accepts status and stop; valid input exits zero, unknown commands exit two. Generate the fixture locally with:

```powershell
ffmpeg -f lavfi -i anullsrc=r=44100:cl=stereo:d=120 -c:a libmp3lame -b:a 128k .local/audio-fixtures/silence.mp3
```

FFmpeg is test-only. These fixtures do not change Plex access, server settings or certificate trust.

## Library, UI and artwork checks

- [x] Real artist → three albums → five tracks → one appended track, Back and all four nodes (`Test-PlexLibrary.ps1`). Inventory at that time: 305 artists, 1333 albums, 20,770 tracks.
- [x] Single-library label without dropdown, no redundant root heading/Back, artist pages of 250 and 56, navigation/search and consistent row height (`Test-PlexLibraryUi.ps1`).
- [x] Layout inspected at 1024 × 600, 600 × 400, 420 × 330 and a 1000 × 450 Bento container.
- [x] Classic, Winamp Modern, Bento and Big Bento actually loaded and screenshots inspected.
- [ ] A real second music library, further third-party skins and other DPI settings.

LibraryCache tests use a controlled clock and counted loader: independent copies, exact TTL, revisions/scopes, clear, disabled cache, empty lists, error propagation/recovery and nonpositive TTL. The real `catalog_probe --library-cache` confirms repeated access without requests, clear causing one request, disabled access causing two, and a new revision causing one (`library-cache-live.log`). Invalid arguments fail before configuration access.

Artwork cache tests include reuse after view destruction, server/scope isolation, clear/disabled behavior, eviction at 64 and actual decoded BGRA pixels from a two-color BMP without a second request (`artwork-shared-pixels-positive.log`).

“Grand Serpent Rising” was inspected with cover, year 2026 and 13 tracks. All tracks inherit the album cover. A real track with no own/inherited image shows a blank slot; an independent positive row has an actual image index. Missing album art/404 has synthetic coverage, not a separate real album fixture.

Both Big Bento player artwork fields show the prepared cover. The provider had 13 mappings and 14 successful queries in the muted run. Counterchecks show DATABASE is not queried for these HTTP tracks and V5-BMP is not displayed despite WIC decoding; EMBEDDED plus PNG/type png works. Artist/album/title metadata hooks and inherited list thumbnails were inspected.

Twenty sorting cases compare 23,000 cells against real API references: both directions for artist, album title/artist/year, and track title/artist/album/duration/number/year (`track-artist-sort-native.log`). Track artist uses originalTitle. Format/rating do not currently offer sorting. A changing server snapshot may legitimately invalidate the comparison.

RapidSearch issues 52 alternating searches through the real timer without changing debounce. Final empty-search and restored artist results survive late responses; triggers were at most 15 ms (`rapid-search-control-text-native.log`, `rapid-search-restored-native.log`). Read edit text through WM_GETTEXT rather than a foreign-window title query.

Bulk handoff checks 238 supported MP3 entries with independent grants, Unicode and order; observed thread count stays 24 (`library-bulk-reset-native.log`). This is not a 10,000-track handoff test or a general leak proof. Tests recreate the view to avoid stale sorting and clear the playlist afterwards.

### Menus, insertion and playlists

`Test-PlexAlbumMenu.ps1` appends the 13-track reference album, then inserts it after position zero. All 26 entries match exact order, titles, durations and allowed paths while current position and stopped state are preserved (`album-context-insert-order-restored.log`). Screenshots wait for asynchronous skin drawing; earlier blank/invisible menu captures are not visual evidence.

`Test-PlexPlaylistInsert.ps1 -UsePlugin -State Stopped|Playing|Paused` inserts three real MP3 selections among five existing entries. All eight entries, order, metadata and grants are checked; playback/pause and position remain unchanged. The current fixture is synthetic silence, so it creates no Plex history. Volume, stop and playlist cleanup are in finally.

Actual insertion context menus were clicked and inspected in all four skins and both languages (`insert-menus-skins.log`, `insert-menu-host-languages-visible.log`). SDK tests also cover empty/final insertion, append, missing editor, invalid positions, long/relative/NUL paths and partial imports. Partial import rollback is not claimed.

`Test-PlexPlaylists.ps1` verifies an existing empty and populated Plex playlist without modifying the server: an explicit empty state and 22 appended tracks in Plex order (`playlists-native.log`, `playlists-after-skin-release.log`). Playlist/track views with covers were inspected in Big Bento at 2048 × 1183.

Host language tests temporarily change only the test copy's langpack using Windows profile APIs while closed, verify both languages, and restore the original German pack (`host-language-native.log`). This does not cover arbitrary packs or live switching without Winamp's normal restart.

Status-overlay tests send a foreign-Host HEAD, expect HTTP 403 and a localized action message, preserve a newer empty search result during error, then recover with a valid HEAD/HTTP 200. Diagnostics contain fixed codes only. Overlay/diagnostics and three layouts were visually inspected (`status-overlay-layout-capture-native.log`).

## Ratings, timeline and compilation artists

The user authorized one existing test track, rating restoration and a remaining test history entry. `feedback_probe --rate <local-state-file>` preserves the original value; restore reads it back. Private state files contain no credentials but are not published (`feedback-rating-live.log`).

`Test-PlexRatings.ps1` verifies the visible track identity, menu action, table update and server value, then restores/readbacks in finally without playback. The menu was inspected in Big Bento, Classic and Modern. This does not validate Winamp's own rating buttons.

The synthetic API timeline test confirms playing/paused states and removal after stopped (`feedback-timeline-live.log`). The native `Test-PlexTimeline.ps1` separately checks actual player progression, pause/resume, ten-second seeking and stop through server readback (`timeline-native-counters.log`). CheckHistory seeks four seconds before end, waits for natural stop and confirms new historyKey plus increased viewCount (`timeline-history-native.log`). No extra scrobble is sent.

Compilation tests compare originalTitle and grandparentTitle against list fields and `IPC_GET_EXTENDED_FILE_INFOW_HOOKABLE` using original extendedFileInfoStructW. Non-hookable decoder queries do not establish Media Library metadata. Real title/artist/album/albumartist and both player cover fields were inspected (`compilation-artists-native-verified.log`, `compilation-player-native.log`). Normal checks do not play or write ratings; InspectPlayer is muted and cleans up.

## Limits

SDK simulation is not the entire Winamp player. HTTP success does not prove audio output. Player progress establishes decoder processing, not a human listening judgment. Shortened expiry tests do not establish a two-hour run. Resource tests do not prove the absence of all leaks.

Remaining external cases include real token revocation/reauthorization, a serverless account, an empty real library, positive home-network local HTTPS selection, real home-server outage, real ADTS/additional AAC profiles, WACUP and additional skins/DPI settings. Do not alter server/router configuration or credentials merely to manufacture these states.
