# Build, SDK and installation

## Prerequisites

- Windows 10/11 with x86 application support.
- Visual Studio 2022 or Build Tools 2022 with C++ x86/x64 tools.
- Windows SDK and CMake 3.24 or later; the Visual Studio CMake component is sufficient.
- 7-Zip to extract the SDK without running its installer.
- Access to the historical Nullsoft download, published WACUP SDK directory and GitHub for checksum-pinned dependencies.

The validated development environment uses MSVC 19.44.35229 and Windows SDK 10.0.19041.0. These are observed versions, not additional minimum requirements.

## Obtain SDK headers

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Fetch-WinampSdk.ps1 `
  -SevenZip "C:\Program Files\7-Zip\7z.exe"
```

Verify your own 7-Zip path. The script downloads `WA5.55_SDK.exe` over HTTPS, checks SHA-256 and extracts `gen_ml/ml.h`, `Winamp/wa_ipc.h`, `Winamp/ipc_pe.h`, `gen_tray/WINAMPCMD.H` and the Wasabi/Agave album-art headers into `.local/sdk`. It separately downloads the unchanged 2016 skin header into `.local/skin-sdk/gen_ml/ml_ipc_0313.h`. Existing target directories are rejected. CMake checks each required header, including the list in `cmake/WinampArtworkSdk.cmake`. Sources, checksums and ABI details are in [winamp-sdk.md](winamp-sdk.md).

For manually extracted files, use `-DWINAMP_SDK_ROOT=C:/path/to/SDK` and `-DWINAMP_SKIN_SDK_ROOT=C:/path/to/SkinSDK`. Checksums must match the inspected versions. Other revisions require separate ABI review and an explicit build-contract change.

## Configure, build and test

In a developer PowerShell, or with CMake on PATH:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The validated MSVC x86 Debug and Release configurations each pass 18 CTest suites. Tests cover protected playlist references, the original dialog procedure, controlled transport failures, WinHTTP callback teardown, diagnostic counterchecks, bounded background view cleanup on a COM-initialized thread and rejection of foreign hosts by the separate replay probe. Resource-policy tests use built-in Windows PowerShell, hold a synthetic process handle and check native GDI/USER thresholds independently of aggregate handle counts. Native Winamp tests use the Release DLL; a Debug host test does not validate skins. Optional `resource_probe` and SDK-host diagnostic modes are described in [testing](testing.md); process-handle growth is reported without determining success or failure.

With BUILD_TESTING enabled, `ml_plex_replay_probe.dll` is also built for the saved-playlist feasibility test. It requires both the WinampPlexTest window class and an executable under `.local/winamp-test`, and refuses ordinary Winamp installations. It is excluded from release packages. `scripts/Test-PlexPlaylistReplay.ps1` installs it temporarily in the isolated copy, runs muted checks, removes it, and verifies that the normal DLL and Plex profile hashes remain unchanged. This optional integration script needs an already approved Plex profile and two uniquely identifiable real titles; ordinary CTest does not need credentials.

For the regular DLL, run that script with `-Product -FirstTitle <unique-title> -SecondTitle <unique-title> -SkipNegative`; optional `-FirstArtist`, `-FirstAlbum` and `-FirstFormat` disambiguate the first sample. It copies the protected connection profile into an ignored isolated test directory, assigns a separate client identifier and hands off both titles through the real library UI. `-Faults` adds cancellation, concurrent-edit and protected-index failure checks. `-Radio` adds an endless loopback MP3 radio fixture and mixed radio/local/Plex restart checks, including foreign playback with a broken Plex index. `-SharedPrepared` requires a FLAC/OGG/AAC first sample and checks that a rejected duplicate handoff preserves its existing file and metadata. The probe DLL must be absent. Native playback remains muted/minimized, and the normal installation/profile hashes are checked afterwards. `-CoreOnly` limits repeated prepared-media downloads after restart, metadata/timeline checks and automatic advancement; requested radio checks still run. `-ProductDll` selects an explicit countercheck DLL instead of the regular Release build.

A separate `.local/repro-build` directory also passed SDK verification, a freshly fetched JSON dependency, Release compilation and all 13 suites without using regular build artifacts. Evidence includes `repro-configure.log`, `repro-view-reaper-build.log` and `repro-view-reaper-ctest.log` under ignored local logs. This validates the build procedure, not bit-for-bit DLL reproducibility.

CMake FetchContent obtains checksum-pinned nlohmann/json 3.12.0. Builds and automated tests require neither Plex credentials nor a server. Fixtures use invented tokens and loopback.

Outputs are `build/Release/ml_plex.dll` and `build/Debug/ml_plex.dll`. Do not install test executables or import libraries into Winamp. CMake does not install into an existing Winamp installation.

