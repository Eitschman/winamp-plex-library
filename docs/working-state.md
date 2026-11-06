# Working state and continuation

## Current work package

The user requests saved Plex playlists that can be played after restarting Winamp on a later day. The authorized feasibility test is implemented only in the separate `playlist_replay_probe` test target. Native MP3 restart/import, main Play, IPC_STARTPLAY, editor selection, double-click, Enter, automatic next, shuffle and mixed-playlist checks pass. Removing the resolver prevents audio progress. A second native FLAC/M4A run passes restart, newly prepared local media and automatic next. The original saved file, normal DLL and Plex DPAPI profile remain unchanged; the probe is removed from the test copy after each run. See [testing.md](testing.md) for evidence and prototype limits. Production persistence remains open; do not describe it as shipped or install a new normal DLL for this test.

The user requested visible buffer usage under Settings → Diagnostics. The panel now refreshes every second with cumulative received/sent MiB, active streaming downloads and shared reserved media storage. German/English native screenshots and real timer refresh pass. Muted MP3 playback demonstrates completed upstream buffering while Winamp still reads; prepared FLAC storage is included without a proxy. Current Debug/Release runs pass 13 suites each, and a disabled-refresh countercheck fails. Keep the intermittent resource-guard and broader library-UI test observations in [testing.md](testing.md) explicit. The user separately authorized installing this diagnostics DLL in normal Winamp; its installed hash matches the tested Release build as recorded below.

The user resumed work on November 6, 2026. Implement a bounded media buffer that decouples upstream Plex downloading from Winamp's consumption rate, while preserving authentication, TLS validation, byte-exact resumption, seeking and controlled teardown.

The buffer is implemented in the regular transport/private-store code. Debug and Release each pass 13 suites with 178 transport checks, including complete upstream retrieval while the decoder temporarily does not read, a paused HTTP 206 response, cancellation, cleanup and shared quotas. Deliberately recoupling producer and consumer fails the independence assertion; that countercheck is removed. Muted native MP3 and M4A runs reach their natural endings at 273 and 251 seconds. Both also pass seeking, metadata and no-TranscodeSession checks. The native fault series passes healthy, 401, 403, 404, redirect, truncated, stalled and healthy-again cases. Do not treat these changes as part of v0.1.0 or install them into the normal Winamp automatically.

The implementation uses a progressive private disk buffer per active response, with a 128 KiB startup threshold and shared private-media quotas of 512 MiB per file and 2 GiB overall. Producer lifetime is bounded by four active proxy workers. It does not implement persistent offline caching or next-track prefetch. The current test-copy DLL matches the Release build, SHA-256 `616576076668969eda8e2f85ea3ec7fee61f0b5b88bff44d02693276a4f140bd`. Seven package checks pass. The received/expected diagnostic label was inspected in the real skinned dialog.

Native evidence: `buffer-puppeteer-natural-end.log`, `buffer-m4a-natural-end.log`, `buffer-seek-faults-native.log` and `buffer-settings-current.log`. Automated evidence: `ctest-buffer-range-release.log`, `ctest-buffer-range-debug.log`, `transport-buffer-coupled-negative.log` and `buffer-packaging-current.log`. Source/history and DLL strings pass redacted scans. Follow-up work must preserve these guards and must not imply a new normal-installation authorization.

