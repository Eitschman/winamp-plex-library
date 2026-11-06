# Winamp Plex Library: requirements and development plan

This is a maintained working document, not an immutable copy of the initial prompt. Evidence is recorded in [testing](../docs/testing.md), and contracts/sources in [research](../docs/research.md) and [Winamp interfaces](../docs/winamp-sdk.md). Public project documentation and all new commit messages are written in English. German and English remain supported UI languages.

## Binding project decisions

- Tracks must play to their natural end without an artificial time limit. Decoder backpressure and pause must not terminate a stream. Native natural-end tests are separate from seeking, backpressure, shutdown and incomplete-request timeout tests.
- Interrupted full media GETs may resume byte-exactly from the same file, with known length, a strong identical ETag, the same server/path, exact HTTP 206 range and remaining length. At most three follow-up requests are allowed. No alternate format, decoder or server is selected on failure. Initial HTTP 206 seeking responses require separate resumption evidence.
- Numeric failure history survives recovery and subsequent healthy playback. A real interruption/resumption reached natural end; persistent outages remain errors.
- The playlist/current display contains artist and title. Title, artist and albumartist metadata stay separate.
- Saved Winamp playlists must retain Plex identity across process restarts so the same Windows user can play them on a later day. The regular DLL persists exact playback alias/server/track/part mappings with CurrentUser DPAPI before host handoff and resolves current media/authorization on a worker. Gate main/editor playback and automatic advancement, verify playlist and connection state before replacement, cancel on Stop or edits, and restore metadata, covers and timeline bindings. Preserve HTTP-grant expiry and media quotas. The bounded registry keeps old mappings without silent eviction. Do not infer lost Plex IDs from display titles. See architecture/testing for implemented limits and evidence.
- Mixed playlists retain local files and web radios with their original addresses and positions. An explicitly selected foreign source must start without reading Plex references, even if unrelated Plex entries are stale or their protected index is damaged. Previous/Next and saved mixed-playlist reload need native regression evidence; third-party queue overrides remain a separate compatibility question.
- Plexa is the behavioral reference for browser sign-in, discovery and music access. Use Plex's strong PIN flow; passwords are entered only at Plex. Manual token input is not a normal workflow.
- Settings open without launching sign-in. Normal settings contain sign-in, server and library selection. Fixed routes and custom HTTPS addresses belong under Advanced. Automatic routing prefers reachable local HTTPS, direct remote access, then relay, and displays the actual route.
- Language comes only from Winamp's active language pack. There is no plugin language selector or saved language preference.
- The library header identifies the music library. Only multiple libraries require a dropdown. Root views do not repeat the tree title. Context headings and Back belong to subviews; paging uses Previous page and Next page.
- Winamp supplies playback controls. No separate player toolbar, Alexa endpoint or permanent external service is included. Home Assistant remote control is optional future work, not an acceptance requirement.
- Tokens are still technically required for API access. The plugin obtains them after browser approval, encrypts them with Windows DPAPI and sends them only as headers to the selected verified server.
- MP3 and existing M4A use expiring loopback URLs. FLAC, OGG and separate ADTS-AAC use private files prepared in the background before handoff. AAC changes container to M4A without re-encoding. Neither path exposes tokens in playlists.
- Queued streaming tracks retain same-process session access after exported capabilities expire, verified through Windows TCP ownership rather than HTTP assertions. Foreign-link validity stays two hours. Provoke expiry with short fixtures; do not run a two-hour waiting test.
- Source, history, logs and packages must be checked for secret exposure before publication. CI repeats a redacted history scan. Access data, private responses and local audit reports are not published.
- Runtime tests use the explicitly authorized separate Winamp copy. Build Tools 2022 installation was authorized. The normal installation/configuration is not a test target. The prior DLL replacement was a one-time authorization.
- Today's automated playback tests must be muted and in the background. Keep the test host minimized; reveal briefly only for necessary visual/mouse checks, then minimize. Stop and clean its playlist afterwards.
- A bounded streaming buffer decouples PMS downloading from decoder consumption using private progressive response files. Playback starts at 128 KiB or short-response completion. Buffers share prepared-media quotas, and each active worker has at most one producer. Preserve authentication, TLS, byte identity and cancellation. Validate slow reading, pause, interruption, seeking, quotas and shutdown. The local buffer changes are not included in v0.1.0; do not claim them as released.

## Verified baseline

