# Validation and integration checklist

Baseline evidence date: November 6, 2026. This document distinguishes executed checks from requirements still awaiting a suitable environment. Local log and screenshot names refer to the ignored `.local` directory; those files may contain private display information and are not published.

## Next-track prefetch and library views

The current source passes all 18 CTest suites in Debug and Release (`tests-features-debug.log`, `tests-features-release.log`). Transport tests compare all cached bytes and exercise HEAD, seeking/open/suffix/out-of-bounds ranges, origin checks, revocation, current-track retention, foreign radio URL rejection, cleanup, incomplete speculative files, the 32 MiB prefetch limit and cancellation of a stalled prefetch. A separate fixture closes its upstream listener after prefetch: full playback and seeking still return the exact cached bytes. Existing interruption tests verify strong-ETag byte-range continuation and reject changed identity, wrong ranges/lengths and excessive retries. These are bounded fault simulations, not a prolonged outage guarantee.

`catalog_probe --filters` against the real server returns genre choices for artists/albums/tracks, album-year choices and track audio codecs (`live-filters.log`). Filtered track codecs are checked against parsed media, filtered years against actual parent-album metadata, and a nonexistent codec on album children returns no rows. Catalog fixtures check filter encoding and malformed responses. DPAPI fixtures round-trip Unicode named queries, filters, page offsets and stable selected/top/focused IDs, and reject inconsistent navigation and injected sort values.

`Test-PlexBrowseFeatures.ps1` exercises native multi-selection and real vertical scrolling, a node switch away and back, saved-view save/apply/reset/load/delete, and filter application. `-CheckPersisted` verifies selection and scroll after process exit/restart (`browse-current.log`). Tests use a separate DPAPI profile and verify the normal profile hash. The new panel was visually inspected in Classic, Winamp Modern, Bento and Big Bento, including English/German through `Test-PlexHostLanguage.ps1 -SettingsOnly -BrowseFeatures`. Local screenshots include `browse-classic-filters.png`, `browse-modern-filters.png`, and `host-language-en-browse-filters.png`.

Library layout is checked at 1024×600, 600×400 and 420×330. The narrow action row uses three proportional buttons and a shorter Filters caption. `Test-PlexViewLayout.ps1` includes a countercheck that moves the filter button outside its parent, requires rejection, restores it and validates the good layout (`layout-countercheck.log`). It examines each control's own visibility flag, so a minimized host cannot silently skip all checks. Screenshot capture temporarily shows the target test window because Winamp can hide its library while minimized; `PrintWindow` otherwise returns an all-black image despite reporting success. Rendered screenshots were inspected after the correction. This does not verify every skin or DPI configuration.

The regular DLL's muted native test uses an MP3 followed by M4A (`Test-PlexPlaylistReplay.ps1 -Product -Prefetch -Radio -SkipNegative`, `native-prefetch-current.log`). Winamp consumes prefetched data for the next track and seeks in it. Saved-list restart, IPC start, editor menu/double-click/Enter, automatic advancement after seeking, shuffle with repeat, and local/radio/Plex mixtures pass. Radio tests preserve foreign URLs/order, exercise ICY metadata, observe no Plex credential headers, and leave the source playlist unchanged. The maximum polled UI diagnostic in this run is below 1000 ms. The test copy exits; the normal DLL and Plex profile hashes are unchanged. The shuffle check enables repeat so the end of a shuffled cycle does not invalidate the transition fixture, and polls for playback progress rather than assuming a two-second startup. This run skips the older replay negative-case group; transport/configuration negatives remain covered by CTest.

Prefetch prepares only an owned physical next entry when shuffle is off. Queue plugins can choose a different item; their order is never modified. Current-file retention and on-demand playback remain available. There is no persistent offline cache, no speculative copy of prepared FLAC/OGG/AAC files, and no promise of playback beyond available data during persistent network loss. These features are included in version 0.3.0.

## Controlled handle-growth investigation

The SDK lifecycle executable reproduces process-resource growth without running the Winamp executable or loading its core or `gen_ml.dll`. It supplies mock IPC and Wasabi services to the real plugin DLL. A Winamp-core defect cannot directly account for growth inside this synthetic process. Windows controls, COM and the plugin remain separate possible contributors. No plugin leak correction is claimed by this investigation.

