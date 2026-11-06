# Technical research

Documentation, reference-code findings and runtime observations are distinguished below. Detailed native evidence is in [testing.md](testing.md). The initial research was performed on November 3, 2026.

## Winamp SDK

The historical [Nullsoft SDK 5.55](https://download.nullsoft.com/winamp/plugin-dev/WA5.55_SDK.exe) was downloaded over HTTPS and extracted with 7-Zip without running the installer. It includes `gen_ml/ml.h`, `Winamp/wa_ipc.h` and Media Library examples such as `ml_xmlex`, `ml_http` and `mlExplorer`. The two principal headers contain permissive Nullsoft terms. SDK binaries are not committed.

The [published WACUP SDK headers](https://getwacup.com/sdk/) distinguish ABI versions `0x15`, `0x16` and `0x17`. Winamp 5.66 and later add a service pointer and Unicode description, while retaining explicit support for the older `0x15` ANSI contract. This project uses checksum-pinned SDK 5.55 definitions, without reconstructed structures or WACUP-specific functions.

`ml_xmlex/main.cpp` registers a Unicode node with `MLTREEITEMW` and `ML_IPC_TREEITEM_ADDW`. Its `xmlview.cpp` handles `ML_MSG_TREE_ONCREATEVIEW` only for its own ID and creates a child dialog. These are interface references; lifecycle and threading for this project are implemented independently.

The inspected local executable reports `5,9,2,10042`; its Media Library and MP3, FLAC, MP4 and Vorbis input plugins are present. Interfaces were additionally tested in the actual isolated Winamp copy. This does not establish compatibility with other Winamp versions or WACUP.

Skinned menus require the unchanged published 2016 `gen_ml/ml_ipc_0313.h` header and its 48-byte popup structure. The 40-byte SDK 5.55 version does not open a popup in the target host. Sources, hashes and ABI evidence are in [winamp-sdk.md](winamp-sdk.md).

The SDK `coverdirectory` example documents the album-art provider contract and host-memory release through `api_memmgr`. Its source is not copied. The tested HTTP tracks require the `EMBEDDED` provider category and PNG data; both Big Bento player cover fields show the artwork. Media files are not modified.

## Plex and Plexa

The [official PMS API](https://developer.plex.tv/pms/) documents JSON/XML, authentication headers, libraries, metadata and pagination. JSON is explicitly requested through `Accept`. The [Plex support article](https://support.plex.tv/articles/201638786-plex-media-server-url-commands/) supplies additional XML examples.

[Plexa authentication](https://github.com/rupisaini123/plexa/blob/main/src/plex/auth.ts) uses a persistent client identifier, a strong PIN and browser sign-in, then discovers resources and stores the selected server's token. This is the required behavioral reference. Passwords stay with Plex; manual token copying is not a user workflow.

Plexa's [adapter](https://github.com/rupisaini123/plexa/blob/main/src/plex/adapter.ts) and media gateway inform pagination, album-track resolution, playlist contents and Direct Play. Alexa, public gateway endpoints, its Node service, web UI and key management are not incorporated. This plugin runs inside Winamp, stores access through DPAPI and forwards media locally.

The URL review on November 4 confirmed that `getMediaParts` reads metadata `Media`/`Part`, while `buildDirectStreamUrl` combines the server origin, part key and query token. The [gateway](https://github.com/rupisaini123/plexa/blob/main/src/media/gateway.ts) separately creates signed expiring paths and streams from PMS with byte ranges. [Alexa playback](https://github.com/rupisaini123/plexa/blob/main/src/alexa/playback.ts) passes the gateway URL into `AudioPlayer.Play` with separate metadata and a queue token. That queue token is not a Plex access token. Plexa therefore also uses a proxy. This project uses an explicitly selected in-process loopback proxy and upstream authentication headers, without adopting Plexa's redirects or transcoding behavior.

Account access and server access are distinct. The account token discovers resources; only the selected resource token reaches the verified server origin. Server `machineIdentifier` is checked. Plex advertises secure resource URIs; [Plex's secure-connection documentation](https://support.plex.tv/articles/206225077-how-to-use-secure-server-connections/) explains why changing a private hostname from HTTP to HTTPS may produce a certificate-name error.

## Playback alternatives

| Path | Evidence and limits | Decision |
| --- | --- | --- |
| Token in a Plex URL passed to Winamp | SDK documents playlist URL persistence; tokens could be exported. | Excluded. |
| Direct HTTPS URL without a token | Playlist IPC has no arbitrary authentication-header field; decoder-specific alternatives are unverified. | No reliable authentication contract. |
| In-process loopback proxy | WinHTTP adds authentication; Winamp receives a random local grant. Real MP3 seeking and metadata were verified. | Selected, with separate format validation. |
| Audio transcoding | Plexa references `/music/:/transcode/universal/start.mp3`; no equivalent client-specific proof exists here. | No automatic transcoding. |

MP3 and M4A passed native HTTP playback, duration and seeking. The target FLAC decoder requires a local file. HTTP OGG playback reports zero duration and rejects seeking even when ranges are correct. Prepared private files provide the required FLAC/OGG behavior. Decoder presence alone proves neither HTTP/HTTPS nor range or container compatibility.

A revoked token must surface as HTTP 401/403. The proxy does not forward redirects, authenticated URLs or response bodies to Winamp. It does not transcode or switch credentials on access denial.

## Separate ADTS-AAC files

Microsoft's [supported formats](https://learn.microsoft.com/en-us/windows/win32/medfound/supported-media-formats-in-media-foundation) list ADTS input and MPEG-4 output. The [MPEG-4 sink](https://learn.microsoft.com/en-us/windows/win32/medfound/mpeg-4-file-sink) supports `MEDIASUBTYPE_RAW_AAC1`; [AAC media types](https://learn.microsoft.com/en-us/windows/win32/medfound/aac-media-types) and `MF_READWRITE_DISABLE_CONVERTERS` define the contract. The plugin passes compressed packets without creating an encoder or decoder.

ADTS fields were cross-checked against FFmpeg's primary `libavcodec/adts_header.c` and `libavcodec/bsf/aac_adtstoasc.c` references, without incorporating their code. [ID3v2.4](https://id3.org/id3v2.4.0-structure) defines separately accounted metadata lengths. Synthetic AAC-LC bitstreams remain identical after container conversion; real Plex ADTS files and other profiles remain unverified.

## Threading and remaining questions

[WinHTTP handle closure](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpclosehandle) requires callback context to survive the final `HANDLE_CLOSING` notification. Safe DLL unload additionally waits for [WINHTTP_OPTION_UNLOAD_NOTIFY_EVENT](https://learn.microsoft.com/en-us/windows/win32/winhttp/option-flags#winhttp_option_unload_notify_event), which confirms callbacks have returned. A deliberately blocked callback and negative countercheck validate that ordering. Requests use asynchronous WinHTTP away from the UI thread.

Real MP3, M4A, FLAC and OGG have native progress, duration, seeking and metadata evidence. Timeline, history and ratings were read back through the API. Shortened grant expiry was tested with the real decoder; this does not represent a two-hour real-time run. A 100-view-switch test satisfies its stated resource limits, not a general proof against leaks. Real token revocation, home-network local HTTPS routing, home-server outages and additional environments remain separate integration cases.