The Release DLL requires the current Microsoft C++ runtime for **x86**, including on 64-bit Windows. PE inspection with `dumpbin /headers /exports /dependents build/Release/ml_plex.dll` confirmed x86, the undecorated `winampGetMediaLibraryPlugin` export and imports of `MSVCP140.dll` and `VCRUNTIME140.dll` (`plugin-release-pe.log`). [Microsoft's runtime guidance](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist/) requires a redistributable at least as recent as the build tools. Debug DLLs are for development environments. No runtime installer is run automatically.

Linux can run the platform-independent security tests through CMake/CTest. It does not build the MSVC Windows DLL or replace native integration testing.

Separate ADTS-AAC preparation requires Windows Media Foundation, specifically `mfplat.dll` and `mfreadwrite.dll`. Missing components, including on Windows N, produce an error. These DLLs are loaded only for AAC and only from System32. AAC is repackaged without re-encoding. FFmpeg is a local test-data generator and independent validator, not a plugin dependency. With `BUILD_TESTING=ON`, `aac_remux_tests.exe` and `aac_remux_probe.exe` are also built.

## GitHub Actions and packages

`.github/workflows/build.yml` runs on pushes to main, pull requests and manual dispatch. Separate Windows 2022 jobs obtain checksum-verified headers and build Win32 Debug and Release with Visual Studio 2022, then run CTest. They require no Plex account, personal configuration or Winamp installation. Actions are pinned to full commit SHAs. Build permissions are `contents: read`, and checkout does not persist Git credentials.

Release validation also runs `tests/packaging_tests.ps1`. `scripts/Package-Plugin.ps1` produces the `winamp-plex-library-win32-release` artifact containing exactly:

- `ml_plex.dll`
- `INSTALL.md`
- `LICENSE`
- `THIRD-PARTY-NOTICES.md`
- `licenses/nlohmann-json-LICENSE.MIT`

Packaging rejects missing files, invalid PE data, wrong architecture and existing output directories. SDK files, Microsoft runtimes, test programs and local settings are excluded. Obtain the Microsoft x86 runtime separately.

After a Release build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/packaging_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Package-Plugin.ps1
```

The default `.local/package` destination must not already exist. For another package, specify a distinct explicit `-OutputDirectory`. CI uses `build/package`. Build artifacts are retained for 30 days and CTest logs for 14 days.

The release workflow handles explicitly pushed tags such as `v0.1.0`. Version and release notes must match. It reuses the build workflow and publishes ZIP plus SHA-256 only after validation, including a checksum-pinned, redacted full-history Gitleaks scan. There is no automatic installation. CI validates builds and automated tests; Winamp rendering and real Plex playback remain separate checks.

## Isolated runtime tests

The test instance starts minimized. `scripts/Restart-PlexTest.ps1` checks the exact process path and window class, stops only the isolated copy normally, clears its playlist, copies the Release DLL and verifies minimized startup. Other test restarts use the same behavior. Reveal windows only for required visual or mouse checks; screenshot helpers restore minimization even on capture failure. Context-menu and insertion tests also minimize in their cleanup paths.

`scripts/Prepare-WinampTest.ps1` creates a separate copy only with explicit authorization. Supply the actual source installation; its version and required files are checked, and an existing destination is rejected.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/Prepare-WinampTest.ps1 `
  -WinampDirectory "C:\verified-installation-path\Winamp"
```

The copy resides in `.local/winamp-test`. Its `paths.ini` selects a separate `settings` directory and window class `WinampPlexTest`. It includes host files, audio plugins, `gen_ff.dll`, freeform components, installed skins and language packs. Existing user configuration is not copied. Change skins and languages only in the test copy; do not create file associations.

The SDK test host sets `WINAMP_PLEX_CONFIG_DIRECTORY` only for its own process to an absolute temporary directory. It neither reads the real connection nor sends real Plex requests. Relative overrides are rejected. Normal configuration remains under `%LOCALAPPDATA%/WinampPlexLibrary`.

## Manual installation and removal

1. Locate the actual `winamp.exe` and verify its version.
2. Identify the Plugins directory actually in use, for example through loaded `gen_ml.dll`; do not assume a default path.
3. Exit Winamp normally.
4. Preserve an existing DLL, then manually copy only `ml_plex.dll` next to `gen_ml.dll`.
5. Start Winamp, inspect Preferences → Plug-ins → Media Library, and open Plex.

A local development observation of an installation path is not an installation rule for other systems. Replacing a normal installation requires explicit authorization.

To remove the plugin, exit Winamp and delete only its installed DLL. Its DPAPI file can be removed separately. Do not perform broad cleanup of Plugins, AppData or Plex directories.

## Troubleshooting

- DLL absent: check x86 architecture, `gen_ml.dll`, export name and required Visual C++ runtime.
- Initialization failure: check SDK hashes and the plugin's own DPAPI configuration. Do not borrow another configuration as an access method.
- HTTPS failure: check the advertised Plex connection and certificate hostname. Never disable certificate validation.
- Browser authorization expired: close the dialog and start a new sign-in.
- Saved playback URL fails after restart: grants are session-bound; add the track again.
- HTTP succeeds but no audio plays: verify the format and actual Winamp input decoder. HTTP 200 alone does not establish decoder compatibility.
