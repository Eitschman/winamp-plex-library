# Working state and continuation

## Current work package

The user authorized publishing version 0.2.0 after testing the installed playlist build. [Release v0.2.0](https://github.com/Eitschman/winamp-plex-library/releases/tag/v0.2.0) is published from `ebf7d7ed7935ecaeacdd8ef7fa66e5acad5a89dd`. [Release notes](releases/0.2.0.md) describe progressive buffering, live diagnostics and saved-playlist restoration, with upgrade instructions and limits. CMake, PE version resources, plugin description and Plex request version identify 0.2.0. [Release workflow 42205798124](https://github.com/Eitschman/winamp-plex-library/actions/runs/42205798124) passes both x86 configurations, 15 suites each, seven package checks and the full-history exposure scan. Downloaded assets are verified below. The normal installation retains its separately authorized native-tested build, SHA-256 `699c0a599b70c929f2825bc73522f08b6868546c844c0693007713d652047507`; versioning the release does not authorize another installation replacement.

Saved-playlist references retain exact alias/server/track/part mappings under CurrentUser DPAPI and are persisted before host handoff. Native MP3, FLAC/M4A and OGG/M4A restart checks pass with metadata, artwork bindings, timeline delivery and automatic advancement. The full MP3 sequence covers editor Play/double-click/Enter, IPC start, shuffle and a mixed list. An endless HTTP/ICY fixture confirms independent radio/local playback with a broken Plex index and saved mixed-list restart without foreign-address changes or credential headers. Counterchecks cover cancellation, edits, shutdown, damaged references, wrong server, unknown aliases and preservation of an already bound prepared file after a rejected duplicate handoff. See [testing.md](testing.md) for evidence, limits and intermittent failures. [Actions run 42204159031](https://github.com/Eitschman/winamp-plex-library/actions/runs/42204159031) passes 15 suites in both configurations, seven package checks and the exposure scan for the integrated feature build.

The user requested visible buffer usage under Settings → Diagnostics. The panel now refreshes every second with cumulative received/sent MiB, active streaming downloads and shared reserved media storage. German/English native screenshots and real timer refresh pass. Muted MP3 playback demonstrates completed upstream buffering while Winamp still reads; prepared FLAC storage is included without a proxy. The diagnostics checks pass 13 suites each in Debug/Release, and a disabled-refresh countercheck fails. Keep the intermittent resource-guard and broader library-UI test observations in [testing.md](testing.md) explicit. These diagnostics are included in the installed build recorded below.

The user resumed work on November 6, 2026. Implement a bounded media buffer that decouples upstream Plex downloading from Winamp's consumption rate, while preserving authentication, TLS validation, byte-exact resumption, seeking and controlled teardown.

The buffer is implemented in the regular transport/private-store code. Debug and Release each pass 13 suites with 178 transport checks, including complete upstream retrieval while the decoder temporarily does not read, a paused HTTP 206 response, cancellation, cleanup and shared quotas. Deliberately recoupling producer and consumer fails the independence assertion; that countercheck is removed. Muted native MP3 and M4A runs reach their natural endings at 273 and 251 seconds. Both also pass seeking, metadata and no-TranscodeSession checks. The native fault series passes healthy, 401, 403, 404, redirect, truncated, stalled and healthy-again cases. Do not treat these changes as part of v0.1.0 or install them into the normal Winamp automatically.

The implementation uses a progressive private disk buffer per active response, with a 128 KiB startup threshold and shared private-media quotas of 512 MiB per file and 2 GiB overall. Producer lifetime is bounded by four active proxy workers. It does not implement persistent offline caching or next-track prefetch. The isolated test copy receives the current Release build for each native run and remains separate from the normal installation. Seven package checks pass. The received/expected diagnostic label was inspected in the real skinned dialog.

Native evidence: `buffer-puppeteer-natural-end.log`, `buffer-m4a-natural-end.log`, `buffer-seek-faults-native.log` and `buffer-settings-current.log`. Automated evidence: `ctest-buffer-range-release.log`, `ctest-buffer-range-debug.log`, `transport-buffer-coupled-negative.log` and `buffer-packaging-current.log`. Source/history and DLL strings pass redacted scans. Follow-up work must preserve these guards and must not imply a new normal-installation authorization.

The buffer code is pushed at cf1f4f9. [Actions run 42189390597](https://github.com/Eitschman/winamp-plex-library/actions/runs/42189390597) passes the exposure scan, Debug/Release 13 suites each, and seven package checks. Its downloaded development package contains exactly the expected files and source-identical English installation/notices. Build logs, package text and DLL strings pass redacted scans. CI DLL SHA-256: `328c6c155883d2fc3c858b253d3bbdc6c5118d4fba230c92f57f636e1be05dad`. No new version tag was created. The separately authorized normal-installation replacement is recorded below.

All 14 public Markdown documents and visible Actions text are now English, published separately as `9906e3d`. Relative links, Actionlint, seven package checks and redacted history/tree scans pass. [Actions run 42187506113](https://github.com/Eitschman/winamp-plex-library/actions/runs/42187506113) succeeds. Published v0.1.0 release notes match the English source. The application continues to offer German/English based on Winamp's language pack.

The user requires English for all new commit messages as well. Follow that convention for subsequent checkpoints and public commits. The isolated test copy was stopped, its saved playlist cleared and its process exited normally after the buffer checks (`buffer-native-shutdown.log`).

**Today's tests must be muted and run in the background.** Keep the isolated Winamp minimized, revealing only briefly for necessary visual checks. Stop and clear its playlist afterwards. There is no new daily cutoff specified. Prior one-day cutoffs do not apply automatically.

## Release and repository

Public repository: https://github.com/Eitschman/winamp-plex-library. Publishing its contents and GitHub Actions is explicitly authorized. AndreasHT has collaboration access; administrative account settings remain with the owner. Use scoped local checkpoints and preserve the original repository history. Do not create branches or rewrite release tags without instruction.

The latest v0.2.0 ZIP is 321928 bytes, SHA-256 `acc7e799b2fecc827e22c25be12212ea15a8aa8c660297557bc630fbc6fe7cc5`. Its checksum matches the downloaded SHA256SUMS.txt. The five files are the x86 DLL, INSTALL.md, LICENSE, THIRD-PARTY-NOTICES.md and the nlohmann/json license; text files match the release source. The independently built published DLL identifies version 0.2.0 and has SHA-256 `9791740e15944b2aee320e57736b9fb917b1ef03214e383ef27ba827984ddd8f`. Downloaded contents, extracted ANSI/UTF-16 DLL strings and complete release-workflow logs pass redacted scans. Local source/history scans cover 75 commits and report no findings. These records do not change the explicitly authorized installed DLL recorded below.

[v0.1.0](https://github.com/Eitschman/winamp-plex-library/releases/tag/v0.1.0) is published at source `3644c12c1649a03d130d71c2a20ff848e969d744`. Its release workflow is [42071402195](https://github.com/Eitschman/winamp-plex-library/actions/runs/42071402195). Publication and downloaded asset verification are now confirmed, rather than pending.

The 276,607-byte ZIP matches the published SHA-256:
`b0e92f4eb86a00474871603e39c6152d714c7c85061f7fe09d97fb3d66ed6fe7`.
Its five files are the x86 DLL, INSTALL.md, LICENSE, THIRD-PARTY-NOTICES.md and the nlohmann/json MIT license. The DLL is 564,224 bytes.

CI builds MSVC x86 Debug and Release, runs 15 suites in each and seven package checks for Release. It also performs a redacted, checksum-pinned Gitleaks full-history scan. Source, history, current changes, CI logs and package content were scanned without findings; synthetic positive and clean negative cases were checked. Scope and limitations are in [SECURITY.md](../SECURITY.md).

The downloaded earlier CI DLL passed native progress, duration, seeking, metadata and no-TranscodeSession checks in a repeat. The first run failed to locate its own Plex session despite passing playback; that cause remains unresolved.

## Playback stability baseline

Premature completion was reproduced on “The Puppeteer”: 129.8 seconds instead of 273, with incomplete source bytes. Full GET responses now support at most three guarded byte-exact continuations, requiring known length, a strong identical ETag, same origin/path/token and an exact HTTP 206 range. Initial HTTP 206 seeking responses are excluded. Numeric failure history survives recovery.

A real Puppeteer run reached natural end after an actual upstream interruption. “Until Dawn” also reached natural end at a decoder duration of 369 seconds, observed elapsed time 368.3 seconds. Artist/title formatting is present in playlists and scrolling display while raw metadata fields remain separate.

The user explicitly authorized installing the tested saved-playlist DLL into normal Winamp on November 6. The target installation is Winamp 5.9.2.10042; no Winamp process was running before replacement. Windows administrator elevation succeeded for the protected Plugins directory. The installed DLL independently matches the native-tested local Release build, SHA-256:
`699c0a599b70c929f2825bc73522f08b6868546c844c0693007713d652047507`.
The replaced diagnostics DLL is preserved under `.local/installation-rollback-replay-20261106/ml_plex.dll`, SHA-256:
`faa098180cad37211210fd7ec5f9cfb7b81bb7454da8e4a54f12f2fc26d625a2`.
Winamp was left closed for the user's own testing. Evidence: `replay-user-installation-admin.log`. **This authorization covered one replacement only; future replacements require their own authorization.**

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
