# Third-party components and sources

## Winamp SDK 5.55

Source: https://download.nullsoft.com/winamp/plugin-dev/WA5.55_SDK.exe

SHA-256 of the inspected installer archive:
`d88abbb9882add625e6286dc68fb7598645862a191d63577be84b72ef0870370`

The unchanged `gen_ml/ml.h` and `Winamp/wa_ipc.h` headers contain Nullsoft terms permitting use, modification and redistribution subject to retaining the notice, accurate attribution and marking altered versions. These terms are commonly classified as zlib-style. The original notice in each header is authoritative. Headers are obtained locally and are not distributed in this repository. The project's MIT License does not replace their terms.

Other SDK examples and components have their own terms; the header license does not imply permission to redistribute the whole SDK. No material from the Winamp source tree published in 2024 is used.

`gen_tray/WINAMPCMD.H` supplies original numeric commands, particularly `WINAMP_BUTTON2`. This definitions-only header has no separate license notice. It is obtained locally from the original SDK and is not redistributed. No SDK example program is incorporated.

Album-art integration also uses unchanged Wasabi/Agave headers from SDK 5.55, listed with checksums in `cmake/WinampArtworkSdk.cmake`. These headers do not share a uniform separate license notice and are obtained locally only. No general redistribution permission is asserted. The `coverdirectory` example is read as an interface reference and is not copied into the project.

`Winamp/ipc_pe.h` provides the original playlist editor interface for inserting M3U8 files. This interface-only header has no separate license notice. It is extracted unchanged from the same verified SDK archive, checked by SHA-256 and not redistributed. No independent redistribution license is inferred.

## Published 2016 skin interface header

Source: https://getwacup.com/sdk/gen_ml/ml_ipc_0313.h

SHA-256: `922e6d21b0b8f2e9fe536456c87e51e05bba82b579ec6eaa77264cbdb25c4e42`

This unchanged header supplies skin interfaces and original structures, including the 48-byte `MLSKINNEDPOPUP` contract. It is obtained locally and not redistributed. It has no separate license notice; no redistribution permission is inferred. The plugin calls the host's skin engine through its interfaces without incorporating SDK example code.

## Published 2013 Nullsoft IPC header

Source: https://getwacup.com/sdk/Winamp/wa_ipc.h

SHA-256: `cb86f3409cfdd4c578c9ab8cf4b5d37339c2a4ab052f5490f46eb899574171b8`

The unchanged header carries the Nullsoft 1997-2013 permissive notice allowing use and alteration while retaining attribution. It is downloaded locally for the documented playback IPC contracts and is not redistributed. It does not establish WACUP runtime compatibility.

## nlohmann/json 3.12.0

Source: https://github.com/nlohmann/json/releases/tag/v3.12.0

License: MIT, see https://json.nlohmann.me/home/license/ and the notice in the downloaded package. CMake checks the release archive's SHA-256. Binary distributions must include its license notice. No separate JSON runtime DLL is required.

## Windows and build tools

WinHTTP, Winsock, BCrypt and DPAPI are Windows system interfaces. Microsoft tools and Windows SDK files are not redistributed with the source project. Binary redistribution must respect any applicable Microsoft Visual C++ runtime terms.

## Reference projects

- SDK `ml_xmlex`: initialization, Unicode tree nodes and dialog lifecycle reference. No example implementation was copied into product code.
- [Plexa](https://github.com/rupisaini123/plexa): behavioral reference for browser sign-in, server selection, library navigation and media gateway design. No project-wide LICENSE file was found in the inspected tree. Its source is not copied or represented as MIT-licensed project code; interfaces are implemented independently using official Plex documentation.
- [Python PlexAPI](https://github.com/pushingkarmaorg/python-plexapi): cross-check of API paths and media-part keys. Neither a runtime dependency nor a source-code incorporation.