By user decision, aggregate process-handle counts are diagnostic values, not pass/fail criteria, in the SDK host, standalone and catalog probes, and native resource scripts. Windows-runtime effects are the working explanation for these observations, not a proven attribution of every handle. GDI/USER checks, plugin service/window teardown, owned-resource counterchecks and functional failures remain enforced. PSS snapshots report handle types and thread-start module basenames; object names and full module paths are not printed. PSS does not provide creation stacks here. Baselines vary with process launch and inherited standard handles, so compare deltas within each process. The recorded failures below describe the measurements made with the original aggregate-count criterion.

Executed comparisons use the local version-0.2.0 DLL and isolated synthetic configuration, without Plex credentials or audible playback:

| Comparison | Observed result | Local evidence |
| --- | --- | --- |
| Five unchanged CTest lifecycle runs | All five pass the original 100-cycle guard | `handles-ctest-controlled-capture.log` |
| Parallel Release suite | 15 suites pass, including the strict lifecycle guard | `handles-controlled-parallel-release.log` |
| Fast dialogs after normal warmup | 153 handles remain unchanged through 100 unloads | `handles-fast-after-warmup.log` |
| One CPU core | 153 handles remain unchanged through 100 unloads | `handles-sdk-single-core.log` |
| External UI Automation client | 128 dialog queries; 158 handles remain unchanged | `handles-uia-controlled-held1-stderr.log`, `handles-uia-held1-run.log` |
| Slower dialogs after normal warmup, Release | 153 to 154 at cycle 39, 156 after 100 unloads and message pumping | `handles-sdk-slow-after-warmup.log` |
| Slower dialogs after normal warmup, Debug | 153 to 154 at cycle 39, 179 at cycle 57, 181 at cycle 77; 181 remain after 100 unloads and 30 additional seconds of message pumping | `handles-sdk-slow-debug-idle.log` |
| Slower dialogs, Release with peak snapshots | Already 178 handles at warmup, 181 after 100 unloads and 30 seconds of pumping | `handles-sdk-slow-peaks-release.log` |
| Same slow Debug workload, IME disabled only on the synthetic UI thread | 150 to 153 handles; CRT allocation deltas zero, USER/GDI guards pass; handle-growth guard still fails | `handles-sdk-without-ime-debug.log` |
| Native dialogs without the plugin, 500 slower cycles | 140 to 141 at iteration 111, returning to 139 by iteration 500 | `handles-native-dialog-slow-control.log` |
| Windows dispatcher queues without the plugin, 500 create/shutdown cycles | 103 to 134 at the first cycle, unchanged through cycle 500, 133 after one second idle | `handles-isolated-dispatcher.log` |