- MSVC x86 builds, original SDK export/descriptor, Plex tree nodes, native views, regular exit/restart and 100 SDK load cycles.
- Browser sign-in approved by the user, HTTPS identity checks, music library discovery and DPAPI persistence.
- Synthetic transport, ranges, cancellation, URL security and classified failures.
- Controlled background view teardown; native stalled-TLS navigation in 2 ms and a 100-switch resource test within stated limits. Debug/Release and an independent Release build pass 13 suites.
- Real MP3 handoff through documented WM_COPYDATA/M3U8 and SDK title/play commands, with seeking, duration and token-free playlists.
- Real MP3/M4A/FLAC/OGG progress, duration, seeking and metadata. Synthetic AAC-LC uses the same preparation path with unchanged compressed payload. Real Plex ADTS and other AAC profiles remain unverified.
- Private media limits of 512 MiB per file and 2 GiB overall; regular exit removes prepared files/session directory.
- Typed queries, pagination, search, sorting, virtual tables, multiple/album handoff, audio playlists and bounded caches. Real artist → album → track → playlist navigation is tested.
- Classic, Winamp Modern, Bento and Big Bento inspected visually, without custom player controls.
- Real “Grand Serpent Rising” artwork and 13 tracks checked. Tracks inherit parentThumb. Real missing-artwork and positive-image counterchecks exist; both Big Bento cover fields display supplied covers.
- Personal ratings write/readback and restoration, actual timeline states, a history entry and increased play count were verified on an authorized test track.
- WACUP remains a future target without runtime evidence. Keep host integration separate from Plex and transport code.

## 1. Project goal

Develop a native Media Library plugin for classic Winamp on Windows:

| Property | Requirement |
| --- | --- |
| Project | Winamp Plex Library |
| DLL | `ml_plex.dll` |
| Platform | Windows 10/11, x86 |
| Host | Winamp 5.9.2 Build 10042 x86, April 26, 2023 |
| Language | C++17 or later if SDK-compatible |
| Build | CMake and MSVC |
| Outcome | Usable Plex music browsing and playback inside Winamp's Media Library |

The authorized public repository is https://github.com/Eitschman/winamp-plex-library in the owner's personal account. AndreasHT has collaboration access. Do not create other repositories or modify unrelated accounts.

Work autonomously through coherent, testable packages. A scaffold or feasibility study does not satisfy the goal.

## 2. Core requirements

Winamp must recognize the DLL as a Media Library plugin and show a Plex navigation node. Connect to an existing PMS without requiring server changes. Discover/select music libraries; show artists, albums and tracks; play through Winamp; support audio playlists where reliable; provide search/filtering, responsive operation, explicit failure handling, secure settings and German/English UI.

No permanent external service is required. The authorized in-process loopback proxy handles authentication safely.

## 3. Research requirements

Before implementation, verify interfaces using primary documentation and existing open-source references.

### Winamp

Investigate SDK availability/license, Media Library API, winampMediaLibraryPlugin, node registration/deletion, ML_MSG_TREE_ONCREATEVIEW and related events, native child views, IPC, playlist URLs/metadata, target-version compatibility, ABI issues, lifecycle and threading. Use reference plugins but do not invent structures, constants or signatures. Record SDK sources and legal limits if unavailable; never reconstruct an interface by assumption.

### Plex

Verify identity, authentication, libraries, artists, albums, tracks, playlists, metadata, artwork, media files, Direct Play, transcoding, playback status, ratings and library changes. Prefer direct PMS communication. Use JSON where supported; add XML only for a demonstrated compatibility need, with secure parser settings and explicit validation. Verify response structures through official documentation or anonymized real examples.

### Audio

Investigate authenticated URL handling, HTTPS, MP3/FLAC/AAC/M4A/OGG support, seeking, duration/metadata, revoked/expired tokens, proxy authentication and avoiding unnecessary transcoding. Compare direct URLs with the proxy for security, reliability and implementation cost. Tokens must never reach ordinary logs, error text or exported playlists.

## 4. Architecture

Separate plugin, Plex client, playback, UI, configuration, cache and common validation code, with public headers, resources, tests, scripts and documentation. Explain justified structure changes.

### Plugin core

Own initialization/lifecycle, Media Library integration, navigation/events, errors and cleanup.

### Plex client

Use WinHTTP or a justified alternative with TLS validation, token headers, timeouts, cancellation, classified errors, validated parsing, pagination and URL encoding. Handle large libraries. No blocking networking on the UI thread.

### Models

Provide typed server, library, artist, album, track, playlist, media-part and connection-settings models. Identify objects through stable Plex IDs and keep API models separate from UI rows.

### Playback

Support individual tracks, album/playlist playback, appending multiple tracks, media resolution, authentication and errors. Use Winamp's decoders unless a demonstrated technical necessity requires otherwise. Preserve correct metadata and seeking. Insertion after the current track must preserve playback state while respecting shuffle/queue-plugin behavior.

### Configuration

Persist server origin/connection, selected library, cache and relevant UI preferences. Language comes from Winamp and is not persisted separately. Store access using DPAPI or an equivalent Windows mechanism. No hardcoded credentials, personal server addresses or personal data.

## 5. User interface

Integrate functionally and visually with Media Library, including Classic and Modern skins. Use native controls and verified Winamp skin interfaces.

The Plex tree contains Artists, Albums, Tracks and Playlists. Offer a meaningful choice when multiple music libraries exist.

Artist lists are alphabetical and navigate to associated albums. Albums show title, artist, year, track count and available artwork; double-click prepares album playback.