The buffer code is pushed at cf1f4f9. [Actions run 42189390597](https://github.com/Eitschman/winamp-plex-library/actions/runs/42189390597) passes the exposure scan, Debug/Release 13 suites each, and seven package checks. Its downloaded development package contains exactly the expected files and source-identical English installation/notices. Build logs, package text and DLL strings pass redacted scans. CI DLL SHA-256: `328c6c155883d2fc3c858b253d3bbdc6c5118d4fba230c92f57f636e1be05dad`. No new version tag was created. The separately authorized normal-installation replacement is recorded below.

All 14 public Markdown documents and visible Actions text are now English, published separately as `9906e3d`. Relative links, Actionlint, seven package checks and redacted history/tree scans pass. [Actions run 42187506113](https://github.com/Eitschman/winamp-plex-library/actions/runs/42187506113) succeeds. Published v0.1.0 release notes match the English source. The application continues to offer German/English based on Winamp's language pack.

The user requires English for all new commit messages as well. Follow that convention for subsequent checkpoints and public commits. The isolated test copy was stopped, its saved playlist cleared and its process exited normally after the buffer checks (`buffer-native-shutdown.log`).

**Today's tests must be muted and run in the background.** Keep the isolated Winamp minimized, revealing only briefly for necessary visual checks. Stop and clear its playlist afterwards. There is no new daily cutoff specified. Prior one-day cutoffs do not apply automatically.

## Release and repository

Public repository: https://github.com/Eitschman/winamp-plex-library. Publishing its contents and GitHub Actions is explicitly authorized. AndreasHT has collaboration access; administrative account settings remain with the owner. Use scoped local checkpoints and preserve the original repository history. Do not create branches or rewrite release tags without instruction.

[v0.1.0](https://github.com/Eitschman/winamp-plex-library/releases/tag/v0.1.0) is published at source `3644c12c1649a03d130d71c2a20ff848e969d744`. Its release workflow is [42071402195](https://github.com/Eitschman/winamp-plex-library/actions/runs/42071402195). Publication and downloaded asset verification are now confirmed, rather than pending.

The 276,607-byte ZIP matches the published SHA-256:
`b0e92f4eb86a00474871603e39c6152d714c7c85061f7fe09d97fb3d66ed6fe7`.
Its five files are the x86 DLL, INSTALL.md, LICENSE, THIRD-PARTY-NOTICES.md and the nlohmann/json MIT license. The DLL is 564,224 bytes.

CI builds MSVC x86 Debug and Release, runs 13 suites in each and seven package checks for Release. It also performs a redacted, checksum-pinned Gitleaks full-history scan. Source, history, current changes, CI logs and package content were scanned without findings; synthetic positive and clean negative cases were checked. Scope and limitations are in [SECURITY.md](../SECURITY.md).

The downloaded earlier CI DLL passed native progress, duration, seeking, metadata and no-TranscodeSession checks in a repeat. The first run failed to locate its own Plex session despite passing playback; that cause remains unresolved.

## Playback stability baseline

Premature completion was reproduced on “The Puppeteer”: 129.8 seconds instead of 273, with incomplete source bytes. Full GET responses now support at most three guarded byte-exact continuations, requiring known length, a strong identical ETag, same origin/path/token and an exact HTTP 206 range. Initial HTTP 206 seeking responses are excluded. Numeric failure history survives recovery.

A real Puppeteer run reached natural end after an actual upstream interruption. “Until Dawn” also reached natural end at a decoder duration of 369 seconds, observed elapsed time 368.3 seconds. Artist/title formatting is present in playlists and scrolling display while raw metadata fields remain separate.

The user explicitly authorized installing the tested diagnostics DLL into normal Winamp on November 6. The target installation is Winamp 5.9.2.10042; no Winamp process was running before replacement. Windows required administrator approval for the protected Plugins directory. The installed DLL independently matches the native-tested local Release build, SHA-256:
`faa098180cad37211210fd7ec5f9cfb7b81bb7454da8e4a54f12f2fc26d625a2`.
The replaced buffer DLL is preserved under `.local/installation-rollback-diagnostics-20261106/ml_plex.dll`, SHA-256:
`616576076668969eda8e2f85ea3ec7fee61f0b5b88bff44d02693276a4f140bd`.
Winamp was left closed for the user's own testing. Evidence: `diagnostics-user-installation-admin.log`. **This authorization covered one replacement only; future replacements require their own authorization.**

Plexamp reference research supports background buffering, retry behavior, disk caching and preloading:
- https://forums.plex.tv/t/plexamp-constantly-pausing/675610
- https://forums.plex.tv/t/plexamp-macos-always-re-caches-tracks-even-when-downloaded/910747
- https://forums.plex.tv/t/plexamp-pre-caching-speed-limit-causes-playback-to-buffer/897623

Exact current Plexamp retry limits and ETag handling are not established by those public sources. They do not justify copying undocumented behavior or hiding errors.

## Validated product scope

- Native target-version Media Library nodes, virtual paginated tables, search/sort, multiple selection, artist/album/track navigation and Plex audio playlists.
- Browser sign-in only after explicit action; secure automatic routing, server/library selection and Advanced manual HTTPS settings.
- Language inherited from Winamp; user-bound DPAPI persistence without plaintext tokens.
- Classic, Modern, Bento and Big Bento visually inspected; no extra player toolbar.
- Insert-after-current checked for order, metadata and preserved playback/pause in both languages and all four skins.
- List/player artwork, inherited track covers, a real missing-image track, and “Grand Serpent Rising” with 13 tracks verified.
- Real MP3/M4A streams and prepared FLAC/OGG files pass progress, duration, seeking and metadata. Synthetic AAC-LC is repackaged losslessly; real ADTS and more profiles remain open.
- Timeline play/pause/resume/seek/stop, history/play count and restored personal rating are confirmed through the real API.
- Bounded shared library/artwork caches; clear, disable, revisions, TTL and counted reuse tested.
- Home Assistant displays player metadata, cover and progress through Plex timeline. Remote control is explicitly optional future work.

## Lifecycle and resources

The proxy waits through full decoder buffers using nonblocking sockets and checks stop while waiting. Backpressure, stoppable shutdown and the separate three-second incomplete-header deadline have positive/negative tests.

Expired exported capabilities remain denied to foreign processes. Same-session Winamp access requires kernel-confirmed TCP ownership. Shortened expiry tests verify native playback/seeking without a two-hour waiting run.

WinHTTP teardown waits until all callbacks return, validated with a deliberately blocked callback and countercheck. ViewReaper transfers cancelled worker state to its COM MTA thread, with a maximum of eight pending views and controlled queue drain before unload.

The latest native 100-switch resource run passes fixed limits: handles 584 → 586, GDI 1479 unchanged, USER 232 unchanged, threads 19 unchanged, private bytes 87,932,928 → 86,704,128 and TCP/UDP zero. Earlier unexplained handle/USER growth and a sporadic lifecycle observation remain documented in [testing.md](testing.md), rather than erased by later passing runs.

A stalled TLS navigation check measured 3 ms UI query, 2 ms view change and 849 ms regular shutdown. Exit during real FLAC preparation and pending browser approval also passed with the original DPAPI file unchanged.

## Environment and access rules

- Separate test copy: `.local/winamp-test`, class WinampPlexTest, private settings directory. Validate its exact executable path before interacting.
- MSVC/CMake/CTest come from the authorized Build Tools 2022 installation. Header acquisition and hashes are documented in [build.md](build.md).
- Access uses only the application's documented user-bound DPAPI workflow. Do not search for, print or log credentials.
- Existing authorization covers creation of the test copy, Build Tools installation and one existing test track for restored ratings and a retained history entry.
- Do not modify normal Winamp, Plex/router configuration or real access credentials for tests.
- Use explicit paths for Git staging/checkpoints. Keep SDK files, local reports, API responses, settings and build artifacts out of Git.

## Remaining external integration cases

Real token revocation and sign-in recovery; serverless account and empty library; positive local HTTPS selection at home; actual home-server outage during playback; real Plex ADTS and additional AAC profiles; WACUP and additional skins/DPI settings.

These require suitable external states or another runtime. Keep them explicit and do not manufacture them through unauthorized configuration changes.