The high SDK baseline includes `CoreMessaging.dll`, `CoreUIComponents.dll` and `wintypes.dll`; these are absent from the low-baseline module inventory and the IME-disabled comparison. The large increase and those modules can therefore appear during warmup rather than after many cycles. The independent dispatcher probe calls the documented [CreateDispatcherQueueController](https://learn.microsoft.com/en-us/windows/win32/api/dispatcherqueue/nf-dispatcherqueue-createdispatcherqueuecontroller), completes `ShutdownQueueAsync` with message pumping, checks its result and releases its interface references each time. Its stable final count establishes a one-time Windows-runtime increase in that probe, not a per-queue leak. The IME comparison uses [ImmDisableIME](https://learn.microsoft.com/en-us/windows/win32/api/imm/nf-imm-immdisableime) before creating the first test-thread window; the normal plugin and other applications' input methods are unchanged. Neither comparison proves the exact origin of the original 24-handle failure. Aggregate growth alone is intentionally excluded from regression failures.

The slow Debug run also fails the USER-object guard, 10 to 11. Remaining thread windows include `MSCTFIME UI` and `IME` alongside the synthetic host window. The first additional thread starts in `ntdll.dll`. The final inventory contains two additional handles referring to the UI thread and two Windows-started worker threads. This is not a creation-stack attribution. At cycles 25, 50, 75 and 100, and after the settling period, the shared Debug CRT reports zero additional normal/client allocation blocks or bytes. That result covers these synthetic paths and CRT allocation classes; it does not establish freedom from every native, COM or playback leak.

`resource_probe` is a non-shipping executable for independent comparisons. Thread creation, sequential and overlapping COM apartments, the real view reaper, DPAPI with synthetic bytes, native dialogs and Text Services Framework activation each complete 500 cycles without handle growth in their respective runs. Racing eight COM threads through 20 balanced initialization/uninitialization pairs per thread instead increases handles from 105 to 130 on the first iteration. The count remains 130 through 500 iterations and 30 seconds of message pumping (`handles-com-race-idle.log`). This proves that process growth is possible without the plugin, but its inventory is not identical to the original intermittent 24-handle increase. It must not be treated as proof of the original cause.

The diagnostic countercheck holds an event open, verifies that PSS sees its exact handle, closes it and verifies its removal. Debug additionally detects a deliberate 4096-byte CRT allocation and verifies its release. `resource_diagnostics` runs these counterchecks through CTest. API definitions follow the Windows SDK and Microsoft documentation for [PssCaptureSnapshot](https://learn.microsoft.com/en-us/windows/win32/api/processsnapshot/nf-processsnapshot-psscapturesnapshot), [PSS_HANDLE_ENTRY](https://learn.microsoft.com/en-us/windows/win32/api/processsnapshot/ns-processsnapshot-pss_handle_entry) and balanced [CoInitializeEx](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-coinitializeex) calls.

The diagnostic-tool baseline passes all 16 regular suites in both Debug and Release, plus seven package checks (`handles-investigation-current-ctest-debug.log`, `handles-investigation-current-ctest-release.log`, `handles-investigation-packaging.log`). The aggregate-count policy adds `plugin_lifecycle_handle_observation`: a deliberately held, subsequently released event must be observed without failing the lifecycle test. Diagnostic event/heap counterchecks still reject incorrect measurements or incomplete fixture release. The Release plugin DLL remains byte-identical, SHA-256 `fce89737a4bf18a884a9eadb353c28be56bb26276e33d1778bfc120ce3be0260`; no product implementation, installed DLL or release tag is changed by these diagnostic additions.

The policy build passes all 18 suites in Debug and Release (`handle-policy-ctest-debug.log`, `handle-policy-ctest-release.log`). The lifecycle fixture reports `observed_handle_growth=1` and succeeds. `resource_policy` exercises six synthetic native-script comparisons: unchanged resources, handle-only growth, permitted GDI/USER growth, excessive GDI, excessive USER and simultaneous GUI growth despite a falling handle count. Isolated counterchecks accept the healthy policy and reject three modified policies that reintroduce a process-handle gate or omit either GUI counter (`handle-policy-counterchecks-current.log`). The dispatcher probe reports growth and exits successfully (`handle-policy-dispatcher.log`). The catalog probe builds in both configurations and reports its handle delta without a threshold failure. These checks do not rerun the live catalog, native view-switch or foreign-plugin scripts; they exercise the shared native-script policy without opening Winamp or contacting Plex.

Reproduce from the project directory after building the diagnostic targets:

```powershell
cmake --build build --config Debug --target plugin_host_tests resource_probe --parallel
& .\build\Debug\plugin_host_tests.exe "$PWD\build\Debug\ml_plex.dll" --diagnose-handles
& .\build\Debug\plugin_host_tests.exe "$PWD\build\Debug\ml_plex.dll" --diagnose-handles-slow-after-warmup
& .\build\Debug\resource_probe.exe --com-race
```

The slow option changes the settings-inspection timer from 25 to 250 ms after the first cycle and adds a bounded 30-second message-pumping comparison after unloading. Other host options are `--diagnose-handles-fast`, `--diagnose-handles-fast-after-warmup`, `--diagnose-handles-single-core`, `--diagnose-handles-without-ime`, `--stress-lifecycle` and `--simulate-handle-growth`. Standalone probe options are `--verify`, `--threads`, `--com`, `--overlapping-com`, `--com-race`, `--dispatcher`, `--reaper`, `--dpapi`, `--text-input`, `--dialogs` and `--dialogs-slow`. Probes report handle growth without turning that observation into failure; API, timeout and cleanup failures still fail. These tools do not replace the native Winamp resource comparisons below.

## Version 0.2.0 release checks

The local versioned DLL identifies 0.2.0 in its PE resource, plugin description and Plex request header. Its SHA-256 is `fce89737a4bf18a884a9eadb353c28be56bb26276e33d1778bfc120ce3be0260`. The muted native process-restart test confirms renewed playback, exact metadata, artwork bindings, timeline delivery, no TranscodeSession and automatic advancement; the normal installation/profile hashes remain unchanged (`release-020-native.log`). Debug and Release serial runs each pass 15 suites, and seven package checks pass (`release-020-ctest-serial.log`, `release-020-ctest-debug.log`, `release-020-packaging.log`).

The initial local Release run fails the strict handle guard at cycle 34, with 181 handles versus baseline 157 (`release-020-ctest.log`). A comparison run with the preserved preceding diagnostics DLL passes 100 cycles (`release-020-previous-dll-lifecycle.log`). The serial versioned-build runs also pass without weakening the guard. This does not identify the cause of the intermittent increase or establish that parallel execution caused it. Release notes retain that limitation. Tag-triggered publication additionally requires both CI configurations and the exposure scan to pass.

[Release workflow 42205798124](https://github.com/Eitschman/winamp-plex-library/actions/runs/42205798124) passes both configurations, 15 suites each, seven package checks and the history scan. The downloaded v0.2.0 ZIP matches SHA256SUMS.txt and contains exactly the five distribution files, with source-identical instructions/notices and an x86 version-0.2.0 DLL. The published DLL SHA-256 is `9791740e15944b2aee320e57736b9fb917b1ef03214e383ef27ba827984ddd8f`. That exact DLL also passes the muted native restart/metadata/artwork-binding/timeline/no-transcode/automatic-next checks. An endless HTTP/ICY fixture verifies foreign playback with a damaged Plex index, mixed-source transitions and saved mixed-list restart: four radio requests, zero credential headers, unchanged foreign addresses and source file (`release-020-published-native.log`). Normal installation/profile hashes remain unchanged. Downloaded content, DLL strings and complete CI logs pass redacted scans.

## Regular DLL: saved-playlist restoration

The user authorized integrating the successful feasibility result into `ml_plex.dll`. Native tests run `scripts/Test-PlexPlaylistReplay.ps1 -Product` with the separate probe absent. They copy the protected connection profile into an ignored test-only directory with a separate client identifier, add two real tracks through the regular library UI, export Winamp's M3U8, close Winamp, start a different process and reload the unchanged file. The normal DLL and original Plex profile hashes remain unchanged. Tests are muted/minimized in Winamp 5.9.2 Build 10042.

`replay-product-native-faults.log` confirms MP3 playback after restart, renewed targets, exact title/artist/album/albumartist metadata, artwork bindings, successful timeline delivery and a real Plex session without TranscodeSession. Seeking near the first track's end leads to automatic playback of the second. Reimport/start via IPC_STARTPLAY, editor Play selection, actual double-click, Enter and shuffle pass. A mixed list preserves its unrelated local entry and resumes index 1. Twelve replacements are observed. The largest polled UI query in this run is 497 ms. The saved source hash remains unchanged.

The same run deliberately stops an in-flight resolution and changes its playlist: neither case resumes playback or replaces entries. A damaged protected registry produces an explicit error before playback, with zero replacements. Protected fixtures referring to another server or an unknown alias return access denied or not found, also before playback with zero replacements. Closing during resolution joins the worker. Restoring the valid registry then passes the positive sequence. The test activates the actual error-dialog button and waits for closure; error reporting prevents nested duplicate dialogs.

`replay-product-native-flac-m4a.log` confirms the prepared-file branch after restart: FLAC is downloaded again to a new private file, M4A receives a new proxy target, separate metadata fields match, artwork bindings and timeline delivery exist, the Plex session has no TranscodeSession, and automatic transition to M4A passes. `replay-product-native-ogg-m4a.log` independently passes the same sequence with real OGG. This is not an overnight-duration test: process teardown invalidates old targets immediately, while the protected registry has no age-based expiry.

`replay-product-native-shared-flac-final.log` additionally rejects a duplicate prepared-file handoff with a deliberately damaged index. The existing file and artist binding remain intact, with no extra playlist entry. After restoring the index, the real FLAC/M4A process-restart sequence passes; maximum polled query is 420 ms. A separate attempt reports premature upstream EOF (Windows 38) during re-download and refuses playback rather than handing over a truncated file (`replay-product-native-shared-flac.log`). No automatic recovery from that upstream interruption is claimed.

The endless MP3 radio fixture exercises actual Winamp HTTP/ICY decoding, not just URL retention. With a damaged Plex index, radio/local Previous/Next still plays and performs zero Plex replacements. With the valid index, radio → local file → Plex → Plex → radio plays in order and replaces only the two Plex entries. Winamp exports that mixed list; a new process reloads it, plays the radio independently of the damaged index and restores Plex after the index is repaired. The fixture records four HTTP requests with ICY metadata negotiation and zero credential headers; addresses, ordering and saved source hash remain unchanged (`replay-product-radio-verified.log`). The pre-correction DLL fails the foreign-source check (`replay-product-radio-negative.log`). The wait assertion requires resolution to finish, so a still-playing previous decoder cannot satisfy the requested Plex start.

The `replay_references` CTest checks ordered durable lookups, unknown aliases, idempotent writes, ciphertext without plaintext IDs/paths, conflicting assignment, forbidden remote/query URLs, ordinary loopback streams and local filenames, invalid track/part paths, cancellation, concurrent writers, corrupt DPAPI, unknown schema, malformed protected identities, reference-count limits and a healthy countercheck. Debug and Release each pass 15 suites, including this test and the original 100-cycle plugin lifecycle test. One Release run fails the existing handle guard at cycle 73 (181 versus baseline 157); its targeted repeat and subsequent full Release run pass without relaxing the assertion. This intermittent resource observation remains unresolved (`ctest-replay-product-release-final.log`, `ctest-replay-product-lifecycle-repeat.log`, `ctest-replay-product-release-verified.log`).

Limits: same Windows user/profile and selected server; up to 10000 playlist entries during restoration; 100000 retained aliases / 64 MiB registry; existing 512 MiB file / 2 GiB shared-media budget. When Plex restoration is required, all stale owned entries are prepared before resuming, so large local-format queues can take time or fail the quota explicitly. Pre-feature entries without recorded IDs need to be added from the library once. There is no title-based reconstruction. Keep the registry with saved playlists. These runs do not establish overnight wall-clock execution, portable/offline playlists, native ADTS replay, real multipart replay, arbitrary startup/autoplay or queue-override plugins, a maximum-size restored queue or reentrant mutation by another plugin. Restored artwork bindings are counted; these runs do not repeat every skin's visual cover-field inspection. The SDK host additionally checks that an unrelated playlist with 10001 entries can still receive Play.

## Isolated saved-playlist feasibility module

The optional `playlist_replay_probe` target is a separate, non-shipping Media Library DLL. It is restricted to the WinampPlexTest class and the executable directory `.local/winamp-test`. The `playlist_replay_isolation` CTest rejects an unrelated class and a matching class outside that executable directory. The positive case is exercised only in the authorized real test copy. Source is under `tests`, and release packages exclude this DLL.

`scripts/Test-PlexPlaylistReplay.ps1` bootstraps two uniquely identified Plex tracks, lets Winamp export its real M3U8 playlist, closes the process, reloads the unchanged file in a new process and requests playback. A DPAPI reference index survives the restart; temporary targets do not. Server identity, TLS and current authentication are checked again before resolving track IDs. Tests are muted and minimized; the normal DLL and Plex profile hashes are checked before/after.

Executed MP3 evidence (`replay-native-mp3-final.log`):

- With the resolver DLL removed, the saved playlist makes no audio progress for ten seconds, maximum position 0 ms.
- With the resolver present in a fresh process, both stale entries are rewritten before decoder start and playback advances past 1500 ms.
- Seeking near the end of the first track provokes a natural automatic transition and progress on the second, without waiting through two whole songs.
- Reloading the same saved file works through IPC_STARTPLAY, editor Play selection, row double-click and Enter. Shuffle/Next also continues playback.
- A mixed playlist preserves its unrelated local entry and resumes the requested owned entry at index 1. Twelve owned-entry replacements are confirmed across the tested loads; the saved source file hash remains unchanged.

The full MP3 run's maximum polled UI query is 403 ms. An earlier test of only the main-window gate could not start through the editor; the explicit editor gates are included in the tested module. That observation is why a main Play-command hook alone is insufficient.

The prepared-file branch is separately verified with real FLAC and M4A (`replay-native-flac-m4a-current.log`). After exit/restart, the FLAC file is prepared again and both entries are replaced. Playback and the automatic transition to M4A pass; the maximum polled UI query is 430 ms. Bootstrap discovery filters artist, album and format to reject ambiguous title matches. This is not an overnight-duration test: a real process teardown invalidates the old targets immediately, while the protected reference mapping remains on disk without an age-based expiry.

The probe is deliberately limited and omits the regular implementation's registry, edit cancellation and service rebinding described above. OGG and ADTS replay are not separately verified here. Normal Winamp remains on the diagnostics DLL; the test module is removed and the test process exits with its playlist cleared after each run.

## Live buffer diagnostics

Settings → Diagnostics refreshes every second in the native skinned panel. The SDK host checks German/English labels, zero values without playback, absence of URLs/tokens and restoration of a deliberately replaced snapshot on WM_TIMER. Disabling the refresh handler makes that regression fail at `skinned_diagnostics_without_credentials` (exit 8); the handler is restored (`ctest-diagnostics-refresh-negative.log`). The native settings test also checks the real timer rather than sending a timer message.

Both language variants were visually inspected without clipping in the isolated Big Bento host (`host-language-en-diagnostics.png`, `host-language-de-diagnostics.png`; `diagnostics-native-languages-settings.log`). Muted MP3 playback showed 8.8 MiB received, 0.7 MiB sent, zero active downloads and 8.8 MiB reserved while playback continued (`diagnostics-playing.log`). A prepared FLAC response showed 59.4 MiB reserved with no streaming proxy (`diagnostics-prepared-display.log`); its 30-second playback, seeking and metadata checks passed (`diagnostics-prepared-playback.log`). These values are shared storage/session counters, not per-track cache percentages.

Current Debug and Release runs each pass 13 suites (`ctest-diagnostics-debug-current.log`, `ctest-diagnostics-release-current.log`). Two earlier Release attempts failed the existing process-handle guard at 181 versus baseline 157, after cycles 49 and 4. A targeted repeat and the subsequent full run passed; the cause of that intermittent resource observation remains unresolved. The initial broader language test also failed the existing single-library dropdown assertion; the focused `-SettingsOnly` run passes both languages and restores German. Neither observation is evidence for every library layout or resource condition.

After restoring the refresh handler, the Release lifecycle check passes again; seven packaging checks and the redacted change scan also pass (`ctest-diagnostics-refresh-restored.log`, `diagnostics-packaging.log`, `diagnostics-secret-scan.log`). The isolated host stops, clears its playlist and exits normally. The user separately authorized replacing the normal Winamp DLL; its installed SHA-256 matches the tested Release build (`diagnostics-user-installation-admin.log`). The previous DLL is preserved, and normal Winamp is left closed for user testing.

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

The current validated baseline contains 18 suites: plugin lifecycle, lifecycle handle observation, resource diagnostics, resource policy, view status, view reaper, WinHTTP callbacks, AAC remux, playback reporter, transport, config/auth, catalog, artwork, artwork provider, feedback, security, protected replay references and replay-probe isolation. Earlier referenced runs had fewer suites; their counts are not current totals.

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
| Held cancellation event, aggregate count observation | 159 versus 158 handles in cycle two, `lifecycle-handles-negative.log`; aggregate counts are now diagnostic only |
| Omitted table skin release | Native GDI growth, `view-resources-negative.log` |
| Invalid CDB output despite exit 0 | `handle-summary-negative.log` |
| Injected 96-event aggregate increase | `catalog-resources-negative.log`; aggregate counts are now diagnostic only |
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
- [x] Resource observations; GDI/USER limits remain enforced and process handles are diagnostic only.

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

Tables release gen_ml skin resources at WM_DESTROY before control WM_NCDESTROY. Removing that release reproduces per-table GDI growth. SDK lifecycle measurement records process handles after every unload against the warmed baseline without treating aggregate growth as a failure.

Foreign-plugin integration used a temporary, hash-checked ml_local.dll only in the closed test copy, removing it afterwards. Five mixed-view cycles passed functionally but added three GDI objects per cycle. Foreign-only audio/video switching added four per cycle independently of Plex views. This remains a target-host observation (`host-integration-fixed.log`, `host-resources-foreign-only.log`, `host-resources-mixed-fixed.log`). RequireStableResources turns GDI/USER growth into an explicit failure; aggregate process handles remain diagnostic values.

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