Tracks show title, artist, album, duration, track number, year and format when available. Support sorting, multiple selection and context menus.

Actions include Play now, Insert after current track, Add to playlist, Play album and Play selected tracks. Search artists/albums/tracks asynchronously.

Settings provide browser sign-in/status, server and library selection, connection testing, cache clearing and diagnostics. Fixed routing/custom HTTPS belong under Advanced. Opening Settings must not initiate authorization.

## 6. Performance and stability

Support thousands of albums and tens of thousands of tracks through asynchronous requests, pagination, demand loading, reduced redundant queries, thread-safe state and controlled workers. Release resources and prevent exceptions crossing ABI boundaries. Unreachable PMS must not block or crash Winamp. Shutdown must cancel and join all work.

## 7. Security

No credentials in source, repository or ordinary logs. Encrypt storage, retain TLS certificate validation, prevent cross-origin credential forwarding and unvalidated authenticated redirects. Bind proxy only to loopback, use bounded temporary capabilities, and open no unnecessary ports. Verify Winamp URL persistence explicitly. Private prepared media must have restricted access and bounded cleanup/storage.

## 8. Development phases and acceptance

### Phase 1: research and proof of concept

Build an x86 DLL that the target host recognizes, lists in plugin management, shows as Plex in the tree and opens as a native view. Confirm clean exit, then real Plex-track playback before proceeding. If the environment is unavailable, record open tests without claiming runtime success.

### Phase 2: connection

Implement configuration, authentication, testing, discovery and errors. Identify a valid server, list music libraries, explain invalid access and survive networking failures.

### Phase 3: views

Implement artist/album/track views, sorting, search, navigation and context menus. Data must display correctly; large results stay usable; navigation avoids reloading everything.

### Phase 4: playback

Implement tracks, albums, multiple-track handoff, correct metadata, seeking and interruption handling. Test distinct formats. Prefer Direct Play.

### Phase 5: extensions

Implement reliable audio playlists, artwork, metadata caching, library refresh, status, history and ratings. Plex-specific effects count as verified only after API readback demonstrates them.

### Phase 6: quality assurance

Check startup/exit, repeated loading, network interruption, invalid access, empty/large libraries, Unicode, both UI languages, missing files, HTTPS, malformed responses and resource management.

## 9. Tests

Automate independent components: API parsing/models, URLs, pagination, errors, configuration, caches and security-sensitive URL processing. Use mock responses and invented credentials only. Maintain a manual checklist for the exact target host. A green build alone does not establish native behavior or visual quality.

## 10. Build and installation

Support Visual Studio 2022/CMake x86 with output `build/Release/ml_plex.dll`. Document prerequisites, SDK/dependencies, configuration, Debug/Release, installation, removal and troubleshooting. Validate the actual target Plugins directory rather than assuming one. Do not automatically install into an existing Winamp without explicit authorization.

## 11. Documentation

Maintain English README, research, architecture, API, SDK, build and testing documents. Cover purpose, support scope, features, prerequisites, setup, known limits, build instructions, evidence and unverified cases. Keep diagrams, responsibility/threading/error/security/playback descriptions consistent with validated code. Do not describe unfinished buffer changes as released behavior.

## 12. Git and publication

Use Git and a suitable C++/Visual Studio/CMake ignore file. Do not commit access data, tokens, personal server addresses, local settings, unapproved binaries or SDK material without redistribution rights.

The public personal repository, README and GitHub Actions are authorized. MIT applies to project code; dependency terms remain separate. SDK/license review is required before adopting/distributing third-party code. Changes and pushes must remain scoped; do not overwrite a release tag.

## 13. Working method

1. Research before implementation and use verified SDK/API definitions.
2. Build regularly and resolve compiler errors.
3. Do not represent placeholders or assumptions as tested functionality.
4. Preserve source comments, document decisions and keep dependencies small.
5. Prefer maintainable fixes at the cause over hidden alternate behavior.
6. Preserve unrelated user changes and normal Winamp/Plex configuration.
7. Obtain explicit authorization for destructive or unrelated external actions.
8. Maintain coherent local Git checkpoints according to the applicable development instructions.
9. Continue authorized work without routine permission requests.
10. Keep documentation current and distinguish tested, untested, planned and blocked work.

## 14. Reporting

After substantial work, report implemented behavior, relevant files, build/test results, limits, blockers and next steps briefly. Name actual validation and its scope; do not infer universal stability from a few runs.

## 15. Acceptance criteria

The first usable release requires an x86 build that loads in the target host, shows Plex, configures access, discovers libraries, displays artists/albums/tracks, plays tracks and albums to completion, populates the playlist correctly, handles failures, encrypts access and exits without crashing. Build instructions must be reproducible and documentation maintained.

Features lacking a suitable real environment remain explicitly open integration cases. Release acceptance remains with the user.

## 16. Continuing work

Prioritize reliable end-to-end music playback within Winamp over optional conveniences. Continue from the maintained working state rather than repeating completed research or scaffolding.
