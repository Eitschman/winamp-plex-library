# Plex API contract

The primary source is the [official PMS API](https://developer.plex.tv/pms/). Documented contracts and explicitly identified reference findings are distinguished below. Listing an endpoint does not imply that its corresponding product feature is implemented or verified on a real server.

## Authentication

1. Generate a random installation client identifier.
2. Send `POST https://plex.tv/api/v2/pins?strong=true` with `Accept: application/json`, `X-Plex-Product` and `X-Plex-Client-Identifier`. The response includes `id`, `code`, `expiresIn` and initially no access token.
3. Open `https://app.plex.tv/auth#?...` in the system browser with the client ID, PIN code and product name. The plugin does not process account passwords.
4. Poll `GET /api/v2/pins/{id}` at plex.tv about once per second until `authToken` appears, the request expires or the user cancels.
5. Use the account token for `GET https://clients.plex.tv/api/v2/resources?includeHttps=1&includeRelay=1&includeIPv6=0`.
6. Offer only resources providing `server`. Resources contain `clientIdentifier`, `name`, `accessToken` and `connections[]`. Resource tokens may differ from the account token.
7. Probe advertised HTTPS connections in the background, preferring local, direct remote and relay in that order. Query `/identity` without authentication and require `machineIdentifier` to match the selected resource. Only then send its token to `/library/sections`. Custom origins use the same identity check.
8. Store profiles, connections, selected access and library using Windows DPAPI. The account token is not needed persistently after discovery. Only the sign-in button starts another browser authorization.

Plex also documents JWT/device-key authentication and renewal. This plugin uses the officially documented strong PIN flow. Expired or revoked server access requires a visible sign-in action rather than silently trying other tokens.

## Server and music

| Purpose | Endpoint or response structure | Usage |
| --- | --- | --- |
| Identity | `GET /identity`, `MediaContainer.machineIdentifier` | Compared before authenticated access. |
| Capabilities/version | `GET /` | Researched; no comprehensive capability model. |
| Libraries | `GET /library/sections`, `MediaContainer.Directory[]`: key, title, type | Music libraries have `type=artist`. |
| Artists | `/library/sections/{id}/all?type=8` | Native paginated search/sort view. |
| Albums | `/library/sections/{id}/all?type=9` | Native paginated search/sort view. |
| Tracks | `/library/sections/{id}/all?type=10` | Native paginated view and playlist handoff. |
| Metadata | `/library/metadata/{ratingKey}`, `MediaContainer.Metadata[]` | Targeted media resolution. |
| Children | `/library/metadata/{ratingKey}/children` | Artist albums and album tracks. |
| Audio playlists | `/playlists?playlistType=audio`, `/playlists/{id}`, `/playlists/{id}/items` | Overview, current detail counts and Plex order; empty/populated cases tested. |
| Artwork | Relative `thumb` under `/library/metadata/{id}/thumb`, optionally with numeric revision | Authenticated original image plus WIC thumbnail; inherited `parentThumb` for tracks. |
| Media bytes | `Media[].Part[].key`, e.g. `/library/parts/46618/1389985872/file.flac` | WinHTTP proxy/private preparation; never use the server-local `file` field. |
| Search/sort | Typed `all` queries with supported filters and `sort` | Check capabilities rather than inventing query syntax. |
| Filter values | `/library/sections/{id}/genre?type=8` (or 9/10), `/library/sections/{id}/year?type=9`, `/library/sections/{id}/audioCodec?type=10` | `MediaContainer.Directory[]` contains `key` and `title`. Values are loaded on a worker; failures remain visible. |

Genre keys, album `year` / track `album.year`, and track `audioCodec` are URL-encoded query predicates, combined with title search, sorting and pagination. They filter the library on PMS, not only the currently visible page. The actual server was checked for artists/albums/tracks, audio-codec equality, album-year equality via parent metadata, and rejection of a nonexistent codec on album children. Source contracts: [official section audio codecs and media queries](https://developer.plex.tv/pms/) and [Python-PlexAPI filter-choice implementation](https://python-plexapi.readthedocs.io/en/latest/_modules/plexapi/library.html#LibrarySection.listFilterChoices). Other server/provider versions still require integration evidence; unsupported endpoints are errors, without silently removing filters.

Tracks may contain multiple Media variants and Parts. Selection must be reproducible; concatenating arbitrary part URLs is not an implementation. API durations use milliseconds; playlist IPC uses seconds.

For tracks, `grandparentTitle` supplies album artist and nonempty `originalTitle` supplies a differing track artist. The [PlexAPI track reference](https://github.com/pkkid/python-plexapi/blob/master/plexapi/audio.py) reads them separately. The model retains `album_artist`; absent a nonempty track override, the album artist is used. Winamp hooks return separate `artist` and `albumartist`. A real compilation-track test compares these against API fields without editing metadata.

Track-artist sorting uses `originalTitle`; album-artist sorting uses `artist.titleSort`. The probe reads `all?includeMeta=1&includeAdvanced=1&X-Plex-Container-Start=0&X-Plex-Container-Size=0`. The real server advertises `originalTitle` under `MediaContainer.Meta.Type[].Sort[].key`, matching the [PlexAPI listSorts implementation](https://python-plexapi.readthedocs.io/en/latest/_modules/plexapi/library.html#LibrarySection.listSorts). `catalog_probe --sorts` prints technical keys only, including possible comma-separated compound keys. Evidence: `plex-sort-capabilities.log`.

## Pagination

Send `X-Plex-Container-Start` and `X-Plex-Container-Size` together. Actual page size may differ from the request. Use returned offset, size, optional totalSize and applicable response headers. Empty results must terminate iteration. An unpaginated response must not be repeatedly appended. Loading every record is not a substitute for demand-driven views.

## JSON and XML

The client explicitly requests JSON and rejects missing or incorrectly typed required fields. No XML processing path is implemented. Add one only for a demonstrated server requirement, with DTD and entity resolution disabled and tested. XML must not conceal HTTP, authentication or parser errors. [Official XML examples](https://support.plex.tv/articles/201638786-plex-media-server-url-commands/) are references for such a future compatibility test.

## Playback feedback and writes

`POST /:/timeline` sends `key=/library/metadata/{id}`, ratingKey, state, position and duration in milliseconds. The official API calls for state changes and progress approximately every ten seconds. `X-Plex-Client-Identifier` identifies the client. Real Winamp play, pause, resume, seek and stop were read back through `GET /status/sessions`. A first report may show zero while buffering; actual progress is checked independently at the player.

`GET /status/sessions/history/all?metadataItemID={id}&sort=viewedAt%3Adesc` provides filtered history. A native test compares the newest historyKey and metadata viewCount before and after a natural ending; both changed for the authorized test track. That test seeks near the end and then allows a natural stop. It does not establish universal counting rules for skipped or briefly played tracks. No additional scrobble request is sent.

`PUT /:/rate?identifier=com.plexapp.plugins.library&key={ratingKey}&rating={0..10}` sets a personal rating. Subsequent metadata must confirm userRating. A real test set and restored the initial rating. Zero means unrated in the plugin; whole stars map to Plex values by multiplying by two, while existing fractions remain unrounded in the model. The native action operates on one track and exposes errors or missing confirmation. It does not modify playlists or library content.

Plexa references `/music/:/transcode/universal/start.mp3`. Required parameters for this Winamp client are not verified, so no automatic transcoding attempt exists.

`GET /status/sessions` lists active playback. The [PlexAPI reference](https://github.com/pkkid/python-plexapi/blob/master/plexapi/base.py) reads TranscodeSession directly under each media object. The integration probe first identifies its own active Player.machineIdentifier, then requires no TranscodeSession. A missing player is not Direct Play evidence. Native MP3 play/pause/resume/seek checks pass, and synthetic counterchecks reject an injected transcode session and a missing player. Evidence: `timeline-no-transcode-native.log`, limited to the tested session and sampling times.

## Errors and security boundaries

401/403 mean denied access; 404 means absent media/metadata; 416 means an unsatisfiable range. Redirects are not followed. Other upstream failures become an empty local HTTP 502. Response bodies, cookies and Location headers do not reach Winamp. UI messages use classified codes rather than tokens or raw API responses.

Each request is bound to a preselected origin. Media paths must be validated relative Plex Part paths. Absolute URLs, network paths, embedded credentials, query tokens and header injection are rejected. Authentication supplied by the local HTTP client is never forwarded to Plex.
