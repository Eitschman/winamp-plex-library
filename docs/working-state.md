# Working state and continuation

## Current release and installed build

[Version 0.3.3](https://github.com/Eitschman/winamp-plex-library/releases/tag/v0.3.3) is published from source commit e4203d3. The [release workflow](https://github.com/Eitschman/winamp-plex-library/actions/runs/42240991267) and [main workflow](https://github.com/Eitschman/winamp-plex-library/actions/runs/42240990738) pass. Both configurations pass 18 suites; Release also passes seven package checks. Downloaded package contents, x86 version, checksum and licenses were verified. Full-history, package, DLL-string and workflow-log secret scans report no findings.

The user explicitly authorized replacing the normal Winamp DLL with the published 0.3.3 DLL. Replacement and independent version/hash verification succeeded while Winamp was stopped. The previous DLL is preserved in the ignored local installation archive. This records that specific replacement; future normal-installation changes need a new instruction.

Public documentation and commit messages are English. UI language follows Winamp, with German and English supported. Do not create branches, rewrite tags or publish another release without instruction.

## Playback and playlist behavior

MP3/M4A responses use bounded progressive private disk buffers, starting playback at 128 KiB or short-response completion. Upstream downloading is independent of decoder consumption. Guarded byte-exact continuation of interrupted full GETs requires known length, an identical strong ETag, the same origin/path/token and exact HTTP 206 ranges, with at most three follow-up requests. Initial seeking responses are excluded from that continuation path. Persistent failures remain explicit.

Saved playlists retain exact alias/server/track/part mappings under CurrentUser DPAPI. Only the requested stale track is resolved before playback resumes. FLAC/OGG/ADTS-AAC selections enter the playlist as protected deferred aliases without whole-selection downloads. Individual local-format preparation completes before its decoder opens; AAC is repackaged into M4A without re-encoding.

At most one predicted successor is prepared ahead. Actual Previous/Next/EOF selection belongs to Winamp; in-place filename replacement preserves its shuffle table. Foreground playback and library handoffs preempt speculation. Unused restored targets are released; revisiting an evicted entry resolves and downloads it again. Local files and web radios retain their original addresses and playback behavior even when unrelated Plex references are unavailable.

Streaming byte prefetch remains sequential, with one cancellable worker, a 33.5 MB per-file limit and at most current/next cached files. The playlist resolver additionally prepares one successor predicted by native Winamp, including shuffle. This is a session cache, not portable or offline playback. Preserve metadata, artwork, timeline, cancellation, same-process HTTP-grant security and mixed-playlist behavior.

## Diagnostics and storage

Settings → Diagnostics refreshes every second. Received/sent MB accumulate over the proxy connection; reserved media storage and next-track cache are current values. Active downloads count streaming producers. A zero count does not establish whether a specific track is buffered. Local disconnects are counted separately from upstream failures. Exact failed-transfer details remain in bytes.

MB means 1,000,000 bytes. Values are truncated to one decimal place, using a comma in German. Unchanged limits display as 536.8 MB per media file, 2147.4 MB for shared media storage and 67.1 MB for streaming prefetch. The [architecture table](architecture.md#storage-units-and-limits) records exact byte limits and separate registry/artwork/configuration budgets.

Both language variants were visually inspected without clipping, and their timer refresh and converted limits pass native checks. The host regression rejects the obsolete unit display. An initial local Release observation test reported remaining Windows IME windows; its separate recheck and both CI configurations pass without weakened guards. No resource-leak fix is claimed.

## Validated scope and remaining integration cases

Native library nodes, artist/album/track and audio-playlist browsing, asynchronous search/sort/pagination, genre/year/codec filters, saved searches and restored navigation are implemented. Settings open without launching sign-in; browser approval, secure connection selection and Advanced manual HTTPS settings remain available. Classic, Modern, Bento and Big Bento have visual evidence, including native search-field alignment and complete diagnostics.

Real MP3/M4A/FLAC/OGG playback, seeking and metadata have native evidence. Natural-end, interruption and backpressure tests are distinct. Native restoration tests cover mixed playlists, cancellation, shutdown during preparation, deferred enqueue, eviction/return and simulated shuffle EOF cycles. Synthetic AAC-LC is tested; real Plex ADTS, additional AAC profiles, real multipart replay, arbitrary queue/autoplay plugins, WACUP and additional skins/DPI remain unverified. One extended FLAC/OGG shuffle run rejected a truncated upstream download; do not claim that run passed.

Artwork inheritance, personal rating write/readback/restoration and Plex timeline/history have real-server evidence. Home Assistant can display player metadata through Plex; remote control remains optional future work. Further external cases include token revocation/recovery, serverless accounts, empty libraries, positive local HTTPS selection at home and actual home-server outages. Details and evidence are in [testing.md](testing.md).

## Environment and access rules

- Use only the separate test copy under the ignored local directory, class WinampPlexTest; verify the exact executable path before interaction.
- Tests remain muted and minimized. Reveal only for necessary visual checks, then minimize; stop the test copy and clear its playlist afterward.
- Use the authorized Build Tools 2022 and checksum-pinned SDK acquisition described in [build.md](build.md).
- Access uses the application's documented user-bound DPAPI workflow. Do not search for, print or log credentials.
- Existing test authorization covers the isolated copy, Build Tools and one existing track for restored ratings and a retained history entry.
- Do not change normal Winamp or Plex/router configuration for tests. Scope installation changes to an explicit user instruction.
- Keep SDK files, private responses, local reports, configuration and build artifacts out of Git. Use explicit staging paths and retain publication secret scans.
